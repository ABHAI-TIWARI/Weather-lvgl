#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string>
#include <math.h>
#include "sdkconfig.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "BMP280.h"

//#include "../../lv_examples.h"
//#if LV_USE_BMP && LV_BUILD_EXAMPLES

static const char *TAG = "MAIN";
#define LV_TICK_PERIOD_MS 1
#define LGFX_WT32_SC01 // Wireless Tag / Seeed WT32-SC01
#define LGFX_USE_V1    // LovyanGFX version
#define MY_USB_SYMBOL "\xEF\x8A\x87"
#define TEMP_ICON_SYMBOL "\xEF\x80\xA1"//"\xEE\x8A\x99"
#define HUMID_ICON_SYMBOL "\xEF\x81\x83"  // LV_SYMBOL_TINT - water droplet (perfect for humidity!)
#define PRESSURE_ICON_SYMBOL "\xEF\x80\x93"  // LV_SYMBOL_SETTINGS - gear (represents gauge)
//#define LGFX_AUTODETECT
#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>

static LGFX lcd;

#include <lvgl.h>
#include "../components/lvgl/examples/lv_examples.h"
#include "../components/lvgl/demos/lv_demos.h"

/*** Setup screen resolution for LVGL ***/
static const uint16_t screenWidth = 480;
static const uint16_t screenHeight = 320;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

/*** Function declaration ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void lv_button_demo(void);
void lv_weather_dashboard(void);
void draw_thermometer_icon(lv_obj_t *parent, lv_coord_t x_offset, lv_coord_t y_offset, lv_color_t color);
void draw_pressure_gauge_icon(lv_obj_t *parent, lv_coord_t x_offset, lv_coord_t y_offset, lv_color_t color);
static void lv_tick_task(void *arg);
static void sensor_task(void *arg);
static esp_err_t i2c_master_init(void);

char txt[100];
lv_obj_t *tlabel; // touch x,y label
lv_obj_t *brightness_btn_label; // Brightness button label

// Brightness levels and current index
static uint8_t brightness_levels[] = {25, 128, 242}; // ~10%, 50%, 95%
static uint8_t current_brightness_index = 0;

// Temperature unit: false = Celsius, true = Fahrenheit
static bool temp_unit_fahrenheit = false;

// BMP280 sensor and data
static bmp280_dev_t bmp280_dev;
static float sensor_temperature = 25.5;  // Default value
static float sensor_pressure = 1013.0;   // Default value (hPa)
static float sensor_humidity = 65.0;     // BMP280 doesn't measure humidity, keep dummy value

// Labels for updating sensor values
lv_obj_t *temp_value_label = NULL;
lv_obj_t *humid_value_label = NULL;
lv_obj_t *pressure_value_label = NULL;
lv_obj_t *temp_unit_btn_label = NULL;

// I2C configuration
#define I2C_MASTER_SCL_IO           19      // GPIO 19 (shared with touch)
#define I2C_MASTER_SDA_IO           18      // GPIO 18 (shared with touch)
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000  // 400kHz
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

// I2C mutex for coordinating access between touch and sensor
extern "C" SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t i2c_mutex = NULL;

extern "C"
{
    void app_main(void)
    {
        // Create I2C mutex before initializing anything that uses I2C
        i2c_mutex = xSemaphoreCreateMutex();
        if (i2c_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C mutex!");
            return;
        }
        ESP_LOGI(TAG, "I2C mutex created successfully");

        /* Initialize I2C bus BEFORE LCD init (for touch controller and BMP280) */
        ESP_LOGI(TAG, "Initializing I2C bus...");
        esp_err_t i2c_ret = i2c_master_init();
        if (i2c_ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(i2c_ret));
        } else {
            ESP_LOGI(TAG, "I2C bus initialized successfully");
        }

        lcd.init(); // Initialize LovyanGFX (will use existing I2C)
        lv_init();  // Initialize lvgl

        // Set backlight brightness (0-255, where 255 is maximum brightness)
        lcd.setBrightness(100); // Set to 78% brightness (200/255)

        // Setting display to landscape
        if (lcd.width() < lcd.height())
            lcd.setRotation(lcd.getRotation() ^ 1);

        /* LVGL : Setting up buffer to use for display */
        lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

        /*** LVGL : Setup & Initialize the display device driver ***/
        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.hor_res = screenWidth;
        disp_drv.ver_res = screenHeight;
        disp_drv.flush_cb = display_flush;
        disp_drv.draw_buf = &draw_buf;
        lv_disp_drv_register(&disp_drv);

        /*** LVGL : Setup & Initialize the input device driver ***/
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touchpad_read;
        lv_indev_drv_register(&indev_drv);

        /* Create and start a periodic timer interrupt to call lv_tick_inc */
        const esp_timer_create_args_t periodic_timer_args = {
            .callback = &lv_tick_task,
            .name = "periodic_gui"};
        esp_timer_handle_t periodic_timer;
        ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

        /* I2C bus already initialized before LCD init */

        /*** Create simple label and show LVGL version ***/

        sprintf(txt, "WT32-SC01 with LVGL v%d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());

        // lv_obj_t *label = lv_label_create(lv_scr_act()); // full screen as the parent
        // lv_label_set_text(label, txt);                   // set label text
        // lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);    // Center but 20 from the top

        tlabel = lv_label_create(lv_scr_act());         // full screen as the parent
        lv_label_set_text(tlabel, "Touch:(000,000)");   // set label text
        lv_obj_align(tlabel, LV_ALIGN_TOP_RIGHT, 0, 0); // Center but 20 from the top

        //lv_button_demo(); // lvl buttons
        //lv_example_anim_1();
        //lv_demo_widgets();
        lv_weather_dashboard();

        /* Start BMP280 sensor reading task (with lower priority to not interfere with GUI) */
        xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 3, NULL);
        ESP_LOGI(TAG, "Sensor task created");

        while (1)
        {
            lv_timer_handler(); /* let the GUI do its work */
            vTaskDelay(1);
        }
    }
}

/*** Display callback to flush the buffer to screen ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.pushColors((uint16_t *)&color_p->full, w * h, true);
    lcd.endWrite();

    lv_disp_flush_ready(disp);
}

/*** Touchpad callback to read the touchpad ***/
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    uint16_t touchX, touchY;
    bool touched = false;

    // Take I2C mutex before reading touch (uses I2C)
    if (i2c_mutex != NULL && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        touched = lcd.getTouch(&touchX, &touchY);
        xSemaphoreGive(i2c_mutex);
    }
    else
    {
        // If mutex not available, skip this read cycle
        touched = false;
    }

    if (!touched)
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;

        /*Set the coordinates*/
        data->point.x = touchX;
        data->point.y = touchY;

        sprintf(txt, "Touch:(%03d,%03d)", touchX, touchY);
        lv_label_set_text(tlabel, txt); // set label text
    }
}

/* Counter button event handler */
static void counter_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED)
    {
        static uint8_t cnt = 0;
        cnt++;

        /*Get the first child of the button which is the label and change its text*/
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Button: %d", cnt);
        LV_LOG_USER("Clicked");
        ESP_LOGI(TAG, "Clicked");
    }
}

/* Toggle button event handler */
static void toggle_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        LV_LOG_USER("Toggled");
        ESP_LOGI(TAG, "Toggled");
    }
}

/* Button with counter & Toggle Button */
void lv_button_demo(void)
{
    lv_obj_t *label;

    // Button with counter
    lv_obj_t *btn1 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn1, counter_event_handler, LV_EVENT_ALL, NULL);

    lv_obj_set_pos(btn1, 100, 100); /*Set its position*/
    lv_obj_set_size(btn1, 120, 50); /*Set its size*/

    label = lv_label_create(btn1);
    lv_label_set_text(label, "Button");
    lv_obj_center(label);

    // Toggle button
    lv_obj_t *btn2 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn2, toggle_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_pos(btn2, 250, 100); /*Set its position*/
    lv_obj_set_size(btn2, 120, 50); /*Set its size*/

    label = lv_label_create(btn2);
    lv_label_set_text(label, "Toggle Button");
    lv_obj_center(label);
}

/* Draw custom thermometer icon using canvas */
void draw_thermometer_icon(lv_obj_t *parent, lv_coord_t x_offset, lv_coord_t y_offset, lv_color_t color)
{
    // Create canvas for drawing thermometer (smaller size)
    static lv_color_t cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(40, 40)];
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, cbuf, 40, 40, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, x_offset, y_offset);

    // Fill with transparent background
    lv_canvas_fill_bg(canvas, lv_color_hex(0x81ecec), LV_OPA_0);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);

    // Draw thermometer bulb (circle at bottom) - reduced size
    rect_dsc.bg_color = color;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.border_width = 0;
    lv_canvas_draw_rect(canvas, 12, 24, 16, 16, &rect_dsc);  // Bulb circle (reduced from 22x22 to 16x16)

    // Draw thermometer tube (vertical rectangle) - reduced size
    rect_dsc.radius = 3;
    lv_canvas_draw_rect(canvas, 16, 6, 8, 22, &rect_dsc);  // Tube (reduced width from 10 to 8, height from 28 to 22)

    // Draw inner white tube (to show liquid level)
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_30;
    lv_canvas_draw_rect(canvas, 18, 8, 4, 18, &rect_dsc);  // Inner tube (reduced proportionally)

    // Draw tick marks (optional - for detail)
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_white();
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_50;

    lv_point_t line_points[2];
    // Draw 3 horizontal tick marks
    for (int i = 0; i < 3; i++) {
        line_points[0].x = 14;
        line_points[0].y = 10 + (i * 5);
        line_points[1].x = 18;
        line_points[1].y = 10 + (i * 5);
        lv_canvas_draw_line(canvas, line_points, 2, &line_dsc);
    }
}

/* Draw custom pressure gauge icon using canvas */
void draw_pressure_gauge_icon(lv_obj_t *parent, lv_coord_t x_offset, lv_coord_t y_offset, lv_color_t color)
{
    // Create canvas for drawing pressure gauge
    static lv_color_t pbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(50, 50)];
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, pbuf, 50, 50, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, x_offset, y_offset);

    // Fill with transparent background
    lv_canvas_fill_bg(canvas, lv_color_hex(0xD1C4E9), LV_OPA_0);

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);

    // Draw outer gauge arc (background track)
    arc_dsc.color = lv_color_white();
    arc_dsc.width = 6;
    arc_dsc.opa = LV_OPA_40;
    arc_dsc.rounded = 1;
    lv_canvas_draw_arc(canvas, 25, 30, 18, 135, 45, &arc_dsc);

    // Draw colored gauge arc (indicator)
    arc_dsc.color = color;
    arc_dsc.width = 6;
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.rounded = 1;
    // Draw arc from 135° to 0° (showing ~50% pressure)
    lv_canvas_draw_arc(canvas, 25, 30, 18, 135, 0, &arc_dsc);

    // Draw center circle (gauge center point)
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = color;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.border_width = 0;
    lv_canvas_draw_rect(canvas, 21, 26, 8, 8, &rect_dsc);

    // Draw needle/pointer
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.round_end = 1;

    lv_point_t needle_points[2];
    needle_points[0].x = 25;  // Center
    needle_points[0].y = 30;
    needle_points[1].x = 32;  // Needle tip pointing to ~45° (mid-high pressure)
    needle_points[1].y = 20;
    lv_canvas_draw_line(canvas, needle_points, 2, &line_dsc);

    // Draw tick marks around the gauge
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = lv_color_white();
    tick_dsc.width = 1;
    tick_dsc.opa = LV_OPA_60;

    // Draw 5 tick marks
    lv_point_t tick_points[2];
    for (int i = 0; i < 5; i++) {
        // Calculate angle for each tick (from 135° to 45°, total 270° span)
        int angle = 135 - (i * 45);  // 135°, 90°, 45°, 0°, -45°
        float rad = (angle * 3.14159f) / 180.0f;

        // Outer point
        tick_points[0].x = 25 + (int)(15 * cosf(rad));
        tick_points[0].y = 30 - (int)(15 * sinf(rad));

        // Inner point
        tick_points[1].x = 25 + (int)(12 * cosf(rad));
        tick_points[1].y = 30 - (int)(12 * sinf(rad));

        lv_canvas_draw_line(canvas, tick_points, 2, &tick_dsc);
    }
}

/* Temperature unit button event handler */
static void temp_unit_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // Toggle temperature unit
        temp_unit_fahrenheit = !temp_unit_fahrenheit;

        // Update button label
        if (temp_unit_fahrenheit) {
            lv_label_set_text(temp_unit_btn_label, "Temp Mode: F");
        } else {
            lv_label_set_text(temp_unit_btn_label, "Temp Mode: C");
        }

        // Update temperature display with current sensor value
        if (temp_value_label != NULL) {
            char temp_str[16];
            if (temp_unit_fahrenheit) {
                float temp_f = (sensor_temperature * 9.0f / 5.0f) + 32.0f;
                sprintf(temp_str, "%.1f°F", temp_f);
            } else {
                sprintf(temp_str, "%.1f°C", sensor_temperature);
            }
            lv_label_set_text(temp_value_label, temp_str);
        }

        ESP_LOGI(TAG, "Temperature unit changed to %s", temp_unit_fahrenheit ? "Fahrenheit" : "Celsius");
    }
}

/* Brightness button event handler */
static void brightness_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        // Cycle to next brightness level
        current_brightness_index = (current_brightness_index + 1) % 3;
        uint8_t new_brightness = brightness_levels[current_brightness_index];

        // Set LCD brightness
        lcd.setBrightness(new_brightness);

        // Calculate percentage
        uint8_t percentage = (new_brightness * 100) / 255;

        // Update button label
        char btn_text[32];
        sprintf(btn_text, "Brightness: %d%%", percentage);
        lv_label_set_text(brightness_btn_label, btn_text);

        ESP_LOGI(TAG, "Brightness changed to %d%% (%d/255)", percentage, new_brightness);
    }
}

/* Weather Dashboard with Temperature, Humidity and Air Pressure */
void lv_weather_dashboard(void)
{
    // Create a background container with pastel blue color
    lv_obj_t *bg_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg_container, 480, 320);
    lv_obj_center(bg_container);
    lv_obj_set_style_bg_color(bg_container, lv_color_hex(0xE3F2FD), 0); // Pastel blue background
    lv_obj_set_style_border_width(bg_container, 0, 0);
    lv_obj_set_style_pad_all(bg_container, 20, 0);

    // Title label
    lv_obj_t *title = lv_label_create(bg_container);
    lv_label_set_text(title, "Weather Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x455A64), 0); // Dark gray
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Create three cards/panels for weather data
    static lv_coord_t col_dsc[] = {140, 140, 140, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {180, LV_GRID_TEMPLATE_LAST};

    lv_obj_t *grid = lv_obj_create(bg_container);
    lv_obj_set_size(grid, 440, 200);
    lv_obj_center(grid);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(grid, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, 0);

    // ===== TEMPERATURE CARD =====
    lv_obj_t *temp_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(temp_card, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(temp_card, 130, 170);
    lv_obj_set_style_bg_color(temp_card, lv_color_hex(0x81ecec), 0); // Pastel red/pink 0xFFCDD2
    lv_obj_set_style_border_color(temp_card, lv_color_hex(0x00b89A), 0); //#0xEF9A9A
    lv_obj_set_style_border_width(temp_card, 2, 0);
    lv_obj_set_style_radius(temp_card, 15, 0);
    lv_obj_set_style_shadow_width(temp_card, 10, 0);
    lv_obj_set_style_shadow_color(temp_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(temp_card, LV_OPA_20, 0);

    // Temperature icon (custom drawn thermometer)
    draw_thermometer_icon(temp_card, 0, 5, lv_color_hex(0xD32F2F));

    // Temperature label
    lv_obj_t *temp_label = lv_label_create(temp_card);
    lv_label_set_text(temp_label, "Temperature");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0x424242), 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_MID, 0, 60);

    // Temperature value
    temp_value_label = lv_label_create(temp_card);
    lv_label_set_text(temp_value_label, "25.5°C");
    lv_obj_set_style_text_font(temp_value_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(temp_value_label, lv_color_hex(0x00796B), 0); // Dark teal (matches theme)
    lv_obj_align(temp_value_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // ===== HUMIDITY CARD =====
    lv_obj_t *humid_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(humid_card, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(humid_card, 130, 170);
    lv_obj_set_style_bg_color(humid_card, lv_color_hex(0xB2DFDB), 0); // Pastel teal
    lv_obj_set_style_border_color(humid_card, lv_color_hex(0x80CBC4), 0);
    lv_obj_set_style_border_width(humid_card, 2, 0);
    lv_obj_set_style_radius(humid_card, 15, 0);
    lv_obj_set_style_shadow_width(humid_card, 10, 0);
    lv_obj_set_style_shadow_color(humid_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(humid_card, LV_OPA_20, 0);

    // Humidity icon
    lv_obj_t *humid_icon = lv_label_create(humid_card);
    lv_label_set_text(humid_icon, HUMID_ICON_SYMBOL); // Water droplet icon
    lv_obj_set_style_text_font(humid_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(humid_icon, lv_color_hex(0x00796B), 0); // Dark teal
    lv_obj_align(humid_icon, LV_ALIGN_TOP_MID, 0, 15);

    // Humidity label
    lv_obj_t *humid_label = lv_label_create(humid_card);
    lv_label_set_text(humid_label, "Humidity");
    lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(humid_label, lv_color_hex(0x424242), 0);
    lv_obj_align(humid_label, LV_ALIGN_TOP_MID, 0, 60);

    // Humidity value
    humid_value_label = lv_label_create(humid_card);
    lv_label_set_text(humid_value_label, "65%");
    lv_obj_set_style_text_font(humid_value_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(humid_value_label, lv_color_hex(0x00695C), 0); // Darker teal
    lv_obj_align(humid_value_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // ===== AIR PRESSURE CARD =====
    lv_obj_t *pressure_card = lv_obj_create(grid);
    lv_obj_set_grid_cell(pressure_card, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(pressure_card, 130, 170);
    lv_obj_set_style_bg_color(pressure_card, lv_color_hex(0xD1C4E9), 0); // Pastel purple
    lv_obj_set_style_border_color(pressure_card, lv_color_hex(0xB39DDB), 0);
    lv_obj_set_style_border_width(pressure_card, 2, 0);
    lv_obj_set_style_radius(pressure_card, 15, 0);
    lv_obj_set_style_shadow_width(pressure_card, 10, 0);
    lv_obj_set_style_shadow_color(pressure_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(pressure_card, LV_OPA_20, 0);

    // Pressure icon (custom drawn gauge)
    draw_pressure_gauge_icon(pressure_card, 0, 5, lv_color_hex(0x5E35B1));

    // Pressure label
    lv_obj_t *pressure_label = lv_label_create(pressure_card);
    lv_label_set_text(pressure_label, "Pressure");
    lv_obj_set_style_text_font(pressure_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pressure_label, lv_color_hex(0x424242), 0);
    lv_obj_align(pressure_label, LV_ALIGN_TOP_MID, 0, 60);

    // Pressure value
    pressure_value_label = lv_label_create(pressure_card);
    lv_label_set_text(pressure_value_label, "1013 hPa");
    lv_obj_set_style_text_font(pressure_value_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(pressure_value_label, lv_color_hex(0x4527A0), 0); // Darker purple
    lv_obj_align(pressure_value_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // ===== CONTROL BUTTONS AT BOTTOM (parallel layout) =====

    // Temperature unit toggle button (left side)
    lv_obj_t *temp_unit_btn = lv_btn_create(bg_container);
    lv_obj_set_size(temp_unit_btn, 140, 45);
    lv_obj_set_pos(temp_unit_btn, 90, 235); // Left side, bottom
    lv_obj_set_style_bg_color(temp_unit_btn, lv_color_hex(0xFFAB91), 0); // Pastel orange
    lv_obj_set_style_bg_color(temp_unit_btn, lv_color_hex(0xFF8A65), LV_STATE_PRESSED);
    lv_obj_set_style_radius(temp_unit_btn, 10, 0);
    lv_obj_set_style_shadow_width(temp_unit_btn, 8, 0);
    lv_obj_set_style_shadow_color(temp_unit_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(temp_unit_btn, LV_OPA_20, 0);
    lv_obj_add_event_cb(temp_unit_btn, temp_unit_btn_event_handler, LV_EVENT_CLICKED, NULL);

    // Temperature button label
    temp_unit_btn_label = lv_label_create(temp_unit_btn);
    lv_label_set_text(temp_unit_btn_label, "Temp Mode: C");
    lv_obj_set_style_text_color(temp_unit_btn_label, lv_color_hex(0x424242), 0);
    lv_obj_set_style_text_font(temp_unit_btn_label, &lv_font_montserrat_12, 0);
    lv_obj_center(temp_unit_btn_label);

    // Brightness control button (right side)
    lv_obj_t *brightness_btn = lv_btn_create(bg_container);
    lv_obj_set_size(brightness_btn, 140, 45);
    lv_obj_set_pos(brightness_btn, 250, 235); // Right side, bottom (parallel to temp button)
    lv_obj_set_style_bg_color(brightness_btn, lv_color_hex(0xFFD54F), 0); // Pastel yellow/gold
    lv_obj_set_style_bg_color(brightness_btn, lv_color_hex(0xFFB300), LV_STATE_PRESSED);
    lv_obj_set_style_radius(brightness_btn, 10, 0);
    lv_obj_set_style_shadow_width(brightness_btn, 8, 0);
    lv_obj_set_style_shadow_color(brightness_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(brightness_btn, LV_OPA_20, 0);
    lv_obj_add_event_cb(brightness_btn, brightness_btn_event_handler, LV_EVENT_CLICKED, NULL);

    // Brightness button label with current brightness
    brightness_btn_label = lv_label_create(brightness_btn);
    uint8_t current_percentage = (brightness_levels[current_brightness_index] * 100) / 255;
    char btn_text[32];
    sprintf(btn_text, "Brightness: %d%%", current_percentage);
    lv_label_set_text(brightness_btn_label, btn_text);
    lv_obj_set_style_text_color(brightness_btn_label, lv_color_hex(0x424242), 0);
    lv_obj_set_style_text_font(brightness_btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(brightness_btn_label);
}

/* Setting up tick task for lvgl */
static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

/* Initialize I2C master */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = 0;

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C master initialized successfully");
    return ESP_OK;
}

/* Sensor reading task */
static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");

    // Wait for I2C bus and LCD to be fully initialized
    vTaskDelay(pdMS_TO_TICKS(500));

    // Try to initialize BMP280 sensor with retries
    esp_err_t ret = ESP_FAIL;
    for (int retry = 0; retry < 3 && ret != ESP_OK; retry++) {
        if (retry > 0) {
            ESP_LOGI(TAG, "Retry %d: Attempting BMP280 initialization...", retry);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        ret = bmp280_init(&bmp280_dev, I2C_MASTER_NUM, BMP280_I2C_ADDR_PRIM, i2c_mutex);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "BMP280 init failed on primary address (0x76), trying secondary (0x77)...");
            vTaskDelay(pdMS_TO_TICKS(100));
            ret = bmp280_init(&bmp280_dev, I2C_MASTER_NUM, BMP280_I2C_ADDR_SEC, i2c_mutex);
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 initialization failed after 3 retries on both addresses");
        ESP_LOGE(TAG, "Please check:");
        ESP_LOGE(TAG, "  1. BMP280 sensor is connected to I2C pins (SDA=GPIO18, SCL=GPIO19)");
        ESP_LOGE(TAG, "  2. Sensor I2C address (0x76 or 0x77 via SDO pin)");
        ESP_LOGE(TAG, "  3. Power supply to sensor (3.3V)");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "BMP280 sensor initialized successfully at address 0x%02X", bmp280_dev.i2c_addr);

    while (1)
    {
        // Read temperature and pressure from BMP280
        ret = bmp280_read_data(&bmp280_dev, &sensor_temperature, &sensor_pressure);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %.2f°C, Pressure: %.2f hPa", sensor_temperature, sensor_pressure);

            // Update LVGL labels (thread-safe as LVGL runs in same context)
            if (temp_value_label != NULL) {
                char temp_str[16];
                if (temp_unit_fahrenheit) {
                    float temp_f = (sensor_temperature * 9.0f / 5.0f) + 32.0f;
                    sprintf(temp_str, "%.1f°F", temp_f);
                } else {
                    sprintf(temp_str, "%.1f°C", sensor_temperature);
                }
                lv_label_set_text(temp_value_label, temp_str);
            }

            if (pressure_value_label != NULL) {
                char pressure_str[16];
                sprintf(pressure_str, "%.0f hPa", sensor_pressure);
                lv_label_set_text(pressure_value_label, pressure_str);
            }
        } else {
            ESP_LOGE(TAG, "Failed to read BMP280 sensor data");
        }

        // Wait 2 seconds before next reading
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
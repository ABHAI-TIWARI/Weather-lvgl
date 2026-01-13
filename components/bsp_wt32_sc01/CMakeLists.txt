idf_component_register(
    SRCS "bsp_wt32_sc01.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd driver lvgl esp_timer
)

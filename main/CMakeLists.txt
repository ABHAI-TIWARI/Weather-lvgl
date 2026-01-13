idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl esp_lcd driver
                    REQUIRES bsp_wt32_sc01 lvgl
                    REQUIRES LovyanGFX lvgl)

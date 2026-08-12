idf_component_register(
    SRCS
        "src/esp_bsp_sdl_common.c"
        "src/esp_bsp_sdl_m5stack_tab5.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_ppa esp_mm
    PRIV_REQUIRES
        espressif__esp_lcd_touch
        espressif__esp_lcd_st7121
        espressif__m5stack_tab5_noglib
)

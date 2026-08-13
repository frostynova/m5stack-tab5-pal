#include "esp_bsp_sdl.h"

esp_err_t tab5_sdl_init(esp_bsp_sdl_display_config_t *config,
                        esp_lcd_panel_handle_t *panel_handle,
                        esp_lcd_panel_io_handle_t *panel_io_handle);
esp_err_t tab5_sdl_backlight_on(void);
esp_err_t tab5_sdl_backlight_off(void);
esp_err_t tab5_sdl_display_on_off(bool enable);
esp_err_t tab5_sdl_touch_init(void);
esp_err_t tab5_sdl_touch_read(esp_bsp_sdl_touch_info_t *touch_info);
void tab5_sdl_set_pal_margin_draw_cb(esp_bsp_sdl_pal_margin_draw_cb_t callback,
                                     void *user_data);
esp_err_t tab5_sdl_present_pal_frame(const void *argb8888_pixels);
esp_err_t tab5_sdl_deinit(void);

esp_err_t esp_bsp_sdl_init(esp_bsp_sdl_display_config_t *config,
                           esp_lcd_panel_handle_t *panel_handle,
                           esp_lcd_panel_io_handle_t *panel_io_handle)
{
    return tab5_sdl_init(config, panel_handle, panel_io_handle);
}

esp_err_t esp_bsp_sdl_backlight_on(void)
{
    return tab5_sdl_backlight_on();
}

esp_err_t esp_bsp_sdl_backlight_off(void)
{
    return tab5_sdl_backlight_off();
}

esp_err_t esp_bsp_sdl_display_on_off(bool enable)
{
    return tab5_sdl_display_on_off(enable);
}

esp_err_t esp_bsp_sdl_touch_init(void)
{
    return tab5_sdl_touch_init();
}

esp_err_t esp_bsp_sdl_touch_read(esp_bsp_sdl_touch_info_t *touch_info)
{
    return tab5_sdl_touch_read(touch_info);
}

void esp_bsp_sdl_set_pal_margin_draw_cb(esp_bsp_sdl_pal_margin_draw_cb_t callback,
                                        void *user_data)
{
    tab5_sdl_set_pal_margin_draw_cb(callback, user_data);
}

esp_err_t esp_bsp_sdl_present_pal_frame(const void *argb8888_pixels)
{
    return tab5_sdl_present_pal_frame(argb8888_pixels);
}

const char *esp_bsp_sdl_get_board_name(void)
{
    return "M5Stack Tab5";
}

esp_err_t esp_bsp_sdl_deinit(void)
{
    return tab5_sdl_deinit();
}

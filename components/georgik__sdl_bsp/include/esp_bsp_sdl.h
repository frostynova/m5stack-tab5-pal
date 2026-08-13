#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    int pixel_format;
    size_t max_transfer_sz;
    bool has_touch;
} esp_bsp_sdl_display_config_t;

typedef struct {
    bool pressed;
    int x;
    int y;
} esp_bsp_sdl_touch_info_t;

typedef bool (*esp_bsp_sdl_pal_margin_draw_cb_t)(uint16_t *framebuffer,
                                                  int panel_width,
                                                  int panel_height,
                                                  void *user_data);

esp_err_t esp_bsp_sdl_init(esp_bsp_sdl_display_config_t *config,
                           esp_lcd_panel_handle_t *panel_handle,
                           esp_lcd_panel_io_handle_t *panel_io_handle);
esp_err_t esp_bsp_sdl_backlight_on(void);
esp_err_t esp_bsp_sdl_backlight_off(void);
esp_err_t esp_bsp_sdl_display_on_off(bool enable);
esp_err_t esp_bsp_sdl_touch_init(void);
esp_err_t esp_bsp_sdl_touch_read(esp_bsp_sdl_touch_info_t *touch_info);
void esp_bsp_sdl_set_pal_margin_draw_cb(esp_bsp_sdl_pal_margin_draw_cb_t callback,
                                        void *user_data);
esp_err_t esp_bsp_sdl_present_pal_frame(const void *argb8888_pixels);
const char *esp_bsp_sdl_get_board_name(void);
esp_err_t esp_bsp_sdl_deinit(void);

#ifdef __cplusplus
}
#endif

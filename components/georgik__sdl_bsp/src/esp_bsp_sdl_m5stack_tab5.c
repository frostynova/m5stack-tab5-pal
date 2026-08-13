#include "esp_bsp_sdl.h"

#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "bsp/touch.h"
#include "esp_lcd_st7121.h"
#include "esp_check.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SDL_PIXELFORMAT_RGB565 0x15151002u
#define IO_EXPANDER_INIT_ATTEMPTS 5
#define IO_EXPANDER_RETRY_DELAY_MS 20
#define PAL_INPUT_WIDTH 320
#define PAL_INPUT_HEIGHT 200
#define PAL_OUTPUT_X 4
#define PAL_OUTPUT_Y 70
#define PAL_OUTPUT_WIDTH 712
#define PAL_OUTPUT_HEIGHT 1140
#define PAL_SCALE 3.5625f

static const char *TAG = "sdl_bsp_tab5";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static esp_lcd_dsi_bus_handle_t dsi_bus;
static esp_ldo_channel_handle_t dsi_phy_power;
static ppa_client_handle_t pal_ppa;
static uint16_t *pal_output;
static SemaphoreHandle_t pal_lcd_done;
static bool pal_present_ready;
static esp_bsp_sdl_pal_margin_draw_cb_t pal_margin_draw_cb;
static void *pal_margin_draw_user_data;
#if CONFIG_SDL_BSP_TOUCH_ENABLE
static esp_lcd_touch_handle_t touch;
#endif

static esp_err_t tab5_prepare_io_expander(void)
{
    for (int attempt = 1; attempt <= IO_EXPANDER_INIT_ATTEMPTS; ++attempt) {
        if (bsp_io_expander_init() != NULL) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "IO expander initialized on attempt %d", attempt);
            }
            return ESP_OK;
        }

        ESP_LOGW(TAG, "IO expander attempt %d/%d failed",
                 attempt, IO_EXPANDER_INIT_ATTEMPTS);
        if (attempt < IO_EXPANDER_INIT_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(IO_EXPANDER_RETRY_DELAY_MS * attempt));
        }
    }

    return ESP_FAIL;
}

static bool tab5_pal_lcd_done(esp_lcd_panel_handle_t panel_handle,
                              esp_lcd_dpi_panel_event_data_t *event_data,
                              void *user_context)
{
    (void)panel_handle;
    (void)event_data;
    (void)user_context;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(pal_lcd_done, &task_woken);
    return task_woken == pdTRUE;
}

static esp_err_t tab5_prepare_pal_presenter(void)
{
    if (pal_present_ready) {
        return ESP_OK;
    }
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pal_lcd_done = xSemaphoreCreateBinary();
    if (pal_lcd_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const ppa_client_config_t client_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    esp_err_t error = ppa_register_client(&client_config, &pal_ppa);
    if (error != ESP_OK) {
        vSemaphoreDelete(pal_lcd_done);
        pal_lcd_done = NULL;
        return error;
    }

    const size_t output_size =
        (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(*pal_output);
    pal_output = heap_caps_aligned_calloc(
        64, 1, output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (pal_output == NULL) {
        ppa_unregister_client(pal_ppa);
        pal_ppa = NULL;
        vSemaphoreDelete(pal_lcd_done);
        pal_lcd_done = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* The PPA only writes the centered game rectangle.  Flush the calloc'ed
     * black background once so the LCD DMA sees deterministic pixels in the
     * untouched letterbox margins instead of stale PSRAM contents. */
    error = esp_cache_msync(pal_output, output_size,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (error != ESP_OK) {
        heap_caps_free(pal_output);
        pal_output = NULL;
        ppa_unregister_client(pal_ppa);
        pal_ppa = NULL;
        vSemaphoreDelete(pal_lcd_done);
        pal_lcd_done = NULL;
        return error;
    }

    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = tab5_pal_lcd_done,
    };
    error = esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, NULL);
    if (error != ESP_OK) {
        heap_caps_free(pal_output);
        pal_output = NULL;
        ppa_unregister_client(pal_ppa);
        pal_ppa = NULL;
        vSemaphoreDelete(pal_lcd_done);
        pal_lcd_done = NULL;
        return error;
    }

    pal_present_ready = true;
    ESP_LOGI(TAG,
             "PAL PPA presentation enabled: ARGB8888 %dx%d -> RGB565 %dx%d",
             PAL_INPUT_WIDTH, PAL_INPUT_HEIGHT,
             PAL_OUTPUT_WIDTH, PAL_OUTPUT_HEIGHT);
    return ESP_OK;
}

esp_err_t tab5_sdl_init(esp_bsp_sdl_display_config_t *config,
                        esp_lcd_panel_handle_t *panel_handle,
                        esp_lcd_panel_io_handle_t *panel_io_handle)
{
    if (config == NULL || panel_handle == NULL || panel_io_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* The touch controller at 0x55 reports firmware major 1 on this unit.
     * M5Stack's current UserDemo maps that revision to an ST7121 display;
     * the released Tab5 BSP only distinguishes GT911 vs 0x55 and therefore
     * incorrectly initializes every 0x55 board as ST7123. */
    ESP_LOGI(TAG, "initializing M5Stack Tab5 ST7121 display path");

    /* M5Stack's reference implementation pulses the shared LCD reset/power
     * expander output before starting DSI. */
    ESP_RETURN_ON_ERROR(tab5_prepare_io_expander(), TAG, "IO expander init failed");
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, false), TAG, "LCD reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, true), TAG, "LCD reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "backlight init failed");

    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &dsi_phy_power), TAG, "DSI PHY power failed");

    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 965,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus), TAG, "DSI bus init failed");

    const esp_lcd_dbi_io_config_t dbi_config = ST7121_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &panel_io), TAG, "DBI IO init failed");

    const esp_lcd_dpi_panel_config_t dpi_config = ST7121_1280_720_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    const st7121_vendor_config_t vendor_config = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7121(panel_io, &panel_config, &panel), TAG, "ST7121 panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "ST7121 reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "ST7121 init failed");

    config->width = BSP_LCD_H_RES;
    config->height = BSP_LCD_V_RES;
    config->pixel_format = SDL_PIXELFORMAT_RGB565;
    config->max_transfer_sz = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t);
#if CONFIG_SDL_BSP_TOUCH_ENABLE
    config->has_touch = BSP_CAPS_TOUCH == 1;
#else
    config->has_touch = false;
#endif

    *panel_handle = panel;
    *panel_io_handle = panel_io;

    ESP_LOGI(TAG, "native display mode: %dx%d RGB565 (ST7121)", config->width, config->height);
    return ESP_OK;
}

esp_err_t tab5_sdl_backlight_on(void)
{
    return bsp_display_backlight_on();
}

esp_err_t tab5_sdl_backlight_off(void)
{
    return bsp_display_backlight_off();
}

esp_err_t tab5_sdl_display_on_off(bool enable)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_disp_on_off(panel, enable);
}

esp_err_t tab5_sdl_touch_init(void)
{
#if CONFIG_SDL_BSP_TOUCH_ENABLE
    if (touch != NULL) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "initializing touch through auto-detected Tab5 BSP");
    return bsp_touch_new(NULL, &touch);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t tab5_sdl_touch_read(esp_bsp_sdl_touch_info_t *touch_info)
{
    if (touch_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    touch_info->pressed = false;
    touch_info->x = 0;
    touch_info->y = 0;

#if CONFIG_SDL_BSP_TOUCH_ENABLE
    if (touch == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = esp_lcd_touch_read_data(touch);
    if (error != ESP_OK) {
        return error;
    }

    esp_lcd_touch_point_data_t point = {0};
    uint8_t points = 0;
    error = esp_lcd_touch_get_data(touch, &point, &points, 1);
    if (error != ESP_OK) {
        return error;
    }
    if (points > 0) {
        touch_info->pressed = true;
        touch_info->x = point.x;
        touch_info->y = point.y;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void tab5_sdl_set_pal_margin_draw_cb(esp_bsp_sdl_pal_margin_draw_cb_t callback,
                                     void *user_data)
{
    pal_margin_draw_cb = callback;
    pal_margin_draw_user_data = user_data;
}

esp_err_t tab5_sdl_present_pal_frame(const void *argb8888_pixels)
{
    if (argb8888_pixels == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = tab5_prepare_pal_presenter();
    if (error != ESP_OK) {
        return error;
    }

    const ppa_srm_oper_config_t operation = {
        .in.buffer = argb8888_pixels,
        .in.pic_w = PAL_INPUT_WIDTH,
        .in.pic_h = PAL_INPUT_HEIGHT,
        .in.block_w = PAL_INPUT_WIDTH,
        .in.block_h = PAL_INPUT_HEIGHT,
        .in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888,
        .out.buffer = pal_output,
        .out.buffer_size =
            (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(*pal_output),
        .out.pic_w = BSP_LCD_H_RES,
        .out.pic_h = BSP_LCD_V_RES,
        .out.block_offset_x = PAL_OUTPUT_X,
        .out.block_offset_y = PAL_OUTPUT_Y,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = PAL_SCALE,
        .scale_y = PAL_SCALE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    error = ppa_do_scale_rotate_mirror(pal_ppa, &operation);
    if (error != ESP_OK) {
        return error;
    }

    if (pal_margin_draw_cb != NULL) {
        pal_margin_draw_cb(pal_output, BSP_LCD_H_RES, BSP_LCD_V_RES,
                           pal_margin_draw_user_data);

        const size_t margin_size =
            (size_t)PAL_OUTPUT_Y * BSP_LCD_H_RES * sizeof(*pal_output);
        error = esp_cache_msync(pal_output, margin_size,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (error != ESP_OK) {
            return error;
        }
        error = esp_cache_msync(
            pal_output + (size_t)(PAL_OUTPUT_Y + PAL_OUTPUT_HEIGHT) * BSP_LCD_H_RES,
            margin_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (error != ESP_OK) {
            return error;
        }
    }

    error = esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                      BSP_LCD_H_RES, BSP_LCD_V_RES,
                                      pal_output);
    if (error != ESP_OK) {
        return error;
    }
    return xSemaphoreTake(pal_lcd_done, pdMS_TO_TICKS(1000)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t tab5_sdl_deinit(void)
{
#if CONFIG_SDL_BSP_TOUCH_ENABLE
    if (touch != NULL) {
        esp_lcd_touch_del(touch);
        touch = NULL;
    }
#endif
    if (pal_ppa != NULL) {
        ppa_unregister_client(pal_ppa);
        pal_ppa = NULL;
    }
    if (pal_output != NULL) {
        heap_caps_free(pal_output);
        pal_output = NULL;
    }
    if (pal_lcd_done != NULL) {
        vSemaphoreDelete(pal_lcd_done);
        pal_lcd_done = NULL;
    }
    pal_present_ready = false;
    if (panel != NULL) {
        bsp_display_delete();
        panel = NULL;
        panel_io = NULL;
    }
    return ESP_OK;
}

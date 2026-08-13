#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>

#include "SDL3/SDL.h"
#include "SDL3/SDL_esp-idf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "resource_check.h"

#define SCREEN_WIDTH  720
#define SCREEN_HEIGHT 1280
#define SDL_THREAD_STACK_SIZE (1024 * 1024)

static const char *TAG = "sdlpal_tab5";
static TaskHandle_t sdl_task_handle;

extern int sdlpal_main(int argc, char *argv[]);

static float touch_coordinate(float value, int extent)
{
    /* The current ESP-IDF SDL touch backend forwards BSP pixel coordinates,
     * while upstream SDL normally exposes normalized coordinates. Accept both
     * so this diagnostic remains useful with a future backend fix. */
    if (value >= 0.0f && value <= 1.0f) {
        return value * (float)extent;
    }
    return value;
}

static void draw_status_text(SDL_Renderer *renderer, const pal_resource_check_result_t *resources)
{
    SDL_SetRenderDrawColor(renderer, 245, 247, 250, 255);
    SDL_SetRenderScale(renderer, 3.0f, 3.0f);

    if (resources->state == PAL_RESOURCE_SD_ERROR) {
        SDL_RenderDebugText(renderer, 12.0f, 18.0f, "SD CARD ERROR");
        SDL_RenderDebugTextFormat(renderer, 12.0f, 30.0f, "%s",
                                  esp_err_to_name(resources->mount_result));
    } else if (resources->state == PAL_RESOURCE_READY) {
        SDL_RenderDebugText(renderer, 12.0f, 18.0f, "PAL FILES READY");
        SDL_RenderDebugText(renderer, 12.0f, 30.0f, "WIN95 CANDIDATE");
        SDL_RenderDebugText(renderer, 12.0f, 42.0f,
                            resources->write_probe_ok ? "SD WRITE OK" : "SD WRITE FAILED");
    } else {
        SDL_RenderDebugText(renderer, 12.0f, 18.0f, "PAL FILES MISSING");
        SDL_RenderDebugTextFormat(renderer, 12.0f, 30.0f, "%u / %u FOUND",
                                  (unsigned)resources->present_count,
                                  (unsigned)resources->required_count);
    }

    SDL_SetRenderScale(renderer, 2.0f, 2.0f);
    SDL_RenderDebugTextFormat(renderer, 18.0f, 62.0f, "PATH: %s", resources->root);

    float y = 82.0f;
    for (size_t i = 0; i < resources->required_count; ++i) {
        SDL_SetRenderDrawColor(renderer,
                               resources->present[i] ? 180 : 255,
                               resources->present[i] ? 255 : 210,
                               resources->present[i] ? 200 : 210,
                               255);
        SDL_RenderDebugTextFormat(renderer, 18.0f, y, "%s %s",
                                  resources->present[i] ? "OK " : "-- ",
                                  pal_resource_required_name(i));
        y += 11.0f;
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static bool draw_resource_status(SDL_Renderer *renderer,
                                 const pal_resource_check_result_t *resources,
                                 float touch_x, float touch_y, bool touching)
{
    switch (resources->state) {
    case PAL_RESOURCE_READY:
        SDL_SetRenderDrawColor(renderer, 12, 86, 58, 255);
        break;
    case PAL_RESOURCE_INCOMPLETE:
        SDL_SetRenderDrawColor(renderer, 121, 75, 12, 255);
        break;
    case PAL_RESOURCE_SD_ERROR:
    default:
        SDL_SetRenderDrawColor(renderer, 118, 28, 36, 255);
        break;
    }
    SDL_RenderClear(renderer);

    draw_status_text(renderer, resources);

    static const SDL_Color palette[] = {
        {38, 70, 83, 255},
        {42, 157, 143, 255},
        {233, 196, 106, 255},
        {244, 162, 97, 255},
        {231, 111, 81, 255},
    };

    const float bar_height = 36.0f;
    for (size_t i = 0; i < SDL_arraysize(palette); ++i) {
        SDL_FRect bar = {
            .x = 40.0f,
            .y = 1040.0f + (float)i * (bar_height + 10.0f),
            .w = SCREEN_WIDTH - 80.0f,
            .h = bar_height,
        };
        SDL_SetRenderDrawColor(renderer, palette[i].r, palette[i].g, palette[i].b, palette[i].a);
        SDL_RenderFillRect(renderer, &bar);
    }

    if (touching) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect marker = {
            .x = touch_x - 18.0f,
            .y = touch_y - 18.0f,
            .w = 36.0f,
            .h = 36.0f,
        };
        SDL_RenderFillRect(renderer, &marker);
    }

    return SDL_RenderPresent(renderer);
}

static void *sdl_task(void *unused)
{
    (void)unused;

    sdl_task_handle = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "free internal RAM: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "free PSRAM: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Native-resolution smoke test: no PPA scaling yet. SDLPal's 320x200
     * presentation policy will be added together with the engine. */
    set_scale_factor(1, 1.0f);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ESP_LOGE(TAG, "SDL_Init failed: %s", SDL_GetError());
        return NULL;
    }

    SDL_Window *window = SDL_CreateWindow("SDLPal Tab5 bring-up", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (window == NULL) {
        ESP_LOGE(TAG, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        ESP_LOGE(TAG, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }

    ESP_LOGI(TAG, "SDL display ready at %dx%d using renderer %s",
             SCREEN_WIDTH, SCREEN_HEIGHT, SDL_GetRendererName(renderer));

    const pal_resource_check_result_t resources = pal_resource_check();

    if (resources.state == PAL_RESOURCE_READY) {
        if (!draw_resource_status(renderer, &resources, 0.0f, 0.0f, false)) {
            ESP_LOGE(TAG, "resource-ready frame failed: %s", SDL_GetError());
        }
        vTaskDelay(pdMS_TO_TICKS(350));

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        /* The ESP-IDF SDL backend has no complete video-quit callback. Keep
         * the initialized video device alive while replacing this temporary
         * window with SDLPal's window. */

        ESP_LOGI(TAG, "launching SDLPal engine with resources from %s", resources.root);
        char executable[] = "sdlpal-tab5";
        char *arguments[] = {executable, NULL};
        const int exit_code = sdlpal_main(1, arguments);
        ESP_LOGE(TAG, "SDLPal engine returned unexpectedly: %d", exit_code);
        return NULL;
    }

    bool touching = false;
    float touch_x = 0.0f;
    float touch_y = 0.0f;

    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
                touching = true;
                touch_x = touch_coordinate(event.tfinger.x, SCREEN_WIDTH);
                touch_y = touch_coordinate(event.tfinger.y, SCREEN_HEIGHT);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                touching = false;
                break;
            default:
                break;
            }
        }

        if (!draw_resource_status(renderer, &resources, touch_x, touch_y, touching)) {
            ESP_LOGE(TAG, "SDL_RenderPresent failed: %s", SDL_GetError());
            break;
        }
        static bool first_frame_presented = false;
        if (!first_frame_presented) {
            first_frame_presented = true;
            ESP_LOGI(TAG, "first SDL frame presented successfully");
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }

    return NULL;
}

static void stack_monitor_task(void *unused)
{
    (void)unused;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (sdl_task_handle != NULL) {
            const size_t free_stack =
                (size_t)uxTaskGetStackHighWaterMark(sdl_task_handle) * sizeof(StackType_t);
            ESP_LOGI(TAG, "SDL thread minimum free stack: %zu bytes", free_stack);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting SDLPal Tab5 hardware bring-up");

    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    /* Status and ending screens can place two 64 KiB decode buffers on the
     * stack at once. Keep enough headroom for their caller chain and pthread
     * bookkeeping, and place the stack in PSRAM so audio DMA retains internal
     * memory. */
    esp_pthread_cfg_t pthread_cfg = esp_pthread_get_default_config();
    pthread_cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    pthread_cfg.pin_to_core = 0;
    esp_err_t cfg_error = esp_pthread_set_cfg(&pthread_cfg);
    if (cfg_error != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure SDL pthread stack: %s",
                 esp_err_to_name(cfg_error));
        pthread_attr_destroy(&attributes);
        return;
    }
    pthread_attr_setstacksize(&attributes, SDL_THREAD_STACK_SIZE);

    const int error = pthread_create(&thread, &attributes, sdl_task, NULL);
    pthread_attr_destroy(&attributes);

    if (error != 0) {
        ESP_LOGE(TAG, "failed to create SDL thread: %d", error);
        return;
    }

    pthread_detach(thread);

    if (xTaskCreate(stack_monitor_task, "pal_stack_monitor", 3072, NULL,
                    tskIDLE_PRIORITY, NULL) != pdPASS) {
        ESP_LOGW(TAG, "failed to start SDL stack monitor");
    }
}

#include "SDL3/SDL.h"

#include <stdbool.h>
#include <string.h>

#include "esp_bsp_sdl.h"
#include "esp_log.h"

#define TAB5_TOUCH_WIDTH 720.0f
#define TAB5_TOUCH_HEIGHT 1280.0f
#define TAB5_TOUCH_ID 1
#define TAB5_FINGER_ID 1

static const char *TAG = "sdlpal_touch";

/* georgik/sdl 3.3.7~3's ESP-IDF backend calls SDL_SendTouch with a boolean
 * where SDL3 requires an SDL_EventType, and forwards pixel coordinates where
 * SDL3 requires normalized coordinates. Wrap its pump function until the
 * component release contains the corresponding upstream fix. */
void __wrap_ESPIDF_PumpTouchEvent(void)
{
    static bool bridge_logged;
    static bool was_pressed;
    static float last_x;
    static float last_y;
    esp_bsp_sdl_touch_info_t touch = {0};

    if (!bridge_logged) {
        bridge_logged = true;
        ESP_LOGI(TAG, "project SDL3 touch bridge active");
    }

    if (esp_bsp_sdl_touch_read(&touch) != ESP_OK) {
        return;
    }

    float x = (float)touch.x / (TAB5_TOUCH_WIDTH - 1.0f);
    float y = (float)touch.y / (TAB5_TOUCH_HEIGHT - 1.0f);
    x = SDL_clamp(x, 0.0f, 1.0f);
    y = SDL_clamp(y, 0.0f, 1.0f);

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.tfinger.touchID = TAB5_TOUCH_ID;
    event.tfinger.fingerID = TAB5_FINGER_ID;
    event.tfinger.pressure = touch.pressed ? 1.0f : 0.0f;

    if (touch.pressed && !was_pressed) {
        event.type = SDL_EVENT_FINGER_DOWN;
        event.tfinger.x = x;
        event.tfinger.y = y;
        last_x = x;
        last_y = y;
        was_pressed = true;
        SDL_PushEvent(&event);
    } else if (touch.pressed && was_pressed &&
               (x != last_x || y != last_y)) {
        event.type = SDL_EVENT_FINGER_MOTION;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.dx = x - last_x;
        event.tfinger.dy = y - last_y;
        last_x = x;
        last_y = y;
        SDL_PushEvent(&event);
    } else if (!touch.pressed && was_pressed) {
        event.type = SDL_EVENT_FINGER_UP;
        event.tfinger.x = last_x;
        event.tfinger.y = last_y;
        was_pressed = false;
        SDL_PushEvent(&event);
    }
}

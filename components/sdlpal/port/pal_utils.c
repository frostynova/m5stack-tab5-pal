#include "main.h"

#include "esp_log.h"
#include "tab5_touch.h"

static const char *TAG = "sdlpal_engine";

BOOL UTIL_GetScreenSize(DWORD *width, DWORD *height)
{
    if (width != NULL) {
        *width = 720;
    }
    if (height != NULL) {
        *height = 1280;
    }
    return TRUE;
}

BOOL UTIL_IsAbsolutePath(LPCSTR filename)
{
    return filename != NULL && filename[0] == '/';
}

INT UTIL_Platform_Init(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    gConfig.fLaunchSetting = FALSE;
    PAL_Tab5TouchRegister();
    ESP_LOGI(TAG, "platform initialized; resource path is %s", gConfig.pszGamePath);
    return 0;
}

VOID UTIL_Platform_Quit(VOID)
{
}

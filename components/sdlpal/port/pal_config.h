#pragma once

#define PAL_PREFIX             "/sdcard/pal/"
#define PAL_SAVE_PREFIX        "/sdcard/pal/"
#define PAL_CONFIG_PREFIX      "/sdcard/pal/"
#define PAL_SCREENSHOT_PREFIX  "/sdcard/pal/"

#define PAL_HAS_JOYSTICKS       0
#define PAL_HAS_MOUSE           0
#define PAL_HAS_TOUCH           0
#define PAL_HAS_SDLCD           0
#define PAL_HAS_MP3             0
#define PAL_HAS_OGG             0
#define PAL_HAS_OPUS            0
#define PAL_HAS_NATIVEMIDI      0
#define PAL_HAS_CONFIG_PAGE     0
#define PAL_HAS_GLSL            0
#define PAL_HAS_PLATFORM_SPECIFIC_UTILS 1

#define PAL_DEFAULT_WINDOW_WIDTH   720
#define PAL_DEFAULT_WINDOW_HEIGHT  1280
#define PAL_DEFAULT_TEXTURE_WIDTH  320
#define PAL_DEFAULT_TEXTURE_HEIGHT 200

#define PAL_VIDEO_INIT_FLAGS 0
#define PAL_SDL_INIT_FLAGS (SDL_INIT_VIDEO | SDL_INIT_EVENTS)

#define PAL_PLATFORM "M5Stack Tab5"
#define PAL_CREDIT   "SDLPal and M5Stack communities"
#define PAL_PORTYEAR "2026"

#define PAL_FILESYSTEM_IGNORE_CASE 1
#define PAL_SCALE_SCREEN TRUE

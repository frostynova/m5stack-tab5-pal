#include "main.h"
#include "tab5_touch.h"
#include "tab5_power.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "esp_bsp_sdl.h"
#include "esp_heap_caps.h"

#define PAL_WIDTH  320
#define PAL_HEIGHT 200

/* PPA output geometry from esp_idf_present_pal_frame(). */
#define PANEL_WIDTH       720.0f
#define PANEL_HEIGHT      1280.0f
#define PAL_BLOCK_X       4.0f
#define PAL_BLOCK_Y       70.0f
#define PAL_BLOCK_WIDTH   712.0f
#define PAL_BLOCK_HEIGHT  1140.0f

/* The centered PAL image leaves 70 physical pixels on both landscape sides. */
#define PHYSICAL_WIDTH       1280
#define PHYSICAL_HEIGHT      720
#define PHYSICAL_MARGIN      70
#define BATTERY_TOUCH_TOP    220
#define BATTERY_TOUCH_BOTTOM 440
#define VOLUME_UP_TOP        220
#define VOLUME_UP_BOTTOM     340
#define VOLUME_DOWN_TOP      360
#define VOLUME_DOWN_BOTTOM   480

typedef enum
{
   kTab5TouchNone = 0,
   kTab5TouchUp,
   kTab5TouchDown,
   kTab5TouchLeft,
   kTab5TouchRight,
   kTab5TouchA,
   kTab5TouchB,
   kTab5TouchX,
   kTab5TouchY,
   kTab5TouchAuto,
   kTab5TouchBattery,
   kTab5TouchVolumeUp,
   kTab5TouchVolumeDown,
} TAB5TOUCHAREA;

static TAB5TOUCHAREA g_CurrentArea = kTab5TouchNone;
static SDL_FingerID g_CurrentFinger = 0;
static BOOL g_FingerDown = FALSE;
static uint32_t *g_OverlayFrame = NULL;
static uint32_t g_BatteryDetailsUntil = 0;
static BOOL g_MarginsDrawn = FALSE;
static TAB5POWERSTATUS g_LastMarginPower;
static INT g_LastMarginVolume = -1;
static BOOL g_LastMarginVolumeUpPressed = FALSE;
static BOOL g_LastMarginVolumeDownPressed = FALSE;
static BOOL g_LastMarginBatteryDetails = FALSE;

static bool PAL_Tab5DrawMargins(uint16_t *framebuffer, int panel_width,
                                int panel_height, void *user_data);

static VOID
PAL_Tab5TouchToPhysical(
   float panel_normalized_x,
   float panel_normalized_y,
   float *physical_x,
   float *physical_y
)
{
   const float panel_x = panel_normalized_x * (PANEL_WIDTH - 1.0f);
   const float panel_y = panel_normalized_y * (PANEL_HEIGHT - 1.0f);

   *physical_x = (PANEL_HEIGHT - 1.0f) - panel_y;
   *physical_y = panel_x;
}

static TAB5TOUCHAREA
PAL_Tab5TouchHitSystem(
   float panel_normalized_x,
   float panel_normalized_y
)
{
   float x;
   float y;
   PAL_Tab5TouchToPhysical(panel_normalized_x, panel_normalized_y, &x, &y);

   if (x < PHYSICAL_MARGIN && y >= BATTERY_TOUCH_TOP &&
       y <= BATTERY_TOUCH_BOTTOM)
   {
      return kTab5TouchBattery;
   }
   if (x >= PHYSICAL_WIDTH - PHYSICAL_MARGIN)
   {
      if (y >= VOLUME_UP_TOP && y <= VOLUME_UP_BOTTOM)
      {
         return kTab5TouchVolumeUp;
      }
      if (y >= VOLUME_DOWN_TOP && y <= VOLUME_DOWN_BOTTOM)
      {
         return kTab5TouchVolumeDown;
      }
   }
   return kTab5TouchNone;
}

static BOOL
PAL_Tab5TouchToGame(
   float panel_normalized_x,
   float panel_normalized_y,
   float *game_x,
   float *game_y
)
{
   const float panel_x = panel_normalized_x * (PANEL_WIDTH - 1.0f);
   const float panel_y = panel_normalized_y * (PANEL_HEIGHT - 1.0f);

   /* The PAL frame is scaled and rotated 90 degrees into the portrait panel
    * buffer.  Apply the inverse transform so hit regions are expressed in the
    * same upright 320x200 coordinate system that is visible to the player. */
   /* The panel touch controller uses the opposite origin from the PPA output
    * on both axes.  Undo that extra 180-degree rotation here. */
   const float x = (PAL_BLOCK_Y + PAL_BLOCK_HEIGHT - panel_y) * PAL_WIDTH /
                   PAL_BLOCK_HEIGHT;
   const float y = (panel_x - PAL_BLOCK_X) * PAL_HEIGHT / PAL_BLOCK_WIDTH;

   if (x < 0.0f || x >= PAL_WIDTH || y < 0.0f || y >= PAL_HEIGHT)
   {
      return FALSE;
   }

   *game_x = x;
   *game_y = y;
   return TRUE;
}

static BOOL
PAL_Tab5TouchInsideCircle(
   float x,
   float y,
   float center_x,
   float center_y,
   float radius
)
{
   const float dx = x - center_x;
   const float dy = y - center_y;
   return dx * dx + dy * dy <= radius * radius;
}

static TAB5TOUCHAREA
PAL_Tab5TouchHitDPad(
   float x,
   float y
)
{
   static const struct
   {
      TAB5TOUCHAREA area;
      float center_x;
      float center_y;
      float radius_x;
      float radius_y;
   } buttons[] =
   {
      { kTab5TouchUp,    48.0f, 130.0f, 23.0f, 23.0f },
      { kTab5TouchDown,  48.0f, 184.0f, 23.0f, 21.0f },
      { kTab5TouchLeft,  21.0f, 157.0f, 22.0f, 23.0f },
      { kTab5TouchRight, 75.0f, 157.0f, 22.0f, 23.0f },
   };
   TAB5TOUCHAREA closest = kTab5TouchNone;
   float closest_distance = 2.0f;

   for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
   {
      const float dx = (x - buttons[i].center_x) / buttons[i].radius_x;
      const float dy = (y - buttons[i].center_y) / buttons[i].radius_y;
      const float distance = dx * dx + dy * dy;
      if (distance <= 1.0f && distance < closest_distance)
      {
         closest = buttons[i].area;
         closest_distance = distance;
      }
   }

   return closest;
}

static TAB5TOUCHAREA
PAL_Tab5TouchHitTest(
   float x,
   float y
)
{
   if (x >= 270.0f && x <= 318.0f && y >= 96.0f && y <= 114.0f)
   {
      return kTab5TouchAuto;
   }

   if (PAL_Tab5TouchInsideCircle(x, y, 282.0f, 166.0f, 17.0f)) return kTab5TouchA;
   if (PAL_Tab5TouchInsideCircle(x, y, 282.0f, 132.0f, 17.0f)) return kTab5TouchB;
   if (PAL_Tab5TouchInsideCircle(x, y, 248.0f, 166.0f, 17.0f)) return kTab5TouchX;
   if (PAL_Tab5TouchInsideCircle(x, y, 248.0f, 132.0f, 17.0f)) return kTab5TouchY;

   return PAL_Tab5TouchHitDPad(x, y);
}

static VOID
PAL_Tab5TouchReleaseArea(
   volatile PALINPUTSTATE *state,
   TAB5TOUCHAREA area
)
{
   if (area == kTab5TouchUp || area == kTab5TouchDown ||
       area == kTab5TouchLeft || area == kTab5TouchRight)
   {
      state->prevdir = state->dir;
      state->dir = kDirUnknown;
   }
}

static VOID
PAL_Tab5TouchPressArea(
   volatile PALINPUTSTATE *state,
   TAB5TOUCHAREA area
)
{
   switch (area)
   {
   case kTab5TouchUp:
      state->prevdir = state->dir;
      state->dir = kDirNorth;
      state->dwKeyPress |= kKeyUp;
      break;
   case kTab5TouchDown:
      state->prevdir = state->dir;
      state->dir = kDirSouth;
      state->dwKeyPress |= kKeyDown;
      break;
   case kTab5TouchLeft:
      state->prevdir = state->dir;
      state->dir = kDirWest;
      state->dwKeyPress |= kKeyLeft;
      break;
   case kTab5TouchRight:
      state->prevdir = state->dir;
      state->dir = kDirEast;
      state->dwKeyPress |= kKeyRight;
      break;
   case kTab5TouchA:
      state->dwKeyPress |= kKeySearch;
      break;
   case kTab5TouchB:
      state->dwKeyPress |= kKeyMenu;
      break;
   case kTab5TouchX:
      state->dwKeyPress |= (gpGlobals != NULL && gpGlobals->fInBattle) ?
                           kKeyRepeat : kKeyUseItem;
      break;
   case kTab5TouchY:
      state->dwKeyPress |= kKeyForce;
      break;
   case kTab5TouchAuto:
      state->dwKeyPress |= kKeyAuto;
      break;
   case kTab5TouchBattery:
      g_BatteryDetailsUntil = SDL_GetTicks() + 5000;
      break;
   case kTab5TouchVolumeUp:
      AUDIO_Tab5AdjustVolume(10);
      break;
   case kTab5TouchVolumeDown:
      AUDIO_Tab5AdjustVolume(-10);
      break;
   default:
      break;
   }
}

static int
PAL_Tab5TouchEventFilter(
   const SDL_Event *event,
   volatile PALINPUTSTATE *state
)
{
   float game_x;
   float game_y;
   TAB5TOUCHAREA area;

   if (event->type != SDL_EVENT_FINGER_DOWN &&
       event->type != SDL_EVENT_FINGER_MOTION &&
       event->type != SDL_EVENT_FINGER_UP &&
       event->type != SDL_EVENT_FINGER_CANCELED)
   {
      return 0;
   }

   if (event->type == SDL_EVENT_FINGER_UP ||
       event->type == SDL_EVENT_FINGER_CANCELED)
   {
      if (g_FingerDown && event->tfinger.fingerID == g_CurrentFinger)
      {
         PAL_Tab5TouchReleaseArea(state, g_CurrentArea);
         g_CurrentArea = kTab5TouchNone;
         g_FingerDown = FALSE;
      }
      return 1;
   }

   area = PAL_Tab5TouchHitSystem(event->tfinger.x, event->tfinger.y);
   if (area != kTab5TouchNone)
   {
      /* Margin controls use physical panel coordinates and never feed PAL. */
   }
   else if (!PAL_Tab5TouchToGame(event->tfinger.x, event->tfinger.y,
                                 &game_x, &game_y))
   {
      area = kTab5TouchNone;
   }
   else
   {
      area = PAL_Tab5TouchHitTest(game_x, game_y);
   }

   if (event->type == SDL_EVENT_FINGER_DOWN)
   {
      if (!g_FingerDown)
      {
         g_FingerDown = TRUE;
         g_CurrentFinger = event->tfinger.fingerID;
         g_CurrentArea = area;
         PAL_Tab5TouchPressArea(state, area);
      }
   }
   else if (g_FingerDown && event->tfinger.fingerID == g_CurrentFinger &&
            area != g_CurrentArea)
   {
      PAL_Tab5TouchReleaseArea(state, g_CurrentArea);
      g_CurrentArea = area;
      PAL_Tab5TouchPressArea(state, area);
   }

   return 1;
}

VOID
PAL_Tab5TouchRegister(
   VOID
)
{
   PAL_Tab5PowerInit();
   esp_bsp_sdl_set_pal_margin_draw_cb(PAL_Tab5DrawMargins, NULL);
   PAL_RegisterInputFilter(NULL, PAL_Tab5TouchEventFilter, NULL);
}

static uint32_t
PAL_Tab5TouchBlend(
   uint32_t background,
   uint32_t foreground,
   unsigned alpha
)
{
   const unsigned inv = 255 - alpha;
   const unsigned br = (background >> 16) & 0xff;
   const unsigned bg = (background >> 8) & 0xff;
   const unsigned bb = background & 0xff;
   const unsigned fr = (foreground >> 16) & 0xff;
   const unsigned fg = (foreground >> 8) & 0xff;
   const unsigned fb = foreground & 0xff;

   return 0xff000000u |
          (((br * inv + fr * alpha) / 255) << 16) |
          (((bg * inv + fg * alpha) / 255) << 8) |
          ((bb * inv + fb * alpha) / 255);
}

static VOID
PAL_Tab5TouchPixel(
   int x,
   int y,
   uint32_t color,
   unsigned alpha
)
{
   if (x >= 0 && x < PAL_WIDTH && y >= 0 && y < PAL_HEIGHT)
   {
      uint32_t *pixel = &g_OverlayFrame[y * PAL_WIDTH + x];
      *pixel = PAL_Tab5TouchBlend(*pixel, color, alpha);
   }
}

static VOID
PAL_Tab5TouchFillRect(
   int x,
   int y,
   int width,
   int height,
   uint32_t color,
   unsigned alpha
)
{
   int row;
   int column;
   for (row = y; row < y + height; ++row)
   {
      for (column = x; column < x + width; ++column)
      {
         PAL_Tab5TouchPixel(column, row, color, alpha);
      }
   }
}

static VOID
PAL_Tab5TouchFillCircle(
   int center_x,
   int center_y,
   int radius,
   uint32_t color,
   unsigned alpha
)
{
   int x;
   int y;
   for (y = -radius; y <= radius; ++y)
   {
      for (x = -radius; x <= radius; ++x)
      {
         if (x * x + y * y <= radius * radius)
         {
            PAL_Tab5TouchPixel(center_x + x, center_y + y, color, alpha);
         }
      }
   }
}

static VOID
PAL_Tab5TouchFillRoundedRect(
   int x,
   int y,
   int width,
   int height,
   int radius,
   uint32_t color,
   unsigned alpha
)
{
   int row;
   int column;
   for (row = 0; row < height; ++row)
   {
      for (column = 0; column < width; ++column)
      {
         int dx = 0;
         int dy = 0;
         if (column < radius) dx = radius - column;
         else if (column >= width - radius) dx = column - (width - radius - 1);
         if (row < radius) dy = radius - row;
         else if (row >= height - radius) dy = row - (height - radius - 1);

         if (dx * dx + dy * dy <= radius * radius)
         {
            PAL_Tab5TouchPixel(x + column, y + row, color, alpha);
         }
      }
   }
}

static const uint8_t g_Tab5TouchFont[][5] =
{
   /* A */ {0x7e, 0x11, 0x11, 0x11, 0x7e},
   /* B */ {0x7f, 0x49, 0x49, 0x49, 0x36},
   /* X */ {0x63, 0x14, 0x08, 0x14, 0x63},
   /* Y */ {0x03, 0x04, 0x78, 0x04, 0x03},
   /* U */ {0x3f, 0x40, 0x40, 0x40, 0x3f},
   /* T */ {0x01, 0x01, 0x7f, 0x01, 0x01},
   /* O */ {0x3e, 0x41, 0x41, 0x41, 0x3e},
};

static VOID
PAL_Tab5TouchDrawGlyph(
   int x,
   int y,
   int glyph,
   uint32_t color,
   unsigned alpha
)
{
   int column;
   int row;
   for (column = 0; column < 5; ++column)
   {
      for (row = 0; row < 7; ++row)
      {
         if (g_Tab5TouchFont[glyph][column] & (1 << row))
         {
            PAL_Tab5TouchPixel(x + column, y + row, color, alpha);
         }
      }
   }
}

static VOID
PAL_Tab5TouchDrawButton(
   int center_x,
   int center_y,
   int glyph,
   TAB5TOUCHAREA area,
   uint32_t color
)
{
   const BOOL pressed = g_FingerDown && g_CurrentArea == area;
   PAL_Tab5TouchFillCircle(center_x + 1, center_y + 2, 15,
                           0x00000000u, pressed ? 80 : 38);
   PAL_Tab5TouchFillCircle(center_x, center_y, 15,
                           0x00ffffffu, pressed ? 175 : 38);
   PAL_Tab5TouchFillCircle(center_x, center_y, 13,
                           0x00101720u, pressed ? 118 : 72);
   PAL_Tab5TouchFillCircle(center_x, center_y, 10,
                           color, pressed ? 48 : 9);
   PAL_Tab5TouchDrawGlyph(center_x - 2, center_y - 3, glyph,
                          0x00ffffffu, pressed ? 255 : 172);
}

static VOID
PAL_Tab5TouchDrawArrow(
   int center_x,
   int center_y,
   TAB5TOUCHAREA area,
   unsigned alpha
)
{
   int step;
   switch (area)
   {
   case kTab5TouchUp:
      for (step = 0; step < 6; ++step)
         PAL_Tab5TouchFillRect(center_x - step, center_y - 5 + step,
                               step * 2 + 1, 1, 0x00edf5f8u, alpha);
      PAL_Tab5TouchFillRect(center_x - 1, center_y + 1, 3, 5,
                            0x00edf5f8u, alpha);
      break;
   case kTab5TouchDown:
      for (step = 0; step < 6; ++step)
         PAL_Tab5TouchFillRect(center_x - step, center_y + 5 - step,
                               step * 2 + 1, 1, 0x00edf5f8u, alpha);
      PAL_Tab5TouchFillRect(center_x - 1, center_y - 5, 3, 5,
                            0x00edf5f8u, alpha);
      break;
   case kTab5TouchLeft:
      for (step = 0; step < 6; ++step)
         PAL_Tab5TouchFillRect(center_x - 5 + step, center_y - step,
                               1, step * 2 + 1, 0x00edf5f8u, alpha);
      PAL_Tab5TouchFillRect(center_x + 1, center_y - 1, 5, 3,
                            0x00edf5f8u, alpha);
      break;
   case kTab5TouchRight:
      for (step = 0; step < 6; ++step)
         PAL_Tab5TouchFillRect(center_x + 5 - step, center_y - step,
                               1, step * 2 + 1, 0x00edf5f8u, alpha);
      PAL_Tab5TouchFillRect(center_x - 5, center_y - 1, 5, 3,
                            0x00edf5f8u, alpha);
      break;
   default:
      break;
   }
}

static VOID
PAL_Tab5TouchDrawDPadButton(
   int x,
   int y,
   int width,
   int height,
   TAB5TOUCHAREA area
)
{
   const BOOL pressed = g_FingerDown && g_CurrentArea == area;
   PAL_Tab5TouchFillRoundedRect(x + 1, y + 2, width, height, 7,
                                0x00000000u, pressed ? 72 : 24);
   PAL_Tab5TouchFillRoundedRect(x, y, width, height, 7,
                                0x00ffffffu, pressed ? 120 : 27);
   PAL_Tab5TouchFillRoundedRect(x + 2, y + 2, width - 4, height - 4, 5,
                                0x00101720u, pressed ? 112 : 55);
   PAL_Tab5TouchDrawArrow(x + width / 2, y + height / 2, area,
                          pressed ? 245 : 145);
}

static VOID
PAL_Tab5TouchDrawDPad(
   VOID
)
{
   /* Four distinct buttons with a blank center make the selected direction
    * unambiguous. Their actual elliptical hit areas extend beyond the art. */
   PAL_Tab5TouchDrawDPadButton(36, 118, 24, 27, kTab5TouchUp);
   PAL_Tab5TouchDrawDPadButton(36, 170, 24, 27, kTab5TouchDown);
   PAL_Tab5TouchDrawDPadButton(7, 145, 27, 24, kTab5TouchLeft);
   PAL_Tab5TouchDrawDPadButton(62, 145, 27, 24, kTab5TouchRight);
}

static VOID
PAL_Tab5TouchDrawOverlay(
   VOID
)
{
   const BOOL auto_pressed = g_FingerDown && g_CurrentArea == kTab5TouchAuto;

   PAL_Tab5TouchDrawDPad();

   PAL_Tab5TouchDrawButton(282, 166, 0, kTab5TouchA, 0x004bd98bu);
   PAL_Tab5TouchDrawButton(282, 132, 1, kTab5TouchB, 0x00f47b72u);
   PAL_Tab5TouchDrawButton(248, 166, 2, kTab5TouchX, 0x006db8f2u);
   PAL_Tab5TouchDrawButton(248, 132, 3, kTab5TouchY, 0x00e9c46au);

   PAL_Tab5TouchFillRoundedRect(269, 97, 49, 17, 7,
                                0x00000000u, auto_pressed ? 90 : 38);
   PAL_Tab5TouchFillRoundedRect(270, 97, 48, 15, 6,
                                0x00ffffffu, auto_pressed ? 125 : 25);
   PAL_Tab5TouchFillRoundedRect(271, 98, 46, 13, 5,
                                0x00101720u, auto_pressed ? 115 : 68);
   PAL_Tab5TouchDrawGlyph(276, 101, 0, 0x00eaf2f6u,
                          auto_pressed ? 255 : 145); /* A */
   PAL_Tab5TouchDrawGlyph(283, 101, 4, 0x00eaf2f6u,
                          auto_pressed ? 255 : 145); /* U */
   PAL_Tab5TouchDrawGlyph(290, 101, 5, 0x00eaf2f6u,
                          auto_pressed ? 255 : 145); /* T */
   PAL_Tab5TouchDrawGlyph(297, 101, 6, 0x00eaf2f6u,
                          auto_pressed ? 255 : 145); /* O */
}

static VOID
PAL_Tab5PhysicalPixel(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   int x,
   int y,
   uint16_t color
)
{
   if (x >= 0 && x < panel_height && y >= 0 && y < panel_width)
   {
      /* The panel buffer is portrait; the glass is mounted 90 degrees. */
      framebuffer[(panel_height - 1 - x) * panel_width + y] = color;
   }
}

static VOID
PAL_Tab5PhysicalFillRect(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   int x,
   int y,
   int width,
   int height,
   uint16_t color
)
{
   for (int row = y; row < y + height; ++row)
   {
      for (int column = x; column < x + width; ++column)
      {
         PAL_Tab5PhysicalPixel(framebuffer, panel_width, panel_height,
                               column, row, color);
      }
   }
}

static const uint8_t *
PAL_Tab5PhysicalGlyph(
   char character
)
{
   static const uint8_t digits[][5] = {
      { 0x3e, 0x51, 0x49, 0x45, 0x3e },
      { 0x00, 0x42, 0x7f, 0x40, 0x00 },
      { 0x62, 0x51, 0x49, 0x49, 0x46 },
      { 0x22, 0x41, 0x49, 0x49, 0x36 },
      { 0x18, 0x14, 0x12, 0x7f, 0x10 },
      { 0x2f, 0x49, 0x49, 0x49, 0x31 },
      { 0x3e, 0x49, 0x49, 0x49, 0x32 },
      { 0x01, 0x71, 0x09, 0x05, 0x03 },
      { 0x36, 0x49, 0x49, 0x49, 0x36 },
      { 0x26, 0x49, 0x49, 0x49, 0x3e },
   };
   static const uint8_t percent[] = { 0x23, 0x13, 0x08, 0x64, 0x62 };
   static const uint8_t letter_v[] = { 0x0f, 0x30, 0x40, 0x30, 0x0f };
   static const uint8_t letter_a[] = { 0x7e, 0x11, 0x11, 0x11, 0x7e };
   static const uint8_t plus[] = { 0x08, 0x08, 0x3e, 0x08, 0x08 };
   static const uint8_t minus[] = { 0x08, 0x08, 0x08, 0x08, 0x08 };
   static const uint8_t dot[] = { 0x00, 0x60, 0x60, 0x00, 0x00 };
   static const uint8_t unknown[] = { 0x02, 0x01, 0x59, 0x05, 0x02 };

   if (character >= '0' && character <= '9') return digits[character - '0'];
   if (character == '%') return percent;
   if (character == 'V') return letter_v;
   if (character == 'A') return letter_a;
   if (character == '+') return plus;
   if (character == '-') return minus;
   if (character == '.') return dot;
   return unknown;
}

static VOID
PAL_Tab5PhysicalDrawText(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   int x,
   int y,
   const char *text,
   int scale,
   uint16_t color
)
{
   while (*text != '\0')
   {
      const uint8_t *glyph = PAL_Tab5PhysicalGlyph(*text++);
      for (int column = 0; column < 5; ++column)
      {
         for (int row = 0; row < 7; ++row)
         {
            if ((glyph[column] & (1 << row)) != 0)
            {
               PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                                        x + column * scale, y + row * scale,
                                        scale, scale, color);
            }
         }
      }
      x += 6 * scale;
   }
}

static VOID
PAL_Tab5PhysicalDrawCenteredText(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   int center_x,
   int y,
   const char *text,
   int scale,
   uint16_t color
)
{
   const int width = ((int)strlen(text) * 6 - 1) * scale;
   PAL_Tab5PhysicalDrawText(framebuffer, panel_width, panel_height,
                            center_x - width / 2, y, text, scale, color);
}

static VOID
PAL_Tab5PhysicalDrawOutline(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   int x,
   int y,
   int width,
   int height,
   int thickness,
   uint16_t color
)
{
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            x, y, width, thickness, color);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            x, y + height - thickness, width, thickness, color);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            x, y, thickness, height, color);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            x + width - thickness, y, thickness, height, color);
}

static VOID
PAL_Tab5PhysicalDrawBattery(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   TAB5POWERSTATUS status
)
{
   const uint16_t white = 0xffff;
   const uint16_t gray = 0x7bef;
   const uint16_t green = 0x07e0;
   const uint16_t yellow = 0xffe0;
   uint16_t state_color = gray;

   if (status.state == kTab5PowerCharging) state_color = green;
   else if (status.state == kTab5PowerDischarging) state_color = yellow;

   PAL_Tab5PhysicalDrawOutline(framebuffer, panel_width, panel_height,
                               13, 260, 43, 22, 2,
                               status.valid ? white : gray);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            56, 267, 5, 8,
                            status.valid ? white : gray);
   if (status.valid && status.percent > 0)
   {
      const int fill_width = status.percent * 37 / 100;
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               16, 263, fill_width, 16, state_color);
   }

   char text[16];
   if (status.valid)
   {
      snprintf(text, sizeof(text), "%d%%", status.percent);
   }
   else
   {
      strcpy(text, "?%");
   }
   PAL_Tab5PhysicalDrawCenteredText(framebuffer, panel_width, panel_height,
                                    35, 291, text, 2, white);

   if (status.state == kTab5PowerCharging)
   {
      /* Compact lightning bolt. */
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               34, 320, 7, 9, green);
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               29, 327, 8, 5, green);
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               32, 330, 7, 9, green);
   }
   else if (status.state == kTab5PowerDischarging)
   {
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               33, 319, 4, 14, yellow);
      for (int step = 0; step < 6; ++step)
      {
         PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                                  29 + step, 331 + step, 12 - step * 2, 1,
                                  yellow);
      }
   }
   else
   {
      PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                               29, 327, 12, 2, gray);
   }

   if (status.valid &&
       !SDL_TICKS_PASSED(SDL_GetTicks(), g_BatteryDetailsUntil))
   {
      snprintf(text, sizeof(text), "%.1fV", status.voltage);
      PAL_Tab5PhysicalDrawCenteredText(framebuffer, panel_width, panel_height,
                                       35, 354, text, 2, white);
      snprintf(text, sizeof(text), "%+.1fA", status.current);
      PAL_Tab5PhysicalDrawCenteredText(framebuffer, panel_width, panel_height,
                                       35, 378, text, 2, state_color);
   }
}

static VOID
PAL_Tab5PhysicalDrawVolume(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height
)
{
   const BOOL up_pressed = g_FingerDown && g_CurrentArea == kTab5TouchVolumeUp;
   const BOOL down_pressed = g_FingerDown && g_CurrentArea == kTab5TouchVolumeDown;
   const uint16_t white = 0xffff;
   const uint16_t gray = 0x7bef;
   const uint16_t pressed = 0x07ff;
   char text[8];

   PAL_Tab5PhysicalDrawOutline(framebuffer, panel_width, panel_height,
                               1216, 230, 56, 100, 2,
                               up_pressed ? pressed : gray);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            1231, 278, 26, 4,
                            up_pressed ? pressed : white);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            1242, 267, 4, 26,
                            up_pressed ? pressed : white);

   snprintf(text, sizeof(text), "%d", AUDIO_Tab5GetVolume());
   PAL_Tab5PhysicalDrawCenteredText(framebuffer, panel_width, panel_height,
                                    1244, 343, text, 2, white);

   PAL_Tab5PhysicalDrawOutline(framebuffer, panel_width, panel_height,
                               1216, 370, 56, 100, 2,
                               down_pressed ? pressed : gray);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            1231, 418, 26, 4,
                            down_pressed ? pressed : white);
}

static bool
PAL_Tab5DrawMargins(
   uint16_t *framebuffer,
   int panel_width,
   int panel_height,
   void *user_data
)
{
   (void)user_data;
   if (framebuffer == NULL || panel_width != PANEL_WIDTH ||
       panel_height != PANEL_HEIGHT)
   {
      return false;
   }

   PAL_Tab5PowerUpdate();

   const TAB5POWERSTATUS status = PAL_Tab5PowerGetStatus();
   const INT volume = AUDIO_Tab5GetVolume();
   const BOOL volume_up_pressed =
      g_FingerDown && g_CurrentArea == kTab5TouchVolumeUp;
   const BOOL volume_down_pressed =
      g_FingerDown && g_CurrentArea == kTab5TouchVolumeDown;
   const BOOL battery_details =
      !SDL_TICKS_PASSED(SDL_GetTicks(), g_BatteryDetailsUntil);
   const BOOL power_changed =
      status.valid != g_LastMarginPower.valid ||
      status.voltage != g_LastMarginPower.voltage ||
      status.current != g_LastMarginPower.current ||
      status.percent != g_LastMarginPower.percent ||
      status.state != g_LastMarginPower.state;

   if (g_MarginsDrawn && !power_changed &&
       volume == g_LastMarginVolume &&
       volume_up_pressed == g_LastMarginVolumeUpPressed &&
       volume_down_pressed == g_LastMarginVolumeDownPressed &&
       battery_details == g_LastMarginBatteryDetails)
   {
      return false;
   }

   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            0, 0, PHYSICAL_MARGIN, PHYSICAL_HEIGHT, 0x0000);
   PAL_Tab5PhysicalFillRect(framebuffer, panel_width, panel_height,
                            PHYSICAL_WIDTH - PHYSICAL_MARGIN, 0,
                            PHYSICAL_MARGIN, PHYSICAL_HEIGHT, 0x0000);
   PAL_Tab5PhysicalDrawBattery(framebuffer, panel_width, panel_height, status);
   PAL_Tab5PhysicalDrawVolume(framebuffer, panel_width, panel_height);

   g_MarginsDrawn = TRUE;
   g_LastMarginPower = status;
   g_LastMarginVolume = volume;
   g_LastMarginVolumeUpPressed = volume_up_pressed;
   g_LastMarginVolumeDownPressed = volume_down_pressed;
   g_LastMarginBatteryDetails = battery_details;
   return true;
}

const void *
PAL_Tab5TouchComposeOverlay(
   const void *pixels,
   INT pitch
)
{
   int row;

   if (g_OverlayFrame == NULL)
   {
      g_OverlayFrame = heap_caps_aligned_alloc(64,
          PAL_WIDTH * PAL_HEIGHT * sizeof(uint32_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
      if (g_OverlayFrame == NULL)
      {
         return pixels;
      }
   }

   for (row = 0; row < PAL_HEIGHT; ++row)
   {
      memcpy(&g_OverlayFrame[row * PAL_WIDTH],
             (const uint8_t *)pixels + row * pitch,
             PAL_WIDTH * sizeof(uint32_t));
   }

   PAL_Tab5TouchDrawOverlay();
   return g_OverlayFrame;
}

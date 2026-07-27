#ifndef DEMON_ASSETS_H
#define DEMON_ASSETS_H

#include <stdint.h>

#define DEMON_MOUSE_CURSOR_WIDTH 16u
#define DEMON_MOUSE_CURSOR_HEIGHT 24u

const uint32_t *demon_mouse_cursor_pixels(void);

/* Stored at quarter resolution and upscaled 2x (nearest neighbor) at blit
   time -- a full 640x480 raw image would push the backbuffer allocation past
   the kernel's 4 MiB identity-mapped window. */
#define DEMON_WALLPAPER_WIDTH 320u
#define DEMON_WALLPAPER_HEIGHT 240u

const uint32_t *demon_wallpaper_pixels(void);

/* Contextual cursor icon pack (assets/Mouse/New Icons/Mouse Icons/), indices
   1..13 of enum graphics_cursor_icon (index 0, GRAPHICS_CURSOR_ARROW, is the
   original demon_mouse_cursor_pixels() image, not part of this blob). Every
   icon shares the same 16x24 footprint as the default cursor. Returns NULL
   for index 0 or an out-of-range index -- callers fall back to the default
   arrow in that case. */
#define DEMON_CURSOR_ICON_WIDTH 16u
#define DEMON_CURSOR_ICON_HEIGHT 24u
#define DEMON_CURSOR_ICON_COUNT 13u

const uint32_t *demon_cursor_icon_pixels(unsigned int icon_index);

/* Titlebar window-control icon pack (assets/Mouse/New Icons/), matching
   enum demon_ui_icon's order. There is no maximize icon (the source pack's
   "maximize program button" files are mislabeled duplicates of the close
   X), so the maximize titlebar control stays a drawn dot rather than using
   a wrong glyph. */
enum demon_ui_icon {
    DEMON_UI_ICON_CLOSE,
    DEMON_UI_ICON_CLOSE_HOVER,
    DEMON_UI_ICON_MINIMIZE,
    DEMON_UI_ICON_MINIMIZE_HOVER,
    DEMON_UI_ICON_COUNT,
};
#define DEMON_UI_ICON_WIDTH 10u
#define DEMON_UI_ICON_HEIGHT 10u

const uint32_t *demon_ui_icon_pixels(enum demon_ui_icon icon);

/* Diamond "Liquid OS" start-button logo. */
#define DEMON_START_LOGO_WIDTH 22u
#define DEMON_START_LOGO_HEIGHT 22u

const uint32_t *demon_start_logo_pixels(void);

#endif

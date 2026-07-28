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

/* Fluent contextual cursor icon pack, indices
   1..13 of enum graphics_cursor_icon (index 0, GRAPHICS_CURSOR_ARROW, is the
   original demon_mouse_cursor_pixels() image, not part of this blob). Every
   icon shares the same 16x24 footprint as the default cursor. Returns NULL
   for index 0 or an out-of-range index -- callers fall back to the default
   arrow in that case. */
#define DEMON_CURSOR_ICON_WIDTH 16u
#define DEMON_CURSOR_ICON_HEIGHT 24u
#define DEMON_CURSOR_ICON_COUNT 13u

const uint32_t *demon_cursor_icon_pixels(unsigned int icon_index);

/* Fluent titlebar window-control icon pack, matching enum demon_ui_icon's
   order. */
enum demon_ui_icon {
    DEMON_UI_ICON_CLOSE,
    DEMON_UI_ICON_CLOSE_HOVER,
    DEMON_UI_ICON_MINIMIZE,
    DEMON_UI_ICON_MINIMIZE_HOVER,
    DEMON_UI_ICON_MAXIMIZE,
    DEMON_UI_ICON_MAXIMIZE_HOVER,
    DEMON_UI_ICON_COUNT,
};
#define DEMON_UI_ICON_WIDTH 10u
#define DEMON_UI_ICON_HEIGHT 10u

const uint32_t *demon_ui_icon_pixels(enum demon_ui_icon icon);

/* Fluent start-here icon used for DemonOS shell branding controls. */
#define DEMON_START_LOGO_WIDTH 22u
#define DEMON_START_LOGO_HEIGHT 22u

const uint32_t *demon_start_logo_pixels(void);

enum demon_shell_icon {
    DEMON_SHELL_ICON_TERMINAL,
    DEMON_SHELL_ICON_FOLDER,
    DEMON_SHELL_ICON_BROWSER,
    DEMON_SHELL_ICON_APPLICATION,
    DEMON_SHELL_ICON_SETTINGS,
    DEMON_SHELL_ICON_HOME,
    DEMON_SHELL_ICON_COUNT,
};
#define DEMON_SHELL_ICON_WIDTH 22u
#define DEMON_SHELL_ICON_HEIGHT 22u

const uint32_t *demon_shell_icon_pixels(enum demon_shell_icon icon);

#endif

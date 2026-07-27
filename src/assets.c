#include <demon/assets.h>

#include <stddef.h>

extern const uint8_t _binary_build_mouse_argb_start[];
extern const uint8_t _binary_build_mouse_argb_end[];

const uint32_t *demon_mouse_cursor_pixels(void) {
    const size_t expected = DEMON_MOUSE_CURSOR_WIDTH * DEMON_MOUSE_CURSOR_HEIGHT * sizeof(uint32_t);
    if ((size_t)(_binary_build_mouse_argb_end - _binary_build_mouse_argb_start) != expected)
        return NULL;
    return (const uint32_t *)(const void *)_binary_build_mouse_argb_start;
}

extern const uint8_t _binary_build_wallpaper_argb_start[];
extern const uint8_t _binary_build_wallpaper_argb_end[];

const uint32_t *demon_wallpaper_pixels(void) {
    const size_t expected = DEMON_WALLPAPER_WIDTH * DEMON_WALLPAPER_HEIGHT * sizeof(uint32_t);
    if ((size_t)(_binary_build_wallpaper_argb_end - _binary_build_wallpaper_argb_start) != expected)
        return NULL;
    return (const uint32_t *)(const void *)_binary_build_wallpaper_argb_start;
}

extern const uint8_t _binary_build_cursor_icons_argb_start[];
extern const uint8_t _binary_build_cursor_icons_argb_end[];

const uint32_t *demon_cursor_icon_pixels(unsigned int icon_index) {
    const size_t icon_bytes = DEMON_CURSOR_ICON_WIDTH * DEMON_CURSOR_ICON_HEIGHT * sizeof(uint32_t);
    const size_t expected = icon_bytes * DEMON_CURSOR_ICON_COUNT;
    if ((size_t)(_binary_build_cursor_icons_argb_end - _binary_build_cursor_icons_argb_start) != expected)
        return NULL;
    if (icon_index < 1u || icon_index > DEMON_CURSOR_ICON_COUNT) return NULL;
    return (const uint32_t *)(const void *)
        (_binary_build_cursor_icons_argb_start + (icon_index - 1u) * icon_bytes);
}

extern const uint8_t _binary_build_ui_icons_argb_start[];
extern const uint8_t _binary_build_ui_icons_argb_end[];

const uint32_t *demon_ui_icon_pixels(enum demon_ui_icon icon) {
    const size_t icon_bytes = DEMON_UI_ICON_WIDTH * DEMON_UI_ICON_HEIGHT * sizeof(uint32_t);
    const size_t expected = icon_bytes * DEMON_UI_ICON_COUNT;
    if ((size_t)(_binary_build_ui_icons_argb_end - _binary_build_ui_icons_argb_start) != expected)
        return NULL;
    if ((unsigned int)icon >= DEMON_UI_ICON_COUNT) return NULL;
    return (const uint32_t *)(const void *)
        (_binary_build_ui_icons_argb_start + (size_t)icon * icon_bytes);
}

extern const uint8_t _binary_build_start_logo_argb_start[];
extern const uint8_t _binary_build_start_logo_argb_end[];

const uint32_t *demon_start_logo_pixels(void) {
    const size_t expected = DEMON_START_LOGO_WIDTH * DEMON_START_LOGO_HEIGHT * sizeof(uint32_t);
    if ((size_t)(_binary_build_start_logo_argb_end - _binary_build_start_logo_argb_start) != expected)
        return NULL;
    return (const uint32_t *)(const void *)_binary_build_start_logo_argb_start;
}

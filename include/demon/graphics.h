#ifndef DEMON_GRAPHICS_H
#define DEMON_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct graphics_rect { int32_t x, y, width, height; };

struct graphics_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    struct graphics_rect clip;
    struct graphics_rect damage;
    bool dirty;
};

/* One entry per embedded cursor icon (assets/Mouse/New Icons/Mouse Icons/,
   see demon_cursor_icon_pixels in demon/assets.h) plus the pre-existing
   procedural cursors this enum already had. Order matches
   CURSOR_ICON_FILES in the Makefile's asset-conversion rule, since both are
   indexed the same way when picking an embedded pixel buffer. */
enum graphics_cursor_icon {
    GRAPHICS_CURSOR_ARROW,
    GRAPHICS_CURSOR_HAND,
    GRAPHICS_CURSOR_TEXT,
    GRAPHICS_CURSOR_RESIZE_DIAGONAL,
    GRAPHICS_CURSOR_RESIZE_N,
    GRAPHICS_CURSOR_RESIZE_S,
    GRAPHICS_CURSOR_RESIZE_E,
    GRAPHICS_CURSOR_RESIZE_W,
    GRAPHICS_CURSOR_RESIZE_NE,
    GRAPHICS_CURSOR_RESIZE_NW,
    GRAPHICS_CURSOR_RESIZE_SE,
    GRAPHICS_CURSOR_RESIZE_SW,
    GRAPHICS_CURSOR_BLOCKED,
    GRAPHICS_CURSOR_INFO,
    GRAPHICS_CURSOR_COUNT,
};

bool graphics_surface_init(struct graphics_surface *surface, uint32_t *pixels,
                           uint32_t width, uint32_t height, uint32_t stride);
void graphics_reset_clip(struct graphics_surface *surface);
bool graphics_set_clip(struct graphics_surface *surface, struct graphics_rect clip);
void graphics_reset_damage(struct graphics_surface *surface);
bool graphics_get_damage(const struct graphics_surface *surface, struct graphics_rect *damage);
void graphics_clear(struct graphics_surface *surface, uint32_t argb);
void graphics_pixel(struct graphics_surface *surface, int32_t x, int32_t y, uint32_t argb);
void graphics_fill_rect(struct graphics_surface *surface, struct graphics_rect rect, uint32_t argb);
void graphics_gradient_vertical(struct graphics_surface *surface, struct graphics_rect rect,
                                uint32_t top, uint32_t bottom);
void graphics_line(struct graphics_surface *surface, int32_t x0, int32_t y0,
                   int32_t x1, int32_t y1, uint32_t argb);
void graphics_rounded_rect(struct graphics_surface *surface, struct graphics_rect rect,
                           uint32_t radius, uint32_t argb);
void graphics_rounded_border(struct graphics_surface *surface, struct graphics_rect rect,
                             uint32_t radius, uint32_t thickness,
                             uint32_t border, uint32_t fill);
void graphics_shadow(struct graphics_surface *surface, struct graphics_rect rect,
                     uint32_t radius, uint32_t spread, uint32_t argb);
void graphics_blit(struct graphics_surface *surface, int32_t x, int32_t y,
                   const uint32_t *pixels, uint32_t width, uint32_t height,
                   uint32_t source_stride);
void graphics_text(struct graphics_surface *surface, int32_t x, int32_t y,
                   const char *text, uint32_t scale, uint32_t argb);
void graphics_cursor(struct graphics_surface *surface, int32_t x, int32_t y,
                     enum graphics_cursor_icon icon);
bool graphics_self_test(void);

#ifdef __cplusplus
}
#endif

#endif

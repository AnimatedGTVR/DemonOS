#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_TRANSFER_MAX_PIXELS 4096u
#define DISPLAY_SUBMIT_PRESENT 1u

struct display_user_info {
    uint64_t width;
    uint64_t height;
    uint64_t stride_pixels;
    uint64_t format;
    uint64_t max_transfer_pixels;
};

struct display_user_submit {
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
    uint64_t pixels;
    uint64_t flags;
};

/* display_submit_effect exposes the kernel's existing rounded-rect/border/
   gradient/shadow compositing primitives (libs/graphics/graphics.c) to
   userspace, so the compositor can draw real Aero-glass-style chrome
   instead of only flat rectangles. It takes no user-space pixel-buffer
   argument, but the dispatcher still bounces to the kernel address space
   before drawing: the backbuffer's low physical address aliases part of
   USER_CODE's virtual range, so writing to it under the caller's own CR3
   can corrupt the running process's own code. */
enum display_effect_kind {
    DISPLAY_EFFECT_ROUNDED_RECT   = 0u,
    DISPLAY_EFFECT_ROUNDED_BORDER = 1u,
    DISPLAY_EFFECT_GRADIENT_V     = 2u,
    DISPLAY_EFFECT_SHADOW         = 3u,
    /* Blit the kernel-embedded desktop wallpaper (assets/Flowers.jpg ->
       build/wallpaper.argb, exposed via demon_wallpaper_pixels) into the
       given rect, upscaled 2x nearest-neighbor from its quarter-resolution
       storage. Each destination pixel reads the source at (x/2, y/2), so
       partial-region requests stay aligned with what a full-screen blit
       would have shown at those pixels. radius/arg1-3 are unused. */
    DISPLAY_EFFECT_WALLPAPER      = 4u,
    /* Blit one titlebar window-control icon (enum demon_ui_icon, selected
       by arg1) at 1:1 scale, no upscaling -- unlike the wallpaper these are
       already screen-resolution UI glyphs. radius/arg2/arg3 are unused. */
    DISPLAY_EFFECT_UI_ICON        = 5u,
    /* Blit the Fluent start-here logo (demon_start_logo_pixels) at 1:1
       scale. radius/arg1-3 are unused. */
    DISPLAY_EFFECT_START_LOGO     = 6u,
    /* Blit one 22x22 Fluent desktop/application icon selected by arg1. */
    DISPLAY_EFFECT_SHELL_ICON     = 7u,
};

struct display_user_effect {
    uint64_t kind;    /* enum display_effect_kind */
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
    uint64_t radius;  /* rounded_rect / rounded_border / shadow */
    uint64_t arg1;    /* rounded_rect+shadow: argb. rounded_border: thickness. gradient: top argb. ui_icon: enum demon_ui_icon */
    uint64_t arg2;    /* rounded_border: border argb. gradient: bottom argb. shadow: spread */
    uint64_t arg3;    /* rounded_border: fill argb */
    uint64_t flags;   /* bit 0 = DISPLAY_SUBMIT_PRESENT, same as display_submit */
};

bool display_describe(struct display_user_info *result);
bool display_submit(const struct display_user_submit *request);
bool display_submit_effect(const struct display_user_effect *request);
uint64_t display_frames_presented(void);
uint64_t display_pixels_transferred(void);
uint64_t display_last_present_tick(void);
uint64_t display_min_present_interval(void);

/* Compositor introspection: the compositor is the only process holding the
   DISPLAY capability's write right, so it self-reports its window-table
   state (live window count and currently focused window id) after every
   change. This lets the kernel boot test assert on real dynamic behavior
   (create/focus/close) instead of scripted pixel/frame counts, the same way
   surface_created()/surface_reclaimed() already let it inspect
   compositor-adjacent state without the kernel implementing WM policy. */
void display_compositor_report(uint64_t window_count, uint64_t focused_window_id);
uint64_t display_compositor_window_count(void);
uint64_t display_compositor_focused_window(void);
uint64_t display_compositor_report_count(void);

#endif

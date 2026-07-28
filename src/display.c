#include <kernel/display.h>
#include <kernel/framebuffer.h>
#include <kernel/interrupts.h>
#include <kernel/serial.h>
#include <demon/assets.h>
#include <demon/graphics.h>

#include <stddef.h>

static uint64_t frames;
static uint64_t pixels;
static uint64_t last_present_tick;
static uint64_t minimum_present_interval = UINT64_MAX;
static uint64_t compositor_window_count;
static uint64_t compositor_focused_window;
static uint64_t compositor_report_count;

bool display_describe(struct display_user_info *result) {
    if (result == NULL || !framebuffer_available()) return false;
    const struct framebuffer_info *info = framebuffer_get_info();
    *result = (struct display_user_info) {
        .width = info->width,
        .height = info->height,
        .stride_pixels = info->width,
        .format = 1u, /* canonical 32-bit ARGB */
        .max_transfer_pixels = DISPLAY_TRANSFER_MAX_PIXELS,
    };
    return true;
}

bool display_submit(const struct display_user_submit *request) {
    if (request == NULL || !framebuffer_available() || request->width == 0u ||
        request->height == 0u || request->width > UINT32_MAX ||
        request->height > UINT32_MAX || request->x > INT32_MAX ||
        request->y > INT32_MAX ||
        (request->flags & ~DISPLAY_SUBMIT_PRESENT) != 0u ||
        request->width > DISPLAY_TRANSFER_MAX_PIXELS / request->height) {
        return false;
    }
    const uint64_t count = request->width * request->height;
    framebuffer_blit((int32_t)request->x, (int32_t)request->y,
        (const uint32_t *)(uintptr_t)request->pixels,
        (uint32_t)request->width, (uint32_t)request->height,
        (uint32_t)request->width);
    if ((request->flags & DISPLAY_SUBMIT_PRESENT) != 0u) {
        framebuffer_present();
        const uint64_t now = interrupts_timer_ticks();
        if (frames != 0u) {
            const uint64_t interval = now - last_present_tick;
            if (interval < minimum_present_interval) minimum_present_interval = interval;
        }
        last_present_tick = now;
        ++frames;
    }
    pixels += count;
    return true;
}

bool display_submit_effect(const struct display_user_effect *request) {
    if (request == NULL || !framebuffer_available() ||
        request->width == 0u || request->height == 0u ||
        request->width > (uint64_t)INT32_MAX || request->height > (uint64_t)INT32_MAX ||
        request->x > (uint64_t)INT32_MAX || request->y > (uint64_t)INT32_MAX ||
        (request->flags & ~DISPLAY_SUBMIT_PRESENT) != 0u ||
        request->kind > DISPLAY_EFFECT_SHELL_ICON) {
        return false;
    }
    struct graphics_surface surface;
    if (!framebuffer_backbuffer_surface(&surface)) return false;

    const struct graphics_rect rect = {
        (int32_t)request->x, (int32_t)request->y,
        (int32_t)request->width, (int32_t)request->height,
    };
    switch ((enum display_effect_kind)request->kind) {
        case DISPLAY_EFFECT_ROUNDED_RECT:
            graphics_rounded_rect(&surface, rect, (uint32_t)request->radius,
                (uint32_t)request->arg1);
            break;
        case DISPLAY_EFFECT_ROUNDED_BORDER:
            graphics_rounded_border(&surface, rect, (uint32_t)request->radius,
                (uint32_t)request->arg1, (uint32_t)request->arg2, (uint32_t)request->arg3);
            break;
        case DISPLAY_EFFECT_GRADIENT_V:
            graphics_gradient_vertical(&surface, rect, (uint32_t)request->arg1,
                (uint32_t)request->arg2);
            break;
        case DISPLAY_EFFECT_SHADOW:
            graphics_shadow(&surface, rect, (uint32_t)request->radius,
                (uint32_t)request->arg2, (uint32_t)request->arg1);
            break;
        case DISPLAY_EFFECT_WALLPAPER: {
            /* 2x nearest-neighbor upscale from the quarter-resolution
               embedded image. Each destination pixel (dx,dy) reads
               wallpaper[dy/2][dx/2], so any partial-region request stays
               aligned with what a full-screen blit shows at those pixels. */
            const uint32_t *wallpaper = demon_wallpaper_pixels();
            if (wallpaper == NULL) return false;
            uint64_t end_x = request->x + request->width;
            uint64_t end_y = request->y + request->height;
            if (end_x > 2u * DEMON_WALLPAPER_WIDTH) end_x = 2u * DEMON_WALLPAPER_WIDTH;
            if (end_y > 2u * DEMON_WALLPAPER_HEIGHT) end_y = 2u * DEMON_WALLPAPER_HEIGHT;
            if (end_x > surface.width) end_x = surface.width;
            if (end_y > surface.height) end_y = surface.height;
            for (uint64_t dy = request->y; dy < end_y; ++dy)
                for (uint64_t dx = request->x; dx < end_x; ++dx)
                    surface.pixels[dy * surface.stride + dx] =
                        wallpaper[(dy / 2u) * DEMON_WALLPAPER_WIDTH + dx / 2u];
            break;
        }
        case DISPLAY_EFFECT_UI_ICON: {
            if (request->arg1 >= DEMON_UI_ICON_COUNT) return false;
            const uint32_t *icon =
                demon_ui_icon_pixels((enum demon_ui_icon)request->arg1);
            if (icon == NULL) return false;
            graphics_blit(&surface, (int32_t)request->x, (int32_t)request->y,
                icon, DEMON_UI_ICON_WIDTH, DEMON_UI_ICON_HEIGHT, DEMON_UI_ICON_WIDTH);
            break;
        }
        case DISPLAY_EFFECT_START_LOGO: {
            const uint32_t *logo = demon_start_logo_pixels();
            if (logo == NULL) return false;
            graphics_blit(&surface, (int32_t)request->x, (int32_t)request->y,
                logo, DEMON_START_LOGO_WIDTH, DEMON_START_LOGO_HEIGHT, DEMON_START_LOGO_WIDTH);
            break;
        }
        case DISPLAY_EFFECT_SHELL_ICON: {
            if (request->arg1 >= DEMON_SHELL_ICON_COUNT) return false;
            const uint32_t *icon =
                demon_shell_icon_pixels((enum demon_shell_icon)request->arg1);
            if (icon == NULL) return false;
            graphics_blit(&surface, (int32_t)request->x, (int32_t)request->y,
                icon, DEMON_SHELL_ICON_WIDTH, DEMON_SHELL_ICON_HEIGHT,
                DEMON_SHELL_ICON_WIDTH);
            break;
        }
    }

    /* graphics_surface tracks its own damage internally (see the comment on
       framebuffer_mark_dirty), but that copy is local to the stack-allocated
       `surface` above and discarded when this function returns, so the real
       scanout damage tracker never learns anything was drawn here. Mark it
       explicitly. Shadow expands outward by up to `spread` pixels beyond the
       nominal rect, so widen the marked region for that one kind rather than
       under-marking a soft-edged effect that visibly bleeds past its rect. */
    {
        int32_t mark_x = rect.x, mark_y = rect.y;
        int32_t mark_width = rect.width, mark_height = rect.height;
        if (request->kind == DISPLAY_EFFECT_SHADOW) {
            int32_t spread = (int32_t)request->arg2;
            if (spread > 32) spread = 32;
            mark_x -= spread; mark_y -= spread;
            mark_width += 2 * spread; mark_height += 2 * spread;
        }
        framebuffer_mark_dirty((struct framebuffer_rect){mark_x, mark_y, mark_width, mark_height});
    }

    if ((request->flags & DISPLAY_SUBMIT_PRESENT) != 0u) {
        framebuffer_present();
        const uint64_t now = interrupts_timer_ticks();
        if (frames != 0u) {
            const uint64_t interval = now - last_present_tick;
            if (interval < minimum_present_interval) minimum_present_interval = interval;
        }
        last_present_tick = now;
        ++frames;
    }
    return true;
}

uint64_t display_frames_presented(void) { return frames; }
uint64_t display_pixels_transferred(void) { return pixels; }
uint64_t display_last_present_tick(void) { return last_present_tick; }
uint64_t display_min_present_interval(void) {
    return frames < 2u ? 0u : minimum_present_interval;
}

void display_compositor_report(uint64_t window_count, uint64_t focused_window_id) {
    compositor_window_count = window_count;
    compositor_focused_window = focused_window_id;
    ++compositor_report_count;
}
uint64_t display_compositor_window_count(void) { return compositor_window_count; }
uint64_t display_compositor_focused_window(void) { return compositor_focused_window; }
uint64_t display_compositor_report_count(void) { return compositor_report_count; }

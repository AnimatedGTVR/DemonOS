#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <kernel/multiboot2.h>
#include <demon/graphics.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAMEBUFFER_MAX_WIDTH 640u
#define FRAMEBUFFER_MAX_HEIGHT 480u
#define FRAMEBUFFER_BACKBUFFER_MAX \
    ((size_t)FRAMEBUFFER_MAX_WIDTH * FRAMEBUFFER_MAX_HEIGHT * sizeof(uint32_t))

struct framebuffer_info {
    uint64_t physical_address;
    uintptr_t virtual_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bits_per_pixel;
    uint8_t pixel_format;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
};

struct framebuffer_rect { int32_t x, y, width, height; };

void framebuffer_reset(void);
bool framebuffer_discover(const struct multiboot2_tag_framebuffer *tag);
size_t framebuffer_backbuffer_bytes(void);
bool framebuffer_enable(uint64_t pdpt_address, uint64_t low_pd_address,
                        uint64_t framebuffer_pd_address,
                        uint64_t backbuffer_address, size_t backbuffer_capacity);
bool framebuffer_available(void);
const struct framebuffer_info *framebuffer_get_info(void);
void framebuffer_clear(uint32_t argb);
void framebuffer_pixel(int32_t x, int32_t y, uint32_t argb);
void framebuffer_fill_rect(struct framebuffer_rect rect, uint32_t argb);
void framebuffer_border_rect(struct framebuffer_rect rect, uint32_t argb);
void framebuffer_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb);
void framebuffer_blit(int32_t x, int32_t y, const uint32_t *pixels,
                      uint32_t width, uint32_t height, uint32_t source_pitch);
void framebuffer_text(int32_t x, int32_t y, const char *text,
                      uint32_t scale, uint32_t argb);
// A genuinely smaller font (4x5 native pixels vs the 5x7 above), not a
// scaled-down version of it -- for callers that want smaller text than
// framebuffer_text's own scale=1 floor can provide (see MakoBox's console).
void framebuffer_text_compact(int32_t x, int32_t y, const char *text,
                              uint32_t scale, uint32_t argb);
/* Registers a region as needing a real scanout copy on the next
   framebuffer_present -- callers that write the backbuffer array directly
   through a graphics_surface (bypassing framebuffer_blit's own damage
   tracking) must call this themselves, or their draw silently never reaches
   the display. See display_submit_effect in src/display.c. */
void framebuffer_mark_dirty(struct framebuffer_rect rect);
void framebuffer_present(void);
bool framebuffer_cursor_move(int32_t x, int32_t y);
/* Select which cursor icon framebuffer_present() overlays at the current
   cursor position. icon_index follows enum graphics_cursor_icon: 0 is the
   default arrow (demon_mouse_cursor_pixels); 1..12 select the contextual
   pack (demon_cursor_icon_pixels). An out-of-range index falls back to the
   default arrow rather than failing, since a stale/unsupported hit-test
   result should never leave the cursor invisible. */
void framebuffer_cursor_set_icon(unsigned int icon_index);
uint64_t framebuffer_cursor_updates(void);
void framebuffer_cursor_hide(void);
bool framebuffer_self_test(void);
void framebuffer_draw_boot_test(void);
bool framebuffer_backbuffer_surface(struct graphics_surface *out);

#endif

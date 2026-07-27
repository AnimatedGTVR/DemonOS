#include <kernel/framebuffer.h>
#include <kernel/serial.h>
#include <demon/graphics.h>
#include <demon/assets.h>

#define PAGE_2M 0x200000ull
#define PAGE_PRESENT_WRITE_HUGE 0x83ull

static struct framebuffer_info info;
static uint32_t *backbuffer;
static bool discovered;
static bool enabled;
static bool dirty;
static struct framebuffer_rect damage;
static int32_t cursor_x;
static int32_t cursor_y;
static bool cursor_visible;
static uint64_t cursor_update_count;
static unsigned int cursor_icon_index;

/* Every entry in the contextual pack (demon_cursor_icon_pixels) shares the
   default arrow's 16x24 footprint, so selecting an icon never changes the
   blit/damage math in framebuffer_present/framebuffer_cursor_move -- only
   which pixel buffer they read from. */
static const uint32_t *active_cursor_pixels(void) {
    if (cursor_icon_index != 0u) {
        const uint32_t *icon = demon_cursor_icon_pixels(cursor_icon_index);
        if (icon != NULL) return icon;
    }
    return demon_mouse_cursor_pixels();
}

static bool add_overflow_u64(uint64_t left, uint64_t right, uint64_t *result) {
    *result = left + right;
    return *result < left;
}

static uint64_t align_down_2m(uint64_t value) { return value & ~(PAGE_2M - 1u); }
static uint64_t align_up_2m(uint64_t value) {
    if (value > UINT64_MAX - (PAGE_2M - 1u)) return 0u;
    return (value + PAGE_2M - 1u) & ~(PAGE_2M - 1u);
}

static bool rect_clip(struct framebuffer_rect *rect) {
    if (rect->width <= 0 || rect->height <= 0) return false;
    int64_t right = (int64_t)rect->x + rect->width;
    int64_t bottom = (int64_t)rect->y + rect->height;
    if (right <= 0 || bottom <= 0 || rect->x >= (int32_t)info.width ||
        rect->y >= (int32_t)info.height) return false;
    if (rect->x < 0) rect->x = 0;
    if (rect->y < 0) rect->y = 0;
    if (right > info.width) right = info.width;
    if (bottom > info.height) bottom = info.height;
    rect->width = (int32_t)(right - rect->x);
    rect->height = (int32_t)(bottom - rect->y);
    return rect->width > 0 && rect->height > 0;
}

static void mark_damage(struct framebuffer_rect rect) {
    if (!rect_clip(&rect)) return;
    if (!dirty) { damage = rect; dirty = true; return; }
    const int32_t left = rect.x < damage.x ? rect.x : damage.x;
    const int32_t top = rect.y < damage.y ? rect.y : damage.y;
    const int32_t r0 = rect.x + rect.width;
    const int32_t r1 = damage.x + damage.width;
    const int32_t bottom0 = rect.y + rect.height;
    const int32_t bottom1 = damage.y + damage.height;
    const int32_t right = r0 > r1 ? r0 : r1;
    const int32_t bottom = bottom0 > bottom1 ? bottom0 : bottom1;
    damage = (struct framebuffer_rect){ left, top, right - left, bottom - top };
}

/* display_submit_effect (src/display.c) writes straight into the backbuffer
   array through its own local graphics_surface, which tracks damage in a
   separate, throwaway struct graphics_surface.damage field that framebuffer
   scanout never sees -- so every rounded-rect/gradient/shadow/wallpaper draw
   silently updated the backbuffer without ever marking framebuffer_present's
   dirty region, meaning presents that only did effect draws (which is most
   of the redesigned compositor) painted nothing to the real display at all,
   leaving stale prior frames on screen until some unrelated draw_rect/
   draw_glyph call happened to touch overlapping pixels on a later frame.
   This is that missing link. */
void framebuffer_mark_dirty(struct framebuffer_rect rect) { mark_damage(rect); }

void framebuffer_reset(void) {
    info = (struct framebuffer_info){0};
    backbuffer = NULL;
    discovered = false;
    enabled = false;
    dirty = false;
    cursor_visible = false;
    cursor_update_count = 0u;
    cursor_icon_index = 0u;
}

bool framebuffer_discover(const struct multiboot2_tag_framebuffer *tag) {
    if (tag == NULL || tag->size < sizeof(*tag) || tag->framebuffer_type != 1u ||
        (tag->bits_per_pixel != 24u && tag->bits_per_pixel != 32u) ||
        tag->width == 0u || tag->height == 0u ||
        tag->width > FRAMEBUFFER_MAX_WIDTH || tag->height > FRAMEBUFFER_MAX_HEIGHT)
        return false;
    const uint64_t bytes_per_pixel = (tag->bits_per_pixel + 7u) / 8u;
    if (tag->pitch < tag->width * bytes_per_pixel || tag->pitch > 16384u ||
        tag->red_mask_size == 0u || tag->green_mask_size == 0u ||
        tag->blue_mask_size == 0u || tag->red_mask_size > 8u ||
        tag->green_mask_size > 8u || tag->blue_mask_size > 8u)
        return false;
    uint64_t byte_length = (uint64_t)tag->pitch * tag->height;
    uint64_t end;
    if (byte_length == 0u || add_overflow_u64(tag->address, byte_length, &end)) return false;
    const uint64_t first = align_down_2m(tag->address);
    const uint64_t mapped_end = align_up_2m(end);
    if (mapped_end == 0u || (first >> 30u) != ((mapped_end - 1u) >> 30u) ||
        (first >> 39u) != 0u) return false;
    info = (struct framebuffer_info){
        .physical_address = tag->address, .virtual_address = (uintptr_t)tag->address,
        .width = tag->width, .height = tag->height, .pitch = tag->pitch,
        .bits_per_pixel = tag->bits_per_pixel, .pixel_format = tag->framebuffer_type,
        .red_position = tag->red_position, .red_mask_size = tag->red_mask_size,
        .green_position = tag->green_position, .green_mask_size = tag->green_mask_size,
        .blue_position = tag->blue_position, .blue_mask_size = tag->blue_mask_size,
    };
    discovered = true;
    return true;
}

size_t framebuffer_backbuffer_bytes(void) {
    return discovered ? (size_t)info.width * info.height * sizeof(uint32_t) : 0u;
}

bool framebuffer_enable(uint64_t pdpt_address, uint64_t low_pd_address,
                        uint64_t framebuffer_pd_address,
                        uint64_t backbuffer_address, size_t backbuffer_capacity) {
    const size_t required = framebuffer_backbuffer_bytes();
    if (!discovered || required == 0u || required > backbuffer_capacity ||
        (backbuffer_address & 4095u) != 0u) return false;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)pdpt_address;
    uint64_t *pd = (uint64_t *)(uintptr_t)framebuffer_pd_address;
    const uint64_t byte_length = (uint64_t)info.pitch * info.height;
    const uint64_t first = align_down_2m(info.physical_address);
    const uint64_t mapped_end = align_up_2m(info.physical_address + byte_length);
    const size_t pdpt_index = (size_t)(first >> 30u) & 0x1FFu;
    if (pdpt_index == 0u) pd = (uint64_t *)(uintptr_t)low_pd_address;
    else {
        for (size_t i = 0u; i < 512u; ++i) pd[i] = 0u;
        pdpt[pdpt_index] = framebuffer_pd_address | 3u;
    }
    for (uint64_t physical = first; physical < mapped_end; physical += PAGE_2M) {
        const size_t index = (size_t)(physical >> 21u) & 0x1FFu;
        if (pdpt_index == 0u && index < 2u) continue;
        pd[index] = physical | PAGE_PRESENT_WRITE_HUGE;
    }
    backbuffer = (uint32_t *)(uintptr_t)backbuffer_address;
    enabled = true;
    dirty = false;
    framebuffer_clear(0xFF000000u);
    return true;
}

bool framebuffer_available(void) { return enabled; }
const struct framebuffer_info *framebuffer_get_info(void) { return enabled ? &info : NULL; }

void framebuffer_pixel(int32_t x, int32_t y, uint32_t argb) {
    if (!enabled || x < 0 || y < 0 || x >= (int32_t)info.width || y >= (int32_t)info.height)
        return;
    backbuffer[(size_t)y * info.width + (size_t)x] = argb;
    mark_damage((struct framebuffer_rect){x, y, 1, 1});
}

void framebuffer_fill_rect(struct framebuffer_rect rect, uint32_t argb) {
    if (!enabled || !rect_clip(&rect)) return;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
            backbuffer[(size_t)y * info.width + (size_t)x] = argb;
    mark_damage(rect);
}

void framebuffer_clear(uint32_t argb) {
    if (!enabled) return;
    framebuffer_fill_rect((struct framebuffer_rect){0, 0, (int32_t)info.width, (int32_t)info.height}, argb);
}

void framebuffer_border_rect(struct framebuffer_rect rect, uint32_t argb) {
    framebuffer_fill_rect((struct framebuffer_rect){rect.x, rect.y, rect.width, 1}, argb);
    framebuffer_fill_rect((struct framebuffer_rect){rect.x, rect.y + rect.height - 1, rect.width, 1}, argb);
    framebuffer_fill_rect((struct framebuffer_rect){rect.x, rect.y, 1, rect.height}, argb);
    framebuffer_fill_rect((struct framebuffer_rect){rect.x + rect.width - 1, rect.y, 1, rect.height}, argb);
}

void framebuffer_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb) {
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        framebuffer_pixel(x0, y0, argb);
        if (x0 == x1 && y0 == y1) break;
        const int32_t twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static uint32_t blend(uint32_t destination, uint32_t source) {
    const uint32_t alpha = source >> 24u;
    if (alpha == 255u) return source;
    if (alpha == 0u) return destination;
    const uint32_t inverse = 255u - alpha;
    const uint32_t rb = (((source & 0x00FF00FFu) * alpha +
        (destination & 0x00FF00FFu) * inverse) >> 8u) & 0x00FF00FFu;
    const uint32_t g = (((source & 0x0000FF00u) * alpha +
        (destination & 0x0000FF00u) * inverse) >> 8u) & 0x0000FF00u;
    return 0xFF000000u | rb | g;
}

void framebuffer_blit(int32_t x, int32_t y, const uint32_t *pixels,
                      uint32_t width, uint32_t height, uint32_t source_pitch) {
    if (!enabled || pixels == NULL || width == 0u || height == 0u ||
        source_pitch < width) return;
    struct framebuffer_rect rect = {x, y, (int32_t)width, (int32_t)height};
    struct framebuffer_rect clipped = rect;
    if (!rect_clip(&clipped)) return;
    for (int32_t destination_y = clipped.y; destination_y < clipped.y + clipped.height; ++destination_y)
        for (int32_t destination_x = clipped.x; destination_x < clipped.x + clipped.width; ++destination_x) {
            const uint32_t source_x = (uint32_t)(destination_x - x);
            const uint32_t source_y = (uint32_t)(destination_y - y);
            const size_t dest_index = (size_t)destination_y * info.width + destination_x;
            const size_t src_index = (size_t)source_y * source_pitch + source_x;
            uint32_t *destination = &backbuffer[dest_index];
            *destination = blend(*destination, pixels[src_index]);
        }
    mark_damage(clipped);
}

/* Compact 5x7 uppercase/digit font. Each byte contains one five-bit row. */
struct glyph { char value; uint8_t rows[7]; };
static const struct glyph font[] = {
 {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}},
 {'#',{10,31,10,10,31,10,0}}, {'>',{16,8,4,2,4,8,16}},
 {'<',{1,2,4,8,4,2,1}}, {'_',{0,0,0,0,0,0,31}},
 {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
 {'|',{4,4,4,4,4,4,4}},
 {':',{0,12,12,0,12,12,0}}, {'/',{1,2,4,8,16,0,0}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}}, {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{28,18,17,17,17,18,28}}, {'E',{31,16,16,30,16,16,31}},
 {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}}, {'K',{17,18,20,24,20,18,17}},
 {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
 {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
 {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
};

static const uint8_t *glyph_rows(char value) {
    if (value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
    for (size_t i = 0u; i < sizeof(font) / sizeof(font[0]); ++i)
        if (font[i].value == value) return font[i].rows;
    return font[0].rows;
}

void framebuffer_text(int32_t x, int32_t y, const char *text,
                      uint32_t scale, uint32_t argb) {
    if (!enabled || text == NULL || scale == 0u || scale > 8u) return;
    int32_t cursor = x;
    while (*text != '\0') {
        const uint8_t *rows = glyph_rows(*text++);
        for (uint32_t gy = 0u; gy < 7u; ++gy)
            for (uint32_t gx = 0u; gx < 5u; ++gx)
                if ((rows[gy] & (1u << (4u - gx))) != 0u)
                    framebuffer_fill_rect((struct framebuffer_rect){
                        cursor + (int32_t)(gx * scale), y + (int32_t)(gy * scale),
                        (int32_t)scale, (int32_t)scale}, argb);
        cursor += (int32_t)(6u * scale);
    }
}

static uint32_t scale_channel(uint32_t value, uint8_t bits) {
    if (bits >= 8u) return value;
    return (value * ((1u << bits) - 1u) + 127u) / 255u;
}

static uint32_t native_pixel(uint32_t argb) {
    const uint32_t red = scale_channel((argb >> 16u) & 0xFFu, info.red_mask_size);
    const uint32_t green = scale_channel((argb >> 8u) & 0xFFu, info.green_mask_size);
    const uint32_t blue = scale_channel(argb & 0xFFu, info.blue_mask_size);
    return (red << info.red_position) | (green << info.green_position) |
        (blue << info.blue_position);
}

static void scanout_pixel(volatile uint8_t *target, uint32_t bytes_per_pixel,
                          bool native_argb32, int32_t x, int32_t y,
                          uint32_t argb) {
    volatile uint8_t *destination = target + (size_t)y * info.pitch +
        (size_t)x * bytes_per_pixel;
    if (native_argb32) {
        *(volatile uint32_t *)destination = argb;
        return;
    }
    const uint32_t pixel = native_pixel(argb);
    destination[0] = (uint8_t)pixel;
    destination[1] = (uint8_t)(pixel >> 8u);
    destination[2] = (uint8_t)(pixel >> 16u);
    if (bytes_per_pixel == 4u) destination[3] = (uint8_t)(pixel >> 24u);
}

/* Cursor motion must not call the ordinary dirty-scene presenter. Effects
   build a frame in the backbuffer across several syscalls before the final
   PRESENT flag; flushing the shared dirty union from a cursor update can
   expose one of those partially-built regions as a colored strip. Restore
   only the old cursor footprint directly from the clean scene buffer, then
   overlay the new cursor. This also makes edge clipping explicit. */
static void scanout_backbuffer_rect(struct framebuffer_rect rect) {
    if (!enabled || !rect_clip(&rect)) return;
    volatile uint8_t *target = (volatile uint8_t *)info.virtual_address;
    const uint32_t bytes_per_pixel = (info.bits_per_pixel + 7u) / 8u;
    const bool native_argb32 = bytes_per_pixel == 4u && info.red_position == 16u &&
        info.green_position == 8u && info.blue_position == 0u &&
        info.red_mask_size == 8u && info.green_mask_size == 8u && info.blue_mask_size == 8u;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
            scanout_pixel(target, bytes_per_pixel, native_argb32, x, y,
                backbuffer[(size_t)y * info.width + (size_t)x]);
}

static void scanout_cursor_overlay(void) {
    if (!enabled || !cursor_visible) return;
    const uint32_t *icon = active_cursor_pixels();
    if (icon == NULL) return;
    volatile uint8_t *target = (volatile uint8_t *)info.virtual_address;
    const uint32_t bytes_per_pixel = (info.bits_per_pixel + 7u) / 8u;
    const bool native_argb32 = bytes_per_pixel == 4u && info.red_position == 16u &&
        info.green_position == 8u && info.blue_position == 0u &&
        info.red_mask_size == 8u && info.green_mask_size == 8u && info.blue_mask_size == 8u;
    for (uint32_t iy = 0u; iy < DEMON_MOUSE_CURSOR_HEIGHT; ++iy)
        for (uint32_t ix = 0u; ix < DEMON_MOUSE_CURSOR_WIDTH; ++ix) {
            const int32_t px = cursor_x + (int32_t)ix;
            const int32_t py = cursor_y + (int32_t)iy;
            if (px < 0 || py < 0 || px >= (int32_t)info.width ||
                py >= (int32_t)info.height) continue;
            const size_t position = (size_t)py * info.width + (size_t)px;
            const size_t source = (size_t)iy * DEMON_MOUSE_CURSOR_WIDTH + ix;
            scanout_pixel(target, bytes_per_pixel, native_argb32, px, py,
                blend(backbuffer[position], icon[source]));
        }
}

static bool scanout_matches_backbuffer(struct framebuffer_rect rect) {
    if (!enabled || !rect_clip(&rect)) return false;
    volatile const uint8_t *target = (volatile const uint8_t *)info.virtual_address;
    const uint32_t bytes_per_pixel = (info.bits_per_pixel + 7u) / 8u;
    const bool native_argb32 = bytes_per_pixel == 4u && info.red_position == 16u &&
        info.green_position == 8u && info.blue_position == 0u &&
        info.red_mask_size == 8u && info.green_mask_size == 8u && info.blue_mask_size == 8u;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x) {
            const volatile uint8_t *pixel = target + (size_t)y * info.pitch +
                (size_t)x * bytes_per_pixel;
            const uint32_t expected_argb = backbuffer[(size_t)y * info.width + (size_t)x];
            if (native_argb32) {
                if (*(volatile const uint32_t *)pixel != expected_argb) return false;
            } else {
                const uint32_t expected = native_pixel(expected_argb);
                if (pixel[0] != (uint8_t)expected ||
                    pixel[1] != (uint8_t)(expected >> 8u) ||
                    pixel[2] != (uint8_t)(expected >> 16u) ||
                    (bytes_per_pixel == 4u &&
                     pixel[3] != (uint8_t)(expected >> 24u))) return false;
            }
        }
    return true;
}

void framebuffer_present(void) {
    if (!enabled || !dirty) return;
    scanout_backbuffer_rect(damage);
    /* The cursor is a scanout overlay: it never contaminates the scene
       backbuffer, so moving it cannot leave stale saved pixels or flash when
       the compositor presents underneath it. Reapply it after every scene
       present and after the small old/new cursor damage copy. */
    scanout_cursor_overlay();
    dirty = false;
}

bool framebuffer_cursor_move(int32_t x, int32_t y) {
    const uint32_t *icon = active_cursor_pixels();
    if (!enabled || icon == NULL) return false;
    if (x < 0) x = 0; else if (x >= (int32_t)info.width) x = (int32_t)info.width - 1;
    if (y < 0) y = 0; else if (y >= (int32_t)info.height) y = (int32_t)info.height - 1;
    if (cursor_visible && x == cursor_x && y == cursor_y) return false;
    if (cursor_visible)
        scanout_backbuffer_rect((struct framebuffer_rect){cursor_x, cursor_y,
            (int32_t)DEMON_MOUSE_CURSOR_WIDTH, (int32_t)DEMON_MOUSE_CURSOR_HEIGHT});
    cursor_x = x; cursor_y = y;
    cursor_visible = true;
    ++cursor_update_count;
    scanout_cursor_overlay();
    return true;
}

void framebuffer_cursor_set_icon(unsigned int icon_index) {
    if (icon_index == cursor_icon_index) return;
    if (cursor_visible)
        scanout_backbuffer_rect((struct framebuffer_rect){cursor_x, cursor_y,
            (int32_t)DEMON_MOUSE_CURSOR_WIDTH, (int32_t)DEMON_MOUSE_CURSOR_HEIGHT});
    cursor_icon_index = icon_index;
    if (!cursor_visible) return;
    scanout_cursor_overlay();
}

uint64_t framebuffer_cursor_updates(void) { return cursor_update_count; }

bool framebuffer_self_test(void) {
    if (!enabled) return false;
    framebuffer_clear(0xFF102030u);
    framebuffer_fill_rect((struct framebuffer_rect){-4, -4, 8, 8}, 0xFFAABBCCu);
    framebuffer_pixel((int32_t)info.width, 0, 0xFFFFFFFFu);
    if (backbuffer[0] != 0xFFAABBCCu ||
        backbuffer[(size_t)(info.height - 1u) * info.width + info.width - 1u] !=
            0xFF102030u) return false;
    const int32_t test_x = (int32_t)info.width / 2;
    const int32_t test_y = (int32_t)info.height / 2;
    const uint32_t scene_pixel = backbuffer[(size_t)test_y * info.width + (size_t)test_x];
    if (!framebuffer_cursor_move(test_x, test_y)) return false;
    if (backbuffer[(size_t)test_y * info.width + (size_t)test_x] != scene_pixel)
        return false;
    /* Exercise the exact top-edge path that used to share the global dirty
       presenter. Moving away must restore every pixel under the old cursor
       from the backbuffer, with no 16x24 trail or adjacent partial scene. */
    if (!framebuffer_cursor_move(0, 0) ||
        !framebuffer_cursor_move((int32_t)DEMON_MOUSE_CURSOR_WIDTH * 2, 0) ||
        !scanout_matches_backbuffer((struct framebuffer_rect){0, 0,
            (int32_t)DEMON_MOUSE_CURSOR_WIDTH, (int32_t)DEMON_MOUSE_CURSOR_HEIGHT}) ||
        !framebuffer_cursor_move(test_x, test_y) ||
        !scanout_matches_backbuffer((struct framebuffer_rect){
            (int32_t)DEMON_MOUSE_CURSOR_WIDTH * 2, 0,
            (int32_t)DEMON_MOUSE_CURSOR_WIDTH, (int32_t)DEMON_MOUSE_CURSOR_HEIGHT}))
        return false;
    return true;
}

static int32_t draw_u32(int32_t x, int32_t y, uint32_t value, uint32_t color) {
    char digits[11];
    size_t end = sizeof(digits);
    digits[--end] = '\0';
    do {
        digits[--end] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    framebuffer_text(x, y, &digits[end], 1u, color);
    size_t count = 0u;
    while (digits[end + count] != '\0') ++count;
    return x + (int32_t)(count * 6u);
}

bool framebuffer_backbuffer_surface(struct graphics_surface *out) {
    if (out == NULL || !enabled) return false;
    return graphics_surface_init(out, backbuffer, info.width, info.height, info.width);
}

void framebuffer_draw_boot_test(void) {
    if (!enabled) return;
    struct graphics_surface surface;
    if (!graphics_surface_init(&surface, backbuffer, info.width, info.height, info.width)) return;
    graphics_gradient_vertical(&surface, (struct graphics_rect){0, 0, (int32_t)info.width, (int32_t)info.height},
        0xFF0C1524u, 0xFF243C5Au);
    struct graphics_rect header = {28, 28, (int32_t)info.width - 56, 92};
    graphics_shadow(&surface, header, 14u, 10u, 0x58000000u);
    graphics_rounded_border(&surface, header, 14u, 2u, 0xFF58A6FFu, 0xE8182437u);
    graphics_text(&surface, 52, 48, "DEMONOS", 4u, 0xFFF2F6FCu);
    graphics_text(&surface, 54, 88, "NATIVE GRAPHICS STAGE 2", 1u, 0xFF9FCBFFu);
    const struct graphics_rect cards[] = {{48,150,160,96},{224,150,160,96},{400,150,160,96}};
    const uint32_t colors[] = {0xFF2C7BE5u, 0xFF50B584u, 0xFFE09F3Eu};
    for (size_t i = 0; i < 3u; ++i) {
        graphics_shadow(&surface, cards[i], 12u, 7u, 0x48000000u);
        graphics_rounded_border(&surface, cards[i], 12u, 2u, 0xAAFFFFFFu, colors[i]);
    }
    graphics_text(&surface, 70, 178, "ARGB", 2u, 0xFFFFFFFFu);
    graphics_text(&surface, 244, 178, "CLIP", 2u, 0xFFFFFFFFu);
    graphics_text(&surface, 420, 178, "DAMAGE", 2u, 0xFFFFFFFFu);
    graphics_line(&surface, 48, 278, (int32_t)info.width - 48, 278, 0xFF7FB8FFu);
    graphics_text(&surface, 48, 300, "SOFTWARE RENDERER ONLINE", 2u, 0xFFFFFFFFu);
    graphics_text(&surface, 48, 332, "ROUNDED SHADOW GRADIENT ALPHA BLIT", 1u, 0xFFC8D8EAu);
    graphics_text(&surface, 48, 356, "HARDWARE INDEPENDENT ARGB SURFACE", 1u, 0xFFC8D8EAu);
    mark_damage((struct framebuffer_rect){0, 0, (int32_t)info.width, (int32_t)info.height});
    framebuffer_text(48, 390, "RESOLUTION", 1u, 0xFF9FCBFFu);
    int32_t cursor = draw_u32(120, 390, info.width, 0xFFFFFFFFu);
    framebuffer_text(cursor + 6, 390, "X", 1u, 0xFF9FCBFFu);
    draw_u32(cursor + 18, 390, info.height, 0xFFFFFFFFu);
    framebuffer_text(48, 410, "PITCH", 1u, 0xFF9FCBFFu);
    cursor = draw_u32(90, 410, info.pitch, 0xFFFFFFFFu);
    framebuffer_text(cursor + 6, 410, "BYTES BPP", 1u, 0xFF9FCBFFu);
    draw_u32(cursor + 66, 410, info.bits_per_pixel, 0xFFFFFFFFu);
    framebuffer_text(48, 430, "FORMAT RGB", 1u, 0xFF9FCBFFu);
    cursor = draw_u32(120, 430, info.red_mask_size, 0xFFFFFFFFu);
    cursor = draw_u32(cursor + 6, 430, info.green_mask_size, 0xFFFFFFFFu);
    draw_u32(cursor + 6, 430, info.blue_mask_size, 0xFFFFFFFFu);
    framebuffer_text(48, 450, "KERNEL C MKO X86-64", 1u, 0xFFC8D8EAu);
    framebuffer_present();
}

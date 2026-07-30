#include <demon/graphics.h>

#include <stddef.h>

static bool intersect(struct graphics_rect a, struct graphics_rect b,
                      struct graphics_rect *out) {
    int64_t left = a.x > b.x ? a.x : b.x;
    int64_t top = a.y > b.y ? a.y : b.y;
    int64_t ar = (int64_t)a.x + a.width, br = (int64_t)b.x + b.width;
    int64_t ab = (int64_t)a.y + a.height, bb = (int64_t)b.y + b.height;
    int64_t right = ar < br ? ar : br, bottom = ab < bb ? ab : bb;
    if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0 ||
        right <= left || bottom <= top) return false;
    *out = (struct graphics_rect){(int32_t)left, (int32_t)top,
        (int32_t)(right - left), (int32_t)(bottom - top)};
    return true;
}

static void damage(struct graphics_surface *s, struct graphics_rect rect) {
    struct graphics_rect bounds = {0, 0, (int32_t)s->width, (int32_t)s->height};
    if (!intersect(rect, bounds, &rect)) return;
    if (!s->dirty) { s->damage = rect; s->dirty = true; return; }
    int32_t left = rect.x < s->damage.x ? rect.x : s->damage.x;
    int32_t top = rect.y < s->damage.y ? rect.y : s->damage.y;
    int32_t right0 = rect.x + rect.width, right1 = s->damage.x + s->damage.width;
    int32_t bottom0 = rect.y + rect.height, bottom1 = s->damage.y + s->damage.height;
    int32_t right = right0 > right1 ? right0 : right1;
    int32_t bottom = bottom0 > bottom1 ? bottom0 : bottom1;
    s->damage = (struct graphics_rect){left, top, right - left, bottom - top};
}

static uint32_t blend(uint32_t dst, uint32_t src) {
    uint32_t a = src >> 24u;
    if (a == 255u) return src;
    if (a == 0u) return dst;
    uint32_t inv = 255u - a;
    uint32_t r = ((((src >> 16u) & 255u) * a + ((dst >> 16u) & 255u) * inv + 127u) / 255u) << 16u;
    uint32_t g = ((((src >> 8u) & 255u) * a + ((dst >> 8u) & 255u) * inv + 127u) / 255u) << 8u;
    uint32_t b = (((src & 255u) * a + (dst & 255u) * inv + 127u) / 255u);
    return 0xFF000000u | r | g | b;
}

bool graphics_surface_init(struct graphics_surface *s, uint32_t *pixels,
                           uint32_t width, uint32_t height, uint32_t stride) {
    if (s == NULL || pixels == NULL || width == 0u || height == 0u || stride < width ||
        width > 32767u || height > 32767u) return false;
    *s = (struct graphics_surface){.pixels = pixels, .width = width, .height = height,
        .stride = stride, .clip = {0, 0, (int32_t)width, (int32_t)height}};
    return true;
}

void graphics_reset_clip(struct graphics_surface *s) {
    if (s != NULL) s->clip = (struct graphics_rect){0, 0, (int32_t)s->width, (int32_t)s->height};
}

bool graphics_set_clip(struct graphics_surface *s, struct graphics_rect clip) {
    if (s == NULL) return false;
    struct graphics_rect bounds = {0, 0, (int32_t)s->width, (int32_t)s->height};
    if (!intersect(clip, bounds, &s->clip)) { s->clip = (struct graphics_rect){0}; return false; }
    return true;
}

void graphics_reset_damage(struct graphics_surface *s) { if (s != NULL) s->dirty = false; }
bool graphics_get_damage(const struct graphics_surface *s, struct graphics_rect *out) {
    if (s == NULL || out == NULL || !s->dirty) return false;
    *out = s->damage; return true;
}

void graphics_pixel(struct graphics_surface *s, int32_t x, int32_t y, uint32_t argb) {
    if (s == NULL || x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.width || y >= s->clip.y + s->clip.height) return;
    uint32_t *p = &s->pixels[(size_t)y * s->stride + (size_t)x];
    *p = blend(*p, argb);
    damage(s, (struct graphics_rect){x, y, 1, 1});
}

void graphics_fill_rect(struct graphics_surface *s, struct graphics_rect rect, uint32_t argb) {
    if (s == NULL || !intersect(rect, s->clip, &rect)) return;
    const bool opaque = (argb >> 24u) == 255u;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y) {
        uint32_t *row = &s->pixels[(size_t)y * s->stride + (size_t)rect.x];
        if (opaque) for (int32_t x = 0; x < rect.width; ++x) row[x] = argb;
        else for (int32_t x = 0; x < rect.width; ++x) row[x] = blend(row[x], argb);
    }
    damage(s, rect);
}

void graphics_clear(struct graphics_surface *s, uint32_t argb) {
    if (s == NULL) return;
    struct graphics_rect old = s->clip;
    graphics_reset_clip(s);
    graphics_fill_rect(s, s->clip, argb);
    s->clip = old;
}

static uint32_t lerp(uint32_t a, uint32_t b, uint32_t n, uint32_t d) {
    uint32_t inv = d - n;
    uint32_t r = ((((a >> 16u) & 255u) * inv + ((b >> 16u) & 255u) * n) / d) << 16u;
    uint32_t g = ((((a >> 8u) & 255u) * inv + ((b >> 8u) & 255u) * n) / d) << 8u;
    uint32_t blue = ((a & 255u) * inv + (b & 255u) * n) / d;
    uint32_t alpha = (((a >> 24u) * inv + (b >> 24u) * n) / d) << 24u;
    return alpha | r | g | blue;
}

void graphics_gradient_vertical(struct graphics_surface *s, struct graphics_rect rect,
                                uint32_t top, uint32_t bottom) {
    if (s == NULL || rect.height <= 0) return;
    uint32_t denominator = rect.height > 1 ? (uint32_t)rect.height - 1u : 1u;
    for (int32_t y = 0; y < rect.height; ++y)
        graphics_fill_rect(s, (struct graphics_rect){rect.x, rect.y + y, rect.width, 1},
                           lerp(top, bottom, (uint32_t)y, denominator));
}

void graphics_line(struct graphics_surface *s, int32_t x0, int32_t y0,
                   int32_t x1, int32_t y1, uint32_t argb) {
    if (s == NULL) return;
    const int32_t original_x0 = x0, original_y0 = y0, original_x1 = x1, original_y1 = y1;
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t ay = y1 > y0 ? y1 - y0 : y0 - y1, dy = -ay, sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x0 >= s->clip.x && y0 >= s->clip.y && x0 < s->clip.x + s->clip.width &&
            y0 < s->clip.y + s->clip.height) {
            uint32_t *p = &s->pixels[(size_t)y0 * s->stride + (size_t)x0]; *p = blend(*p, argb);
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t twice = error * 2; if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; } }
    int32_t left = original_x0 < original_x1 ? original_x0 : original_x1;
    int32_t top = original_y0 < original_y1 ? original_y0 : original_y1;
    int32_t right = original_x0 > original_x1 ? original_x0 : original_x1;
    int32_t bottom = original_y0 > original_y1 ? original_y0 : original_y1;
    damage(s, (struct graphics_rect){left, top, right - left + 1, bottom - top + 1});
}

static bool rounded_contains(struct graphics_rect r, uint32_t radius, int32_t x, int32_t y) {
    uint32_t limit = (uint32_t)(r.width < r.height ? r.width : r.height) / 2u;
    if (radius > limit) radius = limit;
    if (radius == 0u) return true;
    int32_t cx = x < r.x + (int32_t)radius ? r.x + (int32_t)radius - 1 :
        (x >= r.x + r.width - (int32_t)radius ? r.x + r.width - (int32_t)radius : x);
    int32_t cy = y < r.y + (int32_t)radius ? r.y + (int32_t)radius - 1 :
        (y >= r.y + r.height - (int32_t)radius ? r.y + r.height - (int32_t)radius : y);
    int32_t dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= (int32_t)(radius * radius);
}

void graphics_rounded_rect(struct graphics_surface *s, struct graphics_rect rect,
                           uint32_t radius, uint32_t argb) {
    if (s == NULL || rect.width <= 0 || rect.height <= 0) return;
    struct graphics_rect clipped;
    if (!intersect(rect, s->clip, &clipped)) return;
    for (int32_t y = clipped.y; y < clipped.y + clipped.height; ++y)
        for (int32_t x = clipped.x; x < clipped.x + clipped.width; ++x)
            if (rounded_contains(rect, radius, x, y)) {
                uint32_t *p = &s->pixels[(size_t)y * s->stride + (size_t)x]; *p = blend(*p, argb);
            }
    damage(s, clipped);
}

void graphics_rounded_border(struct graphics_surface *s, struct graphics_rect rect,
                             uint32_t radius, uint32_t thickness,
                             uint32_t border, uint32_t fill) {
    graphics_rounded_rect(s, rect, radius, border);
    if (thickness == 0u || thickness * 2u >= (uint32_t)rect.width ||
        thickness * 2u >= (uint32_t)rect.height) return;
    struct graphics_rect inner = {rect.x + (int32_t)thickness, rect.y + (int32_t)thickness,
        rect.width - (int32_t)(2u * thickness), rect.height - (int32_t)(2u * thickness)};
    graphics_rounded_rect(s, inner, radius > thickness ? radius - thickness : 0u, fill);
}

void graphics_shadow(struct graphics_surface *s, struct graphics_rect rect,
                     uint32_t radius, uint32_t spread, uint32_t argb) {
    if (spread > 32u) spread = 32u;
    uint32_t base_alpha = argb >> 24u;
    /* Three broad layers provide useful depth without spread-times-area work. */
    const uint32_t layers = spread < 3u ? spread : 3u;
    for (uint32_t layer = 0u; layer < layers; ++layer) {
        uint32_t i = spread - (spread * layer / layers);
        uint32_t alpha = base_alpha * (layer + 1u) / (layers * 2u);
        struct graphics_rect expanded = {rect.x - (int32_t)i, rect.y - (int32_t)i,
            rect.width + (int32_t)(2u * i), rect.height + (int32_t)(2u * i)};
        graphics_rounded_rect(s, expanded, radius + i, (argb & 0x00FFFFFFu) | (alpha << 24u));
    }
}

void graphics_blit(struct graphics_surface *s, int32_t x, int32_t y,
                   const uint32_t *pixels, uint32_t width, uint32_t height,
                   uint32_t source_stride) {
    if (s == NULL || pixels == NULL || width == 0u || height == 0u || source_stride < width) return;
    struct graphics_rect rect = {x, y, (int32_t)width, (int32_t)height}, clipped;
    if (!intersect(rect, s->clip, &clipped)) return;
    for (int32_t py = clipped.y; py < clipped.y + clipped.height; ++py)
        for (int32_t px = clipped.x; px < clipped.x + clipped.width; ++px) {
            uint32_t src = pixels[(size_t)(py - y) * source_stride + (size_t)(px - x)];
            uint32_t *dst = &s->pixels[(size_t)py * s->stride + (size_t)px]; *dst = blend(*dst, src);
        }
    damage(s, clipped);
}

struct glyph { char value; uint8_t rows[7]; };
static const struct glyph font[] = {
 {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}}, {':',{0,12,12,0,12,12,0}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}}, {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}}, {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{28,18,17,17,17,18,28}}, {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}}
};
static const uint8_t *rows_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) if (font[i].value == c) return font[i].rows;
    return font[0].rows;
}
void graphics_text(struct graphics_surface *s, int32_t x, int32_t y,
                   const char *text, uint32_t scale, uint32_t argb) {
    if (s == NULL || text == NULL || scale == 0u || scale > 8u) return;
    for (int32_t cursor = x; *text != '\0'; cursor += (int32_t)(6u * scale)) {
        const uint8_t *rows = rows_for(*text++);
        for (uint32_t gy = 0; gy < 7u; ++gy) for (uint32_t gx = 0; gx < 5u; ++gx)
            if ((rows[gy] & (1u << (4u - gx))) != 0u)
                graphics_fill_rect(s, (struct graphics_rect){cursor + (int32_t)(gx * scale),
                    y + (int32_t)(gy * scale), (int32_t)scale, (int32_t)scale}, argb);
    }
}

void graphics_cursor(struct graphics_surface *s, int32_t x, int32_t y,
                     enum graphics_cursor_icon icon) {
    const uint32_t shadow = 0x78000000u, edge = 0xFF111827u, fill = 0xFFF8FAFCu;
    if (icon == GRAPHICS_CURSOR_ARROW) {
        for (int32_t row = 0; row < 14; ++row) {
            int32_t width = row / 2 + 1;
            graphics_fill_rect(s, (struct graphics_rect){x + 2, y + row + 2, width, 1}, shadow);
            graphics_pixel(s, x, y + row, edge);
            graphics_pixel(s, x + width - 1, y + row, edge);
            if (width > 2) graphics_fill_rect(s, (struct graphics_rect){x + 1, y + row, width - 2, 1}, fill);
        }
        graphics_rounded_border(s, (struct graphics_rect){x + 4, y + 10, 5, 9}, 1u, 1u, edge, fill);
    } else if (icon == GRAPHICS_CURSOR_HAND) {
        graphics_shadow(s, (struct graphics_rect){x + 3, y + 6, 11, 11}, 3u, 2u, shadow);
        graphics_rounded_border(s, (struct graphics_rect){x + 3, y + 6, 11, 11}, 3u, 1u, edge, fill);
        for (int32_t finger = 0; finger < 4; ++finger)
            graphics_rounded_border(s, (struct graphics_rect){x + 3 + finger * 3, y + 1 + (finger != 0), 3, 9},
                                    1u, 1u, edge, fill);
    } else if (icon == GRAPHICS_CURSOR_TEXT) {
        graphics_fill_rect(s, (struct graphics_rect){x + 6, y + 1, 3, 18}, shadow);
        graphics_fill_rect(s, (struct graphics_rect){x + 4, y, 7, 2}, edge);
        graphics_fill_rect(s, (struct graphics_rect){x + 7, y + 1, 1, 18}, fill);
        graphics_fill_rect(s, (struct graphics_rect){x + 4, y + 18, 7, 2}, edge);
    } else {
        graphics_line(s, x + 2, y + 16, x + 15, y + 3, edge);
        graphics_line(s, x + 3, y + 16, x + 16, y + 3, fill);
        graphics_line(s, x + 2, y + 16, x + 2, y + 10, edge);
        graphics_line(s, x + 2, y + 16, x + 8, y + 16, edge);
        graphics_line(s, x + 16, y + 3, x + 10, y + 3, edge);
        graphics_line(s, x + 16, y + 3, x + 16, y + 9, edge);
    }
}

bool graphics_self_test(void) {
    uint32_t pixels[16u * 16u];
    struct graphics_surface s;
    if (!graphics_surface_init(&s, pixels, 16u, 16u, 16u)) return false;
    graphics_clear(&s, 0xFF000000u);
    if (!graphics_set_clip(&s, (struct graphics_rect){2, 2, 12, 12})) return false;
    graphics_fill_rect(&s, (struct graphics_rect){-4, -4, 8, 8}, 0xFFFFFFFFu);
    if (pixels[0] != 0xFF000000u || pixels[2u * 16u + 2u] != 0xFFFFFFFFu) return false;
    graphics_reset_clip(&s); graphics_reset_damage(&s);
    graphics_rounded_rect(&s, (struct graphics_rect){4, 4, 8, 8}, 3u, 0xFF00FF00u);
    struct graphics_rect d;
    if (!graphics_get_damage(&s, &d) || d.x != 4 || d.y != 4 || d.width != 8 || d.height != 8) return false;
    uint32_t before = pixels[8u * 16u + 8u];
    graphics_pixel(&s, 8, 8, 0x80FF0000u);
    return before == 0xFF00FF00u && pixels[8u * 16u + 8u] != before;
}

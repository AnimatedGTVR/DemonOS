/*
sdl_demonos.c -- backing store for the minimal SDL 1.2 subset declared in
SDL/SDL.h. Implements exactly what graphics/nxsurface.cpp calls: 8bpp
indexed and truecolor RGBA/RGB surfaces, blit/fill/colorkey/palette,
format conversion, and real presentation (SDL_Flip) via demon_display_submit,
the same path vid_demonos.c uses for Quake.
*/

#include "SDL/SDL.h"

#include <demon/c_app.h>
#include <demon/portkit.h>

#include <string.h>

#define NX_DISPLAY_BPP 32u

static uint8_t mask_shift(uint32_t mask) {
    uint8_t shift = 0u;
    if (mask == 0u) return 0u;
    while ((mask & 1u) == 0u) { mask >>= 1; ++shift; }
    return shift;
}

static SDL_PixelFormat *format_create(int bpp, uint32_t rmask, uint32_t gmask,
                                      uint32_t bmask, uint32_t amask) {
    SDL_PixelFormat *format = demon_port_malloc(sizeof(*format));
    if (format == NULL) return NULL;
    memset(format, 0, sizeof(*format));
    format->BitsPerPixel = (uint8_t)bpp;
    format->BytesPerPixel = (uint8_t)((bpp + 7) / 8);
    format->Rmask = rmask;
    format->Gmask = gmask;
    format->Bmask = bmask;
    format->Amask = amask;
    format->Rshift = mask_shift(rmask);
    format->Gshift = mask_shift(gmask);
    format->Bshift = mask_shift(bmask);
    format->Ashift = mask_shift(amask);
    if (bpp <= 8) {
        SDL_Palette *palette = demon_port_malloc(sizeof(*palette));
        if (palette == NULL) {
            demon_port_free(format);
            return NULL;
        }
        palette->ncolors = 256;
        palette->colors = demon_port_malloc(256u * sizeof(SDL_Color));
        if (palette->colors == NULL) {
            demon_port_free(palette);
            demon_port_free(format);
            return NULL;
        }
        memset(palette->colors, 0, 256u * sizeof(SDL_Color));
        format->palette = palette;
    }
    return format;
}

static void format_free(SDL_PixelFormat *format) {
    if (format == NULL) return;
    if (format->palette != NULL) {
        demon_port_free(format->palette->colors);
        demon_port_free(format->palette);
    }
    demon_port_free(format);
}

SDL_Surface *SDL_CreateRGBSurface(uint32_t flags, int width, int height,
                                   int bpp, uint32_t rmask, uint32_t gmask,
                                   uint32_t bmask, uint32_t amask) {
    SDL_Surface *surface;

    if (width <= 0 || height <= 0 || bpp <= 0) return NULL;

    surface = demon_port_malloc(sizeof(*surface));
    if (surface == NULL) return NULL;
    memset(surface, 0, sizeof(*surface));

    surface->format = format_create(bpp, rmask, gmask, bmask, amask);
    if (surface->format == NULL) {
        demon_port_free(surface);
        return NULL;
    }

    surface->flags = flags;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * surface->format->BytesPerPixel;
    surface->clip_rect.x = 0;
    surface->clip_rect.y = 0;
    surface->clip_rect.w = width;
    surface->clip_rect.h = height;
    surface->refcount = 1;

    surface->pixels = demon_port_malloc((size_t)surface->pitch * (size_t)height);
    if (surface->pixels == NULL) {
        format_free(surface->format);
        demon_port_free(surface);
        return NULL;
    }
    memset(surface->pixels, 0, (size_t)surface->pitch * (size_t)height);

    return surface;
}

void SDL_FreeSurface(SDL_Surface *surface) {
    if (surface == NULL) return;
    format_free(surface->format);
    demon_port_free(surface->pixels);
    demon_port_free(surface);
}

static int clamp_rect_to_surface(SDL_Rect *r, const SDL_Surface *surface) {
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x + r->w > surface->w) r->w = surface->w - r->x;
    if (r->y + r->h > surface->h) r->h = surface->h - r->y;
    return r->w > 0 && r->h > 0;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, uint32_t color) {
    SDL_Rect area;
    int bpp;
    int y;

    if (dst == NULL) return -1;
    if (rect != NULL) area = *rect;
    else { area.x = 0; area.y = 0; area.w = dst->w; area.h = dst->h; }

    if (!clamp_rect_to_surface(&area, dst)) return 0;

    bpp = dst->format->BytesPerPixel;
    for (y = area.y; y < area.y + area.h; ++y) {
        uint8_t *row = (uint8_t *)dst->pixels + (size_t)y * dst->pitch +
                       (size_t)area.x * bpp;
        int x;
        for (x = 0; x < area.w; ++x) {
            memcpy(row + (size_t)x * bpp, &color, (size_t)bpp);
        }
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
                     SDL_Surface *dst, SDL_Rect *dstrect) {
    SDL_Rect sr, dr;
    int bpp;
    int y;
    int has_key = (src->flags & SDL_SRCCOLORKEY) != 0u;

    if (src == NULL || dst == NULL) return -1;
    if (src->format->BytesPerPixel != dst->format->BytesPerPixel) return -1;

    sr = (srcrect != NULL) ? *srcrect
                           : (SDL_Rect){0, 0, src->w, src->h};
    dr.x = (dstrect != NULL) ? dstrect->x : 0;
    dr.y = (dstrect != NULL) ? dstrect->y : 0;
    dr.w = sr.w;
    dr.h = sr.h;

    if (dr.x < dst->clip_rect.x) {
        int shift = dst->clip_rect.x - dr.x;
        dr.x += shift; sr.x += shift; dr.w -= shift;
    }
    if (dr.y < dst->clip_rect.y) {
        int shift = dst->clip_rect.y - dr.y;
        dr.y += shift; sr.y += shift; dr.h -= shift;
    }
    if (dr.x + dr.w > dst->clip_rect.x + dst->clip_rect.w)
        dr.w = dst->clip_rect.x + dst->clip_rect.w - dr.x;
    if (dr.y + dr.h > dst->clip_rect.y + dst->clip_rect.h)
        dr.h = dst->clip_rect.y + dst->clip_rect.h - dr.y;
    if (!clamp_rect_to_surface(&dr, dst)) return 0;
    if (dr.w <= 0 || dr.h <= 0) return 0;

    bpp = dst->format->BytesPerPixel;
    for (y = 0; y < dr.h; ++y) {
        const uint8_t *srow = (const uint8_t *)src->pixels +
                               (size_t)(sr.y + y) * src->pitch +
                               (size_t)sr.x * bpp;
        uint8_t *drow = (uint8_t *)dst->pixels +
                         (size_t)(dr.y + y) * dst->pitch +
                         (size_t)dr.x * bpp;
        if (!has_key) {
            memcpy(drow, srow, (size_t)dr.w * bpp);
        } else {
            int x;
            for (x = 0; x < dr.w; ++x) {
                uint32_t pixel = 0;
                memcpy(&pixel, srow + (size_t)x * bpp, (size_t)bpp);
                if (pixel != src->colorkey)
                    memcpy(drow + (size_t)x * bpp, &pixel, (size_t)bpp);
            }
        }
    }
    return 0;
}

int SDL_SetColorKey(SDL_Surface *surface, uint32_t flag, uint32_t key) {
    if (surface == NULL) return -1;
    if (flag & SDL_SRCCOLORKEY) {
        surface->flags |= SDL_SRCCOLORKEY;
        surface->colorkey = key;
    } else {
        surface->flags &= ~SDL_SRCCOLORKEY;
    }
    return 0;
}

int SDL_SetAlpha(SDL_Surface *surface, uint32_t flag, uint8_t alpha) {
    if (surface == NULL) return -1;
    (void)alpha;
    if (flag & SDL_SRCALPHA) {
        surface->flags |= SDL_SRCALPHA;
    } else {
        surface->flags &= ~SDL_SRCALPHA;
    }
    return 0;
}

/* SDL_ttf stubs -- never actually called (SCALE == 1 always selects the
   bitmap-font path in font.cpp's font_init), but real symbols are needed
   to link font_init/NXFont::InitChars/InitCharsShadowed since those are
   each a single compiled function spanning both branches. See
   SDL/SDL_ttf.h. */
int TTF_Init(void) { return -1; }
void TTF_Quit(void) {}
const char *TTF_GetError(void) { return "SDL_ttf is not implemented in this port"; }
struct TTF_Font *TTF_OpenFont(const char *file, int ptsize) {
    (void)file; (void)ptsize;
    return NULL;
}
void TTF_CloseFont(struct TTF_Font *font) { (void)font; }
SDL_Surface *TTF_RenderUTF8_Solid(struct TTF_Font *font, const char *text, SDL_Color fg) {
    (void)font; (void)text; (void)fg;
    return NULL;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
                   int firstcolor, int ncolors) {
    int i;
    if (surface == NULL || surface->format->palette == NULL) return 0;
    if (firstcolor < 0 || ncolors < 0 ||
        firstcolor + ncolors > surface->format->palette->ncolors)
        return 0;
    for (i = 0; i < ncolors; ++i)
        surface->format->palette->colors[firstcolor + i] = colors[i];
    return 1;
}

int SDL_SetClipRect(SDL_Surface *surface, SDL_Rect *rect) {
    if (surface == NULL) return -1;
    if (rect == NULL) {
        surface->clip_rect.x = 0;
        surface->clip_rect.y = 0;
        surface->clip_rect.w = surface->w;
        surface->clip_rect.h = surface->h;
    } else {
        surface->clip_rect = *rect;
    }
    return 1;
}

uint32_t SDL_MapRGB(SDL_PixelFormat *format, uint8_t r, uint8_t g, uint8_t b) {
    if (format->palette != NULL) {
        int i;
        for (i = 0; i < format->palette->ncolors; ++i) {
            SDL_Color *c = &format->palette->colors[i];
            if (c->r == r && c->g == g && c->b == b) return (uint32_t)i;
        }
        return 0;
    }
    return ((uint32_t)r << format->Rshift) |
           ((uint32_t)g << format->Gshift) |
           ((uint32_t)b << format->Bshift);
}

void SDL_GetRGB(uint32_t pixel, SDL_PixelFormat *format,
                uint8_t *r, uint8_t *g, uint8_t *b) {
    if (format->palette != NULL &&
        pixel < (uint32_t)format->palette->ncolors) {
        SDL_Color *c = &format->palette->colors[pixel];
        *r = c->r; *g = c->g; *b = c->b;
        return;
    }
    *r = (uint8_t)((pixel & format->Rmask) >> format->Rshift);
    *g = (uint8_t)((pixel & format->Gmask) >> format->Gshift);
    *b = (uint8_t)((pixel & format->Bmask) >> format->Bshift);
}

SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface) {
    SDL_Surface *converted;
    int x, y;

    if (surface == NULL) return NULL;
    if (surface->format->BitsPerPixel == NX_DISPLAY_BPP) {
        SDL_Surface *copy = SDL_CreateRGBSurface(surface->flags, surface->w,
                                                  surface->h, NX_DISPLAY_BPP,
                                                  surface->format->Rmask,
                                                  surface->format->Gmask,
                                                  surface->format->Bmask,
                                                  surface->format->Amask);
        if (copy != NULL)
            memcpy(copy->pixels, surface->pixels,
                   (size_t)surface->pitch * (size_t)surface->h);
        return copy;
    }

    converted = SDL_CreateRGBSurface(surface->flags, surface->w, surface->h,
                                      NX_DISPLAY_BPP,
                                      0x00FF0000u, 0x0000FF00u,
                                      0x000000FFu, 0xFF000000u);
    if (converted == NULL) return NULL;
    converted->format->Rshift = 16u;
    converted->format->Gshift = 8u;
    converted->format->Bshift = 0u;
    converted->format->Ashift = 24u;

    for (y = 0; y < surface->h; ++y) {
        const uint8_t *srow = (const uint8_t *)surface->pixels +
                               (size_t)y * surface->pitch;
        uint32_t *drow = (uint32_t *)((uint8_t *)converted->pixels +
                                       (size_t)y * converted->pitch);
        for (x = 0; x < surface->w; ++x) {
            uint32_t pixel = 0;
            uint8_t r, g, b;
            memcpy(&pixel, srow + (size_t)x * surface->format->BytesPerPixel,
                   (size_t)surface->format->BytesPerPixel);
            SDL_GetRGB(pixel, surface->format, &r, &g, &b);
            drow[x] = 0xFF000000u | ((uint32_t)r << 16) |
                      ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return converted;
}

/* Real presentation, same path vid_demonos.c uses for Quake: open the
   display/surface services once, expand whatever bpp the given surface
   is into ARGB via the existing SDL_GetRGB path, submit it centered on
   the real framebuffer. Lazily initialized on the first Flip call since
   nothing upstream calls a dedicated "open the display" entry point the
   way core_main.c does for Quake -- this port has no such call yet. */
struct nx_display_info {
    uint64_t width;
    uint64_t height;
    uint64_t stride_pixels;
    uint64_t format;
    uint64_t max_transfer_pixels;
};

struct nx_display_submit {
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
    uint64_t pixels;
    uint64_t flags;
};

static uint64_t nx_display = UINT64_MAX;
static uint64_t nx_surface_factory = UINT64_MAX;
static uint64_t nx_surface = UINT64_MAX;
static struct nx_display_info nx_info;
static uint32_t *nx_argb;
static int nx_argb_w, nx_argb_h;

int SDL_Flip(SDL_Surface *screen) {
    if (screen == NULL) return -1;

    if (nx_display == UINT64_MAX) {
        nx_display = demon_service_open(7u);
        nx_surface_factory = demon_service_open(9u);
        if (nx_display == UINT64_MAX || nx_surface_factory == UINT64_MAX)
            return -1;
        /* demon_display_info's out-param write can fail for large-app
           processes in general (see the comment on user_write_range in
           src/arch/x86_64/userspace.c), but confirmed via a real runtime
           check that it succeeds here and reports the real screen size
           (e.g. 640x480) -- bigger than this port's fixed 320x240 game
           canvas. Our own submit below only ever writes the centered
           320x240 rect, so the surrounding border was never touched and
           kept showing whatever the text console had drawn there before
           this process took the display -- visible as leftover, much
           larger console-font glyphs around the game's own small font.
           Fix: paint the *real* full screen black once, up front, before
           ever submitting the smaller centered game image. */
        /* A one-shot allocation for the real full screen (e.g. 640x480x4
           bytes ~= 1.2MB) risked silently failing against this port's
           4MB dynamic arena, which by this point already holds sprites/
           tiles/textbox state -- demon_port_malloc returning NULL would
           have skipped this whole blank without any error, leaving the
           border never actually cleared (every real per-frame Flip only
           ever touches the smaller centered game rect). display_submit
           (src/display.c) reads directly from request.pixels/width/
           height/x/y -- it has no dependency on demon_surface_create/
           write at all (those exist for a separate surface-sharing
           path) -- so the real fix is simply to call it multiple times,
           each covering one horizontal strip of the real screen, off a
           small fixed stack buffer. No arena allocation needed at all. */
        if (demon_display_info(nx_display, &nx_info) != 0u &&
            nx_info.width > 0u && nx_info.height > 0u &&
            nx_info.width <= 1024u) {
            /* This port's user stack is 128 pages (512KB total, see
               USERSPACE_STACK_PAGES in include/kernel/userspace.h) --
               keep this comfortably small relative to that regardless
               of the real screen's width, rather than sizing for a
               worst-case wide display. */
            enum { BLANK_CHUNK_ROWS = 4u };
            uint32_t blank_chunk[1024u * BLANK_CHUNK_ROWS];
            uint64_t chunk_pixels = nx_info.width * (uint64_t)BLANK_CHUNK_ROWS;
            for (uint64_t i = 0u; i < chunk_pixels; ++i) blank_chunk[i] = 0xFF000000u;
            uint64_t y = 0u;
            while (y < nx_info.height) {
                uint64_t rows = nx_info.height - y;
                if (rows > BLANK_CHUNK_ROWS) rows = BLANK_CHUNK_ROWS;
                struct nx_display_submit blank_request;
                blank_request.x = 0u;
                blank_request.y = y;
                blank_request.width = nx_info.width;
                blank_request.height = rows;
                blank_request.pixels = (uint64_t)(uintptr_t)blank_chunk;
                blank_request.flags = (y + rows >= nx_info.height) ? 1u : 0u;
                if (demon_display_submit(nx_display, &blank_request) != 0u) break;
                y += rows;
            }
        }
        nx_surface = demon_surface_create(nx_surface_factory,
                                          (uint64_t)screen->w, (uint64_t)screen->h);
        if (nx_surface == UINT64_MAX) return -1;
    }

    if (nx_argb == NULL || nx_argb_w != screen->w || nx_argb_h != screen->h) {
        demon_port_free(nx_argb);
        nx_argb = demon_port_malloc((size_t)screen->w * (size_t)screen->h * sizeof(uint32_t));
        if (nx_argb == NULL) return -1;
        nx_argb_w = screen->w;
        nx_argb_h = screen->h;
    }

    {
        int bpp = screen->format->BytesPerPixel;
        int y;
        for (y = 0; y < screen->h; ++y) {
            const uint8_t *srow = (const uint8_t *)screen->pixels + (size_t)y * screen->pitch;
            uint32_t *drow = nx_argb + (size_t)y * screen->w;
            int x;
            for (x = 0; x < screen->w; ++x) {
                uint32_t pixel = 0;
                uint8_t r, g, b;
                memcpy(&pixel, srow + (size_t)x * bpp, (size_t)bpp);
                SDL_GetRGB(pixel, screen->format, &r, &g, &b);
                drow[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    if (demon_surface_write(nx_surface, nx_argb,
                             (uint64_t)screen->w * (uint64_t)screen->h, 0u) !=
        (uint64_t)screen->w * (uint64_t)screen->h)
        return -1;
    (void)demon_surface_damage(nx_surface, 0u, 0u, (uint64_t)screen->w, (uint64_t)screen->h);

    {
        struct nx_display_submit request;
        request.x = nx_info.width > (uint64_t)screen->w ? (nx_info.width - (uint64_t)screen->w) / 2u : 0u;
        request.y = nx_info.height > (uint64_t)screen->h ? (nx_info.height - (uint64_t)screen->h) / 2u : 0u;
        request.width = (uint64_t)screen->w;
        request.height = (uint64_t)screen->h;
        request.pixels = (uint64_t)(uintptr_t)nx_argb;
        request.flags = 1u;
        if (demon_display_submit(nx_display, &request) != 0u) return -1;
    }

    return 0;
}

/* Cave Story's .pbm assets are real Windows BMP files (BM magic) despite
   the extension, per nxsurface.cpp's LoadImage calling SDL_LoadBMP
   directly. Every asset in the fetched data set (see docs/nxengine-port.md)
   is an uncompressed (BI_RGB), 1/4/8-bit indexed bitmap -- so that's the
   only decode path implemented; anything else fails cleanly instead of
   misreading memory.

   1bpp/4bpp source files are always unpacked into an 8bpp destination
   surface (same palette, one byte per pixel), never a native 1bpp/4bpp
   SDL_Surface. This isn't just a simplification: NXEngine's own
   Scale()/Scale8 (graphics/nxsurface.cpp, real and unmodified) only
   implements an 8bpp scaling path -- "all the .pbm files are 8bpp" per
   upstream's own comment -- yet Cave Story's real, original tileset
   assets (checked against studiopixel.jp's own release, not just the
   fetched fan mirror) are genuinely 1bpp/4bpp BMPs. Real SDL 1.2 is
   understood to upconvert sub-8bpp BMPs to 8bpp on load (a common
   real-world SDL_LoadBMP implementation detail for exactly this reason);
   matching that here is what makes Tileset::Load's real, unmodified code
   path actually succeed on real tileset data, instead of preserving a
   literal-but-impractical bit depth no real player would ever have hit. */
#define BMP_HEADER_BYTES 54u

SDL_Surface *SDL_LoadBMP(const char *file) {
    struct demon_port_file handle;
    uint8_t header[BMP_HEADER_BYTES];
    uint32_t pixel_offset, dib_size, compression, colors_used;
    int32_t width, height;
    uint16_t bpp;
    int flip;
    SDL_Surface *surface;
    uint8_t *file_buffer;
    size_t row_bytes_src;

    if (file == NULL || !demon_port_open(&handle, file)) return NULL;
    if (handle.size < BMP_HEADER_BYTES ||
        demon_port_read(&handle, header, BMP_HEADER_BYTES) != BMP_HEADER_BYTES) {
        demon_port_close(&handle);
        return NULL;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        demon_port_close(&handle);
        return NULL;
    }

    pixel_offset = header[10] | (header[11] << 8) | (header[12] << 16) |
                   ((uint32_t)header[13] << 24);
    dib_size = header[14] | (header[15] << 8) | (header[16] << 16) |
               ((uint32_t)header[17] << 24);
    width = (int32_t)(header[18] | (header[19] << 8) | (header[20] << 16) |
                       ((uint32_t)header[21] << 24));
    height = (int32_t)(header[22] | (header[23] << 8) | (header[24] << 16) |
                        ((uint32_t)header[25] << 24));
    bpp = (uint16_t)(header[28] | (header[29] << 8));
    compression = header[30] | (header[31] << 8) | (header[32] << 16) |
                  ((uint32_t)header[33] << 24);
    colors_used = header[46] | (header[47] << 8) | (header[48] << 16) |
                  ((uint32_t)header[49] << 24);

    flip = height > 0;
    if (height < 0) height = -height;

    if (dib_size < 40u || compression != 0u || width <= 0 || height <= 0 ||
        (bpp != 1u && bpp != 4u && bpp != 8u) ||
        pixel_offset < BMP_HEADER_BYTES || pixel_offset >= handle.size) {
        demon_port_close(&handle);
        return NULL;
    }

    {
        uint32_t palette_colors = colors_used != 0u ? colors_used : (1u << bpp);
        uint32_t palette_bytes = palette_colors * 4u;
        uint32_t palette_offset = BMP_HEADER_BYTES;
        uint8_t *palette_raw;

        if (palette_colors > 256u || palette_offset + palette_bytes > pixel_offset) {
            demon_port_close(&handle);
            return NULL;
        }

        surface = SDL_CreateRGBSurface(0u, width, height, 8, 0, 0, 0, 0);
        if (surface == NULL) {
            demon_port_close(&handle);
            return NULL;
        }

        palette_raw = demon_port_malloc(palette_bytes);
        if (palette_raw == NULL ||
            !demon_port_seek(&handle, palette_offset) ||
            demon_port_read(&handle, palette_raw, palette_bytes) != palette_bytes) {
            demon_port_free(palette_raw);
            SDL_FreeSurface(surface);
            demon_port_close(&handle);
            return NULL;
        }
        {
            uint32_t i;
            for (i = 0u; i < palette_colors && i < 256u; ++i) {
                /* BMP palette entries are stored BGRX. */
                surface->format->palette->colors[i].b = palette_raw[i * 4u + 0u];
                surface->format->palette->colors[i].g = palette_raw[i * 4u + 1u];
                surface->format->palette->colors[i].r = palette_raw[i * 4u + 2u];
            }
        }
        demon_port_free(palette_raw);
    }

    row_bytes_src = (((size_t)width * bpp + 31u) / 32u) * 4u;
    file_buffer = demon_port_malloc(row_bytes_src);
    if (file_buffer == NULL || !demon_port_seek(&handle, pixel_offset)) {
        demon_port_free(file_buffer);
        SDL_FreeSurface(surface);
        demon_port_close(&handle);
        return NULL;
    }

    {
        int y;
        for (y = 0; y < height; ++y) {
            int dest_y = flip ? (height - 1 - y) : y;
            uint8_t *dest_row = (uint8_t *)surface->pixels + (size_t)dest_y * surface->pitch;

            if (demon_port_read(&handle, file_buffer, row_bytes_src) != row_bytes_src) {
                demon_port_free(file_buffer);
                SDL_FreeSurface(surface);
                demon_port_close(&handle);
                return NULL;
            }

            if (bpp == 8u) {
                memcpy(dest_row, file_buffer, (size_t)width);
            } else if (bpp == 4u) {
                int x;
                for (x = 0; x < width; ++x) {
                    uint8_t byte = file_buffer[x / 2];
                    dest_row[x] = (x & 1) == 0 ? (byte >> 4) : (byte & 0x0Fu);
                }
            } else { /* bpp == 1 */
                int x;
                for (x = 0; x < width; ++x) {
                    uint8_t byte = file_buffer[x / 8];
                    dest_row[x] = (byte >> (7 - (x % 8))) & 1u;
                }
            }
        }
    }

    demon_port_free(file_buffer);
    demon_port_close(&handle);
    return surface;
}

/* input.cpp only ever reads .type and .key.keysym.sym, so that's all this
   translates. Same shape as the scancode->keynum translation just fixed in
   Quake's Sys_SendKeyEvents: special keys by raw scancode, otherwise the
   already-translated lowercase ASCII in event->value, with a released-key
   fallback table for key-up (which carries no value in the unified ABI). */
static int translate_special(uint16_t code) {
    switch (code) {
        case 0x01u: return SDLK_ESCAPE;
        case 0x0Eu: return SDLK_BACKSPACE;
        case 0x0Fu: return SDLK_TAB;
        case 0x1Cu: return SDLK_RETURN;
        case 0x39u: return SDLK_SPACE;
        case 0x2Au: return SDLK_LSHIFT;
        case 0x36u: return SDLK_RSHIFT;
        case 0x1Du: case 0x9Du: return SDLK_LCTRL;
        case 0x3Bu: return SDLK_F1;
        case 0x3Cu: return SDLK_F2;
        case 0x3Du: return SDLK_F3;
        case 0x3Eu: return SDLK_F4;
        case 0x3Fu: return SDLK_F5;
        case 0x40u: return SDLK_F6;
        case 0x41u: return SDLK_F7;
        case 0x42u: return SDLK_F8;
        case 0x43u: return SDLK_F9;
        case 0x44u: return SDLK_F10;
        case 0x57u: return SDLK_F11;
        case 0x58u: return SDLK_F12;
        case INPUT_KEY_ARROW_UP: return SDLK_UP;
        case INPUT_KEY_ARROW_DOWN: return SDLK_DOWN;
        case INPUT_KEY_ARROW_LEFT: return SDLK_LEFT;
        case INPUT_KEY_ARROW_RIGHT: return SDLK_RIGHT;
        default: return 0;
    }
}

static int translate_released(uint16_t code) {
    static const unsigned char released_ascii[0x3Au] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
        [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
        [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
        [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
        [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
        [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
        [0x29]='`',[0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',
        [0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
        [0x34]='.',[0x35]='/'
    };
    if (code < sizeof(released_ascii)) return released_ascii[code];
    return 0;
}

static int translate_sym(const struct input_event *event) {
    int special = translate_special(event->code);
    if (special != 0) return special;
    if (event->type == INPUT_KEY_DOWN) {
        if (event->value == '\n' || event->value == '\r') return SDLK_RETURN;
        if (event->value == '\b') return SDLK_BACKSPACE;
        if (event->value > 0 && event->value < 128) return event->value;
        return 0;
    }
    return translate_released(event->code);
}

/* main.cpp itself is not compiled by this port (see docs/nxengine-port.md
   -- it's the upstream entry point, tied to argv/atexit/Haiku paths and to
   game data this environment doesn't have yet); these exist for our own
   future core_main-equivalent entry point, which will want the same
   Init/GetTicks/Delay calls main.cpp does. */
int SDL_Init(uint32_t flags) {
    (void)flags;
    return 0;
}

void SDL_Quit(void) { }

const char *SDL_GetError(void) {
    return "";
}

uint32_t SDL_GetTicks(void) {
    return (uint32_t)demon_port_ticks_ms();
}

uint8_t SDL_GetAppState(void) {
    return (uint8_t)(SDL_APPACTIVE | SDL_APPINPUTFOCUS);
}

void SDL_PauseAudio(int pause_on) {
    (void)pause_on;
}

void SDL_Delay(uint32_t ms) {
    demon_port_sleep_ms(ms);
}

int SDL_PollEvent(SDL_Event *out) {
    struct input_event event;
    while (demon_port_poll_input(&event)) {
        if (event.type != INPUT_KEY_DOWN && event.type != INPUT_KEY_UP)
            continue;
        {
            int sym = translate_sym(&event);
            if (sym == 0) continue;
            out->type = (event.type == INPUT_KEY_DOWN) ? SDL_KEYDOWN : SDL_KEYUP;
            out->key.keysym.sym = sym;
            return 1;
        }
    }
    return 0;
}

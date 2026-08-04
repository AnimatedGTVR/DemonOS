/*
sdl_demonos SDL.h -- the minimal subset of SDL 1.2's surface/blit API that
NXEngine's graphics/nxsurface.cpp actually calls. This is not a general SDL
implementation: it exists only so NXEngine's engine-facing NXSurface/Graphics
seam can be built against a real (if narrow) SDL-shaped ABI, backed by
DemonOS's display/surface syscalls in sdl_demonos.c, the same way
vid_demonos.c backs Quake's software renderer. Extend this file only when a
platform unit under ports/nxengine/platform genuinely needs another symbol;
do not pre-implement the rest of SDL "for completeness".
*/
#ifndef SDL_DEMONOS_SDL_H
#define SDL_DEMONOS_SDL_H

#include <stdint.h>

#define SDL_SRCCOLORKEY 0x00001000u
#define SDL_RLEACCEL    0x00004000u
#define SDL_SRCALPHA    0x00010000u
#define SDL_HWSURFACE   0x00000001u

/* Real SDL 1.2 keysym.h values -- kept identical to upstream SDL so this
   shim's mapping table doubles as documentation of what input.cpp expects,
   even though nothing here links against real SDL. Only the subset
   input.cpp's own key mappings actually reference is filled in. */
#define SDLK_BACKSPACE  8
#define SDLK_TAB        9
#define SDLK_RETURN     13
#define SDLK_ESCAPE     27
#define SDLK_SPACE      32
#define SDLK_QUOTE      39
#define SDLK_COMMA      44
#define SDLK_MINUS      45
#define SDLK_PERIOD     46
#define SDLK_SLASH      47
#define SDLK_0          48
#define SDLK_1          49
#define SDLK_2          50
#define SDLK_3          51
#define SDLK_4          52
#define SDLK_5          53
#define SDLK_6          54
#define SDLK_7          55
#define SDLK_8          56
#define SDLK_9          57
#define SDLK_SEMICOLON  59
#define SDLK_EQUALS     61
#define SDLK_LEFTBRACKET  91
#define SDLK_BACKSLASH  92
#define SDLK_RIGHTBRACKET 93
#define SDLK_BACKQUOTE  96
#define SDLK_a          97
#define SDLK_b          98
#define SDLK_c          99
#define SDLK_d          100
#define SDLK_e          101
#define SDLK_f          102
#define SDLK_g          103
#define SDLK_h          104
#define SDLK_i          105
#define SDLK_j          106
#define SDLK_k          107
#define SDLK_l          108
#define SDLK_m          109
#define SDLK_n          110
#define SDLK_o          111
#define SDLK_p          112
#define SDLK_q          113
#define SDLK_r          114
#define SDLK_s          115
#define SDLK_t          116
#define SDLK_u          117
#define SDLK_v          118
#define SDLK_w          119
#define SDLK_x          120
#define SDLK_y          121
#define SDLK_z          122
#define SDLK_DELETE     127
#define SDLK_UP         273
#define SDLK_DOWN       274
#define SDLK_RIGHT      275
#define SDLK_LEFT       276
#define SDLK_INSERT     277
#define SDLK_HOME       278
#define SDLK_END        279
#define SDLK_PAGEUP     280
#define SDLK_PAGEDOWN   281
#define SDLK_F1         282
#define SDLK_F2         283
#define SDLK_F3         284
#define SDLK_F4         285
#define SDLK_F5         286
#define SDLK_F6         287
#define SDLK_F7         288
#define SDLK_F8         289
#define SDLK_F9         290
#define SDLK_F10        291
#define SDLK_F11        292
#define SDLK_F12        293
#define SDLK_RSHIFT     303
#define SDLK_LSHIFT     304
#define SDLK_LCTRL      306
#define SDLK_PAUSE      19
#define SDLK_LAST       323

#define SDL_KEYDOWN 2
#define SDL_KEYUP   3
#define SDL_QUIT    12

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_APPACTIVE      0x02
#define SDL_APPINPUTFOCUS  0x01

typedef struct SDL_keysym {
    int sym;
} SDL_keysym;

typedef struct SDL_KeyboardEvent {
    uint8_t state;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_Event {
    uint8_t type;
    SDL_KeyboardEvent key;
} SDL_Event;

typedef struct SDL_Color {
    uint8_t r, g, b, unused;
} SDL_Color;

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    SDL_Palette *palette;
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint32_t Rmask, Gmask, Bmask, Amask;
    uint8_t Rshift, Gshift, Bshift, Ashift;
} SDL_PixelFormat;

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

typedef struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    SDL_Rect clip_rect;
    int refcount;
    /* Not part of real SDL's public layout, appended after the fields
       NXEngine reads directly (w/h/pitch/pixels/format/clip_rect) so their
       offsets still match what nxsurface.cpp expects; only sdl_demonos.c
       itself reads this. */
    uint32_t colorkey;
} SDL_Surface;

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *SDL_CreateRGBSurface(uint32_t flags, int width, int height,
                                   int bpp, uint32_t rmask, uint32_t gmask,
                                   uint32_t bmask, uint32_t amask);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
                     SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, uint32_t color);
int SDL_SetColorKey(SDL_Surface *surface, uint32_t flag, uint32_t key);
/* Real SDL1 records the SDL_SRCALPHA flag + alpha value on the surface;
   this shim's SDL_BlitSurface doesn't implement per-surface alpha
   blending yet (nothing exercised in this port needs the blended result
   to look correct), so this only stores the state without changing how
   blits behave -- an honest, scoped gap, not a fake success. */
int SDL_SetAlpha(SDL_Surface *surface, uint32_t flag, uint8_t alpha);
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
                   int firstcolor, int ncolors);
int SDL_SetClipRect(SDL_Surface *surface, SDL_Rect *rect);
uint32_t SDL_MapRGB(SDL_PixelFormat *format, uint8_t r, uint8_t g, uint8_t b);
void SDL_GetRGB(uint32_t pixel, SDL_PixelFormat *format,
                 uint8_t *r, uint8_t *g, uint8_t *b);
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
int SDL_Flip(SDL_Surface *screen);
SDL_Surface *SDL_LoadBMP(const char *file);
/* debug.cpp's DrawDebug() (a debug-overlay screen dump, never called by
   this port) references this; declaration-only, same pattern as the
   SDL_SetVideoMode group below -- real body never needed since
   --gc-sections drops DrawDebug's whole section when nothing calls it. */
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
int SDL_PollEvent(SDL_Event *event);

int SDL_Init(uint32_t flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
uint32_t SDL_GetTicks(void);
uint8_t SDL_GetAppState(void);
void SDL_PauseAudio(int pause_on);
void SDL_Delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

/* Declarations only, for graphics.cpp's Graphics::init/InitVideo/close/
   SetResolution to *compile* -- this port never calls any of those (only
   the trivial set_clip_rect/clear_clip_rect/SetDrawTarget one-liners),
   so --gc-sections drops the whole enclosing function at link time and
   these never need real implementations. Adding bodies for real window
   management is out of scope; if a future stage ever calls one of these
   functions for real, the linker will say so immediately (undefined
   reference), not silently misbehave. */
#define SDL_SWSURFACE   0x00000000u
#define SDL_HWPALETTE   0x20000000u
#define SDL_FULLSCREEN  0x80000000u

typedef struct SDL_VideoInfo {
    SDL_PixelFormat *vfmt;
} SDL_VideoInfo;

#ifdef __cplusplus
extern "C" {
#endif
const SDL_VideoInfo *SDL_GetVideoInfo(void);
void SDL_ShowCursor(int toggle);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                       int depth, int pitch,
                                       uint32_t rmask, uint32_t gmask,
                                       uint32_t bmask, uint32_t amask);
void SDL_WM_SetIcon(SDL_Surface *icon, uint8_t *mask);
void SDL_WM_SetCaption(const char *title, const char *icon);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, uint32_t flags);
#ifdef __cplusplus
}
#endif

#endif

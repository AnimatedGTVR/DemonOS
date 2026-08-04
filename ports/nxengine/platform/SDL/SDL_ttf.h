/*
Declaration-only SDL_ttf shim. graphics/font.cpp's font_init() takes a
compile-time #ifdef CONFIG_ENABLE_TTF branch that is never runtime-reached
in this port (SCALE == 1 always selects the bitmap-font path in the
adjacent #if/#elif), but since font_init/NXFont::InitChars/
InitCharsShadowed are each a single compiled function containing both
branches, the linker still needs real symbols for the TTF calls in the
dead branch -- see the comment on the SDL_SetVideoMode group in SDL.h for
the same situation with graphics.cpp. These are never actually invoked;
implementing real TrueType rendering is out of scope for this port (there
is no font file, FreeType, or fontconfig equivalent here), so the .c
definitions in sdl_demonos.c are trivial failure stubs, not fakes of a
working TTF backend.
*/
#ifndef SDL_DEMONOS_SDL_TTF_H
#define SDL_DEMONOS_SDL_TTF_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TTF_Font TTF_Font;

int TTF_Init(void);
void TTF_Quit(void);
const char *TTF_GetError(void);
TTF_Font *TTF_OpenFont(const char *file, int ptsize);
void TTF_CloseFont(TTF_Font *font);
SDL_Surface *TTF_RenderUTF8_Solid(TTF_Font *font, const char *text, SDL_Color fg);

#ifdef __cplusplus
}
#endif

#endif

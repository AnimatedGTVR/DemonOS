#ifndef DEMONOS_BUTTERSCOTCH_SOFTWARE_RENDERER_H
#define DEMONOS_BUTTERSCOTCH_SOFTWARE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include "datawin_index.h"
#include "image_decode.h"

#define DEMON_RENDER_COMMAND_MAX 16u

typedef enum { DEMON_RENDER_CLEAR, DEMON_RENDER_TPAG } DemonRenderOpcode;
typedef struct {
    DemonRenderOpcode opcode;
    int32_t x, y;
    uint32_t color;
    DemonDataWinPageItem item;
} DemonRenderCommand;

typedef struct {
    DemonRenderCommand commands[DEMON_RENDER_COMMAND_MAX];
    uint32_t count;
} DemonRenderList;

bool DemonSoftwareRenderer_compose(const DemonDecodedImage *atlas,
                                   const DemonRenderList *list,
                                   uint32_t width, uint32_t height,
                                   DemonDecodedImage *frame);
bool DemonSoftwareRenderer_recompose(const DemonDecodedImage *atlas,
                                     const DemonRenderList *list,
                                     DemonDecodedImage *frame);
bool DemonSoftwareRenderer_overlap(const DemonRenderCommand *a,
                                   const DemonRenderCommand *b);

#endif

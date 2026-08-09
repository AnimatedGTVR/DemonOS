/* Small bounded command interpreter used while upstream's higher-level
   renderer is progressively connected to the native DemonOS surface. */

#include "software_renderer.h"

#include <demon/portkit.h>
#include <string.h>

static void clear(uint8_t *pixels, uint32_t count, uint32_t rgba) {
    const uint8_t r = (uint8_t)(rgba >> 24u), g = (uint8_t)(rgba >> 16u);
    const uint8_t b = (uint8_t)(rgba >> 8u), a = (uint8_t)rgba;
    for (uint32_t i = 0u; i < count; ++i) {
        pixels[i * 4u] = r; pixels[i * 4u + 1u] = g;
        pixels[i * 4u + 2u] = b; pixels[i * 4u + 3u] = a;
    }
}

static void blit(const DemonDecodedImage *atlas, uint8_t *destination,
                 uint32_t width, uint32_t height,
                 const DemonRenderCommand *command) {
    const DemonDataWinPageItem *item = &command->item;
    for (uint32_t dy = 0u; dy < item->targetHeight; ++dy) {
        const int32_t y = command->y + item->targetY + (int32_t)dy;
        if (y < 0 || (uint32_t)y >= height) continue;
        const uint32_t sy = item->sourceY +
            (dy * item->sourceHeight) / item->targetHeight;
        if (sy >= atlas->height) continue;
        for (uint32_t dx = 0u; dx < item->targetWidth; ++dx) {
            const int32_t x = command->x + item->targetX + (int32_t)dx;
            if (x < 0 || (uint32_t)x >= width) continue;
            const uint32_t sx = item->sourceX +
                (dx * item->sourceWidth) / item->targetWidth;
            if (sx >= atlas->width) continue;
            const uint8_t *source = atlas->pixels +
                ((uint64_t)sy * atlas->width + sx) * 4u;
            uint8_t *target = destination +
                ((uint64_t)(uint32_t)y * width + (uint32_t)x) * 4u;
            if (source[3] == 255u) memcpy(target, source, 4u);
            else if (source[3] != 0u) {
                const uint32_t alpha = source[3];
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                    target[channel] = (uint8_t)((source[channel] * alpha +
                        target[channel] * (255u - alpha)) / 255u);
                target[3] = 255u;
            }
        }
    }
}

bool DemonSoftwareRenderer_recompose(const DemonDecodedImage *atlas,
                                     const DemonRenderList *list,
                                     DemonDecodedImage *frame) {
    if (atlas == NULL || atlas->pixels == NULL || list == NULL ||
        frame == NULL || frame->pixels == NULL || frame->width == 0u ||
        frame->height == 0u ||
        list->count > DEMON_RENDER_COMMAND_MAX ||
        (uint64_t)frame->width * frame->height > UINT32_MAX / 4u ||
        frame->byteCount != frame->width * frame->height * 4u) return false;
    const uint32_t width = frame->width, height = frame->height;
    const uint32_t bytes = frame->byteCount;
    uint8_t *pixels = frame->pixels;
    clear(pixels, width * height, 0x111827ffu);
    for (uint32_t i = 0u; i < list->count; ++i) {
        if (list->commands[i].opcode == DEMON_RENDER_CLEAR)
            clear(pixels, width * height, list->commands[i].color);
        else if (list->commands[i].opcode == DEMON_RENDER_TPAG)
            blit(atlas, pixels, width, height, &list->commands[i]);
        else return false;
    }
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < bytes; ++i)
        hash = (hash ^ pixels[i]) * 16777619u;
    frame->fnv1a = hash;
    return true;
}

bool DemonSoftwareRenderer_compose(const DemonDecodedImage *atlas,
                                   const DemonRenderList *list,
                                   uint32_t width, uint32_t height,
                                   DemonDecodedImage *frame) {
    if (frame == NULL || width == 0u || height == 0u ||
        (uint64_t)width * height > UINT32_MAX / 4u) return false;
    const uint32_t bytes = width * height * 4u;
    uint8_t *pixels = demon_port_malloc(bytes);
    if (pixels == NULL) return false;
    *frame = (DemonDecodedImage){width, height, bytes, 0u, pixels};
    if (DemonSoftwareRenderer_recompose(atlas, list, frame)) return true;
    demon_port_free(pixels);
    memset(frame, 0, sizeof(*frame));
    return false;
}

bool DemonSoftwareRenderer_overlap(const DemonRenderCommand *a,
                                   const DemonRenderCommand *b) {
    if (a == NULL || b == NULL || a->opcode != DEMON_RENDER_TPAG ||
        b->opcode != DEMON_RENDER_TPAG) return false;
    const int64_t aLeft = (int64_t)a->x + a->item.targetX;
    const int64_t aTop = (int64_t)a->y + a->item.targetY;
    const int64_t bLeft = (int64_t)b->x + b->item.targetX;
    const int64_t bTop = (int64_t)b->y + b->item.targetY;
    const int64_t aRight = aLeft + a->item.targetWidth;
    const int64_t aBottom = aTop + a->item.targetHeight;
    const int64_t bRight = bLeft + b->item.targetWidth;
    const int64_t bBottom = bTop + b->item.targetHeight;
    return aLeft < bRight && aRight > bLeft &&
           aTop < bBottom && aBottom > bTop;
}

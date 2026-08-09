#ifndef DEMONOS_BUTTERSCOTCH_IMAGE_DECODE_H
#define DEMONOS_BUTTERSCOTCH_IMAGE_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t byteCount;
    uint32_t fnv1a;
    uint8_t *pixels;
} DemonDecodedImage;

bool DemonImage_decodePngRgba(const uint8_t *blob, size_t blobSize,
                              DemonDecodedImage *summary);
void DemonImage_release(DemonDecodedImage *image);

#endif

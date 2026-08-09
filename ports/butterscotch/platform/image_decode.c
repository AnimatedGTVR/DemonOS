/* Native adapter for the same stb_image decoder used by upstream
   Butterscotch. The implementation is compiled memory-only and PNG-only. */

#include "image_decode.h"

#include <limits.h>
#include <stdlib.h>

#include "stb_image.h"

bool DemonImage_decodePngRgba(const uint8_t *blob, size_t blobSize,
                              DemonDecodedImage *summary) {
    if (blob == NULL || summary == NULL || blobSize > INT_MAX) return false;
    int width = 0, height = 0, sourceChannels = 0;
    stbi_uc *pixels = stbi_load_from_memory(blob, (int)blobSize, &width,
                                            &height, &sourceChannels, 4);
    if (pixels == NULL || width <= 0 || height <= 0 ||
        (uint64_t)(uint32_t)width * (uint32_t)height > UINT32_MAX / 4u) {
        stbi_image_free(pixels);
        return false;
    }
    const uint32_t bytes = (uint32_t)width * (uint32_t)height * 4u;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < bytes; ++i)
        hash = (hash ^ pixels[i]) * 16777619u;
    summary->width = (uint32_t)width;
    summary->height = (uint32_t)height;
    summary->byteCount = bytes;
    summary->fnv1a = hash;
    summary->pixels = pixels;
    return true;
}

void DemonImage_release(DemonDecodedImage *image) {
    if (image == NULL) return;
    stbi_image_free(image->pixels);
    image->pixels = NULL;
}

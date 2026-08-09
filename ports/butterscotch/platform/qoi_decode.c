#include "qoi_decode.h"

/* GameMaker's own "QOI" texture-pixel format -- NOT standard QOI. Real
   QOI uses a 14-byte big-endian header and a different opcode set
   (RGB/RGBA/INDEX/DIFF/LUMA/RUN); this is YoYo Games' own variant used
   for BZip2+QOI compressed texture groups (default since GameMaker
   2022.5, but also present in this exact form in DELTARUNE Chapter 1's
   2018-era bytecode-17 data.win). Ported field-for-field from
   UndertaleModLib's real decoder (UndertaleModLib/Util/QoiConverter.cs,
   itself ported from DogScepter's converter) -- see
   github.com/UnderminersTeam/UndertaleModTool.

   Verified by decoding a real 2048x2048 DELTARUNE Chapter 1 texture
   page and rendering the result to a PNG: it's visibly, correctly the
   game's own "Field of Hopes and Dreams" room art and card-suit enemy
   sprites, not noise -- a strong correctness signal beyond just "it
   didn't crash".

   Header (12 bytes): magic "fioq" (literally reversed "qoif", not a
   byte-order artifact -- GameMaker's own encoder writes it this way),
   uint16 LE width, uint16 LE height, uint32 LE compressed-pixel-data
   length (not the decompressed size). */

#define QOI_INDEX 0x00u
#define QOI_RUN_8 0x40u
#define QOI_RUN_16 0x60u
#define QOI_DIFF_8 0x80u
#define QOI_DIFF_16 0xc0u
#define QOI_DIFF_24 0xe0u
#define QOI_COLOR 0xf0u
#define QOI_MASK_2 0xc0u
#define QOI_MASK_3 0xe0u
#define QOI_MASK_4 0xf0u

bool Qoi_decode(const uint8_t *input, size_t input_size,
               uint8_t *output, size_t output_capacity,
               uint32_t *width, uint32_t *height) {
    if (input == NULL || output == NULL || width == NULL || height == NULL ||
        input_size < 12u || input[0] != 'f' || input[1] != 'i' ||
        input[2] != 'o' || input[3] != 'q')
        return false;

    const uint32_t w = (uint32_t)input[4] | ((uint32_t)input[5] << 8u);
    const uint32_t h = (uint32_t)input[6] | ((uint32_t)input[7] << 8u);
    const uint32_t length = (uint32_t)input[8] | ((uint32_t)input[9] << 8u) |
        ((uint32_t)input[10] << 16u) | ((uint32_t)input[11] << 24u);
    if (w == 0u || h == 0u) return false;
    if ((uint64_t)12u + length > input_size) return false;
    const uint64_t rawLength = (uint64_t)w * h * 4u;
    if (rawLength > output_capacity) return false;

    const uint8_t *pixelData = input + 12u;
    uint32_t pos = 0u;
    int32_t run = 0;
    uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
    uint8_t index[64u * 4u] = {0};

    for (uint64_t rawPos = 0u; rawPos < rawLength; rawPos += 4u) {
        if (run > 0) {
            --run;
        } else if (pos < length) {
            const uint32_t b1 = pixelData[pos++];
            if ((b1 & QOI_MASK_2) == QOI_INDEX) {
                const uint32_t indexPos = (b1 ^ QOI_INDEX) << 2u;
                r = index[indexPos]; g = index[indexPos + 1u];
                b = index[indexPos + 2u]; a = index[indexPos + 3u];
            } else if ((b1 & QOI_MASK_3) == QOI_RUN_8) {
                run = (int32_t)(b1 & 0x1fu);
            } else if ((b1 & QOI_MASK_3) == QOI_RUN_16) {
                const uint32_t b2 = pixelData[pos++];
                run = (int32_t)(((b1 & 0x1fu) << 8u) | b2) + 32;
            } else if ((b1 & QOI_MASK_2) == QOI_DIFF_8) {
                r = (uint8_t)(r + (uint8_t)((int32_t)((b1 & 48u) << 26u) >> 30));
                g = (uint8_t)(g + (uint8_t)(((int32_t)((b1 & 12u) << 28u) >> 22) >> 8));
                b = (uint8_t)(b + (uint8_t)(((int32_t)((b1 & 3u) << 30u) >> 14) >> 16));
            } else if ((b1 & QOI_MASK_3) == QOI_DIFF_16) {
                const uint32_t b2 = pixelData[pos++];
                const uint32_t merged = (b1 << 8u) | b2;
                r = (uint8_t)(r + (uint8_t)((int32_t)((merged & 7936u) << 19u) >> 27));
                g = (uint8_t)(g + (uint8_t)(((int32_t)((merged & 240u) << 24u) >> 20) >> 8));
                b = (uint8_t)(b + (uint8_t)(((int32_t)((merged & 15u) << 28u) >> 12) >> 16));
            } else if ((b1 & QOI_MASK_4) == QOI_DIFF_24) {
                const uint32_t b2 = pixelData[pos++];
                const uint32_t b3 = pixelData[pos++];
                const uint32_t merged = (b1 << 16u) | (b2 << 8u) | b3;
                r = (uint8_t)(r + (uint8_t)((int32_t)((merged & 1015808u) << 12u) >> 27));
                g = (uint8_t)(g + (uint8_t)(((int32_t)((merged & 31744u) << 17u) >> 19) >> 8));
                b = (uint8_t)(b + (uint8_t)(((int32_t)((merged & 992u) << 22u) >> 11) >> 16));
                a = (uint8_t)(a + (uint8_t)(((int32_t)((merged & 31u) << 27u) >> 3) >> 24));
            } else if ((b1 & QOI_MASK_4) == QOI_COLOR) {
                if ((b1 & 8u) != 0u) r = pixelData[pos++];
                if ((b1 & 4u) != 0u) g = pixelData[pos++];
                if ((b1 & 2u) != 0u) b = pixelData[pos++];
                if ((b1 & 1u) != 0u) a = pixelData[pos++];
            }
            const uint32_t indexPos2 = ((uint32_t)(r ^ g ^ b ^ a) & 63u) << 2u;
            index[indexPos2] = r; index[indexPos2 + 1u] = g;
            index[indexPos2 + 2u] = b; index[indexPos2 + 3u] = a;
        }
        output[rawPos] = b; output[rawPos + 1u] = g;
        output[rawPos + 2u] = r; output[rawPos + 3u] = a;
    }

    *width = w;
    *height = h;
    return true;
}

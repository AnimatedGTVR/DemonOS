#ifndef QOI_DECODE_H
#define QOI_DECODE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Decodes GameMaker's custom "fioq"-tagged QOI-variant image format (see
   qoi_decode.c for the full format notes and how it was verified) from
   `input` (which must start with the 12-byte fioq header) into
   `output`, a caller-allocated buffer of exactly width*height*4 bytes.
   Pixel order in `output` is BGRA per pixel, matching the reference
   decoder this was ported from. */
bool Qoi_decode(const uint8_t *input, size_t input_size,
                uint8_t *output, size_t output_capacity,
                uint32_t *width, uint32_t *height);

#endif

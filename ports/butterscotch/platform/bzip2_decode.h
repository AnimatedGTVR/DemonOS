#ifndef BZIP2_DECODE_H
#define BZIP2_DECODE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Decompresses a full bzip2 stream (the "BZh" header through the final
   end-of-stream block) from `input` into `output`. `output_capacity` must
   be large enough for the whole decompressed result; on success
   `*output_size` is set to the actual decompressed length. No dynamic
   allocation beyond the caller-supplied `scratch` buffer (must be at
   least BZIP2_SCRATCH_BYTES(level) for the stream's block-size level,
   read from input[3]-'0'). */
bool Bzip2_decompress(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_capacity,
                      size_t *output_size,
                      uint8_t *scratch, size_t scratch_size);

/* Scratch bytes needed for a given bzip2 level (1-9, 100000 bytes/level
   block size): dominated by the inverse-BWT "tt" array (4 bytes/byte of
   block data) plus the block byte buffer itself. */
#define BZIP2_MAX_BLOCK_BYTES(level) ((size_t)(level) * 100000u)
#define BZIP2_SCRATCH_BYTES(level) \
    (BZIP2_MAX_BLOCK_BYTES(level) * 5u + 4096u)

#endif

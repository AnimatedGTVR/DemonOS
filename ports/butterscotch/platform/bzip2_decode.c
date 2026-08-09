#include "bzip2_decode.h"

#define BZ_MAX_GROUPS 6u
#define BZ_MAX_ALPHA_SIZE 258u
#define BZ_MAX_CODE_LEN 23u
#define BZ_RUNA 0u
#define BZ_RUNB 1u
#define BZ_N_GROUPS_SYMS 50u

struct bit_reader {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    unsigned bit_pos; /* 0..7, next bit to read within data[byte_pos], MSB first */
};

static uint32_t br_bits(struct bit_reader *br, unsigned count) {
    uint32_t value = 0u;
    for (unsigned i = 0u; i < count; ++i) {
        uint8_t bit = 0u;
        if (br->byte_pos < br->size)
            bit = (uint8_t)((br->data[br->byte_pos] >> (7u - br->bit_pos)) & 1u);
        value = (value << 1u) | bit;
        br->bit_pos++;
        if (br->bit_pos == 8u) { br->bit_pos = 0u; br->byte_pos++; }
    }
    return value;
}

struct huff_table {
    int32_t limit[BZ_MAX_CODE_LEN + 1u];
    int32_t base[BZ_MAX_CODE_LEN + 1u];
    uint16_t perm[BZ_MAX_ALPHA_SIZE];
    uint8_t minLen, maxLen;
};

static void build_huffman(struct huff_table *table, const uint8_t *lengths,
                          uint32_t alphaSize) {
    uint8_t minLen = BZ_MAX_CODE_LEN, maxLen = 0u;
    for (uint32_t i = 0u; i < alphaSize; ++i) {
        if (lengths[i] < minLen) minLen = lengths[i];
        if (lengths[i] > maxLen) maxLen = lengths[i];
    }
    table->minLen = minLen;
    table->maxLen = maxLen;

    uint32_t pp = 0u;
    for (uint32_t len = minLen; len <= maxLen; ++len)
        for (uint32_t i = 0u; i < alphaSize; ++i)
            if (lengths[i] == len) table->perm[pp++] = (uint16_t)i;

    int32_t count[BZ_MAX_CODE_LEN + 2u] = {0};
    for (uint32_t i = 0u; i < alphaSize; ++i) count[lengths[i] + 1u]++;
    for (uint32_t i = 1u; i <= BZ_MAX_CODE_LEN + 1u; ++i) count[i] += count[i - 1u];

    int32_t code = 0;
    for (uint32_t len = minLen; len <= maxLen; ++len) {
        const int32_t numCodesThisLen = count[len + 1u] - count[len];
        table->base[len] = code - count[len];
        code += numCodesThisLen;
        table->limit[len] = code - 1;
        code <<= 1;
    }
}

static int32_t huffman_decode(struct bit_reader *br, const struct huff_table *table) {
    uint32_t len = table->minLen;
    int32_t code = (int32_t)br_bits(br, len);
    for (;;) {
        if (len > table->maxLen) return -1;
        if (code <= table->limit[len]) {
            const int32_t index = code - table->base[len];
            if (index < 0 || (uint32_t)index >= BZ_MAX_ALPHA_SIZE) return -1;
            return table->perm[index];
        }
        code = (code << 1) | (int32_t)br_bits(br, 1u);
        ++len;
    }
}

/* Decodes exactly one bzip2 compressed block (the part after the block
   magic and CRC) into `block_out`, returning the block's byte count via
   *block_len. `scratch` must hold at least max_block_bytes*4 bytes for
   the inverse-BWT "tt" permutation array, immediately followed by
   max_block_bytes bytes for the intermediate MTF-decoded byte buffer;
   both are carved out of the caller's scratch region. */
static bool decode_block(struct bit_reader *br, uint32_t max_block_bytes,
                         uint8_t *scratch, uint8_t *final_out,
                         size_t final_capacity, size_t *final_len) {
    const uint32_t randomized = br_bits(br, 1u);
    if (randomized != 0u) return false; /* deprecated bzip2 feature, unsupported */
    const uint32_t origPtr = br_bits(br, 24u);

    uint8_t seqToUnseq[256];
    uint32_t nInUse = 0u;
    {
        bool groupUsed[16];
        for (uint32_t g = 0u; g < 16u; ++g) groupUsed[g] = br_bits(br, 1u) != 0u;
        for (uint32_t g = 0u; g < 16u; ++g) {
            if (!groupUsed[g]) continue;
            for (uint32_t b = 0u; b < 16u; ++b) {
                if (br_bits(br, 1u) != 0u) seqToUnseq[nInUse++] = (uint8_t)(g * 16u + b);
            }
        }
    }
    if (nInUse == 0u) return false;
    const uint32_t alphaSize = nInUse + 2u;

    const uint32_t nGroups = br_bits(br, 3u);
    const uint32_t nSelectors = br_bits(br, 15u);
    if (nGroups < 2u || nGroups > BZ_MAX_GROUPS || nSelectors == 0u) return false;

    uint8_t *selectorMtf = scratch; /* nSelectors bytes, reused below */
    for (uint32_t i = 0u; i < nSelectors; ++i) {
        uint32_t j = 0u;
        while (br_bits(br, 1u) == 1u) ++j;
        if (j >= BZ_MAX_GROUPS) return false;
        selectorMtf[i] = (uint8_t)j;
    }
    uint8_t mtfGroups[BZ_MAX_GROUPS];
    for (uint32_t i = 0u; i < nGroups; ++i) mtfGroups[i] = (uint8_t)i;
    uint8_t *selectors = scratch + nSelectors; /* another nSelectors bytes */
    for (uint32_t i = 0u; i < nSelectors; ++i) {
        uint32_t j = selectorMtf[i];
        const uint8_t value = mtfGroups[j];
        for (; j > 0u; --j) mtfGroups[j] = mtfGroups[j - 1u];
        mtfGroups[0] = value;
        selectors[i] = value;
    }

    static struct huff_table tables[BZ_MAX_GROUPS];
    for (uint32_t g = 0u; g < nGroups; ++g) {
        uint8_t lengths[BZ_MAX_ALPHA_SIZE];
        int32_t curr = (int32_t)br_bits(br, 5u);
        for (uint32_t sym = 0u; sym < alphaSize; ++sym) {
            for (;;) {
                if (curr < 1 || curr > (int32_t)BZ_MAX_CODE_LEN) return false;
                if (br_bits(br, 1u) == 0u) break;
                if (br_bits(br, 1u) == 0u) ++curr; else --curr;
            }
            lengths[sym] = (uint8_t)curr;
        }
        build_huffman(&tables[g], lengths, alphaSize);
    }

    uint8_t mtf[256];
    for (uint32_t i = 0u; i < nInUse; ++i) mtf[i] = seqToUnseq[i];

    uint8_t *block = scratch + (size_t)max_block_bytes * 4u; /* byte buffer region */
    uint32_t nblock = 0u;
    uint32_t groupPos = 0u, groupIndex = (uint32_t)-1;
    const struct huff_table *table = NULL;
    uint32_t run = 0u, runBit = 1u;
    const uint32_t eob = alphaSize - 1u;

    for (;;) {
        if (groupPos == 0u) {
            ++groupIndex;
            if (groupIndex >= nSelectors) return false;
            groupPos = BZ_N_GROUPS_SYMS;
            table = &tables[selectors[groupIndex]];
        }
        --groupPos;
        const int32_t sym = huffman_decode(br, table);
        if (sym < 0) return false;
        if ((uint32_t)sym == BZ_RUNA || (uint32_t)sym == BZ_RUNB) {
            run += ((uint32_t)sym + 1u) * runBit;
            runBit <<= 1u;
            continue;
        }
        if (run > 0u) {
            if ((uint64_t)nblock + run > max_block_bytes) return false;
            const uint8_t value = mtf[0];
            for (uint32_t i = 0u; i < run; ++i) block[nblock++] = value;
            run = 0u; runBit = 1u;
        }
        if ((uint32_t)sym == eob) break;
        const uint32_t mtfIndex = (uint32_t)sym - 1u;
        if (mtfIndex >= nInUse) return false;
        const uint8_t value = mtf[mtfIndex];
        for (uint32_t i = mtfIndex; i > 0u; --i) mtf[i] = mtf[i - 1u];
        mtf[0] = value;
        if (nblock >= max_block_bytes) return false;
        block[nblock++] = value;
    }
    if (origPtr >= nblock) return false;

    /* Inverse Burrows-Wheeler Transform via counting-sort permutation. */
    uint32_t *tt = (uint32_t *)(void *)scratch; /* reuse: nblock uint32 entries */
    uint32_t cftab[257];
    for (uint32_t i = 0u; i < 257u; ++i) cftab[i] = 0u;
    for (uint32_t i = 0u; i < nblock; ++i) cftab[block[i] + 1u]++;
    for (uint32_t i = 0u; i < 256u; ++i) cftab[i + 1u] += cftab[i];
    for (uint32_t i = 0u; i < nblock; ++i) {
        const uint8_t ch = block[i];
        tt[cftab[ch]] = i;
        cftab[ch]++;
    }

    /* Undo RLE1 while walking the inverse-BWT output. */
    uint32_t tPos = tt[origPtr];
    uint8_t lastByte = 0u;
    uint32_t runLength = 0u;
    size_t out = 0u;
    for (uint32_t produced = 0u; produced < nblock; ++produced) {
        const uint8_t value = block[tPos];
        tPos = tt[tPos];
        if (runLength == 4u) {
            const uint32_t extra = value;
            if (out + extra > final_capacity) return false;
            for (uint32_t i = 0u; i < extra; ++i) final_out[out++] = lastByte;
            runLength = 0u;
            continue;
        }
        if (out >= final_capacity) return false;
        final_out[out++] = value;
        if (value == lastByte) ++runLength; else { runLength = 1u; lastByte = value; }
    }
    *final_len = out;
    return true;
}

bool Bzip2_decompress(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t output_capacity,
                      size_t *output_size,
                      uint8_t *scratch, size_t scratch_size) {
    if (input == NULL || input_size < 4u || input[0] != 'B' || input[1] != 'Z' ||
        input[2] != 'h' || input[3] < '1' || input[3] > '9')
        return false;
    const uint32_t level = (uint32_t)(input[3] - '0');
    const uint32_t maxBlockBytes = level * 100000u;
    if (scratch == NULL || scratch_size < (size_t)maxBlockBytes * 5u) return false;

    struct bit_reader br = { input, input_size, 4u, 0u };
    size_t total = 0u;
    for (;;) {
        const uint64_t magicHi = br_bits(&br, 24u);
        const uint64_t magicLo = br_bits(&br, 24u);
        const uint64_t magic = (magicHi << 24u) | magicLo;
        if (magic == 0x177245385090ull) {
            (void)br_bits(&br, 32u); /* combined stream CRC, not verified here */
            break;
        }
        if (magic != 0x314159265359ull) return false;
        (void)br_bits(&br, 32u); /* per-block CRC, not verified here */
        size_t blockLen = 0u;
        if (!decode_block(&br, maxBlockBytes, scratch,
                          output + total, output_capacity - total, &blockLen))
            return false;
        total += blockLen;
    }
    *output_size = total;
    return true;
}

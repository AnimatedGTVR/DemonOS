/* Strict allocation-free scanner for Butterscotch's GameMaker bytecode.
   Instruction bit fields and widths match src/vm.c's decoder. */

#include "bytecode_scan.h"

#include <string.h>

#include "binary_utils.h"

#define OP_CONV 0x07u
#define OP_MUL 0x08u
#define OP_DIV 0x09u
#define OP_REM 0x0au
#define OP_MOD 0x0bu
#define OP_ADD 0x0cu
#define OP_SUB 0x0du
#define OP_AND 0x0eu
#define OP_OR 0x0fu
#define OP_XOR 0x10u
#define OP_NEG 0x11u
#define OP_NOT 0x12u
#define OP_SHL 0x13u
#define OP_SHR 0x14u
#define OP_CMP 0x15u
#define OP_POP 0x45u
#define OP_PUSHI 0x84u
#define OP_DUP 0x86u
#define OP_CALLV 0x99u
#define OP_RET 0x9cu
#define OP_EXIT 0x9du
#define OP_POPZ 0x9eu
#define OP_B 0xb6u
#define OP_BT 0xb7u
#define OP_BF 0xb8u
#define OP_PUSHENV 0xbau
#define OP_POPENV 0xbbu
#define OP_PUSH 0xc0u
#define OP_PUSHLOC 0xc1u
#define OP_PUSHGLB 0xc2u
#define OP_PUSHBLTN 0xc3u
#define OP_CALL 0xd9u
#define OP_BREAK 0xffu

static uint32_t list_entry(BinaryReader *reader, uint32_t list,
                           uint32_t index) {
    BinaryReader_seek(reader, (size_t)list + 4u + (size_t)index * 4u);
    return BinaryReader_readUint32(reader);
}

static uint32_t extra_size(uint32_t instruction) {
    if ((instruction & 0x40000000u) == 0u) return 0u;
    switch ((instruction >> 16u) & 0x0fu) {
        case 0u: case 3u: return 8u; /* double, int64 */
        case 1u: case 2u: case 4u: case 5u: case 6u: return 4u;
        case 15u: return 0u; /* inline int16 */
        default: return UINT32_MAX;
    }
}

static int32_t jump_offset(uint32_t instruction) {
    return ((int32_t)(instruction << 9u)) >> 7u;
}

static bool is_boundary(const uint8_t *bytes, uint32_t length,
                        uint32_t target) {
    if (target == length) return true;
    uint32_t ip = 0u;
    while (ip < length) {
        if (ip == target) return true;
        if (length - ip < 4u) return false;
        const uint32_t extra = extra_size(BinaryUtils_readUint32(bytes + ip));
        if (extra == UINT32_MAX || length - ip < 4u + extra) return false;
        ip += 4u + extra;
    }
    return false;
}

static bool scan_code(const uint8_t *bytes, uint32_t length,
                      DemonBytecodeStats *stats) {
    uint32_t ip = 0u;
    while (ip < length) {
        if (length - ip < 4u) return false;
        const uint32_t instruction = BinaryUtils_readUint32(bytes + ip);
        const uint8_t opcode = (uint8_t)(instruction >> 24u);
        const uint32_t extra = extra_size(instruction);
        if (extra == UINT32_MAX || length - ip < 4u + extra) return false;
        ++stats->instructions;
        stats->operandBytes += extra;
        switch (opcode) {
            case OP_PUSH: case OP_PUSHLOC: case OP_PUSHGLB:
            case OP_PUSHBLTN: case OP_PUSHI:
                ++stats->pushes; break;
            case OP_CONV:
                ++stats->conversions; break;
            case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_XOR:
            case OP_NEG: case OP_NOT: case OP_SHL: case OP_SHR: case OP_CMP:
                ++stats->arithmetic; break;
            case OP_POP:
                ++stats->stores; break;
            case OP_CALL: case OP_CALLV:
                ++stats->calls; break;
            case OP_B: case OP_BT: case OP_BF: case OP_PUSHENV:
            case OP_POPENV: {
                ++stats->branches;
                if (opcode == OP_POPENV &&
                    (instruction & 0x00ffffffu) == 0x00f00000u) break;
                const int64_t target = (int64_t)ip + jump_offset(instruction);
                if (target < 0 || target > length) return false;
                if (!is_boundary(bytes, length, (uint32_t)target)) return false;
                break;
            }
            case OP_POPZ:
                ++stats->stackDrops; break;
            case OP_RET: case OP_EXIT:
                ++stats->exits; break;
            case OP_DUP: case OP_BREAK:
                break;
            default:
                ++stats->unknownOpcodes;
                return false;
        }
        ip += 4u + extra;
    }
    if (ip != length) return false;
    return true;
}

bool DemonBytecode_scan(BinaryReader *reader, const DemonDataWinIndex *index,
                        uint8_t wadVersion, DemonBytecodeStats *stats) {
    const DemonDataWinChunk *code = DemonDataWinIndex_find(index,
        DATAWIN_TAG('C', 'O', 'D', 'E'));
    uint32_t count = 0u;
    if (reader == NULL || stats == NULL || code == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('C', 'O', 'D', 'E'), &count)) return false;
    memset(stats, 0, sizeof(*stats));
    const uint64_t end = (uint64_t)code->payloadOffset + code->size;
    const bool oldFormat = wadVersion <= 14u;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t entry = list_entry(reader, code->payloadOffset, i);
        if (entry == 0u || (uint64_t)entry + (oldFormat ? 8u : 20u) > end)
            return false;
        BinaryReader_seek(reader, entry + 4u);
        const uint32_t length = BinaryReader_readUint32(reader);
        uint64_t bytecode = (uint64_t)entry + 8u;
        if (!oldFormat) {
            BinaryReader_skip(reader, 4u);
            const uint64_t relativeField = BinaryReader_getPosition(reader);
            const int32_t relative = BinaryReader_readInt32(reader);
            const int64_t target = (int64_t)relativeField + relative;
            if (target < 0) return false;
            bytecode = (uint64_t)target;
        }
        if (bytecode > end || length > end - bytecode) return false;
        const uint8_t *bytes = BinaryReader_readBytesAt(reader,
            (size_t)bytecode, length);
        if (bytes == NULL || !scan_code(bytes, length, stats)) return false;
        free((void *)bytes);
        ++stats->codeEntries;
    }
    return stats->codeEntries != 0u;
}

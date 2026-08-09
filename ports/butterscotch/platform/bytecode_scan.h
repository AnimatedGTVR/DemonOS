#ifndef DEMONOS_BUTTERSCOTCH_BYTECODE_SCAN_H
#define DEMONOS_BUTTERSCOTCH_BYTECODE_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "binary_reader.h"
#include "datawin_index.h"

typedef struct {
    uint32_t codeEntries;
    uint32_t instructions;
    uint32_t operandBytes;
    uint32_t pushes;
    uint32_t conversions;
    uint32_t arithmetic;
    uint32_t stores;
    uint32_t calls;
    uint32_t branches;
    uint32_t stackDrops;
    uint32_t exits;
    uint32_t unknownOpcodes;
} DemonBytecodeStats;

bool DemonBytecode_scan(BinaryReader *reader, const DemonDataWinIndex *index,
                        uint8_t wadVersion, DemonBytecodeStats *stats);

#endif

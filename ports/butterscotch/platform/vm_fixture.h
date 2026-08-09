#ifndef DEMONOS_BUTTERSCOTCH_VM_FIXTURE_H
#define DEMONOS_BUTTERSCOTCH_VM_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "binary_reader.h"
#include "datawin_index.h"

typedef struct {
    uint32_t codeEntries;
    uint32_t instructions;
    uint32_t builtinCalls;
    uint32_t keyboardChecks;
    uint32_t debugMessages;
    uint32_t branches;
    uint32_t branchesTaken;
    uint32_t variableStores;
    int32_t finalX;
    int32_t finalY;
    uint32_t messageFnv1a;
    uint32_t diagnosticAddress;
    uint32_t diagnosticInstruction;
    uint32_t diagnosticStackDepth;
} DemonVmExecutionStats;

#define DEMON_VM_INSTANCE_VARIABLE_MAX 8u
typedef struct {
    int32_t variables[DEMON_VM_INSTANCE_VARIABLE_MAX];
    bool initialized;
} DemonVmInstanceState;

enum demon_vm_key_mask {
    DEMON_VM_KEY_LEFT = 1u << 0u,
    DEMON_VM_KEY_RIGHT = 1u << 1u,
    DEMON_VM_KEY_UP = 1u << 2u,
    DEMON_VM_KEY_DOWN = 1u << 3u,
};

bool DemonVm_executeFixture(BinaryReader *reader,
                            const DemonDataWinIndex *index,
                            uint8_t wadVersion,
                            DemonVmExecutionStats *stats);
bool DemonVm_executeFixtureInput(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 uint8_t wadVersion, uint32_t keyMask,
                                 int32_t initialX, int32_t initialY,
                                 DemonVmExecutionStats *stats);
bool DemonVm_executeEvents(BinaryReader *reader,
                           const DemonDataWinIndex *index,
                           uint8_t wadVersion, uint32_t keyMask,
                           uint32_t codeMask, int32_t initialX,
                           int32_t initialY, DemonVmExecutionStats *stats);
void DemonVm_initInstanceState(DemonVmInstanceState *state,
                               int32_t initialX, int32_t initialY);
bool DemonVm_executeEventsState(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint8_t wadVersion, uint32_t keyMask,
                                uint32_t codeMask,
                                DemonVmInstanceState *state,
                                DemonVmExecutionStats *stats);
bool DemonVm_integerOpcodeSelfTest(uint32_t *operations,
                                   uint32_t *comparisons,
                                   uint32_t *branches);
bool DemonVm_rvalueSelfTest(uint32_t *conversions, uint32_t *realOperations);
bool DemonVm_controlOpcodeSelfTest(uint32_t *instructions,
                                   uint32_t *duplicates,
                                   uint32_t *terminators);

#endif

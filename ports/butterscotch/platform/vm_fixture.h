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
    /* The real GameMaker instance ID this state belongs to, and (for a
     * collision dispatch) the "other" instance's ID -- -1 for none/not
     * applicable. Defaulted by DemonVm_initInstanceState; set by
     * DemonVm_setInstanceContext wherever real per-instance dispatch
     * actually knows these (core_main.c's live loop and headless probes),
     * not by the fixture self-tests. */
    int32_t instanceId;
    int32_t otherInstanceId;
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
                           const uint32_t *codeIds, uint32_t codeIdCount,
                           int32_t initialX, int32_t initialY,
                           DemonVmExecutionStats *stats);
void DemonVm_initInstanceState(DemonVmInstanceState *state,
                               int32_t initialX, int32_t initialY);
void DemonVm_setInstanceContext(DemonVmInstanceState *state,
                                int32_t instanceId, int32_t otherInstanceId);
/* codeIds/codeIdCount name the exact real CODE-chunk entries to run this
 * call, in order -- not a bitmask. A uint32_t bitmask can only ever
 * address 32 of a real game's CODE entries (Deltarune Ch1 alone has 1680);
 * `1u << codeId` for any real codeId >= 32 is undefined behavior and was
 * silently running the wrong script entirely rather than failing closed. */
bool DemonVm_executeEventsState(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint8_t wadVersion, uint32_t keyMask,
                                const uint32_t *codeIds, uint32_t codeIdCount,
                                DemonVmInstanceState *state,
                                DemonVmExecutionStats *stats);
bool DemonVm_integerOpcodeSelfTest(uint32_t *operations,
                                   uint32_t *comparisons,
                                   uint32_t *branches);
bool DemonVm_rvalueSelfTest(uint32_t *conversions, uint32_t *realOperations);
bool DemonVm_controlOpcodeSelfTest(uint32_t *instructions,
                                   uint32_t *duplicates,
                                   uint32_t *terminators);
bool DemonVm_callFrameSelfTest(int64_t *result);
bool DemonVm_arraySelfTest(int64_t *result);
bool DemonVm_markerBuiltinSelfTest(void);

#endif

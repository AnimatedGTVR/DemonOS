/* First native execution slice for Butterscotch's real upstream fixture.
   This intentionally implements the fixture's actual WAD16 opcode subset;
   unsupported instructions fail closed instead of being treated as no-ops. */

#include "vm_fixture.h"

#include <string.h>

#include <demon/portkit.h>

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
#define OP_RET 0x9cu
#define OP_EXIT 0x9du
#define OP_POPZ 0x9eu
#define OP_BF 0xb8u
#define OP_B 0xb6u
#define OP_BT 0xb7u
#define OP_PUSH 0xc0u
#define OP_PUSHLOC 0xc1u
#define OP_PUSHGLB 0xc2u
#define OP_CALL 0xd9u
#define OP_BREAK 0xffu
#define VALUE_STACK_MAX 32u

/* BC17+ array support. BREAK_* are OP_BREAK's low-16-bits signed sub-opcode
 * (confirmed against real Deltarune Ch1 data: scanning the CODE chunk for
 * 0xFF-opcode words found 7083 real BREAK instructions, with SETOWNER/POPAF/
 * PUSHAF the overwhelming majority -- PUSHAC/SAVEAREF/RESTOREAREF/etc. are
 * comparatively rare or absent and stay unsupported, see call_builtin/
 * execute_code below). VARTYPE_ARRAYPUSHAF/POPAF are the top-byte tag on a
 * PUSH-variable instruction's own operand word (previously unread by this
 * file -- only the instruction's *address* was used to resolve which
 * variable, never this embedded operand) that upstream uses to mark "this
 * particular push feeds an array-index chain, keep it a live reference"
 * rather than an ordinary value read; see the PUSH type1==5 case. */
#define BREAK_SETOWNER (-5)
#define BREAK_PUSHAF (-2)
#define BREAK_POPAF (-3)
#define VARTYPE_ARRAYPUSHAF 0x10u
#define VARTYPE_ARRAYPOPAF 0x90u
#define VM_ARRAY_MAX 64u

/* Real GameMaker VARI instanceType constants -- confirmed against real
 * Deltarune Ch1 data (build/local-games/deltarune-ch1/data.win): scanning
 * all 5178 VARI records found exactly these five values, with "argument0"
 * at instanceType -6/varIndex 0 and a plain local named "struct" at -7,
 * establishing which is which (the two are easy to swap by guessing). -16
 * (struct-instance variables) has no real occurrences yet and isn't
 * classified below -- falls through to the self/global path unchanged,
 * same as before this file had any scope classification at all. */
#define VARI_SCOPE_SELF (-1)
#define VARI_SCOPE_GLOBAL (-5)
#define VARI_SCOPE_ARGUMENT (-6)
#define VARI_SCOPE_LOCAL (-7)

/* Bounded, fail-closed nested call support -- same discipline as
 * VALUE_STACK_MAX above (a fixed cap the real file's actual needs are
 * checked against, not scaled to match them): call depth in practice is
 * shallow, and a function needing more than 16 arguments or 48 additional
 * locals is far outside anything this increment targets. */
#define CALL_STACK_MAX 8u
#define VM_ARG_SLOTS 16u
#define VM_LOCAL_SLOTS 48u
#define VM_FRAME_SLOTS (VM_ARG_SLOTS + VM_LOCAL_SLOTS)

typedef enum {
    VALUE_INT, VALUE_REAL, VALUE_BOOL, VALUE_STRING, VALUE_UNDEFINED,
    VALUE_FUNCTION, VALUE_ARRAY
} ValueKind;
typedef struct ArrayData ArrayData;
typedef struct { ValueKind kind; int64_t value; double real; ArrayData *array; } Value;
struct ArrayData { uint32_t length; Value *items; };
typedef struct { uint32_t address; uint32_t index; } Reference;

/* Arrays are implemented as always-deep-copied values rather than porting
 * upstream's reference-counted, copy-on-write GMLArray model: every value
 * entering persistent storage (a variable slot via OP_POP, an array element
 * via BREAK_POPAF, a callee's argument locals) is either a fresh allocation
 * or an independent clone, so exactly one persistent location ever owns a
 * given ArrayData and a plain release-before-overwrite is always safe --
 * no refcounting, no ownership tracking, no aliasing hazard. The one place
 * this needs care is BREAK_PUSHAF/POPAF's "array reference" operand, which
 * must stay a *shared* (uncloned) pointer back into the owning slot's
 * ArrayData for in-place element mutation to be visible afterward -- see
 * the VARTYPE_ARRAYPUSHAF/POPAF handling in the PUSH type1==5 case below.
 * Known gap: a value that's read (cloned) onto the stack but then discarded
 * by a control-flow path other than OP_POPZ/BREAK_SETOWNER/top-level RET
 * (all of which release it) leaks rather than double-freeing -- accepted,
 * consistent with this increment's explicitly bounded scope. */
static void array_release(Value v) {
    if (v.kind != VALUE_ARRAY || v.array == NULL) return;
    for (uint32_t i = 0u; i < v.array->length; ++i)
        array_release(v.array->items[i]);
    demon_port_free(v.array->items);
    demon_port_free(v.array);
}

static bool array_clone(Value in, Value *out) {
    if (in.kind != VALUE_ARRAY || in.array == NULL) { *out = in; return true; }
    ArrayData *clone = (ArrayData *)demon_port_malloc(sizeof(ArrayData));
    if (clone == NULL) return false;
    clone->length = in.array->length;
    clone->items = clone->length == 0u ? NULL :
        (Value *)demon_port_malloc((size_t)clone->length * sizeof(Value));
    if (clone->length > 0u && clone->items == NULL) {
        demon_port_free(clone);
        return false;
    }
    for (uint32_t i = 0u; i < clone->length; ++i) {
        if (!array_clone(in.array->items[i], &clone->items[i])) {
            for (uint32_t j = 0u; j < i; ++j) array_release(clone->items[j]);
            demon_port_free(clone->items);
            demon_port_free(clone);
            return false;
        }
    }
    out->kind = VALUE_ARRAY;
    out->value = 0;
    out->real = 0.0;
    out->array = clone;
    return true;
}

/* Per-VARI-table-index scope classification, populated once in
 * build_references from the real instanceType/varIndex fields (previously
 * unread -- only occurrences/firstAddress were consumed). kind 0 keeps the
 * existing flat vm->variables[] behavior (self/global/unclassified) exactly
 * as before; kinds 1/2 route through the active call frame's locals. */
typedef struct { uint8_t kind; uint8_t slot; } VarScope;
enum { VAR_SCOPE_FLAT = 0, VAR_SCOPE_ARGUMENT = 1, VAR_SCOPE_LOCAL = 2 };

/* A suspended caller: where to resume (bytes/absolute/length/ip) and the
 * value-stack top to restore against when the callee returns. */
typedef struct {
    const uint8_t *bytes;
    uint32_t absolute;
    uint32_t length;
    uint32_t ip;
    uint32_t savedTop;
} Frame;

typedef struct {
    const uint8_t *file;
    uint32_t fileSize;
    Value stack[VALUE_STACK_MAX];
    uint32_t top;
    /* Sized to the real file's actual variable/reference counts (heap
     * allocated in build_references) rather than a fixed constant: a real
     * game's VARI/FUNC tables and their reference chains are far larger
     * than the small synthetic fixture these arrays were originally sized
     * for (confirmed against real Deltarune Ch1 data: 5178 variables,
     * 74667 total variable-reference occurrences, 19948 function-reference
     * occurrences). */
    Value *variables;
    VarScope *scope;
    uint32_t variableCount;
    Reference *vars;
    uint32_t varCapacity;
    uint32_t varRefs;
    Reference *funcs;
    uint32_t funcCapacity;
    uint32_t funcRefs;
    const DemonDataWinChunk *strg;
    const DemonDataWinChunk *func;
    const DemonDataWinChunk *code;
    DemonVmExecutionStats *stats;
    uint32_t keyMask;
    /* Real GameMaker instance IDs for the @@This@@/@@Other@@ builtins --
     * -1 means "no current/other instance" (matches DemonVmInstanceState's
     * default and upstream's real fallback-to-self behavior for Other). */
    int32_t selfInstanceId;
    int32_t otherInstanceId;
    Frame frames[CALL_STACK_MAX];
    uint32_t depth;
    /* Heap allocated (CALL_STACK_MAX * VM_FRAME_SLOTS values, ~12 KiB) --
     * Vm itself is stack-declared in the entry points below, and this
     * kernel's userspace stack is a tight 32 KiB (USERSPACE_STACK_PAGES);
     * embedding it directly here the way vm.stack[] is embedded would
     * eat more than a third of that budget in one struct. */
    Value *locals;
} Vm;

static Value *frame_locals(Vm *vm, uint32_t depth) {
    return vm->locals + (size_t)depth * VM_FRAME_SLOTS;
}

/* Resolves a VARI table index to the Value storage it actually addresses:
 * the flat global/self table for VAR_SCOPE_FLAT (unchanged pre-existing
 * behavior), or a slot in the *currently active* call frame's locals for
 * arguments/locals -- correct because each nested call gets its own frame
 * (frame_locals(vm, vm->depth)), so the same VARI index used by two
 * different in-flight calls of the same function resolves to two different
 * Value cells, matching real per-call local scoping. */
static Value *variable_slot(Vm *vm, uint32_t index) {
    if (index >= vm->variableCount) return NULL;
    const VarScope scope = vm->scope[index];
    if (scope.kind == VAR_SCOPE_ARGUMENT)
        return &frame_locals(vm, vm->depth)[scope.slot];
    if (scope.kind == VAR_SCOPE_LOCAL)
        return &frame_locals(vm, vm->depth)[VM_ARG_SLOTS + scope.slot];
    return &vm->variables[index];
}

static void vm_destroy(Vm *vm) {
    /* Every value ever stored into these tables arrived via a clone/move
     * that made this slot its sole owner (see the array_release/array_clone
     * comment above) -- release before freeing the backing tables. */
    if (vm->variables != NULL)
        for (uint32_t i = 0u; i < vm->variableCount; ++i)
            array_release(vm->variables[i]);
    if (vm->locals != NULL)
        for (uint32_t i = 0u; i < CALL_STACK_MAX * VM_FRAME_SLOTS; ++i)
            array_release(vm->locals[i]);
    demon_port_free(vm->variables);
    demon_port_free(vm->scope);
    demon_port_free(vm->vars);
    demon_port_free(vm->funcs);
    demon_port_free(vm->locals);
    vm->variables = NULL;
    vm->scope = NULL;
    vm->vars = NULL;
    vm->funcs = NULL;
    vm->locals = NULL;
}

static uint32_t list_entry(BinaryReader *reader, uint32_t list,
                           uint32_t index) {
    BinaryReader_seek(reader, (size_t)list + 4u + (size_t)index * 4u);
    return BinaryReader_readUint32(reader);
}

static bool push(Vm *vm, Value value) {
    if (vm->top == VALUE_STACK_MAX) return false;
    vm->stack[vm->top++] = value;
    return true;
}

static bool pop(Vm *vm, Value *value) {
    if (vm->top == 0u) return false;
    *value = vm->stack[--vm->top];
    return true;
}

static bool integer_binary(uint8_t opcode, int64_t a, int64_t b,
                           int64_t *result) {
    if (result == NULL) return false;
    switch (opcode) {
        case OP_MUL: *result = a * b; return true;
        case OP_DIV: if (b == 0) return false; *result = a / b; return true;
        case OP_REM: if (b == 0) return false; *result = a % b; return true;
        case OP_MOD:
            if (b == 0) return false;
            *result = a % b;
            if (*result != 0 && ((*result < 0) != (b < 0))) *result += b;
            return true;
        case OP_ADD: *result = a + b; return true;
        case OP_SUB: *result = a - b; return true;
        case OP_AND: *result = a & b; return true;
        case OP_OR: *result = a | b; return true;
        case OP_XOR: *result = a ^ b; return true;
        case OP_SHL:
            if (b < 0 || b >= 63) return false;
            *result = a << (uint32_t)b; return true;
        case OP_SHR:
            if (b < 0 || b >= 63) return false;
            *result = a >> (uint32_t)b; return true;
        default: return false;
    }
}

static bool integer_compare(uint32_t kind, int64_t a, int64_t b,
                            int64_t *result) {
    if (result == NULL || kind < 1u || kind > 6u) return false;
    *result = kind == 1u ? a < b : kind == 2u ? a <= b :
        kind == 3u ? a == b : kind == 4u ? a != b :
        kind == 5u ? a >= b : a > b;
    return true;
}

static bool numeric_value(Value value, double *result) {
    if (result == NULL) return false;
    if (value.kind == VALUE_REAL) *result = value.real;
    else if (value.kind == VALUE_INT || value.kind == VALUE_BOOL)
        *result = (double)value.value;
    else return false;
    return true;
}

static bool numeric_binary(uint8_t opcode, Value a, Value b, Value *result) {
    if (result == NULL) return false;
    if (a.kind != VALUE_REAL && b.kind != VALUE_REAL) {
        int64_t integer;
        if (a.kind != VALUE_INT || b.kind != VALUE_INT ||
            !integer_binary(opcode, a.value, b.value, &integer)) return false;
        *result = (Value){VALUE_INT, integer, 0.0, NULL};
        return true;
    }
    double left, right;
    if (!numeric_value(a, &left) || !numeric_value(b, &right)) return false;
    if ((opcode == OP_DIV || opcode == OP_REM || opcode == OP_MOD) &&
        right == 0.0) return false;
    double value;
    if (opcode == OP_MUL) value = left * right;
    else if (opcode == OP_DIV) value = left / right;
    else if (opcode == OP_ADD) value = left + right;
    else if (opcode == OP_SUB) value = left - right;
    else return false; /* integer-only remainder, bitwise and shifts */
    *result = (Value){VALUE_REAL, 0, value, NULL};
    return true;
}

static bool branch_taken(uint8_t opcode, int64_t condition) {
    return opcode == OP_B || (opcode == OP_BT && condition != 0) ||
        (opcode == OP_BF && condition == 0);
}

static bool resolve(const Reference *refs, uint32_t count, uint32_t address,
                    uint32_t *index) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (refs[i].address == address) {
            *index = refs[i].index;
            return true;
        }
    }
    return false;
}

static bool add_chain(Vm *vm, Reference *refs, uint32_t capacity,
                      uint32_t *refCount, uint32_t index,
                      uint32_t occurrences, uint32_t firstAddress) {
    if (occurrences == 0u) return true;
    uint32_t address = firstAddress;
    for (uint32_t i = 0u; i < occurrences; ++i) {
        if (*refCount == capacity || address + 8u > vm->fileSize)
            return false;
        refs[*refCount].address = address;
        refs[*refCount].index = index;
        ++*refCount;
        if (i + 1u < occurrences) {
            const uint32_t delta = BinaryUtils_readUint32(vm->file +
                address + 4u) & 0x07ffffffu;
            if (delta == 0u || UINT32_MAX - address < delta) return false;
            address += delta;
        }
    }
    return true;
}

static bool build_references(BinaryReader *reader,
                             const DemonDataWinIndex *index, Vm *vm,
                             uint8_t wadVersion) {
    const DemonDataWinChunk *vari = DemonDataWinIndex_find(index,
        DATAWIN_TAG('V', 'A', 'R', 'I'));
    const DemonDataWinChunk *func = DemonDataWinIndex_find(index,
        DATAWIN_TAG('F', 'U', 'N', 'C'));
    if (vari == NULL || func == NULL || vari->size < 12u || func->size < 4u)
        return false;
    const uint32_t variableCount = (vari->size - 12u) / 20u;
    BinaryReader_seek(reader, func->payloadOffset);
    const uint32_t functionCount = BinaryReader_readUint32(reader);
    if ((uint64_t)functionCount * 12u + 4u > func->size) return false;

    /* Pass 1: sum occurrence counts to size the reference-chain arrays
     * exactly, instead of guessing at a fixed capacity. */
    uint64_t totalVarOcc = 0u, totalFuncOcc = 0u;
    for (uint32_t i = 0u; i < variableCount; ++i) {
        BinaryReader_seek(reader, vari->payloadOffset + 12u + i * 20u + 12u);
        totalVarOcc += BinaryReader_readUint32(reader);
    }
    for (uint32_t i = 0u; i < functionCount; ++i) {
        BinaryReader_seek(reader, func->payloadOffset + 4u + i * 12u + 4u);
        totalFuncOcc += BinaryReader_readUint32(reader);
    }
    if (totalVarOcc > UINT32_MAX || totalFuncOcc > UINT32_MAX) return false;

    vm->variables = (Value *)demon_port_malloc(
        (size_t)variableCount * sizeof(Value));
    vm->scope = (VarScope *)demon_port_malloc(
        (size_t)variableCount * sizeof(VarScope));
    vm->locals = (Value *)demon_port_malloc(
        (size_t)CALL_STACK_MAX * VM_FRAME_SLOTS * sizeof(Value));
    vm->vars = totalVarOcc == 0u ? NULL : (Reference *)demon_port_malloc(
        (size_t)totalVarOcc * sizeof(Reference));
    vm->funcs = totalFuncOcc == 0u ? NULL : (Reference *)demon_port_malloc(
        (size_t)totalFuncOcc * sizeof(Reference));
    if ((variableCount > 0u && (vm->variables == NULL || vm->scope == NULL)) ||
        vm->locals == NULL ||
        (totalVarOcc > 0u && vm->vars == NULL) ||
        (totalFuncOcc > 0u && vm->funcs == NULL)) {
        vm_destroy(vm);
        return false;
    }
    memset(vm->variables, 0, (size_t)variableCount * sizeof(Value));
    memset(vm->scope, 0, (size_t)variableCount * sizeof(VarScope));
    vm->variableCount = variableCount;
    vm->varCapacity = (uint32_t)totalVarOcc;
    vm->funcCapacity = (uint32_t)totalFuncOcc;

    /* Pass 2: populate the reference chains, and classify each variable's
     * scope from its real instanceType/varIndex fields (previously unread).
     * Anything that isn't a recognized argument/local -- including plain
     * self/global variables and the rare struct-instance (-16) case this
     * repo hasn't seen real occurrences of yet -- keeps VAR_SCOPE_FLAT,
     * i.e. exactly the pre-existing vm->variables[] behavior. */
    for (uint32_t i = 0u; i < variableCount; ++i) {
        BinaryReader_seek(reader, vari->payloadOffset + 12u + i * 20u + 4u);
        const int32_t instanceType = BinaryReader_readInt32(reader);
        const int32_t varIndex = BinaryReader_readInt32(reader);
        const uint32_t occurrences = BinaryReader_readUint32(reader);
        const uint32_t first = BinaryReader_readUint32(reader);
        if (instanceType == VARI_SCOPE_ARGUMENT && varIndex >= 0 &&
            (uint32_t)varIndex < VM_ARG_SLOTS) {
            vm->scope[i].kind = VAR_SCOPE_ARGUMENT;
            vm->scope[i].slot = (uint8_t)varIndex;
        } else if (instanceType == VARI_SCOPE_LOCAL && varIndex >= 0 &&
                   (uint32_t)varIndex < VM_LOCAL_SLOTS) {
            vm->scope[i].kind = VAR_SCOPE_LOCAL;
            vm->scope[i].slot = (uint8_t)varIndex;
        }
        if (!add_chain(vm, vm->vars, vm->varCapacity, &vm->varRefs, i,
                       occurrences, first))
            return false;
    }
    for (uint32_t i = 0u; i < functionCount; ++i) {
        BinaryReader_seek(reader, func->payloadOffset + 4u + i * 12u + 4u);
        const uint32_t occurrences = BinaryReader_readUint32(reader);
        uint32_t first = BinaryReader_readUint32(reader);
        /* wad17/GMS2.3+ changed FUNC's serialized address to point at the
         * call site's reference operand word instead of the instruction
         * itself (confirmed against real Deltarune Ch1 data: the raw
         * address decodes to an invalid 0x00 opcode, while address-4
         * decodes to a real PUSH/CALL instruction). VARI is unaffected. */
        if (wadVersion == 17u && occurrences > 0u && first >= 4u) first -= 4u;
        if (!add_chain(vm, vm->funcs, vm->funcCapacity, &vm->funcRefs, i,
                       occurrences, first))
            return false;
    }
    return true;
}

static bool string_value(Vm *vm, uint32_t index, const uint8_t **text,
                         uint32_t *length) {
    if (vm->strg == NULL || vm->strg->size < 4u) return false;
    const uint32_t count = BinaryUtils_readUint32(vm->file +
        vm->strg->payloadOffset);
    if (index >= count) return false;
    const uint32_t pointer = BinaryUtils_readUint32(vm->file +
        vm->strg->payloadOffset + 4u + index * 4u);
    if (pointer < vm->strg->payloadOffset + 4u ||
        (uint64_t)pointer + 4u >=
            (uint64_t)vm->strg->payloadOffset + vm->strg->size) return false;
    /* STRG's index points at the serialized length field. Other resource
     * references point at the character data, which is why treating this
     * table like a resource-name pointer rejected the first builtin call. */
    const uint32_t size = BinaryUtils_readUint32(vm->file + pointer);
    const uint32_t content = pointer + 4u;
    if ((uint64_t)content + size >=
        (uint64_t)vm->strg->payloadOffset + vm->strg->size ||
        vm->file[content + size] != 0u) return false;
    *text = vm->file + content;
    *length = size;
    return true;
}

/* FUNC indices are positions in THIS file's FUNC table, not a stable
 * cross-file ID -- dispatch by name, not index, or builtin identification
 * silently breaks on any file whose FUNC table happens to be ordered
 * differently. Confirmed against real data: index 1 is "method" in the
 * real Deltarune file, not "keyboard_check" as the small synthetic
 * fixture's table happened to have it. */
/* A name pointer (as stored raw in FUNC/CODE records) points at the string
 * data directly; its length is the uint32 immediately before it. Shared by
 * function_name_equals below and the new CODE-entry lookup, which compares
 * a FUNC name against every CODE entry's own name the same way. */
static bool read_name_pointer(const Vm *vm, uint32_t namePtr,
                              const uint8_t **text, uint32_t *length) {
    if (namePtr < 4u || (uint64_t)namePtr >= vm->fileSize) return false;
    const uint32_t len = BinaryUtils_readUint32(vm->file + namePtr - 4u);
    if ((uint64_t)namePtr + len > vm->fileSize) return false;
    *text = vm->file + namePtr;
    *length = len;
    return true;
}

static bool function_name_pointer(const Vm *vm, uint32_t functionIndex,
                                  uint32_t *namePtr) {
    if (vm->func == NULL) return false;
    const uint64_t entryOff = (uint64_t)vm->func->payloadOffset + 4u +
        (uint64_t)functionIndex * 12u;
    if (entryOff + 12u > (uint64_t)vm->func->payloadOffset + vm->func->size)
        return false;
    *namePtr = BinaryUtils_readUint32(vm->file + entryOff);
    return true;
}

static bool function_name_equals(const Vm *vm, uint32_t functionIndex,
                                 const char *want) {
    uint32_t namePtr;
    const uint8_t *text;
    uint32_t length;
    if (!function_name_pointer(vm, functionIndex, &namePtr) ||
        !read_name_pointer(vm, namePtr, &text, &length)) return false;
    const size_t wantLen = strlen(want);
    return length == wantLen && memcmp(text, want, length) == 0;
}

/* Resolves a FUNC reference to a real user-defined CODE entry (a GML script
 * or function this file actually compiled), by matching names -- the same
 * way function_name_equals identifies builtins, just against the CODE
 * chunk's own name table instead of a hardcoded string. Linear scan over
 * this file's CODE entries; call sites are not hot enough yet in this
 * increment to warrant a name->index cache. */
static bool find_code_entry(Vm *vm, uint32_t functionIndex,
                            uint32_t *entryPointer) {
    uint32_t namePtr;
    const uint8_t *wantText;
    uint32_t wantLength;
    if (vm->code == NULL || vm->code->size < 4u ||
        !function_name_pointer(vm, functionIndex, &namePtr) ||
        !read_name_pointer(vm, namePtr, &wantText, &wantLength))
        return false;
    const uint64_t chunkStart = vm->code->payloadOffset;
    const uint64_t chunkEnd = chunkStart + vm->code->size;
    if (chunkStart + 4u > vm->fileSize) return false;
    const uint32_t count = BinaryUtils_readUint32(vm->file + chunkStart);
    if ((uint64_t)count * 4u + 4u > vm->code->size) return false;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint64_t listSlot = chunkStart + 4u + (uint64_t)i * 4u;
        if (listSlot + 4u > vm->fileSize) return false;
        const uint32_t entry = BinaryUtils_readUint32(vm->file + listSlot);
        if (entry == 0u || (uint64_t)entry + 20u > chunkEnd) continue;
        const uint32_t entryNamePtr = BinaryUtils_readUint32(vm->file + entry);
        const uint8_t *text;
        uint32_t length;
        if (!read_name_pointer(vm, entryNamePtr, &text, &length)) continue;
        if (length == wantLength && memcmp(text, wantText, length) == 0) {
            *entryPointer = entry;
            return true;
        }
    }
    return false;
}

/* Reads a CODE entry's header (locals/arguments counts and the absolute
 * bytecode span) from its record pointer -- same 20-byte layout confirmed
 * in DemonDataWinIndex_inspectCode (datawin_index.c): namePtr(4),
 * length(4), locals(u16), arguments(u16), then a relative offset to the
 * bytecode blob. Only ever called for wadVersion 16/17 files (checked by
 * every public entry point below), so the pre-GMS2.3 8-byte "old format"
 * header this file doesn't otherwise support is never in play here either. */
static bool resolve_code_entry(const Vm *vm, uint32_t entryPointer,
                               uint32_t *locals, uint32_t *arguments,
                               uint32_t *absolute, uint32_t *length) {
    const uint64_t chunkStart = vm->code->payloadOffset;
    const uint64_t chunkEnd = chunkStart + vm->code->size;
    if ((uint64_t)entryPointer + 20u > chunkEnd) return false;
    *length = BinaryUtils_readUint32(vm->file + entryPointer + 4u);
    *locals = BinaryUtils_readUint16(vm->file + entryPointer + 8u);
    *arguments = BinaryUtils_readUint16(vm->file + entryPointer + 10u);
    const uint32_t relativeFieldOffset = entryPointer + 12u;
    const int32_t relative = BinaryUtils_readInt32(vm->file + relativeFieldOffset);
    const int64_t target = (int64_t)relativeFieldOffset + relative;
    if (target < (int64_t)chunkStart || (uint64_t)target > chunkEnd ||
        (uint64_t)*length > chunkEnd - (uint64_t)target) return false;
    *absolute = (uint32_t)target;
    return true;
}

/* BREAK_PUSHAF: pop index + array ref, push a clone of array[index] (or
 * undefined if the ref isn't an array or the index is out of range --
 * matches upstream's soft failure for this case, confirmed at vm.c's
 * handleBreakPushAF). The pushed element is cloned, not shared, same as any
 * other value read -- only the *array ref* pushed for this op's own operand
 * (via the PUSH type1==5 VARTYPE_ARRAYPUSHAF/POPAF case) stays a live
 * shared reference. */
static bool break_pushaf(Vm *vm) {
    Value idxValue, arrayValue;
    if (!pop(vm, &idxValue) || !pop(vm, &arrayValue)) return false;
    if (idxValue.kind != VALUE_INT || idxValue.value < 0) return false;
    Value element = (Value){VALUE_UNDEFINED, 0, 0.0, NULL};
    if (arrayValue.kind == VALUE_ARRAY && arrayValue.array != NULL &&
        (uint64_t)idxValue.value < arrayValue.array->length &&
        !array_clone(arrayValue.array->items[(uint32_t)idxValue.value],
                     &element))
        return false;
    return push(vm, element);
}

/* BREAK_POPAF: pop index + array ref + value (push order was value, then
 * array ref, then index -- confirmed at vm.c's handleBreakPopAF), store the
 * value at array[index], growing the backing storage up to VM_ARRAY_MAX if
 * needed (real GML arrays auto-grow on write). Writing through a non-array
 * ref is a silent no-op, matching upstream. arrayValue is the live shared
 * reference pushed for this instruction's own use (see break_pushaf above
 * and the PUSH case) -- mutating arrayValue.array->items in place is what
 * makes the write visible through the original variable afterward. */
static bool break_popaf(Vm *vm) {
    Value idxValue, arrayValue, value;
    if (!pop(vm, &idxValue) || !pop(vm, &arrayValue) || !pop(vm, &value))
        return false;
    if (idxValue.kind != VALUE_INT || idxValue.value < 0 ||
        idxValue.value >= (int64_t)VM_ARRAY_MAX) {
        array_release(value);
        return false;
    }
    if (arrayValue.kind != VALUE_ARRAY || arrayValue.array == NULL) {
        array_release(value);
        return true;
    }
    const uint32_t idx = (uint32_t)idxValue.value;
    if (idx >= arrayValue.array->length) {
        Value *grown = (Value *)demon_port_malloc(
            (size_t)(idx + 1u) * sizeof(Value));
        if (grown == NULL) { array_release(value); return false; }
        for (uint32_t i = 0u; i < arrayValue.array->length; ++i)
            grown[i] = arrayValue.array->items[i];
        for (uint32_t i = arrayValue.array->length; i <= idx; ++i)
            grown[i] = (Value){VALUE_UNDEFINED, 0, 0.0, NULL};
        demon_port_free(arrayValue.array->items);
        arrayValue.array->items = grown;
        arrayValue.array->length = idx + 1u;
    }
    array_release(arrayValue.array->items[idx]);
    arrayValue.array->items[idx] = value;
    return true;
}

static bool call_builtin(Vm *vm, uint32_t function, uint32_t arguments) {
    if (function_name_equals(vm, function, "@@NewGMLArray@@")) {
        /* Array literal `[1, 2, 3]` -- compiles to a plain call, no special
         * literal opcode (confirmed at vm_builtins.c:14407-14409). Elements
         * arrive already independently owned (PUSHI literals or already-
         * cloned reads), so this moves them rather than cloning again. */
        if (arguments > VM_ARRAY_MAX) return false;
        Value elements[VM_ARRAY_MAX];
        for (uint32_t k = arguments; k-- > 0u;)
            if (!pop(vm, &elements[k])) return false;
        ArrayData *data = (ArrayData *)demon_port_malloc(sizeof(ArrayData));
        if (data == NULL) return false;
        data->length = arguments;
        data->items = arguments == 0u ? NULL :
            (Value *)demon_port_malloc((size_t)arguments * sizeof(Value));
        if (arguments > 0u && data->items == NULL) {
            demon_port_free(data);
            return false;
        }
        for (uint32_t k = 0u; k < arguments; ++k) data->items[k] = elements[k];
        ++vm->stats->builtinCalls;
        return push(vm, (Value){VALUE_ARRAY, 0, 0.0, data});
    }
    if (function_name_equals(vm, function, "array_create")) {
        /* array_create(size[, fill]) -- confirmed at
         * vm_builtins.c:14419-14426. Unlike NewGMLArray's elements, a
         * single fill value is duplicated into every slot, so each slot
         * needs its own clone. */
        if (arguments < 1u || arguments > 2u) return false;
        Value fillValue = (Value){VALUE_UNDEFINED, 0, 0.0, NULL};
        if (arguments == 2u && !pop(vm, &fillValue)) return false;
        Value sizeValue;
        if (!pop(vm, &sizeValue) || sizeValue.kind != VALUE_INT ||
            sizeValue.value < 0 || sizeValue.value > (int64_t)VM_ARRAY_MAX) {
            array_release(fillValue);
            return false;
        }
        const uint32_t count = (uint32_t)sizeValue.value;
        ArrayData *data = (ArrayData *)demon_port_malloc(sizeof(ArrayData));
        if (data == NULL) { array_release(fillValue); return false; }
        data->length = count;
        data->items = count == 0u ? NULL :
            (Value *)demon_port_malloc((size_t)count * sizeof(Value));
        if (count > 0u && data->items == NULL) {
            demon_port_free(data);
            array_release(fillValue);
            return false;
        }
        for (uint32_t k = 0u; k < count; ++k) {
            if (!array_clone(fillValue, &data->items[k])) {
                for (uint32_t j = 0u; j < k; ++j) array_release(data->items[j]);
                demon_port_free(data->items);
                demon_port_free(data);
                array_release(fillValue);
                return false;
            }
        }
        array_release(fillValue);
        ++vm->stats->builtinCalls;
        return push(vm, (Value){VALUE_ARRAY, 0, 0.0, data});
    }
    if (function_name_equals(vm, function, "method")) {
        /* method(scope, func) binds a callable to a scope. This VM has no
         * real multi-instance scoping yet, so scope is dropped and the
         * function reference is forwarded as-is -- correct for the common
         * case (every GMS2.3 script opens with a self-referencing
         * method(self, <its own function>) preamble) since there is only
         * ever one active scope in this interpreter. */
        if (arguments != 2u) return false;
        Value scope, func;
        if (!pop(vm, &scope) || !pop(vm, &func)) return false;
        ++vm->stats->builtinCalls;
        if (func.kind != VALUE_FUNCTION) return false;
        return push(vm, func);
    }
    /* Four trivial GMS2.3-compiler-emitted internal markers -- confirmed
     * against upstream (vm_builtins.c:14428-14455): each takes 0 arguments
     * and just pushes an instance-ID-shaped constant. Real occurrences
     * confirmed against Deltarune Ch1 data (a real Step script's @@NullObject@@
     * call, previously rejected here because every builtin below used to
     * be forced through the single-argument gate regardless of name). */
    if (function_name_equals(vm, function, "@@NullObject@@")) {
        if (arguments != 0u) return false;
        ++vm->stats->builtinCalls;
        return push(vm, (Value){VALUE_INT, -4, 0.0, NULL}); /* INSTANCE_NOONE */
    }
    if (function_name_equals(vm, function, "@@Global@@")) {
        if (arguments != 0u) return false;
        ++vm->stats->builtinCalls;
        return push(vm, (Value){VALUE_INT, -5, 0.0, NULL}); /* INSTANCE_GLOBAL */
    }
    if (function_name_equals(vm, function, "@@This@@")) {
        if (arguments != 0u) return false;
        ++vm->stats->builtinCalls;
        return push(vm, (Value){VALUE_INT, vm->selfInstanceId, 0.0, NULL});
    }
    if (function_name_equals(vm, function, "@@Other@@")) {
        if (arguments != 0u) return false;
        ++vm->stats->builtinCalls;
        /* Falls back to self when there's no "other" -- matches upstream's
         * real behavior (vm_builtins.c:14442-14448). */
        const int32_t other = vm->otherInstanceId >= 0 ?
            vm->otherInstanceId : vm->selfInstanceId;
        return push(vm, (Value){VALUE_INT, other, 0.0, NULL});
    }
    if (arguments != 1u) return false;
    Value argument;
    if (!pop(vm, &argument)) return false;
    ++vm->stats->builtinCalls;
    if (function_name_equals(vm, function, "show_debug_message")) {
        if (argument.kind != VALUE_STRING) return false;
        const uint8_t *text;
        uint32_t length;
        if (!string_value(vm, (uint32_t)argument.value, &text, &length))
            return false;
        for (uint32_t i = 0u; i < length; ++i) {
            vm->stats->messageFnv1a ^= text[i];
            vm->stats->messageFnv1a *= 16777619u;
        }
        ++vm->stats->debugMessages;
        return push(vm, (Value){VALUE_UNDEFINED, 0, 0.0, NULL});
    }
    if (function_name_equals(vm, function, "keyboard_check")) {
        if (argument.kind != VALUE_INT) return false;
        uint32_t mask = 0u;
        if (argument.value == 37) mask = DEMON_VM_KEY_LEFT;
        else if (argument.value == 39) mask = DEMON_VM_KEY_RIGHT;
        else if (argument.value == 38) mask = DEMON_VM_KEY_UP;
        else if (argument.value == 40) mask = DEMON_VM_KEY_DOWN;
        const bool pressed = (vm->keyMask & mask) != 0u;
        ++vm->stats->keyboardChecks;
        return push(vm, (Value){VALUE_BOOL, pressed ? 1 : 0, 0.0, NULL});
    }
    return false;
}

/* Restores the suspended caller recorded when a nested call was made
 * (the top of vm->frames as of vm->depth-1) into the trampoline's active
 * bytes/absolute/length/ip, and pops that frame off. Shared by every
 * nested-return path (RET, EXIT, and falling off the callee's end) below. */
static void resume_caller(Vm *vm, const uint8_t **bytes, uint32_t *absolute,
                          uint32_t *length, uint32_t *ip) {
    --vm->depth;
    const Frame *frame = &vm->frames[vm->depth];
    *bytes = frame->bytes;
    *absolute = frame->absolute;
    *length = frame->length;
    *ip = frame->ip;
}

/* Resolves and pushes a variable reference by the referencing instruction's
 * own address -- shared by OP_PUSH's type1==5 (GML_TYPE_VARIABLE) case and
 * OP_PUSHLOC/OP_PUSHGLB (dedicated local/global variable push opcodes,
 * confirmed at vm.c:2965-2995 in the pinned upstream source; there is no
 * corresponding OP_POPLOC/OP_POPGLB -- writes always go through the single
 * existing OP_POP). VARI occurrence chains record every address a variable
 * is referenced from regardless of which opcode does the referencing, so
 * the same address-based resolve() this file already uses for plain PUSH
 * works unchanged for these too -- no new resolution mechanism needed. */
static bool push_variable_reference(Vm *vm, uint32_t address,
                                    const uint8_t *extra) {
    Value *slot;
    uint32_t reference;
    if (!resolve(vm->vars, vm->varRefs, address, &reference) ||
        (slot = variable_slot(vm, reference)) == NULL)
        return false;
    /* The operand word's top byte is this push's VARTYPE tag (confirmed at
     * vm.h:28-31,118-119): a ARRAYPUSHAF/POPAF-tagged push feeds an
     * immediately following BREAK_PUSHAF/POPAF and must stay a live shared
     * reference into *slot for in-place element mutation to be visible;
     * any other push clones, so an ordinary `y = arr;` never aliases two
     * variables onto the same ArrayData. */
    const uint8_t vartype = (uint8_t)(BinaryUtils_readUint32(extra) >> 24u);
    if (vartype == VARTYPE_ARRAYPUSHAF || vartype == VARTYPE_ARRAYPOPAF)
        return push(vm, *slot);
    Value cloned;
    return array_clone(*slot, &cloned) && push(vm, cloned);
}

static bool execute_code(Vm *vm, const uint8_t *bytes, uint32_t absolute,
                         uint32_t length) {
    uint32_t ip = 0u;
    for (;;) {
        /* The value-stack level this frame started at: 0 for the true
         * top-level entry (matching every check below exactly as it was
         * before nested calls existed), or the caller's saved stack top
         * for a nested call -- since calls share one operand stack rather
         * than giving each frame its own, a callee's own RET/EXIT/fall-off
         * balance has to be checked against where *it* started, not
         * against 0 unconditionally. */
        const uint32_t base = vm->depth == 0u ? 0u :
            vm->frames[vm->depth - 1u].savedTop;
        if (ip >= length) {
            if (vm->depth == 0u)
                /* Exact original behavior: a script may end with no
                 * explicit RET, leaving its last expression's value on the
                 * stack as an implicit return -- confirmed against a real
                 * Deltarune script (gml_Script_down_p) which ends this way.
                 * Anything beyond one leftover value is a genuine
                 * imbalance. Left in place (not popped) exactly as before;
                 * every existing caller either checks the bool result only
                 * or force-resets vm.top itself afterward. */
                return ip == length && vm->top <= 1u;
            if (vm->top != base && vm->top != base + 1u) return false;
            if (vm->top == base &&
                !push(vm, (Value){VALUE_UNDEFINED, 0, 0.0, NULL})) return false;
            resume_caller(vm, &bytes, &absolute, &length, &ip);
            continue;
        }
        if (length - ip < 4u) return false;
        const uint32_t instruction = BinaryUtils_readUint32(bytes + ip);
        const uint8_t opcode = (uint8_t)(instruction >> 24u);
        const uint8_t type1 = (uint8_t)((instruction >> 16u) & 15u);
        const uint8_t type2 = (uint8_t)((instruction >> 20u) & 15u);
        const uint32_t address = absolute + ip;
        const uint8_t *extra = bytes + ip + 4u;
        uint32_t next = ip + 4u;
        vm->stats->diagnosticAddress = address;
        vm->stats->diagnosticInstruction = instruction;
        vm->stats->diagnosticStackDepth = vm->top;
        if ((instruction & 0x40000000u) != 0u) {
            const uint32_t extraBytes = (type1 == 0u || type1 == 3u) ? 8u :
                (type1 == 15u ? 0u : 4u);
            if (length - next < extraBytes) return false;
            next += extraBytes;
        }
        ++vm->stats->instructions;
        Value a, b;
        uint32_t reference;
        switch (opcode) {
            case OP_PUSH:
                if (type1 == 0u) {
                    uint64_t bits = BinaryUtils_readUint64(extra);
                    double value;
                    memcpy(&value, &bits, sizeof(value));
                    if (!push(vm, (Value){VALUE_REAL, 0, value, NULL}))
                        return false;
                } else if (type1 == 1u) {
                    /* GML_TYPE_FLOAT: 4-byte float, promoted to this VM's
                     * one double-typed VALUE_REAL representation -- same
                     * as upstream's GMLReal, which is uniformly a double
                     * internally regardless of the wire type (confirmed at
                     * vm.c:1124-1126). */
                    uint32_t bits = BinaryUtils_readUint32(extra);
                    float value;
                    memcpy(&value, &bits, sizeof(value));
                    if (!push(vm, (Value){VALUE_REAL, 0, (double)value, NULL}))
                        return false;
                } else if (type1 == 3u) {
                    /* GML_TYPE_INT64: 8-byte signed literal. Value.value is
                     * already int64_t, so no truncation risk here unlike
                     * PUSHI's 16-bit or type1==2's 32-bit literals. */
                    if (!push(vm, (Value){VALUE_INT,
                        (int64_t)BinaryUtils_readUint64(extra), 0.0, NULL}))
                        return false;
                } else if (type1 == 4u) {
                    if (!push(vm, (Value){VALUE_BOOL,
                        BinaryUtils_readInt32(extra) != 0, 0.0, NULL}))
                        return false;
                } else if (type1 == 6u) {
                    if (!push(vm, (Value){VALUE_STRING,
                        BinaryUtils_readInt32(extra), 0.0, NULL})) return false;
                } else if (type1 == 2u) {
                    /* wad17/GMS2.3+ can also use a type-Int32 push to place
                     * a function reference on the stack for an indirect
                     * call (rather than a plain integer literal) -- this is
                     * the counterpart of FUNC's address-field change above:
                     * every GMS2.3 script opens by pushing a reference to
                     * itself for method(self, <itself>). Confirmed against
                     * real data (FUNC[0]'s first occurrence is such a
                     * PUSH). */
                    if (resolve(vm->funcs, vm->funcRefs, address, &reference)) {
                        if (!push(vm, (Value){VALUE_FUNCTION,
                            (int64_t)reference, 0.0, NULL})) return false;
                    } else if (!push(vm, (Value){VALUE_INT,
                        BinaryUtils_readInt32(extra), 0.0, NULL})) return false;
                } else if (type1 == 5u) {
                    if (!push_variable_reference(vm, address, extra))
                        return false;
                } else return false;
                break;
            case OP_PUSHLOC:
            case OP_PUSHGLB:
                /* Always a variable push regardless of type1 -- confirmed
                 * real data already showed type1==5 for an observed
                 * OP_PUSHGLB, consistent with plain PUSH's own
                 * GML_TYPE_VARIABLE tag, so no divergent handling here. */
                if (!push_variable_reference(vm, address, extra))
                    return false;
                break;
            case OP_PUSHI:
                if (!push(vm, (Value){VALUE_INT,
                    (int16_t)(instruction & 0xffffu), 0.0, NULL})) return false;
                break;
            case OP_DUP:
                {
                    const uint32_t count = (instruction & 0xffu) + 1u;
                    if (count > vm->top || count > VALUE_STACK_MAX - vm->top)
                        return false;
                    const uint32_t start = vm->top - count;
                    /* Duplicating creates an extra alias of a value the
                     * original stack slot still holds -- clone array-typed
                     * duplicates for the same reason a plain variable read
                     * does above. */
                    for (uint32_t i = 0u; i < count; ++i) {
                        Value cloned;
                        if (!array_clone(vm->stack[start + i], &cloned))
                            return false;
                        vm->stack[vm->top++] = cloned;
                    }
                }
                break;
            case OP_CONV:
                if (!pop(vm, &a)) return false;
                /* YoYo encodes Conv.<source>.<destination>. Keep the value
                 * payload intact when the destination is the boxed
                 * "variable" type used by builtin arguments. */
                if (type1 == 6u && type2 == 5u && a.kind == VALUE_STRING) {
                    if (!push(vm, a)) return false;
                } else if (type1 == 2u && type2 == 5u &&
                           (a.kind == VALUE_INT || a.kind == VALUE_FUNCTION)) {
                    if (!push(vm, a)) return false;
                } else if (type1 == 5u && type2 == 4u &&
                           a.kind == VALUE_BOOL) {
                    if (!push(vm, a)) return false;
                } else if (type1 == 2u && type2 == 0u &&
                           a.kind == VALUE_INT) {
                    if (!push(vm, (Value){VALUE_REAL, 0, (double)a.value, NULL}))
                        return false;
                } else if (type1 == 0u && type2 == 2u &&
                           a.kind == VALUE_REAL &&
                           a.real >= (double)INT32_MIN &&
                           a.real <= (double)INT32_MAX) {
                    if (!push(vm, (Value){VALUE_INT, (int32_t)a.real, 0.0, NULL}))
                        return false;
                } else if (type2 == 5u && (a.kind == VALUE_REAL ||
                           a.kind == VALUE_BOOL)) {
                    if (!push(vm, a)) return false;
                } else return false;
                break;
            case OP_CALL:
                {
                    if (!resolve(vm->funcs, vm->funcRefs, address, &reference))
                        return false;
                    const uint32_t argCount = instruction & 0xffffu;
                    uint32_t entryPointer;
                    if (find_code_entry(vm, reference, &entryPointer)) {
                        /* A real user-defined GML script/function -- not a
                         * builtin. Set up a nested frame instead of running
                         * it recursively through C: pop its arguments,
                         * save where to resume in the caller, and switch
                         * the trampoline over to the callee's bytecode. */
                        uint32_t locals, arguments, calleeAbsolute, calleeLength;
                        if (!resolve_code_entry(vm, entryPointer, &locals,
                                &arguments, &calleeAbsolute, &calleeLength) ||
                            vm->depth >= CALL_STACK_MAX ||
                            argCount > VM_ARG_SLOTS || locals > VM_LOCAL_SLOTS)
                            return false;
                        Value args[VM_ARG_SLOTS];
                        for (uint32_t k = argCount; k-- > 0u;)
                            if (!pop(vm, &args[k])) return false;
                        Frame *frame = &vm->frames[vm->depth];
                        frame->bytes = bytes;
                        frame->absolute = absolute;
                        frame->length = length;
                        frame->ip = next;
                        frame->savedTop = vm->top;
                        ++vm->depth;
                        Value *calleeLocals = frame_locals(vm, vm->depth);
                        memset(calleeLocals, 0, VM_FRAME_SLOTS * sizeof(Value));
                        for (uint32_t k = 0u; k < argCount; ++k)
                            calleeLocals[k] = args[k];
                        bytes = vm->file + calleeAbsolute;
                        absolute = calleeAbsolute;
                        length = calleeLength;
                        ip = 0u;
                        continue;
                    }
                    if (!call_builtin(vm, reference, argCount)) return false;
                }
                break;
            case OP_POPZ:
                if (!pop(vm, &a)) return false;
                array_release(a);
                break;
            case OP_RET:
                if (!pop(vm, &a) || vm->top != base) return false;
                if (vm->depth == 0u) {
                    /* Matches the pre-existing, unchanged convention that a
                     * top-level RET's value is discarded -- now that a
                     * value can own heap storage, discarding it must also
                     * release it. */
                    array_release(a);
                    return true;
                }
                resume_caller(vm, &bytes, &absolute, &length, &ip);
                if (!push(vm, a)) return false;
                continue;
            case OP_EXIT:
                if (vm->top != base) return false;
                if (vm->depth == 0u) return true;
                resume_caller(vm, &bytes, &absolute, &length, &ip);
                if (!push(vm, (Value){VALUE_UNDEFINED, 0, 0.0, NULL})) return false;
                continue;
            case OP_B: case OP_BT: case OP_BF:
                {
                    int64_t condition = 0;
                    if (opcode != OP_B) {
                        if (!pop(vm, &a) || (a.kind != VALUE_BOOL &&
                                             a.kind != VALUE_INT))
                            return false;
                        condition = a.value;
                    }
                ++vm->stats->branches;
                if (branch_taken(opcode, condition)) {
                    const int32_t offset = ((int32_t)(instruction << 9u)) >> 7u;
                    const int64_t target = (int64_t)ip + offset;
                    if (target < 0 || target > length) return false;
                    next = (uint32_t)target;
                    ++vm->stats->branchesTaken;
                }
                }
                break;
            case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_XOR:
            case OP_SHL: case OP_SHR:
                if (!pop(vm, &b) || !pop(vm, &a)) return false;
                {
                    Value result;
                    if (!numeric_binary(opcode, a, b, &result) ||
                        !push(vm, result)) return false;
                }
                break;
            case OP_NEG: case OP_NOT:
                if (!pop(vm, &a)) return false;
                if (opcode == OP_NEG && a.kind == VALUE_REAL) {
                    if (!push(vm, (Value){VALUE_REAL, 0, -a.real, NULL}))
                        return false;
                } else if ((a.kind != VALUE_INT && a.kind != VALUE_BOOL) ||
                    !push(vm, (Value){opcode == OP_NOT ? VALUE_BOOL :
                        VALUE_INT, opcode == OP_NOT ? !a.value : -a.value,
                        0.0, NULL})) return false;
                break;
            case OP_CMP:
                if (!pop(vm, &b) || !pop(vm, &a)) return false;
                reference = (instruction >> 8u) & 0xffu;
                {
                    double left, right;
                    if (!numeric_value(a, &left) || !numeric_value(b, &right))
                        return false;
                    const int64_t result = reference == 1u ? left < right :
                        reference == 2u ? left <= right :
                        reference == 3u ? left == right :
                        reference == 4u ? left != right :
                        reference == 5u ? left >= right : left > right;
                    if (reference < 1u || reference > 6u ||
                        !push(vm, (Value){VALUE_BOOL, result, 0.0, NULL}))
                        return false;
                }
                break;
            case OP_POP:
                /* Real variables are dynamically typed -- confirmed against
                 * real data, where a script's compiled preamble stores a
                 * bound method (VALUE_FUNCTION) into a variable, not just
                 * plain numbers. */
                {
                    Value *slot;
                    if (!pop(vm, &a) ||
                        !resolve(vm->vars, vm->varRefs, address, &reference) ||
                        (slot = variable_slot(vm, reference)) == NULL)
                        return false;
                    /* `a` is always already independently owned by this
                     * point (a fresh allocation, or already cloned by
                     * whichever push produced it) -- release the slot's old
                     * content and move `a` in directly, no clone needed. */
                    array_release(*slot);
                    *slot = a;
                }
                ++vm->stats->variableStores;
                break;
            case OP_BREAK:
                {
                    const int16_t sub = (int16_t)(instruction & 0xffffu);
                    if (sub == BREAK_SETOWNER) {
                        /* CoW scope-owner token -- irrelevant to this
                         * file's always-copy array model (see the
                         * array_release/array_clone comment), but still a
                         * real stack pop that must be consumed and, if
                         * somehow array-typed, released. */
                        if (!pop(vm, &a)) return false;
                        array_release(a);
                    } else if (sub == BREAK_PUSHAF) {
                        if (!break_pushaf(vm)) return false;
                    } else if (sub == BREAK_POPAF) {
                        if (!break_popaf(vm)) return false;
                    } else return false;
                }
                break;
            default:
                return false;
        }
        ip = next;
    }
}

bool DemonVm_integerOpcodeSelfTest(uint32_t *operations,
                                   uint32_t *comparisons,
                                   uint32_t *branches) {
    if (operations == NULL || comparisons == NULL || branches == NULL)
        return false;
    const uint8_t opcodes[] = {OP_MUL, OP_DIV, OP_REM, OP_MOD, OP_ADD,
        OP_SUB, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR};
    const int64_t expected[] = {18, 2, 0, 0, 9, 3, 2, 7, 5, 48, 0};
    for (uint32_t i = 0u; i < sizeof(opcodes); ++i) {
        int64_t result;
        if (!integer_binary(opcodes[i], 6, 3, &result) ||
            result != expected[i]) return false;
    }
    for (uint32_t kind = 1u; kind <= 6u; ++kind) {
        int64_t result;
        if (!integer_compare(kind, 3, 4, &result)) return false;
        const int64_t expectedComparison =
            (kind == 1u || kind == 2u || kind == 4u) ? 1 : 0;
        if (result != expectedComparison) return false;
    }
    const int64_t negated = -((int64_t)7);
    const bool notZero = !((int64_t)0);
    const bool notThree = !((int64_t)3);
    if (negated != -7 || !notZero || notThree) return false;
    if (!branch_taken(OP_B, 0) || !branch_taken(OP_BT, 1) ||
        branch_taken(OP_BT, 0) || !branch_taken(OP_BF, 0) ||
        branch_taken(OP_BF, 1)) return false;
    *operations = (uint32_t)sizeof(opcodes) + 2u;
    *comparisons = 6u;
    *branches = 3u;
    return true;
}

bool DemonVm_rvalueSelfTest(uint32_t *conversions, uint32_t *realOperations) {
    if (conversions == NULL || realOperations == NULL) return false;
    const Value integer = {VALUE_INT, 7, 0.0, NULL};
    const Value real = {VALUE_REAL, 0, 2.5, NULL};
    Value result;
    double converted;
    if (!numeric_value(integer, &converted) || converted != 7.0 ||
        !numeric_binary(OP_ADD, integer, real, &result) ||
        result.kind != VALUE_REAL || result.real != 9.5 ||
        !numeric_binary(OP_MUL, real, (Value){VALUE_REAL, 0, 2.0, NULL},
                        &result) || result.real != 5.0 ||
        !numeric_binary(OP_DIV, integer, real, &result) ||
        result.real != 2.8 ||
        numeric_binary(OP_DIV, real, (Value){VALUE_REAL, 0, 0.0, NULL}, &result))
        return false;
    *conversions = 3u;
    *realOperations = 3u;
    return true;
}

bool DemonVm_controlOpcodeSelfTest(uint32_t *instructions,
                                   uint32_t *duplicates,
                                   uint32_t *terminators) {
    if (instructions == NULL || duplicates == NULL || terminators == NULL)
        return false;
    const uint32_t program[] = {
        0x84000007u, /* PushI 7 */
        0x86000000u, /* Dup top */
        0x0c000000u, /* Add */
        0x9e000000u, /* Popz */
        0x9d000000u  /* Exit */
    };
    DemonVmExecutionStats stats;
    memset(&stats, 0, sizeof(stats));
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.stats = &stats;
    if (!execute_code(&vm, (const uint8_t *)program, 0u, sizeof(program)) ||
        stats.instructions != 5u || vm.top != 0u) return false;
    const uint32_t invalidDup = 0x86000000u;
    memset(&stats, 0, sizeof(stats));
    memset(&vm, 0, sizeof(vm));
    vm.stats = &stats;
    if (execute_code(&vm, (const uint8_t *)&invalidDup, 0u,
                     sizeof(invalidDup))) return false;
    *instructions = 5u;
    *duplicates = 1u;
    *terminators = 2u; /* Ret and Exit are both implemented. */
    return true;
}

/* Proves nested GML script calls end to end with a small hand-built
 * synthetic file -- no real game data needed, unlike the D8 probe in
 * core_main.c which exercises this same call path against real Deltarune
 * Ch1 scripts. Layout (single buffer, byte offsets chosen by hand and
 * cross-checked below rather than computed, so a mistake here fails loudly
 * instead of silently drifting):
 *
 *   [0..4)    uint32 len=1            \_ the name "b", shared by FUNC's
 *   [4..5)    "b"                     /  and CODE's entry (namePtr=4)
 *   [8..20)   VARI header (12 B, unread beyond size)
 *   [20..40)  VARI record 0: argument0 of "b" (instanceType=-6, varIndex=0),
 *             firstAddress=84 (B's first instruction, checked below)
 *   [40..44)  FUNC header: functionCount=1
 *   [44..56)  FUNC record 0: name "b", firstAddress=108 (A's CALL, checked
 *             below)
 *   [56..60)  CODE header: count=1
 *   [60..64)  CODE pointer table: entry 0 -> offset 64
 *   [64..84)  CODE entry 0 ("b"): length=20, locals=1, arguments=1,
 *             bytecode at 84 (relative field at 76 + 8)
 *   [84..104) B's bytecode (20 bytes): push argument0 (+4 padding -- see
 *             below); pushi 10; add; ret -- i.e. "return argument0 + 10"
 *   [104..112) A's bytecode (8 bytes, executed directly, not looked up by
 *             name): pushi 5; call func#0 with 1 arg. Deliberately has no
 *             trailing RET -- see the comment further below on why.
 *
 * Two opcodes here (PUSH and CALL) set bit 30 of their instruction word
 * simply by virtue of their opcode byte (0xc0 and 0xd9 both have bit 6 set)
 * -- execute_code's generic pre-decode step reads that bit as "this
 * instruction carries extra operand bytes" independent of what the
 * opcode's own case actually does with them, sized by type1 (4 bytes for
 * most types, 8 for type 0/3, 0 for type 15). PUSH's variable-read form
 * (type1=5) doesn't consume that operand itself -- this VM resolves
 * variables by instruction address, not embedded operand -- but still
 * must skip the 4 bytes of real space GameMaker gives it, hence the
 * explicit padding word after B's PUSH below. CALL sidesteps the same
 * trap by using type1=15 (0 extra bytes) since this VM doesn't read
 * CALL's operand either (functions are also resolved by address).
 *
 * Running A calls B(5), which computes 5+10 and returns it; A returns
 * whatever B returned. A correct result of 15 exercises argument passing,
 * frame push, per-call local storage, and return-value propagation
 * together -- any one of them being wrong breaks the final value or trips
 * one of execute_code's fail-closed stack-balance checks. */
bool DemonVm_callFrameSelfTest(int64_t *result) {
    if (result == NULL) return false;
    /* 116, not 112 (A's bytecode ends at 112): add_chain's bounds check
     * requires address+8 <= fileSize for *any* reference occurrence, even
     * a lone one with no continuation, so the FUNC reference at A's CALL
     * (address 108) needs 4 bytes of file to still exist past it. The
     * trailing 4 bytes are otherwise unused padding -- execute_code is
     * called with an explicit length of 8, so it never reads them. */
    uint8_t file[116];
    memset(file, 0, sizeof(file));
    BinaryUtils_writeUint32(file + 0, 1u);
    file[4] = (uint8_t)'b';

    BinaryUtils_writeUint32(file + 20, 0u);             /* namePtr: unread by this file's code */
    BinaryUtils_writeUint32(file + 24, (uint32_t)-6);   /* instanceType: VARI_SCOPE_ARGUMENT */
    BinaryUtils_writeUint32(file + 28, 0u);             /* varIndex: argument0 */
    BinaryUtils_writeUint32(file + 32, 1u);             /* occurrences */
    BinaryUtils_writeUint32(file + 36, 84u);            /* firstAddress: B's PUSH */

    BinaryUtils_writeUint32(file + 40, 1u);             /* FUNC functionCount */
    BinaryUtils_writeUint32(file + 44, 4u);             /* FUNC[0] namePtr -> "b" */
    BinaryUtils_writeUint32(file + 48, 1u);             /* occurrences */
    BinaryUtils_writeUint32(file + 52, 108u);           /* firstAddress: A's CALL */

    BinaryUtils_writeUint32(file + 56, 1u);             /* CODE count */
    BinaryUtils_writeUint32(file + 60, 64u);            /* CODE[0] entry pointer */
    BinaryUtils_writeUint32(file + 64, 4u);             /* entry namePtr -> "b" */
    BinaryUtils_writeUint32(file + 68, 20u);            /* bytecode length */
    BinaryUtils_writeUint16(file + 72, 1u);             /* locals */
    BinaryUtils_writeUint16(file + 74, 1u);             /* arguments */
    BinaryUtils_writeUint32(file + 76, 8u);             /* relative: 76+8=84 */

    /* B: return argument0 + 10 */
    BinaryUtils_writeUint32(file + 84, 0xc0050000u);    /* PUSH var (argument0) */
    BinaryUtils_writeUint32(file + 88, 0u);             /* (4 operand bytes GameMaker
                                                          * reserves here; unread) */
    BinaryUtils_writeUint32(file + 92, 0x8400000au);    /* PUSHI 10 */
    BinaryUtils_writeUint32(file + 96, 0x0c000000u);    /* ADD */
    BinaryUtils_writeUint32(file + 100, 0x9c000000u);   /* RET */

    /* A: b(5), with no explicit RET -- top-level RET discards its value
     * (matching the pre-existing, unchanged behavior: executeEventsState's
     * top-level dispatches never cared what a script returned), so this
     * self-test instead relies on the existing "falls off the end with the
     * last expression's value still on the stack" convention to leave B's
     * return value in place for inspection below. */
    BinaryUtils_writeUint32(file + 104, 0x84000005u);   /* PUSHI 5 */
    BinaryUtils_writeUint32(file + 108, 0xd90f0001u);   /* CALL func#0, 1 arg, type1=15 */

    DemonDataWinIndex index;
    memset(&index, 0, sizeof(index));
    index.count = 2u;
    index.chunks[0] = (DemonDataWinChunk){
        DATAWIN_TAG('V', 'A', 'R', 'I'), 0u, 8u, 32u};
    index.chunks[1] = (DemonDataWinChunk){
        DATAWIN_TAG('F', 'U', 'N', 'C'), 0u, 40u, 16u};
    const DemonDataWinChunk code = {
        DATAWIN_TAG('C', 'O', 'D', 'E'), 0u, 56u, 48u};

    BinaryReader reader = BinaryReader_create(NULL, sizeof(file));
    BinaryReader_setBuffer(&reader, file, 0u, sizeof(file));

    DemonVmExecutionStats stats;
    memset(&stats, 0, sizeof(stats));
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.file = file;
    vm.fileSize = sizeof(file);
    vm.func = DemonDataWinIndex_find(&index, DATAWIN_TAG('F', 'U', 'N', 'C'));
    vm.code = &code;
    vm.stats = &stats;
    if (!build_references(&reader, &index, &vm, 16u)) {
        vm_destroy(&vm);
        return false;
    }
    const bool ok = execute_code(&vm, file + 104u, 104u, 8u);
    const bool balanced = ok && vm.top == 1u;
    Value returned = {VALUE_UNDEFINED, 0, 0.0, NULL};
    if (balanced) (void)pop(&vm, &returned);
    vm_destroy(&vm);
    if (!balanced || returned.kind != VALUE_INT || returned.value != 15)
        return false;
    *result = returned.value;
    return true;
}

/* Proves array creation, element read, and element write end to end with a
 * small hand-built synthetic file -- same style as
 * DemonVm_callFrameSelfTest above. Layout:
 *
 *   [0..4)    uint32 len=15                \_ the builtin name
 *   [4..19)   "@@NewGMLArray@@" (15 bytes)  /  (namePtr=4)
 *   [20..32)  VARI header (12 B, unread beyond size)
 *   [32..52)  VARI record 0: "arr" (instanceType=-1/self, so it takes the
 *             pre-existing flat/global VAR_SCOPE_FLAT path unchanged --
 *             this test isn't exercising call-frame locals), occurrences=4,
 *             firstAddress=92 (the POP below)
 *   [52..56)  FUNC header: functionCount=1
 *   [56..68)  FUNC record 0: "@@NewGMLArray@@", firstAddress=88 (the CALL)
 *   [68..156) bytecode (88 bytes):
 *     68  pushi 0                 ; scope-owner token (unused, see below)
 *     72  break setowner          ; no-op in this file's array model
 *     76  pushi 10 } 80 pushi 20 } 84 pushi 30   ; array literal elements
 *     88  call func#0 argc=3      ; arr = [10, 20, 30]
 *     92  pop arr                 ; occurrence 1 (VARI chain starts here)
 *    100  pushi 99
 *    104  push arr (tag=ARRAYPOPAF)   ; occurrence 2 -- feeds break_popaf
 *    112  pushi 1
 *    116  break popaf             ; arr[1] = 99
 *    120  push arr (tag=ARRAYPUSHAF)  ; occurrence 3 -- feeds break_pushaf
 *    128  pushi 0
 *    132  break pushaf            ; push arr[0] (== 10, untouched by the
 *                                 ; write above -- proves POPAF didn't
 *                                 ; clobber a neighboring element)
 *    136  push arr (tag=ARRAYPUSHAF)  ; occurrence 4 (last -- no chain delta)
 *    144  pushi 1
 *    148  break pushaf            ; push arr[1] (== 99, proves the write
 *                                 ; above is visible through a later read
 *                                 ; of the same variable -- the shared-
 *                                 ; reference push, not a stale clone)
 *    152  add                     ; 10 + 99 = 109, left on the stack for
 *                                 ; the natural-end "falls off the end"
 *                                 ; convention, same as the call-frame test
 *
 * PUSH-var's own operand word (the 4 bytes right after each "push arr"
 * instruction, confirmed real at vm.h:28-31,118-119) does double duty here
 * exactly like add_chain's occurrence-chain delta already did in the
 * call-frame test: its low 27 bits are the delta to the next VARI
 * occurrence, and its top byte is the VARTYPE tag execute_code now reads
 * (ARRAYPOPAF=0x90 for the write's array-ref push, ARRAYPUSHAF=0x10 for
 * both reads) -- both pieces of information genuinely coexist in that one
 * word in real compiled GameMaker bytecode. */
bool DemonVm_arraySelfTest(int64_t *result) {
    if (result == NULL) return false;
    uint8_t file[156];
    memset(file, 0, sizeof(file));
    BinaryUtils_writeUint32(file + 0, 15u);
    memcpy(file + 4, "@@NewGMLArray@@", 15u);

    BinaryUtils_writeUint32(file + 32, 0u);             /* namePtr: unread */
    BinaryUtils_writeUint32(file + 36, (uint32_t)-1);   /* instanceType: self */
    BinaryUtils_writeUint32(file + 40, 0u);             /* varIndex: unused */
    BinaryUtils_writeUint32(file + 44, 4u);             /* occurrences */
    BinaryUtils_writeUint32(file + 48, 92u);            /* firstAddress: POP */

    BinaryUtils_writeUint32(file + 52, 1u);             /* FUNC functionCount */
    BinaryUtils_writeUint32(file + 56, 4u);             /* FUNC[0] namePtr */
    BinaryUtils_writeUint32(file + 60, 1u);             /* occurrences */
    BinaryUtils_writeUint32(file + 64, 88u);            /* firstAddress: CALL */

    BinaryUtils_writeUint32(file + 68, 0x84000000u);    /* pushi 0 */
    BinaryUtils_writeUint32(file + 72, 0xff0ffffbu);    /* break setowner */
    BinaryUtils_writeUint32(file + 76, 0x8400000au);    /* pushi 10 */
    BinaryUtils_writeUint32(file + 80, 0x84000014u);    /* pushi 20 */
    BinaryUtils_writeUint32(file + 84, 0x8400001eu);    /* pushi 30 */
    BinaryUtils_writeUint32(file + 88, 0xd90f0003u);    /* call func#0, argc=3 */
    BinaryUtils_writeUint32(file + 92, 0x45050000u);    /* pop arr */
    BinaryUtils_writeUint32(file + 96, 12u);            /* chain delta: 92->104 */
    BinaryUtils_writeUint32(file + 100, 0x84000063u);   /* pushi 99 */
    BinaryUtils_writeUint32(file + 104, 0xc0050000u);   /* push arr */
    BinaryUtils_writeUint32(file + 108, 0x90000010u);   /* tag=ARRAYPOPAF, delta 16 */
    BinaryUtils_writeUint32(file + 112, 0x84000001u);   /* pushi 1 */
    BinaryUtils_writeUint32(file + 116, 0xff0ffffdu);   /* break popaf */
    BinaryUtils_writeUint32(file + 120, 0xc0050000u);   /* push arr */
    BinaryUtils_writeUint32(file + 124, 0x10000010u);   /* tag=ARRAYPUSHAF, delta 16 */
    BinaryUtils_writeUint32(file + 128, 0x84000000u);   /* pushi 0 */
    BinaryUtils_writeUint32(file + 132, 0xff0ffffeu);   /* break pushaf */
    BinaryUtils_writeUint32(file + 136, 0xc0050000u);   /* push arr */
    BinaryUtils_writeUint32(file + 140, 0x10000000u);   /* tag=ARRAYPUSHAF, last */
    BinaryUtils_writeUint32(file + 144, 0x84000001u);   /* pushi 1 */
    BinaryUtils_writeUint32(file + 148, 0xff0ffffeu);   /* break pushaf */
    BinaryUtils_writeUint32(file + 152, 0x0c000000u);   /* add */

    DemonDataWinIndex index;
    memset(&index, 0, sizeof(index));
    index.count = 2u;
    index.chunks[0] = (DemonDataWinChunk){
        DATAWIN_TAG('V', 'A', 'R', 'I'), 0u, 20u, 32u};
    index.chunks[1] = (DemonDataWinChunk){
        DATAWIN_TAG('F', 'U', 'N', 'C'), 0u, 52u, 16u};

    BinaryReader reader = BinaryReader_create(NULL, sizeof(file));
    BinaryReader_setBuffer(&reader, file, 0u, sizeof(file));

    DemonVmExecutionStats stats;
    memset(&stats, 0, sizeof(stats));
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.file = file;
    vm.fileSize = sizeof(file);
    vm.func = DemonDataWinIndex_find(&index, DATAWIN_TAG('F', 'U', 'N', 'C'));
    vm.stats = &stats;
    if (!build_references(&reader, &index, &vm, 16u)) {
        vm_destroy(&vm);
        return false;
    }
    const bool ok = execute_code(&vm, file + 68u, 68u, 88u);
    const bool balanced = ok && vm.top == 1u;
    Value returned = {VALUE_UNDEFINED, 0, 0.0, NULL};
    if (balanced) (void)pop(&vm, &returned);
    array_release(returned);
    vm_destroy(&vm);
    if (!balanced || returned.kind != VALUE_INT || returned.value != 109)
        return false;
    *result = returned.value;
    return true;
}

/* Proves the four trivial GMS2.3-compiler-emitted marker builtins added
 * alongside call_builtin's arity-gate fix -- @@NullObject@@, @@Global@@,
 * @@This@@, @@Other@@ (with and without an "other" instance set) -- each
 * called with 0 arguments via a real CALL instruction, same synthetic-file
 * style as DemonVm_arraySelfTest. Layout:
 *
 *   [0..18)   name "@@NullObject@@" (len-prefixed, namePtr=4)
 *   [20..34)  name "@@Global@@" (namePtr=24)
 *   [36..48)  name "@@This@@" (namePtr=40)
 *   [48..61)  name "@@Other@@" (namePtr=52)
 *   [64..76)  VARI header only (12 B, 0 records -- these builtins read no
 *             variables)
 *   [76..80)  FUNC functionCount=4
 *   [80..128) FUNC records 0-3, one per name above
 *   [128..144) four independent 1-instruction CALL programs, one per
 *             builtin, each run through execute_code separately (type1=15
 *             so the extra-bytes formula gives 0, matching the earlier
 *             call-frame test's CALL encoding -- no operand bytes needed
 *             since this VM resolves the callee by instruction address,
 *             not the embedded operand). */
bool DemonVm_markerBuiltinSelfTest(void) {
    /* 148, not 144: add_chain's bounds check requires address+8 <=
     * fileSize for every occurrence, even the last (func#3's CALL at 140
     * needs 4 bytes of file to still exist past it) -- same requirement
     * the call-frame self-test hit and padded for. */
    uint8_t file[148];
    memset(file, 0, sizeof(file));

    BinaryUtils_writeUint32(file + 0, 14u);
    memcpy(file + 4, "@@NullObject@@", 14u);
    BinaryUtils_writeUint32(file + 20, 10u);
    memcpy(file + 24, "@@Global@@", 10u);
    BinaryUtils_writeUint32(file + 36, 8u);
    memcpy(file + 40, "@@This@@", 8u);
    BinaryUtils_writeUint32(file + 48, 9u);
    memcpy(file + 52, "@@Other@@", 9u);

    BinaryUtils_writeUint32(file + 76, 4u);   /* FUNC functionCount */
    BinaryUtils_writeUint32(file + 80, 4u);   /* FUNC[0] namePtr: NullObject */
    BinaryUtils_writeUint32(file + 84, 1u);
    BinaryUtils_writeUint32(file + 88, 128u);
    BinaryUtils_writeUint32(file + 92, 24u);  /* FUNC[1] namePtr: Global */
    BinaryUtils_writeUint32(file + 96, 1u);
    BinaryUtils_writeUint32(file + 100, 132u);
    BinaryUtils_writeUint32(file + 104, 40u); /* FUNC[2] namePtr: This */
    BinaryUtils_writeUint32(file + 108, 1u);
    BinaryUtils_writeUint32(file + 112, 136u);
    BinaryUtils_writeUint32(file + 116, 52u); /* FUNC[3] namePtr: Other */
    BinaryUtils_writeUint32(file + 120, 1u);
    BinaryUtils_writeUint32(file + 124, 140u);

    BinaryUtils_writeUint32(file + 128, 0xd90f0000u); /* call @@NullObject@@ */
    BinaryUtils_writeUint32(file + 132, 0xd90f0000u); /* call @@Global@@ */
    BinaryUtils_writeUint32(file + 136, 0xd90f0000u); /* call @@This@@ */
    BinaryUtils_writeUint32(file + 140, 0xd90f0000u); /* call @@Other@@ */

    DemonDataWinIndex index;
    memset(&index, 0, sizeof(index));
    index.count = 2u;
    index.chunks[0] = (DemonDataWinChunk){
        DATAWIN_TAG('V', 'A', 'R', 'I'), 0u, 64u, 12u};
    index.chunks[1] = (DemonDataWinChunk){
        DATAWIN_TAG('F', 'U', 'N', 'C'), 0u, 76u, 52u};

    BinaryReader reader = BinaryReader_create(NULL, sizeof(file));
    BinaryReader_setBuffer(&reader, file, 0u, sizeof(file));

    DemonVmExecutionStats stats;
    memset(&stats, 0, sizeof(stats));
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.file = file;
    vm.fileSize = sizeof(file);
    vm.func = DemonDataWinIndex_find(&index, DATAWIN_TAG('F', 'U', 'N', 'C'));
    vm.stats = &stats;
    vm.selfInstanceId = 42;
    vm.otherInstanceId = -1;
    if (!build_references(&reader, &index, &vm, 16u)) {
        vm_destroy(&vm);
        return false;
    }

    Value result;
    bool ok = execute_code(&vm, file + 128u, 128u, 4u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT && result.value == -4;
    ok = ok && execute_code(&vm, file + 132u, 132u, 4u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT && result.value == -5;
    ok = ok && execute_code(&vm, file + 136u, 136u, 4u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT && result.value == 42;
    /* Other with no "other" set falls back to self (42). */
    ok = ok && execute_code(&vm, file + 140u, 140u, 4u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT && result.value == 42;
    vm.otherInstanceId = 99;
    ok = ok && execute_code(&vm, file + 140u, 140u, 4u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT && result.value == 99;

    vm_destroy(&vm);
    return ok;
}

/* Proves OP_PUSHLOC/OP_PUSHGLB (dedicated local/global variable-push
 * opcodes real GameMaker bytecode uses instead of the generic
 * OP_PUSH type1==5 path -- confirmed real data hit OP_PUSHGLB) and the
 * OP_PUSH literal types this file didn't have cases for yet
 * (type1==3 int64, type1==1 float), same synthetic-file style as
 * DemonVm_arraySelfTest. Layout:
 *
 *   [0..52)   VARI: header (12B) + 2 records -- "g" (flat/self scope,
 *             occurrences at the POP/PUSHGLB below) and "loc" (local
 *             scope, occurrences at the POP/PUSHLOC below)
 *   [52..56)  FUNC: functionCount=0 (no calls in this test)
 *   [56..108) bytecode: pushes an 8-byte int64 literal (5000000000 --
 *             deliberately past INT32_MAX, so a truncation bug would
 *             produce a visibly wrong result) and stores it into "g" via
 *             plain POP; stores 42 into "loc"; reads both back via
 *             PUSHGLB/PUSHLOC (not the generic PUSH path) and adds them,
 *             left on the stack for the natural-end convention
 *   [108..116) a second, independent snippet: pushes a 4-byte float
 *             literal (2.5), run as its own execute_code call reusing the
 *             same build_references-backed Vm. */
bool DemonVm_wideOpcodeSelfTest(void) {
    uint8_t file[116];
    memset(file, 0, sizeof(file));

    BinaryUtils_writeUint32(file + 16, (uint32_t)-1);   /* VARI[0] "g": self scope */
    BinaryUtils_writeUint32(file + 20, 0u);
    BinaryUtils_writeUint32(file + 24, 2u);             /* occurrences */
    BinaryUtils_writeUint32(file + 28, 68u);            /* firstAddress: POP g */

    BinaryUtils_writeUint32(file + 36, (uint32_t)-7);   /* VARI[1] "loc": local scope */
    BinaryUtils_writeUint32(file + 40, 0u);             /* varIndex 0 */
    BinaryUtils_writeUint32(file + 44, 2u);             /* occurrences */
    BinaryUtils_writeUint32(file + 48, 80u);            /* firstAddress: POP loc */

    BinaryUtils_writeUint32(file + 52, 0u);             /* FUNC functionCount */

    BinaryUtils_writeUint32(file + 56, 0xc0030000u);    /* PUSH int64 (type1=3) */
    BinaryUtils_writeInt64(file + 60, 5000000000LL);
    BinaryUtils_writeUint32(file + 68, 0x45050000u);    /* POP g */
    BinaryUtils_writeUint32(file + 72, 20u);            /* chain delta g: 68->88 */
    BinaryUtils_writeUint32(file + 76, 0x8400002au);    /* PUSHI 42 */
    BinaryUtils_writeUint32(file + 80, 0x45050000u);    /* POP loc */
    BinaryUtils_writeUint32(file + 84, 16u);            /* chain delta loc: 80->96 */
    BinaryUtils_writeUint32(file + 88, 0xc2050000u);    /* PUSHGLB g */
    BinaryUtils_writeUint32(file + 92, 0xa0000000u);    /* tag: normal, last occurrence */
    BinaryUtils_writeUint32(file + 96, 0xc1050000u);    /* PUSHLOC loc */
    BinaryUtils_writeUint32(file + 100, 0xa0000000u);   /* tag: normal, last occurrence */
    BinaryUtils_writeUint32(file + 104, 0x0c000000u);   /* ADD */

    BinaryUtils_writeUint32(file + 108, 0xc0010000u);   /* PUSH float (type1=1) */
    BinaryUtils_writeFloat32(file + 112, 2.5f);

    DemonDataWinIndex index;
    memset(&index, 0, sizeof(index));
    index.count = 2u;
    index.chunks[0] = (DemonDataWinChunk){
        DATAWIN_TAG('V', 'A', 'R', 'I'), 0u, 0u, 52u};
    index.chunks[1] = (DemonDataWinChunk){
        DATAWIN_TAG('F', 'U', 'N', 'C'), 0u, 52u, 4u};

    BinaryReader reader = BinaryReader_create(NULL, sizeof(file));
    BinaryReader_setBuffer(&reader, file, 0u, sizeof(file));

    DemonVmExecutionStats stats;
    memset(&stats, 0, sizeof(stats));
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.file = file;
    vm.fileSize = sizeof(file);
    vm.func = DemonDataWinIndex_find(&index, DATAWIN_TAG('F', 'U', 'N', 'C'));
    vm.stats = &stats;
    if (!build_references(&reader, &index, &vm, 16u)) {
        vm_destroy(&vm);
        return false;
    }

    Value result;
    bool ok = execute_code(&vm, file + 56u, 56u, 52u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_INT &&
        result.value == 5000000042LL;
    ok = ok && execute_code(&vm, file + 108u, 108u, 8u) && vm.top == 1u &&
        pop(&vm, &result) && result.kind == VALUE_REAL && result.real == 2.5;

    vm_destroy(&vm);
    return ok;
}

bool DemonVm_executeFixture(BinaryReader *reader,
                            const DemonDataWinIndex *index,
                            uint8_t wadVersion,
                            DemonVmExecutionStats *stats) {
    return DemonVm_executeFixtureInput(reader, index, wadVersion,
        DEMON_VM_KEY_LEFT | DEMON_VM_KEY_DOWN, 0, 0, stats);
}

bool DemonVm_executeFixtureInput(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 uint8_t wadVersion, uint32_t keyMask,
                                 int32_t initialX, int32_t initialY,
                                 DemonVmExecutionStats *stats) {
    const uint32_t codeIds[4] = {0u, 1u, 2u, 3u};
    return DemonVm_executeEvents(reader, index, wadVersion, keyMask,
                                 codeIds, 4u, initialX, initialY, stats);
}

bool DemonVm_executeEvents(BinaryReader *reader,
                           const DemonDataWinIndex *index,
                           uint8_t wadVersion, uint32_t keyMask,
                           const uint32_t *codeIds, uint32_t codeIdCount,
                           int32_t initialX, int32_t initialY,
                           DemonVmExecutionStats *stats) {
    DemonVmInstanceState state;
    DemonVm_initInstanceState(&state, initialX, initialY);
    return DemonVm_executeEventsState(reader, index, wadVersion, keyMask,
                                      codeIds, codeIdCount, &state, stats);
}

void DemonVm_initInstanceState(DemonVmInstanceState *state,
                               int32_t initialX, int32_t initialY) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->variables[3] = initialX;
    state->variables[4] = initialY;
    state->instanceId = -1;
    state->otherInstanceId = -1;
    state->initialized = true;
}

void DemonVm_setInstanceContext(DemonVmInstanceState *state,
                                int32_t instanceId, int32_t otherInstanceId) {
    if (state == NULL) return;
    state->instanceId = instanceId;
    state->otherInstanceId = otherInstanceId;
}

bool DemonVm_executeEventsState(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint8_t wadVersion, uint32_t keyMask,
                                const uint32_t *codeIds, uint32_t codeIdCount,
                                DemonVmInstanceState *state,
                                DemonVmExecutionStats *stats) {
    const DemonDataWinChunk *code = DemonDataWinIndex_find(index,
        DATAWIN_TAG('C', 'O', 'D', 'E'));
    const DemonDataWinChunk *strg = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    const DemonDataWinChunk *func = DemonDataWinIndex_find(index,
        DATAWIN_TAG('F', 'U', 'N', 'C'));
    if (reader == NULL || state == NULL || !state->initialized ||
        stats == NULL || code == NULL || strg == NULL ||
        (wadVersion != 16u && wadVersion != 17u)) return false;
    memset(stats, 0, sizeof(*stats));
    stats->messageFnv1a = 2166136261u;
    Vm vm;
    memset(&vm, 0, sizeof(vm));
    vm.file = reader->buffer;
    if (reader->bufferSize > UINT32_MAX) return false;
    vm.fileSize = (uint32_t)reader->bufferSize;
    vm.strg = strg;
    vm.func = func;
    vm.code = code;
    vm.stats = stats;
    vm.keyMask = keyMask;
    vm.selfInstanceId = state->instanceId;
    vm.otherInstanceId = state->otherInstanceId;
    if (vm.file == NULL || !build_references(reader, index, &vm, wadVersion)) {
        vm_destroy(&vm);
        return false;
    }
    /* DemonVmInstanceState's public int32 slots map onto the low indices
     * of the real (much larger) per-file variable table for the fixture's
     * position-tracking demos; every other slot starts at 0 like any other
     * never-yet-written variable. */
    for (uint32_t i = 0u; i < DEMON_VM_INSTANCE_VARIABLE_MAX &&
         i < vm.variableCount; ++i)
        vm.variables[i] = (Value){VALUE_INT, state->variables[i], 0.0, NULL};
    bool ok = true;
    uint32_t count = 0u;
    if (!DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('C', 'O', 'D', 'E'), &count)) ok = false;
    /* codeIds names the exact CODE entries to run, in order -- not a
     * bitmask, so there is no 32-entry ceiling here (a real game can have
     * thousands of CODE entries; a uint32_t bitmask could never address
     * most of them). */
    for (uint32_t k = 0u; ok && k < codeIdCount; ++k) {
        const uint32_t i = codeIds[k];
        if (i >= count) { ok = false; break; }
        const uint32_t entry = list_entry(reader, code->payloadOffset, i);
        if (entry == 0u || (uint64_t)entry + 20u >
            (uint64_t)code->payloadOffset + code->size) { ok = false; break; }
        BinaryReader_seek(reader, entry + 4u);
        const uint32_t length = BinaryReader_readUint32(reader);
        BinaryReader_skip(reader, 4u);
        const uint32_t relativeField = (uint32_t)BinaryReader_getPosition(reader);
        const int32_t relative = BinaryReader_readInt32(reader);
        const int64_t bytecode = (int64_t)relativeField + relative;
        if (bytecode < 0 || (uint64_t)bytecode + length > vm.fileSize ||
            !execute_code(&vm, vm.file + bytecode, (uint32_t)bytecode,
                          length)) { ok = false; break; }
        /* Each codeIds entry here is an independent top-level dispatch
         * (an event/entry point), not a nested call -- nothing consumes an
         * implicit return value left over from allowing top<=1, so it must
         * not bleed into the next entry's stack. */
        vm.top = 0u;
        ++stats->codeEntries;
    }
    if (!ok) {
        vm_destroy(&vm);
        return false;
    }
    stats->finalX = vm.variableCount > 3u ? (int32_t)vm.variables[3].value : 0;
    stats->finalY = vm.variableCount > 4u ? (int32_t)vm.variables[4].value : 0;
    if (stats->codeEntries != codeIdCount) {
        vm_destroy(&vm);
        return false;
    }
    for (uint32_t i = 0u; i < DEMON_VM_INSTANCE_VARIABLE_MAX &&
         i < vm.variableCount; ++i)
        state->variables[i] = (int32_t)vm.variables[i].value;
    vm_destroy(&vm);
    return true;
}

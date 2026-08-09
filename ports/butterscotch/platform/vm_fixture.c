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
#define OP_CALL 0xd9u
#define VALUE_STACK_MAX 32u

typedef enum {
    VALUE_INT, VALUE_REAL, VALUE_BOOL, VALUE_STRING, VALUE_UNDEFINED,
    VALUE_FUNCTION
} ValueKind;
typedef struct { ValueKind kind; int64_t value; double real; } Value;
typedef struct { uint32_t address; uint32_t index; } Reference;
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
    uint32_t variableCount;
    Reference *vars;
    uint32_t varCapacity;
    uint32_t varRefs;
    Reference *funcs;
    uint32_t funcCapacity;
    uint32_t funcRefs;
    const DemonDataWinChunk *strg;
    const DemonDataWinChunk *func;
    DemonVmExecutionStats *stats;
    uint32_t keyMask;
} Vm;

static void vm_destroy(Vm *vm) {
    demon_port_free(vm->variables);
    demon_port_free(vm->vars);
    demon_port_free(vm->funcs);
    vm->variables = NULL;
    vm->vars = NULL;
    vm->funcs = NULL;
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
        *result = (Value){VALUE_INT, integer, 0.0};
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
    *result = (Value){VALUE_REAL, 0, value};
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
    vm->vars = totalVarOcc == 0u ? NULL : (Reference *)demon_port_malloc(
        (size_t)totalVarOcc * sizeof(Reference));
    vm->funcs = totalFuncOcc == 0u ? NULL : (Reference *)demon_port_malloc(
        (size_t)totalFuncOcc * sizeof(Reference));
    if ((variableCount > 0u && vm->variables == NULL) ||
        (totalVarOcc > 0u && vm->vars == NULL) ||
        (totalFuncOcc > 0u && vm->funcs == NULL)) {
        vm_destroy(vm);
        return false;
    }
    memset(vm->variables, 0, (size_t)variableCount * sizeof(Value));
    vm->variableCount = variableCount;
    vm->varCapacity = (uint32_t)totalVarOcc;
    vm->funcCapacity = (uint32_t)totalFuncOcc;

    /* Pass 2: populate the reference chains. */
    for (uint32_t i = 0u; i < variableCount; ++i) {
        BinaryReader_seek(reader, vari->payloadOffset + 12u + i * 20u + 12u);
        const uint32_t occurrences = BinaryReader_readUint32(reader);
        const uint32_t first = BinaryReader_readUint32(reader);
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
static bool function_name_equals(const Vm *vm, uint32_t functionIndex,
                                 const char *want) {
    if (vm->func == NULL) return false;
    const uint64_t entryOff = (uint64_t)vm->func->payloadOffset + 4u +
        (uint64_t)functionIndex * 12u;
    if (entryOff + 12u > (uint64_t)vm->func->payloadOffset + vm->func->size)
        return false;
    const uint32_t namePtr = BinaryUtils_readUint32(vm->file + entryOff);
    if (namePtr < 4u || (uint64_t)namePtr >= vm->fileSize) return false;
    const uint32_t len = BinaryUtils_readUint32(vm->file + namePtr - 4u);
    const size_t wantLen = strlen(want);
    if (len != wantLen || (uint64_t)namePtr + len > vm->fileSize) return false;
    return memcmp(vm->file + namePtr, want, len) == 0;
}

static bool call_builtin(Vm *vm, uint32_t function, uint32_t arguments) {
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
        return push(vm, (Value){VALUE_UNDEFINED, 0, 0.0});
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
        return push(vm, (Value){VALUE_BOOL, pressed ? 1 : 0, 0.0});
    }
    return false;
}

static bool execute_code(Vm *vm, const uint8_t *bytes, uint32_t absolute,
                         uint32_t length) {
    uint32_t ip = 0u;
    while (ip < length) {
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
                    if (!push(vm, (Value){VALUE_REAL, 0, value}))
                        return false;
                } else if (type1 == 6u) {
                    if (!push(vm, (Value){VALUE_STRING,
                        BinaryUtils_readInt32(extra), 0.0})) return false;
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
                            (int64_t)reference, 0.0})) return false;
                    } else if (!push(vm, (Value){VALUE_INT,
                        BinaryUtils_readInt32(extra), 0.0})) return false;
                } else if (type1 == 5u) {
                    if (!resolve(vm->vars, vm->varRefs, address,
                                 &reference) || reference >= vm->variableCount ||
                        !push(vm, vm->variables[reference])) return false;
                } else return false;
                break;
            case OP_PUSHI:
                if (!push(vm, (Value){VALUE_INT,
                    (int16_t)(instruction & 0xffffu), 0.0})) return false;
                break;
            case OP_DUP:
                {
                    const uint32_t count = (instruction & 0xffu) + 1u;
                    if (count > vm->top || count > VALUE_STACK_MAX - vm->top)
                        return false;
                    const uint32_t start = vm->top - count;
                    for (uint32_t i = 0u; i < count; ++i)
                        vm->stack[vm->top++] = vm->stack[start + i];
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
                    if (!push(vm, (Value){VALUE_REAL, 0, (double)a.value}))
                        return false;
                } else if (type1 == 0u && type2 == 2u &&
                           a.kind == VALUE_REAL &&
                           a.real >= (double)INT32_MIN &&
                           a.real <= (double)INT32_MAX) {
                    if (!push(vm, (Value){VALUE_INT, (int32_t)a.real, 0.0}))
                        return false;
                } else if (type2 == 5u && (a.kind == VALUE_REAL ||
                           a.kind == VALUE_BOOL)) {
                    if (!push(vm, a)) return false;
                } else return false;
                break;
            case OP_CALL:
                if (!resolve(vm->funcs, vm->funcRefs, address, &reference) ||
                    !call_builtin(vm, reference, instruction & 0xffffu))
                    return false;
                break;
            case OP_POPZ:
                if (!pop(vm, &a)) return false;
                break;
            case OP_RET:
                if (!pop(vm, &a) || vm->top != 0u) return false;
                return true;
            case OP_EXIT:
                return vm->top == 0u;
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
                    if (!push(vm, (Value){VALUE_REAL, 0, -a.real}))
                        return false;
                } else if ((a.kind != VALUE_INT && a.kind != VALUE_BOOL) ||
                    !push(vm, (Value){opcode == OP_NOT ? VALUE_BOOL :
                        VALUE_INT, opcode == OP_NOT ? !a.value : -a.value,
                        0.0})) return false;
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
                        !push(vm, (Value){VALUE_BOOL, result, 0.0}))
                        return false;
                }
                break;
            case OP_POP:
                /* Real variables are dynamically typed -- confirmed against
                 * real data, where a script's compiled preamble stores a
                 * bound method (VALUE_FUNCTION) into a variable, not just
                 * plain numbers. */
                if (!pop(vm, &a) ||
                    !resolve(vm->vars, vm->varRefs, address, &reference) ||
                    reference >= vm->variableCount) return false;
                vm->variables[reference] = a;
                ++vm->stats->variableStores;
                break;
            default:
                return false;
        }
        ip = next;
    }
    /* A script (as opposed to event code) may end with no explicit RET,
     * leaving its last expression's value on the stack as an implicit
     * return value -- confirmed against a real Deltarune script
     * (gml_Script_down_p) which ends this way. Anything beyond one
     * leftover value is still a genuine imbalance. */
    return ip == length && vm->top <= 1u;
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
    const Value integer = {VALUE_INT, 7, 0.0};
    const Value real = {VALUE_REAL, 0, 2.5};
    Value result;
    double converted;
    if (!numeric_value(integer, &converted) || converted != 7.0 ||
        !numeric_binary(OP_ADD, integer, real, &result) ||
        result.kind != VALUE_REAL || result.real != 9.5 ||
        !numeric_binary(OP_MUL, real, (Value){VALUE_REAL, 0, 2.0},
                        &result) || result.real != 5.0 ||
        !numeric_binary(OP_DIV, integer, real, &result) ||
        result.real != 2.8 ||
        numeric_binary(OP_DIV, real, (Value){VALUE_REAL, 0, 0.0}, &result))
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
    return DemonVm_executeEvents(reader, index, wadVersion, keyMask, 0x0fu,
                                 initialX, initialY, stats);
}

bool DemonVm_executeEvents(BinaryReader *reader,
                           const DemonDataWinIndex *index,
                           uint8_t wadVersion, uint32_t keyMask,
                           uint32_t codeMask, int32_t initialX,
                           int32_t initialY, DemonVmExecutionStats *stats) {
    DemonVmInstanceState state;
    DemonVm_initInstanceState(&state, initialX, initialY);
    return DemonVm_executeEventsState(reader, index, wadVersion, keyMask,
                                      codeMask, &state, stats);
}

void DemonVm_initInstanceState(DemonVmInstanceState *state,
                               int32_t initialX, int32_t initialY) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->variables[3] = initialX;
    state->variables[4] = initialY;
    state->initialized = true;
}

bool DemonVm_executeEventsState(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint8_t wadVersion, uint32_t keyMask,
                                uint32_t codeMask,
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
    vm.stats = stats;
    vm.keyMask = keyMask;
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
        vm.variables[i] = (Value){VALUE_INT, state->variables[i], 0.0};
    bool ok = true;
    uint32_t count = 0u;
    if (!DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('C', 'O', 'D', 'E'), &count)) ok = false;
    /* codeMask only has 32 usable bits: for i>=32, `1u << i` is undefined
     * behavior (on this platform it wraps mod 32, which used to make bits
     * 1/6/22 spuriously re-match at i=33/38/54/... and silently re-execute
     * unrelated CODE entries far beyond the ones actually requested --
     * latent since the tiny 4-entry fixture never reached i>=32, confirmed
     * once a real 1680-entry file exercised this path for the first time). */
    const uint32_t maskableCount = count < 32u ? count : 32u;
    for (uint32_t i = 0u; ok && i < maskableCount; ++i) {
        if ((codeMask & (1u << i)) == 0u) continue;
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
        /* Each codeMask entry here is an independent top-level dispatch
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
    uint32_t expected = 0u;
    for (uint32_t i = 0u; i < maskableCount; ++i)
        if ((codeMask & (1u << i)) != 0u) ++expected;
    if (stats->codeEntries != expected) {
        vm_destroy(&vm);
        return false;
    }
    for (uint32_t i = 0u; i < DEMON_VM_INSTANCE_VARIABLE_MAX &&
         i < vm.variableCount; ++i)
        state->variables[i] = (int32_t)vm.variables[i].value;
    vm_destroy(&vm);
    return true;
}

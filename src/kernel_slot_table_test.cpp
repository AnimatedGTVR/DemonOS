// K3 of docs/kernel-cxx-port.md -- proves kernel::slot_table standalone
// against the same semantics src/capability.c's hand-rolled per-process
// table already has (allocate-first-free, generation-checked resolve,
// release invalidates stale handles), before capability.c itself migrates
// onto it.
#include <kernel/slot_table.h>
#include <kernel/slot_table_test.h>
#include <kernel/serial.h>

namespace {

struct probe_grant {
    uint32_t rights = 0u;
    uint32_t object_id = 0u;
};

}  // namespace

extern "C" bool kernel_slot_table_self_test(void) {
    kernel::slot_table<probe_grant, 4> table;

    uint32_t slot_a = 0u;
    uint16_t generation_a = 0u;
    probe_grant *payload_a = nullptr;
    if (!table.allocate(&slot_a, &generation_a, &payload_a) ||
        payload_a == nullptr || generation_a == 0u) {
        serial_write("DEBUG slot_table allocate-failed\n");
        return false;
    }
    payload_a->rights = 7u;
    payload_a->object_id = 42u;

    // resolve() with the right slot+generation must see exactly what was
    // written through the allocate-time payload pointer.
    probe_grant *resolved = table.resolve(slot_a, generation_a);
    if (resolved == nullptr || resolved->rights != 7u || resolved->object_id != 42u) {
        serial_write("DEBUG slot_table resolve-mismatch\n");
        return false;
    }

    // A second allocation must land in a distinct slot.
    uint32_t slot_b = 0u;
    uint16_t generation_b = 0u;
    probe_grant *payload_b = nullptr;
    if (!table.allocate(&slot_b, &generation_b, &payload_b) || slot_b == slot_a) {
        serial_write("DEBUG slot_table second-slot-collision\n");
        return false;
    }

    // Wrong generation for a real slot must fail -- this is the whole
    // point of the counter: a stale handle from before some earlier
    // close/reopen cycle must not resolve just because the slot index
    // still exists.
    if (table.resolve(slot_a, static_cast<uint16_t>(generation_a + 1u)) != nullptr) {
        serial_write("DEBUG slot_table wrong-generation-resolved\n");
        return false;
    }

    // release() must hand back the payload that was stored (capability.c
    // needs this to run service-specific cleanup, e.g. surface_release,
    // using the object_id/service the slot held right before it's gone).
    probe_grant released{};
    if (!table.release(slot_a, &released) || released.rights != 7u ||
        released.object_id != 42u) {
        serial_write("DEBUG slot_table release-payload-mismatch\n");
        return false;
    }
    if (table.is_open(slot_a)) {
        serial_write("DEBUG slot_table still-open-after-release\n");
        return false;
    }

    // The old handle (same slot, old generation) must now be dead...
    if (table.resolve(slot_a, generation_a) != nullptr) {
        serial_write("DEBUG slot_table stale-handle-resolved\n");
        return false;
    }

    // ...but the slot itself must be available again, with a NEW
    // generation, so a fresh handle to the same slot index is
    // distinguishable from the old one (the actual bug this type exists
    // to prevent: reusing a slot must not resurrect old handles).
    uint32_t slot_c = 0u;
    uint16_t generation_c = 0u;
    probe_grant *payload_c = nullptr;
    if (!table.allocate(&slot_c, &generation_c, &payload_c) || slot_c != slot_a ||
        generation_c == generation_a) {
        serial_write("DEBUG slot_table slot-not-reused-or-same-generation\n");
        return false;
    }
    if (payload_c->rights != 0u || payload_c->object_id != 0u) {
        serial_write("DEBUG slot_table stale-payload-on-reuse\n");
        return false;
    }

    // peek() must see an open slot regardless of generation (the
    // capabilities_close_all use case: iterate everything this process
    // owns without needing to have a caller-supplied handle at all).
    if (table.peek(slot_c) == nullptr || table.peek(slot_b) == nullptr) {
        serial_write("DEBUG slot_table peek-failed\n");
        return false;
    }

    // Capacity is exactly 4: slot_c (reused slot_a) + slot_b already fill 2
    // of them. slot_d exactly fills the table to capacity (4 total) and
    // must succeed; only the allocation AFTER that, past capacity, must
    // fail outright.
    uint32_t slot_d = 0u;
    uint16_t generation_d = 0u;
    probe_grant *payload_d = nullptr;
    if (!table.allocate(&slot_d, &generation_d, &payload_d)) {
        serial_write("DEBUG slot_table create-failed-d\n");
        return false;
    }
    uint32_t slot_e = 0u;
    uint16_t generation_e = 0u;
    probe_grant *payload_e = nullptr;
    if (!table.allocate(&slot_e, &generation_e, &payload_e)) {
        serial_write("DEBUG slot_table create-failed-e\n");
        return false;
    }
    uint32_t slot_f = 0u;
    uint16_t generation_f = 0u;
    probe_grant *payload_f = nullptr;
    if (table.allocate(&slot_f, &generation_f, &payload_f)) {
        serial_write("DEBUG slot_table capacity-not-enforced\n");
        return false;
    }
    (void)generation_d;
    (void)generation_e;

    return true;
}

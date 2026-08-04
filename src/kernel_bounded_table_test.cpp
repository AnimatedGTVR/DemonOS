// K1 of docs/kernel-cxx-port.md -- proves kernel::bounded_table standalone
// against the same semantics src/ramfs.c's hand-rolled file table already
// has, before anything real (K2: ramfs.c itself) migrates onto it.
#include <kernel/bounded_table.h>
#include <kernel/bounded_table_test.h>
#include <kernel/serial.h>

namespace {

// A tiny payload so the table isn't tested with a bare scalar -- proves
// default-construction and payload access work for a real aggregate, the
// same shape ramfs.c's struct ramfs_file or capability.c's
// struct capability_slot would be.
struct probe_record {
    uint32_t value = 0u;
};

bool str_equal(const char *lhs, const char *rhs, size_t length) {
    for (size_t i = 0; i < length; ++i)
        if (lhs[i] != rhs[i]) return false;
    return true;
}

}  // namespace

extern "C" bool kernel_bounded_table_self_test(void) {
    kernel::bounded_table<probe_record, 4> table;
    uint32_t id_a = 0u;
    uint32_t id_b = 0u;
    uint32_t lookup = 0u;

    // Absent name, create=false must fail -- mirrors ramfs_open(create=false)
    // on a name that was never written.
    if (table.open("missing", 7u, false, &lookup)) {
        serial_write("DEBUG bounded_table found-nonexistent\n");
        return false;
    }

    // Absent name, create=true must claim a fresh slot.
    if (!table.open("alpha", 5u, true, &id_a)) {
        serial_write("DEBUG bounded_table create-failed alpha\n");
        return false;
    }
    if (id_a == 0u) {
        serial_write("DEBUG bounded_table zero-id\n");
        return false;
    }

    // Same name, create=false must now find the SAME id -- lookup, not a
    // second slot (this is what makes it "open-or-create", exactly
    // ramfs_open's contract).
    if (!table.open("alpha", 5u, false, &lookup) || lookup != id_a) {
        serial_write("DEBUG bounded_table reopen-mismatch\n");
        return false;
    }

    // A second distinct name must land in a distinct slot.
    if (!table.open("beta", 4u, true, &id_b) || id_b == id_a) {
        serial_write("DEBUG bounded_table create-failed beta\n");
        return false;
    }

    // Payload access: mutate through one handle, read back through a
    // fresh lookup of the same name to prove both resolve the same slot.
    probe_record *payload = table.payload_for(id_a);
    if (payload == nullptr) {
        serial_write("DEBUG bounded_table null-payload\n");
        return false;
    }
    payload->value = 99u;
    if (!table.find("alpha", 5u, &lookup) ||
        table.payload_for(lookup)->value != 99u) {
        serial_write("DEBUG bounded_table payload-mismatch\n");
        return false;
    }

    // name_for/name_length_for must hand back exactly what was stored.
    if (table.name_length_for(id_a) != 5u ||
        !str_equal(table.name_for(id_a), "alpha", 5u)) {
        serial_write("DEBUG bounded_table name-mismatch\n");
        return false;
    }

    // rename() (K2's ramfs_rename need): same id, new name, payload
    // untouched -- the old name must stop resolving and the new one must
    // resolve to the exact same slot with the value written earlier.
    if (!table.rename(id_a, "alpha-renamed", 13u)) {
        serial_write("DEBUG bounded_table rename-failed\n");
        return false;
    }
    if (table.find("alpha", 5u, &lookup)) {
        serial_write("DEBUG bounded_table old-name-still-resolves\n");
        return false;
    }
    if (!table.find("alpha-renamed", 13u, &lookup) || lookup != id_a ||
        table.payload_for(lookup)->value != 99u) {
        serial_write("DEBUG bounded_table renamed-lookup-mismatch\n");
        return false;
    }
    // Renamed back to "alpha" so the rest of this test (which still
    // expects that name) is unaffected by the rename probe above.
    if (!table.rename(id_a, "alpha", 5u)) {
        serial_write("DEBUG bounded_table rename-back-failed\n");
        return false;
    }

    // Delete-frees-slot-not-storage: release("alpha") must make the name
    // unresolvable immediately (the slot half of the contract)...
    if (!table.release(id_a)) {
        serial_write("DEBUG bounded_table release-failed\n");
        return false;
    }
    if (table.find("alpha", 5u, &lookup)) {
        serial_write("DEBUG bounded_table found-after-release\n");
        return false;
    }

    // ...and the freed slot must become available for reuse by a new
    // name (proves it's a real slot free-list, not merely marked dead
    // forever -- ramfs_open's own create path has the same expectation).
    uint32_t id_c = 0u;
    if (!table.open("gamma", 5u, true, &id_c) || id_c != id_a) {
        serial_write("DEBUG bounded_table slot-not-reused\n");
        return false;
    }
    // The reused slot's payload must be fresh, not beta/alpha's stale
    // value leaking through.
    if (table.payload_for(id_c)->value != 0u) {
        serial_write("DEBUG bounded_table stale-payload-on-reuse\n");
        return false;
    }

    // Capacity is exactly 4: gamma (reused alpha's slot) + beta already
    // fill 2 of them; delta and epsilon must fit in the remaining 2, and a
    // sixth distinct name must fail outright (RAMFS_FILES-style hard
    // capacity, not silent overwrite of an in-use slot).
    uint32_t id_d = 0u;
    uint32_t id_e = 0u;
    uint32_t overflow = 0u;
    if (!table.open("delta", 5u, true, &id_d)) {
        serial_write("DEBUG bounded_table create-failed delta\n");
        return false;
    }
    if (!table.open("epsilon", 7u, true, &id_e)) {
        serial_write("DEBUG bounded_table create-failed epsilon\n");
        return false;
    }
    if (table.open("zeta", 4u, true, &overflow)) {
        serial_write("DEBUG bounded_table capacity-not-enforced\n");
        return false;
    }

    return true;
}

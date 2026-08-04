#ifndef KERNEL_BOUNDED_TABLE_H
#define KERNEL_BOUNDED_TABLE_H

/* K1 of docs/kernel-cxx-port.md.
   ---------------------------------------------------------------------
   src/ramfs.c's file table (RAMFS_FILES-sized array, linear name_equal
   scan, open-or-create, delete-frees-slot-not-storage) is one instance of
   a generic shape that shows up more than once in this codebase at
   different layers -- e.g. ports/quake/platform/sys_demonos.c's
   QUAKE_MAX_HANDLES table is the same "fixed capacity, name/slot lookup,
   slot reuse" pattern hand-rolled again in C. This header is that shape
   made generic and templated, so it can be written once and reused
   instead of hand-copied.

   K1 deliberately does NOT migrate ramfs.c onto this yet (that's K2) --
   this is proven standalone first, against the same semantics ramfs.c
   already has: name-keyed slots, open-or-create, and a delete that frees
   the SLOT for reuse but says nothing about any separate byte storage a
   caller layers on top (ramfs.c's bump-allocated storage arena is exactly
   such a separate concern -- freeing a ramfs file frees its slot
   immediately but never reclaims its storage bytes until reboot; that
   stays true when/if K2 migrates ramfs.c onto this table, because
   bounded_table only owns the slot, not whatever a T payload might
   itself reference).

   Freestanding constraints match every other kernel C++ file here (see
   include/kernel/cxx_runtime.h): no exceptions, no RTTI, no libstdc++ --
   this header must build under -fno-exceptions -fno-rtti -nostdinc++, so
   it uses raw byte loops instead of <cstring>, exactly like ramfs.c's own
   valid_name/name_equal do in C. */

#include <stddef.h>
#include <stdint.h>

namespace kernel {

// NameMax mirrors ramfs.c's RAMFS_NAME_MAX (63): the longest name a slot
// can hold, not counting the implicit null terminator kept alongside it
// for callers that want a C string back.
template <typename T, size_t Capacity, size_t NameMax = 63>
class bounded_table {
public:
    static constexpr size_t capacity() { return Capacity; }

    bounded_table() : slots_{} {}

    // Finds an existing slot by exact name match. On success, *id is a
    // stable 1-based identifier (0 is never valid, matching ramfs.c's own
    // object_id convention so a future ramfs.c migration keeps the same
    // caller-visible handle values).
    bool find(const char *name, size_t name_length, uint32_t *id) const {
        if (name == nullptr || id == nullptr || name_length == 0u ||
            name_length > NameMax)
            return false;
        for (size_t i = 0; i < Capacity; ++i) {
            if (!slots_[i].used) continue;
            if (names_equal(i, name, name_length)) {
                *id = static_cast<uint32_t>(i) + 1u;
                return true;
            }
        }
        return false;
    }

    // Open-or-create, matching ramfs_open's contract exactly: finds an
    // existing name first; if absent and `create` is false, fails; if
    // absent and `create` is true, claims the first free slot, default
    // constructs its payload, and stores the name.
    bool open(const char *name, size_t name_length, bool create, uint32_t *id) {
        if (find(name, name_length, id)) return true;
        if (!create || name == nullptr || name_length == 0u ||
            name_length > NameMax)
            return false;
        for (size_t i = 0; i < Capacity; ++i) {
            if (slots_[i].used) continue;
            slots_[i].used = true;
            slots_[i].name_length = name_length;
            for (size_t byte = 0; byte < name_length; ++byte)
                slots_[i].name[byte] = name[byte];
            slots_[i].name[name_length] = '\0';
            slots_[i].payload = T();
            *id = static_cast<uint32_t>(i) + 1u;
            return true;
        }
        return false;
    }

    // Frees the slot only -- see the file-level comment above. The
    // payload itself is reset to a fresh T() so a later reuse of this
    // slot never observes a stale value, but this class has no idea
    // whether T references memory that needs a separate reclaim step
    // (e.g. a bump-arena offset); that stays the caller's concern, same
    // division of responsibility ramfs.c already has today.
    bool release(uint32_t id) {
        T *slot_payload = payload_for(id);
        if (slot_payload == nullptr) return false;
        slot(id)->used = false;
        slot(id)->name_length = 0u;
        *slot_payload = T();
        return true;
    }

    // Changes a slot's name in place without touching its payload -- added
    // for K2 (src/ramfs.c's ramfs_rename keeps a file's bytes/offset intact
    // and only ever changes what name resolves to it). Callers are
    // responsible for checking the new name isn't already in use (find()
    // before rename()), same division of responsibility ramfs_rename has
    // today: this only knows about the one slot it's asked to rename.
    bool rename(uint32_t id, const char *new_name, size_t new_name_length) {
        entry *slot_entry = slot(id);
        if (slot_entry == nullptr || !slot_entry->used || new_name == nullptr ||
            new_name_length == 0u || new_name_length > NameMax)
            return false;
        slot_entry->name_length = new_name_length;
        for (size_t i = 0; i < new_name_length; ++i)
            slot_entry->name[i] = new_name[i];
        slot_entry->name[new_name_length] = '\0';
        return true;
    }

    T *payload_for(uint32_t id) {
        entry *slot_entry = slot(id);
        return (slot_entry == nullptr || !slot_entry->used) ? nullptr : &slot_entry->payload;
    }

    const T *payload_for(uint32_t id) const {
        const entry *slot_entry = slot(id);
        return (slot_entry == nullptr || !slot_entry->used) ? nullptr : &slot_entry->payload;
    }

    size_t name_length_for(uint32_t id) const {
        const entry *slot_entry = slot(id);
        return (slot_entry == nullptr || !slot_entry->used) ? 0u : slot_entry->name_length;
    }

    const char *name_for(uint32_t id) const {
        const entry *slot_entry = slot(id);
        return (slot_entry == nullptr || !slot_entry->used) ? nullptr : slot_entry->name;
    }

private:
    struct entry {
        bool used = false;
        char name[NameMax + 1u] = {};
        size_t name_length = 0u;
        T payload{};
    };

    entry *slot(uint32_t id) {
        if (id == 0u || id > Capacity) return nullptr;
        return &slots_[id - 1u];
    }
    const entry *slot(uint32_t id) const {
        if (id == 0u || id > Capacity) return nullptr;
        return &slots_[id - 1u];
    }

    bool names_equal(size_t index, const char *name, size_t name_length) const {
        if (slots_[index].name_length != name_length) return false;
        for (size_t i = 0; i < name_length; ++i)
            if (slots_[index].name[i] != name[i]) return false;
        return true;
    }

    entry slots_[Capacity];
};

}  // namespace kernel

#endif

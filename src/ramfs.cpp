// K2 of docs/kernel-cxx-port.md: src/ramfs.c's file table migrated onto
// kernel::bounded_table<T,N> (K1, include/kernel/bounded_table.h). Every
// function below keeps the exact C signature declared in
// include/kernel/ramfs.h (now extern-"C"-guarded there) -- every existing
// caller (src/apps.c, src/git.c, src/makobox.c,
// src/arch/x86_64/userspace.c's syscalls 6/9/10/42/43/44, ...) keeps
// calling the same names unchanged. This file is the actual proof of the
// K2 milestone: if every existing make check/*-smoke target still passes
// unmodified, the C++ subsystem underneath is a drop-in implementation
// change, not an ABI change.
//
// What moved onto bounded_table: the name/slot bookkeeping only --
// used-flag, name storage, slot reuse, capacity enforcement. What did NOT
// move: the separate bump-allocated byte arena (`storage`/`storage_used`)
// stays exactly as hand-rolled in the original C, because that is a
// genuinely separate concern from the slot table (see
// include/kernel/bounded_table.h's file comment: freeing a slot has never
// meant reclaiming its storage bytes here, and that stays true after this
// migration).
#include <kernel/bounded_table.h>
#include <kernel/ramfs.h>

namespace {

// Bumped from 28: the NXEngine port's play-smoke boot (grub-nxengine-
// play.cfg) legitimately needs more real, distinct bundled files (a
// dozen+ small real Cave Story/engine assets -- tilekey.dat, sprites.sif,
// npc.tbl, several .pxm/.pbm/.pxa files) than any previous port, and hit
// this ceiling with "Native PortKit anonymous-memory test failed" --
// portcheck.elf's own dynamically-created test file had no free slot
// left, not a memory problem. Headroom for future ports, not just this
// one.
constexpr size_t kRamfsFiles = 48u;
constexpr size_t kRamfsNameMax = 63u;
constexpr size_t kRamfsDataMax = 196608u;
// See src/ramfs.c's original comment trail on this constant before
// changing it -- every past bump here was caught by ramfs_seed() actually
// failing at boot, not a static check. Bumped from 524288 (512KB): the
// NXEngine D26 stage (docs/nxengine-port.md) added the real Head.tsc/
// ArmsItem.tsc/StageSelect.tsc script files to the play-smoke ISO on top
// of an nxengine-core.elf that had also grown from linking tsc.cpp/
// SaveSelect.cpp/StageSelect.cpp -- combined, the play-smoke boot's
// small/copied module set (everything under kRamfsDataMax, which
// excludes the few large-and-reference-seeded assets like doom-full.elf/
// quake-core.elf/MAKO-source.tar.zst) now totals ~524KB, caught directly
// by ramfs_seed() failing on Credit.tsc's mount ("Could not install MKO
// ISO module"), the same class of failure the kRamfsFiles bump's comment
// above describes. Bumped to 640KB, real headroom for future ports, not
// a tight fit to this one boot's exact total.
constexpr size_t kRamfsStorageMax = 655360u;

// The part of the old struct ramfs_file that isn't name/used bookkeeping
// (bounded_table now owns that half). Default-constructing this via
// `T()` on create, and again via `T()` on release, reproduces exactly
// what the original ramfs_open's create branch and ramfs_delete did:
// offset/capacity/length/external_data all reset to zero/null. offset
// only matters once capacity/length are nonzero (see store() below), so
// defaulting it to 0 instead of the original's "storage_used at creation
// time" is harmless -- store() sets the real offset the first time the
// file is actually written.
struct ramfs_payload {
    size_t offset = 0u;
    size_t capacity = 0u;
    size_t length = 0u;
    const uint8_t *external_data = nullptr;
};

kernel::bounded_table<ramfs_payload, kRamfsFiles, kRamfsNameMax> table;
uint8_t storage[kRamfsStorageMax];
size_t storage_used = 0u;
uint64_t read_count = 0u;
uint64_t write_count = 0u;
uint64_t seeded_count = 0u;

// Names may be a bare filename ("hello.mko") or an absolute path
// ("/system/mko/sdk.mko"). Path segments follow the same character rules as
// a bare filename; "." and ".." segments and "//" are rejected outright so
// there is no traversal ambiguity to resolve later, and a leading "/" must
// be followed by a real segment rather than being the whole name.
bool valid_segment(const char *segment, size_t length) {
    if (length == 0u) return false;
    if (length == 1u && segment[0] == '.') return false;
    if (length == 2u && segment[0] == '.' && segment[1] == '.') return false;
    for (size_t i = 0; i < length; ++i) {
        const char value = segment[i];
        const bool allowed = (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '.' || value == '_' ||
            value == '-';
        if (!allowed) return false;
    }
    return true;
}

bool valid_name(const char *name, size_t length) {
    if (name == nullptr || length == 0u || length > kRamfsNameMax) return false;
    size_t segment_start = 0u;
    if (name[0] == '/') segment_start = 1u;
    if (segment_start == length) return false;  // "/" alone is not a valid name
    size_t cursor = segment_start;
    while (cursor <= length) {
        if (cursor == length || name[cursor] == '/') {
            if (!valid_segment(name + segment_start, cursor - segment_start))
                return false;
            segment_start = cursor + 1u;
        }
        ++cursor;
    }
    return true;
}

bool store(ramfs_payload *file, const uint8_t *data, size_t length) {
    if (file == nullptr || data == nullptr || file->external_data != nullptr ||
        length > kRamfsDataMax)
        return false;
    if (length > file->capacity) {
        if (length > kRamfsStorageMax - storage_used) return false;
        file->offset = storage_used;
        file->capacity = length;
        storage_used += length;
    }
    for (size_t i = 0u; i < length; ++i) storage[file->offset + i] = data[i];
    file->length = length;
    return true;
}

// Strips a leading "/" from prefix so "/system/mko" and "system/mko" list
// the same directory; every stored name is normalized the same way when
// compared, since ramfs itself only ever stores names without a leading "/"
// concept baked into comparisons.
void normalize_prefix(const char *prefix, size_t prefix_length,
                      const char **out, size_t *out_length) {
    if (prefix_length > 0u && prefix[0] == '/') {
        *out = prefix + 1u;
        *out_length = prefix_length - 1u;
    } else {
        *out = prefix;
        *out_length = prefix_length;
    }
}

// Listing a directory shows its immediate children only -- a file three
// levels deep collapses to the name of the subdirectory that leads to it,
// and each such subdirectory name is reported once no matter how many files
// live under it. Only an exact leaf match (no further "/" after the
// prefix) reports a real byte length; a collapsed subdirectory reports 0,
// matching the size convention a real directory entry would have.
bool child_of(const char *name, size_t name_length, const char *dir,
             size_t dir_length, size_t *child_start, size_t *child_length,
             bool *is_leaf) {
    size_t name_start = 0u;
    if (name_length > 0u && name[0] == '/') name_start = 1u;

    size_t rel_start;
    if (dir_length == 0u) {
        rel_start = name_start;
    } else {
        if (name_length - name_start <= dir_length) return false;
        for (size_t i = 0; i < dir_length; ++i)
            if (name[name_start + i] != dir[i]) return false;
        if (name[name_start + dir_length] != '/') return false;
        rel_start = name_start + dir_length + 1u;
    }
    if (rel_start >= name_length) return false;

    size_t end = rel_start;
    while (end < name_length && name[end] != '/') ++end;
    *child_start = rel_start;
    *child_length = end - rel_start;
    *is_leaf = (end == name_length);
    return true;
}

}  // namespace

extern "C" void ramfs_init(void) {
    table = kernel::bounded_table<ramfs_payload, kRamfsFiles, kRamfsNameMax>();
    read_count = 0u;
    write_count = 0u;
    seeded_count = 0u;
    storage_used = 0u;
}

extern "C" bool ramfs_seed_reference(const char *name, size_t name_length,
                                     const uint8_t *data, size_t length) {
    if (data == nullptr) return false;
    uint32_t object_id;
    if (!ramfs_open(name, name_length, true, &object_id)) return false;
    ramfs_payload *file = table.payload_for(object_id);
    if (file == nullptr || file->length != 0u || file->capacity != 0u ||
        file->external_data != nullptr)
        return false;
    file->external_data = data;
    file->length = length;
    ++seeded_count;
    return true;
}

extern "C" bool ramfs_seed(const char *name, size_t name_length,
                           const uint8_t *data, size_t length) {
    if (data == nullptr || length > kRamfsDataMax) return false;
    uint32_t object_id;
    if (!ramfs_open(name, name_length, true, &object_id)) return false;
    ramfs_payload *file = table.payload_for(object_id);
    if (!store(file, data, length)) return false;
    ++seeded_count;
    return true;
}

extern "C" bool ramfs_open(const char *name, size_t name_length, bool create,
                           uint32_t *object_id) {
    if (!valid_name(name, name_length) || object_id == nullptr) return false;
    return table.open(name, name_length, create, object_id);
}

// Frees the file-table slot only -- the storage arena is a simple
// bump allocator with no compaction (matches every other path here, e.g.
// overwriting a file's content never reclaims its old capacity either),
// so the bytes stay allocated until reboot. That's a real, documented
// limitation, not a correctness bug: the file is genuinely gone (its name
// no longer resolves, ramfs_list/ramfs_open won't find it), it just
// doesn't shrink storage_used.
extern "C" bool ramfs_delete(const char *name, size_t name_length) {
    if (!valid_name(name, name_length)) return false;
    uint32_t object_id;
    if (!table.find(name, name_length, &object_id)) return false;
    return table.release(object_id);
}

extern "C" bool ramfs_rename(const char *old_name, size_t old_length,
                             const char *new_name, size_t new_length) {
    if (!valid_name(old_name, old_length) || !valid_name(new_name, new_length))
        return false;
    uint32_t existing;
    if (table.find(new_name, new_length, &existing)) return false;
    uint32_t source_id;
    if (!table.find(old_name, old_length, &source_id)) return false;
    return table.rename(source_id, new_name, new_length);
}

extern "C" bool ramfs_write(uint32_t object_id, const uint8_t *data, size_t length) {
    ramfs_payload *file = table.payload_for(object_id);
    if (!store(file, data, length)) return false;
    ++write_count;
    return true;
}

extern "C" bool ramfs_read(uint32_t object_id, uint8_t *data, size_t capacity,
                           size_t *length) {
    return ramfs_read_range(object_id, 0u, data, capacity, length);
}

extern "C" bool ramfs_read_range(uint32_t object_id, size_t offset, uint8_t *data,
                                 size_t capacity, size_t *length) {
    ramfs_payload *file = table.payload_for(object_id);
    if (file == nullptr || data == nullptr || length == nullptr ||
        offset > file->length)
        return false;
    /* Match ordinary bounded-read semantics. This lets lightweight native
       applications inspect the beginning of a large object without first
       allocating an executable-sized userspace buffer. Demon Web uses this
       to render a 512-byte preview of file:/// RAMFS documents. */
    size_t amount = file->length - offset;
    if (amount > capacity) amount = capacity;
    const uint8_t *source = file->external_data != nullptr ? file->external_data
                                                            : &storage[file->offset];
    for (size_t i = 0; i < amount; ++i) data[i] = source[offset + i];
    *length = amount;
    ++read_count;
    return true;
}

extern "C" bool ramfs_size(uint32_t object_id, size_t *length) {
    ramfs_payload *file = table.payload_for(object_id);
    if (file == nullptr || length == nullptr) return false;
    *length = file->length;
    return true;
}

extern "C" bool ramfs_view(uint32_t object_id, const uint8_t **data, size_t *length) {
    ramfs_payload *file = table.payload_for(object_id);
    if (file == nullptr || data == nullptr || length == nullptr) return false;
    *data = file->external_data != nullptr ? file->external_data
                                            : &storage[file->offset];
    *length = file->length;
    return true;
}

extern "C" bool ramfs_self_test(void) {
    static const char expected_name[] = "project.mko";
    static const uint8_t expected_data[] = "PORTABLE-PROJECT";
    /* The boot image may gain additional preinstalled native tools. The
       invariant is that every module was seeded plus this one test file,
       not that the OS must forever ship exactly seven modules. */
    if (ramfs_file_count() != seeded_count + 1u || seeded_count < 7u ||
        ramfs_bytes_used() <= 16u || read_count != 1u || write_count != 1u)
        return false;
    uint32_t object_id;
    if (!ramfs_open(expected_name, sizeof(expected_name) - 1u, false, &object_id))
        return false;
    const ramfs_payload *file = table.payload_for(object_id);
    if (file == nullptr || file->length != sizeof(expected_data) - 1u) return false;
    const uint8_t *contents = file->external_data != nullptr ? file->external_data
                                                              : &storage[file->offset];
    for (size_t i = 0; i < sizeof(expected_data) - 1u; ++i)
        if (contents[i] != expected_data[i]) return false;
    return true;
}

extern "C" uint64_t ramfs_file_count(void) {
    uint64_t count = 0u;
    for (uint32_t id = 1u; id <= table.capacity(); ++id)
        if (table.payload_for(id) != nullptr) ++count;
    return count;
}

extern "C" uint64_t ramfs_bytes_used(void) {
    uint64_t total = 0u;
    for (uint32_t id = 1u; id <= table.capacity(); ++id) {
        const ramfs_payload *file = table.payload_for(id);
        if (file != nullptr) total += file->length;
    }
    return total;
}

extern "C" uint64_t ramfs_reads(void) { return read_count; }
extern "C" uint64_t ramfs_writes(void) { return write_count; }
extern "C" uint64_t ramfs_seeded_files(void) { return seeded_count; }
extern "C" uint64_t ramfs_storage_capacity(void) { return kRamfsStorageMax; }
extern "C" uint64_t ramfs_max_files(void) { return kRamfsFiles; }

extern "C" bool ramfs_entry(size_t index, const char **name, size_t *name_length,
                            size_t *length) {
    size_t current = 0u;
    for (uint32_t id = 1u; id <= table.capacity(); ++id) {
        const ramfs_payload *file = table.payload_for(id);
        if (file == nullptr) continue;
        if (current++ != index) continue;
        if (name != nullptr) *name = table.name_for(id);
        if (name_length != nullptr) *name_length = table.name_length_for(id);
        if (length != nullptr) *length = file->length;
        return true;
    }
    return false;
}

extern "C" bool ramfs_list(const char *prefix, size_t prefix_length, size_t index,
                           const char **relative_name, size_t *relative_length,
                           size_t *length, bool *is_directory) {
    const char *dir;
    size_t dir_length;
    normalize_prefix(prefix, prefix_length, &dir, &dir_length);

    size_t current = 0u;
    for (uint32_t id = 1u; id <= table.capacity(); ++id) {
        const ramfs_payload *file = table.payload_for(id);
        if (file == nullptr) continue;
        const char *name = table.name_for(id);
        size_t name_length = table.name_length_for(id);

        size_t child_start, child_length;
        bool is_leaf;
        if (!child_of(name, name_length, dir, dir_length, &child_start, &child_length, &is_leaf))
            continue;

        // A non-leaf child (a subdirectory) may be reached by several
        // files; only the first occurrence (lowest id) counts as an entry.
        if (!is_leaf) {
            bool first_occurrence = true;
            for (uint32_t earlier = 1u; earlier < id; ++earlier) {
                const ramfs_payload *earlier_file = table.payload_for(earlier);
                if (earlier_file == nullptr) continue;
                size_t es, el;
                bool el_leaf;
                if (!child_of(table.name_for(earlier), table.name_length_for(earlier),
                              dir, dir_length, &es, &el, &el_leaf))
                    continue;
                if (el == child_length) {
                    bool same = true;
                    const char *earlier_name = table.name_for(earlier);
                    for (size_t i = 0; i < child_length; ++i)
                        if (name[child_start + i] != earlier_name[es + i]) { same = false; break; }
                    if (same) { first_occurrence = false; break; }
                }
            }
            if (!first_occurrence) continue;
        }

        if (current++ != index) continue;
        if (relative_name != nullptr) *relative_name = name + child_start;
        if (relative_length != nullptr) *relative_length = child_length;
        if (length != nullptr) *length = is_leaf ? file->length : 0u;
        if (is_directory != nullptr) *is_directory = !is_leaf;
        return true;
    }
    return false;
}

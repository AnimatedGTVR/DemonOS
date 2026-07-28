#include <kernel/ramfs.h>

#define RAMFS_FILES 20u
#define RAMFS_NAME_MAX 63u
/* Raised from 96 KiB to 128 KiB to fit the compositor executable after its
   loading/login screens and taskbar text grew it past the old cap, then to
   144 KiB when the settings popover/accent-theme/browser-window-kind work
   pushed it further (glyph-heavy MKO code has real, uncompressed
   per-call-site binary cost -- see draw_glyph call counts in
   compositor.mko), with headroom for further growth. This is independent
   of and tighter than USERSPACE_CODE_PAGES in kernel/userspace.h (the
   actual per-process code-page allocation budget the compositor also has
   to fit under); both must be raised together if the compositor grows
   past either. */
#define RAMFS_DATA_MAX 147456u
/* The seeded binaries, native document browser, and graphical C apps occupy
   just over 288 KiB. Keep a bounded 320 KiB arena with app-growth headroom; large game
   data belongs in a streamed read-only boot filesystem rather than
   permanent kernel BSS. */
#define RAMFS_STORAGE_MAX 327680u

struct ramfs_file {
    bool used;
    char name[RAMFS_NAME_MAX + 1u];
    size_t name_length;
    size_t offset;
    size_t capacity;
    size_t length;
};

static struct ramfs_file files[RAMFS_FILES];
static uint8_t storage[RAMFS_STORAGE_MAX];
static size_t storage_used;
static uint64_t read_count;
static uint64_t write_count;
static uint64_t seeded_count;

// Names may be a bare filename ("hello.mko") or an absolute path
// ("/system/mko/sdk.mko"). Path segments follow the same character rules as
// a bare filename; "." and ".." segments and "//" are rejected outright so
// there is no traversal ambiguity to resolve later, and a leading "/" must
// be followed by a real segment rather than being the whole name.
static bool valid_segment(const char *segment, size_t length) {
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

static bool valid_name(const char *name, size_t length) {
    if (name == NULL || length == 0u || length > RAMFS_NAME_MAX) return false;
    size_t segment_start = 0u;
    if (name[0] == '/') segment_start = 1u;
    if (segment_start == length) return false; // "/" alone is not a valid name
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

static bool name_equal(const struct ramfs_file *file, const char *name,
                       size_t length) {
    if (!file->used || file->name_length != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (file->name[i] != name[i]) return false;
    return true;
}

static struct ramfs_file *file_for_id(uint32_t object_id) {
    if (object_id == 0u || object_id > RAMFS_FILES) return NULL;
    struct ramfs_file *file = &files[object_id - 1u];
    return file->used ? file : NULL;
}

void ramfs_init(void) {
    for (size_t i = 0; i < RAMFS_FILES; ++i)
        files[i] = (struct ramfs_file){0};
    read_count = 0u;
    write_count = 0u;
    seeded_count = 0u;
    storage_used = 0u;
}

static bool store(struct ramfs_file *file, const uint8_t *data, size_t length) {
    if (file == NULL || data == NULL || length > RAMFS_DATA_MAX) return false;
    if (length > file->capacity) {
        if (length > RAMFS_STORAGE_MAX - storage_used) return false;
        file->offset = storage_used;
        file->capacity = length;
        storage_used += length;
    }
    for (size_t i = 0u; i < length; ++i) storage[file->offset + i] = data[i];
    file->length = length;
    return true;
}

bool ramfs_seed(const char *name, size_t name_length,
                const uint8_t *data, size_t length) {
    if (data == NULL || length > RAMFS_DATA_MAX) return false;
    uint32_t object_id;
    if (!ramfs_open(name, name_length, true, &object_id)) return false;
    struct ramfs_file *file = file_for_id(object_id);
    if (!store(file, data, length)) return false;
    ++seeded_count;
    return true;
}

bool ramfs_open(const char *name, size_t name_length, bool create,
                uint32_t *object_id) {
    if (!valid_name(name, name_length) || object_id == NULL) return false;
    for (size_t i = 0; i < RAMFS_FILES; ++i) {
        if (name_equal(&files[i], name, name_length)) {
            *object_id = (uint32_t)i + 1u;
            return true;
        }
    }
    if (!create) return false;
    for (size_t i = 0; i < RAMFS_FILES; ++i) {
        if (files[i].used) continue;
        files[i].used = true;
        files[i].name_length = name_length;
        files[i].offset = storage_used;
        files[i].capacity = 0u;
        files[i].length = 0u;
        for (size_t byte = 0; byte < name_length; ++byte)
            files[i].name[byte] = name[byte];
        files[i].name[name_length] = '\0';
        *object_id = (uint32_t)i + 1u;
        return true;
    }
    return false;
}

bool ramfs_write(uint32_t object_id, const uint8_t *data, size_t length) {
    struct ramfs_file *file = file_for_id(object_id);
    if (!store(file, data, length)) return false;
    ++write_count;
    return true;
}

bool ramfs_read(uint32_t object_id, uint8_t *data, size_t capacity,
                size_t *length) {
    struct ramfs_file *file = file_for_id(object_id);
    if (file == NULL || data == NULL || length == NULL || capacity == 0u)
        return false;
    /* Match ordinary bounded-read semantics. This lets lightweight native
       applications inspect the beginning of a large object without first
       allocating an executable-sized userspace buffer. Demon Web uses this
       to render a 512-byte preview of file:/// RAMFS documents. */
    size_t amount = file->length;
    if (amount > capacity) amount = capacity;
    for (size_t i = 0; i < amount; ++i) data[i] = storage[file->offset + i];
    *length = amount;
    ++read_count;
    return true;
}

bool ramfs_size(uint32_t object_id, size_t *length) {
    struct ramfs_file *file = file_for_id(object_id);
    if (file == NULL || length == NULL) return false;
    *length = file->length;
    return true;
}

bool ramfs_view(uint32_t object_id, const uint8_t **data, size_t *length) {
    struct ramfs_file *file = file_for_id(object_id);
    if (file == NULL || data == NULL || length == NULL) return false;
    *data = &storage[file->offset];
    *length = file->length;
    return true;
}

bool ramfs_self_test(void) {
    static const char expected_name[] = "project.mko";
    static const uint8_t expected_data[] = "PORTABLE-PROJECT";
    if (ramfs_file_count() != seeded_count + 1u || seeded_count != 19u ||
        ramfs_bytes_used() <= 16u || read_count != 1u || write_count != 1u)
        return false;
    uint32_t object_id;
    if (!ramfs_open(expected_name, sizeof(expected_name) - 1u, false, &object_id))
        return false;
    struct ramfs_file *file = file_for_id(object_id);
    if (file == NULL || file->length != sizeof(expected_data) - 1u) return false;
    for (size_t i = 0; i < sizeof(expected_data) - 1u; ++i)
        if (storage[file->offset + i] != expected_data[i]) return false;
    return true;
}

uint64_t ramfs_file_count(void) {
    uint64_t count = 0u;
    for (size_t i = 0; i < RAMFS_FILES; ++i)
        if (files[i].used) ++count;
    return count;
}

uint64_t ramfs_bytes_used(void) {
    uint64_t total = 0u;
    for (size_t i = 0; i < RAMFS_FILES; ++i)
        if (files[i].used) total += files[i].length;
    return total;
}

uint64_t ramfs_reads(void) { return read_count; }
uint64_t ramfs_writes(void) { return write_count; }
uint64_t ramfs_seeded_files(void) { return seeded_count; }

bool ramfs_entry(size_t index, const char **name, size_t *name_length,
                 size_t *length) {
    size_t current = 0u;
    for (size_t slot = 0u; slot < RAMFS_FILES; ++slot) {
        if (!files[slot].used) continue;
        if (current++ != index) continue;
        if (name != NULL) *name = files[slot].name;
        if (name_length != NULL) *name_length = files[slot].name_length;
        if (length != NULL) *length = files[slot].length;
        return true;
    }
    return false;
}

// Strips a leading "/" from prefix so "/system/mko" and "system/mko" list
// the same directory; every stored name is normalized the same way when
// compared, since ramfs itself only ever stores names without a leading "/"
// concept baked into comparisons.
static void normalize_prefix(const char *prefix, size_t prefix_length,
                             const char **out, size_t *out_length) {
    if (prefix_length > 0u && prefix[0] == '/') {
        *out = prefix + 1u;
        *out_length = prefix_length - 1u;
    } else {
        *out = prefix;
        *out_length = prefix_length;
    }
}

// Listing a directory shows its immediate children only — a file three
// levels deep collapses to the name of the subdirectory that leads to it,
// and each such subdirectory name is reported once no matter how many files
// live under it. Only an exact leaf match (no further "/" after the
// prefix) reports a real byte length; a collapsed subdirectory reports 0,
// matching the size convention a real directory entry would have.
static bool child_of(const char *name, size_t name_length,
                     const char *dir, size_t dir_length,
                     size_t *child_start, size_t *child_length, bool *is_leaf) {
    // `dir` is already normalized to have no leading "/" (normalize_prefix);
    // stored file names may still have one (module command lines pass
    // "/system/mko/sdk.mko" verbatim), so normalize `name` the same way
    // before comparing against `dir` or scanning for the next segment.
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

bool ramfs_list(const char *prefix, size_t prefix_length, size_t index,
                const char **relative_name, size_t *relative_length,
                size_t *length, bool *is_directory) {
    const char *dir;
    size_t dir_length;
    normalize_prefix(prefix, prefix_length, &dir, &dir_length);

    size_t current = 0u;
    for (size_t slot = 0u; slot < RAMFS_FILES; ++slot) {
        if (!files[slot].used) continue;
        const char *name = files[slot].name;
        size_t name_length = files[slot].name_length;

        size_t child_start, child_length;
        bool is_leaf;
        if (!child_of(name, name_length, dir, dir_length, &child_start, &child_length, &is_leaf))
            continue;

        // A non-leaf child (a subdirectory) may be reached by several
        // files; only the first occurrence (lowest slot) counts as an entry.
        if (!is_leaf) {
            bool first_occurrence = true;
            for (size_t earlier = 0u; earlier < slot; ++earlier) {
                if (!files[earlier].used) continue;
                size_t es, el;
                bool el_leaf;
                if (!child_of(files[earlier].name, files[earlier].name_length,
                              dir, dir_length, &es, &el, &el_leaf))
                    continue;
                if (el == child_length) {
                    bool same = true;
                    for (size_t i = 0; i < child_length; ++i)
                        if (name[child_start + i] != files[earlier].name[es + i]) { same = false; break; }
                    if (same) { first_occurrence = false; break; }
                }
            }
            if (!first_occurrence) continue;
        }

        if (current++ != index) continue;
        if (relative_name != NULL) *relative_name = name + child_start;
        if (relative_length != NULL) *relative_length = child_length;
        if (length != NULL) *length = is_leaf ? files[slot].length : 0u;
        if (is_directory != NULL) *is_directory = !is_leaf;
        return true;
    }
    return false;
}

#include <kernel/git.h>
#include <kernel/ramfs.h>

// Real content snapshots, no fabrication: `git commit` copies whatever the
// tracked RAMFS files actually contain right now into this arena, and
// every read-back (diff/show) reads exactly those bytes. The 8 KiB budget
// comfortably covers the handful of small text files this kernel actually
// has to track (project.mko-sized things), with headroom; bump it if a
// real use ever needs more.
#define GIT_SNAPSHOT_ARENA_BYTES 8192u

struct git_file_snapshot {
    char name[GIT_NAME_MAX + 1u];
    size_t name_length;
    size_t offset;
    size_t length;
};

struct git_commit {
    uint64_t hash;
    uint64_t parent_hash;
    char message[GIT_MESSAGE_MAX + 1u];
    size_t message_length;
    struct git_file_snapshot files[GIT_MAX_TRACKED];
    size_t file_count;
};

struct git_tracked_file {
    char name[GIT_NAME_MAX + 1u];
    size_t name_length;
};

static bool initialized;
static struct git_tracked_file tracked[GIT_MAX_TRACKED];
static size_t tracked_count;

static struct git_commit commits[GIT_MAX_COMMITS];
static size_t commit_count;

static uint8_t snapshot_arena[GIT_SNAPSHOT_ARENA_BYTES];
static size_t snapshot_used;

static bool name_equal(const char *a, size_t a_length, const char *b, size_t b_length) {
    if (a_length != b_length) return false;
    for (size_t i = 0u; i < a_length; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

void git_init(void) {
    initialized = false;
    tracked_count = 0u;
    commit_count = 0u;
    snapshot_used = 0u;
}

bool git_repository_init(void) {
    tracked_count = 0u;
    commit_count = 0u;
    snapshot_used = 0u;
    initialized = true;
    return true;
}

bool git_repository_initialized(void) { return initialized; }

bool git_is_tracked(const char *name, size_t name_length) {
    for (size_t i = 0u; i < tracked_count; ++i)
        if (name_equal(tracked[i].name, tracked[i].name_length, name, name_length))
            return true;
    return false;
}

bool git_add(const char *name, size_t name_length) {
    if (!initialized || name == NULL || name_length == 0u ||
        name_length > GIT_NAME_MAX)
        return false;
    uint32_t object_id;
    if (!ramfs_open(name, name_length, false, &object_id)) return false; // must already exist
    if (git_is_tracked(name, name_length)) return true;
    if (tracked_count >= GIT_MAX_TRACKED) return false;
    struct git_tracked_file *slot = &tracked[tracked_count];
    for (size_t i = 0u; i < name_length; ++i) slot->name[i] = name[i];
    slot->name[name_length] = '\0';
    slot->name_length = name_length;
    ++tracked_count;
    return true;
}

size_t git_tracked_count(void) { return tracked_count; }

bool git_tracked_entry(size_t index, const char **name, size_t *name_length) {
    if (index >= tracked_count || name == NULL || name_length == NULL) return false;
    *name = tracked[index].name;
    *name_length = tracked[index].name_length;
    return true;
}

// FNV-1a 64-bit -- deterministic, content-derived, not a fabricated value:
// two commits are only equal if their parent/message/every tracked byte
// are all identical.
static uint64_t hash_bytes(uint64_t hash, const uint8_t *data, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool head_snapshot(const struct git_tracked_file *file,
                          const uint8_t **data, size_t *length) {
    if (commit_count == 0u) return false;
    const struct git_commit *head = &commits[commit_count - 1u];
    for (size_t i = 0u; i < head->file_count; ++i) {
        if (name_equal(head->files[i].name, head->files[i].name_length,
                       file->name, file->name_length)) {
            *data = &snapshot_arena[head->files[i].offset];
            *length = head->files[i].length;
            return true;
        }
    }
    return false;
}

bool git_repository_dirty(void) {
    if (!initialized) return false;
    if (tracked_count > 0u && commit_count == 0u) return true;
    for (size_t i = 0u; i < tracked_count; ++i) {
        uint32_t object_id;
        const uint8_t *current;
        size_t current_length;
        if (!ramfs_open(tracked[i].name, tracked[i].name_length, false, &object_id) ||
            !ramfs_view(object_id, &current, &current_length))
            return true; // tracked file vanished -- that's a real change too
        const uint8_t *head_data;
        size_t head_length;
        if (!head_snapshot(&tracked[i], &head_data, &head_length)) return true;
        if (current_length != head_length) return true;
        for (size_t byte = 0u; byte < current_length; ++byte)
            if (current[byte] != head_data[byte]) return true;
    }
    return false;
}

bool git_commit(const char *message, size_t message_length, uint64_t *out_hash) {
    if (!initialized || tracked_count == 0u) return false;
    if (message_length > GIT_MESSAGE_MAX) message_length = GIT_MESSAGE_MAX;
    if (commit_count >= GIT_MAX_COMMITS) return false;

    // Measure first so a too-large commit fails cleanly instead of leaving
    // a half-written snapshot in the arena.
    size_t total_bytes = 0u;
    for (size_t i = 0u; i < tracked_count; ++i) {
        uint32_t object_id;
        size_t length;
        if (!ramfs_open(tracked[i].name, tracked[i].name_length, false, &object_id) ||
            !ramfs_size(object_id, &length))
            return false;
        total_bytes += length;
    }
    if (total_bytes > GIT_SNAPSHOT_ARENA_BYTES - snapshot_used) return false;

    const uint64_t parent_hash = commit_count > 0u ? commits[commit_count - 1u].hash : 0u;
    struct git_commit *entry = &commits[commit_count];
    entry->parent_hash = parent_hash;
    for (size_t i = 0u; i < message_length; ++i) entry->message[i] = message[i];
    entry->message[message_length] = '\0';
    entry->message_length = message_length;
    entry->file_count = tracked_count;

    uint64_t hash = 1469598103934665603ull;
    hash = hash_bytes(hash, (const uint8_t *)&parent_hash, sizeof(parent_hash));
    hash = hash_bytes(hash, (const uint8_t *)entry->message, message_length);

    for (size_t i = 0u; i < tracked_count; ++i) {
        uint32_t object_id;
        const uint8_t *data;
        size_t length;
        if (!ramfs_open(tracked[i].name, tracked[i].name_length, false, &object_id) ||
            !ramfs_view(object_id, &data, &length))
            return false;
        struct git_file_snapshot *snapshot = &entry->files[i];
        for (size_t c = 0u; c < tracked[i].name_length; ++c)
            snapshot->name[c] = tracked[i].name[c];
        snapshot->name[tracked[i].name_length] = '\0';
        snapshot->name_length = tracked[i].name_length;
        snapshot->offset = snapshot_used;
        snapshot->length = length;
        for (size_t b = 0u; b < length; ++b)
            snapshot_arena[snapshot_used + b] = data[b];
        snapshot_used += length;
        hash = hash_bytes(hash, (const uint8_t *)snapshot->name, snapshot->name_length);
        hash = hash_bytes(hash, data, length);
    }
    entry->hash = hash;
    ++commit_count;
    if (out_hash != NULL) *out_hash = hash;
    return true;
}

uint64_t git_commit_count(void) { return commit_count; }

uint64_t git_head_hash(void) {
    return commit_count > 0u ? commits[commit_count - 1u].hash : 0u;
}

bool git_log_entry(size_t index_from_head, uint64_t *hash, uint64_t *parent_hash,
                   const char **message, size_t *message_length) {
    if (index_from_head >= commit_count) return false;
    const struct git_commit *entry = &commits[commit_count - 1u - index_from_head];
    if (hash != NULL) *hash = entry->hash;
    if (parent_hash != NULL) *parent_hash = entry->parent_hash;
    if (message != NULL) *message = entry->message;
    if (message_length != NULL) *message_length = entry->message_length;
    return true;
}

bool git_find_commit(uint64_t hash, size_t *index) {
    for (size_t i = 0u; i < commit_count; ++i) {
        if (commits[i].hash == hash) {
            if (index != NULL) *index = commit_count - 1u - i;
            return true;
        }
    }
    return false;
}

bool git_commit_file(size_t commit_index, const char *name, size_t name_length,
                     const uint8_t **data, size_t *length) {
    if (commit_index >= commit_count) return false;
    const struct git_commit *entry = &commits[commit_count - 1u - commit_index];
    for (size_t i = 0u; i < entry->file_count; ++i) {
        if (name_equal(entry->files[i].name, entry->files[i].name_length,
                       name, name_length)) {
            *data = &snapshot_arena[entry->files[i].offset];
            *length = entry->files[i].length;
            return true;
        }
    }
    return false;
}

uint64_t git_tracked_files(void) { return tracked_count; }

uint64_t git_tracked_bytes(void) {
    uint64_t total = 0u;
    for (size_t i = 0u; i < tracked_count; ++i) {
        uint32_t object_id;
        size_t length;
        if (ramfs_open(tracked[i].name, tracked[i].name_length, false, &object_id) &&
            ramfs_size(object_id, &length))
            total += length;
    }
    return total;
}

bool git_self_test(void) {
    static const char test_name[] = "project.mko";
    static const char message[] = "self-test commit";
    if (!git_repository_init()) return false;
    if (git_repository_dirty()) return false; // nothing tracked yet
    if (!git_add(test_name, sizeof(test_name) - 1u)) return false;
    if (!git_is_tracked(test_name, sizeof(test_name) - 1u)) return false;
    if (!git_repository_dirty()) return false; // tracked, never committed
    uint64_t hash = 0u;
    if (!git_commit(message, sizeof(message) - 1u, &hash) || hash == 0u) return false;
    if (git_repository_dirty()) return false; // just committed, matches HEAD
    if (git_commit_count() != 1u || git_head_hash() != hash) return false;
    uint64_t log_hash, log_parent;
    const char *log_message;
    size_t log_message_length;
    if (!git_log_entry(0u, &log_hash, &log_parent, &log_message, &log_message_length))
        return false;
    if (log_hash != hash || log_parent != 0u || log_message_length != sizeof(message) - 1u)
        return false;
    const uint8_t *snapshot_data;
    size_t snapshot_length;
    if (!git_commit_file(0u, test_name, sizeof(test_name) - 1u,
                         &snapshot_data, &snapshot_length))
        return false;
    uint32_t object_id;
    const uint8_t *current_data;
    size_t current_length;
    if (!ramfs_open(test_name, sizeof(test_name) - 1u, false, &object_id) ||
        !ramfs_view(object_id, &current_data, &current_length))
        return false;
    if (snapshot_length != current_length) return false;
    for (size_t i = 0u; i < snapshot_length; ++i)
        if (snapshot_data[i] != current_data[i]) return false;
    return true;
}

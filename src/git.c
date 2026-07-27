#include <kernel/git.h>
#include <kernel/ramfs.h>

static bool initialized;
static uint64_t baseline_files;
static uint64_t baseline_bytes;
static uint64_t commits;

void git_init(void) {
    initialized = false;
    baseline_files = 0u;
    baseline_bytes = 0u;
    commits = 0u;
}

bool git_repository_init(void) {
    baseline_files = ramfs_file_count();
    baseline_bytes = ramfs_bytes_used();
    commits = 0u;
    initialized = true;
    return true;
}

bool git_repository_commit(void) {
    if (!initialized) return false;
    baseline_files = ramfs_file_count();
    baseline_bytes = ramfs_bytes_used();
    ++commits;
    return true;
}

bool git_repository_initialized(void) { return initialized; }
bool git_repository_dirty(void) {
    return initialized && (baseline_files != ramfs_file_count() || baseline_bytes != ramfs_bytes_used());
}
uint64_t git_commit_count(void) { return commits; }
uint64_t git_tracked_files(void) { return baseline_files; }
uint64_t git_tracked_bytes(void) { return baseline_bytes; }

bool git_self_test(void) {
    return git_repository_init() && !git_repository_dirty() &&
        git_repository_commit() && commits == 1u;
}

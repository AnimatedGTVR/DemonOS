#ifndef KERNEL_GIT_H
#define KERNEL_GIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Stage 1 of a staged real-git port (see sidelined/README.md-style scoping
// note: no persistent disk here, everything below lives in RAM and resets
// on reboot, so "real" means "real content snapshots/hashing/diffing of
// whatever's in RAMFS right now", not disk-backed object/pack storage).
// Deferred to a later stage: branches, checkout, merge, remotes.

#define GIT_MAX_TRACKED 8u
#define GIT_MAX_COMMITS 16u
#define GIT_NAME_MAX 63u
#define GIT_MESSAGE_MAX 79u

void git_init(void);
bool git_repository_init(void);
bool git_repository_initialized(void);

// Starts tracking a RAMFS path (must already exist as a file). Idempotent.
bool git_add(const char *name, size_t name_length);
bool git_is_tracked(const char *name, size_t name_length);
size_t git_tracked_count(void);
bool git_tracked_entry(size_t index, const char **name, size_t *name_length);

// True if any tracked file's current RAMFS content differs from HEAD's
// snapshot of it (or a tracked file was added since the last commit).
bool git_repository_dirty(void);

// Snapshots the current content of every tracked file into a new commit.
// hash is a real (non-cryptographic) content hash of parent+message+every
// snapshot's bytes -- deterministic and content-derived, not a literal
// SHA-1, but not fabricated either: two commits with identical tracked
// content and message always hash identically, and differ if either does.
bool git_commit(const char *message, size_t message_length, uint64_t *hash);
uint64_t git_commit_count(void);
uint64_t git_head_hash(void);

// Walks the commit chain, index 0 = most recent (HEAD).
bool git_log_entry(size_t index_from_head, uint64_t *hash, uint64_t *parent_hash,
                   const char **message, size_t *message_length);

// Looks up a commit by its exact hash (as printed by git_log_entry/git_commit).
bool git_find_commit(uint64_t hash, size_t *index);

// Returns the snapshot of `name` stored in commit `commit_index` (0 = HEAD).
bool git_commit_file(size_t commit_index, const char *name, size_t name_length,
                     const uint8_t **data, size_t *length);

// Convenience used by `git status`.
uint64_t git_tracked_files(void);
uint64_t git_tracked_bytes(void);

bool git_self_test(void);

#endif

#ifndef KERNEL_GIT_H
#define KERNEL_GIT_H

#include <stdbool.h>
#include <stdint.h>

void git_init(void);
bool git_repository_init(void);
bool git_repository_commit(void);
bool git_repository_initialized(void);
bool git_repository_dirty(void);
uint64_t git_commit_count(void);
uint64_t git_tracked_files(void);
uint64_t git_tracked_bytes(void);
bool git_self_test(void);

#endif

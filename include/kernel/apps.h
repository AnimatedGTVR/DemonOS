#ifndef KERNEL_APPS_H
#define KERNEL_APPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct app_snapshot {
    const char *name;
    const char *path;
    size_t image_bytes;
    bool valid_elf64;
};

void apps_init(void);
size_t apps_count(void);
bool apps_snapshot(size_t index, struct app_snapshot *snapshot);
bool apps_find(const char *name, struct app_snapshot *snapshot);
bool apps_self_test(void);
uint64_t apps_scan_count(void);

#endif

#ifndef KERNEL_ELF64_H
#define KERNEL_ELF64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct elf64_target {
    uint64_t virtual_base;
    uint8_t *memory;
    size_t memory_size;
};

struct elf64_layout {
    uint64_t virtual_base;
    uint64_t virtual_end;
    uint64_t entry_point;
};

bool elf64_executable_layout(const uint8_t *image, size_t image_size,
                             struct elf64_layout *layout);

bool elf64_load_executable(const uint8_t *image, size_t image_size,
                           const struct elf64_target *target,
                           uint64_t *entry_point);

/* Load into non-contiguous physical frames while preserving a contiguous
   userspace virtual image. Each frame address must identify one writable,
   identity-accessible 4 KiB frame. */
bool elf64_load_executable_pages(const uint8_t *image, size_t image_size,
                                 uint64_t virtual_base,
                                 const uint64_t *frames, size_t page_count,
                                 uint64_t *entry_point);

#endif

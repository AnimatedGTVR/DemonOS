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

bool elf64_load_executable(const uint8_t *image, size_t image_size,
                           const struct elf64_target *target,
                           uint64_t *entry_point);

#endif

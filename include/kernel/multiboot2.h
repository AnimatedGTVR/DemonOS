#ifndef KERNEL_MULTIBOOT2_H
#define KERNEL_MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u
#define MULTIBOOT2_TAG_END 0u
#define MULTIBOOT2_TAG_CMDLINE 1u
#define MULTIBOOT2_TAG_BOOT_LOADER_NAME 2u
#define MULTIBOOT2_TAG_MODULE 3u
#define MULTIBOOT2_TAG_BASIC_MEMINFO 4u
#define MULTIBOOT2_TAG_MMAP 6u
#define MULTIBOOT2_TAG_FRAMEBUFFER 8u

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct multiboot2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t module_start;
    uint32_t module_end;
    char command_line[];
};

struct multiboot2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower_kib;
    uint32_t mem_upper_kib;
};

struct multiboot2_mmap_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot2_mmap_entry entries[];
};

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bits_per_pixel;
    uint8_t framebuffer_type;
    uint16_t reserved;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
} __attribute__((packed));

#endif

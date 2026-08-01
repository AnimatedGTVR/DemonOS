#include <kernel/elf64.h>

#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define EM_X86_64 62u
#define PT_LOAD 1u
#define PF_X 1u

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_names;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

static bool range_fits(uint64_t offset, uint64_t length, uint64_t total) {
    return offset <= total && length <= total - offset;
}

bool elf64_executable_layout(const uint8_t *image, size_t image_size,
                             struct elf64_layout *layout) {
    if (image == NULL || layout == NULL || image_size < sizeof(struct elf64_header))
        return false;
    const struct elf64_header *header = (const struct elf64_header *)image;
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != ELFCLASS64 || header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != 1u || header->type != ET_EXEC ||
        header->machine != EM_X86_64 || header->version != 1u ||
        header->header_size != sizeof(struct elf64_header) ||
        header->program_entry_size != sizeof(struct elf64_program_header) ||
        header->program_count == 0u) return false;
    const uint64_t table_size =
        (uint64_t)header->program_count * sizeof(struct elf64_program_header);
    if (!range_fits(header->program_offset, table_size, image_size)) return false;
    uint64_t base = UINT64_MAX, end = 0u;
    bool executable_entry = false;
    for (uint16_t index = 0; index < header->program_count; ++index) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(image + header->program_offset +
                (uint64_t)index * sizeof(*program));
        if (program->type != PT_LOAD) continue;
        if (program->file_size > program->memory_size ||
            !range_fits(program->offset, program->file_size, image_size) ||
            program->virtual_address > UINT64_MAX - program->memory_size)
            return false;
        const uint64_t segment_end = program->virtual_address + program->memory_size;
        if (program->virtual_address < base) base = program->virtual_address;
        if (segment_end > end) end = segment_end;
        if ((program->flags & PF_X) != 0u && header->entry >= program->virtual_address &&
            header->entry < segment_end) executable_entry = true;
    }
    if (base == UINT64_MAX || end <= base || !executable_entry) return false;
    *layout = (struct elf64_layout){
        .virtual_base = base,
        .virtual_end = end,
        .entry_point = header->entry,
    };
    return true;
}

bool elf64_load_executable(const uint8_t *image, size_t image_size,
                           const struct elf64_target *target,
                           uint64_t *entry_point) {
    if (image == NULL || target == NULL || target->memory == NULL ||
        entry_point == NULL || image_size < sizeof(struct elf64_header))
        return false;
    struct elf64_layout layout;
    if (!elf64_executable_layout(image, image_size, &layout) ||
        layout.virtual_base < target->virtual_base ||
        layout.virtual_end > target->virtual_base + target->memory_size)
        return false;
    const struct elf64_header *header = (const struct elf64_header *)image;
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != ELFCLASS64 || header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != 1u || header->type != ET_EXEC ||
        header->machine != EM_X86_64 || header->version != 1u ||
        header->header_size != sizeof(struct elf64_header) ||
        header->program_entry_size != sizeof(struct elf64_program_header) ||
        header->program_count == 0u)
        return false;
    const uint64_t table_size =
        (uint64_t)header->program_count * sizeof(struct elf64_program_header);
    if (!range_fits(header->program_offset, table_size, image_size)) return false;

    for (size_t i = 0; i < target->memory_size; ++i) target->memory[i] = 0u;
    bool loaded = false;
    bool executable_entry = false;
    for (uint16_t index = 0; index < header->program_count; ++index) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(image + header->program_offset +
                (uint64_t)index * sizeof(*program));
        if (program->type != PT_LOAD) continue;
        if (program->file_size > program->memory_size ||
            !range_fits(program->offset, program->file_size, image_size) ||
            program->virtual_address < target->virtual_base)
            return false;
        const uint64_t destination = program->virtual_address - target->virtual_base;
        if (!range_fits(destination, program->memory_size, target->memory_size))
            return false;
        for (uint64_t byte = 0; byte < program->file_size; ++byte)
            target->memory[destination + byte] = image[program->offset + byte];
        loaded = true;
        if ((program->flags & PF_X) != 0u &&
            header->entry >= program->virtual_address &&
            header->entry < program->virtual_address + program->memory_size)
            executable_entry = true;
    }
    if (!loaded || !executable_entry || header->entry < target->virtual_base ||
        header->entry >= target->virtual_base + target->memory_size)
        return false;
    *entry_point = header->entry;
    return true;
}

bool elf64_load_executable_pages(const uint8_t *image, size_t image_size,
                                 uint64_t virtual_base,
                                 const uint64_t *frames, size_t page_count,
                                 uint64_t *entry_point) {
    if (image == NULL || frames == NULL || page_count == 0u ||
        page_count > SIZE_MAX / 4096u || entry_point == NULL) return false;
    struct elf64_layout layout;
    const uint64_t capacity = (uint64_t)page_count * 4096u;
    if (virtual_base > UINT64_MAX - capacity) return false;
    if (!elf64_executable_layout(image, image_size, &layout) ||
        layout.virtual_base < virtual_base ||
        layout.virtual_end > virtual_base + capacity) return false;
    for (size_t page = 0u; page < page_count; ++page) {
        if (frames[page] == 0u || (frames[page] & 4095u) != 0u) return false;
        uint8_t *destination = (uint8_t *)(uintptr_t)frames[page];
        for (size_t byte = 0u; byte < 4096u; ++byte) destination[byte] = 0u;
    }
    const struct elf64_header *header = (const struct elf64_header *)image;
    for (uint16_t index = 0; index < header->program_count; ++index) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(image + header->program_offset +
                (uint64_t)index * sizeof(*program));
        if (program->type != PT_LOAD) continue;
        const uint64_t destination = program->virtual_address - virtual_base;
        for (uint64_t byte = 0u; byte < program->file_size; ++byte) {
            const uint64_t offset = destination + byte;
            ((uint8_t *)(uintptr_t)frames[offset / 4096u])[offset % 4096u] =
                image[program->offset + byte];
        }
    }
    *entry_point = layout.entry_point;
    return true;
}

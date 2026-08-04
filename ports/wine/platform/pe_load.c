#include "pe_load.h"
#include <demon/c_app.h>

#define SECTION_HEADER_SIZE 40u
/* Mirrors USER_ANON_MAX_BYTES in src/arch/x86_64/userspace.c -- there is no
   header shared between kernel and userspace for this limit, so
   demon_memory_reserve would simply fail past it anyway; checking here
   first just gives a distinct, more useful result code
   (WINE_PE_LOAD_TOO_LARGE) instead of a generic reserve failure. */
#define WINE_PE_MAX_IMAGE_BYTES (24u * 1024u * 1024u)

static uint32_t read_le32(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static bool read_exact(struct demon_port_file *file, size_t offset,
                       uint8_t *bytes, size_t length) {
    return demon_port_seek(file, offset) &&
        demon_port_read(file, bytes, length) == length;
}

static size_t round_up_page(size_t bytes) {
    return (bytes + 4095u) & ~(size_t)4095u;
}

enum wine_pe_load_result wine_pe_load(struct demon_port_file *file,
                                      uint64_t base, struct wine_pe_image *image) {
    if (file == NULL || image == NULL || base == 0u) return WINE_PE_LOAD_READ_FAILED;
    struct wine_pe_info info;
    if (wine_pe_validate(file, &info) != WINE_PE_OK) return WINE_PE_LOAD_VALIDATE_FAILED;
    if (info.size_of_image == 0u || info.size_of_image > WINE_PE_MAX_IMAGE_BYTES)
        return WINE_PE_LOAD_TOO_LARGE;

    size_t committed = 0u; /* bytes of THIS image committed so far, not the
        whole process's reservation -- see the header comment on why
        wine_pe_load takes `base` instead of reserving for itself. */
    for (uint16_t index = 0u; index < info.section_count; ++index) {
        const size_t header_offset = (size_t)info.section_table_offset +
            (size_t)index * SECTION_HEADER_SIZE;
        uint8_t header[SECTION_HEADER_SIZE];
        if (!read_exact(file, header_offset, header, sizeof(header)))
            return WINE_PE_LOAD_READ_FAILED;
        const uint32_t virtual_size = read_le32(&header[8]);
        const uint32_t virtual_address = read_le32(&header[12]);
        const uint32_t raw_size = read_le32(&header[16]);
        const uint32_t raw_pointer = read_le32(&header[20]);
        const uint32_t extent = virtual_size > raw_size ? virtual_size : raw_size;

        /* Sections must be non-decreasing and stay inside the declared
           image size -- both real invariants every linker upholds, not
           arbitrary strictness. */
        if ((size_t)virtual_address < committed ||
            (size_t)virtual_address > (size_t)info.size_of_image - extent)
            return WINE_PE_LOAD_BAD_SECTION;

        /* Every section is committed writable, regardless of
           IMAGE_SCN_MEM_WRITE, and stays that way: the zero-fill and raw-data
           copy below have to write into it during loading no matter what its
           final intended permission is, and there is no mprotect-equivalent
           syscall yet to downgrade a range to read-only after populating it
           (that needs walking already-mapped page tables and a TLB
           invalidate -- real, bounded kernel work, just not done in this
           pass). Discovered the hard way: an earlier version of this
           function committed non-writable sections non-writable up front,
           and its own zero-fill loop immediately page-faulted trying to
           write into .text. A future protect-after-populate step would need
           to re-read each section's Characteristics itself, since nothing
           here retains it once loading is done. Same "commit everything
           through the end of this section in one call" note as before still
           applies to alignment gaps before a section. */
        const size_t needed_end = round_up_page((size_t)virtual_address + extent);
        if (needed_end > committed) {
            if (demon_memory_commit(needed_end - committed, true) == NULL)
                return WINE_PE_LOAD_COMMIT_FAILED;
            committed = needed_end;
        }

        uint8_t *section_memory = (uint8_t *)(uintptr_t)(base + virtual_address);
        for (size_t i = 0u; i < virtual_size; ++i) section_memory[i] = 0u;
        if (raw_pointer != 0u && raw_size != 0u) {
            if (!demon_port_seek(file, raw_pointer)) return WINE_PE_LOAD_READ_FAILED;
            const size_t copy_size = raw_size < virtual_size ? raw_size : virtual_size;
            if (demon_port_read(file, section_memory, copy_size) != copy_size)
                return WINE_PE_LOAD_READ_FAILED;
        }
    }

    /* Any remaining declared image size past the last section is unused
       padding -- committed non-writable since nothing should ever be
       stored there. */
    if (committed < info.size_of_image) {
        if (demon_memory_commit(info.size_of_image - committed, false) == NULL)
            return WINE_PE_LOAD_COMMIT_FAILED;
    }

    image->base = base;
    image->entry_point = base + info.entry_point_rva;
    image->info = info;
    return WINE_PE_LOAD_OK;
}

#include <demon/portkit.h>
#include <demon/c_app.h>
#include "../../ports/wine/platform/pe_reloc.h"
#include <stddef.h>
#include <stdint.h>

/* W4 of docs/wine-port.md. Builds a minimal, synthetic PE32+ fixture at
   runtime with one baked-in "absolute pointer" and a real
   IMAGE_BASE_RELOCATION block describing it -- same legal hygiene as every
   other fixture in this port. */

#define ARENA_SIZE 256u
#define FIXTURE_SIZE 4096u
#define PREFERRED_IMAGE_BASE 0x140000000ULL

static void write_u16(uint8_t *buffer, size_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
    buffer[offset + 2u] = (uint8_t)(value >> 16u);
    buffer[offset + 3u] = (uint8_t)(value >> 24u);
}

static void write_u64(uint8_t *buffer, size_t offset, uint64_t value) {
    write_u32(buffer, offset, (uint32_t)value);
    write_u32(buffer, offset + 4u, (uint32_t)(value >> 32u));
}

static void write_section(uint8_t *buffer, size_t offset, const char *name,
                          uint32_t virtual_size, uint32_t virtual_address,
                          uint32_t raw_size, uint32_t raw_pointer) {
    for (size_t i = 0u; i < 8u && name[i] != '\0'; ++i) buffer[offset + i] = (uint8_t)name[i];
    write_u32(buffer, offset + 8u, virtual_size);
    write_u32(buffer, offset + 12u, virtual_address);
    write_u32(buffer, offset + 16u, raw_size);
    write_u32(buffer, offset + 20u, raw_pointer);
    write_u32(buffer, offset + 36u, 0xC0000040u);
}

/* `reloc_type` lets the same builder produce a well-formed (DIR64), an
   absent-directory (reloc_size 0), and an unsupported-type fixture. Layout
   inside the one section (VA base 0x1000):
     0x1000  the baked-in "pointer": PREFERRED_IMAGE_BASE + 0x1000 (i.e. it
             was computed at "link time" as if this image would load at
             its preferred base, pointing at its own first byte)
     0x1010  one IMAGE_BASE_RELOCATION block: VirtualAddress=0x1000,
             SizeOfBlock=10 (8-byte header + one 2-byte entry) */
static void build_fixture(uint8_t *buffer, bool with_reloc_directory, uint32_t reloc_type) {
    buffer[0] = 'M'; buffer[1] = 'Z';
    write_u32(buffer, 0x3Cu, 64u);
    buffer[64] = 'P'; buffer[65] = 'E'; buffer[66] = 0u; buffer[67] = 0u;
    write_u16(buffer, 68u, 0x8664u);
    write_u16(buffer, 70u, 1u);
    write_u16(buffer, 84u, 240u);
    write_u16(buffer, 86u, 0x0022u);

    const size_t opt = 88u;
    write_u16(buffer, opt + 0u, 0x020Bu);
    write_u32(buffer, opt + 16u, 0x1000u);
    write_u64(buffer, opt + 24u, PREFERRED_IMAGE_BASE);
    write_u32(buffer, opt + 32u, 0x1000u);
    write_u32(buffer, opt + 36u, 0x200u);
    write_u32(buffer, opt + 56u, 0x2000u); /* SizeOfImage */
    write_u32(buffer, opt + 60u, 0x200u);
    write_u16(buffer, opt + 68u, 3u);
    write_u32(buffer, opt + 108u, 16u);
    if (with_reloc_directory) {
        write_u32(buffer, opt + 112u + 5u * 8u, 0x1010u); /* reloc RVA */
        write_u32(buffer, opt + 112u + 5u * 8u + 4u, 10u); /* reloc size */
    }

    write_section(buffer, opt + 240u, ".data", 0x1000u, 0x1000u, 0x200u, 512u);

    uint8_t *s = buffer + 512u; /* s[i] corresponds to VA 0x1000+i */
    write_u64(s, 0x00u, PREFERRED_IMAGE_BASE + 0x1000u); /* the baked-in pointer */
    write_u32(s, 0x10u, 0x1000u);                        /* block.VirtualAddress */
    write_u32(s, 0x14u, 10u);                             /* block.SizeOfBlock */
    write_u16(s, 0x18u, (uint16_t)((reloc_type << 12u) | 0u)); /* one entry, offset 0 */
}

static size_t path_length(const char *path) {
    size_t length = 0u;
    while (path[length] != '\0') ++length;
    return length;
}

static bool write_fixture(const char *path, const uint8_t *bytes, size_t size) {
    const struct demon_port_runtime *port = demon_port_status();
    const uint64_t handle = demon_file_open(port->storage, path, path_length(path), 1u);
    if (handle == DEMON_PORT_INVALID_HANDLE) return false;
    const bool written = demon_handle_write(handle, bytes, size) == size;
    (void)demon_handle_close(handle);
    return written;
}

static uint8_t fixture_buffer[FIXTURE_SIZE];
static uint8_t static_arena[ARENA_SIZE];

static bool load_fixture(const char *path, struct wine_pe_image *image, uint64_t base) {
    struct demon_port_file file;
    if (!demon_port_open(&file, path)) return false;
    const bool ok = wine_pe_load(&file, base, image) == WINE_PE_LOAD_OK;
    demon_port_close(&file);
    return ok;
}

uint64_t wine_pe_reloc_check_main(void) {
    if (!demon_port_init(static_arena, ARENA_SIZE)) return 10u;

    /* Case 1: a real DIR64 relocation must be applied correctly. */
    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_fixture(fixture_buffer, true, 10u /* IMAGE_REL_BASED_DIR64 */);
    if (!write_fixture("/tmp/reloc-dir64.exe", fixture_buffer, FIXTURE_SIZE)) return 11u;

    void *region_a = demon_memory_reserve(0x2000u);
    if (region_a == NULL) return 12u;
    struct wine_pe_image image_a;
    if (!load_fixture("/tmp/reloc-dir64.exe", &image_a, (uint64_t)(uintptr_t)region_a))
        return 13u;
    /* This kernel's anonymous region base (0x10000000, see USER_ANON_BASE
       in src/arch/x86_64/userspace.c) can never equal this fixture's
       preferred ImageBase (0x140000000) -- if it ever did, this assertion
       would need rethinking, not silently pass. */
    if (image_a.base == PREFERRED_IMAGE_BASE) return 14u;
    if (wine_pe_apply_relocations(&image_a) != WINE_PE_RELOC_OK) return 15u;
    /* The baked-in pointer lives at VA 0x1000 (section start), not RVA 0. */
    const uint64_t *patched = (const uint64_t *)(uintptr_t)(image_a.base + 0x1000u);
    if (*patched != image_a.base + 0x1000u) return 16u;
    if (demon_memory_unmap(region_a) != 0u) return 17u;

    /* Case 2: no relocation directory at all must report NONE and leave
       memory untouched (re-checked explicitly, not just trusted). */
    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_fixture(fixture_buffer, false, 10u);
    if (!write_fixture("/tmp/reloc-none.exe", fixture_buffer, FIXTURE_SIZE)) return 18u;

    void *region_b = demon_memory_reserve(0x2000u);
    if (region_b == NULL) return 19u;
    struct wine_pe_image image_b;
    if (!load_fixture("/tmp/reloc-none.exe", &image_b, (uint64_t)(uintptr_t)region_b))
        return 20u;
    if (wine_pe_apply_relocations(&image_b) != WINE_PE_RELOC_NONE) return 21u;
    const uint64_t *untouched = (const uint64_t *)(uintptr_t)(image_b.base + 0x1000u);
    if (*untouched != PREFERRED_IMAGE_BASE + 0x1000u) return 22u;
    if (demon_memory_unmap(region_b) != 0u) return 23u;

    /* Case 3: an unsupported relocation type (3 = IMAGE_REL_BASED_HIGHLOW,
       the 32-bit variant this port doesn't handle) must be rejected, not
       silently applied as if it were DIR64. */
    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_fixture(fixture_buffer, true, 3u /* IMAGE_REL_BASED_HIGHLOW */);
    if (!write_fixture("/tmp/reloc-bad-type.exe", fixture_buffer, FIXTURE_SIZE)) return 24u;

    void *region_c = demon_memory_reserve(0x2000u);
    if (region_c == NULL) return 25u;
    struct wine_pe_image image_c;
    if (!load_fixture("/tmp/reloc-bad-type.exe", &image_c, (uint64_t)(uintptr_t)region_c))
        return 26u;
    if (wine_pe_apply_relocations(&image_c) != WINE_PE_RELOC_UNSUPPORTED) return 27u;
    if (demon_memory_unmap(region_c) != 0u) return 28u;

    demon_port_write("WINE_PE_RELOC_OK dir64-applied none-untouched "
                     "unsupported-rejected\n");
    return 0u;
}

#include <demon/portkit.h>
#include <demon/c_app.h>
#include "../../ports/wine/platform/pe_load.h"
#include <stddef.h>
#include <stdint.h>

/* W2 of docs/wine-port.md. Builds its own minimal, synthetic two-section
   PE32+ fixture at runtime -- same legal hygiene as wine_pe_probe's W0
   test: no real Windows/Wine content is bundled or read here. */

/* Only large enough to satisfy demon_port_init's minimum-size check --
   this test never calls demon_port_malloc/calloc, so it needs no real
   heap, just the storage-capability side effect of init(). A large
   static arena here would bake straight into this ELF's .bss and blow
   past every executable slot's tiny page budget (this is a lesson learned
   the hard way: an earlier 256 KiB arena here inflated the loaded image to
   ~262 KiB and silently failed every candidate slot in
   userspace_spawn_path). */
#define ARENA_SIZE 256u
#define FIXTURE_SIZE 2048u

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
                          uint32_t raw_size, uint32_t raw_pointer,
                          uint32_t characteristics) {
    for (size_t i = 0u; i < 8u && name[i] != '\0'; ++i) buffer[offset + i] = (uint8_t)name[i];
    write_u32(buffer, offset + 8u, virtual_size);
    write_u32(buffer, offset + 12u, virtual_address);
    write_u32(buffer, offset + 16u, raw_size);
    write_u32(buffer, offset + 20u, raw_pointer);
    write_u32(buffer, offset + 36u, characteristics);
}

/* .text: VA 0x1000, execute|read, NOT writable, raw data at file offset 512.
   .data: VA 0x2000, read|write, raw data at file offset 1024.
   SizeOfImage covers through 0x3000. */
static void build_two_section_pe(uint8_t *buffer) {
    buffer[0] = 'M'; buffer[1] = 'Z';
    write_u32(buffer, 0x3Cu, 64u);

    buffer[64] = 'P'; buffer[65] = 'E'; buffer[66] = 0u; buffer[67] = 0u;
    write_u16(buffer, 68u, 0x8664u);
    write_u16(buffer, 70u, 2u); /* two sections */
    write_u16(buffer, 84u, 112u);
    write_u16(buffer, 86u, 0x0022u);

    const size_t opt = 88u;
    write_u16(buffer, opt + 0u, 0x020Bu);
    write_u32(buffer, opt + 16u, 0x1010u); /* entry point RVA, inside .text */
    write_u64(buffer, opt + 24u, 0x140000000ULL);
    write_u32(buffer, opt + 32u, 0x1000u);
    write_u32(buffer, opt + 36u, 0x200u);
    write_u32(buffer, opt + 56u, 0x3000u); /* SizeOfImage */
    write_u32(buffer, opt + 60u, 0x200u);
    write_u16(buffer, opt + 68u, 3u);
    write_u32(buffer, opt + 108u, 0u);

    const size_t sections = opt + 112u;
    write_section(buffer, sections, ".text", 0x1000u, 0x1000u, 0x200u, 512u, 0x60000020u);
    write_section(buffer, sections + 40u, ".data", 0x1000u, 0x2000u, 0x200u, 1024u, 0xC0000040u);

    for (size_t i = 0u; i < 0x200u; ++i) buffer[512u + i] = 0xAAu;
    for (size_t i = 0u; i < 0x200u; ++i) buffer[1024u + i] = 0xBBu;
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
/* A static arena, not demon_port_init_dynamic's demon_memory_map (syscall
   40) -- that syscall is mutually exclusive with the reserve/commit path
   (syscalls 46/47) this test needs for wine_pe_load itself, both being the
   same single per-process anonymous region (see docs/wine-port.md's W1).
   demon_port_init still sets up the storage capability this test's file
   I/O needs, just over a plain static array instead. */
static uint8_t static_arena[ARENA_SIZE];

uint64_t wine_pe_load_check_main(void) {
    if (!demon_port_init(static_arena, ARENA_SIZE)) return 10u;

    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_two_section_pe(fixture_buffer);
    if (!write_fixture("/tmp/two-section.exe", fixture_buffer, FIXTURE_SIZE)) return 11u;

    struct demon_port_file pe;
    if (!demon_port_open(&pe, "/tmp/two-section.exe")) return 12u;

    /* wine_pe_load takes an explicit base and reserves nothing itself (see
       pe_load.h) -- this test only ever loads one image, so one
       reservation sized exactly for it is enough. */
    void *reserved = demon_memory_reserve(0x3000u);
    if (reserved == NULL) return 25u;

    struct wine_pe_image image;
    if (wine_pe_load(&pe, (uint64_t)(uintptr_t)reserved, &image) != WINE_PE_LOAD_OK) return 13u;
    demon_port_close(&pe);

    if (image.base == 0u) return 14u;
    if (image.entry_point != image.base + 0x1010u) return 15u;

    /* .text: the file's raw bytes must have landed at VA 0x1000, and the
       tail past raw_size (up to virtual_size) must be zero-filled, not
       garbage. */
    const uint8_t *text = (const uint8_t *)(uintptr_t)(image.base + 0x1000u);
    for (size_t i = 0u; i < 0x200u; ++i)
        if (text[i] != 0xAAu) return 16u;
    if (text[0x200u] != 0u || text[0xFFFu] != 0u) return 17u;

    /* .data: same raw-bytes-then-zero-fill check, at VA 0x2000. */
    const uint8_t *data_section = (const uint8_t *)(uintptr_t)(image.base + 0x2000u);
    for (size_t i = 0u; i < 0x200u; ++i)
        if (data_section[i] != 0xBBu) return 18u;
    if (data_section[0xFFFu] != 0u) return 19u;

    /* .data was committed writable (IMAGE_SCN_MEM_WRITE) -- a real write
       must actually succeed and be observable, not silently ignored. */
    uint8_t *writable_data = (uint8_t *)(uintptr_t)(image.base + 0x2000u);
    writable_data[0] = 0x42u;
    if (writable_data[0] != 0x42u) return 20u;

    /* Padding between the end of .data (0x3000) and SizeOfImage is exactly
       0x3000 here, so there is no tail to separately check -- the whole
       declared image is covered by the two sections' commits. */

    if (demon_memory_unmap((void *)(uintptr_t)image.base) != 0u) return 21u;

    demon_port_write("WINE_PE_LOAD_OK base entry text-data zero-fill "
                     "writable-section unmap\n");
    demon_port_shutdown();
    return 0u;
}

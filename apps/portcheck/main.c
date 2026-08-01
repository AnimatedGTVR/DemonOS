#include <demon/portkit.h>
#include <demon/c_app.h>
#include "../doom/libc.h"
#include "../doom/wad.h"
#include <stddef.h>
#include <stdint.h>

/* This lives in the ordinary per-process heap mapping, not in kernel memory.
   Keeping the test arena at the beginning also catches accidental reliance
   on Doom's former private arena offset. */
#define PORTCHECK_ARENA_SIZE (1024u * 1024u)

static int compare_ints(const void *left, const void *right) {
    const int a = *(const int *)left;
    const int b = *(const int *)right;
    return (a > b) - (a < b);
}

static bool create_fixture(const char *path, const uint8_t *bytes, size_t size) {
    const struct demon_port_runtime *port = demon_port_status();
    const uint64_t handle = demon_file_open(port->storage, path, strlen(path), 1u);
    if (handle == DEMON_PORT_INVALID_HANDLE) return false;
    const bool written = demon_handle_write(handle, bytes, size) == size;
    (void)demon_handle_close(handle);
    return written;
}

static void report_official_wad(uint32_t lumps) {
    char line[64];
    (void)snprintf(line, sizeof(line), "FREEDOOM_IWAD_READY lumps=%u\n", lumps);
    demon_port_write(line);
}

uint64_t portcheck_main(void) {
    if (!demon_port_init_dynamic(PORTCHECK_ARENA_SIZE)) return 10u;
    uint8_t *first = (uint8_t *)demon_port_calloc(128u, 1u);
    uint8_t *second = (uint8_t *)demon_port_malloc(257u);
    if (first == NULL || second == NULL) return 11u;
    for (size_t i = 0u; i < 257u; ++i) second[i] = (uint8_t)i;
    second = (uint8_t *)demon_port_realloc(second, 600u);
    if (second == NULL || second[256] != 0u) return 12u;
    demon_port_free(first); demon_port_free(second);
    if (demon_port_status()->heap_used != 0u ||
        demon_port_status()->heap_peak < 385u) return 13u;
    demon_port_shutdown();
    /* Map again in the same process: this must consume the frames returned by
       syscall 41, not leak another arena from the physical bump allocator. */
    if (!demon_port_init_dynamic(PORTCHECK_ARENA_SIZE)) return 14u;
    uint8_t *reuse = (uint8_t *)demon_port_malloc(900u * 1024u);
    if (reuse == NULL) return 15u;
    reuse[0] = 0x5Au;
    reuse[900u * 1024u - 1u] = 0xA5u;
    if (reuse[0] != 0x5Au || reuse[900u * 1024u - 1u] != 0xA5u) return 16u;
    demon_port_free(reuse);
    char overlap[8] = "abcdef";
    memmove(overlap + 1, overlap, 6u);
    if (memcmp(overlap, "aabcdef", 8u) != 0) return 22u;
    char *end = NULL;
    if (strtol(" -0x2a!", &end, 0) != -42 || end == NULL || *end != '!')
        return 23u;
    int values[6] = {7, -2, 9, 0, 7, 1};
    qsort(values, 6u, sizeof(values[0]), compare_ints);
    if (values[0] != -2 || values[1] != 0 || values[2] != 1 ||
        values[3] != 7 || values[4] != 7 || values[5] != 9) return 24u;
    char formatted[32];
    if (snprintf(formatted, sizeof(formatted), "%s:%04x:%d", "doom", 42u,
                 -7) != 12 || strcmp(formatted, "doom:002a:-7") != 0)
        return 25u;
    if (snprintf(formatted, sizeof(formatted), "%p", (void *)(uintptr_t)0x1234u) != 6 ||
        strcmp(formatted, "0x1234") != 0)
        return 36u;
    if (snprintf(formatted, sizeof(formatted), "STCFN%.3d", 33) != 8 ||
        strcmp(formatted, "STCFN033") != 0)
        return 37u;
    char *libc_heap = (char *)calloc(32u, 1u);
    if (libc_heap == NULL || libc_heap[31] != '\0') return 26u;
    strcpy(libc_heap, "port-libc");
    libc_heap = (char *)realloc(libc_heap, 128u);
    if (libc_heap == NULL || strcmp(libc_heap, "port-libc") != 0) return 27u;
    free(libc_heap);
    static const uint8_t valid_wad[] = {
        'I','W','A','D', 1,0,0,0, 16,0,0,0,
        0x11,0x22,0x33,0x44,
        12,0,0,0, 4,0,0,0, 'T','E','S','T',0,0,0,0,
    };
    static const uint8_t bad_directory_wad[] = {
        'I','W','A','D', 1,0,0,0, 0xF0,0xFF,0xFF,0xFF,
    };
    static const uint8_t bad_lump_wad[] = {
        'P','W','A','D', 1,0,0,0, 12,0,0,0,
        28,0,0,0, 8,0,0,0, 'B','A','D',0,0,0,0,0,
    };
    if (!create_fixture("/tmp/valid.wad", valid_wad, sizeof(valid_wad)) ||
        !create_fixture("/tmp/bad-directory.wad", bad_directory_wad,
                        sizeof(bad_directory_wad)) ||
        !create_fixture("/tmp/bad-lump.wad", bad_lump_wad,
                        sizeof(bad_lump_wad))) return 28u;
    struct demon_port_file wad;
    struct doom_wad_info wad_info;
    if (!demon_port_open(&wad, "/tmp/valid.wad")) return 29u;
    if (doom_wad_validate(&wad, &wad_info) != DOOM_WAD_OK ||
        !wad_info.is_iwad || wad_info.lump_count != 1u ||
        wad_info.directory_offset != 16u) return 30u;
    demon_port_close(&wad);
    if (!demon_port_open(&wad, "/tmp/bad-directory.wad")) return 31u;
    if (doom_wad_validate(&wad, &wad_info) != DOOM_WAD_BAD_DIRECTORY)
        return 32u;
    demon_port_close(&wad);
    if (!demon_port_open(&wad, "/tmp/bad-lump.wad")) return 33u;
    if (doom_wad_validate(&wad, &wad_info) != DOOM_WAD_BAD_LUMP) return 34u;
    demon_port_close(&wad);
    /* The source archive is a real multi-megabyte Multiboot module. Reading
       its beginning and end proves both reference-backed storage and offset
       I/O without consuming the 1 MiB process heap. */
    struct demon_port_file archive;
    if (!demon_port_open(&archive, "/system/mako/MAKO-source.tar.zst")) return 17u;
    if (archive.size <= 1024u * 1024u) return 18u;
    uint8_t magic[4];
    if (demon_port_read(&archive, magic, sizeof(magic)) != sizeof(magic) ||
        magic[0] != 0x28u || magic[1] != 0xB5u ||
        magic[2] != 0x2Fu || magic[3] != 0xFDu) return 19u;
    if (!demon_port_seek(&archive, archive.size - 1u)) return 20u;
    uint8_t tail;
    if (demon_port_read(&archive, &tail, 1u) != 1u ||
        demon_port_tell(&archive) != archive.size) return 21u;
    demon_port_close(&archive);
    if (demon_port_open(&wad, "/games/freedoom/freedoom1.wad")) {
        if (doom_wad_validate(&wad, &wad_info) != DOOM_WAD_OK ||
            !wad_info.is_iwad || wad_info.lump_count == 0u) return 35u;
        report_official_wad(wad_info.lump_count);
        demon_port_close(&wad);
    }
    demon_port_write("PORTKIT_READY allocator libc wad-validation reuse large-files seek timing input\n");
    demon_port_shutdown();
    return 0u;
}

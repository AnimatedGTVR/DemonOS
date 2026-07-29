#include <kernel/apps.h>
#include <kernel/ramfs.h>

// Raised from 16: by Wave 7 of the EDE port there are 20 .elf files seeded
// into RAMFS (see grub/grub.cfg's module2 list), and apps_init()'s scan
// silently stops cataloging once it hits this limit -- ede-autostart (the
// 17th .elf entry) was landing past the old cutoff and "apps launch
// ede-autostart" failed with "launch target unavailable" despite the file
// being right there in RAMFS. Headroom added for further growth.
#define APP_LIMIT 32u

struct app_record {
    const char *name;
    const char *path;
    size_t bytes;
    bool valid;
};

static struct app_record catalog[APP_LIMIT];
static size_t catalog_count;
static uint64_t scans;

static bool equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static bool elf_suffix(const char *name, size_t length) {
    return length >= 4u && name[length - 4u] == '.' && name[length - 3u] == 'e' &&
        name[length - 2u] == 'l' && name[length - 1u] == 'f';
}

static const char *app_name_for_path(const char *path) {
    if (equal(path, "/projects/hello/main.elf")) return "hello";
    if (equal(path, "/system/bin/tetris.elf")) return "tetris";
    if (equal(path, "/system/bin/calculator.elf")) return "calculator";
    if (equal(path, "/system/bin/ede-calc.elf")) return "ede-calc";
    if (equal(path, "/system/bin/ede-about.elf")) return "ede-about";
    if (equal(path, "/system/bin/ede-tip.elf")) return "ede-tip";
    if (equal(path, "/system/bin/ede-preferred.elf")) return "ede-preferred";
    if (equal(path, "/system/bin/ede-conf.elf")) return "ede-conf";
    if (equal(path, "/system/bin/ede-autostart.elf")) return "ede-autostart";
    if (equal(path, "/system/bin/ede-timedate.elf")) return "ede-timedate";
    return path;
}

void apps_init(void) {
    catalog_count = 0u;
    ++scans;
    for (size_t index = 0u; index < ramfs_file_count() && catalog_count < APP_LIMIT; ++index) {
        const char *path;
        size_t path_length;
        size_t bytes;
        if (!ramfs_entry(index, &path, &path_length, &bytes) || !elf_suffix(path, path_length))
            continue;
        uint32_t object_id;
        const uint8_t *image;
        size_t image_size;
        if (!ramfs_open(path, path_length, false, &object_id) ||
            !ramfs_view(object_id, &image, &image_size)) continue;
        const bool valid = image_size >= 20u && image[0] == 0x7Fu && image[1] == 'E' &&
            image[2] == 'L' && image[3] == 'F' && image[4] == 2u && image[5] == 1u &&
            image[18] == 0x3Eu && image[19] == 0u;
        catalog[catalog_count++] = (struct app_record){
            .name = app_name_for_path(path), .path = path, .bytes = bytes, .valid = valid,
        };
    }
}

size_t apps_count(void) { return catalog_count; }
uint64_t apps_scan_count(void) { return scans; }

bool apps_snapshot(size_t index, struct app_snapshot *snapshot) {
    if (index >= catalog_count || snapshot == NULL) return false;
    snapshot->name = catalog[index].name;
    snapshot->path = catalog[index].path;
    snapshot->image_bytes = catalog[index].bytes;
    snapshot->valid_elf64 = catalog[index].valid;
    return true;
}

bool apps_find(const char *name, struct app_snapshot *snapshot) {
    for (size_t index = 0u; index < catalog_count; ++index)
        if (equal(catalog[index].name, name)) return apps_snapshot(index, snapshot);
    return false;
}

bool apps_self_test(void) {
    struct app_snapshot hello;
    struct app_snapshot tetris;
    struct app_snapshot calculator;
    return catalog_count >= 1u && apps_find("hello", &hello) &&
        hello.valid_elf64 && hello.image_bytes > 0u &&
        apps_find("tetris", &tetris) && tetris.valid_elf64 &&
        tetris.image_bytes > 0u &&
        apps_find("calculator", &calculator) && calculator.valid_elf64 &&
        calculator.image_bytes > 0u;
}

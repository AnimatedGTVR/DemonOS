#include "persistence.h"

#include <demon/portkit.h>
#include <string.h>

#define CONFIG_BYTES 32u
#define CONFIG_VERSION 1u

static void put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static uint16_t get_u16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint32_t checksum(const uint8_t *bytes, uint32_t count) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool valid(const DemonButterscotchConfig *config) {
    return config != NULL && config->windowWidth >= 160u &&
        config->windowWidth <= 8192u && config->windowHeight >= 120u &&
        config->windowHeight <= 8192u && config->masterGainQ8 <= 256u;
}

static bool encode(const DemonButterscotchConfig *config,
                   uint8_t bytes[CONFIG_BYTES]) {
    if (!valid(config)) return false;
    memset(bytes, 0, CONFIG_BYTES);
    bytes[0] = 'B'; bytes[1] = 'S'; bytes[2] = 'C'; bytes[3] = 'F';
    put_u32(bytes + 4u, CONFIG_VERSION);
    put_u32(bytes + 8u, config->gameId);
    put_u32(bytes + 12u, config->windowWidth);
    put_u32(bytes + 16u, config->windowHeight);
    put_u16(bytes + 20u, config->masterGainQ8);
    put_u16(bytes + 22u, config->fullscreen ? 1u : 0u);
    put_u32(bytes + 28u, checksum(bytes, 28u));
    return true;
}

static bool decode(const uint8_t bytes[CONFIG_BYTES],
                   DemonButterscotchConfig *config) {
    if (config == NULL || bytes[0] != 'B' || bytes[1] != 'S' ||
        bytes[2] != 'C' || bytes[3] != 'F' ||
        get_u32(bytes + 4u) != CONFIG_VERSION ||
        get_u32(bytes + 28u) != checksum(bytes, 28u) ||
        get_u16(bytes + 22u) > 1u) return false;
    *config = (DemonButterscotchConfig){
        .gameId = get_u32(bytes + 8u),
        .windowWidth = get_u32(bytes + 12u),
        .windowHeight = get_u32(bytes + 16u),
        .masterGainQ8 = get_u16(bytes + 20u),
        .fullscreen = get_u16(bytes + 22u) != 0u
    };
    return valid(config);
}

bool DemonButterscotchPersistence_save(const char *path, const char *tempPath,
                                       const DemonButterscotchConfig *config) {
    uint8_t bytes[CONFIG_BYTES];
    struct demon_port_file file;
    if (path == NULL || tempPath == NULL || !encode(config, bytes) ||
        !demon_port_create(&file, tempPath)) return false;
    const bool written = demon_port_write_file(&file, bytes, sizeof(bytes)) ==
        sizeof(bytes);
    demon_port_close(&file);
    if (!written) {
        (void)demon_port_delete(tempPath);
        return false;
    }
    (void)demon_port_delete(path);
    if (!demon_port_rename(tempPath, path)) {
        (void)demon_port_delete(tempPath);
        return false;
    }
    return true;
}

bool DemonButterscotchPersistence_load(const char *path,
                                       DemonButterscotchConfig *config) {
    uint8_t bytes[CONFIG_BYTES];
    struct demon_port_file file;
    if (path == NULL || config == NULL || !demon_port_open(&file, path))
        return false;
    const bool loaded = file.size == sizeof(bytes) &&
        demon_port_read(&file, bytes, sizeof(bytes)) == sizeof(bytes);
    demon_port_close(&file);
    return loaded && decode(bytes, config);
}

bool DemonButterscotchPersistence_selfTest(uint32_t *bytes,
                                           uint32_t *hash) {
    static const char path[] = "/home/demon/.butterscotch-test.cfg";
    static const char temp[] = "/home/demon/.butterscotch-test.tmp";
    static const char corrupt[] = "/home/demon/.butterscotch-corrupt.cfg";
    if (bytes == NULL || hash == NULL) return false;
    const DemonButterscotchConfig expected = {
        .gameId = 661770190u, .windowWidth = 640u, .windowHeight = 480u,
        .masterGainQ8 = 192u, .fullscreen = false
    };
    DemonButterscotchConfig actual;
    if (!DemonButterscotchPersistence_save(path, temp, &expected) ||
        !DemonButterscotchPersistence_load(path, &actual) ||
        expected.gameId != actual.gameId ||
        expected.windowWidth != actual.windowWidth ||
        expected.windowHeight != actual.windowHeight ||
        expected.masterGainQ8 != actual.masterGainQ8 ||
        expected.fullscreen != actual.fullscreen) return false;
    struct demon_port_file file;
    uint8_t encoded[CONFIG_BYTES];
    if (!demon_port_open(&file, path) ||
        demon_port_read(&file, encoded, sizeof(encoded)) != sizeof(encoded)) {
        demon_port_close(&file);
        return false;
    }
    demon_port_close(&file);
    *bytes = CONFIG_BYTES;
    *hash = get_u32(encoded + 28u);
    encoded[12] ^= 0x40u;
    if (!demon_port_create(&file, corrupt)) {
        (void)demon_port_delete(path);
        return false;
    }
    const bool corrupt_written = demon_port_write_file(&file, encoded,
        sizeof(encoded)) == sizeof(encoded);
    demon_port_close(&file);
    DemonButterscotchConfig rejected;
    const bool corrupt_rejected = corrupt_written &&
        !DemonButterscotchPersistence_load(corrupt, &rejected);
    const bool cleaned = demon_port_delete(corrupt) &&
        demon_port_delete(path);
    return corrupt_rejected && cleaned;
}

#ifndef DEMONOS_BUTTERSCOTCH_PERSISTENCE_H
#define DEMONOS_BUTTERSCOTCH_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t gameId;
    uint32_t windowWidth;
    uint32_t windowHeight;
    uint16_t masterGainQ8;
    bool fullscreen;
} DemonButterscotchConfig;

bool DemonButterscotchPersistence_save(const char *path, const char *tempPath,
                                       const DemonButterscotchConfig *config);
bool DemonButterscotchPersistence_load(const char *path,
                                       DemonButterscotchConfig *config);
bool DemonButterscotchPersistence_selfTest(uint32_t *bytes,
                                           uint32_t *checksum);

#endif

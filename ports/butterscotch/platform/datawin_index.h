#ifndef DEMONOS_BUTTERSCOTCH_DATAWIN_INDEX_H
#define DEMONOS_BUTTERSCOTCH_DATAWIN_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binary_reader.h"

#define DATAWIN_INDEX_MAX_CHUNKS 64u
#define DATAWIN_TAG(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8u) | \
    ((uint32_t)(c) << 16u) | ((uint32_t)(d) << 24u))

typedef struct {
    uint32_t tag;
    uint32_t headerOffset;
    uint32_t payloadOffset;
    uint32_t size;
} DemonDataWinChunk;

typedef struct {
    DemonDataWinChunk chunks[DATAWIN_INDEX_MAX_CHUNKS];
    uint32_t count;
    uint32_t duplicateCount;
} DemonDataWinIndex;

typedef struct {
    uint32_t count;
    uint32_t nonNullCount;
    uint32_t totalBytes;
    uint32_t maxLength;
    uint32_t fnv1a;
} DemonDataWinStringStats;

typedef struct {
    uint8_t wadVersion;
    uint32_t gameId;
    uint32_t major;
    uint32_t minor;
    uint32_t release;
    uint32_t build;
    uint32_t defaultWidth;
    uint32_t defaultHeight;
} DemonDataWinGen8Summary;

typedef struct {
    uint32_t count;
    uint32_t presentCount;
    uint32_t totalBytecodeBytes;
    uint32_t maxBytecodeLength;
    uint32_t totalLocals;
    uint32_t totalArguments;
    uint32_t blobStart;
    uint32_t blobEnd;
    uint32_t fnv1a;
} DemonDataWinCodeStats;

typedef struct {
    uint32_t count;
    uint32_t presentCount;
    uint32_t eventTypeLists;
    uint32_t eventCount;
    uint32_t actionCount;
    uint32_t codeActionCount;
    uint32_t physicsVertexCount;
    uint32_t inheritanceEdges;
} DemonDataWinObjectStats;

typedef struct {
    uint32_t count;
    uint32_t presentCount;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t backgroundSlots;
    uint32_t enabledBackgrounds;
    uint32_t viewSlots;
    uint32_t enabledViews;
    uint32_t instanceCount;
    uint32_t boundInstanceCount;
    uint32_t tileCount;
    uint32_t creationCodeRefs;
} DemonDataWinRoomStats;

typedef struct {
    uint32_t pageItemCount;
    uint32_t presentPageItems;
    uint32_t textureCount;
    uint32_t embeddedTextures;
    uint32_t totalPngBytes;
    uint32_t totalAtlasPixels;
    uint32_t maxTextureWidth;
    uint32_t maxTextureHeight;
    uint32_t fnv1a;
} DemonDataWinTextureStats;

typedef struct {
    uint32_t soundCount;
    uint32_t presentSounds;
    uint32_t embeddedSounds;
    uint32_t compressedSounds;
    uint32_t audioEntryCount;
    uint32_t presentAudioEntries;
    uint32_t totalAudioBytes;
    uint32_t largestAudioEntry;
    uint32_t fnv1a;
} DemonDataWinAudioStats;

typedef struct {
    uint16_t sourceX, sourceY, sourceWidth, sourceHeight;
    uint16_t targetX, targetY, targetWidth, targetHeight;
    uint16_t boundingWidth, boundingHeight;
    int16_t texturePageId;
} DemonDataWinPageItem;

#define DEMON_DATAWIN_SCENE_INSTANCE_MAX 16u
typedef struct {
    int32_t x, y;
    int32_t objectId;
    int32_t spriteId;
    int32_t pageItemId;
    uint32_t instanceId;
} DemonDataWinSceneInstance;
typedef struct {
    uint32_t roomWidth, roomHeight;
    uint32_t instanceCount;
    DemonDataWinSceneInstance instances[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
} DemonDataWinScene;

bool DemonDataWinIndex_build(BinaryReader *reader, size_t fileSize,
                             DemonDataWinIndex *index);
const DemonDataWinChunk *DemonDataWinIndex_find(const DemonDataWinIndex *index,
                                                uint32_t tag);
bool DemonDataWinIndex_pointerCount(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    uint32_t tag, uint32_t *count);
bool DemonDataWinIndex_inspectStrings(BinaryReader *reader,
                                      const DemonDataWinIndex *index,
                                      DemonDataWinStringStats *stats);
bool DemonDataWinIndex_readGen8(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                DemonDataWinGen8Summary *summary);
bool DemonDataWinIndex_inspectCode(BinaryReader *reader,
                                   const DemonDataWinIndex *index,
                                   uint8_t wadVersion,
                                   DemonDataWinCodeStats *stats);
bool DemonDataWinIndex_inspectObjects(BinaryReader *reader,
                                      const DemonDataWinIndex *index,
                                      const DemonDataWinGen8Summary *gen8,
                                      uint32_t codeCount,
                                      DemonDataWinObjectStats *stats);
bool DemonDataWinIndex_inspectRooms(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    const DemonDataWinGen8Summary *gen8,
                                    uint32_t objectCount,
                                    uint32_t codeCount,
                                    DemonDataWinRoomStats *stats);
bool DemonDataWinIndex_inspectTextures(BinaryReader *reader,
                                       const DemonDataWinIndex *index,
                                       const DemonDataWinGen8Summary *gen8,
                                       DemonDataWinTextureStats *stats);
bool DemonDataWinIndex_inspectAudio(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    uint8_t wadVersion,
                                    DemonDataWinAudioStats *stats);
bool DemonDataWinIndex_audioBoundsSelfTest(void);
/* Decompresses a GameMaker BZip2-compressed texture-group blob into real
   pixel bytes (see datawin_index.c for the wire format and verification
   notes). Caller owns *out_pixels and must demon_port_free it. */
bool DemonDataWinIndex_decompressTexture(BinaryReader *reader, uint32_t start,
                                         uint32_t limit, uint8_t **out_pixels,
                                         size_t *out_length, uint32_t *width,
                                         uint32_t *height);
/* BZip2-decompress and then QOI-variant-decode a texture blob into real
   BGRA8 pixel bytes (width*height*4). Caller owns *out_pixels and must
   demon_port_free it. */
bool DemonDataWinIndex_decodeTexturePixels(BinaryReader *reader, uint32_t start,
                                           uint32_t limit, uint8_t **out_pixels,
                                           size_t *out_length, uint32_t *width,
                                           uint32_t *height);
bool DemonDataWinIndex_textureBlob(BinaryReader *reader,
                                   const DemonDataWinIndex *index,
                                   const DemonDataWinGen8Summary *gen8,
                                   uint32_t textureIndex,
                                   uint32_t *blobOffset,
                                   uint32_t *blobSize);
bool DemonDataWinIndex_pageItem(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint32_t itemIndex,
                                DemonDataWinPageItem *item);
bool DemonDataWinIndex_roomScene(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 const DemonDataWinGen8Summary *gen8,
                                 uint32_t roomIndex,
                                 DemonDataWinScene *scene);
bool DemonDataWinIndex_collisionCode(BinaryReader *reader,
                                     const DemonDataWinIndex *index,
                                     const DemonDataWinGen8Summary *gen8,
                                     uint32_t objectId, uint32_t otherObjectId,
                                     uint32_t *codeId);
bool DemonDataWinIndex_eventCode(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 const DemonDataWinGen8Summary *gen8,
                                 uint32_t objectId, uint32_t eventType,
                                 uint32_t eventSubtype, uint32_t *codeId);

#endif

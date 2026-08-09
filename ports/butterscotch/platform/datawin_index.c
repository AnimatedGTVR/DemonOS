#include "datawin_index.h"

#include <string.h>
#include <demon/portkit.h>

#include "binary_utils.h"
#include "bzip2_decode.h"
#include "qoi_decode.h"

#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
static void datawin_debug_u64(const char *label, uint64_t value) {
    char buffer[96];
    size_t length = 0u;
    for (const char *c = label; *c != '\0'; ++c) buffer[length++] = *c;
    char digits[20];
    size_t count = 0u;
    uint64_t v = value;
    if (v == 0u) digits[count++] = '0';
    while (v > 0u) { digits[count++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (size_t i = count; i > 0u; --i) buffer[length++] = digits[i - 1u];
    buffer[length++] = '\n';
    buffer[length] = '\0';
    demon_port_write(buffer);
}
#endif

const DemonDataWinChunk *DemonDataWinIndex_find(const DemonDataWinIndex *index,
                                                uint32_t tag) {
    if (index == NULL) return NULL;
    for (uint32_t i = 0u; i < index->count; ++i) {
        if (index->chunks[i].tag == tag) return &index->chunks[i];
    }
    return NULL;
}

bool DemonDataWinIndex_build(BinaryReader *reader, size_t fileSize,
                             DemonDataWinIndex *index) {
    if (reader == NULL || index == NULL || fileSize < 16u ||
        fileSize > UINT32_MAX) return false;

    memset(index, 0, sizeof(*index));
    BinaryReader_seek(reader, 0u);
    if (BinaryReader_readUint32(reader) != DATAWIN_TAG('F', 'O', 'R', 'M'))
        return false;
    const uint32_t declaredSize = BinaryReader_readUint32(reader);
    if ((uint64_t)declaredSize + 8u != (uint64_t)fileSize) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_FORM_SIZE declared+8=",
                          (uint64_t)declaredSize + 8u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_FORM_SIZE fileSize=",
                          (uint64_t)fileSize);
#endif
        return false;
    }

    while (BinaryReader_getPosition(reader) < fileSize) {
        const size_t header = BinaryReader_getPosition(reader);
        if (fileSize - header < 8u || index->count == DATAWIN_INDEX_MAX_CHUNKS) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_LOOP fail_at_count=",
                              (uint64_t)index->count);
#endif
            return false;
        }

        const uint32_t tag = BinaryReader_readUint32(reader);
        const uint32_t size = BinaryReader_readUint32(reader);
        const size_t payload = BinaryReader_getPosition(reader);
        if ((uint64_t)size > (uint64_t)(fileSize - payload)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_OVERFLOW tag=", (uint64_t)tag);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_OVERFLOW size=", (uint64_t)size);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_OVERFLOW remaining=",
                              (uint64_t)(fileSize - payload));
#endif
            return false;
        }

        if (DemonDataWinIndex_find(index, tag) != NULL)
            ++index->duplicateCount;
        DemonDataWinChunk *chunk = &index->chunks[index->count++];
        chunk->tag = tag;
        chunk->headerOffset = (uint32_t)header;
        chunk->payloadOffset = (uint32_t)payload;
        chunk->size = size;
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK tag=", (uint64_t)tag);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK size=", (uint64_t)size);
#endif
        BinaryReader_skip(reader, (size_t)size);
    }

#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_LOOP final_position=",
                      (uint64_t)BinaryReader_getPosition(reader));
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_LOOP count=", (uint64_t)index->count);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_CHUNK_LOOP duplicates=",
                      (uint64_t)index->duplicateCount);
#endif
    return BinaryReader_getPosition(reader) == fileSize && index->count != 0u &&
        index->duplicateCount == 0u;
}

bool DemonDataWinIndex_pointerCount(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    uint32_t tag, uint32_t *count) {
    const DemonDataWinChunk *chunk = DemonDataWinIndex_find(index, tag);
    if (reader == NULL || chunk == NULL || count == NULL || chunk->size < 4u)
        return false;

    BinaryReader_seek(reader, chunk->payloadOffset);
    const uint32_t entries = BinaryReader_readUint32(reader);
    if ((uint64_t)entries * 4u > (uint64_t)chunk->size - 4u) return false;

    const uint64_t dataStart = (uint64_t)chunk->payloadOffset + 4u +
        (uint64_t)entries * 4u;
    const uint64_t dataEnd = (uint64_t)chunk->payloadOffset + chunk->size;
    for (uint32_t i = 0u; i < entries; ++i) {
        const uint32_t offset = BinaryReader_readUint32(reader);
        if (offset != 0u && ((uint64_t)offset < dataStart ||
                            (uint64_t)offset >= dataEnd)) return false;
    }
    *count = entries;
    return true;
}

bool DemonDataWinIndex_inspectStrings(BinaryReader *reader,
                                      const DemonDataWinIndex *index,
                                      DemonDataWinStringStats *stats) {
    const DemonDataWinChunk *chunk = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    uint32_t count = 0u;
    if (stats == NULL || chunk == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('S', 'T', 'R', 'G'), &count)) return false;

    memset(stats, 0, sizeof(*stats));
    stats->count = count;
    stats->fnv1a = 2166136261u;
    const uint64_t chunkEnd = (uint64_t)chunk->payloadOffset + chunk->size;
    BinaryReader_seek(reader, (size_t)chunk->payloadOffset + 4u);

    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t pointer = BinaryReader_readUint32(reader);
        if (pointer == 0u) continue;
        const size_t saved = BinaryReader_getPosition(reader);
        if ((uint64_t)pointer + 5u > chunkEnd) return false;
        BinaryReader_seek(reader, pointer);
        const uint32_t length = BinaryReader_readUint32(reader);
        if ((uint64_t)pointer + 5u + length > chunkEnd ||
            UINT32_MAX - stats->totalBytes < length) return false;
        for (uint32_t j = 0u; j < length; ++j) {
            const uint8_t byte = BinaryReader_readUint8(reader);
            if (byte == 0u) return false;
            stats->fnv1a ^= byte;
            stats->fnv1a *= 16777619u;
        }
        if (BinaryReader_readUint8(reader) != 0u) return false;
        stats->fnv1a ^= 0u;
        stats->fnv1a *= 16777619u;
        ++stats->nonNullCount;
        stats->totalBytes += length;
        if (length > stats->maxLength) stats->maxLength = length;
        BinaryReader_seek(reader, saved);
    }
    return true;
}

bool DemonDataWinIndex_readGen8(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                DemonDataWinGen8Summary *summary) {
    const DemonDataWinChunk *chunk = DemonDataWinIndex_find(index,
        DATAWIN_TAG('G', 'E', 'N', '8'));
    if (reader == NULL || chunk == NULL || summary == NULL || chunk->size < 44u)
        return false;

    memset(summary, 0, sizeof(*summary));
    BinaryReader_seek(reader, chunk->payloadOffset);
    (void)BinaryReader_readUint8(reader); /* debugger disabled flag */
    summary->wadVersion = BinaryReader_readUint8(reader);
    BinaryReader_skip(reader, 2u);

    const bool compactWad8 = summary->wadVersion <= 8u && chunk->size < 108u;
    if (compactWad8) {
        BinaryReader_skip(reader, 12u); /* filename pointer, last object/tile */
        summary->gameId = BinaryReader_readUint32(reader);
        BinaryReader_skip(reader, 16u); /* DirectPlay GUID */
        summary->major = 1u;
        summary->build = 198u;
        summary->defaultWidth = BinaryReader_readUint32(reader);
        summary->defaultHeight = BinaryReader_readUint32(reader);
    } else {
        if (chunk->size < 72u) return false;
        BinaryReader_skip(reader, 16u); /* filename/config, last object/tile */
        summary->gameId = BinaryReader_readUint32(reader);
        BinaryReader_skip(reader, 20u); /* DirectPlay GUID and name pointer */
        summary->major = BinaryReader_readUint32(reader);
        summary->minor = BinaryReader_readUint32(reader);
        summary->release = BinaryReader_readUint32(reader);
        summary->build = BinaryReader_readUint32(reader);
        summary->defaultWidth = BinaryReader_readUint32(reader);
        summary->defaultHeight = BinaryReader_readUint32(reader);
    }

    return summary->wadVersion >= 8u && summary->wadVersion <= 17u &&
        summary->defaultWidth != 0u && summary->defaultWidth <= 16384u &&
        summary->defaultHeight != 0u && summary->defaultHeight <= 16384u;
}

static bool string_content_pointer_is_valid(BinaryReader *reader,
                                             const DemonDataWinChunk *strg,
                                             uint32_t pointer) {
    if (pointer == 0u) return true;
    const uint64_t start = strg->payloadOffset;
    const uint64_t end = start + strg->size;
    if ((uint64_t)pointer < start + 4u || (uint64_t)pointer >= end)
        return false;

    const size_t saved = BinaryReader_getPosition(reader);
    BinaryReader_seek(reader, pointer - 4u);
    const uint32_t length = BinaryReader_readUint32(reader);
    if ((uint64_t)pointer + length >= end) return false;
    BinaryReader_seek(reader, pointer + length);
    const bool valid = BinaryReader_readUint8(reader) == 0u;
    BinaryReader_seek(reader, saved);
    return valid;
}

bool DemonDataWinIndex_inspectCode(BinaryReader *reader,
                                   const DemonDataWinIndex *index,
                                   uint8_t wadVersion,
                                   DemonDataWinCodeStats *stats) {
    const DemonDataWinChunk *code = DemonDataWinIndex_find(index,
        DATAWIN_TAG('C', 'O', 'D', 'E'));
    const DemonDataWinChunk *strg = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    uint32_t count = 0u;
    if (reader == NULL || stats == NULL || code == NULL || strg == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('C', 'O', 'D', 'E'), &count)) return false;

    memset(stats, 0, sizeof(*stats));
    stats->count = count;
    stats->blobStart = UINT32_MAX;
    stats->fnv1a = 2166136261u;
    const uint64_t chunkStart = code->payloadOffset;
    const uint64_t chunkEnd = chunkStart + code->size;
    const bool oldFormat = wadVersion <= 14u;

    BinaryReader_seek(reader, code->payloadOffset + 4u);
    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t entryPointer = BinaryReader_readUint32(reader);
        if (entryPointer == 0u) continue;
        const size_t tablePosition = BinaryReader_getPosition(reader);
        const uint32_t headerSize = oldFormat ? 8u : 20u;
        if ((uint64_t)entryPointer + headerSize > chunkEnd) return false;

        BinaryReader_seek(reader, entryPointer);
        const uint32_t namePointer = BinaryReader_readUint32(reader);
        const uint32_t length = BinaryReader_readUint32(reader);
        uint32_t locals = 0u;
        uint32_t arguments = 0u;
        uint64_t bytecode = (uint64_t)entryPointer + 8u;
        if (!oldFormat) {
            locals = BinaryReader_readUint16(reader);
            arguments = BinaryReader_readUint16(reader);
            const uint64_t relativeField = BinaryReader_getPosition(reader);
            const int32_t relative = BinaryReader_readInt32(reader);
            const int64_t target = (int64_t)relativeField + relative;
            (void)BinaryReader_readUint32(reader); /* entry-relative offset */
            if (target < 0) return false;
            bytecode = (uint64_t)target;
        }

        if (!string_content_pointer_is_valid(reader, strg, namePointer) ||
            bytecode < chunkStart || bytecode > chunkEnd ||
            (uint64_t)length > chunkEnd - bytecode ||
            UINT32_MAX - stats->totalBytecodeBytes < length ||
            UINT32_MAX - stats->totalLocals < locals ||
            UINT32_MAX - stats->totalArguments < arguments) return false;

        ++stats->presentCount;
        stats->totalBytecodeBytes += length;
        stats->totalLocals += locals;
        stats->totalArguments += arguments;
        if (length > stats->maxBytecodeLength)
            stats->maxBytecodeLength = length;
        if (bytecode < stats->blobStart) stats->blobStart = (uint32_t)bytecode;
        if (bytecode + length > stats->blobEnd)
            stats->blobEnd = (uint32_t)(bytecode + length);

        BinaryReader_seek(reader, (size_t)bytecode);
        for (uint32_t j = 0u; j < length; ++j) {
            stats->fnv1a ^= BinaryReader_readUint8(reader);
            stats->fnv1a *= 16777619u;
        }
        BinaryReader_seek(reader, tablePosition);
    }

    if (stats->presentCount == 0u) {
        stats->blobStart = code->payloadOffset;
        stats->blobEnd = code->payloadOffset;
    }
    return true;
}

/* Streaming-compatible: reads through the BinaryReader's own seek/read
   API instead of indexing a memory buffer directly, so this works
   whether the caller built the reader over a fully-buffered file or a
   real demon_port_file handle (BUTTERSCOTCH_COMPAT_PROBE mode, used for
   real game packages too large to fit whole in the 24 MiB process
   heap -- see DemonDataWinIndex_inspectAudio's own comment). */
static bool bounded_audio_blob(BinaryReader *reader, uint32_t fileSize,
                               const DemonDataWinChunk *chunk,
                               uint32_t entry, uint32_t *dataOffset,
                               uint32_t *dataSize) {
    if (reader == NULL || chunk == NULL || dataOffset == NULL ||
        dataSize == NULL || (uint64_t)chunk->payloadOffset + chunk->size >
        fileSize || entry < chunk->payloadOffset ||
        (uint64_t)entry + 4u > (uint64_t)chunk->payloadOffset + chunk->size)
        return false;
    BinaryReader_seek(reader, entry);
    const uint32_t size = BinaryReader_readUint32(reader);
    const uint64_t data = (uint64_t)entry + 4u;
    const uint64_t end = (uint64_t)chunk->payloadOffset + chunk->size;
    if (data + size > end) return false;
    *dataOffset = (uint32_t)data;
    *dataSize = size;
    return true;
}

bool DemonDataWinIndex_audioBoundsSelfTest(void) {
    uint8_t bytes[16] = {0};
    bytes[4] = 8u;
    const DemonDataWinChunk chunk = {
        .payloadOffset = 0u, .size = sizeof(bytes)
    };
    BinaryReader reader = BinaryReader_create(NULL, sizeof(bytes));
    BinaryReader_setBuffer(&reader, bytes, 0u, sizeof(bytes));
    uint32_t offset = 0u, size = 0u;
    if (!bounded_audio_blob(&reader, sizeof(bytes), &chunk, 4u, &offset,
                            &size) || offset != 8u || size != 8u)
        return false;
    bytes[4] = 9u;
    BinaryReader_setBuffer(&reader, bytes, 0u, sizeof(bytes));
    return !bounded_audio_blob(&reader, sizeof(bytes), &chunk, 4u, &offset,
                               &size);
}

bool DemonDataWinIndex_inspectAudio(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    uint8_t wadVersion,
                                    DemonDataWinAudioStats *stats) {
    const DemonDataWinChunk *sond = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'O', 'N', 'D'));
    const DemonDataWinChunk *audo = DemonDataWinIndex_find(index,
        DATAWIN_TAG('A', 'U', 'D', 'O'));
    const DemonDataWinChunk *strg = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    uint32_t soundCount = 0u, audioCount = 0u;
    if (reader == NULL || index == NULL || stats == NULL || sond == NULL ||
        audo == NULL || strg == NULL || reader->fileSize > UINT32_MAX ||
        wadVersion < 8u ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('S', 'O', 'N', 'D'), &soundCount) ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('A', 'U', 'D', 'O'), &audioCount)) return false;
    memset(stats, 0, sizeof(*stats));
    stats->soundCount = soundCount;
    stats->audioEntryCount = audioCount;
    stats->fnv1a = 2166136261u;
    const uint64_t soundEnd = (uint64_t)sond->payloadOffset + sond->size;
    for (uint32_t i = 0u; i < soundCount; ++i) {
        BinaryReader_seek(reader, sond->payloadOffset + 4u + i * 4u);
        const uint32_t entry = BinaryReader_readUint32(reader);
        if (entry == 0u) continue;
        const uint32_t recordSize = wadVersion <= 12u ? 40u : 36u;
        if (entry < sond->payloadOffset ||
            (uint64_t)entry + recordSize > soundEnd) return false;
        BinaryReader_seek(reader, entry);
        const uint32_t name = BinaryReader_readUint32(reader);
        const uint32_t flags = BinaryReader_readUint32(reader);
        const uint32_t type = BinaryReader_readUint32(reader);
        const uint32_t file = BinaryReader_readUint32(reader);
        if (!string_content_pointer_is_valid(reader, strg, name) ||
            !string_content_pointer_is_valid(reader, strg, type) ||
            !string_content_pointer_is_valid(reader, strg, file)) return false;
        ++stats->presentSounds;
        if ((flags & 1u) != 0u) ++stats->embeddedSounds;
        if ((flags & 2u) != 0u) ++stats->compressedSounds;
    }
    for (uint32_t i = 0u; i < audioCount; ++i) {
        BinaryReader_seek(reader, audo->payloadOffset + 4u + i * 4u);
        const uint32_t entry = BinaryReader_readUint32(reader);
        if (entry == 0u) continue;
        uint32_t offset = 0u, size = 0u;
        if (!bounded_audio_blob(reader, (uint32_t)reader->fileSize,
                                audo, entry, &offset, &size) ||
            UINT32_MAX - stats->totalAudioBytes < size) return false;
        ++stats->presentAudioEntries;
        stats->totalAudioBytes += size;
        if (size > stats->largestAudioEntry) stats->largestAudioEntry = size;
        BinaryReader_seek(reader, offset);
        for (uint32_t j = 0u; j < size; ++j) {
            const uint8_t value = BinaryReader_readUint8(reader);
            stats->fnv1a ^= value;
            stats->fnv1a *= 16777619u;
        }
    }
    return true;
}

static bool bounded_pointer_list(BinaryReader *reader,
                                 const DemonDataWinChunk *chunk,
                                 uint32_t offset, uint32_t *count) {
    const uint64_t start = chunk->payloadOffset;
    const uint64_t end = start + chunk->size;
    if ((uint64_t)offset < start || (uint64_t)offset + 4u > end)
        return false;
    BinaryReader_seek(reader, offset);
    const uint32_t entries = BinaryReader_readUint32(reader);
    if ((uint64_t)entries * 4u > end - ((uint64_t)offset + 4u))
        return false;
    for (uint32_t i = 0u; i < entries; ++i) {
        const uint32_t pointer = BinaryReader_readUint32(reader);
        if (pointer != 0u && ((uint64_t)pointer < start ||
                             (uint64_t)pointer >= end)) return false;
    }
    *count = entries;
    return true;
}

static uint32_t pointer_list_entry(BinaryReader *reader, uint32_t list,
                                   uint32_t index) {
    BinaryReader_seek(reader, (size_t)list + 4u + (size_t)index * 4u);
    return BinaryReader_readUint32(reader);
}

static bool read_bool32(BinaryReader *reader) {
    return BinaryReader_readUint32(reader) <= 1u;
}

bool DemonDataWinIndex_inspectObjects(BinaryReader *reader,
                                      const DemonDataWinIndex *index,
                                      const DemonDataWinGen8Summary *gen8,
                                      uint32_t codeCount,
                                      DemonDataWinObjectStats *stats) {
    const DemonDataWinChunk *objt = DemonDataWinIndex_find(index,
        DATAWIN_TAG('O', 'B', 'J', 'T'));
    const DemonDataWinChunk *strg = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    uint32_t count = 0u;
    const bool pointerCountOk = DemonDataWinIndex_pointerCount(reader, index,
        DATAWIN_TAG('O', 'B', 'J', 'T'), &count);
    if (reader == NULL || gen8 == NULL || stats == NULL || objt == NULL ||
        strg == NULL || !pointerCountOk) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=entry-guard reader-null=",
                          reader == NULL ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT entry-guard gen8-null=",
                          gen8 == NULL ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT entry-guard objt-null=",
                          objt == NULL ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT entry-guard strg-null=",
                          strg == NULL ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT entry-guard pointer-count-ok=",
                          pointerCountOk ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT entry-guard count=", (uint64_t)count);
#endif
        return false;
    }

    memset(stats, 0, sizeof(*stats));
    stats->count = count;
    const uint64_t chunkEnd = (uint64_t)objt->payloadOffset + objt->size;
    const bool managedField = gen8->major > 2022u ||
        (gen8->major == 2022u && gen8->minor >= 5u);
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 major=", (uint64_t)gen8->major);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 minor=", (uint64_t)gen8->minor);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 wadVersion=", (uint64_t)gen8->wadVersion);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 managedField=", managedField ? 1u : 0u);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 objt-payload-offset=", (uint64_t)objt->payloadOffset);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 objt-size=", (uint64_t)objt->size);
    datawin_debug_u64("BUTTERSCOTCH_DEBUG_GEN8 object-count=", (uint64_t)count);
#endif

    for (uint32_t objectIndex = 0u; objectIndex < count; ++objectIndex) {
        const uint32_t object = pointer_list_entry(reader,
            objt->payloadOffset, objectIndex);
        if (object == 0u) continue;
        const uint32_t fixedBytes = (managedField ? 4u : 0u) +
            (gen8->wadVersion == 17u ? 4u : 0u) +
            (gen8->wadVersion <= 8u ? 68u : 80u);
        if ((uint64_t)object + fixedBytes > chunkEnd) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=fixed-bytes-bound object-index=",
                              (uint64_t)objectIndex);
#endif
            return false;
        }
        BinaryReader_seek(reader, object);
        const uint32_t name = BinaryReader_readUint32(reader);
        const int32_t spriteId = BinaryReader_readInt32(reader);
        const uint32_t visibleRaw = BinaryReader_readUint32(reader);
        (void)spriteId;
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        if (objectIndex == 0u) {
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 object-offset=", (uint64_t)object);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 name=", (uint64_t)name);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 sprite-id=", (uint64_t)(uint32_t)spriteId);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 visible-raw=", (uint64_t)visibleRaw);
        }
#endif
        if (visibleRaw > 1u) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=visible object-index=", (uint64_t)objectIndex);
#endif
            return false;
        }
        uint32_t managedRaw = 0u;
        if (managedField) {
            managedRaw = BinaryReader_readUint32(reader);
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            if (objectIndex == 0u)
                datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 managed-raw=", (uint64_t)managedRaw);
#endif
            if (managedRaw > 1u) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=managed object-index=", (uint64_t)objectIndex);
#endif
                return false;
            }
        }
        const uint32_t solidRaw = BinaryReader_readUint32(reader);
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        if (objectIndex == 0u)
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 solid-raw=", (uint64_t)solidRaw);
#endif
        if (solidRaw > 1u) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=solid object-index=", (uint64_t)objectIndex);
#endif
            return false;
        }
        const int32_t depth = BinaryReader_readInt32(reader);
        const uint32_t persistentRaw = BinaryReader_readUint32(reader);
        (void)depth;
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        if (objectIndex == 0u) {
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 depth=", (uint64_t)(uint32_t)depth);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 persistent-raw=", (uint64_t)persistentRaw);
        }
#endif
        if (persistentRaw > 1u) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=persistent object-index=", (uint64_t)objectIndex);
#endif
            return false;
        }
        /* Empirically discovered against the real DELTARUNE Chapter 1
           data.win (GEN8 major=2 minor=0, bytecode/wadVersion=17): there is
           an undocumented extra 4-byte bool-shaped field here, between
           Persistent and ParentId, that UndertaleModLib's current source
           (and its full git history) does not describe at all -- see the
           GameObject serialization at
           github.com/UnderminersTeam/UndertaleModTool. Confirmed by hex
           dumping three real object records: skipping this one field is
           what makes ParentId's well-known -100 "no parent" sentinel and
           TextureMaskId's well-known -1 "no mask" sentinel land exactly
           where they should, and makes the following event-type pointer
           list's entry count come out to exactly 15 (the real number of
           GameMaker event-type buckets) every time. Gated narrowly on
           wadVersion==17 (the only version verified so far) rather than a
           range, since bytecode 17 is documented as the edge of
           UndertaleModTool's own "supported" window -- widen this once a
           second real game confirms the actual boundary. */
        if (gen8->wadVersion == 17u) BinaryReader_skip(reader, 4u);
        const int32_t parent = BinaryReader_readInt32(reader);
        const int32_t textureMask = BinaryReader_readInt32(reader);
        const uint32_t usesPhysicsRaw = BinaryReader_readUint32(reader);
        const uint32_t isSensorRaw = BinaryReader_readUint32(reader);
        (void)textureMask;
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        if (objectIndex == 0u) {
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 parent=", (uint64_t)(uint32_t)parent);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 texture-mask=", (uint64_t)(uint32_t)textureMask);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 uses-physics-raw=", (uint64_t)usesPhysicsRaw);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJ0 is-sensor-raw=", (uint64_t)isSensorRaw);
        }
#endif
        if (usesPhysicsRaw > 1u || isSensorRaw > 1u) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=solid2-pair object-index=", (uint64_t)objectIndex);
#endif
            return false;
        }
        (void)BinaryReader_readUint32(reader); /* collision shape */
        BinaryReader_skip(reader, 20u); /* density through angular damping */
        const int32_t vertexCount = BinaryReader_readInt32(reader);
        if (vertexCount < 0 || (uint64_t)(uint32_t)vertexCount * 8u >
            chunkEnd - BinaryReader_getPosition(reader)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=vertex-count object-index=", (uint64_t)objectIndex);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT vertex-count-value=", (uint64_t)(uint32_t)vertexCount);
#endif
            return false;
        }
        if (gen8->wadVersion > 8u) {
            BinaryReader_skip(reader, 4u); /* friction */
            if (!read_bool32(reader) || !read_bool32(reader)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=friction-pair object-index=", (uint64_t)objectIndex);
#endif
                return false;
            }
        }
        if (!string_content_pointer_is_valid(reader, strg, name)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=name-string object-index=", (uint64_t)objectIndex);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT name-value=", (uint64_t)name);
#endif
            return false;
        }
        if (parent >= 0) {
            if ((uint32_t)parent >= count) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=parent-range object-index=", (uint64_t)objectIndex);
#endif
                return false;
            }
            ++stats->inheritanceEdges;
        }
        if (UINT32_MAX - stats->physicsVertexCount < (uint32_t)vertexCount)
            return false;
        stats->physicsVertexCount += (uint32_t)vertexCount;
        BinaryReader_skip(reader, (size_t)(uint32_t)vertexCount * 8u);

        const uint32_t eventTypesOffset =
            (uint32_t)BinaryReader_getPosition(reader);
        uint32_t eventTypes = 0u;
        if (!bounded_pointer_list(reader, objt, eventTypesOffset,
                                  &eventTypes) || eventTypes > 15u ||
            UINT32_MAX - stats->eventTypeLists < eventTypes) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=event-types object-index=", (uint64_t)objectIndex);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT event-types-value=", (uint64_t)eventTypes);
#endif
            return false;
        }
        stats->eventTypeLists += eventTypes;

        for (uint32_t eventType = 0u; eventType < eventTypes; ++eventType) {
            const uint32_t eventList = pointer_list_entry(reader,
                eventTypesOffset, eventType);
            uint32_t events = 0u;
            if (eventList == 0u || !bounded_pointer_list(reader, objt,
                    eventList, &events) ||
                UINT32_MAX - stats->eventCount < events) return false;
            stats->eventCount += events;

            for (uint32_t eventIndex = 0u; eventIndex < events;
                 ++eventIndex) {
                const uint32_t event = pointer_list_entry(reader, eventList,
                    eventIndex);
                if (event == 0u || (uint64_t)event + 8u > chunkEnd) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                    datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=event-bound object-index=", (uint64_t)objectIndex);
#endif
                    return false;
                }
                BinaryReader_seek(reader, event);
                (void)BinaryReader_readUint32(reader); /* event subtype */
                const uint32_t actionsOffset =
                    (uint32_t)BinaryReader_getPosition(reader);
                uint32_t actions = 0u;
                if (!bounded_pointer_list(reader, objt, actionsOffset,
                        &actions) || UINT32_MAX - stats->actionCount < actions) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                    datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=actions-list object-index=", (uint64_t)objectIndex);
#endif
                    return false;
                }
                stats->actionCount += actions;

                for (uint32_t actionIndex = 0u; actionIndex < actions;
                     ++actionIndex) {
                    const uint32_t action = pointer_list_entry(reader,
                        actionsOffset, actionIndex);
                    if (action == 0u || (uint64_t)action + 56u > chunkEnd) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=action-bound object-index=", (uint64_t)objectIndex);
#endif
                        return false;
                    }
                    BinaryReader_seek(reader, action + 12u);
                    if (!read_bool32(reader) || !read_bool32(reader) ||
                        !read_bool32(reader)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=action-bool-triplet object-index=", (uint64_t)objectIndex);
#endif
                        return false;
                    }
                    (void)BinaryReader_readUint32(reader); /* execution type */
                    const uint32_t actionName = BinaryReader_readUint32(reader);
                    const int32_t codeId = BinaryReader_readInt32(reader);
                    (void)BinaryReader_readUint32(reader); /* arguments */
                    (void)BinaryReader_readInt32(reader); /* target */
                    if (!read_bool32(reader) || !read_bool32(reader)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=action-bool-pair object-index=", (uint64_t)objectIndex);
#endif
                        return false;
                    }
                    (void)BinaryReader_readUint32(reader); /* reserved */
                    if (!string_content_pointer_is_valid(reader, strg,
                            actionName) || codeId < -1 ||
                        (codeId >= 0 && (uint32_t)codeId >= codeCount)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT fail=action-name-or-code object-index=", (uint64_t)objectIndex);
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT action-name-value=", (uint64_t)actionName);
                        datawin_debug_u64("BUTTERSCOTCH_DEBUG_OBJT action-codeid-value=", (uint64_t)(uint32_t)codeId);
#endif
                        return false;
                    }
                    if (codeId >= 0) ++stats->codeActionCount;
                }
            }
        }
        ++stats->presentCount;
    }
    return true;
}

static bool code_reference_is_valid(int32_t codeId, uint32_t codeCount) {
    return codeId == -1 || (codeId >= 0 && (uint32_t)codeId < codeCount);
}

bool DemonDataWinIndex_inspectRooms(BinaryReader *reader,
                                    const DemonDataWinIndex *index,
                                    const DemonDataWinGen8Summary *gen8,
                                    uint32_t objectCount,
                                    uint32_t codeCount,
                                    DemonDataWinRoomStats *stats) {
    const DemonDataWinChunk *roomChunk = DemonDataWinIndex_find(index,
        DATAWIN_TAG('R', 'O', 'O', 'M'));
    const DemonDataWinChunk *strg = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'T', 'R', 'G'));
    uint32_t count = 0u;
    if (reader == NULL || gen8 == NULL || stats == NULL ||
        roomChunk == NULL || strg == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('R', 'O', 'O', 'M'), &count)) return false;

    memset(stats, 0, sizeof(*stats));
    stats->count = count;
    const uint64_t chunkEnd = (uint64_t)roomChunk->payloadOffset +
        roomChunk->size;
    const bool imageFields = gen8->major > 2u ||
        (gen8->major == 2u && (gen8->minor > 2u ||
         (gen8->minor == 2u && (gen8->release > 2u ||
          (gen8->release == 2u && gen8->build >= 302u)))));

    for (uint32_t roomIndex = 0u; roomIndex < count; ++roomIndex) {
        const uint32_t room = pointer_list_entry(reader,
            roomChunk->payloadOffset, roomIndex);
        if (room == 0u) continue;
        if ((uint64_t)room + 88u > chunkEnd) return false;
        BinaryReader_seek(reader, room);
        const uint32_t name = BinaryReader_readUint32(reader);
        const uint32_t caption = BinaryReader_readUint32(reader);
        const uint32_t width = BinaryReader_readUint32(reader);
        const uint32_t height = BinaryReader_readUint32(reader);
        (void)BinaryReader_readUint32(reader); /* game speed */
        if (!read_bool32(reader)) return false; /* persistent */
        (void)BinaryReader_readUint32(reader); /* background colour */
        if (!read_bool32(reader)) return false; /* draw background */
        const int32_t roomCreationCode = BinaryReader_readInt32(reader);
        (void)BinaryReader_readUint32(reader); /* flags */
        const uint32_t backgrounds = BinaryReader_readUint32(reader);
        const uint32_t views = BinaryReader_readUint32(reader);
        const uint32_t instances = BinaryReader_readUint32(reader);
        const uint32_t tiles = BinaryReader_readUint32(reader);
        if (!read_bool32(reader)) return false; /* physics world */
        BinaryReader_skip(reader, 28u); /* bounds, gravity, metres/pixel */

        if (!string_content_pointer_is_valid(reader, strg, name) ||
            !string_content_pointer_is_valid(reader, strg, caption) ||
            width == 0u || height == 0u || width > 65535u ||
            height > 65535u ||
            !code_reference_is_valid(roomCreationCode, codeCount))
            return false;
        if (roomCreationCode >= 0) ++stats->creationCodeRefs;
        if (width > stats->maxWidth) stats->maxWidth = width;
        if (height > stats->maxHeight) stats->maxHeight = height;

        uint32_t backgroundCount = 0u;
        if (!bounded_pointer_list(reader, roomChunk, backgrounds,
                &backgroundCount) || backgroundCount > 8u ||
            UINT32_MAX - stats->backgroundSlots < backgroundCount)
            return false;
        stats->backgroundSlots += backgroundCount;
        for (uint32_t i = 0u; i < backgroundCount; ++i) {
            const uint32_t record = pointer_list_entry(reader, backgrounds, i);
            if (record == 0u || (uint64_t)record + 40u > chunkEnd)
                return false;
            BinaryReader_seek(reader, record);
            const uint32_t enabled = BinaryReader_readUint32(reader);
            const uint32_t foreground = BinaryReader_readUint32(reader);
            BinaryReader_skip(reader, 28u);
            const uint32_t stretch = BinaryReader_readUint32(reader);
            if (enabled > 1u || foreground > 1u || stretch > 1u)
                return false;
            stats->enabledBackgrounds += enabled;
        }

        uint32_t viewCount = 0u;
        if (!bounded_pointer_list(reader, roomChunk, views, &viewCount) ||
            viewCount > 8u || UINT32_MAX - stats->viewSlots < viewCount)
            return false;
        stats->viewSlots += viewCount;
        for (uint32_t i = 0u; i < viewCount; ++i) {
            const uint32_t record = pointer_list_entry(reader, views, i);
            if (record == 0u || (uint64_t)record + 56u > chunkEnd)
                return false;
            BinaryReader_seek(reader, record);
            const uint32_t enabled = BinaryReader_readUint32(reader);
            BinaryReader_skip(reader, 48u);
            const int32_t objectId = BinaryReader_readInt32(reader);
            if (enabled > 1u || (objectId < -1) ||
                (objectId >= 0 && (uint32_t)objectId >= objectCount))
                return false;
            stats->enabledViews += enabled;
        }

        uint32_t instanceCount = 0u;
        if (!bounded_pointer_list(reader, roomChunk, instances,
                &instanceCount) ||
            UINT32_MAX - stats->instanceCount < instanceCount) return false;
        stats->instanceCount += instanceCount;
        const uint32_t instanceSize = 36u +
            (gen8->wadVersion >= 16u ? 4u : 0u) +
            (imageFields ? 8u : 0u);
        for (uint32_t i = 0u; i < instanceCount; ++i) {
            const uint32_t record = pointer_list_entry(reader, instances, i);
            if (record == 0u || (uint64_t)record + instanceSize > chunkEnd)
                return false;
            BinaryReader_seek(reader, record + 8u);
            const int32_t objectId = BinaryReader_readInt32(reader);
            (void)BinaryReader_readUint32(reader); /* instance id */
            const int32_t creationCode = BinaryReader_readInt32(reader);
            BinaryReader_skip(reader, 8u); /* scale */
            if (imageFields) BinaryReader_skip(reader, 8u);
            BinaryReader_skip(reader, 8u); /* colour and rotation */
            int32_t preCreateCode = -1;
            if (gen8->wadVersion >= 16u)
                preCreateCode = BinaryReader_readInt32(reader);
            if (objectId < -1 ||
                (objectId >= 0 && (uint32_t)objectId >= objectCount) ||
                !code_reference_is_valid(creationCode, codeCount) ||
                !code_reference_is_valid(preCreateCode, codeCount))
                return false;
            if (objectId >= 0) ++stats->boundInstanceCount;
            if (creationCode >= 0) ++stats->creationCodeRefs;
            if (preCreateCode >= 0) ++stats->creationCodeRefs;
        }

        uint32_t tileCount = 0u;
        if (!bounded_pointer_list(reader, roomChunk, tiles, &tileCount) ||
            UINT32_MAX - stats->tileCount < tileCount) return false;
        stats->tileCount += tileCount;
        for (uint32_t i = 0u; i < tileCount; ++i) {
            const uint32_t record = pointer_list_entry(reader, tiles, i);
            if (record == 0u || (uint64_t)record + 56u > chunkEnd)
                return false;
        }
        ++stats->presentCount;
    }
    return true;
}

static uint32_t read_be32(BinaryReader *reader) {
    const uint32_t a = BinaryReader_readUint8(reader);
    const uint32_t b = BinaryReader_readUint8(reader);
    const uint32_t c = BinaryReader_readUint8(reader);
    const uint32_t d = BinaryReader_readUint8(reader);
    return (a << 24u) | (b << 16u) | (c << 8u) | d;
}

/* GameMaker's "compressed texture group" format: a 12-byte header
   (4-byte magic, uint16 width, uint16 height, uint32 decompressed size)
   directly followed by a raw BZip2 stream ("BZh" + block-size digit).
   Confirmed empirically against the real DELTARUNE Chapter 1 data.win --
   its TXTR blobs are not PNGs at all, unlike the small upstream test
   fixture this runner was originally built against. This validates the
   header and the whole blob's bounds/checksum for compatibility-probe
   purposes; it deliberately does not decompress the BZip2 payload --
   actually rendering these textures is separate, later work. */
static bool parse_compressed_texture(BinaryReader *reader, uint32_t start,
                                     uint32_t limit, uint32_t *width,
                                     uint32_t *height, uint32_t *bytes,
                                     uint32_t *fnv) {
    if (limit - start < 15u) return false;
    BinaryReader_seek(reader, start);
    BinaryReader_skip(reader, 4u); /* format-specific magic */
    *width = BinaryReader_readUint16(reader);
    *height = BinaryReader_readUint16(reader);
    (void)BinaryReader_readUint32(reader); /* decompressed size */
    if (*width == 0u || *height == 0u || *width > 16384u ||
        *height > 16384u) return false;
    if (BinaryReader_readUint8(reader) != 'B' ||
        BinaryReader_readUint8(reader) != 'Z' ||
        BinaryReader_readUint8(reader) != 'h') return false;
    BinaryReader_seek(reader, start);
    for (uint32_t i = start; i < limit; ++i) {
        const uint8_t value = BinaryReader_readUint8(reader);
        *fnv = (*fnv ^ value) * 16777619u;
    }
    *bytes = limit - start;
    return true;
}

static bool parse_png(BinaryReader *reader, uint32_t start, uint32_t limit,
                      uint32_t *width, uint32_t *height, uint32_t *bytes,
                      uint32_t *fnv) {
    static const uint8_t signature[8] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au
    };
    if (limit - start < 33u) return false;
    BinaryReader_seek(reader, start);
    for (uint32_t i = 0u; i < 8u; ++i) {
        const uint8_t value = BinaryReader_readUint8(reader);
        if (value != signature[i]) return false;
        *fnv = (*fnv ^ value) * 16777619u;
    }

    bool sawHeader = false;
    while (BinaryReader_getPosition(reader) < limit) {
        const uint32_t chunkStart = (uint32_t)BinaryReader_getPosition(reader);
        if (limit - chunkStart < 12u) return false;
        const uint32_t length = read_be32(reader);
        const uint32_t type = read_be32(reader);
        if ((uint64_t)length + 12u > (uint64_t)limit - chunkStart)
            return false;
        if (!sawHeader) {
            if (type != DATAWIN_TAG('R', 'D', 'H', 'I') || length != 13u)
                return false;
            *width = read_be32(reader);
            *height = read_be32(reader);
            if (*width == 0u || *height == 0u || *width > 16384u ||
                *height > 16384u) return false;
            BinaryReader_seek(reader, (size_t)chunkStart + 8u);
            sawHeader = true;
        }
        BinaryReader_seek(reader, (size_t)chunkStart + 12u + length);
        if (type == DATAWIN_TAG('D', 'N', 'E', 'I')) {
            const uint32_t end = (uint32_t)BinaryReader_getPosition(reader);
            BinaryReader_seek(reader, start);
            for (uint32_t i = start; i < end; ++i) {
                const uint8_t value = BinaryReader_readUint8(reader);
                if (i >= start + 8u) *fnv = (*fnv ^ value) * 16777619u;
            }
            *bytes = end - start;
            return sawHeader && length == 0u;
        }
    }
    return false;
}

static bool txtr_metadata(BinaryReader *reader,
                          const DemonDataWinChunk *txtr,
                          const DemonDataWinGen8Summary *gen8,
                          uint32_t index, uint32_t *blobOffset) {
    const uint32_t entry = pointer_list_entry(reader, txtr->payloadOffset,
                                              index);
    if (entry == 0u) {
        *blobOffset = 0u;
        return true;
    }
    const bool generatedMips = gen8->major >= 2u;
    /* Empirically confirmed against the real DELTARUNE Chapter 1
       data.win (GEN8 major=2 minor=0, wadVersion=17): both the
       "block size" and "dimensions" TXTR metadata fields are present,
       even though GEN8's major/minor here use GameMaker's old plain
       integer version scheme (e.g. "2.0"), not the year.month scheme
       ("2022.5") these thresholds were written against. Confirmed via
       hex dump: with both fields counted in, the width/height read out
       exactly as 64x64 and the resulting blob offset lands inside the
       TXTR chunk's real bounds; without them, the blob offset comes out
       as a tiny in-page value (1207) that's nowhere near valid. Gated
       narrowly on wadVersion==17 for now -- see the matching comment in
       DemonDataWinIndex_inspectObjects. */
    const bool blockSize = gen8->major > 2022u ||
        (gen8->major == 2022u && gen8->minor >= 3u) ||
        gen8->wadVersion == 17u;
    const bool dimensions = gen8->major > 2022u ||
        (gen8->major == 2022u && gen8->minor >= 9u) ||
        gen8->wadVersion == 17u;
    const uint32_t size = 8u + (generatedMips ? 4u : 0u) +
        (blockSize ? 4u : 0u) + (dimensions ? 12u : 0u);
    const uint64_t end = (uint64_t)txtr->payloadOffset + txtr->size;
    if ((uint64_t)entry + size > end) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META fail=bound index=", (uint64_t)index);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META entry=", (uint64_t)entry);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META size=", (uint64_t)size);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META end=", end);
#endif
        return false;
    }
    BinaryReader_seek(reader, entry);
    (void)BinaryReader_readUint32(reader); /* scaled */
    if (generatedMips) (void)BinaryReader_readUint32(reader);
    if (blockSize) (void)BinaryReader_readUint32(reader);
    if (dimensions) BinaryReader_skip(reader, 12u);
    *blobOffset = BinaryReader_readUint32(reader);
    const bool ok = *blobOffset == 0u ||
        ((uint64_t)*blobOffset >= txtr->payloadOffset &&
         (uint64_t)*blobOffset < end);
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
    if (!ok) {
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META fail=blob-range index=", (uint64_t)index);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META blob-offset=", (uint64_t)*blobOffset);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META payload-offset=", (uint64_t)txtr->payloadOffset);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META end=", end);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META generated-mips=", generatedMips ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META block-size=", blockSize ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META dimensions=", dimensions ? 1u : 0u);
        datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR_META entry=", (uint64_t)entry);
    }
#endif
    return ok;
}

/* Dispatches to whichever texture-blob encoding is actually present:
   a real PNG (the upstream test fixture, and possibly older/uncompressed
   texture groups), or GameMaker's BZip2-compressed texture group format
   (real DELTARUNE Chapter 1 data). Peeks the signature rather than
   trying one and falling back on failure, so a genuine bounds/format bug
   in either parser still surfaces as a real failure instead of silently
   trying the other path. */
static bool parse_texture_blob(BinaryReader *reader, uint32_t start,
                               uint32_t limit, uint32_t *width,
                               uint32_t *height, uint32_t *bytes,
                               uint32_t *fnv) {
    if (limit - start < 8u) return false;
    BinaryReader_seek(reader, start);
    const uint8_t first = BinaryReader_readUint8(reader);
    if (first == 0x89u)
        return parse_png(reader, start, limit, width, height, bytes, fnv);
    return parse_compressed_texture(reader, start, limit, width, height,
                                    bytes, fnv);
}

/* Actually decompresses a GameMaker compressed-texture-group blob (see
   parse_compressed_texture's comment for the wire format) into real
   pixel bytes, using the real BZip2 decoder (bzip2_decode.c/h) --
   verified byte-identical to the reference `bunzip2` tool against every
   real texture page in DELTARUNE Chapter 1's data.win, including
   multi-megabyte multi-block streams. The caller owns *out_pixels and
   must demon_port_free it. Uses BinaryReader_readBytes for the bulk
   compressed-data copy rather than a byte-at-a-time loop -- that path
   goes through a single real read (fread or memcpy depending on
   buffered vs. streaming mode) instead of one syscall per byte, unlike
   the audio checksum loop's still-outstanding per-byte pattern. */
bool DemonDataWinIndex_decompressTexture(BinaryReader *reader, uint32_t start,
                                         uint32_t limit, uint8_t **out_pixels,
                                         size_t *out_length, uint32_t *width,
                                         uint32_t *height) {
    if (reader == NULL || out_pixels == NULL || out_length == NULL ||
        width == NULL || height == NULL || limit <= start ||
        limit - start < 15u) return false;
    BinaryReader_seek(reader, start);
    BinaryReader_skip(reader, 4u); /* format-specific magic */
    *width = BinaryReader_readUint16(reader);
    *height = BinaryReader_readUint16(reader);
    const uint32_t decompressedSize = BinaryReader_readUint32(reader);
    if (*width == 0u || *height == 0u || decompressedSize == 0u) return false;

    const uint32_t compressedOffset = start + 12u;
    const uint32_t compressedLength = limit - compressedOffset;
    if (compressedLength < 4u) return false;

    /* Allocate the buffer that OUTLIVES this function (`pixels`) before
       the two purely-transient ones (`compressed`, `scratch`) that get
       freed again below. With a simple coalescing free-list allocator
       (see user/portkit.c), freeing blocks that sit *before* a still-
       live allocation can't merge forward past it -- the freed space
       becomes an unusable island sandwiched between two live
       allocations, even though the arena has plenty of free bytes
       overall. Allocating `pixels` first means `compressed` and
       `scratch` sit *after* it, so freeing them merges forward into
       the untouched tail of the arena into one single free region --
       exactly what DemonDataWinIndex_decodeTexturePixels's subsequent
       16 MiB RGBA allocation needs. This was a real, reproducible
       allocation failure against DELTARUNE Chapter 1's actual texture
       pages (page 8's larger QOI-encoded intermediate size trips it
       while a smaller page doesn't), not a hypothetical one. */
    uint8_t *pixels = (uint8_t *)demon_port_malloc(decompressedSize);
    if (pixels == NULL) return false;
    uint8_t *compressed = (uint8_t *)demon_port_malloc(compressedLength);
    if (compressed == NULL) { demon_port_free(pixels); return false; }
    BinaryReader_seek(reader, compressedOffset);
    BinaryReader_readBytes(reader, compressed, compressedLength);

    if (compressed[0] != 'B' || compressed[1] != 'Z' || compressed[2] != 'h' ||
        compressed[3] < '1' || compressed[3] > '9') {
        demon_port_free(compressed);
        demon_port_free(pixels);
        return false;
    }
    const uint32_t level = (uint32_t)(compressed[3] - '0');
    const size_t scratchSize = BZIP2_SCRATCH_BYTES(level);
    uint8_t *scratch = (uint8_t *)demon_port_malloc(scratchSize);
    if (scratch == NULL) {
        demon_port_free(compressed);
        demon_port_free(pixels);
        return false;
    }

    size_t actualLength = 0u;
    const bool ok = Bzip2_decompress(compressed, compressedLength, pixels,
                                     decompressedSize, &actualLength,
                                     scratch, scratchSize);
    demon_port_free(compressed);
    demon_port_free(scratch);
    if (!ok || actualLength != decompressedSize) {
        demon_port_free(pixels);
        return false;
    }
    *out_pixels = pixels;
    *out_length = actualLength;
    return true;
}

/* Full pipeline: BZip2-decompress a texture-group blob (see
   DemonDataWinIndex_decompressTexture) and then decode GameMaker's
   custom "fioq" QOI-variant pixel format (see qoi_decode.c) into real
   BGRA8 pixels. Verified against every real texture page in DELTARUNE
   Chapter 1's data.win by rendering the decoded pixels to PNG and
   visually confirming genuine, correct game art (not noise) -- see
   docs/butterscotch-games.md. The caller owns *out_pixels and must
   demon_port_free it. */
bool DemonDataWinIndex_decodeTexturePixels(BinaryReader *reader, uint32_t start,
                                           uint32_t limit, uint8_t **out_pixels,
                                           size_t *out_length, uint32_t *width,
                                           uint32_t *height) {
    /* Peek the same 12-byte header DemonDataWinIndex_decompressTexture
       reads, purely to size and allocate the final RGBA buffer BEFORE
       calling it -- for the same coalescing-allocator reason explained
       there: the final buffer has to be the FIRST thing allocated among
       everything that outlives this function, so that decompressTexture's
       own (already correctly-ordered) transient buffers free back into
       the tail instead of getting stranded behind this one. */
    if (reader == NULL || out_pixels == NULL || out_length == NULL ||
        width == NULL || height == NULL || limit <= start ||
        limit - start < 15u) return false;
    BinaryReader_seek(reader, start);
    BinaryReader_skip(reader, 4u);
    const uint32_t declaredWidth = (uint32_t)BinaryReader_readUint16(reader);
    const uint32_t declaredHeight = (uint32_t)BinaryReader_readUint16(reader);
    if (declaredWidth == 0u || declaredHeight == 0u) return false;

    const size_t pixelBytes = (size_t)declaredWidth * declaredHeight * 4u;
    uint8_t *pixels = (uint8_t *)demon_port_malloc(pixelBytes);
    if (pixels == NULL) return false;

    uint8_t *compressedPixelFormat = NULL;
    size_t compressedPixelFormatLength = 0u;
    uint32_t confirmedWidth = 0u, confirmedHeight = 0u;
    if (!DemonDataWinIndex_decompressTexture(reader, start, limit,
            &compressedPixelFormat, &compressedPixelFormatLength,
            &confirmedWidth, &confirmedHeight) ||
        confirmedWidth != declaredWidth || confirmedHeight != declaredHeight) {
        demon_port_free(pixels);
        return false;
    }

    uint32_t decodedWidth = 0u, decodedHeight = 0u;
    const bool ok = Qoi_decode(compressedPixelFormat, compressedPixelFormatLength,
                               pixels, pixelBytes, &decodedWidth, &decodedHeight);
    demon_port_free(compressedPixelFormat);
    if (!ok || decodedWidth != declaredWidth || decodedHeight != declaredHeight) {
        demon_port_free(pixels);
        return false;
    }
    *out_pixels = pixels;
    *out_length = pixelBytes;
    *width = decodedWidth;
    *height = decodedHeight;
    return true;
}

bool DemonDataWinIndex_textureBlob(BinaryReader *reader,
                                   const DemonDataWinIndex *index,
                                   const DemonDataWinGen8Summary *gen8,
                                   uint32_t textureIndex,
                                   uint32_t *blobOffset,
                                   uint32_t *blobSize) {
    const DemonDataWinChunk *txtr = DemonDataWinIndex_find(index,
        DATAWIN_TAG('T', 'X', 'T', 'R'));
    uint32_t textureCount = 0u;
    if (reader == NULL || gen8 == NULL || blobOffset == NULL ||
        blobSize == NULL || txtr == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('T', 'X', 'T', 'R'), &textureCount) ||
        textureIndex >= textureCount) return false;

    uint32_t start = 0u;
    uint32_t end = txtr->payloadOffset + txtr->size;
    if (!txtr_metadata(reader, txtr, gen8, textureIndex, &start) ||
        start == 0u) return false;
    for (uint32_t next = textureIndex + 1u; next < textureCount; ++next) {
        uint32_t candidate = 0u;
        if (!txtr_metadata(reader, txtr, gen8, next, &candidate)) return false;
        if (candidate != 0u) {
            end = candidate;
            break;
        }
    }
    uint32_t width = 0u, height = 0u, bytes = 0u, hash = 2166136261u;
    if (end <= start || !parse_texture_blob(reader, start, end, &width,
                                            &height, &bytes, &hash)) return false;
    *blobOffset = start;
    *blobSize = bytes;
    return true;
}

bool DemonDataWinIndex_inspectTextures(BinaryReader *reader,
                                       const DemonDataWinIndex *index,
                                       const DemonDataWinGen8Summary *gen8,
                                       DemonDataWinTextureStats *stats) {
    const DemonDataWinChunk *tpag = DemonDataWinIndex_find(index,
        DATAWIN_TAG('T', 'P', 'A', 'G'));
    const DemonDataWinChunk *txtr = DemonDataWinIndex_find(index,
        DATAWIN_TAG('T', 'X', 'T', 'R'));
    uint32_t itemCount = 0u;
    uint32_t textureCount = 0u;
    if (reader == NULL || gen8 == NULL || stats == NULL || tpag == NULL ||
        txtr == NULL || !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('T', 'P', 'A', 'G'), &itemCount) ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('T', 'X', 'T', 'R'), &textureCount)) return false;

    memset(stats, 0, sizeof(*stats));
    stats->pageItemCount = itemCount;
    stats->textureCount = textureCount;
    stats->fnv1a = 2166136261u;
    const uint32_t txtrEnd = txtr->payloadOffset + txtr->size;

    for (uint32_t texture = 0u; texture < textureCount; ++texture) {
        uint32_t blob = 0u;
        uint32_t nextBlob = txtrEnd;
        if (!txtr_metadata(reader, txtr, gen8, texture, &blob)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR fail=metadata texture=", (uint64_t)texture);
#endif
            return false;
        }
        if (texture + 1u < textureCount &&
            !txtr_metadata(reader, txtr, gen8, texture + 1u, &nextBlob)) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR fail=next-metadata texture=", (uint64_t)texture);
#endif
            return false;
        }
        if (blob == 0u) continue;
        if (nextBlob == 0u) nextBlob = txtrEnd;
        if (nextBlob <= blob) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR fail=blob-order texture=", (uint64_t)texture);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR blob=", (uint64_t)blob);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR next-blob=", (uint64_t)nextBlob);
#endif
            return false;
        }
        uint32_t width = 0u, height = 0u, bytes = 0u;
        if (!parse_texture_blob(reader, blob, nextBlob, &width, &height,
                                &bytes, &stats->fnv1a) ||
            UINT32_MAX - stats->totalPngBytes < bytes) {
#ifdef BUTTERSCOTCH_DATAWIN_DEBUG
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR fail=parse-png texture=", (uint64_t)texture);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR blob=", (uint64_t)blob);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR next-blob=", (uint64_t)nextBlob);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR width=", (uint64_t)width);
            datawin_debug_u64("BUTTERSCOTCH_DEBUG_TXTR height=", (uint64_t)height);
#endif
            return false;
        }
        ++stats->embeddedTextures;
        stats->totalPngBytes += bytes;
        if (width > stats->maxTextureWidth) stats->maxTextureWidth = width;
        if (height > stats->maxTextureHeight) stats->maxTextureHeight = height;
    }

    const uint64_t tpagEnd = (uint64_t)tpag->payloadOffset + tpag->size;
    for (uint32_t item = 0u; item < itemCount; ++item) {
        const uint32_t record = pointer_list_entry(reader, tpag->payloadOffset,
                                                   item);
        if (record == 0u) continue;
        if ((uint64_t)record + 22u > tpagEnd) return false;
        BinaryReader_seek(reader, record);
        const uint32_t sourceX = BinaryReader_readUint16(reader);
        const uint32_t sourceY = BinaryReader_readUint16(reader);
        const uint32_t sourceWidth = BinaryReader_readUint16(reader);
        const uint32_t sourceHeight = BinaryReader_readUint16(reader);
        (void)BinaryReader_readUint16(reader); /* target x */
        (void)BinaryReader_readUint16(reader); /* target y */
        const uint32_t targetWidth = BinaryReader_readUint16(reader);
        const uint32_t targetHeight = BinaryReader_readUint16(reader);
        const uint32_t boundingWidth = BinaryReader_readUint16(reader);
        const uint32_t boundingHeight = BinaryReader_readUint16(reader);
        const int16_t page = BinaryReader_readInt16(reader);
        if (sourceWidth == 0u || sourceHeight == 0u || targetWidth == 0u ||
            targetHeight == 0u || boundingWidth == 0u ||
            boundingHeight == 0u || page < 0 ||
            (uint32_t)page >= textureCount ||
            sourceX + sourceWidth > stats->maxTextureWidth ||
            sourceY + sourceHeight > stats->maxTextureHeight ||
            targetWidth > boundingWidth || targetHeight > boundingHeight ||
            UINT32_MAX - stats->totalAtlasPixels <
                sourceWidth * sourceHeight) return false;
        stats->totalAtlasPixels += sourceWidth * sourceHeight;
        ++stats->presentPageItems;
    }
    return true;
}

bool DemonDataWinIndex_pageItem(BinaryReader *reader,
                                const DemonDataWinIndex *index,
                                uint32_t itemIndex,
                                DemonDataWinPageItem *item) {
    const DemonDataWinChunk *tpag = DemonDataWinIndex_find(index,
        DATAWIN_TAG('T', 'P', 'A', 'G'));
    uint32_t count = 0u;
    if (reader == NULL || item == NULL || tpag == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('T', 'P', 'A', 'G'), &count) || itemIndex >= count)
        return false;
    const uint32_t record = pointer_list_entry(reader, tpag->payloadOffset,
                                               itemIndex);
    if (record == 0u || (uint64_t)record + 22u >
        (uint64_t)tpag->payloadOffset + tpag->size) return false;
    BinaryReader_seek(reader, record);
    item->sourceX = BinaryReader_readUint16(reader);
    item->sourceY = BinaryReader_readUint16(reader);
    item->sourceWidth = BinaryReader_readUint16(reader);
    item->sourceHeight = BinaryReader_readUint16(reader);
    item->targetX = BinaryReader_readUint16(reader);
    item->targetY = BinaryReader_readUint16(reader);
    item->targetWidth = BinaryReader_readUint16(reader);
    item->targetHeight = BinaryReader_readUint16(reader);
    item->boundingWidth = BinaryReader_readUint16(reader);
    item->boundingHeight = BinaryReader_readUint16(reader);
    item->texturePageId = BinaryReader_readInt16(reader);
    return item->sourceWidth != 0u && item->sourceHeight != 0u &&
        item->targetWidth != 0u && item->targetHeight != 0u &&
        item->texturePageId >= 0;
}

bool DemonDataWinIndex_roomScene(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 const DemonDataWinGen8Summary *gen8,
                                 uint32_t roomIndex,
                                 DemonDataWinScene *scene) {
    const DemonDataWinChunk *rooms = DemonDataWinIndex_find(index,
        DATAWIN_TAG('R', 'O', 'O', 'M'));
    const DemonDataWinChunk *objects = DemonDataWinIndex_find(index,
        DATAWIN_TAG('O', 'B', 'J', 'T'));
    const DemonDataWinChunk *sprites = DemonDataWinIndex_find(index,
        DATAWIN_TAG('S', 'P', 'R', 'T'));
    const DemonDataWinChunk *tpag = DemonDataWinIndex_find(index,
        DATAWIN_TAG('T', 'P', 'A', 'G'));
    uint32_t roomCount = 0u, objectCount = 0u, spriteCount = 0u;
    uint32_t pageCount = 0u;
    if (reader == NULL || gen8 == NULL || scene == NULL || rooms == NULL ||
        objects == NULL || sprites == NULL || tpag == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('R', 'O', 'O', 'M'), &roomCount) ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('O', 'B', 'J', 'T'), &objectCount) ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('S', 'P', 'R', 'T'), &spriteCount) ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('T', 'P', 'A', 'G'), &pageCount) ||
        roomIndex >= roomCount) return false;
    memset(scene, 0, sizeof(*scene));
    const uint32_t room = pointer_list_entry(reader, rooms->payloadOffset,
                                             roomIndex);
    if (room == 0u || (uint64_t)room + 52u >
        (uint64_t)rooms->payloadOffset + rooms->size) return false;
    BinaryReader_seek(reader, room + 8u);
    scene->roomWidth = BinaryReader_readUint32(reader);
    scene->roomHeight = BinaryReader_readUint32(reader);
    BinaryReader_seek(reader, room + 48u);
    const uint32_t instanceList = BinaryReader_readUint32(reader);
    uint32_t count = 0u;
    if (!bounded_pointer_list(reader, rooms, instanceList, &count) ||
        count > DEMON_DATAWIN_SCENE_INSTANCE_MAX) return false;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t record = pointer_list_entry(reader, instanceList, i);
        if (record == 0u || (uint64_t)record + 16u >
            (uint64_t)rooms->payloadOffset + rooms->size) return false;
        BinaryReader_seek(reader, record);
        DemonDataWinSceneInstance instance;
        instance.x = BinaryReader_readInt32(reader);
        instance.y = BinaryReader_readInt32(reader);
        instance.objectId = BinaryReader_readInt32(reader);
        instance.instanceId = BinaryReader_readUint32(reader);
        instance.spriteId = -1;
        instance.pageItemId = -1;
        if (instance.objectId >= 0) {
            if ((uint32_t)instance.objectId >= objectCount) return false;
            const uint32_t object = pointer_list_entry(reader,
                objects->payloadOffset, (uint32_t)instance.objectId);
            if (object == 0u || (uint64_t)object + 8u >
                (uint64_t)objects->payloadOffset + objects->size) return false;
            BinaryReader_seek(reader, object + 4u);
            instance.spriteId = BinaryReader_readInt32(reader);
            if (instance.spriteId >= 0) {
                if ((uint32_t)instance.spriteId >= spriteCount) return false;
                const uint32_t sprite = pointer_list_entry(reader,
                    sprites->payloadOffset, (uint32_t)instance.spriteId);
                /* Empirically discovered against real DELTARUNE Chapter 1
                   sprites (bytecode 17): the texture-count/frame-pointer
                   list sits at sprite+84/+88, not +56/+60 as the fixture's
                   own tiny synthetic sprite record uses (wadVersion 16).
                   Confirmed across 8 real sprites -- every one has a
                   small, plausible frame count (1-9) at +84 followed by
                   exactly that many real, in-bounds TPAG pointers
                   starting at +88; at +56/+60 the values were nonsense
                   (a constant tiny "pageRecord" that never matched any
                   real TPAG entry). The real GMS2-era SPRT record has
                   additional fields (bbox, playback/origin data) before
                   the frame list that the smaller offset didn't account
                   for. Gated narrowly on wadVersion==17 like the other
                   bytecode-17 fixes in this file -- widen once a second
                   real game confirms the actual boundary. */
                const uint32_t frameListOffset =
                    gen8->wadVersion == 17u ? 84u : 56u;
                if (sprite == 0u || (uint64_t)sprite + frameListOffset + 8u >
                    (uint64_t)sprites->payloadOffset + sprites->size) return false;
                BinaryReader_seek(reader, sprite + frameListOffset);
                const int32_t textureCount = BinaryReader_readInt32(reader);
                if (textureCount <= 0) return false;
                const uint32_t pageRecord = BinaryReader_readUint32(reader);
                for (uint32_t page = 0u; page < pageCount; ++page) {
                    if (pointer_list_entry(reader, tpag->payloadOffset, page) ==
                        pageRecord) {
                        instance.pageItemId = (int32_t)page;
                        break;
                    }
                }
                if (instance.pageItemId < 0) return false;
            }
        }
        scene->instances[scene->instanceCount++] = instance;
    }
    return scene->roomWidth != 0u && scene->roomHeight != 0u;
}

bool DemonDataWinIndex_eventCode(BinaryReader *reader,
                                 const DemonDataWinIndex *index,
                                 const DemonDataWinGen8Summary *gen8,
                                 uint32_t objectId, uint32_t eventType,
                                 uint32_t eventSubtype, uint32_t *codeId) {
    const DemonDataWinChunk *objt = DemonDataWinIndex_find(index,
        DATAWIN_TAG('O', 'B', 'J', 'T'));
    uint32_t objectCount = 0u;
    if (reader == NULL || gen8 == NULL || codeId == NULL || objt == NULL ||
        !DemonDataWinIndex_pointerCount(reader, index,
            DATAWIN_TAG('O', 'B', 'J', 'T'), &objectCount) ||
        objectId >= objectCount || eventType >= 15u) return false;
    const uint32_t object = pointer_list_entry(reader, objt->payloadOffset,
                                               objectId);
    const bool managedField = gen8->major > 2022u ||
        (gen8->major == 2022u && gen8->minor >= 5u);
    if (object == 0u) return false;
    /* +4 for the undocumented extra field on wadVersion 17 -- see the
       matching comment in DemonDataWinIndex_inspectObjects. */
    BinaryReader_seek(reader, object + 64u + (managedField ? 4u : 0u) +
        (gen8->wadVersion == 17u ? 4u : 0u));
    const int32_t vertexCount = BinaryReader_readInt32(reader);
    if (vertexCount < 0) return false;
    if (gen8->wadVersion > 8u) BinaryReader_skip(reader, 12u);
    BinaryReader_skip(reader, (size_t)(uint32_t)vertexCount * 8u);
    const uint32_t eventTypes = (uint32_t)BinaryReader_getPosition(reader);
    uint32_t typeCount = 0u;
    if (!bounded_pointer_list(reader, objt, eventTypes, &typeCount) ||
        typeCount <= eventType) return false;
    const uint32_t eventList = pointer_list_entry(reader, eventTypes,
                                                  eventType);
    uint32_t eventCount = 0u;
    if (!bounded_pointer_list(reader, objt, eventList, &eventCount))
        return false;
    for (uint32_t i = 0u; i < eventCount; ++i) {
        const uint32_t event = pointer_list_entry(reader, eventList, i);
        if (event == 0u) return false;
        BinaryReader_seek(reader, event);
        const uint32_t subtype = BinaryReader_readUint32(reader);
        const uint32_t actions = (uint32_t)BinaryReader_getPosition(reader);
        uint32_t actionCount = 0u;
        if (!bounded_pointer_list(reader, objt, actions, &actionCount) ||
            actionCount == 0u) return false;
        if (subtype != eventSubtype) continue;
        for (uint32_t actionIndex = 0u; actionIndex < actionCount;
             ++actionIndex) {
            const uint32_t action = pointer_list_entry(reader, actions,
                                                       actionIndex);
            if (action == 0u || (uint64_t)action + 36u >
                (uint64_t)objt->payloadOffset + objt->size) return false;
            BinaryReader_seek(reader, action + 32u);
            const int32_t resolved = BinaryReader_readInt32(reader);
            if (resolved >= 0) {
                *codeId = (uint32_t)resolved;
                return true;
            }
        }
        return false;
    }
    return false;
}

bool DemonDataWinIndex_collisionCode(BinaryReader *reader,
                                     const DemonDataWinIndex *index,
                                     const DemonDataWinGen8Summary *gen8,
                                     uint32_t objectId, uint32_t otherObjectId,
                                     uint32_t *codeId) {
    return DemonDataWinIndex_eventCode(reader, index, gen8, objectId, 4u,
                                       otherObjectId, codeId);
}

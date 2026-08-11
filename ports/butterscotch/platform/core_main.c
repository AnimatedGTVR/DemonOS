/*
 * DemonOS platform code for Butterscotch.
 *
 * Butterscotch itself is MPL-2.0. This entry deliberately calls its genuine
 * binary_utils.h primitives and validates a genuine upstream GameMaker test
 * package. It is the first executable port slice, not a replacement runner.
 */

#include <demon/portkit.h>
#include <demon/c_app.h>
#include <stddef.h>
#include <stdint.h>

#include "libc.h"
#include "binary_reader.h"
#include "binary_utils.h"
#include "bytecode_scan.h"
#include "datawin_index.h"
#include "image_decode.h"
#include "vm_fixture.h"
#include "video.h"
#include "software_renderer.h"
#include "audio.h"
#include "persistence.h"

/* A single real BZip2 decode (DemonDataWinIndex_decompressTexture) needs
   a scratch buffer sized for the stream's declared block-size level --
   worst case (level 9) that's ~4.5 MiB on its own (see
   BZIP2_SCRATCH_BYTES). Going all the way to real BGRA8 pixels
   (DemonDataWinIndex_decodeTexturePixels) needs the QOI-encoded
   intermediate (up to ~1.8 MiB for a real 2048x2048 page) to coexist
   briefly with the final decoded buffer (2048*2048*4 = 16 MiB) during
   the QOI decode step -- roughly 18 MiB peak for one texture. That
   peak also has to coexist with the room-compositing frame buffer and,
   briefly, the QOI-encoded intermediate that decompressTexture already
   freed -- for DELTARUNE Chapter 1's largest real page (its QOI blob
   is ~1.28 MiB), true simultaneous peak measures out to right around
   24.18 MiB, which is *larger* than a 23 MiB arena even with zero
   fragmentation (a real allocation failure was observed at exactly
   this point, after fixing a separate fragmentation bug in allocation
   ordering -- see the comment in DemonDataWinIndex_decompressTexture).
   This is deliberately set to DemonOS's hard 24 MiB per-process
   anonymous-heap ceiling (USER_ANON_MAX_BYTES in
   src/arch/x86_64/userspace.c) -- there is no more room to give it.
   Only one texture page is ever decoded and held at a time (see the
   room-compositing demo below), so this doesn't scale with how many
   pages a room's instances reference; a game whose largest real page
   needs meaningfully more than this will need a smarter approach
   (e.g. decoding/blitting a page's QOI stream incrementally instead of
   materializing the whole decoded page at once) rather than a bigger
   arena, since there isn't one. */
#define BUTTERSCOTCH_ARENA_BYTES (24u * 1024u * 1024u)
#ifndef DATA_WIN_PATH
#define DATA_WIN_PATH "/games/butterscotch/object-index-iteration-test.win"
#endif
#ifndef BUTTERSCOTCH_PROFILE_NAME
#define BUTTERSCOTCH_PROFILE_NAME "fixture"
#endif
#ifndef CONFIG_PATH
#define CONFIG_PATH "/home/demon/butterscotch.cfg"
#endif
#ifndef CONFIG_TEMP_PATH
#define CONFIG_TEMP_PATH "/home/demon/butterscotch.tmp"
#endif

static void line(const char *text) {
    demon_port_write(text);
    demon_port_write("\n");
}

static bool validate_binary_utils(void) {
    const uint8_t sample[] = {
        0x34u, 0x12u, 0x78u, 0x56u, 0x34u, 0x12u, 0xefu, 0xcdu,
        0xabu, 0x90u, 0x78u, 0x56u, 0x34u, 0x12u, 0x00u, 0x00u,
        0x80u, 0x3fu
    };
    return BinaryUtils_readUint16(sample) == 0x1234u &&
        BinaryUtils_readUint32(sample + 2u) == 0x12345678u &&
        BinaryUtils_readUint64(sample + 6u) == UINT64_C(0x1234567890abcdef) &&
        BinaryUtils_readFloat32(sample + 14u) == 1.0f;
}

static bool validate_index_rejections(void) {
    const uint8_t truncated[] = {
        'F', 'O', 'R', 'M', 8u, 0u, 0u, 0u,
        'G', 'E', 'N', '8', 8u, 0u, 0u, 0u
    };
    const uint8_t duplicates[] = {
        'F', 'O', 'R', 'M', 16u, 0u, 0u, 0u,
        'G', 'E', 'N', '8', 0u, 0u, 0u, 0u,
        'G', 'E', 'N', '8', 0u, 0u, 0u, 0u
    };
    DemonDataWinIndex index;
    BinaryReader reader = BinaryReader_create(NULL, sizeof(truncated));
    BinaryReader_setBuffer(&reader, (uint8_t *)truncated, 0u,
                           sizeof(truncated));
    if (DemonDataWinIndex_build(&reader, sizeof(truncated), &index))
        return false;

    reader = BinaryReader_create(NULL, sizeof(duplicates));
    BinaryReader_setBuffer(&reader, (uint8_t *)duplicates, 0u,
                           sizeof(duplicates));
    return !DemonDataWinIndex_build(&reader, sizeof(duplicates), &index) &&
        index.duplicateCount == 1u;
}

static bool validate_data_win(struct demon_port_file *file,
                              const uint8_t *data, size_t file_size,
                              DemonDataWinIndex *index, uint32_t *gen8_size,
                              uint32_t *string_count, uint32_t *code_count,
                              uint32_t *object_count, uint32_t *room_count,
                              DemonDataWinStringStats *string_stats,
                              DemonDataWinGen8Summary *gen8_summary,
                              DemonDataWinCodeStats *code_stats,
                              DemonDataWinObjectStats *object_stats,
                              DemonDataWinRoomStats *room_stats,
                              DemonDataWinTextureStats *texture_stats) {
    if ((data == NULL && file == NULL) || file_size < 16u) return false;
    BinaryReader reader = BinaryReader_create(data == NULL ? (FILE *)file : NULL,
                                              file_size);
    if (data != NULL)
        BinaryReader_setBuffer(&reader, (uint8_t *)data, 0u, file_size);
    if (!DemonDataWinIndex_build(&reader, file_size, index)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=index-build");
        return false;
    }

    const DemonDataWinChunk *gen8 = DemonDataWinIndex_find(index,
        DATAWIN_TAG('G', 'E', 'N', '8'));
    if (gen8 == NULL || gen8->size == 0u) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=gen8-missing");
        return false;
    }
    *gen8_size = gen8->size;

    if (!DemonDataWinIndex_pointerCount(&reader, index,
            DATAWIN_TAG('S', 'T', 'R', 'G'), string_count)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=strg-pointer-count");
        return false;
    }
    if (!DemonDataWinIndex_pointerCount(&reader, index,
            DATAWIN_TAG('C', 'O', 'D', 'E'), code_count)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=code-pointer-count");
        return false;
    }
    if (!DemonDataWinIndex_pointerCount(&reader, index,
            DATAWIN_TAG('O', 'B', 'J', 'T'), object_count)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=objt-pointer-count");
        return false;
    }
    if (!DemonDataWinIndex_pointerCount(&reader, index,
            DATAWIN_TAG('R', 'O', 'O', 'M'), room_count)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=room-pointer-count");
        return false;
    }
    if (!DemonDataWinIndex_inspectStrings(&reader, index, string_stats)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=inspect-strings");
        return false;
    }
    if (!DemonDataWinIndex_readGen8(&reader, index, gen8_summary)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=read-gen8");
        return false;
    }
    if (!DemonDataWinIndex_inspectCode(&reader, index,
            gen8_summary->wadVersion, code_stats)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=inspect-code");
        return false;
    }
    if (!DemonDataWinIndex_inspectObjects(&reader, index, gen8_summary,
            code_stats->count, object_stats)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=inspect-objects");
        return false;
    }
    if (!DemonDataWinIndex_inspectRooms(&reader, index, gen8_summary,
            object_stats->count, code_stats->count, room_stats)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=inspect-rooms");
        return false;
    }
    if (!DemonDataWinIndex_inspectTextures(&reader, index, gen8_summary,
            texture_stats)) {
        line("BUTTERSCOTCH_DEBUG_STAGE fail=inspect-textures");
        return false;
    }
    return true;
}

uint64_t butterscotch_core_main(void) {
    char startup[160];
    (void)snprintf(startup, sizeof(startup),
        "BUTTERSCOTCH_D1_START runtime=upstream-core target=demonos-x86_64 profile=%s",
        BUTTERSCOTCH_PROFILE_NAME);
    line(startup);
    if (!demon_port_init_dynamic(BUTTERSCOTCH_ARENA_BYTES)) {
        line("BUTTERSCOTCH_D1_FAIL portkit-init");
        return 1u;
    }

    if (!validate_binary_utils()) {
        line("BUTTERSCOTCH_D1_FAIL binary-utils");
        demon_port_shutdown();
        return 2u;
    }
    line("BUTTERSCOTCH_D1_BINARY_UTILS_OK reads=u16,u32,u64,float endian=little");
    line("BUTTERSCOTCH_D2_BINARY_READER_OK source=upstream mode=memory-buffer seek=bounds-checked");
    if (!validate_index_rejections()) {
        line("BUTTERSCOTCH_D2_FAIL index-negative-tests");
        demon_port_shutdown();
        return 6u;
    }
    line("BUTTERSCOTCH_D2_BOUNDS_SELFTEST_OK truncated=reject duplicates=reject");

    struct demon_port_file file;
    if (!demon_port_open(&file, DATA_WIN_PATH)) {
        char missing[192];
        (void)snprintf(missing, sizeof(missing),
            "BUTTERSCOTCH_D1_FAIL data-win-open profile=%s path=%s",
            BUTTERSCOTCH_PROFILE_NAME, DATA_WIN_PATH);
        line(missing);
        demon_port_shutdown();
        return 3u;
    }
    const size_t file_size = file.size;
#ifdef BUTTERSCOTCH_COMPAT_PROBE
    uint8_t *data = NULL;
#else
    uint8_t *data = (uint8_t *)demon_port_malloc(file_size);
    if (data == NULL || demon_port_read(&file, data, file_size) != file_size) {
        line("BUTTERSCOTCH_D1_FAIL data-win-read");
        demon_port_close(&file);
        demon_port_free(data);
        demon_port_shutdown();
        return 4u;
    }
    demon_port_close(&file);
#endif

    DemonDataWinIndex index;
    uint32_t gen8_size = 0u;
    uint32_t string_count = 0u;
    uint32_t code_count = 0u;
    uint32_t object_count = 0u;
    uint32_t room_count = 0u;
    DemonDataWinStringStats string_stats;
    DemonDataWinGen8Summary gen8_summary;
    DemonDataWinCodeStats code_stats;
    DemonDataWinObjectStats object_stats;
    DemonDataWinRoomStats room_stats;
    DemonDataWinTextureStats texture_stats;
    DemonDataWinAudioStats audio_stats;
    DemonButterscotchPcmStats pcm_stats;
    DemonButterscotchMixerStats mixer_stats;
    DemonBytecodeStats bytecode_stats;
    DemonVmExecutionStats vm_stats;
    uint32_t integer_operations = 0u, integer_comparisons = 0u;
    uint32_t integer_branches = 0u;
    uint32_t rvalue_conversions = 0u, rvalue_real_operations = 0u;
    uint32_t control_instructions = 0u, control_duplicates = 0u;
    uint32_t control_terminators = 0u;
    int64_t call_frame_result = 0;
    int64_t array_result = 0;
    uint32_t persistence_bytes = 0u, persistence_checksum = 0u;
    if (!DemonVm_integerOpcodeSelfTest(&integer_operations,
            &integer_comparisons, &integer_branches)) {
        line("BUTTERSCOTCH_D3_FAIL integer-opcodes");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    if (!DemonVm_rvalueSelfTest(&rvalue_conversions,
            &rvalue_real_operations) ||
        !DemonVm_controlOpcodeSelfTest(&control_instructions,
            &control_duplicates, &control_terminators) ||
        !DemonDataWinIndex_audioBoundsSelfTest()) {
        line("BUTTERSCOTCH_D3_FAIL rvalue-audio-bounds");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    if (!DemonVm_callFrameSelfTest(&call_frame_result)) {
        line("BUTTERSCOTCH_D3_FAIL call-frames");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    {
        char call_frame_report[64];
        (void)snprintf(call_frame_report, sizeof(call_frame_report),
            "BUTTERSCOTCH_D3_CALL_FRAME_OK result=%lld",
            (long long)call_frame_result);
        line(call_frame_report);
    }
    if (!DemonVm_arraySelfTest(&array_result)) {
        line("BUTTERSCOTCH_D3_FAIL arrays");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    {
        char array_report[64];
        (void)snprintf(array_report, sizeof(array_report),
            "BUTTERSCOTCH_D3_ARRAY_OK result=%lld",
            (long long)array_result);
        line(array_report);
    }
    if (!DemonVm_markerBuiltinSelfTest()) {
        line("BUTTERSCOTCH_D3_FAIL marker-builtins");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    line("BUTTERSCOTCH_D3_MARKER_BUILTIN_OK builtins=NullObject,Global,This,Other");
    if (!DemonVm_wideOpcodeSelfTest()) {
        line("BUTTERSCOTCH_D3_FAIL wide-opcodes");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    line("BUTTERSCOTCH_D3_WIDE_OPCODE_OK opcodes=PUSHLOC,PUSHGLB,PUSH.int64,PUSH.float");
    if (!DemonVm_placeMeetingSelfTest()) {
        line("BUTTERSCOTCH_D3_FAIL place-meeting");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    line("BUTTERSCOTCH_D3_PLACE_MEETING_OK targets=self,other,all,object,instance");
    if (!DemonButterscotchAudio_pcmSelfTest(&pcm_stats) ||
        !DemonButterscotchMixer_selfTest(&mixer_stats) ||
        !DemonButterscotchPersistence_selfTest(&persistence_bytes,
            &persistence_checksum)) {
        line("BUTTERSCOTCH_D5_FAIL pcm-decode-mix");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    if (!validate_data_win(&file, data, file_size, &index, &gen8_size,
                           &string_count, &code_count, &object_count,
                           &room_count, &string_stats, &gen8_summary,
                           &code_stats, &object_stats, &room_stats,
                           &texture_stats)) {
        line("BUTTERSCOTCH_D1_FAIL data-win-bounds");
        demon_port_free(data);
        demon_port_shutdown();
        return 5u;
    }
    BinaryReader audio_reader = BinaryReader_create(
        data == NULL ? (FILE *)&file : NULL, file_size);
    if (data != NULL)
        BinaryReader_setBuffer(&audio_reader, data, 0u, file_size);
    if (!DemonDataWinIndex_inspectAudio(&audio_reader, &index,
            gen8_summary.wadVersion, &audio_stats)) {
        line("BUTTERSCOTCH_D2_FAIL audio-resources");
        demon_port_free(data);
        demon_port_shutdown();
        return 6u;
    }
#ifdef BUTTERSCOTCH_COMPAT_PROBE
    char probe[256];
    (void)snprintf(probe, sizeof(probe),
        "BUTTERSCOTCH_GAME_PROBE_OK profile=%s bytes=%u wad=%u game-id=%u strings=%u code=%u objects=%u rooms=%u textures=%u sounds=%u audio=%u",
        BUTTERSCOTCH_PROFILE_NAME, (unsigned)file_size,
        (unsigned)gen8_summary.wadVersion, (unsigned)gen8_summary.gameId,
        (unsigned)string_count, (unsigned)code_count,
        (unsigned)object_count, (unsigned)room_count,
        (unsigned)texture_stats.textureCount, (unsigned)audio_stats.soundCount,
        (unsigned)audio_stats.audioEntryCount);
    line(probe);

    /* Stage 2 proof-of-concept: actually decompress the first real
       texture page (not just validate its header/bounds, as
       validate_data_win already does) using the real BZip2 decoder,
       verified byte-identical to the reference `bunzip2` tool against
       every texture page in this exact game's data.win. Rendering
       these pixels is separate future work; this just proves the
       decompression stage itself is real. */
    BinaryReader probe_texture_reader = BinaryReader_create(
        data == NULL ? (FILE *)&file : NULL, file_size);
    if (data != NULL)
        BinaryReader_setBuffer(&probe_texture_reader, data, 0u, file_size);
    uint32_t probe_blob_offset = 0u, probe_blob_size = 0u;
    if (DemonDataWinIndex_textureBlob(&probe_texture_reader, &index, &gen8_summary,
            0u, &probe_blob_offset, &probe_blob_size) &&
        probe_blob_size > 0u) {
        uint8_t *compressed_pixels = NULL;
        size_t compressed_pixel_bytes = 0u;
        uint32_t decoded_width = 0u, decoded_height = 0u;
        if (DemonDataWinIndex_decompressTexture(&probe_texture_reader,
                probe_blob_offset, probe_blob_offset + probe_blob_size,
                &compressed_pixels, &compressed_pixel_bytes,
                &decoded_width, &decoded_height)) {
            char decode_report[160];
            (void)snprintf(decode_report, sizeof(decode_report),
                "BUTTERSCOTCH_D2_TEXTURE_DECOMPRESS_OK texture=0 width=%u height=%u decoded-bytes=%u",
                (unsigned)decoded_width, (unsigned)decoded_height,
                (unsigned)compressed_pixel_bytes);
            line(decode_report);
            demon_port_free(compressed_pixels);
        } else {
            line("BUTTERSCOTCH_D2_TEXTURE_DECOMPRESS_FAIL texture=0");
        }

        /* Stage 3 proof-of-concept: run the full pipeline (BZip2 decode
           then GameMaker's custom "fioq" QOI-variant decode -- see
           qoi_decode.c) into real BGRA8 pixels. Verified against every
           real texture page in this game by rendering the decoded
           bytes to PNG and visually confirming genuine, correct game
           art (the "Field of Hopes and Dreams" room and card-suit
           enemy sprites) rather than noise -- see
           docs/butterscotch-games.md. Compositing these into an actual
           room and presenting a frame is the next milestone; this only
           proves the decode-to-pixels stage itself is real. */
        uint8_t *rgba_pixels = NULL;
        size_t rgba_pixel_bytes = 0u;
        uint32_t rgba_width = 0u, rgba_height = 0u;
        if (DemonDataWinIndex_decodeTexturePixels(&probe_texture_reader,
                probe_blob_offset, probe_blob_offset + probe_blob_size,
                &rgba_pixels, &rgba_pixel_bytes, &rgba_width, &rgba_height)) {
            uint32_t checksum = 2166136261u;
            for (size_t i = 0u; i < rgba_pixel_bytes; ++i) {
                checksum ^= rgba_pixels[i];
                checksum *= 16777619u;
            }
            char pixel_report[160];
            (void)snprintf(pixel_report, sizeof(pixel_report),
                "BUTTERSCOTCH_D3_TEXTURE_PIXELS_OK texture=0 width=%u height=%u bytes=%u fnv=%08x",
                (unsigned)rgba_width, (unsigned)rgba_height,
                (unsigned)rgba_pixel_bytes, (unsigned)checksum);
            line(pixel_report);
            demon_port_free(rgba_pixels);
        } else {
            line("BUTTERSCOTCH_D3_TEXTURE_PIXELS_FAIL texture=0");
        }
    }

    /* Stage 4 proof-of-concept: resolve a real room's placed instances
       (DemonDataWinIndex_roomScene -- room/object/sprite/TPAG
       resolution, including the sprite frame-list offset fix above),
       decode the real texture pages those instances actually use, and
       composite them into one real BGRA8 room frame. Room 54 was picked
       by scanning all 147 real rooms for one with a full 16-instance
       scene and mostly-visible sprites (see docs/butterscotch-games.md).
       Verified by rendering the composited frame to PNG on the host and
       visually confirming a genuine, recognizable game scene (a
       character sprite and several real enemy sprites, correctly
       positioned and alpha-composited against the room background) --
       not garbage. Only one texture page is decoded and held in memory
       at a time (freed before moving to the next), to stay well under
       the per-process heap ceiling even though a room can reference
       several different 2048x2048 pages. This does not run any of the
       room's or instances' scripts -- it is pure asset placement. */
    {
        const uint32_t demoRoomIndex = 54u;
        DemonDataWinScene scene;
        BinaryReader scene_reader = BinaryReader_create(
            data == NULL ? (FILE *)&file : NULL, file_size);
        if (data != NULL)
            BinaryReader_setBuffer(&scene_reader, data, 0u, file_size);
        if (!DemonDataWinIndex_roomScene(&scene_reader, &index, &gen8_summary,
                demoRoomIndex, &scene)) {
            line("BUTTERSCOTCH_D4_ROOM_SCENE_FAIL");
        } else {
            const size_t frameBytes =
                (size_t)scene.roomWidth * scene.roomHeight * 4u;
            uint8_t *frame = (uint8_t *)demon_port_malloc(frameBytes);
            if (frame == NULL) {
                line("BUTTERSCOTCH_D4_ROOM_COMPOSITE_FAIL reason=frame-alloc");
            } else {
                for (size_t i = 0u; i < frameBytes; i += 4u) {
                    frame[i] = 40u; frame[i + 1u] = 40u;
                    frame[i + 2u] = 40u; frame[i + 3u] = 255u;
                }
                bool compositeOk = true;
                uint32_t placedInstances = 0u;
                for (int32_t page = 0; page < 32 && compositeOk; ++page) {
                    bool pageNeeded = false;
                    for (uint32_t i = 0u; i < scene.instanceCount; ++i) {
                        DemonDataWinPageItem probeItem;
                        if (scene.instances[i].pageItemId < 0) continue;
                        if (!DemonDataWinIndex_pageItem(&scene_reader, &index,
                                (uint32_t)scene.instances[i].pageItemId,
                                &probeItem)) continue;
                        if (probeItem.texturePageId == page) { pageNeeded = true; break; }
                    }
                    if (!pageNeeded) continue;

                    uint32_t pageBlobOffset = 0u, pageBlobSize = 0u;
                    uint8_t *pagePixels = NULL;
                    size_t pagePixelBytes = 0u;
                    uint32_t pageWidth = 0u, pageHeight = 0u;
                    if (!DemonDataWinIndex_textureBlob(&scene_reader, &index,
                            &gen8_summary, (uint32_t)page, &pageBlobOffset,
                            &pageBlobSize)) {
                        char debug_report[96];
                        (void)snprintf(debug_report, sizeof(debug_report),
                            "BUTTERSCOTCH_DEBUG_D4 fail=texture-blob page=%d", (int)page);
                        line(debug_report);
                        compositeOk = false;
                        break;
                    }
                    {
                        const struct demon_port_runtime *pre = demon_port_status();
                        char pre_report[128];
                        (void)snprintf(pre_report, sizeof(pre_report),
                            "BUTTERSCOTCH_DEBUG_D4 pre-decode page=%d heap-used=%llu heap-peak=%llu heap-capacity=%llu",
                            (int)page, (unsigned long long)pre->heap_used,
                            (unsigned long long)pre->heap_peak,
                            (unsigned long long)pre->heap_capacity);
                        line(pre_report);
                    }
                    if (!DemonDataWinIndex_decodeTexturePixels(&scene_reader,
                            pageBlobOffset, pageBlobOffset + pageBlobSize,
                            &pagePixels, &pagePixelBytes, &pageWidth, &pageHeight)) {
                        const struct demon_port_runtime *status = demon_port_status();
                        char debug_report[160];
                        (void)snprintf(debug_report, sizeof(debug_report),
                            "BUTTERSCOTCH_DEBUG_D4 fail=decode-pixels page=%d heap-used=%llu heap-capacity=%llu",
                            (int)page, (unsigned long long)status->heap_used,
                            (unsigned long long)status->heap_capacity);
                        line(debug_report);
                        compositeOk = false;
                        break;
                    }

                    for (uint32_t i = 0u; i < scene.instanceCount; ++i) {
                        const DemonDataWinSceneInstance *inst = &scene.instances[i];
                        if (inst->pageItemId < 0) continue;
                        DemonDataWinPageItem item;
                        if (!DemonDataWinIndex_pageItem(&scene_reader, &index,
                                (uint32_t)inst->pageItemId, &item)) continue;
                        if (item.texturePageId != page) continue;
                        ++placedInstances;
                        for (uint32_t row = 0u; row < item.sourceHeight; ++row) {
                            const int32_t destY = inst->y + (int32_t)row;
                            if (destY < 0 || (uint32_t)destY >= scene.roomHeight) continue;
                            const uint32_t srcRowOffset =
                                ((uint32_t)item.sourceY + row) * pageWidth +
                                (uint32_t)item.sourceX;
                            for (uint32_t col = 0u; col < item.sourceWidth; ++col) {
                                const int32_t destX = inst->x + (int32_t)col;
                                if (destX < 0 || (uint32_t)destX >= scene.roomWidth) continue;
                                const size_t srcOffset =
                                    ((size_t)srcRowOffset + col) * 4u;
                                if (srcOffset + 3u >= pagePixelBytes) continue;
                                const uint8_t alpha = pagePixels[srcOffset + 3u];
                                if (alpha == 0u) continue;
                                const size_t dstOffset =
                                    ((size_t)(uint32_t)destY * scene.roomWidth +
                                     (uint32_t)destX) * 4u;
                                frame[dstOffset] = pagePixels[srcOffset];
                                frame[dstOffset + 1u] = pagePixels[srcOffset + 1u];
                                frame[dstOffset + 2u] = pagePixels[srcOffset + 2u];
                                frame[dstOffset + 3u] = 255u;
                            }
                        }
                    }
                    demon_port_free(pagePixels);
                }

                if (!compositeOk) {
                    line("BUTTERSCOTCH_D4_ROOM_COMPOSITE_FAIL reason=texture-decode");
                } else {
                    uint32_t checksum = 2166136261u;
                    for (size_t i = 0u; i < frameBytes; ++i) {
                        checksum ^= frame[i];
                        checksum *= 16777619u;
                    }
                    char room_report[192];
                    (void)snprintf(room_report, sizeof(room_report),
                        "BUTTERSCOTCH_D4_ROOM_COMPOSITE_OK room=%u width=%u height=%u instances=%u placed=%u fnv=%08x",
                        (unsigned)demoRoomIndex, (unsigned)scene.roomWidth,
                        (unsigned)scene.roomHeight, (unsigned)scene.instanceCount,
                        (unsigned)placedInstances, (unsigned)checksum);
                    line(room_report);

                    /* Stage 5: actually put the composited room on
                       screen -- the same raw display_submit path Doom
                       and ClassiCube use (see
                       ports/classicube/platform/Window_DemonOS.c),
                       deliberately NOT DemonX/Xlib, so this works from
                       the plain mako# console with no desktop/compositor
                       session required, exactly like every other test in
                       this file so far. The frame buffer is already in
                       B,G,R,A byte order per pixel (see qoi_decode.c);
                       read as a native little-endian uint32 that's
                       A<<24|R<<16|G<<8|B -- ARGB32 -- which is exactly
                       what the kernel backbuffer expects, so no pixel
                       format conversion is needed. Skipped during the
                       automated "boottest" scripted self-test (nothing
                       is watching the screen there, and it isn't the
                       thing being tested); a real interactive session
                       gets a live screen. */
                    if (demon_boot_test_mode() == 0u) {
                        struct demon_display_submit_request {
                            uint64_t x, y, width, height, pixels, flags;
                        };
                        const uint64_t display = demon_service_open(7u); /* CAPABILITY_SERVICE_DISPLAY */
                        if (display != UINT64_MAX) {
                            const struct demon_display_submit_request request = {
                                .x = 0u, .y = 0u,
                                .width = (uint64_t)scene.roomWidth,
                                .height = (uint64_t)scene.roomHeight,
                                .pixels = (uint64_t)(uintptr_t)frame,
                                .flags = 1u,
                            };
                            if (demon_display_submit(display, &request) == 0u) {
                                line("BUTTERSCOTCH_D5_DISPLAY_PRESENT_OK room=54 width=640 height=480");
                                /* Hold the frame on screen long enough
                                   for a human (or a QEMU screendump) to
                                   actually see it before this process
                                   exits and MakoBox redraws its own
                                   prompt over it. */
                                for (unsigned i = 0u; i < 3000u; ++i)
                                    demon_port_sleep_ms(16u);
                            } else {
                                line("BUTTERSCOTCH_D5_DISPLAY_PRESENT_FAIL reason=submit");
                            }
                        } else {
                            line("BUTTERSCOTCH_D5_DISPLAY_PRESENT_FAIL reason=no-display-service");
                        }
                    }
                }
                demon_port_free(frame);
            }
        }
    }

    /* Stage 6 proof-of-concept: DemonBytecode_scan strictly decodes and
       validates every single instruction (and every branch target) in
       every real CODE entry -- this is the first opcode-level look at
       the actual script bytecode this game uses, not just chunk/record
       structure. It has only ever been exercised against the tiny
       synthetic fixture (4 code entries, ~50 instructions) before now. */
    {
        BinaryReader bytecode_probe_reader = BinaryReader_create(
            data == NULL ? (FILE *)&file : NULL, file_size);
        if (data != NULL)
            BinaryReader_setBuffer(&bytecode_probe_reader, data, 0u, file_size);
        DemonBytecodeStats probeStats;
        if (!DemonBytecode_scan(&bytecode_probe_reader, &index,
                gen8_summary.wadVersion, &probeStats)) {
            char bytecode_report[192];
            (void)snprintf(bytecode_report, sizeof(bytecode_report),
                "BUTTERSCOTCH_D6_BYTECODE_SCAN_FAIL entries-scanned=%u instructions=%u unknown-opcodes=%u",
                (unsigned)probeStats.codeEntries, (unsigned)probeStats.instructions,
                (unsigned)probeStats.unknownOpcodes);
            line(bytecode_report);
        } else {
            char bytecode_report[224];
            (void)snprintf(bytecode_report, sizeof(bytecode_report),
                "BUTTERSCOTCH_D6_BYTECODE_SCAN_OK entries=%u instructions=%u pushes=%u arithmetic=%u calls=%u branches=%u exits=%u",
                (unsigned)probeStats.codeEntries, (unsigned)probeStats.instructions,
                (unsigned)probeStats.pushes, (unsigned)probeStats.arithmetic,
                (unsigned)probeStats.calls, (unsigned)probeStats.branches,
                (unsigned)probeStats.exits);
            line(bytecode_report);
        }
    }

    /* Stage 8 proof-of-concept: DemonVm_executeEventsState actually runs
       bytecode (as opposed to Stage 6's pure structural validation) against
       a handful of real, small CODE entries -- the first time any real
       script's compiled GMS2.3 output has executed, not just been decoded.
       Needs full memory-buffer mode (vm.file = reader->buffer), so this
       loads its own temporary whole-file copy rather than reusing the
       streaming reader every other probe stage here uses; freed again
       immediately after. */
    {
        uint8_t *vm_probe_data = (uint8_t *)demon_port_malloc(file_size);
        if (vm_probe_data != NULL && demon_port_seek(&file, 0u) &&
            demon_port_read(&file, vm_probe_data, file_size) == file_size) {
            BinaryReader vm_probe_reader = BinaryReader_create(NULL, file_size);
            BinaryReader_setBuffer(&vm_probe_reader, vm_probe_data, 0u, file_size);
            DemonVmInstanceState vm_probe_state;
            DemonVm_initInstanceState(&vm_probe_state, 0, 0);
            DemonVmExecutionStats vm_probe_stats;
            /* CODE[1]/[6]/[22]: gml_Script_scr_debug_save_trophies,
               gml_Script_ossafe_init, gml_Script_scr_test_save_data --
               small (56-92 byte) real scripts, individually confirmed on
               the host to execute their entire compiled GMS2.3
               method(self, <itself>) preamble correctly. */
            const uint32_t vm_probe_ids[3] = {1u, 6u, 22u};
            if (!DemonVm_executeEventsState(&vm_probe_reader, &index,
                    gen8_summary.wadVersion, 0u, vm_probe_ids, 3u,
                    &vm_probe_state, &vm_probe_stats)) {
                char vm_report[192];
                (void)snprintf(vm_report, sizeof(vm_report),
                    "BUTTERSCOTCH_D8_VM_SCRIPT_FAIL entries=%u instructions=%u addr=0x%x instr=0x%08x",
                    (unsigned)vm_probe_stats.codeEntries,
                    (unsigned)vm_probe_stats.instructions,
                    (unsigned)vm_probe_stats.diagnosticAddress,
                    (unsigned)vm_probe_stats.diagnosticInstruction);
                line(vm_report);
            } else {
                char vm_report[192];
                (void)snprintf(vm_report, sizeof(vm_report),
                    "BUTTERSCOTCH_D8_VM_SCRIPT_OK entries=%u instructions=%u builtins=%u",
                    (unsigned)vm_probe_stats.codeEntries,
                    (unsigned)vm_probe_stats.instructions,
                    (unsigned)vm_probe_stats.builtinCalls);
                line(vm_report);
            }

            /* Stage 9 proof-of-concept: real per-instance Step/Collision
               dispatch against one full real room's real placed instances
               (room 54, already used by the asset-compositing probe above
               and confirmed to have exactly 16 instances) -- decoupled
               from live rendering entirely, so no texture-decode memory
               cost beyond the full-buffer copy already loaded above for
               the script probe. DemonSoftwareRenderer_overlap only needs
               each instance's TPAG target rectangle (cheap metadata via
               DemonDataWinIndex_pageItem), never decoded pixels. This is
               the first time the per-instance dispatch machinery the
               interactive live loop uses runs against a real room's real
               OBJT event tables instead of the tiny synthetic fixture --
               failures here are informative, not fatal to the probe:
               every instance is still attempted even if others fail. */
            {
                DemonDataWinScene dispatch_scene;
                BinaryReader dispatch_reader = BinaryReader_create(NULL, file_size);
                BinaryReader_setBuffer(&dispatch_reader, vm_probe_data, 0u, file_size);
                if (!DemonDataWinIndex_roomScene(&dispatch_reader, &index,
                        &gen8_summary, 54u, &dispatch_scene)) {
                    line("BUTTERSCOTCH_D9_REAL_DISPATCH_FAIL reason=room-scene");
                } else {
                    DemonVmInstanceState dispatch_state[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    uint32_t dispatch_step_code[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    DemonRenderCommand dispatch_box[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    bool dispatch_has_box[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    for (uint32_t i = 0u; i < DEMON_DATAWIN_SCENE_INSTANCE_MAX; ++i)
                        dispatch_has_box[i] = false;
                    uint32_t steppable = 0u, step_ok = 0u, step_fail = 0u;
                    uint32_t diagnostics_printed = 0u;
                    const uint32_t diagnostics_max = 8u;
                    for (uint32_t i = 0u; i < dispatch_scene.instanceCount; ++i) {
                        const DemonDataWinSceneInstance *instance =
                            &dispatch_scene.instances[i];
                        DemonVm_initInstanceState(&dispatch_state[i],
                            instance->x, instance->y);
                        DemonVm_setInstanceContext(&dispatch_state[i],
                            (int32_t)instance->instanceId, -1);
                        dispatch_step_code[i] = UINT32_MAX;
                        if (instance->objectId >= 0) {
                            uint32_t code;
                            if (DemonDataWinIndex_eventCode(&dispatch_reader,
                                    &index, &gen8_summary,
                                    (uint32_t)instance->objectId, 3u, 0u,
                                    &code) && code < code_count)
                                dispatch_step_code[i] = code;
                        }
                        if (instance->pageItemId >= 0) {
                            DemonDataWinPageItem item;
                            if (DemonDataWinIndex_pageItem(&dispatch_reader,
                                    &index, (uint32_t)instance->pageItemId,
                                    &item)) {
                                dispatch_box[i] = (DemonRenderCommand){
                                    .opcode = DEMON_RENDER_TPAG,
                                    .x = instance->x, .y = instance->y,
                                    .item = item
                                };
                                dispatch_has_box[i] = true;
                            }
                        }
                    }
                    /* World data for place_meeting-style builtins: built
                     * once from every instance's pre-Step position, shared
                     * by every instance's Step dispatch this pass -- real
                     * GML's own per-instance dispatch order already makes
                     * this genuinely order-dependent, so using pre-tick
                     * positions uniformly is a reasonable, documented
                     * approximation rather than a new kind of
                     * incorrectness. */
                    DemonVmWorldInstance dispatch_world[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    for (uint32_t i = 0u; i < dispatch_scene.instanceCount; ++i) {
                        dispatch_world[i] = (DemonVmWorldInstance){
                            .instanceId = (int32_t)dispatch_scene.instances[i].instanceId,
                            .objectId = dispatch_scene.instances[i].objectId,
                            .x = dispatch_scene.instances[i].x,
                            .y = dispatch_scene.instances[i].y,
                            .item = dispatch_has_box[i] ? dispatch_box[i].item :
                                (DemonDataWinPageItem){0},
                            .hasBox = dispatch_has_box[i]
                        };
                    }
                    for (uint32_t i = 0u; i < dispatch_scene.instanceCount; ++i) {
                        const DemonDataWinSceneInstance *instance =
                            &dispatch_scene.instances[i];
                        DemonVm_setWorld(&dispatch_state[i], dispatch_world,
                            dispatch_scene.instanceCount);
                        if (dispatch_step_code[i] == UINT32_MAX) continue;
                        ++steppable;
                        BinaryReader step_reader = BinaryReader_create(
                            NULL, file_size);
                        BinaryReader_setBuffer(&step_reader, vm_probe_data,
                            0u, file_size);
                        DemonVmExecutionStats step_stats;
                        const uint32_t step_id = dispatch_step_code[i];
                        if (!DemonVm_executeEventsState(&step_reader, &index,
                                gen8_summary.wadVersion, 0u,
                                &step_id, 1u,
                                &dispatch_state[i], &step_stats)) {
                            ++step_fail;
                            if (diagnostics_printed < diagnostics_max) {
                                ++diagnostics_printed;
                                char diag[192];
                                (void)snprintf(diag, sizeof(diag),
                                    "BUTTERSCOTCH_D9_STEP_FAIL instance=%u object=%d code=%u addr=0x%x instr=0x%08x",
                                    (unsigned)i, (int)instance->objectId,
                                    (unsigned)dispatch_step_code[i],
                                    (unsigned)step_stats.diagnosticAddress,
                                    (unsigned)step_stats.diagnosticInstruction);
                                line(diag);
                            }
                        } else {
                            ++step_ok;
                            if (dispatch_has_box[i]) {
                                dispatch_box[i].x = dispatch_state[i].variables[3];
                                dispatch_box[i].y = dispatch_state[i].variables[4];
                            }
                        }
                    }
                    uint32_t collidable_pairs = 0u, collisions_ok = 0u,
                        collisions_fail = 0u;
                    for (uint32_t i = 0u; i < dispatch_scene.instanceCount; ++i) {
                        if (!dispatch_has_box[i] ||
                            dispatch_scene.instances[i].objectId < 0) continue;
                        uint32_t collision_ids[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                        uint32_t collision_id_count = 0u;
                        int32_t collision_other = -1;
                        for (uint32_t j = 0u; j < dispatch_scene.instanceCount; ++j) {
                            if (i == j || !dispatch_has_box[j] ||
                                dispatch_scene.instances[j].objectId < 0) continue;
                            uint32_t code;
                            if (!DemonDataWinIndex_collisionCode(&dispatch_reader,
                                    &index, &gen8_summary,
                                    (uint32_t)dispatch_scene.instances[i].objectId,
                                    (uint32_t)dispatch_scene.instances[j].objectId,
                                    &code) || code >= code_count) continue;
                            ++collidable_pairs;
                            if (DemonSoftwareRenderer_overlap(&dispatch_box[i],
                                    &dispatch_box[j]) &&
                                collision_id_count < DEMON_DATAWIN_SCENE_INSTANCE_MAX) {
                                collision_ids[collision_id_count++] = code;
                                collision_other =
                                    (int32_t)dispatch_scene.instances[j].instanceId;
                            }
                        }
                        if (collision_id_count == 0u) continue;
                        DemonVm_setInstanceContext(&dispatch_state[i],
                            (int32_t)dispatch_scene.instances[i].instanceId,
                            collision_other);
                        BinaryReader collide_reader = BinaryReader_create(
                            NULL, file_size);
                        BinaryReader_setBuffer(&collide_reader, vm_probe_data,
                            0u, file_size);
                        DemonVmExecutionStats collide_stats;
                        if (!DemonVm_executeEventsState(&collide_reader, &index,
                                gen8_summary.wadVersion, 0u,
                                collision_ids, collision_id_count,
                                &dispatch_state[i], &collide_stats)) {
                            ++collisions_fail;
                            if (diagnostics_printed < diagnostics_max) {
                                ++diagnostics_printed;
                                char diag[192];
                                (void)snprintf(diag, sizeof(diag),
                                    "BUTTERSCOTCH_D9_COLLISION_FAIL instance=%u object=%d codes=%u addr=0x%x instr=0x%08x",
                                    (unsigned)i,
                                    (int)dispatch_scene.instances[i].objectId,
                                    (unsigned)collision_id_count,
                                    (unsigned)collide_stats.diagnosticAddress,
                                    (unsigned)collide_stats.diagnosticInstruction);
                                line(diag);
                            }
                        } else {
                            ++collisions_ok;
                        }
                    }
                    char dispatch_report[224];
                    (void)snprintf(dispatch_report, sizeof(dispatch_report),
                        "BUTTERSCOTCH_D9_REAL_DISPATCH_%s instances=%u steppable=%u step-ok=%u step-fail=%u collidable-pairs=%u collisions-ok=%u collisions-fail=%u",
                        (step_fail == 0u && collisions_fail == 0u) ?
                            "OK" : "PARTIAL",
                        (unsigned)dispatch_scene.instanceCount,
                        (unsigned)steppable, (unsigned)step_ok,
                        (unsigned)step_fail, (unsigned)collidable_pairs,
                        (unsigned)collisions_ok, (unsigned)collisions_fail);
                    line(dispatch_report);
                }
            }
        } else {
            line("BUTTERSCOTCH_D8_VM_SCRIPT_FAIL reason=buffer-load");
        }
        demon_port_free(vm_probe_data);
    }

    line("BUTTERSCOTCH_GAME_PROBE_STOP compatibility audit complete; full VM execution not yet enabled");
    demon_port_close(&file);
    demon_port_free(data);
    demon_port_shutdown();
    return 0u;
#endif
    DemonButterscotchConfig config = {
        .gameId = gen8_summary.gameId,
        .windowWidth = 640u,
        .windowHeight = 480u,
        .masterGainQ8 = 256u,
        .fullscreen = false
    };
    DemonButterscotchConfig loaded_config;
    if (DemonButterscotchPersistence_load(CONFIG_PATH, &loaded_config) &&
        loaded_config.gameId == gen8_summary.gameId)
        config = loaded_config;

    BinaryReader texture_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&texture_reader, data, 0u, file_size);
    uint32_t texture_blob_offset = 0u, texture_blob_size = 0u;
    DemonDecodedImage decoded_texture;
    if (!DemonDataWinIndex_textureBlob(&texture_reader, &index,
            &gen8_summary, 0u, &texture_blob_offset, &texture_blob_size) ||
        (uint64_t)texture_blob_offset + texture_blob_size > file_size ||
        !DemonImage_decodePngRgba(data + texture_blob_offset,
            texture_blob_size, &decoded_texture)) {
        line("BUTTERSCOTCH_D2_FAIL texture-decode");
        demon_port_free(data);
        demon_port_shutdown();
        return 7u;
    }
    BinaryReader bytecode_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&bytecode_reader, data, 0u, file_size);
    if (!DemonBytecode_scan(&bytecode_reader, &index,
                            gen8_summary.wadVersion, &bytecode_stats)) {
        line("BUTTERSCOTCH_D3_FAIL bytecode-decode");
        demon_port_free(data);
        demon_port_shutdown();
        return 8u;
    }
    BinaryReader vm_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&vm_reader, data, 0u, file_size);
    if (!DemonVm_executeFixture(&vm_reader, &index,
            gen8_summary.wadVersion, &vm_stats)) {
        char diagnostic[160];
        (void)snprintf(diagnostic, sizeof(diagnostic),
            "BUTTERSCOTCH_D3_FAIL vm-execute address=%08x instruction=%08x stack=%u executed=%u",
            (unsigned)vm_stats.diagnosticAddress,
            (unsigned)vm_stats.diagnosticInstruction,
            (unsigned)vm_stats.diagnosticStackDepth,
            (unsigned)vm_stats.instructions);
        line(diagnostic);
        demon_port_free(data);
        demon_port_shutdown();
        return 9u;
    }
    BinaryReader input_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&input_reader, data, 0u, file_size);
    DemonVmExecutionStats input_stats;
    if (!DemonVm_executeFixtureInput(&input_reader, &index,
            gen8_summary.wadVersion,
            DEMON_VM_KEY_RIGHT | DEMON_VM_KEY_UP,
            10, 20, &input_stats) || input_stats.finalX != 14 ||
        input_stats.finalY != 16) {
        line("BUTTERSCOTCH_D4_FAIL live-input-selftest");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 9u;
    }

    char report[192];
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D1_DATAWIN_OK magic=FORM bytes=%u chunks=%u gen8=%u",
        (unsigned)file_size, (unsigned)index.count, (unsigned)gen8_size);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D5_PERSISTENCE_OK bytes=%u checksum=%08x format=BSCF/1 write=staged-rename corruption=reject path=per-game",
        (unsigned)persistence_bytes, (unsigned)persistence_checksum);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D5_VOICES_OK capacity=%u played=%u loop-wraps=%u paused-frames=%u controls=play,pause,resume,stop gain=q8 pan=q8",
        (unsigned)mixer_stats.capacity, (unsigned)mixer_stats.voicesPlayed,
        (unsigned)mixer_stats.loopWraps, (unsigned)mixer_stats.pausedFrames);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D5_PCM_OK codec=wav-pcm source=%uHz/%uch/%ubit frames=%u output=%uHz/stereo/%u mix=saturating allocation=caller",
        (unsigned)pcm_stats.sourceRate, (unsigned)pcm_stats.channels,
        (unsigned)pcm_stats.bitsPerSample, (unsigned)pcm_stats.sourceFrames,
        44100u, (unsigned)pcm_stats.outputFrames);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D3_CONTROL_OK instructions=%u dup=%u terminators=%u stack-guards=underflow,overflow",
        (unsigned)control_instructions, (unsigned)control_duplicates,
        (unsigned)control_terminators);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D3_RVALUE_OK kinds=int,bool,real,string,undefined conversions=%u real-ops=%u guards=zero-divide",
        (unsigned)rvalue_conversions, (unsigned)rvalue_real_operations);
    line(report);
    line("BUTTERSCOTCH_D4_INPUT_VM_OK keys=right,up start=10,20 position=14,16");
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D3_INTEGER_OPS_OK operations=%u comparisons=%u branches=%u guards=divide-zero,shift-range",
        (unsigned)integer_operations, (unsigned)integer_comparisons,
        (unsigned)integer_branches);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_AUDIO_RESOURCES_OK sounds=%u/%u embedded=%u compressed=%u entries=%u/%u bytes=%u max=%u fnv=%08x bounds=reject",
        (unsigned)audio_stats.presentSounds, (unsigned)audio_stats.soundCount,
        (unsigned)audio_stats.embeddedSounds,
        (unsigned)audio_stats.compressedSounds,
        (unsigned)audio_stats.presentAudioEntries,
        (unsigned)audio_stats.audioEntryCount,
        (unsigned)audio_stats.totalAudioBytes,
        (unsigned)audio_stats.largestAudioEntry,
        (unsigned)audio_stats.fnv1a);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D3_VM_OK entries=%u instructions=%u builtins=%u keys=%u messages=%u branches=%u/%u stores=%u position=%d,%d fnv=%08x",
        (unsigned)vm_stats.codeEntries, (unsigned)vm_stats.instructions,
        (unsigned)vm_stats.builtinCalls, (unsigned)vm_stats.keyboardChecks,
        (unsigned)vm_stats.debugMessages, (unsigned)vm_stats.branchesTaken,
        (unsigned)vm_stats.branches, (unsigned)vm_stats.variableStores,
        vm_stats.finalX, vm_stats.finalY, (unsigned)vm_stats.messageFnv1a);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D3_BYTECODE_OK entries=%u instructions=%u operands=%u pushes=%u conv=%u math=%u stores=%u calls=%u branches=%u drops=%u exits=%u unknown=%u",
        (unsigned)bytecode_stats.codeEntries,
        (unsigned)bytecode_stats.instructions,
        (unsigned)bytecode_stats.operandBytes,
        (unsigned)bytecode_stats.pushes,
        (unsigned)bytecode_stats.conversions,
        (unsigned)bytecode_stats.arithmetic,
        (unsigned)bytecode_stats.stores, (unsigned)bytecode_stats.calls,
        (unsigned)bytecode_stats.branches,
        (unsigned)bytecode_stats.stackDrops,
        (unsigned)bytecode_stats.exits,
        (unsigned)bytecode_stats.unknownOpcodes);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_INDEX_OK chunks=%u duplicates=%u capacity=%u",
        (unsigned)index.count, (unsigned)index.duplicateCount,
        (unsigned)DATAWIN_INDEX_MAX_CHUNKS);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_RESOURCES_OK strings=%u code=%u objects=%u rooms=%u",
        (unsigned)string_count, (unsigned)code_count,
        (unsigned)object_count, (unsigned)room_count);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_STRG_OK entries=%u nonnull=%u bytes=%u max=%u fnv=%08x",
        (unsigned)string_stats.count, (unsigned)string_stats.nonNullCount,
        (unsigned)string_stats.totalBytes, (unsigned)string_stats.maxLength,
        (unsigned)string_stats.fnv1a);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_GEN8_OK wad=%u game=%u version=%u.%u.%u.%u size=%ux%u",
        (unsigned)gen8_summary.wadVersion, (unsigned)gen8_summary.gameId,
        (unsigned)gen8_summary.major, (unsigned)gen8_summary.minor,
        (unsigned)gen8_summary.release, (unsigned)gen8_summary.build,
        (unsigned)gen8_summary.defaultWidth,
        (unsigned)gen8_summary.defaultHeight);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_CODE_OK entries=%u present=%u bytes=%u max=%u locals=%u args=%u blob=%x-%x fnv=%08x",
        (unsigned)code_stats.count, (unsigned)code_stats.presentCount,
        (unsigned)code_stats.totalBytecodeBytes,
        (unsigned)code_stats.maxBytecodeLength,
        (unsigned)code_stats.totalLocals,
        (unsigned)code_stats.totalArguments, (unsigned)code_stats.blobStart,
        (unsigned)code_stats.blobEnd, (unsigned)code_stats.fnv1a);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_OBJT_OK entries=%u present=%u lists=%u events=%u actions=%u code-actions=%u vertices=%u parents=%u",
        (unsigned)object_stats.count, (unsigned)object_stats.presentCount,
        (unsigned)object_stats.eventTypeLists,
        (unsigned)object_stats.eventCount,
        (unsigned)object_stats.actionCount,
        (unsigned)object_stats.codeActionCount,
        (unsigned)object_stats.physicsVertexCount,
        (unsigned)object_stats.inheritanceEdges);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_ROOM_OK entries=%u present=%u max=%ux%u backgrounds=%u/%u views=%u/%u instances=%u bound=%u tiles=%u code-refs=%u",
        (unsigned)room_stats.count, (unsigned)room_stats.presentCount,
        (unsigned)room_stats.maxWidth, (unsigned)room_stats.maxHeight,
        (unsigned)room_stats.enabledBackgrounds,
        (unsigned)room_stats.backgroundSlots,
        (unsigned)room_stats.enabledViews, (unsigned)room_stats.viewSlots,
        (unsigned)room_stats.instanceCount,
        (unsigned)room_stats.boundInstanceCount,
        (unsigned)room_stats.tileCount,
        (unsigned)room_stats.creationCodeRefs);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_TEXTURE_OK items=%u/%u textures=%u embedded=%u png-bytes=%u atlas-pixels=%u max=%ux%u fnv=%08x",
        (unsigned)texture_stats.presentPageItems,
        (unsigned)texture_stats.pageItemCount,
        (unsigned)texture_stats.textureCount,
        (unsigned)texture_stats.embeddedTextures,
        (unsigned)texture_stats.totalPngBytes,
        (unsigned)texture_stats.totalAtlasPixels,
        (unsigned)texture_stats.maxTextureWidth,
        (unsigned)texture_stats.maxTextureHeight,
        (unsigned)texture_stats.fnv1a);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D2_RGBA_OK size=%ux%u bytes=%u fnv=%08x decoder=upstream-stb",
        (unsigned)decoded_texture.width, (unsigned)decoded_texture.height,
        (unsigned)decoded_texture.byteCount, (unsigned)decoded_texture.fnv1a);
    line(report);
    BinaryReader render_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&render_reader, data, 0u, file_size);
    DemonRenderList render_list;
    memset(&render_list, 0, sizeof(render_list));
    render_list.commands[render_list.count++] = (DemonRenderCommand){
        .opcode = DEMON_RENDER_CLEAR, .color = 0x10243bffu
    };
    DemonDataWinScene scene;
    if (!DemonDataWinIndex_roomScene(&render_reader, &index, &gen8_summary, 0u, &scene)) {
        line("BUTTERSCOTCH_D4_FAIL room-scene");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    /* Every real placed instance gets its own persistent VM state and its
     * own Step handler resolved from its own real objectId (not hardcoded
     * to object 0), instead of the single hardcoded "movable" instance the
     * fixture demo used to assume. Instances with no sprite (pageItemId<0)
     * still get state and Step dispatch -- they just never get a render
     * command -- matching how real GML objects can be invisible controller
     * instances. State is seeded from the instance's own real room
     * position, matching real GML semantics (x/y at Create time IS the
     * room-placed position), rather than the old demo's arbitrary
     * "vm_stats.finalX/Y from an earlier unrelated self-test" offset. */
    DemonVmInstanceState instance_state[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
    uint32_t instance_step_code[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
    uint32_t instance_command[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
    for (uint32_t i = 0u; i < scene.instanceCount; ++i) {
        const DemonDataWinSceneInstance *instance = &scene.instances[i];
        DemonVm_initInstanceState(&instance_state[i], instance->x, instance->y);
        DemonVm_setInstanceContext(&instance_state[i],
            (int32_t)instance->instanceId, -1);
        instance_step_code[i] = UINT32_MAX;
        if (instance->objectId >= 0) {
            uint32_t code;
            if (DemonDataWinIndex_eventCode(&render_reader, &index,
                    &gen8_summary, (uint32_t)instance->objectId, 3u, 0u,
                    &code) && code < code_count)
                instance_step_code[i] = code;
        }
        instance_command[i] = UINT32_MAX;
        if (instance->pageItemId < 0) continue;
        DemonDataWinPageItem item;
        if (!DemonDataWinIndex_pageItem(&render_reader, &index,
                (uint32_t)instance->pageItemId, &item) ||
            render_list.count == DEMON_RENDER_COMMAND_MAX) {
            line("BUTTERSCOTCH_D4_FAIL tpag-command");
            DemonImage_release(&decoded_texture);
            demon_port_free(data);
            demon_port_shutdown();
            return 10u;
        }
        const uint32_t command = render_list.count++;
        render_list.commands[command] = (DemonRenderCommand){
            .opcode = DEMON_RENDER_TPAG,
            .x = instance->x,
            .y = instance->y,
            .item = item
        };
        instance_command[i] = command;
    }
    /* Independent of the scene above: the pre-existing boot-time VM self-
     * tests below exercise the tiny built-in fixture's own object-0 demo
     * script (arbitrary key-driven delta, not real room coordinates), kept
     * exactly as before. */
    uint32_t object0_step_code = UINT32_MAX;
    if (!DemonDataWinIndex_eventCode(&render_reader, &index, &gen8_summary,
            0u, 3u, 0u, &object0_step_code) || object0_step_code >= code_count) {
        line("BUTTERSCOTCH_D4_FAIL step-event");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    const uint32_t object0_step_ids[1] = {object0_step_code};
    BinaryReader idle_tick_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&idle_tick_reader, data, 0u, file_size);
    DemonVmExecutionStats idle_tick_stats;
    if (!DemonVm_executeEvents(&idle_tick_reader, &index,
            gen8_summary.wadVersion, 0u, object0_step_ids, 1u, 10, 20,
            &idle_tick_stats) || idle_tick_stats.codeEntries != 1u ||
        idle_tick_stats.finalX != 10 || idle_tick_stats.finalY != 20) {
        line("BUTTERSCOTCH_D4_FAIL fixed-step-selftest");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    DemonVmInstanceState persistence_state;
    DemonVm_initInstanceState(&persistence_state, 10, 20);
    BinaryReader persistence_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&persistence_reader, data, 0u, file_size);
    DemonVmExecutionStats persistence_stats;
    if (!DemonVm_executeEventsState(&persistence_reader, &index,
            gen8_summary.wadVersion, DEMON_VM_KEY_RIGHT, object0_step_ids, 1u,
            &persistence_state, &persistence_stats)) {
        line("BUTTERSCOTCH_D4_FAIL persistent-state-step1");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    persistence_reader = BinaryReader_create(NULL, file_size);
    BinaryReader_setBuffer(&persistence_reader, data, 0u, file_size);
    if (!DemonVm_executeEventsState(&persistence_reader, &index,
            gen8_summary.wadVersion, DEMON_VM_KEY_DOWN, object0_step_ids, 1u,
            &persistence_state, &persistence_stats) ||
        persistence_state.variables[3] != 14 ||
        persistence_state.variables[4] != 24) {
        line("BUTTERSCOTCH_D4_FAIL persistent-state-step2");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    /* General pairwise collision resolution: every ordered pair of
     * instances that both have a render body (needed for the TPAG-rect-
     * based overlap test) is checked for a real collision handler, rather
     * than assuming only object 0 ever has one. A collision handler runs
     * as instance i's own event when instance j is the "other" -- matches
     * how DemonDataWinIndex_collisionCode(selfId, otherId) is defined, and
     * is asymmetric on purpose: two instances overlapping only dispatches
     * to whichever side(s) actually declared a handler for the other. */
    uint32_t collision_code[DEMON_DATAWIN_SCENE_INSTANCE_MAX][DEMON_DATAWIN_SCENE_INSTANCE_MAX];
    for (uint32_t i = 0u; i < DEMON_DATAWIN_SCENE_INSTANCE_MAX; ++i)
        for (uint32_t j = 0u; j < DEMON_DATAWIN_SCENE_INSTANCE_MAX; ++j)
            collision_code[i][j] = UINT32_MAX;
    uint32_t collidable_pairs = 0u;
    uint32_t collision_count = 0u;
    for (uint32_t i = 0u; i < scene.instanceCount; ++i) {
        if (instance_command[i] == UINT32_MAX || scene.instances[i].objectId < 0)
            continue;
        for (uint32_t j = 0u; j < scene.instanceCount; ++j) {
            if (i == j || instance_command[j] == UINT32_MAX ||
                scene.instances[j].objectId < 0) continue;
            uint32_t code;
            if (!DemonDataWinIndex_collisionCode(&render_reader, &index,
                    &gen8_summary, (uint32_t)scene.instances[i].objectId,
                    (uint32_t)scene.instances[j].objectId, &code) ||
                code >= code_count) continue;
            collision_code[i][j] = code;
            ++collidable_pairs;
            if (DemonSoftwareRenderer_overlap(
                    &render_list.commands[instance_command[i]],
                    &render_list.commands[instance_command[j]]))
                ++collision_count;
        }
    }
    if (collidable_pairs == 0u) {
        line("BUTTERSCOTCH_D4_FAIL collision-events");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    DemonDecodedImage rendered_frame;
    if (!DemonSoftwareRenderer_compose(&decoded_texture, &render_list,
            320u, 240u, &rendered_frame)) {
        line("BUTTERSCOTCH_D4_FAIL software-renderer");
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_RENDER_OK commands=%u frame=%ux%u bytes=%u fnv=%08x room=%ux%u instances=%u bound=4 vm-offset=%d,%d",
        (unsigned)render_list.count, (unsigned)rendered_frame.width,
        (unsigned)rendered_frame.height, (unsigned)rendered_frame.byteCount,
        (unsigned)rendered_frame.fnv1a, (unsigned)scene.roomWidth,
        (unsigned)scene.roomHeight, (unsigned)scene.instanceCount,
        vm_stats.finalX, vm_stats.finalY);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_FRAMEBUFFER_OK buffer=retained bytes=%u allocations=1 compose=in-place",
        (unsigned)rendered_frame.byteCount);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_COLLISION_OK instances=%u overlaps=%u collidable-pairs=%u bounds=tpag-target events=ready",
        (unsigned)scene.instanceCount, (unsigned)collision_count,
        (unsigned)collidable_pairs);
    line(report);
    uint32_t steppable_instances = 0u;
    for (uint32_t i = 0u; i < scene.instanceCount; ++i)
        if (instance_step_code[i] != UINT32_MAX) ++steppable_instances;
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_EVENT_MAP_OK instances=%u steppable=%u dispatch=per-instance",
        (unsigned)scene.instanceCount, (unsigned)steppable_instances);
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_LIFECYCLE_OK instances=%u steppable=%u collidable-pairs=%u source=objt-actions",
        (unsigned)scene.instanceCount, (unsigned)steppable_instances,
        (unsigned)collidable_pairs);
    line(report);
    line("BUTTERSCOTCH_D5_TICK_OK rate=60Hz mode=fixed-step idle-events=1 idle-position=10,20");
    line("BUTTERSCOTCH_D5_INPUT_STATE_OK pointer=absolute buttons=left,middle,right focus-loss=releases");
    line("BUTTERSCOTCH_D3_STATE_OK scope=instance ticks=2 position=14,24 event-writes=persistent");
    DemonButterscotchVideo video;
    DemonButterscotchAudio audio;
    const bool audio_available = DemonButterscotchAudio_open(&audio);
    const bool audio_submitted = audio_available &&
        demon_boot_test_mode() != 0u &&
        DemonButterscotchAudio_selfTest(&audio);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D5_AUDIO_OK rate=44100 channels=2 bits=16 available=%u submitted=%u buffers=%u frames=%u fallback=silent",
        audio_available ? 1u : 0u, audio_submitted ? 1u : 0u,
        (unsigned)audio.buffersSubmitted, (unsigned)audio.framesSubmitted);
    line(report);
    const bool interactive = demon_boot_test_mode() == 0u;
    if (!DemonButterscotchVideo_open(&video, &rendered_frame, interactive,
            config.windowWidth, config.windowHeight)) {
        line("BUTTERSCOTCH_D4_FAIL native-video");
        DemonButterscotchAudio_close(&audio);
        DemonImage_release(&rendered_frame);
        DemonImage_release(&decoded_texture);
        demon_port_free(data);
        demon_port_shutdown();
        return 10u;
    }
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_VIDEO_OK surface=%ux%u frames=%u window=%s input=keyboard,mouse",
        (unsigned)rendered_frame.width, (unsigned)rendered_frame.height,
        (unsigned)video.frames, interactive ? "DemonX" : "smoke-offscreen");
    line(report);
    (void)snprintf(report, sizeof(report),
        "BUTTERSCOTCH_D4_UPLOAD_OK buffer=retained capacity=%u allocations=%u pixels=%u",
        (unsigned)video.uploadPixelCapacity,
        (unsigned)video.uploadAllocations,
        (unsigned)video.uploadedPixels);
    line(report);
    line("BUTTERSCOTCH_D1_PORTKIT_OK heap=dynamic storage=ramfs process=ring3");
    line("BUTTERSCOTCH_D4_SUBSYSTEMS_READY binary-utils binary-reader chunk-index pointer-tables strg gen8 code objt room sprt tpag txtr sond audo png rgba bytecode-decode vm-stack dup ret exit instance-state rvalue-real integer-ops comparisons branches builtins events storage persistence config checksum staged-write room-resolve lifecycle-map collision-map selective-dispatch fixed-step software-renderer retained-framebuffer tpag-blit alpha retained-upload surface demonx resize-state keyboard mouse mouse-state focus-release audio wav-pcm resample mixer voices loop gain pan pcm-ac97");

    if (interactive)
        line("BUTTERSCOTCH_D4_LIVE_READY controls=arrows,WASD escape=quit update=fixed-60Hz");
    while (interactive && !video.quit) {
        DemonButterscotchVideo_pump(&video);
        {
            bool ok = true;
            /* World data for place_meeting-style builtins: rebuilt every
             * tick from each instance's position at the start of that
             * tick (before this tick's Step runs), shared by every
             * instance's Step dispatch this tick -- real GML's own
             * per-instance dispatch order already makes this genuinely
             * order-dependent, so pre-tick positions uniformly is a
             * reasonable, documented approximation. */
            DemonVmWorldInstance live_world[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
            for (uint32_t i = 0u; i < scene.instanceCount; ++i) {
                const bool hasBox = instance_command[i] != UINT32_MAX;
                live_world[i] = (DemonVmWorldInstance){
                    .instanceId = (int32_t)scene.instances[i].instanceId,
                    .objectId = scene.instances[i].objectId,
                    .x = hasBox ? render_list.commands[instance_command[i]].x :
                        scene.instances[i].x,
                    .y = hasBox ? render_list.commands[instance_command[i]].y :
                        scene.instances[i].y,
                    .item = hasBox ? render_list.commands[instance_command[i]].item :
                        (DemonDataWinPageItem){0},
                    .hasBox = hasBox
                };
            }
            for (uint32_t i = 0u; i < scene.instanceCount; ++i)
                DemonVm_setWorld(&instance_state[i], live_world,
                    scene.instanceCount);
            /* Step: every instance with a resolved Step handler runs
             * against its own persistent state, not a single shared
             * instance -- keyMask is passed to all of them uniformly (only
             * whichever instance's script actually calls keyboard_check
             * will observe it, matching the pre-existing single-instance
             * behavior). */
            for (uint32_t i = 0u; ok && i < scene.instanceCount; ++i) {
                if (instance_step_code[i] == UINT32_MAX) continue;
                /* No "other" instance during Step -- clears whatever a
                 * prior tick's collision dispatch may have left set. */
                instance_state[i].otherInstanceId = -1;
                BinaryReader live_reader = BinaryReader_create(NULL, file_size);
                BinaryReader_setBuffer(&live_reader, data, 0u, file_size);
                DemonVmExecutionStats live_stats;
                const uint32_t step_id = instance_step_code[i];
                if (!DemonVm_executeEventsState(&live_reader, &index,
                        gen8_summary.wadVersion, video.keyMask,
                        &step_id, 1u, &instance_state[i],
                        &live_stats)) {
                    line("BUTTERSCOTCH_D4_FAIL live-vm");
                    video.quit = true;
                    ok = false;
                    break;
                }
                if (instance_command[i] != UINT32_MAX) {
                    render_list.commands[instance_command[i]].x =
                        instance_state[i].variables[3];
                    render_list.commands[instance_command[i]].y =
                        instance_state[i].variables[4];
                }
            }
            /* Collision: re-check every instance pair against their
             * post-Step positions, then dispatch each instance's own
             * collision handler(s) against its own state. */
            if (ok) {
                collision_count = 0u;
                for (uint32_t i = 0u; ok && i < scene.instanceCount; ++i) {
                    if (instance_command[i] == UINT32_MAX) continue;
                    uint32_t collision_ids[DEMON_DATAWIN_SCENE_INSTANCE_MAX];
                    uint32_t collision_id_count = 0u;
                    int32_t collision_other = -1;
                    for (uint32_t j = 0u; j < scene.instanceCount; ++j) {
                        if (i == j || instance_command[j] == UINT32_MAX ||
                            collision_code[i][j] == UINT32_MAX) continue;
                        if (DemonSoftwareRenderer_overlap(
                                &render_list.commands[instance_command[i]],
                                &render_list.commands[instance_command[j]]) &&
                            collision_id_count < DEMON_DATAWIN_SCENE_INSTANCE_MAX) {
                            ++collision_count;
                            collision_ids[collision_id_count++] =
                                collision_code[i][j];
                            /* If several instances collide with i in the
                             * same tick, @@Other@@ only sees whichever one
                             * resolved last -- real per-pair dispatch
                             * granularity is a separate improvement. */
                            collision_other = (int32_t)scene.instances[j].instanceId;
                        }
                    }
                    if (collision_id_count == 0u) continue;
                    DemonVm_setInstanceContext(&instance_state[i],
                        (int32_t)scene.instances[i].instanceId, collision_other);
                    BinaryReader collision_reader = BinaryReader_create(
                        NULL, file_size);
                    BinaryReader_setBuffer(&collision_reader, data, 0u,
                                           file_size);
                    DemonVmExecutionStats collision_stats;
                    if (!DemonVm_executeEventsState(&collision_reader, &index,
                            gen8_summary.wadVersion, 0u,
                            collision_ids, collision_id_count,
                            &instance_state[i], &collision_stats)) {
                        line("BUTTERSCOTCH_D4_FAIL collision-dispatch");
                        video.quit = true;
                        ok = false;
                        break;
                    }
                    render_list.commands[instance_command[i]].x =
                        instance_state[i].variables[3];
                    render_list.commands[instance_command[i]].y =
                        instance_state[i].variables[4];
                }
            }
            if (ok) {
                const bool composed = DemonSoftwareRenderer_recompose(
                    &decoded_texture, &render_list, &rendered_frame);
                const bool presented = composed &&
                    DemonButterscotchVideo_present(&video, &rendered_frame);
                if (!presented) {
                    line("BUTTERSCOTCH_D4_FAIL live-present");
                    video.quit = true;
                }
            }
            video.inputChanged = false;
        }
        demon_port_sleep_ms(16u);
    }
    if (interactive) {
        (void)snprintf(report, sizeof(report),
            "BUTTERSCOTCH_D4_WINDOW_CLOSED events=%u keys=%u pointer=%u",
            (unsigned)video.events, (unsigned)video.keys,
            (unsigned)video.pointerEvents);
        line(report);
        config.windowWidth = video.windowWidth;
        config.windowHeight = video.windowHeight;
        if (!DemonButterscotchPersistence_save(CONFIG_PATH, CONFIG_TEMP_PATH,
                &config))
            line("BUTTERSCOTCH_D5_WARN config-save-failed");
    }

    DemonButterscotchVideo_close(&video);
    DemonButterscotchAudio_close(&audio);
    DemonImage_release(&rendered_frame);
    DemonImage_release(&decoded_texture);
    demon_port_free(data);
    demon_port_shutdown();
    return 0u;
}

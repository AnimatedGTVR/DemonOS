#include "doomgeneric.h"
#include "doomgeneric_demonos.h"
#include "doomstat.h"
#include "i_system.h"

#include <demon/c_app.h>
#include <demon/portkit.h>
#include <stdint.h>
#include <stdio.h>

#define DOOM_HEAP_BYTES (24u * 1024u * 1024u)
#define DOOM_WIDTH 320u
#define DOOM_HEIGHT 200u
#define DEMON_INVALID_HANDLE UINT64_MAX

struct doom_display_info {
    uint64_t width;
    uint64_t height;
    uint64_t stride_pixels;
    uint64_t format;
    uint64_t max_transfer_pixels;
};

struct doom_display_submit {
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
    uint64_t pixels;
    uint64_t flags;
};

struct doom_video {
    uint64_t display;
    uint64_t surface_factory;
    uint64_t surface;
    struct doom_display_info info;
    uint64_t frames;
    uint32_t *scaled_pixels;
    uint32_t scale;
    uint8_t game_ready;
    uint8_t automated_exit;
};

static void doom_present(const uint32_t *pixels, uint32_t width,
                         uint32_t height, void *opaque) {
    struct doom_video *video = (struct doom_video *)opaque;
    if (video == 0 || pixels == 0 || width != DOOM_WIDTH ||
        height != DOOM_HEIGHT) return;

    /* doomgeneric produces 0x00RRGGBB. DemonOS surfaces use canonical
       0xAARRGGBB, so make every engine pixel opaque before publishing it. */
    uint32_t *argb = (uint32_t *)(uintptr_t)pixels;
    for (uint32_t i = 0u; i < width * height; ++i) argb[i] |= 0xFF000000u;

    if (video->surface != DEMON_INVALID_HANDLE) {
        if (demon_surface_write(video->surface, argb,
                                (uint64_t)width * height, 0u) !=
            (uint64_t)width * height) return;
        (void)demon_surface_damage(video->surface, 0u, 0u, width, height);
    }

    uint32_t *present_pixels = argb;
    uint32_t present_width = width;
    uint32_t present_height = height;
    if (video->scale > 1u && video->scaled_pixels != 0) {
        present_width = width * video->scale;
        present_height = height * video->scale;
        for (uint32_t source_y = 0u; source_y < height; ++source_y) {
            for (uint32_t repeat_y = 0u; repeat_y < video->scale; ++repeat_y) {
                uint32_t *destination = video->scaled_pixels +
                    (source_y * video->scale + repeat_y) * present_width;
                for (uint32_t source_x = 0u; source_x < width; ++source_x) {
                    const uint32_t pixel = argb[source_y * width + source_x];
                    for (uint32_t repeat_x = 0u; repeat_x < video->scale;
                         ++repeat_x) {
                        destination[source_x * video->scale + repeat_x] = pixel;
                    }
                }
            }
        }
        present_pixels = video->scaled_pixels;
    }

    const struct doom_display_submit request = {
        .x = video->info.width > present_width ?
            (video->info.width - present_width) / 2u : 0u,
        .y = video->info.height > present_height ?
            (video->info.height - present_height) / 2u : 0u,
        .width = present_width,
        .height = present_height,
        .pixels = (uint64_t)(uintptr_t)present_pixels,
        .flags = 1u,
    };
    if (demon_display_submit(video->display, &request) != 0u) return;
    ++video->frames;
    if (video->frames == 1u) {
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0u; i < width * height; ++i) {
            hash ^= argb[i];
            hash *= 16777619u;
        }
        printf("FREEDOOM_FIRST_FRAME_READY 320x200 ARGB hash=%08x\n",
               (unsigned)hash);
        printf("FREEDOOM_TITLE_READY hash=%08x\n", (unsigned)hash);
        printf("FREEDOOM_DISPLAY_SCALE scale=%u output=%ux%u\n",
               (unsigned)video->scale, (unsigned)present_width,
               (unsigned)present_height);
    }
    if (video->frames == 300u) {
        const struct demon_port_runtime *runtime = demon_port_status();
        printf("FREEDOOM_300_FRAMES_OK surface=%u heap_peak=%u\n",
               (unsigned)video->surface, (unsigned)runtime->heap_peak);
    }
    if (!video->game_ready && gamestate == GS_LEVEL) {
        video->game_ready = 1u;
        printf("FREEDOOM_GAME_READY frame=%u episode=%u map=%u\n",
               (unsigned)video->frames, (unsigned)gameepisode,
               (unsigned)gamemap);
    }
    if (video->automated_exit && video->frames == 360u) {
        printf("FREEDOOM_AUTOMATED_EXIT frame=%u\n", (unsigned)video->frames);
        I_Quit();
    }
}

static void doom_set_title(const char *title, void *opaque) {
    (void)title;
    (void)opaque;
}

uint64_t doom_main(void) {
    const uint64_t test_mode = demon_boot_test_mode();
    if (test_mode == 1u) {
        demon_port_write("DOOM_FULL_IMAGE_MAPPED pages=149 base=0x20000000\n");
        demon_port_write("DOOM_ENGINE_READY\n");
        return 0u;
    }
    if (!demon_port_init_dynamic(DOOM_HEAP_BYTES)) return 10u;
    char program[] = "doom";
    char iwad_option[] = "-iwad";
    char iwad_path[] = "/games/freedoom/freedoom1.wad";
    char no_sound[] = "-nosound";
    char warp_option[] = "-warp";
    char episode[] = "1";
    char map[] = "1";
    char *arguments[] = {program, iwad_option, iwad_path, no_sound,
                         warp_option, episode, map, 0};
    const int argument_count = test_mode == 2u ? 7 : 4;
    struct doom_video video = {
        .display = demon_service_open(7u),
        .surface_factory = demon_service_open(9u),
        .surface = DEMON_INVALID_HANDLE,
        .info = {0u, 0u, 0u, 0u, 0u},
        .frames = 0u,
        .scaled_pixels = 0,
        .scale = 1u,
        .game_ready = 0u,
        .automated_exit = test_mode == 2u,
    };
    if (video.display == DEMON_INVALID_HANDLE ||
        video.surface_factory == DEMON_INVALID_HANDLE ||
        demon_display_info(video.display, &video.info) == DEMON_INVALID_HANDLE)
        return 11u;
    uint64_t scale = video.info.width / DOOM_WIDTH;
    const uint64_t vertical_scale = video.info.height / DOOM_HEIGHT;
    if (vertical_scale < scale) scale = vertical_scale;
    if (scale > 3u) scale = 3u;
    /* The display syscall chunks frames into bounce-buffer-sized row groups;
       only one scaled scanline must fit in a transfer, not the whole frame. */
    while (scale > 1u && (uint64_t)DOOM_WIDTH * scale >
                              video.info.max_transfer_pixels) --scale;
    if (scale > 1u) {
        const uint64_t scaled_count =
            (uint64_t)DOOM_WIDTH * scale * DOOM_HEIGHT * scale;
        video.scaled_pixels = demon_port_malloc(
            (size_t)scaled_count * sizeof(*video.scaled_pixels));
        if (video.scaled_pixels != 0) video.scale = (uint32_t)scale;
    }
    video.surface = demon_surface_create(video.surface_factory,
                                         DOOM_WIDTH, DOOM_HEIGHT);
    if (video.surface == DEMON_INVALID_HANDLE) return 12u;
    const struct demon_doom_backend backend = {
        .present = doom_present,
        .set_title = doom_set_title,
        .context = &video,
    };
    demon_doom_install_backend(&backend);
    demon_port_write("DOOM_FULL_ENGINE_START\n");
    doomgeneric_Create(argument_count, arguments);
    for (;;) doomgeneric_Tick();
    return 0u;
}

#include "doomgeneric.h"
#include "doomgeneric_demonos.h"
#include "doomstat.h"
#include "i_system.h"
#include "s_sound.h"
#include "sounds.h"

#include <demon/c_app.h>
#include <demon/portkit.h>
#include <demon/demonx.h>
#include <X11/Xlib.h>
#include <stdint.h>
#include <stdio.h>

#define DOOM_HEAP_BYTES (24u * 1024u * 1024u)
#define DOOM_WIDTH 320u
#define DOOM_HEIGHT 200u
#define DOOM_WINDOW_WIDTH 480u
#define DOOM_WINDOW_HEIGHT 300u
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
    Display *x_display;
    Window x_window;
    uint8_t quit_requested;
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

    if (video->x_display != 0) {
        ++video->frames;
    } else {
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
    }
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
    if (video->automated_exit && video->frames == 10u)
        S_StartSound(0, sfx_pistol);
    if (video->automated_exit && video->frames == 360u) {
        printf("FREEDOOM_AUTOMATED_EXIT frame=%u\n", (unsigned)video->frames);
        I_Quit();
    }
}

static void doom_set_title(const char *title, void *opaque) {
    struct doom_video *video = (struct doom_video *)opaque;
    if (video != 0 && video->x_display != 0 && video->x_window != None)
        (void)XStoreName(video->x_display, video->x_window,
                         title != 0 ? title : "Freedoom");
}

static uint16_t doom_x_key_code(unsigned int code) {
    if (code == 0x48u || code == 0x4Bu || code == 0x4Du || code == 0x50u)
        return (uint16_t)(0x100u | code);
    return (uint16_t)code;
}

static int doom_poll_input(struct input_event *input, void *opaque) {
    struct doom_video *video = (struct doom_video *)opaque;
    if (video == 0 || input == 0) return 0;
    if (video->x_display == 0) return demon_port_poll_input(input);
    while (XPending(video->x_display)) {
        XEvent event;
        if (XNextEvent(video->x_display, &event) != 0) return 0;
        if (event.type == ClientMessage &&
            event.xclient.message_type == DEMONX_CLIENT_CLOSE_MESSAGE) {
            video->x_window = None;
            video->quit_requested = 1u;
            continue;
        }
        if (event.type != KeyPress && event.type != KeyRelease) continue;
        *input = (struct input_event){
            .type = event.type == KeyPress ? INPUT_KEY_DOWN : INPUT_KEY_UP,
            .code = doom_x_key_code(event.xkey.keycode),
            .value = event.xkey.value,
            .modifiers = event.xkey.state,
        };
        return 1;
    }
    return 0;
}

uint64_t doom_main(void) {
    const uint64_t test_mode = demon_boot_test_mode();
    if (test_mode == 1u) {
        demon_port_write("DOOM_FULL_IMAGE_MAPPED pages=149 base=0x20000000\n");
        demon_port_write("DOOM_ENGINE_READY\n");
        return 0u;
    }
    if (!demon_port_init_dynamic(DOOM_HEAP_BYTES)) {
        demon_port_write("DOOM_WINDOW_HEAP_FAILED\n");
        return 10u;
    }
    demon_port_write("DOOM_WINDOW_HEAP_READY\n");
    char program[] = "doom";
    char iwad_option[] = "-iwad";
    char iwad_path[] = "/games/freedoom/freedoom1.wad";
    char no_music[] = "-nomusic";
    char warp_option[] = "-warp";
    char episode[] = "1";
    char map[] = "1";
    char *arguments[] = {program, iwad_option, iwad_path, no_music,
                         warp_option, episode, map, 0};
    const int argument_count = test_mode == 2u ? 7 : 4;
    struct doom_video video = {
        .display = DEMON_INVALID_HANDLE,
        .surface_factory = demon_service_open(9u),
        .surface = DEMON_INVALID_HANDLE,
        .info = {0u, 0u, 0u, 0u, 0u},
        .frames = 0u,
        .scaled_pixels = 0,
        .scale = 1u,
        .game_ready = 0u,
        .automated_exit = test_mode == 2u,
        .x_display = XOpenDisplay(":5"),
        .x_window = None,
        .quit_requested = 0u,
    };
    if (video.surface_factory == DEMON_INVALID_HANDLE) {
        demon_port_write("DOOM_WINDOW_SURFACE_SERVICE_FAILED\n");
        return 11u;
    }
    if (video.x_display != 0) {
        video.info.width = (uint64_t)XDisplayWidth(video.x_display, 0);
        video.info.height = (uint64_t)XDisplayHeight(video.x_display, 0);
        video.info.max_transfer_pixels = DOOM_WIDTH;
    } else {
        video.display = demon_service_open(7u);
        if (video.display == DEMON_INVALID_HANDLE ||
            demon_display_info(video.display, &video.info) == DEMON_INVALID_HANDLE) {
            demon_port_write("DOOM_WINDOW_DEMONX_CONNECT_FAILED\n");
            return 11u;
        }
    }
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
    if (video.surface == DEMON_INVALID_HANDLE) {
        demon_port_write("DOOM_WINDOW_SURFACE_CREATE_FAILED\n");
        return 12u;
    }
    if (video.x_display != 0) {
        video.scale = 1u;
        video.x_window = XCreateSimpleWindow(video.x_display,
            XDefaultRootWindow(video.x_display), 80, 100,
            DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT,
            0u, 0u, 0xff000000u);
        if (video.x_window == None ||
            !DemonXAttachSurface(video.x_display, video.x_window,
                                 video.surface, DOOM_WIDTH, DOOM_HEIGHT)) {
            demon_port_write("DOOM_WINDOW_ATTACH_FAILED\n");
            return 13u;
        }
        (void)XStoreName(video.x_display, video.x_window, "Freedoom");
        (void)XSelectInput(video.x_display, video.x_window,
            KeyPressMask | KeyReleaseMask | FocusChangeMask |
            StructureNotifyMask);
        (void)XMapWindow(video.x_display, video.x_window);
        demon_port_write("DOOM_WINDOW_READY surface=320x200 window=480x300\n");
    }
    const struct demon_doom_backend backend = {
        .present = doom_present,
        .set_title = doom_set_title,
        .poll_input = doom_poll_input,
        .context = &video,
    };
    demon_doom_install_backend(&backend);
    demon_port_write("DOOM_FULL_ENGINE_START\n");
    doomgeneric_Create(argument_count, arguments);
    for (;;) {
        doomgeneric_Tick();
        if (video.quit_requested) {
            demon_port_write("FREEDOOM_WINDOW_CLOSE\n");
            I_Quit();
        }
    }
    return 0u;
}

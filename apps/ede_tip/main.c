// Wave 3 of the EDE port: ede-tip, ported off
// Desktop/EDE/ede-2.1/ede-tip/ede-tip.cpp. The original reads a compiled
// "fortune"-format tip database from disk (Fortune.cpp/ede-tip-compiler.c)
// and persists a "show tips on startup" checkbox into an XDG autostart
// .desktop file. Neither a fortune-format reader nor an XDG-style user
// config directory exists on this kernel, so the tip text is a small
// hardcoded table (about this OS instead of generic EDE trivia -- more
// useful to someone actually using it) and the startup checkbox is
// dropped rather than faked with fake persistence. PREV/NEXT/CLOSE and the
// tip-cycling logic itself are a faithful, direct port.
//
// Plain C like ede-about: no floating point, normal (non-float) CFLAGS,
// same ~49152-byte generic-app process budget.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 260
#define WIN_Y 90
#define TITLEBAR 32

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

#define TIP_COUNT 6
static const char *const tips[TIP_COUNT][3] = {
    {"MAKO-ABI USES", "CAPABILITIES NOT", "POSIX."},
    {"KERNEL ZEROS", "EVERY NEW", "PROCESS IMAGE."},
    {"EDE-CALC HAS A", "FREESTANDING", "LIBM WE WROTE."},
    {"CLICK DEMON TO", "OPEN THE APP", "LAUNCHER."},
    {"TYPE HELP IN", "TERMINAL FOR", "MAKOBOX CMDS."},
    {"THIS DESKTOP", "RUNS ON MAKO,", "A CUSTOM LANG."},
};

static int current_tip = 0;

static const struct graphics_rect prev_button = {2, 76, 34, 18};
static const struct graphics_rect next_button = {38, 76, 34, 18};
static const struct graphics_rect close_button = {74, 76, 32, 18};

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 8}, 2u, 0xFF2479D8u);
    graphics_text(&surface, 4, 2, "DID YOU KNOW", 1u, 0xFFF8FAFCu);

    for (unsigned line = 0; line < 3u; ++line)
        graphics_text(&surface, 4, 14 + (int32_t)(line * 8u), tips[current_tip][line], 1u, 0xFFB8C4D4u);

    graphics_rounded_rect(&surface, prev_button, 2u, 0xFF2A3B52u);
    graphics_text(&surface, prev_button.x + 4, prev_button.y + 6, "PV", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, next_button, 2u, 0xFF2A3B52u);
    graphics_text(&surface, next_button.x + 3, next_button.y + 6, "NX", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, close_button, 2u, 0xFFE05260u);
    graphics_text(&surface, close_button.x + 2, close_button.y + 6, "CL", 1u, 0xFFF8FAFCu);
}

static int hit(const struct graphics_rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

uint64_t ede_tip_main(void) {
    const uint64_t pid = demon_getpid();
    event_name[11] = (char)('0' + pid % 10u);
    const uint64_t events = demon_channel_create(event_name, 12u);
    const uint64_t compositor =
        demon_channel_connect(DEMON_WINDOW_SERVICE, sizeof(DEMON_WINDOW_SERVICE) - 1u);
    const uint64_t factory = demon_service_open(9u);
    const uint64_t surface_handle = demon_surface_create(factory, W, H);
    if (events > UINT32_MAX || compositor > UINT32_MAX ||
        factory > UINT32_MAX || surface_handle > UINT32_MAX)
        return 1u;

    if (!graphics_surface_init(&surface, pixels, W, H, W)) return 2u;

    // No rand()/srand() here (no libc); demon_ticks() at startup is enough
    // entropy to not always open on the same tip.
    current_tip = (int)(demon_ticks() % (uint64_t)TIP_COUNT);
    redraw();
    if (demon_surface_write(surface_handle, pixels, W * H, 0u) != W * H) return 3u;
    demon_surface_damage(surface_handle, 0u, 0u, W, H);

    packet = (struct demon_window_message){
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_CREATE,
        .serial = 1u,
        .window_id = (uint32_t)pid,
        .flags = 0u,
        .x = WIN_X, .y = WIN_Y, .width = W, .height = H,
        .surface_id = (uint32_t)demon_surface_share(surface_handle, compositor),
    };
    if (demon_channel_send(compositor, &packet, sizeof(packet)) != sizeof(packet))
        return 4u;
    demon_handle_close(compositor);
    demon_handle_close(factory);

    for (;;) {
        if (demon_channel_receive(events, &packet, sizeof(packet), 0u) != sizeof(packet))
            return 0u;
        if (packet.version != DEMON_WINDOW_PROTOCOL_VERSION || packet.window_id != pid)
            continue;
        if (packet.opcode == DEMON_WINDOW_CLOSE) return 0u;
        if (packet.opcode == 11u) {
            const int local_x = packet.x > WIN_X ? (int)packet.x - WIN_X : -1;
            const int local_y = packet.y > WIN_Y + TITLEBAR ? (int)packet.y - (WIN_Y + TITLEBAR) : -1;
            if (hit(&close_button, local_x, local_y)) return 0u;
            if (hit(&next_button, local_x, local_y)) {
                current_tip++;
                if (current_tip >= TIP_COUNT) current_tip = 0;
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
            } else if (hit(&prev_button, local_x, local_y)) {
                current_tip--;
                if (current_tip < 0) current_tip = TIP_COUNT - 1;
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
            }
        }
    }
}

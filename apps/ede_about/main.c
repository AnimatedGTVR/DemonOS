// Wave 2 of the EDE port: ede-about, ported off
// Desktop/EDE/ede-2.1/ede-about/ede-about.cpp. The original is a static
// FLTK Fl_Text_Display showing developer/license text plus a close button
// -- no real engine logic, so unlike ede-calc this is a straight redraw of
// the same content on this kernel's own window/graphics primitives. Plain
// C like apps/calculator/main.c: no floating point anywhere in this app,
// so it needs none of ede-calc's FPU/libm machinery and builds with the
// kernel's normal (non-float) CFLAGS.
//
// Kept to the bitmap font's actual glyph set (space/-/./: /digits/
// uppercase A-Z -- libs/graphics/graphics.c's rows_for()) and to the same
// ~49152-byte "generic app" process budget every launcher-spawned app
// shares (see kernel.c's per-process code_pages and the session notes on
// why that budget isn't easily raised), so the original's full
// developer/contributor/translator credits are condensed to a short
// summary rather than reproduced verbatim.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 220
#define WIN_Y 90
#define TITLEBAR 32

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

static const struct graphics_rect close_button = {14, 72, 80, 18};

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 18}, 3u, 0xFF2479D8u);
    graphics_text(&surface, 6, 4, "EDE", 2u, 0xFFF8FAFCu);

    graphics_text(&surface, 4, 24, "EQUINOX", 1u, 0xFFB8C4D4u);
    graphics_text(&surface, 4, 32, "DESKTOP", 1u, 0xFFB8C4D4u);
    graphics_text(&surface, 4, 40, "ENVIRONMENT", 1u, 0xFFB8C4D4u);
    graphics_text(&surface, 4, 50, "WAVE 2 PORT", 1u, 0xFF6FE08Fu);
    graphics_text(&surface, 4, 58, "GNU GPL V2", 1u, 0xFF8892A0u);

    graphics_rounded_rect(&surface, close_button, 3u, 0xFFE05260u);
    graphics_text(&surface, close_button.x + 14, close_button.y + 6, "CLOSE", 1u, 0xFFF8FAFCu);
}

uint64_t ede_about_main(void) {
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
            if (local_x >= close_button.x && local_x < close_button.x + close_button.width &&
                local_y >= close_button.y && local_y < close_button.y + close_button.height)
                return 0u;
        }
    }
}

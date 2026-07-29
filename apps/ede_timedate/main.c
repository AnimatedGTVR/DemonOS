// Wave 8 of the EDE port: ede-timedate, ported off
// Desktop/EDE/ede-2.1/ede-timedate/ede-timedate.cpp -- originally a full
// wall-clock/calendar/NTP settings dialog (date picker, timezone list,
// hardware clock sync). None of that has a backend here: there is no RTC
// driver, no timezone database, no NTP client. What this kernel does have
// is a real monotonic PIT tick counter (demon_ticks(), ~100 Hz -- see
// src/arch/x86_64/interrupts.c's PIT divisor of 11932), so this is scaled
// down to what that can honestly support: a system uptime readout
// (HH:MM:SS since boot), refreshed on click since there is no periodic
// timer callback available to a plain event-driven app -- only redraws in
// response to a real window event.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 320
#define WIN_Y 100
#define TITLEBAR 32
#define TICKS_PER_SECOND 100u

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

static const struct graphics_rect refresh_button = {2, 50, 50, 20};
static const struct graphics_rect close_button = {56, 50, 50, 20};

static void append_digits2(char *out, unsigned *len, unsigned value) {
    out[(*len)++] = (char)('0' + (value / 10u) % 10u);
    out[(*len)++] = (char)('0' + value % 10u);
}

static void format_uptime(char *out) {
    const uint64_t ticks = demon_ticks();
    const uint64_t total_seconds = ticks / TICKS_PER_SECOND;
    const unsigned hours = (unsigned)((total_seconds / 3600u) % 100u); // caps display at 99h
    const unsigned minutes = (unsigned)((total_seconds / 60u) % 60u);
    const unsigned seconds = (unsigned)(total_seconds % 60u);
    unsigned len = 0;
    append_digits2(out, &len, hours);
    out[len++] = ':';
    append_digits2(out, &len, minutes);
    out[len++] = ':';
    append_digits2(out, &len, seconds);
    out[len] = '\0';
}

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 8}, 2u, 0xFF2479D8u);
    graphics_text(&surface, 4, 2, "UPTIME", 1u, 0xFFF8FAFCu);

    char uptime[16];
    format_uptime(uptime);
    graphics_text(&surface, 8, 24, uptime, 2u, 0xFF6FE08Fu);

    graphics_rounded_rect(&surface, refresh_button, 2u, 0xFF2A3B52u);
    graphics_text(&surface, refresh_button.x + 2, refresh_button.y + 6, "REFR", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, close_button, 2u, 0xFFE05260u);
    graphics_text(&surface, close_button.x + 4, close_button.y + 6, "CLOSE", 1u, 0xFFF8FAFCu);
}

static int hit(const struct graphics_rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

uint64_t ede_timedate_main(void) {
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
            if (hit(&close_button, local_x, local_y)) return 0u;
            if (hit(&refresh_button, local_x, local_y)) {
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
            }
        }
    }
}

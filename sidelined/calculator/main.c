#include <demon/c_app.h>
#include <demon/window.h>

#define W 128u
#define H 96u

static uint32_t pixels[W * H];
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

static void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color) {
    for (unsigned row = y; row < y + h && row < H; ++row)
        for (unsigned column = x; column < x + w && column < W; ++column)
            pixels[row * W + column] = color;
}

static void redraw(unsigned value, unsigned operation) {
    rect(0, 0, W, H, 0xFF101722u);
    rect(6, 6, 116, 20, 0xFF202D3Eu);
    /* A compact binary display keeps this freestanding C app font-free. */
    for (unsigned bit = 0; bit < 8; ++bit)
        rect(12 + bit * 13, 12, 9, 8,
             (value & (1u << bit)) ? 0xFF4CC2FFu : 0xFF34465Cu);
    const uint32_t colors[4] = {
        0xFF2479D8u, 0xFF35A768u, 0xFFE29B32u, 0xFFE05260u
    };
    for (unsigned button = 0; button < 4; ++button) {
        const unsigned x = 6 + (button & 1u) * 59;
        const unsigned y = 32 + (button >> 1u) * 29;
        rect(x, y, 55, 25, colors[button]);
        rect(x + 5, y + 5, 45, 15,
             button == operation ? 0xFFFFFFFFu : colors[button]);
    }
}

uint64_t calculator_main(void) {
    const uint64_t pid = demon_getpid();
    event_name[11] = (char)('0' + pid % 10u);
    const uint64_t events = demon_channel_create(event_name, 12u);
    const uint64_t compositor =
        demon_channel_connect(DEMON_WINDOW_SERVICE, sizeof(DEMON_WINDOW_SERVICE) - 1u);
    const uint64_t factory = demon_service_open(9u);
    const uint64_t surface = demon_surface_create(factory, W, H);
    if (events > UINT32_MAX || compositor > UINT32_MAX ||
        factory > UINT32_MAX || surface > UINT32_MAX) return 1u;

    unsigned value = 0u, operation = 0u;
    redraw(value, operation);
    if (demon_surface_write(surface, pixels, W * H, 0u) != W * H) return 2u;
    demon_surface_damage(surface, 0u, 0u, W, H);

    packet = (struct demon_window_message){
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_CREATE,
        .serial = 1u,
        .window_id = (uint32_t)pid,
        .flags = DEMON_WINDOW_RESIZABLE,
        .x = 172, .y = 80, .width = 296, .height = 180,
        .surface_id = (uint32_t)demon_surface_share(surface, compositor),
    };
    if (demon_channel_send(compositor, &packet, sizeof(packet)) != sizeof(packet))
        return 3u;
    demon_handle_close(compositor);
    demon_handle_close(factory);

    for (;;) {
        if (demon_channel_receive(events, &packet, sizeof(packet), 0u) !=
            sizeof(packet)) return 0u;
        if (packet.version != DEMON_WINDOW_PROTOCOL_VERSION ||
            packet.window_id != pid) continue;
        if (packet.opcode == DEMON_WINDOW_CLOSE) return 0u;
        if (packet.opcode == 11u) {
            /* Four real operations: increment, double, halve, and clear. */
            const unsigned local_x = packet.x > 172 ? (unsigned)packet.x - 172u : 0u;
            const unsigned local_y = packet.y > 112 ? (unsigned)packet.y - 112u : 0u;
            const unsigned button = (local_x >= 148u ? 1u : 0u) +
                (local_y >= 90u ? 2u : 0u);
            operation = button;
            if (button == 0u) ++value;
            else if (button == 1u) value *= 2u;
            else if (button == 2u) value /= 2u;
            else value = 0u;
            redraw(value, operation);
            demon_surface_write(surface, pixels, W * H, 0u);
            demon_surface_damage(surface, 0u, 0u, W, H);
        }
    }
}

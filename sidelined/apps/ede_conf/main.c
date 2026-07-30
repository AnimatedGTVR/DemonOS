// Wave 5 of the EDE port: ede-conf, ported off
// Desktop/EDE/ede-2.1/ede-conf/ede-conf.cpp -- a control panel of buttons
// that each launch another EDE utility ("run_async("ede-launch %s", ...)"
// in the original, via a config-file-driven button list).
//
// Waves 1-4 all had to drop their "launch/save" actions because no
// userspace syscall for spawning another process or touching RAMFS was
// exposed to plain C apps -- only kernel-internal code and hand-rolled MKO
// (compositor.mko/browser_client.mko) could do it. This wave adds the
// missing wrappers (demon_spawn/demon_file_open/demon_handle_read/
// demon_handle_write in demon/c_app.h) instead of dropping the feature
// again, so unlike ede-about/ede-tip/ede-preferred-applications, clicking a
// button here really does launch the target app -- the first genuinely
// complete port in this series. The button list is hardcoded (this OS's
// app catalog, not a config file) rather than reading a resource file.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 340
#define WIN_Y 140
#define TITLEBAR 32

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

// compositor.mko's spawn_app() grants IPC (bit 6) + SURFACE (bit 9) = 576
// to every launcher-spawned window; this also adds STORAGE (bit 4) = 16 so
// apps ede-conf launches -- namely ede-preferred (Wave 6) -- can persist
// settings to RAMFS via demon_file_open/handle_read/handle_write.
#define LAUNCH_SERVICE_MASK 592u

struct panel_entry {
    const char *label;
    const char *path;
};

#define ENTRY_COUNT 4
static const struct panel_entry entries[ENTRY_COUNT] = {
    {"ABOUT", "/system/bin/ede-about.elf"},
    {"TIP", "/system/bin/ede-tip.elf"},
    {"CALC", "/system/bin/ede-preferred.elf"},
    {"AUTO", "/system/bin/ede-autostart.elf"},
};

static const struct graphics_rect cells[ENTRY_COUNT + 1] = {
    {2, 12, 50, 20},
    {56, 12, 50, 20},
    {2, 34, 50, 20},
    {56, 34, 50, 20},
    {2, 58, 104, 20}, // close button, full width
};

static const char *status = "";

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 8}, 2u, 0xFF2479D8u);
    graphics_text(&surface, 4, 2, "EDE CONF", 1u, 0xFFF8FAFCu);

    for (unsigned i = 0; i < ENTRY_COUNT; ++i) {
        graphics_rounded_rect(&surface, cells[i], 3u, 0xFF2A3B52u);
        graphics_text(&surface, cells[i].x + 6, cells[i].y + 6, entries[i].label, 1u, 0xFFF8FAFCu);
    }
    graphics_rounded_rect(&surface, cells[ENTRY_COUNT], 3u, 0xFFE05260u);
    graphics_text(&surface, cells[ENTRY_COUNT].x + 42, cells[ENTRY_COUNT].y + 6, "CLOSE", 1u, 0xFFF8FAFCu);

    if (status[0] != '\0')
        graphics_text(&surface, 4, 80, status, 1u, 0xFF6FE08Fu);
}

static int hit(const struct graphics_rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

uint64_t ede_conf_main(void) {
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
            if (hit(&cells[ENTRY_COUNT], local_x, local_y)) return 0u;
            for (unsigned i = 0; i < ENTRY_COUNT; ++i) {
                if (!hit(&cells[i], local_x, local_y)) continue;
                uint64_t path_length = 0u;
                while (entries[i].path[path_length] != '\0') ++path_length;
                const uint64_t spawned = demon_spawn(entries[i].path, path_length, LAUNCH_SERVICE_MASK);
                status = spawned > 0xFFFFFFFFu ? "LAUNCH FAILED" : "LAUNCHED.";
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
                break;
            }
        }
    }
}

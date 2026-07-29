// Wave 7 of the EDE port: ede-autostart, ported off
// Desktop/EDE/ede-2.1/ede-autostart/ede-autostart.cpp. The original scans
// XDG autostart directories (~/.config/autostart, /etc/xdg/autostart) for
// .desktop files and lets the user toggle each one's "hidden" flag with a
// checkbox list.
//
// No XDG directories or .desktop files exist here, so the list is
// hardcoded to this OS's own EDE apps (ede-about/ede-tip/ede-preferred/
// ede-conf) instead of scanned from disk. Real persistence this time
// (Wave 5/6's demon_file_open/handle_read/handle_write): the on/off flags
// actually save to /config/autostart.txt on RAMFS. What's still not
// wired up is a boot-time consumer that reads this file and actually
// launches the flagged apps -- kernel.c's boot sequence already spawns
// several self-test processes against a hard 7-slot scheduler limit
// (SCHEDULER_PROCESS_LIMIT), and adding unconditional extra spawns to
// every real boot needs its own careful slot-budget analysis rather than
// being bolted on here. So: the picker and its persistence are real: the
// autostart effect itself is not yet, same class of honest gap as
// ede-preferred-applications' "apply" button before Wave 6.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 260
#define WIN_Y 160
#define TITLEBAR 32

#define CONFIG_PATH "/config/autostart.txt"
#define DEMON_SERVICE_STORAGE 4u

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

#define ITEM_COUNT 4
static const char *const item_name[ITEM_COUNT] = {"ABOUT", "TIP", "PREF", "CONF"};
static int enabled[ITEM_COUNT] = {0, 0, 0, 0};

static const struct graphics_rect item_row[ITEM_COUNT] = {
    {2, 12, 104, 13},
    {2, 26, 104, 13},
    {2, 40, 104, 13},
    {2, 54, 104, 13},
};
static const struct graphics_rect ok_button = {2, 76, 50, 18};
static const struct graphics_rect cancel_button = {56, 76, 50, 18};

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 8}, 2u, 0xFF2479D8u);
    graphics_text(&surface, 4, 2, "AUTOSTART", 1u, 0xFFF8FAFCu);

    for (unsigned i = 0; i < ITEM_COUNT; ++i) {
        graphics_rounded_rect(&surface, item_row[i], 2u,
            enabled[i] ? 0xFF2D6B45u : 0xFF2A3B52u);
        graphics_text(&surface, item_row[i].x + 2, item_row[i].y + 3, item_name[i], 1u, 0xFFF8FAFCu);
        graphics_text(&surface, item_row[i].x + 84, item_row[i].y + 3,
            enabled[i] ? "ON" : "-", 1u, enabled[i] ? 0xFF6FE08Fu : 0xFF8892A0u);
    }

    graphics_rounded_rect(&surface, ok_button, 2u, 0xFF35A768u);
    graphics_text(&surface, ok_button.x + 15, ok_button.y + 6, "OK", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, cancel_button, 2u, 0xFFE05260u);
    graphics_text(&surface, cancel_button.x + 4, cancel_button.y + 6, "CANCEL", 1u, 0xFFF8FAFCu);
}

static int hit(const struct graphics_rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

static void load_flags(uint64_t storage) {
    if (storage > 0xFFFFFFFFu) return;
    const uint64_t file = demon_file_open(storage, CONFIG_PATH, sizeof(CONFIG_PATH) - 1u, 0u);
    if (file > 0xFFFFFFFFu) return;
    char buf[ITEM_COUNT];
    if (demon_handle_read(file, buf, ITEM_COUNT) == ITEM_COUNT) {
        for (unsigned i = 0; i < ITEM_COUNT; ++i) enabled[i] = buf[i] == '1';
    }
    demon_handle_close(file);
}

static void save_flags(uint64_t storage) {
    if (storage > 0xFFFFFFFFu) return;
    const uint64_t file = demon_file_open(storage, CONFIG_PATH, sizeof(CONFIG_PATH) - 1u, 1u);
    if (file > 0xFFFFFFFFu) return;
    char buf[ITEM_COUNT];
    for (unsigned i = 0; i < ITEM_COUNT; ++i) buf[i] = enabled[i] ? '1' : '0';
    demon_handle_write(file, buf, ITEM_COUNT);
    demon_handle_close(file);
}

uint64_t ede_autostart_main(void) {
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
    const uint64_t storage = demon_service_open(DEMON_SERVICE_STORAGE);
    load_flags(storage);
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
            if (hit(&ok_button, local_x, local_y)) {
                save_flags(storage);
                return 0u;
            }
            if (hit(&cancel_button, local_x, local_y)) return 0u;
            for (unsigned i = 0; i < ITEM_COUNT; ++i) {
                if (!hit(&item_row[i], local_x, local_y)) continue;
                enabled[i] = !enabled[i];
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
                break;
            }
        }
    }
}

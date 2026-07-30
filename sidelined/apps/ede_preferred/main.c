// Wave 4 of the EDE port: ede-preferred-applications, ported off
// Desktop/EDE/ede-2.1/ede-preferred-applications/{ede-preferred-applications,
// AppChoice}.cpp. The original lets a user pick, per role (browser, mail,
// file manager, terminal), among several installed candidate programs, and
// persists the choice to a "ede-launch" resource file.
//
// Scaled to what this OS actually has: most roles here only have exactly
// one installed candidate (one terminal, one browser), so there is nothing
// real to choose between -- except "calculator", where this OS now
// genuinely has two (apps/calculator's basic 4-function one and
// apps/ede_calc's scientific one from Wave 1). That is the one category
// ported; the others are dropped rather than faked with a single-item
// "choice."
//
// Wave 6 update: real persistence. Wave 4 shipped without it because no
// userspace syscall for RAMFS read/write was exposed via demon/c_app.h --
// Wave 5 (apps/ede_conf) added demon_file_open/handle_read/handle_write, so
// this now actually loads/saves the choice to /config/preferred-calc.txt on
// RAMFS, same as the original persisted to an "ede-launch" resource file.
// Needs the STORAGE capability, which only ede-conf's launcher grants (see
// LAUNCH_SERVICE_MASK there) -- spawned any other way (e.g. "apps launch"
// from MakoBox), the storage handle comes back invalid and this silently
// falls back to the in-memory default, the same as before.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#define W 108u
#define H 96u
#define WIN_X 300
#define WIN_Y 120
#define TITLEBAR 32

#define CONFIG_PATH "/config/preferred-calc.txt"
#define DEMON_SERVICE_STORAGE 4u

static uint32_t pixels[W * H];
static struct graphics_surface surface;
static struct demon_window_message packet;
static char event_name[13] = "desktop.win0";

#define CHOICE_COUNT 2
static const char *const choice_name[CHOICE_COUNT] = {"BASIC", "SCIENTIFIC"};
static const char *const choice_desc[CHOICE_COUNT][2] = {
    {"4 KEYS, INTEGER", "MATH ONLY."},
    {"TRIG, LOG, BASES,", "EDE-CALC PORT."},
};

static int current_choice = 1; // scientific by default

static const struct graphics_rect prev_button = {2, 40, 34, 18};
static const struct graphics_rect next_button = {38, 40, 34, 18};
static const struct graphics_rect ok_button = {2, 76, 50, 18};
static const struct graphics_rect cancel_button = {56, 76, 50, 18};

static void redraw(void) {
    graphics_clear(&surface, 0xFF12161Fu);
    graphics_rounded_rect(&surface, (struct graphics_rect){2, 2, W - 4, 8}, 2u, 0xFF2479D8u);
    graphics_text(&surface, 4, 2, "CALCULATOR", 1u, 0xFFF8FAFCu);

    graphics_text(&surface, 4, 14, choice_name[current_choice], 1u, 0xFF6FE08Fu);
    graphics_text(&surface, 4, 24, choice_desc[current_choice][0], 1u, 0xFFB8C4D4u);
    graphics_text(&surface, 4, 32, choice_desc[current_choice][1], 1u, 0xFFB8C4D4u);

    graphics_rounded_rect(&surface, prev_button, 2u, 0xFF2A3B52u);
    graphics_text(&surface, prev_button.x + 4, prev_button.y + 6, "PV", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, next_button, 2u, 0xFF2A3B52u);
    graphics_text(&surface, next_button.x + 3, next_button.y + 6, "NX", 1u, 0xFFF8FAFCu);

    graphics_rounded_rect(&surface, ok_button, 2u, 0xFF35A768u);
    graphics_text(&surface, ok_button.x + 15, ok_button.y + 6, "OK", 1u, 0xFFF8FAFCu);
    graphics_rounded_rect(&surface, cancel_button, 2u, 0xFFE05260u);
    graphics_text(&surface, cancel_button.x + 4, cancel_button.y + 6, "CANCEL", 1u, 0xFFF8FAFCu);
}

static int hit(const struct graphics_rect *r, int x, int y) {
    return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

static void load_choice(uint64_t storage) {
    if (storage > 0xFFFFFFFFu) return;
    const uint64_t file = demon_file_open(storage, CONFIG_PATH, sizeof(CONFIG_PATH) - 1u, 0u);
    if (file > 0xFFFFFFFFu) return;
    char digit = 0;
    if (demon_handle_read(file, &digit, 1u) == 1u && digit >= '0' && digit < '0' + CHOICE_COUNT)
        current_choice = digit - '0';
    demon_handle_close(file);
}

static void save_choice(uint64_t storage) {
    if (storage > 0xFFFFFFFFu) return;
    const uint64_t file = demon_file_open(storage, CONFIG_PATH, sizeof(CONFIG_PATH) - 1u, 1u);
    if (file > 0xFFFFFFFFu) return;
    const char digit = (char)('0' + current_choice);
    demon_handle_write(file, &digit, 1u);
    demon_handle_close(file);
}

uint64_t ede_preferred_main(void) {
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
    load_choice(storage);
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
                save_choice(storage);
                return 0u;
            }
            if (hit(&cancel_button, local_x, local_y)) return 0u;
            if (hit(&next_button, local_x, local_y)) {
                current_choice = (current_choice + 1) % CHOICE_COUNT;
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
            } else if (hit(&prev_button, local_x, local_y)) {
                current_choice = (current_choice + CHOICE_COUNT - 1) % CHOICE_COUNT;
                redraw();
                demon_surface_write(surface_handle, pixels, W * H, 0u);
                demon_surface_damage(surface_handle, 0u, 0u, W, H);
            }
        }
    }
}

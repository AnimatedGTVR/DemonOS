// Dlib implementation -- see dlib.h for what this is and is not. Every
// call here either does something real (creates a real compositor window,
// draws into a real shared surface, receives a real forwarded event) or is
// explicitly documented as a stub; nothing here fakes success silently.
#include "dlib.h"

#include <stddef.h>

#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

// First slice covers exactly one live top-level window per Display, kept
// deliberately simple rather than reimplementing a resizable window table
// before anything has proven it needs one. WIN_MAX_W/H bound the static
// pixel buffer to fit comfortably inside a generic app's 48 KiB code+data
// budget (see kernel.c's per-process frame pools) alongside the rest of
// this library's own state -- 96x72x4 bytes = 27648 bytes.
#define WIN_MAX_W 96u
#define WIN_MAX_H 72u

struct Display {
    uint64_t events_channel;
    uint32_t serial;
    int has_window;
    Window window_id;
    int mapped;
    int32_t x, y;
    uint32_t width, height;
    uint32_t surface_handle;
    struct graphics_surface surface;
    uint32_t pixels[WIN_MAX_W * WIN_MAX_H];
};

// One Display per process is the only real config today -- XOpenDisplay is
// called once per process in every real Xlib program, so a static instance
// avoids introducing an allocator dependency for something that would only
// ever have one live instance in practice.
static struct Display g_display;
static int g_display_open;

struct DlibGC {
    uint32_t foreground;
    uint32_t background;
};

Display *XOpenDisplay(const char *display_name) {
    (void)display_name;
    if (g_display_open) return NULL;
    const uint64_t pid = demon_getpid();
    char event_name[12] = "dlib.ev.000";
    event_name[8] = (char)('0' + (pid / 100u) % 10u);
    event_name[9] = (char)('0' + (pid / 10u) % 10u);
    event_name[10] = (char)('0' + pid % 10u);
    const uint64_t events = demon_channel_create(event_name, 11u);
    if (events > UINT32_MAX) return NULL;
    g_display.events_channel = events;
    g_display.serial = 1u;
    g_display.has_window = 0;
    g_display.mapped = 0;
    g_display_open = 1;
    return &g_display;
}

int XCloseDisplay(Display *display) {
    if (display == NULL || display != &g_display) return -1;
    if (display->mapped) (void)XDestroyWindow(display, display->window_id);
    demon_handle_close(display->events_channel);
    g_display_open = 0;
    return 0;
}

Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                            unsigned int width, unsigned int height,
                            unsigned int border_width, unsigned long border,
                            unsigned long background) {
    (void)parent; (void)border_width; (void)border;
    if (display == NULL || display->has_window) return DlibNone;
    if (width > WIN_MAX_W) width = WIN_MAX_W;
    if (height > WIN_MAX_H) height = WIN_MAX_H;
    if (width == 0u) width = 1u;
    if (height == 0u) height = 1u;
    if (!graphics_surface_init(&display->surface, display->pixels,
                               width, height, width))
        return DlibNone;
    graphics_clear(&display->surface, (uint32_t)background | 0xFF000000u);
    display->x = x;
    display->y = y;
    display->width = width;
    display->height = height;
    display->window_id = (Window)demon_getpid();
    display->has_window = 1;
    display->mapped = 0;
    return display->window_id;
}

int XMapWindow(Display *display, Window window) {
    if (display == NULL || !display->has_window ||
        display->window_id != window)
        return -1;
    const uint64_t factory = demon_service_open(9u);
    const uint64_t surface = demon_surface_create(factory, display->width,
                                                   display->height);
    if (factory > UINT32_MAX || surface > UINT32_MAX) return -1;
    if (demon_surface_write(surface, display->pixels,
                            display->width * display->height, 0u) !=
        display->width * display->height)
        return -1;
    demon_surface_damage(surface, 0u, 0u, display->width, display->height);
    const uint64_t compositor = demon_channel_connect(
        DEMON_WINDOW_SERVICE, sizeof(DEMON_WINDOW_SERVICE) - 1u);
    if (compositor > UINT32_MAX) return -1;
    struct demon_window_message packet = {
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_CREATE,
        .serial = display->serial++,
        .window_id = window,
        .flags = 0u,
        .x = display->x, .y = display->y,
        .width = display->width, .height = display->height,
        .surface_id = (uint32_t)demon_surface_share(surface, compositor),
        .payload_length = 0u, .payload = {0},
    };
    const int ok = demon_channel_send(compositor, &packet, sizeof(packet)) ==
                    sizeof(packet);
    demon_handle_close(compositor);
    demon_handle_close(factory);
    display->surface_handle = (uint32_t)surface;
    display->mapped = ok;
    return ok ? 0 : -1;
}

// No standalone "hide without destroying" opcode exists in the real
// protocol yet (see demon_window_opcode in window.h) -- documented in
// dlib.h rather than silently pretending this is a real unmap.
int XUnmapWindow(Display *display, Window window) {
    return XDestroyWindow(display, window);
}

int XDestroyWindow(Display *display, Window window) {
    if (display == NULL || !display->has_window ||
        display->window_id != window)
        return -1;
    if (display->mapped) {
        const uint64_t compositor = demon_channel_connect(
            DEMON_WINDOW_SERVICE, sizeof(DEMON_WINDOW_SERVICE) - 1u);
        if (compositor <= UINT32_MAX) {
            struct demon_window_message packet = {
                .version = DEMON_WINDOW_PROTOCOL_VERSION,
                .opcode = DEMON_WINDOW_CLOSE,
                .serial = display->serial++,
                .window_id = window,
                .flags = 0u, .x = 0, .y = 0, .width = 0, .height = 0,
                .surface_id = 0u, .payload_length = 0u, .payload = {0},
            };
            (void)demon_channel_send(compositor, &packet, sizeof(packet));
            demon_handle_close(compositor);
        }
    }
    display->mapped = 0;
    display->has_window = 0;
    return 0;
}

static int translate_event(const struct demon_window_message *packet,
                           XEvent *event) {
    event->window = packet->window_id;
    event->x = packet->x;
    event->y = packet->y;
    event->width = packet->width;
    event->height = packet->height;
    switch (packet->opcode) {
        case DEMON_WINDOW_KEY:
            event->type = DlibKeyPress;
            event->keycode = packet->flags;
            return 1;
        case DEMON_WINDOW_POINTER:
            event->type = DlibButtonPress;
            event->button = packet->flags;
            return 1;
        case DEMON_WINDOW_FOCUS:
            event->type = DlibFocusIn;
            return 1;
        case DEMON_WINDOW_CLOSE:
            event->type = DlibClientMessage;
            return 1;
        case DEMON_WINDOW_MOVE:
            event->type = DlibConfigureNotify;
            return 1;
        default:
            return 0;
    }
}

int XNextEvent(Display *display, XEvent *event) {
    if (display == NULL || event == NULL) return -1;
    for (;;) {
        struct demon_window_message packet;
        if (demon_channel_receive(display->events_channel, &packet,
                                  sizeof(packet), 0u) != sizeof(packet))
            return -1;
        if (translate_event(&packet, event)) return 0;
        // Unrecognized opcode: keep blocking for the next one rather than
        // returning a half-filled event.
    }
}

int XPending(Display *display) {
    if (display == NULL) return 0;
    struct demon_window_message packet;
    if (demon_channel_receive(display->events_channel, &packet,
                              sizeof(packet), 1u) != sizeof(packet))
        return 0;
    return 1;
}

int XFlush(Display *display) { (void)display; return 0; }
int XSync(Display *display, int discard) { (void)display; (void)discard; return 0; }

struct DlibGC *XCreateGC(Display *display) {
    (void)display;
    static struct DlibGC gc;
    gc.foreground = 0xFF000000u;
    gc.background = 0xFFFFFFFFu;
    return &gc;
}

int XFreeGC(struct DlibGC *gc) { (void)gc; return 0; }

int XSetForeground(Display *display, struct DlibGC *gc, unsigned long color) {
    (void)display;
    if (gc == NULL) return -1;
    gc->foreground = (uint32_t)color | 0xFF000000u;
    return 0;
}

int XFillRectangle(Display *display, Window window, struct DlibGC *gc,
                   int x, int y, unsigned int width, unsigned int height) {
    if (display == NULL || gc == NULL || !display->has_window ||
        display->window_id != window)
        return -1;
    struct graphics_rect rect = {x, y, (int32_t)width, (int32_t)height};
    graphics_fill_rect(&display->surface, rect, gc->foreground);
    return 0;
}

int XDrawLine(Display *display, Window window, struct DlibGC *gc,
             int x1, int y1, int x2, int y2) {
    if (display == NULL || gc == NULL || !display->has_window ||
        display->window_id != window)
        return -1;
    graphics_line(&display->surface, x1, y1, x2, y2, gc->foreground);
    return 0;
}

int XDrawString(Display *display, Window window, struct DlibGC *gc,
               int x, int y, const char *text, int length) {
    (void)length;
    if (display == NULL || gc == NULL || !display->has_window ||
        display->window_id != window)
        return -1;
    graphics_text(&display->surface, x, y, text, 1u, gc->foreground);
    return 0;
}

// Re-submits the whole window surface to the compositor -- present after a
// batch of draw calls, the same real_surface_write+damage pair XMapWindow
// itself uses, not a fabricated "success".
int DlibPresent(Display *display) {
    if (display == NULL || !display->mapped) return -1;
    if (demon_surface_write(display->surface_handle, display->pixels,
                            display->width * display->height, 0u) !=
        display->width * display->height)
        return -1;
    demon_surface_damage(display->surface_handle, 0u, 0u,
                         display->width, display->height);
    return 0;
}

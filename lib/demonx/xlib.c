#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xdbe.h>

#include <demon/c_app.h>
#include <demon/demonx.h>
#include <stddef.h>
#include <stdint.h>

struct _XDisplay {
    uint64_t server;
    uint64_t replies;
    uint32_t client_id;
    uint32_t sequence;
    uint32_t next_resource;
    uint32_t resource_base;
    uint32_t resource_mask;
    Window query_children[4];
    Atom query_atoms[4];
    unsigned char property_data[24];
    uint32_t image_data[56];
    char atom_name[17];
    int has_pending;
    int has_putback;
    XEvent putback;
    int pointer_x, pointer_y;
    unsigned int pointer_mask;
    struct demonx_message packet;
    struct demonx_message pending;
    struct demonx_transport wire;
};

struct _XGC {
    XID id;
    Drawable drawable;
    int used;
    int clip_enabled;
    int clip_x, clip_y;
    unsigned int clip_width, clip_height;
};
struct _XFontSet { int used; };

static Display singleton;
static int singleton_open;
static struct _XGC gc_slots[4];
static XImage image_slots[4];
static int image_slot_used[4];
static XFontStruct core_font_metrics;
static int core_font_loaded;
static XWMHints wm_hints_slot;
static XSizeHints size_hints_slot;
static XErrorHandler error_handler;
static XAfterFunction after_function;
static KeyCode modifier_keys[8] = {16u, 0u, 17u, 18u, 0u, 0u, 0u, 0u};
static XModifierKeymap modifier_map = {1, modifier_keys};
static KeySym keyboard_symbols[16];
static struct _XFontSet font_set_slot;
static XFontStruct *font_set_fonts[1];
static char *font_set_names[1];
static char text_property_string[25];
static char *text_property_list[1];
static Visual native_visual = {
    1u, TrueColor, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 8, 256
};
static XVisualInfo native_visual_info;

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0u; index < length; ++index) bytes[index] = 0u;
}

static uint16_t read16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

static uint32_t read32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static void write16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void write32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static size_t string_length(const char *text) {
    size_t length = 0u;
    if (text != NULL)
        while (text[length] != '\0') ++length;
    return length;
}

static void begin_packet(Display *display, uint16_t payload_length,
                         uint16_t flags) {
    zero_bytes(&display->packet, sizeof(display->packet));
    display->packet.magic = DEMONX_TRANSPORT_MAGIC;
    display->packet.payload_length = payload_length;
    display->packet.flags = flags;
    display->packet.client_id = display->client_id;
    display->packet.sequence = display->sequence++;
}

static int send_packet(Display *display) {
    uint32_t offset = 0u;
    do {
        const uint32_t remaining = display->packet.payload_length - offset;
        const uint16_t count = remaining > DEMONX_PAYLOAD_BYTES
            ? DEMONX_PAYLOAD_BYTES : (uint16_t)remaining;
        zero_bytes(&display->wire, sizeof(display->wire));
        display->wire.magic = DEMONX_TRANSPORT_MAGIC;
        display->wire.payload_length = count;
        display->wire.flags = display->packet.flags |
            (remaining > count ? DEMONX_FLAG_MORE : 0u);
        display->wire.client_id = display->packet.client_id;
        display->wire.sequence = display->packet.sequence;
        for (uint16_t index = 0u; index < count; ++index)
            display->wire.payload[index] =
                display->packet.payload[offset + index];
        if (demon_channel_send(display->server, &display->wire,
                               sizeof(display->wire)) !=
            sizeof(display->wire))
            return 0;
        offset += count;
    } while (offset < display->packet.payload_length);
    return 1;
}

static int receive_packet(Display *display, int nonblocking) {
    uint16_t total = 0u;
    uint16_t flags = 0u;
    uint32_t client_id = 0u;
    uint32_t sequence = 0u;
    do {
        if (demon_channel_receive(display->replies, &display->wire,
                                  sizeof(display->wire),
                                  (uint64_t)nonblocking) !=
            sizeof(display->wire) ||
            display->wire.magic != DEMONX_TRANSPORT_MAGIC ||
            display->wire.payload_length > DEMONX_PAYLOAD_BYTES ||
            (uint32_t)total + display->wire.payload_length >
                DEMONX_MESSAGE_BYTES)
            return 0;
        if (total == 0u) {
            flags = display->wire.flags & ~DEMONX_FLAG_MORE;
            client_id = display->wire.client_id;
            sequence = display->wire.sequence;
        } else if (client_id != display->wire.client_id ||
                   sequence != display->wire.sequence ||
                   flags != (display->wire.flags & ~DEMONX_FLAG_MORE)) {
            return 0;
        }
        for (uint16_t index = 0u; index < display->wire.payload_length; ++index)
            display->packet.payload[total + index] =
                display->wire.payload[index];
        total = (uint16_t)(total + display->wire.payload_length);
        nonblocking = 0;
    } while ((display->wire.flags & DEMONX_FLAG_MORE) != 0u);
    display->packet.magic = DEMONX_TRANSPORT_MAGIC;
    display->packet.payload_length = total;
    display->packet.flags = flags;
    display->packet.client_id = client_id;
    display->packet.sequence = sequence;
    return 1;
}

Display *XOpenDisplay(const char *display_name) {
    static const char service_name[] = "demonx.display.0";
    char reply_name[] = "demonx.reply.1";
    if (singleton_open) return NULL;
    zero_bytes(&singleton, sizeof(singleton));
    singleton.client_id = 1u;
    if (display_name != NULL && display_name[0] == ':' &&
        display_name[1] >= '1' &&
        display_name[1] < (char)('1' + DEMONX_MAX_CLIENTS) &&
        display_name[2] == '\0')
        singleton.client_id = (uint32_t)(display_name[1] - '0');
    reply_name[13] = (char)('0' + singleton.client_id);
    singleton.sequence = 1u;
    singleton.replies = demon_channel_create(reply_name, sizeof(reply_name) - 1u);
    singleton.server = demon_channel_connect(service_name,
                                              sizeof(service_name) - 1u);
    if (singleton.replies > UINT32_MAX || singleton.server > UINT32_MAX)
        return NULL;

    begin_packet(&singleton, 12u, DEMONX_FLAG_SETUP);
    singleton.packet.payload[0] = (uint8_t)'l';
    write16(&singleton.packet.payload[2], 11u);
    if (!send_packet(&singleton) || !receive_packet(&singleton, 0) ||
        singleton.packet.flags != DEMONX_FLAG_REPLY ||
        singleton.packet.payload[0] != 1u) {
        demon_handle_close(singleton.server);
        demon_handle_close(singleton.replies);
        return NULL;
    }
    singleton.resource_base = read32(&singleton.packet.payload[12]);
    singleton.resource_mask = read32(&singleton.packet.payload[16]);
    singleton.next_resource = 1u;
    zero_bytes(gc_slots, sizeof(gc_slots));
    singleton_open = 1;
    return &singleton;
}

int XDefaultScreen(Display *display) {
    return display == &singleton && singleton_open ? 0 : -1;
}

int XDefaultDepth(Display *display, int screen) {
    return display == &singleton && singleton_open && screen == 0 ? 32 : 0;
}

Visual *XDefaultVisual(Display *display, int screen) {
    return display == &singleton && singleton_open && screen == 0
        ? &native_visual : NULL;
}

int XDisplayWidth(Display *display, int screen) {
    return display == &singleton && singleton_open && screen == 0 ? 640 : 0;
}

int XDisplayHeight(Display *display, int screen) {
    return display == &singleton && singleton_open && screen == 0 ? 480 : 0;
}

int XCloseDisplay(Display *display) {
    if (display != &singleton || !singleton_open) return -1;
    demon_handle_close(display->server);
    demon_handle_close(display->replies);
    singleton_open = 0;
    return 0;
}

Window XDefaultRootWindow(Display *display) {
    return display == &singleton ? DEMONX_ROOT_WINDOW : None;
}

Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background) {
    (void)border;
    if (display != &singleton || !singleton_open || width == 0u ||
        height == 0u || display->next_resource > display->resource_mask)
        return None;
    const Window window = display->resource_base | display->next_resource++;
    begin_packet(display, 32u, 0u);
    display->packet.payload[0] = DEMONX_CREATE_WINDOW;
    write16(&display->packet.payload[2], 8u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], parent);
    write16(&display->packet.payload[12], (uint16_t)x);
    write16(&display->packet.payload[14], (uint16_t)y);
    write16(&display->packet.payload[16], (uint16_t)width);
    write16(&display->packet.payload[18], (uint16_t)height);
    write16(&display->packet.payload[20], (uint16_t)border_width);
    write16(&display->packet.payload[22], 1u);
    write32(&display->packet.payload[24], (uint32_t)background);
    write32(&display->packet.payload[28], CWBackPixel);
    return send_packet(display) ? window : None;
}

Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class_, Visual *visual,
                     unsigned long value_mask,
                     XSetWindowAttributes *attributes) {
    (void)depth;
    (void)class_;
    (void)visual;
    unsigned long background = 0u;
    if ((value_mask & CWBackPixel) != 0u && attributes != NULL)
        background = attributes->background_pixel;
    return XCreateSimpleWindow(display, parent, x, y, width, height,
                               border_width, 0u, background);
}

int XChangeWindowAttributes(Display *display, Window window,
                            unsigned long value_mask,
                            XSetWindowAttributes *attributes) {
    if (display != &singleton || !singleton_open || attributes == NULL ||
        (value_mask != CWBackPixel && value_mask != CWBackPixmap))
        return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CHANGE_WINDOW_ATTRIBUTES;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], (uint32_t)value_mask);
    write32(&display->packet.payload[12],
            value_mask == CWBackPixmap ? attributes->background_pixmap :
            (uint32_t)attributes->background_pixel);
    return send_packet(display);
}

Status XGetWindowAttributes(Display *display, Window window,
                            XWindowAttributes *attributes) {
    if (display != &singleton || !singleton_open || attributes == NULL)
        return False;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_GET_WINDOW_ATTRIBUTES;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY ||
        display->packet.payload_length != 44u)
        return False;
    zero_bytes(attributes, sizeof(*attributes));
    attributes->x = (int16_t)read16(&display->packet.payload[8]);
    attributes->y = (int16_t)read16(&display->packet.payload[10]);
    attributes->width = read16(&display->packet.payload[12]);
    attributes->height = read16(&display->packet.payload[14]);
    attributes->border_width = read16(&display->packet.payload[16]);
    attributes->depth = display->packet.payload[1];
    attributes->root = read32(&display->packet.payload[20]);
    attributes->class = read16(&display->packet.payload[24]);
    attributes->map_state = display->packet.payload[26];
    attributes->your_event_mask =
        (long)read32(&display->packet.payload[28]);
    attributes->all_event_masks =
        (long)read32(&display->packet.payload[32]);
    attributes->override_redirect =
        display->packet.payload[36] != 0u ? True : False;
    return True;
}

int XSetWindowBackground(Display *display, Window window,
                         unsigned long background) {
    XSetWindowAttributes attributes = {.background_pixel = background};
    return XChangeWindowAttributes(display, window, CWBackPixel, &attributes);
}

int XSetWindowBackgroundPixmap(Display *display, Window window,
                               Pixmap background_pixmap) {
    XSetWindowAttributes attributes = {
        .background_pixmap = background_pixmap
    };
    return XChangeWindowAttributes(display, window, CWBackPixmap, &attributes);
}

int XClearWindow(Display *display, Window window) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CLEAR_AREA;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], window);
    return send_packet(display);
}

int XClearArea(Display *display, Window window, int x, int y,
               unsigned int width, unsigned int height, Bool exposures) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CLEAR_AREA;
    display->packet.payload[1] = exposures != False;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], window);
    write16(&display->packet.payload[8], (uint16_t)x);
    write16(&display->packet.payload[10], (uint16_t)y);
    write16(&display->packet.payload[12], (uint16_t)width);
    write16(&display->packet.payload[14], (uint16_t)height);
    return send_packet(display);
}

int XSelectInput(Display *display, Window window, long event_mask) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 12u, 0u);
    display->packet.payload[0] = DEMONX_SELECT_INPUT;
    write16(&display->packet.payload[2], 3u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], (uint32_t)event_mask);
    return send_packet(display);
}

int XSetInputFocus(Display *display, Window focus, int revert_to,
                   unsigned long time) {
    if (display != &singleton || !singleton_open ||
        revert_to < RevertToNone || revert_to > RevertToParent)
        return 0;
    begin_packet(display, 12u, 0u);
    display->packet.payload[0] = DEMONX_SET_INPUT_FOCUS;
    display->packet.payload[1] = (uint8_t)revert_to;
    write16(&display->packet.payload[2], 3u);
    write32(&display->packet.payload[4], focus);
    write32(&display->packet.payload[8], (uint32_t)time);
    return send_packet(display);
}

static int grab_device(Display *display, uint8_t opcode, Window window,
                       Bool owner_events, unsigned int event_mask,
                       int pointer_mode, int keyboard_mode,
                       unsigned long time) {
    if (display != &singleton || !singleton_open ||
        pointer_mode < GrabModeSync || pointer_mode > GrabModeAsync ||
        keyboard_mode < GrabModeSync || keyboard_mode > GrabModeAsync)
        return GrabNotViewable;
    begin_packet(display, 24u, 0u);
    display->packet.payload[0] = opcode;
    display->packet.payload[1] = owner_events ? 1u : 0u;
    write16(&display->packet.payload[2], 6u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], event_mask);
    display->packet.payload[12] = (uint8_t)pointer_mode;
    display->packet.payload[13] = (uint8_t)keyboard_mode;
    write32(&display->packet.payload[16], (uint32_t)time);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return GrabNotViewable;
    return display->packet.payload[1];
}

int XGrabPointer(Display *display, Window window, Bool owner_events,
                 unsigned int event_mask, int pointer_mode,
                 int keyboard_mode, Window confine_to, Cursor cursor,
                 unsigned long time) {
    (void)confine_to;
    (void)cursor;
    return grab_device(display, DEMONX_GRAB_POINTER, window, owner_events,
                       event_mask, pointer_mode, keyboard_mode, time);
}

int XGrabKeyboard(Display *display, Window window, Bool owner_events,
                  int pointer_mode, int keyboard_mode, unsigned long time) {
    return grab_device(display, DEMONX_GRAB_KEYBOARD, window, owner_events,
                       KeyPressMask | KeyReleaseMask,
                       pointer_mode, keyboard_mode, time);
}

static int ungrab_device(Display *display, uint8_t opcode,
                         unsigned long time) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = opcode;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], (uint32_t)time);
    return send_packet(display);
}

int XUngrabPointer(Display *display, unsigned long time) {
    return ungrab_device(display, DEMONX_UNGRAB_POINTER, time);
}

int XUngrabKeyboard(Display *display, unsigned long time) {
    return ungrab_device(display, DEMONX_UNGRAB_KEYBOARD, time);
}

int XAllowEvents(Display *display, int mode, unsigned long time) {
    (void)time;
    return display == &singleton && singleton_open &&
        mode >= AsyncPointer && mode <= SyncBoth;
}

int XGrabServer(Display *display) {
    return ungrab_device(display, DEMONX_GRAB_SERVER, 0u);
}

int XUngrabServer(Display *display) {
    return ungrab_device(display, DEMONX_UNGRAB_SERVER, 0u);
}

Cursor XCreateFontCursor(Display *display, unsigned int shape) {
    if (display != &singleton || !singleton_open || shape > UINT16_MAX ||
        display->next_resource > display->resource_mask)
        return None;
    const Cursor cursor =
        display->resource_base | display->next_resource++;
    begin_packet(display, 12u, 0u);
    display->packet.payload[0] = DEMONX_CREATE_FONT_CURSOR;
    write16(&display->packet.payload[2], 3u);
    write32(&display->packet.payload[4], cursor);
    write16(&display->packet.payload[8], (uint16_t)shape);
    return send_packet(display) ? cursor : None;
}

int XDefineCursor(Display *display, Window window, Cursor cursor) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 12u, 0u);
    display->packet.payload[0] = DEMONX_DEFINE_CURSOR;
    write16(&display->packet.payload[2], 3u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], cursor);
    return send_packet(display);
}

int XFreeCursor(Display *display, Cursor cursor) {
    if (display != &singleton || !singleton_open || cursor == None) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_FREE_CURSOR;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], cursor);
    return send_packet(display);
}

static int passive_grab(Display *display, uint8_t opcode, Window window,
                        unsigned int detail, unsigned int modifiers,
                        unsigned int event_mask, int pointer_mode,
                        int keyboard_mode) {
    if (display != &singleton || !singleton_open || detail > 255u ||
        pointer_mode < GrabModeSync || pointer_mode > GrabModeAsync ||
        keyboard_mode < GrabModeSync || keyboard_mode > GrabModeAsync)
        return 0;
    begin_packet(display, 24u, 0u);
    display->packet.payload[0] = opcode;
    display->packet.payload[1] = (uint8_t)detail;
    write16(&display->packet.payload[2], 6u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], modifiers);
    write32(&display->packet.payload[12], event_mask);
    display->packet.payload[16] = (uint8_t)pointer_mode;
    display->packet.payload[17] = (uint8_t)keyboard_mode;
    return send_packet(display);
}

int XGrabKey(Display *display, int keycode, unsigned int modifiers,
             Window window, Bool owner_events, int pointer_mode,
             int keyboard_mode) {
    (void)owner_events;
    return keycode >= 0 &&
        passive_grab(display, DEMONX_GRAB_KEY, window,
                     (unsigned int)keycode, modifiers,
                     KeyPressMask | KeyReleaseMask,
                     pointer_mode, keyboard_mode);
}

int XUngrabKey(Display *display, int keycode, unsigned int modifiers,
               Window window) {
    return keycode >= 0 &&
        passive_grab(display, DEMONX_UNGRAB_KEY, window,
                     (unsigned int)keycode, modifiers, 0u,
                     GrabModeAsync, GrabModeAsync);
}

int XGrabButton(Display *display, unsigned int button,
                unsigned int modifiers, Window window, Bool owner_events,
                unsigned int event_mask, int pointer_mode,
                int keyboard_mode, Window confine_to, Cursor cursor) {
    (void)owner_events; (void)confine_to; (void)cursor;
    return passive_grab(display, DEMONX_GRAB_BUTTON, window, button,
                        modifiers, event_mask, pointer_mode, keyboard_mode);
}

int XUngrabButton(Display *display, unsigned int button,
                  unsigned int modifiers, Window window) {
    return passive_grab(display, DEMONX_UNGRAB_BUTTON, window, button,
                        modifiers, 0u, GrabModeAsync, GrabModeAsync);
}

int XMapWindow(Display *display, Window window) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_MAP_WINDOW;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    return send_packet(display);
}

int XMapRaised(Display *display, Window window) {
    return XRaiseWindow(display, window) && XMapWindow(display, window);
}

int XUnmapWindow(Display *display, Window window) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_UNMAP_WINDOW;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    return send_packet(display);
}

int XReparentWindow(Display *display, Window window, Window parent,
                    int x, int y) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_REPARENT_WINDOW;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], parent);
    write16(&display->packet.payload[12], (uint16_t)x);
    write16(&display->packet.payload[14], (uint16_t)y);
    return send_packet(display);
}

int XConfigureWindow(Display *display, Window window,
                     unsigned int value_mask, XWindowChanges *changes) {
    if (display != &singleton || !singleton_open || changes == NULL ||
        (value_mask & ~127u) != 0u)
        return 0;
    uint16_t words = 3u;
    for (unsigned int bit = 1u; bit <= CWStackMode; bit <<= 1u)
        if ((value_mask & bit) != 0u) ++words;
    begin_packet(display, (uint16_t)(words * 4u), 0u);
    display->packet.payload[0] = DEMONX_CONFIGURE_WINDOW;
    write16(&display->packet.payload[2], words);
    write32(&display->packet.payload[4], window);
    write16(&display->packet.payload[8], (uint16_t)value_mask);
    size_t offset = 12u;
    for (unsigned int bit = 1u; bit <= CWStackMode; bit <<= 1u) {
        if ((value_mask & bit) == 0u) continue;
        uint32_t value = 0u;
        if (bit == CWX) value = (uint32_t)changes->x;
        else if (bit == CWY) value = (uint32_t)changes->y;
        else if (bit == CWWidth) value = (uint32_t)changes->width;
        else if (bit == CWHeight) value = (uint32_t)changes->height;
        else if (bit == CWBorderWidth)
            value = (uint32_t)changes->border_width;
        else if (bit == CWSibling) value = changes->sibling;
        else if (bit == CWStackMode) value = (uint32_t)changes->stack_mode;
        write32(&display->packet.payload[offset], value);
        offset += 4u;
    }
    return send_packet(display);
}

int XMoveResizeWindow(Display *display, Window window, int x, int y,
                      unsigned int width, unsigned int height) {
    XWindowChanges changes = {
        .x = x, .y = y, .width = (int)width, .height = (int)height
    };
    return XConfigureWindow(display, window, CWX | CWY | CWWidth | CWHeight,
                            &changes);
}

int XMoveWindow(Display *display, Window window, int x, int y) {
    XWindowChanges changes = {.x = x, .y = y};
    return XConfigureWindow(display, window, CWX | CWY, &changes);
}

int XResizeWindow(Display *display, Window window,
                  unsigned int width, unsigned int height) {
    XWindowChanges changes = {
        .width = (int)width, .height = (int)height
    };
    return XConfigureWindow(display, window, CWWidth | CWHeight, &changes);
}

int XRaiseWindow(Display *display, Window window) {
    XWindowChanges changes = {.stack_mode = Above};
    return XConfigureWindow(display, window, CWStackMode, &changes);
}

int XLowerWindow(Display *display, Window window) {
    XWindowChanges changes = {.stack_mode = Below};
    return XConfigureWindow(display, window, CWStackMode, &changes);
}

int XSetWindowBorderWidth(Display *display, Window window,
                          unsigned int width) {
    XWindowChanges changes = {.border_width = (int)width};
    return XConfigureWindow(display, window, CWBorderWidth, &changes);
}

int XMapSubwindows(Display *display, Window window) {
    Window root, parent, *children;
    unsigned int count;
    if (!XQueryTree(display, window, &root, &parent, &children, &count))
        return 0;
    for (unsigned int index = 0u; index < count; ++index)
        if (!XMapWindow(display, children[index])) return 0;
    return 1;
}

int XRestackWindows(Display *display, Window *windows, int count) {
    if (display != &singleton || !singleton_open || windows == NULL ||
        count <= 0)
        return 0;
    /* Raising from the bottom of the requested list preserves windows[0]
       as the topmost sibling, matching Xlib's stacking contract. */
    for (int index = count - 1; index >= 0; --index)
        if (!XRaiseWindow(display, windows[index])) return 0;
    return 1;
}

int XDestroyWindow(Display *display, Window window) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_DESTROY_WINDOW;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    return send_packet(display);
}

static int change_save_set(Display *display, Window window, int insert) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_CHANGE_SAVE_SET;
    display->packet.payload[1] = insert ? 0u : 1u;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    return send_packet(display);
}

int XAddToSaveSet(Display *display, Window window) {
    return change_save_set(display, window, 1);
}

int XRemoveFromSaveSet(Display *display, Window window) {
    return change_save_set(display, window, 0);
}

int XKillClient(Display *display, XID resource) {
    if (display != &singleton || !singleton_open || resource == None)
        return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_KILL_CLIENT;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], resource);
    return send_packet(display);
}

int XWarpPointer(Display *display, Window source, Window destination,
                 int source_x, int source_y, unsigned int source_width,
                 unsigned int source_height, int destination_x,
                 int destination_y) {
    if (display != &singleton || !singleton_open ||
        source_width > UINT16_MAX || source_height > UINT16_MAX)
        return 0;
    begin_packet(display, 24u, 0u);
    display->packet.payload[0] = DEMONX_WARP_POINTER;
    write16(&display->packet.payload[2], 6u);
    write32(&display->packet.payload[4], source);
    write32(&display->packet.payload[8], destination);
    write16(&display->packet.payload[12], (uint16_t)source_x);
    write16(&display->packet.payload[14], (uint16_t)source_y);
    write16(&display->packet.payload[16], (uint16_t)source_width);
    write16(&display->packet.payload[18], (uint16_t)source_height);
    write16(&display->packet.payload[20], (uint16_t)destination_x);
    write16(&display->packet.payload[22], (uint16_t)destination_y);
    if (!send_packet(display)) return 0;
    display->pointer_x = destination_x;
    display->pointer_y = destination_y;
    return 1;
}

Status XQueryTree(Display *display, Window window, Window *root_return,
                  Window *parent_return, Window **children_return,
                  unsigned int *child_count_return) {
    if (display != &singleton || !singleton_open || root_return == NULL ||
        parent_return == NULL || children_return == NULL ||
        child_count_return == NULL)
        return False;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_QUERY_TREE;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return False;
    const unsigned int count = read16(&display->packet.payload[16]);
    if (count > 4u) return False;
    *root_return = read32(&display->packet.payload[8]);
    *parent_return = read32(&display->packet.payload[12]);
    *child_count_return = count;
    for (unsigned int index = 0u; index < count; ++index)
        display->query_children[index] =
            read32(&display->packet.payload[32u + index * 4u]);
    *children_return = count == 0u ? NULL : display->query_children;
    return True;
}

int XFree(void *data) {
    return data == NULL || data == singleton.query_children ||
           data == singleton.query_atoms ||
           data == singleton.property_data ||
           data == singleton.atom_name || data == &wm_hints_slot ||
           data == &size_hints_slot ? 1 : 0;
}

Atom XInternAtom(Display *display, const char *atom_name,
                 Bool only_if_exists) {
    if (display != &singleton || !singleton_open || atom_name == NULL)
        return None;
    const size_t length = string_length(atom_name);
    if (length == 0u || length > 32u) return None;
    const uint16_t payload_length =
        (uint16_t)(8u + ((length + 3u) & ~3u));
    begin_packet(display, payload_length, 0u);
    display->packet.payload[0] = DEMONX_INTERN_ATOM;
    display->packet.payload[1] = only_if_exists ? 1u : 0u;
    write16(&display->packet.payload[2], payload_length / 4u);
    write16(&display->packet.payload[4], (uint16_t)length);
    for (size_t index = 0u; index < length; ++index)
        display->packet.payload[8u + index] = (uint8_t)atom_name[index];
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return None;
    return read32(&display->packet.payload[8]);
}

Status XInternAtoms(Display *display, char **names, int count,
                    Bool only_if_exists, Atom *atoms_return) {
    if (names == NULL || atoms_return == NULL || count < 0) return False;
    for (int index = 0; index < count; ++index) {
        atoms_return[index] =
            XInternAtom(display, names[index], only_if_exists);
        if (atoms_return[index] == None && !only_if_exists) return False;
    }
    return True;
}

char *XGetAtomName(Display *display, Atom atom) {
    if (display != &singleton || !singleton_open) return NULL;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_GET_ATOM_NAME;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], atom);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return NULL;
    const uint16_t length = read16(&display->packet.payload[8]);
    if (length > 16u) return NULL;
    for (uint16_t index = 0u; index < length; ++index)
        display->atom_name[index] =
            (char)display->packet.payload[32u + index];
    display->atom_name[length] = '\0';
    return display->atom_name;
}

int XChangeProperty(Display *display, Window window, Atom property,
                    Atom type, int format, int mode,
                    const unsigned char *data, int element_count) {
    if (display != &singleton || !singleton_open || data == NULL ||
        element_count < 0 || (format != 8 && format != 16 && format != 32) ||
        mode < PropModeReplace || mode > PropModeAppend)
        return 0;
    const uint32_t bytes =
        (uint32_t)element_count * (uint32_t)(format / 8);
    if (bytes > DEMONX_PROPERTY_DATA_BYTES) return 0;
    const uint16_t payload_length =
        (uint16_t)(24u + ((bytes + 3u) & ~3u));
    begin_packet(display, payload_length, 0u);
    display->packet.payload[0] = DEMONX_CHANGE_PROPERTY;
    display->packet.payload[1] = (uint8_t)mode;
    write16(&display->packet.payload[2], payload_length / 4u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], property);
    write32(&display->packet.payload[12], type);
    display->packet.payload[16] = (uint8_t)format;
    write32(&display->packet.payload[20], (uint32_t)element_count);
    for (uint32_t index = 0u; index < bytes; ++index)
        display->packet.payload[24u + index] = data[index];
    return send_packet(display);
}

int XDeleteProperty(Display *display, Window window, Atom property) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 12u, 0u);
    display->packet.payload[0] = DEMONX_DELETE_PROPERTY;
    write16(&display->packet.payload[2], 3u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], property);
    return send_packet(display);
}

int XGetWindowProperty(Display *display, Window window, Atom property,
                       long long_offset, long long_length, Bool delete_after,
                       Atom requested_type, Atom *actual_type_return,
                       int *actual_format_return,
                       unsigned long *item_count_return,
                       unsigned long *bytes_after_return,
                       unsigned char **property_return) {
    if (display != &singleton || !singleton_open || long_offset < 0 ||
        long_length < 0 || actual_type_return == NULL ||
        actual_format_return == NULL || item_count_return == NULL ||
        bytes_after_return == NULL || property_return == NULL)
        return 1;
    begin_packet(display, 24u, 0u);
    display->packet.payload[0] = DEMONX_GET_PROPERTY;
    display->packet.payload[1] = delete_after ? 1u : 0u;
    write16(&display->packet.payload[2], 6u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], property);
    write32(&display->packet.payload[12], requested_type);
    write32(&display->packet.payload[16], (uint32_t)long_offset);
    write32(&display->packet.payload[20], (uint32_t)long_length);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return 1;
    *actual_format_return = display->packet.payload[1];
    *actual_type_return = read32(&display->packet.payload[8]);
    *bytes_after_return = read32(&display->packet.payload[12]);
    *item_count_return = read32(&display->packet.payload[16]);
    const uint32_t unit = (uint32_t)*actual_format_return / 8u;
    uint32_t bytes = unit * (uint32_t)*item_count_return;
    if (bytes > 24u) bytes = 24u;
    for (uint32_t index = 0u; index < bytes; ++index)
        display->property_data[index] = display->packet.payload[32u + index];
    *property_return =
        *actual_type_return == None ? NULL : display->property_data;
    return 0;
}

Atom *XListProperties(Display *display, Window window,
                      int *property_count_return) {
    if (display != &singleton || !singleton_open ||
        property_count_return == NULL)
        return NULL;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_LIST_PROPERTIES;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], window);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return NULL;
    const uint16_t count = read16(&display->packet.payload[8]);
    if (count > 4u) return NULL;
    for (uint16_t index = 0u; index < count; ++index)
        display->query_atoms[index] =
            read32(&display->packet.payload[32u + index * 4u]);
    *property_count_return = count;
    return count == 0u ? NULL : display->query_atoms;
}

int XStoreName(Display *display, Window window, const char *name) {
    const size_t length = string_length(name);
    if (length > DEMONX_PROPERTY_DATA_BYTES) return 0;
    return XChangeProperty(display, window, XA_WM_NAME, XA_STRING, 8,
                           PropModeReplace, (const unsigned char *)name,
                           (int)length);
}

GC XCreateGC(Display *display, Drawable drawable,
             unsigned long value_mask, XGCValues *values) {
    if (display != &singleton || !singleton_open ||
        (value_mask & ~(unsigned long)(GCForeground | GCFont)) != 0u ||
        (value_mask != 0u && values == NULL))
        return NULL;
    struct _XGC *gc = NULL;
    for (size_t index = 0u; index < 4u; ++index)
        if (!gc_slots[index].used) { gc = &gc_slots[index]; break; }
    if (gc == NULL || display->next_resource > display->resource_mask)
        return NULL;
    gc->id = display->resource_base | display->next_resource++;
    gc->drawable = drawable;
    gc->used = 1;
    const uint16_t value_count =
        (uint16_t)(((value_mask & GCForeground) != 0u ? 1u : 0u) +
                   ((value_mask & GCFont) != 0u ? 1u : 0u));
    const uint16_t length = (uint16_t)(16u + value_count * 4u);
    begin_packet(display, length, 0u);
    display->packet.payload[0] = DEMONX_CREATE_GC;
    write16(&display->packet.payload[2], length / 4u);
    write32(&display->packet.payload[4], gc->id);
    write32(&display->packet.payload[8], drawable);
    write32(&display->packet.payload[12], (uint32_t)value_mask);
    uint16_t offset = 16u;
    if ((value_mask & GCForeground) != 0u) {
        write32(&display->packet.payload[offset], (uint32_t)values->foreground);
        offset += 4u;
    }
    if ((value_mask & GCFont) != 0u)
        write32(&display->packet.payload[offset], values->font);
    if (!send_packet(display)) {
        gc->used = 0;
        return NULL;
    }
    return gc;
}

int XSetForeground(Display *display, GC gc, unsigned long foreground) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used)
        return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CHANGE_GC;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], gc->id);
    write32(&display->packet.payload[8], DEMONX_GC_FOREGROUND);
    write32(&display->packet.payload[12], (uint32_t)foreground);
    return send_packet(display);
}

int XSetFont(Display *display, GC gc, Font font) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        font != 1u)
        return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CHANGE_GC;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], gc->id);
    write32(&display->packet.payload[8], DEMONX_GC_FONT);
    write32(&display->packet.payload[12], font);
    return send_packet(display);
}

int XSetClipRectangles(Display *display, GC gc, int clip_x_origin,
                       int clip_y_origin, XRectangle *rectangles,
                       int count, int ordering) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        ordering != Unsorted || count < 0 || count > 1 ||
        (count == 1 && rectangles == NULL))
        return 0;
    if (count == 0) {
        gc->clip_enabled = 0;
        return 1;
    }
    gc->clip_enabled = 1;
    gc->clip_x = clip_x_origin + rectangles[0].x;
    gc->clip_y = clip_y_origin + rectangles[0].y;
    gc->clip_width = rectangles[0].width;
    gc->clip_height = rectangles[0].height;
    return 1;
}

int XChangeGC(Display *display, GC gc, unsigned long value_mask,
              XGCValues *values) {
    if (values == NULL ||
        (value_mask & ~(unsigned long)(GCForeground | GCFont)) != 0u)
        return 0;
    if ((value_mask & GCForeground) != 0u &&
        !XSetForeground(display, gc, values->foreground))
        return 0;
    if ((value_mask & GCFont) != 0u && !XSetFont(display, gc, values->font))
        return 0;
    return 1;
}

XFontStruct *XLoadQueryFont(Display *display, const char *name) {
    if (display != &singleton || !singleton_open || name == NULL) return NULL;
    core_font_metrics = (XFontStruct){
        .fid = 1u, .min_char_or_byte2 = 32u, .max_char_or_byte2 = 126u,
        .all_chars_exist = True, .default_char = '?',
        .min_bounds = {.width = 6, .ascent = 7},
        .max_bounds = {.width = 6, .ascent = 7},
        .ascent = 7, .descent = 0
    };
    core_font_loaded = 1;
    return &core_font_metrics;
}

int XFreeFont(Display *display, XFontStruct *font) {
    if (display != &singleton || !core_font_loaded ||
        font != &core_font_metrics) return 0;
    core_font_loaded = 0;
    return 1;
}

int XTextWidth(XFontStruct *font, const char *text, int count) {
    if (font != &core_font_metrics || text == NULL || count < 0) return 0;
    return count * 6;
}

int XFreeGC(Display *display, GC gc) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used)
        return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_FREE_GC;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], gc->id);
    if (!send_packet(display)) return 0;
    gc->used = 0;
    return 1;
}

int XFillRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        width > UINT16_MAX || height > UINT16_MAX)
        return 0;
    if (gc->clip_enabled) {
        int right = x + (int)width;
        int bottom = y + (int)height;
        const int clip_right = gc->clip_x + (int)gc->clip_width;
        const int clip_bottom = gc->clip_y + (int)gc->clip_height;
        if (x < gc->clip_x) x = gc->clip_x;
        if (y < gc->clip_y) y = gc->clip_y;
        if (right > clip_right) right = clip_right;
        if (bottom > clip_bottom) bottom = clip_bottom;
        if (right <= x || bottom <= y) return 1;
        width = (unsigned int)(right - x);
        height = (unsigned int)(bottom - y);
    }
    begin_packet(display, 20u, 0u);
    display->packet.payload[0] = DEMONX_POLY_FILL_RECTANGLE;
    write16(&display->packet.payload[2], 5u);
    write32(&display->packet.payload[4], drawable);
    write32(&display->packet.payload[8], gc->id);
    write16(&display->packet.payload[12], (uint16_t)x);
    write16(&display->packet.payload[14], (uint16_t)y);
    write16(&display->packet.payload[16], (uint16_t)width);
    write16(&display->packet.payload[18], (uint16_t)height);
    return send_packet(display);
}

int XFillRectangles(Display *display, Drawable drawable, GC gc,
                    XRectangle *rectangles, int count) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        rectangles == NULL || count <= 0 ||
        count > (int)((DEMONX_MESSAGE_BYTES - 12u) / 8u))
        return 0;
    const uint16_t length = (uint16_t)(12u + (uint16_t)count * 8u);
    begin_packet(display, length, 0u);
    display->packet.payload[0] = DEMONX_POLY_FILL_RECTANGLE;
    write16(&display->packet.payload[2], length / 4u);
    write32(&display->packet.payload[4], drawable);
    write32(&display->packet.payload[8], gc->id);
    for (int index = 0; index < count; ++index) {
        uint8_t *wire = &display->packet.payload[12u + (size_t)index * 8u];
        write16(&wire[0], (uint16_t)rectangles[index].x);
        write16(&wire[2], (uint16_t)rectangles[index].y);
        write16(&wire[4], rectangles[index].width);
        write16(&wire[6], rectangles[index].height);
    }
    return send_packet(display);
}

int XDrawLine(Display *display, Drawable drawable, GC gc,
              int x1, int y1, int x2, int y2) {
    int dx = x2 >= x1 ? x2 - x1 : x1 - x2;
    int sx = x1 < x2 ? 1 : -1;
    int dy = y2 >= y1 ? y1 - y2 : y2 - y1;
    int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (!XFillRectangle(display, drawable, gc, x1, y1, 1u, 1u))
            return 0;
        if (x1 == x2 && y1 == y2) return 1;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x1 += sx; }
        if (doubled <= dx) { error += dx; y1 += sy; }
    }
}

int XDrawRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height) {
    if (width == 0u || height == 0u) return 1;
    if (!XFillRectangle(display, drawable, gc, x, y, width, 1u) ||
        !XFillRectangle(display, drawable, gc, x, y, 1u, height))
        return 0;
    if (height > 1u &&
        !XFillRectangle(display, drawable, gc, x,
                        y + (int)height - 1, width, 1u))
        return 0;
    return width <= 1u ||
        XFillRectangle(display, drawable, gc, x + (int)width - 1,
                       y, 1u, height);
}

int XDrawLines(Display *display, Drawable drawable, GC gc,
               XPoint *points, int count, int mode) {
    if (points == NULL || count < 2 ||
        (mode != CoordModeOrigin && mode != CoordModePrevious))
        return 0;
    int x = points[0].x;
    int y = points[0].y;
    for (int index = 1; index < count; ++index) {
        int next_x = points[index].x;
        int next_y = points[index].y;
        if (mode == CoordModePrevious) {
            next_x += x;
            next_y += y;
        }
        if (!XDrawLine(display, drawable, gc, x, y, next_x, next_y))
            return 0;
        x = next_x;
        y = next_y;
    }
    return 1;
}

int XDrawArc(Display *display, Drawable drawable, GC gc, int x, int y,
             unsigned int width, unsigned int height,
             int angle1, int angle2) {
    if (angle1 != 0 || angle2 != 360 * 64) return 0;
    if (width < 2u || height < 2u) return 0;
    const int cx = x + (int)width / 2;
    const int cy = y + (int)height / 2;
    const int rx = (int)width / 2;
    const int ry = (int)height / 2;
    for (int px = -rx; px <= rx; ++px) {
        const int64_t remain = (int64_t)rx * rx - (int64_t)px * px;
        int py = 0;
        while ((int64_t)(py + 1) * (py + 1) * rx * rx <=
               remain * ry * ry) ++py;
        if (!XFillRectangle(display, drawable, gc, cx + px, cy - py, 1u, 1u) ||
            !XFillRectangle(display, drawable, gc, cx + px, cy + py, 1u, 1u))
            return 0;
    }
    return 1;
}

int XFillArc(Display *display, Drawable drawable, GC gc, int x, int y,
             unsigned int width, unsigned int height,
             int angle1, int angle2) {
    if (angle1 != 0 || angle2 != 360 * 64 || width < 2u || height < 2u)
        return 0;
    const int rx = (int)width / 2;
    const int ry = (int)height / 2;
    const int cx = x + rx;
    for (int py = -ry; py <= ry; ++py) {
        const int64_t remain = (int64_t)ry * ry - (int64_t)py * py;
        int px = 0;
        while ((int64_t)(px + 1) * (px + 1) * ry * ry <=
               remain * rx * rx) ++px;
        if (!XFillRectangle(display, drawable, gc, cx - px, y + ry + py,
                            (unsigned int)(px * 2 + 1), 1u))
            return 0;
    }
    return 1;
}

int XFillPolygon(Display *display, Drawable drawable, GC gc, XPoint *points,
                 int count, int shape, int mode) {
    (void)shape;
    if (points == NULL || count < 3 ||
        (mode != CoordModeOrigin && mode != CoordModePrevious))
        return 0;
    /* A triangle fan is exact for PekWM's convex decoration polygons. */
    int origin_x = points[0].x, origin_y = points[0].y;
    for (int index = 1; index + 1 < count; ++index) {
        int x1 = points[index].x, y1 = points[index].y;
        int x2 = points[index + 1].x, y2 = points[index + 1].y;
        if (mode == CoordModePrevious) {
            x1 += origin_x; y1 += origin_y;
            x2 += x1; y2 += y1;
        }
        int min_y = origin_y;
        int max_y = origin_y;
        if (y1 < min_y) min_y = y1;
        if (y2 < min_y) min_y = y2;
        if (y1 > max_y) max_y = y1;
        if (y2 > max_y) max_y = y2;
        for (int scan = min_y; scan <= max_y; ++scan) {
            int intersections[3], found = 0;
            const int xs[3] = {origin_x, x1, x2};
            const int ys[3] = {origin_y, y1, y2};
            for (int edge = 0; edge < 3; ++edge) {
                const int next = (edge + 1) % 3;
                if ((scan < ys[edge]) == (scan < ys[next]) ||
                    ys[edge] == ys[next]) continue;
                intersections[found++] = xs[edge] +
                    (scan - ys[edge]) * (xs[next] - xs[edge]) /
                    (ys[next] - ys[edge]);
            }
            if (found == 2) {
                if (intersections[0] > intersections[1]) {
                    const int swap = intersections[0];
                    intersections[0] = intersections[1];
                    intersections[1] = swap;
                }
                if (!XFillRectangle(display, drawable, gc, intersections[0],
                                    scan,
                                    (unsigned int)(intersections[1] -
                                                   intersections[0] + 1),
                                    1u))
                    return 0;
            }
        }
    }
    return 1;
}

int XCopyArea(Display *display, Drawable source, Drawable destination, GC gc,
              int source_x, int source_y, unsigned int width,
              unsigned int height, int destination_x, int destination_y) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        width == 0u || height == 0u || width > UINT16_MAX ||
        height > UINT16_MAX)
        return 0;
    begin_packet(display, 28u, 0u);
    display->packet.payload[0] = DEMONX_COPY_AREA;
    write16(&display->packet.payload[2], 7u);
    write32(&display->packet.payload[4], source);
    write32(&display->packet.payload[8], destination);
    write32(&display->packet.payload[12], gc->id);
    write16(&display->packet.payload[16], (uint16_t)source_x);
    write16(&display->packet.payload[18], (uint16_t)source_y);
    write16(&display->packet.payload[20], (uint16_t)destination_x);
    write16(&display->packet.payload[22], (uint16_t)destination_y);
    write16(&display->packet.payload[24], (uint16_t)width);
    write16(&display->packet.payload[26], (uint16_t)height);
    return send_packet(display);
}

Pixmap XCreatePixmap(Display *display, Drawable drawable,
                     unsigned int width, unsigned int height,
                     unsigned int depth) {
    if (display != &singleton || !singleton_open || width == 0u ||
        height == 0u || width > UINT16_MAX || height > UINT16_MAX ||
        depth > UINT8_MAX || display->next_resource > display->resource_mask)
        return None;
    const Pixmap pixmap =
        display->resource_base | display->next_resource++;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_CREATE_PIXMAP;
    display->packet.payload[1] = (uint8_t)depth;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], pixmap);
    write32(&display->packet.payload[8], drawable);
    write16(&display->packet.payload[12], (uint16_t)width);
    write16(&display->packet.payload[14], (uint16_t)height);
    return send_packet(display) ? pixmap : None;
}

int XFreePixmap(Display *display, Pixmap pixmap) {
    if (display != &singleton || !singleton_open || pixmap == None) return 0;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_FREE_PIXMAP;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], pixmap);
    return send_packet(display);
}

XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data,
                     unsigned int width, unsigned int height,
                     int bitmap_pad, int bytes_per_line) {
    (void)visual;
    if (display != &singleton || !singleton_open || format != ZPixmap ||
        offset != 0 || width == 0u || height == 0u ||
        (depth != 24u && depth != 32u))
        return NULL;
    for (size_t index = 0u; index < 4u; ++index) {
        if (image_slot_used[index]) continue;
        image_slot_used[index] = 1;
        image_slots[index] = (XImage){
            .width = (int)width, .height = (int)height, .format = format,
            .data = data, .bitmap_unit = 32, .bitmap_pad = bitmap_pad,
            .depth = (int)depth,
            .bytes_per_line = bytes_per_line != 0
                ? bytes_per_line : (int)width * 4,
            .bits_per_pixel = 32
        };
        return &image_slots[index];
    }
    return NULL;
}

int XDestroyImage(XImage *image) {
    for (size_t index = 0u; index < 4u; ++index) {
        if (&image_slots[index] != image || !image_slot_used[index]) continue;
        image_slot_used[index] = 0;
        return 1;
    }
    return 0;
}

unsigned long XGetPixel(XImage *image, int x, int y) {
    if (image == NULL || image->data == NULL || x < 0 || y < 0 ||
        x >= image->width || y >= image->height ||
        image->bits_per_pixel != 32)
        return 0u;
    const uint8_t *pixel = (const uint8_t *)image->data +
        (size_t)y * (size_t)image->bytes_per_line + (size_t)x * 4u;
    return read32(pixel);
}

int XPutPixel(XImage *image, int x, int y, unsigned long pixel_value) {
    if (image == NULL || image->data == NULL || x < 0 || y < 0 ||
        x >= image->width || y >= image->height ||
        image->bits_per_pixel != 32)
        return 0;
    uint8_t *pixel = (uint8_t *)image->data +
        (size_t)y * (size_t)image->bytes_per_line + (size_t)x * 4u;
    write32(pixel, (uint32_t)pixel_value);
    return 1;
}

int XPutImage(Display *display, Drawable drawable, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        image == NULL || image->data == NULL || image->format != ZPixmap ||
        image->bits_per_pixel != 32 || src_x < 0 || src_y < 0 ||
        width == 0u || height == 0u ||
        (unsigned int)src_x + width > (unsigned int)image->width ||
        (unsigned int)src_y + height > (unsigned int)image->height)
        return 0;
    const unsigned int max_pixels =
        (DEMONX_MESSAGE_BYTES - 24u) / 4u;
    for (unsigned int row = 0u; row < height; ++row) {
        unsigned int column = 0u;
        while (column < width) {
            unsigned int count = width - column;
            if (count > max_pixels) count = max_pixels;
            const uint16_t length = (uint16_t)(24u + count * 4u);
            begin_packet(display, length, 0u);
            display->packet.payload[0] = DEMONX_PUT_IMAGE;
            display->packet.payload[1] = ZPixmap;
            write16(&display->packet.payload[2], length / 4u);
            write32(&display->packet.payload[4], drawable);
            write32(&display->packet.payload[8], gc->id);
            write16(&display->packet.payload[12], (uint16_t)count);
            write16(&display->packet.payload[14], 1u);
            write16(&display->packet.payload[16],
                    (uint16_t)(dest_x + (int)column));
            write16(&display->packet.payload[18],
                    (uint16_t)(dest_y + (int)row));
            display->packet.payload[20] = 0u;
            display->packet.payload[21] = 32u;
            const uint8_t *source = (const uint8_t *)image->data +
                (size_t)(src_y + (int)row) * (size_t)image->bytes_per_line +
                (size_t)(src_x + (int)column) * 4u;
            for (unsigned int index = 0u; index < count * 4u; ++index)
                display->packet.payload[24u + index] = source[index];
            if (!send_packet(display)) return 0;
            column += count;
        }
    }
    return 1;
}

XImage *XGetImage(Display *display, Drawable drawable, int x, int y,
                  unsigned int width, unsigned int height,
                  unsigned long plane_mask, int format) {
    if (display != &singleton || !singleton_open || width == 0u ||
        height == 0u || width * height > 56u || format != ZPixmap)
        return NULL;
    begin_packet(display, 20u, 0u);
    display->packet.payload[0] = DEMONX_GET_IMAGE;
    display->packet.payload[1] = (uint8_t)format;
    write16(&display->packet.payload[2], 5u);
    write32(&display->packet.payload[4], drawable);
    write16(&display->packet.payload[8], (uint16_t)x);
    write16(&display->packet.payload[10], (uint16_t)y);
    write16(&display->packet.payload[12], (uint16_t)width);
    write16(&display->packet.payload[14], (uint16_t)height);
    write32(&display->packet.payload[16], (uint32_t)plane_mask);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY ||
        display->packet.payload_length != 32u + width * height * 4u)
        return NULL;
    for (unsigned int index = 0u; index < width * height; ++index)
        display->image_data[index] =
            read32(&display->packet.payload[32u + index * 4u]);
    return XCreateImage(display, NULL, display->packet.payload[1],
                        ZPixmap, 0, (char *)display->image_data,
                        width, height, 32, (int)width * 4);
}

int XDrawString(Display *display, Drawable drawable, GC gc, int x, int y,
                const char *text, int length) {
    if (display != &singleton || !singleton_open || gc == NULL || !gc->used ||
        text == NULL || length < 0 || length > 240)
        return 0;
    const uint16_t request_length =
        (uint16_t)((17u + (unsigned int)length + 3u) & ~3u);
    begin_packet(display, request_length, 0u);
    display->packet.payload[0] = DEMONX_POLY_TEXT8;
    write16(&display->packet.payload[2], request_length / 4u);
    write32(&display->packet.payload[4], drawable);
    write32(&display->packet.payload[8], gc->id);
    write16(&display->packet.payload[12], (uint16_t)x);
    write16(&display->packet.payload[14], (uint16_t)y);
    display->packet.payload[16] = (uint8_t)length;
    for (int index = 0; index < length; ++index)
        display->packet.payload[17u + (unsigned int)index] =
            (uint8_t)text[index];
    return send_packet(display);
}

void XmbDrawString(Display *display, Drawable drawable, XFontSet font_set,
                   GC gc, int x, int y, const char *text, int length) {
    if (font_set != &font_set_slot || !font_set_slot.used) return;
    (void)XDrawString(display, drawable, gc, x, y, text, length);
}

void Xutf8DrawString(Display *display, Drawable drawable, XFontSet font_set,
                     GC gc, int x, int y, const char *text, int length) {
    /* The native core font is byte-oriented. ASCII UTF-8 is rendered
       directly; non-ASCII bytes retain deterministic replacement glyphs in
       the server's core-font renderer. */
    XmbDrawString(display, drawable, font_set, gc, x, y, text, length);
}

static int font_set_text_extents(XFontSet font_set, const char *text,
                                 int length, XRectangle *ink_return,
                                 XRectangle *logical_return) {
    if (font_set != &font_set_slot || !font_set_slot.used ||
        text == NULL || length < 0)
        return 0;
    const int width = XTextWidth(font_set_fonts[0], text, length);
    const int ascent = font_set_fonts[0]->ascent;
    const int height = ascent + font_set_fonts[0]->descent;
    XRectangle bounds = {
        0, (short)-ascent, (unsigned short)width, (unsigned short)height
    };
    if (ink_return != NULL) *ink_return = bounds;
    if (logical_return != NULL) *logical_return = bounds;
    return width;
}

int XmbTextExtents(XFontSet font_set, const char *text, int length,
                   XRectangle *ink_return, XRectangle *logical_return) {
    return font_set_text_extents(font_set, text, length, ink_return,
                                 logical_return);
}

int Xutf8TextExtents(XFontSet font_set, const char *text, int length,
                     XRectangle *ink_return, XRectangle *logical_return) {
    return font_set_text_extents(font_set, text, length, ink_return,
                                 logical_return);
}

int XmbTextPropertyToTextList(Display *display, XTextProperty *property,
                              char ***list_return, int *count_return) {
    if (display != &singleton || !singleton_open || property == NULL ||
        list_return == NULL || count_return == NULL ||
        property->value == NULL || property->format != 8)
        return -1;
    unsigned long count = property->nitems;
    if (count > sizeof(text_property_string) - 1u)
        count = sizeof(text_property_string) - 1u;
    for (unsigned long index = 0u; index < count; ++index)
        text_property_string[index] = (char)property->value[index];
    text_property_string[count] = '\0';
    text_property_list[0] = text_property_string;
    *list_return = text_property_list;
    *count_return = 1;
    return 0;
}

Status XSetWMProtocols(Display *display, Window window,
                       Atom *protocols, int count) {
    if (protocols == NULL || count < 0 ||
        count > (int)(DEMONX_PROPERTY_DATA_BYTES / 4u))
        return False;
    const Atom property = XInternAtom(display, "WM_PROTOCOLS", False);
    if (property == None) return False;
    return XChangeProperty(display, window, property, XA_ATOM, 32,
                           PropModeReplace,
                           (const unsigned char *)protocols, count)
        ? True : False;
}

Status XGetWMProtocols(Display *display, Window window,
                       Atom **protocols_return, int *count_return) {
    if (protocols_return == NULL || count_return == NULL) return False;
    const Atom property = XInternAtom(display, "WM_PROTOCOLS", True);
    Atom actual;
    int format;
    unsigned long count, after;
    unsigned char *data;
    if (property == None ||
        XGetWindowProperty(display, window, property, 0, 4, False, XA_ATOM,
                           &actual, &format, &count, &after, &data) != 0 ||
        actual != XA_ATOM || format != 32)
        return False;
    *protocols_return = (Atom *)data;
    *count_return = (int)count;
    return True;
}

Status XGetTransientForHint(Display *display, Window window,
                            Window *owner_return) {
    if (owner_return == NULL) return False;
    const Atom property = XInternAtom(display, "WM_TRANSIENT_FOR", True);
    Atom actual;
    int format;
    unsigned long count, after;
    unsigned char *data;
    if (property == None ||
        XGetWindowProperty(display, window, property, 0, 1, False, XA_WINDOW,
                           &actual, &format, &count, &after, &data) != 0 ||
        actual != XA_WINDOW || format != 32 || count != 1u)
        return False;
    *owner_return = read32(data);
    return True;
}

Status XSetClassHint(Display *display, Window window, XClassHint *hint) {
    if (hint == NULL || hint->res_name == NULL || hint->res_class == NULL)
        return False;
    const size_t name_length = string_length(hint->res_name);
    const size_t class_length = string_length(hint->res_class);
    if (name_length + class_length + 2u > 16u) return False;
    unsigned char data[16];
    for (size_t index = 0u; index < name_length; ++index)
        data[index] = (unsigned char)hint->res_name[index];
    data[name_length] = 0u;
    for (size_t index = 0u; index < class_length; ++index)
        data[name_length + 1u + index] =
            (unsigned char)hint->res_class[index];
    data[name_length + class_length + 1u] = 0u;
    const Atom property = XInternAtom(display, "WM_CLASS", False);
    return property != None &&
        XChangeProperty(display, window, property, XA_STRING, 8,
                        PropModeReplace, data,
                        (int)(name_length + class_length + 2u));
}

Status XGetClassHint(Display *display, Window window, XClassHint *hint) {
    if (hint == NULL) return False;
    const Atom property = XInternAtom(display, "WM_CLASS", True);
    Atom actual;
    int format;
    unsigned long count, after;
    unsigned char *data;
    if (property == None ||
        XGetWindowProperty(display, window, property, 0, 4, False, XA_STRING,
                           &actual, &format, &count, &after, &data) != 0 ||
        actual != XA_STRING || format != 8 || count < 2u)
        return False;
    size_t split = 0u;
    while (split < count && data[split] != 0u) ++split;
    if (split + 1u >= count) return False;
    hint->res_name = (char *)data;
    hint->res_class = (char *)&data[split + 1u];
    return True;
}

Status XGetTextProperty(Display *display, Window window,
                        XTextProperty *property_return, Atom property) {
    if (property_return == NULL) return False;
    unsigned long after;
    if (XGetWindowProperty(display, window, property, 0, 4, False,
                           AnyPropertyType, &property_return->encoding,
                           &property_return->format,
                           &property_return->nitems, &after,
                           &property_return->value) != 0)
        return False;
    return property_return->encoding != None;
}

int XSetWMHints(Display *display, Window window, XWMHints *hints) {
    if (hints == NULL) return 0;
    uint8_t data[16];
    write32(&data[0], (uint32_t)hints->flags);
    write32(&data[4], (uint32_t)hints->input);
    write32(&data[8], (uint32_t)hints->initial_state);
    write32(&data[12], hints->icon_window);
    const Atom property = XInternAtom(display, "WM_HINTS", False);
    return property != None &&
        XChangeProperty(display, window, property, property, 32,
                        PropModeReplace, data, 4);
}

XWMHints *XGetWMHints(Display *display, Window window) {
    const Atom property = XInternAtom(display, "WM_HINTS", True);
    Atom actual;
    int format;
    unsigned long count, after;
    unsigned char *data;
    if (property == None ||
        XGetWindowProperty(display, window, property, 0, 4, False, property,
                           &actual, &format, &count, &after, &data) != 0 ||
        actual != property || format != 32 || count < 3u)
        return NULL;
    zero_bytes(&wm_hints_slot, sizeof(wm_hints_slot));
    wm_hints_slot.flags = (long)read32(&data[0]);
    wm_hints_slot.input = (Bool)read32(&data[4]);
    wm_hints_slot.initial_state = (int)read32(&data[8]);
    if (count >= 4u) wm_hints_slot.icon_window = read32(&data[12]);
    return &wm_hints_slot;
}

XSizeHints *XAllocSizeHints(void) {
    zero_bytes(&size_hints_slot, sizeof(size_hints_slot));
    return &size_hints_slot;
}

void XFreeStringList(char **list) {
    (void)list;
}

Status XSendEvent(Display *display, Window window, Bool propagate,
                  long event_mask, XEvent *event) {
    if (display != &singleton || !singleton_open || event == NULL ||
        event->type != ClientMessage)
        return False;
    begin_packet(display, 44u, 0u);
    display->packet.payload[0] = DEMONX_SEND_EVENT;
    display->packet.payload[1] = propagate ? 1u : 0u;
    write16(&display->packet.payload[2], 11u);
    write32(&display->packet.payload[4], window);
    write32(&display->packet.payload[8], (uint32_t)event_mask);
    display->packet.payload[12] = ClientMessage;
    display->packet.payload[13] = (uint8_t)event->xclient.format;
    write32(&display->packet.payload[16], event->xclient.window);
    write32(&display->packet.payload[20], event->xclient.message_type);
    if (event->xclient.format != 8 && event->xclient.format != 16 &&
        event->xclient.format != 32)
        return False;
    if (event->xclient.format == 32) {
        for (uint32_t index = 0u; index < 5u; ++index)
            write32(&display->packet.payload[24u + index * 4u],
                    (uint32_t)event->xclient.data.l[index]);
    } else {
        const uint8_t *data = (const uint8_t *)&event->xclient.data;
        for (uint32_t index = 0u; index < 20u; ++index)
            display->packet.payload[24u + index] = data[index];
    }
    return send_packet(display) ? True : False;
}

Status XGetGeometry(Display *display, Window drawable, Window *root_return,
                    int *x_return, int *y_return,
                    unsigned int *width_return, unsigned int *height_return,
                    unsigned int *border_width_return,
                    unsigned int *depth_return) {
    if (display != &singleton || !singleton_open) return False;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_GET_GEOMETRY;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], drawable);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return False;
    if (root_return != NULL) *root_return = read32(&display->packet.payload[4]);
    if (x_return != NULL) *x_return = (int16_t)read16(&display->packet.payload[8]);
    if (y_return != NULL) *y_return = (int16_t)read16(&display->packet.payload[10]);
    if (width_return != NULL) *width_return = read16(&display->packet.payload[12]);
    if (height_return != NULL) *height_return = read16(&display->packet.payload[14]);
    if (border_width_return != NULL)
        *border_width_return = read16(&display->packet.payload[16]);
    if (depth_return != NULL) *depth_return = display->packet.payload[1];
    return True;
}

Bool XQueryPointer(Display *display, Window window, Window *root_return,
                   Window *child_return, int *root_x_return,
                   int *root_y_return, int *window_x_return,
                   int *window_y_return, unsigned int *mask_return) {
    if (display != &singleton || !singleton_open) return False;
    int window_x = display->pointer_x;
    int window_y = display->pointer_y;
    if (window != DEMONX_ROOT_WINDOW) {
        Window root;
        int x, y;
        unsigned int width, height, border, depth;
        if (!XGetGeometry(display, window, &root, &x, &y, &width, &height,
                          &border, &depth))
            return False;
        window_x -= x;
        window_y -= y;
    }
    if (root_return != NULL) *root_return = DEMONX_ROOT_WINDOW;
    if (child_return != NULL) *child_return = None;
    if (root_x_return != NULL) *root_x_return = display->pointer_x;
    if (root_y_return != NULL) *root_y_return = display->pointer_y;
    if (window_x_return != NULL) *window_x_return = window_x;
    if (window_y_return != NULL) *window_y_return = window_y;
    if (mask_return != NULL) *mask_return = display->pointer_mask;
    return True;
}

Bool XTranslateCoordinates(Display *display, Window source, Window destination,
                           int source_x, int source_y, int *destination_x,
                           int *destination_y, Window *child_return) {
    Window root;
    int sx = 0, sy = 0, dx = 0, dy = 0;
    unsigned int width, height, border, depth;
    if (source != DEMONX_ROOT_WINDOW &&
        !XGetGeometry(display, source, &root, &sx, &sy, &width, &height,
                      &border, &depth))
        return False;
    if (destination != DEMONX_ROOT_WINDOW &&
        !XGetGeometry(display, destination, &root, &dx, &dy, &width, &height,
                      &border, &depth))
        return False;
    if (destination_x != NULL) *destination_x = source_x + sx - dx;
    if (destination_y != NULL) *destination_y = source_y + sy - dy;
    if (child_return != NULL) *child_return = None;
    return True;
}

Window XGetSelectionOwner(Display *display, Atom selection) {
    if (display != &singleton || !singleton_open) return None;
    begin_packet(display, 8u, 0u);
    display->packet.payload[0] = DEMONX_GET_SELECTION_OWNER;
    write16(&display->packet.payload[2], 2u);
    write32(&display->packet.payload[4], selection);
    if (!send_packet(display) || !receive_packet(display, 0) ||
        display->packet.flags != DEMONX_FLAG_REPLY)
        return None;
    return read32(&display->packet.payload[8]);
}

int XSetSelectionOwner(Display *display, Atom selection, Window owner,
                       unsigned long time) {
    if (display != &singleton || !singleton_open) return 0;
    begin_packet(display, 16u, 0u);
    display->packet.payload[0] = DEMONX_SET_SELECTION_OWNER;
    write16(&display->packet.payload[2], 4u);
    write32(&display->packet.payload[4], selection);
    write32(&display->packet.payload[8], owner);
    write32(&display->packet.payload[12], (uint32_t)time);
    return send_packet(display);
}

XModifierKeymap *XGetModifierMapping(Display *display) {
    return display == &singleton && singleton_open ? &modifier_map : NULL;
}

int XFreeModifiermap(XModifierKeymap *mapping) {
    return mapping == &modifier_map ? 1 : 0;
}

KeySym *XGetKeyboardMapping(Display *display, KeyCode first, int count,
                            int *symbols_per_keycode) {
    if (display != &singleton || !singleton_open || count <= 0 ||
        count > 16 || symbols_per_keycode == NULL)
        return NULL;
    for (int index = 0; index < count; ++index)
        keyboard_symbols[index] = (KeySym)(first + (KeyCode)index);
    *symbols_per_keycode = 1;
    return keyboard_symbols;
}

int XRefreshKeyboardMapping(XEvent *event) {
    return event == NULL ? 0 : 1;
}

int XNextEvent(Display *display, XEvent *event_return) {
    if (display != &singleton || event_return == NULL || !singleton_open)
        return -1;
    if (display->has_putback) {
        *event_return = display->putback;
        display->has_putback = 0;
        return 0;
    }
    for (;;) {
        if (display->has_pending) {
            display->packet = display->pending;
            display->has_pending = 0;
        } else if (!receive_packet(display, 0)) {
            return -1;
        }
        if (display->packet.flags != DEMONX_FLAG_EVENT) continue;
        zero_bytes(event_return, sizeof(*event_return));
        event_return->type = display->packet.payload[0] & 0x7fu;
        if (event_return->type == KeyPress ||
            event_return->type == KeyRelease) {
            event_return->xkey.type = event_return->type;
            event_return->xkey.serial =
                read16(&display->packet.payload[2]);
            event_return->xkey.display = display;
            event_return->xkey.root =
                read32(&display->packet.payload[8]);
            event_return->xkey.window =
                read32(&display->packet.payload[12]);
            event_return->xkey.keycode = display->packet.payload[1];
            event_return->xkey.value =
                read32(&display->packet.payload[16]);
            event_return->xkey.x =
                (int16_t)read16(&display->packet.payload[20]);
            event_return->xkey.y =
                (int16_t)read16(&display->packet.payload[22]);
            event_return->xkey.state =
                read32(&display->packet.payload[24]);
            event_return->xkey.same_screen =
                display->packet.payload[30] != 0u;
        } else if (event_return->type == ButtonPress ||
                   event_return->type == ButtonRelease) {
            event_return->xbutton.type = event_return->type;
            event_return->xbutton.serial =
                read16(&display->packet.payload[2]);
            event_return->xbutton.display = display;
            event_return->xbutton.root =
                read32(&display->packet.payload[8]);
            event_return->xbutton.window =
                read32(&display->packet.payload[12]);
            event_return->xbutton.button = display->packet.payload[1];
            event_return->xbutton.x =
                (int16_t)read16(&display->packet.payload[20]);
            event_return->xbutton.y =
                (int16_t)read16(&display->packet.payload[22]);
            event_return->xbutton.state =
                read32(&display->packet.payload[24]);
            event_return->xbutton.same_screen =
                display->packet.payload[30] != 0u;
        } else if (event_return->type == MotionNotify) {
            event_return->xmotion.type = MotionNotify;
            event_return->xmotion.serial =
                read16(&display->packet.payload[2]);
            event_return->xmotion.display = display;
            event_return->xmotion.root =
                read32(&display->packet.payload[8]);
            event_return->xmotion.window =
                read32(&display->packet.payload[12]);
            event_return->xmotion.x =
                (int16_t)read16(&display->packet.payload[20]);
            event_return->xmotion.y =
                (int16_t)read16(&display->packet.payload[22]);
            event_return->xmotion.state =
                read32(&display->packet.payload[24]);
            event_return->xmotion.is_hint =
                (char)display->packet.payload[29];
            event_return->xmotion.same_screen =
                display->packet.payload[30] != 0u;
            display->pointer_x = event_return->xmotion.x;
            display->pointer_y = event_return->xmotion.y;
            display->pointer_mask = event_return->xmotion.state;
        } else if (event_return->type == FocusIn ||
                   event_return->type == FocusOut) {
            event_return->xfocus.type = event_return->type;
            event_return->xfocus.serial =
                read16(&display->packet.payload[2]);
            event_return->xfocus.display = display;
            event_return->xfocus.window =
                read32(&display->packet.payload[4]);
            event_return->xfocus.mode = display->packet.payload[8];
            event_return->xfocus.detail = display->packet.payload[1];
        } else if (event_return->type == MapNotify) {
            event_return->xmap.type = MapNotify;
            event_return->xmap.serial = read16(&display->packet.payload[2]);
            event_return->xmap.display = display;
            event_return->xmap.event = read32(&display->packet.payload[4]);
            event_return->xmap.window = read32(&display->packet.payload[8]);
        } else if (event_return->type == MapRequest) {
            event_return->xmaprequest.type = MapRequest;
            event_return->xmaprequest.serial =
                read16(&display->packet.payload[2]);
            event_return->xmaprequest.display = display;
            event_return->xmaprequest.parent =
                read32(&display->packet.payload[4]);
            event_return->xmaprequest.window =
                read32(&display->packet.payload[8]);
        } else if (event_return->type == ConfigureNotify) {
            event_return->xconfigure.type = ConfigureNotify;
            event_return->xconfigure.serial =
                read16(&display->packet.payload[2]);
            event_return->xconfigure.display = display;
            event_return->xconfigure.event =
                read32(&display->packet.payload[4]);
            event_return->xconfigure.window =
                read32(&display->packet.payload[8]);
            event_return->xconfigure.above =
                read32(&display->packet.payload[12]);
            event_return->xconfigure.x =
                (int16_t)read16(&display->packet.payload[16]);
            event_return->xconfigure.y =
                (int16_t)read16(&display->packet.payload[18]);
            event_return->xconfigure.width =
                read16(&display->packet.payload[20]);
            event_return->xconfigure.height =
                read16(&display->packet.payload[22]);
            event_return->xconfigure.border_width =
                read16(&display->packet.payload[24]);
        } else if (event_return->type == ConfigureRequest) {
            event_return->xconfigurerequest.type = ConfigureRequest;
            event_return->xconfigurerequest.serial =
                read16(&display->packet.payload[2]);
            event_return->xconfigurerequest.display = display;
            event_return->xconfigurerequest.parent =
                read32(&display->packet.payload[4]);
            event_return->xconfigurerequest.window =
                read32(&display->packet.payload[8]);
            event_return->xconfigurerequest.above =
                read32(&display->packet.payload[12]);
            event_return->xconfigurerequest.x =
                (int16_t)read16(&display->packet.payload[16]);
            event_return->xconfigurerequest.y =
                (int16_t)read16(&display->packet.payload[18]);
            event_return->xconfigurerequest.width =
                read16(&display->packet.payload[20]);
            event_return->xconfigurerequest.height =
                read16(&display->packet.payload[22]);
            event_return->xconfigurerequest.border_width =
                read16(&display->packet.payload[24]);
            event_return->xconfigurerequest.value_mask =
                read16(&display->packet.payload[26]);
        } else if (event_return->type == ReparentNotify) {
            event_return->xreparent.type = ReparentNotify;
            event_return->xreparent.serial =
                read16(&display->packet.payload[2]);
            event_return->xreparent.display = display;
            event_return->xreparent.event =
                read32(&display->packet.payload[4]);
            event_return->xreparent.window =
                read32(&display->packet.payload[8]);
            event_return->xreparent.parent =
                read32(&display->packet.payload[12]);
            event_return->xreparent.x =
                (int16_t)read16(&display->packet.payload[16]);
            event_return->xreparent.y =
                (int16_t)read16(&display->packet.payload[18]);
        } else if (event_return->type == UnmapNotify) {
            event_return->xunmap.type = UnmapNotify;
            event_return->xunmap.serial =
                read16(&display->packet.payload[2]);
            event_return->xunmap.display = display;
            event_return->xunmap.event =
                read32(&display->packet.payload[4]);
            event_return->xunmap.window =
                read32(&display->packet.payload[8]);
        } else if (event_return->type == PropertyNotify) {
            event_return->xproperty.type = PropertyNotify;
            event_return->xproperty.serial =
                read16(&display->packet.payload[2]);
            event_return->xproperty.display = display;
            event_return->xproperty.window =
                read32(&display->packet.payload[4]);
            event_return->xproperty.atom =
                read32(&display->packet.payload[8]);
            event_return->xproperty.time =
                read32(&display->packet.payload[12]);
            event_return->xproperty.state = display->packet.payload[16];
        } else if (event_return->type == ClientMessage) {
            event_return->xclient.type = ClientMessage;
            event_return->xclient.serial =
                read16(&display->packet.payload[2]);
            event_return->xclient.send_event =
                (display->packet.payload[0] & 0x80u) != 0u;
            event_return->xclient.display = display;
            event_return->xclient.format = display->packet.payload[1];
            event_return->xclient.window =
                read32(&display->packet.payload[4]);
            event_return->xclient.message_type =
                read32(&display->packet.payload[8]);
            if (event_return->xclient.format == 32) {
                for (uint32_t index = 0u; index < 5u; ++index)
                    event_return->xclient.data.l[index] =
                        (long)read32(&display->packet.payload[12u + index * 4u]);
            } else {
                uint8_t *data = (uint8_t *)&event_return->xclient.data;
                for (uint32_t index = 0u; index < 20u; ++index)
                    data[index] = display->packet.payload[12u + index];
            }
        }
        return 0;
    }
}

int XPending(Display *display) {
    if (display != &singleton || !singleton_open) return 0;
    if (display->has_putback) return 1;
    if (display->has_pending) return 1;
    if (!receive_packet(display, 1) ||
        display->packet.flags != DEMONX_FLAG_EVENT)
        return 0;
    display->pending = display->packet;
    display->has_pending = 1;
    return 1;
}

static long event_mask_for_type(int type) {
    if (type == KeyPress) return KeyPressMask;
    if (type == KeyRelease) return KeyReleaseMask;
    if (type == ButtonPress) return ButtonPressMask;
    if (type == ButtonRelease) return ButtonReleaseMask;
    if (type == MotionNotify) return PointerMotionMask;
    if (type == FocusIn || type == FocusOut) return FocusChangeMask;
    if (type == MapNotify || type == ConfigureNotify ||
        type == UnmapNotify || type == ReparentNotify)
        return StructureNotifyMask;
    if (type == PropertyNotify) return PropertyChangeMask;
    return 0;
}

Bool XCheckTypedEvent(Display *display, int type, XEvent *event_return) {
    if (!XPending(display)) return False;
    const int pending_type = display->has_putback
        ? display->putback.type :
          (int)(display->pending.payload[0] & 0x7fu);
    return pending_type == type &&
        XNextEvent(display, event_return) == 0 ? True : False;
}

Bool XCheckMaskEvent(Display *display, long mask, XEvent *event_return) {
    if (!XPending(display)) return False;
    const int type = display->has_putback
        ? display->putback.type :
          (int)(display->pending.payload[0] & 0x7fu);
    return (event_mask_for_type(type) & mask) != 0 &&
        XNextEvent(display, event_return) == 0 ? True : False;
}

Bool XCheckTypedWindowEvent(Display *display, Window window, int type,
                            XEvent *event_return) {
    XEvent event;
    if (!XCheckTypedEvent(display, type, &event)) return False;
    if (event.xany.window == window) {
        *event_return = event;
        return True;
    }
    (void)XPutBackEvent(display, &event);
    return False;
}

Bool XCheckWindowEvent(Display *display, Window window, long mask,
                       XEvent *event_return) {
    XEvent event;
    if (!XCheckMaskEvent(display, mask, &event)) return False;
    if (event.xany.window == window) {
        *event_return = event;
        return True;
    }
    (void)XPutBackEvent(display, &event);
    return False;
}

int XMaskEvent(Display *display, long mask, XEvent *event_return) {
    XEvent event;
    if (XNextEvent(display, &event) != 0) return -1;
    if ((event_mask_for_type(event.type) & mask) == 0) {
        (void)XPutBackEvent(display, &event);
        return -1;
    }
    *event_return = event;
    return 0;
}

int XWindowEvent(Display *display, Window window, long mask,
                 XEvent *event_return) {
    XEvent event;
    if (XNextEvent(display, &event) != 0) return -1;
    if (event.xany.window != window ||
        (event_mask_for_type(event.type) & mask) == 0) {
        (void)XPutBackEvent(display, &event);
        return -1;
    }
    *event_return = event;
    return 0;
}

int XFlush(Display *display) {
    return display == &singleton && singleton_open ? 0 : -1;
}

int XSync(Display *display, Bool discard) {
    (void)discard;
    return XFlush(display);
}

int XPutBackEvent(Display *display, XEvent *event) {
    if (display != &singleton || !singleton_open || event == NULL ||
        display->has_putback)
        return 0;
    display->putback = *event;
    display->has_putback = 1;
    return 1;
}

int XGetErrorText(Display *display, int code, char *buffer, int length) {
    if (display != &singleton || !singleton_open || buffer == NULL ||
        length <= 0)
        return 0;
    const char *text = "DemonX protocol error";
    int index = 0;
    while (index + 1 < length && text[index] != '\0') {
        buffer[index] = text[index];
        ++index;
    }
    if (index + 5 < length) {
        buffer[index++] = ' ';
        buffer[index++] = '(';
        buffer[index++] = (char)('0' + (code / 10) % 10);
        buffer[index++] = (char)('0' + code % 10);
        buffer[index++] = ')';
    }
    buffer[index] = '\0';
    return 0;
}

static int synchronous_after(Display *display) {
    return XFlush(display);
}

XAfterFunction XSetAfterFunction(Display *display, XAfterFunction function) {
    if (display != &singleton || !singleton_open) return NULL;
    XAfterFunction previous = after_function;
    after_function = function;
    return previous;
}

XAfterFunction XSynchronize(Display *display, Bool enabled) {
    return XSetAfterFunction(display, enabled ? synchronous_after : NULL);
}

XErrorHandler XSetErrorHandler(XErrorHandler handler) {
    XErrorHandler previous = error_handler;
    error_handler = handler;
    return previous;
}

long XMaxRequestSize(Display *display) {
    return display == &singleton && singleton_open
        ? (long)(DEMONX_MESSAGE_BYTES / 4u) : 0;
}

Colormap XCreateColormap(Display *display, Window window, Visual *visual,
                         int allocate) {
    (void)window;
    (void)visual;
    return display == &singleton && singleton_open && allocate == AllocNone
        ? 1u : None;
}

int XFreeColormap(Display *display, Colormap colormap) {
    return display == &singleton && singleton_open && colormap == 1u;
}

int XInstallColormap(Display *display, Colormap colormap) {
    return XFreeColormap(display, colormap);
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int text_equal(const char *left, const char *right) {
    size_t index = 0u;
    if (left == NULL || right == NULL) return 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

Status XParseColor(Display *display, Colormap colormap,
                   const char *specification, XColor *exact_return) {
    if (display != &singleton || !singleton_open || colormap != 1u ||
        specification == NULL || exact_return == NULL)
        return False;
    unsigned int red = 0u, green = 0u, blue = 0u;
    if (specification[0] == '#' && string_length(specification) == 7u) {
        int digits[6];
        for (int index = 0; index < 6; ++index) {
            digits[index] = hex_digit(specification[index + 1]);
            if (digits[index] < 0) return False;
        }
        red = (unsigned int)(digits[0] * 16 + digits[1]);
        green = (unsigned int)(digits[2] * 16 + digits[3]);
        blue = (unsigned int)(digits[4] * 16 + digits[5]);
    } else if (text_equal(specification, "black")) {
        red = green = blue = 0u;
    } else if (text_equal(specification, "white")) {
        red = green = blue = 255u;
    } else if (text_equal(specification, "red")) {
        red = 255u;
    } else if (text_equal(specification, "green")) {
        green = 255u;
    } else if (text_equal(specification, "blue")) {
        blue = 255u;
    } else {
        return False;
    }
    exact_return->red = (unsigned short)(red * 257u);
    exact_return->green = (unsigned short)(green * 257u);
    exact_return->blue = (unsigned short)(blue * 257u);
    exact_return->flags = 7u;
    exact_return->pad = 0u;
    exact_return->pixel = (red << 16u) | (green << 8u) | blue;
    return True;
}

Status XAllocNamedColor(Display *display, Colormap colormap,
                        const char *name, XColor *screen_return,
                        XColor *exact_return) {
    if (!XParseColor(display, colormap, name, exact_return) ||
        screen_return == NULL)
        return False;
    *screen_return = *exact_return;
    return True;
}

int XFreeColors(Display *display, Colormap colormap,
                unsigned long *pixels, int count, unsigned long planes) {
    (void)pixels;
    (void)planes;
    /* DemonX's direct TrueColor pixels require no server-side allocation. */
    return display == &singleton && singleton_open && colormap == 1u &&
        count >= 0;
}

KeySym XStringToKeysym(const char *name) {
    if (name == NULL || name[0] == '\0') return NoSymbol;
    if (name[1] == '\0') return (KeySym)(unsigned char)name[0];
    if (text_equal(name, "Return")) return 0xff0du;
    if (text_equal(name, "Escape")) return 0xff1bu;
    if (text_equal(name, "BackSpace")) return 0xff08u;
    if (text_equal(name, "Tab")) return 0xff09u;
    if (text_equal(name, "Delete")) return 0xffffu;
    if (text_equal(name, "Left")) return 0xff51u;
    if (text_equal(name, "Up")) return 0xff52u;
    if (text_equal(name, "Right")) return 0xff53u;
    if (text_equal(name, "Down")) return 0xff54u;
    if (text_equal(name, "Home")) return 0xff50u;
    if (text_equal(name, "End")) return 0xff57u;
    if (text_equal(name, "space")) return 0x20u;
    if (text_equal(name, "Num_Lock")) return 0xff7fu;
    if (text_equal(name, "Scroll_Lock")) return 0xff14u;
    return NoSymbol;
}

KeyCode XKeysymToKeycode(Display *display, KeySym keysym) {
    if (display != &singleton || !singleton_open) return 0u;
    if (keysym <= 0xffu) return (KeyCode)keysym;
    switch (keysym) {
        case 0xff08u: return 8u;
        case 0xff09u: return 9u;
        case 0xff0du: return 13u;
        case 0xff1bu: return 27u;
        case 0xffffu: return 127u;
        default: return 0u;
    }
}

KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index) {
    if (display != &singleton || !singleton_open || index != 0)
        return NoSymbol;
    return (KeySym)keycode;
}

int XLookupString(XKeyEvent *event, char *buffer, int bytes,
                  KeySym *keysym_return, void *compose) {
    (void)compose;
    if (event == NULL || bytes < 0) return 0;
    KeySym symbol = event->value != 0u
        ? (KeySym)event->value : (KeySym)event->keycode;
    if (keysym_return != NULL) *keysym_return = symbol;
    if (buffer == NULL || bytes == 0 || symbol > 0xffu) return 0;
    buffer[0] = (char)symbol;
    return 1;
}

unsigned long XVisualIDFromVisual(Visual *visual) {
    /* DemonX currently exposes one fixed TrueColor visual. */
    return visual == NULL ? 0u : visual->visualid;
}

XVisualInfo *XGetVisualInfo(Display *display, long mask,
                            XVisualInfo *template_info,
                            int *count_return) {
    if (count_return != NULL) *count_return = 0;
    if (display != &singleton || !singleton_open || count_return == NULL)
        return NULL;
    if ((mask & VisualIDMask) != 0 &&
        (template_info == NULL ||
         template_info->visualid != native_visual.visualid))
        return NULL;
    native_visual_info = (XVisualInfo){
        &native_visual, native_visual.visualid, 0, 32, TrueColor,
        native_visual.red_mask, native_visual.green_mask,
        native_visual.blue_mask, native_visual.map_entries,
        native_visual.bits_per_rgb
    };
    *count_return = 1;
    return &native_visual_info;
}

int XSetNormalHints(Display *display, Window window, XSizeHints *hints) {
    if (hints == NULL) return 0;
    uint8_t data[24];
    write32(&data[0], (uint32_t)hints->flags);
    write16(&data[4], (uint16_t)hints->min_width);
    write16(&data[6], (uint16_t)hints->min_height);
    write16(&data[8], (uint16_t)hints->max_width);
    write16(&data[10], (uint16_t)hints->max_height);
    write16(&data[12], (uint16_t)hints->width_inc);
    write16(&data[14], (uint16_t)hints->height_inc);
    write16(&data[16], (uint16_t)hints->base_width);
    write16(&data[18], (uint16_t)hints->base_height);
    write32(&data[20], (uint32_t)hints->win_gravity);
    const Atom property = XInternAtom(display, "WM_NORMAL_HINTS", False);
    return property != None &&
        XChangeProperty(display, window, property, property, 32,
                        PropModeReplace, data, 6);
}

Status XGetWMNormalHints(Display *display, Window window, XSizeHints *hints,
                         long *supplied_return) {
    if (hints == NULL) return False;
    const Atom property = XInternAtom(display, "WM_NORMAL_HINTS", True);
    Atom actual;
    int format;
    unsigned long count, after;
    unsigned char *data;
    if (property == None ||
        XGetWindowProperty(display, window, property, 0, 6, False, property,
                           &actual, &format, &count, &after, &data) != 0 ||
        actual != property || format != 32 || count != 6u)
        return False;
    zero_bytes(hints, sizeof(*hints));
    hints->flags = (long)read32(&data[0]);
    hints->min_width = read16(&data[4]);
    hints->min_height = read16(&data[6]);
    hints->max_width = read16(&data[8]);
    hints->max_height = read16(&data[10]);
    hints->width_inc = read16(&data[12]);
    hints->height_inc = read16(&data[14]);
    hints->base_width = read16(&data[16]);
    hints->base_height = read16(&data[18]);
    hints->win_gravity = (int)read32(&data[20]);
    if (supplied_return != NULL) *supplied_return = hints->flags;
    return True;
}

XFontSet XCreateFontSet(Display *display, const char *name,
                        char ***missing, int *missing_count,
                        char **default_string) {
    XFontStruct *font = XLoadQueryFont(display, name);
    if (font == NULL || font_set_slot.used) return NULL;
    font_set_slot.used = 1;
    font_set_fonts[0] = font;
    font_set_names[0] = (char *)name;
    if (missing != NULL) *missing = NULL;
    if (missing_count != NULL) *missing_count = 0;
    if (default_string != NULL) *default_string = (char *)"?";
    return &font_set_slot;
}

int XFontsOfFontSet(XFontSet set, XFontStruct ***fonts, char ***names) {
    if (set != &font_set_slot || !font_set_slot.used) return 0;
    if (fonts != NULL) *fonts = font_set_fonts;
    if (names != NULL) *names = font_set_names;
    return 1;
}

void XFreeFontSet(Display *display, XFontSet set) {
    if (display == &singleton && set == &font_set_slot &&
        font_set_slot.used) {
        (void)XFreeFont(display, font_set_fonts[0]);
        font_set_slot.used = 0;
    }
}

Bool XRRQueryExtension(Display *display, int *event_base, int *error_base) {
    (void)display;
    if (event_base != NULL) *event_base = 0;
    if (error_base != NULL) *error_base = 0;
    return False;
}

Status XRRQueryVersion(Display *display, int *major, int *minor) {
    (void)display;
    if (major != NULL) *major = 0;
    if (minor != NULL) *minor = 0;
    return False;
}

XRRScreenResources *XRRGetScreenResources(Display *display, Window window) {
    (void)display; (void)window; return NULL;
}
XRRCrtcInfo *XRRGetCrtcInfo(Display *display, XRRScreenResources *resources,
                            RRCrtc crtc) {
    (void)display; (void)resources; (void)crtc; return NULL;
}
XRROutputInfo *XRRGetOutputInfo(Display *display,
                                XRRScreenResources *resources,
                                RROutput output) {
    (void)display; (void)resources; (void)output; return NULL;
}
void XRRFreeCrtcInfo(XRRCrtcInfo *info) { (void)info; }
void XRRFreeOutputInfo(XRROutputInfo *info) { (void)info; }
void XRRFreeScreenResources(XRRScreenResources *resources) {
    (void)resources;
}
Status XRRSetCrtcConfig(Display *display, XRRScreenResources *resources,
                        RRCrtc crtc, unsigned long time, int x, int y,
                        RRMode mode, Rotation rotation, RROutput *outputs,
                        int count) {
    (void)display; (void)resources; (void)crtc; (void)time; (void)x; (void)y;
    (void)mode; (void)rotation; (void)outputs; (void)count;
    return 1;
}
void XRRSelectInput(Display *display, Window window, int mask) {
    (void)display; (void)window; (void)mask;
}
int XRRUpdateConfiguration(XEvent *event) { (void)event; return 0; }
RROutput XRRGetOutputPrimary(Display *display, Window window) {
    (void)display; (void)window; return None;
}
int XRRGetOutputProperty(Display *display, RROutput output, Atom property,
                         long offset, long length, Bool delete_after,
                         Bool pending, Atom requested, Atom *actual,
                         int *format, unsigned long *count,
                         unsigned long *after, unsigned char **data) {
    (void)display; (void)output; (void)property; (void)offset; (void)length;
    (void)delete_after; (void)pending; (void)requested; (void)actual;
    (void)format; (void)count; (void)after; (void)data;
    return 1;
}
void XRRSetScreenSize(Display *display, Window window, int width, int height,
                      int millimeters_width, int millimeters_height) {
    (void)display; (void)window; (void)width; (void)height;
    (void)millimeters_width; (void)millimeters_height;
}

Bool XShapeQueryExtension(Display *display, int *event_base, int *error_base) {
    return XRRQueryExtension(display, event_base, error_base);
}
void XShapeCombineRectangles(Display *display, Window window, int kind,
                             int x, int y, XRectangle *rectangles, int count,
                             int operation, int ordering) {
    (void)display; (void)window; (void)kind; (void)x; (void)y;
    (void)rectangles; (void)count; (void)operation; (void)ordering;
}
void XShapeCombineShape(Display *display, Window window, int kind, int x,
                        int y, Window source, int source_kind, int operation) {
    (void)display; (void)window; (void)kind; (void)x; (void)y;
    (void)source; (void)source_kind; (void)operation;
}
void XShapeCombineMask(Display *display, Window window, int kind, int x, int y,
                       Pixmap source, int operation) {
    (void)display; (void)window; (void)kind; (void)x; (void)y;
    (void)source; (void)operation;
}
Status XShapeQueryExtents(Display *display, Window window, Bool *bounding,
                          int *bx, int *by, unsigned int *bw,
                          unsigned int *bh, Bool *clip, int *cx, int *cy,
                          unsigned int *cw, unsigned int *ch) {
    (void)display; (void)window; (void)bounding; (void)bx; (void)by;
    (void)bw; (void)bh; (void)clip; (void)cx; (void)cy; (void)cw; (void)ch;
    return False;
}
void XShapeSelectInput(Display *display, Window window, unsigned long mask) {
    (void)display; (void)window; (void)mask;
}

Bool XScreenSaverQueryExtension(Display *display, int *event_base,
                                int *error_base) {
    return XRRQueryExtension(display, event_base, error_base);
}
Status XScreenSaverQueryInfo(Display *display, Drawable drawable,
                             XScreenSaverInfo *info) {
    (void)display; (void)drawable; (void)info; return False;
}

Bool XineramaIsActive(Display *display) {
    (void)display;
    /* DemonX exposes one root coordinate space. PekWM consequently uses its
       ordinary single-head path rather than inventing monitor partitions. */
    return False;
}

XineramaScreenInfo *XineramaQueryScreens(Display *display,
                                         int *screen_count_return) {
    (void)display;
    if (screen_count_return != NULL) *screen_count_return = 0;
    return NULL;
}

Status XdbeQueryExtension(Display *display, int *major_version_return,
                          int *minor_version_return) {
    (void)display;
    if (major_version_return != NULL) *major_version_return = 0;
    if (minor_version_return != NULL) *minor_version_return = 0;
    /* Native DemonX surfaces are already retained and composited, but they
       do not implement Xdbe's swap-action contract. Report that boundary
       honestly so clients select their pixmap software-buffer fallback. */
    return False;
}

XdbeBackBuffer XdbeAllocateBackBufferName(Display *display, Window window,
                                           XdbeSwapAction swap_action) {
    (void)display; (void)window; (void)swap_action;
    return None;
}

Status XdbeDeallocateBackBufferName(Display *display,
                                     XdbeBackBuffer back_buffer) {
    (void)display; (void)back_buffer;
    return False;
}

Status XdbeSwapBuffers(Display *display, XdbeSwapInfo *swap_info, int count) {
    (void)display; (void)swap_info; (void)count;
    return False;
}

#ifndef DEMON_DEMONX_H
#define DEMON_DEMONX_H

#include <stdint.h>

#define DEMONX_TRANSPORT_MAGIC 0x58313144u /* "D11X", little endian */
#define DEMONX_TRANSPORT_BYTES 64u
#define DEMONX_PAYLOAD_BYTES 48u
#define DEMONX_MESSAGE_BYTES 256u
#define DEMONX_ROOT_WINDOW 0x00000100u
#define DEMONX_RESOURCE_BASE 0x00200000u
#define DEMONX_RESOURCE_MASK 0x001fffffu
#define DEMONX_MAX_CLIENTS 8u

enum demonx_transport_flag {
    DEMONX_FLAG_SETUP = 1u,
    DEMONX_FLAG_REPLY = 2u,
    DEMONX_FLAG_ERROR = 4u,
    DEMONX_FLAG_EVENT = 8u,
    DEMONX_FLAG_MORE = 16u,
};

enum demonx_core_opcode {
    DEMONX_CREATE_WINDOW = 1u,
    DEMONX_CHANGE_WINDOW_ATTRIBUTES = 2u,
    DEMONX_GET_WINDOW_ATTRIBUTES = 3u,
    DEMONX_DESTROY_WINDOW = 4u,
    DEMONX_CHANGE_SAVE_SET = 6u,
    DEMONX_REPARENT_WINDOW = 7u,
    DEMONX_MAP_WINDOW = 8u,
    DEMONX_UNMAP_WINDOW = 10u,
    DEMONX_CONFIGURE_WINDOW = 12u,
    DEMONX_GET_GEOMETRY = 14u,
    DEMONX_QUERY_TREE = 15u,
    DEMONX_INTERN_ATOM = 16u,
    DEMONX_GET_ATOM_NAME = 17u,
    DEMONX_CHANGE_PROPERTY = 18u,
    DEMONX_DELETE_PROPERTY = 19u,
    DEMONX_GET_PROPERTY = 20u,
    DEMONX_LIST_PROPERTIES = 21u,
    DEMONX_SET_SELECTION_OWNER = 22u,
    DEMONX_GET_SELECTION_OWNER = 23u,
    DEMONX_SEND_EVENT = 25u,
    DEMONX_GRAB_POINTER = 26u,
    DEMONX_UNGRAB_POINTER = 27u,
    DEMONX_GRAB_BUTTON = 28u,
    DEMONX_UNGRAB_BUTTON = 29u,
    DEMONX_GRAB_KEYBOARD = 31u,
    DEMONX_UNGRAB_KEYBOARD = 32u,
    DEMONX_GRAB_KEY = 33u,
    DEMONX_UNGRAB_KEY = 34u,
    DEMONX_GRAB_SERVER = 36u,
    DEMONX_UNGRAB_SERVER = 37u,
    DEMONX_WARP_POINTER = 41u,
    DEMONX_SET_INPUT_FOCUS = 42u,
    DEMONX_SELECT_INPUT = 43u,
    DEMONX_CREATE_PIXMAP = 53u,
    DEMONX_FREE_PIXMAP = 54u,
    DEMONX_CREATE_GC = 55u,
    DEMONX_CHANGE_GC = 56u,
    DEMONX_FREE_GC = 60u,
    DEMONX_CLEAR_AREA = 61u,
    DEMONX_COPY_AREA = 62u,
    DEMONX_POLY_FILL_RECTANGLE = 70u,
    DEMONX_PUT_IMAGE = 72u,
    DEMONX_GET_IMAGE = 73u,
    DEMONX_POLY_TEXT8 = 74u,
    DEMONX_CREATE_FONT_CURSOR = 93u,
    DEMONX_FREE_CURSOR = 95u,
    DEMONX_DEFINE_CURSOR = 96u,
    DEMONX_KILL_CLIENT = 113u,
    /* DemonX-specific extension, not a real X11 core opcode -- core opcodes
       only go up to 127, so 200 can never collide with one this server
       later grows real support for. Draws text at an integer pixel scale
       (see demonx_draw_string_scaled/draw_text_scaled) for clients like
       xterm that want adjustable font size without a real X font server. */
    DEMONX_POLY_TEXT8_SCALED = 200u,
};

#define DEMONX_PASSIVE_GRAB_LIMIT 8u
struct demonx_passive_grab {
    uint32_t window;
    uint32_t client;
    uint32_t modifiers;
    uint32_t event_mask;
    uint8_t detail;
    uint8_t kind; /* 1 = key, 2 = button */
    uint8_t used;
    uint8_t reserved;
};

enum demonx_event_type {
    DEMONX_KEY_PRESS = 2u,
    DEMONX_KEY_RELEASE = 3u,
    DEMONX_BUTTON_PRESS = 4u,
    DEMONX_BUTTON_RELEASE = 5u,
    DEMONX_MOTION_NOTIFY = 6u,
    DEMONX_FOCUS_IN = 9u,
    DEMONX_FOCUS_OUT = 10u,
    DEMONX_UNMAP_NOTIFY = 18u,
    DEMONX_MAP_NOTIFY = 19u,
    DEMONX_MAP_REQUEST = 20u,
    DEMONX_REPARENT_NOTIFY = 21u,
    DEMONX_CONFIGURE_NOTIFY = 22u,
    DEMONX_CONFIGURE_REQUEST = 23u,
    DEMONX_PROPERTY_NOTIFY = 28u,
    DEMONX_CLIENT_MESSAGE = 33u,
};

enum demonx_event_mask {
    DEMONX_KEY_PRESS_MASK = 1u << 0u,
    DEMONX_KEY_RELEASE_MASK = 1u << 1u,
    DEMONX_BUTTON_PRESS_MASK = 1u << 2u,
    DEMONX_BUTTON_RELEASE_MASK = 1u << 3u,
    DEMONX_POINTER_MOTION_MASK = 1u << 6u,
    DEMONX_FOCUS_CHANGE_MASK = 1u << 21u,
    DEMONX_STRUCTURE_NOTIFY_MASK = 1u << 17u,
    DEMONX_SUBSTRUCTURE_NOTIFY_MASK = 1u << 19u,
    DEMONX_SUBSTRUCTURE_REDIRECT_MASK = 1u << 20u,
    DEMONX_PROPERTY_CHANGE_MASK = 1u << 22u,
};

enum demonx_configure_mask {
    DEMONX_CW_X = 1u << 0u,
    DEMONX_CW_Y = 1u << 1u,
    DEMONX_CW_WIDTH = 1u << 2u,
    DEMONX_CW_HEIGHT = 1u << 3u,
    DEMONX_CW_BORDER_WIDTH = 1u << 4u,
    DEMONX_CW_SIBLING = 1u << 5u,
    DEMONX_CW_STACK_MODE = 1u << 6u,
};

struct demonx_transport {
    uint32_t magic;
    uint16_t payload_length;
    uint16_t flags;
    uint32_t client_id;
    uint32_t sequence;
    uint8_t payload[DEMONX_PAYLOAD_BYTES];
} __attribute__((packed));

/* Reassembled protocol message. This is never sent directly through IPC. */
struct demonx_message {
    uint32_t magic;
    uint16_t payload_length;
    uint16_t flags;
    uint32_t client_id;
    uint32_t sequence;
    uint8_t payload[DEMONX_MESSAGE_BYTES];
} __attribute__((packed));

struct demonx_window {
    uint32_t id;
    uint32_t parent;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t border;
    uint16_t window_class;
    uint32_t event_mask;
    uint32_t event_client;
    /* event_client is last-writer-wins across every SelectInput caller (see
       demonx_server.c's non-root DEMONX_SELECT_INPUT handler), which is
       correct for the window-manager's own structural/button interest in a
       client it manages (Desktop/demonwm/demonwm.cc's watchWindow) but was
       silently stealing KEY delivery from the client that actually owns
       keyboard input: watchWindow's own mask never requests KeyPress/
       KeyReleaseMask, yet its later SelectInput call still overwrote
       event_client to the window manager, so no keystroke ever reached a
       managed window's real client once DemonWM started watching it.
       key_event_client tracks the same "who last selected this" idea but
       scoped to just the key-press/-release bits, so a WM's later,
       Key-less SelectInput call can never override it. */
    uint32_t key_event_client;
    uint32_t owner_client;
    uint32_t surface_handle;
    uint32_t compositor_surface;
    uint32_t background_pixel;
    uint32_t background_pixmap;
    uint32_t cursor;
    uint8_t mapped;
    uint8_t used;
    uint8_t saved_by;
};

#define DEMONX_CURSOR_LIMIT 4u
struct demonx_cursor {
    uint32_t id;
    uint16_t shape;
    uint8_t owner_client;
    uint8_t used;
};

#define DEMONX_ATOM_LIMIT 64u
#define DEMONX_PROPERTY_LIMIT 16u
#define DEMONX_PROPERTY_DATA_BYTES 24u
#define DEMONX_DYNAMIC_ATOM_BASE 0x00001000u

struct demonx_atom {
    uint32_t hash;
    uint16_t id;
    uint8_t length;
    uint8_t used;
    uint8_t name[32];
} __attribute__((packed));

struct demonx_property {
    uint32_t window;
    uint32_t atom;
    uint32_t type;
    uint32_t item_count;
    uint8_t format;
    uint8_t data_length;
    uint8_t used;
    uint8_t reserved;
    uint8_t data[DEMONX_PROPERTY_DATA_BYTES];
} __attribute__((packed));

#define DEMONX_GC_LIMIT 16u
#define DEMONX_GC_FOREGROUND (1u << 2u)
#define DEMONX_GC_FONT (1u << 14u)
#define DEMONX_DRAW_CHUNK_PIXELS 48u

struct demonx_gc {
    uint32_t id;
    uint32_t drawable;
    uint32_t foreground;
    uint32_t font;
    uint32_t owner_client;
    uint8_t used;
};

#define DEMONX_PIXMAP_LIMIT 8u

struct demonx_pixmap {
    uint32_t id;
    uint32_t owner_client;
    uint32_t surface_handle;
    uint16_t width;
    uint16_t height;
    uint8_t depth;
    uint8_t used;
};

#endif

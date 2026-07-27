#ifndef DEMON_WINDOW_H
#define DEMON_WINDOW_H

#include <stdint.h>

#define DEMON_WINDOW_PROTOCOL_VERSION 1u
#define DEMON_WINDOW_SERVICE "desktop.compositor"
#define DEMON_WINDOW_TITLE_MAX 31u

enum demon_window_opcode {
    DEMON_WINDOW_HELLO = 1,
    DEMON_WINDOW_CREATE,
    DEMON_WINDOW_CREATED,
    DEMON_WINDOW_DAMAGE,
    DEMON_WINDOW_PRESENT,
    DEMON_WINDOW_FOCUS,
    DEMON_WINDOW_POINTER,
    DEMON_WINDOW_KEY,
    DEMON_WINDOW_CLOSE,
    DEMON_WINDOW_MOVE,
};

enum demon_window_flags {
    DEMON_WINDOW_RESIZABLE = 1u << 0,
    DEMON_WINDOW_MAXIMIZED = 1u << 1,
    DEMON_WINDOW_FULLSCREEN = 1u << 2,
};

/* Every control packet fits one 64-byte kernel IPC message. Pixel payloads
   stay out-of-band in capability-authorized read-only surface mappings. */
struct demon_window_message {
    uint16_t version;
    uint16_t opcode;
    uint32_t serial;
    uint32_t window_id;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t surface_id;
    uint32_t payload_length;
    uint8_t payload[24];
};

_Static_assert(sizeof(struct demon_window_message) == 64u,
    "window protocol packet must fit one IPC message");

#endif

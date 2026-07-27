#ifndef DEMON_DEMONX_H
#define DEMON_DEMONX_H

#include <stdint.h>

#define DEMONX_TRANSPORT_MAGIC 0x58313144u /* "D11X", little endian */
#define DEMONX_TRANSPORT_BYTES 64u
#define DEMONX_PAYLOAD_BYTES 48u
#define DEMONX_ROOT_WINDOW 0x00000100u
#define DEMONX_RESOURCE_BASE 0x00200000u
#define DEMONX_RESOURCE_MASK 0x001fffffu

enum demonx_transport_flag {
    DEMONX_FLAG_SETUP = 1u,
    DEMONX_FLAG_REPLY = 2u,
    DEMONX_FLAG_ERROR = 4u,
};

enum demonx_core_opcode {
    DEMONX_CREATE_WINDOW = 1u,
    DEMONX_DESTROY_WINDOW = 4u,
    DEMONX_MAP_WINDOW = 8u,
    DEMONX_CONFIGURE_WINDOW = 12u,
    DEMONX_GET_GEOMETRY = 14u,
};

struct demonx_transport {
    uint32_t magic;
    uint16_t payload_length;
    uint16_t flags;
    uint32_t client_id;
    uint32_t sequence;
    uint8_t payload[DEMONX_PAYLOAD_BYTES];
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
    uint8_t mapped;
    uint8_t used;
};

#endif

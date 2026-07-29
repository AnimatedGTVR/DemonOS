#include <demon/demonx.h>
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_EXIT 2u
#define SYSCALL_CHANNEL_CREATE 14u
#define SYSCALL_CHANNEL_CONNECT 15u
#define SYSCALL_CHANNEL_SEND 16u
#define SYSCALL_CHANNEL_RECEIVE 17u
#define SYSCALL_HANDLE_CLOSE 8u
#define SYSCALL_FAILURE UINT64_MAX

/* USER_HEAP moved from 0x318000 to 0x31E000, then to 0x322000, and now to
   0x328000 (40 code pages) for the native session loading/login work. */
#define incoming (*(struct demonx_transport *)(uintptr_t)0x328000u)
#define outgoing (*(struct demonx_transport *)(uintptr_t)0x328040u)
#define windows ((struct demonx_window *)(uintptr_t)0x328080u)

static uint64_t syscall1(uint64_t number, uint64_t first) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi) : "memory", "cc");
    return rax;
}

static uint64_t syscall2(uint64_t number, uint64_t first, uint64_t second) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi), "S"(rsi) : "memory", "cc");
    return rax;
}

static uint64_t syscall3(uint64_t number, uint64_t first, uint64_t second,
                         uint64_t third) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi), "S"(rsi), "d"(rdx)
                     : "memory", "cc");
    return rax;
}

static uint64_t syscall4(uint64_t number, uint64_t first, uint64_t second,
                         uint64_t third, uint64_t fourth) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    register uint64_t r10 __asm__("r10") = fourth;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10)
                     : "memory", "cc");
    return rax;
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

static void clear_outgoing(void) {
    uint8_t *bytes = (uint8_t *)&outgoing;
    for (size_t index = 0u; index < sizeof(outgoing); ++index) bytes[index] = 0u;
    outgoing.magic = DEMONX_TRANSPORT_MAGIC;
    outgoing.client_id = incoming.client_id;
    outgoing.sequence = incoming.sequence;
}

static struct demonx_window *find_window(uint32_t id) {
    for (size_t index = 0u; index < 16u; ++index)
        if (windows[index].used != 0u && windows[index].id == id)
            return &windows[index];
    return NULL;
}

static struct demonx_window *allocate_window(uint32_t id) {
    if ((id & ~DEMONX_RESOURCE_MASK) != DEMONX_RESOURCE_BASE || find_window(id) != NULL)
        return NULL;
    for (size_t index = 0u; index < 16u; ++index) {
        if (windows[index].used != 0u) continue;
        windows[index] = (struct demonx_window){.id = id, .used = 1u};
        return &windows[index];
    }
    return NULL;
}

static void protocol_error(uint8_t code, uint8_t opcode, uint32_t bad_value) {
    clear_outgoing();
    outgoing.flags = DEMONX_FLAG_ERROR;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = 0u;
    outgoing.payload[1] = code;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], bad_value);
    outgoing.payload[10] = opcode;
}

static int parse_setup(void) {
    if (incoming.payload_length != 12u || incoming.payload[0] != (uint8_t)'l' ||
        read16(&incoming.payload[2]) != 11u ||
        read16(&incoming.payload[6]) != 0u || read16(&incoming.payload[8]) != 0u)
        return 0;
    clear_outgoing();
    outgoing.flags = DEMONX_FLAG_REPLY;
    outgoing.payload_length = 40u;
    outgoing.payload[0] = 1u;
    write16(&outgoing.payload[2], 11u);
    write16(&outgoing.payload[6], 8u);
    write32(&outgoing.payload[8], 1u);
    write32(&outgoing.payload[12], DEMONX_RESOURCE_BASE);
    write32(&outgoing.payload[16], DEMONX_RESOURCE_MASK);
    write16(&outgoing.payload[24], 0u);
    write16(&outgoing.payload[26], 12u);
    outgoing.payload[28] = 0u;
    outgoing.payload[29] = 0u;
    outgoing.payload[30] = 0u;
    outgoing.payload[31] = 0u;
    return 1;
}

static int parse_request(void) {
    const uint8_t opcode = incoming.payload[0];
    const uint16_t words = read16(&incoming.payload[2]);
    if (words == 0u || (uint32_t)words * 4u != incoming.payload_length) {
        protocol_error(16u, opcode, words);
        return 1;
    }
    const uint32_t id = read32(&incoming.payload[4]);
    if (opcode == DEMONX_CREATE_WINDOW) {
        if (incoming.payload_length != 32u || read32(&incoming.payload[8]) != DEMONX_ROOT_WINDOW) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        struct demonx_window *window = allocate_window(id);
        if (window == NULL) { protocol_error(14u, opcode, id); return 1; }
        window->parent = DEMONX_ROOT_WINDOW;
        window->x = (int16_t)read16(&incoming.payload[12]);
        window->y = (int16_t)read16(&incoming.payload[14]);
        window->width = read16(&incoming.payload[16]);
        window->height = read16(&incoming.payload[18]);
        window->border = read16(&incoming.payload[20]);
        window->window_class = read16(&incoming.payload[22]);
        if (window->width == 0u || window->height == 0u) {
            window->used = 0u;
            protocol_error(2u, opcode, id);
        }
        return 0;
    }
    struct demonx_window *window = find_window(id);
    if (window == NULL) { protocol_error(3u, opcode, id); return 1; }
    if (opcode == DEMONX_MAP_WINDOW && incoming.payload_length == 8u) {
        window->mapped = 1u;
        return 0;
    }
    if (opcode == DEMONX_DESTROY_WINDOW && incoming.payload_length == 8u) {
        window->used = 0u;
        return 0;
    }
    if (opcode == DEMONX_GET_GEOMETRY && incoming.payload_length == 8u) {
        clear_outgoing();
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 32u;
        outgoing.payload[0] = 1u;
        outgoing.payload[1] = 32u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[4], DEMONX_ROOT_WINDOW);
        write16(&outgoing.payload[8], (uint16_t)window->x);
        write16(&outgoing.payload[10], (uint16_t)window->y);
        write16(&outgoing.payload[12], window->width);
        write16(&outgoing.payload[14], window->height);
        write16(&outgoing.payload[16], window->border);
        return 1;
    }
    protocol_error(1u, opcode, id);
    return 1;
}

uint64_t demonx_main(const char *service_name, const char *reply_name) {
    const uint64_t server = syscall2(SYSCALL_CHANNEL_CREATE,
        (uint64_t)(uintptr_t)service_name, 16u);
    if (server == SYSCALL_FAILURE) return 120u;
    uint64_t reply = SYSCALL_FAILURE;
    for (;;) {
        const uint64_t received = syscall4(SYSCALL_CHANNEL_RECEIVE, server,
            (uint64_t)(uintptr_t)&incoming, sizeof(incoming), 0u);
        if (received != sizeof(incoming) || incoming.magic != DEMONX_TRANSPORT_MAGIC ||
            incoming.payload_length > DEMONX_PAYLOAD_BYTES)
            return 121u;
        if (reply == SYSCALL_FAILURE) {
            reply = syscall2(SYSCALL_CHANNEL_CONNECT,
                (uint64_t)(uintptr_t)reply_name, 14u);
            if (reply == SYSCALL_FAILURE) return 122u;
        }
        int sends_reply;
        if ((incoming.flags & DEMONX_FLAG_SETUP) != 0u) {
            if (!parse_setup()) return 123u;
            sends_reply = 1;
        } else {
            sends_reply = parse_request();
        }
        if (sends_reply != 0 && syscall3(SYSCALL_CHANNEL_SEND, reply,
                (uint64_t)(uintptr_t)&outgoing, sizeof(outgoing)) != sizeof(outgoing))
            return 124u;
    }
    (void)syscall1(SYSCALL_HANDLE_CLOSE, server);
    (void)syscall1(SYSCALL_EXIT, 0u);
    return 0u;
}

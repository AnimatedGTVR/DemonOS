#include <demon/demonx.h>
#include <demon/window.h>
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_EXIT 2u
#define SYSCALL_CHANNEL_CREATE 14u
#define SYSCALL_CHANNEL_CONNECT 15u
#define SYSCALL_CHANNEL_SEND 16u
#define SYSCALL_CHANNEL_RECEIVE 17u
#define SYSCALL_HANDLE_CLOSE 8u
#define SYSCALL_SERVICE_OPEN 5u
#define SYSCALL_SURFACE_CREATE 22u
#define SYSCALL_SURFACE_WRITE 23u
#define SYSCALL_SURFACE_SHARE 24u
#define SYSCALL_SURFACE_MAP 27u
#define SYSCALL_SURFACE_DAMAGE 28u
#define SYSCALL_SURFACE_UNMAP 30u
#define SYSCALL_FAILURE UINT64_MAX

/* USER_HEAP moved from 0x318000 to 0x31E000, to 0x322000, to 0x328000
   (40 code pages) for the native session loading/login work, and now to
   0x330000 (48 code pages) for EDDE's real-ported taskbar context menu. */
#define incoming_wire (*(struct demonx_transport *)(uintptr_t)0x330000u)
#define outgoing_wire (*(struct demonx_transport *)(uintptr_t)0x330040u)
#define windows ((struct demonx_window *)(uintptr_t)0x330080u)
#define reply_handles ((uint64_t *)(uintptr_t)0x330400u)
#define root_event_mask (*(uint32_t *)(uintptr_t)0x330440u)
#define root_redirect_client (*(uint32_t *)(uintptr_t)0x330444u)
#define root_event_client (*(uint32_t *)(uintptr_t)0x330448u)
#define atoms ((struct demonx_atom *)(uintptr_t)0x330450u)
#define properties ((struct demonx_property *)(uintptr_t)0x330F00u)
#define surface_factory_handle (*(uint64_t *)(uintptr_t)0x331200u)
#define compositor_handle (*(uint64_t *)(uintptr_t)0x331208u)
#define compositor_serial (*(uint32_t *)(uintptr_t)0x331210u)
#define surface_failure (*(uint32_t *)(uintptr_t)0x331214u)
#define focused_window (*(uint32_t *)(uintptr_t)0x331218u)
#define pointer_grab_client (*(uint32_t *)(uintptr_t)0x33121cu)
#define pointer_grab_window (*(uint32_t *)(uintptr_t)0x331220u)
#define pointer_grab_mask (*(uint32_t *)(uintptr_t)0x331224u)
#define keyboard_grab_client (*(uint32_t *)(uintptr_t)0x33122cu)
#define keyboard_grab_window (*(uint32_t *)(uintptr_t)0x331230u)
#define server_grab_client (*(uint32_t *)(uintptr_t)0x331234u)
#define selection_atom (*(uint32_t *)(uintptr_t)0x331238u)
#define selection_owner (*(uint32_t *)(uintptr_t)0x33123cu)
#define passive_grabs ((struct demonx_passive_grab *)(uintptr_t)0x331240u)
#define cursors ((struct demonx_cursor *)(uintptr_t)0x3312e0u)
#define gcs ((struct demonx_gc *)(uintptr_t)0x331300u)
#define draw_pixels ((uint32_t *)(uintptr_t)0x331600u)
#define incoming (*(struct demonx_message *)(uintptr_t)0x334700u)
#define outgoing (*(struct demonx_message *)(uintptr_t)0x334820u)
#define assembly_active (*(uint32_t *)(uintptr_t)0x334940u)
#define pixmaps ((struct demonx_pixmap *)(uintptr_t)0x334950u)
#define DEMONX_FALLBACK_SURFACE UINT32_MAX
#define DEMONX_FALLBACK_WIDTH 56u
#define DEMONX_FALLBACK_HEIGHT 56u
/* Mirrors the kernel's own SURFACE_MAX_WIDTH/SURFACE_MAX_HEIGHT
   (include/kernel/surface.h) -- the real cap on a window's live pixel
   surface enforced by surface_create() in ring 0. Kept as a duplicated
   constant across the syscall boundary rather than a shared header,
   matching this codebase's existing convention for other kernel/userspace
   ABI constants. */
#define DEMONX_WINDOW_MAX_WIDTH 640u
#define DEMONX_WINDOW_MAX_HEIGHT 480u

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

static uint64_t syscall5(uint64_t number, uint64_t first, uint64_t second,
                         uint64_t third, uint64_t fourth, uint64_t fifth) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    register uint64_t r10 __asm__("r10") = fourth;
    register uint64_t r8 __asm__("r8") = fifth;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
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

static uint32_t resource_base(uint32_t client_id) {
    return client_id * (DEMONX_RESOURCE_MASK + 1u);
}

static void clear_outgoing(uint32_t client_id);
static int send_outgoing(const char *reply_name);

static int focus_notify(struct demonx_window *window, uint8_t type) {
    if (window == NULL ||
        (window->event_mask & DEMONX_FOCUS_CHANGE_MASK) == 0u ||
        window->event_client == 0u)
        return 0;
    clear_outgoing(window->event_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = type;
    outgoing.payload[1] = 3u; /* NotifyNonlinear */
    write32(&outgoing.payload[4], window->id);
    outgoing.payload[8] = 0u; /* NotifyNormal */
    return 1;
}

static void clear_outgoing(uint32_t client_id) {
    uint8_t *bytes = (uint8_t *)&outgoing;
    for (size_t index = 0u; index < sizeof(outgoing); ++index) bytes[index] = 0u;
    outgoing.magic = DEMONX_TRANSPORT_MAGIC;
    outgoing.client_id = client_id;
    outgoing.sequence = incoming.sequence;
}

static struct demonx_window *find_window(uint32_t id) {
    for (size_t index = 0u; index < 16u; ++index)
        if (windows[index].used != 0u && windows[index].id == id)
            return &windows[index];
    return NULL;
}

static struct demonx_pixmap *find_pixmap(uint32_t id) {
    for (size_t index = 0u; index < DEMONX_PIXMAP_LIMIT; ++index)
        if (pixmaps[index].used != 0u && pixmaps[index].id == id)
            return &pixmaps[index];
    return NULL;
}

static struct demonx_pixmap *allocate_pixmap(uint32_t id,
                                             uint32_t client_id) {
    if (find_pixmap(id) != NULL) return NULL;
    for (size_t index = 0u; index < DEMONX_PIXMAP_LIMIT; ++index) {
        if (pixmaps[index].used != 0u) continue;
        pixmaps[index] = (struct demonx_pixmap){
            .id = id, .owner_client = client_id, .used = 1u
        };
        return &pixmaps[index];
    }
    return NULL;
}

static int valid_drawable(uint32_t id) {
    return find_window(id) != NULL || find_pixmap(id) != NULL;
}

static struct demonx_window *allocate_window(uint32_t id, uint32_t client_id) {
    if ((id & ~DEMONX_RESOURCE_MASK) != resource_base(client_id) ||
        find_window(id) != NULL)
        return NULL;
    for (size_t index = 0u; index < 16u; ++index) {
        if (windows[index].used != 0u) continue;
        windows[index] = (struct demonx_window){.id = id, .used = 1u};
        return &windows[index];
    }
    return NULL;
}

static struct demonx_gc *find_gc(uint32_t id) {
    for (size_t index = 0u; index < DEMONX_GC_LIMIT; ++index)
        if (gcs[index].used != 0u && gcs[index].id == id)
            return &gcs[index];
    return NULL;
}

static struct demonx_cursor *find_cursor(uint32_t id) {
    for (size_t index = 0u; index < DEMONX_CURSOR_LIMIT; ++index)
        if (cursors[index].used != 0u && cursors[index].id == id)
            return &cursors[index];
    return NULL;
}

static struct demonx_cursor *allocate_cursor(uint32_t id,
                                              uint32_t client_id) {
    if ((id & ~DEMONX_RESOURCE_MASK) != resource_base(client_id) ||
        find_cursor(id) != NULL)
        return NULL;
    for (size_t index = 0u; index < DEMONX_CURSOR_LIMIT; ++index) {
        if (cursors[index].used != 0u) continue;
        cursors[index] = (struct demonx_cursor){
            .id = id, .owner_client = (uint8_t)client_id, .used = 1u
        };
        return &cursors[index];
    }
    return NULL;
}

static struct demonx_gc *allocate_gc(uint32_t id, uint32_t client_id) {
    if ((id & ~DEMONX_RESOURCE_MASK) != resource_base(client_id) ||
        find_gc(id) != NULL || find_window(id) != NULL)
        return NULL;
    for (size_t index = 0u; index < DEMONX_GC_LIMIT; ++index) {
        if (gcs[index].used != 0u) continue;
        gcs[index] = (struct demonx_gc){
            .id = id, .foreground = 0xff000000u,
            .owner_client = client_id, .used = 1u
        };
        return &gcs[index];
    }
    return NULL;
}

static struct demonx_passive_grab *matching_passive_grab(
        uint8_t kind, uint8_t detail, uint32_t modifiers) {
    for (size_t index = 0u; index < DEMONX_PASSIVE_GRAB_LIMIT; ++index) {
        struct demonx_passive_grab *grab = &passive_grabs[index];
        if (grab->used != 0u && grab->kind == kind &&
            (grab->detail == 0u || grab->detail == detail) &&
            (grab->modifiers == (1u << 15u) ||
             grab->modifiers == modifiers))
            return grab;
    }
    return NULL;
}

static int ensure_desktop_handles(void) {
    static const char compositor_name[] = DEMON_WINDOW_SERVICE;
    if (surface_factory_handle == 0u)
        surface_factory_handle = syscall1(SYSCALL_SERVICE_OPEN, 9u);
    if (compositor_handle == 0u)
        compositor_handle = syscall2(SYSCALL_CHANNEL_CONNECT,
            (uint64_t)(uintptr_t)compositor_name,
            sizeof(compositor_name) - 1u);
    return surface_factory_handle != SYSCALL_FAILURE &&
           compositor_handle != SYSCALL_FAILURE;
}

static int create_window_surface(struct demonx_window *window) {
    if (window->surface_handle != 0u) return 1;
    if (surface_factory_handle == 0u)
        surface_factory_handle = syscall1(SYSCALL_SERVICE_OPEN, 9u);
    if (surface_factory_handle == SYSCALL_FAILURE) {
        window->surface_handle = DEMONX_FALLBACK_SURFACE;
        for (uint32_t index = 0u;
             index < DEMONX_FALLBACK_WIDTH * DEMONX_FALLBACK_HEIGHT; ++index)
            draw_pixels[index] = 0xff101722u;
        return 1;
    }
    const uint32_t surface_width =
        window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width;
    const uint32_t surface_height =
        window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height;
    const uint64_t surface = syscall3(SYSCALL_SURFACE_CREATE,
        surface_factory_handle, surface_width, surface_height);
    if (surface == SYSCALL_FAILURE || surface > UINT32_MAX) {
        window->surface_handle = DEMONX_FALLBACK_SURFACE;
        for (uint32_t index = 0u;
             index < DEMONX_FALLBACK_WIDTH * DEMONX_FALLBACK_HEIGHT; ++index)
            draw_pixels[index] = 0xff101722u;
        return 1;
    }
    window->surface_handle = (uint32_t)surface;
    for (uint32_t index = 0u; index < DEMONX_DRAW_CHUNK_PIXELS; ++index)
        draw_pixels[index] = 0xff101722u;
    const uint32_t pixels = surface_width * surface_height;
    for (uint32_t offset = 0u; offset < pixels;
         offset += DEMONX_DRAW_CHUNK_PIXELS) {
        uint32_t count = pixels - offset;
        if (count > DEMONX_DRAW_CHUNK_PIXELS)
            count = DEMONX_DRAW_CHUNK_PIXELS;
        if (syscall4(SYSCALL_SURFACE_WRITE, surface,
                     (uint64_t)(uintptr_t)draw_pixels, count, offset) != count)
            {
                surface_failure = 3u;
                return 0;
            }
    }
    return 1;
}

static int publish_window(struct demonx_window *window) {
    if (!create_window_surface(window) || !ensure_desktop_handles()) return 0;
    if (window->surface_handle == DEMONX_FALLBACK_SURFACE) return 0;
    const uint32_t surface_width =
        window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width;
    const uint32_t surface_height =
        window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height;
    if (window->compositor_surface == 0u) {
        const uint64_t shared = syscall2(SYSCALL_SURFACE_SHARE,
            window->surface_handle, compositor_handle);
        if (shared == SYSCALL_FAILURE || shared > UINT32_MAX) return 0;
        window->compositor_surface = (uint32_t)shared;
    }
    int32_t desktop_x = window->x;
    int32_t desktop_y = window->y;
    uint32_t parent = window->parent;
    /*
     * X child coordinates are relative to their parent, while the native
     * compositor protocol uses desktop coordinates.  Walk the bounded
     * DemonX hierarchy so reparented PekWM clients stay inside their frames.
     */
    for (uint32_t depth = 0u;
         parent != DEMONX_ROOT_WINDOW && depth < 16u; ++depth) {
        struct demonx_window *ancestor = find_window(parent);
        if (ancestor == NULL) break;
        desktop_x += ancestor->x;
        desktop_y += ancestor->y;
        parent = ancestor->parent;
    }
    struct demon_window_message message = {
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_CREATE,
        .serial = ++compositor_serial,
        .window_id = window->id,
        .x = desktop_x, .y = desktop_y,
        .width = surface_width, .height = surface_height,
        .surface_id = window->compositor_surface,
    };
    return syscall3(SYSCALL_CHANNEL_SEND, compositor_handle,
        (uint64_t)(uintptr_t)&message, sizeof(message)) == sizeof(message);
}

static void move_published_window(struct demonx_window *window) {
    if (window->mapped == 0u || window->compositor_surface == 0u ||
        !ensure_desktop_handles())
        return;
    int32_t desktop_x = window->x;
    int32_t desktop_y = window->y;
    uint32_t parent = window->parent;
    for (uint32_t depth = 0u;
         parent != DEMONX_ROOT_WINDOW && depth < 16u; ++depth) {
        struct demonx_window *ancestor = find_window(parent);
        if (ancestor == NULL) break;
        desktop_x += ancestor->x;
        desktop_y += ancestor->y;
        parent = ancestor->parent;
    }
    struct demon_window_message message = {
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_MOVE,
        .serial = ++compositor_serial,
        .window_id = window->id,
        .x = desktop_x,
        .y = desktop_y,
        .width = window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width,
        .height = window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height,
    };
    (void)syscall3(SYSCALL_CHANNEL_SEND, compositor_handle,
        (uint64_t)(uintptr_t)&message, sizeof(message));
}

static void move_published_descendants(uint32_t parent) {
    for (size_t index = 0u; index < 16u; ++index) {
        if (windows[index].used == 0u || windows[index].parent != parent)
            continue;
        move_published_window(&windows[index]);
        move_published_descendants(windows[index].id);
    }
}

static void close_published_window(struct demonx_window *window) {
    if (window->compositor_surface != 0u && ensure_desktop_handles()) {
        struct demon_window_message message = {
            .version = DEMON_WINDOW_PROTOCOL_VERSION,
            .opcode = DEMON_WINDOW_CLOSE,
            .serial = ++compositor_serial,
            .window_id = window->id,
        };
        (void)syscall3(SYSCALL_CHANNEL_SEND, compositor_handle,
            (uint64_t)(uintptr_t)&message, sizeof(message));
    }
    if (window->surface_handle != 0u &&
        window->surface_handle != DEMONX_FALLBACK_SURFACE)
        (void)syscall1(SYSCALL_HANDLE_CLOSE, window->surface_handle);
    window->surface_handle = 0u;
    window->compositor_surface = 0u;
}

static int fill_rectangle(struct demonx_window *window,
                          const struct demonx_gc *gc,
                          int16_t x, int16_t y,
                          uint16_t width, uint16_t height) {
    if (!create_window_surface(window)) return -1;
    int32_t left = x < 0 ? 0 : x;
    int32_t top = y < 0 ? 0 : y;
    int32_t right = (int32_t)x + width;
    int32_t bottom = (int32_t)y + height;
    const uint32_t surface_width =
        window->surface_handle == DEMONX_FALLBACK_SURFACE ?
        (window->width > DEMONX_FALLBACK_WIDTH ?
         DEMONX_FALLBACK_WIDTH : window->width) :
        (window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width);
    const uint32_t surface_height =
        window->surface_handle == DEMONX_FALLBACK_SURFACE ?
        (window->height > DEMONX_FALLBACK_HEIGHT ?
         DEMONX_FALLBACK_HEIGHT : window->height) :
        (window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height);
    if (right > (int32_t)surface_width) right = (int32_t)surface_width;
    if (bottom > (int32_t)surface_height) bottom = (int32_t)surface_height;
    if (left >= right || top >= bottom) return 1;
    for (uint32_t index = 0u; index < DEMONX_DRAW_CHUNK_PIXELS; ++index)
        draw_pixels[index] = gc->foreground | 0xff000000u;
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t column = (uint32_t)left;
        while (column < (uint32_t)right) {
            uint32_t count = (uint32_t)right - column;
            if (count > DEMONX_DRAW_CHUNK_PIXELS)
                count = DEMONX_DRAW_CHUNK_PIXELS;
            const uint32_t offset = (uint32_t)row * surface_width + column;
            if (window->surface_handle == DEMONX_FALLBACK_SURFACE) {
                for (uint32_t index = 0u; index < count; ++index)
                    draw_pixels[offset + index] = gc->foreground | 0xff000000u;
            } else if (syscall4(SYSCALL_SURFACE_WRITE, window->surface_handle,
                                (uint64_t)(uintptr_t)draw_pixels,
                                count, offset) != count) {
                return -2;
            }
            column += count;
        }
    }
    if (window->surface_handle == DEMONX_FALLBACK_SURFACE) return 1;
    if (syscall5(SYSCALL_SURFACE_DAMAGE, window->surface_handle,
                 (uint32_t)left, (uint32_t)top,
                 (uint32_t)(right - left),
                 (uint32_t)(bottom - top)) != 0u)
        return -3;
    return 1;
}

static int fill_pixmap(struct demonx_pixmap *pixmap,
                       const struct demonx_gc *gc,
                       int16_t x, int16_t y,
                       uint16_t width, uint16_t height) {
    int32_t left = x < 0 ? 0 : x;
    int32_t top = y < 0 ? 0 : y;
    int32_t right = (int32_t)x + width;
    int32_t bottom = (int32_t)y + height;
    if (right > pixmap->width) right = pixmap->width;
    if (bottom > pixmap->height) bottom = pixmap->height;
    if (left >= right || top >= bottom) return 1;
    for (uint32_t index = 0u; index < DEMONX_DRAW_CHUNK_PIXELS; ++index)
        draw_pixels[index] = gc->foreground | 0xff000000u;
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t column = (uint32_t)left;
        while (column < (uint32_t)right) {
            uint32_t count = (uint32_t)right - column;
            if (count > DEMONX_DRAW_CHUNK_PIXELS)
                count = DEMONX_DRAW_CHUNK_PIXELS;
            if (syscall4(SYSCALL_SURFACE_WRITE, pixmap->surface_handle,
                         (uint64_t)(uintptr_t)draw_pixels, count,
                         (uint32_t)row * pixmap->width + column) != count)
                return -2;
            column += count;
        }
    }
    return 1;
}

static int put_image(uint32_t drawable_id, int16_t x, int16_t y,
                     uint16_t width, uint16_t height,
                     const uint8_t *data) {
    struct demonx_window *window = find_window(drawable_id);
    struct demonx_pixmap *pixmap = find_pixmap(drawable_id);
    uint32_t surface = 0u, stride = 0u, drawable_height = 0u;
    if (pixmap != NULL) {
        surface = pixmap->surface_handle;
        stride = pixmap->width;
        drawable_height = pixmap->height;
    } else if (window != NULL) {
        if (!create_window_surface(window) ||
            window->surface_handle == DEMONX_FALLBACK_SURFACE)
            return -1;
        surface = window->surface_handle;
        stride = window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width;
        drawable_height = window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height;
    } else {
        return -4;
    }
    for (uint32_t row = 0u; row < height; ++row) {
        const int32_t destination_y = (int32_t)y + row;
        if (destination_y < 0 || destination_y >= (int32_t)drawable_height)
            continue;
        uint32_t source_column = 0u;
        int32_t destination_x = x;
        if (destination_x < 0) {
            source_column = (uint32_t)-destination_x;
            destination_x = 0;
        }
        if (source_column >= width || destination_x >= (int32_t)stride)
            continue;
        uint32_t count = width - source_column;
        if ((uint32_t)destination_x + count > stride)
            count = stride - (uint32_t)destination_x;
        for (uint32_t index = 0u; index < count; ++index) {
            const uint32_t source = (row * width + source_column + index) * 4u;
            draw_pixels[index] = read32(&data[source]) | 0xff000000u;
        }
        if (syscall4(SYSCALL_SURFACE_WRITE, surface,
                     (uint64_t)(uintptr_t)draw_pixels, count,
                     (uint32_t)destination_y * stride +
                         (uint32_t)destination_x) != count)
            return -2;
    }
    if (window != NULL)
        (void)syscall5(SYSCALL_SURFACE_DAMAGE, surface,
                       x < 0 ? 0u : (uint32_t)x,
                       y < 0 ? 0u : (uint32_t)y, width, height);
    return 1;
}

static int drawable_surface(uint32_t drawable_id, uint32_t *surface,
                            uint32_t *width, uint32_t *height,
                            struct demonx_window **window_out) {
    struct demonx_pixmap *pixmap = find_pixmap(drawable_id);
    struct demonx_window *window = find_window(drawable_id);
    if (pixmap != NULL) {
        *surface = pixmap->surface_handle;
        *width = pixmap->width;
        *height = pixmap->height;
        *window_out = NULL;
        return 1;
    }
    if (window == NULL || !create_window_surface(window) ||
        window->surface_handle == DEMONX_FALLBACK_SURFACE)
        return 0;
    *surface = window->surface_handle;
    *width = window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width;
    *height = window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height;
    *window_out = window;
    return 1;
}

static int copy_area(uint32_t source_id, uint32_t destination_id,
                     int16_t source_x, int16_t source_y,
                     int16_t destination_x, int16_t destination_y,
                     uint16_t width, uint16_t height) {
    uint32_t source_surface, source_width, source_height;
    uint32_t destination_surface, destination_width, destination_height;
    struct demonx_window *source_window, *destination_window;
    if (!drawable_surface(source_id, &source_surface, &source_width,
                          &source_height, &source_window) ||
        !drawable_surface(destination_id, &destination_surface,
                          &destination_width, &destination_height,
                          &destination_window))
        return -1;

    int32_t sx = source_x, sy = source_y;
    int32_t dx = destination_x, dy = destination_y;
    int32_t copy_width = width, copy_height = height;
    if (sx < 0) { dx -= sx; copy_width += sx; sx = 0; }
    if (sy < 0) { dy -= sy; copy_height += sy; sy = 0; }
    if (dx < 0) { sx -= dx; copy_width += dx; dx = 0; }
    if (dy < 0) { sy -= dy; copy_height += dy; dy = 0; }
    if (sx + copy_width > (int32_t)source_width)
        copy_width = (int32_t)source_width - sx;
    if (sy + copy_height > (int32_t)source_height)
        copy_height = (int32_t)source_height - sy;
    if (dx + copy_width > (int32_t)destination_width)
        copy_width = (int32_t)destination_width - dx;
    if (dy + copy_height > (int32_t)destination_height)
        copy_height = (int32_t)destination_height - dy;
    if (copy_width <= 0 || copy_height <= 0) return 1;

    const uint64_t mapped = syscall1(SYSCALL_SURFACE_MAP, source_surface);
    if (mapped == SYSCALL_FAILURE) return -2;
    int32_t row = 0, row_end = copy_height, row_step = 1;
    if (source_surface == destination_surface && dy > sy) {
        row = copy_height - 1;
        row_end = -1;
        row_step = -1;
    }
    for (; row != row_end; row += row_step) {
        uint32_t column = 0u;
        while (column < (uint32_t)copy_width) {
            uint32_t count = (uint32_t)copy_width - column;
            if (count > DEMONX_DRAW_CHUNK_PIXELS)
                count = DEMONX_DRAW_CHUNK_PIXELS;
            uint32_t chunk_column = column;
            if (source_surface == destination_surface && dy == sy && dx > sx)
                chunk_column = (uint32_t)copy_width - column - count;
            const uint32_t *source = (const uint32_t *)(uintptr_t)mapped +
                (uint32_t)(sy + row) * source_width +
                (uint32_t)sx + chunk_column;
            for (uint32_t index = 0u; index < count; ++index)
                draw_pixels[index] = source[index];
            if (syscall4(SYSCALL_SURFACE_WRITE, destination_surface,
                         (uint64_t)(uintptr_t)draw_pixels, count,
                         (uint32_t)(dy + row) * destination_width +
                             (uint32_t)dx + chunk_column) != count) {
                (void)syscall1(SYSCALL_SURFACE_UNMAP, source_surface);
                return -3;
            }
            column += count;
        }
    }
    (void)syscall1(SYSCALL_SURFACE_UNMAP, source_surface);
    if (destination_window != NULL)
        (void)syscall5(SYSCALL_SURFACE_DAMAGE, destination_surface,
                       (uint32_t)dx, (uint32_t)dy,
                       (uint32_t)copy_width, (uint32_t)copy_height);
    return 1;
}

static int clear_background(struct demonx_window *window,
                            int16_t x, int16_t y,
                            uint16_t width, uint16_t height) {
    const uint16_t clear_width = width == 0u
        ? (x < 0 || x >= (int16_t)window->width ? 0u :
           (uint16_t)(window->width - (uint16_t)x)) : width;
    const uint16_t clear_height = height == 0u
        ? (y < 0 || y >= (int16_t)window->height ? 0u :
           (uint16_t)(window->height - (uint16_t)y)) : height;
    struct demonx_pixmap *background =
        find_pixmap(window->background_pixmap);
    if (background == NULL || background->width == 0u ||
        background->height == 0u) {
        const struct demonx_gc background_gc = {
            .foreground = window->background_pixel
        };
        return fill_rectangle(window, &background_gc, x, y,
                              clear_width, clear_height);
    }
    if (!create_window_surface(window) ||
        window->surface_handle == DEMONX_FALLBACK_SURFACE)
        return -1;
    const uint64_t mapped =
        syscall1(SYSCALL_SURFACE_MAP, background->surface_handle);
    if (mapped == SYSCALL_FAILURE) return -2;
    const uint32_t stride = window->width > DEMONX_WINDOW_MAX_WIDTH ? DEMONX_WINDOW_MAX_WIDTH : window->width;
    int32_t left = x < 0 ? 0 : x;
    int32_t top = y < 0 ? 0 : y;
    int32_t right = (int32_t)x + clear_width;
    int32_t bottom = (int32_t)y + clear_height;
    if (right > (int32_t)stride) right = stride;
    if (bottom > (int32_t)(window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height))
        bottom = window->height > DEMONX_WINDOW_MAX_HEIGHT ? DEMONX_WINDOW_MAX_HEIGHT : window->height;
    if (left >= right || top >= bottom) {
        (void)syscall1(SYSCALL_SURFACE_UNMAP, background->surface_handle);
        return 1;
    }
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t column = (uint32_t)left;
        while (column < (uint32_t)right) {
            uint32_t count = (uint32_t)right - column;
            if (count > DEMONX_DRAW_CHUNK_PIXELS)
                count = DEMONX_DRAW_CHUNK_PIXELS;
            for (uint32_t index = 0u; index < count; ++index) {
                const uint32_t source_x = (column + index) % background->width;
                const uint32_t source_y = (uint32_t)row % background->height;
                draw_pixels[index] =
                    ((const uint32_t *)(uintptr_t)mapped)
                    [source_y * background->width + source_x];
            }
            if (syscall4(SYSCALL_SURFACE_WRITE, window->surface_handle,
                         (uint64_t)(uintptr_t)draw_pixels, count,
                         (uint32_t)row * stride + column) != count) {
                (void)syscall1(SYSCALL_SURFACE_UNMAP,
                               background->surface_handle);
                return -3;
            }
            column += count;
        }
    }
    (void)syscall1(SYSCALL_SURFACE_UNMAP, background->surface_handle);
    (void)syscall5(SYSCALL_SURFACE_DAMAGE, window->surface_handle,
                   (uint32_t)left, (uint32_t)top,
                   (uint32_t)(right - left), (uint32_t)(bottom - top));
    return 1;
}

static const uint8_t core_font[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

static const uint8_t *glyph_columns(uint8_t character) {
    if (character >= '0' && character <= '9')
        return core_font[character - '0'];
    if (character >= 'a' && character <= 'z')
        character = (uint8_t)(character - ('a' - 'A'));
    if (character >= 'A' && character <= 'Z')
        return core_font[10u + character - 'A'];
    return NULL;
}

static int draw_text(uint32_t drawable_id, const struct demonx_gc *gc,
                     int16_t x, int16_t baseline,
                     const uint8_t *text, uint8_t length) {
    uint32_t surface, width, height;
    struct demonx_window *window;
    if (!drawable_surface(drawable_id, &surface, &width, &height, &window))
        return -1;
    const uint64_t mapped = syscall1(SYSCALL_SURFACE_MAP, surface);
    if (mapped == SYSCALL_FAILURE) return -2;
    const int32_t top = (int32_t)baseline - 7;
    for (uint8_t character = 0u; character < length; ++character) {
        const uint8_t *glyph = glyph_columns(text[character]);
        if (glyph == NULL) continue;
        for (uint32_t row = 0u; row < 7u; ++row) {
            const int32_t destination_y = top + (int32_t)row;
            const int32_t destination_x = (int32_t)x + character * 6;
            if (destination_y < 0 || destination_y >= (int32_t)height ||
                destination_x < 0 || destination_x + 5 > (int32_t)width)
                continue;
            for (uint32_t column = 0u; column < 5u; ++column)
                draw_pixels[column] = (glyph[column] & (1u << row)) != 0u
                    ? (gc->foreground | 0xff000000u) : 0x00000000u;
            for (uint32_t column = 0u; column < 5u; ++column)
                if ((glyph[column] & (1u << row)) == 0u)
                    draw_pixels[column] =
                        ((const uint32_t *)(uintptr_t)mapped)
                        [(uint32_t)destination_y * width +
                         (uint32_t)destination_x + column];
            if (syscall4(SYSCALL_SURFACE_WRITE, surface,
                         (uint64_t)(uintptr_t)draw_pixels, 5u,
                         (uint32_t)destination_y * width +
                             (uint32_t)destination_x) != 5u)
                {
                    (void)syscall1(SYSCALL_SURFACE_UNMAP, surface);
                    return -3;
                }
        }
    }
    (void)syscall1(SYSCALL_SURFACE_UNMAP, surface);
    if (window != NULL)
        (void)syscall5(SYSCALL_SURFACE_DAMAGE, surface,
                       x < 0 ? 0u : (uint32_t)x,
                       top < 0 ? 0u : (uint32_t)top,
                       (uint32_t)length * 6u, 7u);
    return 1;
}

static void map_notify(const struct demonx_window *window) {
    clear_outgoing(window->event_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_MAP_NOTIFY;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], window->id);
    write32(&outgoing.payload[8], window->id);
}

static void map_request(const struct demonx_window *window) {
    clear_outgoing(root_redirect_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_MAP_REQUEST;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], DEMONX_ROOT_WINDOW);
    write32(&outgoing.payload[8], window->id);
}

static void configure_request(const struct demonx_window *window,
                              uint16_t value_mask) {
    clear_outgoing(root_redirect_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_CONFIGURE_REQUEST;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], window->parent);
    write32(&outgoing.payload[8], window->id);
    write32(&outgoing.payload[12], 0u);
    write16(&outgoing.payload[16], (uint16_t)window->x);
    write16(&outgoing.payload[18], (uint16_t)window->y);
    write16(&outgoing.payload[20], window->width);
    write16(&outgoing.payload[22], window->height);
    write16(&outgoing.payload[24], window->border);
    write16(&outgoing.payload[26], value_mask);
}

static void configure_notify(const struct demonx_window *window) {
    clear_outgoing(window->event_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_CONFIGURE_NOTIFY;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], window->id);
    write32(&outgoing.payload[8], window->id);
    write32(&outgoing.payload[12], 0u);
    write16(&outgoing.payload[16], (uint16_t)window->x);
    write16(&outgoing.payload[18], (uint16_t)window->y);
    write16(&outgoing.payload[20], window->width);
    write16(&outgoing.payload[22], window->height);
    write16(&outgoing.payload[24], window->border);
}

static void unmap_notify(const struct demonx_window *window,
                         uint32_t destination, uint32_t event_window) {
    clear_outgoing(destination);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_UNMAP_NOTIFY;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], event_window);
    write32(&outgoing.payload[8], window->id);
}

static void reparent_notify(const struct demonx_window *window) {
    clear_outgoing(window->event_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_REPARENT_NOTIFY;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], window->id);
    write32(&outgoing.payload[8], window->id);
    write32(&outgoing.payload[12], window->parent);
    write16(&outgoing.payload[16], (uint16_t)window->x);
    write16(&outgoing.payload[18], (uint16_t)window->y);
}

static int property_notify(uint32_t window_id, uint32_t atom,
                           uint8_t state) {
    uint32_t destination = 0u;
    if (window_id == DEMONX_ROOT_WINDOW) {
        if ((root_event_mask & DEMONX_PROPERTY_CHANGE_MASK) == 0u)
            return 0;
        destination = root_event_client;
    } else {
        const struct demonx_window *window = find_window(window_id);
        if (window == NULL ||
            (window->event_mask & DEMONX_PROPERTY_CHANGE_MASK) == 0u)
            return 0;
        destination = window->event_client;
    }
    clear_outgoing(destination);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = DEMONX_PROPERTY_NOTIFY;
    write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
    write32(&outgoing.payload[4], window_id);
    write32(&outgoing.payload[8], atom);
    write32(&outgoing.payload[12], incoming.sequence);
    outgoing.payload[16] = state;
    return 1;
}

static uint32_t value_count(uint16_t mask) {
    uint32_t count = 0u;
    for (uint16_t bit = 1u; bit <= DEMONX_CW_STACK_MODE; bit <<= 1u)
        if ((mask & bit) != 0u) ++count;
    return count;
}

static uint32_t atom_hash(const uint8_t *name, uint8_t length) {
    uint32_t hash = 2166136261u;
    for (uint8_t index = 0u; index < length; ++index) {
        hash ^= name[index];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t intern_atom(const uint8_t *name, uint8_t length,
                            int only_if_exists) {
    const uint32_t hash = atom_hash(name, length);
    for (size_t index = 0u; index < DEMONX_ATOM_LIMIT; ++index) {
        if (atoms[index].used != 0u && atoms[index].hash == hash &&
            atoms[index].length == length) {
            uint8_t same = 1u;
            for (uint8_t byte = 0u; byte < length; ++byte)
                if (atoms[index].name[byte] != name[byte]) same = 0u;
            if (same != 0u) return atoms[index].id;
        }
    }
    if (only_if_exists) return 0u;
    for (size_t index = 0u; index < DEMONX_ATOM_LIMIT; ++index) {
        if (atoms[index].used != 0u) continue;
        atoms[index].hash = hash;
        atoms[index].id = (uint16_t)(DEMONX_DYNAMIC_ATOM_BASE + index);
        atoms[index].length = length;
        atoms[index].used = 1u;
        for (uint8_t byte = 0u; byte < length; ++byte)
            atoms[index].name[byte] = name[byte];
        return atoms[index].id;
    }
    return 0u;
}

static struct demonx_atom *find_atom(uint32_t id) {
    for (size_t index = 0u; index < DEMONX_ATOM_LIMIT; ++index)
        if (atoms[index].used != 0u && atoms[index].id == id)
            return &atoms[index];
    return NULL;
}

static struct demonx_property *find_property(uint32_t window, uint32_t atom) {
    for (size_t index = 0u; index < DEMONX_PROPERTY_LIMIT; ++index)
        if (properties[index].used != 0u &&
            properties[index].window == window &&
            properties[index].atom == atom)
            return &properties[index];
    return NULL;
}

static struct demonx_property *property_slot(uint32_t window, uint32_t atom) {
    struct demonx_property *property = find_property(window, atom);
    if (property != NULL) return property;
    for (size_t index = 0u; index < DEMONX_PROPERTY_LIMIT; ++index) {
        if (properties[index].used != 0u) continue;
        properties[index] = (struct demonx_property){
            .window = window, .atom = atom, .used = 1u
        };
        return &properties[index];
    }
    return NULL;
}

static int valid_property_window(uint32_t id) {
    return id == DEMONX_ROOT_WINDOW || find_window(id) != NULL;
}

static void remove_window_properties(uint32_t id) {
    for (size_t index = 0u; index < DEMONX_PROPERTY_LIMIT; ++index)
        if (properties[index].used != 0u &&
            properties[index].window == id)
            properties[index].used = 0u;
}

static void destroy_window_state(struct demonx_window *window) {
    const uint32_t id = window->id;
    if (focused_window == id) focused_window = 0u;
    if (pointer_grab_window == id) {
        pointer_grab_client = 0u;
        pointer_grab_window = 0u;
        pointer_grab_mask = 0u;
    }
    if (keyboard_grab_window == id) {
        keyboard_grab_client = 0u;
        keyboard_grab_window = 0u;
    }
    if (selection_owner == id) {
        selection_atom = 0u;
        selection_owner = 0u;
    }
    close_published_window(window);
    remove_window_properties(id);
    for (size_t index = 0u; index < DEMONX_GC_LIMIT; ++index)
        if (gcs[index].used != 0u && gcs[index].drawable == id)
            gcs[index].used = 0u;
    for (size_t index = 0u; index < DEMONX_PASSIVE_GRAB_LIMIT; ++index)
        if (passive_grabs[index].used != 0u &&
            passive_grabs[index].window == id)
            passive_grabs[index].used = 0u;
    window->used = 0u;
}

/* A display server must survive clients vanishing. X11 does this by
   destroying every resource the dead client owned; DemonX mirrors that:
   send_outgoing calls this when a client's reply channel is gone (its
   process exited or it explicitly tore the connection down), and the
   KillClient request uses the same cleanup. After this returns, the
   server keeps serving the remaining clients. */
static void release_client(uint32_t target_client) {
    if (target_client == 0u) return;
    for (size_t index = 0u; index < 16u; ++index)
        if (windows[index].used != 0u &&
            windows[index].owner_client == target_client)
            destroy_window_state(&windows[index]);
    for (size_t index = 0u; index < DEMONX_PIXMAP_LIMIT; ++index)
        if (pixmaps[index].used != 0u &&
            pixmaps[index].owner_client == target_client) {
            if (pixmaps[index].surface_handle != 0u)
                (void)syscall1(SYSCALL_HANDLE_CLOSE,
                               pixmaps[index].surface_handle);
            pixmaps[index].used = 0u;
        }
    for (size_t index = 0u; index < DEMONX_GC_LIMIT; ++index)
        if (gcs[index].used != 0u &&
            gcs[index].owner_client == target_client)
            gcs[index].used = 0u;
    for (size_t index = 0u; index < DEMONX_CURSOR_LIMIT; ++index)
        if (cursors[index].used != 0u &&
            cursors[index].owner_client == (uint8_t)target_client) {
            const uint32_t cursor_id = cursors[index].id;
            for (size_t window_index = 0u; window_index < 16u;
                 ++window_index)
                if (windows[window_index].used != 0u &&
                    windows[window_index].cursor == cursor_id)
                    windows[window_index].cursor = 0u;
            cursors[index].used = 0u;
        }
    for (size_t index = 0u; index < DEMONX_PASSIVE_GRAB_LIMIT; ++index)
        if (passive_grabs[index].used != 0u &&
            passive_grabs[index].client == target_client)
            passive_grabs[index].used = 0u;
    if (root_redirect_client == target_client) {
        root_event_mask = 0u;
        root_redirect_client = 0u;
        root_event_client = 0u;
    }
    if (pointer_grab_client == target_client) {
        pointer_grab_client = 0u;
        pointer_grab_window = 0u;
        pointer_grab_mask = 0u;
    }
    if (keyboard_grab_client == target_client) {
        keyboard_grab_client = 0u;
        keyboard_grab_window = 0u;
    }
    if (server_grab_client == target_client) server_grab_client = 0u;
    if (target_client <= DEMONX_MAX_CLIENTS) {
        if (reply_handles[target_client - 1u] != 0u &&
            reply_handles[target_client - 1u] != SYSCALL_FAILURE)
            (void)syscall1(SYSCALL_HANDLE_CLOSE,
                           reply_handles[target_client - 1u]);
        reply_handles[target_client - 1u] = 0u;
    }
}

static void apply_configure(struct demonx_window *window, uint16_t mask) {
    size_t offset = 12u;
    for (uint16_t bit = 1u; bit <= DEMONX_CW_STACK_MODE; bit <<= 1u) {
        if ((mask & bit) == 0u) continue;
        const uint32_t value = read32(&incoming.payload[offset]);
        if (bit == DEMONX_CW_X) window->x = (int16_t)value;
        else if (bit == DEMONX_CW_Y) window->y = (int16_t)value;
        else if (bit == DEMONX_CW_WIDTH && value != 0u)
            window->width = (uint16_t)value;
        else if (bit == DEMONX_CW_HEIGHT && value != 0u)
            window->height = (uint16_t)value;
        else if (bit == DEMONX_CW_BORDER_WIDTH)
            window->border = (uint16_t)value;
        offset += 4u;
    }
}

static void protocol_error(uint8_t code, uint8_t opcode, uint32_t bad_value) {
    clear_outgoing(incoming.client_id);
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
    clear_outgoing(incoming.client_id);
    outgoing.flags = DEMONX_FLAG_REPLY;
    outgoing.payload_length = 40u;
    outgoing.payload[0] = 1u;
    write16(&outgoing.payload[2], 11u);
    write16(&outgoing.payload[6], 8u);
    write32(&outgoing.payload[8], 1u);
    write32(&outgoing.payload[12], resource_base(incoming.client_id));
    write32(&outgoing.payload[16], DEMONX_RESOURCE_MASK);
    write16(&outgoing.payload[24], 0u);
    write16(&outgoing.payload[26], 12u);
    outgoing.payload[28] = 0u;
    outgoing.payload[29] = 0u;
    outgoing.payload[30] = 0u;
    outgoing.payload[31] = 0u;
    return 1;
}

static int parse_request(const char *reply_name) {
    const uint8_t opcode = incoming.payload[0];
    const uint16_t words = read16(&incoming.payload[2]);
    if (words == 0u || (uint32_t)words * 4u != incoming.payload_length) {
        protocol_error(16u, opcode, words);
        return 1;
    }
    const uint32_t id = read32(&incoming.payload[4]);
    if (opcode == DEMONX_GRAB_SERVER && incoming.payload_length == 8u) {
        if (server_grab_client == 0u ||
            server_grab_client == incoming.client_id) {
            server_grab_client = incoming.client_id;
            return 0;
        }
        protocol_error(10u, opcode, incoming.client_id);
        return 1;
    }
    if (opcode == DEMONX_UNGRAB_SERVER && incoming.payload_length == 8u) {
        if (server_grab_client == incoming.client_id)
            server_grab_client = 0u;
        return 0;
    }
    if (server_grab_client != 0u &&
        server_grab_client != incoming.client_id) {
        protocol_error(10u, opcode, incoming.client_id);
        return 1;
    }
    if (opcode == DEMONX_CHANGE_SAVE_SET &&
        incoming.payload_length == 8u) {
        struct demonx_window *target = find_window(id);
        if (target == NULL ||
            target->owner_client == incoming.client_id ||
            incoming.payload[1] > 1u) {
            protocol_error(target == NULL ? 3u : 8u, opcode, id);
            return 1;
        }
        if (incoming.payload[1] == 0u) {
            target->saved_by = (uint8_t)incoming.client_id;
        } else if (target->saved_by == (uint8_t)incoming.client_id) {
            target->saved_by = 0u;
        }
        return 0;
    }
    if (opcode == DEMONX_WARP_POINTER &&
        incoming.payload_length == 24u) {
        const uint32_t source = id;
        const uint32_t destination = read32(&incoming.payload[8]);
        struct demonx_window *destination_window =
            destination == DEMONX_ROOT_WINDOW ? NULL :
            find_window(destination);
        if ((source != 0u && source != DEMONX_ROOT_WINDOW &&
             find_window(source) == NULL) ||
            (destination != DEMONX_ROOT_WINDOW &&
             destination_window == NULL)) {
            protocol_error(3u, opcode,
                destination_window == NULL ? destination : source);
            return 1;
        }
        int32_t pointer_x = (int16_t)read16(&incoming.payload[20]);
        int32_t pointer_y = (int16_t)read16(&incoming.payload[22]);
        if (destination_window != NULL) {
            pointer_x += destination_window->x;
            pointer_y += destination_window->y;
        }
        if (pointer_x < 0) pointer_x = 0;
        if (pointer_y < 0) pointer_y = 0;
        if (pointer_x > 639) pointer_x = 639;
        if (pointer_y > 479) pointer_y = 479;
        if (!ensure_desktop_handles()) {
            protocol_error(11u, opcode, destination);
            return 1;
        }
        const struct demon_window_message message = {
            .version = DEMON_WINDOW_PROTOCOL_VERSION,
            .opcode = DEMON_WINDOW_POINTER_WARP,
            .serial = ++compositor_serial,
            .x = pointer_x, .y = pointer_y,
        };
        if (syscall3(SYSCALL_CHANNEL_SEND, compositor_handle,
                (uint64_t)(uintptr_t)&message, sizeof(message)) !=
                sizeof(message)) {
            protocol_error(11u, opcode, destination);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_KILL_CLIENT &&
        incoming.payload_length == 8u) {
        uint32_t target_client = 0u;
        struct demonx_window *resource_window = find_window(id);
        struct demonx_pixmap *resource_pixmap = find_pixmap(id);
        struct demonx_gc *resource_gc = find_gc(id);
        struct demonx_cursor *resource_cursor = find_cursor(id);
        if (resource_window != NULL)
            target_client = resource_window->owner_client;
        else if (resource_pixmap != NULL)
            target_client = resource_pixmap->owner_client;
        else if (resource_gc != NULL)
            target_client = resource_gc->owner_client;
        else if (resource_cursor != NULL)
            target_client = resource_cursor->owner_client;
        if (target_client == 0u) {
            protocol_error(2u, opcode, id);
            return 1;
        }
        release_client(target_client);
        return 0;
    }
    if (opcode == DEMONX_CREATE_FONT_CURSOR &&
        incoming.payload_length == 12u) {
        struct demonx_cursor *cursor =
            allocate_cursor(id, incoming.client_id);
        if (cursor == NULL) {
            protocol_error(14u, opcode, id);
            return 1;
        }
        cursor->shape = read16(&incoming.payload[8]);
        return 0;
    }
    if (opcode == DEMONX_DEFINE_CURSOR && incoming.payload_length == 12u) {
        struct demonx_window *target = find_window(id);
        struct demonx_cursor *cursor =
            find_cursor(read32(&incoming.payload[8]));
        if ((id != DEMONX_ROOT_WINDOW && target == NULL) ||
            (cursor != NULL &&
             cursor->owner_client != (uint8_t)incoming.client_id)) {
            protocol_error(target == NULL ? 3u : 6u, opcode, id);
            return 1;
        }
        if (target != NULL)
            target->cursor = cursor == NULL ? 0u : cursor->id;
        return 0;
    }
    if (opcode == DEMONX_FREE_CURSOR && incoming.payload_length == 8u) {
        struct demonx_cursor *cursor = find_cursor(id);
        if (cursor == NULL ||
            cursor->owner_client != (uint8_t)incoming.client_id) {
            protocol_error(6u, opcode, id);
            return 1;
        }
        for (size_t index = 0u; index < 16u; ++index)
            if (windows[index].used != 0u && windows[index].cursor == id)
                windows[index].cursor = 0u;
        cursor->used = 0u;
        return 0;
    }
    if (opcode == DEMONX_SET_SELECTION_OWNER &&
        incoming.payload_length == 16u) {
        const uint32_t owner = read32(&incoming.payload[8]);
        if (owner != 0u && find_window(owner) == NULL) {
            protocol_error(3u, opcode, owner);
            return 1;
        }
        selection_atom = id;
        selection_owner = owner;
        return 0;
    }
    if (opcode == DEMONX_GET_SELECTION_OWNER &&
        incoming.payload_length == 8u) {
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 32u;
        outgoing.payload[0] = 1u;
        write32(&outgoing.payload[8],
                selection_atom == id ? selection_owner : 0u);
        return 1;
    }
    if ((opcode == DEMONX_GRAB_KEY || opcode == DEMONX_GRAB_BUTTON ||
         opcode == DEMONX_UNGRAB_KEY || opcode == DEMONX_UNGRAB_BUTTON) &&
        incoming.payload_length == 24u) {
        const uint8_t kind =
            (opcode == DEMONX_GRAB_KEY || opcode == DEMONX_UNGRAB_KEY)
            ? 1u : 2u;
        const uint8_t detail = incoming.payload[1];
        const uint32_t modifiers = read32(&incoming.payload[8]);
        if (find_window(id) == NULL && id != DEMONX_ROOT_WINDOW) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        if (opcode == DEMONX_UNGRAB_KEY ||
            opcode == DEMONX_UNGRAB_BUTTON) {
            for (size_t index = 0u; index < DEMONX_PASSIVE_GRAB_LIMIT;
                 ++index) {
                struct demonx_passive_grab *grab = &passive_grabs[index];
                if (grab->used != 0u && grab->client == incoming.client_id &&
                    grab->kind == kind && grab->window == id &&
                    (detail == 0u || grab->detail == detail) &&
                    (modifiers == (1u << 15u) ||
                     grab->modifiers == modifiers))
                    grab->used = 0u;
            }
            return 0;
        }
        for (size_t index = 0u; index < DEMONX_PASSIVE_GRAB_LIMIT; ++index) {
            struct demonx_passive_grab *grab = &passive_grabs[index];
            if (grab->used != 0u && grab->kind == kind &&
                grab->detail == detail && grab->modifiers == modifiers &&
                grab->window == id) {
                if (grab->client != incoming.client_id)
                    protocol_error(10u, opcode, id);
                return grab->client != incoming.client_id;
            }
            if (grab->used == 0u) {
                *grab = (struct demonx_passive_grab){
                    .window = id, .client = incoming.client_id,
                    .modifiers = modifiers,
                    .event_mask = read32(&incoming.payload[12]),
                    .detail = detail, .kind = kind, .used = 1u,
                };
                return 0;
            }
        }
        protocol_error(11u, opcode, id);
        return 1;
    }
    if ((opcode == DEMONX_GRAB_POINTER ||
         opcode == DEMONX_GRAB_KEYBOARD) &&
        incoming.payload_length == 24u) {
        struct demonx_window *grab_window = find_window(id);
        uint8_t status = 0u;
        uint32_t *grab_client = opcode == DEMONX_GRAB_POINTER
            ? &pointer_grab_client : &keyboard_grab_client;
        if (grab_window == NULL || grab_window->mapped == 0u)
            status = 3u; /* GrabNotViewable */
        else if (*grab_client != 0u && *grab_client != incoming.client_id)
            status = 1u; /* AlreadyGrabbed */
        else if (opcode == DEMONX_GRAB_POINTER) {
            pointer_grab_client = incoming.client_id;
            pointer_grab_window = id;
            pointer_grab_mask = read32(&incoming.payload[8]);
        } else {
            keyboard_grab_client = incoming.client_id;
            keyboard_grab_window = id;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 32u;
        outgoing.payload[0] = 1u;
        outgoing.payload[1] = status;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        return 1;
    }
    if ((opcode == DEMONX_UNGRAB_POINTER ||
         opcode == DEMONX_UNGRAB_KEYBOARD) &&
        incoming.payload_length == 8u) {
        if (opcode == DEMONX_UNGRAB_POINTER &&
            pointer_grab_client == incoming.client_id) {
            pointer_grab_client = 0u;
            pointer_grab_window = 0u;
            pointer_grab_mask = 0u;
        } else if (opcode == DEMONX_UNGRAB_KEYBOARD &&
                   keyboard_grab_client == incoming.client_id) {
            keyboard_grab_client = 0u;
            keyboard_grab_window = 0u;
        }
        return 0;
    }
    if (opcode == DEMONX_INTERN_ATOM && incoming.payload_length >= 8u) {
        const uint16_t length = read16(&incoming.payload[4]);
        const uint32_t padded = ((uint32_t)length + 3u) & ~3u;
        if (length == 0u || length > 32u ||
            incoming.payload_length != 8u + padded) {
            protocol_error(2u, opcode, length);
            return 1;
        }
        const uint32_t atom = intern_atom(&incoming.payload[8],
                                          (uint8_t)length,
                                          incoming.payload[1] != 0u);
        if (atom == 0u && incoming.payload[1] == 0u) {
            protocol_error(11u, opcode, 0u);
            return 1;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 32u;
        outgoing.payload[0] = 1u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[8], atom);
        return 1;
    }
    if (opcode == DEMONX_GET_ATOM_NAME && incoming.payload_length == 8u) {
        struct demonx_atom *atom = find_atom(id);
        if (atom == NULL || atom->length > 16u) {
            protocol_error(atom == NULL ? 5u : 11u, opcode, id);
            return 1;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length =
            (uint16_t)(32u + ((atom->length + 3u) & ~3u));
        outgoing.payload[0] = 1u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[4], (atom->length + 3u) / 4u);
        write16(&outgoing.payload[8], atom->length);
        for (uint8_t index = 0u; index < atom->length; ++index)
            outgoing.payload[32u + index] = atom->name[index];
        return 1;
    }
    if (opcode == DEMONX_CHANGE_PROPERTY && incoming.payload_length >= 24u) {
        const uint32_t property_atom = read32(&incoming.payload[8]);
        const uint32_t type = read32(&incoming.payload[12]);
        const uint8_t format = incoming.payload[16];
        const uint32_t items = read32(&incoming.payload[20]);
        const uint32_t item_bytes =
            format == 8u ? 1u : (format == 16u ? 2u :
            (format == 32u ? 4u : 0u));
        const uint32_t data_length = items * item_bytes;
        const uint32_t padded = (data_length + 3u) & ~3u;
        if (!valid_property_window(id) || property_atom == 0u || type == 0u ||
            item_bytes == 0u || data_length > DEMONX_PROPERTY_DATA_BYTES ||
            incoming.payload_length != 24u + padded ||
            incoming.payload[1] > 2u) {
            protocol_error(2u, opcode, property_atom);
            return 1;
        }
        struct demonx_property *property = property_slot(id, property_atom);
        if (property == NULL) {
            protocol_error(11u, opcode, property_atom);
            return 1;
        }
        /* Replace is complete. Prepend/append are accepted only while the
           result remains inside the deliberately bounded property payload. */
        const uint8_t mode = incoming.payload[1];
        uint32_t start = 0u;
        if (mode != 0u) {
            if (property->type != type || property->format != format ||
                (uint32_t)property->data_length + data_length >
                    DEMONX_PROPERTY_DATA_BYTES) {
                protocol_error(8u, opcode, property_atom);
                return 1;
            }
            if (mode == 2u) start = property->data_length;
            else {
                for (uint32_t index = property->data_length; index > 0u; --index)
                    property->data[index + data_length - 1u] =
                        property->data[index - 1u];
            }
        }
        for (uint32_t index = 0u; index < data_length; ++index)
            property->data[start + index] = incoming.payload[24u + index];
        property->type = type;
        property->format = format;
        property->data_length =
            mode == 0u ? (uint8_t)data_length :
                         (uint8_t)(property->data_length + data_length);
        property->item_count = property->data_length / item_bytes;
        return property_notify(id, property_atom, 0u);
    }
    if (opcode == DEMONX_DELETE_PROPERTY && incoming.payload_length == 12u) {
        const uint32_t property_atom = read32(&incoming.payload[8]);
        struct demonx_property *property =
            find_property(id, property_atom);
        if (property != NULL) property->used = 0u;
        return property_notify(id, property_atom, 1u);
    }
    if (opcode == DEMONX_GET_PROPERTY && incoming.payload_length == 24u) {
        const uint32_t property_atom = read32(&incoming.payload[8]);
        const uint32_t requested_type = read32(&incoming.payload[12]);
        const uint32_t long_offset = read32(&incoming.payload[16]);
        const uint32_t long_length = read32(&incoming.payload[20]);
        struct demonx_property *property = find_property(id, property_atom);
        if (!valid_property_window(id)) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 32u;
        outgoing.payload[0] = 1u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        if (property == NULL ||
            (requested_type != 0u && requested_type != property->type))
            return 1;
        const uint32_t byte_offset = long_offset * 4u;
        if (byte_offset > property->data_length) {
            protocol_error(2u, opcode, long_offset);
            return 1;
        }
        uint32_t returned = property->data_length - byte_offset;
        const uint32_t requested_bytes = long_length * 4u;
        if (returned > requested_bytes) returned = requested_bytes;
        if (returned > 16u) returned = 16u;
        outgoing.payload[1] = property->format;
        write32(&outgoing.payload[4], (returned + 3u) / 4u);
        write32(&outgoing.payload[8], property->type);
        write32(&outgoing.payload[12],
                property->data_length - byte_offset - returned);
        const uint32_t unit = property->format / 8u;
        write32(&outgoing.payload[16], unit == 0u ? 0u : returned / unit);
        for (uint32_t index = 0u; index < returned; ++index)
            outgoing.payload[32u + index] =
                property->data[byte_offset + index];
        outgoing.payload_length = (uint16_t)(32u + ((returned + 3u) & ~3u));
        if (incoming.payload[1] != 0u &&
            property->data_length - byte_offset - returned == 0u)
            property->used = 0u;
        return 1;
    }
    if (opcode == DEMONX_LIST_PROPERTIES && incoming.payload_length == 8u) {
        if (!valid_property_window(id)) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        uint32_t listed[4];
        uint16_t count = 0u;
        for (size_t index = 0u; index < DEMONX_PROPERTY_LIMIT; ++index) {
            if (properties[index].used == 0u ||
                properties[index].window != id)
                continue;
            if (count == 4u) {
                protocol_error(11u, opcode, id);
                return 1;
            }
            listed[count++] = properties[index].atom;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = (uint16_t)(32u + count * 4u);
        outgoing.payload[0] = 1u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[4], count);
        write16(&outgoing.payload[8], count);
        for (uint16_t index = 0u; index < count; ++index)
            write32(&outgoing.payload[32u + index * 4u], listed[index]);
        return 1;
    }
    if (opcode == DEMONX_SEND_EVENT && incoming.payload_length == 44u) {
        struct demonx_window *destination = find_window(id);
        const uint8_t event_type = incoming.payload[12] & 0x7fu;
        if (destination == NULL || event_type != DEMONX_CLIENT_MESSAGE) {
            protocol_error(destination == NULL ? 3u : 2u, opcode, id);
            return 1;
        }
        clear_outgoing(destination->owner_client);
        outgoing.flags = DEMONX_FLAG_EVENT;
        outgoing.payload_length = 32u;
        for (uint8_t index = 0u; index < 32u; ++index)
            outgoing.payload[index] = incoming.payload[12u + index];
        outgoing.payload[0] = event_type | 0x80u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        return 1;
    }
    if (opcode == DEMONX_CREATE_PIXMAP && incoming.payload_length == 16u) {
        const uint32_t drawable = read32(&incoming.payload[8]);
        const uint16_t width = read16(&incoming.payload[12]);
        const uint16_t height = read16(&incoming.payload[14]);
        const uint8_t depth = incoming.payload[1];
        if ((drawable != DEMONX_ROOT_WINDOW && !valid_drawable(drawable)) ||
            width == 0u || height == 0u || width > DEMONX_WINDOW_MAX_WIDTH || height > DEMONX_WINDOW_MAX_HEIGHT ||
            (depth != 1u && depth != 24u && depth != 32u)) {
            protocol_error(2u, opcode, id);
            return 1;
        }
        struct demonx_pixmap *pixmap =
            allocate_pixmap(id, incoming.client_id);
        if (pixmap == NULL || !ensure_desktop_handles()) {
            if (pixmap != NULL) pixmap->used = 0u;
            protocol_error(11u, opcode, id);
            return 1;
        }
        const uint64_t surface = syscall3(SYSCALL_SURFACE_CREATE,
            surface_factory_handle, width, height);
        if (surface == SYSCALL_FAILURE || surface > UINT32_MAX) {
            pixmap->used = 0u;
            protocol_error(11u, opcode, id);
            return 1;
        }
        pixmap->surface_handle = (uint32_t)surface;
        pixmap->width = width;
        pixmap->height = height;
        pixmap->depth = depth;
        return 0;
    }
    if (opcode == DEMONX_FREE_PIXMAP && incoming.payload_length == 8u) {
        struct demonx_pixmap *pixmap = find_pixmap(id);
        if (pixmap == NULL || pixmap->owner_client != incoming.client_id) {
            protocol_error(4u, opcode, id);
            return 1;
        }
        (void)syscall1(SYSCALL_HANDLE_CLOSE, pixmap->surface_handle);
        pixmap->used = 0u;
        return 0;
    }
    if (opcode == DEMONX_CREATE_GC && incoming.payload_length >= 16u) {
        const uint32_t drawable = read32(&incoming.payload[8]);
        const uint32_t mask = read32(&incoming.payload[12]);
        const uint32_t supported =
            DEMONX_GC_FOREGROUND | DEMONX_GC_FONT;
        const uint32_t value_count =
            ((mask & DEMONX_GC_FOREGROUND) != 0u ? 1u : 0u) +
            ((mask & DEMONX_GC_FONT) != 0u ? 1u : 0u);
        if ((mask & ~supported) != 0u ||
            incoming.payload_length !=
                16u + value_count * 4u ||
            !valid_drawable(drawable)) {
            protocol_error(2u, opcode, id);
            return 1;
        }
        struct demonx_gc *gc = allocate_gc(id, incoming.client_id);
        if (gc == NULL) {
            protocol_error(14u, opcode, id);
            return 1;
        }
        gc->drawable = drawable;
        uint32_t offset = 16u;
        if ((mask & DEMONX_GC_FOREGROUND) != 0u) {
            gc->foreground = read32(&incoming.payload[offset]);
            offset += 4u;
        }
        if ((mask & DEMONX_GC_FONT) != 0u) {
            gc->font = read32(&incoming.payload[offset]);
            if (gc->font != 1u) {
                gc->used = 0u;
                protocol_error(7u, opcode, gc->font);
                return 1;
            }
        }
        return 0;
    }
    if (opcode == DEMONX_CHANGE_GC && incoming.payload_length >= 12u) {
        struct demonx_gc *gc = find_gc(id);
        const uint32_t mask = read32(&incoming.payload[8]);
        if (gc == NULL || gc->owner_client != incoming.client_id ||
            (mask != DEMONX_GC_FOREGROUND && mask != DEMONX_GC_FONT) ||
            incoming.payload_length != 16u) {
            protocol_error(gc == NULL ? 13u : 2u, opcode, id);
            return 1;
        }
        if (mask == DEMONX_GC_FOREGROUND)
            gc->foreground = read32(&incoming.payload[12]);
        else if (read32(&incoming.payload[12]) == 1u)
            gc->font = 1u;
        else {
            protocol_error(7u, opcode, read32(&incoming.payload[12]));
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_FREE_GC && incoming.payload_length == 8u) {
        struct demonx_gc *gc = find_gc(id);
        if (gc == NULL || gc->owner_client != incoming.client_id) {
            protocol_error(13u, opcode, id);
            return 1;
        }
        gc->used = 0u;
        return 0;
    }
    if (opcode == DEMONX_POLY_FILL_RECTANGLE &&
        incoming.payload_length >= 20u &&
        (incoming.payload_length - 12u) % 8u == 0u) {
        struct demonx_window *drawable = find_window(id);
        struct demonx_pixmap *pixmap = find_pixmap(id);
        struct demonx_gc *gc = find_gc(read32(&incoming.payload[8]));
        if ((drawable == NULL && pixmap == NULL) || gc == NULL ||
            gc->owner_client != incoming.client_id ||
            gc->drawable != id) {
            protocol_error(drawable == NULL && pixmap == NULL ? 9u : 13u,
                           opcode, id);
            return 1;
        }
        const uint32_t count = (incoming.payload_length - 12u) / 8u;
        for (uint32_t index = 0u; index < count; ++index) {
            const uint8_t *rectangle = &incoming.payload[12u + index * 8u];
            const int drawn = drawable != NULL
                ? fill_rectangle(drawable, gc,
                    (int16_t)read16(&rectangle[0]),
                    (int16_t)read16(&rectangle[2]),
                    read16(&rectangle[4]), read16(&rectangle[6]))
                : fill_pixmap(pixmap, gc,
                    (int16_t)read16(&rectangle[0]),
                    (int16_t)read16(&rectangle[2]),
                    read16(&rectangle[4]), read16(&rectangle[6]));
            if (drawn < 0) {
                protocol_error(drawn == -1 ?
                               (uint8_t)(10u + surface_failure) :
                               (drawn == -2 ? 17u : 8u),
                               opcode, id);
                return 1;
            }
        }
        return 0;
    }
    if (opcode == DEMONX_PUT_IMAGE && incoming.payload_length >= 28u) {
        const uint32_t drawable = id;
        const uint32_t gc_id = read32(&incoming.payload[8]);
        const uint16_t width = read16(&incoming.payload[12]);
        const uint16_t height = read16(&incoming.payload[14]);
        const uint32_t data_bytes = incoming.payload_length - 24u;
        struct demonx_gc *gc = find_gc(gc_id);
        if (incoming.payload[1] != 2u || incoming.payload[20] != 0u ||
            incoming.payload[21] != 32u || width == 0u || height == 0u ||
            (uint32_t)width * height > (DEMONX_MESSAGE_BYTES - 24u) / 4u ||
            data_bytes != (uint32_t)width * height * 4u ||
            gc == NULL || gc->owner_client != incoming.client_id ||
            gc->drawable != drawable || !valid_drawable(drawable)) {
            protocol_error(gc == NULL ? 13u : 2u, opcode, drawable);
            return 1;
        }
        const int written = put_image(drawable,
            (int16_t)read16(&incoming.payload[16]),
            (int16_t)read16(&incoming.payload[18]),
            width, height, &incoming.payload[24]);
        if (written < 0) {
            protocol_error(written == -4 ? 9u : 11u, opcode, drawable);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_COPY_AREA && incoming.payload_length == 28u) {
        const uint32_t source = id;
        const uint32_t destination = read32(&incoming.payload[8]);
        struct demonx_gc *gc = find_gc(read32(&incoming.payload[12]));
        if (!valid_drawable(source) || !valid_drawable(destination) ||
            gc == NULL || gc->owner_client != incoming.client_id ||
            gc->drawable != destination) {
            protocol_error(!valid_drawable(source) ||
                           !valid_drawable(destination) ? 9u : 13u,
                           opcode, !valid_drawable(source) ? source :
                           (!valid_drawable(destination) ? destination :
                            read32(&incoming.payload[12])));
            return 1;
        }
        const int copied = copy_area(source, destination,
            (int16_t)read16(&incoming.payload[16]),
            (int16_t)read16(&incoming.payload[18]),
            (int16_t)read16(&incoming.payload[20]),
            (int16_t)read16(&incoming.payload[22]),
            read16(&incoming.payload[24]), read16(&incoming.payload[26]));
        if (copied < 0) {
            protocol_error(copied == -1 ? 9u : 11u, opcode, destination);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_GET_IMAGE && incoming.payload_length == 20u) {
        uint32_t surface, drawable_width, drawable_height;
        struct demonx_window *drawable_window;
        const int16_t x = (int16_t)read16(&incoming.payload[8]);
        const int16_t y = (int16_t)read16(&incoming.payload[10]);
        const uint16_t width = read16(&incoming.payload[12]);
        const uint16_t height = read16(&incoming.payload[14]);
        const uint32_t count = (uint32_t)width * height;
        if (incoming.payload[1] != 2u || width == 0u || height == 0u ||
            count > 56u || x < 0 || y < 0 ||
            !drawable_surface(id, &surface, &drawable_width,
                              &drawable_height, &drawable_window) ||
            (uint32_t)x + width > drawable_width ||
            (uint32_t)y + height > drawable_height) {
            protocol_error(valid_drawable(id) ? 2u : 9u, opcode, id);
            return 1;
        }
        const uint64_t mapped = syscall1(SYSCALL_SURFACE_MAP, surface);
        if (mapped == SYSCALL_FAILURE) {
            protocol_error(11u, opcode, id);
            return 1;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = (uint16_t)(32u + count * 4u);
        outgoing.payload[0] = 1u;
        outgoing.payload[1] = 32u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[8], 0u);
        const uint32_t *pixels = (const uint32_t *)(uintptr_t)mapped;
        for (uint32_t row = 0u; row < height; ++row)
            for (uint32_t column = 0u; column < width; ++column)
                write32(&outgoing.payload[32u + (row * width + column) * 4u],
                        pixels[((uint32_t)y + row) * drawable_width +
                               (uint32_t)x + column] &
                        read32(&incoming.payload[16]));
        (void)syscall1(SYSCALL_SURFACE_UNMAP, surface);
        return 1;
    }
    if (opcode == DEMONX_POLY_TEXT8 && incoming.payload_length >= 20u) {
        struct demonx_gc *gc = find_gc(read32(&incoming.payload[8]));
        const uint8_t length = incoming.payload[16];
        const uint16_t expected =
            (uint16_t)((17u + (uint16_t)length + 3u) & ~3u);
        if (incoming.payload_length != expected || gc == NULL ||
            gc->owner_client != incoming.client_id || gc->drawable != id ||
            !valid_drawable(id)) {
            protocol_error(gc == NULL ? 13u : 2u, opcode, id);
            return 1;
        }
        const int drawn = draw_text(id, gc,
            (int16_t)read16(&incoming.payload[12]),
            (int16_t)read16(&incoming.payload[14]),
            &incoming.payload[17], length);
        if (drawn < 0) {
            protocol_error(11u, opcode, id);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_SELECT_INPUT && id == DEMONX_ROOT_WINDOW &&
        incoming.payload_length == 12u) {
        const uint32_t mask = read32(&incoming.payload[8]);
        if ((mask & DEMONX_SUBSTRUCTURE_REDIRECT_MASK) != 0u &&
            root_redirect_client != 0u &&
            root_redirect_client != incoming.client_id) {
            protocol_error(10u, opcode, id);
            return 1;
        }
        root_event_mask = mask;
        root_event_client = mask != 0u ? incoming.client_id : 0u;
        root_redirect_client =
            (mask & DEMONX_SUBSTRUCTURE_REDIRECT_MASK) != 0u
                ? incoming.client_id : 0u;
        return 0;
    }
    if (opcode == DEMONX_CREATE_WINDOW) {
        if (incoming.payload_length != 32u || read32(&incoming.payload[8]) != DEMONX_ROOT_WINDOW) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        struct demonx_window *window = allocate_window(id, incoming.client_id);
        if (window == NULL) { protocol_error(14u, opcode, id); return 1; }
        window->parent = DEMONX_ROOT_WINDOW;
        window->x = (int16_t)read16(&incoming.payload[12]);
        window->y = (int16_t)read16(&incoming.payload[14]);
        window->width = read16(&incoming.payload[16]);
        window->height = read16(&incoming.payload[18]);
        window->border = read16(&incoming.payload[20]);
        window->window_class = read16(&incoming.payload[22]);
        window->owner_client = incoming.client_id;
        window->background_pixel = read32(&incoming.payload[24]);
        const uint32_t attribute_mask = read32(&incoming.payload[28]);
        if ((attribute_mask & ~2u) != 0u) {
            window->used = 0u;
            protocol_error(2u, opcode, id);
            return 1;
        }
        if (window->width == 0u || window->height == 0u) {
            window->used = 0u;
            protocol_error(2u, opcode, id);
        }
        return 0;
    }
    struct demonx_window *window = find_window(id);
    if (opcode == DEMONX_SET_INPUT_FOCUS &&
        incoming.payload_length == 12u) {
        if (window == NULL || window->mapped == 0u ||
            incoming.payload[1] > 2u) {
            protocol_error(window == NULL ? 3u : 8u, opcode, id);
            return 1;
        }
        struct demonx_window *previous = find_window(focused_window);
        if (previous == window) return 0;
        if (previous != window &&
            focus_notify(previous, DEMONX_FOCUS_OUT) != 0)
            (void)send_outgoing(reply_name);
        focused_window = window->id;
        /* X11-level FocusIn/FocusOut above only reaches the client itself.
           The native compositor tracks its own, separate notion of which
           window receives native KEY/POINTER forwarding (see
           compositor_wait's real dispatch loop), and nothing here ever told
           it a focus change happened -- so it kept routing input to
           whichever window had most recently sent CREATE (typically
           DemonWM's own reparenting frame, never the client content window
           XSetInputFocus actually targets). Push the same real CREATE/MOVE/
           CLOSE-style native message so the compositor's focused window
           tracking matches DemonX's. */
        if (ensure_desktop_handles()) {
            struct demon_window_message native_focus = {
                .version = DEMON_WINDOW_PROTOCOL_VERSION,
                .opcode = DEMON_WINDOW_FOCUS,
                .serial = ++compositor_serial,
                .window_id = window->id,
            };
            (void)syscall3(SYSCALL_CHANNEL_SEND, compositor_handle,
                (uint64_t)(uintptr_t)&native_focus, sizeof(native_focus));
        }
        return focus_notify(window, DEMONX_FOCUS_IN);
    }
    if (opcode == DEMONX_GET_WINDOW_ATTRIBUTES &&
        incoming.payload_length == 8u) {
        if (window == NULL) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = 44u;
        outgoing.payload[0] = 1u;
        outgoing.payload[1] = 32u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write16(&outgoing.payload[8], (uint16_t)window->x);
        write16(&outgoing.payload[10], (uint16_t)window->y);
        write16(&outgoing.payload[12], window->width);
        write16(&outgoing.payload[14], window->height);
        write16(&outgoing.payload[16], window->border);
        write32(&outgoing.payload[20], DEMONX_ROOT_WINDOW);
        write16(&outgoing.payload[24], window->window_class);
        outgoing.payload[26] = window->mapped != 0u ? 2u : 0u;
        write32(&outgoing.payload[28],
                window->event_client == incoming.client_id
                    ? window->event_mask : 0u);
        write32(&outgoing.payload[32], window->event_mask);
        return 1;
    }
    if (opcode == DEMONX_QUERY_TREE && incoming.payload_length == 8u) {
        if (id != DEMONX_ROOT_WINDOW && window == NULL) {
            protocol_error(3u, opcode, id);
            return 1;
        }
        uint32_t children[4];
        uint16_t count = 0u;
        for (size_t index = 0u; index < 16u; ++index) {
            if (windows[index].used == 0u || windows[index].parent != id)
                continue;
            if (count == 4u) {
                protocol_error(11u, opcode, id);
                return 1;
            }
            children[count++] = windows[index].id;
        }
        clear_outgoing(incoming.client_id);
        outgoing.flags = DEMONX_FLAG_REPLY;
        outgoing.payload_length = (uint16_t)(32u + count * 4u);
        outgoing.payload[0] = 1u;
        write16(&outgoing.payload[2], (uint16_t)incoming.sequence);
        write32(&outgoing.payload[4], count);
        write32(&outgoing.payload[8], DEMONX_ROOT_WINDOW);
        write32(&outgoing.payload[12],
                id == DEMONX_ROOT_WINDOW ? 0u : window->parent);
        write16(&outgoing.payload[16], count);
        for (uint16_t index = 0u; index < count; ++index)
            write32(&outgoing.payload[32u + index * 4u], children[index]);
        return 1;
    }
    if (window == NULL) { protocol_error(3u, opcode, id); return 1; }
    if (opcode == DEMONX_CHANGE_WINDOW_ATTRIBUTES &&
        incoming.payload_length == 16u) {
        const uint32_t mask = read32(&incoming.payload[8]);
        const uint32_t value = read32(&incoming.payload[12]);
        if (mask == 1u) {
            if (value != 0u && value != 1u && find_pixmap(value) == NULL) {
                protocol_error(4u, opcode, value);
                return 1;
            }
            window->background_pixmap = value > 1u ? value : 0u;
        } else if (mask == 2u) {
            window->background_pixel = value;
            window->background_pixmap = 0u;
        } else {
            protocol_error(2u, opcode, id);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_CLEAR_AREA &&
        incoming.payload_length == 16u) {
        const int cleared = clear_background(window,
            (int16_t)read16(&incoming.payload[8]),
            (int16_t)read16(&incoming.payload[10]),
            read16(&incoming.payload[12]), read16(&incoming.payload[14]));
        if (cleared < 0) {
            protocol_error(11u, opcode, id);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_MAP_WINDOW && incoming.payload_length == 8u) {
        if ((root_event_mask & DEMONX_SUBSTRUCTURE_REDIRECT_MASK) != 0u &&
            root_redirect_client != incoming.client_id) {
            map_request(window);
            return 1;
        }
        /* Mapping is an X11 state transition even if the native desktop is
           temporarily at its bounded surface/window capacity. Keep the X
           server responsive; drawing remains retained in the native surface
           and a later compositor bridge can republish it. */
        if (create_window_surface(window))
            (void)clear_background(window, 0, 0, 0u, 0u);
        (void)publish_window(window);
        window->mapped = 1u;
        if ((window->event_mask & DEMONX_STRUCTURE_NOTIFY_MASK) != 0u) {
            map_notify(window);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_SELECT_INPUT && incoming.payload_length == 12u) {
        const uint32_t mask = read32(&incoming.payload[8]);
        /* Additive, not a replace: a window manager's later SelectInput on
           a client it now manages (watchWindow's own Structure/Button/
           Motion/Focus/Property interest) must not erase the bits the
           client itself already registered (its own KeyPress/KeyRelease,
           typically), or the required-mask check every native input
           dispatch does below would start rejecting events the client
           genuinely still wants. See key_event_client's own comment for
           the matching event_client half of this. */
        window->event_mask |= mask;
        if (mask != 0u) window->event_client = incoming.client_id;
        if ((mask & (DEMONX_KEY_PRESS_MASK | DEMONX_KEY_RELEASE_MASK)) != 0u)
            window->key_event_client = incoming.client_id;
        return 0;
    }
    if (opcode == DEMONX_CONFIGURE_WINDOW && incoming.payload_length >= 12u) {
        const uint16_t mask = read16(&incoming.payload[8]);
        if ((mask & ~(uint16_t)127u) != 0u ||
            incoming.payload_length != 12u + value_count(mask) * 4u) {
            protocol_error(2u, opcode, mask);
            return 1;
        }
        if ((root_event_mask & DEMONX_SUBSTRUCTURE_REDIRECT_MASK) != 0u &&
            root_redirect_client != incoming.client_id) {
            struct demonx_window requested = *window;
            apply_configure(&requested, mask);
            configure_request(&requested, mask);
            return 1;
        }
        apply_configure(window, mask);
        move_published_window(window);
        move_published_descendants(window->id);
        if ((window->event_mask & DEMONX_STRUCTURE_NOTIFY_MASK) != 0u) {
            configure_notify(window);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_UNMAP_WINDOW && incoming.payload_length == 8u) {
        window->mapped = 0u;
        if ((root_event_mask & DEMONX_SUBSTRUCTURE_NOTIFY_MASK) != 0u) {
            unmap_notify(window, root_event_client, window->parent);
            return 1;
        }
        if ((window->event_mask & DEMONX_STRUCTURE_NOTIFY_MASK) != 0u) {
            unmap_notify(window, window->event_client, window->id);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_REPARENT_WINDOW && incoming.payload_length == 16u) {
        const uint32_t parent = read32(&incoming.payload[8]);
        if (parent != DEMONX_ROOT_WINDOW && find_window(parent) == NULL) {
            protocol_error(3u, opcode, parent);
            return 1;
        }
        window->parent = parent;
        window->x = (int16_t)read16(&incoming.payload[12]);
        window->y = (int16_t)read16(&incoming.payload[14]);
        move_published_window(window);
        move_published_descendants(window->id);
        if ((window->event_mask & DEMONX_STRUCTURE_NOTIFY_MASK) != 0u) {
            reparent_notify(window);
            return 1;
        }
        return 0;
    }
    if (opcode == DEMONX_DESTROY_WINDOW && incoming.payload_length == 8u) {
        destroy_window_state(window);
        return 0;
    }
    if (opcode == DEMONX_GET_GEOMETRY && incoming.payload_length == 8u) {
        clear_outgoing(incoming.client_id);
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

static uint64_t connect_reply(const char *template_name, uint32_t client_id) {
    if (client_id == 0u || client_id > DEMONX_MAX_CLIENTS)
        return SYSCALL_FAILURE;
    char name[14];
    for (size_t index = 0u; index < sizeof(name); ++index)
        name[index] = template_name[index];
    name[13] = (char)('0' + client_id);
    return syscall2(SYSCALL_CHANNEL_CONNECT,
                    (uint64_t)(uintptr_t)name, sizeof(name));
}

static int send_outgoing(const char *reply_name) {
    const uint32_t client_id = outgoing.client_id;
    if (client_id == 0u || client_id > DEMONX_MAX_CLIENTS) return 1;
    uint64_t *handle = &reply_handles[client_id - 1u];
    if (*handle == 0u || *handle == SYSCALL_FAILURE)
        *handle = connect_reply(reply_name, client_id);
    if (*handle == SYSCALL_FAILURE) {
        /* The client's reply channel is gone, so its process is gone too.
           Release its resources and keep serving the rest of the desktop
           rather than dying with it (previously a stale client teardown
           raced the server and killed the whole display: the client burst
           its X requests, exited, and the server only drained the queue
           afterwards, failing the delivery of an event reply). */
        release_client(client_id);
        return 1;
    }
    uint32_t offset = 0u;
    do {
        const uint32_t remaining = outgoing.payload_length - offset;
        const uint16_t count = remaining > DEMONX_PAYLOAD_BYTES
            ? DEMONX_PAYLOAD_BYTES : (uint16_t)remaining;
        for (size_t index = 0u; index < sizeof(outgoing_wire); ++index)
            ((uint8_t *)(uintptr_t)&outgoing_wire)[index] = 0u;
        outgoing_wire.magic = DEMONX_TRANSPORT_MAGIC;
        outgoing_wire.payload_length = count;
        outgoing_wire.flags = outgoing.flags |
            (remaining > count ? DEMONX_FLAG_MORE : 0u);
        outgoing_wire.client_id = outgoing.client_id;
        outgoing_wire.sequence = outgoing.sequence;
        for (uint16_t index = 0u; index < count; ++index)
            outgoing_wire.payload[index] = outgoing.payload[offset + index];
        if (syscall3(SYSCALL_CHANNEL_SEND, *handle,
                     (uint64_t)(uintptr_t)&outgoing_wire,
                     sizeof(outgoing_wire)) != sizeof(outgoing_wire)) {
            (void)syscall1(SYSCALL_HANDLE_CLOSE, *handle);
            *handle = connect_reply(reply_name, client_id);
            if (*handle == SYSCALL_FAILURE) {
                release_client(client_id);
                return 1;
            }
            if (syscall3(SYSCALL_CHANNEL_SEND, *handle,
                         (uint64_t)(uintptr_t)&outgoing_wire,
                         sizeof(outgoing_wire)) != sizeof(outgoing_wire)) {
                (void)syscall1(SYSCALL_HANDLE_CLOSE, *handle);
                *handle = 0u;
                release_client(client_id);
                return 1;
            }
        }
        offset += count;
    } while (offset < outgoing.payload_length);
    return 1;
}

static int process_native_input(const struct demon_window_message *message,
                                const char *reply_name) {
    struct demonx_window *window = find_window(message->window_id);
    const int is_key = message->opcode == DEMON_WINDOW_KEY;
    const int is_pointer = message->opcode == 11u ||
                           message->opcode == DEMON_WINDOW_POINTER;
    uint32_t grab_client = is_key ? keyboard_grab_client :
                           (is_pointer ? pointer_grab_client : 0u);
    uint32_t grab_window = is_key ? keyboard_grab_window :
                           (is_pointer ? pointer_grab_window : 0u);
    if (grab_client != 0u)
        window = find_window(grab_window);
    if (window == NULL) return 1;
    uint8_t event_type = 0u, detail = 0u;
    uint32_t state = 0u, value = 0u;
    int16_t x = 0, y = 0;
    if (message->opcode == DEMON_WINDOW_KEY) {
        const uint32_t packed = (uint32_t)message->x;
        const uint32_t native_type = packed & 0xffffu;
        event_type = native_type == 1u ? DEMONX_KEY_PRESS :
            (native_type == 2u ? DEMONX_KEY_RELEASE : 0u);
        detail = (uint8_t)(packed >> 16u);
        state = (uint32_t)message->y;
        value = message->width;
        if (grab_client == 0u && event_type == DEMONX_KEY_PRESS) {
            struct demonx_passive_grab *passive =
                matching_passive_grab(1u, detail, state);
            if (passive != NULL) {
                grab_client = passive->client;
                grab_window = passive->window;
            }
        }
        const uint32_t required = event_type == DEMONX_KEY_PRESS
            ? DEMONX_KEY_PRESS_MASK : DEMONX_KEY_RELEASE_MASK;
        if (event_type == 0u ||
            (grab_client == 0u &&
             (window->event_mask & required) == 0u))
            return 1;
    } else if (message->opcode == 11u) {
        const uint32_t required = message->width == 2u
            ? DEMONX_BUTTON_RELEASE_MASK : DEMONX_BUTTON_PRESS_MASK;
        if (grab_client == 0u &&
            (window->event_mask & required) == 0u) return 1;
        if (grab_client != 0u &&
            (pointer_grab_mask & required) == 0u) return 1;
        event_type = message->width == 2u
            ? DEMONX_BUTTON_RELEASE : DEMONX_BUTTON_PRESS;
        detail = message->height == 0u ? 1u : (uint8_t)message->height;
        x = (int16_t)message->x;
        y = (int16_t)message->y;
        if (grab_client == 0u && event_type == DEMONX_BUTTON_PRESS) {
            struct demonx_passive_grab *passive =
                matching_passive_grab(2u, detail, 0u);
            if (passive != NULL) {
                grab_client = passive->client;
                grab_window = passive->window;
                pointer_grab_mask = passive->event_mask;
            }
        }
    } else if (message->opcode == DEMON_WINDOW_POINTER) {
        if (grab_client == 0u &&
            (window->event_mask & DEMONX_POINTER_MOTION_MASK) == 0u)
            return 1;
        if (grab_client != 0u &&
            (pointer_grab_mask & DEMONX_POINTER_MOTION_MASK) == 0u)
            return 1;
        event_type = DEMONX_MOTION_NOTIFY;
        x = (int16_t)message->x;
        y = (int16_t)message->y;
        state = message->width;
    } else {
        return 1;
    }
    const uint32_t owner_client = is_key ? window->key_event_client : window->event_client;
    if (grab_client == 0u && owner_client == 0u) return 1;
    const uint32_t destination_client =
        grab_client != 0u ? grab_client : owner_client;
    clear_outgoing(destination_client);
    outgoing.flags = DEMONX_FLAG_EVENT;
    outgoing.payload_length = 32u;
    outgoing.payload[0] = event_type;
    outgoing.payload[1] = detail;
    write32(&outgoing.payload[8], DEMONX_ROOT_WINDOW);
    write32(&outgoing.payload[12],
            grab_client != 0u ? grab_window : window->id);
    write32(&outgoing.payload[16], value);
    write16(&outgoing.payload[20], (uint16_t)x);
    write16(&outgoing.payload[22], (uint16_t)y);
    write32(&outgoing.payload[24], state);
    outgoing.payload[30] = 1u;
    return send_outgoing(reply_name);
}

uint64_t demonx_main(const char *service_name, const char *reply_name) {
    const uint64_t server = syscall2(SYSCALL_CHANNEL_CREATE,
        (uint64_t)(uintptr_t)service_name, 16u);
    if (server == SYSCALL_FAILURE) return 120u;
    for (;;) {
        const uint64_t received = syscall4(SYSCALL_CHANNEL_RECEIVE, server,
            (uint64_t)(uintptr_t)&incoming_wire, sizeof(incoming_wire), 0u);
        if (received != sizeof(incoming_wire))
            return 121u;
        const struct demon_window_message *native =
            (const struct demon_window_message *)(const void *)&incoming_wire;
        if (native->version == DEMON_WINDOW_PROTOCOL_VERSION &&
            (native->opcode == DEMON_WINDOW_KEY ||
             native->opcode == DEMON_WINDOW_POINTER ||
             native->opcode == 11u)) {
            if (!process_native_input(native, reply_name)) return 126u;
            continue;
        }
        if (incoming_wire.magic != DEMONX_TRANSPORT_MAGIC ||
            incoming_wire.payload_length > DEMONX_PAYLOAD_BYTES)
            return 121u;
        if (incoming_wire.client_id == 0u ||
            incoming_wire.client_id > DEMONX_MAX_CLIENTS)
            return 122u;
        if (!assembly_active) {
            incoming.magic = incoming_wire.magic;
            incoming.payload_length = 0u;
            incoming.flags = incoming_wire.flags & ~DEMONX_FLAG_MORE;
            incoming.client_id = incoming_wire.client_id;
            incoming.sequence = incoming_wire.sequence;
            assembly_active = 1u;
        } else if (incoming.client_id != incoming_wire.client_id ||
                   incoming.sequence != incoming_wire.sequence ||
                   incoming.flags !=
                       (incoming_wire.flags & ~DEMONX_FLAG_MORE)) {
            return 125u;
        }
        if ((uint32_t)incoming.payload_length +
            incoming_wire.payload_length > DEMONX_MESSAGE_BYTES)
            return 126u;
        for (uint16_t index = 0u; index < incoming_wire.payload_length; ++index)
            incoming.payload[incoming.payload_length + index] =
                incoming_wire.payload[index];
        incoming.payload_length =
            (uint16_t)(incoming.payload_length + incoming_wire.payload_length);
        if ((incoming_wire.flags & DEMONX_FLAG_MORE) != 0u) continue;
        assembly_active = 0u;
        int sends_reply;
        if ((incoming.flags & DEMONX_FLAG_SETUP) != 0u) {
            if (!parse_setup()) return 123u;
            sends_reply = 1;
        } else {
            sends_reply = parse_request(reply_name);
        }
        if (sends_reply != 0 && !send_outgoing(reply_name))
            return 124u;
    }
    (void)syscall1(SYSCALL_HANDLE_CLOSE, server);
    (void)syscall1(SYSCALL_EXIT, 0u);
    return 0u;
}

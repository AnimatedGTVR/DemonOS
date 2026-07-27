#include <kernel/surface.h>

struct surface_object {
    uint32_t owner_pid;
    uint32_t width;
    uint32_t height;
    uint32_t offset_pixels;
    uint32_t capacity_pixels;
    uint32_t references;
    uint16_t damage_x, damage_y, damage_width, damage_height;
    bool damage_pending;
    bool active;
};

static struct surface_object objects[SURFACE_LIMIT];
static uint32_t *pixel_arena;
static size_t pixel_arena_count;
static uint64_t write_count;
static uint64_t read_count;
static uint64_t share_count;
static uint64_t created_count;
static uint64_t reclaimed_count;
static uint64_t mapping_count;
static uint64_t damage_count;
static uint64_t damage_consumed_count;
static struct surface_object *resolve(uint32_t object_id);

bool surfaces_init(uint32_t *arena, size_t arena_bytes) {
    if (arena == NULL || arena_bytes < SURFACE_ARENA_BYTES) return false;
    pixel_arena = arena;
    pixel_arena_count = arena_bytes / sizeof(uint32_t);
    for (size_t object = 0u; object < SURFACE_LIMIT; ++object) {
        objects[object] = (struct surface_object){0};
    }
    for (size_t pixel = 0u; pixel < pixel_arena_count; ++pixel) pixel_arena[pixel] = 0u;
    write_count = read_count = share_count = 0u;
    created_count = reclaimed_count = 0u;
    mapping_count = damage_count = damage_consumed_count = 0u;
    return true;
}

bool surface_create(uint32_t owner_pid, uint32_t width, uint32_t height,
                    uint32_t *object_id) {
    if (pixel_arena == NULL || owner_pid == 0u || object_id == NULL ||
        width == 0u || height == 0u || width > SURFACE_MAX_WIDTH ||
        height > SURFACE_MAX_HEIGHT) return false;
    for (size_t object = 0u; object < SURFACE_LIMIT; ++object) {
        if (objects[object].active) continue;
        const size_t pixels = (size_t)width * height;
        const size_t capacity_pixels = (pixels + 1023u) & ~1023u;
        size_t offset_pixels = 0u;
        bool found = false;
        while (offset_pixels <= pixel_arena_count &&
               capacity_pixels <= pixel_arena_count - offset_pixels) {
            bool overlaps = false;
            for (size_t other = 0u; other < SURFACE_LIMIT; ++other) {
                if (!objects[other].active) continue;
                const size_t other_start = objects[other].offset_pixels;
                const size_t other_end = other_start + objects[other].capacity_pixels;
                if (offset_pixels < other_end &&
                    other_start < offset_pixels + capacity_pixels) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) { found = true; break; }
            offset_pixels += 1024u;
        }
        if (!found) return false;
        objects[object] = (struct surface_object){
            .owner_pid = owner_pid, .width = width, .height = height,
            .offset_pixels = (uint32_t)offset_pixels,
            .capacity_pixels = (uint32_t)capacity_pixels, .active = true,
        };
        *object_id = (uint32_t)object + 1u;
        ++created_count;
        return true;
    }
    return false;
}

bool surface_retain(uint32_t object_id) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || surface->references == UINT32_MAX) return false;
    ++surface->references;
    return true;
}

static void clear_object(struct surface_object *surface) {
    uint32_t *pixels = pixel_arena + surface->offset_pixels;
    for (size_t pixel = 0u; pixel < surface->capacity_pixels; ++pixel)
        pixels[pixel] = 0u;
    *surface = (struct surface_object){0};
    ++reclaimed_count;
}

bool surface_release(uint32_t object_id) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || surface->references == 0u) return false;
    --surface->references;
    if (surface->references == 0u) clear_object(surface);
    return true;
}

bool surface_discard(uint32_t object_id) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || surface->references != 0u) return false;
    clear_object(surface);
    return true;
}

static struct surface_object *resolve(uint32_t object_id) {
    if (object_id == 0u || object_id > SURFACE_LIMIT ||
        !objects[object_id - 1u].active) return NULL;
    return &objects[object_id - 1u];
}

bool surface_write(uint32_t object_id, size_t pixel_offset,
                   const uint32_t *pixels, size_t pixel_count) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || pixels == NULL) return false;
    const size_t total = (size_t)surface->width * surface->height;
    if (pixel_offset > total || pixel_count > total - pixel_offset) return false;
    uint32_t *destination = pixel_arena + surface->offset_pixels + pixel_offset;
    for (size_t pixel = 0u; pixel < pixel_count; ++pixel)
        destination[pixel] = pixels[pixel];
    ++write_count;
    return true;
}

bool surface_data(uint32_t object_id, const uint32_t **pixels,
                  size_t *pixel_count, uint32_t *width, uint32_t *height) {
    const struct surface_object *surface = resolve(object_id);
    if (surface == NULL || pixels == NULL || pixel_count == NULL) return false;
    *pixels = pixel_arena + surface->offset_pixels;
    *pixel_count = (size_t)surface->width * surface->height;
    if (width != NULL) *width = surface->width;
    if (height != NULL) *height = surface->height;
    ++read_count;
    return true;
}

bool surface_mapping_data(uint32_t object_id, uintptr_t *physical_address,
                          size_t *page_count) {
    const struct surface_object *surface = resolve(object_id);
    if (surface == NULL || physical_address == NULL || page_count == NULL)
        return false;
    *physical_address = (uintptr_t)(pixel_arena + surface->offset_pixels);
    *page_count = (surface->capacity_pixels * sizeof(uint32_t) +
                   4095u) / 4096u;
    ++mapping_count;
    return true;
}

bool surface_damage(uint32_t object_id, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || width == 0u || height == 0u ||
        x >= surface->width || y >= surface->height ||
        width > surface->width - x || height > surface->height - y ||
        x > UINT16_MAX || y > UINT16_MAX || width > UINT16_MAX ||
        height > UINT16_MAX) return false;
    surface->damage_x = (uint16_t)x;
    surface->damage_y = (uint16_t)y;
    surface->damage_width = (uint16_t)width;
    surface->damage_height = (uint16_t)height;
    surface->damage_pending = true;
    ++damage_count;
    return true;
}

bool surface_take_damage(uint32_t object_id, uint64_t *packed_damage) {
    struct surface_object *surface = resolve(object_id);
    if (surface == NULL || packed_damage == NULL || !surface->damage_pending)
        return false;
    *packed_damage = (uint64_t)surface->damage_x |
        ((uint64_t)surface->damage_y << 16u) |
        ((uint64_t)surface->damage_width << 32u) |
        ((uint64_t)surface->damage_height << 48u);
    surface->damage_pending = false;
    ++damage_consumed_count;
    return true;
}

void surface_note_share(void) { ++share_count; }
size_t surface_count(void) {
    size_t count = 0u;
    for (size_t object = 0u; object < SURFACE_LIMIT; ++object)
        if (objects[object].active) ++count;
    return count;
}
uint64_t surface_writes(void) { return write_count; }
uint64_t surface_reads(void) { return read_count; }
uint64_t surface_shares(void) { return share_count; }
uint64_t surface_created(void) { return created_count; }
uint64_t surface_reclaimed(void) { return reclaimed_count; }
uint64_t surface_mappings(void) { return mapping_count; }
uint64_t surface_damages(void) { return damage_count; }
uint64_t surface_damage_consumed(void) { return damage_consumed_count; }

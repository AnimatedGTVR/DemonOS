#ifndef KERNEL_SURFACE_H
#define KERNEL_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SURFACE_LIMIT 8u
#define SURFACE_MAX_WIDTH 640u
#define SURFACE_MAX_HEIGHT 480u
/* The arena is suballocated per surface, sized for a realistic concurrent
   mix rather than the worst case of SURFACE_LIMIT surfaces all at the
   maximum size: one full-width panel (640x40) plus several ordinary app
   windows. Kept under FRAMEBUFFER_BACKBUFFER_MAX (kernel.c's
   allocate_contiguous shares that ceiling across every contiguous
   allocation it serves, not just the framebuffer backbuffer itself). */
#define SURFACE_ARENA_BYTES 1048576u

bool surfaces_init(uint32_t *arena, size_t arena_bytes);
bool surface_create(uint32_t owner_pid, uint32_t width, uint32_t height,
                    uint32_t *object_id);
bool surface_retain(uint32_t object_id);
bool surface_release(uint32_t object_id);
bool surface_discard(uint32_t object_id);
bool surface_write(uint32_t object_id, size_t pixel_offset,
                   const uint32_t *pixels, size_t pixel_count);
bool surface_data(uint32_t object_id, const uint32_t **pixels,
                  size_t *pixel_count, uint32_t *width, uint32_t *height);
bool surface_mapping_data(uint32_t object_id, uintptr_t *physical_address,
                          size_t *page_count);
bool surface_damage(uint32_t object_id, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height);
bool surface_take_damage(uint32_t object_id, uint64_t *packed_damage);
void surface_note_share(void);
size_t surface_count(void);
uint64_t surface_writes(void);
uint64_t surface_reads(void);
uint64_t surface_shares(void);
uint64_t surface_created(void);
uint64_t surface_reclaimed(void);
uint64_t surface_mappings(void);
uint64_t surface_damages(void);
uint64_t surface_damage_consumed(void);

#ifdef __cplusplus
}
#endif

#endif

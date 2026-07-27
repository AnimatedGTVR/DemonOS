#ifndef KERNEL_SURFACE_H
#define KERNEL_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SURFACE_LIMIT 4u
#define SURFACE_MAX_WIDTH 256u
#define SURFACE_MAX_HEIGHT 64u
/* The arena is suballocated per surface. Terminal and browser each use a
   256x64 surface, while two ordinary 64x64 apps bring the four-surface
   maximum to 160 KiB. */
#define SURFACE_ARENA_BYTES 163840u

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

#endif

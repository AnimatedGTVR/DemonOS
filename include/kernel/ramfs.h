#ifndef KERNEL_RAMFS_H
#define KERNEL_RAMFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ramfs_init(void);
bool ramfs_seed(const char *name, size_t name_length,
                const uint8_t *data, size_t length);
/* Register immutable boot-module storage without copying it into RAMFS. The
   caller must keep data alive for the lifetime of the filesystem. */
bool ramfs_seed_reference(const char *name, size_t name_length,
                          const uint8_t *data, size_t length);
bool ramfs_open(const char *name, size_t name_length, bool create,
                uint32_t *object_id);
bool ramfs_write(uint32_t object_id, const uint8_t *data, size_t length);
bool ramfs_delete(const char *name, size_t name_length);
bool ramfs_rename(const char *old_name, size_t old_length,
                  const char *new_name, size_t new_length);
bool ramfs_read(uint32_t object_id, uint8_t *data, size_t capacity,
                size_t *length);
bool ramfs_read_range(uint32_t object_id, size_t offset, uint8_t *data,
                      size_t capacity, size_t *length);
bool ramfs_size(uint32_t object_id, size_t *length);
bool ramfs_view(uint32_t object_id, const uint8_t **data, size_t *length);
bool ramfs_self_test(void);
uint64_t ramfs_file_count(void);
uint64_t ramfs_bytes_used(void);
uint64_t ramfs_reads(void);
uint64_t ramfs_writes(void);
uint64_t ramfs_seeded_files(void);
uint64_t ramfs_storage_capacity(void);
uint64_t ramfs_max_files(void);
bool ramfs_entry(size_t index, const char **name, size_t *name_length,
                 size_t *length);

// Directory listing: immediate children of `prefix` (or, for prefix ==
// "" / "/", the root). Callers iterate index = 0, 1, 2, ... until this
// returns false. `relative_name` is just the child's own segment (e.g.
// listing "/system" yields "mko", not "mko/sdk.mko"). `is_directory` is
// true when the child is itself a directory (more path below it) rather
// than a file, in which case `length` is always 0.
bool ramfs_list(const char *prefix, size_t prefix_length, size_t index,
                const char **relative_name, size_t *relative_length,
                size_t *length, bool *is_directory);

#endif

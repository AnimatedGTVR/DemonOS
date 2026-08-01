#ifndef DEMON_PORTKIT_H
#define DEMON_PORTKIT_H

/* Small, freestanding compatibility layer for native C ports.
   It deliberately sits above MAKO-ABI: engines use these stable helpers and
   keep syscall numbers, capability handles, and RAMFS quirks out of upstream
   source files.  The caller supplies the heap arena, so a port has a visible,
   bounded memory budget instead of silently overwriting another mapping. */

#include <demon/input.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEMON_PORT_INVALID_HANDLE UINT64_MAX

struct demon_port_file {
    size_t size;
    size_t position;
    uint64_t handle;
};

struct demon_port_runtime {
    uint64_t storage;
    uint64_t input;
    uint64_t heap_used;
    uint64_t heap_peak;
    uint64_t heap_capacity;
};

/* Must be called once before allocation or file access. */
bool demon_port_init(void *arena, size_t arena_size);
bool demon_port_init_dynamic(size_t arena_size);
void demon_port_shutdown(void);
const struct demon_port_runtime *demon_port_status(void);

void *demon_port_malloc(size_t size);
void *demon_port_calloc(size_t count, size_t size);
void *demon_port_realloc(void *pointer, size_t size);
void demon_port_free(void *pointer);

bool demon_port_open(struct demon_port_file *file, const char *path);
bool demon_port_create(struct demon_port_file *file, const char *path);
size_t demon_port_write_file(struct demon_port_file *file, const void *source,
                             size_t bytes);
bool demon_port_delete(const char *path);
bool demon_port_rename(const char *old_path, const char *new_path);
size_t demon_port_read(struct demon_port_file *file, void *destination, size_t bytes);
bool demon_port_seek(struct demon_port_file *file, size_t absolute_offset);
size_t demon_port_tell(const struct demon_port_file *file);
void demon_port_close(struct demon_port_file *file);

uint64_t demon_port_ticks_ms(void);
void demon_port_sleep_ms(uint32_t milliseconds);
bool demon_port_poll_input(struct input_event *event);
void demon_port_write(const char *text);
__attribute__((noreturn)) void demon_port_exit(uint64_t status);
void demon_port_fatal(const char *message, uint64_t status);

#endif

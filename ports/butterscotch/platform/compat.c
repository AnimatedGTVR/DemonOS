/* Adapt upstream BinaryReader's FILE fallback to a PortKit file. FILE remains
   opaque here: callers pass a demon_port_file pointer cast to FILE*. This lets
   large data.win packages use bounded, seekable reads instead of copying the
   complete package into DemonOS's anonymous-memory window. */

#include <demon/portkit.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "libc.h"

void vLogError(const char *format, va_list arguments) {
    char buffer[320];
    (void)vsnprintf(buffer, sizeof(buffer), format, arguments);
    demon_port_write("butterscotch: ");
    demon_port_write(buffer);
}

void logError(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vLogError(format, arguments);
    va_end(arguments);
}

_Noreturn void abort(void) {
    demon_port_fatal("upstream parser aborted", 127u);
    for (;;) {
        /* demon_port_fatal exits the process; retain a freestanding fallback. */
    }
}

size_t fread(void *destination, size_t size, size_t count, FILE *stream) {
    if (size == 0u || count > (size_t)-1 / size) return 0u;
    const size_t bytes = size * count;
    const size_t read = demon_port_read((struct demon_port_file *)stream,
                                        destination, bytes);
    return read / size;
}

long ftell(FILE *stream) {
    const size_t position = demon_port_tell((struct demon_port_file *)stream);
    return position > 0x7fffffffu ? -1l : (long)position;
}

int fseek(FILE *stream, long offset, int origin) {
    struct demon_port_file *file = (struct demon_port_file *)stream;
    int64_t base;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = (int64_t)file->position;
    else if (origin == SEEK_END) base = (int64_t)file->size;
    else return -1;
    const int64_t target = base + (int64_t)offset;
    if (target < 0 || (uint64_t)target > file->size) return -1;
    return demon_port_seek(file, (size_t)target) ? 0 : -1;
}

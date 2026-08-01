#include <demon/portkit.h>

#include "libc.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define DEMON_SEEK_SET 0
#define DEMON_SEEK_CUR 1
#define DEMON_SEEK_END 2

typedef struct demon_stdio_file {
    struct demon_port_file port;
    unsigned console;
    unsigned writable;
    unsigned char *buffer;
    size_t length;
    size_t capacity;
} FILE;

static FILE stdout_file = { .console = 1u };
static FILE stderr_file = { .console = 1u };
FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;
static int demon_errno;

static const char *canonical_path(const char *path) {
    while (path != 0 && path[0] == '.' && path[1] == '/') path += 2;
    return path;
}

int *__errno_location(void) { return &demon_errno; }

FILE *fopen(const char *path, const char *mode) {
    if (path == 0 || mode == 0 || (mode[0] != 'r' && mode[0] != 'w')) return 0;
    path = canonical_path(path);
    FILE *stream = malloc(sizeof(*stream));
    if (stream == 0) return 0;
    memset(stream, 0, sizeof(*stream));
    stream->console = 0u;
    stream->writable = mode[0] == 'w';
    const int opened = stream->writable ? demon_port_create(&stream->port, path) :
        demon_port_open(&stream->port, path);
    if (!opened) {
        if (mode[0] == 'w') {
            demon_port_write("DOOM_FILE_OPEN_WRITE_FAILED path=");
            demon_port_write(path);
            demon_port_write("\n");
        }
        free(stream);
        return 0;
    }
    if (stream->writable) {
        stream->capacity = 4096u;
        stream->buffer = malloc(stream->capacity);
        if (stream->buffer == 0) {
            demon_port_close(&stream->port); free(stream); return 0;
        }
    }
    return stream;
}

int fclose(FILE *stream) {
    if (stream == 0 || stream->console != 0u) return -1;
    int result = 0;
    if (stream->writable &&
        demon_port_write_file(&stream->port, stream->buffer, stream->length) !=
            stream->length) result = -1;
    else if (stream->writable) {
        char marker[64];
        snprintf(marker, sizeof(marker), "DOOM_FILE_WRITE_OK bytes=%u\n",
                 (unsigned)stream->length);
        demon_port_write(marker);
    }
    demon_port_close(&stream->port);
    free(stream->buffer);
    free(stream);
    return result;
}

size_t fread(void *destination, size_t size, size_t count, FILE *stream) {
    if (stream == 0 || destination == 0 || size == 0u ||
        count > SIZE_MAX / size || stream->console != 0u) return 0u;
    return demon_port_read(&stream->port, destination, size * count) / size;
}

size_t fwrite(const void *source, size_t size, size_t count, FILE *stream) {
    if (stream == 0 || source == 0 || size == 0u || count > SIZE_MAX / size)
        return 0u;
    if (stream->console != 0u) {
        /* Console writes are diagnostics, not persistent file writes. */
        const size_t bytes = size * count;
        const char *text = source;
        for (size_t offset = 0u; offset < bytes;) {
            char chunk[257];
            size_t length = bytes - offset;
            if (length > sizeof(chunk) - 1u) length = sizeof(chunk) - 1u;
            memcpy(chunk, text + offset, length);
            chunk[length] = '\0';
            demon_port_write(chunk);
            offset += length;
        }
        return count;
    }
    if (!stream->writable) return 0u;
    const size_t bytes = size * count;
    const size_t position = stream->port.position;
    if (bytes > SIZE_MAX - position) return 0u;
    const size_t needed = position + bytes;
    if (needed > stream->capacity) {
        size_t capacity = stream->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2u) { capacity = needed; break; }
            capacity *= 2u;
        }
        unsigned char *grown = realloc(stream->buffer, capacity);
        if (grown == 0) return 0u;
        stream->buffer = grown; stream->capacity = capacity;
    }
    memcpy(stream->buffer + position, source, bytes);
    stream->port.position = needed;
    if (needed > stream->length) stream->length = needed;
    return count;
}

int fseek(FILE *stream, long offset, int origin) {
    if (stream == 0 || stream->console != 0u) return -1;
    int64_t target;
    if (origin == DEMON_SEEK_SET) target = offset;
    else if (origin == DEMON_SEEK_CUR)
        target = (int64_t)demon_port_tell(&stream->port) + offset;
    else if (origin == DEMON_SEEK_END)
        target = (int64_t)stream->port.size + offset;
    else return -1;
    const size_t limit = stream->writable ? stream->length : stream->port.size;
    if (target < 0 || (uint64_t)target > limit) return -1;
    if (stream->writable) { stream->port.position = (size_t)target; return 0; }
    return demon_port_seek(&stream->port, (size_t)target) ? 0 : -1;
}

long ftell(FILE *stream) {
    if (stream == 0 || stream->console != 0u) return -1;
    return (long)demon_port_tell(&stream->port);
}

int fflush(FILE *stream) {
    if (stream == 0 || stream->console != 0u || !stream->writable) return 0;
    return demon_port_write_file(&stream->port, stream->buffer, stream->length) ==
        stream->length ? 0 : -1;
}

int vfprintf(FILE *stream, const char *format, va_list arguments) {
    char output[1024];
    const int length = vsnprintf(output, sizeof(output), format, arguments);
    if (length < 0) return length;
    if (stream == stdout || stream == stderr) {
        demon_port_write(output);
    } else if (stream != 0 && stream->writable) {
        size_t bytes = (size_t)length;
        if (bytes >= sizeof(output)) bytes = sizeof(output) - 1u;
        if (fwrite(output, 1u, bytes, stream) != bytes) return -1;
    }
    return length;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

int putchar(int character) {
    char output[2] = {(char)character, '\0'};
    demon_port_write(output);
    return (unsigned char)character;
}

int puts(const char *text) {
    demon_port_write(text);
    demon_port_write("\n");
    return 0;
}

/* RAMFS paths are flat names with slash-separated presentation, so creating a
   directory requires no separate inode. */
int mkdir(const char *path, ...) { return path == 0 ? -1 : 0; }
int remove(const char *path) {
    return demon_port_delete(canonical_path(path)) ? 0 : -1;
}
int rename(const char *old_path, const char *new_path) {
    return demon_port_rename(canonical_path(old_path), canonical_path(new_path)) ? 0 : -1;
}
int system(const char *command) { (void)command; return -1; }

void exit(int status) {
    demon_port_exit((uint64_t)(unsigned)status);
}

double fabs(double value) { return value < 0.0 ? -value : value; }

double atof(const char *text) {
    int negative = 0;
    double value = 0.0;
    double scale = 0.1;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    while (*text >= '0' && *text <= '9') value = value * 10.0 + (*text++ - '0');
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            value += (*text++ - '0') * scale;
            scale *= 0.1;
        }
    }
    return negative ? -value : value;
}

/* Upstream only uses sscanf for one integer conversion in this build. */
static int scan_integer(const char *text, const char *format, va_list arguments) {
    while (*format == ' ') ++format;
    while (*text == ' ' || *text == '\t') ++text;
    int base = 10;
    int is_unsigned = 0;
    if (format[0] != '%') return 0;
    if (format[1] == 'x') { base = 16; is_unsigned = 1; }
    else if (format[1] == 'o') { base = 8; is_unsigned = 1; }
    else if (format[1] != 'd' && format[1] != 'i') return 0;
    char *end = 0;
    const long value = strtol(text, &end, base == 10 && format[1] == 'i' ? 0 : base);
    if (end == text) return 0;
    if (is_unsigned) *va_arg(arguments, unsigned *) = (unsigned)value;
    else *va_arg(arguments, int *) = (int)value;
    return 1;
}

int sscanf(const char *text, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = scan_integer(text, format, arguments);
    va_end(arguments);
    return result;
}

int __isoc99_sscanf(const char *text, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = scan_integer(text, format, arguments);
    va_end(arguments);
    return result;
}

/* Host ctype.h compiled toupper through this glibc ABI. Recompiling with the
   owned headers will remove it; retain a complete table during transition. */
static int32_t ctype_upper_table[384];
static const int32_t *ctype_upper_pointer = ctype_upper_table + 128;
const int32_t **__ctype_toupper_loc(void) {
    for (int value = -128; value < 256; ++value)
        ctype_upper_table[value + 128] = toupper(value);
    return &ctype_upper_pointer;
}

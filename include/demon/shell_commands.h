#ifndef DEMON_SHELL_COMMANDS_H
#define DEMON_SHELL_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host-supplied I/O primitives so the same command bodies run both in the
   kernel (ring 0, MakoBox, direct RAMFS calls) and in userspace apps like
   xterm (ring 3, via the demon_* syscall wrappers) -- this is the single
   shared implementation of the portable, file/logic-oriented half of
   MakoBox's command set (the other half -- mem/ps/frames/caps/git/runit/
   apps/desktop/etc -- stays MakoBox-only since it needs direct kernel
   state with no userspace equivalent). */
struct shell_backend {
    void *context;
    void (*emit_line)(void *context, const char *text);
    /* Copies up to capacity bytes of `path` into `buffer`. *out_length is
       the file's real length (may exceed capacity, in which case the copy
       was truncated). Returns false if the path doesn't name a file. */
    bool (*read_file)(void *context, const char *path, size_t path_length,
                      uint8_t *buffer, size_t capacity, size_t *out_length);
    bool (*write_file)(void *context, const char *path, size_t path_length,
                       const uint8_t *data, size_t length);
    bool (*delete_file)(void *context, const char *path, size_t path_length);
    bool (*rename_file)(void *context, const char *old_path, size_t old_length,
                        const char *new_path, size_t new_length);
    /* Same semantics as ramfs_list/demon_dir_list: call with index = 0, 1,
       2, ... until it returns false. */
    bool (*list_dir)(void *context, const char *prefix, size_t prefix_length,
                     size_t index, char *name_out, size_t name_capacity,
                     size_t *name_length_out, size_t *size_out,
                     bool *is_directory_out);
};

/* Tries to run `command_line` as one of the shared commands. Returns true
   if the command name was recognized (regardless of whether it succeeded
   -- errors are reported through emit_line same as any other output),
   false if the command name isn't one this module handles at all. */
bool shell_dispatch(const struct shell_backend *backend, const char *command_line);

#endif

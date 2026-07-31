#ifndef DEMON_DIRENT_H
#define DEMON_DIRENT_H

#include <stdint.h>

/* Wire layout for syscall 39 (directory listing), shared between the
   kernel's dispatcher (src/arch/x86_64/userspace.c) and any userspace
   reader (see demon_dir_list in demon/c_app.h). Fixed size so it can be
   copied with userspace_copy_to without a separate length negotiation. */
struct demon_dir_entry {
    char name[60];
    uint32_t name_length;
    uint32_t is_directory;
    uint64_t size;
};

#endif

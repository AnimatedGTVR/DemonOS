#ifndef KERNEL_MAKOBOX_H
#define KERNEL_MAKOBOX_H

#include <stdbool.h>
#include <stdint.h>

struct makobox_state {
    uint64_t usable_memory_bytes;
    uint64_t frame_next;
    uint64_t frame_end;
    uint64_t paging_root;
    uint64_t kernel_size_bytes;
    bool idt_ready;
    bool native_mko_ready;
    bool app_runtime_ready;
};

void makobox_init(const struct makobox_state *state);
bool makobox_run(const char *command_line);
bool makobox_self_test(void);
__attribute__((noreturn)) void makobox_shell(void);

#endif

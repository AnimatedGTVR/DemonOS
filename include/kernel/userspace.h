#ifndef KERNEL_USERSPACE_H
#define KERNEL_USERSPACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/scheduler.h>

/* The virtual executable window is 144 KiB, ending at the fixed 0x324000
   heap. It was raised from 136 KiB / 0x322000 for the native session
   loading, credential gate, and lock screen; earlier stages used 120 KiB /
   0x31E000 and 96 KiB / 0x318000. Every
   hardcoded heap-scratch literal elsewhere (window_client.mko,
   window_move_client.mko, window_event_client.mko,
   window_crash_client.mko, terminal_client.mko, demonx_client.mko,
   init.mko, demonx_server.c, apps/tetris/main.c, and kernel.c's own
   boot-log line) had to move with it, since they all assume a fixed
   literal address rather than deriving it from this value. Physical frames
   are allocated per scheduler slot rather than at this maximum for every
   process: bootstrap tasks get 16 KiB, the persistent system slot gets the
   full window, and ordinary application slots get 48 KiB. */
#define USERSPACE_CODE_PAGES 36u
#define USERSPACE_HEAP_PAGES 5u
/* The dynamic window-table compositor's call chain (compositor_main ->
   render_scene -> draw_rect/draw_cursor/draw_glyph) needs more stack than
   the previous 12 KiB budget. render_scene alone was measured (via the
   native backend's emitted `sub $N, %rsp` prologue) at 7040 bytes for its
   own frame, and compositor_main's own frame (the while-true dispatch loop
   with its many opcode-branch locals) is a further, larger allocation that
   is still live on the stack under render_scene since the call is a normal
   (non-tail) call from inside that loop body. Tested up to 5 pages (20 KiB)
   without resolving a RIP=0 fault on render_scene's very first call, which
   argues against simple stack overflow as the cause -- see the fault
   investigation notes in user/compositor.mko. Left at a modest 5 pages
   (headroom over the old 12 KiB) rather than grown further, since growing
   it competes with the surface arena for the same fixed low-memory budget
   (see kernel.c's allocate_contiguous/surfaces_init) and did not fix the
   fault when tested. */
#define USERSPACE_STACK_PAGES 8u

struct userspace_memory {
    uint64_t address_space;
    uint64_t page_table;
    size_t code_page_count;
    uint64_t code_frames[USERSPACE_CODE_PAGES];
    uint64_t heap_frames[USERSPACE_HEAP_PAGES];
    uint64_t stack_frames[USERSPACE_STACK_PAGES];
};

bool userspace_init(const struct userspace_memory processes[SCHEDULER_PROCESS_LIMIT - 1u]);
uint32_t userspace_spawn_path(uint32_t parent_pid, const char *path, size_t path_length,
                              const char *name, uint32_t service_mask);
void userspace_release_process(uint32_t pid);
bool userspace_copy_to(uint32_t pid, uint64_t address, const uint8_t *data, size_t length);
bool userspace_surface_mapping_readonly(uint32_t pid, uint32_t surface_id);
bool userspace_unmap_surface(uint32_t pid, uint32_t surface_id);
bool userspace_run_init(void);
uint64_t userspace_exit_code(void);
uint64_t userspace_yield_count(void);
void userspace_set_kernel_stack(uintptr_t stack_top);
void userspace_set_boot_test_mode(bool test_mode);

#endif

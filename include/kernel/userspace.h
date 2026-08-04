#ifndef KERNEL_USERSPACE_H
#define KERNEL_USERSPACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/scheduler.h>

/* The virtual executable window is 192 KiB, ending at the fixed 0x330000
   heap (raised from 160 KiB / 0x328000 for EDDE's real-ported taskbar
   context menu, which the shared 160 KiB budget had no room left for --
   verified via the same full-tree numeric-range scan used for the Wave 9
   raise below, not ad hoc greps, since two same-day attempts at THAT
   raise both broke boot in different ways from an incomplete dependent
   list). Earlier stages: 144 KiB / 0x324000 (EDE port's ede-conf 4th
   launcher slot, Wave 9), 136 KiB / 0x322000 (native session loading/
   credential gate/lock screen), 120 KiB / 0x31E000, 96 KiB / 0x318000.

   Every one of the following hardcodes an absolute address derived from
   this heap base instead of deriving it live, and ALL of them must move by
   the same delta whenever this constant changes:
     +0      packet_address / isolation / incoming / EVENT_ADDRESS:
             window_client.mko, window_move_client.mko,
             window_event_client.mko, window_crash_client.mko,
             terminal_client.mko, demonx_client.mko, browser_client.mko,
             counter_client.mko (all: `packet_address: u64 = <heap>;`),
             init.mko (`isolation`), demonx_server.c (`incoming`),
             apps/tetris/main.c (`EVENT_ADDRESS`)
     +0x40   request_address (compositor.mko), outgoing (demonx_server.c)
     +0x80   pixel_address (compositor.mko), windows (demonx_server.c)
     +0x100  file_buffer (init.mko), BOARD (apps/tetris/main.c)
     +0x400  OUTPUT (apps/tetris/main.c)
     +0x4100 table_ptr() in compositor.mko (the live window table)
     +0x4380 EFFECT_REQUEST_BASE in compositor.mko (2 call sites + 2
             comments referencing the literal value)
     +0x43D0 spawn_path_ptr() in compositor.mko (2 call sites + 1 comment)
   kernel.c's own boot-log line ("heap=0x...") also hardcodes it, for
   humans reading the serial log, not for correctness.
   To verify this list is still complete before the next raise: grep the
   whole tree (not just these files) for both the hex prefix (0x32 followed
   by a byte in the current heap-to-heap+0x5000 range) and the decimal
   equivalent of every offset above -- a plain grep for the single old
   literal is not enough, since several of these are heap+non-zero-offset
   values that look unrelated at a glance. Physical frames are allocated
   per scheduler slot rather than at this maximum for every process:
   bootstrap tasks get 16 KiB, the persistent system slot gets the full
   window, and ordinary application slots get 48 KiB. */
#define USERSPACE_CODE_PAGES 48u
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
/* The Quake port's software renderer allocates its edge/surface scratch
   arrays on the C stack: R_EdgeDrawing (r_main.c) declares
   ledges[NUMSTACKEDGES + CACHE_SIZE/sizeof(edge_t) + 1] plus
   lsurfs[NUMSTACKSURFACES + ...] where NUMSTACKEDGES=2400 and
   NUMSTACKSURFACES=800, i.e. ~200 KiB of stack per R_EdgeDrawing frame.
   Measured during the D5 play boot (e1m1 first rendered frame) at ~270 KiB
   total stack use before the R_EdgeDrawing call push faulted at RSP-8 in
   unmapped memory. 512 KiB (128 pages) covers that with headroom for the
   callers (Host_Frame -> R_RenderScene -> R_EdgeDrawing) and for anything
   above the renderer in the frame loop. Raised globally rather than given
   only to Quake because the kernel spawn path uses one constant; the extra
   120 pages x 7 process slots (~3.5 MiB) is well within the 256 MiB QEMU
   budget alongside the seeded 18.7 MiB Quake pak. */
#define USERSPACE_STACK_PAGES 128u

struct userspace_memory {
    uint64_t address_space;
    uint64_t page_directory;
    uint64_t page_table;
    size_t code_page_count;
    uint64_t code_frames[USERSPACE_CODE_PAGES];
    uint64_t heap_frames[USERSPACE_HEAP_PAGES];
    uint64_t stack_frames[USERSPACE_STACK_PAGES];
};

bool userspace_init(const struct userspace_memory processes[SCHEDULER_PROCESS_LIMIT - 1u],
                    uintptr_t frame_allocator_state);
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
void userspace_set_boot_test_mode(uint64_t test_mode);
void userspace_set_demonwm_mode(bool enabled);

#endif

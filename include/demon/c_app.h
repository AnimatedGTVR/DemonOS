#ifndef DEMON_C_APP_H
#define DEMON_C_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <demon/dirent.h>

static inline uint64_t demon_syscall0(uint64_t number) {
    register uint64_t rax __asm__("rax") = number;
    __asm__ volatile("int $0x80" : "+a"(rax) : : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall1(uint64_t number, uint64_t first) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi) : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall2(uint64_t number, uint64_t first,
                                      uint64_t second) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    __asm__ volatile("int $0x80" : "+a"(rax) : "D"(rdi), "S"(rsi)
                     : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall3(uint64_t number, uint64_t first,
                                      uint64_t second, uint64_t third) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx) : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall4(uint64_t number, uint64_t first,
                                      uint64_t second, uint64_t third,
                                      uint64_t fourth) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    register uint64_t r10 __asm__("r10") = fourth;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10)
                     : "memory", "cc");
    return rax;
}

static inline uint64_t demon_syscall5(uint64_t number, uint64_t first,
                                      uint64_t second, uint64_t third,
                                      uint64_t fourth, uint64_t fifth) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = first;
    register uint64_t rsi __asm__("rsi") = second;
    register uint64_t rdx __asm__("rdx") = third;
    register uint64_t r10 __asm__("r10") = fourth;
    register uint64_t r8 __asm__("r8") = fifth;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
                     : "memory", "cc");
    return rax;
}

static inline uint64_t demon_write(const void *bytes, uint64_t length) {
    return demon_syscall2(0u, (uint64_t)(uintptr_t)bytes, length);
}

static inline uint64_t demon_yield(void) { return demon_syscall0(1u); }
static inline uint64_t demon_exit(uint64_t status) {
    return demon_syscall1(2u, status);
}
static inline uint64_t demon_ticks(void) { return demon_syscall0(3u); }
static inline uint64_t demon_service_open(uint64_t service) {
    return demon_syscall1(5u, service);
}
static inline uint64_t demon_handle_close(uint64_t handle) {
    return demon_syscall1(8u, handle);
}
static inline uint64_t demon_handle_query(uint64_t handle, uint64_t property) {
    return demon_syscall2(7u, handle, property);
}
static inline uint64_t demon_input_poll(uint64_t handle, void *event) {
    return demon_syscall2(20u, handle, (uint64_t)(uintptr_t)event);
}
static inline uint64_t demon_getpid(void) { return demon_syscall0(4u); }
static inline uint64_t demon_channel_create(const char *name, uint64_t length) {
    return demon_syscall2(14u, (uint64_t)(uintptr_t)name, length);
}
static inline uint64_t demon_channel_connect(const char *name, uint64_t length) {
    return demon_syscall2(15u, (uint64_t)(uintptr_t)name, length);
}
static inline uint64_t demon_channel_send(uint64_t channel, const void *message,
                                          uint64_t length) {
    return demon_syscall3(16u, channel, (uint64_t)(uintptr_t)message, length);
}
static inline uint64_t demon_channel_receive(uint64_t channel, void *message,
                                             uint64_t capacity, uint64_t nonblocking) {
    return demon_syscall4(17u, channel, (uint64_t)(uintptr_t)message,
                          capacity, nonblocking);
}
static inline uint64_t demon_surface_create(uint64_t factory, uint64_t width,
                                            uint64_t height) {
    return demon_syscall3(22u, factory, width, height);
}
static inline uint64_t demon_surface_write(uint64_t surface, const void *pixels,
                                           uint64_t count, uint64_t offset) {
    return demon_syscall4(23u, surface, (uint64_t)(uintptr_t)pixels, count, offset);
}
static inline uint64_t demon_surface_share(uint64_t surface, uint64_t channel) {
    return demon_syscall2(24u, surface, channel);
}
static inline uint64_t demon_surface_damage(uint64_t surface, uint64_t x,
                                            uint64_t y, uint64_t width,
                                            uint64_t height) {
    register uint64_t rax __asm__("rax") = 28u;
    register uint64_t rdi __asm__("rdi") = surface;
    register uint64_t rsi __asm__("rsi") = x;
    register uint64_t rdx __asm__("rdx") = y;
    register uint64_t r10 __asm__("r10") = width;
    register uint64_t r8 __asm__("r8") = height;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
                     : "memory", "cc");
    return rax;
}

/* Syscalls 6/9/10/11/12 already exist and are used by browser_client.mko
   (file_open/handle_read, service_open(4)==storage) and compositor.mko
   (spawn/wait) -- these are the missing C wrappers for the same ABI, added
   for Wave 5 of the EDE port (apps/ede_conf) so a plain C app can open a
   RAMFS file or launch another app the same way MKO apps already do. */
static inline uint64_t demon_handle_write(uint64_t handle, const void *bytes,
                                          uint64_t length) {
    return demon_syscall3(6u, handle, (uint64_t)(uintptr_t)bytes, length);
}
static inline uint64_t demon_file_open(uint64_t storage, const char *name,
                                       uint64_t length, uint64_t create) {
    return demon_syscall4(9u, storage, (uint64_t)(uintptr_t)name, length, create);
}
static inline uint64_t demon_handle_read(uint64_t handle, void *bytes,
                                         uint64_t capacity) {
    return demon_syscall3(10u, handle, (uint64_t)(uintptr_t)bytes, capacity);
}
static inline uint64_t demon_file_delete(uint64_t storage, const char *name,
                                         uint64_t length) {
    return demon_syscall3(43u, storage, (uint64_t)(uintptr_t)name, length);
}
static inline uint64_t demon_file_rename(uint64_t storage,
                                         const char *old_name, uint64_t old_length,
                                         const char *new_name, uint64_t new_length) {
    register uint64_t rax __asm__("rax") = 44u;
    register uint64_t rdi __asm__("rdi") = storage;
    register uint64_t rsi __asm__("rsi") = (uint64_t)(uintptr_t)old_name;
    register uint64_t rdx __asm__("rdx") = old_length;
    register uint64_t r10 __asm__("r10") = (uint64_t)(uintptr_t)new_name;
    register uint64_t r8 __asm__("r8") = new_length;
    __asm__ volatile("int $0x80" : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
                     : "memory", "cc");
    return rax;
}
/* Submit signed 16-bit, 44.1 kHz, interleaved stereo PCM frames. */
static inline uint64_t demon_audio_submit(uint64_t audio, const int16_t *samples,
                                          uint64_t frame_count) {
    return demon_syscall3(45u, audio, (uint64_t)(uintptr_t)samples, frame_count);
}
static inline uint64_t demon_spawn(const char *path, uint64_t length,
                                   uint64_t service_mask) {
    return demon_syscall3(11u, (uint64_t)(uintptr_t)path, length, service_mask);
}
static inline uint64_t demon_wait(uint64_t pid) { return demon_syscall1(12u, pid); }

/* Display/compositor-only syscalls (see user/sdk.mko's display_info,
   display_submit, etc. for the MKO-side equivalents this mirrors 1:1 --
   same syscall numbers, same argument order). Added for the C++ EDDE
   compositor prototype: a process holding the DISPLAY capability is what
   these gate on, not the language that compiled it (userspace_spawn_path
   grants capabilities by spawned path, see src/kernel.c), so this is a
   straight port of the wrapper layer, not new kernel behavior. */
static inline uint64_t demon_display_info(uint64_t display, void *destination) {
    return demon_syscall2(18u, display, (uint64_t)(uintptr_t)destination);
}
static inline uint64_t demon_display_submit(uint64_t display, const void *request) {
    return demon_syscall2(19u, display, (uint64_t)(uintptr_t)request);
}
static inline uint64_t demon_display_dimensions(uint64_t display) {
    return demon_syscall1(21u, display);
}
/* Blocks until the compositor control channel or unified input becomes
   ready. Returns 1 for a 64-byte window packet (written to
   message_destination), 2 for an input event (written to
   event_destination), or 3 for the timeout_ticks deadline. */
static inline uint64_t demon_compositor_wait(uint64_t channel, uint64_t input_handle,
                                             void *message_destination,
                                             void *event_destination,
                                             uint64_t timeout_ticks) {
    return demon_syscall5(26u, channel, input_handle,
                          (uint64_t)(uintptr_t)message_destination,
                          (uint64_t)(uintptr_t)event_destination, timeout_ticks);
}
/* Maps one retained surface read-only into the caller; the returned
   address is stable for the process lifetime. */
static inline uint64_t demon_surface_map(uint64_t surface) {
    return demon_syscall1(27u, surface);
}
static inline uint64_t demon_surface_take_damage(uint64_t surface) {
    return demon_syscall1(29u, surface);
}
static inline uint64_t demon_surface_unmap(uint64_t surface) {
    return demon_syscall1(30u, surface);
}
/* Publishes the live window count/focused id into kernel-side counters a
   boot test can assert on -- gated on the same DISPLAY write right
   display_submit needs. No window-manager policy runs in ring 0; the
   kernel just stores what it's told. */
static inline uint64_t demon_compositor_report(uint64_t display, uint64_t window_count,
                                               uint64_t focused_window_id) {
    return demon_syscall3(31u, display, window_count, focused_window_id);
}
/* Moves the compositor-owned scanout cursor overlay. icon follows enum
   graphics_cursor_icon: 0 is the default arrow, 1-13 select the
   contextual resize/hand/text/blocked/info pack. */
static inline uint64_t demon_display_cursor_move(uint64_t display, uint64_t x,
                                                  uint64_t y, uint64_t icon) {
    return demon_syscall4(32u, display, x, y, icon);
}
/* Draws directly onto the kernel-resident backbuffer via the kernel's own
   rounded-rect/border/gradient/shadow compositing, selected by the
   request's "kind" word -- see docs/application-abi.md for the 10-word
   request layout. */
static inline uint64_t demon_display_submit_effect(uint64_t display, const void *request) {
    return demon_syscall2(33u, display, (uint64_t)(uintptr_t)request);
}
static inline uint64_t demon_boot_test_mode(void) { return demon_syscall0(34u); }
static inline uint64_t demon_battery_present(void) { return demon_syscall0(37u); }

/* Lists the immediate children of `prefix` (see ramfs_list). Call with
   index = 0, 1, 2, ... until it returns 0. `storage` is a handle opened
   via demon_service_open(4). `out` must point at a valid, writable
   struct demon_dir_entry. Returns 1 on a filled entry, 0 past the last
   entry, or UINT64_MAX on error (bad handle/range). */
static inline uint64_t demon_dir_list(uint64_t storage, const char *prefix,
                                      uint64_t prefix_length, uint64_t index,
                                      void *out) {
    return demon_syscall5(39u, storage, (uint64_t)(uintptr_t)prefix,
                          prefix_length, index, (uint64_t)(uintptr_t)out);
}

/* One bounded anonymous arena per process. This is intentionally smaller
   than POSIX mmap: it gives freestanding ports a reclaimable large heap
   without reserving that RAM for every lightweight application. */
static inline void *demon_memory_map(uint64_t byte_count) {
    const uint64_t result = demon_syscall1(40u, byte_count);
    return result == UINT64_MAX ? (void *)0 : (void *)(uintptr_t)result;
}
static inline uint64_t demon_memory_unmap(void *address) {
    return demon_syscall1(41u, (uint64_t)(uintptr_t)address);
}
/* W1 of docs/wine-port.md: claims address space without committing physical
   frames for it -- demon_memory_map alone can't express that, it always
   commits everything it maps in one shot. Mutually exclusive with
   demon_memory_map for the same process (one anonymous region, reserved
   either all-at-once or incrementally, not both). */
static inline void *demon_memory_reserve(uint64_t byte_count) {
    const uint64_t result = demon_syscall1(46u, byte_count);
    return result == UINT64_MAX ? (void *)0 : (void *)(uintptr_t)result;
}
/* Extends the committed portion of an already-reserved region by
   byte_count more bytes, always starting exactly where the last commit (or
   the reservation itself) left off. Returns the address the newly
   committed bytes start at, or NULL on failure (reservation exhausted or
   out of physical frames). The caller must never read or write past its
   own last successful commit's end: there is no recoverable page fault to
   catch that mistake in this kernel yet (see the comment on
   anonymous_reserved_pages in src/arch/x86_64/userspace.c), so doing so
   halts the whole kernel, not just this process.

   `writable` controls only the PTE write bit -- every committed page stays
   executable regardless (this kernel's EFER has LME but not NXE yet, so a
   non-executable mapping isn't safely expressible; see
   anonymous_commit_pages's comment in src/arch/x86_64/userspace.c). */
static inline void *demon_memory_commit(uint64_t byte_count, bool writable) {
    const uint64_t result = demon_syscall2(47u, byte_count, writable ? 1u : 0u);
    return result == UINT64_MAX ? (void *)0 : (void *)(uintptr_t)result;
}
static inline uint64_t demon_handle_read_at(uint64_t handle, void *bytes,
                                            uint64_t capacity,
                                            uint64_t absolute_offset) {
    return demon_syscall4(42u, handle, (uint64_t)(uintptr_t)bytes, capacity,
                          absolute_offset);
}

#endif

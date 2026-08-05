#include <kernel/userspace.h>
#include <kernel/acpi.h>
#include <kernel/capability.h>
#include <kernel/elf64.h>
#include <kernel/display.h>
#include <kernel/framebuffer.h>
#include <demon/dirent.h>
#include <demon/input.h>
#include <kernel/interrupt_frame.h>
#include <kernel/interrupts.h>
#include <kernel/ipc.h>
#include <kernel/http.h>
#include <kernel/network.h>
#include <kernel/ramfs.h>
#include <kernel/ac97.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/surface.h>
#include <kernel/terminal.h>

#include <stddef.h>
#include <stdint.h>

#define USER_CODE 0x300000u
#define USER_CODE_SIZE (USERSPACE_CODE_PAGES * 4096u)
#define USER_HEAP (USER_CODE + USER_CODE_SIZE)
#define USER_HEAP_SIZE (USERSPACE_HEAP_PAGES * 4096u)
#define USER_STACK_PAGE (USER_HEAP + USER_HEAP_SIZE)
#define USER_STACK_SIZE (USERSPACE_STACK_PAGES * 4096u)
#define USER_ADDRESS_END (USER_STACK_PAGE + USER_STACK_SIZE)
#define USER_ANON_BASE 0x10000000u
#define USER_ANON_MAX_BYTES (24u * 1024u * 1024u)
#define USER_ANON_MAX_PAGES (USER_ANON_MAX_BYTES / 4096u)
#define USER_LARGE_CODE_BASE 0x20000000u
/* D30 (NXEngine port) pushed nxengine-core.elf's LOAD segment to within
   ~600 bytes of the previous 256-page (1MB) cap -- real save-select/
   title-flow logic (TB_SaveSelect real navigation + a real D27-style
   stage-transition-on-selection), no new engine translation units.
   D27/D28/D29 all flagged this cap as tightening with each new stage and
   this is the first one to actually need a bump. Raised by 4 pages
   (16KB) -- enough real headroom for another stage or two of similar
   size (D30 itself only added ~1.4KB), not a speculative 2x/10x jump.
   D32 (the real stage-boss system: stageboss.cpp + the nine boss AI
   files under ai/boss plus IrregularBBox/sym/AssignSprites) grew the
   memsize to 269 pages, over the 260-page cap, so raised to 320
   (1.25MB) -- real headroom for the remaining boss-adjacent work, still
   one contiguous 1.25MB large-code region per process. */
#define USER_LARGE_CODE_MAX_PAGES 320u
#define USER_SURFACE_MAP 0x380000u
/* 16 pages (64 KiB) matches the old 256x64 SURFACE_MAX_WIDTH/HEIGHT
   ceiling (see include/kernel/surface.h). Raising SURFACE_MAX_WIDTH/HEIGHT
   to 640x480 means a window wider than what 16 pages can hold (anything
   over 256x64, e.g. a full-width panel) still gets a real surface and a
   real compositor-side share, but map_surface_readonly's page_count check
   here rejects mapping it into this process's own address space, so the
   compositor falls back to a flat placeholder for it. Tried raising this
   to fit larger windows (24, 32 pages) and got back a corrupted frame
   both times -- something about this constant's relationship to the rest
   of the process's single low page table isn't What the surrounding
   comments assume, and needs real investigation, not another guess, before
   moving it again. Left at the known-safe value pending that follow-up. */
#define USER_SURFACE_MAP_PAGES 16u
#define USER_SURFACE_MAP_SIZE (USER_SURFACE_MAP_PAGES * 4096u)
/* Multiple simultaneous windows each need their own mapped client surface.
   Slots are laid out contiguously after USER_SURFACE_MAP, still inside the
   2 MiB region covered by the process's single low page table. */
#define USER_SURFACE_MAP_SLOTS 4u
#define USER_SURFACE_MAP_SLOT_STRIDE (USER_SURFACE_MAP_PAGES * 4096u)
#define USER_SURFACE_MAP_REGION_SIZE (USER_SURFACE_MAP_SLOTS * USER_SURFACE_MAP_SLOT_STRIDE)
#define USER_SURFACE_MAP_SLOT_BASE(slot) (USER_SURFACE_MAP + (slot) * USER_SURFACE_MAP_SLOT_STRIDE)

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

struct table_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern const uint8_t _binary_build_user_program_elf_start[];
extern const uint8_t _binary_build_user_program_elf_end[];
extern void user_resume_ring3(uintptr_t frame_address);

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss64 tss;
static volatile uint64_t last_exit_status;
static bool initialized;
static struct userspace_memory process_memory[SCHEDULER_PROCESS_LIMIT];
static bool process_memory_used[SCHEDULER_PROCESS_LIMIT];
static uint32_t mapped_surface[SCHEDULER_PROCESS_LIMIT][USER_SURFACE_MAP_SLOTS];
static uintptr_t kernel_address_space;
static uintptr_t userspace_frame_allocator;
static uint64_t anonymous_free_frames;
static size_t anonymous_page_count[SCHEDULER_PROCESS_LIMIT];
/* W1 of docs/wine-port.md: how many pages of the anonymous region are
   RESERVED (address space claimed) vs. anonymous_page_count's existing
   meaning of how many are actually COMMITTED (physical frame + present
   PTE). Reservation is pure bookkeeping -- no frame allocation, no page
   table entries -- so it costs nothing until committed. See
   anonymous_reserve/anonymous_commit below for why this is a deliberately
   smaller, safer feature than real demand paging: touching a reserved-but-
   uncommitted page is a genuine unmapped access, and this kernel's page
   fault handler is diagnostic-only (see interrupt_page_fault_handler,
   __attribute__((noreturn))) -- it cannot recover and resume the faulting
   instruction, so such an access halts the whole kernel, not just the
   offending process. Callers MUST NOT touch memory past their own last
   successful commit. */
static size_t anonymous_reserved_pages[SCHEDULER_PROCESS_LIMIT];
static uint64_t large_code_frames[SCHEDULER_PROCESS_LIMIT][USER_LARGE_CODE_MAX_PAGES];
static size_t large_code_page_count[SCHEDULER_PROCESS_LIMIT];
static uint64_t large_code_page_table[SCHEDULER_PROCESS_LIMIT];
extern uint64_t mako_frame_allocate(uintptr_t state_address);
/* Set once from kernel_main (see multiboot_test_mode in kernel.c) before any
   userspace task runs, and never changed afterward. Exposed read-only to
   every process via syscall 34 so the compositor can tell a scripted boot
   (make smoke/check, which needs a responsive window manager within a
   small fixed tick budget) apart from a real interactive boot (make run,
   where a human needs several real seconds to read the loading/login
   screens) -- see compositor.mko's boot_test_mode/deadline setup. */
static uint64_t boot_test_mode;
static bool demonwm_mode;
/* Display pixels originate in the caller's address space, while the physical
   framebuffer backbuffer is intentionally accessed through the kernel's
   identity map.  Never let framebuffer code dereference that low physical
   address with a user CR3 active: USER_CODE remaps part of the same virtual
   range.  One 640-pixel scanline keeps the syscall boundary fixed and
   small while supporting the framebuffer's maximum width. */
#define DISPLAY_BOUNCE_PIXELS 640u
static uint32_t display_bounce[DISPLAY_BOUNCE_PIXELS] __attribute__((aligned(16)));
#define STORAGE_BOUNCE_BYTES 4096u
static uint8_t storage_bounce[STORAGE_BOUNCE_BYTES] __attribute__((aligned(16)));

/* RAMFS reference-backed files point at physical Multiboot module memory.
   Low module addresses can share a virtual address with a process's code
   pages, so they must only be dereferenced with the kernel CR3 active. Copy
   through one bounded page, then restore the caller's CR3 before touching
   its destination. */
static bool copy_ramfs_to_user(uint32_t object_id, size_t file_offset,
                               uint8_t *destination, size_t capacity,
                               size_t *copied) {
    if (copied == NULL) return false;
    *copied = 0u;
    while (*copied < capacity) {
        if (*copied > (size_t)-1 - file_offset) return false;
        size_t chunk = capacity - *copied;
        if (chunk > STORAGE_BOUNCE_BYTES) chunk = STORAGE_BOUNCE_BYTES;
        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
        size_t received = 0u;
        const bool ok = ramfs_read_range(object_id, file_offset + *copied,
                                         storage_bounce, chunk, &received);
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        if (!ok) return false;
        for (size_t i = 0u; i < received; ++i)
            destination[*copied + i] = storage_bounce[i];
        *copied += received;
        if (received < chunk) break;
    }
    return true;
}
static void install_gdt_tss(void) {
    gdt[0] = 0u;
    gdt[1] = 0x00AF9A000000FFFFull;
    gdt[2] = 0x00AF92000000FFFFull;
    gdt[3] = 0x00AFF2000000FFFFull;
    gdt[4] = 0x00AFFA000000FFFFull;
    const uintptr_t base = (uintptr_t)&tss;
    const uint64_t limit = sizeof(tss) - 1u;
    gdt[5] = (limit & 0xFFFFu) | ((base & 0xFFFFFFu) << 16u) |
        (0x89ull << 40u) | (((limit >> 16u) & 0xFu) << 48u) |
        (((base >> 24u) & 0xFFu) << 56u);
    gdt[6] = base >> 32u;
    tss.io_map_base = sizeof(tss);
    const struct table_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uintptr_t)&gdt[0],
    };
    __asm__ volatile ("lgdt %0" : : "m"(pointer) : "memory");
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)0x28u) : "memory");
}

void userspace_set_kernel_stack(uintptr_t stack_top) {
    tss.rsp0 = stack_top;
}

void userspace_set_boot_test_mode(uint64_t test_mode) {
    boot_test_mode = test_mode;
}

void userspace_set_demonwm_mode(bool enabled) {
    demonwm_mode = enabled;
}

static bool any_surface_mapped(uint32_t pid) {
    for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot)
        if (mapped_surface[pid][slot] != 0u) return true;
    return false;
}

static uint64_t anonymous_frame_take(void) {
    uint64_t frame;
    if (anonymous_free_frames != 0u) {
        frame = anonymous_free_frames;
        anonymous_free_frames = *(uint64_t *)(uintptr_t)frame;
    } else {
        frame = mako_frame_allocate(userspace_frame_allocator);
    }
    if (frame == 0u) return 0u;
    uint64_t *words = (uint64_t *)(uintptr_t)frame;
    for (size_t i = 0u; i < 512u; ++i) words[i] = 0u;
    return frame;
}

static void anonymous_frame_release(uint64_t frame) {
    if (frame == 0u) return;
    *(uint64_t *)(uintptr_t)frame = anonymous_free_frames;
    anonymous_free_frames = frame;
}

static void large_code_release(uint32_t pid) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT) return;
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    uint64_t *directory = (uint64_t *)(uintptr_t)process_memory[pid].page_directory;
    const size_t directory_index = USER_LARGE_CODE_BASE >> 21u;
    directory[directory_index] = 0u;
    for (size_t page = 0u; page < large_code_page_count[pid]; ++page) {
        anonymous_frame_release(large_code_frames[pid][page]);
        large_code_frames[pid][page] = 0u;
    }
    large_code_page_count[pid] = 0u;
    anonymous_frame_release(large_code_page_table[pid]);
    large_code_page_table[pid] = 0u;
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
}

static bool large_code_load(uint32_t pid, const uint8_t *image, size_t image_size,
                            const struct elf64_layout *layout, uint64_t *entry) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || layout == NULL ||
        layout->virtual_base != USER_LARGE_CODE_BASE ||
        layout->virtual_end <= USER_LARGE_CODE_BASE) return false;
    const uint64_t byte_count = layout->virtual_end - USER_LARGE_CODE_BASE;
    if (byte_count > USER_LARGE_CODE_MAX_PAGES * 4096u) return false;
    const size_t pages = (size_t)((byte_count + 4095u) / 4096u);
    const uint64_t table = anonymous_frame_take();
    if (table == 0u) return false;
    large_code_page_table[pid] = table;
    uint64_t *page_table = (uint64_t *)(uintptr_t)table;
    for (size_t page = 0u; page < pages; ++page) {
        const uint64_t frame = anonymous_frame_take();
        if (frame == 0u) {
            large_code_page_count[pid] = page;
            large_code_release(pid);
            return false;
        }
        large_code_frames[pid][page] = frame;
        large_code_page_count[pid] = page + 1u;
        page_table[page] = frame + 7u;
    }
    if (!elf64_load_executable_pages(image, image_size, USER_LARGE_CODE_BASE,
            large_code_frames[pid], pages, entry)) {
        large_code_release(pid);
        return false;
    }
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    uint64_t *directory = (uint64_t *)(uintptr_t)process_memory[pid].page_directory;
    directory[USER_LARGE_CODE_BASE >> 21u] = table + 7u;
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
    return true;
}

/* One bounded anonymous arena per process is enough for a conventional libc
   heap and avoids pretending we already implement full POSIX mmap semantics. */
static void anonymous_release(uint32_t pid) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT) return;
    if (anonymous_page_count[pid] == 0u) {
        /* Nothing committed, so no page tables to tear down -- but a
           reserve-only (never committed) reservation still needs clearing,
           or a later reserve call for this pid would wrongly see one still
           outstanding. */
        anonymous_reserved_pages[pid] = 0u;
        return;
    }
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    uint64_t *directory = (uint64_t *)(uintptr_t)process_memory[pid].page_directory;
    const size_t table_count = (anonymous_page_count[pid] + 511u) / 512u;
    for (size_t table = 0u; table < table_count; ++table) {
        const size_t directory_index = (USER_ANON_BASE >> 21u) + table;
        const uint64_t entry = directory[directory_index];
        if ((entry & 1u) == 0u || (entry & 0x80u) != 0u) continue;
        uint64_t *page_table = (uint64_t *)(uintptr_t)(entry & ~0xFFFull);
        size_t pages = anonymous_page_count[pid] - table * 512u;
        if (pages > 512u) pages = 512u;
        for (size_t page = 0u; page < pages; ++page) {
            if ((page_table[page] & 1u) != 0u)
                anonymous_frame_release(page_table[page] & ~0xFFFull);
            page_table[page] = 0u;
        }
        directory[directory_index] = (uint64_t)directory_index * 0x200000u + 0x87u;
        anonymous_frame_release(entry & ~0xFFFull);
    }
    anonymous_page_count[pid] = 0u;
    anonymous_reserved_pages[pid] = 0u;
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
    else
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
}

/* Maps pages [start_page, end_page) of pid's anonymous region -- shared by
   anonymous_map (which always commits [0, pages)) and anonymous_commit
   (W1's incremental commit, which extends from whatever is already
   committed). Correct for any contiguous, monotonically-increasing,
   non-overlapping range: each 2MB chunk's page table is allocated exactly
   once, the first time the loop's per-page cursor lands on a multiple of
   512, and the loop always walks every intermediate page one at a time
   (never jumping), so that condition fires at the right index regardless
   of where a given call's range starts. Must be called with the target
   process's page tables active (kernel_address_space or pid's own CR3);
   callers are responsible for the CR3 switch around this.

   `writable` controls only the PTE write bit. There is no equivalent
   `executable` parameter: this kernel's EFER has LME (long mode) set but
   not NXE (`src/arch/x86_64/boot.S` only ORs in bit 8 of MSR 0xC0000080) --
   setting a PTE's NX bit (63) with NXE disabled is a reserved-bit
   violation, i.e. an immediate page fault, and this kernel's page fault
   handler can't recover from one (see anonymous_reserved_pages's comment).
   Every committed page stays executable regardless of `writable`, matching
   this kernel's existing global behavior everywhere else -- not a new gap
   introduced here, just not yet closed anywhere. */
static bool anonymous_commit_pages(uint32_t pid, size_t start_page, size_t end_page,
                                   bool writable) {
    uint64_t *directory = (uint64_t *)(uintptr_t)process_memory[pid].page_directory;
    const uint64_t page_flags = 5u | (writable ? 2u : 0u); /* present|user, +write */
    for (size_t page = start_page; page < end_page; ++page) {
        const size_t directory_index = (USER_ANON_BASE >> 21u) + page / 512u;
        uint64_t *page_table;
        if (page % 512u == 0u) {
            const uint64_t table_frame = anonymous_frame_take();
            if (table_frame == 0u) return false;
            directory[directory_index] = table_frame + 7u;
            page_table = (uint64_t *)(uintptr_t)table_frame;
        } else {
            page_table = (uint64_t *)(uintptr_t)(directory[directory_index] & ~0xFFFull);
        }
        const uint64_t frame = anonymous_frame_take();
        if (frame == 0u) {
            if (page % 512u == 0u) {
                anonymous_frame_release(directory[directory_index] & ~0xFFFull);
                directory[directory_index] =
                    (uint64_t)directory_index * 0x200000u + 0x87u;
            }
            return false;
        }
        page_table[page % 512u] = frame | page_flags;
        anonymous_page_count[pid] = page + 1u;
    }
    return true;
}

static uint64_t anonymous_map(uint32_t pid, uint64_t byte_count) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || byte_count == 0u ||
        byte_count > USER_ANON_MAX_BYTES || anonymous_page_count[pid] != 0u ||
        anonymous_reserved_pages[pid] != 0u ||
        byte_count > UINT64_MAX - 4095u) return UINT64_MAX;
    const size_t pages = (size_t)((byte_count + 4095u) / 4096u);
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    const bool ok = anonymous_commit_pages(pid, 0u, pages, true);
    if (!ok) anonymous_release(pid);
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
    if (!ok) return UINT64_MAX;
    return USER_ANON_BASE;
}

/* W1 of docs/wine-port.md: pure bookkeeping, no page table or frame work at
   all -- claims address space without paying for it, the actual
   VirtualAlloc(MEM_RESERVE) semantic anonymous_map alone can't express
   (it always commits everything it maps, in one shot, exactly once).
   Mutually exclusive with anonymous_map's all-at-once path (same
   process-lifetime single-region model, just two ways to fill it). */
static uint64_t anonymous_reserve(uint32_t pid, uint64_t byte_count) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || byte_count == 0u ||
        byte_count > USER_ANON_MAX_BYTES || anonymous_page_count[pid] != 0u ||
        anonymous_reserved_pages[pid] != 0u ||
        byte_count > UINT64_MAX - 4095u) return UINT64_MAX;
    anonymous_reserved_pages[pid] = (size_t)((byte_count + 4095u) / 4096u);
    return USER_ANON_BASE;
}

/* Extends the committed portion of an already-reserved region by
   byte_count more bytes, starting exactly where the last commit (or the
   reservation itself, on the first call) left off -- never sparse, never
   out of order. Returns the address the newly committed bytes start at,
   or UINT64_MAX if that would overrun the reservation or a physical frame
   is unavailable. The caller must never read or write past its own last
   successful commit's end (see anonymous_reserved_pages's comment on why:
   there is no recoverable page fault to catch that mistake).

   `writable` exists for W2's PE section loader (see docs/wine-port.md): a
   read-only section (e.g. ".rdata") can be committed non-writable instead
   of every commit defaulting to read-write. See
   anonymous_commit_pages's comment for why there is no equivalent
   `executable` control yet. */
static uint64_t anonymous_commit(uint32_t pid, uint64_t byte_count, bool writable) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || byte_count == 0u ||
        anonymous_reserved_pages[pid] == 0u ||
        byte_count > UINT64_MAX - 4095u) return UINT64_MAX;
    const size_t new_pages = (size_t)((byte_count + 4095u) / 4096u);
    const size_t start_page = anonymous_page_count[pid];
    if (new_pages > anonymous_reserved_pages[pid] - start_page) return UINT64_MAX;
    const uint64_t start_address = USER_ANON_BASE + (uint64_t)start_page * 4096u;
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    const bool ok = anonymous_commit_pages(pid, start_page, start_page + new_pages, writable);
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
    return ok ? start_address : UINT64_MAX;
}

static bool anonymous_range(uint32_t pid, uint64_t address, uint64_t length) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT ||
        anonymous_page_count[pid] == 0u || address < USER_ANON_BASE) return false;
    const uint64_t end = USER_ANON_BASE + anonymous_page_count[pid] * 4096u;
    return address <= end && length <= end - address;
}

static bool user_range(uint64_t address, uint64_t length) {
    const uint32_t pid = scheduler_current_pid();
    if (anonymous_range(pid, address, length)) return true;
    if (address >= USER_HEAP && address <= USER_ADDRESS_END &&
        length <= USER_ADDRESS_END - address) return true;
    if (pid != 0u && pid < SCHEDULER_PROCESS_LIMIT && process_memory_used[pid] &&
        address >= USER_CODE &&
        address <= USER_CODE + process_memory[pid].code_page_count * 4096u &&
        length <= USER_CODE + process_memory[pid].code_page_count * 4096u - address)
        return true;
    if (pid != 0u && pid < SCHEDULER_PROCESS_LIMIT &&
        large_code_page_count[pid] != 0u && address >= USER_LARGE_CODE_BASE &&
        address <= USER_LARGE_CODE_BASE + large_code_page_count[pid] * 4096u &&
        length <= USER_LARGE_CODE_BASE + large_code_page_count[pid] * 4096u - address)
        return true;
    /* NOTE: this only confirms address/length fall somewhere inside the
       aggregate 4-slot surface-map region, not that they stay within the
       one slot the caller actually mapped. A per-slot version of this
       check was tried and reproducibly broke DEMON_WEB_NAVIGATION_OK in
       the boot-test (surface_damages() stopped advancing) -- root cause
       not yet found, likely related to USER_SURFACE_MAP_SLOTS (4) being
       easy to exceed once several real windows (terminal, browser, demonx
       panel, ...) are mapped at once and slots getting reused. Needs a
       real investigation of slot lifecycle/reuse before tightening this
       again, not another guess. */
    return pid != 0u && pid < SCHEDULER_PROCESS_LIMIT &&
        any_surface_mapped(pid) && address >= USER_SURFACE_MAP &&
        address <= USER_SURFACE_MAP + USER_SURFACE_MAP_REGION_SIZE &&
        length <= USER_SURFACE_MAP + USER_SURFACE_MAP_REGION_SIZE - address;
}

// Destinations the kernel is allowed to write into on a user process's
// behalf: the heap and the stack, plus the code pages. The code pages
// used to be excluded here on the assumption they're mapped read/execute
// only, but every port's PTEs are actually built with the writable bit
// set (load_process ORs in 0x7 = Present|Writable|User, matching each of
// these freestanding ELFs' own RWX LOAD segment -- ld even warns "has a
// LOAD segment with RWX permissions" building them), so a kernel write
// there doesn't desync from the real page permissions after all. Found
// via NXEngine's settings.dat round trip: fread()'s underlying
// demon_port_read syscall silently returned 0 bytes whenever its
// destination was a BSS global inside the large-code region (normal_settings,
// living past USER_LARGE_CODE_BASE once nxengine-core.elf grew past the
// normal code budget) -- the exact same class of bug already noted (and
// tolerated, there, since the caller ignored the failure) on
// demon_display_info's out-param write for large-app processes; this is
// the first case where it broke real correctness instead of just being
// silently absorbed. Mirrors user_range's own USER_CODE/
// USER_LARGE_CODE_BASE checks immediately above.
static bool user_write_range(uint64_t address, uint64_t length) {
    const uint32_t pid = scheduler_current_pid();
    if (anonymous_range(pid, address, length)) return true;
    if (address >= USER_HEAP && address <= USER_ADDRESS_END &&
        length <= USER_ADDRESS_END - address) return true;
    if (pid != 0u && pid < SCHEDULER_PROCESS_LIMIT && process_memory_used[pid] &&
        address >= USER_CODE &&
        address <= USER_CODE + process_memory[pid].code_page_count * 4096u &&
        length <= USER_CODE + process_memory[pid].code_page_count * 4096u - address)
        return true;
    if (pid != 0u && pid < SCHEDULER_PROCESS_LIMIT &&
        large_code_page_count[pid] != 0u && address >= USER_LARGE_CODE_BASE &&
        address <= USER_LARGE_CODE_BASE + large_code_page_count[pid] * 4096u &&
        length <= USER_LARGE_CODE_BASE + large_code_page_count[pid] * 4096u - address)
        return true;
    return false;
}

bool userspace_copy_to(uint32_t pid, uint64_t address, const uint8_t *data, size_t length) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || !process_memory_used[pid] ||
        data == NULL || !user_write_range(address, length)) return false;
    /* Physical frames live below 4 MiB, but each process remaps part of that
       low range for its private code and heap. Dereferencing a frame number
       while the sender's CR3 is active can therefore write into the sender's
       alias instead of the destination frame. Copy in small kernel-stack
       chunks, temporarily activate the destination address space, and write
       through its validated user virtual mapping. Interrupt gates keep this
       CR3 transition non-preemptible. */
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    const uint64_t target_cr3 = process_memory[pid].address_space;
    uint8_t chunk[64];
    size_t copied = 0u;
    while (copied < length) {
        size_t amount = length - copied;
        if (amount > sizeof(chunk)) amount = sizeof(chunk);
        for (size_t byte = 0u; byte < amount; ++byte)
            chunk[byte] = data[copied + byte];
        if (target_cr3 != original_cr3)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(target_cr3) : "memory");
        uint8_t *destination = (uint8_t *)(uintptr_t)(address + copied);
        for (size_t byte = 0u; byte < amount; ++byte)
            destination[byte] = chunk[byte];
        if (target_cr3 != original_cr3)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
        copied += amount;
    }
    return true;
}

static void edit_surface_ptes(uint32_t pid, size_t slot, uintptr_t physical_address,
                               size_t page_count, bool present) {
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    uint64_t *pt1 = (uint64_t *)(uintptr_t)process_memory[pid].page_table;
    const size_t first = (USER_SURFACE_MAP_SLOT_BASE(slot) - 0x200000u) / 4096u;
    for (size_t page = 0u; page < USER_SURFACE_MAP_PAGES; ++page)
        pt1[first + page] = present && page < page_count ?
            physical_address + page * 4096u + 5u : 0u;
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
}

static bool map_surface_readonly(uint32_t pid, uint32_t object_id,
                                 uint64_t *user_address) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || user_address == NULL ||
        !process_memory_used[pid]) return false;
    int free_slot = -1;
    for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot) {
        if (mapped_surface[pid][slot] == object_id) {
            *user_address = USER_SURFACE_MAP_SLOT_BASE(slot);
            return true;
        }
        if (free_slot < 0 && mapped_surface[pid][slot] == 0u) free_slot = (int)slot;
    }
    if (free_slot < 0) return false;
    uintptr_t physical_address;
    size_t page_count;
    if (!surface_mapping_data(object_id, &physical_address, &page_count) ||
        page_count == 0u || page_count > USER_SURFACE_MAP_PAGES ||
        (physical_address & 4095u) != 0u ||
        !surface_retain(object_id)) return false;
    edit_surface_ptes(pid, (size_t)free_slot, physical_address, page_count, true);
    mapped_surface[pid][free_slot] = object_id;
    *user_address = USER_SURFACE_MAP_SLOT_BASE(free_slot);
    return true;
}

static bool unmap_surface_id(uint32_t pid, uint32_t object_id) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || object_id == 0u) return false;
    for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot) {
        if (mapped_surface[pid][slot] != object_id) continue;
        edit_surface_ptes(pid, slot, 0u, 0u, false);
        mapped_surface[pid][slot] = 0u;
        (void)surface_release(object_id);
        return true;
    }
    return false;
}

bool userspace_unmap_surface(uint32_t pid, uint32_t surface_id) {
    return unmap_surface_id(pid, surface_id);
}

static void unmap_surface(uint32_t pid) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT) return;
    for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot) {
        const uint32_t object_id = mapped_surface[pid][slot];
        if (object_id == 0u) continue;
        edit_surface_ptes(pid, slot, 0u, 0u, false);
        mapped_surface[pid][slot] = 0u;
        (void)surface_release(object_id);
    }
}

bool userspace_surface_mapping_readonly(uint32_t pid, uint32_t surface_id) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || surface_id == 0u) return false;
    int found_slot = -1;
    for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot)
        if (mapped_surface[pid][slot] == surface_id) { found_slot = (int)slot; break; }
    if (found_slot < 0) return false;
    uintptr_t expected_physical;
    size_t expected_pages;
    if (!surface_mapping_data(surface_id, &expected_physical, &expected_pages) ||
        expected_pages == 0u || expected_pages > USER_SURFACE_MAP_PAGES)
        return false;
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
    const uint64_t *pt1 = (const uint64_t *)(uintptr_t)process_memory[pid].page_table;
    const size_t first = (USER_SURFACE_MAP_SLOT_BASE((size_t)found_slot) - 0x200000u) / 4096u;
    bool valid = expected_physical != 0u;
    for (size_t page = 0u; page < USER_SURFACE_MAP_PAGES; ++page) {
        const uint64_t entry = pt1[first + page];
        if (page < expected_pages)
            valid = valid && (entry & 7u) == 5u &&
                (entry & ~4095ull) == expected_physical + page * 4096u;
        else valid = valid && entry == 0u;
    }
    if (original_cr3 != kernel_address_space)
        __asm__ volatile ("mov %0, %%cr3" : : "r"(original_cr3) : "memory");
    return valid;
}

/* Set (in the display_submit syscall handler below) to the pid of
   whichever process most recently submitted a real frame to the
   display, and cleared on that process's exit. While set, that
   process's own demon_port_write output (used pervasively for real
   engine status/error logging, e.g. NXEngine's stat()/staterr()) is
   still sent to the serial log but no longer mirrored into the
   kernel's own graphical text terminal -- otherwise every such log
   line's trailing '\n' triggers terminal_write's newline() ->
   terminal_graphical_refresh(), which does a full framebuffer_clear()
   plus an 80x25 redraw at the terminal's own much larger 7x14 glyph
   cells, stomping over whatever the app had just drawn at its own
   resolution/font size on the very same shared framebuffer. Found by
   reading this file after NXEngine's D37 freeplay mode kept showing
   stale/garbled oversized text fragments over its own real, correctly
   rendered graphics no matter how the app-side drawing was fixed. */
static uint32_t display_owner_pid;

static uint64_t write_user_text(uint64_t address, uint64_t length) {
    if (!user_range(address, length)) return UINT64_MAX;
    const char *text = (const char *)(uintptr_t)address;
    const bool suppress_terminal_mirror =
        display_owner_pid != 0u && display_owner_pid == scheduler_current_pid();
    for (uint64_t i = 0; i < length; ++i) {
        char output[2] = { text[i], '\0' };
        serial_write(output);
        if (!suppress_terminal_mirror) terminal_write(output);
    }
    return length;
}

static bool display_submit_from_user(const struct display_user_submit *request) {
    struct display_user_info info;
    if (request == NULL || !display_describe(&info) || request->width == 0u ||
        request->height == 0u || request->width > info.width ||
        request->width > DISPLAY_BOUNCE_PIXELS || request->x > INT32_MAX ||
        request->y > INT32_MAX || request->height > UINT32_MAX ||
        request->height > (uint64_t)INT32_MAX + 1u - request->y ||
        (request->flags & ~DISPLAY_SUBMIT_PRESENT) != 0u)
        return false;

    const uint64_t rows_per_chunk = DISPLAY_BOUNCE_PIXELS / request->width;
    uint64_t source_row = 0u;
    while (source_row < request->height) {
        uint64_t rows = request->height - source_row;
        if (rows > rows_per_chunk) rows = rows_per_chunk;
        const uint64_t pixel_count = request->width * rows;
        const uint32_t *source = (const uint32_t *)(uintptr_t)
            (request->pixels + source_row * request->width * sizeof(uint32_t));
        for (uint64_t pixel = 0u; pixel < pixel_count; ++pixel)
            display_bounce[pixel] = source[pixel];

        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");

        const bool final_chunk = source_row + rows == request->height;
        const struct display_user_submit chunk = {
            .x = request->x,
            .y = request->y + source_row,
            .width = request->width,
            .height = rows,
            .pixels = (uintptr_t)&display_bounce[0],
            .flags = final_chunk ? request->flags : 0u,
        };
        const bool submitted = display_submit(&chunk);

        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        if (!submitted) return false;
        source_row += rows;
    }
    return true;
}

uintptr_t syscall_dispatch(uintptr_t frame_address) {
    struct interrupt_frame *frame = (struct interrupt_frame *)frame_address;
    const uint64_t number = frame->rax;
    if (number == 0u) {
        frame->rax = write_user_text(frame->rdi, frame->rsi);
        return frame_address;
    }
    if (number == 1u) {
        frame->rax = 0u;
        return scheduler_on_yield(frame_address);
    }
    if (number == 2u) {
        last_exit_status = frame->rdi;
        if (display_owner_pid == scheduler_current_pid()) display_owner_pid = 0u;
        ipc_process_cleanup(scheduler_current_pid());
        capabilities_close_all(scheduler_current_pid());
        return scheduler_on_exit(frame_address, frame->rdi);
    }
    if (number == 3u) {
        frame->rax = interrupts_timer_ticks();
        return frame_address;
    }
    if (number == 4u) {
        frame->rax = scheduler_current_pid();
        return frame_address;
    }
    if (number == 5u) {
        frame->rax = capability_open(scheduler_current_pid(), (uint32_t)frame->rdi);
        return frame_address;
    }
    if (number == 6u) {
        enum capability_service service;
        uint32_t object_id;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                                       CAPABILITY_RIGHT_WRITE, &service,
                                       &object_id)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        if (service == CAPABILITY_SERVICE_CONSOLE) {
            frame->rax = write_user_text(frame->rsi, frame->rdx);
        } else if (service == CAPABILITY_SERVICE_FILE &&
                   user_range(frame->rsi, frame->rdx) &&
                   ramfs_write(object_id, (const uint8_t *)(uintptr_t)frame->rsi,
                               (size_t)frame->rdx)) {
            frame->rax = frame->rdx;
        } else {
            frame->rax = UINT64_MAX;
        }
        return frame_address;
    }
    if (number == 7u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                                CAPABILITY_RIGHT_QUERY, &service)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        if (service == CAPABILITY_SERVICE_CLOCK)
            frame->rax = frame->rsi == 0u ? interrupts_timer_ticks() : UINT64_MAX;
        else if (service == CAPABILITY_SERVICE_PROCESS)
            frame->rax = frame->rsi == 0u ? scheduler_current_pid() : UINT64_MAX;
        else if (service == CAPABILITY_SERVICE_FILE) {
            uint32_t object_id;
            size_t length;
            if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                                           CAPABILITY_RIGHT_QUERY, &service,
                                           &object_id) ||
                frame->rsi != 0u || !ramfs_size(object_id, &length))
                frame->rax = UINT64_MAX;
            else
                frame->rax = length;
        }
        else if (service == CAPABILITY_SERVICE_DISPLAY) {
            struct display_user_info info;
            if (!display_describe(&info)) frame->rax = UINT64_MAX;
            else if (frame->rsi == 0u) frame->rax = info.width;
            else if (frame->rsi == 1u) frame->rax = info.height;
            else if (frame->rsi == 2u) frame->rax = info.stride_pixels;
            else if (frame->rsi == 3u) frame->rax = info.format;
            else if (frame->rsi == 4u) frame->rax = info.max_transfer_pixels;
            else if (frame->rsi == 5u) frame->rax = info.width | (info.height << 32u);
            else frame->rax = UINT64_MAX;
        } else
            frame->rax = UINT64_MAX;
        return frame_address;
    }
    if (number == 8u) {
        frame->rax = capability_close(scheduler_current_pid(), frame->rdi) ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 9u) {
        enum capability_service service;
        uint32_t object_id;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                                CAPABILITY_RIGHT_OPEN, &service) ||
            service != CAPABILITY_SERVICE_STORAGE ||
            !user_range(frame->rsi, frame->rdx) ||
            !ramfs_open((const char *)(uintptr_t)frame->rsi, (size_t)frame->rdx,
                        frame->r10 != 0u, &object_id)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        frame->rax = capability_open_file(scheduler_current_pid(), object_id);
        return frame_address;
    }
    if (number == 10u) {
        enum capability_service service;
        uint32_t object_id;
        size_t length;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                                       CAPABILITY_RIGHT_READ, &service,
                                       &object_id) ||
            service != CAPABILITY_SERVICE_FILE ||
            !user_write_range(frame->rsi, frame->rdx) ||
            !copy_ramfs_to_user(object_id, 0u,
                                (uint8_t *)(uintptr_t)frame->rsi,
                                (size_t)frame->rdx, &length)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        frame->rax = length;
        return frame_address;
    }
    if (number == 42u) {
        enum capability_service service;
        uint32_t object_id;
        size_t length;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                                       CAPABILITY_RIGHT_READ, &service,
                                       &object_id) ||
            service != CAPABILITY_SERVICE_FILE ||
            frame->r10 > (uint64_t)(size_t)-1 ||
            !user_write_range(frame->rsi, frame->rdx) ||
            !copy_ramfs_to_user(object_id, (size_t)frame->r10,
                                (uint8_t *)(uintptr_t)frame->rsi,
                                (size_t)frame->rdx, &length)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        frame->rax = length;
        return frame_address;
    }
    if (number == 43u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_OPEN, &service) ||
            service != CAPABILITY_SERVICE_STORAGE ||
            !user_range(frame->rsi, frame->rdx)) frame->rax = UINT64_MAX;
        else frame->rax = ramfs_delete((const char *)(uintptr_t)frame->rsi,
            (size_t)frame->rdx) ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 44u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_OPEN, &service) ||
            service != CAPABILITY_SERVICE_STORAGE ||
            !user_range(frame->rsi, frame->rdx) ||
            !user_range(frame->r10, frame->r8)) frame->rax = UINT64_MAX;
        else frame->rax = ramfs_rename((const char *)(uintptr_t)frame->rsi,
            (size_t)frame->rdx, (const char *)(uintptr_t)frame->r10,
            (size_t)frame->r8) ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 45u) {
        enum capability_service service;
        const uint64_t frames = frame->rdx;
        const uint64_t sample_count = frames * 2u;
        const uint64_t bytes = sample_count * sizeof(int16_t);
        if (frames == 0u || frames > AC97_MAX_FRAMES ||
            sample_count / 2u != frames || bytes / sizeof(int16_t) != sample_count ||
            !capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service) ||
            service != CAPABILITY_SERVICE_AUDIO ||
            !user_range(frame->rsi, bytes)) frame->rax = UINT64_MAX;
        else frame->rax = ac97_submit((const int16_t *)(uintptr_t)frame->rsi,
                                      (size_t)frames) ? frames : 0u;
        return frame_address;
    }
    if (number == 11u) {
        if (!user_range(frame->rdi, frame->rsi) || frame->rsi == 0u || frame->rsi > 127u) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        frame->rax = userspace_spawn_path(scheduler_current_pid(),
            (const char *)(uintptr_t)frame->rdi, (size_t)frame->rsi, "spawned", (uint32_t)frame->rdx);
        if (frame->rax == 0u) frame->rax = UINT64_MAX;
        return frame_address;
    }
    if (number == 12u) return scheduler_on_wait(frame_address, (uint32_t)frame->rdi);
    if (number == 13u) {
        uint32_t target = (uint32_t)frame->rdi;
        ipc_process_cleanup(target); capabilities_close_all(target);
        frame->rax = scheduler_terminate(target, frame->rsi) ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 14u || number == 15u) {
        if (!user_range(frame->rdi, frame->rsi) || frame->rsi == 0u || frame->rsi >= IPC_NAME_MAX) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        frame->rax = number == 14u ? ipc_create(scheduler_current_pid(),
            (const char *)(uintptr_t)frame->rdi, (size_t)frame->rsi) :
            ipc_connect(scheduler_current_pid(), (const char *)(uintptr_t)frame->rdi, (size_t)frame->rsi);
        return frame_address;
    }
    if (number == 16u) {
        frame->rax = user_range(frame->rsi, frame->rdx) &&
            ipc_send(scheduler_current_pid(), frame->rdi,
                     (const uint8_t *)(uintptr_t)frame->rsi, (size_t)frame->rdx) ? frame->rdx : UINT64_MAX;
        return frame_address;
    }
    if (number == 17u) {
        if (!user_write_range(frame->rsi, frame->rdx) || frame->rdx == 0u || frame->rdx > IPC_MESSAGE_MAX) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        uint8_t message[IPC_MESSAGE_MAX]; size_t length; uint32_t sender;
        if (ipc_receive(scheduler_current_pid(), frame->rdi, message, (size_t)frame->rdx, &length, &sender)) {
            if (!userspace_copy_to(scheduler_current_pid(), frame->rsi, message, length)) frame->rax=UINT64_MAX;
            else { frame->rax=length; frame->rdx=sender; }
            return frame_address;
        }
        if (frame->r10 != 0u || !ipc_wait(scheduler_current_pid(), frame->rdi, frame->rsi, (size_t)frame->rdx)) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        return scheduler_block_current(frame_address);
    }
    if (number == 18u) {
        enum capability_service service = 0;
        struct display_user_info info;
        bool ok = capability_resolve(scheduler_current_pid(), frame->rdi,
            CAPABILITY_RIGHT_QUERY, &service);
        if (!ok) serial_write("display: capability denied\n");
        if (ok) { ok = service == CAPABILITY_SERVICE_DISPLAY; if (!ok) serial_write("display: wrong service\n"); }
        if (ok) { ok = user_write_range(frame->rsi, sizeof(info)); if (!ok) serial_write("display: invalid destination\n"); }
        if (ok) { ok = display_describe(&info); if (!ok) serial_write("display: unavailable\n"); }
        if (ok) { ok = userspace_copy_to(scheduler_current_pid(), frame->rsi,
            (const uint8_t *)&info, sizeof(info)); if (!ok) serial_write("display: copy failed\n"); }
        frame->rax = ok ? info.width | (info.height << 32u) : UINT64_MAX;
        return frame_address;
    }
    if (number == 19u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service) ||
            service != CAPABILITY_SERVICE_DISPLAY ||
            !user_range(frame->rsi, sizeof(struct display_user_submit))) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        const struct display_user_submit request =
            *(const struct display_user_submit *)(uintptr_t)frame->rsi;
        const uint64_t count = request.width * request.height;
        if (request.height != 0u && count / request.height != request.width) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        const uint64_t bytes = count * sizeof(uint32_t);
        /* The public limit describes one kernel-side transfer chunk, not the
           largest frame a client may submit. display_submit_from_user()
           deliberately walks larger frames a few rows at a time through the
           bounded bounce buffer. Reject only arithmetic overflow or an
           invalid complete source range here. */
        if (bytes / sizeof(uint32_t) != count ||
            !user_range(request.pixels, bytes)) frame->rax = UINT64_MAX;
        else {
            const bool submitted = display_submit_from_user(&request);
            if (submitted) display_owner_pid = scheduler_current_pid();
            frame->rax = submitted ? 0u : UINT64_MAX;
        }
        return frame_address;
    }
    if (number == 20u) {
        enum capability_service service; struct input_event event;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service) ||
            service != CAPABILITY_SERVICE_INPUT ||
            !user_write_range(frame->rsi, sizeof(event))) frame->rax = UINT64_MAX;
        else if (!input_poll(&event)) frame->rax = 1u;
        else frame->rax = userspace_copy_to(scheduler_current_pid(), frame->rsi,
            (const uint8_t *)&event, sizeof(event)) ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 21u) {
        enum capability_service service;
        struct display_user_info info;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_QUERY, &service) ||
            service != CAPABILITY_SERVICE_DISPLAY || !display_describe(&info))
            frame->rax = UINT64_MAX;
        else frame->rax = info.width * 1000u + info.height;
        return frame_address;
    }
    if (number == 22u) {
        enum capability_service service;
        uint32_t object_id;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_OPEN, &service) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !surface_create(scheduler_current_pid(), (uint32_t)frame->rsi,
                            (uint32_t)frame->rdx, &object_id)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        frame->rax = capability_open_surface(scheduler_current_pid(), object_id,
            CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY);
        if (frame->rax == UINT64_MAX) (void)surface_discard(object_id);
        return frame_address;
    }
    if (number == 23u) {
        enum capability_service service;
        uint32_t object_id;
        const uint64_t bytes = frame->rdx * sizeof(uint32_t);
        if (frame->rdx > SURFACE_MAX_WIDTH * SURFACE_MAX_HEIGHT ||
            (frame->rdx != 0u && bytes / sizeof(uint32_t) != frame->rdx) ||
            !capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !user_range(frame->rsi, bytes) ||
            !surface_write(object_id, (size_t)frame->r10,
                (const uint32_t *)(uintptr_t)frame->rsi, (size_t)frame->rdx))
            frame->rax = UINT64_MAX;
        else frame->rax = frame->rdx;
        return frame_address;
    }
    if (number == 24u) {
        uint32_t target_pid;
        if (!ipc_channel_owner(scheduler_current_pid(), frame->rsi, &target_pid)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        frame->rax = capability_grant(scheduler_current_pid(), frame->rdi,
            target_pid, CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_QUERY);
        if (frame->rax != UINT64_MAX) surface_note_share();
        return frame_address;
    }
    if (number == 25u) {
        enum capability_service service;
        uint32_t object_id;
        const uint32_t *pixels;
        size_t pixel_count;
        const uint64_t capacity_bytes = frame->rdx * sizeof(uint32_t);
        if (frame->rdx > SURFACE_MAX_WIDTH * SURFACE_MAX_HEIGHT ||
            (frame->rdx != 0u && capacity_bytes / sizeof(uint32_t) != frame->rdx) ||
            !capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !surface_data(object_id, &pixels, &pixel_count, NULL, NULL) ||
            frame->rdx < pixel_count ||
            !user_write_range(frame->rsi, pixel_count * sizeof(uint32_t)) ||
            !userspace_copy_to(scheduler_current_pid(), frame->rsi,
                (const uint8_t *)pixels, pixel_count * sizeof(uint32_t)))
            frame->rax = UINT64_MAX;
        else frame->rax = pixel_count;
        return frame_address;
    }
    if (number == 26u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rsi,
                CAPABILITY_RIGHT_READ, &service) ||
            service != CAPABILITY_SERVICE_INPUT ||
            !user_write_range(frame->rdx, IPC_MESSAGE_MAX) ||
            !user_write_range(frame->r10, sizeof(struct input_event))) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        uint8_t message[IPC_MESSAGE_MAX];
        size_t length;
        uint32_t sender;
        if (ipc_receive(scheduler_current_pid(), frame->rdi, message,
                        sizeof(message), &length, &sender)) {
            frame->rax = userspace_copy_to(scheduler_current_pid(), frame->rdx,
                message, length) ? 1u : UINT64_MAX;
            frame->rdx = length;
            return frame_address;
        }
        struct input_event event;
        if (input_poll(&event)) {
            frame->rax = userspace_copy_to(scheduler_current_pid(), frame->r10,
                (const uint8_t *)&event, sizeof(event)) ? 2u : UINT64_MAX;
            frame->rdx = sizeof(event);
            return frame_address;
        }
        if (!ipc_wait_select(scheduler_current_pid(), frame->rdi, frame->rdx,
                             IPC_MESSAGE_MAX) ||
            !input_wait(scheduler_current_pid(), frame->r10)) {
            (void)ipc_cancel_wait(scheduler_current_pid());
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        if (frame->r8 != 0u)
            return scheduler_block_current_timeout(frame_address, frame->r8, 3u);
        return scheduler_block_current(frame_address);
    }
    if (number == 27u) {
        enum capability_service service;
        uint32_t object_id;
        uint64_t address;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !map_surface_readonly(scheduler_current_pid(), object_id, &address))
            frame->rax = UINT64_MAX;
        else
            frame->rax = address;
        return frame_address;
    }
    if (number == 28u) {
        enum capability_service service;
        uint32_t object_id;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !surface_damage(object_id, (uint32_t)frame->rsi, (uint32_t)frame->rdx,
                            (uint32_t)frame->r10, (uint32_t)frame->r8))
            frame->rax = UINT64_MAX;
        else
            frame->rax = 0u;
        return frame_address;
    }
    if (number == 29u) {
        enum capability_service service;
        uint32_t object_id;
        uint64_t damage;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_QUERY, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !surface_take_damage(object_id, &damage))
            frame->rax = UINT64_MAX;
        else
            frame->rax = damage;
        return frame_address;
    }
    if (number == 30u) {
        enum capability_service service;
        uint32_t object_id;
        if (!capability_resolve_object(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service, &object_id) ||
            service != CAPABILITY_SERVICE_SURFACE ||
            !userspace_unmap_surface(scheduler_current_pid(), object_id))
            frame->rax = UINT64_MAX;
        else
            frame->rax = 0u;
        return frame_address;
    }
    if (number == 31u) {
        /* Compositor introspection: the compositor self-reports its live
           window count and focused window id after every window-table
           change. Gated on the same DISPLAY write right display_submit
           requires, so only the compositor (the sole holder of that
           capability in this system) can publish this state -- the kernel
           stores exactly what it is told and runs no window-manager policy
           of its own. */
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service) ||
            service != CAPABILITY_SERVICE_DISPLAY) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        display_compositor_report(frame->rsi, frame->rdx);
        frame->rax = 0u;
        return frame_address;
    }
    if (number == 32u) {
        /* Cursor composition is accelerated as a tiny scanout overlay, but
           remains compositor-owned policy and requires DISPLAY write access.
           r10 selects the contextual icon (enum graphics_cursor_icon): 0 is
           the default arrow, 1-13 the resize/hand/text/blocked/info pack
           (see demon_cursor_icon_pixels). An out-of-range index falls back
           to the default arrow in framebuffer_cursor_set_icon rather than
           failing the call. */
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service) ||
            service != CAPABILITY_SERVICE_DISPLAY ||
            frame->rsi > INT32_MAX || frame->rdx > INT32_MAX) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        /* The backbuffer is a low physical kernel allocation. Under a user
           CR3, that numeric address can name the caller's private code/heap
           mapping instead. Cursor restore/blend therefore must use the
           kernel page tables, exactly like display_submit_from_user and
           display_submit_effect. This was the source of intermittent cursor
           pixel trails and compositor-code corruption: the old cursor path
           read/wrote the right offset through the wrong address space. */
        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
        framebuffer_cursor_set_icon((unsigned int)frame->r10);
        (void)framebuffer_cursor_move((int32_t)frame->rsi, (int32_t)frame->rdx);
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        frame->rax = 0u;
        return frame_address;
    }
    if (number == 33u) {
        /* Exposes the kernel's rounded-rect/border/gradient/shadow
           compositing (libs/graphics/graphics.c) to the compositor for real
           chrome effects instead of flat rectangles. The backbuffer's low
           physical address aliases part of USER_CODE's virtual range (see
           the warning on framebuffer access further up this file), so -- like
           display_submit_from_user's bounce -- the actual draw must run
           under the kernel's own address space, not the caller's, or writes
           near the high end of the backbuffer corrupt the running process's
           own code pages. */
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_WRITE, &service) ||
            service != CAPABILITY_SERVICE_DISPLAY ||
            !user_range(frame->rsi, sizeof(struct display_user_effect))) {
            frame->rax = UINT64_MAX; return frame_address;
        }
        const struct display_user_effect request =
            *(const struct display_user_effect *)(uintptr_t)frame->rsi;
        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
        const bool submitted = display_submit_effect(&request);
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        frame->rax = submitted ? 0u : UINT64_MAX;
        return frame_address;
    }
    if (number == 34u) {
        /* Read-only, no capability gate -- boot_test_mode carries no
           sensitive state, it's just "is this the scripted boot-test
           harness or a real interactive session", so any process may ask. */
        frame->rax = boot_test_mode;
        return frame_address;
    }
    if (number == 35u) {
        enum capability_service service;
        uint8_t response[512];
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service) ||
            service != CAPABILITY_SERVICE_NETWORK || frame->rdx == 0u ||
            frame->rdx > sizeof(response) ||
            !user_write_range(frame->rsi, frame->rdx)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
        const size_t amount = network_http_response(response, (size_t)frame->rdx);
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        frame->rax = amount != 0u &&
            userspace_copy_to(scheduler_current_pid(), frame->rsi,
                              response, amount) ? amount : UINT64_MAX;
        return frame_address;
    }
    if (number == 36u) {
        enum capability_service service;
        char url[128];
        uint8_t response[512];
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_READ, &service) ||
            service != CAPABILITY_SERVICE_NETWORK || frame->rdx == 0u ||
            frame->rdx >= sizeof(url) || frame->r8 == 0u ||
            frame->r8 > sizeof(response) ||
            !user_range(frame->rsi, frame->rdx) ||
            !user_write_range(frame->r10, frame->r8)) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        const char *user_url = (const char *)(uintptr_t)frame->rsi;
        for (size_t index = 0u; index < (size_t)frame->rdx; ++index)
            url[index] = user_url[index];
        url[frame->rdx] = '\0';
        uint64_t caller_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(caller_cr3));
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_address_space) : "memory");
        const size_t amount = http_get_url(url, (size_t)frame->rdx,
                                           response, (size_t)frame->r8);
        if (caller_cr3 != kernel_address_space)
            __asm__ volatile ("mov %0, %%cr3" : : "r"(caller_cr3) : "memory");
        frame->rax = amount != 0u &&
            userspace_copy_to(scheduler_current_pid(), frame->r10,
                              response, amount) ? amount : UINT64_MAX;
        return frame_address;
    }
    if (number == 37u) {
        /* Read-only, no capability gate, same reasoning as boot_test_mode
           (syscall 34): whether the firmware's ACPI namespace declares a
           battery device carries no sensitive state. See acpi.c -- this is
           real table-walked presence, never a fabricated charge level. */
        frame->rax = acpi_battery_present() ? 1u : 0u;
        return frame_address;
    }
    if (number == 38u) {
        frame->rax = demonwm_mode ? 1u : 0u;
        return frame_address;
    }
    if (number == 39u) {
        enum capability_service service;
        if (!capability_resolve(scheduler_current_pid(), frame->rdi,
                CAPABILITY_RIGHT_OPEN, &service) ||
            service != CAPABILITY_SERVICE_STORAGE ||
            !user_range(frame->rsi, frame->rdx) ||
            !user_write_range(frame->r8, sizeof(struct demon_dir_entry))) {
            frame->rax = UINT64_MAX;
            return frame_address;
        }
        const char *relative_name = NULL;
        size_t relative_length = 0u, length = 0u;
        bool is_directory = false;
        if (!ramfs_list((const char *)(uintptr_t)frame->rsi, (size_t)frame->rdx,
                        (size_t)frame->r10, &relative_name, &relative_length,
                        &length, &is_directory)) {
            frame->rax = 0u;
            return frame_address;
        }
        struct demon_dir_entry entry;
        for (size_t i = 0u; i < sizeof(entry.name); ++i) entry.name[i] = '\0';
        const size_t copy_length = relative_length < sizeof(entry.name)
            ? relative_length : sizeof(entry.name) - 1u;
        for (size_t i = 0u; i < copy_length; ++i) entry.name[i] = relative_name[i];
        entry.name_length = (uint32_t)copy_length;
        entry.is_directory = is_directory ? 1u : 0u;
        entry.size = (uint64_t)length;
        frame->rax = userspace_copy_to(scheduler_current_pid(), frame->r8,
            (const uint8_t *)&entry, sizeof(entry)) ? 1u : UINT64_MAX;
        return frame_address;
    }
    if (number == 40u) {
        frame->rax = anonymous_map(scheduler_current_pid(), frame->rdi);
        return frame_address;
    }
    if (number == 41u) {
        const uint32_t pid = scheduler_current_pid();
        if (frame->rdi != USER_ANON_BASE || pid == 0u ||
            (anonymous_page_count[pid] == 0u && anonymous_reserved_pages[pid] == 0u)) {
            frame->rax = UINT64_MAX;
        } else {
            anonymous_release(pid);
            frame->rax = 0u;
        }
        return frame_address;
    }
    if (number == 46u) {
        frame->rax = anonymous_reserve(scheduler_current_pid(), frame->rdi);
        return frame_address;
    }
    if (number == 47u) {
        frame->rax = anonymous_commit(scheduler_current_pid(), frame->rdi, frame->rsi != 0u);
        return frame_address;
    }
    frame->rax = (uint64_t)-1;
    return frame_address;
}

// Frames allocated for one purpose (code, heap) come from consecutive
// mako_frame_allocate() calls against the same bump-allocator state, so
// each group is physically contiguous — code_frames[0] is both the base
// physical address and, since the low region stays identity-mapped, a
// valid kernel pointer to the whole group in one shot.
static bool frames_contiguous(const uint64_t *frames, size_t count) {
    for (size_t i = 1; i < count; ++i)
        if (frames[i] != frames[i - 1] + 4096u) return false;
    return true;
}

static bool load_process(uint32_t expected_pid, uint32_t parent_pid, const char *name,
                         const struct userspace_memory *memory,
                         const uint8_t *image, size_t image_size, uint32_t service_mask) {
    if (memory->code_page_count == 0u ||
        memory->code_page_count > USERSPACE_CODE_PAGES ||
        !frames_contiguous(memory->code_frames, memory->code_page_count) ||
        !frames_contiguous(memory->heap_frames, USERSPACE_HEAP_PAGES))
        return false;

    for (size_t page = 0u; page < USERSPACE_STACK_PAGES; ++page) {
        uint8_t *stack = (uint8_t *)(uintptr_t)memory->stack_frames[page];
        for (size_t i = 0; i < 4096u; ++i) stack[i] = 0u;
    }
    uint8_t *heap = (uint8_t *)(uintptr_t)memory->heap_frames[0];
    for (size_t i = 0; i < USER_HEAP_SIZE; ++i) heap[i] = 0u;

    struct elf64_layout layout;
    if (!elf64_executable_layout(image, image_size, &layout)) return false;
    uint64_t entry = 0u;
    if (layout.virtual_base == USER_LARGE_CODE_BASE) {
        if (!large_code_load(expected_pid, image, image_size, &layout, &entry))
            return false;
    } else {
        const struct elf64_target target = {
            .virtual_base = USER_CODE,
            .memory = (uint8_t *)(uintptr_t)memory->code_frames[0],
            .memory_size = memory->code_page_count * 4096u,
        };
        if (!elf64_load_executable(image, image_size, &target, &entry)) return false;
    }

    uint64_t *pt1 = (uint64_t *)(uintptr_t)memory->page_table;
    // +7u (present+write+user), not +5u: the ELF loader folds every app's
    // .bss/.data into this same single LOAD segment as .text (there is no
    // separate read-only text vs. writable data mapping in this loader), so
    // any app with mutable static storage here needs the code pages
    // writable. Previously unnoticed because no self-test-spawned app wrote
    // to a code-segment static until cxx_hello's cxx_runtime arena did.
    for (size_t i = 0; i < memory->code_page_count; ++i)
        pt1[(USER_CODE - 0x200000u) / 4096u + i] = memory->code_frames[i] + 7u;
    for (size_t i = 0; i < USERSPACE_HEAP_PAGES; ++i)
        pt1[(USER_HEAP - 0x200000u) / 4096u + i] = memory->heap_frames[i] + 7u;
    for (size_t page = 0u; page < USERSPACE_STACK_PAGES; ++page)
        pt1[(USER_STACK_PAGE - 0x200000u) / 4096u + page] =
            memory->stack_frames[page] + 7u;

    uint32_t pid = scheduler_create_user(parent_pid, name, entry,
        USER_STACK_PAGE + USER_STACK_SIZE - 16u, memory->address_space);
    if (pid != expected_pid || !capability_assign_services(pid, service_mask)) {
        large_code_release(expected_pid);
        return false;
    }
    process_memory_used[pid] = true;
    return true;
}

bool userspace_init(const struct userspace_memory processes[SCHEDULER_PROCESS_LIMIT - 1u],
                    uintptr_t frame_allocator_state) {
    if (processes == NULL || frame_allocator_state == 0u) return false;
    install_gdt_tss();
    __asm__ volatile ("mov %%cr3, %0" : "=r"(kernel_address_space));
    userspace_frame_allocator = frame_allocator_state;
    anonymous_free_frames = 0u;
    for (uint32_t pid = 0u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) {
        anonymous_page_count[pid] = 0u;
        large_code_page_count[pid] = 0u;
        large_code_page_table[pid] = 0u;
        for (size_t page = 0u; page < USER_LARGE_CODE_MAX_PAGES; ++page)
            large_code_frames[pid][page] = 0u;
        for (size_t slot = 0u; slot < USER_SURFACE_MAP_SLOTS; ++slot)
            mapped_surface[pid][slot] = 0u;
    }
    for (uint32_t pid = 1u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) {
        process_memory[pid] = processes[pid - 1u]; process_memory_used[pid] = false;
    }
    const uint8_t *image = _binary_build_user_program_elf_start;
    size_t image_size = (size_t)(_binary_build_user_program_elf_end - image);
    const uint32_t bootstrap_services =
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CLOCK) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC);
    if (!load_process(1u, 0u, "init", &process_memory[1], image, image_size, bootstrap_services) ||
        !load_process(2u, 0u, "worker", &process_memory[2], image, image_size, bootstrap_services))
        return false;
    initialized = true;
    return true;
}

uint32_t userspace_spawn_path(uint32_t parent_pid, const char *path, size_t path_length,
                              const char *name, uint32_t service_mask) {
    if (!initialized || path == NULL || name == NULL || path_length == 0u) return 0u;
    uint32_t object_id; size_t image_size; const uint8_t *image;
    if (!ramfs_open(path, path_length, false, &object_id) ||
        !ramfs_view(object_id, &image, &image_size)) return 0u;
    /* Try each free slot. A large executable can skip compact application
       slots and use a larger system slot when one is available. load_process
       has no scheduler/capability side effects until ELF validation succeeds. */
    for (uint32_t candidate = 1u; candidate < SCHEDULER_PROCESS_LIMIT; ++candidate) {
        if (process_memory_used[candidate]) continue;
        if (load_process(candidate, parent_pid, name, &process_memory[candidate],
                         image, image_size, service_mask)) return candidate;
    }
    return 0u;
}

void userspace_release_process(uint32_t pid) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT) return;
    ipc_process_cleanup(pid);
    capabilities_close_all(pid);
    unmap_surface(pid);
    anonymous_release(pid);
    large_code_release(pid);
    process_memory_used[pid] = false;
}

bool userspace_run_init(void) {
    if (!initialized) return false;
    last_exit_status = UINT64_MAX;
    const uintptr_t first_frame = scheduler_start_users();
    if (first_frame == 0u) return false;
    user_resume_ring3(first_frame);
    /* The last exit syscall returns through the kernel continuation instead
       of iretq. An interrupt gate clears IF on entry, so explicitly restore
       hardware interrupts before boot continues into the interactive shell. */
    __asm__ volatile ("sti" : : : "memory");
    return last_exit_status != UINT64_MAX;
}

uint64_t userspace_exit_code(void) { return last_exit_status; }

uint64_t userspace_yield_count(void) {
    uint64_t total = 0u;
    for (size_t index = 1u; index < scheduler_task_count(); ++index) {
        struct scheduler_task_snapshot task;
        if (scheduler_snapshot(index, &task)) total += task.yields;
    }
    return total;
}

#include <kernel/multiboot2.h>
#include <kernel/acpi.h>
#include <kernel/ac97.h>
#include <kernel/ahci.h>
#include <kernel/capability.h>
#include <kernel/cxx_runtime.h>
#include <kernel/bounded_table_test.h>
#include <kernel/slot_table_test.h>
#include <kernel/apps.h>
#include <kernel/framebuffer.h>
#include <kernel/display.h>
#include <kernel/e1000.h>
#include <demon/graphics.h>
#include <demon/input.h>
#include <kernel/git.h>
#include <kernel/http.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/ipc.h>
#include <kernel/makobox.h>
#include <kernel/network.h>
#include <kernel/pci.h>
#include <kernel/ramfs.h>
#include <kernel/runas.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/surface.h>
#include <kernel/terminal.h>
#include <kernel/userspace.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

extern uint64_t mako_add(uint64_t left, uint64_t right);
extern uint64_t mako_mix(uint64_t value, uint64_t salt);
extern uint64_t mako_memory_accumulate(uint64_t total, uint64_t base,
                                       uint64_t length, uint64_t kind);
extern uint64_t mako_memory_entry_accumulate(uint64_t total,
                                             uintptr_t entry_address);
extern uint64_t mako_signed_less(int64_t left, int64_t right);
extern int64_t mako_signed_div(int64_t value, int64_t divisor);
extern void mako_vga_cell(uint64_t index, uint64_t glyph, uint64_t color);
extern uint16_t mako_vga_read(uint64_t index);
extern uint64_t mako_frame_region_init(uintptr_t state_address,
                                       uintptr_t entry_address,
                                       uint64_t minimum);
extern uint64_t mako_frame_allocate(uintptr_t state_address);
extern uint64_t mako_build_identity_4m(uint64_t pml4, uint64_t pdpt,
                                       uint64_t pd, uint64_t pt0, uint64_t pt1);
extern uint64_t mako_page_probe(uint64_t address, uint64_t value);

struct frame_allocator {
    uint64_t next;
    uint64_t end;
};

static struct frame_allocator frames;

static uint64_t allocate_contiguous(size_t bytes, size_t *capacity) {
    if (bytes == 0u || capacity == NULL || bytes > FRAMEBUFFER_BACKBUFFER_MAX)
        return 0u;
    const size_t pages = (bytes + 4095u) / 4096u;
    uint64_t first = 0u;
    uint64_t previous = 0u;
    for (size_t page = 0u; page < pages; ++page) {
        const uint64_t frame = mako_frame_allocate((uintptr_t)&frames);
        if (frame == 0u || (page != 0u && frame != previous + 4096u)) return 0u;
        if (page == 0u) first = frame;
        previous = frame;
    }
    *capacity = pages * 4096u;
    return first;
}

static bool map_mmio_identity_2m(uint64_t pdpt_address, uint64_t low_pd_address,
                                 uint64_t spare_pd_address,
                                 uint64_t physical_address, size_t length) {
    if (length == 0u || physical_address > UINT64_MAX - length) return false;
    const uint64_t first = physical_address & ~0x1FFFFFull;
    const uint64_t end = (physical_address + length + 0x1FFFFFull) & ~0x1FFFFFull;
    if (end <= first || (first >> 30u) != ((end - 1u) >> 30u) ||
        (first >> 39u) != 0u) return false;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)pdpt_address;
    const size_t pdpt_index = (size_t)(first >> 30u) & 0x1FFu;
    uint64_t *pd;
    if (pdpt_index == 0u) {
        pd = (uint64_t *)(uintptr_t)low_pd_address;
    } else if ((pdpt[pdpt_index] & 1u) != 0u) {
        pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_index] & ~0xFFFull);
    } else {
        pd = (uint64_t *)(uintptr_t)spare_pd_address;
        for (size_t index = 0u; index < 512u; ++index) pd[index] = 0u;
        pdpt[pdpt_index] = spare_pd_address | 3u;
    }
    for (uint64_t address = first; address < end; address += 0x200000u) {
        const size_t index = (size_t)(address >> 21u) & 0x1FFu;
        if (pdpt_index == 0u && index < 2u) continue;
        if ((pd[index] & 1u) != 0u && (pd[index] & ~0x1FFFFFull) != address)
            return false;
        /* Present, writable, huge, and cache-disabled for device registers. */
        pd[index] = address | 0x93u;
    }
    return true;
}
extern char kernel_end[];
extern char kernel_start[];

static uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static void write_cr0(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(value) : "memory");
}

static void write_cr4(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr4" : : "r"(value) : "memory");
}

// User apps built with real `double` arithmetic (Wave 1 of the EDE port --
// see apps/ede_calc) need the hardware FPU/SSE unit actually enabled, and
// the scheduler needs FXSAVE/FXRSTOR per task (see scheduler.c's
// fpu_state[]/switch_fpu) so one task's floating-point registers can't leak
// into or get clobbered by another's on a preemptive context switch. This
// must run once, here, before scheduler_init() creates any task and before
// any code executes an x87/SSE instruction.
static void enable_fpu(void) {
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ull << 2u);  // CR0.EM = 0: FPU/SSE instructions are not emulated/trapped
    cr0 |= (1ull << 1u);   // CR0.MP = 1: WAIT/FWAIT instructions respect TS
    write_cr0(cr0);
    uint64_t cr4 = read_cr4();
    cr4 |= (1ull << 9u);   // CR4.OSFXSR: FXSAVE/FXRSTOR are legal
    cr4 |= (1ull << 10u);  // CR4.OSXMMEXCPT: unmasked SSE exceptions use #XM, not #UD
    write_cr4(cr4);
    __asm__ volatile ("fninit");
}

static bool allocate_process_memory(struct userspace_memory *memory,
                                    uint64_t kernel_pdpt_address,
                                    size_t code_page_count) {
    if (code_page_count == 0u || code_page_count > USERSPACE_CODE_PAGES)
        return false;
    const uint64_t pml4 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pdpt = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pd = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pt0 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pt1 = mako_frame_allocate((uintptr_t)&frames);
    uint64_t stack_frames[USERSPACE_STACK_PAGES];
    for (size_t page = 0u; page < USERSPACE_STACK_PAGES; ++page)
        stack_frames[page] = mako_frame_allocate((uintptr_t)&frames);
    bool stack_ok = stack_frames[0] != 0u;
    for (size_t page = 1u; page < USERSPACE_STACK_PAGES; ++page)
        stack_ok = stack_ok && stack_frames[page] == stack_frames[0] + page * 4096u;
    if (pml4 == 0u || pdpt == 0u || pd == 0u || pt0 == 0u || pt1 == 0u ||
        !stack_ok || mako_build_identity_4m(pml4, pdpt, pd, pt0, pt1) != 1u)
        return false;
    if (framebuffer_available()) {
        const struct framebuffer_info *display = framebuffer_get_info();
        const size_t index = (size_t)(display->physical_address >> 30u) & 0x1FFu;
        uint64_t *process_pdpt = (uint64_t *)(uintptr_t)pdpt;
        const uint64_t *kernel_pdpt = (const uint64_t *)(uintptr_t)kernel_pdpt_address;
        process_pdpt[index] = kernel_pdpt[index];
    }
    *memory = (struct userspace_memory){
        .address_space = pml4,
        .page_directory = pd,
        .page_table = pt1,
        .code_page_count = code_page_count,
    };
    for (size_t page = 0u; page < USERSPACE_STACK_PAGES; ++page)
        memory->stack_frames[page] = stack_frames[page];
    for (size_t i = 0; i < code_page_count; ++i) {
        const uint64_t frame = mako_frame_allocate((uintptr_t)&frames);
        if (frame == 0u) return false;
        memory->code_frames[i] = frame;
    }
    for (size_t i = 0; i < USERSPACE_HEAP_PAGES; ++i) {
        const uint64_t frame = mako_frame_allocate((uintptr_t)&frames);
        if (frame == 0u) return false;
        memory->heap_frames[i] = frame;
    }
    return true;
}

__attribute__((noreturn))
static void boot_fatal(const char *message) {
    serial_write("[FAILED] ");
    serial_write(message);
    serial_write("\n");
    terminal_write("[FAILED] ");
    terminal_write_line(message);
    for (;;) __asm__ volatile ("cli; hlt");
}

static void boot_status(const char *name, const char *detail) {
    serial_write("[  OK  ] ");
    serial_write(name);
    if (detail != NULL) {
        serial_write(" -- ");
        serial_write(detail);
    }
    serial_write("\n");

    terminal_write("[  OK  ] ");
    terminal_write(name);
    if (detail != NULL) {
        terminal_write(" -- ");
        terminal_write(detail);
    }
    terminal_write_line("");
}

/* Announce work before it begins so a slow hardware/network operation never
   looks like a frozen black screen. Unlike a splash animation, every message
   names the real operation the kernel is about to execute. */
static void boot_progress(const char *detail) {
    serial_write("[ .... ] ");
    serial_write(detail);
    serial_write("\n");
    terminal_write("[ .... ] ");
    terminal_write_line(detail);
}

static void boot_banner(void) {
    serial_write("\n");
    serial_write("             /\\\n");
    serial_write("            /  \\       MAKO Kernel Project\n");
    serial_write("           / /\\ \\      Native C + MKO on x86_64\n");
    serial_write("          / ____ \\     Sharp tools. Small core.\n");
    serial_write("         /_/    \\_\\\n");
    serial_write("\n");

    terminal_write_line("             /\\");
    terminal_write_line("            /  \\       MAKO Kernel Project");
    terminal_write_line("           / /\\ \\      Native C + MKO on x86_64");
    terminal_write_line("          / ____ \\     Sharp tools. Small core.");
    terminal_write_line("         /_/    \\_\\");
    terminal_write_line("");
}

static void boot_welcome(void) {
    serial_write("\n============================================================\n");
    serial_write("             Welcome to the MAKO kernel!\n");
    serial_write("============================================================\n");
    serial_write("The kernel is initialized and waiting for the next subsystem.\n");

    terminal_write_line("============================================================");
    terminal_write_line("             Welcome to the MAKO kernel!");
    terminal_write_line("============================================================");
    terminal_write_line("The kernel is ready. Scheduler, syscalls, and ring 3 are online.");
}

/* Graphical boots stay in the native userspace session. Hardware IRQs wake
   blocked compositor/client tasks; the kernel runs them until they block
   again, discards only the mirrored kernel-console characters, then halts
   until the next interrupt. Desktop input itself remains in the unified
   input queue and is never consumed by an invisible MakoBox prompt. */
__attribute__((noreturn))
static void desktop_session_loop(uint32_t compositor_pid) {
    serial_write("DESKTOP_DEFAULT_TARGET_OK target=desktop.target\n");
    serial_write("DESKTOP_SESSION_READY display=compositor input=compositor console=recovery-only\n");
    for (;;) {
        if (scheduler_has_ready_users()) (void)userspace_run_init();
        keyboard_discard_chars();
        struct scheduler_task_snapshot compositor;
        if (!scheduler_snapshot(compositor_pid, &compositor) ||
            compositor.state == SCHEDULER_TASK_EXITED)
            boot_fatal("Native desktop compositor exited during the session");
        /* A userspace return or a future scheduler path must never leave IF
           clear here.  The desktop depends on IRQ1/IRQ12/PIT to wake blocked
           clients, so make the idle transition atomically interruptible on
           every iteration rather than inheriting an unknown flags state. */
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
}

static uintptr_t align_up_8(uintptr_t value) {
    return (value + 7u) & ~(uintptr_t)7u;
}

static uintptr_t multiboot_reserved_end(uintptr_t info_address,
                                        uintptr_t minimum) {
    const struct multiboot2_info *info =
        (const struct multiboot2_info *)info_address;
    uintptr_t reserved_end = minimum;
    uintptr_t cursor = info_address + sizeof(*info);
    const uintptr_t end = info_address + info->total_size;
    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag =
            (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_END) break;
        if (tag->size < sizeof(*tag)) break;
        if (tag->type == MULTIBOOT2_TAG_MODULE &&
            tag->size >= sizeof(struct multiboot2_tag_module)) {
            const struct multiboot2_tag_module *module =
                (const struct multiboot2_tag_module *)tag;
            if (module->module_end > reserved_end)
                reserved_end = module->module_end;
        }
        cursor = align_up_8(cursor + tag->size);
    }
    return reserved_end;
}

static uint64_t install_multiboot_projects(uintptr_t info_address) {
    const struct multiboot2_info *info =
        (const struct multiboot2_info *)info_address;
    uintptr_t cursor = info_address + sizeof(*info);
    const uintptr_t end = info_address + info->total_size;
    uint64_t installed = 0u;
    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag =
            (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_END) break;
        if (tag->size < sizeof(*tag)) break;
        if (tag->type == MULTIBOOT2_TAG_MODULE &&
            tag->size >= sizeof(struct multiboot2_tag_module)) {
            const struct multiboot2_tag_module *module =
                (const struct multiboot2_tag_module *)tag;
            const size_t command_capacity =
                tag->size - offsetof(struct multiboot2_tag_module, command_line);
            size_t name_length = 0u;
            while (name_length < command_capacity &&
                   module->command_line[name_length] != '\0')
                ++name_length;
            if (name_length == command_capacity ||
                module->module_end < module->module_start ||
                module->module_end > 0x40000000u)
                boot_fatal("Invalid or unreachable MKO ISO module");
            const size_t module_length =
                (size_t)(module->module_end - module->module_start);
            const uint8_t *module_data =
                (const uint8_t *)(uintptr_t)module->module_start;
            /* Large boot assets remain in their Multiboot-reserved frames.
               Small executable/config modules retain the historical copied,
               writable behavior. */
            const bool seeded = module_length > 196608u ?
                ramfs_seed_reference(module->command_line, name_length,
                                     module_data, module_length) :
                ramfs_seed(module->command_line, name_length,
                           module_data, module_length);
            if (!seeded)
                boot_fatal("Could not install MKO ISO module");
            serial_write("preinstalled MKO asset: ");
            serial_write(module->command_line);
            serial_write(" (");
            serial_write_u64(module_length);
            serial_write(" bytes)\n");
            ++installed;
        }
        cursor = align_up_8(cursor + tag->size);
    }
    return installed;
}

static uint64_t report_memory_map(const struct multiboot2_tag_mmap *map,
                                  uintptr_t allocation_floor) {
    uint64_t usable_bytes = 0;
    uint32_t entries = 0;
    const uintptr_t begin = (uintptr_t)&map->entries[0];
    const uintptr_t end = (uintptr_t)map + map->size;

    for (uintptr_t cursor = begin; cursor + map->entry_size <= end; cursor += map->entry_size) {
        ++entries;
        usable_bytes = mako_memory_entry_accumulate(usable_bytes, cursor);
        if (frames.next == 0u) {
            (void)mako_frame_region_init((uintptr_t)&frames, cursor, allocation_floor);
        }
    }

    serial_write("memory map entries: ");
    serial_write_u64(entries);
    serial_write("\nusable memory: ");
    serial_write_u64(usable_bytes / (1024u * 1024u));
    serial_write(" MiB\n");
    return usable_bytes;
}

/* Distinguishes the scripted boot-test sequence (make smoke/check, which
   needs the compositor responsive within a fixed, small tick budget for its
   exact frame-count/timing assertions) from a real interactive boot (make
   run, where a human needs several real seconds to actually read the
   loading/login screens) -- see grub/grub.cfg's second "MAKO Kernel Project
   (boot test)" menu entry, which is the only place this cmdline token is
   ever passed. Absent the token (the default, normal menu entry), boot is
   real/interactive; this is the correct default for what is meant to become
   an actual installable OS, not a permanently-scripted demo. */
/* Substring search over the multiboot2 command line -- used by
   multiboot_test_mode (needle "boottest", grub-test.cfg only). */
static bool multiboot_cmdline_has(uintptr_t info_address, const char *needle) {
    const struct multiboot2_info *info = (const struct multiboot2_info *)info_address;
    uintptr_t cursor = info_address + sizeof(*info);
    const uintptr_t end = info_address + info->total_size;
    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag = (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_END) break;
        if (tag->size < sizeof(*tag)) break;
        if (tag->type == MULTIBOOT2_TAG_CMDLINE) {
            const struct multiboot2_tag_string *cmdline =
                (const struct multiboot2_tag_string *)tag;
            for (const char *scan = cmdline->string; *scan != '\0'; ++scan) {
                size_t match = 0u;
                while (needle[match] != '\0' && scan[match] == needle[match]) ++match;
                if (needle[match] == '\0') return true;
            }
        }
        cursor = align_up_8(cursor + tag->size);
    }
    return false;
}

static bool multiboot_test_mode(uintptr_t info_address) {
    return multiboot_cmdline_has(info_address, "boottest");
}

/* Same pattern as multiboot_test_mode's "boottest" needle: the desktop
   stack (compositor/DemonX/DemonWM/xterm) only takes over the boot when
   explicitly requested via this cmdline token (grub-desktop.cfg), not
   merely because the binaries happen to be present in RAMFS. Without
   this gate, `make run`'s plain interactive console -- the one every
   other port's play command (cave-story, quake, doom) actually runs
   from -- would be silently replaced by the desktop session the moment
   compositor.elf was added to that ISO's module list. */
static bool multiboot_desktop_mode(uintptr_t info_address) {
    return multiboot_cmdline_has(info_address, "desktop");
}


static uint64_t report_multiboot(uintptr_t info_address, uintptr_t allocation_floor) {
    const struct multiboot2_info *info = (const struct multiboot2_info *)info_address;
    serial_write("multiboot info: ");
    serial_write_hex(info_address);
    serial_write(" size=");
    serial_write_u64(info->total_size);
    serial_write("\n");

    uintptr_t cursor = info_address + sizeof(*info);
    const uintptr_t end = info_address + info->total_size;
    uint64_t usable_bytes = 0;
    bool found_map = false;
    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag = (const struct multiboot2_tag *)cursor;
        if (tag->type == MULTIBOOT2_TAG_END) break;
        if (tag->size < sizeof(*tag)) break;

        if (tag->type == MULTIBOOT2_TAG_BOOT_LOADER_NAME) {
            const struct multiboot2_tag_string *name =
                (const struct multiboot2_tag_string *)tag;
            serial_write("bootloader: ");
            serial_write(name->string);
            serial_write("\n");
        } else if (tag->type == MULTIBOOT2_TAG_BASIC_MEMINFO) {
            const struct multiboot2_tag_basic_meminfo *memory =
                (const struct multiboot2_tag_basic_meminfo *)tag;
            serial_write("upper memory: ");
            serial_write_u64(memory->mem_upper_kib / 1024u);
            serial_write(" MiB\n");
        } else if (tag->type == MULTIBOOT2_TAG_MMAP) {
            usable_bytes = report_memory_map(
                (const struct multiboot2_tag_mmap *)tag, allocation_floor);
            found_map = true;
        } else if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER) {
            const struct multiboot2_tag_framebuffer *framebuffer =
                (const struct multiboot2_tag_framebuffer *)tag;
            if (framebuffer_discover(framebuffer)) {
                serial_write("FRAMEBUFFER_DETECTED ");
                serial_write_u64(framebuffer->width);
                serial_write("x");
                serial_write_u64(framebuffer->height);
                serial_write(" pitch=");
                serial_write_u64(framebuffer->pitch);
                serial_write(" bpp=");
                serial_write_u64(framebuffer->bits_per_pixel);
                serial_write(" address=");
                serial_write_hex(framebuffer->address);
                serial_write("\n");
            } else {
                serial_write("FRAMEBUFFER_REJECTED unsupported or unsafe mode\n");
            }
        }
        cursor = align_up_8(cursor + tag->size);
    }
    if (!found_map || usable_bytes == 0u) boot_fatal("No usable Multiboot2 memory map");
    return usable_bytes;
}

__attribute__((noreturn))
void kernel_main(uint32_t multiboot_magic, uintptr_t multiboot_info) {
    serial_init();
    terminal_init();
    framebuffer_reset();
    boot_banner();
    boot_status("Early console", "VGA text + COM1 serial");
    const uint64_t cr0 = read_cr0();
    const uint64_t cr3 = read_cr3();
    const uint64_t cr4 = read_cr4();
    if ((cr0 & (1ull << 31u)) == 0u || cr3 == 0u || (cr4 & (1ull << 5u)) == 0u)
        boot_fatal("CPU paging/PAE state is invalid");
    boot_status("CPU paging", "CR0.PG + CR4.PAE active, CR3 loaded");
    enable_fpu();
    boot_status("FPU/SSE", "CR0.EM cleared, CR4.OSFXSR set, FPU reset");
    if (multiboot_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        boot_fatal("Invalid Multiboot2 magic");
    }
    boot_status("Boot protocol", "Multiboot2 validated");
    const bool test_mode = multiboot_test_mode(multiboot_info);
    const bool doom_frame_test =
        multiboot_cmdline_has(multiboot_info, "doomtest");
    /* 0 = interactive boot, 1 = ordinary scripted smoke boot, and
       2 = dedicated Doom gameplay test. The distinction lets the full Doom
       image run real game logic in doomtest without making every normal
       launch skip its title/menu. */
    userspace_set_boot_test_mode(test_mode ? 1u : (doom_frame_test ? 2u : 0u));

    const struct multiboot2_info *boot_info =
        (const struct multiboot2_info *)multiboot_info;
    uintptr_t allocation_floor = (uintptr_t)kernel_end;
    const uintptr_t boot_info_end = multiboot_info + boot_info->total_size;
    if (boot_info_end > allocation_floor) allocation_floor = boot_info_end;
    allocation_floor = multiboot_reserved_end(multiboot_info, allocation_floor);
    allocation_floor = (allocation_floor + 4095u) & ~(uintptr_t)4095u;
    serial_write("reserved through: ");
    serial_write_hex(allocation_floor);
    serial_write("\n");
    const uint64_t usable_bytes = report_multiboot(multiboot_info, allocation_floor);
    if (usable_bytes == 0u || frames.next < allocation_floor)
        boot_fatal("Firmware map or allocator reservation check failed");
    serial_write("MKO_MULTIBOOT_PARSE_OK\n");
    boot_status("Firmware memory map", "decoded by native MKO");
    const uint64_t frame0 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t frame1 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t frame2 = mako_frame_allocate((uintptr_t)&frames);
    serial_write("MKO frames: ");
    serial_write_hex(frame0);
    serial_write(" ");
    serial_write_hex(frame1);
    serial_write(" ");
    serial_write_hex(frame2);
    serial_write("\n");
    if (frame0 == 0u || frame1 != frame0 + 4096u || frame2 != frame1 + 4096u) {
        boot_fatal("MKO frame allocator sequence failed");
    }
    serial_write("MKO_FRAME_ALLOCATOR_OK\n");
    boot_status("Physical frames", "MKO 4 KiB allocator online");
    const uint64_t pml4 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pdpt = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pd = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pt0 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t pt1 = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t probe_frame = mako_frame_allocate((uintptr_t)&frames);
    const uint64_t framebuffer_pd = framebuffer_backbuffer_bytes() != 0u ?
        mako_frame_allocate((uintptr_t)&frames) : 0u;
    size_t backbuffer_capacity = 0u;
    const uint64_t backbuffer_address = allocate_contiguous(
        framebuffer_backbuffer_bytes(), &backbuffer_capacity);
    serial_write("BACKBUFFER_RANGE base="); serial_write_hex(backbuffer_address);
    serial_write(" bytes="); serial_write_hex((uint64_t)backbuffer_capacity);
    serial_write(" end="); serial_write_hex(backbuffer_address + (uint64_t)backbuffer_capacity);
    serial_write(" frames.next_after="); serial_write_hex(frames.next);
    serial_write("\n");
    if (pml4 == 0u || pdpt == 0u || pd == 0u || pt0 == 0u || pt1 == 0u ||
        probe_frame == 0u ||
        mako_build_identity_4m(pml4, pdpt, pd, pt0, pt1) != 1u)
        boot_fatal("MKO page-table construction failed");
    write_cr3(pml4);
    if (read_cr3() != pml4 ||
        mako_page_probe(probe_frame, 0x4D414B4F50414745ull) != 1u)
        boot_fatal("MKO virtual-memory activation/probe failed");
    serial_write("active CR3: ");
    serial_write_hex(read_cr3());
    serial_write("\nMKO_VIRTUAL_MEMORY_OK\n");
    boot_status("Virtual memory", "MKO 4 KiB tables active, 1 GiB mapped");
    /* Real ACPI table walk (RSDP -> RSDT/XSDT -> FADT -> DSDT/SSDT byte scan
       for the PNP0C0A Control Method Battery ID) -- needs the 1 GiB identity
       map active since firmware tables can live well past the first 4 MiB. */
    (void)acpi_init();
    boot_status("ACPI", acpi_present() ?
        (acpi_battery_present() ? "tables parsed, battery device declared" :
                                   "tables parsed, no battery device declared") :
        "not present");
    if (framebuffer_backbuffer_bytes() != 0u) {
        if (framebuffer_pd == 0u || backbuffer_address == 0u ||
            backbuffer_address + backbuffer_capacity > 0x40000000u ||
            !framebuffer_enable(pdpt, pd, framebuffer_pd, backbuffer_address,
                                backbuffer_capacity))
            boot_fatal("Detected framebuffer could not be mapped safely");
        write_cr3(pml4);
        serial_write("FRAMEBUFFER_MAPPING_OK\n");
        if (!framebuffer_self_test()) boot_fatal("Framebuffer clipping/backbuffer test failed");
        serial_write("FRAMEBUFFER_PRIMITIVES_OK\n");
        if (!graphics_self_test()) boot_fatal("Stage 2 software renderer self-test failed");
        serial_write("GRAPHICS_LIBRARY_OK\n");
        /* framebuffer_self_test deliberately leaves its pointer test overlay
           on scanout. Take display ownership now, before network discovery and
           the rest of boot, so progress is visible instead of a black test
           scene with a frozen cursor. */
        framebuffer_cursor_hide();
        terminal_graphical_enable();
        boot_status("Framebuffer", "linear RGB mode + software backbuffer online");
    } else {
        serial_write("FRAMEBUFFER_FALLBACK_VGA\n");
        boot_status("Framebuffer", "unavailable; VGA text fallback retained");
    }
    boot_progress("Loading interrupt descriptor table");
    interrupts_init();
    if (!interrupts_breakpoint_self_test())
        boot_fatal("IDT breakpoint round-trip failed");
    serial_write("INTERRUPT_SELF_TEST_OK\n");
    boot_status("CPU exceptions", "IDT loaded; int3 round-trip passed");
    scheduler_init(pml4);
    capabilities_init();
    ipc_init();
    ramfs_init();
    boot_progress("Checking Ethernet, ARP, and IPv4 packet core");
    network_init();
    if (!network_self_test())
        boot_fatal("Ethernet/ARP/IPv4 packet-core self-test failed");
    /* The self-test uses a synthetic link; restore the honest boot state.
       A NIC driver will call network_set_device/network_set_link later. */
    network_init();
    serial_write("NETWORK_PACKET_CORE_OK ethernet=validated arp=cache ipv4=checksum\n");
    boot_status("Network core", "Ethernet + ARP + IPv4 parser ready; no NIC attached");
    boot_progress("Scanning PCI devices");
    pci_init();
    serial_write("PCI_ENUMERATION_OK devices=");
    serial_write_u64(pci_device_count());
    serial_write("\n");
    if (ac97_init())
        boot_status("Audio", "AC'97 PCM output ready at 44.1 kHz stereo");
    else
        boot_status("Audio", "no supported AC'97 controller; sound disabled");
    struct pci_device ethernet;
    if (pci_find_class(2u, 0u, 0u, &ethernet)) {
        serial_write("PCI_ETHERNET_FOUND vendor=");
        serial_write_hex(ethernet.vendor_id);
        serial_write(" device=");
        serial_write_hex(ethernet.device_id);
        serial_write(" bdf=");
        serial_write_u64(ethernet.bus);
        serial_write(":");
        serial_write_u64(ethernet.slot);
        serial_write(".");
        serial_write_u64(ethernet.function);
        serial_write(" irq=");
        serial_write_u64(ethernet.interrupt_line);
        serial_write("\n");
        boot_status("PCI network", "Ethernet controller discovered; driver pending");
        const uint64_t ethernet_mmio = ethernet.bars[0] & ~0xFull;
        if (ethernet.vendor_id == 0x8086u && ethernet.device_id == 0x100Eu &&
            map_mmio_identity_2m(pdpt, pd, framebuffer_pd, ethernet_mmio, 0x20000u)) {
            write_cr3(pml4);
            if (!e1000_probe(&ethernet, (uintptr_t)ethernet_mmio))
                boot_fatal("E1000 reset/MAC probe failed");
            const uint8_t *mac = e1000_mac();
            serial_write("E1000_DEVICE_READY mmio=");
            serial_write_hex(e1000_mmio_base());
            serial_write(" mac=");
            for (size_t byte = 0u; byte < 6u; ++byte) {
                if (byte != 0u) serial_write(":");
                const char digits[] = "0123456789ABCDEF";
                char pair[3] = {digits[mac[byte] >> 4u],
                                digits[mac[byte] & 0x0Fu], '\0'};
                serial_write(pair);
            }
            serial_write(e1000_link_up() ? " link=up\n" : " link=down\n");
            boot_status("E1000", e1000_link_up() ?
                "reset + MAC discovery passed; link up" :
                "reset + MAC discovery passed; link down");
            size_t network_dma_capacity = 0u;
            const uint64_t network_dma = allocate_contiguous(9u * 4096u,
                                                              &network_dma_capacity);
            if (network_dma == 0u ||
                !e1000_start((uintptr_t)network_dma, network_dma_capacity))
                boot_fatal("E1000 descriptor-ring initialization failed");
            serial_write("E1000_DMA_RINGS_READY rx=8 tx=8 buffers=2048 arena=");
            serial_write_hex(network_dma);
            serial_write("\n");
            boot_progress("Requesting a network lease with DHCP");
            uint8_t dhcp_discover[320];
            const uint32_t dhcp_transaction = 0x4D414B4Fu;
            const size_t dhcp_length = network_build_dhcp_discover(
                dhcp_discover, sizeof(dhcp_discover), dhcp_transaction);
            if (dhcp_length == 0u ||
                !e1000_transmit(dhcp_discover, dhcp_length))
                boot_fatal("E1000 could not submit DHCP Discover");
            struct network_dhcp_offer offer;
            bool offered = false;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                if (network_dhcp_offer(dhcp_transaction, &offer)) {
                    offered = true;
                    break;
                }
                __asm__ volatile ("pause");
            }
            if (!offered)
                boot_fatal("DHCP Discover transmitted but no Offer arrived");
            serial_write("DHCP_REAL_OFFER_OK address=");
            serial_write_u64((offer.address >> 24u) & 0xFFu); serial_write(".");
            serial_write_u64((offer.address >> 16u) & 0xFFu); serial_write(".");
            serial_write_u64((offer.address >> 8u) & 0xFFu); serial_write(".");
            serial_write_u64(offer.address & 0xFFu);
            serial_write(" gateway=");
            serial_write_hex(offer.gateway);
            serial_write(" dns=");
            serial_write_hex(offer.dns);
            serial_write(" server=");
            serial_write_hex(offer.server);
            serial_write("\n");
            uint8_t dhcp_request[320];
            const size_t request_length = network_build_dhcp_request(
                dhcp_request, sizeof(dhcp_request), dhcp_transaction,
                offer.address, offer.server);
            if (request_length == 0u ||
                !e1000_transmit(dhcp_request, request_length))
                boot_fatal("E1000 could not submit DHCP Request");
            struct network_dhcp_offer lease;
            bool acknowledged = false;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                if (network_dhcp_ack(dhcp_transaction, &lease)) {
                    acknowledged = true;
                    break;
                }
                __asm__ volatile ("pause");
            }
            if (!acknowledged)
                boot_fatal("DHCP Request transmitted but no ACK arrived");
            network_set_ipv4(lease.address);
            serial_write("DHCP_REAL_ACK_OK address=");
            serial_write_u64((lease.address >> 24u) & 0xFFu); serial_write(".");
            serial_write_u64((lease.address >> 16u) & 0xFFu); serial_write(".");
            serial_write_u64((lease.address >> 8u) & 0xFFu); serial_write(".");
            serial_write_u64(lease.address & 0xFFu);
            serial_write(" gateway=");
            serial_write_hex(lease.gateway);
            serial_write(" dns=");
            serial_write_hex(lease.dns);
            serial_write("\n");
            boot_status("DHCP", "lease acknowledged and IPv4 address configured");
            if (lease.gateway == 0u)
                boot_fatal("DHCP ACK did not provide a gateway");
            uint8_t arp_request[60];
            const size_t arp_length = network_build_arp_request(
                arp_request, sizeof(arp_request), lease.gateway);
            if (arp_length == 0u || !e1000_transmit(arp_request, arp_length))
                boot_fatal("E1000 could not submit gateway ARP request");
            uint8_t gateway_mac[6];
            bool gateway_resolved = false;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                if (network_arp_lookup(lease.gateway, gateway_mac)) {
                    gateway_resolved = true;
                    break;
                }
                __asm__ volatile ("pause");
            }
            if (!gateway_resolved)
                boot_fatal("E1000 transmitted ARP but received no gateway reply");
            network_set_route(lease.gateway, lease.dns, gateway_mac);
            serial_write("E1000_REAL_ARP_OK gateway=");
            serial_write_hex(lease.gateway);
            serial_write(" mac=");
            for (size_t byte = 0u; byte < 6u; ++byte) {
                if (byte != 0u) serial_write(":");
                const char digits[] = "0123456789ABCDEF";
                char pair[3] = {digits[gateway_mac[byte] >> 4u],
                                digits[gateway_mac[byte] & 0x0Fu], '\0'};
                serial_write(pair);
            }
            serial_write(" tx=");
            serial_write_u64(e1000_transmitted_frames());
            serial_write(" rx=");
            serial_write_u64(e1000_received_frames());
            serial_write("\n");
            boot_status("Network route", "gateway resolved through ARP");
            if (lease.dns == 0u)
                boot_fatal("DHCP ACK did not provide a DNS server");
            uint8_t dns_query[320];
            const uint16_t dns_transaction = 0xD305u;
            const size_t dns_length = network_build_dns_query(
                dns_query, sizeof(dns_query), gateway_mac, lease.dns,
                dns_transaction, "example.com");
            if (dns_length == 0u || !e1000_transmit(dns_query, dns_length))
                boot_fatal("E1000 could not submit DNS query");
            struct network_dns_answer dns_answer;
            bool dns_resolved = false;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                if (network_dns_answer(dns_transaction, &dns_answer)) {
                    dns_resolved = true;
                    break;
                }
                __asm__ volatile ("pause");
            }
            if (!dns_resolved)
                boot_fatal("DNS query transmitted but no A answer arrived");
            serial_write("DNS_REAL_QUERY_OK host=example.com address=");
            serial_write_u64((dns_answer.address >> 24u) & 0xFFu);
            serial_write(".");
            serial_write_u64((dns_answer.address >> 16u) & 0xFFu);
            serial_write(".");
            serial_write_u64((dns_answer.address >> 8u) & 0xFFu);
            serial_write(".");
            serial_write_u64(dns_answer.address & 0xFFu);
            serial_write(" ttl=");
            serial_write_u64(dns_answer.ttl_seconds);
            serial_write("\n");
            boot_status("DNS", "example.com resolved through configured server");
            uint8_t tcp_syn[60];
            const uint16_t tcp_local_port = 49153u;
            const uint16_t tcp_remote_port = 80u;
            const uint32_t tcp_initial_sequence = 0x44454D4Fu;
            const size_t tcp_syn_length = network_build_tcp_syn(
                tcp_syn, sizeof(tcp_syn), gateway_mac, dns_answer.address,
                tcp_local_port, tcp_remote_port, tcp_initial_sequence);
            if (tcp_syn_length == 0u || !e1000_transmit(tcp_syn, tcp_syn_length))
                boot_fatal("E1000 could not submit TCP SYN");
            struct network_tcp_syn_ack tcp_syn_ack;
            bool tcp_answered = false;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                if (network_tcp_syn_ack(dns_answer.address, tcp_local_port,
                                        tcp_remote_port, tcp_initial_sequence,
                                        &tcp_syn_ack)) {
                    tcp_answered = true;
                    break;
                }
                __asm__ volatile ("pause");
            }
            if (!tcp_answered)
                boot_fatal("TCP SYN transmitted but no SYN-ACK arrived");
            uint8_t tcp_ack[60];
            const size_t tcp_ack_length = network_build_tcp_ack(
                tcp_ack, sizeof(tcp_ack), gateway_mac, dns_answer.address,
                tcp_local_port, tcp_remote_port, tcp_initial_sequence + 1u,
                tcp_syn_ack.remote_sequence + 1u);
            if (tcp_ack_length == 0u ||
                !e1000_transmit(tcp_ack, tcp_ack_length))
                boot_fatal("E1000 could not submit final TCP ACK");
            serial_write("TCP_REAL_HANDSHAKE_OK remote=");
            serial_write_u64((dns_answer.address >> 24u) & 0xFFu);
            serial_write(".");
            serial_write_u64((dns_answer.address >> 16u) & 0xFFu);
            serial_write(".");
            serial_write_u64((dns_answer.address >> 8u) & 0xFFu);
            serial_write(".");
            serial_write_u64(dns_answer.address & 0xFFu);
            serial_write(":80 window=");
            serial_write_u64(tcp_syn_ack.receive_window);
            serial_write("\n");
            boot_status("TCP", "real three-way handshake completed");
            static const uint8_t http_request[] =
                "GET / HTTP/1.0\r\n"
                "Host: example.com\r\n"
                "User-Agent: DemonWeb/0.1\r\n"
                "Connection: close\r\n\r\n";
            uint8_t http_frame[256];
            const size_t http_request_length = sizeof(http_request) - 1u;
            const uint32_t http_local_sequence = tcp_initial_sequence + 1u;
            const uint32_t http_remote_sequence =
                tcp_syn_ack.remote_sequence + 1u;
            const size_t http_frame_length = network_build_tcp_data(
                http_frame, sizeof(http_frame), gateway_mac, dns_answer.address,
                tcp_local_port, tcp_remote_port, http_local_sequence,
                http_remote_sequence, http_request, http_request_length);
            if (http_frame_length == 0u ||
                !e1000_transmit(http_frame, http_frame_length))
                boot_fatal("E1000 could not submit HTTP request");
            uint8_t http_response[1024];
            uint32_t http_remote_next = 0u;
            size_t http_response_length = 0u;
            for (size_t wait = 0u; wait < 50000000u; ++wait) {
                (void)e1000_poll();
                http_response_length = network_tcp_receive(
                    dns_answer.address, tcp_local_port, tcp_remote_port,
                    http_response, sizeof(http_response), &http_remote_next);
                if (http_response_length != 0u) break;
                __asm__ volatile ("pause");
            }
            if (http_response_length < 12u ||
                http_response[0] != 'H' || http_response[1] != 'T' ||
                http_response[2] != 'T' || http_response[3] != 'P' ||
                http_response[4] != '/')
                boot_fatal("HTTP server returned no valid status line");
            network_publish_http_response(http_response, http_response_length);
            uint8_t http_ack[60];
            const size_t http_ack_length = network_build_tcp_ack(
                http_ack, sizeof(http_ack), gateway_mac, dns_answer.address,
                tcp_local_port, tcp_remote_port,
                http_local_sequence + (uint32_t)http_request_length,
                http_remote_next);
            if (http_ack_length == 0u ||
                !e1000_transmit(http_ack, http_ack_length))
                boot_fatal("E1000 could not acknowledge HTTP response");
            serial_write("HTTP_REAL_RESPONSE_OK bytes=");
            serial_write_u64(http_response_length);
            serial_write(" status=\"");
            for (size_t index = 0u; index < http_response_length &&
                 index < 64u && http_response[index] != '\r' &&
                 http_response[index] != '\n'; ++index) {
                char character[2] = {(char)http_response[index], '\0'};
                serial_write(character);
            }
            serial_write("\"\n");
            boot_status("HTTP", "real response received and validated");
        }
    } else {
        serial_write("PCI_ETHERNET_UNAVAILABLE\n");
        boot_status("PCI network", "no Ethernet controller exposed");
    }
    struct pci_device ahci_controller;
    /* class 1 (mass storage), subclass 6 (SATA) -- programming_interface 1
       (AHCI) is the norm for anything exposing this class/subclass at all,
       so it isn't re-checked here the same way E1000's exact vendor/device
       pair is. Entirely optional: a machine with no SATA controller, or an
       AHCI controller with nothing plugged into any port, just boots
       without a disk exactly like it boots without a NIC above -- never
       boot_fatal for absence, only for the driver itself misbehaving once
       a live port was actually found. */
    if (pci_find_class(1u, 6u, 0u, &ahci_controller)) {
        serial_write("PCI_AHCI_FOUND vendor=");
        serial_write_hex(ahci_controller.vendor_id);
        serial_write(" device=");
        serial_write_hex(ahci_controller.device_id);
        serial_write(" bdf=");
        serial_write_u64(ahci_controller.bus);
        serial_write(":");
        serial_write_u64(ahci_controller.slot);
        serial_write(".");
        serial_write_u64(ahci_controller.function);
        serial_write("\n");
        const uint64_t ahci_mmio = ahci_controller.bars[5] & ~0xFull;
        if (ahci_mmio != 0u &&
            map_mmio_identity_2m(pdpt, pd, framebuffer_pd, ahci_mmio, 0x20000u)) {
            write_cr3(pml4);
            if (!ahci_probe(&ahci_controller, (uintptr_t)ahci_mmio)) {
                serial_write("AHCI_NO_LIVE_PORT\n");
                boot_status("AHCI", "controller present, no drive detected on any port");
            } else {
                size_t ahci_dma_capacity = 0u;
                const uint64_t ahci_dma = allocate_contiguous(
                    AHCI_DMA_ARENA_BYTES, &ahci_dma_capacity);
                if (ahci_dma == 0u ||
                    !ahci_start((uintptr_t)ahci_dma, ahci_dma_capacity)) {
                    serial_write("AHCI_START_FAILED\n");
                    boot_status("AHCI", "port found but command-list/IDENTIFY setup failed");
                } else {
                    serial_write("AHCI_DEVICE_READY mmio=");
                    serial_write_hex(ahci_mmio_base());
                    serial_write(" sectors=");
                    serial_write_u64(ahci_sector_count());
                    serial_write("\n");
                    boot_status("AHCI", "SATA disk ready for sector read/write");
                }
            }
        } else {
            serial_write("AHCI_MMIO_MAP_FAILED\n");
            boot_status("AHCI", "controller present, BAR5 could not be mapped");
        }
    } else {
        serial_write("PCI_AHCI_UNAVAILABLE\n");
        boot_status("AHCI", "no SATA/AHCI controller exposed");
    }
    boot_progress("Checking ISO modules and preinstalled project files");
    if (install_multiboot_projects(multiboot_info) < 9u)
        boot_fatal("MKO repository/SDK/starter modules are missing from the ISO");
    serial_write("MKO_SYSTEM_PREINSTALLED\n");
    boot_status("MKO system", "MAKO manifest + SDK + starter installed from ISO");
    if (!input_self_test()) boot_fatal("Unified input event queue self-test failed");
    if (framebuffer_available()) {
        const struct framebuffer_info *display = framebuffer_get_info();
        mouse_set_bounds(display->width, display->height);
    }
    boot_progress("Starting timer, keyboard, and mouse hardware IRQs");
    if (!interrupts_hardware_start())
        boot_fatal("PIT timer IRQ did not arrive");
    serial_write("timer ticks: ");
    serial_write_u64(interrupts_timer_ticks());
    serial_write("\nHARDWARE_IRQ_SELF_TEST_OK\n");
    serial_write("KEYBOARD_INPUT_READY\n");
    serial_write(mouse_controller_ready() ? "MOUSE_INPUT_READY\n" : "MOUSE_INPUT_UNAVAILABLE\n");
    serial_write("UNIFIED_INPUT_ABI_OK\n");
    boot_status("Hardware IRQs", mouse_controller_ready() ?
        "PIC, PIT, keyboard, and PS/2 mouse enabled" :
        "PIC, PIT, and keyboard enabled; mouse unavailable");
    boot_progress("Allocating isolated userspace process pools");
    struct userspace_memory processes[SCHEDULER_PROCESS_LIMIT - 1u];
    size_t code_page_total = 0u;
    for (size_t process = 0u; process < SCHEDULER_PROCESS_LIMIT - 1u; ++process) {
        size_t code_pages = 12u;
        if (process < 2u) code_pages = 4u;
        if (process == 2u) code_pages = USERSPACE_CODE_PAGES;
        if (!allocate_process_memory(&processes[process], pdpt, code_pages))
            boot_fatal("Dynamic process frame-pool allocation failed");
        code_page_total += code_pages;
    }
    if (code_page_total != 104u)
        boot_fatal("Userspace executable pool budget changed unexpectedly");
    serial_write("USERSPACE_CODE_POOLS_OK pages=");
    serial_write_u64(code_page_total);
    serial_write(" max="); serial_write_u64(USERSPACE_CODE_PAGES);
    serial_write(" heap=0x330000\n");
    size_t surface_capacity = 0u;
    const uint64_t surface_arena = allocate_contiguous(SURFACE_ARENA_BYTES,
                                                        &surface_capacity);
    if (surface_arena == 0u || surface_arena + surface_capacity > 0x40000000u ||
        !surfaces_init((uint32_t *)(uintptr_t)surface_arena, surface_capacity))
        boot_fatal("Shared surface arena allocation failed");
    serial_write("SURFACE_ARENA_READY bytes=");
    serial_write_u64(surface_capacity);
    serial_write(" base="); serial_write_hex(surface_arena);
    serial_write(" end="); serial_write_hex(surface_arena + surface_capacity);
    serial_write("\n");
    if (!userspace_init(processes, (uintptr_t)&frames))
        boot_fatal("Ring-3 address-space setup failed");
    serial_write("ELF64_MKO_LOAD_OK\n");
    boot_status("Application loader", "ELF64 image with native MKO code loaded");
    const bool userspace_completed = userspace_run_init();
    serial_write("userspace exit: ");
    serial_write_u64(userspace_exit_code());
    serial_write(" yields: ");
    serial_write_u64(userspace_yield_count());
    serial_write("\n");
    for (size_t task_index = 1u; task_index < scheduler_task_count(); ++task_index) {
        struct scheduler_task_snapshot task;
        if (scheduler_snapshot(task_index, &task)) {
            serial_write("task exit pid=");
            serial_write_u64(task.pid);
            serial_write(" status=");
            serial_write_u64(task.exit_code);
            serial_write("\n");
        }
    }
    serial_write("project store reads=");
    serial_write_u64(ramfs_reads());
    serial_write(" writes=");
    serial_write_u64(ramfs_writes());
    serial_write(" bytes=");
    serial_write_u64(ramfs_bytes_used());
    serial_write("\n");
    if (!userspace_completed || userspace_exit_code() != 41u ||
        userspace_yield_count() == 0u)
        boot_fatal("Ring-3 program/syscall contract failed");
    if (ipc_messages_sent() == 0u || ipc_messages_received() == 0u ||
        ipc_messages_dropped() != 0u)
        boot_fatal("Blocking userspace IPC delivery failed");
    serial_write("IPC_BLOCKING_USERSPACE_OK\n");
    serial_write("\nUSERSPACE_SYSCALLS_OK\n");
    boot_status("User mode", "two ring-3 tasks + syscall ABI passed");
    if (!capabilities_self_test())
        boot_fatal("Capability ownership/rights lifecycle failed");
    serial_write("CAPABILITY_ABI_OK\n");
    boot_status("Capabilities", "owned service handles + rights passed");
    if (!kernel_cxx_self_test())
        boot_fatal("Kernel-side C++ runtime allocator/vtable self-test failed");
    serial_write("KERNEL_CXX_RUNTIME_OK\n");
    boot_status("Kernel C++ runtime", "new/delete + vtable + destructor passed (K0)");
    if (!kernel_bounded_table_self_test())
        boot_fatal("Kernel bounded_table<T,N> template self-test failed");
    serial_write("KERNEL_BOUNDED_TABLE_OK\n");
    boot_status("Kernel bounded_table<T,N>", "open/lookup/release/capacity passed (K1)");
    if (!kernel_slot_table_self_test())
        boot_fatal("Kernel slot_table<T,N> template self-test failed");
    serial_write("KERNEL_SLOT_TABLE_OK\n");
    boot_status("Kernel slot_table<T,N>", "allocate/generation/release/capacity passed (K3)");
    if (!ramfs_self_test())
        boot_fatal("RAM project store write/read contract failed");
    serial_write("PROJECT_STORE_OK\n");
    boot_status("Project store", "RAM file persisted across processes");
    if (!scheduler_self_test())
        boot_fatal("Scheduler task lifecycle/quantum test failed");
    serial_write("PROCESS_ISOLATION_OK\n");
    serial_write("scheduler dispatches: ");
    serial_write_u64(scheduler_dispatches());
    serial_write("\nSCHEDULER_SELF_TEST_OK\n");
    boot_status("Scheduler", "PIT preemption + context switching passed");
    const uint32_t spawned_pid = userspace_spawn_path(0u, "/projects/hello/main.elf", 24u,
        "hello", CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
                 CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS));
    if (spawned_pid == 0u || !userspace_run_init())
        boot_fatal("Dynamic ELF64 spawn from RAMFS failed");
    uint64_t spawned_status = UINT64_MAX;
    if (!scheduler_reap(0u, spawned_pid, &spawned_status) || spawned_status != 0u)
        boot_fatal("Dynamic process wait/cleanup failed");
    serial_write("DYNAMIC_SPAWN_OK pid="); serial_write_u64(spawned_pid);
    serial_write(" status="); serial_write_u64(spawned_status); serial_write("\n");
    serial_write("PROCESS_WAIT_CLEANUP_OK\n");
    boot_status("Process manager", "RAMFS spawn, PID/PPID, wait, cleanup, and policy passed");
    /* Wave 0 of the EDE port (Desktop/EDE/ede-2.1, docs/c-apps.md): proves a
       real C++ MAKO-ABI process -- heap allocation through cxx_runtime.cpp's
       bump arena, a vtable call through a virtual method, a clean destructor
       -- actually runs, not just links. Spawned and reaped exactly like the
       hello.elf dynamic-spawn test above; a future EDE-derived component
       reuses this same proof, not a new one. */
    const uint32_t cxx_hello_pid = userspace_spawn_path(0u, "/system/bin/cxx-hello.elf", 25u,
        "cxx-hello", CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
                     CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS));
    if (cxx_hello_pid == 0u || !userspace_run_init())
        boot_fatal("C++ runtime proof-of-concept spawn failed");
    uint64_t cxx_hello_status = UINT64_MAX;
    if (!scheduler_reap(0u, cxx_hello_pid, &cxx_hello_status) || cxx_hello_status != 0u)
        boot_fatal("C++ runtime proof-of-concept did not exit cleanly");
    serial_write("CXX_RUNTIME_SPAWN_OK pid="); serial_write_u64(cxx_hello_pid);
    serial_write(" status="); serial_write_u64(cxx_hello_status); serial_write("\n");
    boot_status("C++ runtime (Wave 0)", "heap alloc, vtable dispatch, and clean exit passed");
    const uint32_t portcheck_pid = userspace_spawn_path(0u,
        "/system/bin/portcheck.elf", 25u, "portcheck",
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_INPUT));
    if (portcheck_pid == 0u || !userspace_run_init())
        boot_fatal("Native PortKit anonymous-memory test failed to start");
    uint64_t portcheck_status = UINT64_MAX;
    const bool portcheck_reaped = scheduler_reap(0u, portcheck_pid, &portcheck_status);
    if (!portcheck_reaped || portcheck_status != 0u)
        boot_fatal("Native PortKit anonymous-memory test failed");
    serial_write("PORTKIT_RING3_OK pid="); serial_write_u64(portcheck_pid);
    serial_write(" status="); serial_write_u64(portcheck_status);
    serial_write("\n");
    boot_status("Native port runtime", "dynamic heap, reuse, files, timing, and input ABI passed");
    /* The ClassiCube core runs a full render/input smoke that exits itself, so
       the kernel can reap it and assert an exit status on the scripted smoke
       ISO. On the interactive ISO the same executable becomes a persistent
       game launched from MakoBox ("classicube"), so it must not be spawned
       here or the boot would block until the player quits. Mirrors the
       doom-full gate below. */
    if (test_mode || doom_frame_test) {
        const uint32_t classicube_core_pid = userspace_spawn_path(0u,
            "/system/bin/classicube-core.elf", 31u, "classicube-core",
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE));
        if (classicube_core_pid == 0u || !userspace_run_init())
            boot_fatal("ClassiCube D1 core failed to start");
        uint64_t classicube_core_status = UINT64_MAX;
        const bool classicube_core_reaped =
            scheduler_reap(0u, classicube_core_pid, &classicube_core_status);
        serial_write("CLASSICUBE_CORE_EXIT reaped=");
        serial_write_u64(classicube_core_reaped ? 1u : 0u);
        serial_write(" status="); serial_write_u64(classicube_core_status);
        serial_write("\n");
        if (!classicube_core_reaped || classicube_core_status != 0u)
            boot_fatal("ClassiCube D1 core did not exit cleanly");
        serial_write("CLASSICUBE_CORE_RING3_OK pid=");
        serial_write_u64(classicube_core_pid);
        serial_write(" status="); serial_write_u64(classicube_core_status);
        serial_write("\n");
        boot_status("ClassiCube port D1", "upstream C core executed in ring 3");
    }
    const uint32_t doom_pid = userspace_spawn_path(0u,
        "/system/bin/doom.elf", 20u, "doom-version",
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS));
    if (doom_pid == 0u || !userspace_run_init())
        boot_fatal("Doom D0 executable failed to start");
    uint64_t doom_status = UINT64_MAX;
    if (!scheduler_reap(0u, doom_pid, &doom_status) || doom_status != 0u)
        boot_fatal("Doom D0 executable did not exit cleanly");
    serial_write("DOOM_VERSION_RING3_OK pid="); serial_write_u64(doom_pid);
    serial_write(" status="); serial_write_u64(doom_status); serial_write("\n");
    boot_status("Doom engine D0", "native version executable exited cleanly");
    if (test_mode || doom_frame_test) {
        const uint32_t doom_full_pid = userspace_spawn_path(0u,
            "/system/bin/doom-full.elf", 25u, "doom-full",
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_INPUT) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_AUDIO));
        if (doom_full_pid == 0u || !userspace_run_init())
            boot_fatal("Full Doom image failed to map or start");
        if (doom_frame_test) {
            serial_write("DOOM_FULL_INTERACTIVE_RUNNING pid=");
            serial_write_u64(doom_full_pid); serial_write("\n");
            for (;;) __asm__ volatile ("hlt");
        }
        uint64_t doom_full_status = UINT64_MAX;
        if (!scheduler_reap(0u, doom_full_pid, &doom_full_status) ||
            doom_full_status != 0u)
            boot_fatal("Full Doom image did not exit cleanly");
        serial_write("DOOM_FULL_RING3_OK pid="); serial_write_u64(doom_full_pid);
        serial_write(" status="); serial_write_u64(doom_full_status); serial_write("\n");
        boot_status("Doom large image", "149 executable pages mapped and reclaimed");
    }
    /* Re-wired desktop stack (un-sidelined -- see sidelined/README.md): the
       ring-3 Mako compositor, the DemonX X11 server, the DemonWM window
       manager, and the xterm-style terminal client. Interactive boots spawn
       the full session and hand off to the desktop session loop; scripted
       boot-test boots run a lean end-to-end smoke (compositor + DemonX +
       xterm) that replaces the old ~1000-line click/drag choreography -- the
       xterm injects its own typed session and asserts its buffer, so the
       whole key->compositor->DemonX->xlib->window wire path is exercised
       without the brittle scripted choreography. */
    uint32_t compositor_pid = 0u;
    /* Not every ISO this kernel boots carries the desktop stack -- Quake/
       Doom/NXEngine's own play-mode images have a real framebuffer (they
       need one for their own rendering) but never module2 in compositor.elf/
       demonx.elf/demonwm.elf/xterm.elf, since they're built by separate,
       narrower Makefile targets. Only attempt the desktop stack when the
       compositor binary is actually present *and* explicitly requested via
       the "desktop" cmdline needle (grub-desktop.cfg / multiboot_desktop_mode) --
       otherwise fall back to the plain console path exactly as before.
       Presence-only gating silently replaced the plain mako# console every
       other port's play command (cave-story, quake, doom) runs from the
       moment compositor.elf was added to an ISO's module list, which is
       not what an interactive `make run` session should do by default. */
    uint32_t compositor_probe_id;
    const bool desktop_stack_present = framebuffer_available() &&
        multiboot_desktop_mode(multiboot_info) &&
        ramfs_open("/system/bin/compositor.elf", 26u, false, &compositor_probe_id);
    if (desktop_stack_present) {
        if (capability_open(1u, CAPABILITY_SERVICE_DISPLAY) != UINT64_MAX ||
            capability_open(2u, CAPABILITY_SERVICE_INPUT) != UINT64_MAX)
            boot_fatal("Display/input authority leaked to bootstrap applications");
        compositor_pid = userspace_spawn_path(0u,
            "/system/bin/compositor.elf", 26u, "compositor",
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_INPUT) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE));
        if (compositor_pid == 0u)
            boot_fatal("Ring-3 compositor launch failed");
        (void)userspace_run_init();
        struct scheduler_task_snapshot compositor_state;
        if (!scheduler_snapshot(compositor_pid, &compositor_state) ||
            compositor_state.state != SCHEDULER_TASK_BLOCKED) {
            serial_write("compositor startup state=");
            serial_write(scheduler_state_name(compositor_state.state));
            serial_write(" status=");
            serial_write_u64(compositor_state.exit_code);
            serial_write("\n");
            boot_fatal("Compositor service did not reach receive-ready state");
        }
        serial_write("COMPOSITOR_SERVICE_READY\n");
        const uint32_t demonx_pid = userspace_spawn_path(0u,
            "/system/bin/demonx.elf", 22u, "demonx",
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE));
        if (demonx_pid == 0u)
            boot_fatal("DemonX server failed to spawn");
        (void)userspace_run_init();
        struct scheduler_task_snapshot demonx_state;
        if (!scheduler_snapshot(demonx_pid, &demonx_state) ||
            demonx_state.state != SCHEDULER_TASK_BLOCKED) {
            serial_write("demonx startup state=");
            serial_write(scheduler_state_name(demonx_state.state));
            serial_write(" status=");
            serial_write_u64(demonx_state.exit_code);
            serial_write("\n");
            boot_fatal("DemonX server did not reach receive-ready state");
        }
        serial_write("DEMONX_SERVER_READY transport=capability-ipc protocol=X11\n");
        if (!test_mode && !doom_frame_test) {
            const uint32_t demonwm_pid = userspace_spawn_path(0u,
                "/system/bin/demonwm.elf", 23u, "demonwm",
                CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC));
            if (demonwm_pid == 0u)
                boot_fatal("Interactive DemonWM launch failed");
            (void)userspace_run_init();
            struct scheduler_task_snapshot demonwm_state;
            if (!scheduler_snapshot(demonwm_pid, &demonwm_state) ||
                demonwm_state.state != SCHEDULER_TASK_BLOCKED) {
                serial_write("DEMONWM_STARTUP_FAILED state=");
                serial_write(scheduler_state_name(demonwm_state.state));
                serial_write(" exit=");
                serial_write_u64(demonwm_state.exit_code);
                serial_write("\n");
                boot_fatal("DemonWM did not claim DemonX and enter its event loop");
            }
            const uint32_t xterm_pid = userspace_spawn_path(0u,
                "/system/bin/xterm.elf", 21u, "xterm",
                CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC) |
                CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE));
            if (xterm_pid == 0u)
                boot_fatal("Interactive xterm launch failed");
            (void)userspace_run_init();
            serial_write("DEMONWM_SESSION_READY pid=");
            serial_write_u64(demonwm_pid);
            serial_write(" display=:2 wm=click-focus,raise,drag\n");
            desktop_session_loop(compositor_pid);
        } else {
            const uint32_t xterm_pid = userspace_spawn_path(0u,
                "/system/bin/xterm.elf", 21u, "xterm-smoke",
                CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC) |
                CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE));
            if (xterm_pid == 0u || !userspace_run_init())
                boot_fatal("xterm boot-test smoke failed to start");
            uint64_t xterm_status = UINT64_MAX;
            if (!scheduler_reap(0u, xterm_pid, &xterm_status) ||
                xterm_status != 0u) {
                serial_write("xterm smoke status=");
                serial_write_u64(xterm_status);
                serial_write("\n");
                boot_fatal("xterm boot-test smoke failed");
            }
            serial_write("XTERM_RING3_OK pid=");
            serial_write_u64(xterm_pid);
            serial_write(" status=");
            serial_write_u64(xterm_status);
            serial_write("\n");
            struct scheduler_task_snapshot demonx_smoke_state;
            if (!scheduler_snapshot(demonx_pid, &demonx_smoke_state) ||
                demonx_smoke_state.state == SCHEDULER_TASK_EXITED) {
                serial_write("demonx state after smoke=");
                serial_write(scheduler_state_name(demonx_smoke_state.state));
                serial_write(" exit=");
                serial_write_u64(demonx_smoke_state.exit_code);
                serial_write("\n");
                boot_fatal("DemonX died when its client disconnected");
            }
            serial_write("DEMONX_LIVE_AFTER_SMOKE state=blocked\n");
            serial_write("DESKTOP_STACK_SMOKE_OK compositor+demonx+xterm\n");
        }
    }
    if (!ipc_self_test()) boot_fatal("Capability IPC channel lifecycle failed");
    serial_write("IPC_CHANNEL_SELF_TEST_OK\n");
    serial_write("IPC_NAMED_SERVICE_OK\n");
    boot_status("IPC", "named capability channels + bounded messages passed");

    const uint64_t add_result = mako_add(20u, 22u);
    const uint64_t mix_result = mako_mix(200u, 7u);
    const uint64_t signed_less_result = mako_signed_less(-1, 1);
    const int64_t signed_div_result = mako_signed_div(-9, 2);
    serial_write("MKO mako_add(20, 22): ");
    serial_write_u64(add_result);
    serial_write("\nMKO mako_mix(200, 7): ");
    serial_write_u64(mix_result);
    serial_write("\nMKO signed backend checks: ");
    serial_write(signed_less_result == 1u && signed_div_result == -4 ? "pass" : "fail");
    serial_write("\n");
    if (add_result != 42u || mix_result != 2511u ||
        signed_less_result != 1u || signed_div_result != -4) {
        boot_fatal("Native MKO arithmetic result mismatch");
    }
    serial_write("MKO_NATIVE_OK\n");
    boot_status("MAKO native backend", "typed MIR -> x86_64 online");

    if (!framebuffer_available()) {
        const uint64_t vga_row = 24u * 80u + 76u;
        mako_vga_cell(vga_row + 0u, 'M', 0x0du);
        mako_vga_cell(vga_row + 1u, 'K', 0x0du);
        mako_vga_cell(vga_row + 2u, 'O', 0x0du);
        mako_vga_cell(vga_row + 3u, '!', 0x0eu);
        if (mako_vga_read(vga_row + 0u) != ((uint16_t)0x0du << 8u | 'M') ||
            mako_vga_read(vga_row + 1u) != ((uint16_t)0x0du << 8u | 'K') ||
            mako_vga_read(vga_row + 2u) != ((uint16_t)0x0du << 8u | 'O') ||
            mako_vga_read(vga_row + 3u) != ((uint16_t)0x0eu << 8u | '!'))
            boot_fatal("MKO VGA write/readback mismatch");
        for (uint64_t cell = 0u; cell < 4u; ++cell)
            mako_vga_cell(vga_row + cell, ' ', 0x0Fu);
        for (uint64_t cell = 0u; cell < 4u; ++cell)
            if (mako_vga_read(vga_row + cell) != ((uint16_t)0x0Fu << 8u | ' '))
                boot_fatal("MKO VGA test-pattern cleanup failed");
        serial_write("MKO_VGA_WRITE_OK\n");
        serial_write("VGA_RENDER_CLEAN_OK\n");
        boot_status("Device memory", "typed vptr VGA write passed");
    } else {
        serial_write("MKO_VGA_SKIPPED_LINEAR_FRAMEBUFFER\n");
        boot_status("Device memory", "linear framebuffer read/write test passed");
    }
    serial_write("DISPLAY_DEVICE_TEST_OK\n");

    init_system_init();
    runas_init();
    apps_init();
    git_init();
    if (!init_system_reach_boot_target(compositor_pid) || !init_system_self_test() ||
        !runas_self_test() || !apps_self_test() || !git_self_test())
        boot_fatal("Init, runas, apps, or Git foundation self-test failed");
    serial_write("MAKO_INIT_SYSTEM_OK\n");
    serial_write("RUNAS_POLICY_OK\n");
    serial_write("APP_REGISTRY_OK\n");
    serial_write("C_APP_TETRIS_READY input=keyboard runtime=MAKO-ABI\n");
    serial_write("GIT_WORKTREE_OK\n");
    if (compositor_pid != 0u) {
        serial_write("DESKTOP_TARGET_ACTIVE pid=");
        serial_write_u64(compositor_pid);
        serial_write(" state=blocked\n");
        boot_status("Init system", "10 units reached native desktop.target");
    } else {
        serial_write("CONSOLE_TARGET_OK target=console.target\n");
        boot_status("Init system", "8 units reached console default.target");
    }
    boot_status("runas policy", "local-console administrative allowlist active");

    const struct makobox_state box_state = {
        .usable_memory_bytes = usable_bytes,
        .frame_next = frames.next,
        .frame_end = frames.end,
        .paging_root = read_cr3(),
        .kernel_size_bytes = (uintptr_t)kernel_end - (uintptr_t)kernel_start,
        .idt_ready = true,
        .native_mko_ready = true,
        .app_runtime_ready = true,
    };
    makobox_init(&box_state);
    terminal_set_output(false);
    const bool makobox_ok = makobox_self_test();
    terminal_set_output(true);
    if (!makobox_ok) boot_fatal("MakoBox dispatcher self-test failed");
    serial_write("MAKOBOX_SELF_TEST_OK\n");
    boot_status("MakoBox", "init/runit/runas applets ready");
    serial_write("KERNEL_BOOT_OK\n");
    // framebuffer_self_test() (run earlier, during core subsystem checks)
    // legitimately exercises the cursor overlay and leaves it sitting
    // visible mid-screen when it's done -- fine for what it was testing,
    // but a plain TTY console has no pointer to show, so hide it before
    // the console's own framebuffer mirroring takes over.
    framebuffer_cursor_hide();
    terminal_graphical_enable();
    boot_welcome();
    makobox_shell();
}

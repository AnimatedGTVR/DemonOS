#include <kernel/makobox.h>
#include <kernel/apps.h>
#include <kernel/capability.h>
#include <kernel/git.h>
#include <kernel/framebuffer.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/ipc.h>
#include <kernel/ramfs.h>
#include <kernel/runas.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/terminal.h>
#include <kernel/userspace.h>
#include <demon/input.h>

#include <stddef.h>
#include <stdint.h>

static struct makobox_state live;

static void cpu_vendor(char output[13]) {
    uint32_t unused;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile ("cpuid"
        : "=a"(unused), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0u));
    const uint32_t words[3] = { ebx, edx, ecx };
    for (unsigned word = 0; word < 3u; ++word)
        for (unsigned byte = 0; byte < 4u; ++byte)
            output[word * 4u + byte] = (char)(words[word] >> (byte * 8u));
    output[12] = '\0';
}

static bool equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

// True if `text` begins with `prefix`; on success `*rest` points just past
// it (e.g. starts_with("ls /system", "ls ", &rest) -> rest == "/system").
static bool starts_with(const char *text, const char *prefix, const char **rest) {
    while (*prefix != '\0') {
        if (*text != *prefix) return false;
        ++text;
        ++prefix;
    }
    if (rest != NULL) *rest = text;
    return true;
}

static size_t string_length(const char *text) {
    size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static void line(const char *text) {
    serial_write(text);
    serial_write("\n");
    terminal_write_line(text);
}

static void value_line(const char *label, uint64_t value, const char *suffix) {
    serial_write(label);
    serial_write_u64(value);
    if (suffix != NULL) serial_write(suffix);
    serial_write("\n");
    terminal_write(label);
    terminal_write_u64(value);
    if (suffix != NULL) terminal_write(suffix);
    terminal_write_line("");
}

static void hex_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_hex(value);
    serial_write("\n");
    terminal_write(label);
    terminal_write_hex(value);
    terminal_write_line("");
}

static void applet_help(void) {
    line("MakoBox applets:");
    line("  help     list available applets");
    line("  uname    identify the running kernel");
    line("  status   summarize initialized subsystems");
    line("  mem      show usable physical memory");
    line("  frames   show physical-frame allocator state");
    line("  paging   show the active page-table root");
    line("  ticks    show live PIT timer ticks");
    line("  ps       show kernel task scheduler state");
    line("  abi      show the portable application contract");
    line("  caps     show capability service statistics");
    line("  projects show the RAM-backed project store");
    line("  apps     list and inspect installed applications");
    line("  tetris   launch the native freestanding C Tetris app");
    line("  git      lightweight project worktree/commit status");
    line("  desktop  report graphical desktop readiness");
    line("  systemctl list/status units (mutations require runas)");
    line("  runas    execute an allowed administrative command");
    line("  ls [dir] list files under a project-store path (default: /)");
    line("  mko      show the preinstalled MKO environment");
    line("  input    show unified keyboard/mouse statistics");
    line("  ipc      show channel and message statistics");
    line("  fetch    show a fast system summary");
    line("  clear    clear the VGA text console");
}

static void applet_uname(void) {
    line("MAKO Kernel 0.1 x86_64 (C bootstrap + native MKO)");
}

static void applet_status(void) {
    line("MakoBox kernel status:");
    line(live.idt_ready ? "  exceptions: ready" : "  exceptions: unavailable");
    line(live.native_mko_ready ? "  native MKO: ready" : "  native MKO: unavailable");
    line(live.paging_root != 0u ? "  virtual memory: active" : "  virtual memory: unavailable");
    line(live.app_runtime_ready ? "  application ABI: ready" : "  application ABI: unavailable");
}

static void applet_mem(void) {
    value_line("usable memory: ", live.usable_memory_bytes / (1024u * 1024u), " MiB");
}

static void applet_frames(void) {
    hex_line("next physical frame: ", live.frame_next);
    hex_line("allocator region end: ", live.frame_end);
}

static void applet_paging(void) {
    hex_line("active CR3 root: ", live.paging_root);
}

static void applet_ticks(void) {
    value_line("PIT ticks since IRQ enable: ", interrupts_timer_ticks(), NULL);
}

static void task_line(const struct scheduler_task_snapshot *task) {
    serial_write_u64(task->pid);
    serial_write("   ");
    serial_write(scheduler_state_name(task->state));
    serial_write("   ");
    serial_write(task->name);
    serial_write("   ticks=");
    serial_write_u64(task->cpu_ticks);
    serial_write(" yields=");
    serial_write_u64(task->yields);
    serial_write(" quanta=");
    serial_write_u64(task->quantum_expirations);
    serial_write(" cr3=");
    serial_write_hex(task->address_space);
    serial_write("\n");

    terminal_write_u64(task->pid);
    terminal_write("   ");
    terminal_write(scheduler_state_name(task->state));
    terminal_write("   ");
    terminal_write(task->name);
    terminal_write("   ticks=");
    terminal_write_u64(task->cpu_ticks);
    terminal_write(" yields=");
    terminal_write_u64(task->yields);
    terminal_write(" quanta=");
    terminal_write_u64(task->quantum_expirations);
    terminal_write(" cr3=");
    terminal_write_hex(task->address_space);
    terminal_write_line("");
}

static void applet_ps(void) {
    line("PID STATE     NAME   SCHEDULER COUNTERS");
    for (size_t index = 0; index < scheduler_task_count(); ++index) {
        struct scheduler_task_snapshot task;
        if (scheduler_snapshot(index, &task)) task_line(&task);
    }
    value_line("scheduler dispatches: ", scheduler_dispatches(), NULL);
}

static void applet_abi(void) {
    line("MAKO-ABI 0.1");
    line("Executable: ELF64 x86_64, isolated ring-3 address space");
    line("Entry: RDI=pid, private 16-byte-aligned user stack");
    line("Syscalls: core 0-4; handles 5-8; project files 9-10");
    line("Host dependency: none (no Windows/Linux userspace API)");
}

static void applet_caps(void) {
    line("Capability service:");
    value_line("  opened: ", capabilities_opened(), NULL);
    value_line("  closed: ", capabilities_closed(), NULL);
    value_line("  denied: ", capabilities_denied(), NULL);
    value_line("  live user handles: ", capabilities_live(), NULL);
}

static void applet_projects(void) {
    line("Project store (RAM):");
    value_line("  files: ", ramfs_file_count(), NULL);
    value_line("  bytes: ", ramfs_bytes_used(), NULL);
    value_line("  reads: ", ramfs_reads(), NULL);
    value_line("  writes: ", ramfs_writes(), NULL);
    for (size_t index = 0u; index < ramfs_file_count(); ++index) {
        const char *name;
        size_t name_length;
        size_t length;
        if (!ramfs_entry(index, &name, &name_length, &length)) continue;
        (void)name_length;
        serial_write("  "); serial_write(name); serial_write("  ");
        serial_write_u64(length); serial_write(" bytes\n");
        terminal_write("  "); terminal_write(name); terminal_write("  ");
        terminal_write_u64(length); terminal_write_line(" bytes");
    }
}

static void applet_ls(const char *path) {
    const size_t path_length = string_length(path);
    line(path_length == 0u ? "Listing: /" : path);
    size_t shown = 0u;
    char entry_name[66];
    for (size_t index = 0u; ; ++index) {
        const char *rel_name;
        size_t rel_length;
        size_t length;
        bool is_directory;
        if (!ramfs_list(path, path_length, index, &rel_name, &rel_length, &length, &is_directory))
            break;
        ++shown;
        if (rel_length > sizeof(entry_name) - 2u) rel_length = sizeof(entry_name) - 2u;
        for (size_t i = 0; i < rel_length; ++i) entry_name[i] = rel_name[i];
        if (is_directory) entry_name[rel_length++] = '/';
        entry_name[rel_length] = '\0';
        serial_write("  "); serial_write(entry_name);
        if (is_directory) { serial_write("\n"); }
        else { serial_write("  "); serial_write_u64(length); serial_write(" bytes\n"); }
        terminal_write("  ");
        /* Classic ls convention: directories in a distinct color (light
           blue, VGA color 9) from plain files (default white, 15) -- serial
           output stays plain text either way, no ANSI codes to strip there. */
        terminal_set_color(is_directory ? 9u : 15u, 0u);
        terminal_write(entry_name);
        terminal_set_color(15u, 0u);
        if (is_directory) { terminal_write_line(""); }
        else { terminal_write("  "); terminal_write_u64(length); terminal_write_line(" bytes"); }
    }
    if (shown == 0u) line("  (empty)");
}

static void applet_mko(void) {
    line("MKO system environment:");
    line("  native runtime: online");
    line("  repository: AnimatedGTVR/MAKO");
    line("  ISO source: /system/mako/MAKO-source.tar.zst");
    line("  boot manifest: /system/mako/manifest.txt (origin + commit + SHA-256)");
    line("  ABI SDK: /system/mko/sdk.mko (preinstalled)");
    line("  starter source: /projects/hello/main.mko (preinstalled)");
    line("  starter executable: /projects/hello/main.elf (preinstalled)");
    value_line("  ISO-provided assets: ", ramfs_seeded_files(), NULL);
    line("  compiler execution: requires VFS + compatible runtime/self-hosted port");
}

static void applet_input(void) {
    line("Unified input service:");
    line(keyboard_controller_ready() ? "  keyboard: ready" : "  keyboard: unavailable");
    value_line("  IRQ1 deliveries: ", keyboard_irq_count(), NULL);
    value_line("  decoded characters: ", keyboard_character_count(), NULL);
    line(mouse_controller_ready() ? "  mouse: ready" : "  mouse: unavailable");
    line(mouse_scroll_ready() ? "  wheel protocol: ready" : "  wheel protocol: standard buttons only");
    value_line("  IRQ12 deliveries: ", mouse_irq_count(), NULL);
    value_line("  decoded packets: ", mouse_packet_count(), NULL);
    value_line("  pointer x: ", (uint64_t)mouse_x(), NULL);
    value_line("  pointer y: ", (uint64_t)mouse_y(), NULL);
    value_line("  queued events: ", input_pending(), NULL);
    value_line("  userspace deliveries: ", input_userspace_deliveries(), NULL);
    value_line("  coalesced motions: ", input_coalesced(), NULL);
    value_line("  dropped events: ", input_dropped(), NULL);
    value_line("  cursor redraws: ", framebuffer_cursor_updates(), NULL);
}

static void applet_fetch(void) {
    char vendor[13];
    cpu_vendor(vendor);
    terminal_set_color(11u, 0u);
    line("       /\\        mako@kernel");
    line("      /  \\       -----------");
    line("     / /\\ \\      OS: MAKO Kernel 0.1");
    line("    / ____ \\     Arch: x86_64");
    line("   /_/    \\_\\    Core: C + native MKO");
    terminal_set_color(15u, 0u);
    terminal_write("CPU: "); terminal_write_line(vendor);
    serial_write("CPU: "); serial_write(vendor); serial_write("\n");
    value_line("Memory: ", live.usable_memory_bytes / (1024u * 1024u), " MiB usable");
    value_line("Kernel: ", live.kernel_size_bytes / 1024u, " KiB in memory");
    hex_line("CR3: ", live.paging_root);
    hex_line("Next frame: ", live.frame_next);
    value_line("Uptime ticks: ", interrupts_timer_ticks(), " @ 100 Hz");
    value_line("Scheduler dispatches: ", scheduler_dispatches(), NULL);
    line(live.app_runtime_ready ? "ABI: MAKO-ABI 0.1" : "ABI: unavailable");
    value_line("Capability denials: ", capabilities_denied(), NULL);
    value_line("Project files: ", ramfs_file_count(), " in RAM");
    line(live.native_mko_ready ? "MKO: native backend online" : "MKO: unavailable");
    value_line("Init units: ", init_system_active_count(), " active");
    value_line("Apps: ", apps_count(), " installed");
}

static void applet_ipc(void) {
    line("Capability IPC service:");
    value_line("  named channels: ", ipc_channel_count(), NULL);
    value_line("  messages sent: ", ipc_messages_sent(), NULL);
    value_line("  messages received: ", ipc_messages_received(), NULL);
    value_line("  messages dropped: ", ipc_messages_dropped(), NULL);
    line("  message limit: 64 bytes; queue depth: 8");
    line("  receive: blocking and non-blocking");
}

static void applet_apps_list(void) {
    line("Installed applications:");
    for (size_t index = 0u; index < apps_count(); ++index) {
        struct app_snapshot app;
        if (!apps_snapshot(index, &app)) continue;
        serial_write("  "); serial_write(app.name);
        serial_write(app.valid_elf64 ? "  ELF64  ready  " : "  invalid  blocked  ");
        serial_write_u64(app.image_bytes); serial_write(" bytes  ");
        serial_write(app.path); serial_write("\n");
        terminal_write("  "); terminal_write(app.name);
        terminal_write(app.valid_elf64 ? "  ELF64  ready  " : "  invalid  blocked  ");
        terminal_write_line(app.path);
    }
    value_line("  catalog scans: ", apps_scan_count(), NULL);
}

static bool applet_apps_info(const char *name) {
    struct app_snapshot app;
    if (!apps_find(name, &app)) {
        line("apps: application not found");
        return false;
    }
    line("Application manifest:");
    line(app.name);
    line(app.path);
    value_line("  image bytes: ", app.image_bytes, NULL);
    line(app.valid_elf64 ? "  format: ELF64 x86_64 validated" : "  format: invalid");
    line("  launch: dynamic RAMFS spawn ready");
    return true;
}

static void applet_git_status(void) {
    line("MAKO native Git worktree:");
    line(git_repository_initialized() ? "  repository: initialized" : "  repository: not initialized");
    line(git_repository_dirty() ? "  worktree: modified" : "  worktree: clean");
    value_line("  tracked files: ", git_tracked_files(), NULL);
    value_line("  tracked bytes: ", git_tracked_bytes(), NULL);
    value_line("  commits: ", git_commit_count(), NULL);
    line("  compatibility: worktree foundation; Git objects/packs/remotes pending");
}

static void applet_desktop(void) {
    line("Desktop readiness:");
    line("  [ready] PS/2 keyboard input");
    line("  [ready] isolated ELF64 application runtime");
    line("  [ready] app-manager.service catalog");
    line("  [ready] project storage and capability handles");
    line("  [ready] framebuffer and Stage 2 ARGB renderer");
    line("  [ready] PS/2 mouse and unified input events");
    line("  [ready] dynamic process creation and lifecycle");
    line("  [ready] named capability IPC channels");
    line("  [ready] ring-3 compositor transport + atomic presentation");
    line("  [ready] three-client focus/z-order + crash containment");
    line("  [ready] pointer hit-testing and click-to-focus routing");
    line("  [ready] persistent receive/repaint + retained MOVE geometry");
    line("  [ready] client-owned surface + read-only capability transfer");
    line("  [ready] blocking IPC/input waitset (no compositor polling)");
    line("  [ready] title-bar pointer drag with bounded geometry");
    line("  [ready] bottom-right pointer resize with min/screen bounds");
    line("  [ready] focused KEY delivery to isolated window endpoint");
    line("  [ready] adjacent pointer-motion burst coalescing");
    line("  [ready] reference-counted surface reclamation");
    line("  [ready] read-only mapped client surface + owned damage metadata");
    line("  [ready] timer-deadline frame pacing and dirty wakeups");
    line("  [ready] native decorations, resize grip, and launcher toggle");
    line("  [ready] compositor-owned arrow cursor and compact launcher glyphs");
    line("  [ready] IRQ-driven desktop event pump beside MakoBox");
    if (init_system_desktop_active()) {
        line("  [ready] init-owned desktop.target session");
        value_line("  desktop compositor PID: ", init_system_desktop_pid(), " blocked/ready");
    } else {
        line("  [fallback] desktop.target unavailable without framebuffer");
    }
    line("  [complete] desktop-demo platform contract 100/100");
    line("  [ready] allocation-free Ethernet/ARP/IPv4 packet core");
    line("  [future] NIC drivers, DHCP, DNS, TCP/TLS, and hardware breadth");
}

static void print_unit(const struct init_unit_snapshot *unit) {
    serial_write("  "); serial_write(unit->name); serial_write("  ");
    serial_write(init_system_state_name(unit->state)); serial_write("  ");
    serial_write(unit->description); serial_write(" starts=");
    serial_write_u64(unit->starts); serial_write(" stops=");
    serial_write_u64(unit->stops);
    if (unit->process_id != 0u) {
        serial_write(" pid="); serial_write_u64(unit->process_id);
    }
    serial_write("\n");
    terminal_write("  "); terminal_write(unit->name); terminal_write("  ");
    terminal_write(init_system_state_name(unit->state)); terminal_write("  ");
    terminal_write_line(unit->description);
}

static bool applet_systemctl_status(const char *name) {
    for (size_t index = 0u; index < init_system_unit_count(); ++index) {
        struct init_unit_snapshot unit;
        if (!init_system_snapshot(index, &unit)) continue;
        if (name[0] == '\0' || equal(name, unit.name)) {
            print_unit(&unit);
            if (name[0] != '\0') return true;
        }
    }
    if (name[0] == '\0') {
        value_line("  transactions: ", init_system_transaction_count(), NULL);
        return true;
    }
    line("systemctl: unit not found");
    return false;
}

static bool systemctl_mutate(const char *command) {
    const char *name;
    bool result = false;
    if (starts_with(command, "systemctl start ", &name)) result = init_system_start(name);
    else if (starts_with(command, "systemctl stop ", &name)) result = init_system_stop(name);
    else if (starts_with(command, "systemctl restart ", &name)) result = init_system_restart(name);
    else return false;
    if (!result) {
        line("systemctl: transaction rejected (unknown, immutable, or invalid state)");
        return false;
    }
    line("systemctl: transaction committed");
    return applet_systemctl_status(name);
}

static bool applet_runas(const char *command) {
    if (command[0] == '\0') {
        line("usage: runas systemctl <start|stop|restart> UNIT");
        return false;
    }
    if (!runas_authorize(command)) {
        line("runas: policy denied command");
        return false;
    }
    line("runas: policy granted local-console administrator role");
    return systemctl_mutate(command);
}

void makobox_init(const struct makobox_state *state) {
    live = *state;
}

static bool launch_app(const char *name) {
    struct app_snapshot app;
    if (!apps_find(name, &app) || !app.valid_elf64) {
        line("apps: launch target unavailable");
        return false;
    }
    size_t path_length = 0u;
    while (app.path[path_length] != '\0') ++path_length;
    const uint32_t desktop_pid = init_system_desktop_active() ?
        init_system_desktop_pid() : 0u;
    const bool resume_desktop = desktop_pid != 0u && !terminal_graphical_active();
    if (desktop_pid != 0u) {
        /* Foreground apps own unified input while running. Remove both halves
           of the compositor waitset but leave its task blocked. A graphical
           terminal session keeps it suspended; a desktop session re-arms it
           after the app exits. */
        (void)ipc_cancel_wait(desktop_pid);
        (void)input_cancel_wait(desktop_pid);
    }
    keyboard_discard_chars();
    input_discard_pending();
    uint32_t pid = userspace_spawn_path(0u, app.path, path_length, app.name,
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_INPUT));
    if (pid == 0u || !userspace_run_init()) {
        keyboard_discard_chars();
        input_discard_pending();
        if (resume_desktop) (void)scheduler_wake(desktop_pid, 3u, 0u);
        line("apps: spawn failed");
        return false;
    }
    uint64_t status;
    if (!scheduler_reap(0u, pid, &status)) {
        keyboard_discard_chars();
        input_discard_pending();
        if (resume_desktop) (void)scheduler_wake(desktop_pid, 3u, 0u);
        line("apps: wait failed");
        return false;
    }
    keyboard_discard_chars();
    input_discard_pending();
    if (resume_desktop) {
        (void)scheduler_wake(desktop_pid, 3u, 0u);
        (void)userspace_run_init();
    }
    if (terminal_graphical_active()) terminal_graphical_refresh();
    value_line("apps: process exited with status ", status, NULL);
    return status == 0u;
}

bool makobox_run(const char *command_line) {
    const char *argument;
    while (*command_line == ' ') ++command_line;
    if (equal(command_line, "help")) applet_help();
    else if (equal(command_line, "uname")) applet_uname();
    else if (equal(command_line, "status")) applet_status();
    else if (equal(command_line, "mem")) applet_mem();
    else if (equal(command_line, "frames")) applet_frames();
    else if (equal(command_line, "paging")) applet_paging();
    else if (equal(command_line, "ticks")) applet_ticks();
    else if (equal(command_line, "ps")) applet_ps();
    else if (equal(command_line, "abi")) applet_abi();
    else if (equal(command_line, "caps")) applet_caps();
    else if (equal(command_line, "projects")) applet_projects();
    else if (equal(command_line, "apps") || equal(command_line, "apps list")) applet_apps_list();
    else if (starts_with(command_line, "apps info ", &argument)) return applet_apps_info(argument);
    else if (starts_with(command_line, "apps launch ", &argument)) return launch_app(argument);
    else if (equal(command_line, "tetris")) return launch_app("tetris");
    else if (equal(command_line, "git") || equal(command_line, "git status")) applet_git_status();
    else if (equal(command_line, "git version")) line("MAKO Git foundation 0.1 (native, no host binary)");
    else if (equal(command_line, "git init")) {
        if (!git_repository_init()) return false;
        line("Initialized MAKO project worktree");
    }
    else if (equal(command_line, "git commit")) {
        if (!git_repository_commit()) { line("git: repository not initialized"); return false; }
        line("Committed current RAM project snapshot");
    }
    else if (equal(command_line, "git log")) value_line("MAKO snapshot commits: ", git_commit_count(), NULL);
    else if (equal(command_line, "desktop") || equal(command_line, "desktop status")) applet_desktop();
    else if (equal(command_line, "systemctl") || equal(command_line, "systemctl list-units"))
        applet_systemctl_status("");
    else if (starts_with(command_line, "systemctl status ", &argument))
        return applet_systemctl_status(argument);
    else if (starts_with(command_line, "systemctl start ", &argument) ||
             starts_with(command_line, "systemctl stop ", &argument) ||
             starts_with(command_line, "systemctl restart ", &argument)) {
        (void)argument;
        line("systemctl: administrative transaction requires runas");
        return false;
    }
    else if (equal(command_line, "runas")) return applet_runas("");
    else if (starts_with(command_line, "runas ", &argument)) return applet_runas(argument);
    else if (equal(command_line, "ls")) applet_ls("");
    else if (starts_with(command_line, "ls ", &command_line)) applet_ls(command_line);
    else if (equal(command_line, "mko") || equal(command_line, "mko info")) applet_mko();
    else if (equal(command_line, "input")) applet_input();
    else if (equal(command_line, "ipc")) applet_ipc();
    else if (equal(command_line, "fetch")) applet_fetch();
    else if (equal(command_line, "clear")) terminal_init();
    else {
        serial_write("makobox: applet not found: ");
        serial_write(command_line);
        serial_write("\n");
        terminal_write("makobox: applet not found: ");
        terminal_write_line(command_line);
        return false;
    }
    return true;
}

bool makobox_self_test(void) {
    if (makobox_run("systemctl stop project-host.service")) return false;
    return makobox_run("uname") && makobox_run("status") &&
        makobox_run("mem") && makobox_run("frames") &&
        makobox_run("paging") && makobox_run("ticks") && makobox_run("ps") &&
        makobox_run("abi") && makobox_run("caps") &&
        makobox_run("projects") && makobox_run("systemctl list-units") &&
        makobox_run("systemctl status project-host.service") &&
        makobox_run("runas systemctl restart project-host.service") &&
        makobox_run("apps list") && makobox_run("apps info hello") &&
        makobox_run("git status") && makobox_run("desktop status") && makobox_run("mko") &&
        makobox_run("input") && makobox_run("ipc") && makobox_run("fetch");
}

__attribute__((noreturn))
void makobox_shell(void) {
    char input[64];
    size_t length = 0;
    if (terminal_graphical_active() && init_system_desktop_active()) {
        const uint32_t desktop_pid = init_system_desktop_pid();
        (void)ipc_cancel_wait(desktop_pid);
        (void)input_cancel_wait(desktop_pid);
        input_discard_pending();
        terminal_graphical_refresh();
        serial_write("TERMINAL_OWNERSHIP_READY display=kernel input=makobox compositor=suspended\n");
    }
    /* Clear the visual console only (serial keeps every boot-status line
       that already scrolled by -- smoke tests grep that log). Without this,
       a real interactive session starts buried under dozens of "[ OK ]"
       boot lines and reads like a dmesg dump instead of a shell you just
       landed in. */
    terminal_write("\f");
    line("");
    line("MakoBox interactive console ready. Type 'help'.");
    if (terminal_graphical_active())
        line("Terminal owns display and keyboard; compositor is suspended.");
    else {
        line("Desktop event pump active (IPC + keyboard + pointer).");
        serial_write("DESKTOP_EVENT_PUMP_READY\n");
    }
    terminal_set_color(11u, 0u);
    /* Visual prompt only -- serial_write keeps the plain "mako# " text every
       keyboard/process/vfs smoke test greps for (see the Makefile's
       "mako# help"/"mako# ls /system"/etc. assertions), so it's left alone
       and only the on-screen rendering gets the friendlier [user@host]#
       shape. */
    terminal_write("ROOT-DEMONOS: ");
    serial_write("mako# ");
    terminal_set_color(15u, 0u);
    for (;;) {
        /* Input IRQs wake the persistent ring-3 compositor while the kernel
           console owns the CPU. Resume ready desktop work once, then return
           here as soon as it blocks for the next event. This keeps the shell
           and desktop responsive without a polling thread or idle CPU burn. */
        if (scheduler_has_ready_users()) {
            (void)userspace_run_init();
            if (terminal_graphical_active()) terminal_graphical_refresh();
        }
        if (terminal_graphical_active()) input_discard_pending();
        if (framebuffer_available() && !init_system_desktop_active())
            (void)framebuffer_cursor_move(mouse_x(), mouse_y());
        char value;
        if (!keyboard_read_char(&value)) {
            __asm__ volatile ("hlt");
            continue;
        }
        if (value == '\n') {
            terminal_write_line("");
            serial_write("\n");
            input[length] = '\0';
            if (length > 0u) (void)makobox_run(input);
            length = 0;
            terminal_set_color(11u, 0u);
            terminal_write("ROOT-DEMONOS: ");
            serial_write("mako# ");
            terminal_set_color(15u, 0u);
        } else if (value == '\b') {
            if (length > 0u) {
                --length;
                terminal_backspace();
                serial_write("\b \b");
            }
        } else if (length + 1u < sizeof(input)) {
            input[length++] = value;
            char echo[2] = { value, '\0' };
            terminal_write(echo);
            serial_write(echo);
        }
    }
}

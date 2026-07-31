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
    line("  git ...  real content-snapshot Git (init/add/commit/log/diff/show/status)");
    line("  desktop  report console readiness (no GUI -- see sidelined/)");
    line("  systemctl list/status units (mutations require runas)");
    line("  runas <command>  run any MakoBox command with elevated (root) rights");
    line("  ls [-la] [dir]  list files under a project-store path (default: /)");
    line("  cat <path>  print a file's contents");
    line("  head <path> print a file's first 10 lines");
    line("  wc <path>   count lines/words/bytes in a file");
    line("  touch <path> create an empty file if it doesn't exist");
    line("  echo <text> print text back");
    line("  whoami   print the current identity (root only inside runas)");
    line("  pwd      print the current directory (always /)");
    line("  hostname print this kernel's hostname");
    line("  date     print the real CMOS/RTC date and time");
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

// Parses "-l"/"-a"/"-la"/"-al" (any order/combo of the letters l/a) as a
// single leading flag token, same as real ls's short-option bundling.
// There's no hidden-dotfile concept in RAMFS, so -a is accepted (for
// muscle-memory compatibility) but doesn't change what's shown -- only -l
// (long format) has a real, visible effect here.
static bool parse_ls_flags(const char *path, bool *long_format, const char **rest) {
    *long_format = false;
    *rest = path;
    if (path[0] != '-') return true;
    size_t i = 1u;
    while (path[i] != '\0' && path[i] != ' ') {
        if (path[i] == 'l') *long_format = true;
        else if (path[i] != 'a') return false; // unknown flag letter
        ++i;
    }
    while (path[i] == ' ') ++i;
    *rest = path + i;
    return true;
}

static void applet_ls(const char *argument) {
    bool long_format;
    const char *path;
    if (!parse_ls_flags(argument, &long_format, &path)) {
        line("ls: unknown option (supported: -l, -a, -la)");
        return;
    }
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
        if (long_format) {
            // No real permission bits or ownership model here (single
            // local-console user, no uid/gid) -- the type/size columns
            // are real (RAMFS-derived); the rwx-looking column is a fixed
            // placeholder shape so the output at least *reads* like `ls -l`.
            serial_write(is_directory ? "d" : "-");
            serial_write("rwx------  1 mako mako  ");
            serial_write_u64(length);
            serial_write("  ");
            serial_write(entry_name);
            serial_write("\n");
            terminal_write(is_directory ? "d" : "-");
            terminal_write("rwx------ 1 mako mako ");
            terminal_write_u64(length);
            terminal_write(" ");
            terminal_write_line(entry_name);
        } else {
            serial_write("  "); serial_write(entry_name);
            if (is_directory) { serial_write("\n"); }
            else { serial_write("  "); serial_write_u64(length); serial_write(" bytes\n"); }
            terminal_write("  ");
            terminal_write(entry_name);
            if (is_directory) { terminal_write_line(""); }
            else { terminal_write("  "); terminal_write_u64(length); terminal_write_line(" bytes"); }
        }
    }
    if (shown == 0u) line("  (empty)");
}

// Set only for the dynamic scope of a `runas <command>` dispatch (see
// applet_runas/makobox_run's systemctl gate) -- real sudo-style: the shell
// itself always runs as the single local-console user, and only commands
// actually wrapped in runas see elevated identity/administrative rights.
static bool running_elevated;

static void applet_whoami(void) {
    line(running_elevated ? "root" : "mako");
}

static void applet_pwd(void) {
    // No cwd tracking -- every path this shell takes is already absolute
    // against RAMFS's single root, so pwd is always "/".
    line("/");
}

static void applet_hostname(void) {
    line("kernel");
}

static void applet_echo(const char *text) {
    line(text);
}

// serial_write/terminal_write only take null-terminated strings; RAMFS
// file content is a raw length-bounded byte span, so this copies it out in
// fixed-size null-terminated chunks rather than assuming any one line is
// short enough for a single small stack buffer.
static void write_span(const char *data, size_t length) {
    char chunk[65];
    size_t offset = 0u;
    while (offset < length) {
        size_t take = length - offset;
        if (take > sizeof(chunk) - 1u) take = sizeof(chunk) - 1u;
        for (size_t i = 0u; i < take; ++i) chunk[i] = data[offset + i];
        chunk[take] = '\0';
        serial_write(chunk);
        terminal_write(chunk);
        offset += take;
    }
}

static void applet_cat(const char *path) {
    if (path[0] == '\0') { line("usage: cat <path>"); return; }
    uint32_t object_id;
    const uint8_t *data;
    size_t length;
    const size_t path_length = string_length(path);
    if (!ramfs_open(path, path_length, false, &object_id) ||
        !ramfs_view(object_id, &data, &length)) {
        line("cat: no such file");
        return;
    }
    // Real file bytes, not a preview -- printed a line at a time so
    // embedded '\n's render the same way applet output normally does.
    size_t start = 0u;
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] == '\n') {
            write_span((const char *)&data[start], i - start);
            serial_write("\n");
            terminal_write_line("");
            start = i + 1u;
        }
    }
    if (start < length) {
        write_span((const char *)&data[start], length - start);
        serial_write("\n");
        terminal_write_line("");
    }
}

static void applet_head(const char *path) {
    if (path[0] == '\0') { line("usage: head <path>"); return; }
    uint32_t object_id;
    const uint8_t *data;
    size_t length;
    const size_t path_length = string_length(path);
    if (!ramfs_open(path, path_length, false, &object_id) ||
        !ramfs_view(object_id, &data, &length)) {
        line("head: no such file");
        return;
    }
    size_t start = 0u;
    unsigned shown_lines = 0u;
    for (size_t i = 0u; i < length && shown_lines < 10u; ++i) {
        if (data[i] == '\n') {
            write_span((const char *)&data[start], i - start);
            serial_write("\n");
            terminal_write_line("");
            start = i + 1u;
            ++shown_lines;
        }
    }
    if (shown_lines < 10u && start < length) {
        write_span((const char *)&data[start], length - start);
        serial_write("\n");
        terminal_write_line("");
    }
}

static void applet_wc(const char *path) {
    if (path[0] == '\0') { line("usage: wc <path>"); return; }
    uint32_t object_id;
    const uint8_t *data;
    size_t length;
    const size_t path_length = string_length(path);
    if (!ramfs_open(path, path_length, false, &object_id) ||
        !ramfs_view(object_id, &data, &length)) {
        line("wc: no such file");
        return;
    }
    uint64_t lines = 0u, words = 0u;
    bool in_word = false;
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] == '\n') ++lines;
        const bool space = data[i] == ' ' || data[i] == '\n' || data[i] == '\t';
        if (!space && !in_word) { ++words; in_word = true; }
        else if (space) in_word = false;
    }
    if (length > 0u && data[length - 1u] != '\n') ++lines; // count a trailing partial line
    value_line("  lines: ", lines, NULL);
    value_line("  words: ", words, NULL);
    value_line("  bytes: ", (uint64_t)length, NULL);
}

static void applet_touch(const char *path) {
    if (path[0] == '\0') { line("usage: touch <path>"); return; }
    uint32_t object_id;
    if (!ramfs_open(path, string_length(path), true, &object_id)) {
        line("touch: could not create file");
        return;
    }
    line("touched");
}

static inline void rtc_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t rtc_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t reg) {
    rtc_out8(0x70u, reg);
    return rtc_in8(0x71u);
}

// Real CMOS/RTC hardware read (ports 0x70/0x71) -- QEMU (and real
// hardware) backs this with the actual host/BIOS clock, so this is a
// genuine date/time, not a fabricated one. Polls Register A's
// update-in-progress bit first (real RTCs latch a fresh update roughly
// once a second) and converts from BCD if Register B says the RTC isn't
// already in binary mode -- both real, documented MC146818 behavior, not
// simplifications that happen to work only in QEMU.
static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)(((value >> 4u) * 10u) + (value & 0x0Fu));
}

static void applet_date(void) {
    for (unsigned attempt = 0u; attempt < 1000000u; ++attempt)
        if ((cmos_read(0x0Au) & 0x80u) == 0u) break;
    uint8_t second = cmos_read(0x00u);
    uint8_t minute = cmos_read(0x02u);
    uint8_t hour = cmos_read(0x04u);
    uint8_t day = cmos_read(0x07u);
    uint8_t month = cmos_read(0x08u);
    uint8_t year = cmos_read(0x09u);
    const uint8_t status_b = cmos_read(0x0Bu);
    if ((status_b & 0x04u) == 0u) { // BCD mode
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour & 0x7Fu) | (uint8_t)(hour & 0x80u);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }
    char buffer[48];
    int offset = 0;
    static const char prefix[] = "20";
    for (size_t i = 0u; i < sizeof(prefix) - 1u; ++i) buffer[offset++] = prefix[i];
    buffer[offset++] = (char)('0' + year / 10u);
    buffer[offset++] = (char)('0' + year % 10u);
    buffer[offset++] = '-';
    buffer[offset++] = (char)('0' + month / 10u);
    buffer[offset++] = (char)('0' + month % 10u);
    buffer[offset++] = '-';
    buffer[offset++] = (char)('0' + day / 10u);
    buffer[offset++] = (char)('0' + day % 10u);
    buffer[offset++] = ' ';
    buffer[offset++] = (char)('0' + hour / 10u);
    buffer[offset++] = (char)('0' + hour % 10u);
    buffer[offset++] = ':';
    buffer[offset++] = (char)('0' + minute / 10u);
    buffer[offset++] = (char)('0' + minute % 10u);
    buffer[offset++] = ':';
    buffer[offset++] = (char)('0' + second / 10u);
    buffer[offset++] = (char)('0' + second % 10u);
    buffer[offset] = '\0';
    line(buffer);
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
    line("       /\\        mako@kernel");
    line("      /  \\       -----------");
    line("     / /\\ \\      OS: MAKO Kernel 0.1");
    line("    / ____ \\     Arch: x86_64");
    line("   /_/    \\_\\    Core: C + native MKO");
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

static void hash_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_hex(value);
    serial_write("\n");
    terminal_write(label);
    terminal_write_hex(value);
    terminal_write_line("");
}

static void applet_git_status(void) {
    line("MAKO Git (stage 1: real content snapshots, no branches/remotes yet)");
    line(git_repository_initialized() ? "  repository: initialized" : "  repository: not initialized");
    line(git_repository_dirty() ? "  worktree: modified" : "  worktree: clean");
    value_line("  tracked files: ", git_tracked_files(), NULL);
    value_line("  tracked bytes: ", git_tracked_bytes(), NULL);
    value_line("  commits: ", git_commit_count(), NULL);
    if (git_commit_count() > 0u) hash_line("  HEAD: ", git_head_hash());
    for (size_t i = 0u; i < git_tracked_count(); ++i) {
        const char *name;
        size_t name_length;
        if (git_tracked_entry(i, &name, &name_length)) {
            serial_write("  tracked: "); serial_write(name); serial_write("\n");
            terminal_write("  tracked: "); terminal_write_line(name);
        }
    }
}

static bool applet_git_add(const char *path) {
    if (path[0] == '\0') { line("usage: git add <path>"); return false; }
    if (!git_repository_initialized()) { line("git: repository not initialized"); return false; }
    if (!git_add(path, string_length(path))) {
        line("git add: no such file, tracked-file limit reached, or repository not initialized");
        return false;
    }
    line("added to tracked files");
    return true;
}

// Parses `git commit -m "message text"` (message may be unquoted too --
// this keeps things simple rather than implementing real shell quoting).
static bool applet_git_commit(const char *argument) {
    const char *message = argument;
    if (starts_with(argument, "-m ", &message)) {
        if (message[0] == '"') {
            ++message;
            static char buffer[GIT_MESSAGE_MAX + 1u];
            size_t length = 0u;
            while (message[length] != '\0' && message[length] != '"' &&
                   length < sizeof(buffer) - 1u) {
                buffer[length] = message[length];
                ++length;
            }
            buffer[length] = '\0';
            message = buffer;
        }
    }
    if (message[0] == '\0') { line("usage: git commit -m \"message\""); return false; }
    uint64_t hash;
    if (!git_commit(message, string_length(message), &hash)) {
        line("git commit: nothing tracked, repository not initialized, or snapshot arena full");
        return false;
    }
    hash_line("committed ", hash);
    return true;
}

static void applet_git_log(void) {
    if (git_commit_count() == 0u) { line("git log: no commits yet"); return; }
    for (size_t i = 0u; ; ++i) {
        uint64_t hash, parent;
        const char *message;
        size_t message_length;
        if (!git_log_entry(i, &hash, &parent, &message, &message_length)) break;
        hash_line("commit ", hash);
        serial_write("    "); serial_write(message); serial_write("\n");
        terminal_write("    "); terminal_write_line(message);
    }
}

// Simple, honest diff: finds the longest matching prefix and suffix
// between HEAD's snapshot and the current content, and prints whatever's
// left in the middle as removed/added. Correct for a single contiguous
// edit (the common case); scattered multi-region edits show as one
// larger changed block rather than several small hunks -- a real
// simplification, not a fake result.
static void print_diff_span(const char *prefix, const uint8_t *data, size_t length) {
    if (length == 0u) return;
    size_t start = 0u;
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] == '\n' || i == length - 1u) {
            const size_t end = data[i] == '\n' ? i : i + 1u;
            serial_write(prefix);
            terminal_write(prefix);
            write_span((const char *)&data[start], end - start);
            serial_write("\n");
            terminal_write_line("");
            start = i + 1u;
        }
    }
}

static void applet_git_diff(const char *path) {
    if (git_commit_count() == 0u) { line("git diff: no commits yet"); return; }
    bool any = false;
    for (size_t i = 0u; i < git_tracked_count(); ++i) {
        const char *name;
        size_t name_length;
        if (!git_tracked_entry(i, &name, &name_length)) continue;
        (void)name_length;
        if (path[0] != '\0' && !equal(name, path))
            continue;
        any = true;
        const uint8_t *head_data;
        size_t head_length;
        uint32_t object_id;
        const uint8_t *current_data;
        size_t current_length;
        if (!git_commit_file(0u, name, name_length, &head_data, &head_length) ||
            !ramfs_open(name, name_length, false, &object_id) ||
            !ramfs_view(object_id, &current_data, &current_length))
            continue;
        size_t prefix = 0u;
        while (prefix < head_length && prefix < current_length &&
               head_data[prefix] == current_data[prefix])
            ++prefix;
        size_t head_suffix = head_length, current_suffix = current_length;
        while (head_suffix > prefix && current_suffix > prefix &&
               head_data[head_suffix - 1u] == current_data[current_suffix - 1u]) {
            --head_suffix; --current_suffix;
        }
        if (prefix == head_length && prefix == current_length) continue; // unchanged
        serial_write("--- a/"); serial_write(name); serial_write("\n");
        serial_write("+++ b/"); serial_write(name); serial_write("\n");
        terminal_write("--- a/"); terminal_write_line(name);
        terminal_write("+++ b/"); terminal_write_line(name);
        print_diff_span("-", &head_data[prefix], head_suffix - prefix);
        print_diff_span("+", &current_data[prefix], current_suffix - prefix);
    }
    if (!any) line(path[0] != '\0' ? "git diff: not a tracked file" : "git diff: no tracked files");
}

static void applet_git_show(uint64_t hash) {
    size_t commit_index;
    if (!git_find_commit(hash, &commit_index)) { line("git show: unknown commit"); return; }
    for (size_t i = 0u; i < git_tracked_count(); ++i) {
        const char *name;
        size_t name_length;
        if (!git_tracked_entry(i, &name, &name_length)) continue;
        const uint8_t *data;
        size_t length;
        if (!git_commit_file(commit_index, name, name_length, &data, &length)) continue;
        serial_write("--- "); serial_write(name); serial_write(" ("); serial_write_u64((uint64_t)length);
        serial_write(" bytes) ---\n");
        terminal_write("--- "); terminal_write(name); terminal_write_line(" ---");
        write_span((const char *)data, length);
        serial_write("\n");
        terminal_write_line("");
    }
}

static void applet_desktop(void) {
    /* The GUI/compositor stack (compositor, DemonX, DemonWM, windowed apps)
       is sidelined -- see sidelined/. This used to be a long list of
       compositor/window-manager claims; keeping any of those "[ready]"
       once the code they described no longer runs would just be false
       advertising, so this only reports the real, still-true console-mode
       subsystems. */
    line("Console readiness:");
    line("  [ready] PS/2 keyboard input");
    line("  [ready] isolated ELF64 application runtime");
    line("  [ready] app-manager.service catalog");
    line("  [ready] project storage and capability handles");
    line("  [ready] dynamic process creation and lifecycle");
    line("  [ready] named capability IPC channels");
    line("  [none] no GUI/compositor -- TTY-only boot (see sidelined/)");
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

// Generalized like real sudo/doas: authorizes and runs any MakoBox
// command with elevated (root) identity for that one call, not just
// systemctl mutations -- those just happen to be the one thing that's
// otherwise refused (see the systemctl start/stop/restart gate above).
static bool applet_runas(const char *command) {
    if (command[0] == '\0') {
        line("usage: runas <command>");
        return false;
    }
    if (!runas_authorize(command)) {
        line("runas: policy denied command");
        return false;
    }
    line("runas: policy granted local-console administrator role");
    running_elevated = true;
    const bool result = makobox_run(command);
    running_elevated = false;
    return result;
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
    else if (equal(command_line, "git version"))
        line("MAKO Git stage 1 (native, real content snapshots, no host binary)");
    else if (equal(command_line, "git init")) {
        if (!git_repository_init()) return false;
        line("Initialized MAKO project worktree");
    }
    else if (starts_with(command_line, "git add ", &argument)) return applet_git_add(argument);
    else if (starts_with(command_line, "git commit ", &argument)) return applet_git_commit(argument);
    else if (equal(command_line, "git commit")) return applet_git_commit("");
    else if (equal(command_line, "git log")) applet_git_log();
    else if (equal(command_line, "git diff")) applet_git_diff("");
    else if (starts_with(command_line, "git diff ", &argument)) applet_git_diff(argument);
    else if (starts_with(command_line, "git show ", &argument)) {
        uint64_t hash = 0u;
        for (const char *hex = argument; *hex != '\0'; ++hex) {
            uint8_t digit;
            if (*hex >= '0' && *hex <= '9') digit = (uint8_t)(*hex - '0');
            else if (*hex >= 'a' && *hex <= 'f') digit = (uint8_t)(*hex - 'a' + 10);
            else if (*hex >= 'A' && *hex <= 'F') digit = (uint8_t)(*hex - 'A' + 10);
            else { line("git show: invalid commit hash"); hash = 0u; goto git_show_done; }
            hash = (hash << 4u) | digit;
        }
        applet_git_show(hash);
        git_show_done:;
    }
    else if (equal(command_line, "desktop") || equal(command_line, "desktop status")) applet_desktop();
    else if (equal(command_line, "systemctl") || equal(command_line, "systemctl list-units"))
        applet_systemctl_status("");
    else if (starts_with(command_line, "systemctl status ", &argument))
        return applet_systemctl_status(argument);
    else if (starts_with(command_line, "systemctl start ", &argument) ||
             starts_with(command_line, "systemctl stop ", &argument) ||
             starts_with(command_line, "systemctl restart ", &argument)) {
        (void)argument;
        if (!running_elevated) {
            line("systemctl: administrative transaction requires runas");
            return false;
        }
        return systemctl_mutate(command_line);
    }
    else if (equal(command_line, "runas")) return applet_runas("");
    else if (starts_with(command_line, "runas ", &argument)) return applet_runas(argument);
    else if (equal(command_line, "ls")) applet_ls("");
    else if (starts_with(command_line, "ls ", &command_line)) applet_ls(command_line);
    else if (equal(command_line, "mko") || equal(command_line, "mko info")) applet_mko();
    else if (equal(command_line, "input")) applet_input();
    else if (equal(command_line, "ipc")) applet_ipc();
    else if (equal(command_line, "fetch")) applet_fetch();
    else if (equal(command_line, "whoami")) applet_whoami();
    else if (equal(command_line, "pwd")) applet_pwd();
    else if (equal(command_line, "hostname")) applet_hostname();
    else if (equal(command_line, "date")) applet_date();
    else if (starts_with(command_line, "echo ", &argument)) applet_echo(argument);
    else if (equal(command_line, "echo")) applet_echo("");
    else if (starts_with(command_line, "cat ", &argument)) applet_cat(argument);
    else if (starts_with(command_line, "head ", &argument)) applet_head(argument);
    else if (starts_with(command_line, "wc ", &argument)) applet_wc(argument);
    else if (starts_with(command_line, "touch ", &argument)) applet_touch(argument);
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
        makobox_run("git status") && makobox_run("git init") &&
        makobox_run("git add project.mko") && makobox_run("git commit -m \"self-test\"") &&
        makobox_run("git log") && makobox_run("git diff") &&
        makobox_run("desktop status") && makobox_run("mko") &&
        makobox_run("input") && makobox_run("ipc") && makobox_run("fetch") &&
        makobox_run("whoami") && makobox_run("runas whoami") &&
        makobox_run("pwd") && makobox_run("hostname") && makobox_run("date") &&
        makobox_run("echo self-test") && makobox_run("ls -la") &&
        makobox_run("cat project.mko") && makobox_run("head project.mko") &&
        makobox_run("wc project.mko") && makobox_run("touch selftest.tmp");
}

__attribute__((noreturn))
void makobox_shell(void) {
    char input[64];
    size_t length = 0;
    /* Clear the visual console only (serial keeps every boot-status line
       that already scrolled by -- smoke tests grep that log). Without this,
       a real interactive session starts buried under dozens of "[ OK ]"
       boot lines and reads like a dmesg dump instead of a shell you just
       landed in. */
    terminal_write("\f");
    line("");
    line("MakoBox interactive console ready. Type 'help'.");
    line("Terminal owns display and keyboard.");
    /* Visual prompt only -- serial_write keeps the plain "mako# " text every
       keyboard/process/vfs smoke test greps for (see the Makefile's
       "mako# help"/"mako# ls /system"/etc. assertions), so it's left alone
       and only the on-screen rendering gets the friendlier [user@host]#
       shape. */
    terminal_write("ROOT-DEMONOS: ");
    serial_write("mako# ");
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
            terminal_write("ROOT-DEMONOS: ");
            serial_write("mako# ");
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

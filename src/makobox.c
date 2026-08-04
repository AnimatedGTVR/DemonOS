#include <kernel/makobox.h>
#include <kernel/apps.h>
#include <kernel/ac97.h>
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
static int16_t bleep_pcm[AC97_MAX_FRAMES * 2u];

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
    line("  doom     launch Doom with the installed Freedoom IWAD");
    line("  classicube  launch the ClassiCube game (ESC pause, Q quit to console)");
    line("  quake / quake-core  boot the Quake engine (D4 full engine boot + frame)");
    line("  beep [Hz [ms]]  play a short square-wave bleep (defaults: 880 80)");
    line("  tone <Hz> [ms]  play a 20-20000 Hz tone for up to 90 ms");
    line("  bleeps   play a short three-note startup chime");
    line("  git ...  real content-snapshot Git (init/add/commit/log/diff/show/status)");
    line("  desktop  report console readiness (no GUI -- see sidelined/)");
    line("  runit list/status units (mutations require runas)");
    line("  runas <command>  run any MakoBox command with elevated (root) rights");
    line("  ls [-la] [dir]  list files under a project-store path (default: /)");
    line("  cat <path>  print a file's contents");
    line("  head [-n N] <path>  print a file's first 10 lines (or N)");
    line("  tail [-n N] <path>  print a file's last 10 lines (or N)");
    line("  wc <path>   count lines/words/bytes in a file");
    line("  touch <path> create an empty file if it doesn't exist");
    line("  write <path> <text>  create/replace a file with text");
    line("  rm <path>   remove a file from the project store");
    line("  cp <src> <dst>  copy a file");
    line("  mv <src> <dst>  move (rename) a file");
    line("  grep <pattern> <path>  print matching lines");
    line("  hexdump <path>  inspect a file as offsets, hex bytes, and ASCII");
    line("  strings <path>  print runs of at least four printable bytes");
    line("  df       show RAMFS storage capacity");
    line("  du [dir] show bytes used under a path");
    line("  free     show memory allocator state");
    line("  uptime   show time since boot");
    line("  stat <path>  show file metadata");
    line("  find [dir]  walk the project store, printing paths");
    line("  tree [dir]  print the project store as an indented tree");
    line("  tac <path>  print a file with lines in reverse order");
    line("  rev <path>  print a file with each line reversed");
    line("  sort <path> print a file's lines sorted");
    line("  uniq <path> print consecutive duplicate lines collapsed");
    line("  cmp <a> <b> compare two files byte by byte");
    line("  diff <a> <b> compare two files line by line");
    line("  basename <path>  print the trailing path segment");
    line("  dirname <path>   print the path with its last segment removed");
    line("  nproc    print the number of logical CPUs");
    line("  seq [first [inc]] last  print a numeric sequence");
    line("  sleep <seconds>  busy-wait for a number of seconds");
    line("  time <command>   run a command and report its duration");
    line("  calc <expr>      evaluate an integer expression (+ - * / % and parens)");
    line("  printf <fmt> [args...]  minimal %s/%d/%u/%x formatter");
    line("  echo <text> print text back");
    line("  whoami   print the current identity (root only inside runas)");
    line("  groups   print the current user's groups");
    line("  id       print uid/gid (root only inside runas)");
    line("  pwd      print the current directory (always /)");
    line("  hostname print this kernel's hostname");
    line("  arch     print the machine architecture");
    line("  tty      print the active console device");
    line("  who/users  show logged-in local-console users");
    line("  env/printenv  show the built-in session environment");
    line("  which/type/command -v <name>  locate a MakoBox command or app");
    line("  date     print the real CMOS/RTC date and time");
    line("  cal      print the current month's calendar");
    line("  history  show the last commands typed in this session");
    line("  kill <pid>  terminate a task by PID (137 on success)");
    line("  true/false   succeed or fail, like their Unix namesakes");
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
// applet_runas/makobox_run's runit gate) -- real sudo-style: the shell
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

// A length-bounded slice of a RAMFS file's raw bytes, delimited by a '\n'
// (or the end of the file). Points straight into the storage arena, so the
// data stays valid for as long as the file is open and unmodified.
struct line_view {
    const char *start;
    size_t length;
    bool newline;
};

static bool next_line(const uint8_t *data, size_t length, size_t *offset,
                      struct line_view *line_view) {
    if (*offset >= length) return false;
    const size_t start = *offset;
    size_t end = start;
    while (end < length && data[end] != '\n') ++end;
    line_view->start = (const char *)&data[start];
    line_view->length = end - start;
    line_view->newline = end < length;
    *offset = end + (end < length ? 1u : 0u);
    return true;
}

static void print_line_view(const struct line_view *line_view) {
    write_span(line_view->start, line_view->length);
    serial_write("\n");
    terminal_write_line("");
}

// Shared open+view for the read-only file applets -- returns a pointer to
// the file's bytes (and its length) on success.
static bool open_file_view(const char *path, const uint8_t **data, size_t *length) {
    uint32_t object_id;
    return ramfs_open(path, string_length(path), false, &object_id) &&
        ramfs_view(object_id, data, length);
}

// A 32-bit unsigned decimal parse -- freestanding, no atoi/strtoul here.
static bool parse_u32(const char *text, uint32_t *out) {
    if (text[0] == '\0') return false;
    uint32_t value = 0u;
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return false;
        value = value * 10u + (uint32_t)(*c - '0');
    }
    *out = value;
    return true;
}

// Parses an optional leading `-n N` (head/tail style); default is 10.
// On success `*path` points past the flag token. The count is scanned
// digit-by-digit since the remainder of the line is the path, not just
// digits.
static bool parse_n_option(const char *argument, unsigned *lines, const char **path) {
    *lines = 10u;
    *path = argument;
    if (!starts_with(argument, "-n ", &argument)) return true;
    uint32_t count = 0u;
    bool any_digit = false;
    while (argument[0] >= '0' && argument[0] <= '9') {
        any_digit = true;
        count = count * 10u + (uint32_t)(argument[0] - '0');
        ++argument;
    }
    if (!any_digit) return false;
    while (argument[0] == ' ') ++argument;
    if (argument[0] == '\0') return false;
    *lines = count > 100000u ? 100000u : (unsigned)count;
    *path = argument;
    return true;
}

// Length-aware line comparison (no strcmp/strlen dependence).
static int compare_line_views(const struct line_view *left, const struct line_view *right) {
    const size_t common = left->length < right->length ? left->length : right->length;
    for (size_t i = 0u; i < common; ++i) {
        const unsigned char a = (unsigned char)left->start[i];
        const unsigned char b = (unsigned char)right->start[i];
        if (a < b) return -1;
        if (a > b) return 1;
    }
    if (left->length < right->length) return -1;
    if (left->length > right->length) return 1;
    return 0;
}

// Sorting table for `sort`; views point into the (unchanged) RAMFS arena.
#define SORT_MAX_LINES 256u
static struct line_view sort_lines[SORT_MAX_LINES];

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

static void applet_head(const char *argument) {
    unsigned lines;
    const char *path;
    if (!parse_n_option(argument, &lines, &path) || path[0] == '\0') {
        line("usage: head [-n N] <path>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("head: no such file");
        return;
    }
    size_t offset = 0u;
    struct line_view current;
    unsigned shown = 0u;
    while (next_line(data, length, &offset, &current) && shown < lines) {
        print_line_view(&current);
        ++shown;
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

static bool split_two_args(const char *argument, char *first,
                           size_t first_capacity, const char **second);

static void applet_touch(const char *path) {
    if (path[0] == '\0') { line("usage: touch <path>"); return; }
    uint32_t object_id;
    if (!ramfs_open(path, string_length(path), true, &object_id)) {
        line("touch: could not create file");
        return;
    }
    line("touched");
}

static void applet_write(const char *argument) {
    char path[GIT_NAME_MAX + 1u];
    const char *text;
    if (!split_two_args(argument, path, sizeof(path), &text)) {
        line("usage: write <path> <text>");
        return;
    }
    uint32_t object_id;
    const size_t length = string_length(text);
    if (!ramfs_open(path, string_length(path), true, &object_id) ||
        !ramfs_write(object_id, (const uint8_t *)text, length)) {
        line("write: could not create or replace file");
        return;
    }
    value_line("wrote ", (uint64_t)length, " bytes");
}

static void write_hex_byte(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[3] = {digits[value >> 4u], digits[value & 15u], '\0'};
    serial_write(text);
    terminal_write(text);
}

static void applet_hexdump(const char *path) {
    if (path[0] == '\0') { line("usage: hexdump <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("hexdump: no such file");
        return;
    }
    for (size_t offset = 0u; offset < length; offset += 16u) {
        serial_write_hex((uint64_t)offset); terminal_write_hex((uint64_t)offset);
        serial_write("  "); terminal_write("  ");
        for (size_t column = 0u; column < 16u; ++column) {
            if (offset + column < length) write_hex_byte(data[offset + column]);
            else { serial_write("  "); terminal_write("  "); }
            serial_write(column == 7u ? "  " : " ");
            terminal_write(column == 7u ? "  " : " ");
        }
        serial_write(" |"); terminal_write(" |");
        for (size_t column = 0u; column < 16u && offset + column < length; ++column) {
            const uint8_t value = data[offset + column];
            char visible[2] = {(char)(value >= 32u && value <= 126u ? value : '.'), '\0'};
            serial_write(visible); terminal_write(visible);
        }
        serial_write("|\n"); terminal_write_line("|");
    }
    value_line("bytes: ", (uint64_t)length, NULL);
}

static void applet_strings(const char *path) {
    if (path[0] == '\0') { line("usage: strings <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("strings: no such file");
        return;
    }
    size_t start = 0u;
    for (size_t index = 0u; index <= length; ++index) {
        const bool printable = index < length && data[index] >= 32u && data[index] <= 126u;
        if (printable) continue;
        if (index - start >= 4u) {
            write_span((const char *)&data[start], index - start);
            serial_write("\n"); terminal_write_line("");
        }
        start = index + 1u;
    }
}

static void applet_rm(const char *path) {
    if (path[0] == '\0') { line("usage: rm <path>"); return; }
    if (!ramfs_delete(path, string_length(path))) {
        line("rm: no such file");
        return;
    }
    line("removed");
}

// Splits "first second" on the first space -- real cp/mv/grep argument
// shapes are more forgiving than this (quoting, multiple spaces in a
// name), but this is a real, working split for the common case, not a
// fake one.
static bool split_two_args(const char *argument, char *first, size_t first_capacity,
                           const char **second) {
    size_t i = 0u;
    while (argument[i] != '\0' && argument[i] != ' ' && i + 1u < first_capacity) {
        first[i] = argument[i];
        ++i;
    }
    if (i == 0u || argument[i] != ' ') return false;
    first[i] = '\0';
    ++i;
    while (argument[i] == ' ') ++i;
    if (argument[i] == '\0') return false;
    *second = &argument[i];
    return true;
}

// Copies a file's raw bytes into a new (or existing) destination entry.
// Shared by `cp` (which reports the byte count) and `mv` (which stays
// quiet like real mv and deletes the source afterwards).
static bool copy_file(const char *src, size_t src_length,
                      const char *dst, size_t dst_length,
                      size_t *copied) {
    uint32_t src_id;
    const uint8_t *data;
    size_t length;
    if (!ramfs_open(src, src_length, false, &src_id) ||
        !ramfs_view(src_id, &data, &length))
        return false;
    uint32_t dst_id;
    if (!ramfs_open(dst, dst_length, true, &dst_id) ||
        !ramfs_write(dst_id, data, length))
        return false;
    *copied = length;
    return true;
}

static void applet_cp(const char *argument) {
    char src[GIT_NAME_MAX + 1u];
    const char *dst;
    if (!split_two_args(argument, src, sizeof(src), &dst)) {
        line("usage: cp <src> <dst>");
        return;
    }
    size_t copied;
    if (!copy_file(src, string_length(src), dst, string_length(dst), &copied)) {
        line("cp: could not read source or write destination");
        return;
    }
    value_line("copied ", (uint64_t)copied, " bytes");
}

static void applet_mv(const char *argument) {
    char src[GIT_NAME_MAX + 1u];
    const char *dst;
    if (!split_two_args(argument, src, sizeof(src), &dst)) {
        line("usage: mv <src> <dst>");
        return;
    }
    size_t copied;
    if (!copy_file(src, string_length(src), dst, string_length(dst), &copied)) {
        line("mv: could not read source or write destination");
        return;
    }
    (void)copied;
    if (!ramfs_delete(src, string_length(src))) {
        line("mv: could not remove source");
        return;
    }
}

static bool contains(const char *haystack, size_t haystack_length,
                     const char *needle, size_t needle_length) {
    if (needle_length == 0u) return true;
    for (size_t start = 0u; start + needle_length <= haystack_length; ++start) {
        bool match = true;
        for (size_t i = 0u; i < needle_length; ++i)
            if (haystack[start + i] != needle[i]) { match = false; break; }
        if (match) return true;
    }
    return false;
}

static void applet_grep(const char *argument) {
    char pattern[GIT_NAME_MAX + 1u];
    const char *path;
    if (!split_two_args(argument, pattern, sizeof(pattern), &path)) {
        line("usage: grep <pattern> <path>");
        return;
    }
    uint32_t object_id;
    const uint8_t *data;
    size_t length;
    if (!ramfs_open(path, string_length(path), false, &object_id) ||
        !ramfs_view(object_id, &data, &length)) {
        line("grep: no such file");
        return;
    }
    const size_t pattern_length = string_length(pattern);
    size_t start = 0u;
    unsigned matches = 0u;
    for (size_t i = 0u; i < length; ++i) {
        if (data[i] == '\n' || i == length - 1u) {
            const size_t end = data[i] == '\n' ? i : i + 1u;
            if (contains((const char *)&data[start], end - start, pattern, pattern_length)) {
                write_span((const char *)&data[start], end - start);
                serial_write("\n");
                terminal_write_line("");
                ++matches;
            }
            start = i + 1u;
        }
    }
    if (matches == 0u) line("grep: no matches");
}

static void applet_tail(const char *argument) {
    unsigned lines;
    const char *path;
    if (!parse_n_option(argument, &lines, &path) || path[0] == '\0') {
        line("usage: tail [-n N] <path>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("tail: no such file");
        return;
    }
    // Two passes, no storage: count lines once, then walk again printing
    // only the final `lines` of them.
    size_t total = 0u;
    size_t offset = 0u;
    struct line_view scratch;
    while (next_line(data, length, &offset, &scratch)) ++total;
    const size_t skip = total > lines ? total - lines : 0u;
    offset = 0u;
    struct line_view current;
    for (size_t index = 0u; next_line(data, length, &offset, &current); ++index)
        if (index >= skip) print_line_view(&current);
}

static void applet_tac(const char *path) {
    if (path[0] == '\0') { line("usage: tac <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("tac: no such file");
        return;
    }
    // Bounded line-start index table so the last N lines can be re-walked
    // in reverse without copying any file content.
    size_t offsets[256];
    size_t count = 0u;
    size_t offset = 0u;
    struct line_view current;
    while (next_line(data, length, &offset, &current)) {
        if (count >= 256u) { line("tac: file has more than 256 lines"); return; }
        offsets[count++] = offset - current.length - (current.newline ? 1u : 0u);
    }
    for (size_t index = count; index > 0u; --index) {
        offset = offsets[index - 1u];
        (void)next_line(data, length, &offset, &current);
        print_line_view(&current);
    }
}

static void applet_rev(const char *path) {
    if (path[0] == '\0') { line("usage: rev <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("rev: no such file");
        return;
    }
    size_t offset = 0u;
    struct line_view current;
    while (next_line(data, length, &offset, &current)) {
        for (size_t index = current.length; index > 0u; --index) {
            char ch[2] = { current.start[index - 1u], '\0' };
            serial_write(ch);
            terminal_write(ch);
        }
        serial_write("\n");
        terminal_write_line("");
    }
}

static void applet_sort(const char *path) {
    if (path[0] == '\0') { line("usage: sort <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("sort: no such file");
        return;
    }
    size_t count = 0u;
    size_t offset = 0u;
    struct line_view current;
    while (next_line(data, length, &offset, &current)) {
        if (count >= SORT_MAX_LINES) { line("sort: file has more than 256 lines"); return; }
        sort_lines[count++] = current;
    }
    for (size_t i = 0u; i < count; ++i) {
        size_t best = i;
        for (size_t j = i + 1u; j < count; ++j)
            if (compare_line_views(&sort_lines[j], &sort_lines[best]) < 0) best = j;
        const struct line_view temp = sort_lines[i];
        sort_lines[i] = sort_lines[best];
        sort_lines[best] = temp;
    }
    for (size_t i = 0u; i < count; ++i) print_line_view(&sort_lines[i]);
}

static void applet_uniq(const char *path) {
    if (path[0] == '\0') { line("usage: uniq <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!open_file_view(path, &data, &length)) {
        line("uniq: no such file");
        return;
    }
    size_t offset = 0u;
    struct line_view current;
    struct line_view previous;
    bool have_previous = false;
    while (next_line(data, length, &offset, &current)) {
        if (!have_previous || compare_line_views(&current, &previous) != 0) {
            print_line_view(&current);
            previous = current;
            have_previous = true;
        }
    }
}

static void applet_cmp(const char *argument) {
    char first[GIT_NAME_MAX + 1u];
    const char *second;
    if (!split_two_args(argument, first, sizeof(first), &second)) {
        line("usage: cmp <file1> <file2>");
        return;
    }
    const uint8_t *a_data, *b_data;
    size_t a_length, b_length;
    if (!open_file_view(first, &a_data, &a_length) ||
        !open_file_view(second, &b_data, &b_length)) {
        line("cmp: no such file");
        return;
    }
    const size_t common = a_length < b_length ? a_length : b_length;
    for (size_t i = 0u; i < common; ++i) {
        if (a_data[i] != b_data[i]) {
            value_line("cmp: first difference at byte ", (uint64_t)i, NULL);
            return;
        }
    }
    if (a_length != b_length) {
        value_line("cmp: files differ in size: first=", (uint64_t)a_length, " bytes");
        value_line("                            second=", (uint64_t)b_length, " bytes");
        return;
    }
    line("cmp: files identical");
}

// A real line-by-line comparison, but aligned by index rather than by
// longest-common-subsequence: each line N of file A is compared with line N
// of file B, and the differing pairs are printed with their 1-based line
// number. That's the honest, simple version of diff for a tool this size --
// real diff's LCS alignment would be far more code for the same readout on
// the common "tweaked a few lines" case.
static void applet_diff(const char *argument) {
    char first[GIT_NAME_MAX + 1u];
    const char *second;
    if (!split_two_args(argument, first, sizeof(first), &second)) {
        line("usage: diff <file1> <file2>");
        return;
    }
    const uint8_t *a_data, *b_data;
    size_t a_length, b_length;
    if (!open_file_view(first, &a_data, &a_length) ||
        !open_file_view(second, &b_data, &b_length)) {
        line("diff: no such file");
        return;
    }
    size_t a_off = 0u, b_off = 0u;
    struct line_view a_line, b_line;
    unsigned line_number = 1u;
    unsigned changes = 0u;
    for (;;) {
        const bool have_a = next_line(a_data, a_length, &a_off, &a_line);
        const bool have_b = next_line(b_data, b_length, &b_off, &b_line);
        if (!have_a && !have_b) break;
        if (have_a && have_b && compare_line_views(&a_line, &b_line) == 0) {
            ++line_number;
            continue;
        }
        if (have_a) {
            serial_write("- "); serial_write_u64((uint64_t)line_number); serial_write(": ");
            terminal_write("- "); terminal_write_u64((uint64_t)line_number); terminal_write(": ");
            write_span(a_line.start, a_line.length);
            serial_write("\n"); terminal_write_line("");
        }
        if (have_b) {
            serial_write("+ "); serial_write_u64((uint64_t)line_number); serial_write(": ");
            terminal_write("+ "); terminal_write_u64((uint64_t)line_number); terminal_write(": ");
            write_span(b_line.start, b_line.length);
            serial_write("\n"); terminal_write_line("");
        }
        ++changes;
        ++line_number;
    }
    if (changes == 0u) line("diff: files identical");
}

static void applet_stat(const char *path) {
    if (path[0] == '\0') { line("usage: stat <path>"); return; }
    uint32_t object_id;
    size_t length;
    if (ramfs_open(path, string_length(path), false, &object_id) &&
        ramfs_size(object_id, &length)) {
        line("File:");
        serial_write("  "); serial_write(path); serial_write("\n");
        terminal_write("  "); terminal_write_line(path);
        value_line("  size: ", (uint64_t)length, " bytes");
        line("  type: regular file (RAMFS project store)");
        return;
    }
    size_t entries = 0u;
    for (size_t index = 0u;
         ramfs_list(path, string_length(path), index, NULL, NULL, NULL, NULL);
         ++index)
        ++entries;
    if (entries > 0u) {
        line("Directory:");
        serial_write("  "); serial_write(path); serial_write("\n");
        terminal_write("  "); terminal_write_line(path);
        value_line("  entries: ", (uint64_t)entries, NULL);
        return;
    }
    line("stat: no such file or directory");
}

// Recursively walks the RAMFS tree under `path`, printing every file's
// full path and byte length (dirs get a trailing '/'). `path` is "" for
// the root. Builds each child path in a bounded buffer.
static void find_walk(const char *path, size_t path_length) {
    size_t index = 0u;
    for (;;) {
        const char *rel_name;
        size_t rel_length;
        size_t length;
        bool is_directory;
        if (!ramfs_list(path, path_length, index++, &rel_name, &rel_length, &length, &is_directory))
            break;
        char child[256];
        size_t base = 0u;
        if (path_length > 0u) {
            if (path_length + 2u >= sizeof(child)) { line("find: path too deep"); return; }
            for (size_t i = 0u; i < path_length; ++i) child[base++] = path[i];
            if (child[base - 1u] != '/') child[base++] = '/';
        }
        for (size_t i = 0u; i < rel_length && base + 1u < sizeof(child); ++i)
            child[base++] = rel_name[i];
        child[base] = '\0';
        if (is_directory) {
            serial_write(child); serial_write("/\n");
            terminal_write(child); terminal_write_line("/");
            find_walk(child, base);
        } else {
            serial_write(child); serial_write("  "); serial_write_u64((uint64_t)length); serial_write(" bytes\n");
            terminal_write(child); terminal_write("  "); terminal_write_u64((uint64_t)length); terminal_write_line(" bytes");
        }
    }
}

static void applet_find(const char *argument) {
    find_walk(argument, string_length(argument));
}

static void tree_walk(const char *path, size_t path_length, unsigned depth) {
    size_t index = 0u;
    for (;;) {
        const char *rel_name;
        size_t rel_length;
        size_t length;
        bool is_directory;
        if (!ramfs_list(path, path_length, index++, &rel_name, &rel_length, &length, &is_directory))
            break;
        char out[256];
        size_t pos = 0u;
        for (unsigned d = 0u; d < depth && pos + 2u < sizeof(out); ++d) {
            out[pos++] = ' ';
            out[pos++] = ' ';
        }
        for (size_t i = 0u; i < rel_length && pos + 2u < sizeof(out); ++i) out[pos++] = rel_name[i];
        if (is_directory) out[pos++] = '/';
        else out[pos++] = ' ';
        out[pos] = '\0';
        if (is_directory) {
            serial_write(out); serial_write("\n");
            terminal_write_line(out);
            char child[256];
            size_t base = 0u;
            if (path_length > 0u) {
                if (path_length + 2u >= sizeof(child)) { line("tree: path too deep"); return; }
                for (size_t i = 0u; i < path_length; ++i) child[base++] = path[i];
                if (child[base - 1u] != '/') child[base++] = '/';
            }
            for (size_t i = 0u; i < rel_length && base + 1u < sizeof(child); ++i) child[base++] = rel_name[i];
            child[base] = '\0';
            tree_walk(child, base, depth + 1u);
        } else {
            serial_write(out); serial_write_u64((uint64_t)length); serial_write(" bytes\n");
            terminal_write(out); terminal_write_u64((uint64_t)length); terminal_write_line(" bytes");
        }
    }
}

static void applet_tree(const char *argument) {
    tree_walk(argument, string_length(argument), 0u);
}

// Recursively sums every leaf byte under `path`, printing each immediate
// child's total as it goes (dirs print their subtree totals).
static void du_walk(const char *path, size_t path_length, uint64_t *total) {
    size_t index = 0u;
    for (;;) {
        const char *rel_name;
        size_t rel_length;
        size_t length;
        bool is_directory;
        if (!ramfs_list(path, path_length, index++, &rel_name, &rel_length, &length, &is_directory))
            break;
        char child[256];
        size_t base = 0u;
        if (path_length > 0u) {
            if (path_length + 2u >= sizeof(child)) { line("du: path too deep"); return; }
            for (size_t i = 0u; i < path_length; ++i) child[base++] = path[i];
            if (child[base - 1u] != '/') child[base++] = '/';
        }
        for (size_t i = 0u; i < rel_length && base + 1u < sizeof(child); ++i) child[base++] = rel_name[i];
        child[base] = '\0';
        if (is_directory) {
            uint64_t sub = 0u;
            du_walk(child, base, &sub);
            serial_write(child); serial_write("/  "); serial_write_u64(sub); serial_write(" bytes\n");
            terminal_write(child); terminal_write("/  "); terminal_write_u64(sub); terminal_write_line(" bytes");
            *total += sub;
        } else {
            serial_write(child); serial_write("  "); serial_write_u64((uint64_t)length); serial_write(" bytes\n");
            terminal_write(child); terminal_write("  "); terminal_write_u64((uint64_t)length); terminal_write_line(" bytes");
            *total += (uint64_t)length;
        }
    }
}

static void applet_du(const char *argument) {
    uint64_t total = 0u;
    du_walk(argument, string_length(argument), &total);
    value_line("total: ", total, " bytes");
}

static void applet_basename(const char *path) {
    if (path[0] == '\0') { line("usage: basename <path>"); return; }
    const char *start = path;
    for (const char *c = path; *c != '\0'; ++c)
        if (*c == '/') start = c + 1u;
    line(start);
}

static void applet_dirname(const char *path) {
    if (path[0] == '\0') { line("usage: dirname <path>"); return; }
    const char *last_slash = NULL;
    for (const char *c = path; *c != '\0'; ++c)
        if (*c == '/') last_slash = c;
    if (last_slash == NULL) { line("."); return; }
    if (last_slash == path) { line("/"); return; }
    char buffer[256];
    size_t length = (size_t)(last_slash - path);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1u;
    for (size_t i = 0u; i < length; ++i) buffer[i] = path[i];
    buffer[length] = '\0';
    line(buffer);
}

static void applet_nproc(void) {
    uint32_t ebx;
    __asm__ volatile ("cpuid"
        : "=b"(ebx)
        : "a"(1u)
        : "ecx", "edx");
    const uint32_t count = ((ebx >> 16u) & 0xFFu) + 1u;
    value_line("logical CPUs: ", (uint64_t)count, NULL);
}

// Signed 64-bit decimal parse -- freestanding, no strtol here.
static bool parse_i64(const char *text, int64_t *out) {
    if (text[0] == '\0') return false;
    const char *c = text;
    bool negative = false;
    if (*c == '-' || *c == '+') { negative = (*c == '-'); ++c; }
    if (*c == '\0') return false;
    int64_t value = 0;
    for (; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return false;
        if (value > 922337203685477580ll) return false; // keep value < INT64_MAX/10
        value = value * 10 + (int64_t)(*c - '0');
    }
    *out = negative ? -value : value;
    return true;
}

static void signed_value_line(const char *label, int64_t value, const char *suffix) {
    serial_write(label); terminal_write(label);
    if (value < 0) {
        serial_write("-"); terminal_write("-");
        value = -value;
    }
    serial_write_u64((uint64_t)value); terminal_write_u64((uint64_t)value);
    if (suffix != NULL) { serial_write(suffix); terminal_write(suffix); }
    serial_write("\n"); terminal_write_line("");
}

// seq takes one, two, or three args: last / first last / first step last.
static bool parse_seq_args(const char *argument, int64_t *first, int64_t *step, int64_t *last) {
    char first_token[GIT_NAME_MAX + 1u];
    const char *rest;
    if (!split_two_args(argument, first_token, sizeof(first_token), &rest)) {
        if (!parse_i64(argument, last)) return false;
        *first = 1;
        *step = 1;
        return true;
    }
    char second_token[GIT_NAME_MAX + 1u];
    const char *third;
    if (!split_two_args(rest, second_token, sizeof(second_token), &third)) {
        if (!parse_i64(first_token, first) || !parse_i64(rest, last)) return false;
        *step = 1;
        return true;
    }
    return parse_i64(first_token, first) && parse_i64(second_token, step) &&
        parse_i64(third, last);
}

static void applet_seq(const char *argument) {
    int64_t first, step, last;
    if (!parse_seq_args(argument, &first, &step, &last) || step == 0) {
        line("usage: seq [first [step]] last");
        return;
    }
    int64_t value = first;
    for (;;) {
        signed_value_line("", value, NULL);
        if (step > 0) {
            if (value > last - step) break;
            value += step;
        } else {
            if (value < last - step) break;
            value += step;
        }
    }
}

static void applet_sleep(const char *argument) {
    uint32_t seconds;
    if (!parse_u32(argument, &seconds)) {
        line("usage: sleep <seconds>");
        return;
    }
    const uint64_t target = interrupts_timer_ticks() + (uint64_t)seconds * 100u;
    while (interrupts_timer_ticks() < target) __asm__ volatile ("hlt");
    value_line("slept ", (uint64_t)seconds, " seconds");
}

static bool play_bleep(uint32_t frequency, uint32_t milliseconds) {
    if (!ac97_available()) {
        line("beep: no AC'97 output device is available");
        return false;
    }
    if (frequency < 20u || frequency > 20000u ||
        milliseconds == 0u || milliseconds > 90u) {
        line("beep: frequency must be 20-20000 Hz; duration must be 1-90 ms");
        return false;
    }
    size_t frames = ((size_t)AC97_SAMPLE_RATE * milliseconds) / 1000u;
    if (frames == 0u) frames = 1u;
    uint32_t phase = 0u;
    for (size_t frame = 0u; frame < frames; ++frame) {
        int32_t amplitude = 7000;
        /* A tiny linear envelope avoids the harsh clicks caused by starting
           or stopping a square wave at full amplitude. */
        if (frame < 96u) amplitude = amplitude * (int32_t)frame / 96;
        const size_t remaining = frames - frame;
        if (remaining < 96u) amplitude = amplitude * (int32_t)remaining / 96;
        const int16_t sample = (int16_t)(phase < AC97_SAMPLE_RATE / 2u
            ? amplitude : -amplitude);
        bleep_pcm[frame * 2u] = sample;
        bleep_pcm[frame * 2u + 1u] = sample;
        phase += frequency;
        if (phase >= AC97_SAMPLE_RATE) phase %= AC97_SAMPLE_RATE;
    }
    if (!ac97_submit(bleep_pcm, frames)) {
        line("beep: audio device is busy; try again");
        return false;
    }
    serial_write("BLEEP_PLAYED frequency="); serial_write_u64(frequency);
    serial_write(" duration_ms="); serial_write_u64(milliseconds);
    serial_write("\n");
    return true;
}

static bool parse_tone_args(const char *argument, uint32_t default_frequency,
                            uint32_t *frequency, uint32_t *milliseconds) {
    *frequency = default_frequency;
    *milliseconds = 80u;
    if (argument[0] == '\0') return true;
    char frequency_text[16];
    const char *duration_text;
    if (!split_two_args(argument, frequency_text, sizeof(frequency_text),
                        &duration_text))
        return parse_u32(argument, frequency);
    return parse_u32(frequency_text, frequency) &&
        parse_u32(duration_text, milliseconds);
}

static bool applet_beep(const char *argument, bool frequency_required) {
    uint32_t frequency;
    uint32_t milliseconds;
    if ((frequency_required && argument[0] == '\0') ||
        !parse_tone_args(argument, 880u, &frequency, &milliseconds)) {
        line(frequency_required ? "usage: tone <Hz> [milliseconds]"
                                : "usage: beep [Hz [milliseconds]]");
        return false;
    }
    return play_bleep(frequency, milliseconds);
}

static bool applet_bleeps(void) {
    static const uint32_t notes[] = { 523u, 659u, 784u };
    for (size_t index = 0u; index < sizeof(notes) / sizeof(notes[0]); ++index) {
        if (!play_bleep(notes[index], 70u)) return false;
        const uint64_t end = interrupts_timer_ticks() + 9u;
        while (interrupts_timer_ticks() < end) __asm__ volatile ("hlt");
    }
    return true;
}

static void applet_groups(void) {
    line(running_elevated ? "root" : "mako");
}

static bool applet_true(void) { return true; }
static bool applet_false(void) { return false; }

// A tiny recursive-descent integer expression evaluator (no libc): + - * /
// %, unary plus/minus, and parentheses, on 64-bit signed values.
struct calc_parser {
    const char *cursor;
    bool error;
};

static void calc_skip_spaces(struct calc_parser *parser) {
    while (parser->cursor[0] == ' ') ++parser->cursor;
}

static int64_t calc_expression(struct calc_parser *parser);
static int64_t calc_term(struct calc_parser *parser);

static int64_t calc_factor(struct calc_parser *parser) {
    calc_skip_spaces(parser);
    if (parser->error) return 0;
    if (parser->cursor[0] == '-') { ++parser->cursor; return -calc_factor(parser); }
    if (parser->cursor[0] == '+') { ++parser->cursor; return calc_factor(parser); }
    if (parser->cursor[0] == '(') {
        ++parser->cursor;
        const int64_t value = calc_expression(parser);
        calc_skip_spaces(parser);
        if (parser->cursor[0] != ')') { parser->error = true; return 0; }
        ++parser->cursor;
        return value;
    }
    if (parser->cursor[0] < '0' || parser->cursor[0] > '9') { parser->error = true; return 0; }
    int64_t value = 0;
    while (parser->cursor[0] >= '0' && parser->cursor[0] <= '9') {
        value = value * 10 + (int64_t)(parser->cursor[0] - '0');
        ++parser->cursor;
    }
    return value;
}

static int64_t calc_term(struct calc_parser *parser) {
    int64_t value = calc_factor(parser);
    for (;;) {
        calc_skip_spaces(parser);
        const char op = parser->cursor[0];
        if (op != '*' && op != '/' && op != '%') return value;
        ++parser->cursor;
        const int64_t right = calc_factor(parser);
        if (op == '*') value *= right;
        else if (op == '/') {
            if (right == 0) { parser->error = true; return 0; }
            value /= right;
        } else {
            if (right == 0) { parser->error = true; return 0; }
            value %= right;
        }
    }
}

static int64_t calc_expression(struct calc_parser *parser) {
    int64_t value = calc_term(parser);
    for (;;) {
        calc_skip_spaces(parser);
        const char op = parser->cursor[0];
        if (op != '+' && op != '-') return value;
        ++parser->cursor;
        const int64_t right = calc_term(parser);
        if (op == '+') value += right;
        else value -= right;
    }
}

static void applet_calc(const char *argument) {
    if (argument[0] == '\0') { line("usage: calc <expression>"); return; }
    struct calc_parser parser = { argument, false };
    const int64_t value = calc_expression(&parser);
    calc_skip_spaces(&parser);
    if (parser.error || parser.cursor[0] != '\0') {
        line("calc: invalid expression");
        return;
    }
    signed_value_line("= ", value, NULL);
}

struct arg_iter {
    const char *cursor;
};

static bool next_arg(char *out, size_t capacity, struct arg_iter *iter) {
    while (iter->cursor[0] == ' ') ++iter->cursor;
    if (iter->cursor[0] == '\0') return false;
    size_t pos = 0u;
    while (iter->cursor[0] != '\0' && iter->cursor[0] != ' ' && pos + 1u < capacity) {
        out[pos++] = iter->cursor[0];
        ++iter->cursor;
    }
    out[pos] = '\0';
    return true;
}

static void printf_put_char(char value) {
    char buffer[2] = { value, '\0' };
    serial_write(buffer);
    terminal_write(buffer);
}

static void printf_put_string(const char *text) {
    serial_write(text);
    terminal_write(text);
}

static void printf_put_u64(uint64_t value, bool hex) {
    char buffer[24];
    size_t pos = 0u;
    if (value == 0u) { printf_put_string("0"); return; }
    char rev[24];
    size_t n = 0u;
    if (hex) {
        static const char digits[] = "0123456789abcdef";
        while (value != 0u) { rev[n++] = digits[value & 0xFu]; value >>= 4u; }
    } else {
        while (value != 0u) { rev[n++] = (char)('0' + value % 10u); value /= 10u; }
    }
    while (n > 0u) buffer[pos++] = rev[--n];
    buffer[pos] = '\0';
    printf_put_string(buffer);
}

static uint64_t printf_arg_u64(const char *text) {
    uint64_t value = 0u;
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return 0u;
        value = value * 10u + (uint64_t)(*c - '0');
    }
    return value;
}

// A minimal printf: %s, %d, %u, %x, and %%. The format must not contain a
// space (the shell passes the whole command line as one string), and args
// are the space-separated tokens that follow it.
static void applet_printf(const char *argument) {
    char format[64];
    const char *rest;
    if (!split_two_args(argument, format, sizeof(format), &rest)) {
        if (argument[0] == '\0') { line("usage: printf <format> [args...]"); return; }
        printf_put_string(argument);
        serial_write("\n"); terminal_write_line("");
        return;
    }
    struct arg_iter iter = { rest };
    for (const char *f = format; *f != '\0'; ++f) {
        if (*f != '%') { printf_put_char(*f); continue; }
        ++f;
        if (*f == '\0' || *f == '%') { printf_put_char('%'); continue; }
        char arg[64];
        if (!next_arg(arg, sizeof(arg), &iter)) { printf_put_char('%'); printf_put_char(*f); continue; }
        if (*f == 's') printf_put_string(arg);
        else if (*f == 'u') printf_put_u64(printf_arg_u64(arg), false);
        else if (*f == 'x') printf_put_u64(printf_arg_u64(arg), true);
        else if (*f == 'd') {
            int64_t value;
            if (parse_i64(arg, &value)) {
                if (value < 0) { printf_put_char('-'); value = -value; }
                printf_put_u64((uint64_t)value, false);
            } else printf_put_u64(0u, false);
        }
        else { printf_put_char('%'); printf_put_char(*f); }
    }
    serial_write("\n"); terminal_write_line("");
}

static void applet_df(void) {
    line("Filesystem   Used        Capacity");
    serial_write("RAMFS        "); serial_write_u64(ramfs_bytes_used());
    serial_write(" bytes  "); serial_write_u64(ramfs_storage_capacity());
    serial_write(" bytes\n");
    terminal_write("RAMFS        "); terminal_write_u64(ramfs_bytes_used());
    terminal_write(" bytes  "); terminal_write_u64(ramfs_storage_capacity());
    terminal_write_line(" bytes");
    value_line("  files: ", ramfs_file_count(), NULL);
    value_line("  file slots: ", ramfs_max_files(), NULL);
}

static void applet_free(void) {
    line("Memory:");
    value_line("  usable total: ", live.usable_memory_bytes / (1024u * 1024u), " MiB");
    value_line("  kernel image: ", (uint64_t)(live.kernel_size_bytes / 1024u), " KiB");
    hex_line("  next free frame: ", live.frame_next);
    hex_line("  frame allocator end: ", live.frame_end);
}

static void applet_uptime(void) {
    const uint64_t ticks = interrupts_timer_ticks();
    const uint64_t total_seconds = ticks / 100u; // PIT runs at 100 Hz, see HARDWARE_IRQ_SELF_TEST_OK
    const uint64_t hours = total_seconds / 3600u;
    const uint64_t minutes = (total_seconds / 60u) % 60u;
    const uint64_t seconds = total_seconds % 60u;
    serial_write("up "); serial_write_u64(hours); serial_write("h ");
    serial_write_u64(minutes); serial_write("m "); serial_write_u64(seconds);
    serial_write("s ("); serial_write_u64(ticks); serial_write(" ticks @ 100 Hz)\n");
    terminal_write("up "); terminal_write_u64(hours); terminal_write("h ");
    terminal_write_u64(minutes); terminal_write("m "); terminal_write_u64(seconds);
    terminal_write("s ("); terminal_write_u64(ticks); terminal_write_line(" ticks @ 100 Hz)");
}

static void applet_id(void) {
    line(running_elevated ? "uid=0(root) gid=0(root)" : "uid=1000(mako) gid=1000(mako)");
}

#define HISTORY_DEPTH 8u
#define HISTORY_ENTRY_MAX 63u
static char command_history[HISTORY_DEPTH][HISTORY_ENTRY_MAX + 1u];
static size_t history_count;
static size_t history_next;

static void history_record(const char *command_line) {
    char *slot = command_history[history_next % HISTORY_DEPTH];
    size_t i = 0u;
    while (command_line[i] != '\0' && i < HISTORY_ENTRY_MAX) {
        slot[i] = command_line[i];
        ++i;
    }
    slot[i] = '\0';
    ++history_next;
    if (history_count < HISTORY_DEPTH) ++history_count;
}

static void applet_history(void) {
    const size_t oldest = history_next >= history_count ? history_next - history_count : 0u;
    for (size_t i = 0u; i < history_count; ++i) {
        const size_t entry_index = (oldest + i) % HISTORY_DEPTH;
        value_line("  ", (uint64_t)(i + 1u), NULL);
        serial_write("    "); serial_write(command_history[entry_index]); serial_write("\n");
        terminal_write("    "); terminal_write_line(command_history[entry_index]);
    }
    if (history_count == 0u) line("  (empty)");
}

static bool applet_kill(const char *argument) {
    uint32_t pid;
    if (!parse_u32(argument, &pid) || pid == 0u) {
        line("usage: kill <pid>");
        return false;
    }
    if (!scheduler_terminate(pid, 137u)) { // 137 = 128+SIGKILL, same convention as real shells
        line("kill: no such process (or it already exited)");
        return false;
    }
    line("killed");
    return true;
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

struct rtc_date {
    unsigned year;
    unsigned month;
    unsigned day;
};

// Reads and decodes the full CMOS/RTC date (shared by `date` and `cal`).
static void read_rtc(struct rtc_date *out) {
    for (unsigned attempt = 0u; attempt < 1000000u; ++attempt)
        if ((cmos_read(0x0Au) & 0x80u) == 0u) break;
    uint8_t day = cmos_read(0x07u);
    uint8_t month = cmos_read(0x08u);
    uint8_t year = cmos_read(0x09u);
    const uint8_t status_b = cmos_read(0x0Bu);
    if ((status_b & 0x04u) == 0u) { // BCD mode
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }
    out->year = 2000u + year;
    out->month = month;
    out->day = day;
}

static void applet_date(void) {
    struct rtc_date now;
    read_rtc(&now);
    for (unsigned attempt = 0u; attempt < 1000000u; ++attempt)
        if ((cmos_read(0x0Au) & 0x80u) == 0u) break;
    const uint8_t second = bcd_to_binary(cmos_read(0x00u));
    const uint8_t minute = bcd_to_binary(cmos_read(0x02u));
    const uint8_t hour = bcd_to_binary(cmos_read(0x04u) & 0x7Fu);
    char buffer[48];
    int offset = 0;
    buffer[offset++] = (char)('0' + (now.year / 1000u) % 10u);
    buffer[offset++] = (char)('0' + (now.year / 100u) % 10u);
    buffer[offset++] = (char)('0' + (now.year / 10u) % 10u);
    buffer[offset++] = (char)('0' + now.year % 10u);
    buffer[offset++] = '-';
    buffer[offset++] = (char)('0' + now.month / 10u);
    buffer[offset++] = (char)('0' + now.month % 10u);
    buffer[offset++] = '-';
    buffer[offset++] = (char)('0' + now.day / 10u);
    buffer[offset++] = (char)('0' + now.day % 10u);
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

// Day of week for a 1-based Gregorian date: 0 = Sunday. (Sakamoto's
// algorithm; the Gregorian calendar only exists from 1582 but every value
// CMOS can hand us is decades later than that.)
static unsigned day_of_week(unsigned year, unsigned month, unsigned day) {
    static const unsigned table[12] = { 0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u };
    if (month < 3u) year -= 1u;
    return (year + year / 4u - year / 100u + year / 400u + table[month - 1u] + day) % 7u;
}

static void applet_cal(void) {
    struct rtc_date now;
    read_rtc(&now);
    static const char *const month_names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December",
    };
    static const unsigned days_in_month[12] = {
        31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u,
    };
    unsigned days = days_in_month[now.month - 1u];
    if (now.month == 2u &&
        (now.year % 4u == 0u && (now.year % 100u != 0u || now.year % 400u == 0u)))
        days = 29u;
    const unsigned first_dow = day_of_week(now.year, now.month, 1u);
    char heading[32];
    size_t pos = 0u;
    for (const char *name = month_names[now.month - 1u]; *name != '\0' && pos + 2u < sizeof(heading); ++name)
        heading[pos++] = *name;
    heading[pos++] = ' ';
    heading[pos++] = (char)('0' + now.year / 1000u % 10u);
    heading[pos++] = (char)('0' + now.year / 100u % 10u);
    heading[pos++] = (char)('0' + now.year / 10u % 10u);
    heading[pos++] = (char)('0' + now.year % 10u);
    heading[pos] = '\0';
    line(heading);
    line("Su Mo Tu We Th Fr Sa");
    char row[32];
    unsigned day = 1u;
    for (unsigned week = 0u; week < 6u && day <= days; ++week) {
        pos = 0u;
        for (unsigned col = 0u; col < 7u; ++col) {
            if (week == 0u && col < first_dow) {
                row[pos++] = ' '; row[pos++] = ' '; row[pos++] = ' ';
            } else if (day <= days) {
                row[pos++] = ' ';
                row[pos++] = (char)('0' + day / 10u);
                row[pos++] = (char)('0' + day % 10u);
                ++day;
            } else {
                row[pos++] = ' '; row[pos++] = ' '; row[pos++] = ' ';
            }
            row[pos++] = ' ';
        }
        row[pos] = '\0';
        line(row);
    }
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
    const uint64_t seconds = interrupts_timer_ticks() / 100u;
    size_t tasks = 0u;
    for (size_t index = 0u; index < scheduler_task_count(); ++index) {
        struct scheduler_task_snapshot task;
        if (scheduler_snapshot(index, &task) && task.state != SCHEDULER_TASK_UNUSED) ++tasks;
    }
    line("        /\\          mako@demonos");
    line("       /  \\         ------------");
    line("      / /\\ \\        OS: DemonOS 0.1");
    line("     / ____ \\       Kernel: MAKO Kernel 0.1");
    line("    /_/    \\_\\      Arch: x86_64");
    line("                    Shell: MakoBox");
    line("                    Init: runit");
    terminal_write("                    CPU: "); terminal_write_line(vendor);
    serial_write("                    CPU: "); serial_write(vendor); serial_write("\n");
    terminal_write("                    Uptime: "); terminal_write_u64(seconds / 3600u);
    terminal_write("h "); terminal_write_u64((seconds / 60u) % 60u);
    terminal_write("m "); terminal_write_u64(seconds % 60u); terminal_write_line("s");
    serial_write("                    Uptime: "); serial_write_u64(seconds / 3600u);
    serial_write("h "); serial_write_u64((seconds / 60u) % 60u);
    serial_write("m "); serial_write_u64(seconds % 60u); serial_write("s\n");
    value_line("                    Memory: ", live.usable_memory_bytes / (1024u * 1024u), " MiB usable");
    value_line("                    Kernel: ", live.kernel_size_bytes / 1024u, " KiB");
    value_line("                    Tasks: ", tasks, NULL);
    value_line("                    Apps: ", apps_count(), " installed");
    value_line("                    Files: ", ramfs_file_count(), " in RAMFS");
    value_line("                    Storage: ", ramfs_bytes_used(), " bytes used");
    value_line("                    Services: ", init_system_active_count(), " active");
    line(live.native_mko_ready ? "                    MKO: native backend online" :
                                 "                    MKO: unavailable");
    line("                    Colors: [] [] [] [] [] []");
}

static void applet_env(void) {
    line("USER=mako");
    line("LOGNAME=mako");
    line("HOME=/home/mako");
    line("SHELL=/bin/makobox");
    line("PATH=/bin:/system/bin:/apps");
    line("TERM=demon-console");
    line("OS=DemonOS");
    line("ARCH=x86_64");
}

static bool command_exists(const char *name) {
    static const char *const names[] = {
        "help", "uname", "status", "mem", "frames", "paging", "ticks", "ps",
        "abi", "caps", "projects", "apps", "tetris", "doom", "classicube", "quake", "quake-core",
        "nxengine", "nxengine-core", "beep", "tone",
        "bleeps", "git", "desktop", "runit",
        "runas", "ls", "cat", "head", "tail", "wc", "touch", "write", "rm",
        "cp", "mv", "grep", "hexdump", "strings", "df", "du", "free", "uptime",
        "stat", "find", "tree", "tac", "rev", "sort", "uniq", "cmp", "diff",
        "basename", "dirname", "nproc", "seq", "sleep", "time", "calc", "printf",
        "echo", "whoami", "groups", "id", "pwd", "hostname", "date", "cal",
        "history", "kill", "true", "false", "mko", "input", "ipc", "fetch",
        "clear", "arch", "tty", "who", "users", "env", "printenv", "which", "type"
    };
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        if (equal(name, names[i])) return true;
    struct app_snapshot app;
    return apps_find(name, &app) && app.valid_elf64;
}

static bool applet_which(const char *name, bool descriptive) {
    if (name[0] == '\0') { line(descriptive ? "usage: type <name>" : "usage: which <name>"); return false; }
    if (!command_exists(name)) return false;
    struct app_snapshot app;
    if (apps_find(name, &app) && app.valid_elf64) {
        if (descriptive) { terminal_write(name); terminal_write(" is "); terminal_write_line(app.path);
                           serial_write(name); serial_write(" is "); serial_write(app.path); serial_write("\n"); }
        else line(app.path);
    } else {
        if (descriptive) { terminal_write(name); terminal_write_line(" is a MakoBox applet");
                           serial_write(name); serial_write(" is a MakoBox applet\n"); }
        else { terminal_write("/bin/"); terminal_write_line(name);
               serial_write("/bin/"); serial_write(name); serial_write("\n"); }
    }
    return true;
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

static bool applet_runit_status(const char *name) {
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
    line("runit: unit not found");
    return false;
}

static bool runit_mutate(const char *command) {
    const char *name;
    bool result = false;
    if (starts_with(command, "runit start ", &name)) result = init_system_start(name);
    else if (starts_with(command, "runit stop ", &name)) result = init_system_stop(name);
    else if (starts_with(command, "runit restart ", &name)) result = init_system_restart(name);
    else return false;
    if (!result) {
        line("runit: transaction rejected (unknown, immutable, or invalid state)");
        return false;
    }
    line("runit: transaction committed");
    return applet_runit_status(name);
}

// Generalized like real sudo/doas: authorizes and runs any MakoBox
// command with elevated (root) identity for that one call, not just
// runit mutations -- those just happen to be the one thing that's
// otherwise refused (see the runit start/stop/restart gate above).
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
    uint32_t capabilities =
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_CONSOLE) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_PROCESS) |
        CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_INPUT);
    /* Doom is an ordinary userspace application, but unlike the text-mode
       programs it reads its IWAD and presents a pixel surface. Keep those
       broader rights attached to this installed executable rather than
       silently granting them to every application in the catalog. */
    if (equal(app.path, "/system/bin/doom-full.elf")) {
        capabilities |=
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_AUDIO);
    }
    if (equal(app.path, "/system/bin/classicube-core.elf")) {
        capabilities |=
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE);
    }
    if (equal(app.path, "/system/bin/quake-core.elf")) {
        /* PortKit's arena bootstrap and file shims use the storage service;
           the D4 engine boot drives the display/surface services so the
           software renderer can present frames. */
        capabilities |=
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE);
    }
    if (equal(app.path, "/system/bin/nxengine-core.elf")) {
        /* PortKit's arena bootstrap (demon_port_init_dynamic) opens the
           storage service unconditionally; the D3 rendering stage drives
           the display/surface services to present a real frame; D31's
           real sound() implementation opens the audio service (the same
           real AC'97-backed demon_audio_submit path doom-full.elf already
           uses) to submit genuine PCM. */
        capabilities |=
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_STORAGE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_DISPLAY) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE) |
            CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_AUDIO);
    }
    uint32_t pid = userspace_spawn_path(0u, app.path, path_length, app.name,
        capabilities);
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
    else if (equal(command_line, "doom")) return launch_app("doom");
    else if (equal(command_line, "classicube")) return launch_app("classicube");
    else if (equal(command_line, "quake") || equal(command_line, "quake-core")) return launch_app("quake-core");
    else if (equal(command_line, "nxengine") || equal(command_line, "nxengine-core")) return launch_app("nxengine-core");
    else if (equal(command_line, "beep")) return applet_beep("", false);
    else if (starts_with(command_line, "beep ", &argument)) return applet_beep(argument, false);
    else if (equal(command_line, "tone")) return applet_beep("", true);
    else if (starts_with(command_line, "tone ", &argument)) return applet_beep(argument, true);
    else if (equal(command_line, "bleeps")) return applet_bleeps();
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
    else if (equal(command_line, "runit") || equal(command_line, "runit list-units"))
        applet_runit_status("");
    else if (starts_with(command_line, "runit status ", &argument))
        return applet_runit_status(argument);
    else if (starts_with(command_line, "runit start ", &argument) ||
             starts_with(command_line, "runit stop ", &argument) ||
             starts_with(command_line, "runit restart ", &argument)) {
        (void)argument;
        if (!running_elevated) {
            line("runit: administrative transaction requires runas");
            return false;
        }
        return runit_mutate(command_line);
    }
    else if (equal(command_line, "runas")) return applet_runas("");
    else if (starts_with(command_line, "runas ", &argument)) return applet_runas(argument);
    else if (equal(command_line, "ls")) applet_ls("");
    else if (starts_with(command_line, "ls ", &command_line)) applet_ls(command_line);
    else if (equal(command_line, "mko") || equal(command_line, "mko info")) applet_mko();
    else if (equal(command_line, "input")) applet_input();
    else if (equal(command_line, "ipc")) applet_ipc();
    else if (equal(command_line, "fetch")) applet_fetch();
    else if (equal(command_line, "arch")) line("x86_64");
    else if (equal(command_line, "tty")) line("/dev/console");
    else if (equal(command_line, "who") || equal(command_line, "users")) line("mako");
    else if (equal(command_line, "env") || equal(command_line, "printenv")) applet_env();
    else if (starts_with(command_line, "which ", &argument)) return applet_which(argument, false);
    else if (starts_with(command_line, "type ", &argument)) return applet_which(argument, true);
    else if (starts_with(command_line, "command -v ", &argument)) return applet_which(argument, false);
    else if (equal(command_line, "whoami")) applet_whoami();
    else if (equal(command_line, "pwd")) applet_pwd();
    else if (equal(command_line, "hostname")) applet_hostname();
    else if (equal(command_line, "date")) applet_date();
    else if (equal(command_line, "cal")) applet_cal();
    else if (starts_with(command_line, "echo ", &argument)) applet_echo(argument);
    else if (equal(command_line, "echo")) applet_echo("");
    else if (starts_with(command_line, "cat ", &argument)) applet_cat(argument);
    else if (starts_with(command_line, "head ", &argument)) applet_head(argument);
    else if (starts_with(command_line, "tail ", &argument)) applet_tail(argument);
    else if (starts_with(command_line, "tac ", &argument)) applet_tac(argument);
    else if (starts_with(command_line, "rev ", &argument)) applet_rev(argument);
    else if (starts_with(command_line, "sort ", &argument)) applet_sort(argument);
    else if (starts_with(command_line, "uniq ", &argument)) applet_uniq(argument);
    else if (starts_with(command_line, "cmp ", &argument)) applet_cmp(argument);
    else if (starts_with(command_line, "diff ", &argument)) applet_diff(argument);
    else if (starts_with(command_line, "stat ", &argument)) applet_stat(argument);
    else if (starts_with(command_line, "find ", &argument)) applet_find(argument);
    else if (equal(command_line, "find")) applet_find("");
    else if (starts_with(command_line, "tree ", &argument)) applet_tree(argument);
    else if (equal(command_line, "tree")) applet_tree("");
    else if (starts_with(command_line, "du ", &argument)) applet_du(argument);
    else if (equal(command_line, "du")) applet_du("");
    else if (starts_with(command_line, "basename ", &argument)) applet_basename(argument);
    else if (starts_with(command_line, "dirname ", &argument)) applet_dirname(argument);
    else if (equal(command_line, "nproc")) applet_nproc();
    else if (starts_with(command_line, "seq ", &argument)) applet_seq(argument);
    else if (starts_with(command_line, "sleep ", &argument)) applet_sleep(argument);
    else if (starts_with(command_line, "calc ", &argument)) applet_calc(argument);
    else if (starts_with(command_line, "printf ", &argument)) applet_printf(argument);
    else if (starts_with(command_line, "time ", &argument)) {
        const uint64_t before = interrupts_timer_ticks();
        const bool result = makobox_run(argument);
        const uint64_t elapsed = interrupts_timer_ticks() - before;
        serial_write("time: elapsed "); serial_write_u64(elapsed / 100u);
        serial_write("s "); serial_write_u64(elapsed % 100u); serial_write(" ticks\n");
        terminal_write("time: elapsed "); terminal_write_u64(elapsed / 100u);
        terminal_write("s "); terminal_write_u64(elapsed % 100u); terminal_write_line(" ticks");
        return result;
    }
    else if (equal(command_line, "true")) return applet_true();
    else if (equal(command_line, "false")) return applet_false();
    else if (starts_with(command_line, "rm ", &argument)) applet_rm(argument);
    else if (starts_with(command_line, "mv ", &argument)) applet_mv(argument);
    else if (starts_with(command_line, "cp ", &argument)) applet_cp(argument);
    else if (starts_with(command_line, "grep ", &argument)) applet_grep(argument);
    else if (starts_with(command_line, "write ", &argument)) applet_write(argument);
    else if (starts_with(command_line, "hexdump ", &argument)) applet_hexdump(argument);
    else if (starts_with(command_line, "strings ", &argument)) applet_strings(argument);
    else if (equal(command_line, "df")) applet_df();
    else if (equal(command_line, "free")) applet_free();
    else if (equal(command_line, "uptime")) applet_uptime();
    else if (equal(command_line, "id")) applet_id();
    else if (equal(command_line, "groups")) applet_groups();
    else if (equal(command_line, "history")) applet_history();
    else if (starts_with(command_line, "kill ", &argument)) return applet_kill(argument);
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
    if (makobox_run("runit stop project-host.service")) return false;
    return makobox_run("uname") && makobox_run("status") &&
        makobox_run("mem") && makobox_run("frames") &&
        makobox_run("paging") && makobox_run("ticks") && makobox_run("ps") &&
        makobox_run("abi") && makobox_run("caps") &&
        makobox_run("projects") && makobox_run("runit list-units") &&
        makobox_run("runit status project-host.service") &&
        makobox_run("runas runit restart project-host.service") &&
        makobox_run("apps list") && makobox_run("apps info hello") &&
        makobox_run("git status") && makobox_run("git init") &&
        makobox_run("git add project.mko") && makobox_run("git commit -m \"self-test\"") &&
        makobox_run("git log") && makobox_run("git diff") &&
        makobox_run("desktop status") && makobox_run("mko") &&
        makobox_run("input") && makobox_run("ipc") && makobox_run("fetch") &&
        makobox_run("arch") && makobox_run("tty") && makobox_run("who") &&
        makobox_run("users") && makobox_run("env") && makobox_run("printenv") &&
        makobox_run("which ls") && makobox_run("type fetch") && makobox_run("command -v tetris") &&
        makobox_run("whoami") && makobox_run("runas whoami") &&
        makobox_run("pwd") && makobox_run("hostname") && makobox_run("date") &&
        makobox_run("cal") && makobox_run("echo self-test") && makobox_run("ls -la") &&
        makobox_run("cat project.mko") && makobox_run("wc project.mko") &&
        makobox_run("head project.mko") &&
        makobox_run("head -n 1 project.mko") && makobox_run("tail project.mko") &&
        makobox_run("tail -n 1 project.mko") && makobox_run("tac project.mko") &&
        makobox_run("rev project.mko") && makobox_run("sort project.mko") &&
        makobox_run("uniq project.mko") && makobox_run("cmp project.mko project.mko") &&
        makobox_run("diff project.mko project.mko") && makobox_run("stat project.mko") &&
        makobox_run("find /") && makobox_run("tree /") && makobox_run("du /") &&
        makobox_run("basename /system/mko/sdk.mko") &&
        makobox_run("dirname /system/mko/sdk.mko") &&
        makobox_run("nproc") && makobox_run("seq 3") && makobox_run("seq 2 6") &&
        makobox_run("seq 10 -2 4") && makobox_run("calc 10+5*2") &&
        makobox_run("printf %s hello") && makobox_run("printf %d 42") && makobox_run("printf %x 255") &&
        makobox_run("time uname") && makobox_run("true") && makobox_run("groups") &&
        makobox_run("id") && makobox_run("uptime") && makobox_run("free") &&
        makobox_run("df") && makobox_run("history") &&
        makobox_run("touch selftest.tmp") && makobox_run("write selftest.tmp hello-makobox") &&
        makobox_run("hexdump selftest.tmp") && makobox_run("strings selftest.tmp") &&
        makobox_run("cp selftest.tmp copy.tmp") &&
        makobox_run("mv copy.tmp moved.tmp") && makobox_run("cmp moved.tmp selftest.tmp") &&
        makobox_run("rm moved.tmp") && makobox_run("rm selftest.tmp");
}

__attribute__((noreturn))
void makobox_shell(void) {
    char input[64] = {0};
    char history_draft[64] = {0};
    size_t length = 0;
    size_t history_offset = 0u;
    /* Clear the visual console only (serial keeps every boot-status line
       that already scrolled by -- smoke tests grep that log). Without this,
       a real interactive session starts buried under dozens of "[ OK ]"
       boot lines and reads like a dmesg dump instead of a shell you just
       landed in. */
    terminal_write("\f");
    line("");
    line("MakoBox interactive console ready. Type 'help'.");
    line("Terminal owns display and keyboard.");
    // Same prompt text on-screen and on serial -- a real console doesn't
    // get a different "friendlier" visual skin, see sidelined/README.md.
    terminal_write("mako# ");
    serial_write("mako# ");
    for (;;) {
        /* Input IRQs wake blocked userspace tasks while the kernel console
           owns the CPU; resume any ready work once, then return here as
           soon as it blocks again. Keeps the shell responsive without a
           polling thread or idle CPU burn. */
        if (scheduler_has_ready_users()) {
            (void)userspace_run_init();
            if (terminal_graphical_active()) terminal_graphical_refresh();
        }
        if (terminal_graphical_active()) input_discard_pending();
        char value;
        if (!keyboard_read_char(&value)) {
            __asm__ volatile ("hlt");
            continue;
        }
        if (value == '\n') {
            terminal_write_line("");
            serial_write("\n");
            input[length] = '\0';
            if (length > 0u) {
                history_record(input);
                (void)makobox_run(input);
            }
            length = 0;
            history_offset = 0u;
            history_draft[0] = '\0';
            terminal_write("mako# ");
            serial_write("mako# ");
        } else if (value == KEYBOARD_CHAR_HISTORY_UP ||
                   value == KEYBOARD_CHAR_HISTORY_DOWN) {
            if (history_count == 0u) continue;
            if (value == KEYBOARD_CHAR_HISTORY_UP) {
                if (history_offset == 0u) {
                    for (size_t i = 0u; i <= length; ++i) history_draft[i] = input[i];
                }
                if (history_offset < history_count) ++history_offset;
            } else if (history_offset > 0u) {
                --history_offset;
            }
            while (length > 0u) {
                --length;
                terminal_backspace();
                serial_write("\b \b");
            }
            const char *replacement = history_draft;
            if (history_offset > 0u) {
                const size_t absolute = history_next - history_offset;
                replacement = command_history[absolute % HISTORY_DEPTH];
            }
            while (replacement[length] != '\0' && length + 1u < sizeof(input)) {
                input[length] = replacement[length];
                ++length;
            }
            input[length] = '\0';
            terminal_write(input);
            serial_write(input);
            serial_write("\nHISTORY_RECALL offset=");
            serial_write_u64(history_offset);
            serial_write(" command=");
            serial_write(input);
            serial_write("\n");
        } else if (value == '\b') {
            if (length > 0u) {
                --length;
                input[length] = '\0';
                terminal_backspace();
                serial_write("\b \b");
            }
        } else if (length + 1u < sizeof(input)) {
            input[length++] = value;
            input[length] = '\0';
            char echo[2] = { value, '\0' };
            terminal_write(echo);
            serial_write(echo);
        }
    }
}

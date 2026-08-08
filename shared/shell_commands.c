#include <demon/shell_commands.h>

/* Portable command core shared between MakoBox (kernel, ring 0) and xterm
   (userspace, ring 3) -- see include/demon/shell_commands.h for why this
   exists and what it deliberately leaves out. Freestanding: no libc, no
   dynamic allocation, same restrictions as the rest of this kernel/SDK. */

/* Kept small on purpose: this buffer is static storage baked into every
   binary that links this module, and xterm in particular has to fit in a
   single fixed-size compact process code/data slot (see
   USERSPACE_CODE_PAGES in src/arch/x86_64/userspace.c) shared system-wide
   with every other non-"large" app -- there's no room here for a
   generous multi-page scratch arena. 4 KiB is still far more than a
   64x22 terminal can usefully show at once. */
#define SHELL_FILE_CAPACITY 1024u
#define SHELL_LINE_CAPACITY 96u
#define SHELL_NAME_CAPACITY 64u
#define SHELL_SORT_MAX_LINES 24u

static size_t shell_string_length(const char *text) {
    size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static bool shell_equal(const char *left, const char *right) {
    size_t i = 0u;
    for (; left[i] != '\0' && right[i] != '\0'; ++i)
        if (left[i] != right[i]) return false;
    return left[i] == right[i];
}

static bool shell_starts_with(const char *text, const char *prefix, const char **rest) {
    size_t i = 0u;
    for (; prefix[i] != '\0'; ++i)
        if (text[i] != prefix[i]) return false;
    *rest = text + i;
    return true;
}

/* Like shell_starts_with, but only matches at a word boundary (end of
   string or a space right after the prefix) -- so "ls" doesn't also
   swallow a typo like "lsblk". */
static bool shell_starts_with_command(const char *text, const char *prefix, const char **rest) {
    size_t i = 0u;
    for (; prefix[i] != '\0'; ++i)
        if (text[i] != prefix[i]) return false;
    if (text[i] != '\0' && text[i] != ' ') return false;
    *rest = text + i;
    return true;
}

static bool shell_contains(const char *haystack, size_t haystack_length,
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

static bool shell_split_two_args(const char *argument, char *first,
                                 size_t first_capacity, const char **second) {
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

static bool shell_parse_u32(const char *text, uint32_t *out) {
    if (text[0] == '\0') return false;
    uint32_t value = 0u;
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return false;
        value = value * 10u + (uint32_t)(*c - '0');
    }
    *out = value;
    return true;
}

static bool shell_parse_i64(const char *text, int64_t *out) {
    if (text[0] == '\0') return false;
    const char *c = text;
    bool negative = false;
    if (*c == '-' || *c == '+') { negative = (*c == '-'); ++c; }
    if (*c == '\0') return false;
    int64_t value = 0;
    for (; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return false;
        if (value > 922337203685477580ll) return false;
        value = value * 10 + (int64_t)(*c - '0');
    }
    *out = negative ? -value : value;
    return true;
}

static bool shell_parse_n_option(const char *argument, unsigned *lines, const char **path) {
    *lines = 10u;
    *path = argument;
    if (!shell_starts_with(argument, "-n ", &argument)) return true;
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

static bool shell_parse_ls_flags(const char *path, bool *long_format, const char **rest) {
    *long_format = false;
    *rest = path;
    if (path[0] != '-') return true;
    size_t i = 1u;
    while (path[i] != '\0' && path[i] != ' ') {
        if (path[i] == 'l') *long_format = true;
        else if (path[i] != 'a') return false;
        ++i;
    }
    while (path[i] == ' ') ++i;
    *rest = path + i;
    return true;
}

/* Writes `value` as decimal digits into `out` (no terminator), returns the
   digit count. No snprintf/itoa in this freestanding build. */
static size_t shell_format_u64(char *out, uint64_t value) {
    char digits[20];
    size_t count = 0u;
    if (value == 0u) { out[0] = '0'; return 1u; }
    while (value > 0u) { digits[count++] = (char)('0' + (value % 10u)); value /= 10u; }
    for (size_t i = 0u; i < count; ++i) out[i] = digits[count - 1u - i];
    return count;
}

static void shell_line(const struct shell_backend *backend, const char *text) {
    backend->emit_line(backend->context, text);
}

/* Builds "<label><value><suffix>" (suffix may be NULL) into a bounded
   buffer and emits it as one line -- the shared equivalent of makobox's
   value_line/signed_value_line, minus the dual serial+terminal fan-out
   (each backend's emit_line handles that itself). */
static void shell_value_line(const struct shell_backend *backend, const char *label,
                             int64_t value, const char *suffix) {
    char buffer[SHELL_LINE_CAPACITY];
    size_t length = 0u;
    for (const char *c = label; *c != '\0' && length + 1u < sizeof(buffer); ++c)
        buffer[length++] = *c;
    if (value < 0 && length + 1u < sizeof(buffer)) buffer[length++] = '-';
    const uint64_t magnitude = value < 0 ? (uint64_t)(-value) : (uint64_t)value;
    if (length + 20u < sizeof(buffer)) length += shell_format_u64(buffer + length, magnitude);
    if (suffix != NULL)
        for (const char *c = suffix; *c != '\0' && length + 1u < sizeof(buffer); ++c)
            buffer[length++] = *c;
    buffer[length] = '\0';
    shell_line(backend, buffer);
}

/* A length-bounded slice of a file's raw bytes, delimited by a '\n' (or
   the end of the buffer). Mirrors makobox.c's line_view/next_line. */
struct shell_line_view {
    const char *start;
    size_t length;
};

static bool shell_next_line(const uint8_t *data, size_t length, size_t *offset,
                            struct shell_line_view *out) {
    if (*offset >= length) return false;
    const size_t start = *offset;
    size_t end = start;
    while (end < length && data[end] != '\n') ++end;
    out->start = (const char *)&data[start];
    out->length = end - start;
    *offset = end + (end < length ? 1u : 0u);
    return true;
}

static void shell_print_line_view(const struct shell_backend *backend,
                                  const struct shell_line_view *view) {
    char buffer[SHELL_LINE_CAPACITY];
    size_t length = view->length;
    if (length > sizeof(buffer) - 1u) length = sizeof(buffer) - 1u;
    for (size_t i = 0u; i < length; ++i) buffer[i] = view->start[i];
    buffer[length] = '\0';
    shell_line(backend, buffer);
}

static int shell_compare_line_views(const struct shell_line_view *left,
                                    const struct shell_line_view *right) {
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

static uint8_t shell_file_buffer[SHELL_FILE_CAPACITY];

static bool shell_read_whole(const struct shell_backend *backend, const char *path,
                             const uint8_t **data, size_t *length) {
    size_t real_length;
    if (!backend->read_file(backend->context, path, shell_string_length(path),
                            shell_file_buffer, sizeof(shell_file_buffer), &real_length))
        return false;
    *data = shell_file_buffer;
    *length = real_length < sizeof(shell_file_buffer) ? real_length : sizeof(shell_file_buffer);
    return true;
}

/* ---- individual commands ---- */

static void shell_cmd_echo(const struct shell_backend *backend, const char *text) {
    shell_line(backend, text);
}

static void shell_cmd_pwd(const struct shell_backend *backend) {
    shell_line(backend, "/");
}

static void shell_cmd_whoami(const struct shell_backend *backend) {
    shell_line(backend, "mako");
}

static void shell_cmd_cat(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: cat <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "cat: no such file");
        return;
    }
    size_t offset = 0u;
    struct shell_line_view view;
    while (shell_next_line(data, length, &offset, &view))
        shell_print_line_view(backend, &view);
}

static void shell_cmd_head(const struct shell_backend *backend, const char *argument) {
    unsigned lines;
    const char *path;
    if (!shell_parse_n_option(argument, &lines, &path) || path[0] == '\0') {
        shell_line(backend, "usage: head [-n N] <path>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "head: no such file");
        return;
    }
    size_t offset = 0u;
    struct shell_line_view view;
    unsigned shown = 0u;
    while (shown < lines && shell_next_line(data, length, &offset, &view)) {
        shell_print_line_view(backend, &view);
        ++shown;
    }
}

static void shell_cmd_tail(const struct shell_backend *backend, const char *argument) {
    unsigned lines;
    const char *path;
    if (!shell_parse_n_option(argument, &lines, &path) || path[0] == '\0') {
        shell_line(backend, "usage: tail [-n N] <path>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "tail: no such file");
        return;
    }
    size_t total = 0u;
    size_t offset = 0u;
    struct shell_line_view scratch;
    while (shell_next_line(data, length, &offset, &scratch)) ++total;
    const size_t skip = total > lines ? total - lines : 0u;
    offset = 0u;
    struct shell_line_view view;
    for (size_t index = 0u; shell_next_line(data, length, &offset, &view); ++index)
        if (index >= skip) shell_print_line_view(backend, &view);
}

static void shell_cmd_tac(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: tac <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "tac: no such file");
        return;
    }
    size_t offsets[SHELL_SORT_MAX_LINES];
    size_t count = 0u;
    size_t offset = 0u;
    struct shell_line_view view;
    while (shell_next_line(data, length, &offset, &view)) {
        if (count >= SHELL_SORT_MAX_LINES) { shell_line(backend, "tac: file too long"); return; }
        offsets[count++] = (size_t)(view.start - (const char *)data);
    }
    for (size_t i = count; i > 0u; --i) {
        size_t walk = offsets[i - 1u];
        shell_next_line(data, length, &walk, &view);
        shell_print_line_view(backend, &view);
    }
}

static void shell_cmd_rev(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: rev <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "rev: no such file");
        return;
    }
    size_t offset = 0u;
    struct shell_line_view view;
    char buffer[SHELL_LINE_CAPACITY];
    while (shell_next_line(data, length, &offset, &view)) {
        size_t reversed_length = view.length;
        if (reversed_length > sizeof(buffer) - 1u) reversed_length = sizeof(buffer) - 1u;
        for (size_t i = 0u; i < reversed_length; ++i)
            buffer[i] = view.start[view.length - 1u - i];
        buffer[reversed_length] = '\0';
        shell_line(backend, buffer);
    }
}

static struct shell_line_view shell_sort_lines[SHELL_SORT_MAX_LINES];

static void shell_cmd_sort(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: sort <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "sort: no such file");
        return;
    }
    size_t count = 0u;
    size_t offset = 0u;
    struct shell_line_view view;
    while (shell_next_line(data, length, &offset, &view)) {
        if (count >= SHELL_SORT_MAX_LINES) { shell_line(backend, "sort: file too long"); return; }
        shell_sort_lines[count++] = view;
    }
    for (size_t i = 0u; i < count; ++i) {
        size_t best = i;
        for (size_t j = i + 1u; j < count; ++j)
            if (shell_compare_line_views(&shell_sort_lines[j], &shell_sort_lines[best]) < 0) best = j;
        const struct shell_line_view temp = shell_sort_lines[i];
        shell_sort_lines[i] = shell_sort_lines[best];
        shell_sort_lines[best] = temp;
    }
    for (size_t i = 0u; i < count; ++i) shell_print_line_view(backend, &shell_sort_lines[i]);
}

static void shell_cmd_uniq(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: uniq <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "uniq: no such file");
        return;
    }
    size_t offset = 0u;
    struct shell_line_view view;
    struct shell_line_view previous;
    bool have_previous = false;
    while (shell_next_line(data, length, &offset, &view)) {
        if (!have_previous || shell_compare_line_views(&view, &previous) != 0) {
            shell_print_line_view(backend, &view);
            previous = view;
            have_previous = true;
        }
    }
}

static void shell_cmd_grep(const struct shell_backend *backend, const char *argument) {
    char pattern[SHELL_NAME_CAPACITY];
    const char *path;
    if (!shell_split_two_args(argument, pattern, sizeof(pattern), &path)) {
        shell_line(backend, "usage: grep <pattern> <path>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "grep: no such file");
        return;
    }
    const size_t pattern_length = shell_string_length(pattern);
    size_t offset = 0u;
    struct shell_line_view view;
    unsigned matches = 0u;
    while (shell_next_line(data, length, &offset, &view)) {
        if (shell_contains(view.start, view.length, pattern, pattern_length)) {
            shell_print_line_view(backend, &view);
            ++matches;
        }
    }
    if (matches == 0u) shell_line(backend, "grep: no matches");
}

static void shell_cmd_wc(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: wc <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "wc: no such file");
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
    if (length > 0u && data[length - 1u] != '\n') ++lines;
    shell_value_line(backend, "  lines: ", (int64_t)lines, NULL);
    shell_value_line(backend, "  words: ", (int64_t)words, NULL);
    shell_value_line(backend, "  bytes: ", (int64_t)length, NULL);
}

static void shell_cmd_touch(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: touch <path>"); return; }
    if (!backend->write_file(backend->context, path, shell_string_length(path),
                             (const uint8_t *)"", 0u)) {
        shell_line(backend, "touch: could not create file");
        return;
    }
    shell_line(backend, "touched");
}

static void shell_cmd_write(const struct shell_backend *backend, const char *argument) {
    char path[SHELL_NAME_CAPACITY];
    const char *text;
    if (!shell_split_two_args(argument, path, sizeof(path), &text)) {
        shell_line(backend, "usage: write <path> <text>");
        return;
    }
    const size_t length = shell_string_length(text);
    if (!backend->write_file(backend->context, path, shell_string_length(path),
                             (const uint8_t *)text, length)) {
        shell_line(backend, "write: could not create or replace file");
        return;
    }
    shell_value_line(backend, "wrote ", (int64_t)length, " bytes");
}

static void shell_cmd_rm(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: rm <path>"); return; }
    if (!backend->delete_file(backend->context, path, shell_string_length(path))) {
        shell_line(backend, "rm: no such file");
        return;
    }
    shell_line(backend, "removed");
}

static void shell_cmd_cp(const struct shell_backend *backend, const char *argument) {
    char src[SHELL_NAME_CAPACITY];
    const char *dst;
    if (!shell_split_two_args(argument, src, sizeof(src), &dst)) {
        shell_line(backend, "usage: cp <src> <dst>");
        return;
    }
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, src, &data, &length) ||
        !backend->write_file(backend->context, dst, shell_string_length(dst), data, length)) {
        shell_line(backend, "cp: could not read source or write destination");
        return;
    }
    shell_value_line(backend, "copied ", (int64_t)length, " bytes");
}

static void shell_cmd_mv(const struct shell_backend *backend, const char *argument) {
    char src[SHELL_NAME_CAPACITY];
    const char *dst;
    if (!shell_split_two_args(argument, src, sizeof(src), &dst)) {
        shell_line(backend, "usage: mv <src> <dst>");
        return;
    }
    if (backend->rename_file(backend->context, src, shell_string_length(src),
                             dst, shell_string_length(dst))) {
        return;
    }
    /* Fall back to copy+delete if the backend can't rename in place. */
    const uint8_t *data;
    size_t length;
    if (!shell_read_whole(backend, src, &data, &length) ||
        !backend->write_file(backend->context, dst, shell_string_length(dst), data, length) ||
        !backend->delete_file(backend->context, src, shell_string_length(src))) {
        shell_line(backend, "mv: could not move file");
    }
}

static void shell_cmd_ls(const struct shell_backend *backend, const char *argument) {
    bool long_format;
    const char *path;
    if (!shell_parse_ls_flags(argument, &long_format, &path)) {
        shell_line(backend, "ls: unknown option (supported: -l, -a, -la)");
        return;
    }
    const size_t path_length = shell_string_length(path);
    shell_line(backend, path_length == 0u ? "Listing: /" : path);
    size_t shown = 0u;
    char name[SHELL_NAME_CAPACITY];
    for (size_t index = 0u; ; ++index) {
        size_t name_length, size;
        bool is_directory;
        if (!backend->list_dir(backend->context, path, path_length, index,
                               name, sizeof(name), &name_length, &size, &is_directory))
            break;
        ++shown;
        char buffer[SHELL_LINE_CAPACITY];
        size_t length = 0u;
        if (long_format) {
            buffer[length++] = is_directory ? 'd' : '-';
            for (const char *c = "rwx------  1 mako mako  "; *c != '\0'; ++c) buffer[length++] = *c;
            length += shell_format_u64(buffer + length, (uint64_t)size);
            buffer[length++] = ' ';
            buffer[length++] = ' ';
        } else {
            buffer[length++] = ' ';
            buffer[length++] = ' ';
        }
        for (size_t i = 0u; i < name_length && length + 2u < sizeof(buffer); ++i)
            buffer[length++] = name[i];
        if (is_directory && length + 1u < sizeof(buffer)) buffer[length++] = '/';
        if (!long_format && !is_directory) {
            for (const char *c = "  "; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            if (length + 20u < sizeof(buffer)) length += shell_format_u64(buffer + length, (uint64_t)size);
            for (const char *c = " bytes"; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
        }
        buffer[length] = '\0';
        shell_line(backend, buffer);
    }
    if (shown == 0u) shell_line(backend, "  (empty)");
}

static void shell_cmd_stat(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: stat <path>"); return; }
    const uint8_t *data;
    size_t length;
    if (shell_read_whole(backend, path, &data, &length)) {
        shell_line(backend, "File:");
        shell_line(backend, path);
        shell_value_line(backend, "  size: ", (int64_t)length, " bytes");
        shell_line(backend, "  type: regular file");
        return;
    }
    size_t name_length, size;
    bool is_directory;
    char name[SHELL_NAME_CAPACITY];
    size_t entries = 0u;
    for (size_t index = 0u;
         backend->list_dir(backend->context, path, shell_string_length(path), index,
                           name, sizeof(name), &name_length, &size, &is_directory);
         ++index)
        ++entries;
    if (entries > 0u) {
        shell_line(backend, "Directory:");
        shell_line(backend, path);
        shell_value_line(backend, "  entries: ", (int64_t)entries, NULL);
        return;
    }
    shell_line(backend, "stat: no such file or directory");
}

static void shell_join_child(char *child, size_t child_capacity, const char *path,
                             size_t path_length, const char *name, size_t name_length) {
    size_t base = 0u;
    if (path_length > 0u) {
        for (size_t i = 0u; i < path_length && base + 1u < child_capacity; ++i) child[base++] = path[i];
        if (base > 0u && child[base - 1u] != '/' && base + 1u < child_capacity) child[base++] = '/';
    }
    for (size_t i = 0u; i < name_length && base + 1u < child_capacity; ++i) child[base++] = name[i];
    child[base] = '\0';
}

static void shell_find_walk(const struct shell_backend *backend, const char *path, size_t path_length) {
    char name[SHELL_NAME_CAPACITY];
    for (size_t index = 0u; ; ++index) {
        size_t name_length, size;
        bool is_directory;
        if (!backend->list_dir(backend->context, path, path_length, index,
                               name, sizeof(name), &name_length, &size, &is_directory))
            break;
        char child[256];
        shell_join_child(child, sizeof(child), path, path_length, name, name_length);
        const size_t child_length = shell_string_length(child);
        if (is_directory) {
            char with_slash[258];
            size_t i = 0u;
            for (; i < child_length; ++i) with_slash[i] = child[i];
            with_slash[i++] = '/';
            with_slash[i] = '\0';
            shell_line(backend, with_slash);
            shell_find_walk(backend, child, child_length);
        } else {
            char buffer[SHELL_LINE_CAPACITY];
            size_t length = 0u;
            for (size_t j = 0u; j < child_length && length + 1u < sizeof(buffer); ++j) buffer[length++] = child[j];
            for (const char *c = "  "; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            if (length + 20u < sizeof(buffer)) length += shell_format_u64(buffer + length, (uint64_t)size);
            for (const char *c = " bytes"; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            buffer[length] = '\0';
            shell_line(backend, buffer);
        }
    }
}

static void shell_cmd_find(const struct shell_backend *backend, const char *argument) {
    shell_find_walk(backend, argument, shell_string_length(argument));
}

static void shell_tree_walk(const struct shell_backend *backend, const char *path,
                            size_t path_length, unsigned depth) {
    char name[SHELL_NAME_CAPACITY];
    for (size_t index = 0u; ; ++index) {
        size_t name_length, size;
        bool is_directory;
        if (!backend->list_dir(backend->context, path, path_length, index,
                               name, sizeof(name), &name_length, &size, &is_directory))
            break;
        (void)size;
        char buffer[SHELL_LINE_CAPACITY];
        size_t length = 0u;
        for (unsigned i = 0u; i < depth && length + 2u < sizeof(buffer); ++i) {
            buffer[length++] = ' '; buffer[length++] = ' ';
        }
        for (size_t i = 0u; i < name_length && length + 2u < sizeof(buffer); ++i)
            buffer[length++] = name[i];
        if (is_directory) buffer[length++] = '/';
        buffer[length] = '\0';
        shell_line(backend, buffer);
        if (is_directory) {
            char child[256];
            shell_join_child(child, sizeof(child), path, path_length, name, name_length);
            shell_tree_walk(backend, child, shell_string_length(child), depth + 1u);
        }
    }
}

static void shell_cmd_tree(const struct shell_backend *backend, const char *argument) {
    shell_tree_walk(backend, argument, shell_string_length(argument), 0u);
}

static uint64_t shell_du_walk(const struct shell_backend *backend, const char *path, size_t path_length) {
    char name[SHELL_NAME_CAPACITY];
    uint64_t total = 0u;
    for (size_t index = 0u; ; ++index) {
        size_t name_length, size;
        bool is_directory;
        if (!backend->list_dir(backend->context, path, path_length, index,
                               name, sizeof(name), &name_length, &size, &is_directory))
            break;
        char child[256];
        shell_join_child(child, sizeof(child), path, path_length, name, name_length);
        const size_t child_length = shell_string_length(child);
        if (is_directory) {
            const uint64_t sub = shell_du_walk(backend, child, child_length);
            char buffer[SHELL_LINE_CAPACITY];
            size_t length = 0u;
            for (size_t j = 0u; j < child_length && length + 1u < sizeof(buffer); ++j) buffer[length++] = child[j];
            for (const char *c = "/  "; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            if (length + 20u < sizeof(buffer)) length += shell_format_u64(buffer + length, sub);
            for (const char *c = " bytes"; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            buffer[length] = '\0';
            shell_line(backend, buffer);
            total += sub;
        } else {
            char buffer[SHELL_LINE_CAPACITY];
            size_t length = 0u;
            for (size_t j = 0u; j < child_length && length + 1u < sizeof(buffer); ++j) buffer[length++] = child[j];
            for (const char *c = "  "; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            if (length + 20u < sizeof(buffer)) length += shell_format_u64(buffer + length, (uint64_t)size);
            for (const char *c = " bytes"; *c != '\0' && length + 1u < sizeof(buffer); ++c) buffer[length++] = *c;
            buffer[length] = '\0';
            shell_line(backend, buffer);
            total += (uint64_t)size;
        }
    }
    return total;
}

static void shell_cmd_du(const struct shell_backend *backend, const char *argument) {
    const uint64_t total = shell_du_walk(backend, argument, shell_string_length(argument));
    shell_value_line(backend, "total: ", (int64_t)total, " bytes");
}

static void shell_cmd_basename(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: basename <path>"); return; }
    const char *start = path;
    for (const char *c = path; *c != '\0'; ++c)
        if (*c == '/') start = c + 1u;
    shell_line(backend, start);
}

static void shell_cmd_dirname(const struct shell_backend *backend, const char *path) {
    if (path[0] == '\0') { shell_line(backend, "usage: dirname <path>"); return; }
    const char *last_slash = NULL;
    for (const char *c = path; *c != '\0'; ++c)
        if (*c == '/') last_slash = c;
    if (last_slash == NULL) { shell_line(backend, "."); return; }
    if (last_slash == path) { shell_line(backend, "/"); return; }
    char buffer[256];
    size_t length = (size_t)(last_slash - path);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1u;
    for (size_t i = 0u; i < length; ++i) buffer[i] = path[i];
    buffer[length] = '\0';
    shell_line(backend, buffer);
}

static bool shell_parse_seq_args(const char *argument, int64_t *first, int64_t *step, int64_t *last) {
    char first_token[SHELL_NAME_CAPACITY];
    const char *rest;
    if (!shell_split_two_args(argument, first_token, sizeof(first_token), &rest)) {
        if (!shell_parse_i64(argument, last)) return false;
        *first = 1; *step = 1;
        return true;
    }
    char second_token[SHELL_NAME_CAPACITY];
    const char *third;
    if (!shell_split_two_args(rest, second_token, sizeof(second_token), &third)) {
        if (!shell_parse_i64(first_token, first) || !shell_parse_i64(rest, last)) return false;
        *step = 1;
        return true;
    }
    return shell_parse_i64(first_token, first) && shell_parse_i64(second_token, step) &&
        shell_parse_i64(third, last);
}

static void shell_cmd_seq(const struct shell_backend *backend, const char *argument) {
    int64_t first, step, last;
    if (!shell_parse_seq_args(argument, &first, &step, &last) || step == 0) {
        shell_line(backend, "usage: seq [first [step]] last");
        return;
    }
    int64_t value = first;
    for (;;) {
        shell_value_line(backend, "", value, NULL);
        if (step > 0) {
            if (value > last - step) break;
            value += step;
        } else {
            if (value < last - step) break;
            value += step;
        }
    }
}

struct shell_calc_parser {
    const char *cursor;
    bool error;
};

static void shell_calc_skip_spaces(struct shell_calc_parser *parser) {
    while (parser->cursor[0] == ' ') ++parser->cursor;
}

static int64_t shell_calc_expression(struct shell_calc_parser *parser);

static int64_t shell_calc_factor(struct shell_calc_parser *parser) {
    shell_calc_skip_spaces(parser);
    if (parser->error) return 0;
    if (parser->cursor[0] == '-') { ++parser->cursor; return -shell_calc_factor(parser); }
    if (parser->cursor[0] == '+') { ++parser->cursor; return shell_calc_factor(parser); }
    if (parser->cursor[0] == '(') {
        ++parser->cursor;
        const int64_t value = shell_calc_expression(parser);
        shell_calc_skip_spaces(parser);
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

static int64_t shell_calc_term(struct shell_calc_parser *parser) {
    int64_t value = shell_calc_factor(parser);
    for (;;) {
        shell_calc_skip_spaces(parser);
        const char op = parser->cursor[0];
        if (op != '*' && op != '/' && op != '%') return value;
        ++parser->cursor;
        const int64_t right = shell_calc_factor(parser);
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

static int64_t shell_calc_expression(struct shell_calc_parser *parser) {
    int64_t value = shell_calc_term(parser);
    for (;;) {
        shell_calc_skip_spaces(parser);
        const char op = parser->cursor[0];
        if (op != '+' && op != '-') return value;
        ++parser->cursor;
        const int64_t right = shell_calc_term(parser);
        if (op == '+') value += right;
        else value -= right;
    }
}

static void shell_cmd_calc(const struct shell_backend *backend, const char *argument) {
    if (argument[0] == '\0') { shell_line(backend, "usage: calc <expression>"); return; }
    struct shell_calc_parser parser = { argument, false };
    const int64_t value = shell_calc_expression(&parser);
    shell_calc_skip_spaces(&parser);
    if (parser.error || parser.cursor[0] != '\0') {
        shell_line(backend, "calc: invalid expression");
        return;
    }
    shell_value_line(backend, "= ", value, NULL);
}

bool shell_dispatch(const struct shell_backend *backend, const char *command_line) {
    const char *rest;
    if (shell_equal(command_line, "pwd")) { shell_cmd_pwd(backend); return true; }
    if (shell_equal(command_line, "whoami")) { shell_cmd_whoami(backend); return true; }
    if (shell_equal(command_line, "true")) { return true; }
    if (shell_equal(command_line, "false")) { return true; }
    if (shell_starts_with(command_line, "echo ", &rest)) { shell_cmd_echo(backend, rest); return true; }
    if (shell_equal(command_line, "echo")) { shell_cmd_echo(backend, ""); return true; }
    if (shell_starts_with(command_line, "cat ", &rest)) { shell_cmd_cat(backend, rest); return true; }
    if (shell_equal(command_line, "cat")) { shell_cmd_cat(backend, ""); return true; }
    if (shell_starts_with_command(command_line, "head", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_head(backend, rest); return true;
    }
    if (shell_starts_with_command(command_line, "tail", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_tail(backend, rest); return true;
    }
    if (shell_starts_with(command_line, "tac ", &rest)) { shell_cmd_tac(backend, rest); return true; }
    if (shell_equal(command_line, "tac")) { shell_cmd_tac(backend, ""); return true; }
    if (shell_starts_with(command_line, "rev ", &rest)) { shell_cmd_rev(backend, rest); return true; }
    if (shell_equal(command_line, "rev")) { shell_cmd_rev(backend, ""); return true; }
    if (shell_starts_with(command_line, "sort ", &rest)) { shell_cmd_sort(backend, rest); return true; }
    if (shell_equal(command_line, "sort")) { shell_cmd_sort(backend, ""); return true; }
    if (shell_starts_with(command_line, "uniq ", &rest)) { shell_cmd_uniq(backend, rest); return true; }
    if (shell_equal(command_line, "uniq")) { shell_cmd_uniq(backend, ""); return true; }
    if (shell_starts_with(command_line, "grep ", &rest)) { shell_cmd_grep(backend, rest); return true; }
    if (shell_equal(command_line, "grep")) { shell_cmd_grep(backend, ""); return true; }
    if (shell_starts_with(command_line, "wc ", &rest)) { shell_cmd_wc(backend, rest); return true; }
    if (shell_equal(command_line, "wc")) { shell_cmd_wc(backend, ""); return true; }
    if (shell_starts_with(command_line, "touch ", &rest)) { shell_cmd_touch(backend, rest); return true; }
    if (shell_equal(command_line, "touch")) { shell_cmd_touch(backend, ""); return true; }
    if (shell_starts_with(command_line, "write ", &rest)) { shell_cmd_write(backend, rest); return true; }
    if (shell_equal(command_line, "write")) { shell_cmd_write(backend, ""); return true; }
    if (shell_starts_with(command_line, "rm ", &rest)) { shell_cmd_rm(backend, rest); return true; }
    if (shell_equal(command_line, "rm")) { shell_cmd_rm(backend, ""); return true; }
    if (shell_starts_with(command_line, "cp ", &rest)) { shell_cmd_cp(backend, rest); return true; }
    if (shell_equal(command_line, "cp")) { shell_cmd_cp(backend, ""); return true; }
    if (shell_starts_with(command_line, "mv ", &rest)) { shell_cmd_mv(backend, rest); return true; }
    if (shell_equal(command_line, "mv")) { shell_cmd_mv(backend, ""); return true; }
    if (shell_starts_with_command(command_line, "ls", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_ls(backend, rest); return true;
    }
    if (shell_starts_with(command_line, "stat ", &rest)) { shell_cmd_stat(backend, rest); return true; }
    if (shell_equal(command_line, "stat")) { shell_cmd_stat(backend, ""); return true; }
    if (shell_starts_with_command(command_line, "find", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_find(backend, rest); return true;
    }
    if (shell_starts_with_command(command_line, "tree", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_tree(backend, rest); return true;
    }
    if (shell_starts_with_command(command_line, "du", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_du(backend, rest); return true;
    }
    if (shell_starts_with(command_line, "basename ", &rest)) { shell_cmd_basename(backend, rest); return true; }
    if (shell_equal(command_line, "basename")) { shell_cmd_basename(backend, ""); return true; }
    if (shell_starts_with(command_line, "dirname ", &rest)) { shell_cmd_dirname(backend, rest); return true; }
    if (shell_equal(command_line, "dirname")) { shell_cmd_dirname(backend, ""); return true; }
    if (shell_starts_with_command(command_line, "seq", &rest)) {
        while (rest[0] == ' ') ++rest;
        shell_cmd_seq(backend, rest); return true;
    }
    if (shell_starts_with(command_line, "calc ", &rest)) { shell_cmd_calc(backend, rest); return true; }
    if (shell_equal(command_line, "calc")) { shell_cmd_calc(backend, ""); return true; }
    (void)shell_parse_u32;
    return false;
}

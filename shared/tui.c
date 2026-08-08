#include <demon/tui.h>

/* Freestanding: no libc, no snprintf -- same constraints as the rest of
   this kernel/SDK. Every line is built in a fixed local buffer and capped
   to TUI_MAX_WIDTH so a caller-supplied width can never overflow it. */
#define TUI_MAX_WIDTH 100u

static size_t tui_clamp_width(size_t width) {
    if (width < 4u) return 4u;
    if (width > TUI_MAX_WIDTH) return TUI_MAX_WIDTH;
    return width;
}

static size_t tui_string_length(const char *text) {
    size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

void tui_box_edge(tui_emit_fn emit, void *context, size_t width) {
    width = tui_clamp_width(width);
    char buffer[TUI_MAX_WIDTH + 1u];
    buffer[0] = '+';
    for (size_t i = 1u; i + 1u < width; ++i) buffer[i] = '-';
    buffer[width - 1u] = '+';
    buffer[width] = '\0';
    emit(context, buffer);
}

void tui_box_line(tui_emit_fn emit, void *context, size_t width, const char *text) {
    width = tui_clamp_width(width);
    char buffer[TUI_MAX_WIDTH + 1u];
    buffer[0] = '|';
    const size_t inner = width - 2u;
    size_t text_length = tui_string_length(text);
    if (text_length > inner) text_length = inner;
    size_t i = 0u;
    for (; i < text_length; ++i) buffer[1u + i] = text[i];
    for (; i < inner; ++i) buffer[1u + i] = ' ';
    buffer[width - 1u] = '|';
    buffer[width] = '\0';
    emit(context, buffer);
}

void tui_banner(tui_emit_fn emit, void *context, size_t width, const char *title) {
    width = tui_clamp_width(width);
    tui_box_edge(emit, context, width);
    const size_t inner = width - 2u;
    const size_t title_length = tui_string_length(title);
    char centered[TUI_MAX_WIDTH + 1u];
    if (title_length >= inner) {
        size_t i = 0u;
        for (; i < inner; ++i) centered[i] = title[i];
        centered[inner] = '\0';
    } else {
        const size_t left_pad = (inner - title_length) / 2u;
        size_t i = 0u;
        for (; i < left_pad; ++i) centered[i] = ' ';
        for (size_t j = 0u; j < title_length; ++j) centered[i++] = title[j];
        for (; i < inner; ++i) centered[i] = ' ';
        centered[inner] = '\0';
    }
    tui_box_line(emit, context, width, centered);
    tui_box_edge(emit, context, width);
}

/* No snprintf/itoa in this freestanding build. */
static size_t tui_format_u64(char *out, unsigned long long value) {
    char digits[20];
    size_t count = 0u;
    if (value == 0u) { out[0] = '0'; return 1u; }
    while (value > 0u) { digits[count++] = (char)('0' + (value % 10u)); value /= 10u; }
    for (size_t i = 0u; i < count; ++i) out[i] = digits[count - 1u - i];
    return count;
}

void tui_progress_bar(tui_emit_fn emit, void *context, size_t width,
                      unsigned percent, const char *label) {
    width = tui_clamp_width(width);
    if (percent > 100u) percent = 100u;
    char buffer[TUI_MAX_WIDTH + 1u];
    size_t length = 0u;

    /* Reserve room for " NNN%" (and " " + label, if given) after the
       "[...]" bar so everything still fits in `width` columns. */
    size_t suffix_length = 4u; /* " NN%" minimum */
    if (percent >= 100u) suffix_length = 5u; /* " 100%" */
    size_t label_length = label != NULL ? tui_string_length(label) : 0u;
    if (label_length > 0u) suffix_length += 1u + label_length;

    size_t bar_width = width > suffix_length + 2u ? width - suffix_length - 2u : 1u;
    const size_t filled = bar_width * percent / 100u;

    buffer[length++] = '[';
    for (size_t i = 0u; i < bar_width && length + 1u < sizeof(buffer); ++i)
        buffer[length++] = i < filled ? '#' : '.';
    if (length + 1u < sizeof(buffer)) buffer[length++] = ']';
    if (length + 1u < sizeof(buffer)) buffer[length++] = ' ';
    if (length + 4u < sizeof(buffer)) length += tui_format_u64(buffer + length, percent);
    if (length + 1u < sizeof(buffer)) buffer[length++] = '%';
    if (label_length > 0u) {
        if (length + 1u < sizeof(buffer)) buffer[length++] = ' ';
        for (size_t i = 0u; i < label_length && length + 1u < sizeof(buffer); ++i)
            buffer[length++] = label[i];
    }
    buffer[length] = '\0';
    emit(context, buffer);
}

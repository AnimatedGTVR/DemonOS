#include "libc.h"
#include <demon/portkit.h>

static char *strtok_next;
static unsigned random_state = 1u;

/* ---- memory ---- */

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0u; i < n; ++i) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0u) return dest;
    if (d < s) {
        for (size_t i = 0u; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i-- > 0u;) d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0u; i < n; ++i) d[i] = (uint8_t)value;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0u; i < n; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

/* ---- strings ---- */

size_t strlen(const char *s) {
    size_t n = 0u;
    while (s[n] != '\0') ++n;
    return n;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0u;
    while (n < max && s[n] != '\0') ++n;
    return n;
}

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*out++ = *src++) != '\0') {}
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0u;
    for (; i < n && src[i] != '\0'; ++i) dest[i] = src[i];
    for (; i < n; ++i) dest[i] = '\0';
    return dest;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    size_t i = 0u;
    while (i < n && a[i] != '\0' && a[i] == b[i]) ++i;
    if (i == n) return 0;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

char *strcat(char *dest, const char *src) {
    char *out = dest;
    while (*out != '\0') ++out;
    while ((*out++ = *src++) != '\0') {}
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *out = dest;
    while (*out != '\0') ++out;
    size_t i = 0u;
    while (i < n && src[i] != '\0') { out[i] = src[i]; ++i; }
    out[i] = '\0';
    return dest;
}

char *strchr(const char *s, int c) {
    const char ch = (char)c;
    for (; *s != '\0'; ++s)
        if (*s == ch) return (char *)s;
    return ch == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char ch = (char)c;
    const char *found = NULL;
    for (; *s != '\0'; ++s)
        if (*s == ch) found = s;
    return ch == '\0' && found == NULL ? (char *)s : (char *)found;
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    for (; *haystack != '\0'; ++haystack) {
        const char *a = haystack;
        const char *b = needle;
        while (*b != '\0' && *a == *b) { ++a; ++b; }
        if (*b == '\0') return (char *)haystack;
    }
    return NULL;
}

char *strtok(char *str, const char *delim) {
    char *current = str != NULL ? str : strtok_next;
    while (*current != '\0' && strchr(delim, *current) != NULL) ++current;
    if (*current == '\0') { strtok_next = current; return NULL; }
    char *start = current;
    while (*current != '\0' && strchr(delim, *current) == NULL) ++current;
    if (*current != '\0') { *current = '\0'; ++current; }
    strtok_next = current;
    return start;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0u;
    while (s[n] != '\0' && strchr(accept, s[n]) != NULL) ++n;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0u;
    while (s[n] != '\0' && strchr(reject, s[n]) == NULL) ++n;
    return n;
}

static int ascii_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

int strcasecmp(const char *a, const char *b) {
    while (*a != '\0' && ascii_lower(*a) == ascii_lower(*b)) { ++a; ++b; }
    return ascii_lower(*a) - ascii_lower(*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    size_t i = 0u;
    while (i < n && a[i] != '\0' && ascii_lower(a[i]) == ascii_lower(b[i])) ++i;
    if (i == n) return 0;
    return ascii_lower(a[i]) - ascii_lower(b[i]);
}

char *strdup(const char *s) {
    const size_t n = strlen(s);
    char *copy = (char *)malloc(n + 1u);
    if (copy == NULL) return NULL;
    for (size_t i = 0u; i < n; ++i) copy[i] = s[i];
    copy[n] = '\0';
    return copy;
}

/* ---- ctype ---- */

int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && !isspace(c); }

/* ---- numeric ---- */

long strtol(const char *s, char **endptr, int base) {
    while (isspace((unsigned char)*s)) ++s;
    int negative = 0;
    if (*s == '-') { negative = 1; ++s; }
    else if (*s == '+') ++s;
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else base = 8;
        } else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    if (base < 2 || base > 36) {
        if (endptr != NULL) *endptr = (char *)s;
        return 0;
    }
    unsigned long value = 0ul;
    int any = 0;
    for (;;) {
        const int c = (unsigned char)*s;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (unsigned long)base + (unsigned long)digit;
        any = 1;
        ++s;
    }
    if (endptr != NULL) *endptr = (char *)s;
    (void)any;
    return negative ? (long)(0ul - value) : (long)value;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* ---- random (LCG) ---- */

void srand(unsigned seed) { random_state = seed != 0u ? seed : 1u; }

int rand(void) {
    random_state = random_state * 1103515245u + 12345u;
    return (int)((random_state >> 16u) & 0x7FFFu);
}

/* ---- qsort ---- */

static void swap_bytes(void *a, void *b, size_t n) {
    uint8_t *pa = (uint8_t *)a;
    uint8_t *pb = (uint8_t *)b;
    for (size_t i = 0u; i < n; ++i) {
        const uint8_t t = pa[i];
        pa[i] = pb[i];
        pb[i] = t;
    }
}

static void quicksort(char *base, size_t lo, size_t hi, size_t size,
                      int (*compare)(const void *, const void *)) {
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2u;
        char *const a = base + lo * size;
        char *const b = base + mid * size;
        char *const c = base + hi * size;
        if (compare(a, c) > 0) swap_bytes(a, c, size);
        if (compare(b, c) > 0) swap_bytes(b, c, size);
        if (compare(a, b) > 0) swap_bytes(a, b, size);
        swap_bytes(b, c, size);
        size_t store = lo;
        for (size_t i = lo; i < hi; ++i) {
            if (compare(base + i * size, c) < 0) {
                swap_bytes(base + store * size, base + i * size, size);
                ++store;
            }
        }
        swap_bytes(base + store * size, c, size);
        if (store == lo) {
            quicksort(base, lo + 1u, hi, size, compare);
            return;
        }
        if (store - lo < hi - store) {
            quicksort(base, lo, store - 1u, size, compare);
            lo = store + 1u;
        } else {
            quicksort(base, store + 1u, hi, size, compare);
            hi = store - 1u;
        }
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    if (base == NULL || count < 2u || size == 0u || compare == NULL) return;
    quicksort((char *)base, 0u, count - 1u, size, compare);
}

/* ---- formatted output ---- */

struct fmt_sink {
    char *buf;
    size_t capacity;
    size_t length;
};

static void fmt_emit(struct fmt_sink *out, char c) {
    if (out->length + 1u < out->capacity) out->buf[out->length] = c;
    ++out->length;
}

static void fmt_repeat(struct fmt_sink *out, char c, size_t count) {
    for (size_t i = 0u; i < count; ++i) fmt_emit(out, c);
}

static void fmt_unsigned(struct fmt_sink *out, uint64_t value, unsigned base,
                         int width, bool zero_pad, bool left, bool upper) {
    char digits[21];
    size_t count = 0u;
    if (value == 0u) {
        digits[count++] = '0';
    } else {
        while (value != 0u) {
            const unsigned d = (unsigned)(value % base);
            digits[count++] = d < 10u ? (char)('0' + d)
                                      : (char)((upper ? 'A' : 'a') + d - 10u);
            value /= base;
        }
    }
    const size_t pad = count < (size_t)width ? (size_t)width - count : 0u;
    if (left) {
        for (size_t i = count; i-- > 0u;) fmt_emit(out, digits[i]);
        fmt_repeat(out, ' ', pad);
    } else if (zero_pad) {
        fmt_repeat(out, '0', pad);
        for (size_t i = count; i-- > 0u;) fmt_emit(out, digits[i]);
    } else {
        fmt_repeat(out, ' ', pad);
        for (size_t i = count; i-- > 0u;) fmt_emit(out, digits[i]);
    }
}

static void fmt_signed(struct fmt_sink *out, int64_t value, int width,
                       bool zero_pad, bool left, bool plus) {
    char sign = '\0';
    if (value < 0) { sign = '-'; value = 0 - (int64_t)(uint64_t)value; }
    else if (plus) sign = '+';
    char digits[21];
    size_t count = 0u;
    if (value == 0u) {
        digits[count++] = '0';
    } else {
        while (value != 0u) {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        }
    }
    const size_t sign_len = sign != '\0' ? 1u : 0u;
    const size_t min_width = (size_t)width > sign_len ? (size_t)width - sign_len : 0u;
    const size_t pad = count < min_width ? min_width - count : 0u;
    if (left) {
        if (sign != '\0') fmt_emit(out, sign);
        for (size_t i = count; i-- > 0u;) fmt_emit(out, digits[i]);
        fmt_repeat(out, ' ', pad);
    } else {
        if (!zero_pad) fmt_repeat(out, ' ', pad);
        if (sign != '\0') fmt_emit(out, sign);
        if (zero_pad) fmt_repeat(out, '0', pad);
        for (size_t i = count; i-- > 0u;) fmt_emit(out, digits[i]);
    }
}

int vsnprintf(char *buf, size_t capacity, const char *fmt, va_list ap) {
    struct fmt_sink out;
    out.buf = buf;
    out.capacity = capacity;
    out.length = 0u;
    for (; *fmt != '\0'; ++fmt) {
        if (*fmt != '%') { fmt_emit(&out, *fmt); continue; }
        ++fmt;
        if (*fmt == '\0') break;
        if (*fmt == '%') { fmt_emit(&out, '%'); continue; }
        bool left = false, plus = false, zero = false;
        for (;;) {
            if (*fmt == '-') left = true;
            else if (*fmt == '+') plus = true;
            else if (*fmt == ' ') { if (!plus) plus = true; }
            else if (*fmt == '0') zero = true;
            else break;
            ++fmt;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }
        int precision = -1;
        if (*fmt == '.') {
            ++fmt;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                ++fmt;
            }
        }
        int length = 0;
        if (*fmt == 'h') { length = -1; ++fmt; if (*fmt == 'h') ++fmt; }
        else if (*fmt == 'l') { length = 1; ++fmt; if (*fmt == 'l') { length = 2; ++fmt; } }
        const char spec = *fmt;
        if (spec == '\0') break;
        const bool zpad = (zero && !left) || precision >= 0;
        const int number_width = precision > width ? precision : width;
        if (spec == 'd' || spec == 'i') {
            int64_t v;
            if (length == 2) v = (int64_t)va_arg(ap, long long);
            else if (length == 1) v = (int64_t)va_arg(ap, long);
            else v = (int64_t)va_arg(ap, int);
            fmt_signed(&out, v, number_width, zpad, left, plus);
        } else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
            uint64_t v;
            if (length == 2) v = (uint64_t)va_arg(ap, unsigned long long);
            else if (length == 1) v = (uint64_t)va_arg(ap, unsigned long);
            else v = (uint64_t)va_arg(ap, unsigned int);
            const unsigned base = spec == 'u' ? 10u : spec == 'o' ? 8u : 16u;
            fmt_unsigned(&out, v, base, number_width, zpad, left, spec == 'X');
        } else if (spec == 'c') {
            const char c = (char)va_arg(ap, int);
            const size_t pad = (size_t)width > 1u ? (size_t)width - 1u : 0u;
            if (left) { fmt_emit(&out, c); fmt_repeat(&out, ' ', pad); }
            else { fmt_repeat(&out, ' ', pad); fmt_emit(&out, c); }
        } else if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            size_t len = strlen(s);
            if (precision >= 0 && (size_t)precision < len) len = (size_t)precision;
            const size_t pad = len < (size_t)width ? (size_t)width - len : 0u;
            if (left) {
                for (size_t i = 0u; i < len; ++i) fmt_emit(&out, s[i]);
                fmt_repeat(&out, ' ', pad);
            } else {
                fmt_repeat(&out, ' ', pad);
                for (size_t i = 0u; i < len; ++i) fmt_emit(&out, s[i]);
            }
        } else if (spec == 'p') {
            const uintptr_t p = (uintptr_t)va_arg(ap, void *);
            fmt_emit(&out, '0');
            fmt_emit(&out, 'x');
            /* An omitted field width is zero. Subtracting the "0x" prefix
               from that produced -2, which fmt_unsigned then converted to
               SIZE_MAX-sized padding and made Doom appear to hang in
               I_ZoneBase's first pointer diagnostic. */
            const int digit_width = width > 2 ? width - 2 : 0;
            fmt_unsigned(&out, (uint64_t)p, 16u, digit_width, false, left, true);
        } else {
            fmt_emit(&out, '%');
            fmt_emit(&out, spec);
        }
    }
    if (capacity > 0u) {
        const size_t store = out.length < capacity ? out.length : capacity - 1u;
        buf[store] = '\0';
    }
    return (int)out.length;
}

int snprintf(char *buf, size_t capacity, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int result = vsnprintf(buf, capacity, fmt, ap);
    va_end(ap);
    return result;
}

void *malloc(size_t size) {
    return demon_port_malloc(size);
}

void free(void *ptr) { demon_port_free(ptr); }

void *calloc(size_t count, size_t size) {
    if (count != 0u && size > (size_t)-1 / count) return NULL;
    const size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr != NULL) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0u) { free(ptr); return NULL; }
    return demon_port_realloc(ptr, size);
}

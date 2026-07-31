/*
 * DemonOS Settings -- a minimal skeleton per the desktop design: sidebar
 * navigation into rounded content cards. Only Appearance and About exist
 * for now; more sections are meant to slot into the same sidebar list.
 */
#include <X11/Xlib.h>
#include <demon/c_app.h>
#include <stdint.h>

namespace {

constexpr unsigned int kWindowW = 320u;
constexpr unsigned int kWindowH = 260u;
constexpr int kSidebarW = 92;
constexpr int kRowHeight = 28;

constexpr unsigned long kColorSurface = 0xff20242cu;
constexpr unsigned long kColorSurfaceAlt = 0xff262b33u;
constexpr unsigned long kColorSidebar = 0xff1a1d24u;
constexpr unsigned long kColorEmber = 0xffff5c42u;
constexpr unsigned long kColorViolet = 0xff8b6bffu;
constexpr unsigned long kColorText = 0xfff1f3f5u;
constexpr unsigned long kColorTextMuted = 0xff9ba3afu;

constexpr unsigned int kSectionCount = 2u;
constexpr const char *kSectionNames[kSectionCount] = {"Appearance", "About"};

int stringLength(const char *text) {
    int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

void writeU64(char *out, int &offset, uint64_t value) {
    char digits[21];
    int index = 21;
    do {
        digits[--index] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (index < 21) out[offset++] = digits[index++];
}

class Settings {
public:
    explicit Settings(Display *display)
        : display_(display), window_(None), gc_(nullptr), section_(0u),
          accent_(0u) {}

    bool open() {
        const Window root = XDefaultRootWindow(display_);
        window_ = XCreateSimpleWindow(display_, root, 180, 130, kWindowW,
            kWindowH, 0u, 0xff2f3540u, kColorSurface);
        if (window_ == None) return false;
        XSelectInput(display_, window_, KeyPressMask | ButtonPressMask);
        gc_ = XCreateGC(display_, window_, 0u, nullptr);
        draw();
        XMapWindow(display_, window_);
        return true;
    }

    uint64_t run() {
        XEvent event;
        for (;;) {
            if (!XNextEvent(display_, &event)) return 214u;
            switch (event.type) {
            case KeyPress:
                handleKey(event.xkey.keycode);
                break;
            case ButtonPress:
                handleClick(event.xbutton.x, event.xbutton.y);
                break;
            default:
                break;
            }
        }
    }

private:
    void handleKey(unsigned int keycode) {
        switch (keycode) {
        case 72u: /* Up */
            if (section_ > 0u) --section_;
            draw();
            break;
        case 80u: /* Down */
            if (section_ + 1u < kSectionCount) ++section_;
            draw();
            break;
        case 1u: /* Escape */
            demon_exit(0u);
            break;
        default:
            break;
        }
    }

    void handleClick(int x, int y) {
        if (x < kSidebarW) {
            const unsigned int row = static_cast<unsigned int>(y / kRowHeight);
            if (row < kSectionCount) {
                section_ = row;
                draw();
            }
            return;
        }
        if (section_ == 0u) {
            /* Two accent swatches on the Appearance card. */
            if (y >= 60 && y < 92) {
                if (x >= 106 && x < 138) { accent_ = 0u; draw(); }
                else if (x >= 150 && x < 182) { accent_ = 1u; draw(); }
            }
        }
    }

    void drawSidebar() {
        XSetForeground(display_, gc_, kColorSidebar);
        XFillRectangle(display_, window_, gc_, 0, 0,
                       static_cast<unsigned int>(kSidebarW), kWindowH);
        for (unsigned int index = 0u; index < kSectionCount; ++index) {
            const int row_y = static_cast<int>(index) * kRowHeight;
            if (index == section_) {
                XSetForeground(display_, gc_, kColorSurfaceAlt);
                XFillRectangle(display_, window_, gc_, 0, row_y,
                               static_cast<unsigned int>(kSidebarW), kRowHeight);
                XSetForeground(display_, gc_, kColorEmber);
                XFillRectangle(display_, window_, gc_, 0, row_y, 3u, kRowHeight);
            }
            XSetForeground(display_, gc_, kColorText);
            XDrawString(display_, window_, gc_, 12, row_y + 18,
                       kSectionNames[index],
                       static_cast<int>(stringLength(kSectionNames[index])));
        }
    }

    void drawCard(int x, int y, unsigned int w, unsigned int h) {
        XSetForeground(display_, gc_, kColorSurfaceAlt);
        XFillRectangle(display_, window_, gc_, x, y, w, h);
    }

    void drawAppearance() {
        const int cx = kSidebarW + 12;
        drawCard(cx, 10, kWindowW - static_cast<unsigned int>(kSidebarW) - 22u, 200u);
        XSetForeground(display_, gc_, kColorText);
        static const char title[] = "Appearance";
        XDrawString(display_, window_, gc_, cx + 10, 30, title,
                   static_cast<int>(sizeof(title) - 1u));
        XSetForeground(display_, gc_, kColorTextMuted);
        static const char label[] = "Accent color";
        XDrawString(display_, window_, gc_, cx + 10, 54, label,
                   static_cast<int>(sizeof(label) - 1u));

        /* Ember swatch. */
        if (accent_ == 0u) {
            XSetForeground(display_, gc_, kColorText);
            XDrawRectangle(display_, window_, gc_, cx + 10, 60, 32u, 32u);
        }
        XSetForeground(display_, gc_, kColorEmber);
        XFillRectangle(display_, window_, gc_, cx + 12, 62, 28u, 28u);

        /* Violet swatch. */
        if (accent_ == 1u) {
            XSetForeground(display_, gc_, kColorText);
            XDrawRectangle(display_, window_, gc_, cx + 54, 60, 32u, 32u);
        }
        XSetForeground(display_, gc_, kColorViolet);
        XFillRectangle(display_, window_, gc_, cx + 56, 62, 28u, 28u);

        XSetForeground(display_, gc_, kColorTextMuted);
        static const char note[] = "Applies to new windows for now.";
        XDrawString(display_, window_, gc_, cx + 10, 122, note,
                   static_cast<int>(sizeof(note) - 1u));
    }

    void drawAbout() {
        const int cx = kSidebarW + 12;
        drawCard(cx, 10, kWindowW - static_cast<unsigned int>(kSidebarW) - 22u, 200u);
        XSetForeground(display_, gc_, kColorText);
        static const char title[] = "DemonOS";
        XDrawString(display_, window_, gc_, cx + 10, 30, title,
                   static_cast<int>(sizeof(title) - 1u));
        XSetForeground(display_, gc_, kColorTextMuted);
        static const char subtitle[] = "MAKO-ABI desktop, native Xlib shell";
        XDrawString(display_, window_, gc_, cx + 10, 48, subtitle,
                   static_cast<int>(sizeof(subtitle) - 1u));

        char line[48];
        int offset = 0;
        static const char pid_label[] = "pid: ";
        for (unsigned int i = 0u; i < sizeof(pid_label) - 1u; ++i)
            line[offset++] = pid_label[i];
        writeU64(line, offset, demon_getpid());
        XSetForeground(display_, gc_, kColorText);
        XDrawString(display_, window_, gc_, cx + 10, 76, line, offset);

        offset = 0;
        static const char tick_label[] = "uptime ticks: ";
        for (unsigned int i = 0u; i < sizeof(tick_label) - 1u; ++i)
            line[offset++] = tick_label[i];
        writeU64(line, offset, demon_ticks());
        XDrawString(display_, window_, gc_, cx + 10, 94, line, offset);
    }

    void draw() {
        if (window_ == None || gc_ == nullptr) return;
        XSetForeground(display_, gc_, kColorSurface);
        XFillRectangle(display_, window_, gc_, 0, 0, kWindowW, kWindowH);
        drawSidebar();
        if (section_ == 0u) drawAppearance();
        else drawAbout();
    }

    Display *display_;
    Window window_;
    GC gc_;
    unsigned int section_;
    unsigned int accent_;
};

} // namespace

extern "C" uint64_t settings_main() {
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) return 230u;
    Settings app(display);
    if (!app.open()) {
        XCloseDisplay(display);
        return 231u;
    }
    demon_write("SETTINGS_READY\n", 16u);
    const uint64_t status = app.run();
    XCloseDisplay(display);
    return status;
}

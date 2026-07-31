/*
 * DemonWM -- the native window manager for DemonOS's own desktop shell.
 *
 * This is a freestanding platform entry point spawned by the kernel
 * alongside the ring-3 compositor and the interactive DemonX server (see
 * src/kernel.c). It speaks only public Xlib calls against DemonX,
 * following the same click-to-focus, raise-on-focus, Alt+drag policy the
 * earlier PekWM port validated before it was retired in favor of this
 * from-scratch replacement.
 */
#include <X11/Xlib.h>
#include <demon/c_app.h>
#include <stdint.h>

namespace {

constexpr unsigned int kPanelHeight = 28u;
constexpr unsigned int kScreenWidth = 640u;
constexpr unsigned int kScreenHeight = 480u;
constexpr unsigned int kMargin = 6u;

/* DemonOS palette: dark ember/violet accents (see docs/desktop design). */
constexpr unsigned long kColorPanelBg = 0xff1a1d24u;
constexpr unsigned long kColorSurface = 0xff20242cu;
constexpr unsigned long kColorSurfaceAlt = 0xff262b33u;
constexpr unsigned long kColorEmber = 0xffff5c42u;
constexpr unsigned long kColorViolet = 0xff8b6bffu;
constexpr unsigned long kColorText = 0xfff1f3f5u;
constexpr unsigned long kColorTextMuted = 0xff9ba3afu;

constexpr unsigned int kAppCount = 5u;
constexpr unsigned int kPinnedCount = 3u;
constexpr const char *kAppNames[kAppCount] = {"Terminal", "Calculator",
                                              "Browser", "Files", "Settings"};

/* Launcher button lives at the left of the panel. */
constexpr int kLauncherBtnX0 = static_cast<int>(kMargin) + 4;
constexpr int kLauncherBtnX1 = kLauncherBtnX0 + 72;

constexpr int kLauncherW = 360;
constexpr int kLauncherH = 320;
constexpr int kSearchH = 26;
constexpr int kCellW = 108;
constexpr int kCellH = 60;
constexpr int kGridCols = 3;
constexpr unsigned int kGridColsU = 3u;
constexpr unsigned int kPinnedIndices[kPinnedCount] = {0u, 1u, 2u};
constexpr unsigned int kSearchMax = 24u;

int stringLength(const char *text) {
    int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

/* Lowercase letter typed by a set-1 scan code, or '\0' for anything else
   (only the QWERTY letter block + space are needed for app-name search). */
char scancodeToLower(unsigned int keycode) {
    switch (keycode) {
    case 0x10u: return 'q'; case 0x11u: return 'w'; case 0x12u: return 'e';
    case 0x13u: return 'r'; case 0x14u: return 't'; case 0x15u: return 'y';
    case 0x16u: return 'u'; case 0x17u: return 'i'; case 0x18u: return 'o';
    case 0x19u: return 'p'; case 0x1Eu: return 'a'; case 0x1Fu: return 's';
    case 0x20u: return 'd'; case 0x21u: return 'f'; case 0x22u: return 'g';
    case 0x23u: return 'h'; case 0x24u: return 'j'; case 0x25u: return 'k';
    case 0x26u: return 'l'; case 0x2Cu: return 'z'; case 0x2Du: return 'x';
    case 0x2Eu: return 'c'; case 0x2Fu: return 'v'; case 0x30u: return 'b';
    case 0x31u: return 'n'; case 0x32u: return 'm'; case 0x39u: return ' ';
    default: return '\0';
    }
}

bool containsIgnoreCase(const char *haystack, const char *needle,
                        unsigned int needle_length) {
    if (needle_length == 0u) return true;
    const int haystack_length = stringLength(haystack);
    for (int start = 0; start + static_cast<int>(needle_length) <= haystack_length;
        ++start) {
        bool matched = true;
        for (unsigned int i = 0u; i < needle_length; ++i) {
            char a = haystack[start + static_cast<int>(i)];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) { matched = false; break; }
        }
        if (matched) return true;
    }
    return false;
}

class WindowManager {
public:
    explicit WindowManager(Display *display)
        : display_(display), root_(XDefaultRootWindow(display)),
          panel_(None), panel_gc_(nullptr), client_count_(0),
          drag_window_(None), drag_origin_x_(0), drag_origin_y_(0),
          window_origin_x_(0), window_origin_y_(0), last_clock_minute_(0xff),
          launcher_(None), launcher_gc_(nullptr), launcher_open_(false),
          launcher_selected_(0u), search_length_(0u), visible_count_(kAppCount),
          resize_frame_(None), resize_origin_x_(0), resize_origin_y_(0),
          resize_start_width_(0), resize_start_height_(0) {
        for (unsigned int index = 0u; index < kAppCount; ++index)
            visible_[index] = index;
        for (unsigned int index = 0; index < max_clients_; ++index) {
            clients_[index].client = None;
            clients_[index].frame = None;
            clients_[index].gc = nullptr;
        }
    }

    bool claimDisplay() {
        return XSelectInput(display_, root_,
            SubstructureRedirectMask | SubstructureNotifyMask |
            PropertyChangeMask | KeyPressMask) != 0;
    }

    void createPanel() {
        panel_ = XCreateSimpleWindow(display_, root_,
            static_cast<int>(kMargin), static_cast<int>(kMargin),
            kScreenWidth - 2u * kMargin, kPanelHeight, 0u, 0xff2f3540u,
            kColorPanelBg);
        if (panel_ == None) return;
        XSelectInput(display_, panel_,
                     ButtonPressMask | KeyPressMask | ExposureMask);
        panel_gc_ = XCreateGC(display_, panel_, 0u, nullptr);
        drawPanel(0xff);
        XRaiseWindow(display_, panel_);
        XMapWindow(display_, panel_);
    }

    void createLauncher() {
        const int x = (static_cast<int>(kScreenWidth) - kLauncherW) / 2;
        const int y = static_cast<int>(kMargin + kPanelHeight) + 8;
        launcher_ = XCreateSimpleWindow(display_, root_, x, y,
            static_cast<unsigned int>(kLauncherW),
            static_cast<unsigned int>(kLauncherH), 0u, 0xff2f3540u,
            kColorSurface);
        if (launcher_ == None) return;
        XSelectInput(display_, launcher_,
                     ButtonPressMask | KeyPressMask | ExposureMask);
        launcher_gc_ = XCreateGC(display_, launcher_, 0u, nullptr);
    }

    void adoptExistingWindows() {
        Window root = None, parent = None, *children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(display_, root_, &root, &parent, &children, &count))
            return;
        for (unsigned int index = 0; index < count; ++index) {
            XWindowAttributes attributes;
            if (XGetWindowAttributes(display_, children[index], &attributes) &&
                attributes.map_state != IsUnmapped) {
                if (children[index] != panel_) manage(children[index]);
            }
        }
        XFree(children);
    }

    uint64_t run() {
        XEvent event;
        for (;;) {
            const unsigned int minute = static_cast<unsigned int>(
                (demon_ticks() / 6000u) % 60u);
            if (minute != last_clock_minute_) {
                last_clock_minute_ = minute;
                drawPanel(minute);
            }
            if (!XNextEvent(display_, &event)) return 214u;
            switch (event.type) {
            case MapRequest:
                manage(event.xmaprequest.window);
                break;
            case ConfigureRequest:
                configure(event.xconfigurerequest);
                break;
            case ButtonPress: {
                if (event.xbutton.window == panel_) {
                    panelClick(event.xbutton.x);
                    break;
                }
                if (event.xbutton.window == launcher_) {
                    launcherClick(event.xbutton.x, event.xbutton.y);
                    break;
                }
                Client *hit = findClient(event.xbutton.window);
                if (hit != nullptr && event.xbutton.window == hit->frame &&
                    event.xbutton.y < title_height_) {
                    const int control = titleControlAt(*hit, event.xbutton.x);
                    if (control == 2) {
                        closeClient(*hit);
                        break;
                    }
                    if (control == 1) {
                        toggleMaximize(*hit);
                        break;
                    }
                }
                beginDrag(event.xbutton);
                break;
            }
            case KeyPress:
                /* DemonX reports the native set-1 scan code as keycode. */
                handleKey(event.xkey.keycode);
                break;
            case MotionNotify:
                if (resize_frame_ != None) continueResize(event.xmotion);
                else continueDrag(event.xmotion);
                break;
            case ButtonRelease:
                endDrag();
                endResize();
                break;
            case UnmapNotify:
                if (drag_window_ == event.xunmap.window) endDrag();
                if (resize_frame_ == event.xunmap.window) endResize();
                break;
            default:
                break;
            }
        }
    }

private:
    static constexpr unsigned int max_clients_ = 4u;
    static constexpr int title_height_ = 24;

    struct Client {
        Window client;
        Window frame;
        GC gc;
        int width;
        int height;
        int orig_x;
        int orig_y;
        int orig_width;
        int orig_height;
        bool maximized;
    };

    Client *findClient(Window window) {
        for (unsigned int index = 0; index < client_count_; ++index)
            if (clients_[index].client == window ||
                clients_[index].frame == window)
                return &clients_[index];
        return nullptr;
    }

    Window managedWindow(Window window) {
        Client *client = findClient(window);
        return client == nullptr ? window : client->frame;
    }

    void drawPanel(unsigned int minute) {
        if (panel_ == None || panel_gc_ == nullptr) return;
        const unsigned int width = kScreenWidth - 2u * kMargin;
        XSetForeground(display_, panel_gc_, kColorPanelBg);
        XFillRectangle(display_, panel_, panel_gc_, 0, 0, width, kPanelHeight);

        /* Launcher button. */
        XSetForeground(display_, panel_gc_,
                       launcher_open_ ? kColorViolet : kColorEmber);
        XFillRectangle(display_, panel_, panel_gc_, kLauncherBtnX0, 4,
                       static_cast<unsigned int>(kLauncherBtnX1 - kLauncherBtnX0),
                       static_cast<unsigned int>(kPanelHeight) - 8u);
        XSetForeground(display_, panel_gc_, kColorText);
        static const char label[] = "DemonOS";
        XDrawString(display_, panel_, panel_gc_, kLauncherBtnX0 + 8, 19,
                    label, static_cast<int>(sizeof(label) - 1u));

        /* Workspace dots, just left of center. */
        const int dots_x = static_cast<int>(width) / 2 - 30;
        for (unsigned int dot = 0u; dot < 3u; ++dot) {
            XSetForeground(display_, panel_gc_,
                           dot == 0u ? kColorEmber : kColorTextMuted);
            XFillRectangle(display_, panel_, panel_gc_,
                           dots_x + static_cast<int>(dot) * 10, 12, 6u, 6u);
        }

        /* Clock, centered. */
        XSetForeground(display_, panel_gc_, kColorText);
        char clock_text[6];
        unsigned int index = 0;
        clock_text[index++] = static_cast<char>('0' + (minute / 10u) % 10u);
        clock_text[index++] = static_cast<char>('0' + minute % 10u);
        clock_text[index] = '\0';
        XDrawString(display_, panel_, panel_gc_,
                    static_cast<int>(width) / 2 + 8, 19, clock_text,
                    static_cast<int>(index));

        /* Tray placeholders, right-aligned. */
        for (unsigned int slot = 0u; slot < 3u; ++slot) {
            XSetForeground(display_, panel_gc_, kColorTextMuted);
            const int tray_x = static_cast<int>(width) - 12 -
                static_cast<int>(slot) * 22 - 14;
            XFillRectangle(display_, panel_, panel_gc_, tray_x, 8, 14u, 14u);
        }
    }

    void launch(unsigned int kind) {
        static const char terminal[] = "/system/bin/terminal-client.elf";
        static const char calculator[] = "/system/bin/calculator.elf";
        static const char browser[] = "/system/bin/browser-client.elf";
        static const char filemanager[] = "/system/bin/filemanager.elf";
        static const char settings[] = "/system/bin/settings.elf";
        const char *path = terminal;
        uint64_t length = sizeof(terminal) - 1u;
        uint64_t services = (1u << 6) | (1u << 9);
        if (kind == 1u) {
            path = calculator;
            length = sizeof(calculator) - 1u;
        } else if (kind == 2u) {
            path = browser;
            length = sizeof(browser) - 1u;
            services |= 1u << 4;
        } else if (kind == 3u) {
            path = filemanager;
            length = sizeof(filemanager) - 1u;
            services |= 1u << 4; /* CAPABILITY_SERVICE_STORAGE */
        } else if (kind == 4u) {
            path = settings;
            length = sizeof(settings) - 1u;
        }
        (void)demon_spawn(path, length, services);
    }

    void panelClick(int x) {
        if (x >= kLauncherBtnX0 && x < kLauncherBtnX1) toggleLauncher();
    }

    void toggleLauncher() {
        if (launcher_ == None) return;
        launcher_open_ = !launcher_open_;
        if (launcher_open_) {
            launcher_selected_ = 0u;
            search_length_ = 0u;
            updateFilter();
            drawLauncher();
            XMapRaised(display_, launcher_);
            XSetInputFocus(display_, launcher_, RevertToPointerRoot,
                           CurrentTime);
        } else {
            XUnmapWindow(display_, launcher_);
        }
        drawPanel(last_clock_minute_);
    }

    void closeLauncher() {
        if (!launcher_open_) return;
        launcher_open_ = false;
        XUnmapWindow(display_, launcher_);
        drawPanel(last_clock_minute_);
    }

    void updateFilter() {
        visible_count_ = 0u;
        for (unsigned int index = 0u; index < kAppCount; ++index) {
            if (containsIgnoreCase(kAppNames[index], search_buffer_, search_length_))
                visible_[visible_count_++] = index;
        }
        if (launcher_selected_ >= visible_count_)
            launcher_selected_ = visible_count_ > 0u ? visible_count_ - 1u : 0u;
    }

    void drawLauncher() {
        if (launcher_ == None || launcher_gc_ == nullptr) return;
        XSetForeground(display_, launcher_gc_, kColorSurface);
        XFillRectangle(display_, launcher_, launcher_gc_, 0, 0,
                       static_cast<unsigned int>(kLauncherW),
                       static_cast<unsigned int>(kLauncherH));

        /* Search bar: shows what's actually been typed, or a hint. */
        XSetForeground(display_, launcher_gc_, kColorSurfaceAlt);
        XFillRectangle(display_, launcher_, launcher_gc_, 10, 10,
                       static_cast<unsigned int>(kLauncherW) - 20u,
                       static_cast<unsigned int>(kSearchH));
        if (search_length_ == 0u) {
            XSetForeground(display_, launcher_gc_, kColorTextMuted);
            static const char search_hint[] = "Search apps...";
            XDrawString(display_, launcher_, launcher_gc_, 18, 27, search_hint,
                        static_cast<int>(sizeof(search_hint) - 1u));
        } else {
            XSetForeground(display_, launcher_gc_, kColorText);
            XDrawString(display_, launcher_, launcher_gc_, 18, 27, search_buffer_,
                        static_cast<int>(search_length_));
        }

        /* Pinned row (first kPinnedCount apps, single row, unaffected by
           search -- always available regardless of what's typed). */
        const int pinned_y = 10 + kSearchH + 12;
        XSetForeground(display_, launcher_gc_, kColorTextMuted);
        static const char pinned_label[] = "Pinned";
        XDrawString(display_, launcher_, launcher_gc_, 10, pinned_y,
                    pinned_label, static_cast<int>(sizeof(pinned_label) - 1u));
        drawAppGrid(pinned_y + 6, /*selectable=*/false, kPinnedIndices, kPinnedCount);

        /* App grid: apps matching the search text, wraps to more than one
           row once it grows past kGridCols. */
        const int grid_label_y = pinned_y + 6 + kCellH + 14;
        XSetForeground(display_, launcher_gc_, kColorTextMuted);
        static const char grid_label[] = "All Apps";
        XDrawString(display_, launcher_, launcher_gc_, 10, grid_label_y,
                    grid_label, static_cast<int>(sizeof(grid_label) - 1u));
        if (visible_count_ == 0u) {
            XSetForeground(display_, launcher_gc_, kColorTextMuted);
            static const char no_match[] = "No matching apps";
            XDrawString(display_, launcher_, launcher_gc_, 10, grid_label_y + 24,
                       no_match, static_cast<int>(sizeof(no_match) - 1u));
        } else {
            drawAppGrid(grid_label_y + 6, /*selectable=*/true, visible_, visible_count_);
        }
    }

    void drawAppGrid(int top, bool selectable, const unsigned int *indices,
                     unsigned int count) {
        for (unsigned int slot = 0u; slot < count; ++slot) {
            const unsigned int index = indices[slot];
            const int col = static_cast<int>(slot % kGridColsU);
            const int row = static_cast<int>(slot / kGridColsU);
            const int cell_x = 10 + col * kCellW;
            const int cell_y = top + row * kCellH;
            const bool selected = selectable && slot == launcher_selected_;
            if (selected) {
                XSetForeground(display_, launcher_gc_, kColorEmber);
                XDrawRectangle(display_, launcher_, launcher_gc_, cell_x, cell_y,
                              static_cast<unsigned int>(kCellW) - 8u,
                              static_cast<unsigned int>(kCellH) - 4u);
            }
            XSetForeground(display_, launcher_gc_, kColorSurfaceAlt);
            XFillRectangle(display_, launcher_, launcher_gc_, cell_x + 2,
                           cell_y + 2, static_cast<unsigned int>(kCellW) - 12u,
                           static_cast<unsigned int>(kCellH) - 8u);
            XSetForeground(display_, launcher_gc_, kColorViolet);
            XFillRectangle(display_, launcher_, launcher_gc_, cell_x + 10,
                           cell_y + 8, 20u, 20u);
            XSetForeground(display_, launcher_gc_, kColorText);
            XDrawString(display_, launcher_, launcher_gc_, cell_x + 10,
                        cell_y + 44, kAppNames[index],
                        static_cast<int>(stringLength(kAppNames[index])));
        }
    }

    void launcherClick(int x, int y) {
        const int pinned_y = 10 + kSearchH + 12 + 6;
        const int grid_y = pinned_y + kCellH + 14 + 6;
        int slot = hitTestGrid(x, y, grid_y, visible_count_);
        if (slot >= 0) {
            launch(visible_[slot]);
            closeLauncher();
            return;
        }
        slot = hitTestGrid(x, y, pinned_y, kPinnedCount);
        if (slot >= 0) {
            launch(kPinnedIndices[slot]);
            closeLauncher();
        }
    }

    int hitTestGrid(int x, int y, int top, unsigned int count) const {
        if (y < top) return -1;
        const int col = (x - 10) / kCellW;
        const int row = (y - top) / kCellH;
        if (col < 0 || col >= kGridCols || row < 0) return -1;
        const int index = row * kGridCols + col;
        if (index < 0 || static_cast<unsigned int>(index) >= count) return -1;
        const int cell_x = 10 + col * kCellW;
        if (x < cell_x || x >= cell_x + kCellW - 8) return -1;
        return index;
    }

    void handleKey(unsigned int keycode) {
        if (keycode == 91u) { /* Left Super */
            toggleLauncher();
            return;
        }
        if (!launcher_open_) {
            /* T is a keyboard-first terminal shortcut. */
            if (keycode == 20u) launch(0u);
            return;
        }
        switch (keycode) {
        case 1u: /* Escape */
            closeLauncher();
            return;
        case 28u: /* Enter */
            if (visible_count_ > 0u) launch(visible_[launcher_selected_]);
            closeLauncher();
            return;
        case 14u: /* Backspace */
            if (search_length_ > 0u) --search_length_;
            updateFilter();
            drawLauncher();
            return;
        case 75u: /* Left */
            if (launcher_selected_ % kGridColsU > 0u) --launcher_selected_;
            drawLauncher();
            return;
        case 77u: /* Right */
            if (launcher_selected_ % kGridColsU < kGridColsU - 1u &&
                launcher_selected_ + 1u < visible_count_)
                ++launcher_selected_;
            drawLauncher();
            return;
        case 72u: /* Up */
            if (launcher_selected_ >= kGridColsU)
                launcher_selected_ -= kGridColsU;
            drawLauncher();
            return;
        case 80u: /* Down */
            if (launcher_selected_ + kGridColsU < visible_count_)
                launcher_selected_ += kGridColsU;
            drawLauncher();
            return;
        default:
            break;
        }
        const char typed = scancodeToLower(keycode);
        if (typed != '\0' && search_length_ < kSearchMax) {
            search_buffer_[search_length_++] = typed;
            updateFilter();
            drawLauncher();
        }
    }

    void watchWindow(Window window) {
        XSelectInput(display_, window,
            StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
            PointerMotionMask | FocusChangeMask | PropertyChangeMask);
    }

    void manage(Window window) {
        if (window == panel_ || findClient(window) != nullptr ||
            client_count_ >= max_clients_)
            return;
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display_, window, &attributes)) return;
        int frame_y = attributes.y - title_height_;
        if (frame_y < static_cast<int>(kMargin + kPanelHeight) + 2)
            frame_y = static_cast<int>(kMargin + kPanelHeight) + 2;
        Window frame = XCreateSimpleWindow(display_, root_,
            attributes.x, frame_y, static_cast<unsigned int>(attributes.width),
            static_cast<unsigned int>(attributes.height + title_height_),
            1u, 0xff8996a8u, 0xff171b22u);
        if (frame == None) return;
        Client *entry = &clients_[client_count_++];
        entry->client = window;
        entry->frame = frame;
        entry->gc = XCreateGC(display_, frame, 0u, nullptr);
        entry->width = static_cast<int>(attributes.width);
        entry->height = static_cast<int>(attributes.height) + title_height_;
        entry->orig_x = attributes.x;
        entry->orig_y = frame_y;
        entry->orig_width = entry->width;
        entry->orig_height = entry->height;
        entry->maximized = false;
        XSelectInput(display_, frame,
            StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
            PointerMotionMask | ExposureMask | FocusChangeMask);
        watchWindow(window);
        XReparentWindow(display_, window, frame, 0, title_height_);
        drawFrame(*entry, true);
        XMapWindow(display_, window);
        XMapRaised(display_, frame);
        focusAndRaise(frame);
    }

    /* Title-bar control buttons live right-aligned: [maximize][close]. */
    static constexpr int kResizeGrip = 10;
    static constexpr int kMinWindowWidth = 96;
    static constexpr int kMinWindowHeight = 64;

    static constexpr int kControlSize = 16;
    static constexpr int kControlGap = 4;

    int maximizeControlX(const Client &entry) const {
        return entry.width - 8 - 2 * kControlSize - kControlGap;
    }

    int closeControlX(const Client &entry) const {
        return entry.width - 8 - kControlSize;
    }

    void drawFrame(Client &entry, bool focused) {
        const unsigned long title = focused ? kColorViolet : kColorSurfaceAlt;
        XSetForeground(display_, entry.gc, title);
        XFillRectangle(display_, entry.frame, entry.gc, 0, 0,
                       static_cast<unsigned int>(entry.width),
                       static_cast<unsigned int>(title_height_));
        XSetForeground(display_, entry.gc, kColorText);
        static const char title_text[] = "DemonWM Client";
        XDrawString(display_, entry.frame, entry.gc, 9, 17, title_text,
                    static_cast<int>(sizeof(title_text) - 1u));

        const int control_y = (title_height_ - kControlSize) / 2;

        /* Maximize / restore control: a plain square outline. */
        const int max_x = maximizeControlX(entry);
        XSetForeground(display_, entry.gc, kColorTextMuted);
        XDrawRectangle(display_, entry.frame, entry.gc, max_x, control_y,
                       static_cast<unsigned int>(kControlSize),
                       static_cast<unsigned int>(kControlSize));

        /* Close control: an ember square with a white X mark. */
        const int close_x = closeControlX(entry);
        XSetForeground(display_, entry.gc, kColorEmber);
        XFillRectangle(display_, entry.frame, entry.gc, close_x, control_y,
                       static_cast<unsigned int>(kControlSize),
                       static_cast<unsigned int>(kControlSize));
        XSetForeground(display_, entry.gc, kColorText);
        XDrawLine(display_, entry.frame, entry.gc, close_x + 4,
                 control_y + 4, close_x + kControlSize - 4,
                 control_y + kControlSize - 4);
        XDrawLine(display_, entry.frame, entry.gc, close_x + kControlSize - 4,
                 control_y + 4, close_x + 4, control_y + kControlSize - 4);
    }

    /* Returns 1 for the maximize control, 2 for close, 0 for neither. */
    int titleControlAt(const Client &entry, int x) const {
        const int max_x = maximizeControlX(entry);
        if (x >= max_x && x < max_x + kControlSize) return 1;
        const int close_x = closeControlX(entry);
        if (x >= close_x && x < close_x + kControlSize) return 2;
        return 0;
    }

    void closeClient(Client &entry) {
        XKillClient(display_, entry.client);
        XDestroyWindow(display_, entry.frame);
        if (drag_window_ == entry.frame) drag_window_ = None;
        if (resize_frame_ == entry.frame) resize_frame_ = None;
        const unsigned int index =
            static_cast<unsigned int>(&entry - clients_);
        clients_[index] = clients_[client_count_ - 1u];
        --client_count_;
    }

    void toggleMaximize(Client &entry) {
        if (!entry.maximized) {
            XWindowAttributes attributes;
            if (!XGetWindowAttributes(display_, entry.frame, &attributes))
                return;
            entry.orig_x = attributes.x;
            entry.orig_y = attributes.y;
            entry.orig_width = entry.width;
            entry.orig_height = entry.height;
            const int x = 0;
            const int y = static_cast<int>(kMargin + kPanelHeight) + 2;
            const unsigned int width = kScreenWidth;
            const unsigned int height =
                kScreenHeight - static_cast<unsigned int>(y);
            XMoveResizeWindow(display_, entry.frame, x, y, width, height);
            XResizeWindow(display_, entry.client, width,
                          height - static_cast<unsigned int>(title_height_));
            entry.width = static_cast<int>(width);
            entry.height = static_cast<int>(height);
            entry.maximized = true;
        } else {
            XMoveResizeWindow(display_, entry.frame, entry.orig_x,
                              entry.orig_y,
                              static_cast<unsigned int>(entry.orig_width),
                              static_cast<unsigned int>(entry.orig_height));
            XResizeWindow(display_, entry.client,
                          static_cast<unsigned int>(entry.orig_width),
                          static_cast<unsigned int>(entry.orig_height -
                                                     title_height_));
            entry.width = entry.orig_width;
            entry.height = entry.orig_height;
            entry.maximized = false;
        }
        drawFrame(entry, true);
    }

    void configure(const XConfigureRequestEvent &request) {
        XWindowChanges changes;
        changes.x = request.x;
        changes.y = request.y;
        changes.width = request.width;
        changes.height = request.height;
        changes.border_width = request.border_width;
        changes.sibling = request.above;
        changes.stack_mode = request.detail;
        XConfigureWindow(display_, request.window,
                         static_cast<unsigned int>(request.value_mask),
                         &changes);
    }

    void focusAndRaise(Window window) {
        Client *entry = findClient(window);
        Window target = managedWindow(window);
        XRaiseWindow(display_, target);
        XSetInputFocus(display_, entry == nullptr ? window : entry->client,
                       RevertToPointerRoot, CurrentTime);
        if (entry != nullptr) drawFrame(*entry, true);
    }

    /* A click within kResizeGrip pixels of a client's own bottom-right
       corner starts a resize instead of a move -- checked against the
       client window's local coordinates (event.x/y), since the client
       fills the whole frame below the title bar and so owns that corner
       pixel-for-pixel. There's no visual grip marker yet (nothing to draw
       it in a way the client's own content wouldn't paint over); this is
       a known, honest gap until the WM can composite a corner overlay. */
    bool hitsResizeCorner(const Client &entry, const XButtonEvent &event) const {
        const int client_h = entry.height - title_height_;
        return event.x >= entry.width - kResizeGrip &&
            event.y >= client_h - kResizeGrip;
    }

    void beginDrag(const XButtonEvent &event) {
        Client *client_entry = findClient(event.window);
        if (client_entry != nullptr && event.window == client_entry->client &&
            event.button == 1u && hitsResizeCorner(*client_entry, event)) {
            beginResize(*client_entry, event);
            return;
        }
        Window target = managedWindow(event.window);
        focusAndRaise(target);
        if (event.button != 1u) return;
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display_, target, &attributes)) return;
        drag_window_ = target;
        drag_origin_x_ = event.x_root;
        drag_origin_y_ = event.y_root;
        window_origin_x_ = attributes.x;
        window_origin_y_ = attributes.y;
        XGrabPointer(display_, event.window, False,
                     PointerMotionMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    }

    void continueDrag(const XMotionEvent &event) {
        if (drag_window_ == None) return;
        int x = window_origin_x_ + event.x_root - drag_origin_x_;
        int y = window_origin_y_ + event.y_root - drag_origin_y_;
        if (x < 0) x = 0;
        if (y < static_cast<int>(kMargin + kPanelHeight))
            y = static_cast<int>(kMargin + kPanelHeight);
        if (x > static_cast<int>(kScreenWidth) - 32)
            x = static_cast<int>(kScreenWidth) - 32;
        if (y > static_cast<int>(kScreenHeight) - 32)
            y = static_cast<int>(kScreenHeight) - 32;
        XMoveWindow(display_, drag_window_, x, y);
    }

    void endDrag() {
        if (drag_window_ == None) return;
        XUngrabPointer(display_, CurrentTime);
        drag_window_ = None;
    }

    void beginResize(Client &entry, const XButtonEvent &event) {
        focusAndRaise(entry.frame);
        resize_frame_ = entry.frame;
        resize_origin_x_ = event.x_root;
        resize_origin_y_ = event.y_root;
        resize_start_width_ = entry.width;
        resize_start_height_ = entry.height;
        XGrabPointer(display_, event.window, False,
                     PointerMotionMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    }

    void continueResize(const XMotionEvent &event) {
        if (resize_frame_ == None) return;
        Client *entry = findClient(resize_frame_);
        if (entry == nullptr) { resize_frame_ = None; return; }
        int width = resize_start_width_ + (event.x_root - resize_origin_x_);
        int height = resize_start_height_ + (event.y_root - resize_origin_y_);
        if (width < kMinWindowWidth) width = kMinWindowWidth;
        if (height < kMinWindowHeight) height = kMinWindowHeight;
        if (width > static_cast<int>(kScreenWidth)) width = static_cast<int>(kScreenWidth);
        if (height > static_cast<int>(kScreenHeight)) height = static_cast<int>(kScreenHeight);
        XResizeWindow(display_, entry->frame,
                     static_cast<unsigned int>(width),
                     static_cast<unsigned int>(height));
        XResizeWindow(display_, entry->client,
                     static_cast<unsigned int>(width),
                     static_cast<unsigned int>(height - title_height_));
        entry->width = width;
        entry->height = height;
        entry->maximized = false;
        drawFrame(*entry, true);
    }

    void endResize() {
        if (resize_frame_ == None) return;
        XUngrabPointer(display_, CurrentTime);
        resize_frame_ = None;
    }

    Display *display_;
    Window root_;
    Window panel_;
    GC panel_gc_;
    Client clients_[max_clients_];
    unsigned int client_count_;
    Window drag_window_;
    int drag_origin_x_;
    int drag_origin_y_;
    int window_origin_x_;
    int window_origin_y_;
    unsigned int last_clock_minute_;
    Window launcher_;
    GC launcher_gc_;
    bool launcher_open_;
    unsigned int launcher_selected_;
    char search_buffer_[kSearchMax];
    unsigned int search_length_;
    unsigned int visible_[kAppCount];
    unsigned int visible_count_;
    Window resize_frame_;
    int resize_origin_x_;
    int resize_origin_y_;
    int resize_start_width_;
    int resize_start_height_;
};

} // namespace

extern "C" uint64_t demonwm_main() {
    Display *display = XOpenDisplay(":2");
    if (display == nullptr) return 210u;
    WindowManager manager(display);
    if (!manager.claimDisplay()) {
        XCloseDisplay(display);
        return 211u;
    }
    manager.createPanel();
    manager.createLauncher();
    manager.adoptExistingWindows();
    demon_write("DEMONWM_READY\n", 14u);
    const uint64_t status = manager.run();
    XCloseDisplay(display);
    return status;
}

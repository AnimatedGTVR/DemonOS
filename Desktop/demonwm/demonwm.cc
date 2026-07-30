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

constexpr unsigned int kPanelHeight = 32u;
constexpr unsigned int kScreenWidth = 640u;
constexpr unsigned int kScreenHeight = 480u;

class WindowManager {
public:
    explicit WindowManager(Display *display)
        : display_(display), root_(XDefaultRootWindow(display)),
          panel_(None), panel_gc_(nullptr), client_count_(0),
          drag_window_(None), drag_origin_x_(0), drag_origin_y_(0),
          window_origin_x_(0), window_origin_y_(0), last_clock_minute_(0xff) {
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
        panel_ = XCreateSimpleWindow(display_, root_, 0, 0,
            kScreenWidth, kPanelHeight, 0u, 0xff2a3038u, 0xff14181du);
        if (panel_ == None) return;
        XSelectInput(display_, panel_,
                     ButtonPressMask | KeyPressMask | ExposureMask);
        panel_gc_ = XCreateGC(display_, panel_, 0u, nullptr);
        drawPanel(0xff);
        XRaiseWindow(display_, panel_);
        XMapWindow(display_, panel_);
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
            case ButtonPress:
                if (event.xbutton.window == panel_)
                    launchFromPanel(event.xbutton.x);
                else
                    beginDrag(event.xbutton);
                break;
            case KeyPress:
                /* T is a keyboard-first terminal shortcut. DemonX reports
                   the native set-1 key code here. */
                if (event.xkey.keycode == 20u) launch(0u);
                break;
            case MotionNotify:
                continueDrag(event.xmotion);
                break;
            case ButtonRelease:
                endDrag();
                break;
            case UnmapNotify:
                if (drag_window_ == event.xunmap.window) endDrag();
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
        XSetForeground(display_, panel_gc_, 0xff14181du);
        XFillRectangle(display_, panel_, panel_gc_, 0, 0,
                       kScreenWidth, kPanelHeight);
        XSetForeground(display_, panel_gc_, 0xff3a6ea5u);
        XFillRectangle(display_, panel_, panel_gc_, 4, 4, 96u, 24u);
        XSetForeground(display_, panel_gc_, 0xfff1f3f5u);
        static const char label[] = " DemonOS";
        XDrawString(display_, panel_, panel_gc_, 12, 20, label,
                    static_cast<int>(sizeof(label) - 1u));
        static const char clock_prefix[] = ":";
        char clock_text[8];
        unsigned int index = 0;
        clock_text[index++] = static_cast<char>('0' + (minute / 10u) % 10u);
        clock_text[index++] = static_cast<char>('0' + minute % 10u);
        clock_text[index] = '\0';
        XDrawString(display_, panel_, panel_gc_, kScreenWidth - 40,
                    20, clock_prefix, 1);
        XDrawString(display_, panel_, panel_gc_, kScreenWidth - 32,
                    20, clock_text, static_cast<int>(index));
    }

    void launch(unsigned int kind) {
        static const char terminal[] = "/system/bin/terminal-client.elf";
        static const char calculator[] = "/system/bin/calculator.elf";
        static const char browser[] = "/system/bin/browser-client.elf";
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
        }
        (void)demon_spawn(path, length, services);
    }

    void launchFromPanel(int x) {
        if (x < 112) return;
        if (x < 224) launch(0u);
        else if (x < 336) launch(1u);
        else if (x < 448) launch(2u);
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
        if (frame_y < static_cast<int>(kPanelHeight) + 2)
            frame_y = static_cast<int>(kPanelHeight) + 2;
        Window frame = XCreateSimpleWindow(display_, root_,
            attributes.x, frame_y, static_cast<unsigned int>(attributes.width),
            static_cast<unsigned int>(attributes.height + title_height_),
            1u, 0xff8996a8u, 0xff171b22u);
        if (frame == None) return;
        Client *entry = &clients_[client_count_++];
        entry->client = window;
        entry->frame = frame;
        entry->gc = XCreateGC(display_, frame, 0u, nullptr);
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

    void drawFrame(Client &entry, bool focused) {
        const unsigned long title = focused ? 0xff3c668cu : 0xff3b424cu;
        XSetForeground(display_, entry.gc, title);
        XFillRectangle(display_, entry.frame, entry.gc, 0, 0, 4096u,
                       static_cast<unsigned int>(title_height_));
        XSetForeground(display_, entry.gc, 0xfff4f6f8u);
        static const char title_text[] = "DemonWM Client";
        XDrawString(display_, entry.frame, entry.gc, 9, 17, title_text,
                    static_cast<int>(sizeof(title_text) - 1u));
        XSetForeground(display_, entry.gc, 0xffd95b62u);
        XFillRectangle(display_, entry.frame, entry.gc, 0, 6, 12u, 12u);
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

    void beginDrag(const XButtonEvent &event) {
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
        if (y < static_cast<int>(kPanelHeight)) y = static_cast<int>(kPanelHeight);
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
    manager.adoptExistingWindows();
    demon_write("DEMONWM_READY\n", 14u);
    const uint64_t status = manager.run();
    XCloseDisplay(display);
    return status;
}

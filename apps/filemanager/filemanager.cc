/*
 * DemonOS Files -- a minimal read-only RAMFS browser, the first cut of the
 * file manager described in the DemonOS desktop design. Runs as a normal
 * DemonWM client (no window-management logic of its own); the WM supplies
 * the title bar and controls.
 */
#include <X11/Xlib.h>
#include <demon/c_app.h>
#include <demon/dirent.h>
#include <stdint.h>

namespace {

constexpr unsigned int kWindowW = 320u;
constexpr unsigned int kWindowH = 260u;
constexpr int kRowHeight = 22;
constexpr int kHeaderHeight = 26;
constexpr unsigned int kMaxEntries = 16u;
constexpr unsigned int kMaxPath = 120u;

constexpr unsigned long kColorSurface = 0xff20242cu;
constexpr unsigned long kColorSurfaceAlt = 0xff262b33u;
constexpr unsigned long kColorEmber = 0xffff5c42u;
constexpr unsigned long kColorViolet = 0xff8b6bffu;
constexpr unsigned long kColorText = 0xfff1f3f5u;
constexpr unsigned long kColorTextMuted = 0xff9ba3afu;

int stringLength(const char *text) {
    int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

class FileManager {
public:
    explicit FileManager(Display *display)
        : display_(display), window_(None), gc_(nullptr), storage_(0u),
          path_length_(0u), entry_count_(0u), selected_(0u) {}

    bool open() {
        const Window root = XDefaultRootWindow(display_);
        window_ = XCreateSimpleWindow(display_, root, 160, 120, kWindowW,
            kWindowH, 0u, 0xff2f3540u, kColorSurface);
        if (window_ == None) return false;
        XSelectInput(display_, window_,
                     KeyPressMask | ButtonPressMask | ExposureMask);
        gc_ = XCreateGC(display_, window_, 0u, nullptr);
        storage_ = demon_service_open(4u); /* CAPABILITY_SERVICE_STORAGE */
        loadDirectory();
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
    struct Entry {
        char name[60];
        uint32_t length;
        bool is_directory;
    };

    void loadDirectory() {
        entry_count_ = 0u;
        selected_ = 0u;
        for (uint64_t index = 0u; entry_count_ < kMaxEntries; ++index) {
            struct demon_dir_entry raw;
            if (demon_dir_list(storage_, path_, path_length_, index, &raw) != 1u)
                break;
            Entry &slot = entries_[entry_count_++];
            const uint32_t copy_length = raw.name_length < sizeof(slot.name)
                ? raw.name_length : static_cast<uint32_t>(sizeof(slot.name) - 1u);
            for (uint32_t i = 0u; i < copy_length; ++i) slot.name[i] = raw.name[i];
            slot.name[copy_length] = '\0';
            slot.length = copy_length;
            slot.is_directory = raw.is_directory != 0u;
        }
    }

    void navigateInto(unsigned int index) {
        if (index >= entry_count_ || !entries_[index].is_directory) return;
        const uint32_t add_length = entries_[index].length;
        uint32_t new_length = path_length_;
        if (path_length_ != 0u && new_length + 1u < kMaxPath)
            path_[new_length++] = '/';
        for (uint32_t i = 0u; i < add_length && new_length < kMaxPath - 1u; ++i)
            path_[new_length++] = entries_[index].name[i];
        path_length_ = new_length;
        loadDirectory();
        draw();
    }

    void navigateUp() {
        if (path_length_ == 0u) return;
        uint32_t cut = path_length_;
        while (cut > 0u && path_[cut - 1u] != '/') --cut;
        path_length_ = cut > 0u ? cut - 1u : 0u;
        loadDirectory();
        draw();
    }

    void draw() {
        if (window_ == None || gc_ == nullptr) return;
        XSetForeground(display_, gc_, kColorSurface);
        XFillRectangle(display_, window_, gc_, 0, 0, kWindowW, kWindowH);

        /* Header: current path, click to go up a level. */
        XSetForeground(display_, gc_, kColorSurfaceAlt);
        XFillRectangle(display_, window_, gc_, 0, 0, kWindowW,
                       static_cast<unsigned int>(kHeaderHeight));
        XSetForeground(display_, gc_, path_length_ == 0u ? kColorTextMuted
                                                          : kColorEmber);
        static const char up_label[] = "< Up";
        if (path_length_ != 0u)
            XDrawString(display_, window_, gc_, 6, 18, up_label,
                        static_cast<int>(sizeof(up_label) - 1u));
        XSetForeground(display_, gc_, kColorText);
        if (path_length_ == 0u) {
            static const char root_label[] = "/";
            XDrawString(display_, window_, gc_, 56, 18, root_label, 1);
        } else {
            XDrawString(display_, window_, gc_, 56, 18, path_,
                        static_cast<int>(path_length_));
        }

        for (unsigned int index = 0u; index < entry_count_; ++index) {
            const int row_y = kHeaderHeight + static_cast<int>(index) * kRowHeight;
            if (index == selected_) {
                XSetForeground(display_, gc_, kColorSurfaceAlt);
                XFillRectangle(display_, window_, gc_, 2, row_y,
                               kWindowW - 4u, static_cast<unsigned int>(kRowHeight));
                XSetForeground(display_, gc_, kColorEmber);
                XDrawRectangle(display_, window_, gc_, 2, row_y, kWindowW - 4u,
                              static_cast<unsigned int>(kRowHeight) - 1u);
            }
            XSetForeground(display_, gc_, entries_[index].is_directory
                                              ? kColorViolet : kColorTextMuted);
            XFillRectangle(display_, window_, gc_, 8, row_y + 5, 12u, 12u);
            XSetForeground(display_, gc_, kColorText);
            XDrawString(display_, window_, gc_, 28, row_y + 15,
                        entries_[index].name,
                        static_cast<int>(stringLength(entries_[index].name)));
        }
    }

    int hitTestRow(int y) const {
        if (y < kHeaderHeight) return -1;
        const int row = (y - kHeaderHeight) / kRowHeight;
        if (row < 0 || static_cast<unsigned int>(row) >= entry_count_) return -1;
        return row;
    }

    void handleClick(int x, int y) {
        if (y < kHeaderHeight) {
            if (x < 56 && path_length_ != 0u) navigateUp();
            return;
        }
        const int row = hitTestRow(y);
        if (row < 0) return;
        selected_ = static_cast<unsigned int>(row);
        navigateInto(selected_);
    }

    void handleKey(unsigned int keycode) {
        switch (keycode) {
        case 72u: /* Up */
            if (selected_ > 0u) --selected_;
            draw();
            break;
        case 80u: /* Down */
            if (selected_ + 1u < entry_count_) ++selected_;
            draw();
            break;
        case 28u: /* Enter */
            navigateInto(selected_);
            break;
        case 14u: /* Backspace */
            navigateUp();
            break;
        case 1u: /* Escape */
            demon_exit(0u);
            break;
        default:
            break;
        }
    }

    Display *display_;
    Window window_;
    GC gc_;
    uint64_t storage_;
    char path_[kMaxPath];
    uint32_t path_length_;
    Entry entries_[kMaxEntries];
    unsigned int entry_count_;
    unsigned int selected_;
};

} // namespace

extern "C" uint64_t filemanager_main() {
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) return 220u;
    FileManager manager(display);
    if (!manager.open()) {
        XCloseDisplay(display);
        return 221u;
    }
    demon_write("FILEMANAGER_READY\n", 19u);
    const uint64_t status = manager.run();
    XCloseDisplay(display);
    return status;
}

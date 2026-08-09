#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use demon_abi::{
    CapabilityService, DisplayInfo, DisplaySubmit, InputEvent, WindowMessage, WindowOpcode,
    DEMONX_DISPLAY_CHANNEL, DEMONX_WINDOW_ID_BASE, DISPLAY_SUBMIT_PRESENT,
    INPUT_KEY_DOWN, INPUT_KEY_UP, INPUT_MOUSE_BUTTON_DOWN, INPUT_MOUSE_BUTTON_UP,
    INPUT_MOUSE_MOVE, UINT64_MAX, WINDOW_PROTOCOL_VERSION, WINDOW_SERVICE,
};

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    demon_abi::write(b"RUST_COMPOSITOR_PANIC\n");
    demon_abi::exit(1);
}

// Native Rust compositor and desktop shell. It owns real window bookkeeping,
// zero-copy surface compositing, input delivery to DemonX, window chrome,
// workspaces, launcher, and taskbar. Keeping those responsibilities together
// avoids a second decorative shell process and makes the default desktop both
// smaller and easier to reason about while the userspace stack matures.

const SCREEN_WIDTH: u32 = 640;
const SCREEN_HEIGHT: u32 = 480;
const WINDOW_LIMIT: usize = 8;
// A real hardware-keystroke verification (see the temporary debug_u32 calls
// below) caught this at 32: DemonWM's real panel window is a genuine 628x28
// CREATE (Desktop/demonwm/demonwm.cc's createPanel, kPanelHeight = 28), not
// an untracked native strip -- the old MKO compositor's 32px floor (and its
// matching "until DemonWM's retained panel surface is visible" input-proxy
// hack, removed below) predated the panel becoming a real window and were
// never updated once it did. The only real invariant this compositor needs
// is a nonzero size, so composite()'s per-row slice writes never operate on
// an empty/degenerate window.
const MIN_CREATE_SIZE: u32 = 1;
const MIN_MOVE_WIDTH: u32 = 160;
const MIN_MOVE_HEIGHT: u32 = 120;
// #FF101820 / placeholder fill, transcribed as the exact decimal literals
// user/compositor.mko's render_demonwm_backend used for the same two colors.
const BACKGROUND_COLOR: u32 = 4_279_245_080;
const PLACEHOLDER_COLOR: u32 = 4_280_032_545;

// Decoration geometry/colors, ported from Desktop/demonwm/demonwm.cc's own
// constants (kColor*, title_height_, kControlSize/kControlGap/kResizeGrip,
// kMinWindowWidth/Height) -- this compositor now draws that same chrome
// itself instead of DemonWM reparenting a second frame window around each
// client. Decoration is drawn as a strip ABOVE the window's own (x, y),
// extending its on-screen footprint upward rather than shrinking the
// client's content area, so no client needs to know decoration exists (no
// reparenting, no coordinate renegotiation).
const TITLE_HEIGHT: u32 = 24;
const CONTROL_SIZE: u32 = 16;
const CONTROL_GAP: u32 = 4;
const RESIZE_GRIP: u32 = 10;
const MIN_WINDOW_WIDTH: u32 = 96;
const MIN_WINDOW_HEIGHT: u32 = 64;
const RESERVED_TOP: u32 = 34; // kMargin(6) + kPanelHeight(28), matching demonwm's own panel band
// DemonOS chrome deliberately stays compact and quiet: graphite/navy
// surfaces, one cyan focus accent, and high-contrast text. This keeps the
// desktop legible without spending fill bandwidth on large ornamental
// gradients or making every control compete for attention.
const COLOR_TITLE_FOCUSED: u32 = 0xff263a4a;
const COLOR_TITLE_UNFOCUSED: u32 = 0xff1b2732;
const COLOR_TEXT: u32 = 0xffedf4f7;
const COLOR_TEXT_MUTED: u32 = 0xff91a5b2;
const COLOR_ACCENT: u32 = 0xff35b8e6;
const COLOR_ACCENT_DARK: u32 = 0xff176b8b;
const COLOR_PANEL_BG: u32 = 0xff101a23;
const COLOR_TASKBAR_ITEM: u32 = 0xff1c2a35;
const COLOR_SURFACE_HOVER: u32 = 0xff2a3b48;
const COLOR_BORDER: u32 = 0xff456174;
// Native symbol controls. Close remains red because it is destructive;
// minimize and maximize use the same neutral ink as the rest of the title.
const COLOR_CONTROL_CLOSE: u32 = 0xffff5f57;
const COLOR_CONTROL_MAXIMIZE: u32 = 0xffa9bbc5;
const CORNER_RADIUS: i32 = 6;
const SNAP_DISTANCE: u32 = 12;
const CURSOR_ARROW: u64 = 0;
const CURSOR_HAND: u64 = 1;
const CURSOR_RESIZE_SE: u64 = 10;
// Chrome surfaces (panel, launcher) blend into the wallpaper instead of a
// flat opaque fill -- a soft frosted-glass read instead of a solid card
// stacked on top of the desktop.
const PANEL_BLEND_ALPHA: u8 = 244;

// Panel geometry, ported from demonwm's kMargin/kPanelHeight/
// kLauncherBtnX0/X1 -- drawn as a pure compositor overlay (never a window
// table entry, unlike DemonWM's real 628x28 CREATE'd panel window) since
// it has no client of its own to composite from.
const PANEL_MARGIN: i32 = 6;
const PANEL_HEIGHT: u32 = 28;
const LAUNCHER_BTN_X0: i32 = PANEL_MARGIN + 4;
const LAUNCHER_BTN_X1: i32 = LAUNCHER_BTN_X0 + 72;
const QUICK_LAUNCH_X: i32 = LAUNCHER_BTN_X1 + 8;
const QUICK_LAUNCH_SIZE: i32 = 24;
const QUICK_LAUNCH_GAP: i32 = 4;
const WORKSPACE_COUNT: u32 = 3;

// A real taskbar along the bottom edge, mirroring the top panel's own
// margin/height convention. Shows one entry per open window on the
// current workspace -- clicking one focuses and raises it, the same
// action a real click on the window itself would trigger.
const BOTTOM_BAR_HEIGHT: u32 = 40;
const BOTTOM_BAR_MARGIN: i32 = 6;
const BOTTOM_BAR_Y: i32 = SCREEN_HEIGHT as i32 - BOTTOM_BAR_MARGIN - BOTTOM_BAR_HEIGHT as i32;
// How much vertical space windows must leave clear at the bottom of the
// screen so dragging/resizing never lets them slide under the taskbar --
// mirrors RESERVED_TOP's own role for the top panel.
const RESERVED_BOTTOM: u32 = (BOTTOM_BAR_MARGIN as u32) * 2 + BOTTOM_BAR_HEIGHT;
const TASKBAR_ITEM_WIDTH: u32 = 120;
const TASKBAR_ITEM_GAP: i32 = 8;
const INPUT_MOD_ALT: u32 = 1 << 2;
const KEY_ESCAPE: u16 = 0x01;
const KEY_WORKSPACE_1: u16 = 0x02;
const KEY_WORKSPACE_3: u16 = 0x04;
const KEY_TAB: u16 = 0x0f;
const KEY_ENTER: u16 = 0x1c;
const KEY_SPACE: u16 = 0x39;
const KEY_F4: u16 = 0x3e;
const KEY_F9: u16 = 0x43;
const KEY_F10: u16 = 0x44;
// Extended Set-1 keys retain the E0 prefix in bit 8 (include/demon/input.h),
// keeping the cursor cluster distinct from the numeric keypad.
const KEY_UP: u16 = 0x148;
const KEY_DOWN: u16 = 0x150;

// The launcher popover, opened by clicking the panel's DEMON button.
// Three entries:
//
// - Terminal: restores and raises the session's existing xterm. This avoids
//   the known duplicate-spawn IPC issue while still making the launcher entry
//   useful instead of decorative.
// - ClassiCube and Doom: ordinary asynchronous DemonX clients. Each owns a
//   retained surface which DemonX shares read-only with this compositor, so
//   neither game takes over DISPLAY or blocks the desktop event loop.
const LAUNCHER_X: i32 = PANEL_MARGIN;
const LAUNCHER_Y: i32 = PANEL_MARGIN + PANEL_HEIGHT as i32 + 8;
const LAUNCHER_WIDTH: u32 = 196;
const LAUNCHER_CELL_HEIGHT: i32 = 42;
const LAUNCHER_CELL_GAP: i32 = 6;
const LAUNCHER_CELL_COUNT: i32 = 4;
const LAUNCHER_HEADER_HEIGHT: i32 = 30;
const LAUNCHER_HEIGHT: u32 = (LAUNCHER_HEADER_HEIGHT + LAUNCHER_CELL_HEIGHT * LAUNCHER_CELL_COUNT
    + LAUNCHER_CELL_GAP * (LAUNCHER_CELL_COUNT + 1)) as u32;
// CONSOLE | PROCESS | STORAGE | IPC | SURFACE. Windowed games talk to input
// and display through DemonX; they intentionally receive neither exclusive
// DISPLAY nor raw INPUT ownership.
const CLASSICUBE_SERVICE_MASK: u64 =
    (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 9);
// Same base plus AUDIO for the real sustained-play Freedoom build.
const DOOM_SERVICE_MASK: u64 = CLASSICUBE_SERVICE_MASK | (1 << 11);
const SHELL_ICON_WIDTH: usize = 22;
const SHELL_ICON_HEIGHT: usize = 22;
const SHELL_ICON_COUNT: usize = 9;
const SHELL_ICON_TERMINAL: usize = 0;
const SHELL_ICON_APPLICATION: usize = 3;
const SHELL_ICON_MINECRAFT: usize = 6;
const SHELL_ICON_DOOM: usize = 7;
const SHELL_ICON_QUAKE: usize = 8;

#[derive(Clone, Copy)]
struct Window {
    in_use: bool,
    id: u32,
    z: u32,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    surface_id: u32,
    mapped_address: u64,
    // The mapped surface's real pixel dimensions, queried from its handle
    // at Create time and never changed by a logical resize. composite()
    // scales exactly this bounded source into the window's content rect,
    // while client_point() applies the inverse transform to mouse input.
    surface_width: u32,
    surface_height: u32,
    maximized: bool,
    minimized: bool,
    restore_x: u32,
    restore_y: u32,
    restore_width: u32,
    restore_height: u32,
    // Which of the WORKSPACE_COUNT desktops this window belongs to --
    // assigned at Create time from whatever the current workspace is, and
    // never changed after (no "move window to workspace" gesture exists
    // yet). Windows outside the current workspace are skipped by both
    // composite() and every hit-test, so they're fully hidden and
    // unclickable rather than merely drawn behind everything else.
    workspace: u32,
    // The client's real title, carried one-way in Create's payload field
    // (see user/demonx_server.c's publish_window: DemonX reads whatever
    // XStoreName set as XA_WM_NAME before the window was mapped and copies
    // it in here) -- not kept in sync with any later rename, since nothing
    // here renames itself after mapping. title_length == 0 means no title
    // was set; callers fall back to a generic label in that case.
    title: [u8; 24],
    title_length: u8,
}

impl Window {
    const EMPTY: Window = Window {
        in_use: false,
        id: 0,
        z: 0,
        x: 0,
        y: 0,
        width: 0,
        height: 0,
        surface_id: 0,
        mapped_address: 0,
        surface_width: 0,
        surface_height: 0,
        maximized: false,
        minimized: false,
        restore_x: 0,
        restore_y: 0,
        restore_width: 0,
        restore_height: 0,
        workspace: 0,
        title: [0; 24],
        title_length: 0,
    };

    // The client's title, uppercased (the compact font is uppercase-only,
    // see glyph_rows) with an ASCII fallback for anything it can't draw,
    // or "WINDOW" if none was set.
    fn title_text(&self, out: &mut [u8; 24]) -> usize {
        if self.title_length == 0 {
            let fallback = b"WINDOW";
            out[..fallback.len()].copy_from_slice(fallback);
            return fallback.len();
        }
        let length = self.title_length as usize;
        for i in 0..length {
            out[i] = self.title[i].to_ascii_uppercase();
        }
        length
    }
}

// A window's true visual footprint including its title bar strip, which
// lives entirely above (x, y) and is never allowed to go above
// RESERVED_TOP (see decoration_hit_at/clamp_drag_y).
fn decoration_top(window: &Window) -> u32 {
    window.y.saturating_sub(TITLE_HEIGHT)
}

// Title controls are right aligned and represented by their center x offset
// from window.x. Keeping the geometry in one place ensures drawing and hit
// testing cannot drift apart.
fn close_control_x(window: &Window) -> i32 {
    window.width as i32 - 13
}

fn maximize_control_x(window: &Window) -> i32 {
    close_control_x(window) - CONTROL_GAP as i32 - CONTROL_SIZE as i32
}

fn minimize_control_x(window: &Window) -> i32 {
    maximize_control_x(window) - CONTROL_GAP as i32 - CONTROL_SIZE as i32
}

// 1 = maximize, 2 = close, 3 = minimize, 0 = ordinary title drag. The hit
// region is CONTROL_SIZE square even though each glyph is smaller.
fn title_control_at(window: &Window, local_x: u32) -> u32 {
    let half = CONTROL_SIZE as i32 / 2;
    let close_x = close_control_x(window);
    if (local_x as i32 - close_x).abs() <= half {
        return 2;
    }
    let max_x = maximize_control_x(window);
    if (local_x as i32 - max_x).abs() <= half {
        return 1;
    }
    let min_x = minimize_control_x(window);
    if (local_x as i32 - min_x).abs() <= half {
        return 3;
    }
    0
}

fn decoration_hit_at(table: &[Window; WINDOW_LIMIT], workspace: u32, x: u32, y: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| {
            w.in_use
                && !w.minimized
                && w.workspace == workspace
                && w.id >= DEMONX_WINDOW_ID_BASE
                && x >= w.x
                && x < w.x + w.width
                && y >= decoration_top(w)
                && y < w.y
        })
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
}

// Returns which workspace dot (0..WORKSPACE_COUNT) a panel click landed
// on, or None if the click was elsewhere in the panel (or outside it).
// Mirrors demonwm's own panelClick, extended with the workspace band it
// only ever drew, never made clickable.
fn workspace_dot_at(x: i32, y: i32) -> Option<u32> {
    if y < PANEL_MARGIN || y >= PANEL_MARGIN + PANEL_HEIGHT as i32 {
        return None;
    }
    let width = SCREEN_WIDTH as i32 - 2 * PANEL_MARGIN;
    let dots_x = PANEL_MARGIN + width / 2 - 30;
    for dot in 0..WORKSPACE_COUNT {
        let dot_x = dots_x + dot as i32 * 12;
        // The visible mark is intentionally small; the 10px hit target is
        // forgiving enough for real mouse use at 640x480.
        if x >= dot_x - 1 && x < dot_x + 9 {
            return Some(dot);
        }
    }
    None
}

// Same "walk the table in slot order, skip anything not real/on this
// workspace" filter as draw_taskbar -- kept identical between the two so
// an item's drawn position always matches where clicking it is detected.
fn taskbar_item_at(table: &[Window; WINDOW_LIMIT], workspace: u32, x: i32, y: i32) -> Option<u32> {
    if y < BOTTOM_BAR_Y || y >= BOTTOM_BAR_Y + BOTTOM_BAR_HEIGHT as i32 {
        return None;
    }
    let item_width = taskbar_item_width(table, workspace);
    let mut item_x = BOTTOM_BAR_MARGIN + 8;
    for window in table.iter() {
        if !window.in_use || window.workspace != workspace || window.id < DEMONX_WINDOW_ID_BASE {
            continue;
        }
        if x >= item_x && x < item_x + item_width as i32 {
            return Some(window.id);
        }
        item_x += item_width as i32 + TASKBAR_ITEM_GAP;
    }
    None
}

fn taskbar_item_width(table: &[Window; WINDOW_LIMIT], workspace: u32) -> u32 {
    let count = table
        .iter()
        .filter(|window| {
            window.in_use && window.workspace == workspace && window.id >= DEMONX_WINDOW_ID_BASE
        })
        .count() as u32;
    if count == 0 {
        return TASKBAR_ITEM_WIDTH;
    }
    let inner_width = SCREEN_WIDTH - (BOTTOM_BAR_MARGIN as u32 * 2) - 16;
    let gaps = TASKBAR_ITEM_GAP as u32 * count.saturating_sub(1);
    ((inner_width.saturating_sub(gaps)) / count)
        .min(TASKBAR_ITEM_WIDTH)
        .max(64)
}

fn hits_resize_corner(window: &Window, x: u32, y: u32) -> bool {
    x + RESIZE_GRIP >= window.x + window.width && y + RESIZE_GRIP >= window.y + window.height
}

fn in_launcher_button(x: i32, y: i32) -> bool {
    x >= LAUNCHER_BTN_X0 && x < LAUNCHER_BTN_X1 && y >= PANEL_MARGIN && y < PANEL_MARGIN + PANEL_HEIGHT as i32
}

fn quick_launch_at(x: i32, y: i32) -> Option<i32> {
    if y < PANEL_MARGIN + 4 || y >= PANEL_MARGIN + PANEL_HEIGHT as i32 - 4 {
        return None;
    }
    for index in 0..LAUNCHER_CELL_COUNT {
        let left = QUICK_LAUNCH_X + index * (QUICK_LAUNCH_SIZE + QUICK_LAUNCH_GAP);
        if x >= left && x < left + QUICK_LAUNCH_SIZE {
            return Some(index);
        }
    }
    None
}

fn launcher_cell_y(index: i32) -> i32 {
    LAUNCHER_Y + LAUNCHER_HEADER_HEIGHT + LAUNCHER_CELL_GAP
        + index * (LAUNCHER_CELL_HEIGHT + LAUNCHER_CELL_GAP)
}

// Returns which cell (0 = Terminal, 1 = ClassiCube, 2 = Doom, 3 = Quake) a launcher
// click landed on, or None if it missed all entries (still inside the popover, or
// outside it entirely -- either way, not an app entry).
fn launcher_cell_at(x: i32, y: i32) -> Option<i32> {
    if x < LAUNCHER_X || x >= LAUNCHER_X + LAUNCHER_WIDTH as i32 {
        return None;
    }
    for index in 0..LAUNCHER_CELL_COUNT {
        let cell_y = launcher_cell_y(index);
        if y >= cell_y && y < cell_y + LAUNCHER_CELL_HEIGHT {
            return Some(index);
        }
    }
    None
}

// Familiar desktop edge tiling without a layout daemon. The target is
// derived only while a title drag is active, so merely parking the pointer
// at an edge never changes a window.
fn snap_target(x: u32, y: u32) -> u32 {
    if y <= RESERVED_TOP + SNAP_DISTANCE {
        1 // maximize
    } else if x <= SNAP_DISTANCE {
        2 // left half
    } else if x + SNAP_DISTANCE >= SCREEN_WIDTH {
        3 // right half
    } else {
        0
    }
}

fn snap_geometry(target: u32) -> (u32, u32, u32, u32) {
    let y = RESERVED_TOP + TITLE_HEIGHT;
    let height = SCREEN_HEIGHT - RESERVED_BOTTOM - y;
    match target {
        2 => (0, y, SCREEN_WIDTH / 2, height),
        3 => (SCREEN_WIDTH / 2, y, SCREEN_WIDTH - SCREEN_WIDTH / 2, height),
        _ => (0, y, SCREEN_WIDTH, height),
    }
}

fn cursor_icon_at(
    table: &[Window; WINDOW_LIMIT],
    workspace: u32,
    x: u32,
    y: u32,
    dragging_id: u32,
    resizing_id: u32,
    launcher_open: bool,
) -> u64 {
    if resizing_id != 0 {
        return CURSOR_RESIZE_SE;
    }
    if dragging_id != 0 {
        return CURSOR_HAND;
    }
    if in_launcher_button(x as i32, y as i32)
        || quick_launch_at(x as i32, y as i32).is_some()
        || workspace_dot_at(x as i32, y as i32).is_some()
        || taskbar_item_at(table, workspace, x as i32, y as i32).is_some()
        || (launcher_open && launcher_cell_at(x as i32, y as i32).is_some())
        || decoration_hit_at(table, workspace, x, y).is_some()
    {
        return CURSOR_HAND;
    }
    if let Some(slot) = top_slot_at(table, workspace, x, y) {
        if table[slot].id >= DEMONX_WINDOW_ID_BASE
            && hits_resize_corner(&table[slot], x, y)
        {
            return CURSOR_RESIZE_SE;
        }
    }
    CURSOR_ARROW
}

fn draw_launcher_cell(
    frame: &mut [u32],
    index: i32,
    label: &[u8],
    hovered: bool,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    let cell_y = launcher_cell_y(index);
    if hovered {
        fill_rounded_rect(frame, LAUNCHER_X + 6, cell_y, LAUNCHER_WIDTH - 12,
                          LAUNCHER_CELL_HEIGHT as u32, 4, COLOR_SURFACE_HOVER);
    }
    let icon_x = LAUNCHER_X + 15;
    let icon_y = cell_y + 10;
    let icon = if index == 0 {
        SHELL_ICON_TERMINAL
    } else if index == 1 {
        SHELL_ICON_MINECRAFT
    } else if index == 2 {
        SHELL_ICON_DOOM
    } else {
        SHELL_ICON_QUAKE
    };
    draw_shell_icon(frame, shell_icons, shell_icons_loaded, icon, icon_x, icon_y, 22);
    draw_text(frame, LAUNCHER_X + 48, cell_y + 18, label, COLOR_TEXT);
}

fn draw_launcher(
    frame: &mut [u32],
    selection: i32,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    draw_shadow(frame, LAUNCHER_X, LAUNCHER_Y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
    blend_rounded_rect(frame, LAUNCHER_X, LAUNCHER_Y, LAUNCHER_WIDTH,
                       LAUNCHER_HEIGHT, CORNER_RADIUS, COLOR_PANEL_BG, PANEL_BLEND_ALPHA);
    draw_text(frame, LAUNCHER_X + 12, LAUNCHER_Y + 11, b"APPLICATIONS", COLOR_TEXT_MUTED);
    fill_rect(frame, LAUNCHER_X + 8, LAUNCHER_Y + LAUNCHER_HEADER_HEIGHT - 1,
              LAUNCHER_WIDTH - 16, 1, COLOR_BORDER);
    // Mouse motion updates `selection` in the event loop, so drawing from one
    // shared value lets the last input method win. In particular, a pointer
    // left resting over Terminal must not mask a later Down-arrow selection.
    draw_launcher_cell(frame, 0, b"TERMINAL", selection == 0, shell_icons, shell_icons_loaded);
    draw_launcher_cell(frame, 1, b"CLASSICUBE", selection == 1, shell_icons, shell_icons_loaded);
    draw_launcher_cell(frame, 2, b"DOOM", selection == 2, shell_icons, shell_icons_loaded);
    draw_launcher_cell(frame, 3, b"QUAKE", selection == 3, shell_icons, shell_icons_loaded);
}

fn find_free_slot(table: &[Window; WINDOW_LIMIT]) -> Option<usize> {
    table.iter().position(|w| !w.in_use)
}

fn find_slot(table: &[Window; WINDOW_LIMIT], id: u32) -> Option<usize> {
    table.iter().position(|w| w.in_use && w.id == id)
}

fn find_top_slot(table: &[Window; WINDOW_LIMIT], workspace: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| w.in_use && !w.minimized && w.workspace == workspace)
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
}

fn toggle_maximize(window: &mut Window) {
    window.minimized = false;
    if window.maximized {
        window.x = window.restore_x;
        window.y = window.restore_y;
        window.width = window.restore_width;
        window.height = window.restore_height;
        window.maximized = false;
    } else {
        window.restore_x = window.x;
        window.restore_y = window.y;
        window.restore_width = window.width;
        window.restore_height = window.height;
        let (x, y, width, height) = snap_geometry(1);
        window.x = x;
        window.y = y;
        window.width = width;
        window.height = height;
        window.maximized = true;
    }
}

// User-requested closes notify the owning DemonX client before releasing the
// compositor's mapped surface. This is shared by the title button and
// Alt+F4, so neither path can leave a headless game process behind.
fn close_window(
    table: &mut [Window; WINDOW_LIMIT],
    slot: usize,
    workspace: u32,
) -> u32 {
    let window = table[slot];
    if window.id >= DEMONX_WINDOW_ID_BASE {
        send_window_event(window.id, WindowOpcode::Close, 9, 0, 0, 0, 0);
    }
    if window.surface_id != 0 {
        demon_abi::surface_unmap(window.surface_id as u64);
        demon_abi::handle_close(window.surface_id as u64);
    }
    table[slot] = Window::EMPTY;
    find_top_slot(table, workspace).map_or(0, |top| table[top].id)
}

fn shell_icon_for_window(window: &Window) -> usize {
    let mut title = [0u8; 24];
    let length = window.title_text(&mut title);
    if &title[..length] == b"XTERM" {
        SHELL_ICON_TERMINAL
    } else if &title[..length] == b"CLASSICUBE" {
        SHELL_ICON_MINECRAFT
    } else if &title[..length] == b"FREEDOOM" {
        SHELL_ICON_DOOM
    } else if &title[..length] == b"QUAKE" {
        SHELL_ICON_QUAKE
    } else {
        SHELL_ICON_APPLICATION
    }
}

fn find_window_by_title(table: &[Window; WINDOW_LIMIT], wanted: &[u8]) -> Option<usize> {
    table.iter().position(|window| {
        if !window.in_use || window.id < DEMONX_WINDOW_ID_BASE {
            return false;
        }
        let mut title = [0u8; 24];
        let length = window.title_text(&mut title);
        length == wanted.len() && &title[..length] == wanted
    })
}

// Select the most recently used window other than the focused one. Raising
// the result after every cycle naturally maintains MRU order without another
// allocation or list. Minimized windows participate and are restored when
// selected, matching familiar desktop Alt+Tab behavior.
fn find_cycle_slot(
    table: &[Window; WINDOW_LIMIT],
    workspace: u32,
    focused_window: u32,
) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, window)| {
            window.in_use
                && window.workspace == workspace
                && window.id >= DEMONX_WINDOW_ID_BASE
                && window.id != focused_window
        })
        .max_by_key(|(_, window)| window.z)
        .map(|(index, _)| index)
        .or_else(|| find_slot(table, focused_window))
}

fn activate_launcher_cell(
    cell: i32,
    table: &mut [Window; WINDOW_LIMIT],
    current_workspace: &mut u32,
    focused_window: &mut u32,
    next_z: &mut u32,
) {
    if cell == 0 {
        // The session starts one xterm already. Reuse it instead of
        // triggering the known duplicate-spawn IPC bug.
        if let Some(slot) = find_window_by_title(table, b"XTERM") {
            *current_workspace = table[slot].workspace;
            table[slot].minimized = false;
            table[slot].z = *next_z;
            *next_z += 1;
            *focused_window = table[slot].id;
        }
    } else if cell == 1 {
        if let Some(slot) = find_window_by_title(table, b"ClassiCube") {
            *current_workspace = table[slot].workspace;
            table[slot].minimized = false;
            table[slot].z = *next_z;
            *next_z += 1;
            *focused_window = table[slot].id;
        } else {
            let pid = demon_abi::spawn(
                b"/system/bin/classicube-core.elf",
                CLASSICUBE_SERVICE_MASK,
            );
            if pid == u64::MAX {
                demon_abi::write(b"CLASSICUBE_WINDOW_SPAWN_FAILED\n");
            } else {
                demon_abi::write(b"CLASSICUBE_WINDOW_SPAWNED\n");
            }
        }
    } else if cell == 2 {
        if let Some(slot) = find_window_by_title(table, b"Freedoom") {
            *current_workspace = table[slot].workspace;
            table[slot].minimized = false;
            table[slot].z = *next_z;
            *next_z += 1;
            *focused_window = table[slot].id;
        } else {
            let pid = demon_abi::spawn(b"/system/bin/doom-full.elf", DOOM_SERVICE_MASK);
            if pid == u64::MAX {
                demon_abi::write(b"FREEDOOM_WINDOW_SPAWN_FAILED\n");
            } else {
                demon_abi::write(b"FREEDOOM_WINDOW_SPAWNED\n");
            }
        }
    } else if cell == 3 {
        if let Some(slot) = find_window_by_title(table, b"Quake") {
            *current_workspace = table[slot].workspace;
            table[slot].minimized = false;
            table[slot].z = *next_z;
            *next_z += 1;
            *focused_window = table[slot].id;
        } else {
            let pid = demon_abi::spawn(
                b"/system/bin/quake-core.elf",
                CLASSICUBE_SERVICE_MASK,
            );
            if pid == u64::MAX {
                demon_abi::write(b"QUAKE_WINDOW_SPAWN_FAILED\n");
            } else {
                demon_abi::write(b"QUAKE_WINDOW_SPAWNED\n");
            }
        }
    }
}

fn top_slot_at(table: &[Window; WINDOW_LIMIT], workspace: u32, x: u32, y: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| {
            w.in_use
                && !w.minimized
                && w.workspace == workspace
                && x >= w.x
                && x < w.x + w.width
                && y >= w.y
                && y < w.y + w.height
        })
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
}

// Fit a client's fixed-size retained surface into its current logical
// window while preserving the source aspect ratio. The allocation never
// grows when the user resizes or maximizes a window, keeping the bounded
// surface arena cheap enough for xterm, Doom and ClassiCube to coexist.
fn content_rect(window: &Window) -> (u32, u32, u32, u32) {
    if window.surface_width == 0 || window.surface_height == 0 ||
       window.width == 0 || window.height == 0 {
        return (window.x, window.y, window.width, window.height);
    }
    let mut width = window.width;
    let mut height = ((width as u64 * window.surface_height as u64) /
                      window.surface_width as u64) as u32;
    if height > window.height {
        height = window.height;
        width = ((height as u64 * window.surface_width as u64) /
                 window.surface_height as u64) as u32;
    }
    width = width.max(1);
    height = height.max(1);
    (
        window.x + (window.width - width) / 2,
        window.y + (window.height - height) / 2,
        width,
        height,
    )
}

// Rendering and input share the exact same transform. A click therefore
// continues to land on the same client pixel after resizing instead of
// drifting with the enlarged desktop coordinates.
fn client_point(window: &Window, x: u32, y: u32) -> Option<(i32, i32)> {
    let (content_x, content_y, content_width, content_height) = content_rect(window);
    if x < content_x || y < content_y ||
       x >= content_x + content_width || y >= content_y + content_height {
        return None;
    }
    let source_x = ((x - content_x) as u64 * window.surface_width as u64 /
                    content_width as u64) as i32;
    let source_y = ((y - content_y) as u64 * window.surface_height as u64 /
                    content_height as u64) as i32;
    Some((source_x, source_y))
}

// Connect to a window's native event channel and deliver a 64-byte
// demon_window_message. Every real client that currently exists sits at or
// above DEMONX_WINDOW_ID_BASE (DemonX is the sole native client; every X11
// window is one of its XIDs), so that fixed channel is the only path
// actually exercised today. The digit-suffix "desktop.winN" scheme is kept
// for wire-format parity with user/compositor.mko's write_event_channel_name
// for any future direct (non-X11) native client below that id range.
// Failing to connect or send is silently dropped, matching the original's
// tolerance for a slow/unready/exited client -- one missed input event must
// never take the whole desktop down.
fn send_window_event(
    target_id: u32,
    opcode: WindowOpcode,
    serial: u32,
    x: i32,
    y: i32,
    width: u32,
    height: u32,
) {
    let handle = if target_id >= DEMONX_WINDOW_ID_BASE {
        demon_abi::channel_connect(DEMONX_DISPLAY_CHANNEL)
    } else {
        let mut name = *b"desktop.win0";
        name[11] = b'0' + (target_id % 10) as u8;
        demon_abi::channel_connect(&name)
    };
    if handle == UINT64_MAX {
        return;
    }
    let message = WindowMessage {
        version: WINDOW_PROTOCOL_VERSION,
        opcode: opcode as u16,
        serial,
        window_id: target_id,
        flags: 0,
        x,
        y,
        width,
        height,
        surface_id: 0,
        payload_length: 0,
        payload: [0; 24],
    };
    let bytes = unsafe {
        core::slice::from_raw_parts(
            &message as *const WindowMessage as *const u8,
            core::mem::size_of::<WindowMessage>(),
        )
    };
    demon_abi::channel_send(handle, bytes);
    demon_abi::handle_close(handle);
}

// A real 4x5 pixel bitmap font, not a placeholder -- same design as the
// kernel's own framebuffer_text_compact (see src/framebuffer.c), ported
// here as a stopgap so decoration/panel text renders now. This is a
// deliberate, temporary stand-in for the Pixel12x10 TTF: it draws real,
// readable glyphs today rather than leaving text blank while the TTF
// parser/rasterizer (a separate, much larger piece of work) gets built.
// Each row is a 4-bit value, bit 3 = leftmost column.
const FONT_CHARS: &[u8] = b" -.:/0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const FONT_ROWS: [[u8; 5]; 41] = [
    [0, 0, 0, 0, 0],       // space
    [0, 0, 15, 0, 0],      // -
    [0, 0, 0, 0, 4],       // .
    [0, 4, 0, 4, 0],       // :
    [1, 2, 4, 8, 0],       // /
    [6, 9, 9, 9, 6],       // 0
    [2, 6, 2, 2, 7],       // 1
    [6, 9, 2, 4, 15],      // 2
    [14, 1, 6, 1, 14],     // 3
    [9, 9, 15, 1, 1],      // 4
    [15, 8, 14, 1, 14],    // 5
    [6, 8, 14, 9, 6],      // 6
    [15, 1, 2, 4, 4],      // 7
    [6, 9, 6, 9, 6],       // 8
    [6, 9, 7, 1, 6],       // 9
    [6, 9, 15, 9, 9],      // A
    [14, 9, 14, 9, 14],    // B
    [7, 8, 8, 8, 7],       // C
    [14, 9, 9, 9, 14],     // D
    [15, 8, 14, 8, 15],    // E
    [15, 8, 14, 8, 8],     // F
    [7, 8, 11, 9, 7],      // G
    [9, 9, 15, 9, 9],      // H
    [15, 4, 4, 4, 15],     // I
    [3, 1, 1, 9, 6],       // J
    [9, 10, 12, 10, 9],    // K
    [8, 8, 8, 8, 15],      // L
    [9, 15, 15, 9, 9],     // M
    [9, 13, 11, 9, 9],     // N
    [6, 9, 9, 9, 6],       // O
    [14, 9, 14, 8, 8],     // P
    [6, 9, 9, 6, 1],       // Q
    [14, 9, 14, 10, 9],    // R
    [7, 8, 6, 1, 14],      // S
    [15, 4, 4, 4, 4],      // T
    [9, 9, 9, 9, 6],       // U
    [9, 9, 9, 6, 4],       // V
    [9, 9, 15, 15, 9],     // W
    [9, 6, 4, 6, 9],       // X
    [9, 9, 6, 4, 4],       // Y
    [15, 2, 4, 8, 15],     // Z
];

fn glyph_rows(character: u8) -> &'static [u8; 5] {
    let upper = if character.is_ascii_lowercase() {
        character - b'a' + b'A'
    } else {
        character
    };
    match FONT_CHARS.iter().position(|&c| c == upper) {
        Some(index) => &FONT_ROWS[index],
        None => &FONT_ROWS[0],
    }
}

fn put_pixel(frame: &mut [u32], x: i32, y: i32, color: u32) {
    if x < 0 || y < 0 || x >= SCREEN_WIDTH as i32 || y >= SCREEN_HEIGHT as i32 {
        return;
    }
    frame[y as usize * SCREEN_WIDTH as usize + x as usize] = color;
}

fn fill_rect(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32, color: u32) {
    for row in 0..height as i32 {
        for col in 0..width as i32 {
            put_pixel(frame, x + col, y + row, color);
        }
    }
}

fn stroke_rect(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32, color: u32) {
    if width == 0 || height == 0 { return; }
    fill_rect(frame, x, y, width, 1, color);
    fill_rect(frame, x, y + height as i32 - 1, width, 1, color);
    fill_rect(frame, x, y, 1, height, color);
    fill_rect(frame, x + width as i32 - 1, y, 1, height, color);
}

// Alpha-blends `color`'s RGB into whatever is already at (x, y), weighted
// by `alpha` (0..=255). Used for the drop shadow -- a hard-edged shadow
// rect would just look like a second, darker rectangle; blending against
// the real pixels underneath (wallpaper or another window) is what
// actually reads as elevation/depth rather than decoration.
fn blend_pixel(frame: &mut [u32], x: i32, y: i32, color: u32, alpha: u8) {
    if x < 0 || y < 0 || x >= SCREEN_WIDTH as i32 || y >= SCREEN_HEIGHT as i32 {
        return;
    }
    let index = y as usize * SCREEN_WIDTH as usize + x as usize;
    let dst = frame[index];
    let a = alpha as u32;
    let inv = 255 - a;
    let blend_channel = |shift: u32| -> u32 {
        let src_channel = (color >> shift) & 0xff;
        let dst_channel = (dst >> shift) & 0xff;
        ((src_channel * a + dst_channel * inv) / 255) & 0xff
    };
    frame[index] = 0xff000000
        | (blend_channel(16) << 16)
        | (blend_channel(8) << 8)
        | blend_channel(0);
}

// The atlas is generated from the repository's Fluent SVGs at build time.
// Scaling here is nearest-neighbour and allocation-free; the same 22px asset
// can therefore serve a compact title bar and a full-size launcher row.
fn draw_shell_icon(
    frame: &mut [u32],
    atlas: &[u32],
    loaded: bool,
    icon: usize,
    x: i32,
    y: i32,
    size: u32,
) {
    if !loaded || icon >= SHELL_ICON_COUNT || size == 0 {
        return;
    }
    let icon_offset = icon * SHELL_ICON_WIDTH * SHELL_ICON_HEIGHT;
    for destination_y in 0..size {
        let source_y = destination_y as usize * SHELL_ICON_HEIGHT / size as usize;
        for destination_x in 0..size {
            let source_x = destination_x as usize * SHELL_ICON_WIDTH / size as usize;
            let pixel = atlas[icon_offset + source_y * SHELL_ICON_WIDTH + source_x];
            let alpha = (pixel >> 24) as u8;
            if alpha == 255 {
                put_pixel(frame, x + destination_x as i32, y + destination_y as i32, pixel);
            } else if alpha != 0 {
                blend_pixel(
                    frame,
                    x + destination_x as i32,
                    y + destination_y as i32,
                    pixel,
                    alpha,
                );
            }
        }
    }
}

// A soft perimeter shadow. Only the narrow bands outside the surface are
// touched; the older implementation blended the entire covered rectangle
// even though the window immediately painted over it. This preserves the
// same depth cue with substantially less CPU and memory traffic.
const SHADOW_FEATHER: i32 = 8;
fn draw_shadow(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32) {
    let right = x + width as i32;
    let bottom = y + height as i32;
    for distance in 1..=SHADOW_FEATHER {
        let alpha = ((SHADOW_FEATHER - distance + 1) * 64 / SHADOW_FEATHER) as u8;
        let left_x = x - distance;
        let right_x = right - 1 + distance;
        for row in (y - distance)..(bottom + distance) {
            blend_pixel(frame, left_x, row, 0xff000000, alpha);
            blend_pixel(frame, right_x, row, 0xff000000, alpha);
        }
        let top_y = y - distance;
        let bottom_y = bottom - 1 + distance;
        for col in x..right {
            blend_pixel(frame, col, top_y, 0xff000000, alpha);
            blend_pixel(frame, col, bottom_y, 0xff000000, alpha);
        }
    }
}

// A filled rect with its four corners cut to a quarter-circle radius --
// every compositor-drawn chrome surface (panel, launcher, title bar) uses
// this instead of a hard-cornered fill_rect for a softer, more deliberate
// look than plain rectangles.
fn fill_rounded_rect(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32, radius: i32, color: u32) {
    let w = width as i32;
    let h = height as i32;
    for row in 0..h {
        for col in 0..w {
            let corner_x = if col < radius {
                radius - col
            } else if col >= w - radius {
                col - (w - radius - 1)
            } else {
                0
            };
            let corner_y = if row < radius {
                radius - row
            } else if row >= h - radius {
                row - (h - radius - 1)
            } else {
                0
            };
            if corner_x > 0 && corner_y > 0 && corner_x * corner_x + corner_y * corner_y > radius * radius {
                continue;
            }
            put_pixel(frame, x + col, y + row, color);
        }
    }
}

// Same corner mask as fill_rounded_rect, but only the TOP two corners --
// for the title bar strip, which sits directly above a window's square-
// cornered content with no gap, so only its own top edge should round.
fn fill_rounded_rect_top(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32, radius: i32, color: u32) {
    let w = width as i32;
    for row in 0..height as i32 {
        for col in 0..w {
            let corner_x = if col < radius {
                radius - col
            } else if col >= w - radius {
                col - (w - radius - 1)
            } else {
                0
            };
            let corner_y = if row < radius { radius - row } else { 0 };
            if corner_x > 0 && corner_y > 0 && corner_x * corner_x + corner_y * corner_y > radius * radius {
                continue;
            }
            put_pixel(frame, x + col, y + row, color);
        }
    }
}

// Same corner mask as fill_rounded_rect, blended instead of opaque -- the
// frosted-glass look for panel/launcher chrome (see PANEL_BLEND_ALPHA).
fn blend_rounded_rect(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32, radius: i32, color: u32, alpha: u8) {
    let w = width as i32;
    let h = height as i32;
    for row in 0..h {
        for col in 0..w {
            let corner_x = if col < radius {
                radius - col
            } else if col >= w - radius {
                col - (w - radius - 1)
            } else {
                0
            };
            let corner_y = if row < radius {
                radius - row
            } else if row >= h - radius {
                row - (h - radius - 1)
            } else {
                0
            };
            if corner_x > 0 && corner_y > 0 && corner_x * corner_x + corner_y * corner_y > radius * radius {
                continue;
            }
            blend_pixel(frame, x + col, y + row, color, alpha);
        }
    }
}

fn draw_text(frame: &mut [u32], x: i32, y: i32, text: &[u8], color: u32) {
    let mut cursor = x;
    for &character in text {
        let rows = glyph_rows(character);
        for (row_index, &row) in rows.iter().enumerate() {
            for col in 0..4 {
                if row & (1 << (3 - col)) != 0 {
                    put_pixel(frame, cursor + col as i32, y + row_index as i32, color);
                }
            }
        }
        cursor += 5;
    }
}

// Decoration strip for one window. Hover feedback is deliberately confined
// to the topmost title bar, making the controls feel responsive without an
// animation timer or another backing surface.
fn draw_decoration(
    frame: &mut [u32],
    window: &Window,
    focused: bool,
    hovered_control: u32,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    let top = decoration_top(window) as i32;
    let title_color = if focused {
        COLOR_TITLE_FOCUSED
    } else {
        COLOR_TITLE_UNFOCUSED
    };
    fill_rounded_rect_top(frame, window.x as i32, top, window.width, TITLE_HEIGHT, CORNER_RADIUS, title_color);
    if focused {
        fill_rect(frame, window.x as i32 + CORNER_RADIUS, top,
                  window.width.saturating_sub((CORNER_RADIUS * 2) as u32), 2, COLOR_ACCENT);
    }
    draw_shell_icon(
        frame,
        shell_icons,
        shell_icons_loaded,
        shell_icon_for_window(window),
        window.x as i32 + 7,
        top + 5,
        14,
    );
    let mut title_buffer = [0u8; 24];
    let title_length = window.title_text(&mut title_buffer)
        .min((window.width.saturating_sub(104) / 5) as usize);
    draw_text(frame, window.x as i32 + 27, top + 9,
              &title_buffer[..title_length], if focused { COLOR_TEXT } else { COLOR_TEXT_MUTED });

    let control_y = top + TITLE_HEIGHT as i32 / 2;
    let symbol = if focused { COLOR_CONTROL_MAXIMIZE } else { COLOR_TEXT_MUTED };
    let min_x = window.x as i32 + minimize_control_x(window);
    let max_x = window.x as i32 + maximize_control_x(window);
    let close_x = window.x as i32 + close_control_x(window);
    if hovered_control == 3 {
        fill_rounded_rect(frame, min_x - 8, control_y - 8, CONTROL_SIZE, CONTROL_SIZE,
                          4, COLOR_SURFACE_HOVER);
    } else if hovered_control == 1 {
        fill_rounded_rect(frame, max_x - 8, control_y - 8, CONTROL_SIZE, CONTROL_SIZE,
                          4, COLOR_SURFACE_HOVER);
    } else if hovered_control == 2 {
        fill_rounded_rect(frame, close_x - 8, control_y - 8, CONTROL_SIZE, CONTROL_SIZE,
                          4, 0xffb43d46);
    }
    fill_rect(frame, min_x - 4, control_y + 3, 8, 1, symbol);
    stroke_rect(frame, max_x - 4, control_y - 4, 8, 8, symbol);
    let close_color = if hovered_control == 2 {
        COLOR_TEXT
    } else if focused {
        COLOR_CONTROL_CLOSE
    } else {
        COLOR_TEXT_MUTED
    };
    for offset in -3..=3 {
        put_pixel(frame, close_x + offset, control_y + offset, close_color);
        put_pixel(frame, close_x + offset, control_y - offset, close_color);
    }
}

fn push_digit(buffer: &mut [u8; 5], index: &mut usize, value: u32) {
    buffer[*index] = b'0' + (value % 10) as u8;
    *index += 1;
}

// Top panel: launcher button, workspace marks, a real HH:MM clock (see
// syscall 48 / demon_abi::real_time_of_day -- CMOS/RTC time, not kernel
// uptime), and compact status glyphs. It is a compositor overlay with no
// window-table entry of its own.
fn draw_panel(
    frame: &mut [u32],
    current_workspace: u32,
    launcher_open: bool,
    cursor_x: u32,
    cursor_y: u32,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    let width = SCREEN_WIDTH as i32 - 2 * PANEL_MARGIN;
    draw_shadow(frame, PANEL_MARGIN, PANEL_MARGIN, width as u32, PANEL_HEIGHT);
    blend_rounded_rect(frame, PANEL_MARGIN, PANEL_MARGIN, width as u32, PANEL_HEIGHT, CORNER_RADIUS, COLOR_PANEL_BG, PANEL_BLEND_ALPHA);

    let launcher_hovered = in_launcher_button(cursor_x as i32, cursor_y as i32);
    let launcher_color = if launcher_open {
        COLOR_ACCENT_DARK
    } else if launcher_hovered {
        COLOR_SURFACE_HOVER
    } else {
        COLOR_TASKBAR_ITEM
    };
    fill_rounded_rect(
        frame,
        LAUNCHER_BTN_X0,
        PANEL_MARGIN + 4,
        (LAUNCHER_BTN_X1 - LAUNCHER_BTN_X0) as u32,
        PANEL_HEIGHT - 8,
        6,
        launcher_color,
    );
    draw_shell_icon(
        frame,
        shell_icons,
        shell_icons_loaded,
        SHELL_ICON_APPLICATION,
        LAUNCHER_BTN_X0 + 3,
        PANEL_MARGIN + 5,
        18,
    );
    draw_text(frame, LAUNCHER_BTN_X0 + 25, PANEL_MARGIN + 10, b"DEMON", COLOR_TEXT);

    // Always-visible launchers keep common apps one click away. They call
    // the same activation function as the menu, so an already-open app is
    // focused and raised instead of duplicated.
    for index in 0..LAUNCHER_CELL_COUNT {
        let x = QUICK_LAUNCH_X + index * (QUICK_LAUNCH_SIZE + QUICK_LAUNCH_GAP);
        if quick_launch_at(cursor_x as i32, cursor_y as i32) == Some(index) {
            fill_rounded_rect(frame, x, PANEL_MARGIN + 4,
                              QUICK_LAUNCH_SIZE as u32,
                              PANEL_HEIGHT - 8, 5, COLOR_SURFACE_HOVER);
        }
        let icon = if index == 0 {
            SHELL_ICON_TERMINAL
        } else if index == 1 {
            SHELL_ICON_MINECRAFT
        } else if index == 2 {
            SHELL_ICON_DOOM
        } else {
            SHELL_ICON_QUAKE
        };
        draw_shell_icon(frame, shell_icons, shell_icons_loaded, icon,
                        x + 3, PANEL_MARGIN + 7, 18);
    }

    // Workspace marks. The active desktop expands into a short cyan capsule,
    // which is easier to identify at a glance than three equal dots.
    let dots_x = PANEL_MARGIN + width / 2 - 30;
    for dot in 0..WORKSPACE_COUNT {
        let active = dot == current_workspace;
        let hovered = workspace_dot_at(cursor_x as i32, cursor_y as i32) == Some(dot);
        let mark_width = if active { 8 } else { 4 };
        fill_rounded_rect(frame, dots_x + dot as i32 * 12, PANEL_MARGIN + 13,
                          mark_width, 4, 2,
                          if active { COLOR_ACCENT } else if hovered { COLOR_TEXT } else { COLOR_TEXT_MUTED });
    }

    // Clock, centered: real wall-clock time from CMOS/RTC, not uptime.
    let packed = demon_abi::real_time_of_day();
    let hour = ((packed >> 8) & 0xff) as u32;
    let minute = (packed & 0xff) as u32;
    let mut clock_text = [0u8; 5];
    let mut index = 0usize;
    push_digit(&mut clock_text, &mut index, hour / 10);
    push_digit(&mut clock_text, &mut index, hour % 10);
    clock_text[index] = b':';
    index += 1;
    push_digit(&mut clock_text, &mut index, minute / 10);
    push_digit(&mut clock_text, &mut index, minute % 10);
    draw_text(frame, PANEL_MARGIN + width / 2 + 8, PANEL_MARGIN + 10, &clock_text, COLOR_TEXT);

    // Small, low-cost status glyphs: signal strength, audio and power. They
    // communicate purpose without pretending to expose unavailable data.
    let tray_right = PANEL_MARGIN + width - 12;
    for bar in 0..3 {
        fill_rect(frame, tray_right - 8 + bar * 4, PANEL_MARGIN + 18 - bar * 3,
                  2, (4 + bar * 3) as u32, COLOR_TEXT_MUTED);
    }
    fill_rect(frame, tray_right - 34, PANEL_MARGIN + 12, 4, 7, COLOR_TEXT_MUTED);
    fill_rect(frame, tray_right - 30, PANEL_MARGIN + 10, 2, 11, COLOR_TEXT_MUTED);
    stroke_rect(frame, tray_right - 58, PANEL_MARGIN + 10, 15, 9, COLOR_TEXT_MUTED);
    fill_rect(frame, tray_right - 42, PANEL_MARGIN + 13, 2, 3, COLOR_TEXT_MUTED);
}

// Bottom taskbar: one pill-shaped entry per open window on the current
// workspace, the focused one highlighted in the accent color. Real, not
// decorative -- see taskbar_item_at's matching hit-test, wired to the
// same focus+raise action clicking the window itself triggers.
fn draw_taskbar(
    frame: &mut [u32],
    table: &[Window; WINDOW_LIMIT],
    workspace: u32,
    focused_window: u32,
    cursor_x: u32,
    cursor_y: u32,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    let width = SCREEN_WIDTH as i32 - 2 * BOTTOM_BAR_MARGIN;
    draw_shadow(frame, BOTTOM_BAR_MARGIN, BOTTOM_BAR_Y, width as u32, BOTTOM_BAR_HEIGHT);
    blend_rounded_rect(frame, BOTTOM_BAR_MARGIN, BOTTOM_BAR_Y, width as u32, BOTTOM_BAR_HEIGHT, CORNER_RADIUS, COLOR_PANEL_BG, PANEL_BLEND_ALPHA);

    let item_width = taskbar_item_width(table, workspace);
    let hovered_window = taskbar_item_at(
        table,
        workspace,
        cursor_x as i32,
        cursor_y as i32,
    ).unwrap_or(0);
    let mut item_x = BOTTOM_BAR_MARGIN + 8;
    for window in table.iter() {
        if !window.in_use || window.workspace != workspace || window.id < DEMONX_WINDOW_ID_BASE {
            continue;
        }
        let focused = window.id == focused_window;
        let item_color = if focused {
            COLOR_TITLE_FOCUSED
        } else if window.id == hovered_window {
            COLOR_SURFACE_HOVER
        } else {
            COLOR_TASKBAR_ITEM
        };
        fill_rounded_rect(frame, item_x, BOTTOM_BAR_Y + 6, item_width,
                          BOTTOM_BAR_HEIGHT - 12, 6, item_color);
        draw_shell_icon(
            frame,
            shell_icons,
            shell_icons_loaded,
            shell_icon_for_window(window),
            item_x + 6,
            BOTTOM_BAR_Y + 9,
            22,
        );
        let mut title_buffer = [0u8; 24];
        let title_length = window.title_text(&mut title_buffer)
            .min((item_width.saturating_sub(36) / 5) as usize);
        draw_text(frame, item_x + 32, BOTTOM_BAR_Y + 16, &title_buffer[..title_length],
                  if window.minimized { COLOR_TEXT_MUTED } else { COLOR_TEXT });
        if focused && !window.minimized {
            fill_rect(frame, item_x + 8, BOTTOM_BAR_Y + BOTTOM_BAR_HEIGHT as i32 - 4,
                      item_width.saturating_sub(16), 2, COLOR_ACCENT);
        }
        item_x += item_width as i32 + TASKBAR_ITEM_GAP;
    }
}

// Clear to the desktop background, then paint every live window back to
// front by ascending z -- a real zero-copy blit from its mapped surface, or
// a flat placeholder if it has none mapped, exactly like
// render_demonwm_backend's own two draw paths. Bounds were already enforced
// when the window entered the table (CREATE/MOVE both reject anything that
// would not fit), so every row copy here stays inside `frame`.
// The real wallpaper, read from /system/wallpaper.argb (see
// grub-desktop.cfg's own module2 line) at its native 640x480 resolution --
// no upscaling needed, unlike the kernel's own quarter-resolution copy for
// its DISPLAY_EFFECT_WALLPAPER primitive (see src/display.c), since this
// file is shipped at full screen size specifically for this. Falls back
// to the flat BACKGROUND_COLOR fill if the file can't be read (e.g. a
// non-desktop boot with no such RAMFS entry) rather than leaving the
// frame uninitialized.
fn draw_wallpaper(frame: &mut [u32], wallpaper: &[u32], wallpaper_loaded: bool) {
    if wallpaper_loaded {
        frame.copy_from_slice(wallpaper);
    } else {
        frame.fill(BACKGROUND_COLOR);
    }
}

fn composite(
    table: &[Window; WINDOW_LIMIT],
    frame: &mut [u32],
    focused_window: u32,
    current_workspace: u32,
    launcher_open: bool,
    launcher_selection: i32,
    cursor_x: u32,
    cursor_y: u32,
    dragging_id: u32,
    wallpaper: &[u32],
    wallpaper_loaded: bool,
    shell_icons: &[u32],
    shell_icons_loaded: bool,
) {
    draw_wallpaper(frame, wallpaper, wallpaper_loaded);
    let hovered_decoration = decoration_hit_at(table, current_workspace, cursor_x, cursor_y)
        .map(|slot| table[slot].id)
        .unwrap_or(0);
    let mut order: [usize; WINDOW_LIMIT] = [0, 1, 2, 3, 4, 5, 6, 7];
    order.sort_unstable_by_key(|&index| table[index].z);
    for &index in order.iter() {
        let window = table[index];
        if !window.in_use || window.minimized || window.workspace != current_workspace {
            continue;
        }
        if window.id >= DEMONX_WINDOW_ID_BASE {
            draw_shadow(frame, window.x as i32, decoration_top(&window) as i32, window.width, window.height + TITLE_HEIGHT);
        }
        if window.mapped_address != 0 {
            let pixels = window.surface_width as usize * window.surface_height as usize;
            let source =
                unsafe { core::slice::from_raw_parts(window.mapped_address as *const u32, pixels) };
            let (content_x, content_y, content_width, content_height) = content_rect(&window);
            if content_width != window.width || content_height != window.height {
                fill_rect(frame, window.x as i32, window.y as i32,
                          window.width, window.height, 0xff05080bu32);
            }
            if content_width == window.surface_width &&
               content_height == window.surface_height {
                for row in 0..content_height as usize {
                    let src_start = row * window.surface_width as usize;
                    let dst_start = (content_y as usize + row) * SCREEN_WIDTH as usize +
                                    content_x as usize;
                    frame[dst_start..dst_start + content_width as usize]
                        .copy_from_slice(&source[src_start..src_start + content_width as usize]);
                }
            } else {
                // Allocation-free nearest-neighbour scaling keeps pixel-art
                // applications sharp and works without graphics hardware.
                for destination_y in 0..content_height {
                    let source_y = (destination_y as u64 *
                                    window.surface_height as u64 /
                                    content_height as u64) as usize;
                    let dst_start = (content_y + destination_y) as usize *
                                    SCREEN_WIDTH as usize + content_x as usize;
                    for destination_x in 0..content_width {
                        let source_x = (destination_x as u64 *
                                        window.surface_width as u64 /
                                        content_width as u64) as usize;
                        frame[dst_start + destination_x as usize] =
                            source[source_y * window.surface_width as usize + source_x];
                    }
                }
            }
        } else {
            for row in 0..window.height {
                let dst_start = (window.y + row) as usize * SCREEN_WIDTH as usize + window.x as usize;
                frame[dst_start..dst_start + window.width as usize].fill(PLACEHOLDER_COLOR);
            }
        }
        if window.id >= DEMONX_WINDOW_ID_BASE {
            stroke_rect(frame, window.x as i32, window.y as i32, window.width, window.height,
                        if window.id == focused_window { COLOR_ACCENT_DARK } else { COLOR_BORDER });
            // Three tiny diagonal marks make the real resize target visible.
            for grip in 0..3 {
                put_pixel(frame,
                          window.x as i32 + window.width as i32 - 3 - grip,
                          window.y as i32 + window.height as i32 - 2,
                          COLOR_TEXT_MUTED);
            }
            let hovered_control = if window.id == hovered_decoration {
                title_control_at(&window, cursor_x.saturating_sub(window.x))
            } else {
                0
            };
            draw_decoration(
                frame,
                &window,
                window.id == focused_window,
                hovered_control,
                shell_icons,
                shell_icons_loaded,
            );
        }
    }
    if dragging_id != 0 {
        let target = snap_target(cursor_x, cursor_y);
        if target != 0 {
            let (x, y, width, height) = snap_geometry(target);
            let preview_top = y.saturating_sub(TITLE_HEIGHT);
            blend_rounded_rect(
                frame,
                x as i32 + 4,
                preview_top as i32 + 4,
                width.saturating_sub(8),
                height + TITLE_HEIGHT - 8,
                CORNER_RADIUS,
                COLOR_ACCENT_DARK,
                92,
            );
            stroke_rect(
                frame,
                x as i32 + 4,
                preview_top as i32 + 4,
                width.saturating_sub(8),
                height + TITLE_HEIGHT - 8,
                COLOR_ACCENT,
            );
        }
    }
    draw_panel(
        frame,
        current_workspace,
        launcher_open,
        cursor_x,
        cursor_y,
        shell_icons,
        shell_icons_loaded,
    );
    draw_taskbar(
        frame,
        table,
        current_workspace,
        focused_window,
        cursor_x,
        cursor_y,
        shell_icons,
        shell_icons_loaded,
    );
    if launcher_open {
        draw_launcher(frame, launcher_selection, shell_icons, shell_icons_loaded);
    }
}

#[no_mangle]
pub extern "C" fn rust_main() -> ! {
    demon_abi::write(b"RUST_COMPOSITOR_START\n");

    let server = demon_abi::channel_create(WINDOW_SERVICE);
    if server == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL channel-create\n");
        demon_abi::exit(1);
    }

    let input_handle = demon_abi::service_open(CapabilityService::Input as u64);
    if input_handle == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL input-open\n");
        demon_abi::exit(1);
    }

    let display = demon_abi::service_open(CapabilityService::Display as u64);
    if display == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-open\n");
        demon_abi::exit(1);
    }

    let mut info = DisplayInfo::default();
    if demon_abi::display_info(display, &mut info) == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-info\n");
        demon_abi::exit(1);
    }
    if info.width != SCREEN_WIDTH as u64 || info.height != SCREEN_HEIGHT as u64 {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-size\n");
        demon_abi::exit(1);
    }

    let mut table = [Window::EMPTY; WINDOW_LIMIT];
    let mut next_z: u32 = 1;
    let mut focused_window: u32 = 0;
    let mut cursor_x: u32 = 60;
    let mut cursor_y: u32 = 80;
    // Drag/resize state, ported from Desktop/demonwm/demonwm.cc's
    // drag_window_/resize_frame_ pair -- 0 means "not active" (id 0 is
    // never a real window id, see DEMONX_WINDOW_ID_BASE). Grab offsets are
    // stored as (cursor - window origin) at gesture start so motion stays
    // anchored to wherever on the title bar/corner the user actually
    // grabbed, matching continueDrag/continueResize's own math.
    let mut dragging_id: u32 = 0;
    let mut drag_grab_x: i32 = 0;
    let mut drag_grab_y: i32 = 0;
    let mut resizing_id: u32 = 0;
    let mut resize_grab_x: i32 = 0;
    let mut resize_grab_y: i32 = 0;
    let mut resize_start_width: u32 = 0;
    let mut resize_start_height: u32 = 0;
    // Workspace/launcher UI state.
    let mut current_workspace: u32 = 0;
    let mut launcher_open: bool = false;
    let mut launcher_selection: i32 = 0;
    // A launcher action can close the launcher on key-down. Remember that
    // key so its matching key-up is not delivered to the newly focused app
    // without a preceding key-down.
    let mut suppressed_key_up: u16 = 0;
    if demon_abi::display_cursor_move(display, cursor_x as u64, cursor_y as u64, 0) == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL cursor-init\n");
        demon_abi::exit(1);
    }

    const PIXELS: usize = SCREEN_WIDTH as usize * SCREEN_HEIGHT as usize;
    static mut FRAME: [u32; PIXELS] = [0; PIXELS];

    // Heap-allocated, not a static array: WALLPAPER_PIXELS * 4 bytes
    // (1.2 MiB at full 640x480 resolution) compiled directly into this
    // binary's own image would push its total size well past
    // USER_LARGE_CODE_MAX_PAGES's 320-page ceiling (see anonymous_map's
    // own comment) -- confirmed by trying exactly that first, at the
    // smaller quarter-resolution size, and the compositor stopped loading
    // at all.
    const WALLPAPER_PIXELS: usize = demon_abi::WALLPAPER_WIDTH * demon_abi::WALLPAPER_HEIGHT;
    const WALLPAPER_BYTES: usize = WALLPAPER_PIXELS * 4;
    const SHELL_ICON_PIXELS: usize =
        SHELL_ICON_WIDTH * SHELL_ICON_HEIGHT * SHELL_ICON_COUNT;
    const SHELL_ICON_BYTES: usize = SHELL_ICON_PIXELS * 4;
    const DESKTOP_ASSET_BYTES: usize = WALLPAPER_BYTES + SHELL_ICON_BYTES;

    // anonymous_map is intentionally a one-shot, process-lifetime allocator.
    // Reserve both assets together and partition that single mapping; trying
    // to map the icon atlas separately made its second allocation fail and
    // left every carefully positioned icon invisible.
    let desktop_assets_address = demon_abi::anonymous_map(DESKTOP_ASSET_BYTES as u64);
    let wallpaper_address = desktop_assets_address;
    let wallpaper_loaded = if wallpaper_address != UINT64_MAX {
        let storage = demon_abi::service_open(CapabilityService::Storage as u64);
        let handle = if storage != UINT64_MAX {
            demon_abi::file_open(storage, b"/system/wallpaper.argb", false)
        } else {
            UINT64_MAX
        };
        let read_bytes = if handle != UINT64_MAX {
            let destination = unsafe {
                core::slice::from_raw_parts_mut(wallpaper_address as *mut u8, WALLPAPER_BYTES)
            };
            let result = demon_abi::handle_read(handle, destination);
            demon_abi::handle_close(handle);
            result
        } else {
            UINT64_MAX
        };
        read_bytes == WALLPAPER_BYTES as u64
    } else {
        false
    };
    let wallpaper: &[u32] = if wallpaper_loaded {
        unsafe { core::slice::from_raw_parts(wallpaper_address as *const u32, WALLPAPER_PIXELS) }
    } else {
        &[]
    };

    // Keep artwork outside the executable: the 15 KiB atlas is an ordinary
    // RAMFS resource generated from the Fluent SVG pack. It occupies the
    // aligned tail of the same anonymous asset mapping as the wallpaper.
    let shell_icons_address = if desktop_assets_address != UINT64_MAX {
        desktop_assets_address + WALLPAPER_BYTES as u64
    } else {
        UINT64_MAX
    };
    let shell_icons_loaded = if shell_icons_address != UINT64_MAX {
        let storage = demon_abi::service_open(CapabilityService::Storage as u64);
        let handle = if storage != UINT64_MAX {
            demon_abi::file_open(storage, b"/system/shell-icons.argb", false)
        } else {
            UINT64_MAX
        };
        let read_bytes = if handle != UINT64_MAX {
            let destination = unsafe {
                core::slice::from_raw_parts_mut(
                    shell_icons_address as *mut u8,
                    SHELL_ICON_BYTES,
                )
            };
            let result = demon_abi::handle_read(handle, destination);
            demon_abi::handle_close(handle);
            result
        } else {
            UINT64_MAX
        };
        read_bytes == SHELL_ICON_BYTES as u64
    } else {
        false
    };
    let shell_icons: &[u32] = if shell_icons_loaded {
        unsafe {
            core::slice::from_raw_parts(
                shell_icons_address as *const u32,
                SHELL_ICON_PIXELS,
            )
        }
    } else {
        &[]
    };

    let mut dirty = true;
    let mut next_frame_tick = demon_abi::ticks() + 5;

    demon_abi::write(b"RUST_COMPOSITOR_READY\n");
    demon_abi::write(
        b"RUST_DESKTOP_SHELL_READY style=native-blue minimize=1 workspaces=3 keyboard=1\n",
    );

    loop {
        /* Games are asynchronous children of the desktop. Collect completed
           ones without blocking this event loop or consuming one of the
           kernel's bounded process slots indefinitely. */
        while demon_abi::reap_exited() != UINT64_MAX {}
        let now = demon_abi::ticks();
        let wait_ticks = if now < next_frame_tick {
            next_frame_tick - now
        } else {
            1
        };
        let mut message = WindowMessage::default();
        let mut event = InputEvent::default();
        let ready = demon_abi::compositor_wait(server, input_handle, &mut message, &mut event, wait_ticks);
        let mut repaint = false;

        if ready == 1 {
            match message.opcode {
                x if x == WindowOpcode::Create as u16 => {
                    if message.x >= 0
                        && message.y >= 0
                        && message.width >= MIN_CREATE_SIZE
                        && message.height >= MIN_CREATE_SIZE
                        && message.width <= SCREEN_WIDTH
                        && message.height <= SCREEN_HEIGHT
                        && message.x as u32 + message.width <= SCREEN_WIDTH
                        && message.y as u32 + message.height <= SCREEN_HEIGHT
                        && find_slot(&table, message.window_id).is_none()
                    {
                        if let Some(slot) = find_free_slot(&table) {
                            table[slot] = Window {
                                in_use: true,
                                id: message.window_id,
                                z: next_z,
                                x: message.x as u32,
                                y: message.y as u32,
                                width: message.width,
                                height: message.height,
                                surface_id: 0,
                                mapped_address: 0,
                                surface_width: message.width,
                                surface_height: message.height,
                                maximized: false,
                                minimized: false,
                                restore_x: message.x as u32,
                                restore_y: message.y as u32,
                                restore_width: message.width,
                                restore_height: message.height,
                                workspace: current_workspace,
                                title: message.payload,
                                title_length: (message.payload_length as usize).min(message.payload.len()) as u8,
                            };
                            next_z += 1;
                            if message.surface_id != 0 {
                                // A logical X window may intentionally be
                                // larger than its lightweight pixel surface.
                                // Query the handle instead of assuming both
                                // dimensions match the CREATE message.
                                let queried_width =
                                    demon_abi::handle_query(message.surface_id as u64, 0);
                                let queried_height =
                                    demon_abi::handle_query(message.surface_id as u64, 1);
                                let queried_pixels =
                                    demon_abi::handle_query(message.surface_id as u64, 2);
                                let valid_dimensions = queried_width > 0 && queried_height > 0 &&
                                    queried_width <= SCREEN_WIDTH as u64 &&
                                    queried_height <= SCREEN_HEIGHT as u64 &&
                                    queried_width.saturating_mul(queried_height) == queried_pixels;
                                let address = if valid_dimensions {
                                    demon_abi::surface_map(message.surface_id as u64)
                                } else {
                                    UINT64_MAX
                                };
                                if address != 0 && address != UINT64_MAX {
                                    table[slot].surface_id = message.surface_id;
                                    table[slot].mapped_address = address;
                                    table[slot].surface_width = queried_width as u32;
                                    table[slot].surface_height = queried_height as u32;
                                } else {
                                    demon_abi::handle_close(message.surface_id as u64);
                                }
                            }
                            focused_window = message.window_id;
                            repaint = true;
                        }
                    }
                }
                x if x == WindowOpcode::Close as u16 => {
                    if let Some(slot) = find_slot(&table, message.window_id) {
                        let window = table[slot];
                        if window.surface_id != 0 {
                            demon_abi::surface_unmap(window.surface_id as u64);
                            demon_abi::handle_close(window.surface_id as u64);
                        }
                        table[slot] = Window::EMPTY;
                        if focused_window == message.window_id {
                            focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                        }
                        repaint = true;
                    }
                }
                x if x == WindowOpcode::Focus as u16 => {
                    if let Some(slot) = find_slot(&table, message.window_id) {
                        table[slot].minimized = false;
                        table[slot].z = next_z;
                        next_z += 1;
                        focused_window = message.window_id;
                        repaint = true;
                    }
                }
                x if x == WindowOpcode::Move as u16 => {
                    if let Some(slot) = find_slot(&table, message.window_id) {
                        if message.x >= 0
                            && message.y >= 0
                            && message.width >= MIN_MOVE_WIDTH
                            && message.height >= MIN_MOVE_HEIGHT
                            && message.x as u32 + message.width <= SCREEN_WIDTH
                            && message.y as u32 + message.height <= SCREEN_HEIGHT
                        {
                            table[slot].x = message.x as u32;
                            table[slot].y = message.y as u32;
                            table[slot].width = message.width;
                            table[slot].height = message.height;
                            repaint = true;
                        }
                    }
                }
                x if x == WindowOpcode::PointerWarp as u16 => {
                    cursor_x = (message.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (message.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    demon_abi::display_cursor_move(display, cursor_x as u64, cursor_y as u64, 0);
                    repaint = true;
                }
                _ => {}
            }
        } else if ready == 2 {
            match event.kind {
                k if k == INPUT_MOUSE_MOVE => {
                    cursor_x = (event.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (event.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    if launcher_open {
                        if let Some(cell) = launcher_cell_at(cursor_x as i32, cursor_y as i32) {
                            launcher_selection = cell;
                        }
                    }
                    if dragging_id != 0 {
                        if let Some(slot) = find_slot(&table, dragging_id) {
                            // Pull a maximized or snapped window out at its
                            // remembered size before continuing the drag.
                            // Preserve the pointer's proportional horizontal
                            // position so the window does not jump sideways.
                            if table[slot].maximized {
                                let old = table[slot];
                                let restored_width = old.restore_width
                                    .max(MIN_WINDOW_WIDTH)
                                    .min(SCREEN_WIDTH);
                                let restored_height = old.restore_height
                                    .max(MIN_WINDOW_HEIGHT)
                                    .min(SCREEN_HEIGHT - RESERVED_BOTTOM - RESERVED_TOP - TITLE_HEIGHT);
                                let local_x = cursor_x.saturating_sub(old.x).min(old.width);
                                drag_grab_x = ((local_x as u64 * restored_width as u64)
                                    / old.width.max(1) as u64) as i32;
                                table[slot].x = old.restore_x;
                                table[slot].y = old.restore_y;
                                table[slot].width = restored_width;
                                table[slot].height = restored_height;
                                table[slot].maximized = false;
                            }
                            let width = table[slot].width;
                            let height = table[slot].height;
                            let mut new_x = cursor_x as i32 - drag_grab_x;
                            let mut new_y = cursor_y as i32 - drag_grab_y;
                            new_x = new_x.max(0).min((SCREEN_WIDTH - width.min(SCREEN_WIDTH)) as i32);
                            new_y = new_y
                                .max(RESERVED_TOP as i32 + TITLE_HEIGHT as i32)
                                .min((SCREEN_HEIGHT - RESERVED_BOTTOM - height.min(SCREEN_HEIGHT - RESERVED_BOTTOM)) as i32);
                            table[slot].x = new_x as u32;
                            table[slot].y = new_y as u32;
                            table[slot].maximized = false;
                        } else {
                            dragging_id = 0;
                        }
                    } else if resizing_id != 0 {
                        if let Some(slot) = find_slot(&table, resizing_id) {
                            let window = table[slot];
                            let mut width = resize_start_width as i32 + (cursor_x as i32 - resize_grab_x);
                            let mut height = resize_start_height as i32 + (cursor_y as i32 - resize_grab_y);
                            width = width
                                .max(MIN_WINDOW_WIDTH as i32)
                                .min((SCREEN_WIDTH - window.x) as i32);
                            height = height
                                .max(MIN_WINDOW_HEIGHT as i32)
                                .min((SCREEN_HEIGHT - RESERVED_BOTTOM - window.y) as i32);
                            table[slot].width = width as u32;
                            table[slot].height = height as u32;
                            table[slot].maximized = false;
                        } else {
                            resizing_id = 0;
                        }
                    } else if let Some(slot) = top_slot_at(&table, current_workspace, cursor_x, cursor_y) {
                        let window = table[slot];
                        if window.id >= DEMONX_WINDOW_ID_BASE {
                            if let Some((client_x, client_y)) =
                                client_point(&window, cursor_x, cursor_y) {
                                send_window_event(
                                    window.id,
                                    WindowOpcode::Pointer,
                                    5,
                                    client_x,
                                    client_y,
                                    0,
                                    0,
                                );
                            }
                        }
                    }
                    let cursor_icon = cursor_icon_at(
                        &table,
                        current_workspace,
                        cursor_x,
                        cursor_y,
                        dragging_id,
                        resizing_id,
                        launcher_open,
                    );
                    demon_abi::display_cursor_move(
                        display,
                        cursor_x as u64,
                        cursor_y as u64,
                        cursor_icon,
                    );
                    repaint = true;
                }
                k if k == INPUT_MOUSE_BUTTON_DOWN => {
                    cursor_x = (event.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (event.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    if in_launcher_button(cursor_x as i32, cursor_y as i32) {
                        launcher_open = !launcher_open;
                        launcher_selection = 0;
                        repaint = true;
                    } else if let Some(cell) = quick_launch_at(
                        cursor_x as i32,
                        cursor_y as i32,
                    ) {
                        activate_launcher_cell(
                            cell,
                            &mut table,
                            &mut current_workspace,
                            &mut focused_window,
                            &mut next_z,
                        );
                        launcher_open = false;
                        repaint = true;
                    } else if launcher_open {
                        if let Some(cell) = launcher_cell_at(cursor_x as i32, cursor_y as i32) {
                            activate_launcher_cell(
                                cell,
                                &mut table,
                                &mut current_workspace,
                                &mut focused_window,
                                &mut next_z,
                            );
                        }
                        launcher_open = false;
                        repaint = true;
                    } else if let Some(dot) = workspace_dot_at(cursor_x as i32, cursor_y as i32) {
                        current_workspace = dot;
                        focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                        repaint = true;
                    } else if let Some(id) = taskbar_item_at(&table, current_workspace, cursor_x as i32, cursor_y as i32) {
                        if let Some(slot) = find_slot(&table, id) {
                            if focused_window == id && !table[slot].minimized {
                                table[slot].minimized = true;
                                focused_window = find_top_slot(&table, current_workspace)
                                    .map_or(0, |top| table[top].id);
                            } else {
                                table[slot].minimized = false;
                                focused_window = id;
                                table[slot].z = next_z;
                                next_z += 1;
                            }
                            repaint = true;
                        }
                    } else if let Some(slot) = decoration_hit_at(&table, current_workspace, cursor_x, cursor_y) {
                        let window = table[slot];
                        focused_window = window.id;
                        table[slot].z = next_z;
                        next_z += 1;
                        let local_x = cursor_x - window.x;
                        match title_control_at(&window, local_x) {
                            2 => {
                                focused_window = close_window(
                                    &mut table,
                                    slot,
                                    current_workspace,
                                );
                            }
                            1 => {
                                toggle_maximize(&mut table[slot]);
                            }
                            3 => {
                                table[slot].minimized = true;
                                dragging_id = 0;
                                resizing_id = 0;
                                focused_window = find_top_slot(&table, current_workspace)
                                    .map_or(0, |top| table[top].id);
                            }
                            _ => {
                                dragging_id = window.id;
                                drag_grab_x = cursor_x as i32 - window.x as i32;
                                drag_grab_y = cursor_y as i32 - window.y as i32;
                            }
                        }
                        repaint = true;
                    } else if let Some(slot) = top_slot_at(&table, current_workspace, cursor_x, cursor_y) {
                        let window = table[slot];
                        focused_window = window.id;
                        table[slot].z = next_z;
                        next_z += 1;
                        if window.id >= DEMONX_WINDOW_ID_BASE && hits_resize_corner(&window, cursor_x, cursor_y) {
                            resizing_id = window.id;
                            resize_grab_x = cursor_x as i32;
                            resize_grab_y = cursor_y as i32;
                            resize_start_width = window.width;
                            resize_start_height = window.height;
                        } else if window.id >= DEMONX_WINDOW_ID_BASE {
                            if let Some((client_x, client_y)) =
                                client_point(&window, cursor_x, cursor_y) {
                                send_window_event(
                                    window.id,
                                    WindowOpcode::Button,
                                    4,
                                    client_x,
                                    client_y,
                                    1,
                                    event.code as u32,
                                );
                            }
                        }
                        repaint = true;
                    }
                }
                k if k == INPUT_MOUSE_BUTTON_UP => {
                    cursor_x = (event.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (event.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    let finished_drag = dragging_id;
                    let finished_resize = resizing_id;
                    if finished_drag != 0 {
                        let target = snap_target(cursor_x, cursor_y);
                        if target != 0 {
                            if let Some(slot) = find_slot(&table, finished_drag) {
                                let window = table[slot];
                                table[slot].restore_x = window.x;
                                table[slot].restore_y = window.y;
                                table[slot].restore_width = window.width;
                                table[slot].restore_height = window.height;
                                let (x, y, width, height) = snap_geometry(target);
                                table[slot].x = x;
                                table[slot].y = y;
                                table[slot].width = width;
                                table[slot].height = height;
                                table[slot].maximized = true;
                            }
                        }
                    }
                    dragging_id = 0;
                    resizing_id = 0;
                    if finished_drag == 0 && finished_resize == 0 {
                      if let Some(slot) = top_slot_at(&table, current_workspace, cursor_x, cursor_y) {
                        let window = table[slot];
                        if window.id >= DEMONX_WINDOW_ID_BASE {
                            if let Some((client_x, client_y)) =
                                client_point(&window, cursor_x, cursor_y) {
                                send_window_event(
                                    window.id,
                                    WindowOpcode::Button,
                                    6,
                                    client_x,
                                    client_y,
                                    2,
                                    event.code as u32,
                                );
                            }
                        }
                      }
                    }
                    let cursor_icon = cursor_icon_at(
                        &table,
                        current_workspace,
                        cursor_x,
                        cursor_y,
                        0,
                        0,
                        launcher_open,
                    );
                    demon_abi::display_cursor_move(
                        display,
                        cursor_x as u64,
                        cursor_y as u64,
                        cursor_icon,
                    );
                    repaint = true;
                }
                k if k == INPUT_KEY_DOWN || k == INPUT_KEY_UP => {
                    if k == INPUT_KEY_UP && suppressed_key_up == event.code {
                        suppressed_key_up = 0;
                        continue;
                    }
                    let ctrl = (event.modifiers & demon_abi::INPUT_MOD_CTRL) != 0;
                    let alt = (event.modifiers & INPUT_MOD_ALT) != 0;
                    let workspace_shortcut = ctrl
                        && (KEY_WORKSPACE_1..=KEY_WORKSPACE_3).contains(&event.code);
                    let launcher_shortcut = ctrl && event.code == KEY_SPACE;
                    let cycle_shortcut = alt && event.code == KEY_TAB;
                    let window_shortcut = alt
                        && (event.code == KEY_F4
                            || event.code == KEY_F9
                            || event.code == KEY_F10);

                    // Shortcut key-up events are swallowed too, preventing an
                    // application from seeing half of a desktop-level chord.
                    if workspace_shortcut {
                        if k == INPUT_KEY_DOWN {
                            suppressed_key_up = event.code;
                            current_workspace = (event.code - KEY_WORKSPACE_1) as u32;
                            focused_window = find_top_slot(&table, current_workspace)
                                .map_or(0, |slot| table[slot].id);
                            launcher_open = false;
                            repaint = true;
                        }
                    } else if launcher_shortcut {
                        if k == INPUT_KEY_DOWN {
                            suppressed_key_up = event.code;
                            launcher_open = !launcher_open;
                            launcher_selection = 0;
                            repaint = true;
                        }
                    } else if cycle_shortcut {
                        if k == INPUT_KEY_DOWN {
                            suppressed_key_up = event.code;
                            if let Some(slot) = find_cycle_slot(
                                &table,
                                current_workspace,
                                focused_window,
                            ) {
                                table[slot].minimized = false;
                                table[slot].z = next_z;
                                next_z += 1;
                                focused_window = table[slot].id;
                            }
                            launcher_open = false;
                            repaint = true;
                        }
                    } else if window_shortcut {
                        if k == INPUT_KEY_DOWN {
                            suppressed_key_up = event.code;
                            if let Some(slot) = find_slot(&table, focused_window) {
                                if event.code == KEY_F4 {
                                    focused_window = close_window(
                                        &mut table,
                                        slot,
                                        current_workspace,
                                    );
                                } else if event.code == KEY_F9 {
                                    table[slot].minimized = true;
                                    focused_window = find_top_slot(&table, current_workspace)
                                        .map_or(0, |top| table[top].id);
                                } else {
                                    toggle_maximize(&mut table[slot]);
                                }
                            }
                            dragging_id = 0;
                            resizing_id = 0;
                            launcher_open = false;
                            repaint = true;
                        }
                    } else if launcher_open {
                        if k == INPUT_KEY_DOWN {
                            if event.code == KEY_ESCAPE {
                                suppressed_key_up = event.code;
                                launcher_open = false;
                            } else if event.code == KEY_UP {
                                launcher_selection =
                                    (launcher_selection + LAUNCHER_CELL_COUNT - 1)
                                        % LAUNCHER_CELL_COUNT;
                            } else if event.code == KEY_DOWN {
                                launcher_selection =
                                    (launcher_selection + 1) % LAUNCHER_CELL_COUNT;
                            } else if event.code == KEY_ENTER {
                                suppressed_key_up = event.code;
                                activate_launcher_cell(
                                    launcher_selection,
                                    &mut table,
                                    &mut current_workspace,
                                    &mut focused_window,
                                    &mut next_z,
                                );
                                launcher_open = false;
                            } else if (KEY_WORKSPACE_1..=KEY_WORKSPACE_3)
                                .contains(&event.code)
                            {
                                suppressed_key_up = event.code;
                                launcher_selection =
                                    (event.code - KEY_WORKSPACE_1) as i32;
                                activate_launcher_cell(
                                    launcher_selection,
                                    &mut table,
                                    &mut current_workspace,
                                    &mut focused_window,
                                    &mut next_z,
                                );
                                launcher_open = false;
                            }
                            repaint = true;
                        }
                    } else if focused_window != 0 {
                        let packed_type_code = event.kind as u32 | ((event.code as u32) << 16);
                        send_window_event(
                            focused_window,
                            WindowOpcode::Key,
                            3,
                            packed_type_code as i32,
                            event.modifiers as i32,
                            event.value as u32,
                            0,
                        );
                    }
                }
                _ => {}
            }
        } else if ready == 3 {
            dirty = true;
        } else {
            demon_abi::write(b"RUST_COMPOSITOR_FAIL wait\n");
            demon_abi::exit(1);
        }

        if repaint {
            dirty = true;
            let window_count = table.iter().filter(|w| w.in_use).count() as u64;
            demon_abi::compositor_report(display, window_count, focused_window as u64);
        }

        let present_now = demon_abi::ticks();
        if dirty && (ready == 3 || present_now >= next_frame_tick) {
            let frame = unsafe { &mut *core::ptr::addr_of_mut!(FRAME) };
            composite(&table, frame, focused_window, current_workspace, launcher_open,
                      launcher_selection, cursor_x, cursor_y, dragging_id,
                      wallpaper, wallpaper_loaded, shell_icons, shell_icons_loaded);
            let submit = DisplaySubmit {
                x: 0,
                y: 0,
                width: SCREEN_WIDTH as u64,
                height: SCREEN_HEIGHT as u64,
                pixels: frame.as_ptr() as u64,
                flags: DISPLAY_SUBMIT_PRESENT,
            };
            if demon_abi::display_submit(display, &submit) != UINT64_MAX {
                dirty = false;
            }
            next_frame_tick = demon_abi::ticks() + 5;
        }
    }
}

// Same real MAKO-ABI entry shape as rust/hello and Stage 2 -- see those
// crates' comments for why `_start` stays raw assembly.
global_asm!(
    ".global _start",
    "_start:",
    "call rust_main",
    "1: hlt",
    "jmp 1b",
);

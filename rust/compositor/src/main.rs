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

// This Stage 3 port replaces user/compositor.mko as the one real compositor
// in the desktop stack (kernel.c now spawns this binary, not compositor.elf
// -- see the comment there). It is deliberately scoped to what actually
// matters when DemonWM is the session shell (the only mode this desktop
// stack boots today): real window bookkeeping, real zero-copy surface
// compositing, and real input delivery to DemonX. Everything the MKO
// compositor also carried for its own retired chrome -- the login gate
// (permanently bypassed there already), the launcher/taskbar/settings
// popovers, native window drag/resize, per-window titlebars, and
// open/close fade animation -- belongs to a shell DemonWM has already
// replaced, so none of it is ported here. DemonWM draws its own decorations
// as ordinary window content; this compositor's only job is to place that
// content on screen and route input to it, exactly like render_demonwm_backend
// already scoped the old compositor down to when DemonWM was in charge.

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
// A cohesive indigo/violet identity instead of the earlier muddier
// gray-and-orange mix: richer, more saturated focused accents, a deep
// near-black indigo (not flat gray-black) for chrome surfaces, and a
// warmer ember reserved for the one or two spots that need a genuine
// "hot" accent (active workspace dot, closed launcher button) rather
// than sprinkled everywhere.
const COLOR_TITLE_FOCUSED: u32 = 0xff7c5cfc;
const COLOR_TITLE_UNFOCUSED: u32 = 0xff2e3348;
const COLOR_TEXT: u32 = 0xfff5f6fa;
const COLOR_TEXT_MUTED: u32 = 0xffa9b1c6;
const COLOR_EMBER: u32 = 0xffff6b4a;
const COLOR_VIOLET: u32 = 0xff9b7bff;
const COLOR_PANEL_BG: u32 = 0xff141622;
const COLOR_TASKBAR_ITEM: u32 = 0xff272c42;
// macOS-style traffic-light controls: red = close, green = maximize/
// restore. No decorative third circle -- every button drawn actually
// does something when clicked (see title_control_at), unlike a plain
// unwired "minimize" dot would.
const COLOR_CONTROL_CLOSE: u32 = 0xffff5f57;
const COLOR_CONTROL_MAXIMIZE: u32 = 0xff30d158;
const CORNER_RADIUS: i32 = 8;
// Chrome surfaces (panel, launcher) blend into the wallpaper instead of a
// flat opaque fill -- a soft frosted-glass read instead of a solid card
// stacked on top of the desktop.
const PANEL_BLEND_ALPHA: u8 = 235;

// Panel geometry, ported from demonwm's kMargin/kPanelHeight/
// kLauncherBtnX0/X1 -- drawn as a pure compositor overlay (never a window
// table entry, unlike DemonWM's real 628x28 CREATE'd panel window) since
// it has no client of its own to composite from.
const PANEL_MARGIN: i32 = 6;
const PANEL_HEIGHT: u32 = 28;
const LAUNCHER_BTN_X0: i32 = PANEL_MARGIN + 4;
const LAUNCHER_BTN_X1: i32 = LAUNCHER_BTN_X0 + 72;
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

// The launcher popover, opened by clicking the panel's DEMONOS button.
// Two entries:
//
// - Terminal: xterm is the only real windowed compositor client that
//   exists today, but this cell isn't wired to actually spawn a second
//   one (see the click handler's own comment) -- that was tried and
//   reproducibly crashed the whole compositor via a real, separate kernel
//   IPC/input wait-state bug, not anything in this file. The cell exists
//   and closes the launcher on click so the UI reads as real rather than
//   decorative, without shipping the one known-unsafe action available
//   for it today.
// - ClassiCube: a real, working app, but a fundamentally different kind
//   from xterm -- it takes exclusive DISPLAY/SURFACE ownership and draws
//   directly to the screen instead of rendering into a compositor window
//   (see its CAPABILITY_SERVICE_DISPLAY grant in src/makobox.c's
//   launch_app). Launching it uses demon_abi::launch_foreground (syscall
//   50), which blocks this whole compositor process until it exits --
//   see that syscall's own comment in src/arch/x86_64/userspace.c for why
//   a new syscall was needed rather than reusing plain spawn+wait.
const LAUNCHER_X: i32 = PANEL_MARGIN;
const LAUNCHER_Y: i32 = PANEL_MARGIN + PANEL_HEIGHT as i32 + 8;
const LAUNCHER_WIDTH: u32 = 170;
const LAUNCHER_CELL_HEIGHT: i32 = 50;
const LAUNCHER_CELL_GAP: i32 = 6;
const LAUNCHER_CELL_COUNT: i32 = 3;
const LAUNCHER_HEIGHT: u32 =
    (LAUNCHER_CELL_HEIGHT * LAUNCHER_CELL_COUNT + LAUNCHER_CELL_GAP * (LAUNCHER_CELL_COUNT + 1)) as u32;
// CONSOLE | PROCESS | INPUT | STORAGE | DISPLAY | SURFACE -- the exact
// same capability set src/makobox.c's launch_app grants
// /system/bin/classicube-core.elf.
const CLASSICUBE_SERVICE_MASK: u64 =
    (1 << 1) | (1 << 3) | (1 << 8) | (1 << 4) | (1 << 7) | (1 << 9);
// Same base plus AUDIO -- the exact set launch_app grants
// /system/bin/doom-full.elf, the one real sustained-play build (unlike
// classicube-core.elf, currently only a self-test/demo milestone that
// loads a world and exits quickly).
const DOOM_SERVICE_MASK: u64 = CLASSICUBE_SERVICE_MASK | (1 << 11);

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
    // The mapped surface's own real pixel dimensions, fixed at Create time
    // and never touched by resize -- composite() blits at most this many
    // columns/rows regardless of the window's current on-screen width/
    // height, so a resize never reads past what the client actually
    // allocated (most clients here, like xterm, have a fixed-size surface
    // and don't repaint at a new resolution; resizing them just crops or
    // pads with background rather than reading out of bounds).
    surface_width: u32,
    surface_height: u32,
    maximized: bool,
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

// Traffic-light layout, macOS convention: close then maximize, left to
// right, near the title bar's left edge. Both return the dot's CENTER x
// offset from window.x, not a top-left corner -- these are circles, not
// squares. (_window is unused for now: both positions are fixed offsets
// regardless of window width, kept as a parameter in case a future control
// needs width-relative placement the way the old right-aligned layout did.)
fn close_control_x(_window: &Window) -> i32 {
    16
}

fn maximize_control_x(_window: &Window) -> i32 {
    16 + CONTROL_GAP as i32 + CONTROL_SIZE as i32
}

// 1 = hit the maximize control, 2 = hit the close control, 0 = neither
// (just an ordinary drag-start point in the title bar). Hit region is a
// square CONTROL_SIZE across, centered on each dot -- generous compared to
// the visible circle radius, matching how real traffic lights have a
// bigger click target than their drawn size.
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
    0
}

fn decoration_hit_at(table: &[Window; WINDOW_LIMIT], workspace: u32, x: u32, y: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| {
            w.in_use
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
        let dot_x = dots_x + dot as i32 * 10;
        if x >= dot_x && x < dot_x + 6 {
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
    let mut item_x = BOTTOM_BAR_MARGIN + 8;
    for window in table.iter() {
        if !window.in_use || window.workspace != workspace || window.id < DEMONX_WINDOW_ID_BASE {
            continue;
        }
        if x >= item_x && x < item_x + TASKBAR_ITEM_WIDTH as i32 {
            return Some(window.id);
        }
        item_x += TASKBAR_ITEM_WIDTH as i32 + TASKBAR_ITEM_GAP;
    }
    None
}

fn hits_resize_corner(window: &Window, x: u32, y: u32) -> bool {
    x + RESIZE_GRIP >= window.x + window.width && y + RESIZE_GRIP >= window.y + window.height
}

fn in_launcher_button(x: i32, y: i32) -> bool {
    x >= LAUNCHER_BTN_X0 && x < LAUNCHER_BTN_X1 && y >= PANEL_MARGIN && y < PANEL_MARGIN + PANEL_HEIGHT as i32
}

fn launcher_cell_y(index: i32) -> i32 {
    LAUNCHER_Y + LAUNCHER_CELL_GAP + index * (LAUNCHER_CELL_HEIGHT + LAUNCHER_CELL_GAP)
}

// Returns which cell (0 = Terminal, 1 = ClassiCube) a launcher click
// landed on, or None if it missed both (still inside the popover, or
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

fn draw_launcher_cell(frame: &mut [u32], index: i32, icon_color: u32, label: &[u8]) {
    let cell_y = launcher_cell_y(index);
    fill_rounded_rect(frame, LAUNCHER_X + 8, cell_y + (LAUNCHER_CELL_HEIGHT - 20) / 2, 20, 20, 5, icon_color);
    draw_text(frame, LAUNCHER_X + 36, cell_y + (LAUNCHER_CELL_HEIGHT - 5) / 2, label, COLOR_TEXT);
}

fn draw_launcher(frame: &mut [u32]) {
    draw_shadow(frame, LAUNCHER_X, LAUNCHER_Y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
    blend_rounded_rect(frame, LAUNCHER_X, LAUNCHER_Y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT, CORNER_RADIUS, 0xff20242c, PANEL_BLEND_ALPHA);
    draw_launcher_cell(frame, 0, COLOR_VIOLET, b"TERMINAL");
    draw_launcher_cell(frame, 1, COLOR_CONTROL_MAXIMIZE, b"CLASSICUBE");
    draw_launcher_cell(frame, 2, COLOR_CONTROL_CLOSE, b"DOOM");
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
        .filter(|(_, w)| w.in_use && w.workspace == workspace)
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
}

fn top_slot_at(table: &[Window; WINDOW_LIMIT], workspace: u32, x: u32, y: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| {
            w.in_use
                && w.workspace == workspace
                && x >= w.x
                && x < w.x + w.width
                && y >= w.y
                && y < w.y + w.height
        })
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
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

// A soft rectangular shadow: rather than a hard-edged alpha rect (which
// just reads as a second, flat-gray rectangle), alpha fades linearly over
// SHADOW_FEATHER pixels at each edge, so it actually reads as something
// glowing/receding behind the window rather than another decoration.
const SHADOW_FEATHER: i32 = 8;
fn draw_shadow(frame: &mut [u32], x: i32, y: i32, width: u32, height: u32) {
    let x0 = x - SHADOW_FEATHER;
    let y0 = y - SHADOW_FEATHER;
    let x1 = x + width as i32 + SHADOW_FEATHER;
    let y1 = y + height as i32 + SHADOW_FEATHER;
    for row in y0..y1 {
        let fade_y = ((row - y0).min(y1 - 1 - row) + 1).clamp(0, SHADOW_FEATHER);
        for col in x0..x1 {
            let fade_x = ((col - x0).min(x1 - 1 - col) + 1).clamp(0, SHADOW_FEATHER);
            let fade = fade_x.min(fade_y);
            let alpha = (fade * 70 / SHADOW_FEATHER) as u8;
            if alpha > 0 {
                blend_pixel(frame, col, row, 0xff000000, alpha);
            }
        }
    }
}

fn fill_circle(frame: &mut [u32], center_x: i32, center_y: i32, radius: i32, color: u32) {
    for row in -radius..=radius {
        for col in -radius..=radius {
            if col * col + row * row <= radius * radius {
                put_pixel(frame, center_x + col, center_y + row, color);
            }
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

// Decoration strip for one window: title bar fill, close/maximize
// controls, ported from Desktop/demonwm/demonwm.cc's drawFrame -- drawn as
// a compositor-level overlay above the window's own content rather than a
// second reparented frame window (see this file's own decoration comment).
const CONTROL_DOT_RADIUS: i32 = 6;

fn draw_decoration(frame: &mut [u32], window: &Window, focused: bool) {
    let top = decoration_top(window) as i32;
    let title_color = if focused {
        COLOR_TITLE_FOCUSED
    } else {
        COLOR_TITLE_UNFOCUSED
    };
    fill_rounded_rect_top(frame, window.x as i32, top, window.width, TITLE_HEIGHT, CORNER_RADIUS, title_color);
    let mut title_buffer = [0u8; 24];
    let title_length = window.title_text(&mut title_buffer);
    draw_text(frame, window.x as i32 + 44, top + 8, &title_buffer[..title_length], COLOR_TEXT);

    let control_y = top + TITLE_HEIGHT as i32 / 2;
    // macOS-style traffic lights: solid dots, dimmed (muted grey) when the
    // window isn't focused, matching how real title bar controls fade on
    // an inactive window instead of staying fully saturated.
    let close_color = if focused { COLOR_CONTROL_CLOSE } else { COLOR_TEXT_MUTED };
    let maximize_color = if focused { COLOR_CONTROL_MAXIMIZE } else { COLOR_TEXT_MUTED };
    fill_circle(frame, window.x as i32 + close_control_x(window), control_y, CONTROL_DOT_RADIUS, close_color);
    fill_circle(frame, window.x as i32 + maximize_control_x(window), control_y, CONTROL_DOT_RADIUS, maximize_color);
}

fn push_digit(buffer: &mut [u8; 5], index: &mut usize, value: u32) {
    buffer[*index] = b'0' + (value % 10) as u8;
    *index += 1;
}

// Top panel: launcher button, workspace dots, a real HH:MM clock (see
// syscall 48 / demon_abi::real_time_of_day -- CMOS/RTC time, not kernel
// uptime), and tray placeholders. Ported from demonwm's own drawPanel,
// drawn as a pure overlay with no window-table entry of its own.
fn draw_panel(frame: &mut [u32], current_workspace: u32, launcher_open: bool) {
    let width = SCREEN_WIDTH as i32 - 2 * PANEL_MARGIN;
    draw_shadow(frame, PANEL_MARGIN, PANEL_MARGIN, width as u32, PANEL_HEIGHT);
    blend_rounded_rect(frame, PANEL_MARGIN, PANEL_MARGIN, width as u32, PANEL_HEIGHT, CORNER_RADIUS, COLOR_PANEL_BG, PANEL_BLEND_ALPHA);

    let launcher_color = if launcher_open { COLOR_VIOLET } else { COLOR_EMBER };
    fill_rounded_rect(
        frame,
        LAUNCHER_BTN_X0,
        PANEL_MARGIN + 4,
        (LAUNCHER_BTN_X1 - LAUNCHER_BTN_X0) as u32,
        PANEL_HEIGHT - 8,
        6,
        launcher_color,
    );
    draw_text(frame, LAUNCHER_BTN_X0 + 8, PANEL_MARGIN + 10, b"DEMONOS", COLOR_TEXT);

    // Workspace dots, just left of center.
    let dots_x = PANEL_MARGIN + width / 2 - 30;
    for dot in 0..WORKSPACE_COUNT {
        let color = if dot == current_workspace { COLOR_EMBER } else { COLOR_TEXT_MUTED };
        fill_circle(frame, dots_x + dot as i32 * 10 + 3, PANEL_MARGIN + 15, 3, color);
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

    // Tray placeholders, right-aligned.
    for slot in 0..3 {
        let tray_x = PANEL_MARGIN + width - 12 - slot * 22 - 7;
        fill_circle(frame, tray_x, PANEL_MARGIN + 15, 6, COLOR_TEXT_MUTED);
    }
}

// Bottom taskbar: one pill-shaped entry per open window on the current
// workspace, the focused one highlighted in the accent color. Real, not
// decorative -- see taskbar_item_at's matching hit-test, wired to the
// same focus+raise action clicking the window itself triggers.
fn draw_taskbar(frame: &mut [u32], table: &[Window; WINDOW_LIMIT], workspace: u32, focused_window: u32) {
    let width = SCREEN_WIDTH as i32 - 2 * BOTTOM_BAR_MARGIN;
    draw_shadow(frame, BOTTOM_BAR_MARGIN, BOTTOM_BAR_Y, width as u32, BOTTOM_BAR_HEIGHT);
    blend_rounded_rect(frame, BOTTOM_BAR_MARGIN, BOTTOM_BAR_Y, width as u32, BOTTOM_BAR_HEIGHT, CORNER_RADIUS, COLOR_PANEL_BG, PANEL_BLEND_ALPHA);

    let mut item_x = BOTTOM_BAR_MARGIN + 8;
    for window in table.iter() {
        if !window.in_use || window.workspace != workspace || window.id < DEMONX_WINDOW_ID_BASE {
            continue;
        }
        let focused = window.id == focused_window;
        let item_color = if focused { COLOR_TITLE_FOCUSED } else { COLOR_TASKBAR_ITEM };
        fill_rounded_rect(frame, item_x, BOTTOM_BAR_Y + 6, TASKBAR_ITEM_WIDTH, BOTTOM_BAR_HEIGHT - 12, 8, item_color);
        let mut title_buffer = [0u8; 24];
        let title_length = window.title_text(&mut title_buffer);
        draw_text(frame, item_x + 10, BOTTOM_BAR_Y + 16, &title_buffer[..title_length], COLOR_TEXT);
        item_x += TASKBAR_ITEM_WIDTH as i32 + TASKBAR_ITEM_GAP;
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
    wallpaper: &[u32],
    wallpaper_loaded: bool,
) {
    draw_wallpaper(frame, wallpaper, wallpaper_loaded);
    let mut order: [usize; WINDOW_LIMIT] = [0, 1, 2, 3, 4, 5, 6, 7];
    order.sort_unstable_by_key(|&index| table[index].z);
    for &index in order.iter() {
        let window = table[index];
        if !window.in_use || window.workspace != current_workspace {
            continue;
        }
        if window.id >= DEMONX_WINDOW_ID_BASE {
            draw_shadow(frame, window.x as i32, decoration_top(&window) as i32, window.width, window.height + TITLE_HEIGHT);
        }
        if window.mapped_address != 0 {
            // Blit at most the surface's own real dimensions -- resize only
            // ever changes window.width/height (on-screen placement), never
            // surface_width/height (what the client actually allocated), so
            // this never reads past the mapped surface even mid-resize.
            let copy_width = window.width.min(window.surface_width) as usize;
            let copy_height = window.height.min(window.surface_height) as usize;
            if copy_width < window.width as usize || copy_height < window.height as usize {
                // Resized larger than the surface actually is: pad the
                // uncovered strip with background instead of leaving
                // whatever was there from the previous frame.
                fill_rect(frame, window.x as i32, window.y as i32, window.width, window.height, BACKGROUND_COLOR);
            }
            let pixels = window.surface_width as usize * window.surface_height as usize;
            let source =
                unsafe { core::slice::from_raw_parts(window.mapped_address as *const u32, pixels) };
            for row in 0..copy_height {
                let src_start = row * window.surface_width as usize;
                let dst_start = (window.y as usize + row) * SCREEN_WIDTH as usize + window.x as usize;
                frame[dst_start..dst_start + copy_width]
                    .copy_from_slice(&source[src_start..src_start + copy_width]);
            }
        } else {
            for row in 0..window.height {
                let dst_start = (window.y + row) as usize * SCREEN_WIDTH as usize + window.x as usize;
                frame[dst_start..dst_start + window.width as usize].fill(PLACEHOLDER_COLOR);
            }
        }
        if window.id >= DEMONX_WINDOW_ID_BASE {
            draw_decoration(frame, &window, window.id == focused_window);
        }
    }
    draw_panel(frame, current_workspace, launcher_open);
    draw_taskbar(frame, table, current_workspace, focused_window);
    if launcher_open {
        draw_launcher(frame);
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
    let wallpaper_address = demon_abi::anonymous_map(WALLPAPER_BYTES as u64);
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

    let mut dirty = true;
    let mut next_frame_tick = demon_abi::ticks() + 5;

    demon_abi::write(b"RUST_COMPOSITOR_READY\n");

    loop {
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
                                let address = demon_abi::surface_map(message.surface_id as u64);
                                if address != 0 && address != UINT64_MAX {
                                    table[slot].surface_id = message.surface_id;
                                    table[slot].mapped_address = address;
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
                    demon_abi::display_cursor_move(display, cursor_x as u64, cursor_y as u64, 0);
                    if dragging_id != 0 {
                        if let Some(slot) = find_slot(&table, dragging_id) {
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
                            send_window_event(
                                window.id,
                                WindowOpcode::Pointer,
                                5,
                                (cursor_x - window.x) as i32,
                                (cursor_y - window.y) as i32,
                                0,
                                0,
                            );
                        }
                    }
                    repaint = true;
                }
                k if k == INPUT_MOUSE_BUTTON_DOWN => {
                    cursor_x = (event.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (event.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    if in_launcher_button(cursor_x as i32, cursor_y as i32) {
                        launcher_open = !launcher_open;
                        repaint = true;
                    } else if launcher_open {
                        if let Some(cell) = launcher_cell_at(cursor_x as i32, cursor_y as i32) {
                            if cell == 1 {
                                // ClassiCube: a real full-screen app launch. This
                                // call BLOCKS this entire process until ClassiCube
                                // exits (see demon_abi::launch_foreground's own
                                // comment) -- everything below only runs again
                                // once the game has already quit and control
                                // returns here.
                                let _ = demon_abi::launch_foreground(
                                    b"/system/bin/classicube-core.elf",
                                    CLASSICUBE_SERVICE_MASK,
                                );
                            } else if cell == 2 {
                                // Doom: the one real sustained-play build (see
                                // DOOM_SERVICE_MASK's own comment) -- same
                                // blocking launch_foreground call, just a
                                // longer real session before it returns.
                                let _ = demon_abi::launch_foreground(
                                    b"/system/bin/doom-full.elf",
                                    DOOM_SERVICE_MASK,
                                );
                            }
                            // Cell 0 (Terminal) is deliberately NOT wired to
                            // demon_abi::spawn: spawning a second xterm.elf while
                            // one is already running was tested and reproducibly
                            // took the whole compositor down (compositor_wait
                            // itself started returning an unrecognized value,
                            // hitting RUST_COMPOSITOR_FAIL and a clean exit -- not
                            // a panic in this file's own code). Isolated by
                            // removing the spawn call and repeating the exact
                            // same click sequence, which then reproduced cleanly
                            // with no failure at all, so the trigger is
                            // specifically spawning a second instance of an
                            // already-running process, not the launcher UI
                            // itself. Root cause is in shared kernel IPC/input
                            // wait-state bookkeeping (src/ipc.c's
                            // ipc_wait_select/ipc_process_cleanup, src/input.c's
                            // input_wait), not this compositor -- real,
                            // pre-existing, and worth a dedicated investigation,
                            // but out of scope to guess-fix here.
                        }
                        launcher_open = false;
                        repaint = true;
                    } else if let Some(dot) = workspace_dot_at(cursor_x as i32, cursor_y as i32) {
                        current_workspace = dot;
                        focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                        repaint = true;
                    } else if let Some(id) = taskbar_item_at(&table, current_workspace, cursor_x as i32, cursor_y as i32) {
                        if let Some(slot) = find_slot(&table, id) {
                            focused_window = id;
                            table[slot].z = next_z;
                            next_z += 1;
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
                                // Close: same cleanup WindowOpcode::Close already
                                // does for a client-initiated close.
                                if window.surface_id != 0 {
                                    demon_abi::surface_unmap(window.surface_id as u64);
                                    demon_abi::handle_close(window.surface_id as u64);
                                }
                                table[slot] = Window::EMPTY;
                                if focused_window == window.id {
                                    focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                                }
                            }
                            1 => {
                                if window.maximized {
                                    table[slot].x = window.restore_x;
                                    table[slot].y = window.restore_y;
                                    table[slot].width = window.restore_width;
                                    table[slot].height = window.restore_height;
                                    table[slot].maximized = false;
                                } else {
                                    table[slot].restore_x = window.x;
                                    table[slot].restore_y = window.y;
                                    table[slot].restore_width = window.width;
                                    table[slot].restore_height = window.height;
                                    table[slot].x = 0;
                                    table[slot].y = RESERVED_TOP + TITLE_HEIGHT;
                                    table[slot].width = SCREEN_WIDTH;
                                    table[slot].height = SCREEN_HEIGHT - RESERVED_BOTTOM - table[slot].y;
                                    table[slot].maximized = true;
                                }
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
                            send_window_event(
                                window.id,
                                WindowOpcode::Button,
                                4,
                                (cursor_x - window.x) as i32,
                                (cursor_y - window.y) as i32,
                                1,
                                event.code as u32,
                            );
                        }
                        repaint = true;
                    }
                }
                k if k == INPUT_MOUSE_BUTTON_UP => {
                    cursor_x = (event.x.max(0) as u32).min(SCREEN_WIDTH - 1);
                    cursor_y = (event.y.max(0) as u32).min(SCREEN_HEIGHT - 1);
                    dragging_id = 0;
                    resizing_id = 0;
                    if let Some(slot) = top_slot_at(&table, current_workspace, cursor_x, cursor_y) {
                        let window = table[slot];
                        if window.id >= DEMONX_WINDOW_ID_BASE {
                            send_window_event(
                                window.id,
                                WindowOpcode::Button,
                                6,
                                (cursor_x - window.x) as i32,
                                (cursor_y - window.y) as i32,
                                2,
                                event.code as u32,
                            );
                        }
                    }
                }
                k if k == INPUT_KEY_DOWN || k == INPUT_KEY_UP => {
                    // Ctrl+1/2/3 switches workspaces directly, without ever
                    // reaching the focused client -- scan codes 0x02/0x03/
                    // 0x04 are the physical '1'/'2'/'3' keys regardless of
                    // shift state, matching the panel dots 1:1.
                    if k == INPUT_KEY_DOWN
                        && (event.modifiers & demon_abi::INPUT_MOD_CTRL) != 0
                        && (0x02..=0x04).contains(&event.code)
                    {
                        current_workspace = (event.code - 0x02) as u32;
                        focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                        repaint = true;
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
            composite(&table, frame, focused_window, current_workspace, launcher_open, wallpaper, wallpaper_loaded);
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

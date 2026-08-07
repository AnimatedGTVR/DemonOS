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
const COLOR_TITLE_FOCUSED: u32 = 0xff8b6bff;
const COLOR_TITLE_UNFOCUSED: u32 = 0xff262b33;
const COLOR_TEXT: u32 = 0xfff1f3f5;
const COLOR_TEXT_MUTED: u32 = 0xff9ba3af;
const COLOR_EMBER: u32 = 0xffff5c42;
const COLOR_VIOLET: u32 = 0xff8b6bff;
const COLOR_PANEL_BG: u32 = 0xff1a1d24;

// Panel geometry, ported from demonwm's kMargin/kPanelHeight/
// kLauncherBtnX0/X1 -- drawn as a pure compositor overlay (never a window
// table entry, unlike DemonWM's real 628x28 CREATE'd panel window) since
// it has no client of its own to composite from.
const PANEL_MARGIN: i32 = 6;
const PANEL_HEIGHT: u32 = 28;
const LAUNCHER_BTN_X0: i32 = PANEL_MARGIN + 4;
const LAUNCHER_BTN_X1: i32 = LAUNCHER_BTN_X0 + 72;
const WORKSPACE_COUNT: u32 = 3;

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
    };
}

// A window's true visual footprint including its title bar strip, which
// lives entirely above (x, y) and is never allowed to go above
// RESERVED_TOP (see decoration_hit_at/clamp_drag_y).
fn decoration_top(window: &Window) -> u32 {
    window.y.saturating_sub(TITLE_HEIGHT)
}

fn maximize_control_x(window: &Window) -> u32 {
    window.width - 8 - 2 * CONTROL_SIZE - CONTROL_GAP
}

fn close_control_x(window: &Window) -> u32 {
    window.width - 8 - CONTROL_SIZE
}

// 1 = hit the maximize control, 2 = hit the close control, 0 = neither
// (just an ordinary drag-start point in the title bar).
fn title_control_at(window: &Window, local_x: u32) -> u32 {
    let max_x = maximize_control_x(window);
    if local_x >= max_x && local_x < max_x + CONTROL_SIZE {
        return 1;
    }
    let close_x = close_control_x(window);
    if local_x >= close_x && local_x < close_x + CONTROL_SIZE {
        return 2;
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

fn hits_resize_corner(window: &Window, x: u32, y: u32) -> bool {
    x + RESIZE_GRIP >= window.x + window.width && y + RESIZE_GRIP >= window.y + window.height
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
fn draw_decoration(frame: &mut [u32], window: &Window, focused: bool) {
    let top = decoration_top(window) as i32;
    let title_color = if focused {
        COLOR_TITLE_FOCUSED
    } else {
        COLOR_TITLE_UNFOCUSED
    };
    fill_rect(frame, window.x as i32, top, window.width, TITLE_HEIGHT, title_color);
    draw_text(frame, window.x as i32 + 6, top + 8, b"DEMONOS", COLOR_TEXT);

    let control_y = top + (TITLE_HEIGHT as i32 - CONTROL_SIZE as i32) / 2;
    let max_x = window.x as i32 + maximize_control_x(window) as i32;
    // Maximize/restore: an outlined square (just the four edges).
    fill_rect(frame, max_x, control_y, CONTROL_SIZE, 1, COLOR_TEXT_MUTED);
    fill_rect(frame, max_x, control_y + CONTROL_SIZE as i32 - 1, CONTROL_SIZE, 1, COLOR_TEXT_MUTED);
    fill_rect(frame, max_x, control_y, 1, CONTROL_SIZE, COLOR_TEXT_MUTED);
    fill_rect(frame, max_x + CONTROL_SIZE as i32 - 1, control_y, 1, CONTROL_SIZE, COLOR_TEXT_MUTED);

    let close_x = window.x as i32 + close_control_x(window) as i32;
    fill_rect(frame, close_x, control_y, CONTROL_SIZE, CONTROL_SIZE, COLOR_EMBER);
    // X mark: two diagonals, drawn as single pixels (no line primitive yet).
    for i in 0..CONTROL_SIZE as i32 {
        put_pixel(frame, close_x + i, control_y + i, COLOR_TEXT);
        put_pixel(frame, close_x + CONTROL_SIZE as i32 - 1 - i, control_y + i, COLOR_TEXT);
    }
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
    fill_rect(frame, PANEL_MARGIN, PANEL_MARGIN, width as u32, PANEL_HEIGHT, COLOR_PANEL_BG);

    let launcher_color = if launcher_open { COLOR_VIOLET } else { COLOR_EMBER };
    fill_rect(
        frame,
        LAUNCHER_BTN_X0,
        PANEL_MARGIN + 4,
        (LAUNCHER_BTN_X1 - LAUNCHER_BTN_X0) as u32,
        PANEL_HEIGHT - 8,
        launcher_color,
    );
    draw_text(frame, LAUNCHER_BTN_X0 + 8, PANEL_MARGIN + 10, b"DEMONOS", COLOR_TEXT);

    // Workspace dots, just left of center.
    let dots_x = PANEL_MARGIN + width / 2 - 30;
    for dot in 0..WORKSPACE_COUNT {
        let color = if dot == current_workspace { COLOR_EMBER } else { COLOR_TEXT_MUTED };
        fill_rect(frame, dots_x + dot as i32 * 10, PANEL_MARGIN + 12, 6, 6, color);
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
        let tray_x = PANEL_MARGIN + width - 12 - slot * 22 - 14;
        fill_rect(frame, tray_x, PANEL_MARGIN + 8, 14, 14, COLOR_TEXT_MUTED);
    }
}

// Clear to the desktop background, then paint every live window back to
// front by ascending z -- a real zero-copy blit from its mapped surface, or
// a flat placeholder if it has none mapped, exactly like
// render_demonwm_backend's own two draw paths. Bounds were already enforced
// when the window entered the table (CREATE/MOVE both reject anything that
// would not fit), so every row copy here stays inside `frame`.
fn composite(
    table: &[Window; WINDOW_LIMIT],
    frame: &mut [u32],
    focused_window: u32,
    current_workspace: u32,
    launcher_open: bool,
) {
    frame.fill(BACKGROUND_COLOR);
    let mut order: [usize; WINDOW_LIMIT] = [0, 1, 2, 3, 4, 5, 6, 7];
    order.sort_unstable_by_key(|&index| table[index].z);
    for &index in order.iter() {
        let window = table[index];
        if !window.in_use || window.workspace != current_workspace {
            continue;
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
    let launcher_open: bool = false;
    if demon_abi::display_cursor_move(display, cursor_x as u64, cursor_y as u64, 0) == UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL cursor-init\n");
        demon_abi::exit(1);
    }

    const PIXELS: usize = SCREEN_WIDTH as usize * SCREEN_HEIGHT as usize;
    static mut FRAME: [u32; PIXELS] = [0; PIXELS];

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
                                .min((SCREEN_HEIGHT - height.min(SCREEN_HEIGHT)) as i32);
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
                                .min((SCREEN_HEIGHT - window.y) as i32);
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
                    if let Some(dot) = workspace_dot_at(cursor_x as i32, cursor_y as i32) {
                        current_workspace = dot;
                        focused_window = find_top_slot(&table, current_workspace).map_or(0, |s| table[s].id);
                        repaint = true;
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
                                    table[slot].height = SCREEN_HEIGHT - table[slot].y;
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
            composite(&table, frame, focused_window, current_workspace, launcher_open);
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

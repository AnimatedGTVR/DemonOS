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
    };
}

fn find_free_slot(table: &[Window; WINDOW_LIMIT]) -> Option<usize> {
    table.iter().position(|w| !w.in_use)
}

fn find_slot(table: &[Window; WINDOW_LIMIT], id: u32) -> Option<usize> {
    table.iter().position(|w| w.in_use && w.id == id)
}

fn find_top_slot(table: &[Window; WINDOW_LIMIT]) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| w.in_use)
        .max_by_key(|(_, w)| w.z)
        .map(|(index, _)| index)
}

fn top_slot_at(table: &[Window; WINDOW_LIMIT], x: u32, y: u32) -> Option<usize> {
    table
        .iter()
        .enumerate()
        .filter(|(_, w)| {
            w.in_use && x >= w.x && x < w.x + w.width && y >= w.y && y < w.y + w.height
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

// Clear to the desktop background, then paint every live window back to
// front by ascending z -- a real zero-copy blit from its mapped surface, or
// a flat placeholder if it has none mapped, exactly like
// render_demonwm_backend's own two draw paths. Bounds were already enforced
// when the window entered the table (CREATE/MOVE both reject anything that
// would not fit), so every row copy here stays inside `frame`.
fn composite(table: &[Window; WINDOW_LIMIT], frame: &mut [u32]) {
    frame.fill(BACKGROUND_COLOR);
    let mut order: [usize; WINDOW_LIMIT] = [0, 1, 2, 3, 4, 5, 6, 7];
    order.sort_unstable_by_key(|&index| table[index].z);
    for &index in order.iter() {
        let window = table[index];
        if !window.in_use {
            continue;
        }
        if window.mapped_address != 0 {
            let pixels = window.width as usize * window.height as usize;
            let source =
                unsafe { core::slice::from_raw_parts(window.mapped_address as *const u32, pixels) };
            for row in 0..window.height as usize {
                let src_start = row * window.width as usize;
                let dst_start = (window.y as usize + row) * SCREEN_WIDTH as usize + window.x as usize;
                frame[dst_start..dst_start + window.width as usize]
                    .copy_from_slice(&source[src_start..src_start + window.width as usize]);
            }
        } else {
            for row in 0..window.height {
                let dst_start = (window.y + row) as usize * SCREEN_WIDTH as usize + window.x as usize;
                frame[dst_start..dst_start + window.width as usize].fill(PLACEHOLDER_COLOR);
            }
        }
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
                            focused_window = find_top_slot(&table).map_or(0, |s| table[s].id);
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
                    if let Some(slot) = top_slot_at(&table, cursor_x, cursor_y) {
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
                    if let Some(slot) = top_slot_at(&table, cursor_x, cursor_y) {
                        let window = table[slot];
                        focused_window = window.id;
                        table[slot].z = next_z;
                        next_z += 1;
                        if window.id >= DEMONX_WINDOW_ID_BASE {
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
                    if let Some(slot) = top_slot_at(&table, cursor_x, cursor_y) {
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
                    if focused_window != 0 {
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
            composite(&table, frame);
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

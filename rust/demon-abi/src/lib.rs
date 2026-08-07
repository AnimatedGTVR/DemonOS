//! Real MAKO-ABI syscall bindings and the real window-manager wire protocol,
//! mirrored byte-for-byte from this repo's own C headers:
//!   - include/demon/c_app.h   (raw `int $0x80` syscalls)
//!   - include/kernel/capability.h (capability service ids)
//!   - include/kernel/display.h (display_user_info / display_user_submit)
//!   - include/demon/window.h  (the compositor <-> client wire protocol)
//!
//! This crate is the single shared source of truth for that protocol on the
//! Rust side, the same way c_app.h is the shared source of truth for every
//! C/C++ port. Any Rust binary that needs to be a display client (or, once
//! later stages get there, the compositor itself) depends on this crate
//! instead of re-declaring the syscall numbers and struct layouts locally.
#![no_std]

use core::arch::asm;

// ---------------------------------------------------------------------
// Raw syscalls: `int $0x80`, syscall number in RAX, args in RDI/RSI/RDX/
// R10/R8, return value in RAX. Matches c_app.h's demon_syscall0..5 exactly.
// ---------------------------------------------------------------------

#[inline(always)]
unsafe fn syscall0(number: u64) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result,
        out("rdi") _, out("rsi") _, out("rdx") _, out("r10") _, out("r8") _,
        options(nostack));
    result
}

#[inline(always)]
unsafe fn syscall1(number: u64, first: u64) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result, in("rdi") first,
        out("rsi") _, out("rdx") _, out("r10") _, out("r8") _,
        options(nostack));
    result
}

#[inline(always)]
unsafe fn syscall2(number: u64, first: u64, second: u64) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result, in("rdi") first, in("rsi") second,
        out("rdx") _, out("r10") _, out("r8") _,
        options(nostack));
    result
}

#[inline(always)]
unsafe fn syscall3(number: u64, first: u64, second: u64, third: u64) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result,
        in("rdi") first, in("rsi") second, in("rdx") third,
        out("r10") _, out("r8") _,
        options(nostack));
    result
}

#[inline(always)]
unsafe fn syscall4(number: u64, first: u64, second: u64, third: u64, fourth: u64) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result,
        in("rdi") first, in("rsi") second, in("rdx") third, in("r10") fourth,
        out("r8") _,
        options(nostack));
    result
}

#[inline(always)]
unsafe fn syscall5(
    number: u64,
    first: u64,
    second: u64,
    third: u64,
    fourth: u64,
    fifth: u64,
) -> u64 {
    let mut result = number;
    asm!("int $0x80", inout("rax") result,
        in("rdi") first, in("rsi") second, in("rdx") third, in("r10") fourth, in("r8") fifth,
        options(nostack));
    result
}

pub const UINT64_MAX: u64 = u64::MAX;

pub fn write(bytes: &[u8]) -> u64 {
    unsafe { syscall2(0, bytes.as_ptr() as u64, bytes.len() as u64) }
}
pub fn yield_now() -> u64 {
    unsafe { syscall0(1) }
}
pub fn exit(status: u64) -> ! {
    unsafe {
        syscall1(2, status);
    }
    loop {
        unsafe { asm!("hlt", options(nomem, nostack)) };
    }
}
pub fn ticks() -> u64 {
    unsafe { syscall0(3) }
}
// Real CMOS/RTC wall-clock time, not kernel uptime -- packed as
// (hour << 8) | minute, hour 0-23, minute 0-59. See syscall 48's own
// comment in src/arch/x86_64/userspace.c for why this needs to be a
// syscall at all (CMOS port I/O is privileged, ring 3 cannot read it).
pub fn real_time_of_day() -> u64 {
    unsafe { syscall0(48) }
}
pub fn getpid() -> u64 {
    unsafe { syscall0(4) }
}
pub fn service_open(service: u64) -> u64 {
    unsafe { syscall1(5, service) }
}
pub fn handle_write(handle: u64, bytes: &[u8]) -> u64 {
    unsafe { syscall3(6, handle, bytes.as_ptr() as u64, bytes.len() as u64) }
}
pub fn handle_query(handle: u64, property: u64) -> u64 {
    unsafe { syscall2(7, handle, property) }
}
pub fn handle_close(handle: u64) -> u64 {
    unsafe { syscall1(8, handle) }
}
pub fn file_open(storage: u64, name: &[u8], create: bool) -> u64 {
    unsafe {
        syscall4(
            9,
            storage,
            name.as_ptr() as u64,
            name.len() as u64,
            create as u64,
        )
    }
}
pub fn handle_read(handle: u64, buffer: &mut [u8]) -> u64 {
    unsafe { syscall3(10, handle, buffer.as_mut_ptr() as u64, buffer.len() as u64) }
}
pub fn spawn(path: &[u8], service_mask: u64) -> u64 {
    unsafe { syscall3(11, path.as_ptr() as u64, path.len() as u64, service_mask) }
}
pub fn wait(pid: u64) -> u64 {
    unsafe { syscall1(12, pid) }
}
pub fn channel_create(name: &[u8]) -> u64 {
    unsafe { syscall2(14, name.as_ptr() as u64, name.len() as u64) }
}
pub fn channel_connect(name: &[u8]) -> u64 {
    unsafe { syscall2(15, name.as_ptr() as u64, name.len() as u64) }
}
pub fn channel_send(channel: u64, message: &[u8]) -> u64 {
    unsafe { syscall3(16, channel, message.as_ptr() as u64, message.len() as u64) }
}
pub fn channel_receive(channel: u64, buffer: &mut [u8], nonblocking: bool) -> u64 {
    unsafe {
        syscall4(
            17,
            channel,
            buffer.as_mut_ptr() as u64,
            buffer.len() as u64,
            nonblocking as u64,
        )
    }
}
pub fn display_info(display: u64, destination: &mut DisplayInfo) -> u64 {
    unsafe { syscall2(18, display, destination as *mut DisplayInfo as u64) }
}
pub fn display_submit(display: u64, request: &DisplaySubmit) -> u64 {
    unsafe { syscall2(19, display, request as *const DisplaySubmit as u64) }
}
pub fn input_poll(handle: u64, event: &mut InputEvent) -> u64 {
    unsafe { syscall2(20, handle, event as *mut InputEvent as u64) }
}
pub fn display_dimensions(display: u64) -> u64 {
    unsafe { syscall1(21, display) }
}
pub fn surface_create(factory: u64, width: u64, height: u64) -> u64 {
    unsafe { syscall3(22, factory, width, height) }
}
pub fn surface_write(surface: u64, pixels: &[u32], offset: u64) -> u64 {
    unsafe {
        syscall4(
            23,
            surface,
            pixels.as_ptr() as u64,
            pixels.len() as u64,
            offset,
        )
    }
}
pub fn surface_share(surface: u64, channel: u64) -> u64 {
    unsafe { syscall2(24, surface, channel) }
}
pub fn surface_damage(surface: u64, x: u64, y: u64, width: u64, height: u64) -> u64 {
    unsafe { syscall5(28, surface, x, y, width, height) }
}
pub fn compositor_wait(
    channel: u64,
    input_handle: u64,
    message_destination: &mut WindowMessage,
    event_destination: &mut InputEvent,
    timeout_ticks: u64,
) -> u64 {
    unsafe {
        syscall5(
            26,
            channel,
            input_handle,
            message_destination as *mut WindowMessage as u64,
            event_destination as *mut InputEvent as u64,
            timeout_ticks,
        )
    }
}
pub fn surface_map(surface: u64) -> u64 {
    unsafe { syscall1(27, surface) }
}
pub fn surface_take_damage(surface: u64) -> u64 {
    unsafe { syscall1(29, surface) }
}
pub fn surface_unmap(surface: u64) -> u64 {
    unsafe { syscall1(30, surface) }
}
pub fn compositor_report(display: u64, window_count: u64, focused_window_id: u64) -> u64 {
    unsafe { syscall3(31, display, window_count, focused_window_id) }
}
pub fn display_cursor_move(display: u64, x: u64, y: u64, icon: u64) -> u64 {
    unsafe { syscall4(32, display, x, y, icon) }
}

// ---------------------------------------------------------------------
// Capability services (include/kernel/capability.h).
// ---------------------------------------------------------------------

#[repr(u64)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CapabilityService {
    Console = 1,
    Clock = 2,
    Process = 3,
    Storage = 4,
    File = 5,
    Ipc = 6,
    Display = 7,
    Input = 8,
    Surface = 9,
    Network = 10,
    Audio = 11,
}

// ---------------------------------------------------------------------
// Display syscalls' plain-old-data structs (include/kernel/display.h).
// repr(C) + explicit field order/types matching the real layout exactly.
// ---------------------------------------------------------------------

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DisplayInfo {
    pub width: u64,
    pub height: u64,
    pub stride_pixels: u64,
    pub format: u64,
    pub max_transfer_pixels: u64,
}

pub const DISPLAY_SUBMIT_PRESENT: u64 = 1;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DisplaySubmit {
    pub x: u64,
    pub y: u64,
    pub width: u64,
    pub height: u64,
    pub pixels: u64,
    pub flags: u64,
}

// ---------------------------------------------------------------------
// Real unified input event, mirrored field-for-field from the real struct
// in include/demon/input.h (NOT guessed -- verified against that header).
// ---------------------------------------------------------------------

pub const INPUT_KEY_DOWN: u16 = 1;
pub const INPUT_KEY_UP: u16 = 2;
pub const INPUT_MOUSE_MOVE: u16 = 3;
pub const INPUT_MOUSE_BUTTON_DOWN: u16 = 4;
pub const INPUT_MOUSE_BUTTON_UP: u16 = 5;
pub const INPUT_MOUSE_SCROLL: u16 = 6;
pub const INPUT_MOD_CTRL: u32 = 1 << 4;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct InputEvent {
    pub timestamp: u64,
    pub kind: u16,
    pub code: u16,
    pub x: i32,
    pub y: i32,
    pub delta_x: i32,
    pub delta_y: i32,
    pub value: i32,
    pub modifiers: u32,
}

const _: () = assert!(core::mem::size_of::<InputEvent>() == 40);

// ---------------------------------------------------------------------
// Real window-manager wire protocol (include/demon/window.h). Every
// control packet fits one 64-byte kernel IPC message; pixel payloads stay
// out-of-band in capability-authorized read-only surface mappings.
// ---------------------------------------------------------------------

pub const WINDOW_PROTOCOL_VERSION: u16 = 1;
pub const WINDOW_SERVICE: &[u8] = b"desktop.compositor";
pub const WINDOW_TITLE_MAX: usize = 31;

#[repr(u16)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum WindowOpcode {
    Hello = 1,
    Create = 2,
    Created = 3,
    Damage = 4,
    Present = 5,
    Focus = 6,
    Pointer = 7,
    Key = 8,
    Close = 9,
    Move = 10,
    Button = 11,
    PointerWarp = 12,
}

pub const WINDOW_RESIZABLE: u32 = 1 << 0;
pub const WINDOW_MAXIMIZED: u32 = 1 << 1;
pub const WINDOW_FULLSCREEN: u32 = 1 << 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WindowMessage {
    pub version: u16,
    pub opcode: u16,
    pub serial: u32,
    pub window_id: u32,
    pub flags: u32,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub surface_id: u32,
    pub payload_length: u32,
    pub payload: [u8; 24],
}

impl Default for WindowMessage {
    fn default() -> Self {
        WindowMessage {
            version: WINDOW_PROTOCOL_VERSION,
            opcode: 0,
            serial: 0,
            window_id: 0,
            flags: 0,
            x: 0,
            y: 0,
            width: 0,
            height: 0,
            surface_id: 0,
            payload_length: 0,
            payload: [0; 24],
        }
    }
}

// Real compile-time proof this matches the C struct's real, wire-verified
// size -- the same static_assert window.h itself carries.
const _: () = assert!(core::mem::size_of::<WindowMessage>() == 64);

// ---------------------------------------------------------------------
// DemonX bridge constants, mirrored from user/demonx_server.c (the fixed
// channel it listens for native input forwarding on) and from every X11
// window's id namespace as DemonX itself assigns it (client 1's first XID
// is 0x200000 == 2097152; DemonWM is always client 2, whose first XID is
// 0x200001 == 2097153 and doubles as the fixed proxy target for clicks on
// its own top panel strip, which is not a tracked compositor window).
// Neither constant is derived from anything the compositor negotiates at
// runtime -- both sides simply agree on them ahead of time, the same way
// WINDOW_SERVICE is agreed on ahead of time instead of being discovered.
// ---------------------------------------------------------------------

pub const DEMONX_DISPLAY_CHANNEL: &[u8] = b"demonx.display.0";
pub const DEMONX_WINDOW_ID_BASE: u32 = 2_097_152;
pub const DEMONWM_PANEL_PROXY_ID: u32 = 2_097_153;

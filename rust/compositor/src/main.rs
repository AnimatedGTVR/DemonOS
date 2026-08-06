#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use demon_abi::{CapabilityService, DisplayInfo, DisplaySubmit, DISPLAY_SUBMIT_PRESENT};

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    demon_abi::write(b"RUST_COMPOSITOR_PANIC\n");
    demon_abi::exit(1);
}

// A fixed-capacity static frame buffer: this stage doesn't have an
// allocator yet (no_std, no alloc crate), and Rust's own writable statics
// work fine in this port's real .bss segment (rust/linker.ld mirrors
// ports/nxengine/linker.ld's real RWX PT_LOAD layout) -- no different from
// how the C ports use plain globals. 640x480 matches every real boot this
// kernel has reported (FRAMEBUFFER_DETECTED 640x480) throughout this whole
// project; if a real display ever reports something bigger, this stage
// honestly fails closed below rather than silently drawing a wrong-sized
// frame.
const MAX_PIXELS: usize = 640 * 480;
static mut FRAME_BUFFER: [u32; MAX_PIXELS] = [0; MAX_PIXELS];

#[no_mangle]
pub extern "C" fn rust_main() -> ! {
    demon_abi::write(b"RUST_COMPOSITOR_START\n");

    let display = demon_abi::service_open(CapabilityService::Display as u64);
    if display == demon_abi::UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-open\n");
        demon_abi::exit(1);
    }

    let mut info = DisplayInfo::default();
    if demon_abi::display_info(display, &mut info) == demon_abi::UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-info\n");
        demon_abi::exit(1);
    }

    let pixel_count = (info.width * info.height) as usize;
    if pixel_count == 0 || pixel_count > MAX_PIXELS {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-size-out-of-bounds\n");
        demon_abi::exit(1);
    }

    // Real opaque solid-blue frame (format 1 = 32-bit ARGB, matching
    // display_describe's real convention and src/framebuffer.c's blend()
    // alpha==255 fast path). Proves this Rust binary can actually drive
    // the real display syscalls end to end -- later stages replace this
    // flat fill with real window compositing.
    const OPAQUE_BLUE: u32 = 0xFF0000FFu32;
    unsafe {
        let buffer = &mut *core::ptr::addr_of_mut!(FRAME_BUFFER);
        for pixel in buffer.iter_mut().take(pixel_count) {
            *pixel = OPAQUE_BLUE;
        }
    }

    let request = unsafe {
        let buffer = &*core::ptr::addr_of!(FRAME_BUFFER);
        DisplaySubmit {
            x: 0,
            y: 0,
            width: info.width,
            height: info.height,
            pixels: buffer.as_ptr() as u64,
            flags: DISPLAY_SUBMIT_PRESENT,
        }
    };

    if demon_abi::display_submit(display, &request) == demon_abi::UINT64_MAX {
        demon_abi::write(b"RUST_COMPOSITOR_FAIL display-submit\n");
        demon_abi::exit(1);
    }

    demon_abi::write(b"RUST_COMPOSITOR_FRAME_OK\n");
    demon_abi::handle_close(display);
    demon_abi::exit(0);
}

// Same real MAKO-ABI entry shape as rust/hello -- see that crate's comment
// for why `_start` stays raw assembly.
global_asm!(
    ".global _start",
    "_start:",
    "call rust_main",
    "1: hlt",
    "jmp 1b",
);

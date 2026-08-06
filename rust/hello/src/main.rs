#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    demon_abi::write(b"RUST_HELLO_PANIC\n");
    demon_abi::exit(1);
}

#[no_mangle]
pub extern "C" fn rust_main() -> ! {
    // Real proof this binary is using the shared demon-abi crate for its
    // syscalls (Stage 1), not just a standalone hand-rolled `int $0x80` --
    // both the raw write/exit calls and the WindowMessage/DisplayInfo
    // layouts' compile-time size assertions come from that crate now.
    demon_abi::write(b"RUST_HELLO_READY toolchain=nightly target=x86_64-unknown-none-elf crate=demon-abi\n");
    demon_abi::exit(0);
}

// Real MAKO-ABI entry point: RDI holds pid, stack is already a private,
// 16-byte-aligned user stack (see docs/native-porting.md's "MAKO-ABI 0.1"
// dump) -- nothing to set up before jumping into safe Rust. `_start` stays
// raw assembly (no ordinary fn prologue) since a normal `extern "C" fn`
// entry point can't be guaranteed against an implicit stack-alignment
// assumption the same way the C ports' own entry.S is written by hand.
global_asm!(
    ".global _start",
    "_start:",
    "call rust_main",
    "1: hlt",
    "jmp 1b",
);

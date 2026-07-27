# Native Framebuffer Stage 1

Stage 1 consumes the Multiboot2 linear-framebuffer tag and keeps VGA text plus
COM1 serial as the fallback path. The Multiboot2 header requests 640×480×32 but
marks the request optional, so unsupported firmware does not make the kernel
unbootable.

## Validated mode contract

The kernel accepts RGB linear modes only when all of the following hold:

- 24 or 32 bits per pixel;
- width and height are nonzero and no larger than 640×480;
- pitch is at least `width * bytes_per_pixel` and no more than 16 KiB;
- RGB masks are present and no wider than eight bits;
- address and `pitch * height` arithmetic does not overflow;
- the framebuffer occupies one 1-GiB page-directory region below 512 GiB.

`struct framebuffer_info` records the physical/identity-virtual address, width,
height, pitch, bpp, Multiboot pixel-format type, and each RGB channel position
and mask width. Pitch is always used directly; rendering never assumes
`width * 4` scanlines.

## Mapping and ownership

The existing kernel page-table root receives supervisor-only writable 2-MiB
identity mappings covering the framebuffer physical range. A dedicated page
directory is allocated when the device lies outside the low 1-GiB region (QEMU
places it at `0xFD000000`). Reloading CR3 makes the new mappings visible.

The physical-frame allocator supplies one contiguous ARGB32 software
backbuffer. Allocation is resolution-sized and capped at 1,228,800 bytes. No
backbuffer is allocated on fallback boots. Stage 1 is kernel-owned; userspace
receives no raw framebuffer pointer. A later display capability will transfer
exclusive device ownership to the ring-3 compositor.

## Rendering API

All access goes through [`framebuffer.h`](../include/kernel/framebuffer.h):

- clipped pixel writes, rectangle fill/border, and Bresenham lines;
- bounded ARGB bitmap blitting with alpha blending;
- compact bitmap text rendering;
- framebuffer clearing;
- one unioned dirty rectangle;
- conversion from internal ARGB32 to firmware RGB masks;
- pitch-aware 24/32-bit presentation.

Invalid coordinates are clipped or ignored. The boot self-test clears the
backbuffer, draws a rectangle partially outside the upper-left edge, attempts an
out-of-bounds pixel, and verifies both affected and unaffected pixels before
reporting `FRAMEBUFFER_PRIMITIVES_OK`.

## Graphical boot diagnostic

The boot diagnostic is rendered by the live kernel after all existing boot
self-tests. It displays the DemonOS wordmark, detected resolution, pitch, bpp,
RGB mask widths, kernel implementation identity, gradient, borders, lines,
rectangles, and bitmap text. `GRAPHICAL_BOOT_TEST_OK` is emitted only after the
dirty backbuffer is presented.

This is a hardware/graphics diagnostic, not a desktop mockup. Mouse input,
dynamic spawn, IPC, and the userspace compositor remain subsequent stages.

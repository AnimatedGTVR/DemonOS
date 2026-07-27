# Display submission address-space invariant

The compositor remains an ordinary ring-3 process in system RAM. Its code is
mapped at `0x300000`, its executable window ends at `0x31E000` (raised from
`0x318000` when USERSPACE_CODE_PAGES grew from 24 to 30 pages -- see the
comment on that constant in `kernel/userspace.h`), its heap starts there,
and neither mapping is shared with the framebuffer.

## Root cause of the `0x300ff8` corruption

The physical software backbuffer occupied a low-memory range that included
physical address `0x300ff8`. Kernel framebuffer code treated that physical
address as an identity-mapped pointer. During syscall 19 (`display_submit`),
however, PID 3's CR3 was still active. PID 3 deliberately maps virtual
`0x300000` to its private executable frames, so the otherwise-valid
backbuffer write resolved to compositor code rather than to the identity
mapped backbuffer frame.

A hardware write watchpoint caught the first write in ring 0. The addresses
below are the historical capture from before the heap moved to `0x318000`:

- instruction: `framebuffer_blit+754: mov %eax,(%rbp)`;
- destination: `RBP=0x300ff8`;
- pixel source: `R8=0x310080`;
- submitted width: `R10=640`;
- pixel value: `EAX=0xff1f2937`;
- active address space: compositor CR3 `0x352000`;
- privilege: `CS=0x8` (ring 0).

Under that captured CR3, virtual `0x300000` mapped to compositor code frame
`0x35c000`, so the watched virtual destination resolved to physical
`0x35cff8`. The blitter intended physical backbuffer byte `0x300ff8` through
the kernel identity map. The source virtual address `0x310080` mapped to the
compositor heap frame beginning at physical `0x36c000`.

The rectangle bounds and destination index were valid. The defect was the
address space in which the kernel dereferenced the low physical pointer.

## Enforced boundary

`display_submit_from_user` validates the complete request and copies at most
one 640-pixel scanline at a time into a fixed kernel buffer while the caller's
CR3 is active. It then switches to the kernel CR3 for both
`framebuffer_blit` and `framebuffer_present`, and restores the caller's CR3
before touching user memory again. Intermediate chunks never present; the
original present flag is applied only to the final chunk.

This is not a RAM-layout workaround. It is the required separation between
user-pointer access and kernel physical-buffer access.

At boot, the kernel hashes every byte in the compositor's executable pages
before the first frame and checks the same pages after the complete dynamic
window/cursor/launcher workload and after the final desktop repaint. A change
is fatal; successful runs emit `COMPOSITOR_CODE_IMMUTABLE_OK`.

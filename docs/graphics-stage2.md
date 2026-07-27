# Native graphics Stage 2

Stage 2 adds a hardware-independent, freestanding ARGB software-rendering library in
`libs/graphics`. It draws only into caller-owned memory and has no framebuffer, allocator,
architecture, or kernel-global dependency. The framebuffer remains a presentation backend.

Implemented primitives:

- clipped pixels, filled rectangles, and Bresenham lines;
- vertical gradients and source-alpha compositing;
- rounded rectangles, rounded borders, and bounded soft shadows;
- alpha image blits with independent source stride;
- compact bitmap text;
- per-surface clip rectangles and unioned damage tracking.

Every surface carries a pixel pointer, dimensions, pixel stride, clip, and damage state.
There is no heap allocation and shadow spread is capped, keeping time and memory bounded.
Opaque spans bypass alpha arithmetic, lines batch their damage update, standard 32-bit RGB
framebuffers use a direct presentation path, and shadows use three bounded layers rather
than work proportional to every spread pixel.
The kernel runs the same renderer against a 16x16 ordinary memory surface before using it
for the graphical boot diagnostic. This tests clipping, rounded geometry, damage, and alpha
without relying on display hardware.

This is still a graphics foundation, not a desktop compositor. Input routing, windows,
process isolation, and IPC remain later stages.

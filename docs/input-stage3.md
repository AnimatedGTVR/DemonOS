# Native input Stage 3

Stage 3 adds a PS/2 auxiliary-device driver and a device-independent input ABI.
Desktop software never receives raw PS/2 bytes.

The kernel now:

- initializes the second PS/2 controller port and IRQ12;
- negotiates IntelliMouse wheel packets when the emulated device supports them;
- synchronizes and decodes three- or four-byte packets;
- clamps an absolute pointer position to configured display bounds;
- configures 1:1 device scaling and a 200 Hz sample rate while retaining the
  emulator-compatible default resolution;
- applies a deterministic 1x/2x/3x movement curve for precision, ordinary
  travel, and quick screen crossing;
- validates packet sign bits so a dropped byte cannot become a large jump;
- drains complete queued auxiliary-byte bursts per IRQ, preventing a busy
  compositor or terminal startup from stranding a partial packet without a
  subsequent interrupt edge;
- emits relative movement, button press/release, and scroll events;
- emits keyboard press/release events while retaining the console character queue;
- preserves E0-prefixed arrow/Super key identities and tracks Alt and Super as
  first-class modifiers for compositor-owned desktop shortcuts;
- timestamps events from the 100 Hz kernel timer;
- stores fallback events in a bounded 128-entry queue and counts overflow drops;
- directly wakes a validated ring-3 input waiter and counts userspace deliveries;
- coalesces adjacent queued pointer-motion records while preserving every
  intervening key, button, and scroll ordering boundary.

`struct input_event` in `include/demon/input.h` is the common ABI. Its event types
cover key down/up, pointer movement, three mouse buttons, and scrolling. The queue
uses fixed storage and performs no allocation in interrupt context.

The compositor atomically blocks on unified input and its IPC channel. An input
IRQ copies one event to its validated destination, cancels the IPC half of the
wait, and wakes it exactly once. MakoBox resumes that ready task and regains the
CPU when the compositor blocks again.

The `mouse-smoke` target injects movement through QEMU's emulated PS/2 mouse and
asserts real IRQ12 delivery, packet decoding, pointer movement, a successful
userspace delivery, a drained queue, zero dropped events, and a tightly bounded
cursor-only framebuffer change rather than a full-scene flash. Boot also submits
two adjacent resize motions and verifies that one is collapsed while the final
position is rendered exactly once.

For interactive testing, `make run` forces QEMU GTK through XWayland when the
host session is Wayland. This gives the emulated relative PS/2 device a real
pointer grab; native GTK/Wayland can otherwise deliver one slow entry movement
and then leave the guest cursor stationary while the host cursor moves.

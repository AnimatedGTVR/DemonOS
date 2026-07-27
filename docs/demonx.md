# DemonX: native X11 compatibility service

DemonX is a clean, native C/MAKO implementation of an X11-compatible display
service for DemonOS. It is not the upstream X.Org Server and does not depend on
POSIX sockets, `fork`, device files, or a hosted C library.

The freestanding C server owns protocol parsing and resource validation. The
first MAKO client sends byte-for-byte little-endian X11 setup and core requests
inside fixed 64-byte capability-IPC records. The 16-byte `D11X` transport header
contains payload length, flags, client id, and sequence; the remaining 48 bytes
contain unmodified X11 wire data. Larger requests will use continuation records
in the next stage.

The initial boot-tested subset implements:

- X11 11.0 little-endian setup negotiation;
- per-client resource IDs with base `0x00200000` and mask `0x001fffff`;
- a synthetic root window (`0x100`);
- `CreateWindow`, `MapWindow`, `GetGeometry`, and `DestroyWindow`;
- 16 bounded server-side window resources;
- X11-style 32-byte replies and protocol error records;
- server survival after its test client exits.

Next protocol work is event masks and delivery, properties/atoms, graphics
contexts, pixmaps, `PutImage`, and compositor surface publication. The native
compositor remains the sole owner of framebuffer presentation.

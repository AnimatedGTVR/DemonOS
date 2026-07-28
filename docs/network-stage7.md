# Stage 7 networking

DemonOS now has an allocation-free kernel packet core:

- validated Ethernet II receive framing,
- a four-entry bounded ARP cache,
- IPv4 header-length and total-length validation,
- Internet-header checksum generation and verification,
- explicit `no device`, `link down`, and `link up` states, and
- counters for accepted and rejected frames.

PCI configuration-space enumeration discovers all functions and records class,
identity, BAR, and interrupt data in a fixed 32-device table. QEMU's Ethernet
controller is reported at boot, allowing the driver target to be selected from
observed hardware rather than assumed.

The default QEMU configuration currently exposes an Intel 82540EM E1000
controller (`8086:100e`) at `00:03.0`, IRQ 11. BAR mapping, reset, MAC
discovery, eight-entry RX/TX DMA rings, and polling receive/transmit are
working. DemonOS performs the complete DHCP Discover, Offer, Request, and ACK
exchange, installs the leased address, resolves the leased gateway through
ARP, and sends a real UDP DNS A query through that gateway. It then completes
a checksum-validated TCP three-way handshake with the resolved host, sends an
HTTP/1.0 request, validates the returned HTTP status line, and acknowledges
the response bytes. The smoke test requires all of those replies to arrive
through the hardware receive ring.

HTTP is exposed to Demon Web through a capability-gated, bounded userspace
ABI. The browser opens only the NETWORK service it was assigned. Entering an
`http://host/path` address performs a fresh DNS query, TCP handshake, and HTTP
GET, copies at most 512 body bytes into the browser's writable heap, and
renders the result in its native surface. Host, URL, request, frame, and
response storage all have fixed upper bounds.

The runtime client retries DNS, SYN, and HTTP request transmissions up to
three times, preserves consecutive in-order TCP response segments in a bounded
4 KiB queue, acknowledges the received sequence range, and queues an orderly
FIN/ACK close. This completes the lightweight plain-HTTP networking milestone.

E1000 interrupt-driven receive, HTTPS/TLS, congestion control, and a broader
HTML/CSS engine are future enhancements rather than blockers for the desktop
remake. If the VM configuration moves to VirtIO-net, the same packet core
remains reusable behind that driver.

All packet parsing uses fixed-size state and checked lengths. Malformed frames
are dropped before protocol fields are consumed, keeping the networking path
compatible with the kernel's lightweight memory goals.

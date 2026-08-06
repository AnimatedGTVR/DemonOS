#ifndef KERNEL_USB_UHCI_H
#define KERNEL_USB_UHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A from-scratch UHCI (USB 1.1) host controller driver, scoped narrowly to
// one job: find QEMU's emulated usb-tablet device and feed its absolute
// pointer reports into the same input_publish pipeline the PS/2 mouse uses
// (see mouse_report_absolute in interrupts.h). This is not a general USB
// stack -- there is no descriptor parsing, no hub support, no bulk/isochronous
// transfers, and no support for any other device class. It exists because
// PS/2's relative-motion protocol requires the host to grab the pointer
// (see every run-*/QEMU target's own "Ctrl+Alt+G releases it" comment);
// a USB tablet reports true absolute position, so QEMU's own idea of where
// the pointer is and this kernel's idea of where it is never have to be
// reconciled through a grab at all.
//
// dma_arena/arena_size follow the exact same convention as e1000_start/
// ahci_start (src/e1000.c, src/ahci.c): the caller (kernel.c) allocates a
// physically contiguous, page-aligned block via allocate_contiguous and
// hands it over untouched. Physical memory is identity-mapped in this
// kernel (no phys->virt translation exists anywhere), so the returned
// address is used directly as a pointer.
// screen_width/screen_height must match whatever mouse_set_bounds (see
// interrupts.h) was already called with -- the tablet's raw 0..0x7FFF axis
// reports are rescaled into that same coordinate space before being handed
// to mouse_report_absolute.
bool usb_uhci_start(uintptr_t dma_arena, size_t arena_size,
                    uint32_t screen_width, uint32_t screen_height);

// Polls the recurring interrupt transfer for a new HID report and, if one
// completed since the last call, rescales it and calls
// mouse_report_absolute. Cheap enough to call from the timer tick (see
// drain_mouse_bytes's own polling precedent in src/arch/x86_64/interrupts.c)
// -- a no-op if usb_uhci_start was never called or found no device.
void usb_uhci_poll(void);

bool usb_uhci_present(void);

#endif

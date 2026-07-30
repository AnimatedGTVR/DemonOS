CC := gcc
CXX := g++
LD := ld
STRIP := strip
# MAKO is a plain .NET console project and does not use optional workloads.
# Disabling the workload resolver keeps builds reproducible on hosts whose
# package manager has removed stale workload manifests from an older SDK.
export MSBuildEnableWorkloadResolver := false

BUILD := build
ISO_ROOT := $(BUILD)/iso
KERNEL := $(BUILD)/kernel.elf
# $(ISO) boots the scripted "boottest" GRUB entry (grub/grub-test.cfg):
# every smoke/check/*-smoke target depends on this responding immediately
# with no loading/login screens, since they assert exact frame counts and
# timing (see multiboot_test_mode in kernel.c, checked once at boot and
# read by the compositor via MakoAbi.boot_test_mode()). Real interactive
# use (`make run`) needs the opposite -- a human actually seeing and
# reading those screens -- so it uses the separate $(RUN_ISO) below,
# built from the exact same staged files but grub/grub.cfg's plain
# default entry (no boottest flag) instead.
ISO := $(BUILD)/kernel.iso
RUN_ISO := $(BUILD)/kernel-run.iso
USER_ELF := $(BUILD)/user_program.elf
PORTABLE_ELF := $(BUILD)/portable_hello.elf
COMPOSITOR_ELF := $(BUILD)/compositor.elf
# EDDE (Equinox Desktop DemonOS Environment): a separately-buildable fork of
# the compositor/desktop shell (user/compositor_edde.mko, Desktop/desktop_edde.mko),
# selectable as its own GRUB boot entry. See compositor_edde.mko's header
# comment for why it's a fork rather than an in-place edit.
EDDE_COMPOSITOR_ELF := $(BUILD)/compositor_edde.elf
WINDOW_CLIENT_ELF := $(BUILD)/window_client.elf
WINDOW_MOVE_CLIENT_ELF := $(BUILD)/window_move_client.elf
WINDOW_EVENT_CLIENT_ELF := $(BUILD)/window_event_client.elf
WINDOW_CRASH_CLIENT_ELF := $(BUILD)/window_crash_client.elf
DEMONX_ELF := $(BUILD)/demonx.elf
DEMONX_CLIENT_ELF := $(BUILD)/demonx_client.elf
DEMONX_XLIB_TEST_ELF := $(BUILD)/demonx-xlib-test.elf
TETRIS_ELF := $(BUILD)/tetris.elf
CALCULATOR_ELF := $(BUILD)/calculator.elf
TERMINAL_CLIENT_ELF := $(BUILD)/terminal_client.elf
COUNTER_CLIENT_ELF := $(BUILD)/counter_client.elf
BROWSER_CLIENT_ELF := $(BUILD)/browser_client.elf
# Wave 0 of the EDE port (see Desktop/EDE/ede-2.1 and docs/c-apps.md's "C
# runtime boundary" section): proves a real C++ app -- heap allocation,
# vtables, a virtual destructor -- can build and run under MAKO-ABI at all,
# before any actual EDE component is attempted.
CXX_HELLO_ELF := $(BUILD)/cxx_hello.elf
# Wave 1 of the EDE port: a real ede-calc, ported off SciCalc.cpp's FLTK
# widget tree onto this kernel's own window/surface/graphics primitives
# (see apps/ede_calc/). Needs actual `double` arithmetic, which is what
# drove the FPU/SSE + FXSAVE/FXRSTOR scheduler work (src/scheduler.c) and
# the FLOAT_CFLAGS/FLOAT_CXXFLAGS variants below -- kernel.c CFLAGS keep
# -mno-sse/-msoft-float since GCC refuses `double`-returning functions
# under those flags on x86_64 (soft-float there is calling-convention-
# incompatible, not just slower), so anything using real floats needs SSE2
# actually enabled instead.
EDE_CALC_ELF := $(BUILD)/ede_calc.elf
# Dlib (lib/dlib/): an Xlib-API-compatible shim over the real compositor
# window protocol (window.h's demon_window_message), NOT over demonx_server
# .c's protocol-conformance test harness (that one never reaches the real
# display -- see lib/dlib/dlib.h's own header comment). dlib_hello is a
# real, minimal end-to-end proof: creates an actual on-screen window,
# draws into it with Xlib-named calls, presents it, and waits for a real
# close event. First slice only -- window lifecycle plus a minimal GC/
# drawing subset, a small fraction of the ~158 distinct Xlib functions
# PekWM's own source calls.
DLIB_HELLO_ELF := $(BUILD)/dlib_hello.elf
# Wave 2 of the EDE port: ede-about (apps/ede_about) -- a static credits/
# about window, no floating point anywhere in it, so unlike ede-calc it
# builds with the kernel's normal CFLAGS/ASFLAGS, not FLOAT_CFLAGS.
EDE_ABOUT_ELF := $(BUILD)/ede_about.elf
# Wave 3 of the EDE port: ede-tip (apps/ede_tip) -- prev/next/close tip
# cycler. Also plain C, no floating point.
EDE_TIP_ELF := $(BUILD)/ede_tip.elf
# Wave 4 of the EDE port: ede-preferred-applications (apps/ede_preferred) --
# a basic-vs-scientific calculator picker, the one role this OS actually
# has two installed alternatives for. Plain C, no floating point.
EDE_PREFERRED_ELF := $(BUILD)/ede_preferred.elf
# Wave 5 of the EDE port: ede-conf (apps/ede_conf) -- a real control panel
# that spawns other apps via the newly-added demon_spawn() wrapper.
EDE_CONF_ELF := $(BUILD)/ede_conf.elf
# Wave 7 of the EDE port: ede-autostart (apps/ede_autostart) -- toggle
# checklist with real RAMFS persistence, launched from ede-conf's 4th slot.
EDE_AUTOSTART_ELF := $(BUILD)/ede_autostart.elf
# Wave 8 of the EDE port: ede-timedate (apps/ede_timedate) -- scaled down to
# a real system-uptime readout (demon_ticks()), since no RTC/timezone/NTP
# backend exists for the original's full clock+calendar dialog.
EDE_TIMEDATE_ELF := $(BUILD)/ede_timedate.elf
MAKO_REPO := ../MAKO
MAKO_SOURCE_ARCHIVE := $(BUILD)/MAKO-source.tar.zst
MAKO_MANIFEST := $(BUILD)/mako-manifest.txt

CFLAGS := -std=c11 -Os -g -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections \
	-m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Iinclude
ASFLAGS := -g -ffreestanding -fno-pie -fno-pic -m64
LDFLAGS := -nostdlib --gc-sections -z max-page-size=0x1000 -T linker.ld
USER_LDFLAGS := -nostdlib -z max-page-size=0x10 -T user/linker.ld

OBJECTS := $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/serial.o $(BUILD)/terminal.o \
	$(BUILD)/interrupt_stubs.o $(BUILD)/interrupts.o $(BUILD)/pci.o $(BUILD)/input.o $(BUILD)/graphics.o $(BUILD)/mouse_argb.o $(BUILD)/wallpaper_argb.o $(BUILD)/cursor_icons_argb.o $(BUILD)/ui_icons_argb.o $(BUILD)/start_logo_argb.o $(BUILD)/shell_icons_argb.o $(BUILD)/assets.o $(BUILD)/framebuffer.o $(BUILD)/makobox.o $(BUILD)/kernel_probe.o

OBJECTS += $(BUILD)/scheduler.o $(BUILD)/userspace.o $(BUILD)/elf64.o \
	$(BUILD)/capability.o \
	$(BUILD)/ipc.o $(BUILD)/display.o $(BUILD)/surface.o $(BUILD)/network.o $(BUILD)/http.o $(BUILD)/e1000.o $(BUILD)/ahci.o $(BUILD)/init.o $(BUILD)/runas.o $(BUILD)/apps.o $(BUILD)/git.o \
	$(BUILD)/ramfs.o $(BUILD)/acpi.o \
	$(BUILD)/user_program_blob.o

.PHONY: all iso iso-check project qemu run run-wayland run-sdl run-vnc smoke framebuffer-smoke framebuffer-fallback-smoke keyboard-smoke desktop-shortcut-smoke terminal-smoke mouse-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check check size clean FORCE

all: $(KERNEL)

project: $(PORTABLE_ELF)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/arch/x86_64/boot.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel.o: src/kernel.c include/demon/graphics.h include/demon/input.h include/kernel/multiboot2.h include/kernel/framebuffer.h include/kernel/display.h include/kernel/surface.h include/kernel/network.h include/kernel/http.h include/kernel/pci.h include/kernel/e1000.h include/kernel/capability.h include/kernel/apps.h include/kernel/git.h include/kernel/init.h include/kernel/ipc.h include/kernel/runas.h include/kernel/ramfs.h include/kernel/serial.h include/kernel/terminal.h include/kernel/scheduler.h include/kernel/userspace.h include/kernel/acpi.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/serial.o: src/arch/x86_64/serial.c include/kernel/serial.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/terminal.o: src/terminal.c include/kernel/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupt_stubs.o: src/arch/x86_64/interrupt_stubs.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: src/arch/x86_64/interrupts.c include/demon/input.h include/kernel/interrupts.h include/kernel/scheduler.h include/kernel/serial.h include/kernel/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pci.o: src/arch/x86_64/pci.c include/kernel/pci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/input.o: src/input.c include/demon/input.h include/kernel/ipc.h include/kernel/scheduler.h include/kernel/userspace.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/network.o: src/network.c include/kernel/network.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/http.o: src/http.c include/kernel/http.h include/kernel/network.h include/kernel/e1000.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/e1000.o: src/e1000.c include/kernel/e1000.h include/kernel/network.h include/kernel/pci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ahci.o: src/ahci.c include/kernel/ahci.h include/kernel/pci.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# -Wno-array-bounds: acpi.c deliberately dereferences small constant
# physical addresses (e.g. the EBDA segment pointer at 0x40E) through the
# 1 GiB identity map; GCC's array-bounds pass mistakes those literal
# addresses for null-pointer-adjacent array indexing and refuses to build
# under -Werror otherwise.
$(BUILD)/acpi.o: src/acpi.c include/kernel/acpi.h include/kernel/serial.h | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds -c $< -o $@

$(BUILD)/graphics.o: libs/graphics/graphics.c include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

ICON_THEME := assets/IconPack/Fluent-icon-theme

$(BUILD)/mouse.argb: $(ICON_THEME)/cursors/src/svg/default.svg | $(BUILD)
	magick -background none $< -trim +repage -resize 16x24 -gravity northwest -background none \
		-extent 16x24 -channel RGBA -depth 8 BGRA:$@

$(BUILD)/mouse_argb.o: $(BUILD)/mouse.argb
	$(LD) -r -b binary $< -o $@

# 320x240, upscaled 2x by the kernel at blit time: a full 640x480 raw image
# (1200 KiB) pushes the backbuffer allocation past the 4 MiB identity-mapped
# window (see the 0x400000 bound in kernel.c's framebuffer mapping check),
# while the quarter-size image (300 KiB) leaves the whole layout comfortably
# inside it.
$(BUILD)/wallpaper.argb: assets/Flowers.jpg | $(BUILD)
	magick $< -resize 320x240^ -gravity center -extent 320x240 \
		-channel RGBA -depth 8 BGRA:$@

$(BUILD)/wallpaper_argb.o: $(BUILD)/wallpaper.argb
	$(LD) -r -b binary $< -o $@

# Contextual cursor icon pack (assets/Mouse/New Icons/Mouse Icons/). Unlike
# mouse.png, these source files are already true RGBA with real alpha (not a
# flat-color background needing floodfill removal -- running mouse.png's
# floodfill/border/shave recipe on them corrupted the small resize-arrow
# glyphs into blurry fragments during development), so this rule is just a
# trim-to-content + pad-to-16x24, concatenated in exactly the order enum
# graphics_cursor_icon expects starting at GRAPHICS_CURSOR_HAND (index 1) --
# GRAPHICS_CURSOR_ARROW (index 0) keeps using the original mouse.argb, so
# this blob covers indices 1..12 back to back, 1536 bytes each, letting
# demon_cursor_icon_pixels index it with one multiply.
CURSOR_ICON_SOURCES := \
	"$(ICON_THEME)/cursors/src/svg/pointer.svg" \
	"$(ICON_THEME)/cursors/src/svg/text.svg" \
	"$(ICON_THEME)/cursors/src/svg/size_bdiag.svg" \
	"$(ICON_THEME)/cursors/src/svg/top_side.svg" \
	"$(ICON_THEME)/cursors/src/svg/bottom_side.svg" \
	"$(ICON_THEME)/cursors/src/svg/right_side.svg" \
	"$(ICON_THEME)/cursors/src/svg/left_side.svg" \
	"$(ICON_THEME)/cursors/src/svg/top_right_corner.svg" \
	"$(ICON_THEME)/cursors/src/svg/top_left_corner.svg" \
	"$(ICON_THEME)/cursors/src/svg/bottom_right_corner.svg" \
	"$(ICON_THEME)/cursors/src/svg/bottom_left_corner.svg" \
	"$(ICON_THEME)/cursors/src/svg/not-allowed.svg" \
	"$(ICON_THEME)/cursors/src/svg/help.svg"

$(BUILD)/cursor_icons.argb: FORCE | $(BUILD)
	rm -f $@
	for src in $(CURSOR_ICON_SOURCES); do \
		magick "$$src" -trim +repage -gravity northwest -background none \
			-extent 16x24 -channel RGBA -depth 8 BGRA:- >> $@; \
	done

$(BUILD)/cursor_icons_argb.o: $(BUILD)/cursor_icons.argb
	$(LD) -r -b binary $< -o $@

# Titlebar window-control icon pack (assets/Mouse/New Icons/), same
# trim-to-content + pad recipe as the cursor pack (real alpha, no floodfill
# needed). Four 10x10 icons back to back: close, close-hover, minimize,
# minimize-hover -- see enum demon_ui_icon in demon/assets.h for the index
# order this blob must match. There is no real maximize glyph in the
# source pack (the "maximize program button" files are mislabeled
# duplicates of the close X), so the maximize control stays a drawn dot
# rather than using a wrong icon.
UI_ICON_SOURCES := \
	"$(ICON_THEME)/src/symbolic/actions/window-close-symbolic.svg" \
	"$(ICON_THEME)/src/symbolic/actions/window-close-symbolic.svg" \
	"$(ICON_THEME)/src/symbolic/actions/window-minimize-symbolic.svg" \
	"$(ICON_THEME)/src/symbolic/actions/window-minimize-symbolic.svg" \
	"$(ICON_THEME)/src/symbolic/actions/window-maximize-symbolic.svg" \
	"$(ICON_THEME)/src/symbolic/actions/window-maximize-symbolic.svg"

$(BUILD)/ui_icons.argb: FORCE | $(BUILD)
	rm -f $@
	for src in $(UI_ICON_SOURCES); do \
		magick -background none "$$src" -trim +repage -resize 8x8 -gravity center -background none \
			-extent 10x10 -fill '#F1F5F9' -colorize 100 \
			-channel RGBA -depth 8 BGRA:- >> $@; \
	done

$(BUILD)/ui_icons_argb.o: $(BUILD)/ui_icons.argb
	$(LD) -r -b binary $< -o $@

# Start-button logo (assets/Mouse/New Icons/Liquid OS Start Button Logo.png),
# padded to a square 22x22 canvas.
$(BUILD)/start_logo.argb: FORCE | $(BUILD)
	rm -f $@
	magick -background none "$(ICON_THEME)/src/22/places/start-here.svg" \
		-trim +repage -resize 20x20 -gravity center -background none \
		-extent 22x22 -channel RGBA -depth 8 BGRA:$@

$(BUILD)/start_logo_argb.o: $(BUILD)/start_logo.argb
	$(LD) -r -b binary $< -o $@

SHELL_ICON_SOURCES := \
	"$(ICON_THEME)/src/scalable/apps/terminal.svg" \
	"$(ICON_THEME)/src/scalable/places/default-folder.svg" \
	"$(ICON_THEME)/src/scalable/apps/web-browser.svg" \
	"$(ICON_THEME)/templates/app-blue-square.svg" \
	"$(ICON_THEME)/src/scalable/apps/systemsettings.svg" \
	"$(ICON_THEME)/src/scalable/devices/computer.svg"

$(BUILD)/shell_icons.argb: FORCE | $(BUILD)
	rm -f $@
	for src in $(SHELL_ICON_SOURCES); do \
		magick -background none "$$src" -trim +repage -resize 20x20 \
			-gravity center -background none -extent 22x22 \
			-channel RGBA -depth 8 BGRA:- >> $@; \
	done

$(BUILD)/shell_icons_argb.o: $(BUILD)/shell_icons.argb
	$(LD) -r -b binary $< -o $@

$(BUILD)/assets.o: src/assets.c include/demon/assets.h $(BUILD)/mouse_argb.o $(BUILD)/wallpaper_argb.o $(BUILD)/cursor_icons_argb.o $(BUILD)/ui_icons_argb.o $(BUILD)/start_logo_argb.o $(BUILD)/shell_icons_argb.o | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/framebuffer.o: src/framebuffer.c include/demon/assets.h include/demon/graphics.h include/kernel/framebuffer.h include/kernel/multiboot2.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/makobox.o: src/makobox.c include/kernel/makobox.h include/kernel/apps.h include/kernel/capability.h include/kernel/framebuffer.h include/kernel/git.h include/kernel/init.h include/kernel/interrupts.h include/kernel/ipc.h include/kernel/runas.h include/kernel/ramfs.h include/kernel/scheduler.h include/kernel/serial.h include/kernel/terminal.h include/kernel/userspace.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/scheduler.o: src/scheduler.c include/kernel/scheduler.h include/kernel/interrupt_frame.h include/kernel/userspace.h include/kernel/ipc.h include/demon/input.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/userspace.o: src/arch/x86_64/userspace.c include/kernel/userspace.h include/kernel/acpi.h include/kernel/capability.h include/kernel/display.h include/kernel/surface.h include/kernel/elf64.h include/kernel/interrupt_frame.h include/kernel/interrupts.h include/kernel/ipc.h include/kernel/ramfs.h include/kernel/scheduler.h include/kernel/serial.h include/kernel/terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/elf64.o: src/elf64.c include/kernel/elf64.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/capability.o: src/capability.c include/kernel/capability.h include/kernel/surface.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ipc.o: src/ipc.c include/kernel/ipc.h include/kernel/capability.h include/kernel/scheduler.h include/kernel/userspace.h include/demon/input.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/display.o: src/display.c include/kernel/display.h include/kernel/framebuffer.h include/kernel/interrupts.h include/demon/assets.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/surface.o: src/surface.c include/kernel/surface.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/init.o: src/init.c include/kernel/init.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/runas.o: src/runas.c include/kernel/runas.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/apps.o: src/apps.c include/kernel/apps.h include/kernel/ramfs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/git.o: src/git.c include/kernel/git.h include/kernel/ramfs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ramfs.o: src/ramfs.c include/kernel/ramfs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/user_entry.o: user/init.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/user_init_mko.S: user/init.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/user_init_mko.o: $(BUILD)/user_init_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(USER_ELF): $(BUILD)/user_entry.o $(BUILD)/user_init_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/user_entry.o $(BUILD)/user_init_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/portable_hello_entry.o: projects/hello/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/portable_hello_mko.S: projects/hello/main.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/portable_hello_mko.o: $(BUILD)/portable_hello_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(PORTABLE_ELF): $(BUILD)/portable_hello_entry.o $(BUILD)/portable_hello_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/portable_hello_entry.o $(BUILD)/portable_hello_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/user_program_blob.o: $(USER_ELF)
	$(LD) -r -b binary $< -o $@

$(BUILD)/compositor_entry.o: user/compositor.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/compositor_mko.S: user/compositor.mko user/sdk.mko Desktop/desktop.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/compositor_mko.o: $(BUILD)/compositor_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(COMPOSITOR_ELF): $(BUILD)/compositor_entry.o $(BUILD)/compositor_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/compositor_entry.o $(BUILD)/compositor_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/compositor_edde_mko.S: user/compositor_edde.mko user/sdk.mko Desktop/desktop_edde.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/compositor_edde_mko.o: $(BUILD)/compositor_edde_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

# Reuses compositor_entry.o (user/compositor.S) unmodified -- the entry
# stub is generic, it just calls compositor_main, and EDDE_COMPOSITOR_ELF
# links in compositor_edde.mko's own definition of that symbol.
$(EDDE_COMPOSITOR_ELF): $(BUILD)/compositor_entry.o $(BUILD)/compositor_edde_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/compositor_entry.o $(BUILD)/compositor_edde_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/window_client_entry.o: user/window_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/window_client_mko.S: user/window_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/window_client_mko.o: $(BUILD)/window_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(WINDOW_CLIENT_ELF): $(BUILD)/window_client_entry.o $(BUILD)/window_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/window_client_entry.o $(BUILD)/window_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/window_move_client_entry.o: user/window_move_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/window_move_client_mko.S: user/window_move_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/window_move_client_mko.o: $(BUILD)/window_move_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(WINDOW_MOVE_CLIENT_ELF): $(BUILD)/window_move_client_entry.o $(BUILD)/window_move_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/window_move_client_entry.o $(BUILD)/window_move_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/window_event_client_entry.o: user/window_event_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/window_event_client_mko.S: user/window_event_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/window_event_client_mko.o: $(BUILD)/window_event_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(WINDOW_EVENT_CLIENT_ELF): $(BUILD)/window_event_client_entry.o $(BUILD)/window_event_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/window_event_client_entry.o $(BUILD)/window_event_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/window_crash_client_entry.o: user/window_crash_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/window_crash_client_mko.S: user/window_crash_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/window_crash_client_mko.o: $(BUILD)/window_crash_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(WINDOW_CRASH_CLIENT_ELF): $(BUILD)/window_crash_client_entry.o $(BUILD)/window_crash_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/window_crash_client_entry.o $(BUILD)/window_crash_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/demonx_server_entry.o: user/demonx_server.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/demonx_server.o: user/demonx_server.c include/demon/demonx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DEMONX_ELF): $(BUILD)/demonx_server_entry.o $(BUILD)/demonx_server.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/demonx_server_entry.o $(BUILD)/demonx_server.o -o $@
	$(STRIP) -s $@

$(BUILD)/demonx_client_entry.o: user/demonx_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/demonx_client_mko.S: user/demonx_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/demonx_client_mko.o: $(BUILD)/demonx_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(DEMONX_CLIENT_ELF): $(BUILD)/demonx_client_entry.o $(BUILD)/demonx_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/demonx_client_entry.o $(BUILD)/demonx_client_mko.o -o $@
	$(STRIP) -s $@

# First freestanding Xlib compatibility slice.  This test binary is kept
# separate from the scripted MKO wire client: the latter boot-tests DemonX,
# while this target proves normal C source can compile and link against the
# public <X11/Xlib.h> boundary that PekWM will consume.
$(BUILD)/demonx_xlib.o: lib/demonx/xlib.c include/X11/Xlib.h include/demon/demonx.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/demonx_xlib_test.o: user/demonx_xlib_test.c include/X11/Xlib.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/demonx_xlib_test_entry.o: user/demonx_xlib_test.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(DEMONX_XLIB_TEST_ELF): $(BUILD)/demonx_xlib_test_entry.o \
		$(BUILD)/demonx_xlib_test.o $(BUILD)/demonx_xlib.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/demonx_xlib_test_entry.o \
		$(BUILD)/demonx_xlib_test.o $(BUILD)/demonx_xlib.o -o $@
	$(STRIP) -s $@

$(BUILD)/tetris_entry.o: apps/tetris/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/tetris.o: apps/tetris/main.c include/demon/c_app.h include/demon/input.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TETRIS_ELF): $(BUILD)/tetris_entry.o $(BUILD)/tetris.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/tetris_entry.o $(BUILD)/tetris.o -o $@
	$(STRIP) -s $@

$(BUILD)/calculator_entry.o: apps/calculator/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/calculator.o: apps/calculator/main.c include/demon/c_app.h include/demon/window.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(CALCULATOR_ELF): $(BUILD)/calculator_entry.o $(BUILD)/calculator.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/calculator_entry.o $(BUILD)/calculator.o -o $@
	$(STRIP) -s $@

# Same freestanding constraints as CFLAGS, plus the two things every
# freestanding C++ target needs: -fno-exceptions (no unwind-table runtime
# support here) and -fno-rtti (no __cxa_type_match/typeinfo runtime either).
# Every EDE-derived .cpp in later waves builds with this same flag set.
# -nostdinc++ forces #include <vector>/<string> to resolve to this
# project's own freestanding shims (include/vector, include/string) via
# -Iinclude instead of g++'s bundled libstdc++ headers, which don't build
# under -ffreestanding -fno-exceptions -nostdlib (they assume __cxa_throw,
# a real allocator, etc. exist).
CXXFLAGS := -std=c++17 -Os -g -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections \
	-m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-nostdinc++ -Iinclude

$(BUILD)/cxx_runtime.o: src/cxx_runtime.cpp include/demon/cxx_runtime.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cxx_hello_entry.o: apps/cxx_hello/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/cxx_hello.o: apps/cxx_hello/main.cpp include/demon/c_app.h include/demon/cxx_runtime.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CXX_HELLO_ELF): $(BUILD)/cxx_hello_entry.o $(BUILD)/cxx_hello.o $(BUILD)/cxx_runtime.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/cxx_hello_entry.o $(BUILD)/cxx_hello.o $(BUILD)/cxx_runtime.o -o $@
	$(STRIP) -s $@

# CFLAGS/CXXFLAGS keep -mno-mmx -mno-sse -mno-sse2 -msoft-float because the
# kernel and every non-floating-point app don't need the FPU at all, and
# leaving it fully disabled is the safer default. GCC on x86_64 refuses to
# even compile a function that returns `double` under those flags at all
# ("SSE register return with SSE disabled") -- soft-float there still needs
# XMM0 for the ABI return slot, so -msoft-float alone doesn't help. ede-calc
# is this kernel's first real floating-point app (apps/ede_calc, Wave 1 of
# the EDE port), so it -- and only it -- builds with SSE2 actually enabled;
# src/scheduler.c's FXSAVE/FXRSTOR-per-task change is what makes that safe
# under this kernel's preemptive scheduler.
FLOAT_CFLAGS := -std=c11 -Os -g -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections \
	-m64 -mno-red-zone \
	-Iinclude
FLOAT_CXXFLAGS := -std=c++17 -Os -g -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections \
	-m64 -mno-red-zone \
	-nostdinc++ -Iinclude

$(BUILD)/libm_freestanding.o: src/libm_freestanding.c include/demon/libm_freestanding.h | $(BUILD)
	$(CC) $(FLOAT_CFLAGS) -c $< -o $@

# libs/graphics/graphics.c has no floating point in it at all, so it builds
# fine either way -- this is a second object file (not the kernel's own
# $(BUILD)/graphics.o) purely because user apps and the kernel link
# separately; ede-calc needs graphics_text()/graphics_rounded_rect() for its
# button grid and LED display the same way the kernel's compositor does.
$(BUILD)/graphics_user.o: libs/graphics/graphics.c include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ede_calc_entry.o: apps/ede_calc/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_calc_engine.o: apps/ede_calc/engine.cpp apps/ede_calc/engine.h include/demon/libm_freestanding.h | $(BUILD)
	$(CXX) $(FLOAT_CXXFLAGS) -c $< -o $@

$(BUILD)/ede_calc_main.o: apps/ede_calc/main.cpp apps/ede_calc/engine.h include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CXX) $(FLOAT_CXXFLAGS) -c $< -o $@

$(EDE_CALC_ELF): $(BUILD)/ede_calc_entry.o $(BUILD)/ede_calc_main.o $(BUILD)/ede_calc_engine.o \
                 $(BUILD)/libm_freestanding.o $(BUILD)/graphics_user.o $(BUILD)/cxx_runtime.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_calc_entry.o $(BUILD)/ede_calc_main.o $(BUILD)/ede_calc_engine.o \
		$(BUILD)/libm_freestanding.o $(BUILD)/graphics_user.o $(BUILD)/cxx_runtime.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_about_entry.o: apps/ede_about/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_about_main.o: apps/ede_about/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_ABOUT_ELF): $(BUILD)/ede_about_entry.o $(BUILD)/ede_about_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_about_entry.o $(BUILD)/ede_about_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_tip_entry.o: apps/ede_tip/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_tip_main.o: apps/ede_tip/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_TIP_ELF): $(BUILD)/ede_tip_entry.o $(BUILD)/ede_tip_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_tip_entry.o $(BUILD)/ede_tip_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_preferred_entry.o: apps/ede_preferred/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_preferred_main.o: apps/ede_preferred/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_PREFERRED_ELF): $(BUILD)/ede_preferred_entry.o $(BUILD)/ede_preferred_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_preferred_entry.o $(BUILD)/ede_preferred_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_conf_entry.o: apps/ede_conf/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_conf_main.o: apps/ede_conf/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_CONF_ELF): $(BUILD)/ede_conf_entry.o $(BUILD)/ede_conf_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_conf_entry.o $(BUILD)/ede_conf_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_autostart_entry.o: apps/ede_autostart/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_autostart_main.o: apps/ede_autostart/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_AUTOSTART_ELF): $(BUILD)/ede_autostart_entry.o $(BUILD)/ede_autostart_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_autostart_entry.o $(BUILD)/ede_autostart_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/ede_timedate_entry.o: apps/ede_timedate/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/ede_timedate_main.o: apps/ede_timedate/main.c include/demon/c_app.h include/demon/window.h include/demon/graphics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(EDE_TIMEDATE_ELF): $(BUILD)/ede_timedate_entry.o $(BUILD)/ede_timedate_main.o $(BUILD)/graphics_user.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections \
		$(BUILD)/ede_timedate_entry.o $(BUILD)/ede_timedate_main.o $(BUILD)/graphics_user.o -o $@
	$(STRIP) -s $@

$(BUILD)/terminal_client_entry.o: user/terminal_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/terminal_client_mko.S: user/terminal_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/terminal_client_mko.o: $(BUILD)/terminal_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(TERMINAL_CLIENT_ELF): $(BUILD)/terminal_client_entry.o $(BUILD)/terminal_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/terminal_client_entry.o $(BUILD)/terminal_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/counter_client_entry.o: user/counter_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/counter_client_mko.S: user/counter_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/counter_client_mko.o: $(BUILD)/counter_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(COUNTER_CLIENT_ELF): $(BUILD)/counter_client_entry.o $(BUILD)/counter_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/counter_client_entry.o $(BUILD)/counter_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/browser_client_entry.o: user/browser_client.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/browser_client_mko.S: user/browser_client.mko user/sdk.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/browser_client_mko.o: $(BUILD)/browser_client_mko.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(BROWSER_CLIENT_ELF): $(BUILD)/browser_client_entry.o $(BUILD)/browser_client_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/browser_client_entry.o $(BUILD)/browser_client_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/kernel_probe.S: mako/kernel_probe.mko | $(BUILD)
	dotnet run --project ../MAKO/src/Mako -- native $< --kernel -o $@

$(BUILD)/kernel_probe.o: $(BUILD)/kernel_probe.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	@grub-file --is-x86-multiboot2 $@

FORCE:

$(MAKO_SOURCE_ARCHIVE) $(MAKO_MANIFEST) &: tools/package-mako-source.sh FORCE | $(BUILD)
	sh tools/package-mako-source.sh $(MAKO_REPO) $(MAKO_SOURCE_ARCHIVE) $(MAKO_MANIFEST)

iso: $(ISO) iso-check

$(ISO): $(KERNEL) $(PORTABLE_ELF) $(COMPOSITOR_ELF) $(EDDE_COMPOSITOR_ELF) $(WINDOW_CLIENT_ELF) $(WINDOW_MOVE_CLIENT_ELF) $(WINDOW_EVENT_CLIENT_ELF) $(WINDOW_CRASH_CLIENT_ELF) $(DEMONX_ELF) $(DEMONX_CLIENT_ELF) $(TETRIS_ELF) $(CALCULATOR_ELF) $(TERMINAL_CLIENT_ELF) $(COUNTER_CLIENT_ELF) $(BROWSER_CLIENT_ELF) $(CXX_HELLO_ELF) $(EDE_CALC_ELF) $(EDE_ABOUT_ELF) $(EDE_TIP_ELF) $(EDE_PREFERRED_ELF) $(EDE_CONF_ELF) $(EDE_AUTOSTART_ELF) $(EDE_TIMEDATE_ELF) $(MAKO_SOURCE_ARCHIVE) $(MAKO_MANIFEST) user/sdk.mko projects/hello/main.mko docs/desktop-iso-roadmap.md docs/init-system.md docs/apps-and-git.md docs/c-apps.md docs/freedoom-port.md docs/display-address-space.md docs/framebuffer-stage1.md docs/graphics-stage2.md docs/input-stage3.md docs/process-stage4.md docs/ipc-stage5.md docs/compositor-stage6.md docs/demonx.md docs/network-stage7.md grub/grub-test.cfg
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub $(ISO_ROOT)/boot/mako $(ISO_ROOT)/system/mako $(ISO_ROOT)/docs
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp user/sdk.mko $(ISO_ROOT)/boot/mako/sdk.mko
	cp projects/hello/main.mko $(ISO_ROOT)/boot/mako/hello.mko
	cp $(PORTABLE_ELF) $(ISO_ROOT)/boot/mako/hello.elf
	cp $(COMPOSITOR_ELF) $(ISO_ROOT)/boot/mako/compositor.elf
	cp $(EDDE_COMPOSITOR_ELF) $(ISO_ROOT)/boot/mako/compositor-edde.elf
	cp $(WINDOW_CLIENT_ELF) $(ISO_ROOT)/boot/mako/window-client.elf
	cp $(WINDOW_MOVE_CLIENT_ELF) $(ISO_ROOT)/boot/mako/window-move-client.elf
	cp $(WINDOW_EVENT_CLIENT_ELF) $(ISO_ROOT)/boot/mako/window-event-client.elf
	cp $(WINDOW_CRASH_CLIENT_ELF) $(ISO_ROOT)/boot/mako/window-crash-client.elf
	cp $(DEMONX_ELF) $(ISO_ROOT)/boot/mako/demonx.elf
	cp $(DEMONX_CLIENT_ELF) $(ISO_ROOT)/boot/mako/demonx-client.elf
	cp $(TETRIS_ELF) $(ISO_ROOT)/boot/mako/tetris.elf
	cp $(CALCULATOR_ELF) $(ISO_ROOT)/boot/mako/calculator.elf
	cp $(CXX_HELLO_ELF) $(ISO_ROOT)/boot/mako/cxx-hello.elf
	cp $(EDE_CALC_ELF) $(ISO_ROOT)/boot/mako/ede-calc.elf
	cp $(EDE_ABOUT_ELF) $(ISO_ROOT)/boot/mako/ede-about.elf
	cp $(EDE_TIP_ELF) $(ISO_ROOT)/boot/mako/ede-tip.elf
	cp $(EDE_PREFERRED_ELF) $(ISO_ROOT)/boot/mako/ede-preferred.elf
	cp $(EDE_CONF_ELF) $(ISO_ROOT)/boot/mako/ede-conf.elf
	cp $(EDE_AUTOSTART_ELF) $(ISO_ROOT)/boot/mako/ede-autostart.elf
	cp $(EDE_TIMEDATE_ELF) $(ISO_ROOT)/boot/mako/ede-timedate.elf
	cp $(TERMINAL_CLIENT_ELF) $(ISO_ROOT)/boot/mako/terminal-client.elf
	cp $(COUNTER_CLIENT_ELF) $(ISO_ROOT)/boot/mako/counter-client.elf
	cp $(BROWSER_CLIENT_ELF) $(ISO_ROOT)/boot/mako/browser-client.elf
	cp $(MAKO_MANIFEST) $(ISO_ROOT)/boot/mako/mako-manifest.txt
	cp README.md $(ISO_ROOT)/boot/mako/README.md
	cp $(MAKO_MANIFEST) $(ISO_ROOT)/system/mako/manifest.txt
	cp $(MAKO_SOURCE_ARCHIVE) $(ISO_ROOT)/system/mako/MAKO-source.tar.zst
	cp README.md $(ISO_ROOT)/README.md
	cp docs/desktop-iso-roadmap.md $(ISO_ROOT)/docs/desktop-iso-roadmap.md
	cp docs/init-system.md $(ISO_ROOT)/docs/init-system.md
	cp docs/apps-and-git.md $(ISO_ROOT)/docs/apps-and-git.md
	cp docs/c-apps.md $(ISO_ROOT)/docs/c-apps.md
	cp docs/freedoom-port.md $(ISO_ROOT)/docs/freedoom-port.md
	cp docs/display-address-space.md $(ISO_ROOT)/docs/display-address-space.md
	cp docs/framebuffer-stage1.md $(ISO_ROOT)/docs/framebuffer-stage1.md
	cp docs/graphics-stage2.md $(ISO_ROOT)/docs/graphics-stage2.md
	cp docs/input-stage3.md $(ISO_ROOT)/docs/input-stage3.md
	cp docs/process-stage4.md $(ISO_ROOT)/docs/process-stage4.md
	cp docs/ipc-stage5.md $(ISO_ROOT)/docs/ipc-stage5.md
	cp docs/compositor-stage6.md $(ISO_ROOT)/docs/compositor-stage6.md
	cp docs/demonx.md $(ISO_ROOT)/docs/demonx.md
	cp docs/network-stage7.md $(ISO_ROOT)/docs/network-stage7.md
	cp grub/grub-test.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT) >/dev/null 2>&1

$(RUN_ISO): $(ISO) grub/grub.cfg assets/Flowers.jpg
	rm -rf $(BUILD)/iso-run
	cp -r $(ISO_ROOT) $(BUILD)/iso-run
	cp grub/grub.cfg $(BUILD)/iso-run/boot/grub/grub.cfg
	# Re-encoded to PNG, not just copied: assets/Flowers.jpg is a progressive
	# JPEG, and GRUB's built-in jpeg.mod only decodes baseline JPEGs -- fed a
	# progressive one it renders scrambled noise instead of an error. PNG has
	# no such baseline/progressive split, so this sidesteps the whole class
	# of bug rather than re-encoding as a baseline JPEG and hoping nothing
	# ever regresses that.
	magick assets/Flowers.jpg $(BUILD)/iso-run/boot/grub/boot-background.png
	mkdir -p $(BUILD)/iso-run/boot/grub/fonts
	# Font location varies by distro/packaging (plain /usr/share/grub on most
	# hosts, a per-store path under Nix); grub.cfg's own loadfont call is
	# already guarded (see grub/grub.cfg) so a miss here just means the boot
	# menu falls back to plain text mode instead of failing the ISO build.
	cp /usr/share/grub/unicode.pf2 $(BUILD)/iso-run/boot/grub/fonts/unicode.pf2 2>/dev/null || \
		cp $$(find /nix/store -maxdepth 4 -name unicode.pf2 2>/dev/null | head -n1) \
			$(BUILD)/iso-run/boot/grub/fonts/unicode.pf2 2>/dev/null || true
	grub-mkrescue -o $@ $(BUILD)/iso-run >/dev/null 2>&1

iso-check: $(ISO)
	@xorriso -indev $(ISO) -find /boot/kernel.elf -type f 2>/dev/null | grep -q kernel.elf
	@xorriso -indev $(ISO) -find /boot/mako/sdk.mko -type f 2>/dev/null | grep -q sdk.mko
	@xorriso -indev $(ISO) -find /boot/mako/hello.mko -type f 2>/dev/null | grep -q hello.mko
	@xorriso -indev $(ISO) -find /boot/mako/hello.elf -type f 2>/dev/null | grep -q hello.elf
	@xorriso -indev $(ISO) -find /boot/mako/compositor.elf -type f 2>/dev/null | grep -q compositor.elf
	@xorriso -indev $(ISO) -find /boot/mako/compositor-edde.elf -type f 2>/dev/null | grep -q compositor-edde.elf
	@xorriso -indev $(ISO) -find /boot/mako/window-client.elf -type f 2>/dev/null | grep -q window-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/window-move-client.elf -type f 2>/dev/null | grep -q window-move-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/window-event-client.elf -type f 2>/dev/null | grep -q window-event-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/window-crash-client.elf -type f 2>/dev/null | grep -q window-crash-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/demonx.elf -type f 2>/dev/null | grep -q demonx.elf
	@xorriso -indev $(ISO) -find /boot/mako/demonx-client.elf -type f 2>/dev/null | grep -q demonx-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/tetris.elf -type f 2>/dev/null | grep -q tetris.elf
	@xorriso -indev $(ISO) -find /boot/mako/calculator.elf -type f 2>/dev/null | grep -q calculator.elf
	@test $$(wc -c < $(CALCULATOR_ELF)) -le 49152
	@test $$(wc -c < $(CXX_HELLO_ELF)) -le 16384
	@test $$(wc -c < $(EDE_CALC_ELF)) -le 32768
	@test $$(wc -c < $(EDE_ABOUT_ELF)) -le 16384
	@test $$(wc -c < $(EDE_TIP_ELF)) -le 16384
	@test $$(wc -c < $(EDE_PREFERRED_ELF)) -le 16384
	@test $$(wc -c < $(EDE_CONF_ELF)) -le 16384
	@test $$(wc -c < $(EDE_AUTOSTART_ELF)) -le 16384
	@test $$(wc -c < $(EDE_TIMEDATE_ELF)) -le 16384
	@xorriso -indev $(ISO) -find /boot/mako/terminal-client.elf -type f 2>/dev/null | grep -q terminal-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/counter-client.elf -type f 2>/dev/null | grep -q counter-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/browser-client.elf -type f 2>/dev/null | grep -q browser-client.elf
	@xorriso -indev $(ISO) -find /boot/mako/README.md -type f 2>/dev/null | grep -q README.md
	@xorriso -indev $(ISO) -find /boot/mako/mako-manifest.txt -type f 2>/dev/null | grep -q mako-manifest.txt
	@xorriso -indev $(ISO) -find /system/mako/MAKO-source.tar.zst -type f 2>/dev/null | grep -q MAKO-source.tar.zst
	@xorriso -indev $(ISO) -find /system/mako/manifest.txt -type f 2>/dev/null | grep -q manifest.txt
	@xorriso -indev $(ISO) -find /README.md -type f 2>/dev/null | grep -q README.md
	@xorriso -indev $(ISO) -find /docs/desktop-iso-roadmap.md -type f 2>/dev/null | grep -q desktop-iso-roadmap.md
	@xorriso -indev $(ISO) -find /docs/init-system.md -type f 2>/dev/null | grep -q init-system.md
	@xorriso -indev $(ISO) -find /docs/apps-and-git.md -type f 2>/dev/null | grep -q apps-and-git.md
	@xorriso -indev $(ISO) -find /docs/c-apps.md -type f 2>/dev/null | grep -q c-apps.md
	@xorriso -indev $(ISO) -find /docs/freedoom-port.md -type f 2>/dev/null | grep -q freedoom-port.md
	@xorriso -indev $(ISO) -find /docs/display-address-space.md -type f 2>/dev/null | grep -q display-address-space.md
	@xorriso -indev $(ISO) -find /docs/network-stage7.md -type f 2>/dev/null | grep -q network-stage7.md
	@xorriso -indev $(ISO) -find /docs/framebuffer-stage1.md -type f 2>/dev/null | grep -q framebuffer-stage1.md
	@xorriso -indev $(ISO) -find /docs/graphics-stage2.md -type f 2>/dev/null | grep -q graphics-stage2.md
	@xorriso -indev $(ISO) -find /docs/input-stage3.md -type f 2>/dev/null | grep -q input-stage3.md
	@xorriso -indev $(ISO) -find /docs/process-stage4.md -type f 2>/dev/null | grep -q process-stage4.md
	@xorriso -indev $(ISO) -find /docs/ipc-stage5.md -type f 2>/dev/null | grep -q ipc-stage5.md
	@xorriso -indev $(ISO) -find /docs/compositor-stage6.md -type f 2>/dev/null | grep -q compositor-stage6.md
	@xorriso -indev $(ISO) -find /docs/demonx.md -type f 2>/dev/null | grep -q demonx.md
	@test $$(wc -c < user/sdk.mko) -le 8192
	@test $$(wc -c < $(PORTABLE_ELF)) -le 8192
	@test $$(wc -c < $(COMPOSITOR_ELF)) -le 196608
	@test $$(wc -c < $(EDDE_COMPOSITOR_ELF)) -le 196608
	@test $$(wc -c < $(WINDOW_CLIENT_ELF)) -le 12288
	@test $$(wc -c < $(WINDOW_MOVE_CLIENT_ELF)) -le 8192
	@test $$(wc -c < $(WINDOW_EVENT_CLIENT_ELF)) -le 12288
	@test $$(wc -c < $(WINDOW_CRASH_CLIENT_ELF)) -le 12288
	@# Native grabs, cursors, save sets, client teardown, and pointer warp
	@# remain inside a strict 16 KiB server image budget.
	@test $$(wc -c < $(DEMONX_ELF)) -le 16384
	@# Two-client WM redirect and ICCCM atom/property protocol test.
	@# PropertyNotify, continuation, and the native pixmap/PutImage lifecycle
	@# bring the freestanding MAKO protocol test to roughly 41 KiB.
	@test $$(wc -c < $(DEMONX_CLIENT_ELF)) -le 46080
	@test $$(wc -c < $(TETRIS_ELF)) -le 12288
	@# Bumped from 32768: echo/uptime/pid/whoami plus the shared
	@# low24/low48/write_decimal helpers pushed the binary to 33088 bytes.
	@test $$(wc -c < $(TERMINAL_CLIENT_ELF)) -le 36864
	@test $$(wc -c < $(COUNTER_CLIENT_ELF)) -le 12288
	@test $$(wc -c < $(BROWSER_CLIENT_ELF)) -le 49152
	@test $$(wc -c < $(MAKO_MANIFEST)) -le 8192
	@zstd -q -t $(MAKO_SOURCE_ARCHIVE)
	@grep -Eq '^origin=https://github.com/AnimatedGTVR/MAKO([.]git)?$$' $(MAKO_MANIFEST)
	@grep -q '^archive_sha256=' $(MAKO_MANIFEST)
	@echo "MAKO ISO contents verified"

qemu: run

run: $(RUN_ISO)
	@echo "Mouse: using the reliable XWayland QEMU backend; Ctrl+Alt+G releases it"
	env GDK_BACKEND=x11 qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		-display gtk,grab-on-hover=on \
		-no-reboot -no-shutdown

# Native Wayland is opt-in because GTK's relative-pointer grab can render a
# perfectly healthy guest while forwarding only the first host movement on
# some compositors. Keep it available for systems where that protocol works.
run-wayland: $(RUN_ISO)
	@echo "Mouse: native Wayland mode (experimental); Ctrl+Alt+G releases it"
	env GDK_BACKEND=wayland qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		-display gtk,grab-on-hover=on \
		-no-reboot -no-shutdown

# SDL is a second local fallback when the GTK backend cannot acquire a
# relative pointer grab through either XWayland or native Wayland.
run-sdl: $(RUN_ISO)
	@echo "Mouse: SDL relative-pointer mode; Ctrl+Alt+G releases it"
	qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		-display sdl \
		-no-reboot -no-shutdown

# Alternative for environments where a local QEMU window never receives
# host pointer/keyboard input at all.
# VNC does its own independent pointer/keyboard capture in the connecting
# client, sidestepping GTK/Wayland grab entirely. Connect with any VNC
# client to localhost:5901 (VNC display :1 = TCP port 5900+1).
run-vnc: $(RUN_ISO)
	@echo "Connect a VNC client to localhost:5901. Ctrl+Alt+G is not needed -- VNC has no host/guest grab step."
	qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		-display vnc=:1 \
		-no-reboot -no-shutdown

smoke: $(ISO)
	@rm -f $(BUILD)/serial.log
	@timeout 6s qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/serial.log -display none -no-reboot -no-shutdown \
		>/dev/null 2>&1 || test $$? -eq 124
	@grep -q "KERNEL_BOOT_OK" $(BUILD)/serial.log
	@grep -q "MKO_NATIVE_OK" $(BUILD)/serial.log
	@grep -q "DISPLAY_DEVICE_TEST_OK" $(BUILD)/serial.log
	@grep -q "FRAMEBUFFER_DETECTED" $(BUILD)/serial.log
	@grep -q "FRAMEBUFFER_MAPPING_OK" $(BUILD)/serial.log
	@grep -q "FRAMEBUFFER_PRIMITIVES_OK" $(BUILD)/serial.log
	@grep -q "GRAPHICS_LIBRARY_OK" $(BUILD)/serial.log
	@grep -q "NETWORK_PACKET_CORE_OK" $(BUILD)/serial.log
	@grep -q "PCI_ENUMERATION_OK" $(BUILD)/serial.log
	@grep -q "PCI_ETHERNET_FOUND" $(BUILD)/serial.log
	@grep -q "E1000_DEVICE_READY" $(BUILD)/serial.log
	@grep -q "E1000_DMA_RINGS_READY" $(BUILD)/serial.log
	@grep -q "E1000_REAL_ARP_OK" $(BUILD)/serial.log
	@grep -q "DHCP_REAL_OFFER_OK" $(BUILD)/serial.log
	@grep -q "DHCP_REAL_ACK_OK" $(BUILD)/serial.log
	@grep -q "DNS_REAL_QUERY_OK host=example.com" $(BUILD)/serial.log
	@grep -q "TCP_REAL_HANDSHAKE_OK" $(BUILD)/serial.log
	@grep -q "HTTP_REAL_RESPONSE_OK" $(BUILD)/serial.log
	@grep -q "DEMON_WEB_NETWORK_ABI_OK" $(BUILD)/serial.log
	@grep -q "HTTP_RUNTIME_CLIENT_OK" $(BUILD)/serial.log
	@grep -q "MOUSE_INPUT_READY" $(BUILD)/serial.log
	@grep -q "UNIFIED_INPUT_ABI_OK" $(BUILD)/serial.log
	@grep -q "GRAPHICAL_BOOT_TEST_OK" $(BUILD)/serial.log
	@grep -q "LIVE_CURSOR_READY owner=compositor" $(BUILD)/serial.log
	@grep -Eq "GRAPHICS_RENDER_TICKS=[0-9]+" $(BUILD)/serial.log
	@grep -q "MKO_MULTIBOOT_PARSE_OK" $(BUILD)/serial.log
	@grep -q "MKO_FRAME_ALLOCATOR_OK" $(BUILD)/serial.log
	@grep -q "MKO_VIRTUAL_MEMORY_OK" $(BUILD)/serial.log
	@grep -q "INTERRUPT_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "HARDWARE_IRQ_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "KEYBOARD_INPUT_READY" $(BUILD)/serial.log
	@grep -q "MKO_SYSTEM_PREINSTALLED" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/mko/sdk.mko" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /projects/hello/main.mko" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /projects/hello/main.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/compositor.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/window-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/window-move-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/window-event-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/window-crash-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/demonx.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/demonx-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/tetris.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/terminal-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/counter-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/browser-client.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/bin/calculator.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/mako/manifest.txt" $(BUILD)/serial.log
	@grep -q "MKO system environment:" $(BUILD)/serial.log
	@grep -q "repository: AnimatedGTVR/MAKO" $(BUILD)/serial.log
	@grep -q "ISO source: /system/mako/MAKO-source.tar.zst" $(BUILD)/serial.log
	@grep -q "ISO-provided assets: 25" $(BUILD)/serial.log
	@grep -q "files: 26" $(BUILD)/serial.log
	@grep -q "USERSPACE_SYSCALLS_OK" $(BUILD)/serial.log
	@grep -q "ELF64_MKO_LOAD_OK" $(BUILD)/serial.log
	@grep -q "USERSPACE_CODE_POOLS_OK pages=104 max=48 heap=0x330000" $(BUILD)/serial.log
	@grep -q "PROCESS_ISOLATION_OK" $(BUILD)/serial.log
	@grep -q "DYNAMIC_SPAWN_OK pid=3 status=0" $(BUILD)/serial.log
	@grep -q "PROCESS_WAIT_CLEANUP_OK" $(BUILD)/serial.log
	@grep -q "DISPLAY_CAPABILITY_ISOLATION_OK" $(BUILD)/serial.log
	@grep -q "DISPLAY_ATOMIC_COMMIT_OK" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_SERVICE_READY" $(BUILD)/serial.log
	@grep -q "DESKTOP_TERMINAL_READY" $(BUILD)/serial.log
	@grep -q "DEMON_WEB_NAVIGATION_OK page=about input=keyboard surface=damaged" $(BUILD)/serial.log
	@grep -q "DEMON_WEB_FILE_URL_OK url=file:///README.md source=ramfs bytes=bounded" $(BUILD)/serial.log
	@grep -q "DEMON_WEB_HISTORY_OK controls=back,forward depth=4 input=mouse" $(BUILD)/serial.log
	@grep -q "WINDOW_TABLE_EMPTY_OK count=0" $(BUILD)/serial.log
	@grep -q "WINDOW_CREATE_DYNAMIC_OK id=4 count=1" $(BUILD)/serial.log
	@grep -q "WINDOW_CREATE_DYNAMIC_OK id=5 count=2" $(BUILD)/serial.log
	@grep -q "CLIENT_CRASH_CONTAINMENT_OK pid=6 status=77 count=3" $(BUILD)/serial.log
	@grep -q "MAPPED_SURFACE_ZERO_COPY_OK live=2 maps=" $(BUILD)/serial.log
	@grep -q "WINDOW_FOCUS_CHANGED_OK focused=6" $(BUILD)/serial.log
	@grep -q "WINDOW_MOVE_OK id=4 x=144 y=64" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_PERSISTENT_SESSION_OK state=blocked frames=" $(BUILD)/serial.log
	@grep -q "SURFACE_ARENA_READY bytes=163840" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_WAITSET_OK sources=ipc,input state=blocked" $(BUILD)/serial.log
	@grep -q "WINDOW_POINTER_DRAG_OK frames=" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_FRAME_PACING_OK min_ticks=" $(BUILD)/serial.log
	@grep -q "WINDOW_FOCUS_CHANGED_OK focused=4" $(BUILD)/serial.log
	@grep -q "WINDOW_KEYBOARD_DELIVERY_OK" $(BUILD)/serial.log
	@grep -q "WINDOW_KEYBOARD_ROUTE_OK window=4 code=30 client_pid=" $(BUILD)/serial.log
	@grep -q "WINDOW_CLOSE_OK closed=6 count=2 focused=" $(BUILD)/serial.log
	@grep -q "DESKTOP_ALT_F4_CLOSE_OK window=6" $(BUILD)/serial.log
	@grep -q "DESKTOP_TASKBAR_MINIMIZE_OK window=4 focused=5 live=2" $(BUILD)/serial.log
	@grep -q "DESKTOP_TASKBAR_RESTORE_OK window=4 live=2" $(BUILD)/serial.log
	@grep -q "DESKTOP_MAXIMIZE_RESTORE_OK window=4 workarea=624x388" $(BUILD)/serial.log
	@grep -q "DESKTOP_SUPER_D_TOGGLE_OK live=2" $(BUILD)/serial.log
	@grep -q "DESKTOP_SUPER_SNAP_OK left=308x388 restore=exact" $(BUILD)/serial.log
	@grep -q "DESKTOP_SUPER_LAUNCHER_OK key=left-super" $(BUILD)/serial.log
	@grep -q "DESKTOP_LAUNCHER_TOGGLE_OK open=1 frames=" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_CURSOR_OWNERSHIP_OK x=319 y=239 frames=" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_TEXT_GLYPHS_OK label=MAKO damage_pixels=140" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_CODE_IMMUTABLE_OK" $(BUILD)/serial.log
	@grep -q "USERSPACE_COMPOSITOR_OK frames=" $(BUILD)/serial.log
	@grep -q "WINDOW_MANAGER_DYNAMIC_OK created=3 closed=1 live=2" $(BUILD)/serial.log
	@grep -q "DESKTOP_TARGET_REPAINT_OK frames=" $(BUILD)/serial.log
	@grep -q "DEMONX_SERVER_READY transport=capability-ipc protocol=X11" $(BUILD)/serial.log
	@grep -q "DEMONX_SETUP_OK version=11.0 byte_order=little" $(BUILD)/serial.log
	@grep -q "DEMONX_CORE_REQUESTS_OK clients=2 transport=continuation,max256 create=1 select_input=1 map_request=1 map_notify=1 configure_request=1 configure_notify=1 query_tree=1 unmap_notify=1 atoms=1 properties=change,get,list,delete property_notify=1 client_message=1 gc=foreground,font fill_rectangle=1 pixmap=create,putimage,copyarea,background,getimage,text8,free backing=native,fallback-retained destroy=1" $(BUILD)/serial.log
	@grep -q "DEMONX_CLIENT_MAKO_OK status=0" $(BUILD)/serial.log
	@grep -q "C_APP_TETRIS_READY input=keyboard runtime=MAKO-ABI" $(BUILD)/serial.log
	@grep -q "DESKTOP_DEFAULT_TARGET_OK target=desktop.target" $(BUILD)/serial.log
	@grep -q "DESKTOP_SESSION_READY display=compositor input=compositor console=recovery-only" $(BUILD)/serial.log
	@! grep -q "TERMINAL_OWNERSHIP_READY" $(BUILD)/serial.log
	@grep -q "IPC_BLOCKING_USERSPACE_OK" $(BUILD)/serial.log
	@grep -q "IPC_CHANNEL_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "CAPABILITY_ABI_OK" $(BUILD)/serial.log
	@grep -q "PROJECT_STORE_OK" $(BUILD)/serial.log
	@grep -q "project.mko  16 bytes" $(BUILD)/serial.log
	@grep -q "SCHEDULER_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "MAKOBOX_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "MAKO_INIT_SYSTEM_OK" $(BUILD)/serial.log
	@grep -q "RUNAS_POLICY_OK" $(BUILD)/serial.log
	@grep -q "project-host.service  active" $(BUILD)/serial.log
	@grep -q "Init units: 10 active" $(BUILD)/serial.log
	@grep -q "desktop-compositor.service  active" $(BUILD)/serial.log
	@grep -q "desktop.target  active" $(BUILD)/serial.log
	@grep -q "DESKTOP_TARGET_ACTIVE pid=" $(BUILD)/serial.log
	@grep -q "APP_REGISTRY_OK" $(BUILD)/serial.log
	@grep -q "GIT_WORKTREE_OK" $(BUILD)/serial.log
	@grep -q "hello  ELF64  ready" $(BUILD)/serial.log
	@grep -q "Desktop readiness:" $(BUILD)/serial.log
	@grep -q "\[ready\] read-only mapped client surface + owned damage metadata" $(BUILD)/serial.log
	@grep -q "\[ready\] timer-deadline frame pacing and dirty wakeups" $(BUILD)/serial.log
	@grep -q "\[ready\] native decorations, resize grip, and launcher toggle" $(BUILD)/serial.log
	@grep -q "\[ready\] compositor-owned arrow cursor and compact launcher glyphs" $(BUILD)/serial.log
	@grep -q "\[complete\] desktop-demo platform contract 100/100" $(BUILD)/serial.log
	@grep -q "systemctl: administrative transaction requires runas" $(BUILD)/serial.log
	@grep -q "runas: policy granted local-console administrator role" $(BUILD)/serial.log
	@grep -q "systemctl: transaction committed" $(BUILD)/serial.log
	@grep -q "mako@kernel" $(BUILD)/serial.log
	@grep -q "PID STATE     NAME" $(BUILD)/serial.log
	@grep -q "1   exited   init" $(BUILD)/serial.log
	@grep -q "2   exited   worker" $(BUILD)/serial.log
	@grep -q "userspace exit: 41 yields: 2" $(BUILD)/serial.log
	@grep -q "scheduler dispatches: 9" $(BUILD)/serial.log
	@grep -q "Hello from an MKO ELF64 process!" $(BUILD)/serial.log
	@grep -q "MAKO-ABI 0.1" $(BUILD)/serial.log
	@grep -q "CPU:" $(BUILD)/serial.log
	@grep -q "usable memory: 255 MiB" $(BUILD)/serial.log
	@grep -q "CPU paging -- CR0.PG + CR4.PAE active, CR3 loaded" $(BUILD)/serial.log
	@grep -q "Welcome to the MAKO kernel!" $(BUILD)/serial.log
	@! grep -q "\[FAILED\]" $(BUILD)/serial.log
	@echo "Kernel boot smoke test passed"

framebuffer-smoke: smoke
	@grep -q "FRAMEBUFFER_DETECTED 640x480 pitch=2560 bpp=32" $(BUILD)/serial.log
	@grep -q "FRAMEBUFFER_MAPPING_OK" $(BUILD)/serial.log
	@grep -q "FRAMEBUFFER_PRIMITIVES_OK" $(BUILD)/serial.log
	@grep -q "GRAPHICS_LIBRARY_OK" $(BUILD)/serial.log
	@grep -q "GRAPHICAL_BOOT_TEST_OK" $(BUILD)/serial.log
	@echo "Kernel linear-framebuffer smoke test passed"

framebuffer-fallback-smoke: $(ISO)
	@rm -f $(BUILD)/framebuffer-fallback.log
	@timeout 7s qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
		-serial file:$(BUILD)/framebuffer-fallback.log -display none \
		-no-reboot -no-shutdown >/dev/null 2>&1 || test $$? -eq 124
	@grep -q "FRAMEBUFFER_FALLBACK_VGA" $(BUILD)/framebuffer-fallback.log
	@grep -q "MKO_VGA_WRITE_OK" $(BUILD)/framebuffer-fallback.log
	@grep -q "VGA_RENDER_CLEAN_OK" $(BUILD)/framebuffer-fallback.log
	@grep -q "KERNEL_BOOT_OK" $(BUILD)/framebuffer-fallback.log
	@! grep -q "\[FAILED\]" $(BUILD)/framebuffer-fallback.log
	@echo "Kernel VGA/serial framebuffer-fallback smoke test passed"

keyboard-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/keyboard.log $(BUILD)/monitor.sock
	@qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
		-serial file:$(BUILD)/keyboard.log -display none \
		-monitor unix:$(BUILD)/monitor.sock,server,nowait \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 80); do \
			if grep -q "mako#" $(BUILD)/keyboard.log 2>/dev/null && test -S $(BUILD)/monitor.sock; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'sendkey h\nsendkey e\nsendkey l\nsendkey p\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/monitor.sock >/dev/null; \
		sleep 0.5; \
		printf 'sendkey i\nsendkey n\nsendkey p\nsendkey u\nsendkey t\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/monitor.sock >/dev/null; \
		for attempt in $$(seq 1 30); do \
			if grep -q "mako# input" $(BUILD)/keyboard.log 2>/dev/null; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'sendkey t\nsendkey e\nsendkey t\nsendkey r\nsendkey i\nsendkey s\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/monitor.sock >/dev/null; \
		for attempt in $$(seq 1 30); do \
			if grep -q "DEMONOS C TETRIS" $(BUILD)/keyboard.log 2>/dev/null; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'sendkey q\n' | socat - UNIX-CONNECT:$(BUILD)/monitor.sock >/dev/null; \
		for attempt in $$(seq 1 30); do \
			if grep -q "TETRIS EXITED CLEANLY" $(BUILD)/keyboard.log 2>/dev/null && \
			   grep -q "apps: process exited with status 0" $(BUILD)/keyboard.log 2>/dev/null; then break; fi; \
			sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "mako# help" $(BUILD)/keyboard.log
	@grep -q "MakoBox applets:" $(BUILD)/keyboard.log
	@grep -q "input    show unified keyboard/mouse statistics" $(BUILD)/keyboard.log
	@grep -q "mako# input" $(BUILD)/keyboard.log
	@grep -q "Unified input service:" $(BUILD)/keyboard.log
	@grep -Eq "decoded characters: [1-9][0-9]*" $(BUILD)/keyboard.log
	@grep -q "FRAMEBUFFER_FALLBACK_VGA" $(BUILD)/keyboard.log
	@grep -q "DEMONOS C TETRIS" $(BUILD)/keyboard.log
	@grep -q "TETRIS EXITED CLEANLY" $(BUILD)/keyboard.log
	@grep -q "apps: process exited with status 0" $(BUILD)/keyboard.log
	@grep -q "dropped events: 0" $(BUILD)/keyboard.log
	@! grep -q "\[FAILED\]" $(BUILD)/keyboard.log
	@echo "Kernel PS/2 keyboard recovery-console smoke test passed"

desktop-shortcut-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/desktop-shortcut.log $(BUILD)/desktop-shortcut-monitor.sock \
		$(BUILD)/desktop-shortcut-before.ppm $(BUILD)/desktop-shortcut-after.ppm
	@qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/desktop-shortcut.log -display none \
		-monitor unix:$(BUILD)/desktop-shortcut-monitor.sock,server,nowait \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 80); do \
			if grep -q "DESKTOP_SESSION_READY" $(BUILD)/desktop-shortcut.log 2>/dev/null && \
			   test -S $(BUILD)/desktop-shortcut-monitor.sock; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'screendump %s/$(BUILD)/desktop-shortcut-before.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/desktop-shortcut-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/desktop-shortcut-before.ppm && break; sleep 0.05; done; \
		printf 'sendkey meta_l\n' | \
			socat - UNIX-CONNECT:$(BUILD)/desktop-shortcut-monitor.sock >/dev/null; \
		sleep 0.4; \
		printf 'screendump %s/$(BUILD)/desktop-shortcut-after.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/desktop-shortcut-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/desktop-shortcut-after.ppm && break; sleep 0.05; done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@! cmp -s $(BUILD)/desktop-shortcut-before.ppm $(BUILD)/desktop-shortcut-after.ppm
	@grep -q "DESKTOP_SESSION_READY display=compositor input=compositor console=recovery-only" $(BUILD)/desktop-shortcut.log
	@! grep -Eq "PAGE FAULT|EXCEPTION|PANIC|\[FAILED\]" $(BUILD)/desktop-shortcut.log
	@echo "Native desktop Super-key launcher smoke test passed"

terminal-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/terminal-visual.log $(BUILD)/terminal-monitor.sock \
		$(BUILD)/terminal-before.ppm $(BUILD)/terminal-mouse.ppm \
		$(BUILD)/terminal-typed.ppm $(BUILD)/terminal-command.ppm
	@qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/terminal-visual.log -display none \
		-monitor unix:$(BUILD)/terminal-monitor.sock,server,nowait \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 80); do \
			if grep -q "DESKTOP_SESSION_READY" $(BUILD)/terminal-visual.log 2>/dev/null && \
			   test -S $(BUILD)/terminal-monitor.sock; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'screendump %s/$(BUILD)/terminal-before.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/terminal-before.ppm && break; sleep 0.05; done; \
		printf 'mouse_move 80 40\n' | socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		sleep 0.3; \
		printf 'screendump %s/$(BUILD)/terminal-mouse.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/terminal-mouse.ppm && break; sleep 0.05; done; \
		{ for repeat in $$(seq 1 40); do printf 'sendkey a\n'; sleep 0.02; done; } | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		sleep 0.3; \
		printf 'screendump %s/$(BUILD)/terminal-typed.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/terminal-typed.ppm && break; sleep 0.05; done; \
		{ for key in ret h e l p ret; do printf 'sendkey %s\n' "$$key"; done; } | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		sleep 0.3; \
		printf 'screendump %s/$(BUILD)/terminal-command.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/terminal-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/terminal-command.ppm && break; sleep 0.05; done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@! cmp -s $(BUILD)/terminal-before.ppm $(BUILD)/terminal-mouse.ppm
	@! cmp -s $(BUILD)/terminal-mouse.ppm $(BUILD)/terminal-typed.ppm
	@! cmp -s $(BUILD)/terminal-typed.ppm $(BUILD)/terminal-command.ppm
	@grep -q "DESKTOP_TERMINAL_READY" $(BUILD)/terminal-visual.log
	@grep -q "DESKTOP_DEFAULT_TARGET_OK target=desktop.target" $(BUILD)/terminal-visual.log
	@grep -q "DESKTOP_SESSION_READY display=compositor input=compositor console=recovery-only" $(BUILD)/terminal-visual.log
	@! grep -q "TERMINAL_OWNERSHIP_READY" $(BUILD)/terminal-visual.log
	@! grep -q "\[FAILED\]" $(BUILD)/terminal-visual.log
	@echo "Native desktop terminal mouse/keyboard visual smoke test passed"

mouse-smoke: $(ISO)
	@# The 3s settle wait (was 1s) clears the ~2s window-open materialize
	@# fade (anim_transition_ticks() in compositor.mko) that the boot
	@# test's own terminal-window creation triggers, so the two
	@# screenshots below capture a genuinely settled desktop instead of
	@# racing that animation.
	@command -v socat >/dev/null
	@rm -f $(BUILD)/mouse.log $(BUILD)/mouse-monitor.sock \
		$(BUILD)/mouse-before.ppm $(BUILD)/mouse-after.ppm
	@qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/mouse.log -display none \
		-monitor unix:$(BUILD)/mouse-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 80); do \
			test -S $(BUILD)/mouse-monitor.sock && grep -q "DESKTOP_SESSION_READY" $(BUILD)/mouse.log 2>/dev/null && break; sleep 0.1; \
		done; \
		sleep 3; \
		printf 'screendump %s/$(BUILD)/mouse-before.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/mouse-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/mouse-before.ppm && break; sleep 0.05; done; \
		{ for step in $$(seq 1 16); do printf 'mouse_move 5 3\n'; done; } | \
			socat - UNIX-CONNECT:$(BUILD)/mouse-monitor.sock >/dev/null; \
		sleep 0.3; \
		printf 'screendump %s/$(BUILD)/mouse-after.ppm\n' "$(CURDIR)" | \
			socat - UNIX-CONNECT:$(BUILD)/mouse-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 20); do test -s $(BUILD)/mouse-after.ppm && break; sleep 0.05; done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "MOUSE_INPUT_READY" $(BUILD)/mouse.log
	@grep -q "DESKTOP_SESSION_READY display=compositor input=compositor console=recovery-only" $(BUILD)/mouse.log
	@! grep -q "TERMINAL_OWNERSHIP_READY" $(BUILD)/mouse.log
	@! cmp -s $(BUILD)/mouse-before.ppm $(BUILD)/mouse-after.ppm
	@# 12000 covers the focused window's titlebar breathing-glow animation
	@# (a ~296x32 = 9472px band that recolors on the compositor's idle
	@# heartbeat, see anim_pulse in compositor.mko) landing between the two
	@# screenshots, with headroom, while still catching a real full-screen
	@# redraw bug or artifact (307200px total).
	@changed=$$(magick $(BUILD)/mouse-before.ppm $(BUILD)/mouse-after.ppm \
		-compose difference -composite -threshold 0 \
		-format '%[fx:mean*w*h]' info:); \
		awk 'BEGIN { exit !(ARGV[1] > 0 && ARGV[1] <= 12000) }' "$$changed"
	@! grep -q "\[FAILED\]" $(BUILD)/mouse.log
	@echo "Kernel PS/2 mouse to native-desktop smoke test passed"

process-smoke: $(ISO)
	@rm -f $(BUILD)/process.log $(BUILD)/process-monitor.sock
	@timeout 12s qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
		-serial file:$(BUILD)/process.log -display none \
		-monitor unix:$(BUILD)/process-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 100); do \
			test -S $(BUILD)/process-monitor.sock && grep -q "mako#" $(BUILD)/process.log 2>/dev/null && break; sleep 0.1; \
		done; \
		test -S $(BUILD)/process-monitor.sock || { kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true; exit 1; }; \
		{ for key in a p p s spc l a u n c h spc h e l l o ret; do printf 'sendkey %s\n' "$$key"; done; } | \
			socat - UNIX-CONNECT:$(BUILD)/process-monitor.sock >/dev/null; \
		wait $$pid || test $$? -eq 124
	@grep -q "DYNAMIC_SPAWN_OK pid=3 status=0" $(BUILD)/process.log
	@grep -q "mako# apps launch hello" $(BUILD)/process.log
	@grep -q "apps: process exited with status 0" $(BUILD)/process.log
	@! grep -Eq "spawn failed|wait failed|PAGE FAULT|\[FAILED\]" $(BUILD)/process.log
	@echo "Kernel dynamic process spawn/wait/reuse smoke test passed"

ipc-smoke: smoke
	@grep -q "IPC_BLOCKING_USERSPACE_OK" $(BUILD)/serial.log
	@grep -q "IPC_CHANNEL_SELF_TEST_OK" $(BUILD)/serial.log
	@grep -q "IPC_NAMED_SERVICE_OK" $(BUILD)/serial.log
	@grep -q "Capability IPC service:" $(BUILD)/serial.log
	@grep -q "messages dropped: 0" $(BUILD)/serial.log
	@echo "Kernel capability IPC channel smoke test passed"

vfs-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/vfs.log $(BUILD)/vfs_monitor.sock
	@qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
		-serial file:$(BUILD)/vfs.log -display none \
		-monitor unix:$(BUILD)/vfs_monitor.sock,server,nowait \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 80); do \
			if grep -q "mako#" $(BUILD)/vfs.log 2>/dev/null && test -S $(BUILD)/vfs_monitor.sock; then break; fi; \
			sleep 0.1; \
		done; \
		printf 'sendkey l\nsendkey s\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/vfs_monitor.sock >/dev/null; \
		sleep 0.4; \
		printf 'sendkey l\nsendkey s\nsendkey spc\nsendkey slash\nsendkey s\nsendkey y\nsendkey s\nsendkey t\nsendkey e\nsendkey m\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/vfs_monitor.sock >/dev/null; \
		for attempt in $$(seq 1 30); do \
			if grep -q "mako# ls /system" $(BUILD)/vfs.log 2>/dev/null && \
			   grep -q "  mko/" $(BUILD)/vfs.log 2>/dev/null && \
			   grep -q "  mako/" $(BUILD)/vfs.log 2>/dev/null; then break; fi; \
			sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "mako# ls" $(BUILD)/vfs.log
	@grep -q "Listing: /" $(BUILD)/vfs.log
	@grep -q "  system/" $(BUILD)/vfs.log
	@grep -q "  projects/" $(BUILD)/vfs.log
	@grep -q "mako# ls /system" $(BUILD)/vfs.log
	@grep -q "  mko/" $(BUILD)/vfs.log
	@grep -q "  mako/" $(BUILD)/vfs.log
	@echo "Kernel VFS path/directory-listing smoke test passed"

mako-check: $(PORTABLE_ELF) $(COMPOSITOR_ELF) $(EDDE_COMPOSITOR_ELF) $(WINDOW_CLIENT_ELF) $(WINDOW_MOVE_CLIENT_ELF) $(WINDOW_EVENT_CLIENT_ELF) $(WINDOW_CRASH_CLIENT_ELF) $(BROWSER_CLIENT_ELF)
	dotnet run --project ../MAKO/src/Mako -- check mako/kernel_probe.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check mako/abi_probe.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/init.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/compositor.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/compositor_edde.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/window_client.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/window_move_client.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/window_event_client.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/window_crash_client.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/browser_client.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check projects/hello/main.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- mir mako/kernel_probe.mko --opt >/dev/null
	dotnet run --project ../MAKO/src/Mako -- native mako/kernel_probe.mko --kernel -o $(BUILD)/kernel_probe.check.S
	dotnet run --project ../MAKO/src/Mako -- native mako/abi_probe.mko --kernel -o $(BUILD)/abi_probe.check.S

footprint-check: $(KERNEL) $(BUILD)/init.o $(BUILD)/runas.o
	@total=$$(size $(BUILD)/init.o $(BUILD)/runas.o | awk 'NR > 1 { sum += $$4 } END { print sum }'); \
		test $$total -le 4096 || { echo "init/runas footprint $$total exceeds 4096-byte budget"; exit 1; }
	@bss=$$(size $(BUILD)/init.o $(BUILD)/runas.o | awk 'NR > 1 { sum += $$3 } END { print sum }'); \
		test $$bss -le 256 || { echo "init/runas BSS $$bss exceeds 256-byte budget"; exit 1; }
	@# 1 MiB covers the previous 384 KiB kernel plus the embedded 320x240
	@# desktop wallpaper (300 KiB, assets/Flowers.jpg -> build/wallpaper.argb,
	@# linked the same way as the mouse cursor asset) with real headroom --
	@# not meant to stay minimal, just bounded so a genuine bloat regression
	@# (e.g. an asset embedded twice, or a much larger unintended image) still
	@# fails loudly instead of growing silently forever. Note the hard
	@# physical ceiling is separate: kernel+modules+backbuffer must fit the
	@# 4 MiB identity-mapped window (kernel.c's 0x400000 mapping check).
	@kernel=$$(size $(KERNEL) | awk 'NR == 2 { print $$4 }'); \
		test $$kernel -le 1048576 || { echo "kernel memory footprint $$kernel exceeds 1-MiB budget"; exit 1; }
	@echo "Footprint budgets passed: init+runas <= 4 KiB, BSS <= 256 B, kernel <= 1 MiB"

check: iso-check framebuffer-smoke framebuffer-fallback-smoke keyboard-smoke desktop-shortcut-smoke terminal-smoke mouse-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check

size: $(KERNEL)
	@size $(KERNEL)

clean:
	rm -rf $(BUILD)

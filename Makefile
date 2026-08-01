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
TETRIS_ELF := $(BUILD)/tetris.elf
# Proves a real C++ app -- heap allocation, vtables, a virtual destructor --
# can build and run under MAKO-ABI at all.
CXX_HELLO_ELF := $(BUILD)/cxx_hello.elf
PORTCHECK_ELF := $(BUILD)/portcheck.elf
DOOM_ELF := $(BUILD)/doom.elf
DOOM_FULL_ELF := $(BUILD)/doom-full.elf
MAKO_REPO := ../MAKO
MAKO_SOURCE_ARCHIVE := $(BUILD)/MAKO-source.tar.zst
MAKO_MANIFEST := $(BUILD)/mako-manifest.txt
FREEDOOM_DIR := $(BUILD)/freedoom
FREEDOOM_WAD := $(FREEDOOM_DIR)/freedoom1.wad
FREEDOOM_ISO := $(BUILD)/kernel-freedoom.iso
FREEDOOM_PLAY_ISO := $(BUILD)/kernel-freedoom-play.iso
DOOM_UPSTREAM := $(BUILD)/doomgeneric-upstream
DOOM_SOURCE_STAMP := $(DOOM_UPSTREAM)/.demonos-pinned
DOOM_ENGINE_BUILD := $(BUILD)/doom-engine
DOOM_CORE_NAMES := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
	d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info \
	i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv \
	m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng \
	p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
	p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp \
	r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump \
	st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main \
	w_wad z_zone w_file_stdc i_input i_video doomgeneric
DOOM_CORE_SOURCES := $(addprefix $(DOOM_UPSTREAM)/doomgeneric/,$(addsuffix .c,$(DOOM_CORE_NAMES)))
DOOM_CORE_OBJECTS := $(addprefix $(DOOM_ENGINE_BUILD)/,$(addsuffix .o,$(DOOM_CORE_NAMES)))
DOOM_CFLAGS := -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pie -fno-pic -ffunction-sections -fdata-sections -m64 -mno-red-zone -msse2 \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-implicit-fallthrough -Wno-unterminated-string-initialization \
	-DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE \
	-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
	-I$(DOOM_UPSTREAM)/doomgeneric

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

.PHONY: all iso iso-check project portkit-check doom-source doom-platform-audit doom-engine-audit doom-runtime-audit doom-check freedoom-assets freedoom-iso freedoom-play-iso freedoom-smoke freedoom-command-smoke qemu run run-doom run-wayland run-sdl run-vnc smoke framebuffer-fallback-smoke keyboard-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check check size clean FORCE

all: $(KERNEL)

project: $(PORTABLE_ELF)

portkit-check: $(PORTCHECK_ELF)

doom-source: $(DOOM_SOURCE_STAMP)

$(DOOM_SOURCE_STAMP): tools/fetch-doomgeneric.sh
	sh tools/fetch-doomgeneric.sh $(DOOM_UPSTREAM)

$(DOOM_ENGINE_BUILD):
	mkdir -p $@

# Teach make that the explicit fetch step materializes these files. This keeps
# a clean-tree `make doom-engine-audit` working without making ordinary builds
# perform a network operation.
$(DOOM_CORE_SOURCES): | $(DOOM_SOURCE_STAMP)
	@test -f $@

$(DOOM_ENGINE_BUILD)/%.o: $(DOOM_UPSTREAM)/doomgeneric/%.c | $(DOOM_SOURCE_STAMP) $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(DOOM_ENGINE_BUILD)/doomgeneric_demonos.o: ports/doom/platform/doomgeneric_demonos.c ports/doom/platform/doomgeneric_demonos.h include/demon/portkit.h include/demon/input.h | $(DOOM_SOURCE_STAMP) $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -Iports/doom/platform -c $< -o $@

$(DOOM_ENGINE_BUILD)/demonos_stdio.o: ports/doom/platform/demonos_stdio.c apps/doom/libc.h include/demon/portkit.h | $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -Iapps/doom -c $< -o $@

$(DOOM_ENGINE_BUILD)/doom_libc.o: apps/doom/libc.c apps/doom/libc.h include/demon/portkit.h | $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -Iapps/doom -c $< -o $@

$(DOOM_ENGINE_BUILD)/portkit.o: user/portkit.c include/demon/portkit.h include/demon/c_app.h | $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -c $< -o $@

doom-platform-audit: $(DOOM_ENGINE_BUILD)/doomgeneric_demonos.o
	@nm -u $< | awk '{print $$2}' | sort -u > $(DOOM_ENGINE_BUILD)/platform-unresolved.txt
	@grep -q '^DG_ScreenBuffer$$' $(DOOM_ENGINE_BUILD)/platform-unresolved.txt
	@grep -q '^demon_port_poll_input$$' $(DOOM_ENGINE_BUILD)/platform-unresolved.txt
	@! grep -Eq '^(__|fopen|printf|malloc|memcpy)$$' $(DOOM_ENGINE_BUILD)/platform-unresolved.txt
	@echo "DemonOS doomgeneric platform adapter compile audit passed"

doom-engine-audit: $(DOOM_CORE_OBJECTS) doom-platform-audit
	$(LD) -r $(DOOM_CORE_OBJECTS) -o $(DOOM_ENGINE_BUILD)/doomgeneric-core.o
	@nm -u $(DOOM_ENGINE_BUILD)/doomgeneric-core.o | awk '{print $$2}' | sort -u > $(DOOM_ENGINE_BUILD)/unresolved.txt
	@grep -q '^DG_Init$$' $(DOOM_ENGINE_BUILD)/unresolved.txt
	@grep -q '^fopen$$' $(DOOM_ENGINE_BUILD)/unresolved.txt
	@echo "doomgeneric core compile audit passed"

$(DOOM_ENGINE_BUILD)/doomgeneric-with-platform.o: $(DOOM_CORE_OBJECTS) $(DOOM_ENGINE_BUILD)/doomgeneric_demonos.o $(DOOM_ENGINE_BUILD)/demonos_stdio.o $(DOOM_ENGINE_BUILD)/doom_libc.o $(DOOM_ENGINE_BUILD)/portkit.o
	$(LD) -r $^ -o $@
	@nm -u $@ | awk '{print $$2}' | sort -u > $(DOOM_ENGINE_BUILD)/remaining-runtime.txt

.PHONY: doom-runtime-audit
doom-runtime-audit: $(DOOM_ENGINE_BUILD)/doomgeneric-with-platform.o
	@test ! -s $(DOOM_ENGINE_BUILD)/remaining-runtime.txt
	@echo "DemonOS Doom stdio/platform runtime audit passed"

$(DOOM_ENGINE_BUILD)/entry.o: ports/doom/platform/entry.S | $(DOOM_ENGINE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(DOOM_ENGINE_BUILD)/main.o: ports/doom/platform/main.c include/demon/portkit.h | $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -c $< -o $@

$(DOOM_ENGINE_BUILD)/engine_main.o: ports/doom/platform/engine_main.c include/demon/portkit.h | $(DOOM_SOURCE_STAMP) $(DOOM_ENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -c $< -o $@

$(DOOM_ELF): $(DOOM_ENGINE_BUILD)/entry.o $(DOOM_ENGINE_BUILD)/main.o $(DOOM_ENGINE_BUILD)/portkit.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(DOOM_ENGINE_BUILD)/entry.o \
		$(DOOM_ENGINE_BUILD)/main.o $(DOOM_ENGINE_BUILD)/portkit.o -o $@
	$(STRIP) -s $@

doom-check: doom-runtime-audit $(DOOM_ELF)
	@readelf -h $(DOOM_ELF) | grep -q 'Entry point address:.*0x300000'
	@echo "DemonOS Doom D0 executable ready"

$(DOOM_FULL_ELF): $(DOOM_ENGINE_BUILD)/entry.o $(DOOM_ENGINE_BUILD)/engine_main.o $(DOOM_ENGINE_BUILD)/doomgeneric-with-platform.o ports/doom/linker.ld
	$(LD) -nostdlib --gc-sections -z max-page-size=0x1000 -T ports/doom/linker.ld \
		$(DOOM_ENGINE_BUILD)/entry.o $(DOOM_ENGINE_BUILD)/engine_main.o \
		$(DOOM_ENGINE_BUILD)/doomgeneric-with-platform.o -o $@

.PHONY: doom-full-check
doom-full-check: doom-runtime-audit $(DOOM_FULL_ELF)
	@readelf -h $(DOOM_FULL_ELF) | grep -q 'Entry point address:.*0x20000000'
	@readelf -lW $(DOOM_FULL_ELF) | grep -q 'LOAD.*0x0000000020000000'
	@echo "Full upstream Doom ELF linked at isolated large-app base"

freedoom-assets:
	sh tools/fetch-freedoom.sh $(FREEDOOM_DIR)

freedoom-iso: $(FREEDOOM_ISO)

freedoom-play-iso: $(FREEDOOM_PLAY_ISO)

$(FREEDOOM_PLAY_ISO): $(ISO) freedoom-assets grub/grub-freedoom-play.cfg
	rm -rf $(BUILD)/iso-freedoom-play
	cp -r $(ISO_ROOT) $(BUILD)/iso-freedoom-play
	mkdir -p $(BUILD)/iso-freedoom-play/games/freedoom $(BUILD)/iso-freedoom-play/licenses/doom
	cp $(FREEDOOM_WAD) $(BUILD)/iso-freedoom-play/games/freedoom/freedoom1.wad
	cp $(FREEDOOM_DIR)/COPYING.txt $(BUILD)/iso-freedoom-play/licenses/doom/Freedoom-COPYING.txt
	cp grub/grub-freedoom-play.cfg $(BUILD)/iso-freedoom-play/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-freedoom-play >/dev/null 2>&1

$(FREEDOOM_ISO): $(ISO) freedoom-assets grub/grub-freedoom-test.cfg
	rm -rf $(BUILD)/iso-freedoom
	cp -r $(ISO_ROOT) $(BUILD)/iso-freedoom
	mkdir -p $(BUILD)/iso-freedoom/games/freedoom $(BUILD)/iso-freedoom/licenses/doom
	cp $(FREEDOOM_WAD) $(BUILD)/iso-freedoom/games/freedoom/freedoom1.wad
	cp $(FREEDOOM_DIR)/COPYING.txt $(BUILD)/iso-freedoom/licenses/doom/Freedoom-COPYING.txt
	cp grub/grub-freedoom-test.cfg $(BUILD)/iso-freedoom/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-freedoom >/dev/null 2>&1

freedoom-smoke: $(FREEDOOM_ISO)
	@rm -f $(BUILD)/freedoom-serial.log
	@timeout 40s qemu-system-x86_64 -m 256M -cdrom $(FREEDOOM_ISO) \
		-serial file:$(BUILD)/freedoom-serial.log -display none -no-reboot -no-shutdown \
		>/dev/null 2>&1 || test $$? -eq 124
	@grep -q "preinstalled MKO asset: /games/freedoom/freedoom1.wad" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_IWAD_READY lumps=" $(BUILD)/freedoom-serial.log
	@grep -q "PORTKIT_RING3_OK pid=3 status=0" $(BUILD)/freedoom-serial.log
	@grep -q "DOOM_FULL_ENGINE_START" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_FIRST_FRAME_READY 320x200 ARGB" $(BUILD)/freedoom-serial.log
	@grep -Eq "FREEDOOM_DISPLAY_SCALE scale=[2-3] output=" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_TITLE_READY hash=" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_300_FRAMES_OK surface=" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_GAME_READY frame=.* episode=1 map=1" $(BUILD)/freedoom-serial.log
	@grep -q "FREEDOOM_AUTOMATED_EXIT frame=360" $(BUILD)/freedoom-serial.log
	@grep -Eq "DOOM_FILE_WRITE_OK bytes=[1-9][0-9]*" $(BUILD)/freedoom-serial.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/freedoom-serial.log
	@echo "Official Freedoom engine frame smoke test passed"

freedoom-command-smoke: $(FREEDOOM_PLAY_ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/freedoom-command.log $(BUILD)/freedoom-monitor.sock
	@timeout 35s qemu-system-x86_64 -cdrom $(FREEDOOM_PLAY_ISO) -m 256M \
		-serial file:$(BUILD)/freedoom-command.log -display none \
		-monitor unix:$(BUILD)/freedoom-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/freedoom-monitor.sock && grep -q "mako#" $(BUILD)/freedoom-command.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey d\nsendkey o\nsendkey o\nsendkey m\nsendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/freedoom-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "FREEDOOM_FIRST_FRAME_READY" $(BUILD)/freedoom-command.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "mako# doom" $(BUILD)/freedoom-command.log
	@grep -q "FREEDOOM_FIRST_FRAME_READY 320x200 ARGB" $(BUILD)/freedoom-command.log
	@! grep -Eq "spawn failed|wait failed|PAGE FAULT|\[FAILED\]" $(BUILD)/freedoom-command.log
	@echo "MakoBox Doom command launch smoke test passed"

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

$(BUILD)/tetris_entry.o: apps/tetris/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/tetris.o: apps/tetris/main.c include/demon/c_app.h include/demon/input.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TETRIS_ELF): $(BUILD)/tetris_entry.o $(BUILD)/tetris.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/tetris_entry.o $(BUILD)/tetris.o -o $@
	$(STRIP) -s $@

$(BUILD)/portkit_entry.o: apps/portcheck/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/portkit.o: user/portkit.c include/demon/portkit.h include/demon/c_app.h include/demon/input.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/portcheck.o: apps/portcheck/main.c include/demon/portkit.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/doom_libc.o: apps/doom/libc.c apps/doom/libc.h include/demon/portkit.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/doom_wad.o: apps/doom/wad.c apps/doom/wad.h include/demon/portkit.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(PORTCHECK_ELF): $(BUILD)/portkit_entry.o $(BUILD)/portkit.o $(BUILD)/doom_libc.o $(BUILD)/doom_wad.o $(BUILD)/portcheck.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/portkit_entry.o $(BUILD)/portkit.o \
		$(BUILD)/doom_libc.o $(BUILD)/doom_wad.o $(BUILD)/portcheck.o -o $@

# Same freestanding constraints as CFLAGS, plus the two things every
# freestanding C++ target needs: -fno-exceptions (no unwind-table runtime
# support here) and -fno-rtti (no __cxa_type_match/typeinfo runtime either).
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

$(ISO): $(KERNEL) $(PORTABLE_ELF) $(TETRIS_ELF) $(CXX_HELLO_ELF) $(PORTCHECK_ELF) $(DOOM_ELF) $(DOOM_FULL_ELF) $(MAKO_SOURCE_ARCHIVE) $(MAKO_MANIFEST) user/sdk.mko projects/hello/main.mko docs/init-system.md docs/apps-and-git.md docs/git-port.md docs/c-apps.md docs/native-porting.md docs/freedoom-port.md docs/display-address-space.md docs/framebuffer-stage1.md docs/graphics-stage2.md docs/input-stage3.md docs/process-stage4.md docs/ipc-stage5.md docs/network-stage7.md grub/grub-test.cfg
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub $(ISO_ROOT)/boot/mako $(ISO_ROOT)/system/mako $(ISO_ROOT)/docs
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp user/sdk.mko $(ISO_ROOT)/boot/mako/sdk.mko
	cp projects/hello/main.mko $(ISO_ROOT)/boot/mako/hello.mko
	cp $(PORTABLE_ELF) $(ISO_ROOT)/boot/mako/hello.elf
	cp $(TETRIS_ELF) $(ISO_ROOT)/boot/mako/tetris.elf
	cp $(CXX_HELLO_ELF) $(ISO_ROOT)/boot/mako/cxx-hello.elf
	cp $(PORTCHECK_ELF) $(ISO_ROOT)/boot/mako/portcheck.elf
	cp $(DOOM_ELF) $(ISO_ROOT)/boot/mako/doom.elf
	cp $(DOOM_FULL_ELF) $(ISO_ROOT)/boot/mako/doom-full.elf
	cp $(MAKO_MANIFEST) $(ISO_ROOT)/boot/mako/mako-manifest.txt
	cp README.md $(ISO_ROOT)/boot/mako/README.md
	cp $(MAKO_MANIFEST) $(ISO_ROOT)/system/mako/manifest.txt
	cp $(MAKO_SOURCE_ARCHIVE) $(ISO_ROOT)/system/mako/MAKO-source.tar.zst
	cp README.md $(ISO_ROOT)/README.md
	cp docs/init-system.md $(ISO_ROOT)/docs/init-system.md
	cp docs/apps-and-git.md $(ISO_ROOT)/docs/apps-and-git.md
	cp docs/git-port.md $(ISO_ROOT)/docs/git-port.md
	cp docs/c-apps.md $(ISO_ROOT)/docs/c-apps.md
	cp docs/native-porting.md $(ISO_ROOT)/docs/native-porting.md
	cp docs/freedoom-port.md $(ISO_ROOT)/docs/freedoom-port.md
	cp docs/display-address-space.md $(ISO_ROOT)/docs/display-address-space.md
	cp docs/framebuffer-stage1.md $(ISO_ROOT)/docs/framebuffer-stage1.md
	cp docs/graphics-stage2.md $(ISO_ROOT)/docs/graphics-stage2.md
	cp docs/input-stage3.md $(ISO_ROOT)/docs/input-stage3.md
	cp docs/process-stage4.md $(ISO_ROOT)/docs/process-stage4.md
	cp docs/ipc-stage5.md $(ISO_ROOT)/docs/ipc-stage5.md
	cp docs/network-stage7.md $(ISO_ROOT)/docs/network-stage7.md
	cp grub/grub-test.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT) >/dev/null 2>&1

$(RUN_ISO): $(ISO) freedoom-assets grub/grub.cfg
	rm -rf $(BUILD)/iso-run
	cp -r $(ISO_ROOT) $(BUILD)/iso-run
	mkdir -p $(BUILD)/iso-run/games/freedoom $(BUILD)/iso-run/licenses/doom
	cp $(FREEDOOM_WAD) $(BUILD)/iso-run/games/freedoom/freedoom1.wad
	cp $(FREEDOOM_DIR)/COPYING.txt $(BUILD)/iso-run/licenses/doom/Freedoom-COPYING.txt
	cp grub/grub.cfg $(BUILD)/iso-run/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-run >/dev/null 2>&1

iso-check: $(ISO)
	@xorriso -indev $(ISO) -find /boot/kernel.elf -type f 2>/dev/null | grep -q kernel.elf
	@xorriso -indev $(ISO) -find /boot/mako/sdk.mko -type f 2>/dev/null | grep -q sdk.mko
	@xorriso -indev $(ISO) -find /boot/mako/hello.mko -type f 2>/dev/null | grep -q hello.mko
	@xorriso -indev $(ISO) -find /boot/mako/hello.elf -type f 2>/dev/null | grep -q hello.elf
	@xorriso -indev $(ISO) -find /boot/mako/tetris.elf -type f 2>/dev/null | grep -q tetris.elf
	@xorriso -indev $(ISO) -find /boot/mako/portcheck.elf -type f 2>/dev/null | grep -q portcheck.elf
	@xorriso -indev $(ISO) -find /boot/mako/doom.elf -type f 2>/dev/null | grep -q doom.elf
	@xorriso -indev $(ISO) -find /boot/mako/doom-full.elf -type f 2>/dev/null | grep -q doom-full.elf
	@xorriso -indev $(ISO) -find /boot/mako/README.md -type f 2>/dev/null | grep -q README.md
	@xorriso -indev $(ISO) -find /boot/mako/mako-manifest.txt -type f 2>/dev/null | grep -q mako-manifest.txt
	@xorriso -indev $(ISO) -find /system/mako/MAKO-source.tar.zst -type f 2>/dev/null | grep -q MAKO-source.tar.zst
	@xorriso -indev $(ISO) -find /system/mako/manifest.txt -type f 2>/dev/null | grep -q manifest.txt
	@xorriso -indev $(ISO) -find /README.md -type f 2>/dev/null | grep -q README.md
	@xorriso -indev $(ISO) -find /docs/init-system.md -type f 2>/dev/null | grep -q init-system.md
	@xorriso -indev $(ISO) -find /docs/apps-and-git.md -type f 2>/dev/null | grep -q apps-and-git.md
	@xorriso -indev $(ISO) -find /docs/git-port.md -type f 2>/dev/null | grep -q git-port.md
	@xorriso -indev $(ISO) -find /docs/c-apps.md -type f 2>/dev/null | grep -q c-apps.md
	@xorriso -indev $(ISO) -find /docs/native-porting.md -type f 2>/dev/null | grep -q native-porting.md
	@xorriso -indev $(ISO) -find /docs/freedoom-port.md -type f 2>/dev/null | grep -q freedoom-port.md
	@xorriso -indev $(ISO) -find /docs/display-address-space.md -type f 2>/dev/null | grep -q display-address-space.md
	@xorriso -indev $(ISO) -find /docs/network-stage7.md -type f 2>/dev/null | grep -q network-stage7.md
	@xorriso -indev $(ISO) -find /docs/framebuffer-stage1.md -type f 2>/dev/null | grep -q framebuffer-stage1.md
	@xorriso -indev $(ISO) -find /docs/graphics-stage2.md -type f 2>/dev/null | grep -q graphics-stage2.md
	@xorriso -indev $(ISO) -find /docs/input-stage3.md -type f 2>/dev/null | grep -q input-stage3.md
	@xorriso -indev $(ISO) -find /docs/process-stage4.md -type f 2>/dev/null | grep -q process-stage4.md
	@xorriso -indev $(ISO) -find /docs/ipc-stage5.md -type f 2>/dev/null | grep -q ipc-stage5.md
	@test $$(wc -c < user/sdk.mko) -le 8192
	@test $$(wc -c < $(PORTABLE_ELF)) -le 8192
	@test $$(wc -c < $(TETRIS_ELF)) -le 12288
	@test $$(wc -c < $(CXX_HELLO_ELF)) -le 16384
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

# Explicit alias for the normal interactive image, which includes the real
# Freedoom IWAD and exposes it to MakoBox as /games/freedoom/freedoom1.wad.
run-doom: run

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
	@grep -q "\[ .... \] Checking ISO modules and preinstalled project files" $(BUILD)/serial.log
	@grep -q "\[  OK  \] HTTP -- real response received and validated" $(BUILD)/serial.log
	@! grep -q "PAGE FAULT" $(BUILD)/serial.log
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
	@grep -q "MOUSE_INPUT_READY" $(BUILD)/serial.log
	@grep -q "UNIFIED_INPUT_ABI_OK" $(BUILD)/serial.log
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
	@grep -q "preinstalled MKO asset: /system/bin/tetris.elf" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/mako/manifest.txt" $(BUILD)/serial.log
	@grep -q "preinstalled MKO asset: /system/mako/MAKO-source.tar.zst" $(BUILD)/serial.log
	@grep -q "MKO system environment:" $(BUILD)/serial.log
	@grep -q "repository: AnimatedGTVR/MAKO" $(BUILD)/serial.log
	@grep -q "ISO source: /system/mako/MAKO-source.tar.zst" $(BUILD)/serial.log
	@grep -q "USERSPACE_SYSCALLS_OK" $(BUILD)/serial.log
	@grep -q "ELF64_MKO_LOAD_OK" $(BUILD)/serial.log
	@grep -q "PROCESS_ISOLATION_OK" $(BUILD)/serial.log
	@grep -q "DYNAMIC_SPAWN_OK pid=3 status=0" $(BUILD)/serial.log
	@grep -q "PORTKIT_READY allocator libc wad-validation reuse large-files seek timing input" $(BUILD)/serial.log
	@grep -q "PORTKIT_RING3_OK pid=3 status=0" $(BUILD)/serial.log
	@grep -q "DOOM_ENGINE_READY" $(BUILD)/serial.log
	@grep -q "DOOM_VERSION_RING3_OK" $(BUILD)/serial.log
	@grep -q "DOOM_FULL_IMAGE_MAPPED pages=149 base=0x20000000" $(BUILD)/serial.log
	@grep -q "DOOM_FULL_RING3_OK" $(BUILD)/serial.log
	@grep -q "PROCESS_WAIT_CLEANUP_OK" $(BUILD)/serial.log
	@grep -q "CONSOLE_TARGET_OK target=console.target" $(BUILD)/serial.log
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
	@grep -q "APP_REGISTRY_OK" $(BUILD)/serial.log
	@grep -q "GIT_WORKTREE_OK" $(BUILD)/serial.log
	@grep -q "hello  ELF64  ready" $(BUILD)/serial.log
	@grep -q "runit: administrative transaction requires runas" $(BUILD)/serial.log
	@grep -q "runas: policy granted local-console administrator role" $(BUILD)/serial.log
	@grep -q "runit: transaction committed" $(BUILD)/serial.log
	@grep -q "mako@demonos" $(BUILD)/serial.log
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

mako-check: $(PORTABLE_ELF)
	dotnet run --project ../MAKO/src/Mako -- check mako/kernel_probe.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check mako/abi_probe.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check user/init.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- check projects/hello/main.mko --kernel
	dotnet run --project ../MAKO/src/Mako -- mir mako/kernel_probe.mko --opt >/dev/null
	dotnet run --project ../MAKO/src/Mako -- native mako/kernel_probe.mko --kernel -o $(BUILD)/kernel_probe.check.S
	dotnet run --project ../MAKO/src/Mako -- native mako/abi_probe.mko --kernel -o $(BUILD)/abi_probe.check.S

footprint-check: $(KERNEL) $(BUILD)/init.o $(BUILD)/runas.o
	@total=$$(size $(BUILD)/init.o $(BUILD)/runas.o | awk 'NR > 1 { sum += $$4 } END { print sum }'); \
		test $$total -le 4096 || { echo "init/runas footprint $$total exceeds 4096-byte budget"; exit 1; }
	@bss=$$(size $(BUILD)/init.o $(BUILD)/runas.o | awk 'NR > 1 { sum += $$3 } END { print sum }'); \
		test $$bss -le 256 || { echo "init/runas BSS $$bss exceeds 256-byte budget"; exit 1; }
	@# 1.25 MiB: the GUI/compositor stack is sidelined (see sidelined/), but
	@# the Makefile's icon/wallpaper embedding pipeline (wallpaper.argb,
	@# cursor/UI/shell icon blobs) is still linked in even though nothing
	@# draws them anymore -- a real, known follow-up is to actually strip
	@# that pipeline rather than just keep raising this number. Bounded so
	@# a genuine bloat regression still fails loudly instead of growing
	@# silently forever. Note the hard physical ceiling is separate:
	@# kernel+modules+backbuffer must fit the 4 MiB identity-mapped window
	@# (kernel.c's 0x400000 mapping check).
	@kernel=$$(size $(KERNEL) | awk 'NR == 2 { print $$4 }'); \
		test $$kernel -le 1310720 || { echo "kernel memory footprint $$kernel exceeds 1.25-MiB budget"; exit 1; }
	@echo "Footprint budgets passed: init+runas <= 4 KiB, BSS <= 256 B, kernel <= 1.25 MiB"

check: iso-check framebuffer-fallback-smoke keyboard-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check

size: $(KERNEL)
	@size $(KERNEL)

clean:
	rm -rf $(BUILD)

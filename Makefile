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
# W0 of docs/wine-port.md: a real PE/COFF header parser proof, standalone
# (not wired into the main $(ISO) yet -- see the doc for why).
WINE_PE_PROBE_ELF := $(BUILD)/wine_pe_probe.elf
# W1 of docs/wine-port.md: proves the reserve/commit syscalls (46/47),
# standalone for the same reason as WINE_PE_PROBE_ELF above.
MEM_RESERVE_CHECK_ELF := $(BUILD)/mem_reserve_check.elf
# W2 of docs/wine-port.md: proves the PE section loader, standalone for the
# same reason as the two above.
WINE_PE_LOAD_CHECK_ELF := $(BUILD)/wine_pe_load_check.elf
# W3 of docs/wine-port.md: proves export/import resolution + IAT patching,
# standalone for the same reason as the others.
WINE_PE_IMPORT_CHECK_ELF := $(BUILD)/wine_pe_import_check.elf
# W4 of docs/wine-port.md: proves base relocation application, standalone
# for the same reason as the others.
WINE_PE_RELOC_CHECK_ELF := $(BUILD)/wine_pe_reloc_check.elf
DOOM_ELF := $(BUILD)/doom.elf
DOOM_FULL_ELF := $(BUILD)/doom-full.elf
# Desktop stack (un-sidelined): ring-3 MKO compositor, the DemonX X11 server,
# the DemonWM window manager, and the xterm-style terminal client.
COMPOSITOR_ELF := $(BUILD)/compositor.elf
DEMONX_ELF := $(BUILD)/demonx.elf
DEMONWM_ELF := $(BUILD)/demonwm.elf
XTERM_ELF := $(BUILD)/xterm.elf
MAKO_REPO := ../MAKO
MAKO_SOURCE_ARCHIVE := $(BUILD)/MAKO-source.tar.zst
MAKO_MANIFEST := $(BUILD)/mako-manifest.txt
FREEDOOM_DIR := $(BUILD)/freedoom
FREEDOOM_WAD := $(FREEDOOM_DIR)/freedoom1.wad
FREEDOOM_ISO := $(BUILD)/kernel-freedoom.iso
FREEDOOM_PLAY_ISO := $(BUILD)/kernel-freedoom-play.iso
QUAKE_DATA_DIR := $(BUILD)/quake-data
QUAKE_PAK := $(QUAKE_DATA_DIR)/pak0.pak
QUAKE_PLAY_ISO := $(BUILD)/kernel-quake-play.iso
QEMU_AUDIO_TEST := -audiodev none,id=demon_audio -device AC97,audiodev=demon_audio
QEMU_AUDIO_RUN := -audiodev pipewire,id=demon_audio -device AC97,audiodev=demon_audio
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
	-DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE -DDEMONOS -DFEATURE_SOUND \
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
	$(BUILD)/ramfs.o $(BUILD)/acpi.o $(BUILD)/ac97.o \
	$(BUILD)/kernel_cxx_runtime.o \
	$(BUILD)/kernel_bounded_table_test.o \
	$(BUILD)/kernel_slot_table_test.o \
	$(BUILD)/user_program_blob.o

.PHONY: all iso iso-check project portkit-check wine-pe-check mem-reserve-check wine-pe-load-check wine-pe-import-check wine-pe-reloc-check doom-source doom-platform-audit doom-engine-audit doom-runtime-audit doom-check classicube-source classicube-port-audit classicube-core quake-source quake-port-audit quake-core quake-smoke quake-data wolf3d-source wolf3d-source-audit nxengine-source nxengine-source-audit nxengine-platform-audit nxengine-data nxengine-core nxengine-smoke nxengine-play-iso nxengine-play-smoke nxengine-play-freeplay nxengine-play-freeplay-iso nxengine-play-freeplay-smoke quake-play-iso quake-play-smoke freedoom-assets freedoom-iso freedoom-play-iso freedoom-smoke freedoom-command-smoke qemu run run-doom run-cave-story run-quake run-wayland run-sdl run-vnc smoke framebuffer-fallback-smoke keyboard-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check check size clean FORCE

QUAKE_SOURCE := $(BUILD)/quake-upstream
QUAKE_COMMIT := bf4ac424ce754894ac8f1dae6a3981954bc9852d

quake-source: $(QUAKE_SOURCE)/.demonos-pinned

$(QUAKE_SOURCE)/.demonos-pinned: tools/fetch-quake.sh
	sh tools/fetch-quake.sh $(QUAKE_SOURCE)

quake-port-audit: quake-source
	@test "$$(git -C $(QUAKE_SOURCE) rev-parse HEAD)" = "$(QUAKE_COMMIT)"
	@grep -q 'void Sys_Printf' $(QUAKE_SOURCE)/WinQuake/sys.h
	@grep -q 'void Sys_Error' $(QUAKE_SOURCE)/WinQuake/sys.h
	@grep -q 'int Sys_FileOpenRead' $(QUAKE_SOURCE)/WinQuake/sys.h
	@grep -q 'double Sys_FloatTime' $(QUAKE_SOURCE)/WinQuake/sys.h
	@grep -q 'void Sys_SendKeyEvents' $(QUAKE_SOURCE)/WinQuake/sys.h
	@grep -q 'extern	viddef_t	vid' $(QUAKE_SOURCE)/WinQuake/vid.h
	@grep -q 'pixel_t			*buffer' $(QUAKE_SOURCE)/WinQuake/vid.h
	@grep -q 'void VID_Init' $(QUAKE_SOURCE)/WinQuake/vid.h
	@test -f ports/quake/platform/sys_demonos.c
	@test -f ports/quake/platform/com_demonos.c
	@test -f ports/quake/platform/libm_demonos.c
	@test -f ports/quake/platform/core_main.c
	@test -f ports/quake/platform/entry.S
	@echo "Quake D0 source/platform contract audit passed"

WOLF3D_SOURCE := $(BUILD)/wolf4sdl-upstream
WOLF3D_COMMIT := 5387b99d32fc5bac39c87defcb0abbf1018d8083

wolf3d-source: $(WOLF3D_SOURCE)/.demonos-pinned

$(WOLF3D_SOURCE)/.demonos-pinned: tools/fetch-wolf4sdl.sh
	sh tools/fetch-wolf4sdl.sh $(WOLF3D_SOURCE)

# D0: source-contract-only audit. Wolf4SDL has no clean Sys_/VID_/IN_
# boundary like WinQuake -- id_vl.cpp/id_in.cpp/id_sd.cpp/id_pm.cpp call
# SDL_Surface/SDL_Color/SDL_RWops directly throughout, so the DemonOS
# platform layer will replace those four files' bodies wholesale (same
# shape as sys_demonos.c/vid_demonos.c) rather than filling in a thin
# stub header. No ports/wolf3d/platform files exist yet -- this only
# proves the pinned upstream still has the symbols that replacement will
# target.
wolf3d-source-audit: wolf3d-source
	@test "$$(git -C $(WOLF3D_SOURCE) rev-parse HEAD)" = "$(WOLF3D_COMMIT)"
	@grep -q 'VL_SetPalette' $(WOLF3D_SOURCE)/id_vl.h
	@grep -Eq 'IN_ProcessEvents|IN_ReadControl' $(WOLF3D_SOURCE)/id_in.h
	@grep -q 'PM_Startup' $(WOLF3D_SOURCE)/id_pm.h
	@grep -q 'SD_Startup' $(WOLF3D_SOURCE)/id_sd.h
	@echo "Wolf3D D0 source contract audit passed"

NXENGINE_SOURCE := $(BUILD)/nxengine-upstream
NXENGINE_COMMIT := 16bf776febef2a041cf07677f663c4cca2e810a1

nxengine-source: $(NXENGINE_SOURCE)/.demonos-pinned

$(NXENGINE_SOURCE)/.demonos-pinned: tools/fetch-nxengine.sh
	sh tools/fetch-nxengine.sh $(NXENGINE_SOURCE)

# D0: source-contract-only audit (see docs/nxengine-port.md). NXEngine's
# SDL dependency is concentrated in graphics/nxsurface.cpp (the actual
# SDL_Surface backing), input.cpp, sound/sound.cpp, and main.cpp -- the
# Graphics:: namespace and NXSurface's public interface are already a
# clean, engine-facing seam (unlike Wolf4SDL), so the platform layer only
# needs to replace those four files' internals, same shape as
# vid_demonos.c/in_demonos.c in the Quake port.
nxengine-source-audit: nxengine-source
	@test "$$(git -C $(NXENGINE_SOURCE) rev-parse HEAD)" = "$(NXENGINE_COMMIT)"
	@grep -q 'class NXSurface' $(NXENGINE_SOURCE)/graphics/nxsurface.h
	@grep -q 'namespace Graphics' $(NXENGINE_SOURCE)/graphics/graphics.h
	@grep -q 'int main' $(NXENGINE_SOURCE)/main.cpp
	@echo "NXEngine D0 source contract audit passed"

NXENGINE_BUILD := $(BUILD)/nxengine
NXENGINE_CORE_ELF := $(BUILD)/nxengine-core.elf

# Real freestanding flags (matching DOOM_CFLAGS/QUAKE_CFLAGS), not host
# g++: earlier passes of this audit compiled against the host's libstdc++,
# which masked that <cmath>/<cstdlib> refuse to build in freestanding C++
# mode. ports/nxengine/platform/math.h and stdlib.h shadow just the
# declarations engine units need, backed by the same freestanding libm/libc
# already used by the Quake/Doom ports.
NXENGINE_CXXFLAGS := -std=c++11 -Os -ffreestanding -fno-builtin \
	-fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections -m64 -mno-red-zone -msse2 \
	-Iports/nxengine/platform -I$(NXENGINE_SOURCE)

$(NXENGINE_BUILD):
	mkdir -p $@

$(NXENGINE_BUILD)/sdl_demonos.o: ports/nxengine/platform/sdl_demonos.c | $(NXENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -Iports/nxengine/platform -c $< -o $@

$(NXENGINE_BUILD)/entry.o: ports/nxengine/platform/entry.S | $(NXENGINE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/core_main.o: ports/nxengine/platform/core_main.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iinclude -c $< -o $@

$(NXENGINE_BUILD)/trig.o: $(NXENGINE_SOURCE)/trig.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/libm_demonos.o: ports/quake/platform/libm_demonos.c | $(NXENGINE_BUILD)
	$(CC) $(DOOM_CFLAGS) -Iinclude -c $< -o $@

$(NXENGINE_BUILD)/nxsurface.o: $(NXENGINE_SOURCE)/graphics/nxsurface.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/map.o: $(NXENGINE_SOURCE)/map.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

# tileset.cpp relies on <stdio.h> being visible transitively (real SDL.h
# pulls it in on a normal desktop build); force it directly rather than
# editing the real, unmodified upstream file.
$(NXENGINE_BUILD)/tileset.o: $(NXENGINE_SOURCE)/graphics/tileset.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/stagedata.o: $(NXENGINE_SOURCE)/stagedata.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

# siflib (sprites.sif reader) + the small common/ container classes it
# needs (BList/DBuffer/DString/StringList/bufio) -- all real, unmodified,
# self-contained utility code, no game-specific dependencies of their own.
NXENGINE_SIFLIB_OBJS := $(NXENGINE_BUILD)/sif.o $(NXENGINE_BUILD)/sifloader.o \
	$(NXENGINE_BUILD)/sectSprites.o $(NXENGINE_BUILD)/sectStringArray.o \
	$(NXENGINE_BUILD)/BList.o $(NXENGINE_BUILD)/DBuffer.o \
	$(NXENGINE_BUILD)/DString.o $(NXENGINE_BUILD)/StringList.o \
	$(NXENGINE_BUILD)/bufio.o $(NXENGINE_BUILD)/sprites.o \
	$(NXENGINE_BUILD)/object.o $(NXENGINE_BUILD)/ai.o $(NXENGINE_BUILD)/InitList.o \
	$(NXENGINE_BUILD)/slope.o $(NXENGINE_BUILD)/graphics.o $(NXENGINE_BUILD)/floattext.o \
	$(NXENGINE_BUILD)/caret.o $(NXENGINE_BUILD)/screeneffect.o $(NXENGINE_BUILD)/font.o \
	$(NXENGINE_BUILD)/misc_comm.o $(NXENGINE_BUILD)/profile.o \
	$(NXENGINE_BUILD)/input.o $(NXENGINE_BUILD)/settings.o \
	$(NXENGINE_BUILD)/TextBox.o $(NXENGINE_BUILD)/ItemImage.o \
	$(NXENGINE_BUILD)/YesNoPrompt.o $(NXENGINE_BUILD)/CredReader.o \
	$(NXENGINE_BUILD)/dialog.o \
	$(NXENGINE_BUILD)/player.o $(NXENGINE_BUILD)/ObjManager.o \
	$(NXENGINE_BUILD)/p_arms.o $(NXENGINE_BUILD)/playerstats.o \
	$(NXENGINE_BUILD)/statusbar.o $(NXENGINE_BUILD)/whimstar.o \
	$(NXENGINE_BUILD)/smoke.o $(NXENGINE_BUILD)/weapons.o $(NXENGINE_BUILD)/polar_mgun.o \
	$(NXENGINE_BUILD)/first_cave.o $(NXENGINE_BUILD)/weed.o $(NXENGINE_BUILD)/puppy.o \
	$(NXENGINE_BUILD)/debug.o $(NXENGINE_BUILD)/objnames.o \
	$(NXENGINE_BUILD)/SaveSelect.o $(NXENGINE_BUILD)/StageSelect.o \
	$(NXENGINE_BUILD)/tsc.o

$(NXENGINE_BUILD)/sif.o: $(NXENGINE_SOURCE)/siflib/sif.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/sifloader.o: $(NXENGINE_SOURCE)/siflib/sifloader.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/sectSprites.o: $(NXENGINE_SOURCE)/siflib/sectSprites.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/sectStringArray.o: $(NXENGINE_SOURCE)/siflib/sectStringArray.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/BList.o: $(NXENGINE_SOURCE)/common/BList.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/DBuffer.o: $(NXENGINE_SOURCE)/common/DBuffer.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/DString.o: $(NXENGINE_SOURCE)/common/DString.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/StringList.o: $(NXENGINE_SOURCE)/common/StringList.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/bufio.o: $(NXENGINE_SOURCE)/common/bufio.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/sprites.o: $(NXENGINE_SOURCE)/graphics/sprites.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# object.cpp (the real Object base class), ai.cpp (only load_npc_tbl() is
# called; --gc-sections is betting on pruning ai_init/AIRoutines and the
# rest of the NPC dispatch system, same bet as map.cpp/tileset.cpp/
# sprites.cpp before it), and InitList.cpp (small, self-contained,
# ai.cpp's global AIRoutines needs the type even if CallFunctions() is
# never reached).
$(NXENGINE_BUILD)/object.o: $(NXENGINE_SOURCE)/object.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/ai.o: $(NXENGINE_SOURCE)/ai/ai.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/InitList.o: $(NXENGINE_SOURCE)/common/InitList.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

# Object::apply_xinertia/UpdateBlockStates unconditionally call these
# slope-handling functions (branches are runtime, not compile-time, even
# though this stage's player Object never sets NXFLAG_FOLLOW_SLOPE) --
# real, unmodified, and self-contained (only needs map/sprites/tileattr,
# all already linked).
$(NXENGINE_BUILD)/slope.o: $(NXENGINE_SOURCE)/slope.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# graphics.cpp: only its trivial set_clip_rect/clear_clip_rect/
# SetDrawTarget one-liners are ever called (see the comment on
# SDL_SetVideoMode in SDL.h) -- Graphics::init/InitVideo/close/
# SetResolution need extra SDL declarations to *compile* but
# --gc-sections drops the functions themselves at link time since
# nothing calls them.
$(NXENGINE_BUILD)/graphics.o: $(NXENGINE_SOURCE)/graphics/graphics.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -include stdlib.h -c $< -o $@

$(NXENGINE_BUILD)/floattext.o: $(NXENGINE_SOURCE)/floattext.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# caret.cpp (particle effects, e.g. the star-poof dissipation effect):
# every symbol it needs (map/sprites/draw_sprite/random/staterr,
# vector_from_angle from trig.cpp) is already linked -- zero new files
# beyond this one.
$(NXENGINE_BUILD)/caret.o: $(NXENGINE_SOURCE)/caret.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# screeneffect.cpp (FlashScreen/Starflash/Fade screen overlays): every real
# symbol it needs (Graphics::ClearScreen/FillRect, map.displayed_xscroll/
# yscroll, draw_sprite) is already linked; sound() is declared but stubbed
# in core_main.cpp since no audio backend exists yet in this port.
$(NXENGINE_BUILD)/screeneffect.o: $(NXENGINE_SOURCE)/screeneffect.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# graphics/font.cpp (bitmap text rendering, whitefont/greenfont/bluefont/
# shadowfont): real, unmodified. Its compile-time CONFIG_ENABLE_TTF branch
# is never runtime-reached (SCALE == 1 always selects the adjacent bitmap
# path) but is still compiled into font_init/NXFont::InitChars as one
# function body, so the SDL_ttf calls in that dead branch need real
# linkable symbols -- ports/nxengine/platform/SDL/SDL_ttf.h + trivial
# failure stubs in sdl_demonos.c, never actually invoked.
$(NXENGINE_BUILD)/font.o: $(NXENGINE_SOURCE)/graphics/font.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# common/misc_comm.cpp: the real fgeti/fgetl/fputi/fputl/fverifystring/
# random/stprintf/file_exists helper library, self-contained apart from
# fileopen/stat/staterr (already provided) and a handful of small libc
# pieces this port hadn't needed before (atof/exit/__errno_location/
# strerror -- added directly in core_main.cpp). Supersedes this file's
# earlier hand-rolled fverifystring/fgeti/fgetl/random stubs.
$(NXENGINE_BUILD)/misc_comm.o: $(NXENGINE_SOURCE)/common/misc_comm.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# profile.cpp: real save-file (Profile struct) binary load/save, using
# misc_comm.cpp's real fgeti/fgetl/fputi/fputl/fverifystring and this
# port's existing CVTDir stub (tsc.cpp's real one isn't linked -- see the
# comment on CVTDir in core_main.cpp).
$(NXENGINE_BUILD)/profile.o: $(NXENGINE_SOURCE)/profile.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# input.cpp: real key-mapping tables and buttondown/justpushed/
# input_get_mapping/input_set_mappings. input_poll() itself references
# console.cpp/replay.cpp globals (Console, Replay -- both deliberately
# out of scope) but is never called by this port (D16 populates
# inputs[]/mappings[] directly through the real lower-level functions,
# the same "bypass the orchestration layer, call the real primitives"
# pattern as D8's load_tileattr vs initmapfirsttime); --gc-sections
# drops input_poll's whole section, so those symbols never need to
# resolve.
$(NXENGINE_BUILD)/input.o: $(NXENGINE_SOURCE)/input.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# settings.cpp: real settings.dat save/load. Real upstream includes
# <SDL.h> (bare, unlike every other file's <SDL/SDL.h>) -- an extra -I
# onto this port's own SDL/ shim directory resolves it to the same real
# shim, not a second implementation.
$(NXENGINE_BUILD)/settings.o: $(NXENGINE_SOURCE)/settings.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# TextBox/TextBox.cpp: only the static TextBox::DrawFrame is ever called
# (the dialogue-box frame-drawing sprite chop, real and self-contained --
# needs just Sprites::draw_sprite_chopped, already linked since D9).
# TextBox::Draw/Init/etc (which touch TB_SaveSelect/TB_StageSelect/
# TB_YNJPrompt and player/game.mode state) are never called, so
# --gc-sections drops them, the same pattern already proven for D16's
# input.cpp/input_poll.
$(NXENGINE_BUILD)/TextBox.o: $(NXENGINE_SOURCE)/TextBox/TextBox.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# TextBox/ItemImage.cpp: the real "item get" popup animation (used when
# picking up a new weapon/life capsule), self-contained apart from
# TextBox::DrawFrame/Sprites::draw_sprite, both already linked.
$(NXENGINE_BUILD)/ItemImage.o: $(NXENGINE_SOURCE)/TextBox/ItemImage.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# TextBox/YesNoPrompt.cpp: the real Yes/No confirmation prompt (used for
# "really quit?"/teleporter confirm dialogs), self-contained apart from
# justpushed (D16) and sound (stubbed).
$(NXENGINE_BUILD)/YesNoPrompt.o: $(NXENGINE_SOURCE)/TextBox/YesNoPrompt.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -include stdio.h -c $< -o $@

# endgame/CredReader.cpp: the real credits-script (Credit.tsc) parser.
# Its own #include "../nx.h" pulls in the full real aggregate header
# (including player.h/game.h -- declarations only, harmless, since
# CredReader.cpp itself never references player/game), so needs the same
# extra -I as settings.cpp for <SDL.h>. tsc_decrypt is declared in
# tsc.cpp (the deferred script-engine wall) but is copied verbatim into
# core_main.cpp -- a small, genuinely self-contained XOR-style file
# decrypt with no other tsc.cpp dependency, not a stub.
$(NXENGINE_BUILD)/CredReader.o: $(NXENGINE_SOURCE)/endgame/CredReader.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# pause/dialog.cpp: the real generic choice-list menu widget (Options::
# Dialog) used for the pause/options screens. Self-contained apart from
# Options::optionstack (a plain BList subclass, defined directly in
# core_main.cpp rather than linking all of options.cpp -- see the
# comment there) and justpushed/buttonjustpushed/font_draw/TextBox::
# DrawFrame, all already linked.
$(NXENGINE_BUILD)/dialog.o: $(NXENGINE_SOURCE)/pause/dialog.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D21: the real Player class and its supporting real subsystems -- see
# the docs/nxengine-port.md D21 entry for the full scope/stub rationale.
$(NXENGINE_BUILD)/player.o: $(NXENGINE_SOURCE)/player.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/ObjManager.o: $(NXENGINE_SOURCE)/ObjManager.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/p_arms.o: $(NXENGINE_SOURCE)/p_arms.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/playerstats.o: $(NXENGINE_SOURCE)/playerstats.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/statusbar.o: $(NXENGINE_SOURCE)/statusbar.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/whimstar.o: $(NXENGINE_SOURCE)/ai/weapons/whimstar.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# object.cpp's Object::Kill() calls SmokeClouds unconditionally;
# whimstar.cpp's ai_whimsical_star() calls check_hit_enemy unconditionally.
# ai/sym/smoke.cpp and ai/weapons/weapons.cpp are each small, self-contained
# real effect/helper files (--gc-sections drops the rest of weapons.cpp's
# actual weapon-collision AI, never called here).
$(NXENGINE_BUILD)/smoke.o: $(NXENGINE_SOURCE)/ai/sym/smoke.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/weapons.o: $(NXENGINE_SOURCE)/ai/weapons/weapons.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# ai/weapons/polar_mgun.cpp: real Polar Star/MGun shot per-tick behavior
# (ai_polar_shot -- ttl countdown, wall/enemy collision, real deletion).
# Its INITFUNC(AIRoutines) is the real registration mechanism: a static
# global object whose constructor (run via .init_array at boot, the D9
# fix) calls AIRoutines.AddFunction(...), so calling the real, already-
# linked AIRoutines.CallFunctions() once at setup wires
# objprop[OBJ_POLAR_SHOT].ai_routines.ontick = ai_polar_shot for real,
# instead of it staying null.
$(NXENGINE_BUILD)/polar_mgun.o: $(NXENGINE_SOURCE)/ai/weapons/polar_mgun.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D23: real NPCs -- ai/first_cave/first_cave.cpp registers OBJ_DOOR_ENEMY/
# OBJ_BAT_BLUE/OBJ_HERMIT_GUNSMITH/OBJ_CRITTER_HOPPING_BLUE via the same
# real AIRoutines/INITFUNC mechanism D22 wired up. Its INITFUNC takes
# ai_critter's (ai/weed/weed.cpp) and ai_zzzz_spawner's (ai/sand/
# puppy.cpp) addresses unconditionally (ONTICK(...)), so both must link
# too even though this stage only spawns OBJ_DOOR_ENEMY, which is fully
# self-contained within first_cave.cpp itself. weed.cpp's own INITFUNC
# in turn takes ai_giant_jelly's address, which needs quake() (defined
# directly in core_main.cpp -- see the comment there).
$(NXENGINE_BUILD)/first_cave.o: $(NXENGINE_SOURCE)/ai/first_cave/first_cave.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/weed.o: $(NXENGINE_SOURCE)/ai/weed/weed.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/puppy.o: $(NXENGINE_SOURCE)/ai/sand/puppy.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D24: debug.cpp is only linked for DescribeObjectType (called by
# map.cpp's real load_entities, D24's target) -- everything else in this
# file (DrawDebug, DrawBoundingBoxes, the debug console command table)
# is never called by anything this port reaches, so --gc-sections drops
# it, including a never-called SDL_SaveBMP reference (declaration-only
# stub in SDL.h).
$(NXENGINE_BUILD)/debug.o: $(NXENGINE_SOURCE)/debug.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# autogen/objnames.cpp: the real, auto-generated object-type-name string
# table DescribeObjectType() indexes into. Pure data, no logic.
$(NXENGINE_BUILD)/objnames.o: $(NXENGINE_SOURCE)/autogen/objnames.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

# D26: TextBox/SaveSelect.cpp + TextBox/StageSelect.cpp -- the real
# save-file-picker and stage-select/teleporter TextBox sub-widgets.
# tsc.cpp's real ExecScript calls their member functions directly
# (textbox.SaveSelect.SetVisible/textbox.StageSelect.SetSlot/SetVisible)
# for the <SVP/<PSP/<PS+ commands, so unlike D17/D18 (where TextBox.cpp
# was linked only for its static DrawFrame and these two sub-widgets'
# code was pruned by --gc-sections since nothing called them), tsc.cpp
# now genuinely reaches them. Both are real, unmodified, and their own
# footprint (font_draw/GetFontWidth/whitefont, DrawNumberRAlign/
# DrawPercentage, buttondown/buttonjustpushed/justpushed,
# GetSpriteForGun, CheckInventoryList, profile_load/GetProfileName,
# map_get_stage_name) was already linked by D9/D14/D16/D21/D5 -- zero
# new subsystems needed beyond these two files themselves.
$(NXENGINE_BUILD)/SaveSelect.o: $(NXENGINE_SOURCE)/TextBox/SaveSelect.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/StageSelect.o: $(NXENGINE_SOURCE)/TextBox/StageSelect.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D26: tsc.cpp -- the real TSC script interpreter driving all NPC
# dialogue/cutscenes/map-entry events. Real, unmodified. See the D26
# entry in docs/nxengine-port.md and the stub-removal/new-stub comments
# in core_main.cpp for the exact scope line (boss-fight state, the
# .org music player, credits *playback*, and the dedicated inventory/
# save/niku-timer subsystems stay honestly stubbed; everything else
# tsc.cpp's real ExecScript needs was already linked by D1-D25).
$(NXENGINE_BUILD)/tsc.o: $(NXENGINE_SOURCE)/tsc.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D32: the real stage-boss system. stageboss.cpp (the real
# StageBossManager: SetType/Type/SetState/Run/RunAftermove/OnMapEntry/
# OnMapExit), all nine real ai/boss/*.cpp AIs (registered through the same
# real INITFUNC/AIRoutines mechanism D22-D23 wired up -- their static
# init objects run via .init_array at boot), ai/IrregularBBox.cpp (the
# shield system Heavy Press and Ballos use), ai/sym/sym.cpp (the shared
# small-helper library the boss AIs call: ai_animate4/ai_smokecloud/
# SpawnObjectAtActionPoint/EmFireAngledShot/hitdetect/transfer_damage,
# superseding the D13-era hand-rolled copies -- those had to go so this
# stage's Heavy Press fight doesn't mix two conflicting implementations
# of the same functions), and autogen/AssignSprites.cpp (the real sprite
# assignment table Game::init calls; gives OBJ_HEAVY_PRESS its real
# SPR_HEAVY_PRESS per-frame bboxes, without which OnMapEntry's bbox reads
# would overrun SPR_NULL's single frame). All real, unmodified, all
# needing only the already-linked object/map/sprite/player/sound/tile
# seams -- the same bet as every prior stage, now proven by D32's fight.
# Note stageboss.cpp needs SDL.h like the other ../nx.h includers.
NXENGINE_D32_OBJS := $(NXENGINE_BUILD)/stageboss.o \
	$(NXENGINE_BUILD)/IrregularBBox.o $(NXENGINE_BUILD)/sym.o \
	$(NXENGINE_BUILD)/AssignSprites.o \
	$(NXENGINE_BUILD)/balfrog.o $(NXENGINE_BUILD)/ballos.o \
	$(NXENGINE_BUILD)/core.o $(NXENGINE_BUILD)/heavypress.o \
	$(NXENGINE_BUILD)/ironhead.o $(NXENGINE_BUILD)/omega.o \
	$(NXENGINE_BUILD)/sisters.o $(NXENGINE_BUILD)/undead_core.o \
	$(NXENGINE_BUILD)/x.o

$(NXENGINE_BUILD)/stageboss.o: $(NXENGINE_SOURCE)/stageboss.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/IrregularBBox.o: $(NXENGINE_SOURCE)/ai/IrregularBBox.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/sym.o: $(NXENGINE_SOURCE)/ai/sym/sym.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/AssignSprites.o: $(NXENGINE_SOURCE)/autogen/AssignSprites.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/balfrog.o: $(NXENGINE_SOURCE)/ai/boss/balfrog.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/ballos.o: $(NXENGINE_SOURCE)/ai/boss/ballos.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/core.o: $(NXENGINE_SOURCE)/ai/boss/core.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/heavypress.o: $(NXENGINE_SOURCE)/ai/boss/heavypress.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/ironhead.o: $(NXENGINE_SOURCE)/ai/boss/ironhead.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/omega.o: $(NXENGINE_SOURCE)/ai/boss/omega.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/sisters.o: $(NXENGINE_SOURCE)/ai/boss/sisters.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/undead_core.o: $(NXENGINE_SOURCE)/ai/boss/undead_core.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/x.o: $(NXENGINE_SOURCE)/ai/boss/x.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D34: three new, real, self-contained translation units -- the real
# in-game inventory screen (inventory.cpp), the real pause screen
# (pause/pause.cpp), and the real 290.rec Nikumaru-counter save/load pair
# (niku.cpp), which niku_save (game_save's sibling, <STC) now calls for
# real instead of the old stub. Standalone-compiled + nm -u'd each before
# linking (see docs/nxengine-port.md's D34 write-up): inventory.cpp's only
# genuinely new symbol was DrawScene (game.cpp, given a real bounded
# reimplementation directly in core_main.cpp -- see the comment there);
# everything else (weapon_slide/DrawWeaponLevel/DrawWeaponAmmo from
# statusbar.cpp, StartScript/StopScripts/GetCurrentScript from tsc.cpp,
# justpushed/buttonjustpushed from input.cpp, TextBox::Draw/DrawFrame,
# Sprites::draw_sprite) was already linked since D17/D21/D26/D29.
# pause/pause.cpp needed nothing new beyond Game::pause/Game::reset
# (both real now, see core_main.cpp). niku.cpp needed nothing beyond
# fileopen/fread/fwrite/fclose/random/stat/staterr, all already linked.
NXENGINE_D34_OBJS := $(NXENGINE_BUILD)/inventory.o \
	$(NXENGINE_BUILD)/pause.o $(NXENGINE_BUILD)/niku.o

$(NXENGINE_BUILD)/inventory.o: $(NXENGINE_SOURCE)/inventory.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/pause.o: $(NXENGINE_SOURCE)/pause/pause.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/niku.o: $(NXENGINE_SOURCE)/niku.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

# D35: real demo/replay recording + playback. replay.cpp links unmodified
# -- standalone-compile + nm -u (see docs/nxengine-port.md's D35 write-up)
# showed every dependency already satisfied by prior stages except
# common/FileBuffer.cpp (the small buffered-file writer replay.cpp's real
# RLE input-log format uses; its own DBuffer dependency was already
# linked since D11's siflib pull) and game_load(Profile*)/flipacceltime,
# both supplied directly in core_main.cpp (see the comment there).
NXENGINE_D35_OBJS := $(NXENGINE_BUILD)/FileBuffer.o $(NXENGINE_BUILD)/replay.o

$(NXENGINE_BUILD)/FileBuffer.o: $(NXENGINE_SOURCE)/common/FileBuffer.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -c $< -o $@

$(NXENGINE_BUILD)/replay.o: $(NXENGINE_SOURCE)/replay.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iports/nxengine/platform/SDL -include stdio.h -c $< -o $@

$(NXENGINE_BUILD)/stdio_demonos.o: ports/quake/platform/stdio_demonos.c | $(NXENGINE_BUILD)
	$(CC) -std=gnu89 -Os -ffreestanding -fno-builtin -fno-stack-protector \
		-fno-pie -fno-pic -ffunction-sections -fdata-sections -m64 -mno-red-zone \
		-msse2 -fcommon -Iinclude -c $< -o $@

# D1: prove the SDL1-subset shim (SDL/SDL.h + sdl_demonos.c) is enough to
# build NXEngine's real, unmodified graphics/nxsurface.cpp -- the
# engine-facing surface/blit seam described in docs/nxengine-port.md --
# without pulling in a display/present path yet (SDL_Flip is a stub; see
# the comment in sdl_demonos.c). Mirrors doom-platform-audit's shape: link
# and inspect unresolved symbols rather than asserting a full boot.
nxengine-platform-audit: $(NXENGINE_BUILD)/sdl_demonos.o nxengine-source
	@nm -u $< | awk '{print $$2}' | sort -u > $(NXENGINE_BUILD)/sdl-unresolved.txt
	@! grep -Eq '^SDL_' $(NXENGINE_BUILD)/sdl-unresolved.txt
	@g++ $(NXENGINE_CXXFLAGS) \
		-c $(NXENGINE_SOURCE)/graphics/nxsurface.cpp \
		-o $(NXENGINE_BUILD)/nxsurface.o
	@nm -u $(NXENGINE_BUILD)/nxsurface.o | awk '{print $$2}' | sort -u > $(NXENGINE_BUILD)/nxsurface-unresolved.txt
	@grep -q '^SDL_CreateRGBSurface$$' $(NXENGINE_BUILD)/nxsurface-unresolved.txt
	@grep -q 'staterr' $(NXENGINE_BUILD)/nxsurface-unresolved.txt
	@echo "NXEngine D1 nxsurface.cpp/SDL-shim compile audit passed"
	@g++ $(NXENGINE_CXXFLAGS) \
		-c $(NXENGINE_SOURCE)/input.cpp \
		-o $(NXENGINE_BUILD)/input.o
	@nm -u $(NXENGINE_BUILD)/input.o | awk '{print $$2}' | sort -u > $(NXENGINE_BUILD)/input-unresolved.txt
	@grep -q 'SDL_PollEvent' $(NXENGINE_BUILD)/input-unresolved.txt
	@! grep -Eq '^SDLK_|^SDL_KEY' $(NXENGINE_BUILD)/input-unresolved.txt
	@echo "NXEngine D1 input.cpp/SDL-shim compile audit passed"

# D1 boot: the real, unmodified trig.cpp linked into an actual bootable ELF
# through this port's own entry point (core_main.cpp, not upstream's
# main.cpp -- see docs/nxengine-port.md), proving the whole freestanding
# C++/libm/PortKit/linker chain produces correct results, not just that it
# compiles.
$(NXENGINE_CORE_ELF): $(NXENGINE_BUILD)/entry.o $(NXENGINE_BUILD)/core_main.o \
		$(NXENGINE_BUILD)/trig.o $(NXENGINE_BUILD)/libm_demonos.o \
		$(NXENGINE_BUILD)/sdl_demonos.o $(NXENGINE_BUILD)/nxsurface.o \
		$(NXENGINE_BUILD)/map.o $(NXENGINE_BUILD)/stdio_demonos.o \
		$(NXENGINE_BUILD)/tileset.o $(NXENGINE_BUILD)/stagedata.o \
		$(NXENGINE_SIFLIB_OBJS) $(NXENGINE_D32_OBJS) $(NXENGINE_D34_OBJS) $(NXENGINE_D35_OBJS) \
		$(BUILD)/doom_libc.o $(BUILD)/portkit.o ports/nxengine/linker.ld
	$(LD) -nostdlib --gc-sections -z max-page-size=0x1000 -T ports/nxengine/linker.ld \
		$(NXENGINE_BUILD)/entry.o $(BUILD)/portkit.o $(BUILD)/doom_libc.o \
		$(NXENGINE_BUILD)/libm_demonos.o $(NXENGINE_BUILD)/sdl_demonos.o \
		$(NXENGINE_BUILD)/nxsurface.o $(NXENGINE_BUILD)/map.o \
		$(NXENGINE_BUILD)/stdio_demonos.o $(NXENGINE_BUILD)/tileset.o \
		$(NXENGINE_BUILD)/stagedata.o $(NXENGINE_SIFLIB_OBJS) \
		$(NXENGINE_D32_OBJS) $(NXENGINE_D34_OBJS) $(NXENGINE_D35_OBJS) \
		$(NXENGINE_BUILD)/trig.o \
		$(NXENGINE_BUILD)/core_main.o -o $@
	$(STRIP) -s $@

nxengine-core: $(NXENGINE_CORE_ELF)
	@readelf -h $< | grep -q 'Class:.*ELF64'
	@echo "NXEngine D1 core ELF linked"

# D37: freeplay -- the same core_main.cpp, compiled a second time with
# -DNXENGINE_FREEPLAY, which switches which of the two nxengine_core_main
# bodies gets compiled (see the #ifndef/#else/#endif in core_main.cpp).
# Every other object file is reused byte-for-byte from the D1-D36 build
# above -- this mode adds zero new engine subsystems, only a new entry
# point built from already-linked real primitives.
NXENGINE_FREEPLAY_ELF := $(BUILD)/nxengine-play-freeplay.elf

$(NXENGINE_BUILD)/core_main_freeplay.o: ports/nxengine/platform/core_main.cpp nxengine-source | $(NXENGINE_BUILD)
	g++ $(NXENGINE_CXXFLAGS) -Iinclude -DNXENGINE_FREEPLAY -c $< -o $@

$(NXENGINE_FREEPLAY_ELF): $(NXENGINE_BUILD)/entry.o $(NXENGINE_BUILD)/core_main_freeplay.o \
		$(NXENGINE_BUILD)/trig.o $(NXENGINE_BUILD)/libm_demonos.o \
		$(NXENGINE_BUILD)/sdl_demonos.o $(NXENGINE_BUILD)/nxsurface.o \
		$(NXENGINE_BUILD)/map.o $(NXENGINE_BUILD)/stdio_demonos.o \
		$(NXENGINE_BUILD)/tileset.o $(NXENGINE_BUILD)/stagedata.o \
		$(NXENGINE_SIFLIB_OBJS) $(NXENGINE_D32_OBJS) $(NXENGINE_D34_OBJS) $(NXENGINE_D35_OBJS) \
		$(BUILD)/doom_libc.o $(BUILD)/portkit.o ports/nxengine/linker.ld
	$(LD) -nostdlib --gc-sections -z max-page-size=0x1000 -T ports/nxengine/linker.ld \
		$(NXENGINE_BUILD)/entry.o $(BUILD)/portkit.o $(BUILD)/doom_libc.o \
		$(NXENGINE_BUILD)/libm_demonos.o $(NXENGINE_BUILD)/sdl_demonos.o \
		$(NXENGINE_BUILD)/nxsurface.o $(NXENGINE_BUILD)/map.o \
		$(NXENGINE_BUILD)/stdio_demonos.o $(NXENGINE_BUILD)/tileset.o \
		$(NXENGINE_BUILD)/stagedata.o $(NXENGINE_SIFLIB_OBJS) \
		$(NXENGINE_D32_OBJS) $(NXENGINE_D34_OBJS) $(NXENGINE_D35_OBJS) \
		$(NXENGINE_BUILD)/trig.o \
		$(NXENGINE_BUILD)/core_main_freeplay.o -o $@
	$(STRIP) -s $@

nxengine-play-freeplay: $(NXENGINE_FREEPLAY_ELF)
	@readelf -h $< | grep -q 'Class:.*ELF64'
	@echo "NXEngine D37 freeplay ELF linked"

NXENGINE_DATA := $(BUILD)/nxengine-data

nxengine-data: tools/fetch-cavestory-data.sh
	sh tools/fetch-cavestory-data.sh $(NXENGINE_DATA)

NXENGINE_PLAY_ISO := $(BUILD)/kernel-nxengine-play.iso

nxengine-play-iso: $(NXENGINE_PLAY_ISO)

$(NXENGINE_PLAY_ISO): $(ISO) nxengine-data nxengine-source grub/grub-nxengine-play.cfg
	rm -rf $(BUILD)/iso-nxengine-play
	cp -r $(ISO_ROOT) $(BUILD)/iso-nxengine-play
	mkdir -p $(BUILD)/iso-nxengine-play/games/nxengine
	cp $(NXENGINE_DATA)/CaveStory/data/Bullet.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Bullet.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/casts.pbm $(BUILD)/iso-nxengine-play/games/nxengine/casts.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/0.pxm $(BUILD)/iso-nxengine-play/games/nxengine/0.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Prt0.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Prt0.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxm $(BUILD)/iso-nxengine-play/games/nxengine/Pens1.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtPens.pbm $(BUILD)/iso-nxengine-play/games/nxengine/PrtPens.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/MyChar.pbm $(BUILD)/iso-nxengine-play/games/nxengine/MyChar.pbm
	cp $(NXENGINE_SOURCE)/tilekey.dat $(BUILD)/iso-nxengine-play/games/nxengine/tilekey.dat
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens.pxa $(BUILD)/iso-nxengine-play/games/nxengine/Pens.pxa
	cp $(NXENGINE_SOURCE)/sprites.sif $(BUILD)/iso-nxengine-play/games/nxengine/sprites.sif
	cp $(NXENGINE_DATA)/CaveStory/data/npc.tbl $(BUILD)/iso-nxengine-play/games/nxengine/npc.tbl
	cp $(NXENGINE_DATA)/CaveStory/data/TextBox.pbm $(BUILD)/iso-nxengine-play/games/nxengine/TextBox.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Caret.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Caret.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Fade.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Fade.pbm
	cp $(NXENGINE_SOURCE)/smalfont.bmp $(BUILD)/iso-nxengine-play/games/nxengine/smalfont.bmp
	cp $(NXENGINE_DATA)/CaveStory/data/Credit.tsc $(BUILD)/iso-nxengine-play/games/nxengine/Credit.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxe $(BUILD)/iso-nxengine-play/games/nxengine/Pens1.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Head.tsc $(BUILD)/iso-nxengine-play/games/nxengine/Head.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsItem.tsc $(BUILD)/iso-nxengine-play/games/nxengine/ArmsItem.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/StageSelect.tsc $(BUILD)/iso-nxengine-play/games/nxengine/StageSelect.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxm $(BUILD)/iso-nxengine-play/games/nxengine/Start.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxe $(BUILD)/iso-nxengine-play/games/nxengine/Start.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxm $(BUILD)/iso-nxengine-play/games/nxengine/Frog.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxe $(BUILD)/iso-nxengine-play/games/nxengine/Frog.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.tsc $(BUILD)/iso-nxengine-play/games/nxengine/Frog.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtWeed.pbm $(BUILD)/iso-nxengine-play/games/nxengine/PrtWeed.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Weed.pxa $(BUILD)/iso-nxengine-play/games/nxengine/Weed.pxa
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsImage.pbm $(BUILD)/iso-nxengine-play/games/nxengine/ArmsImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Arms.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Arms.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Npc/NpcSym.pbm $(BUILD)/iso-nxengine-play/games/nxengine/NpcSym.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/ItemImage.pbm $(BUILD)/iso-nxengine-play/games/nxengine/ItemImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Title.pbm $(BUILD)/iso-nxengine-play/games/nxengine/Title.pbm
	cp grub/grub-nxengine-play.cfg $(BUILD)/iso-nxengine-play/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-nxengine-play >/dev/null 2>&1

nxengine-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/nxengine-serial.log $(BUILD)/nxengine-monitor.sock
	@timeout 40s qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/nxengine-serial.log -display none \
		-monitor unix:$(BUILD)/nxengine-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/nxengine-monitor.sock && grep -q "mako#" $(BUILD)/nxengine-serial.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey n\nsendkey x\nsendkey e\nsendkey n\nsendkey g\nsendkey i\nsendkey n\nsendkey e\nsendkey minus\nsendkey c\nsendkey o\nsendkey r\nsendkey e\n'; \
		  printf 'sendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/nxengine-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "process exited with status" $(BUILD)/nxengine-serial.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "NXENGINE_D1_PORTKIT_READY" $(BUILD)/nxengine-serial.log
	@grep -q "NXENGINE_D1_TRIG_READY" $(BUILD)/nxengine-serial.log
	@grep -q "NXENGINE_D1_SUBSYSTEMS_READY trig" $(BUILD)/nxengine-serial.log
	@grep -q "NXENGINE_D2_NO_DATA self-test-mode" $(BUILD)/nxengine-serial.log
	@grep -q "process exited with status 0" $(BUILD)/nxengine-serial.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/nxengine-serial.log
	@echo "NXEngine D1/D2 core smoke test passed"

nxengine-play-smoke: $(NXENGINE_PLAY_ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/nxengine-play.log $(BUILD)/nxengine-play-monitor.sock
	@timeout 150s qemu-system-x86_64 -cdrom $(NXENGINE_PLAY_ISO) -m 256M \
		$(QEMU_AUDIO_TEST) \
		-serial file:$(BUILD)/nxengine-play.log -display none \
		-monitor unix:$(BUILD)/nxengine-play-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/nxengine-play-monitor.sock && grep -q "mako#" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey n\nsendkey x\nsendkey e\nsendkey n\nsendkey g\nsendkey i\nsendkey n\nsendkey e\nsendkey minus\nsendkey c\nsendkey o\nsendkey r\nsendkey e\n'; \
		  printf 'sendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D4_INTERACTIVE_READY" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for i in $$(seq 1 20); do \
			printf 'sendkey right\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
			sleep 0.05; \
		done; \
		printf 'sendkey esc\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D8_INTERACTIVE_READY" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for i in $$(seq 1 20); do \
			printf 'sendkey right\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
			sleep 0.05; \
		done; \
		printf 'sendkey esc\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D10_INTERACTIVE_READY\|NXENGINE_D10_NO_DATA" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for i in $$(seq 1 20); do \
			printf 'sendkey right\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
			sleep 0.05; \
		done; \
		printf 'sendkey esc\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D21_INTERACTIVE_READY\|NXENGINE_D21_NO_DATA" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D28_INTERACTIVE_READY\|NXENGINE_D27_NO_DATA" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		printf 'sendkey right 700\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D30_INTERACTIVE_READY\|NXENGINE_D30_NO_DATA" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		printf 'sendkey down 900\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		sleep 1.5; \
		printf 'sendkey z 900\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		sleep 1.5; \
		for attempt in $$(seq 1 120); do \
			grep -q "NXENGINE_D35_INTERACTIVE_READY" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		printf 'sendkey right 900\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-play-monitor.sock >/dev/null; \
		sleep 1.5; \
		for attempt in $$(seq 1 400); do \
			grep -q "process exited with status" $(BUILD)/nxengine-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "NXENGINE_D2_BMP_OK w=320 h=176 bpp=8" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D2_SUBSYSTEMS_READY bmp" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D3_RENDER_OK w=320 h=240" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D3_SUBSYSTEMS_READY render" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D4_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D4_MOVE_OK" $(BUILD)/nxengine-play.log
	@grep -q "load_map: level size 21x20" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D5_MAP_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D5_SUBSYSTEMS_READY map" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D6_TILESET_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D6_SUBSYSTEMS_READY tileset" $(BUILD)/nxengine-play.log
	@grep -q "load_map: level size 21x16" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D7_TILEATTR_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D7_RENDER_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D7_SUBSYSTEMS_READY level" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D8_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D8_MOVE_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D9_SPRITES_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D9_SUBSYSTEMS_READY sprites" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D10_NPCTBL_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D10_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D10_MOVE_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D11_FLOATTEXT_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D11_SUBSYSTEMS_READY floattext" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D12_CARET_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D12_SUBSYSTEMS_READY caret" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D13_FADE_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D13_STARFLASH_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D13_SUBSYSTEMS_READY screeneffect" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D14_FONT_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D14_SUBSYSTEMS_READY font" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D15_PROFILE_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D15_SUBSYSTEMS_READY profile" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D16_SETTINGS_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D16_SUBSYSTEMS_READY input_settings" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D17_ITEMIMAGE_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D17_SUBSYSTEMS_READY itemimage" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D18_YNPROMPT_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D18_SUBSYSTEMS_READY ynprompt" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D19_CREDITS_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D19_SUBSYSTEMS_READY credreader" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D20_DIALOG_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D20_SUBSYSTEMS_READY dialog" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D21_PLAYER_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D21_SUBSYSTEMS_READY player" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D22_SHOT_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D22_SUBSYSTEMS_READY shot_ai" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D23_NPC_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D23_SUBSYSTEMS_READY npc_ai" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D24_ENTITIES_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D24_SUBSYSTEMS_READY map_entities" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D25_TICK_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D25_SUBSYSTEMS_READY stage_sim" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D26_SCRIPT_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D26_SUBSYSTEMS_READY tsc" $(BUILD)/nxengine-play.log
	@grep -q "load_map: level size 21x16" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D27_TRANSITION_OK" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D27_SUBSYSTEMS_READY stage_transition" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D28_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D28_LIVEINPUT_OK keydown_seen=1 moved=1 keyup_seen=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D28_SUBSYSTEMS_READY live_input" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D29_HUD_OK hud_pixels_changed=1 hp_pixels_changed=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D29_SUBSYSTEMS_READY hud" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D30_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D30_TITLEFLOW_OK slot_selected=0 game_started=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D30_SUBSYSTEMS_READY titleflow" $(BUILD)/nxengine-play.log
	@grep -q "AC97_AUDIO_READY rate=44100 channels=2 bits=16" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D31_AUDIO_OK audio_open=1 total_sound_calls=[1-9][0-9]* total_submit_ok=[1-9][0-9]* total_samples_queued=[1-9][0-9]* trigger_snd=[0-9]+ trigger_calls=1 trigger_submits=1 trigger_samples_queued=[1-9][0-9]*" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D31_SUBSYSTEMS_READY audio" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D32_BOSS_OK boss=heavy_press state=102 hp=600 shields=2 butes=1 lightning_charge=1 lightning_strike=1 sounds_fired=[1-9][0-9]* map_tiles_changed=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D32_SUBSYSTEMS_READY boss" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D33_BALFROG_OK jump_sprite=1 shot=1 minifrog=1 landing_smoke=1 mouth_target=1 quake=30 fight_sounds=[1-9][0-9]* death_frames=[1-9][0-9]*" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D33_SUBSYSTEMS_READY balfrog" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D34_MIDGAME_SAVE_OK slot=2 stage=[0-9]+ hp=[0-9]+ weapon=[0-9]+ niku=0x1234" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D34_INVENTORY_OK items_shown=[1-9][0-9]* curwpn_slot=[0-9]+" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D34_MODE_OK transitions=4" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D34_SUBSYSTEMS_READY inventory_pause_save_modes" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D35_INTERACTIVE_READY" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D35_REPLAY_OK recorded_frames=[0-9]+ live_final_x=-?[0-9]+ live_final_y=-?[0-9]+ replay_final_x=-?[0-9]+ replay_final_y=-?[0-9]+ match=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D35_SUBSYSTEMS_READY replay" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=ironhead .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=omega .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=sisters .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=core .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=undead_core .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=monster_x .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_BOSS_OK boss=ballos .* hp_changed=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D36_ALLBOSSES_OK bosses_verified=7" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D36_SUBSYSTEMS_READY allbosses" $(BUILD)/nxengine-play.log
	@grep -Eq "NXENGINE_D36_TITLE_OK title_pixels_changed=1 .* pixel_forever_omitted=1" $(BUILD)/nxengine-play.log
	@grep -q "NXENGINE_D36_TITLE_SUBSYSTEMS_READY title" $(BUILD)/nxengine-play.log
	@grep -q "process exited with status 0" $(BUILD)/nxengine-play.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/nxengine-play.log
	@echo "NXEngine D2-D35 real-asset interactive play smoke test passed"

# D37: freeplay ISO -- reuses the exact same asset set as
# $(NXENGINE_PLAY_ISO) above (Pens1/Start/Frog + tileset/sprite/font/tsc
# assets, all already fetched by nxengine-data), just booting the
# freeplay ELF instead of the D1-D36 test-harness ELF.
NXENGINE_FREEPLAY_ISO := $(BUILD)/kernel-nxengine-freeplay.iso

nxengine-play-freeplay-iso: $(NXENGINE_FREEPLAY_ISO)

$(NXENGINE_FREEPLAY_ISO): $(ISO) nxengine-data nxengine-source grub/grub-nxengine-freeplay.cfg $(NXENGINE_FREEPLAY_ELF)
	rm -rf $(BUILD)/iso-nxengine-freeplay
	cp -r $(ISO_ROOT) $(BUILD)/iso-nxengine-freeplay
	mkdir -p $(BUILD)/iso-nxengine-freeplay/games/nxengine
	cp $(NXENGINE_DATA)/CaveStory/data/Bullet.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Bullet.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/casts.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/casts.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/0.pxm $(BUILD)/iso-nxengine-freeplay/games/nxengine/0.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Prt0.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Prt0.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Pens1.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtPens.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/PrtPens.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/MyChar.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/MyChar.pbm
	cp $(NXENGINE_SOURCE)/tilekey.dat $(BUILD)/iso-nxengine-freeplay/games/nxengine/tilekey.dat
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens.pxa $(BUILD)/iso-nxengine-freeplay/games/nxengine/Pens.pxa
	cp $(NXENGINE_SOURCE)/sprites.sif $(BUILD)/iso-nxengine-freeplay/games/nxengine/sprites.sif
	cp $(NXENGINE_DATA)/CaveStory/data/npc.tbl $(BUILD)/iso-nxengine-freeplay/games/nxengine/npc.tbl
	cp $(NXENGINE_DATA)/CaveStory/data/TextBox.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/TextBox.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Caret.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Caret.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Fade.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Fade.pbm
	cp $(NXENGINE_SOURCE)/smalfont.bmp $(BUILD)/iso-nxengine-freeplay/games/nxengine/smalfont.bmp
	cp $(NXENGINE_DATA)/CaveStory/data/Credit.tsc $(BUILD)/iso-nxengine-freeplay/games/nxengine/Credit.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxe $(BUILD)/iso-nxengine-freeplay/games/nxengine/Pens1.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Head.tsc $(BUILD)/iso-nxengine-freeplay/games/nxengine/Head.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsItem.tsc $(BUILD)/iso-nxengine-freeplay/games/nxengine/ArmsItem.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/StageSelect.tsc $(BUILD)/iso-nxengine-freeplay/games/nxengine/StageSelect.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Start.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxe $(BUILD)/iso-nxengine-freeplay/games/nxengine/Start.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Frog.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxe $(BUILD)/iso-nxengine-freeplay/games/nxengine/Frog.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.tsc $(BUILD)/iso-nxengine-freeplay/games/nxengine/Frog.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtWeed.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/PrtWeed.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Weed.pxa $(BUILD)/iso-nxengine-freeplay/games/nxengine/Weed.pxa
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsImage.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/ArmsImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Arms.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Arms.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Npc/NpcSym.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/NpcSym.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/ItemImage.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/ItemImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Title.pbm $(BUILD)/iso-nxengine-freeplay/games/nxengine/Title.pbm
	cp $(NXENGINE_FREEPLAY_ELF) $(BUILD)/iso-nxengine-freeplay/games/nxengine/nxengine-play-freeplay.elf
	cp grub/grub-nxengine-freeplay.cfg $(BUILD)/iso-nxengine-freeplay/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-nxengine-freeplay >/dev/null 2>&1

# D37 scripted verification: proves the freeplay loop genuinely has no
# frame cap by letting it run for well over 500 real frames (paced at the
# real ~60fps demon_port_sleep_ms(16) rate, so this alone takes >8s of
# real wall-clock time inside the guest) driven entirely by real, timed
# QEMU monitor sendkeys -- through the real title screen, the real
# TB_SaveSelect widget, into real gameplay -- then confirms the player is
# still live and responds to one final real keypress before a real ESCKEY
# quits cleanly. NXENGINE_D37_FREEPLAY_OK's own frames_run counter (not a
# fixed test-side counter) is checked against >=500 -- if the engine ever
# regains a hidden frame cap below that, this fails loudly.
nxengine-play-freeplay-smoke: $(NXENGINE_FREEPLAY_ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/nxengine-freeplay.log $(BUILD)/nxengine-freeplay-monitor.sock
	@timeout 180s qemu-system-x86_64 -cdrom $(NXENGINE_FREEPLAY_ISO) -m 256M \
		-serial file:$(BUILD)/nxengine-freeplay.log -display none \
		-monitor unix:$(BUILD)/nxengine-freeplay-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/nxengine-freeplay-monitor.sock && grep -q "mako#" $(BUILD)/nxengine-freeplay.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey n\nsendkey x\nsendkey e\nsendkey n\nsendkey g\nsendkey i\nsendkey n\nsendkey e\nsendkey minus\nsendkey p\nsendkey l\nsendkey a\nsendkey y\nsendkey minus\nsendkey f\nsendkey r\nsendkey e\nsendkey e\nsendkey p\nsendkey l\nsendkey a\nsendkey y\n'; \
		  printf 'sendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 150); do \
			grep -q "NXENGINE_D37_TITLE_READY" $(BUILD)/nxengine-freeplay.log 2>/dev/null && break; sleep 0.1; \
		done; \
		printf 'sendkey z 500\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
		sleep 0.7; \
		for attempt in $$(seq 1 150); do \
			grep -q "NXENGINE_D37_SAVESELECT_READY" $(BUILD)/nxengine-freeplay.log 2>/dev/null && break; sleep 0.1; \
		done; \
		sleep 0.3; \
		printf 'sendkey z 500\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
		sleep 0.7; \
		for attempt in $$(seq 1 150); do \
			grep -q "NXENGINE_D37_GAMEPLAY_START" $(BUILD)/nxengine-freeplay.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for burst in $$(seq 1 20); do \
			printf 'sendkey right 300\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
			sleep 0.5; \
			printf 'sendkey left 300\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
			sleep 0.5; \
		done; \
		printf 'sendkey right 900\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
		sleep 1.2; \
		printf 'sendkey esc\n' | socat - UNIX-CONNECT:$(BUILD)/nxengine-freeplay-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 150); do \
			grep -q "process exited with status" $(BUILD)/nxengine-freeplay.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "NXENGINE_D37_TITLE_READY" $(BUILD)/nxengine-freeplay.log
	@grep -q "NXENGINE_D37_SAVESELECT_READY" $(BUILD)/nxengine-freeplay.log
	@grep -q "NXENGINE_D37_GAMEPLAY_START" $(BUILD)/nxengine-freeplay.log
	@grep -Eoq "NXENGINE_D37_FREEPLAY_OK frames_run=[5-9][0-9][0-9] still_responsive=1|NXENGINE_D37_FREEPLAY_OK frames_run=[0-9]{4,} still_responsive=1" $(BUILD)/nxengine-freeplay.log
	@grep -q "process exited with status 0" $(BUILD)/nxengine-freeplay.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/nxengine-freeplay.log
	@echo "NXEngine D37 freeplay smoke test passed"

QUAKE_BUILD := $(BUILD)/quake
QUAKE_CORE_ELF := $(BUILD)/quake-core.elf
QUAKE_CFLAGS := -std=gnu89 -Os -g -Wall -Wno-format -Wno-unused-variable \
	-Wno-unused-function -Wno-unused-but-set-variable -ffreestanding \
	-fno-builtin -fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections -m64 -mno-red-zone -msse2 \
	-fcommon -Wno-pointer-to-int-cast -Wno-unused-parameter \
	-Iinclude -Iapps/doom -I$(QUAKE_SOURCE)/WinQuake

# The 58 genuine upstream WinQuake units the D4 engine boot links against.
QUAKE_ENGINE_UNITS := cl_demo cl_input cl_main cl_parse cl_tent chase \
	console keys menu screen sbar view host_cmd draw wad world model \
	nonintel pr_cmds pr_edict pr_exec r_main r_bsp r_surf r_draw r_edge \
	r_misc r_part r_sky r_sprite r_light r_efrag r_aclip r_alias r_vars \
	d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite \
	d_surf d_vars d_zpoint sv_main sv_phys sv_move sv_user host net_main \
	net_loop net_dgrm net_vcr snd_null cd_null
QUAKE_ENGINE_OBJS := $(addprefix $(QUAKE_BUILD)/, $(addsuffix .o, $(QUAKE_ENGINE_UNITS)))

# Platform units: the vanilla Sys/VID/IN/NET shims plus freestanding setjmp
# and the stdio FILE shim.
QUAKE_PLATFORM_OBJS := $(QUAKE_BUILD)/sys_demonos.o $(QUAKE_BUILD)/com_demonos.o \
	$(QUAKE_BUILD)/vid_demonos.o $(QUAKE_BUILD)/in_demonos.o \
	$(QUAKE_BUILD)/net_demonos.o $(QUAKE_BUILD)/stdio_demonos.o \
	$(QUAKE_BUILD)/setjmp_demonos.o $(QUAKE_BUILD)/libc_demonos.o \
	$(QUAKE_BUILD)/libm_demonos.o

$(QUAKE_BUILD):
	mkdir -p $@

$(QUAKE_BUILD)/entry.o: ports/quake/platform/entry.S | $(QUAKE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(QUAKE_BUILD)/setjmp_demonos.o: ports/quake/platform/setjmp_demonos.S | $(QUAKE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(QUAKE_BUILD)/core_main.o: ports/quake/platform/core_main.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/sys_demonos.o: ports/quake/platform/sys_demonos.c | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/com_demonos.o: ports/quake/platform/com_demonos.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/vid_demonos.o: ports/quake/platform/vid_demonos.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/in_demonos.o: ports/quake/platform/in_demonos.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/net_demonos.o: ports/quake/platform/net_demonos.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/stdio_demonos.o: ports/quake/platform/stdio_demonos.c | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/libc_demonos.o: ports/quake/platform/libc_demonos.c | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/libm_demonos.o: ports/quake/platform/libm_demonos.c | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

# Generic rule for the 58 upstream engine units.
$(QUAKE_BUILD)/%.o: $(QUAKE_SOURCE)/WinQuake/%.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

# The shared apps/doom/libc.c is used both here and by Doom.  Doom links a
# soft-float build; Quake needs its %f formatter with real SSE doubles.
$(QUAKE_BUILD)/quake_libc.o: apps/doom/libc.c apps/doom/libc.h include/demon/portkit.h | $(QUAKE_BUILD)
	$(CC) -std=c11 -Os -g -Wall -Wextra -ffreestanding -fno-builtin \
		-fno-stack-protector -fno-pie -fno-pic -ffunction-sections \
		-fdata-sections -m64 -mno-red-zone -msse2 -Iinclude -Iapps/doom \
		-DQUAKE_LIBC_FLOAT -c $< -o $@

$(QUAKE_BUILD)/zone.o: $(QUAKE_SOURCE)/WinQuake/zone.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/mathlib.o: $(QUAKE_SOURCE)/WinQuake/mathlib.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/crc.o: $(QUAKE_SOURCE)/WinQuake/crc.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/cmd.o: $(QUAKE_SOURCE)/WinQuake/cmd.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_BUILD)/cvar.o: $(QUAKE_SOURCE)/WinQuake/cvar.c $(QUAKE_SOURCE)/.demonos-pinned | $(QUAKE_BUILD)
	$(CC) $(QUAKE_CFLAGS) -c $< -o $@

$(QUAKE_CORE_ELF): $(QUAKE_BUILD)/entry.o $(QUAKE_BUILD)/core_main.o \
		$(QUAKE_PLATFORM_OBJS) $(QUAKE_ENGINE_OBJS) \
		$(QUAKE_BUILD)/zone.o $(QUAKE_BUILD)/mathlib.o \
		$(QUAKE_BUILD)/crc.o $(QUAKE_BUILD)/cmd.o $(QUAKE_BUILD)/cvar.o \
		$(QUAKE_BUILD)/quake_libc.o \
		$(BUILD)/portkit.o ports/quake/linker.ld
	$(LD) -nostdlib --gc-sections -z max-page-size=0x1000 -T ports/quake/linker.ld \
		$(QUAKE_BUILD)/entry.o $(BUILD)/portkit.o \
		$(QUAKE_BUILD)/quake_libc.o $(QUAKE_PLATFORM_OBJS) \
		$(QUAKE_ENGINE_OBJS) $(QUAKE_BUILD)/zone.o $(QUAKE_BUILD)/mathlib.o \
		$(QUAKE_BUILD)/crc.o $(QUAKE_BUILD)/cmd.o $(QUAKE_BUILD)/cvar.o \
		$(QUAKE_BUILD)/core_main.o -o $@
	$(STRIP) -s $@

quake-core: $(QUAKE_CORE_ELF)
	@readelf -h $< | grep -q 'Class:.*ELF64'
	@readelf -h $< | grep -q 'Entry point address:.*0x20000000'
	@readelf -lW $< | grep -q 'LOAD.*0x0000000020000000'
	@echo "Quake D4 upstream engine ELF linked at isolated large-app base"

CLASSICUBE_SOURCE := $(BUILD)/classicube-upstream
CLASSICUBE_COMMIT := 9a3101c00330aa6ca0e091bcd5c76d019ee85b7e
CLASSICUBE_BUILD := $(BUILD)/classicube
CLASSICUBE_CORE_ELF := $(BUILD)/classicube-core.elf
CLASSICUBE_CFLAGS := -std=c11 -Os -g -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections -m64 -mno-red-zone -msse2 \
	-Iinclude -D__demonos__ -I$(CLASSICUBE_SOURCE)/src \
	-Wno-unused-parameter -Wno-sign-compare -Wno-cast-function-type

classicube-source: $(CLASSICUBE_SOURCE)/.demonos-pinned

$(CLASSICUBE_SOURCE)/.demonos-pinned: tools/fetch-classicube.sh ports/classicube/classicube-demonos-core.patch
	sh tools/fetch-classicube.sh $(CLASSICUBE_SOURCE)

classicube-port-audit: classicube-source
	@test "$$(git -C $(CLASSICUBE_SOURCE) rev-parse HEAD)" = "$(CLASSICUBE_COMMIT)"
	@grep -q 'void Platform_Init(void)' $(CLASSICUBE_SOURCE)/src/Platform.h
	@grep -q 'void Window_ProcessEvents(float delta)' $(CLASSICUBE_SOURCE)/src/Window.h
	@grep -q 'void Window_DrawFramebuffer' $(CLASSICUBE_SOURCE)/src/Window.h
	@grep -q 'Window_DrawFramebuffer' $(CLASSICUBE_SOURCE)/src/Graphics_SoftGPU.c
	@grep -q 'Socket_Connect' $(CLASSICUBE_SOURCE)/src/Platform.h
	@grep -q 'struct AudioChunk' $(CLASSICUBE_SOURCE)/src/Audio.h
	@test -f ports/classicube/platform/Platform_DemonOS.c
	@test -f ports/classicube/platform/Window_DemonOS.c
	@echo "ClassiCube D0 source/platform contract audit passed"

$(CLASSICUBE_BUILD):
	mkdir -p $@

$(CLASSICUBE_BUILD)/entry.o: ports/classicube/platform/entry.S | $(CLASSICUBE_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/core_main.o: ports/classicube/platform/core_main.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/String.o: $(CLASSICUBE_SOURCE)/src/String.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Stream.o: $(CLASSICUBE_SOURCE)/src/Stream.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Bitmap.o: $(CLASSICUBE_SOURCE)/src/Bitmap.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Utils.o: $(CLASSICUBE_SOURCE)/src/Utils.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Generator.o: $(CLASSICUBE_SOURCE)/src/Generator.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -Wno-error=old-style-declaration \
		-Wno-error=implicit-fallthrough -c $< -o $@

$(CLASSICUBE_BUILD)/ExtMath.o: $(CLASSICUBE_SOURCE)/src/ExtMath.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -Wno-error=old-style-declaration \
		-Wno-error=strict-aliasing -c $< -o $@

$(CLASSICUBE_BUILD)/Vectors.o: $(CLASSICUBE_SOURCE)/src/Vectors.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Event.o: $(CLASSICUBE_SOURCE)/src/Event.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Input.o: $(CLASSICUBE_SOURCE)/src/Input.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -Wno-missing-field-initializers -c $< -o $@

$(CLASSICUBE_BUILD)/Graphics_SoftGPU.o: $(CLASSICUBE_SOURCE)/src/Graphics_SoftGPU.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -Wno-error=missing-field-initializers \
		-Wno-error=unused-function -Wno-error=maybe-uninitialized -c $< -o $@

$(CLASSICUBE_BUILD)/Platform_DemonOS.o: ports/classicube/platform/Platform_DemonOS.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_BUILD)/Window_DemonOS.o: ports/classicube/platform/Window_DemonOS.c $(CLASSICUBE_SOURCE)/.demonos-pinned | $(CLASSICUBE_BUILD)
	$(CC) $(CLASSICUBE_CFLAGS) -c $< -o $@

$(CLASSICUBE_CORE_ELF): $(CLASSICUBE_BUILD)/entry.o $(CLASSICUBE_BUILD)/core_main.o \
		$(CLASSICUBE_BUILD)/String.o $(CLASSICUBE_BUILD)/Stream.o \
		$(CLASSICUBE_BUILD)/Bitmap.o $(CLASSICUBE_BUILD)/Utils.o \
		$(CLASSICUBE_BUILD)/ExtMath.o $(CLASSICUBE_BUILD)/Vectors.o $(CLASSICUBE_BUILD)/Event.o \
		$(CLASSICUBE_BUILD)/Input.o $(CLASSICUBE_BUILD)/Generator.o \
		$(CLASSICUBE_BUILD)/Graphics_SoftGPU.o \
		$(CLASSICUBE_BUILD)/Platform_DemonOS.o $(CLASSICUBE_BUILD)/Window_DemonOS.o \
		$(BUILD)/portkit.o $(BUILD)/doom_libc.o user/linker.ld
	$(LD) $(USER_LDFLAGS) --gc-sections $(CLASSICUBE_BUILD)/entry.o $(BUILD)/portkit.o \
		$(BUILD)/doom_libc.o $(CLASSICUBE_BUILD)/String.o \
		$(CLASSICUBE_BUILD)/Stream.o $(CLASSICUBE_BUILD)/Bitmap.o \
		$(CLASSICUBE_BUILD)/Utils.o $(CLASSICUBE_BUILD)/ExtMath.o $(CLASSICUBE_BUILD)/Vectors.o \
		$(CLASSICUBE_BUILD)/Event.o $(CLASSICUBE_BUILD)/Input.o \
		$(CLASSICUBE_BUILD)/Generator.o \
		$(CLASSICUBE_BUILD)/Graphics_SoftGPU.o \
		$(CLASSICUBE_BUILD)/Platform_DemonOS.o $(CLASSICUBE_BUILD)/Window_DemonOS.o \
		$(CLASSICUBE_BUILD)/core_main.o -o $@
	$(STRIP) -s $@

classicube-core: $(CLASSICUBE_CORE_ELF)
	@readelf -h $< | grep -q 'Class:.*ELF64'
	@readelf -lW $< | grep -q 'LOAD'
	@echo "ClassiCube D1 upstream core ELF built"

all: $(KERNEL)

project: $(PORTABLE_ELF)

portkit-check: $(PORTCHECK_ELF)

wine-pe-check: $(WINE_PE_PROBE_ELF)

mem-reserve-check: $(MEM_RESERVE_CHECK_ELF)

wine-pe-load-check: $(WINE_PE_LOAD_CHECK_ELF)

wine-pe-import-check: $(WINE_PE_IMPORT_CHECK_ELF)

wine-pe-reloc-check: $(WINE_PE_RELOC_CHECK_ELF)

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

$(DOOM_ENGINE_BUILD)/demonos_sound.o: ports/doom/platform/demonos_sound.c include/demon/c_app.h | $(DOOM_SOURCE_STAMP) $(DOOM_ENGINE_BUILD)
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

$(DOOM_ENGINE_BUILD)/doomgeneric-with-platform.o: $(DOOM_CORE_OBJECTS) $(DOOM_ENGINE_BUILD)/doomgeneric_demonos.o $(DOOM_ENGINE_BUILD)/demonos_stdio.o $(DOOM_ENGINE_BUILD)/demonos_sound.o $(DOOM_ENGINE_BUILD)/doom_libc.o $(DOOM_ENGINE_BUILD)/portkit.o
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
		$(QEMU_AUDIO_TEST) \
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
	@grep -q "AC97_AUDIO_READY rate=44100 channels=2 bits=16" $(BUILD)/freedoom-serial.log
	@grep -q "DOOM_AUDIO_MIXER_READY rate=44100 channels=2 voices=8" $(BUILD)/freedoom-serial.log
	@grep -q "DOOM_AUDIO_FIRST_BUFFER frames=1260" $(BUILD)/freedoom-serial.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/freedoom-serial.log
	@echo "Official Freedoom engine frame smoke test passed"

freedoom-command-smoke: $(FREEDOOM_PLAY_ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/freedoom-command.log $(BUILD)/freedoom-monitor.sock
	@timeout 35s qemu-system-x86_64 -cdrom $(FREEDOOM_PLAY_ISO) -m 256M \
		$(QEMU_AUDIO_TEST) \
		-serial file:$(BUILD)/freedoom-command.log -display none \
		-monitor unix:$(BUILD)/freedoom-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/freedoom-monitor.sock && grep -q "mako#" $(BUILD)/freedoom-command.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey b\nsendkey e\nsendkey e\nsendkey p\nsendkey ret\n'; \
		  printf 'sendkey d\nsendkey o\nsendkey o\nsendkey m\nsendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/freedoom-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "FREEDOOM_FIRST_FRAME_READY" $(BUILD)/freedoom-command.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "mako# doom" $(BUILD)/freedoom-command.log
	@grep -q "BLEEP_PLAYED frequency=880 duration_ms=80" $(BUILD)/freedoom-command.log
	@grep -q "FREEDOOM_FIRST_FRAME_READY 320x200 ARGB" $(BUILD)/freedoom-command.log
	@! grep -Eq "spawn failed|wait failed|PAGE FAULT|\[FAILED\]" $(BUILD)/freedoom-command.log
	@echo "MakoBox Doom command launch smoke test passed"

quake-data:
	sh tools/fetch-quake.sh $(QUAKE_DATA_DIR)

quake-play-iso: $(QUAKE_PLAY_ISO)

$(QUAKE_PLAY_ISO): $(ISO) quake-data grub/grub-quake-play.cfg
	rm -rf $(BUILD)/iso-quake-play
	cp -r $(ISO_ROOT) $(BUILD)/iso-quake-play
	mkdir -p $(BUILD)/iso-quake-play/games/quake $(BUILD)/iso-quake-play/licenses/quake
	cp $(QUAKE_PAK) $(BUILD)/iso-quake-play/games/quake/pak0.pak
	cp $(QUAKE_DATA_DIR)/SLICNSE.TXT $(BUILD)/iso-quake-play/licenses/quake/SLICNSE.TXT
	cp $(QUAKE_DATA_DIR)/README.TXT $(BUILD)/iso-quake-play/licenses/quake/README.TXT
	cp grub/grub-quake-play.cfg $(BUILD)/iso-quake-play/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso-quake-play >/dev/null 2>&1

quake-play-smoke: $(QUAKE_PLAY_ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/quake-play.log $(BUILD)/quake-play-monitor.sock
	@timeout 40s qemu-system-x86_64 -cdrom $(QUAKE_PLAY_ISO) -m 256M \
		-serial file:$(BUILD)/quake-play.log -display none \
		-monitor unix:$(BUILD)/quake-play-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/quake-play-monitor.sock && grep -q "mako#" $(BUILD)/quake-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey q\nsendkey u\nsendkey a\nsendkey k\nsendkey e\n'; \
		  printf 'sendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/quake-play-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 240); do \
			grep -q "QUAKE_D5_PLAY_READY" $(BUILD)/quake-play.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "mako# quake" $(BUILD)/quake-play.log
	@grep -q "QUAKE_D5_PAK_REAL" $(BUILD)/quake-play.log
	@grep -q "QUAKE_D5_MAP_READY" $(BUILD)/quake-play.log
	@grep -q "QUAKE_D5_PLAY_READY" $(BUILD)/quake-play.log
	@! grep -q "PAGE FAULT\|\[FAILED\]" $(BUILD)/quake-play.log
	@echo "Quake shareware play smoke test passed"

quake-smoke: $(ISO)
	@command -v socat >/dev/null
	@rm -f $(BUILD)/quake-serial.log $(BUILD)/quake-monitor.sock
	@timeout 40s qemu-system-x86_64 -cdrom $(ISO) -m 256M \
		-serial file:$(BUILD)/quake-serial.log -display none \
		-monitor unix:$(BUILD)/quake-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
			test -S $(BUILD)/quake-monitor.sock && grep -q "mako#" $(BUILD)/quake-serial.log 2>/dev/null && break; sleep 0.1; \
		done; \
		{ printf 'sendkey q\nsendkey u\nsendkey a\nsendkey k\nsendkey e\n'; \
		  printf 'sendkey ret\n'; } | \
			socat - UNIX-CONNECT:$(BUILD)/quake-monitor.sock >/dev/null; \
		for attempt in $$(seq 1 120); do \
			grep -q "QUAKE_D1_SUBSYSTEMS_READY" $(BUILD)/quake-serial.log 2>/dev/null && break; sleep 0.1; \
		done; \
		for attempt in $$(seq 1 120); do \
			grep -q "process exited with status 0" $(BUILD)/quake-serial.log 2>/dev/null && break; sleep 0.1; \
		done; \
		kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true
	@grep -q "QUAKE_D1_START" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK zone zero-fill" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK hunk alignment" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK crc bytes" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK vector normalize" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK cvar register" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_OK cvar setvalue" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_CMD_RAN argc=1 argv0=quake_selftest" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D1_SUBSYSTEMS_READY zone crc mathlib cmd cvar" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_OK cmd init" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_FILE_ROUNDTRIP_OK bytes=" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_OK com loadhunkfile" $(BUILD)/quake-serial.log
	@grep -q "config_loaded" $(BUILD)/quake-serial.log
	@grep -q "inline_echo" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_OK console exec+set" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_OK console alias" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_CONSOLE_OK" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D2_SUBSYSTEMS_READY console cmd files" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_PAK_OK pak=d3test numfiles=2" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_LOAD_OK lump=palette.lmp bytes=768" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_LOAD_OK lump=quake_d3.txt" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_LOAD_OK lump=missing miss=1" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_ASSET_SUBSYSTEMS_READY" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D3_SUBSYSTEMS_READY assets pak crc" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D4_WAD_OK lumps=" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D4_FRAMES .*video=" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D4_BOOT_OK" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D4_SUBSYSTEMS_READY host vid draw sbar console" $(BUILD)/quake-serial.log
	@grep -q "QUAKE_D4_VIDEO_READY" $(BUILD)/quake-serial.log
	@grep -q "process exited with status 0" $(BUILD)/quake-serial.log
	@! grep -q "QUAKE_D1_FAIL\|QUAKE_D2_FAIL\|QUAKE_D3_FAIL\|QUAKE_D4_FAIL\|PAGE FAULT" $(BUILD)/quake-serial.log
	@echo "Quake D1/D2/D3/D4 core smoke test passed"

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

$(BUILD)/ac97.o: src/ac97.c include/kernel/ac97.h include/kernel/pci.h | $(BUILD)
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

# K3 of docs/kernel-cxx-port.md: src/capability.c's per-process table
# migrated onto kernel::slot_table<T,N> (src/capability.cpp). Same
# extern-"C" ABI, built with KERNEL_CXXFLAGS since the source is now .cpp.
$(BUILD)/capability.o: src/capability.cpp include/kernel/capability.h include/kernel/surface.h include/kernel/slot_table.h | $(BUILD)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

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

# K2 of docs/kernel-cxx-port.md: src/ramfs.c's file table migrated onto
# kernel::bounded_table<T,N> (src/ramfs.cpp). Same extern-"C" ABI, built
# with KERNEL_CXXFLAGS instead of CFLAGS since the source is now .cpp.
$(BUILD)/ramfs.o: src/ramfs.cpp include/kernel/ramfs.h include/kernel/bounded_table.h | $(BUILD)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

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

# Desktop stack (un-sidelined, see sidelined/README.md). The compositor and
# DemonX server run as ordinary ring-3 processes linked against user/linker.ld
# exactly like tetris/portcheck; the xlib shim (lib/demonx/xlib.c) is the only
# extra dependency the X11 clients link.

$(BUILD)/compositor_entry.o: user/compositor.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/compositor_mko.S: user/compositor.mko user/sdk.mko Desktop/desktop.mko | $(BUILD)
	dotnet run --project $(MAKO_REPO)/src/Mako -- native $< --kernel -o $@

$(BUILD)/compositor_mko.o: $(BUILD)/compositor_mko.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(COMPOSITOR_ELF): $(BUILD)/compositor_entry.o $(BUILD)/compositor_mko.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/compositor_entry.o $(BUILD)/compositor_mko.o -o $@
	$(STRIP) -s $@

$(BUILD)/demonx_server_entry.o: user/demonx_server.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/demonx_server.o: user/demonx_server.c include/demon/demonx.h include/demon/window.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DEMONX_ELF): $(BUILD)/demonx_server_entry.o $(BUILD)/demonx_server.o user/linker.ld
	$(LD) $(USER_LDFLAGS) \
		$(BUILD)/demonx_server_entry.o $(BUILD)/demonx_server.o -o $@
	$(STRIP) -s $@

$(BUILD)/demonx_xlib.o: lib/demonx/xlib.c include/X11/Xlib.h include/demon/demonx.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/demonwm_entry.o: Desktop/demonwm/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/demonwm.o: Desktop/demonwm/demonwm.cc include/X11/Xlib.h include/demon/c_app.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEMONWM_ELF): $(BUILD)/demonwm_entry.o $(BUILD)/demonwm.o $(BUILD)/demonx_xlib.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/demonwm_entry.o \
		$(BUILD)/demonwm.o $(BUILD)/demonx_xlib.o -o $@
	$(STRIP) -s $@

$(BUILD)/xterm_entry.o: apps/xterm/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/xterm.o: apps/xterm/xterm.c include/X11/Xlib.h include/demon/c_app.h include/demon/demonx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(XTERM_ELF): $(BUILD)/xterm_entry.o $(BUILD)/xterm.o $(BUILD)/demonx_xlib.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/xterm_entry.o \
		$(BUILD)/xterm.o $(BUILD)/demonx_xlib.o -o $@
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

$(BUILD)/wine_pe_probe_entry.o: apps/wine_pe_probe/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/wine_pe.o: ports/wine/platform/pe.c ports/wine/platform/pe.h include/demon/portkit.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wine_pe_probe.o: apps/wine_pe_probe/main.c ports/wine/platform/pe.h include/demon/portkit.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(WINE_PE_PROBE_ELF): $(BUILD)/wine_pe_probe_entry.o $(BUILD)/portkit.o $(BUILD)/wine_pe.o $(BUILD)/wine_pe_probe.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/wine_pe_probe_entry.o $(BUILD)/portkit.o \
		$(BUILD)/wine_pe.o $(BUILD)/wine_pe_probe.o -o $@

$(BUILD)/mem_reserve_check_entry.o: apps/mem_reserve_check/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/mem_reserve_check.o: apps/mem_reserve_check/main.c include/demon/portkit.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(MEM_RESERVE_CHECK_ELF): $(BUILD)/mem_reserve_check_entry.o $(BUILD)/portkit.o $(BUILD)/mem_reserve_check.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/mem_reserve_check_entry.o $(BUILD)/portkit.o \
		$(BUILD)/mem_reserve_check.o -o $@

$(BUILD)/wine_pe_load_check_entry.o: apps/wine_pe_load_check/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/wine_pe_load.o: ports/wine/platform/pe_load.c ports/wine/platform/pe_load.h ports/wine/platform/pe.h include/demon/portkit.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wine_pe_load_check.o: apps/wine_pe_load_check/main.c ports/wine/platform/pe_load.h include/demon/portkit.h include/demon/c_app.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(WINE_PE_LOAD_CHECK_ELF): $(BUILD)/wine_pe_load_check_entry.o $(BUILD)/portkit.o $(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_load_check.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/wine_pe_load_check_entry.o $(BUILD)/portkit.o \
		$(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_load_check.o -o $@

$(BUILD)/wine_pe_import_check_entry.o: apps/wine_pe_import_check/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/wine_pe_export.o: ports/wine/platform/pe_export.c ports/wine/platform/pe_export.h ports/wine/platform/pe_load.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wine_pe_import.o: ports/wine/platform/pe_import.c ports/wine/platform/pe_import.h ports/wine/platform/pe_export.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wine_pe_import_check.o: apps/wine_pe_import_check/main.c ports/wine/platform/pe_import.h ports/wine/platform/pe_export.h ports/wine/platform/pe_load.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(WINE_PE_IMPORT_CHECK_ELF): $(BUILD)/wine_pe_import_check_entry.o $(BUILD)/portkit.o $(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_export.o $(BUILD)/wine_pe_import.o $(BUILD)/wine_pe_import_check.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/wine_pe_import_check_entry.o $(BUILD)/portkit.o \
		$(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_export.o \
		$(BUILD)/wine_pe_import.o $(BUILD)/wine_pe_import_check.o -o $@

$(BUILD)/wine_pe_reloc_check_entry.o: apps/wine_pe_reloc_check/entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/wine_pe_reloc.o: ports/wine/platform/pe_reloc.c ports/wine/platform/pe_reloc.h ports/wine/platform/pe_load.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wine_pe_reloc_check.o: apps/wine_pe_reloc_check/main.c ports/wine/platform/pe_reloc.h ports/wine/platform/pe_load.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(WINE_PE_RELOC_CHECK_ELF): $(BUILD)/wine_pe_reloc_check_entry.o $(BUILD)/portkit.o $(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_reloc.o $(BUILD)/wine_pe_reloc_check.o user/linker.ld
	$(LD) $(USER_LDFLAGS) $(BUILD)/wine_pe_reloc_check_entry.o $(BUILD)/portkit.o \
		$(BUILD)/wine_pe.o $(BUILD)/wine_pe_load.o $(BUILD)/wine_pe_reloc.o \
		$(BUILD)/wine_pe_reloc_check.o -o $@

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

# K0 of docs/kernel-cxx-port.md: same freestanding constraints as CXXFLAGS,
# but against the kernel's own -Iinclude headers (kernel/cxx_runtime.h) and
# soft-float ABI instead of the userspace one -- kept as its own variable so
# a kernel .cpp never accidentally picks up a userspace assumption.
KERNEL_CXXFLAGS := -std=c++17 -Os -g -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pie -fno-pic \
	-ffunction-sections -fdata-sections \
	-m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-nostdinc++ -Iinclude

$(BUILD)/kernel_cxx_runtime.o: src/kernel_cxx_runtime.cpp include/kernel/cxx_runtime.h | $(BUILD)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

# K1 of docs/kernel-cxx-port.md: the bounded_table<T,N> template proof,
# built and linked the same way as the K0 runtime above.
$(BUILD)/kernel_bounded_table_test.o: src/kernel_bounded_table_test.cpp include/kernel/bounded_table.h include/kernel/bounded_table_test.h | $(BUILD)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

# K3 of docs/kernel-cxx-port.md: the slot_table<T,N> template proof.
$(BUILD)/kernel_slot_table_test.o: src/kernel_slot_table_test.cpp include/kernel/slot_table.h include/kernel/slot_table_test.h | $(BUILD)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

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

$(ISO): $(KERNEL) $(PORTABLE_ELF) $(TETRIS_ELF) $(CXX_HELLO_ELF) $(PORTCHECK_ELF) $(DOOM_ELF) $(DOOM_FULL_ELF) $(CLASSICUBE_CORE_ELF) $(QUAKE_CORE_ELF) $(NXENGINE_CORE_ELF) $(COMPOSITOR_ELF) $(DEMONX_ELF) $(DEMONWM_ELF) $(XTERM_ELF) $(MAKO_SOURCE_ARCHIVE) $(MAKO_MANIFEST) user/sdk.mko projects/hello/main.mko docs/init-system.md docs/apps-and-git.md docs/git-port.md docs/c-apps.md docs/native-porting.md docs/freedoom-port.md docs/classicube-port.md docs/quake-port.md docs/display-address-space.md docs/framebuffer-stage1.md docs/graphics-stage2.md docs/input-stage3.md docs/process-stage4.md docs/ipc-stage5.md docs/network-stage7.md grub/grub-test.cfg
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
	cp $(CLASSICUBE_CORE_ELF) $(ISO_ROOT)/boot/mako/classicube-core.elf
	cp $(QUAKE_CORE_ELF) $(ISO_ROOT)/boot/mako/quake-core.elf
	cp $(NXENGINE_CORE_ELF) $(ISO_ROOT)/boot/mako/nxengine-core.elf
	cp $(COMPOSITOR_ELF) $(ISO_ROOT)/boot/mako/compositor.elf
	cp $(DEMONX_ELF) $(ISO_ROOT)/boot/mako/demonx.elf
	cp $(DEMONWM_ELF) $(ISO_ROOT)/boot/mako/demonwm.elf
	cp $(XTERM_ELF) $(ISO_ROOT)/boot/mako/xterm.elf
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
	cp docs/classicube-port.md $(ISO_ROOT)/docs/classicube-port.md
	cp docs/quake-port.md $(ISO_ROOT)/docs/quake-port.md
	cp docs/display-address-space.md $(ISO_ROOT)/docs/display-address-space.md
	cp docs/framebuffer-stage1.md $(ISO_ROOT)/docs/framebuffer-stage1.md
	cp docs/graphics-stage2.md $(ISO_ROOT)/docs/graphics-stage2.md
	cp docs/input-stage3.md $(ISO_ROOT)/docs/input-stage3.md
	cp docs/process-stage4.md $(ISO_ROOT)/docs/process-stage4.md
	cp docs/ipc-stage5.md $(ISO_ROOT)/docs/ipc-stage5.md
	cp docs/network-stage7.md $(ISO_ROOT)/docs/network-stage7.md
	cp grub/grub-test.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT) >/dev/null 2>&1

$(RUN_ISO): $(ISO) freedoom-assets quake-data nxengine-data nxengine-source $(NXENGINE_FREEPLAY_ELF) grub/grub.cfg
	rm -rf $(BUILD)/iso-run
	cp -r $(ISO_ROOT) $(BUILD)/iso-run
	mkdir -p $(BUILD)/iso-run/games/freedoom $(BUILD)/iso-run/licenses/doom
	cp $(FREEDOOM_WAD) $(BUILD)/iso-run/games/freedoom/freedoom1.wad
	cp $(FREEDOOM_DIR)/COPYING.txt $(BUILD)/iso-run/licenses/doom/Freedoom-COPYING.txt
	mkdir -p $(BUILD)/iso-run/games/quake $(BUILD)/iso-run/licenses/quake
	cp $(QUAKE_PAK) $(BUILD)/iso-run/games/quake/pak0.pak
	cp $(QUAKE_DATA_DIR)/SLICNSE.TXT $(BUILD)/iso-run/licenses/quake/SLICNSE.TXT
	cp $(QUAKE_DATA_DIR)/README.TXT $(BUILD)/iso-run/licenses/quake/README.TXT
	mkdir -p $(BUILD)/iso-run/games/nxengine
	cp $(NXENGINE_FREEPLAY_ELF) $(BUILD)/iso-run/boot/mako/nxengine-play-freeplay.elf
	cp $(NXENGINE_DATA)/CaveStory/data/Bullet.pbm $(BUILD)/iso-run/games/nxengine/Bullet.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/casts.pbm $(BUILD)/iso-run/games/nxengine/casts.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/0.pxm $(BUILD)/iso-run/games/nxengine/0.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Prt0.pbm $(BUILD)/iso-run/games/nxengine/Prt0.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxm $(BUILD)/iso-run/games/nxengine/Pens1.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens1.pxe $(BUILD)/iso-run/games/nxengine/Pens1.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtPens.pbm $(BUILD)/iso-run/games/nxengine/PrtPens.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/MyChar.pbm $(BUILD)/iso-run/games/nxengine/MyChar.pbm
	cp $(NXENGINE_SOURCE)/tilekey.dat $(BUILD)/iso-run/games/nxengine/tilekey.dat
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Pens.pxa $(BUILD)/iso-run/games/nxengine/Pens.pxa
	cp $(NXENGINE_SOURCE)/sprites.sif $(BUILD)/iso-run/games/nxengine/sprites.sif
	cp $(NXENGINE_DATA)/CaveStory/data/npc.tbl $(BUILD)/iso-run/games/nxengine/npc.tbl
	cp $(NXENGINE_DATA)/CaveStory/data/TextBox.pbm $(BUILD)/iso-run/games/nxengine/TextBox.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Caret.pbm $(BUILD)/iso-run/games/nxengine/Caret.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Fade.pbm $(BUILD)/iso-run/games/nxengine/Fade.pbm
	cp $(NXENGINE_SOURCE)/smalfont.bmp $(BUILD)/iso-run/games/nxengine/smalfont.bmp
	cp $(NXENGINE_DATA)/CaveStory/data/Credit.tsc $(BUILD)/iso-run/games/nxengine/Credit.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Head.tsc $(BUILD)/iso-run/games/nxengine/Head.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsItem.tsc $(BUILD)/iso-run/games/nxengine/ArmsItem.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/StageSelect.tsc $(BUILD)/iso-run/games/nxengine/StageSelect.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxm $(BUILD)/iso-run/games/nxengine/Start.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Start.pxe $(BUILD)/iso-run/games/nxengine/Start.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxm $(BUILD)/iso-run/games/nxengine/Frog.pxm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.pxe $(BUILD)/iso-run/games/nxengine/Frog.pxe
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Frog.tsc $(BUILD)/iso-run/games/nxengine/Frog.tsc
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/PrtWeed.pbm $(BUILD)/iso-run/games/nxengine/PrtWeed.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Stage/Weed.pxa $(BUILD)/iso-run/games/nxengine/Weed.pxa
	cp $(NXENGINE_DATA)/CaveStory/data/ArmsImage.pbm $(BUILD)/iso-run/games/nxengine/ArmsImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Arms.pbm $(BUILD)/iso-run/games/nxengine/Arms.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Npc/NpcSym.pbm $(BUILD)/iso-run/games/nxengine/NpcSym.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/ItemImage.pbm $(BUILD)/iso-run/games/nxengine/ItemImage.pbm
	cp $(NXENGINE_DATA)/CaveStory/data/Title.pbm $(BUILD)/iso-run/games/nxengine/Title.pbm
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
	@xorriso -indev $(ISO) -find /boot/mako/classicube-core.elf -type f 2>/dev/null | grep -q classicube-core.elf
	@xorriso -indev $(ISO) -find /boot/mako/quake-core.elf -type f 2>/dev/null | grep -q quake-core.elf
	@xorriso -indev $(ISO) -find /boot/mako/compositor.elf -type f 2>/dev/null | grep -q compositor.elf
	@xorriso -indev $(ISO) -find /boot/mako/demonx.elf -type f 2>/dev/null | grep -q demonx.elf
	@xorriso -indev $(ISO) -find /boot/mako/demonwm.elf -type f 2>/dev/null | grep -q demonwm.elf
	@xorriso -indev $(ISO) -find /boot/mako/xterm.elf -type f 2>/dev/null | grep -q xterm.elf
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
	@xorriso -indev $(ISO) -find /docs/classicube-port.md -type f 2>/dev/null | grep -q classicube-port.md
	@xorriso -indev $(ISO) -find /docs/quake-port.md -type f 2>/dev/null | grep -q quake-port.md
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
	@test $$(wc -c < $(COMPOSITOR_ELF)) -le 262144
	@test $$(wc -c < $(DEMONX_ELF)) -le 131072
	@test $$(wc -c < $(DEMONWM_ELF)) -le 131072
	@test $$(wc -c < $(XTERM_ELF)) -le 65536
	@test $$(wc -c < $(MAKO_MANIFEST)) -le 8192
	@zstd -q -t $(MAKO_SOURCE_ARCHIVE)
	@grep -Eq '^origin=https://github.com/AnimatedGTVR/MAKO([.]git)?$$' $(MAKO_MANIFEST)
	@grep -q '^archive_sha256=' $(MAKO_MANIFEST)
	@echo "MAKO ISO contents verified"

qemu: run

run: $(RUN_ISO)
	@echo "Mouse: using the reliable XWayland QEMU backend; Ctrl+Alt+G releases it"
	env GDK_BACKEND=x11 qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		$(QEMU_AUDIO_RUN) \
		-display gtk,grab-on-hover=on,zoom-to-fit=on \
		-no-reboot -no-shutdown

# Explicit alias for the normal interactive image, which includes the real
# Freedoom IWAD and exposes it to MakoBox as /games/freedoom/freedoom1.wad.
run-doom: run

# $(RUN_ISO) also carries the real NXEngine D37 freeplay build and the
# real Cave Story freeware data (see $(RUN_ISO)'s own recipe above), so
# `make run` already plays real Cave Story, not just its self-test --
# this is just an explicit alias plus a reminder of the shell command.
run-cave-story: run
	@echo "At the mako# prompt, type: cave-story"

# $(RUN_ISO) itself now also carries the real, freely redistributable
# Quake 1.06 shareware pak0.pak (see $(RUN_ISO)'s own recipe above), so
# `make run` already plays real Quake, not just its self-test -- this is
# just an explicit alias, same as run-doom, plus a reminder of the command.
# (quake-smoke deliberately targets the separate, data-free $(ISO) instead
# of $(RUN_ISO): it depends on `quake` finishing its D1-D4 self-test and
# exiting cleanly, which real gameplay mode never does on its own.)
run-quake: $(RUN_ISO)
	@echo "Mouse: using the reliable XWayland QEMU backend; Ctrl+Alt+G releases it"
	@echo "At the mako# prompt, type: quake"
	env GDK_BACKEND=x11 qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		$(QEMU_AUDIO_RUN) \
		-display gtk,grab-on-hover=on,zoom-to-fit=on \
		-no-reboot -no-shutdown
	env GDK_BACKEND=x11 qemu-system-x86_64 -cdrom $(QUAKE_PLAY_ISO) -m 256M -serial stdio \
		$(QEMU_AUDIO_RUN) \
		-display gtk,grab-on-hover=on,zoom-to-fit=on \
		-no-reboot -no-shutdown

# Native Wayland is opt-in because GTK's relative-pointer grab can render a
# perfectly healthy guest while forwarding only the first host movement on
# some compositors. Keep it available for systems where that protocol works.
run-wayland: $(RUN_ISO)
	@echo "Mouse: native Wayland mode (experimental); Ctrl+Alt+G releases it"
	env GDK_BACKEND=wayland qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		$(QEMU_AUDIO_RUN) \
		-display gtk,grab-on-hover=on,zoom-to-fit=on \
		-no-reboot -no-shutdown

# SDL is a second local fallback when the GTK backend cannot acquire a
# relative pointer grab through either XWayland or native Wayland.
run-sdl: $(RUN_ISO)
	@echo "Mouse: SDL relative-pointer mode; Ctrl+Alt+G releases it"
	qemu-system-x86_64 -cdrom $(RUN_ISO) -m 256M -serial stdio \
		$(QEMU_AUDIO_RUN) \
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
		$(QEMU_AUDIO_RUN) \
		-display vnc=:1 \
		-no-reboot -no-shutdown

smoke: $(ISO)
	@rm -f $(BUILD)/serial.log
	@timeout 15s qemu-system-x86_64 -cdrom $(ISO) -m 256M \
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
	@grep -q "CLASSICUBE_CORE_RING3_OK" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D1_SUBSYSTEMS_READY stream rng bitmap" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D2_FILE_ROUNDTRIP_OK bytes=8" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D2_WORLD_OK flatgrass=32x32x32 bytes=32768" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D3_WINDOW_BACKEND_OK surface=256x192 frames=120 input-drain=1" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D3_SOFTGPU_OK surface=256x192 loop=interactive idle-exit=180 geometry=voxels faces=exposed shaded=1 camera=player upstream=1" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_GAME_LOOP_OK input=continuous exit=escape frame-cap=36000" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_LIVE_EDIT_OK input=edge-triggered mesh=chunk-neighbourhood autosave=1" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D3_PREVIEW_OK surface=256x192 frames=120 animated=1 renderer=isometric" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_INPUT_TRANSLATION_OK key=W mouse=-7,4 button=left wheel=-1" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_INPUT_UPSTREAM_OK state=keys,pointer,buttons events=down,up,move,raw,wheel" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_PLAYER_OK controls=wasd,mouse-look,jump,sprint physics=gravity,aabb-3d,wall-slide,normalised" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_BLOCK_EDIT_OK targeting=camera-ray reach=6 actions=remove,place,pick mesh=dirty-rebuild" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_HUD_OK crosshair=target-aware hotbar=visible renderer=softgpu" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_CHUNKS_OK grid=4x4 size=8 rebuild=neighbourhood" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D2_EDITED_WORLD_OK bytes=32768 checksum=verified" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D2_STARTUP_LOAD_OK source=edited-world bytes=32768" $(BUILD)/serial.log
	@grep -q "CLASSICUBE_D4_HOTBAR_OK slots=grass,dirt,stone keys=1,2,3 wheel=cycle placement=safe" $(BUILD)/serial.log
	@grep -q "DOOM_ENGINE_READY" $(BUILD)/serial.log
	@grep -q "DOOM_VERSION_RING3_OK" $(BUILD)/serial.log
	@grep -q "DOOM_FULL_IMAGE_MAPPED pages=149 base=0x20000000" $(BUILD)/serial.log
	@grep -q "DOOM_FULL_RING3_OK" $(BUILD)/serial.log
	@grep -q "PROCESS_WAIT_CLEANUP_OK" $(BUILD)/serial.log
	@grep -q "DESKTOP_TARGET_ACTIVE pid=3 state=blocked" $(BUILD)/serial.log
	@grep -q "COMPOSITOR_SERVICE_READY" $(BUILD)/serial.log
	@grep -q "DEMONX_SERVER_READY transport=capability-ipc protocol=X11" $(BUILD)/serial.log
	@grep -q "XTERM_SMOKE_OK grid=64x22" $(BUILD)/serial.log
	@grep -q "XTERM_RING3_OK pid=5 status=0" $(BUILD)/serial.log
	@grep -q "DEMONX_LIVE_AFTER_SMOKE state=blocked" $(BUILD)/serial.log
	@grep -q "DESKTOP_STACK_SMOKE_OK compositor+demonx+xterm" $(BUILD)/serial.log
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
	@timeout 15s qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
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
		for attempt in $$(seq 1 150); do \
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
		printf 'sendkey up\nsendkey ret\n' | \
			socat - UNIX-CONNECT:$(BUILD)/monitor.sock >/dev/null; \
		for attempt in $$(seq 1 30); do \
			if grep -q "HISTORY_RECALL offset=1 command=input" $(BUILD)/keyboard.log 2>/dev/null; then break; fi; \
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
	@grep -q "HISTORY_RECALL offset=1 command=input" $(BUILD)/keyboard.log
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
	@timeout 20s qemu-system-x86_64 -cdrom $(ISO) -m 256M -vga none \
		-serial file:$(BUILD)/process.log -display none \
		-monitor unix:$(BUILD)/process-monitor.sock,server=on,wait=off \
		-no-reboot -no-shutdown >/dev/null 2>&1 & pid=$$!; \
		for attempt in $$(seq 1 150); do \
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
		for attempt in $$(seq 1 150); do \
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
	@# 2.0 MiB: the GUI/compositor stack is re-wired into the running system,
	@# so the embedded blobs are live consumers of kernel memory rather than
	@# dead weight: the wallpaper ARGB blob (~300 KiB, drawn as the desktop
	@# background by display.c), the start logo, the fixed RAMFS backing
	@# arena (kRamfsStorageMax, 1 MiB), the IPC channel table and the
	@# compositor/demonx/xterm/demonwm spawn paths. Bounded so a genuine
	@# bloat regression still fails loudly instead of growing silently
	@# forever. The hard physical ceiling is separate: kernel+modules+
	@# backbuffer must fit the 4 MiB identity-mapped window (kernel.c's
	@# 0x400000 mapping check).
	@kernel=$$(size $(KERNEL) | awk 'NR == 2 { print $$4 }'); \
		test $$kernel -le 2097152 || { echo "kernel memory footprint $$kernel exceeds 2-MiB budget"; exit 1; }
	@echo "Footprint budgets passed: init+runas <= 4 KiB, BSS <= 256 B, kernel <= 2 MiB"

check: iso-check framebuffer-fallback-smoke keyboard-smoke process-smoke ipc-smoke vfs-smoke mako-check footprint-check

size: $(KERNEL)
	@size $(KERNEL)

clean:
	rm -rf $(BUILD)

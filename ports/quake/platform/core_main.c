/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

core_main.c -- DemonOS D1 entry for the Quake port.

The narrow core links genuine upstream WinQuake units (zone.c, mathlib.c,
crc.c, cmd.c, cvar.c) inside a freestanding ring-3 environment and runs a
self-check against real engine state.  It prints markers to the serial
console and exits 0.
*/

#include <demon/portkit.h>
#include <stdint.h>
#include <stddef.h>

/* WinQuake's common.h defines qboolean as "enum { false, true }", which
   clashes with <stdbool.h> pulled in by the PortKit header. */
#undef true
#undef false
#undef bool

#include "quakedef.h"
#include "demonos_common.h"

#define QUAKE_D1_ARENA 24u * 1024u * 1024u
#define QUAKE_D1_HUNK  4u * 1024u * 1024u

static void report(int ok, const char *what)
{
    Sys_Printf(ok ? "QUAKE_D1_OK %s\n" : "QUAKE_D1_FAIL %s\n", what);
}

static void report2(int ok, const char *what)
{
    Sys_Printf(ok ? "QUAKE_D2_OK %s\n" : "QUAKE_D2_FAIL %s\n", what);
}

/* D4 references a few engine globals that upstream keeps in .c files
   (no header declaration): the registered-check pop table and the
   video-presented frame counter.  (com_searchpaths is declared in
   demonos_common.h.) */
extern unsigned short pop[];
extern int d_video_frames(void);

/* D5: the real shareware pak (id1/pak0.pak seeded from the play ISO)
   registers hundreds of files through COM_AddGameDirectory during
   Host_Init's COM_Init.  A D4 self-test boot has no such file, so the
   engine stays in the bounded self-check mode; the play boot continues
   into the unbounded game loop. */
static int d5_real_pak_files(void)
{
    searchpath_t *sp;
    for (sp = com_searchpaths; sp != NULL; sp = sp->next)
        if (sp->pack != NULL && sp->pack->numfiles > 100)
            return sp->pack->numfiles;
    return 0;
}

/*
============
D4 synthetic asset generation

The engine expects a real (registered) Quake install: gfx.wad (WAD2, one
qpic lump per picture the boot, console, sbar and menus reference), plus
gfx/palette.lmp, gfx/colormap.lmp, gfx/pop.lmp and gfx/conback.lmp as
filesystem files.  The D4 core synthesizes all of them from scratch into
the id1 gamedir so the genuine W_LoadWadFile / COM_LoadHunkFile /
COM_CheckRegistered paths are exercised against known-valid data.
============
*/
#define D4_MAX_PICS 160
#define D4_TYP_QPIC 66

static char d4_pic_names[D4_MAX_PICS][16];
static int  d4_pic_w[D4_MAX_PICS];
static int  d4_pic_h[D4_MAX_PICS];
static int  d4_pic_n;

/* The asset staging buffers are heap-allocated (not BSS) so the whole ELF
   fits the 1 MiB large-app image. */
#define D4_WAD_MAX (96 * 1024)
#define D4_CONBACK_SIZE (8 + 320 * 200)
static byte *d4_wad;
static byte *d4_conback;
static byte d4_palette[768];
static byte *d4_colormap;

static void d4_put32(byte *p, int v)
{
    p[0] = (byte)(v & 255);
    p[1] = (byte)((v >> 8) & 255);
    p[2] = (byte)((v >> 16) & 255);
    p[3] = (byte)((v >> 24) & 255);
}

static void d4_add_pic(const char *name, int w, int h)
{
    char *dst;
    int i;
    if (d4_pic_n >= D4_MAX_PICS)
        return;
    dst = d4_pic_names[d4_pic_n];
    for (i = 0; i < 15 && name[i]; ++i)
        dst[i] = name[i];
    dst[i] = 0;
    d4_pic_w[d4_pic_n] = w;
    d4_pic_h[d4_pic_n] = h;
    ++d4_pic_n;
}

static int d4_qpic_bytes(int w, int h)
{
    return 8 + w * h;
}

static void d4_emit_qpic(byte *out, int w, int h)
{
    d4_put32(out, w);
    d4_put32(out + 4, h);
    Q_memset(out + 8, 0, w * h);
}

static void d4_register_pics(void)
{
    static const char *const weapons[7] = {
        "shotgun", "sshotgun", "nailgun", "snailgun",
        "rlaunch", "srlaunch", "lightng"
    };
    char name[16];
    int i;
    int a;

    d4_add_pic("conchars", 128, 128);
    d4_add_pic("disc", 24, 24);
    d4_add_pic("backtile", 64, 64);
    d4_add_pic("ram", 8, 8);
    d4_add_pic("net", 8, 8);
    d4_add_pic("turtle", 8, 8);

    for (i = 0; i < 10; ++i)
    {
        sprintf(name, "num_%i", i);
        d4_add_pic(name, 16, 16);
    }
    for (i = 0; i < 10; ++i)
    {
        sprintf(name, "anum_%i", i);
        d4_add_pic(name, 16, 16);
    }
    d4_add_pic("num_minus", 16, 16);
    d4_add_pic("anum_minus", 16, 16);
    d4_add_pic("num_colon", 16, 16);
    d4_add_pic("num_slash", 16, 16);

    for (i = 0; i < 7; ++i)
    {
        sprintf(name, "inv_%s", weapons[i]);
        d4_add_pic(name, 24, 16);
    }
    for (i = 0; i < 7; ++i)
    {
        sprintf(name, "inv2_%s", weapons[i]);
        d4_add_pic(name, 24, 16);
    }
    for (a = 1; a <= 5; ++a)
        for (i = 0; i < 7; ++i)
        {
            sprintf(name, "inva%i_%s", a, weapons[i]);
            d4_add_pic(name, 24, 16);
        }

    d4_add_pic("sb_shells", 8, 8);
    d4_add_pic("sb_nails", 8, 8);
    d4_add_pic("sb_rocket", 8, 8);
    d4_add_pic("sb_cells", 8, 8);
    d4_add_pic("sb_armor1", 8, 8);
    d4_add_pic("sb_armor2", 8, 8);
    d4_add_pic("sb_armor3", 8, 8);
    d4_add_pic("sb_key1", 8, 8);
    d4_add_pic("sb_key2", 8, 8);
    d4_add_pic("sb_invis", 8, 8);
    d4_add_pic("sb_invuln", 8, 8);
    d4_add_pic("sb_suit", 8, 8);
    d4_add_pic("sb_quad", 8, 8);
    d4_add_pic("sb_sigil1", 8, 8);
    d4_add_pic("sb_sigil2", 8, 8);
    d4_add_pic("sb_sigil3", 8, 8);
    d4_add_pic("sb_sigil4", 8, 8);

    d4_add_pic("face1", 16, 16);
    d4_add_pic("face2", 16, 16);
    d4_add_pic("face3", 16, 16);
    d4_add_pic("face4", 16, 16);
    d4_add_pic("face5", 16, 16);
    d4_add_pic("face_p1", 16, 16);
    d4_add_pic("face_p2", 16, 16);
    d4_add_pic("face_p3", 16, 16);
    d4_add_pic("face_p4", 16, 16);
    d4_add_pic("face_p5", 16, 16);
    d4_add_pic("face_invis", 16, 16);
    d4_add_pic("face_invul2", 16, 16);
    d4_add_pic("face_inv2", 16, 16);
    d4_add_pic("face_quad", 16, 16);

    d4_add_pic("sbar", 320, 32);
    d4_add_pic("ibar", 320, 24);
    d4_add_pic("scorebar", 320, 24);
}

static void d4_build_wad(void)
{
    int header = 12;
    int dir_off;
    int off;
    int size;
    int i;

    dir_off = header;
    for (i = 0; i < d4_pic_n; ++i)
        dir_off += d4_qpic_bytes(d4_pic_w[i], d4_pic_h[i]);

    Q_memset(d4_wad, 0, D4_WAD_MAX);
    d4_wad[0] = 'W'; d4_wad[1] = 'A'; d4_wad[2] = 'D'; d4_wad[3] = '2';
    d4_put32(d4_wad + 4, d4_pic_n);
    d4_put32(d4_wad + 8, dir_off);

    off = header;
    for (i = 0; i < d4_pic_n; ++i)
    {
        size = d4_qpic_bytes(d4_pic_w[i], d4_pic_h[i]);
        d4_emit_qpic(d4_wad + off, d4_pic_w[i], d4_pic_h[i]);
        {
            byte *e = d4_wad + dir_off + i * 32;
            d4_put32(e + 0, off);
            d4_put32(e + 4, size);
            d4_put32(e + 8, size);
            e[12] = (byte)D4_TYP_QPIC;
            e[13] = 0;
            e[14] = 0;
            e[15] = 0;
            strcpy((char *)e + 16, d4_pic_names[i]);
        }
        off += size;
    }

    size = dir_off + d4_pic_n * 32;
    COM_WriteFile("gfx.wad", d4_wad, size);
    Sys_Printf("QUAKE_D4_WAD_OK lumps=%i bytes=%i\n", d4_pic_n, size);
}

static void d4_write_assets(void)
{
    byte popbuf[256];
    int i;

    /* D3 may have left com_gamedir pointing at its synthetic test dir;
       the D4 self-test assets belong in the id1 gamedir that Host_Init
       will build searchpaths from. */
    Q_strcpy(com_gamedir, "/home/demon/id1");

    d4_wad = demon_port_malloc(D4_WAD_MAX);
    d4_conback = demon_port_malloc(D4_CONBACK_SIZE);
    d4_colormap = demon_port_malloc(256 * 64);
    if (d4_wad == NULL || d4_conback == NULL || d4_colormap == NULL)
    {
        Sys_Printf("QUAKE_D4_FAIL heap=asset-oom\n");
        return;
    }

    d4_build_wad();

    d4_put32(d4_conback, 320);
    d4_put32(d4_conback + 4, 200);
    Q_memset(d4_conback + 8, 0, 320 * 200);
    COM_WriteFile("gfx/conback.lmp", d4_conback, (int)D4_CONBACK_SIZE);

    for (i = 0; i < 256; ++i)
    {
        d4_palette[i * 3 + 0] = (byte)((i * 4) & 255);
        d4_palette[i * 3 + 1] = (byte)((i * 4) & 255);
        d4_palette[i * 3 + 2] = (byte)((i * 4) & 255);
    }
    COM_WriteFile("gfx/palette.lmp", d4_palette, 768);

    for (i = 0; i < 256 * 64; ++i)
        d4_colormap[i] = (byte)(i >> 6);
    COM_WriteFile("gfx/colormap.lmp", d4_colormap, 256 * 64);

    for (i = 0; i < 128; ++i)
    {
        unsigned short v = pop[i];
        popbuf[i * 2 + 0] = (byte)(v >> 8);
        popbuf[i * 2 + 1] = (byte)(v & 255);
    }
    COM_WriteFile("gfx/pop.lmp", popbuf, 256);
}

static void test_cmd_func(void)
{
    Sys_Printf("QUAKE_D1_CMD_RAN argc=%i argv0=%s\n",
               Cmd_Argc(), Cmd_Argv(0));
}

int quake_core_main(void)
{
    void *hunk_mem;
    void *zptr;
    char *val;
    static cvar_t test_var;
    static cvar_t d2_var;
    vec3_t v;
    float len;
    unsigned short crc;
    const char *crc_data = "id Software";
    int i;
    int lowmark;
    double t0, t1;

    if (!demon_port_init_dynamic(QUAKE_D1_ARENA)) return 10u;

    demon_port_write("QUAKE_D1_START\n");

    Sys_Printf("QUAKE_D1_VERSION winquake=%.3f\n", (double)WINQUAKE_VERSION);

    hunk_mem = demon_port_malloc(QUAKE_D1_HUNK);
    if (hunk_mem == NULL)
    {
        report(0, "zone arena");
        demon_port_exit(1u);
    }
    Memory_Init(hunk_mem, QUAKE_D1_HUNK);

    zptr = Z_Malloc(100);
    if (zptr == NULL)
    {
        report(0, "zone malloc");
        demon_port_exit(1u);
    }
    {
        unsigned char *p = (unsigned char *)zptr;
        int clean = 1;
        for (i = 0; i < 100; ++i)
            if (p[i] != 0) clean = 0;
        report(clean, "zone zero-fill");
    }
    Z_Free(zptr);

    lowmark = Hunk_LowMark();
    if (lowmark < 0)
    {
        report(0, "hunk mark");
        demon_port_exit(1u);
    }
    {
        void *h = Hunk_Alloc(77);
        report(((uintptr_t)h % 16u) == 0u, "hunk alignment");
    }
    Hunk_FreeToLowMark(lowmark);
    report(1, "hunk lowmark/free");

    CRC_Init(&crc);
    for (i = 0; crc_data[i]; ++i) CRC_ProcessByte(&crc, (byte)crc_data[i]);
    report(CRC_Value(crc) == 36889u, "crc bytes");

    v[0] = 0.0f; v[1] = 0.0f; v[2] = 0.0f;
    v[0] = 3.0f;
    v[1] = 4.0f;
    len = VectorNormalize(v);
    {
        int ok = len > 4.99f && len < 5.01f &&
                 v[0] > 0.59f && v[0] < 0.61f &&
                 v[1] > 0.79f && v[1] < 0.81f &&
                 v[2] > -0.01f && v[2] < 0.01f;
        report(ok, "vector normalize");
    }

    Cbuf_Init();
    Cmd_AddCommand("quake_selftest", test_cmd_func);

    test_var.name = "quake_testvar";
    test_var.string = "42";
    test_var.archive = 0;
    test_var.server = 0;
    test_var.value = 0.0f;
    test_var.next = NULL;
    Cvar_RegisterVariable(&test_var);

    val = Cvar_VariableString("quake_testvar");
    report(val != NULL && val[0] == '4' && val[1] == '2' && val[2] == 0,
           "cvar register");

    Cvar_SetValue("quake_testvar", 7.0f);
    val = Cvar_VariableString("quake_testvar");
    report(val != NULL && val[0] == '7' && Q_atof(val) > 6.99f &&
           Q_atof(val) < 7.01f, "cvar setvalue");

    Cbuf_AddText("quake_selftest\n");
    Cbuf_Execute();
    report(Cmd_CheckParm("quake_selftest") == 0, "cmd checkparm");

    t0 = Sys_FloatTime();
    demon_port_sleep_ms(5u);
    t1 = Sys_FloatTime();
    report(t1 > t0, "float time");

    /* ---------- D2: console, command core, PortKit files ---------- */

    /* The D2 self-check drives the genuine upstream console machinery
       (Cmd_Init registers exec/echo/alias/cmd/wait/stuffcmds) and the real
       COM_WriteFile/COM_LoadHunkFile file paths over the vanilla Sys_File*
       shim. Config and save files live under the MakoBox home. */
    COM_ByteSwapInit();
    COM_AddGameDirectory("/home/demon");
    Cmd_Init();

    d2_var.name = "quake_d2_var";
    d2_var.string = "0";
    d2_var.archive = 0;
    d2_var.server = 0;
    d2_var.value = 0.0f;
    d2_var.next = NULL;
    Cvar_RegisterVariable(&d2_var);

    report2(Cmd_Exists("exec") && Cmd_Exists("echo") &&
            Cmd_Exists("alias") && Cmd_Exists("wait") &&
            Cmd_Exists("stuffcmds"), "cmd init");

    {
        static const char quake_d2_config[] =
            "echo config_loaded\n"
            "quake_d2_var 23\n";
        COM_WriteFile("quake-test.cfg", (void *)quake_d2_config,
                      (int)(sizeof(quake_d2_config) - 1u));

        {
            int handle;
            int len = Sys_FileOpenRead("/home/demon/quake-test.cfg", &handle);
            int ok = handle >= 0 && len == (int)(sizeof(quake_d2_config) - 1u);
            if (ok)
            {
                char readback[128];
                Q_memset(readback, 0, sizeof(readback));
                ok = Sys_FileRead(handle, readback, len) == len &&
                     Q_memcmp(readback, quake_d2_config, len) == 0;
                Sys_FileClose(handle);
            }
            Sys_Printf(ok ? "QUAKE_D2_FILE_ROUNDTRIP_OK bytes=%i\n"
                          : "QUAKE_D2_FILE_ROUNDTRIP_FAIL\n", len);
        }

        {
            byte *loaded = COM_LoadHunkFile("quake-test.cfg");
            report2(loaded != NULL &&
                    Q_memcmp(loaded, quake_d2_config,
                             (int)(sizeof(quake_d2_config) - 1u)) == 0,
                    "com loadhunkfile");
        }
    }

    /* Console stream, phase A: inline cvar assignment, echo, and exec of the
       file just written. The exec must run last so the config's value wins. */
    Cbuf_AddText("quake_d2_var 17\n");
    Cbuf_AddText("echo inline_echo\n");
    Cbuf_AddText("exec quake-test.cfg\n");
    Cbuf_Execute();
    report2(Cvar_VariableValue("quake_d2_var") == 23.0f,
            "console exec+set");

    /* Console stream, phase B: the alias dispatch path. */
    Cbuf_AddText("alias quake_aliastest \"quake_d2_var 99\"\n");
    Cbuf_AddText("quake_aliastest\n");
    Cbuf_Execute();
    report2(Cvar_VariableValue("quake_d2_var") == 99.0f,
            "console alias");

    Sys_Printf("QUAKE_D2_CONSOLE_OK cmd_file=%s\n", com_gamedir);

    /* ---------- D3: asset pipeline ---------- */

    /* Build a synthetic PAK in the genuine Quake packfile format and load it
       through the real upstream COM_LoadPackFile + searchpath path. No game
       data is bundled; the D3 smoke generates the pack so the loader itself
       is verified against known bytes. */
    {
        enum {
            D3_PALETTE_BYTES = 768,
            D3_TEXT_BYTES = 18,
            D3_DATA_OFF = 140,
            D3_DIR_OFF = D3_DATA_OFF + D3_PALETTE_BYTES + D3_TEXT_BYTES,
            D3_PAK_BYTES = D3_DIR_OFF + 128
        };
        static const char d3_text[D3_TEXT_BYTES] = "QUAKE_D3_ASSET_OK\n";
        static const char *const d3_names[2] = { "palette.lmp",
                                                 "quake_d3.txt" };
        static const int d3_pos[2] = { D3_DATA_OFF,
                                       D3_DATA_OFF + D3_PALETTE_BYTES };
        static const int d3_len[2] = { D3_PALETTE_BYTES, D3_TEXT_BYTES };
        char pak[D3_PAK_BYTES];
        unsigned short crc;
        int dircrc;
        pack_t *pack;
        byte *lump;
        int ok;
        int i;
        int j;
        int off;

        Q_memset(pak, 0, sizeof(pak));
        pak[0] = 'P'; pak[1] = 'A'; pak[2] = 'C'; pak[3] = 'K';
        pak[4] = (char)(D3_DIR_OFF & 255); pak[5] = (char)((D3_DIR_OFF >> 8) & 255);
        pak[6] = (char)((D3_DIR_OFF >> 16) & 255); pak[7] = (char)((D3_DIR_OFF >> 24) & 255);
        pak[8] = 128; pak[9] = 0; pak[10] = 0; pak[11] = 0;
        for (i = 0; i < D3_PALETTE_BYTES; ++i)
            pak[D3_DATA_OFF + i] = (char)(i & 255);
        for (i = 0; i < D3_TEXT_BYTES; ++i)
            pak[D3_DATA_OFF + D3_PALETTE_BYTES + i] = d3_text[i];
        for (j = 0; j < 2; ++j)
        {
            off = D3_DIR_OFF + j * 64;
            Q_strcpy(pak + off, (char *)d3_names[j]);
            pak[off + 56] = (char)(d3_pos[j] & 255);
            pak[off + 57] = (char)((d3_pos[j] >> 8) & 255);
            pak[off + 58] = (char)((d3_pos[j] >> 16) & 255);
            pak[off + 59] = (char)((d3_pos[j] >> 24) & 255);
            pak[off + 60] = (char)(d3_len[j] & 255);
            pak[off + 61] = (char)((d3_len[j] >> 8) & 255);
            pak[off + 62] = (char)((d3_len[j] >> 16) & 255);
            pak[off + 63] = (char)((d3_len[j] >> 24) & 255);
        }

        COM_WriteFile("d3test/pak0.pak", pak, D3_PAK_BYTES);

        CRC_Init(&crc);
        for (i = 0; i < 128; ++i)
            CRC_ProcessByte(&crc, (byte)pak[D3_DIR_OFF + i]);
        dircrc = (int)CRC_Value(crc);

        pack = COM_LoadPackFile("/home/demon/d3test/pak0.pak");
        ok = pack != NULL && pack->numfiles == 2 && com_modified;
        Sys_Printf(ok ? "QUAKE_D3_PAK_OK pak=d3test numfiles=%i crc=%i "
                         "dir=verified\n"
                      : "QUAKE_D3_PAK_FAIL numfiles=%i\n",
                   ok ? pack->numfiles : -1, dircrc);

        COM_AddGameDirectory("/home/demon/d3test");

        lump = COM_LoadHunkFile("palette.lmp");
        ok = lump != NULL && lump[0] == 0 && lump[255] == (byte)255 &&
             lump[767] == (byte)255;
        Sys_Printf(ok ? "QUAKE_D3_LOAD_OK lump=palette.lmp bytes=%i\n"
                      : "QUAKE_D3_LOAD_FAIL lump=palette.lmp\n",
                   D3_PALETTE_BYTES);

        lump = COM_LoadHunkFile("quake_d3.txt");
        ok = lump != NULL && Q_memcmp(lump, (void *)d3_text, D3_TEXT_BYTES) == 0;
        Sys_Printf(ok ? "QUAKE_D3_LOAD_OK lump=quake_d3.txt bytes=%i\n"
                      : "QUAKE_D3_LOAD_FAIL lump=quake_d3.txt\n",
                   D3_TEXT_BYTES);

        {
            /* A missing lump must fail cleanly through the searchpath. */
            int h;
            ok = COM_OpenFile("does-not-exist.lmp", &h) == -1 && h == -1;
            Sys_Printf(ok ? "QUAKE_D3_LOAD_OK lump=missing miss=1\n"
                          : "QUAKE_D3_LOAD_FAIL lump=missing\n");
        }

        Sys_Printf("QUAKE_D3_ASSET_SUBSYSTEMS_READY pak=id1 gamedir=%s\n",
                   com_gamedir);
    }

    demon_port_write("QUAKE_D1_SUBSYSTEMS_READY zone crc mathlib cmd cvar\n");
    demon_port_write("QUAKE_D2_SUBSYSTEMS_READY console cmd files\n");
    demon_port_write("QUAKE_D3_SUBSYSTEMS_READY assets pak crc\n");

    /* ---------- D4: full engine boot ---------- */

    /* D1-D3 left the searchpath list allocated in the abandoned pre-D4
       zone; Host_Init rebuilds memory, so drop the stale path. */
    com_searchpaths = NULL;

    d4_register_pics();
    d4_write_assets();

    {
        quakeparms_t parms;
        void *membase;
        int memsize = 16 * 1024 * 1024;
        char *bootargv[1];
        int run;
        int limit = 60;
        int video = 0;
        /* Host_Frame wants the elapsed time *since the last call*, not an
           absolute clock reading -- passing Sys_FloatTime() directly (as
           this port originally did) made Host_FilterTime's accumulated
           "realtime" balloon by a whole absolute timestamp every frame,
           running the simulation absurdly fast. Track the last reading and
           pass the delta instead, like every other Quake platform layer. */
        double d4_last_time;

        Q_memset(&parms, 0, sizeof(parms));

        membase = demon_port_malloc((size_t)memsize);
        if (membase == NULL)
        {
            Sys_Printf("QUAKE_D4_FAIL mem=%i\n", memsize);
            demon_port_exit(2u);
        }

        bootargv[0] = (char *)"quake-demonos";
        COM_InitArgv(1, bootargv);

        parms.basedir = "/home/demon";
        parms.memsize = memsize;
        parms.membase = membase;
        parms.argc = com_argc;
        parms.argv = com_argv;

        Host_Init(&parms);

        d4_last_time = Sys_FloatTime();
        for (run = 0; run < limit; ++run)
        {
            double now = Sys_FloatTime();
            Host_Frame((float)(now - d4_last_time));
            d4_last_time = now;
            if (d_video_frames() > 0)
            {
                video = 1;
                break;
            }
        }
        Sys_Printf("QUAKE_D4_FRAMES %i video=%i\n", run + 1, d_video_frames());

        Sys_Printf("QUAKE_D4_BOOT_OK heap=%i frames=%i\n", memsize,
                   d_video_frames());

        demon_port_write("QUAKE_D4_SUBSYSTEMS_READY host vid draw sbar console\n");
        demon_port_write(video ? "QUAKE_D4_VIDEO_READY presented\n"
                               : "QUAKE_D4_VIDEO_READY no-present\n");

        /* ---------- D5: playable Quake ---------- */

        {
            int pakfiles = d5_real_pak_files();
            int map_ready = 0;
            int play_ready = 0;

            if (pakfiles == 0)
            {
                /* D4 self-test boot: no game data, exit as before. */
                Sys_Printf("QUAKE_D5_NO_PAK self-test-mode\n");
            }
            else
            {
                const char *d5_map = "e1m1";

                Sys_Printf("QUAKE_D5_PAK_REAL numfiles=%i\n", pakfiles);

                /* Nothing ever binds a key in this port: there is no
                   quake.rc/default.cfg (the shareware pak0.pak doesn't
                   carry one, and this port doesn't ship its own -- see
                   the real, faithfully-reproduced "couldn't exec
                   quake.rc" console line every boot already prints).
                   Without this, in_demonos.c's upstream-faithful
                   IN_MouseMove (mouse yaw always active; pitch gated on
                   +mlook, exactly like real WinQuake) has nothing to
                   pair with on the keyboard side at all. This is the
                   real, historical id default.cfg bind set (arrow keys,
                   ctrl/space/shift, +moveleft/+moveright already being
                   genuine vanilla Quake commands) plus WASD aliases for
                   the arrow keys and MOUSE1 for +attack, which are
                   additions, not replacements -- and a permanent
                   "+mlook" (issued with no matching "-mlook", so it never
                   turns back off) so the mouse controls both look axes
                   by default instead of needing a held key, matching
                   modern expectations without changing engine behavior. */
                Cbuf_AddText(
                    "bind UPARROW +forward\n"
                    "bind DOWNARROW +back\n"
                    "bind LEFTARROW +left\n"
                    "bind RIGHTARROW +right\n"
                    "bind w +forward\n"
                    "bind s +back\n"
                    "bind a +moveleft\n"
                    "bind d +moveright\n"
                    "bind SPACE +jump\n"
                    "bind CTRL +attack\n"
                    "bind MOUSE1 +attack\n"
                    "bind SHIFT +speed\n"
                    "bind ALT +strafe\n"
                    "+mlook\n"
                );
                Cbuf_Execute();

                Cbuf_AddText("map e1m1\n");
                Cbuf_Execute();

                /* run is shared with the D4 frame-counting loop above and
                   holds whatever value that loop ended at -- reset before
                   counting this loop's own frames. */
                run = 0;
                d4_last_time = Sys_FloatTime();

                /* Real, indefinite play: no frame cap and no self-imposed
                   break. quake-play-smoke's automated test only ever waits
                   to SEE the QUAKE_D5_PLAY_READY marker in the serial log
                   before killing the whole QEMU process externally (see
                   Makefile) -- it never required this loop to exit on its
                   own, so removing the old 60-frame bounded-then-break
                   smoke-test proof doesn't break that test. Real
                   termination now works exactly like upstream WinQuake:
                   this loop has no exit condition of its own, and
                   Sys_Quit (sys_demonos.c) -- reached from the console
                   "quit" command or the in-game Quit menu, both real,
                   already-working Quake code paths -- calls
                   demon_port_exit directly, ending the process from
                   inside the loop rather than through it returning. */
                for (;;)
                {
                    double now = Sys_FloatTime();
                    Host_Frame((float)(now - d4_last_time));
                    d4_last_time = now;
                    if (!map_ready && sv.active && cls.state == ca_connected)
                    {
                        Sys_Printf("QUAKE_D5_MAP_READY map=%s\n", d5_map);
                        map_ready = 1;
                    }
                    if (map_ready && !play_ready)
                    {
                        Sys_Printf("QUAKE_D5_PLAY_READY frames=%i\n", run + 1);
                        play_ready = 1;
                    }
                    ++run;
                }
            }
        }

        Host_Shutdown();

    }

    demon_port_exit(0u);
    return 0;
}

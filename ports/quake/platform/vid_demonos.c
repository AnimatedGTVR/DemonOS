/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

vid_demonos.c -- Quake software video driver for DemonOS.

The engine renders 320x200 palette-indexed pixels into vid.buffer exactly as
the upstream null driver does; VID_Update expands them through the current
palette and presents the frame to the DemonOS display service, mirroring the
proven path the Doom port uses (surface write + damage + scaled display
submit).  Service numbers and request layouts match include/demon/c_app.h.
*/

#include "quakedef.h"
#include "d_local.h"

#include <demon/c_app.h>
#include <demon/portkit.h>
#include <stdint.h>

#define D_BASEWIDTH	320
#define D_BASEHEIGHT	200

/* Video buffers live on the demon heap (not BSS) so the whole ELF fits the
   1 MiB large-app image: 614K surface cache + 256K ARGB + 128K Z + 64K
   framebuffer.  They are only valid between VID_Init and VID_Shutdown. */
static byte	*d_vid_buffer;
static short	*d_zbuffer;
/* D_SurfaceCacheForRes is a function (not a constant), so the cache is a
   fixed-size buffer exactly matching its 320x200 result, like vid_null.c. */
#define D_SURFCACHE_BYTES (SURFCACHE_SIZE_AT_320X200 + 16)
static byte	*d_surfcache;

viddef_t	vid;				// global video state

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

/* 24-bit ARGB staging for presentation (heap), plus the current 768-byte
   palette. */
static uint32_t	*d_argb;
static byte	d_palette[768];

/* Framebuffer plumbing (mirrors the Doom port's doom_video layout). */
struct d_display_info {
	uint64_t width;
	uint64_t height;
	uint64_t stride_pixels;
	uint64_t format;
	uint64_t max_transfer_pixels;
};

struct d_display_submit {
	uint64_t x;
	uint64_t y;
	uint64_t width;
	uint64_t height;
	uint64_t pixels;
	uint64_t flags;
};

static struct d_display_info d_info;
static uint64_t d_display = UINT64_MAX;
static uint64_t d_surface_factory = UINT64_MAX;
static uint64_t d_surface = UINT64_MAX;
static uint32_t *d_scaled = NULL;
static uint32_t d_scale = 1u;
static int d_presented = 0;
static int d_frames = 0;

void VID_Shutdown (void);
void VID_SetPalette (unsigned char *palette);

/*
===============
BuildLightTable
===============
*/
static void d_build_tables (void)
{
	int i;

	for (i = 0; i < 256; ++i)
	{
		d_8to16table[i] = (unsigned short)
			((((d_palette[i * 3 + 0] & 0xF8u) << 8) |
			  ((d_palette[i * 3 + 1] & 0xFCu) << 3) |
			  ((d_palette[i * 3 + 2] & 0xF8u) >> 3)));
		d_8to24table[i] = ((unsigned)d_palette[i * 3 + 0] << 16) |
			((unsigned)d_palette[i * 3 + 1] << 8) |
			(unsigned)d_palette[i * 3 + 2];
	}
}

/*
===============
VID_SetPalette
===============
*/
void VID_SetPalette (unsigned char *palette)
{
	memcpy (d_palette, palette, sizeof(d_palette));
	d_build_tables ();
}

/*
===============
VID_ShiftPalette
===============
*/
void VID_ShiftPalette (unsigned char *palette)
{
	VID_SetPalette (palette);
}

/*
===============
D_BeginDirectRect
===============
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	(void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}

/*
===============
D_EndDirectRect
===============
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
	(void)x; (void)y; (void)width; (void)height;
}

/*
===============
VID_Update

Present the whole 320x200 palette-indexed buffer through the current
palette.  The engine only ever hands us the dirty-rect list; expanding the
full frame on every call is the same work at this resolution and avoids
aliasing bugs between the rect list and the present path.
===============
*/
void VID_Update (vrect_t *rects)
{
	int i;
	uint32_t *dest;
	const byte *src;

	(void)rects;

	src = vid.buffer;
	dest = d_argb;
	for (i = 0; i < D_BASEWIDTH * D_BASEHEIGHT; ++i)
		*dest++ = 0xFF000000u | d_8to24table[*src++];

	if (d_surface != UINT64_MAX)
	{
		if (demon_surface_write (d_surface, d_argb,
					(uint64_t)(D_BASEWIDTH * D_BASEHEIGHT), 0u) ==
		    (uint64_t)(D_BASEWIDTH * D_BASEHEIGHT))
			(void)demon_surface_damage (d_surface, 0u, 0u,
				(uint64_t)D_BASEWIDTH, (uint64_t)D_BASEHEIGHT);
	}

	{
		const uint32_t *present = d_argb;
		uint32_t pwidth = D_BASEWIDTH;
		uint32_t pheight = D_BASEHEIGHT;
		struct d_display_submit request;

		if (d_scale > 1u && d_scaled != NULL)
		{
			uint32_t sy, sx, ry, rx;
			pwidth = D_BASEWIDTH * d_scale;
			pheight = D_BASEHEIGHT * d_scale;
			for (sy = 0; sy < D_BASEHEIGHT; ++sy)
			{
				for (ry = 0; ry < d_scale; ++ry)
				{
					uint32_t *row = d_scaled +
						(sy * d_scale + ry) * pwidth;
					for (sx = 0; sx < D_BASEWIDTH; ++sx)
					{
						const uint32_t pixel = d_argb[sy * D_BASEWIDTH + sx];
						for (rx = 0; rx < d_scale; ++rx)
							row[sx * d_scale + rx] = pixel;
					}
				}
			}
			present = d_scaled;
		}

		request.x = d_info.width > pwidth ? (d_info.width - pwidth) / 2u : 0u;
		request.y = d_info.height > pheight ? (d_info.height - pheight) / 2u : 0u;
		request.width = pwidth;
		request.height = pheight;
		request.pixels = (uint64_t)(uintptr_t)present;
		request.flags = 1u;
		if (demon_display_submit (d_display, &request) == 0u)
		{
			++d_frames;
			if (!d_presented)
			{
				d_presented = 1;
				Sys_Printf ("QUAKE_D4_VID_OK surface=%ux%u frames=%i\n",
					    D_BASEWIDTH, D_BASEHEIGHT, d_frames);
			}
		}
	}
}

/*
===============
VID_SetMode

The DemonOS driver is fixed at 320x200 (mode 0), matching the null driver.
The engine only calls this to drop back to the base mode on allocation
failures.
===============
*/
int VID_SetMode (int modenum, unsigned char *palette)
{
	if (modenum != 0)
		return 0;
	Cvar_SetValue ("vid_mode", 0.0f);
	VID_SetPalette (palette);
	return 1;
}

/*
===============
VID_Init
===============
*/
void VID_Init (unsigned char *palette)
{
	d_vid_buffer = demon_port_malloc (D_BASEWIDTH * D_BASEHEIGHT);
	d_zbuffer = demon_port_malloc ((size_t)D_BASEWIDTH * D_BASEHEIGHT *
				       sizeof(*d_zbuffer));
	d_surfcache = demon_port_malloc (D_SURFCACHE_BYTES);
	d_argb = demon_port_malloc ((size_t)D_BASEWIDTH * D_BASEHEIGHT *
				    sizeof(*d_argb));
	if (d_vid_buffer == NULL || d_zbuffer == NULL ||
	    d_surfcache == NULL || d_argb == NULL)
	{
		Sys_Printf ("QUAKE_D4_VID_FAIL heap=oom\n");
		return;
	}

	vid.maxwarpwidth = vid.width = vid.conwidth = D_BASEWIDTH;
	vid.maxwarpheight = vid.height = vid.conheight = D_BASEHEIGHT;
	vid.aspect = 1.0;
	vid.numpages = 1;
	vid.colormap = (pixel_t *)host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.buffer = vid.conbuffer = d_vid_buffer;
	vid.rowbytes = vid.conrowbytes = D_BASEWIDTH;
	vid.recalc_refdef = 0;

	memset (d_palette, 0, sizeof(d_palette));
	VID_SetPalette (palette);

	d_pzbuffer = d_zbuffer;
	D_InitCaches (d_surfcache, D_SURFCACHE_BYTES);

	/* Wire the display service.  demo services match the Doom port:
	   7 == DISPLAY, 9 == SURFACE factory. */
	d_display = demon_service_open (7u);
	d_surface_factory = demon_service_open (9u);
	if (d_display == UINT64_MAX || d_surface_factory == UINT64_MAX)
	{
		Sys_Printf ("QUAKE_D4_VID_FAIL display=unavailable\n");
		return;
	}
	if (demon_display_info (d_display, &d_info) == UINT64_MAX)
	{
		Sys_Printf ("QUAKE_D4_VID_FAIL display=no-info\n");
		return;
	}

	{
		uint64_t scale = d_info.width / D_BASEWIDTH;
		uint64_t vscale = d_info.height / D_BASEHEIGHT;
		if (vscale < scale) scale = vscale;
		/* Was capped at 3 (960x600); raised to 4 (1280x800) for a more
		   usable window on a real modern display. Not raised further:
		   the 24 MiB process arena (QUAKE_D1_ARENA) already gives 16 MiB
		   to Quake's own Hunk heap, and scale 6 alone would need another
		   ~9 MiB for d_scaled on top of the zbuffer/surface cache/d_argb
		   -- likely to fail the allocation below rather than render
		   bigger. That failure is already handled safely (falls back to
		   scale 1, not a crash), so this cap is a memory-budget choice,
		   not a hard technical ceiling. */
		if (scale > 4u) scale = 4u;
		while (scale > 1u && (uint64_t)D_BASEWIDTH * scale >
				      d_info.max_transfer_pixels)
			--scale;
		if (scale > 1u)
		{
			const uint64_t count = (uint64_t)D_BASEWIDTH * scale *
					       D_BASEHEIGHT * scale;
			d_scaled = (uint32_t *)demon_port_malloc (
				(size_t)count * sizeof(*d_scaled));
			if (d_scaled != NULL) d_scale = (uint32_t)scale;
		}
	}

	d_surface = demon_surface_create (d_surface_factory,
					 (uint64_t)D_BASEWIDTH,
					 (uint64_t)D_BASEHEIGHT);
	if (d_surface == UINT64_MAX)
		Sys_Printf ("QUAKE_D4_VID_FAIL surface=unavailable\n");
	else
		Sys_Printf ("QUAKE_D4_VID_INIT surface=%ux%u scale=%u\n",
			    D_BASEWIDTH, D_BASEHEIGHT, d_scale);
}

/*
===============
VID_Shutdown
===============
*/
void VID_Shutdown (void)
{
	if (d_surface != UINT64_MAX)
		(void)demon_handle_close (d_surface);
	d_surface = UINT64_MAX;
	if (d_scaled != NULL)
		demon_port_free (d_scaled);
	d_scaled = NULL;
	if (d_argb != NULL)
		demon_port_free (d_argb);
	if (d_surfcache != NULL)
		demon_port_free (d_surfcache);
	if (d_zbuffer != NULL)
		demon_port_free (d_zbuffer);
	if (d_vid_buffer != NULL)
		demon_port_free (d_vid_buffer);
	d_argb = NULL;
	d_surfcache = NULL;
	d_zbuffer = NULL;
	d_vid_buffer = NULL;
}

/*
===============
VID_HandlePause
===============
*/
void VID_HandlePause (qboolean pause)
{
	(void)pause;
}

/*
===============
d_video_frames

Frames actually presented to the DemonOS display, for the D4/D5 markers.
===============
*/
int d_video_frames (void)
{
	return d_frames;
}

/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

com_demonos.c -- DemonOS D1/D2 glue providing the small subset of upstream
common.c that the narrow core needs (Q_* string/memory helpers, COM_Parse,
COM_CheckParm, SZ_* sizebufs, COM_FileBase, COM_WriteFile, MSG_WriteByte,
MSG_WriteString) plus console output routing and the D2 PortKit-backed
COM_LoadFile/COM_LoadHunkFile.

The functions below are faithful ports of the corresponding upstream
common.c functions; only Con_Printf, the file-loading glue, SV_BroadcastPrintf
and the global state are DemonOS shims.
*/

#include "quakedef.h"
#include "demonos_common.h"

#include <stdarg.h>
#include <stdint.h>

char com_token[1024];
int com_argc;
char **com_argv;

/* D2: config and save files live under the MakoBox home, like ClassiCube's
   persisted world file. core_main.c sets this before COM_WriteFile/exec. */
char com_gamedir[MAX_OSPATH];

void Q_memset (void *dest, int fill, int count)
{
	int             i;

	if ( (((long)dest | count) & 3) == 0)
	{
		count >>= 2;
		fill = fill | (fill<<8) | (fill<<16) | (fill<<24);
		for (i=0 ; i<count ; i++)
			((int *)dest)[i] = fill;
	}
	else
		for (i=0 ; i<count ; i++)
			((byte *)dest)[i] = fill;
}

void Q_memcpy (void *dest, void *src, int count)
{
	int             i;

	if (( ( (long)dest | (long)src | count) & 3) == 0 )
	{
		count>>=2;
		for (i=0 ; i<count ; i++)
			((int *)dest)[i] = ((int *)src)[i];
	}
	else
		for (i=0 ; i<count ; i++)
			((byte *)dest)[i] = ((byte *)src)[i];
}

int Q_memcmp (void *m1, void *m2, int count)
{
	while(count)
	{
		count--;
		if (((byte *)m1)[count] != ((byte *)m2)[count])
			return -1;
	}
	return 0;
}

void Q_strcpy (char *dest, char *src)
{
	while (*src)
	{
		*dest++ = *src++;
	}
	*dest++ = 0;
}

void Q_strncpy (char *dest, char *src, int count)
{
	while (*src && count--)
	{
		*dest++ = *src++;
	}
	if (count)
		*dest++ = 0;
}

int Q_strlen (char *str)
{
	int             count;

	count = 0;
	while (str[count])
		count++;

	return count;
}

char *Q_strrchr(char *s, char c)
{
    int len = Q_strlen(s);
    s += len;
    while (len--)
	if (*--s == c) return s;
    return 0;
}

void Q_strcat (char *dest, char *src)
{
	dest += Q_strlen(dest);
	Q_strcpy (dest, src);
}

int Q_strcmp (char *s1, char *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return -1;              // strings not equal
		if (!*s1)
			return 0;               // strings are equal
		s1++;
		s2++;
	}
}

int Q_strncmp (char *s1, char *s2, int count)
{
	while (1)
	{
		if (!count--)
			return 0;
		if (*s1 != *s2)
			return -1;              // strings not equal
		if (!*s1)
			return 0;               // strings are equal
		s1++;
		s2++;
	}
}

int Q_strncasecmp (char *s1, char *s2, int n)
{
	int             c1, c2;

	while (1)
	{
		c1 = *s1++;
		c2 = *s2++;

		if (!n--)
			return 0;               // strings are equal until end point

		if (c1 != c2)
		{
			if (c1 >= 'a' && c1 <= 'z')
				c1 -= ('a' - 'A');
			if (c2 >= 'a' && c2 <= 'z')
				c2 -= ('a' - 'A');
			if (c1 != c2)
				return -1;              // strings not equal
		}
		if (!c1)
			return 0;               // strings are equal
	}
}

int Q_strcasecmp (char *s1, char *s2)
{
	return Q_strncasecmp (s1, s2, 99999);
}

int Q_atoi (char *str)
{
	int             val;
	int             sign;
	int             c;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}
}

float Q_atof (char *str)
{
	double			val;
	int             sign;
	int             c;
	int             decimal, total;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	decimal = -1;
	total = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
			break;
		val = val*10 + c - '0';
		total++;
	}

	if (decimal == -1)
		return val*sign;

	while (total > decimal)
	{
		val /= 10;
		total--;
	}

	return val*sign;
}

char *COM_Parse (char *data)
{
	int             c;
	int             len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ( (c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;                    // end of file;
		data++;
	}

// skip // comments
	if (c=='/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

// parse single characters
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
			break;
	} while (c>32);

	com_token[len] = 0;
	return data;
}

int COM_CheckParm (char *parm)
{
	int             i;

	for (i=1 ; i<com_argc ; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.
		if (!Q_strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = Hunk_AllocName (startsize, "sizebuf");
	buf->maxsize = startsize;
	buf->cursize = 0;
}

void SZ_Free (sizebuf_t *buf)
{
//      Z_Free (buf->data);
//      buf->data = NULL;
//      buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void    *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf);
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, void *data, int length)
{
	Q_memcpy (SZ_GetSpace(buf,length),data,length);
}

void SZ_Print (sizebuf_t *buf, char *data)
{
	int             len;

	len = Q_strlen(data)+1;

// byte * cast to keep VC++ happy
	if (buf->data[buf->cursize-1])
		Q_memcpy ((byte *)SZ_GetSpace(buf, len),data,len); // no trailing 0
	else
		Q_memcpy ((byte *)SZ_GetSpace(buf, len-1)-1,data,len); // write over trailing 0
}

/* =============================================================================
   D3: byte-swap machinery and the PAK filesystem (faithful common.c ports).
   ============================================================================= */

qboolean bigendien;

short (*BigShort) (short l);
short (*LittleShort) (short l);
int (*BigLong) (int l);
int (*LittleLong) (int l);
float (*BigFloat) (float l);
float (*LittleFloat) (float l);

short ShortSwap (short l)
{
	byte    b1,b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int    LongSwap (int l)
{
	byte    b1,b2,b3,b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

int     LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float   f;
		byte    b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

/* Faithful subset of upstream COM_Init's byte-order setup, separated out
   because the D3 core registers none of COM_Init's cvars/commands yet. */
void COM_ByteSwapInit (void)
{
	byte    swaptest[2] = {1,0};

	if ( *(short *)swaptest == 1)
	{
		bigendien = false;
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}
	else
	{
		bigendien = true;
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
}

//
// in memory
//
typedef struct
{
	char    name[56];
	int             filepos, filelen;
} dpackfile_t;

typedef struct
{
	char    id[4];
	int             dirofs;
	int             dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK       2048

#define PAK0_COUNT              339
#define PAK0_CRC                32981

qboolean        com_modified;
qboolean        proghack;
int             static_registered = 1;

int             com_filesize;

searchpath_t    *com_searchpaths;

/*
===========
COM_FindFile

Finds the file in the search path. Sets com_filesize and one of handle
or file. Faithful to the upstream common.c function: the pak branch seeks
the shared pak handle, or opens a new FILE * into the pak; the directory
branch probes the tree and hands back either a handle or a FILE *.  The
netcache gates are dropped (com_cachedir is always empty on DemonOS).
===========
*/
int COM_FindFile (char *filename, int *handle, FILE **file)
{
	searchpath_t    *search;
	char            netpath[MAX_OSPATH];
	pack_t          *pak;
	int                     i;
	int                     findtime;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");
	if (!file && !handle)
		Sys_Error ("COM_FindFile: neither handle or file set");

	search = com_searchpaths;
	if (proghack)
	{
		if (!strcmp(filename, "progs.dat"))
			search = search->next;
	}

	for ( ; search ; search = search->next)
	{
	// is the element a pak file?
		if (search->pack)
		{
		// look through all the pak file elements
			pak = search->pack;
			for (i=0 ; i<pak->numfiles ; i++)
				if (!strcmp (pak->files[i].name, filename))
				{       // found it!
					Sys_Printf ("PackFile: %s : %s\n",pak->filename, filename);
					if (handle)
					{
						*handle = pak->handle;
						Sys_FileSeek (pak->handle, pak->files[i].filepos);
					}
					else
					{       // open a new file on the pakfile
						*file = fopen (pak->filename, "rb");
						if (*file)
							fseek (*file, pak->files[i].filepos, SEEK_SET);
					}
					com_filesize = pak->files[i].filelen;
					return com_filesize;
				}
		}
		else
		{
	// check a file in the directory tree
			if (!static_registered)
			{       // if not a registered version, don't ever go beyond base
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
			}

			sprintf (netpath, "%s/%s",search->filename, filename);

			findtime = Sys_FileTime (netpath);
			if (findtime == -1)
				continue;

			Sys_Printf ("FindFile: %s\n",netpath);
			com_filesize = Sys_FileOpenRead (netpath, &i);
			if (handle)
				*handle = i;
			else
			{
				Sys_FileClose (i);
				*file = fopen (netpath, "rb");
			}
			return com_filesize;
		}
	}

	Sys_Printf ("FindFile: can't find %s\n", filename);

	if (handle)
		*handle = -1;
	else
		*file = NULL;
	com_filesize = -1;
	return -1;
}

/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
int COM_OpenFile (char *filename, int *handle)
{
	return COM_FindFile (filename, handle, NULL);
}

int COM_FOpenFile (char *filename, FILE **file)
{
	return COM_FindFile (filename, NULL, file);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile (int h)
{
	searchpath_t    *s;

	for (s = com_searchpaths ; s ; s=s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose (h);
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files. (upstream, faithful)
=================
*/
pack_t *COM_LoadPackFile (char *packfile)
{
	dpackheader_t   header;
	int                             i;
	packfile_t              *newfiles;
	int                             numpackfiles;
	pack_t                  *pack;
	int                             packhandle;
	dpackfile_t             *info;
	unsigned short          crc;

	if (Sys_FileOpenRead (packfile, &packhandle) == -1)
	{
//              Con_Printf ("Couldn't open %s\n", packfile);
		return NULL;
	}
	Sys_FileRead (packhandle, (void *)&header, sizeof(header));
	if (header.id[0] != 'P' || header.id[1] != 'A'
	|| header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);
	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	if (numpackfiles != PAK0_COUNT)
		com_modified = true;    // not the original file

	newfiles = Hunk_AllocName (numpackfiles * sizeof(packfile_t), "packfile");
	/* DemonOS user processes get a fixed 32 KiB stack and read-only code
	   pages, so the upstream on-stack MAX_FILES_IN_PACK directory buffer
	   (128 KiB) has nowhere valid to live; allocate it on the writable
	   hunk instead, sized to the pack's actual file count. */
	info = Hunk_TempAlloc (numpackfiles * sizeof(dpackfile_t));

	Sys_FileSeek (packhandle, header.dirofs);
	Sys_FileRead (packhandle, (void *)info, header.dirlen);

// crc the directory to check for modifications
	CRC_Init (&crc);
	for (i=0 ; i<header.dirlen ; i++)
		CRC_ProcessByte (&crc, ((byte *)info)[i]);
	if (crc != PAK0_CRC)
		com_modified = true;

// parse the directory
	for (i=0 ; i<numpackfiles ; i++)
	{
		strcpy (newfiles[i].name, info[i].name);
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = Hunk_Alloc (sizeof (pack_t));
	strcpy (pack->filename, packfile);
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	Con_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
This is faithful to upstream except the trailing parms.txt command-line
merge, which the D3 core leaves to the D5 bootstrap.
================
*/
void COM_AddGameDirectory (char *dir)
{
	int                             i;
	searchpath_t    *search;
	pack_t                  *pak;
	char                    pakfile[MAX_OSPATH];

	strcpy (com_gamedir, dir);

//
// add the directory to the search path
//
	search = Hunk_Alloc (sizeof(searchpath_t));
	strcpy (search->filename, dir);
	search->next = com_searchpaths;
	com_searchpaths = search;

//
// add any pak files in the format pak0.pak pak1.pak, ...
//
	for (i=0 ; ; i++)
	{
		sprintf (pakfile, "%s/pak%i.pak", dir, i);
		pak = COM_LoadPackFile (pakfile);
		if (!pak)
			break;
		search = Hunk_Alloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = com_searchpaths;
		com_searchpaths = search;
	}
}

/*
============
COM_FileBase

Extracts a base file name from a path (upstream common.c).
============
*/
void COM_FileBase (char *in, char *out)
{
	char *s, *s2;

	s = in + strlen(in) - 1;

	while (s != in && *s != '.')
		s--;

	for (s2 = s ; *s2 && *s2 != '/' ; s2--)
	;

	if (s-s2 < 2)
		strcpy (out,"?model?");
	else
	{
		s--;
		strncpy (out,s2+1, s-s2);
		out[s-s2] = 0;
	}
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
(upstream common.c, faithful).
============
*/
void COM_WriteFile (char *filename, void *data, int len)
{
	int             handle;
	char    name[MAX_OSPATH];

	sprintf (name, "%s/%s", com_gamedir, filename);

	handle = Sys_FileOpenWrite (name);
	if (handle == -1)
	{
		Sys_Printf ("COM_WriteFile: failed on %s\n", name);
		return;
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	Sys_FileWrite (handle, data, len);
	Sys_FileClose (handle);
}

/*
============
COM_LoadFile

Filename are relative to the quake directory. Allways appends a 0 byte.
Faithful port of upstream common.c COM_LoadFile covering all five usehunk
modes (0 Z_Malloc, 1 hunk, 2 hunk-temp, 3 cache, 4 stack). Draw_BeginDisc/
Draw_EndDisc around the read are dropped for the same reason the core
routes console output straight to the serial console.
============
*/
cache_user_t *loadcache;
byte    *loadbuf;
int             loadsize;

byte *COM_LoadFile (char *path, int usehunk)
{
	int             h;
	byte    *buf;
	char    base[32];
	int             len;

	buf = NULL;     // quiet compiler warning

// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h);
	if (h == -1)
		return NULL;

// extract the filename base name for hunk tag
	COM_FileBase (path, base);

	if (usehunk == 1)
		buf = Hunk_AllocName (len+1, base);
	else if (usehunk == 2)
		buf = Hunk_TempAlloc (len+1);
	else if (usehunk == 0)
		buf = Z_Malloc (len+1);
	else if (usehunk == 3)
		buf = Cache_Alloc (loadcache, len+1, base);
	else if (usehunk == 4)
	{
		if (len+1 > loadsize)
			buf = Hunk_TempAlloc (len+1);
		else
			buf = loadbuf;
	}
	else
		Sys_Error ("COM_LoadFile: bad usehunk");

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	Sys_FileRead (h, buf, len);
	COM_CloseFile (h);

	return buf;
}

byte *COM_LoadHunkFile (char *path)
{
	return COM_LoadFile (path, 1);
}

void COM_LoadCacheFile (char *path, struct cache_user_s *cu)
{
	loadcache = cu;
	(void)COM_LoadFile (path, 3);
}

byte *COM_LoadStackFile (char *path, void *buffer, int bufsize)
{
	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	return COM_LoadFile (path, 4);
}

/* MSG_WriteByte / MSG_WriteString are faithful ports of the upstream
   common.c helpers, needed by Cmd_ForwardToServer once Cmd_Init registers
   the real "cmd" command. */

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, Q_strlen(s)+1);
}

/* =============================================================================
   D4/D5: the rest of upstream common.c's engine-boot surface, ported
   faithfully.  These run in the full engine once Host_Init is up.
   ============================================================================= */

#define CMDLINE_LENGTH  256
#define NUM_SAFE_ARGVS  7

static char     *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static char     *argvdummy = " ";
static char     *safeargvs[NUM_SAFE_ARGVS] =
{
	"-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy",
	"-nomouse", "-dibonly"
};

char    com_cmdline[CMDLINE_LENGTH];
char    com_cachedir[MAX_OSPATH];

qboolean        standard_quake = true, rogue, hipnotic;

cvar_t  registered = {"registered", "0"};
cvar_t  cmdline = {"cmdline", "0", false, true};

int             msg_readcount;
qboolean        msg_badread;
qboolean        msg_suppress_1 = 0;

/*
============
COM_InitArgv

Reconstitute the command line for the cmdline cvar, then copy the
arguments.  -safe forces the fixed safe-mode switch set; -rogue/-hipnotic
flip the mission pack flags (upstream common.c, faithful).
============
*/
void COM_InitArgv (int argc, char **argv)
{
	qboolean        safe;
	int             i, j, n;

	n = 0;

	for (j=0 ; (j<MAX_NUM_ARGVS) && (j< argc) ; j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	com_cmdline[n] = 0;

	safe = false;

	for (com_argc=0 ; (com_argc<MAX_NUM_ARGVS) && (com_argc < argc) ;
		 com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!Q_strcmp ("-safe", argv[com_argc]))
			safe = true;
	}

	if (safe)
	{
		for (i=0 ; i<NUM_SAFE_ARGVS ; i++)
		{
			largv[com_argc] = safeargvs[i];
			com_argc++;
		}
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic"))
	{
		hipnotic = true;
		standard_quake = false;
	}
}

/*
============
COM_Path_f
============
*/
void COM_Path_f (void)
{
	searchpath_t    *s;

	Con_Printf ("Current search path:\n");
	for (s=com_searchpaths ; s ; s=s->next)
	{
		if (s->pack)
		{
			Con_Printf ("%s (%i files)\n", s->pack->filename,
				    s->pack->numfiles);
		}
		else
			Con_Printf ("%s\n", s->filename);
	}
}

/* this graphic needs to be in the pak file to use registered features */
unsigned short pop[] =
{
 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
,0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000
,0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000
,0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600
,0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563
,0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564
,0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564
,0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563
,0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500
,0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200
,0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000
,0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};

/*
============
COM_CheckRegistered

The shareware/registered gate reads gfx/pop.lmp and compares it against
the built-in splash (upstream common.c, faithful).
============
*/
void COM_CheckRegistered (void)
{
	int             h;
	unsigned short  check[128];
	int                     i;

	COM_OpenFile ("gfx/pop.lmp", &h);
	static_registered = 0;

	if (h == -1)
	{
		Con_Printf ("Playing shareware version.\n");
		if (com_modified)
			Sys_Error ("You must have the registered version to use modified games");
		return;
	}

	Sys_FileRead (h, check, sizeof(check));
	COM_CloseFile (h);

	for (i=0 ; i<128 ; i++)
		if (pop[i] != (unsigned short)BigShort (check[i]))
			Sys_Error ("Corrupted data file.");

	Cvar_Set ("cmdline", com_cmdline);
	Cvar_Set ("registered", "1");
	static_registered = 1;
	Con_Printf ("Playing registered version.\n");
}

/*
============
COM_InitFilesystem

Builds the search path from -basedir/-cachedir/-game/-path, adding the
GAMENAME (id1) directory first (upstream common.c, faithful).
============
*/
void COM_InitFilesystem (void)
{
	int             i, j;
	char    basedir[MAX_OSPATH];
	searchpath_t    *search;

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
		strcpy (basedir, com_argv[i+1]);
	else
		strcpy (basedir, host_parms.basedir);

	j = strlen (basedir);

	if (j > 0)
	{
		if ((basedir[j-1] == '\\') || (basedir[j-1] == '/'))
			basedir[j-1] = 0;
	}

	i = COM_CheckParm ("-cachedir");
	if (i && i < com_argc-1)
	{
		if (com_argv[i+1][0] == '-')
			com_cachedir[0] = 0;
		else
			strcpy (com_cachedir, com_argv[i+1]);
	}
	else if (host_parms.cachedir)
		strcpy (com_cachedir, host_parms.cachedir);
	else
		com_cachedir[0] = 0;

	COM_AddGameDirectory (va("%s/"GAMENAME, basedir) );

	if (COM_CheckParm ("-rogue"))
		COM_AddGameDirectory (va("%s/rogue", basedir) );
	if (COM_CheckParm ("-hipnotic"))
		COM_AddGameDirectory (va("%s/hipnotic", basedir) );

	i = COM_CheckParm ("-game");
	if (i && i < com_argc-1)
	{
		com_modified = true;
		COM_AddGameDirectory (va("%s/%s", basedir, com_argv[i+1]));
	}

	i = COM_CheckParm ("-path");
	if (i)
	{
		com_modified = true;
		com_searchpaths = NULL;
		while (++i < com_argc)
		{
			if (!com_argv[i] || com_argv[i][0] == '+' ||
			    com_argv[i][0] == '-')
				break;

			search = Hunk_Alloc (sizeof(searchpath_t));
			if (!strcmp (COM_FileExtension (com_argv[i]), "pak"))
			{
				search->pack = COM_LoadPackFile (com_argv[i]);
				if (!search->pack)
					Sys_Error ("Couldn't load packfile: %s",
						   com_argv[i]);
			}
			else
				strcpy (search->filename, com_argv[i]);
			search->next = com_searchpaths;
			com_searchpaths = search;
		}
	}

	if (COM_CheckParm ("-proghack"))
		proghack = true;
}

/*
============
COM_Init

Upstream common.c COM_Init: byte-order setup, the registered/cmdline
cvars, the "path" command, then the filesystem and the registered check.
============
*/
void COM_Init (char *basedir)
{
	COM_ByteSwapInit ();

	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);

	COM_InitFilesystem ();
	COM_CheckRegistered ();
}

/* COM_FileExtension / COM_DefaultExtension (upstream common.c, faithful). */
char *COM_FileExtension (char *in)
{
	static char exten[8];
	int             i;

	while (*in && *in != '.')
		in++;
	if (!*in)
		return "";
	in++;
	for (i=0 ; i<7 && *in ; i++,in++)
		exten[i] = *in;
	exten[i] = 0;
	return exten;
}

void COM_DefaultExtension (char *path, char *extension)
{
	char    *src;

	src = path + strlen(path) - 1;

	while (*src != '/' && src != path)
	{
		if (*src == '.')
			return;
		src--;
	}

	strcat (path, extension);
}

/* va() into a static 1024-byte buffer (upstream common.c, faithful). */
char    *va(char *format, ...)
{
	va_list         argptr;
	static char             string[1024];

	va_start (argptr, format);
	vsprintf (string, format,argptr);
	va_end (argptr);

	return string;
}

/* The link helpers (upstream common.c, faithful). */
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/* =============================================================================
   MSG read/write family (upstream common.c, faithful).
   ============================================================================= */

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

int MSG_ReadChar (void)
{
	int     c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int     c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int     c;

	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int     c;

	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount+1]<<8)
	+ (net_message.data[msg_readcount+2]<<16)
	+ (net_message.data[msg_readcount+3]<<24);

	msg_readcount += 4;

	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte    b[4];
		float   f;
		int     l;
	} dat;

	dat.b[0] =      net_message.data[msg_readcount];
	dat.b[1] =      net_message.data[msg_readcount+1];
	dat.b[2] =      net_message.data[msg_readcount+2];
	dat.b[3] =      net_message.data[msg_readcount+3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

float MSG_ReadCoord (void)
{
	return MSG_ReadShort() * (1.0/8);
}

float MSG_ReadAngle (void)
{
	return MSG_ReadChar() * (360.0/256);
}

char *MSG_ReadString (void)
{
	static char     string[2048];
	int             l,c;

	l = 0;
	do
	{
		c = MSG_ReadChar ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string)-1);

	string[l] = 0;

	return string;
}

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte    *buf;

	buf = SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float   f;
		int     l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteCoord (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, (int)(f*8));
}

void MSG_WriteAngle (sizebuf_t *sb, float f)
{
	MSG_WriteByte (sb, ((int)f*256/360) & 255);
}

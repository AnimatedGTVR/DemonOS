/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

stdio_demonos.c -- freestanding FILE shim for the Quake port.

WinQuake's savegame/exec/demo code (host_cmd.c, menu.c, cl_demo.c) uses the
C stream API.  There is no libc FILE, so streams are small structs that
carry their own PortKit port plus a base/limit window.  The base/limit
window is what lets a FILE sit over a single entry inside a PAK file (open
the whole pack, then fseek to the entry's filepos with a bounded length).

Only the stream API the engine's reachable code actually calls is
implemented: fopen/fclose/fread/fwrite/fgetc/getc/feof/fflush/fseek,
fprintf, fscanf, plus the Con_DebugLog fd trio (open/write/close) and
unlink.
*/

#include <demon/portkit.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* glibc's <fcntl.h>/<unistd.h> baked these values into the engine objects
   at compile time; the calls arrive with the host constants. */
#define DEMON_O_WRONLY  1
#define DEMON_O_RDWR    2
#define DEMON_O_CREAT   64
#define DEMON_O_APPEND  1024

#define DEMON_EOF       (-1)

/* Write-mode streams buffer their whole content in memory and only touch
   storage once, in fclose(). This isn't a stylistic choice: ramfs_write()
   (src/ramfs.cpp) takes no offset at all -- every call replaces a file's
   *entire* content with just that call's buffer, full stop. A seek-then-
   write sequence (NXEngine's profile.cpp writes a save file as ~20 small
   sequential/non-sequential fputl/fputi/fseek calls, not one big write)
   would otherwise silently end up with only the bytes from whichever
   fwrite() call happened to run last -- everything written before it
   gets clobbered. DEMON_WRITE_BUF_SIZE just needs to cover the largest
   file any port using this shim writes in one open/close (NXEngine's
   PROFILE_LENGTH is 0x604; Quake's config.cfg/savegames are a few KB of
   text); 4KB per slot covers that with margin. This lives in every
   port's static BSS (16 slots), so it isn't free -- keep it as small as
   the real callers need, not "generous," since large executables here
   have a hard 1MB total image budget (USER_LARGE_CODE_MAX_PAGES). */
#define DEMON_WRITE_BUF_SIZE 4096u

typedef struct
{
	struct demon_port_file port;
	long base;		/* absolute offset where this stream starts */
	long pos;		/* position relative to base */
	long limit;		/* bytes readable from base, -1 if unbounded */
	int eof;
	int writing;
	int open;
	uint8_t *write_buf;	/* whole in-progress file content, writing != 0 only */
	size_t write_len;	/* high-water mark of bytes actually written */
} demonos_FILE;

#define DEMON_MAX_FILES 16

static demonos_FILE d_files[DEMON_MAX_FILES];
static uint8_t d_write_bufs[DEMON_MAX_FILES][DEMON_WRITE_BUF_SIZE];

FILE *fopen (const char *path, const char *mode)
{
	demonos_FILE *f;
	unsigned i;

	if (path == NULL || mode == NULL)
		return NULL;

	for (i = 0; i < DEMON_MAX_FILES; ++i)
		if (!d_files[i].open)
			break;
	if (i == DEMON_MAX_FILES)
		return NULL;
	f = &d_files[i];

	if (mode[0] == 'w')
	{
		if (!demon_port_create (&f->port, path))
			return NULL;
		f->writing = 1;
		f->base = 0;
		f->pos = 0;
		f->limit = -1;
		f->write_buf = d_write_bufs[i];
		f->write_len = 0;
	}
	else if (mode[0] == 'a')
	{
		size_t existing = 0;

		if (!demon_port_open (&f->port, path))
		{
			if (!demon_port_create (&f->port, path))
				return NULL;
		}
		else
		{
			existing = f->port.size;
			if (existing > DEMON_WRITE_BUF_SIZE)
				existing = DEMON_WRITE_BUF_SIZE;
			(void)demon_port_seek (&f->port, 0);
			existing = demon_port_read (&f->port, d_write_bufs[i], existing);
		}
		f->writing = 1;
		f->base = 0;
		f->pos = (long)existing;
		f->limit = -1;
		f->write_buf = d_write_bufs[i];
		f->write_len = existing;
	}
	else
	{
		if (!demon_port_open (&f->port, path))
			return NULL;
		f->writing = 0;
		f->base = 0;
		f->pos = 0;
		f->limit = -1;
	}

	f->eof = 0;
	f->open = 1;
	return (FILE *)f;
}

int fclose (FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;

	if (f == NULL || !f->open)
		return DEMON_EOF;
	if (f->writing && f->write_buf != NULL)
		(void)demon_port_write_file (&f->port, f->write_buf, f->write_len);
	demon_port_close (&f->port);
	f->open = 0;
	f->write_buf = NULL;
	return 0;
}

size_t fread (void *ptr, size_t size, size_t nmemb, FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	size_t want, avail, got;

	if (ptr == NULL || f == NULL || !f->open || f->writing || size == 0)
		return 0;
	want = size * nmemb;
	if (f->limit >= 0)
	{
		avail = (long)(f->limit - f->pos);
		if (avail <= 0)
		{
			f->eof = 1;
			return 0;
		}
		if (want > (size_t)avail)
			want = (size_t)avail;
	}
	if (want == 0)
		return 0;
	(void)demon_port_seek (&f->port, (size_t)(f->base + f->pos));
	got = demon_port_read (&f->port, ptr, want);
	f->pos += (long)got;
	if (got < want)
		f->eof = 1;
	return got / size;
}

/* Writes land in this stream's in-memory buffer (d_write_bufs), not
   storage directly -- see the comment on DEMON_WRITE_BUF_SIZE above.
   fseek() + fwrite() therefore behaves like a normal seekable stream
   (this is where NXEngine's profile.cpp save-file writer, which seeks
   forward to fixed offsets and writes fields out of order, actually
   gets to work correctly); the real, single ramfs_write() call happens
   once, in fclose(). */
size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	size_t want, i, offset;

	if (ptr == NULL || f == NULL || !f->open || !f->writing || size == 0)
		return 0;
	want = size * nmemb;
	offset = (size_t)(f->base + f->pos);
	if (f->write_buf == NULL || offset > DEMON_WRITE_BUF_SIZE)
		return 0;
	if (offset + want > DEMON_WRITE_BUF_SIZE)
		want = DEMON_WRITE_BUF_SIZE - offset;
	for (i = 0; i < want; ++i)
		f->write_buf[offset + i] = ((const uint8_t *)ptr)[i];
	f->pos += (long)want;
	if ((size_t)f->pos > f->write_len)
		f->write_len = (size_t)f->pos;
	return want / size;
}

int fgetc (FILE *fp)
{
	unsigned char c;

	if (fread (&c, 1, 1, fp) != 1)
		return DEMON_EOF;
	return (int)c;
}

int getc (FILE *fp)
{
	return fgetc (fp);
}

/* Added for common/misc_comm.cpp's fbooleanwrite/fbooleanflush (NXEngine
   port's profile.cpp save-file round trip), the first caller of fputc
   across every port sharing this file. */
int fputc (int ch, FILE *fp)
{
	unsigned char c = (unsigned char)ch;

	if (fwrite (&c, 1, 1, fp) != 1)
		return DEMON_EOF;
	return (int)c;
}

int feof (FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	return f != NULL && f->eof;
}

/* This shim only ever fails reads via running past base+limit (the same
   path that sets ->eof); there's no separate I/O-error condition to
   track, so ferror folding into the same flag as feof is an honest
   reflection of this shim's actual failure modes, not a stand-in for a
   real distinct error state. Added for common/misc_comm.cpp (NXEngine
   port), the first caller of ferror() across every port sharing this
   file. */
int ferror (FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	return f != NULL && f->eof;
}

int fflush (FILE *fp)
{
	(void)fp;
	return 0;
}

int fseek (FILE *fp, long offset, int whence)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	long size;

	if (f == NULL || !f->open)
		return DEMON_EOF;

	switch (whence)
	{
	case 0: /* SEEK_SET */
		f->pos = offset;
		break;
	case 1: /* SEEK_CUR */
		f->pos += offset;
		break;
	case 2: /* SEEK_END */
		/* f->port.size is the real on-disk file size (set once by
		   demon_port_open/create and kept current by writes); using
		   demon_port_tell (the *current cursor position*, still 0 right
		   after a fresh open) here instead used to make ftell()-after-
		   SEEK_END always report 0 on any file whose position hadn't
		   already been advanced -- exactly the fseek(fp,0,SEEK_END);
		   ftell(fp); idiom endgame/CredReader.cpp's tsc_decrypt (D19)
		   uses to find a file's length, which needs the real size, not
		   the current position. */
		if (f->limit >= 0)
			size = f->limit;
		else
			size = (long)f->port.size;
		f->pos = size + offset;
		break;
	default:
		return DEMON_EOF;
	}
	f->eof = 0;
	return 0;
}

/* Added for tsc_decrypt's fseek(fp,0,SEEK_END); ftell(fp); idiom (D19,
   the first caller of ftell across every port sharing this file). */
long ftell (FILE *fp)
{
	demonos_FILE *f = (demonos_FILE *)fp;

	if (f == NULL || !f->open)
		return -1L;
	return f->base + f->pos;
}

/* Bounded-window helper used by COM_FindFile when a FILE sits inside a PAK:
   only 'limit' more bytes are readable from the current base. */
void d_fopen_limit (FILE *fp, long limit)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	if (f != NULL && f->open)
		f->limit = limit;
}

int fprintf (FILE *fp, const char *format, ...)
{
	char buf[512];
	va_list args;
	int n;

	if (format == NULL)
		return 0;
	va_start (args, format);
	n = vsnprintf (buf, sizeof(buf), format, args);
	va_end (args);
	if (n < 0)
		return 0;
	if (n > (int)sizeof(buf))
		n = (int)sizeof(buf);
	if (fwrite (buf, 1, (size_t)n, fp) != (size_t)n)
		return DEMON_EOF;
	return n;
}

static int f_skip_ws (demonos_FILE *f)
{
	int c;

	do
	{
		c = fgetc ((FILE *)f);
		if (c == DEMON_EOF)
			return DEMON_EOF;
	} while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

	return c;
}

static void f_ungetc (demonos_FILE *f)
{
	if (f->pos > f->base)
		f->pos--;
	f->eof = 0;
}

/* Compact fscanf covering the subset the engine uses: %i/%d (int *),
   %s with an optional width (char *), %f (float *), plus literal format
   text and whitespace (each whitespace run skips all input whitespace).
   On short input returns EOF; otherwise the conversion count. */
static int fscanf_impl (FILE *fp, const char *format, va_list args)
{
	demonos_FILE *f = (demonos_FILE *)fp;
	const char *fmt = format;
	char *argp;
	int assignments = 0;
	int c;

	if (f == NULL || format == NULL)
		return DEMON_EOF;

	while (*fmt)
	{
		if (*fmt == '%')
		{
			int width = 0;
			unsigned n = 0;

			fmt++;
			if (*fmt == '*')
			{
				fmt++;
				argp = NULL;
			}
			else
				argp = va_arg (args, char *);

			while (*fmt >= '0' && *fmt <= '9')
			{
				width = width * 10 + (*fmt - '0');
				fmt++;
			}

			c = f_skip_ws (f);
			if (c == DEMON_EOF)
				return DEMON_EOF;
			f_ungetc (f);

			switch (*fmt)
			{
			case 'i':
			case 'd':
			{
				int sign = 1, v = 0;
				char buf[64];

				if (c == '-' || c == '+')
				{
					if (c == '-') sign = -1;
					c = fgetc ((FILE *)f);
					if (c == DEMON_EOF)
						return DEMON_EOF;
					buf[n++] = (char)c;
				}
				while (n < sizeof(buf) - 1)
				{
					c = fgetc ((FILE *)f);
					if (c == DEMON_EOF)
						break;
					if (c < '0' || c > '9')
					{
						f_ungetc (f);
						break;
					}
					buf[n++] = (char)c;
				}
				buf[n] = 0;
				for (n = 0; buf[n] >= '0' && buf[n] <= '9'; n++)
					v = v * 10 + (buf[n] - '0');
				if (argp != NULL)
					*(int *)argp = sign * v;
				if (argp != NULL)
					assignments++;
				break;
			}
			case 'f':
			{
				int sign = 1, seen = 0;
				double v = 0.0, frac = 0.1;
				char buf[80];

				if (c == '-' || c == '+')
				{
					if (c == '-') sign = -1;
					c = fgetc ((FILE *)f);
					if (c == DEMON_EOF)
						return DEMON_EOF;
					buf[n++] = (char)c;
				}
				while (n < sizeof(buf) - 1)
				{
					c = fgetc ((FILE *)f);
					if (c == DEMON_EOF)
						break;
					if ((c < '0' || c > '9') && c != '.')
					{
						f_ungetc (f);
						break;
					}
					buf[n++] = (char)c;
					if (c == '.') seen = 1;
				}
				buf[n] = 0;
				for (n = 0; buf[n]; n++)
				{
					if (buf[n] == '.')
						continue;
					if (buf[n] == '-' || buf[n] == '+')
						continue;
					if (seen)
					{
						v += (buf[n] - '0') * frac;
						frac *= 0.1;
					}
					else
						v = v * 10.0 + (buf[n] - '0');
				}
				if (argp != NULL)
					*(float *)argp = (float)(sign * v);
				if (argp != NULL)
					assignments++;
				break;
			}
			case 's':
			{
				int out = 0;
				if (width <= 0)
					width = 512;
				while (out < width - 1)
				{
					c = fgetc ((FILE *)f);
					if (c == DEMON_EOF)
						break;
					if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
					{
						f_ungetc (f);
						break;
					}
					if (argp != NULL)
						((char *)argp)[out] = (char)c;
					out++;
				}
				if (argp != NULL)
				{
					((char *)argp)[out] = 0;
					assignments++;
				}
				break;
			}
			default:
				va_end (args);
				return assignments;
			}
			fmt++;
		}
		else if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n' ||
			 *fmt == '\r')
		{
			while (*fmt == ' ' || *fmt == '\t' || *fmt == '\n' ||
			       *fmt == '\r')
				fmt++;
			c = f_skip_ws (f);
			if (c == DEMON_EOF)
				return assignments;
			f_ungetc (f);
		}
		else
		{
			c = fgetc ((FILE *)f);
			if (c == DEMON_EOF || c != (unsigned char)*fmt)
				return assignments;
			fmt++;
		}
	}
	return assignments;
}

int fscanf (FILE *fp, const char *format, ...)
{
	va_list args;
	int r;

	va_start (args, format);
	r = fscanf_impl (fp, format, args);
	va_end (args);
	return r;
}

/* ---------------------------------------------------------------------
   Con_DebugLog fd trio (open/write/close) and unlink.
   --------------------------------------------------------------------- */

#define DEMON_MAX_FDS 8

static demonos_FILE d_fds[DEMON_MAX_FDS];

int open (const char *pathname, int flags, ...)
{
	int i;

	if (pathname == NULL)
		return -1;
	for (i = 0; i < DEMON_MAX_FDS; ++i)
		if (!d_fds[i].open)
			break;
	if (i == DEMON_MAX_FDS)
		return -1;
	d_fds[i].open = 0;
	d_fds[i].writing = 0;
	d_fds[i].eof = 0;
	d_fds[i].base = 0;
	d_fds[i].pos = 0;
	d_fds[i].limit = -1;

	if ((flags & DEMON_O_CREAT) &&
	    !demon_port_create (&d_fds[i].port, pathname))
		return -1;
	if (!(flags & DEMON_O_CREAT) &&
	    !demon_port_open (&d_fds[i].port, pathname))
		return -1;
	if (flags & DEMON_O_APPEND)
		(void)demon_port_seek (&d_fds[i].port, demon_port_tell (&d_fds[i].port));

	d_fds[i].writing = (flags & DEMON_O_WRONLY) || (flags & DEMON_O_RDWR);
	d_fds[i].open = 1;
	return i;
}

int write (int fd, const void *buf, size_t count)
{
	demonos_FILE *f;

	if (fd < 0 || (unsigned)fd >= DEMON_MAX_FDS || !d_fds[fd].open)
		return -1;
	f = &d_fds[fd];
	return (int)demon_port_write_file (&f->port, buf, count);
}

int close (int fd)
{
	if (fd < 0 || (unsigned)fd >= DEMON_MAX_FDS || !d_fds[fd].open)
		return -1;
	demon_port_close (&d_fds[fd].port);
	d_fds[fd].open = 0;
	return 0;
}

int unlink (const char *pathname)
{
	if (pathname == NULL)
		return -1;
	return demon_port_delete (pathname) ? 0 : -1;
}

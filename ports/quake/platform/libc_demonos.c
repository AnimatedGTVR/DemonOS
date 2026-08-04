/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

libc_demonos.c -- freestanding libc additions for the Quake port.

The shared apps/doom/libc.c provides vsnprintf/snprintf and friends;
this file adds sprintf, which WinQuake calls directly.
*/

#include "libc.h"

#include <demon/portkit.h>
#include <stddef.h>

extern void Sys_Printf (char *fmt, ...);

int sprintf(char *dest, const char *format, ...)
{
	va_list args;
	int n;

	va_start(args, format);
	n = vsnprintf(dest, (size_t)-1, format, args);
	va_end(args);
	return n;
}

int vsprintf(char *dest, const char *format, va_list ap)
{
	return vsnprintf(dest, (size_t)-1, format, ap);
}

int printf(const char *format, ...)
{
	char buf[1024];
	va_list args;
	int n;

	va_start(args, format);
	n = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	if (n < 0)
		return n;
	if (n > (int)sizeof(buf))
		n = (int)sizeof(buf);
	Sys_Printf("%s", buf);
	return n;
}

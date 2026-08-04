/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
/* demonos_common.h -- shared D3 packfile/searchpath contract between
   com_demonos.c (definitions) and core_main.c (the D3 self-check).

   Requires quakedef.h to be included first: pack_t/searchpath_t and the
   byte-swap globals are internal to upstream common.c, so they have no
   quakedef.h declaration to reuse, and quakedef.h itself has no include
   guard. */

#ifndef DEMONOS_QUAKE_COMMON_H
#define DEMONOS_QUAKE_COMMON_H

typedef struct
{
	char    name[MAX_QPATH];
	int             filepos, filelen;
} packfile_t;

typedef struct pack_s
{
	char    filename[MAX_OSPATH];
	int             handle;
	int             numfiles;
	packfile_t      *files;
} pack_t;

typedef struct searchpath_s
{
	char    filename[MAX_OSPATH];
	pack_t  *pack;          // only one of filename / pack will be used
	struct searchpath_s *next;
} searchpath_t;

extern void COM_ByteSwapInit (void);
extern pack_t *COM_LoadPackFile (char *packfile);
extern void COM_AddGameDirectory (char *dir);
extern int COM_FindFile (char *filename, int *handle, FILE **file);
extern int COM_OpenFile (char *filename, int *handle);
extern void COM_CloseFile (int h);

extern qboolean com_modified;
extern qboolean proghack;
extern searchpath_t *com_searchpaths;
extern int com_filesize;

/* D4/D5 boot surface: the upstream common.c globals the engine units share.
   host_parms / cls / sv / Con_Printf / SV_BroadcastPrintf come from host.c,
   cl_main.c, sv_main.c and console.c respectively once the full engine links. */
extern qboolean bigendien;
extern short (*BigShort) (short l);
extern short (*LittleShort) (short l);
extern int (*BigLong) (int l);
extern int (*LittleLong) (int l);
extern float (*BigFloat) (float l);
extern float (*LittleFloat) (float l);
extern qboolean standard_quake, rogue, hipnotic;
extern cvar_t registered;
extern cvar_t cmdline;
extern int msg_readcount;
extern qboolean msg_badread;
extern qboolean msg_suppress_1;
extern char com_cmdline[256];
extern char com_cachedir[MAX_OSPATH];
extern cache_user_t *loadcache;
extern byte *loadbuf;
extern int loadsize;

void COM_InitArgv (int argc, char **argv);
void COM_Init (char *basedir);
void COM_CheckRegistered (void);
char *COM_FileExtension (char *in);
void COM_DefaultExtension (char *path, char *extension);
char *va (char *format, ...);
void COM_LoadCacheFile (char *path, struct cache_user_s *cu);
byte *COM_LoadStackFile (char *path, void *buffer, int bufsize);
int COM_FOpenFile (char *filename, FILE **file);
void MSG_BeginReading (void);
int MSG_ReadChar (void);
int MSG_ReadByte (void);
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);
float MSG_ReadCoord (void);
float MSG_ReadAngle (void);
void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteCoord (sizebuf_t *sb, float f);
void MSG_WriteAngle (sizebuf_t *sb, float f);
void ClearLink (link_t *l);
void RemoveLink (link_t *l);
void InsertLinkBefore (link_t *l, link_t *before);
void InsertLinkAfter (link_t *l, link_t *after);

#endif

/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

net_demonos.c -- networking tables and shims for the DemonOS Quake port.

Upstream net_bsd.c defines net_drivers (Loopback + Datagram) and
net_landrivers (UDP).  Both are needed at link time, but DemonOS has no
sockets: the driver tables are copied verbatim (the Loopback/Datagram
entries live in engine objects), while the UDP landriver is provided as a
single disabled entry whose Init() returns -1 so Datagram_Init simply skips
it.  inet_addr/inet_ntoa are taken from mplpc.c; banAddr/banMask come from
the BAN_TEST code in net_dgrm.c.
*/

#include "quakedef.h"
#include "net_loop.h"
#include "net_dgrm.h"
#include "net_udp.h"

#include <stdint.h>
#include <stdio.h>

qboolean	isDedicated = false;

net_driver_t net_drivers[MAX_NET_DRIVERS] =
{
	{
	"Loopback",
	false,
	Loop_Init,
	Loop_Listen,
	Loop_SearchForHosts,
	Loop_Connect,
	Loop_CheckNewConnections,
	Loop_GetMessage,
	Loop_SendMessage,
	Loop_SendUnreliableMessage,
	Loop_CanSendMessage,
	Loop_CanSendUnreliableMessage,
	Loop_Close,
	Loop_Shutdown
	}
	,
	{
	"Datagram",
	false,
	Datagram_Init,
	Datagram_Listen,
	Datagram_SearchForHosts,
	Datagram_Connect,
	Datagram_CheckNewConnections,
	Datagram_GetMessage,
	Datagram_SendMessage,
	Datagram_SendUnreliableMessage,
	Datagram_CanSendMessage,
	Datagram_CanSendUnreliableMessage,
	Datagram_Close,
	Datagram_Shutdown
	}
};

int net_numdrivers = 2;

/* ---------------------------------------------------------------------
   Disabled UDP landriver.  Datagram_Init skips drivers whose Init()
   returns -1, so this yields a Loopback-only network stack.
   --------------------------------------------------------------------- */

int UDP_Init (void)			{ return -1; }
void UDP_Shutdown (void)	{ }
void UDP_Listen (qboolean state)	{ (void)state; }
int UDP_OpenSocket (int port)	{ (void)port; return -1; }
int UDP_CloseSocket (int socket)	{ (void)socket; return -1; }
int UDP_Connect (int socket, struct qsockaddr *addr)
{
	(void)socket; (void)addr; return -1;
}
int UDP_CheckNewConnections (void)	{ return -1; }
int UDP_Read (int socket, byte *buf, int len, struct qsockaddr *addr)
{
	(void)socket; (void)buf; (void)len; (void)addr; return -1;
}
int UDP_Write (int socket, byte *buf, int len, struct qsockaddr *addr)
{
	(void)socket; (void)buf; (void)len; (void)addr; return -1;
}
int UDP_Broadcast (int socket, byte *buf, int len)
{
	(void)socket; (void)buf; (void)len; return -1;
}
char *UDP_AddrToString (struct qsockaddr *addr)
{
	(void)addr; return NULL;
}
int UDP_StringToAddr (char *string, struct qsockaddr *addr)
{
	(void)string; (void)addr; return -1;
}
int UDP_GetSocketAddr (int socket, struct qsockaddr *addr)
{
	(void)socket; (void)addr; return -1;
}
int UDP_GetNameFromAddr (struct qsockaddr *addr, char *name)
{
	(void)addr; (void)name; return -1;
}
int UDP_GetAddrFromName (char *name, struct qsockaddr *addr)
{
	(void)name; (void)addr; return -1;
}
int UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2)
{
	(void)addr1; (void)addr2; return -1;
}
int UDP_GetSocketPort (struct qsockaddr *addr)
{
	(void)addr; return -1;
}
int UDP_SetSocketPort (struct qsockaddr *addr, int port)
{
	(void)addr; (void)port; return -1;
}

net_landriver_t	net_landrivers[MAX_NET_DRIVERS] =
{
	{
	"UDP",
	false,
	0,
	UDP_Init,
	UDP_Shutdown,
	UDP_Listen,
	UDP_OpenSocket,
	UDP_CloseSocket,
	UDP_Connect,
	UDP_CheckNewConnections,
	UDP_Read,
	UDP_Write,
	UDP_Broadcast,
	UDP_AddrToString,
	UDP_StringToAddr,
	UDP_GetSocketAddr,
	UDP_GetNameFromAddr,
	UDP_GetAddrFromName,
	UDP_AddrCompare,
	UDP_GetSocketPort,
	UDP_SetSocketPort
	}
};

int net_numlandrivers = 1;

/* ---------------------------------------------------------------------
   inet_addr/inet_ntoa (from mplpc.c).  struct in_addr is passed by value
   as a 4-byte quantity, matching what net_dgrm.c compiled against.
   --------------------------------------------------------------------- */

struct in_addr
{
	uint32_t s_addr;
};

unsigned long inet_addr(const char *cp)
{
	unsigned int ha[4];
	int i = 0, v = 0;
	int hasdigit = 0;

	if (cp == NULL)
		return (unsigned long)-1;
	for (;;)
	{
		if (*cp >= '0' && *cp <= '9')
		{
			hasdigit = 1;
			v = v * 10 + (*cp - '0');
			if (v > 255)
				return (unsigned long)-1;
			cp++;
		}
		else if (*cp == '.' || *cp == 0)
		{
			if (!hasdigit || i >= 4)
				return (unsigned long)-1;
			ha[i++] = v;
			v = 0;
			hasdigit = 0;
			if (*cp == 0)
				break;
			cp++;
		}
		else
			return (unsigned long)-1;
	}
	if (i != 4)
		return (unsigned long)-1;

	return ((unsigned long)ha[0] << 24) |
	       ((unsigned long)ha[1] << 16) |
	       ((unsigned long)ha[2] << 8) | ha[3];
}

char *inet_ntoa (struct in_addr in)
{
	static char buf [32];
	unsigned char *b = (unsigned char *)&in.s_addr;

	sprintf(buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return buf;
}

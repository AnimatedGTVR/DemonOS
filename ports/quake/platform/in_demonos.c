/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

in_demonos.c -- Quake input driver for DemonOS.

Keyboard events reach the engine through Key_Event exactly as on any
platform: sys_demonos.c's Sys_SendKeyEvents translates unified input
events and calls Key_Event directly, and keys.c drives the kbutton_t
movement state through the "+forward"/"-forward" command stream.  This
file owns the mouse: it drains accumulated relative deltas and ports the
upstream in_win.c IN_MouseMove look math (sensitivity, m_* scaling, and
in_mlook/in_strafe handling) onto the unified MouseMove event deltas.
*/

#include "quakedef.h"

#include <demon/input.h>
#include <demon/portkit.h>

#include <stdint.h>

static cvar_t	m_filter = {"m_filter", "0"};
static cvar_t	in_mouse = {"in_mouse", "1"};

/* Relative mouse deltas accumulated by Sys_SendKeyEvents between frames,
   scaled by the engine's sensitivity in IN_Move like the vanilla driver. */
static int	mx_accum;
static int	my_accum;
static int	old_mouse_x;
static int	old_mouse_y;

float		mouse_x, mouse_y;
int		mouse_oldbuttonstate;

/*
===============
IN_Commands

The vanilla driver only handles this for the joystick advanced-update
command; DemonOS has no joystick, so nothing to do.
===============
*/
void IN_Commands (void)
{
}

/*
===============
IN_AccumulateMouse

Called from Sys_SendKeyEvents for every INPUT_MOUSE_MOVE event.
===============
*/
void IN_AccumulateMouse (int delta_x, int delta_y)
{
	mx_accum += delta_x;
	my_accum += delta_y;
}

/*
===============
IN_MouseMove

Port of the upstream in_win.c look math.  DemonOS relative deltas replace
the DirectInput reads; the resulting command/viewangle math is identical.
===============
*/
static void IN_MouseMove (usercmd_t *cmd)
{
	int	mx, my;

	if (!in_mouse.value)
		return;

	mx = mx_accum;
	my = my_accum;
	mx_accum = 0;
	my_accum = 0;

	if (m_filter.value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}

	old_mouse_x = mx;
	old_mouse_y = my;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

	/* add mouse X/Y movement to cmd */
	if ((in_strafe.state & 1) ||
	    (lookstrafe.value && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
}

/*
===============
IN_Move
===============
*/
void IN_Move (usercmd_t *cmd)
{
	IN_MouseMove (cmd);
}

/*
===============
IN_ClearStates
===============
*/
void IN_ClearStates (void)
{
	mx_accum = 0;
	my_accum = 0;
	mouse_oldbuttonstate = 0;
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown (void)
{
}

/*
===============
IN_Init
===============
*/
void IN_Init (void)
{
	Cvar_RegisterVariable (&m_filter);
	Cvar_RegisterVariable (&in_mouse);
	mouse_oldbuttonstate = 0;
}

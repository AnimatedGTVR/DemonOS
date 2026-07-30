//
// X11_Xkb.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_X11_XKB_HH_
#define _PEKWM_X11_XKB_HH_

#include "config.h"

#include "Compat.hh"
#include "X11.hh"

extern "C" {
#ifdef PEKWM_HAVE_X11_XKBLIB_H
#include <X11/XKBlib.h>
#endif // PEKWM_HAVE_X11_XKBLIB_H
}

bool X11_Xkb_IsExtensionAvailable();
bool X11_Xkb_SelectInput();
bool X11_Xkb_KeycodeToKeysym(KeyCode keycode, KeySym &keysym);
bool X11_Xkb_GetIndicatorState(uint &state, uint &locked_state);
bool X11_Xkb_GetLayout(std::string &layout);

bool X11_Xkb_HandleStateNotify(XEvent *ev, uint &state, uint &locked_state);

#endif // _PEKWM_X11_XKB_HH_

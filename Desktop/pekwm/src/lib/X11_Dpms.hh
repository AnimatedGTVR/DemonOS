//
// X11_Dpms.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_X11_DPMS_HH_
#define _PEKWM_X11_DPMS_HH_

#include "config.h"

#include "Compat.hh"
#include "X11.hh"

extern "C" {
#ifdef PEKWM_HAVE_DPMS
#include <X11/extensions/dpms.h>
#endif // PEKWM_HAVE_DPMS
}

bool X11_Dpms_IsExtensionAvailable();
bool X11_Dpms_IsEnabled();
void X11_Dpms_SetEnabled(bool enabled);
void X11_Dpms_ForceLevel(bool on_off);

#endif // _PEKWM_X11_DPMS_HH_

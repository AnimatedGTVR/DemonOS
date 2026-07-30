//
// X11_Dpms.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "X11_Dpms.hh"

#ifdef PEKWM_HAVE_DPMS

static int _extension_available = -1;

bool
X11_Dpms_IsExtensionAvailable()
{
	if (_extension_available == -1) {
		int dpms_ev, dpms_err;
		if (DPMSQueryExtension(X11::getDpy(), &dpms_ev, &dpms_err)) {
			_extension_available = 1;
		} else {
			_extension_available = 0;
		}
	}
	return _extension_available == 1;
}

bool
X11_Dpms_IsEnabled()
{
	if (X11_Dpms_IsExtensionAvailable()) {
		CARD16 power_level;
		BOOL state;
		if (DPMSInfo(X11::getDpy(), &power_level, &state)) {
			return state;
		}
	}
	return false;
}

void
X11_Dpms_SetEnabled(bool enabled)
{
	if (X11_Dpms_IsExtensionAvailable()) {
		if (enabled) {
			DPMSEnable(X11::getDpy());
		} else {
			DPMSDisable(X11::getDpy());
		}
	}
}

void
X11_Dpms_ForceLevel(bool on_off)
{

	if (X11_Dpms_IsExtensionAvailable()) {
		DPMSForceLevel(X11::getDpy(),
			       on_off ? DPMSModeOn : DPMSModeOff);
	}
}

#else // ! PEKWM_HAVE_DPMS

bool
X11_Dpms_IsExtensionAvailable()
{
	return false;
}

bool
X11_Dpms_IsEnabled()
{
	return false;
}

void
X11_Dpms_SetEnabled(bool)
{
}

void
X11_Dpms_ForceLevel(bool)
{
}

#endif // PEKWM_HAVE_DPMS

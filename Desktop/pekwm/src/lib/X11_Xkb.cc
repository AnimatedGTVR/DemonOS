//
// X11_Xkb.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//


#include "Util.hh"
#include "X11_Xkb.hh"

#ifdef PEKWM_HAVE_X11_XKBLIB_H

static int _extension_available = -1;
static int _event = -1;

bool
X11_Xkb_IsExtensionAvailable()
{
	if (_extension_available  == -1) {
		if (! X11::getDpy()) {
			return false;
		}
		int major = XkbMajorVersion;
		int minor = XkbMinorVersion;
		int ext_opcode, ext_err;
		if (XkbQueryExtension(X11::getDpy(), &ext_opcode, &_event,
				      &ext_err, &major, &minor)) {
			_extension_available = 1;
		} else {
			_extension_available = 0;
		}
	}
	return _extension_available;
}

bool
X11_Xkb_SelectInput()
{
	if (X11_Xkb_IsExtensionAvailable()) {
		XkbSelectEvents(X11::getDpy(), XkbUseCoreKbd,
				XkbStateNotifyMask, XkbStateNotifyMask);
	}
	return false;
}

bool
X11_Xkb_KeycodeToKeysym(KeyCode keycode, KeySym &keysym)
{
	if (X11_Xkb_IsExtensionAvailable()) {
		keysym = XkbKeycodeToKeysym(X11::getDpy(), keycode, 0, 0);
		return true;
	}
	return false;
}

bool
X11_Xkb_GetIndicatorState(uint &state, uint &locked_state)
{
	if (X11_Xkb_IsExtensionAvailable()) {
		XkbStateRec state_rec = {};
		XkbGetState(X11::getDpy(), XkbUseCoreKbd, &state_rec);
		state = state_rec.mods;
		locked_state = state_rec.locked_mods;
		return true;
	}
	return false;
}

bool
X11_Xkb_GetLayout(std::string &layout)
{
	std::string value;
	if (X11_Xkb_IsExtensionAvailable()
	    && X11::getString(X11::getRoot(), XKB_RULES_NAMES, value)) {
		std::vector<std::string> toks;
		if (Util::splitString(value, toks, ",") > 2) {
			layout = toks[2];
			return true;
		}
	}
	return false;
}

bool
X11_Xkb_HandleStateNotify(XEvent *ev, uint &state, uint &locked_state)
{
	if (ev->type == _event) {
		XkbEvent *xkb_ev = reinterpret_cast<XkbEvent*>(ev);
		if (xkb_ev->any.xkb_type == XkbStateNotify) {
			state = xkb_ev->state.mods;
			locked_state = xkb_ev->state.locked_mods;
			return true;
		}
	}
	return false;
}

#else // ! PEKWM_HAVE_X11_XKBLIB_H

bool
X11_Xkb_IsExtensionAvailable()
{
	return false;
}

bool
X11_Xkb_SelectInput()
{
	return false;
}

bool
X11_Xkb_KeycodeToKeysym(KeyCode, KeySym&)
{
	return false;
}

bool
X11_Xkb_GetIndicatorState(uint&, uint&)
{
	return false;
}

bool
X11_Xkb_GetLayout(std::string&)
{
	return false;
}

bool
X11_Xkb_HandleStateNotify(XEvent*, uint&, uint&)
{
	return false;
}

#endif // PEKWM_HAVE_X11_XKBLIB_H


//
// pekwm_lock.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"
#include "pekwm_lock.hh"

extern "C" {
#include <X11/Xutil.h> // XLookupString
#ifdef PEKWM_HAVE_PAM
#include <security/pam_appl.h>
#endif
#ifdef PEKWM_HAVE_LIBSYSTEMD
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>
#endif // PEKWM_HAVE_LIBSYSTEMD
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
}

#include "Compat.hh"
#include "Debug.hh"
#include "LibNames.hh"
#include "Types.hh"
#include "X11.hh"
#include "X11_Dpms.hh"
#include "X11_Xkb.hh"
#include "X11_XRandr.hh"
#include "tk/Theme.hh"
#include "tk/ThemedX11App.hh"
#include "tk/X11App.hh"

#include <iostream>
#include <string>

static volatile sig_atomic_t stop = 0;

static std::string _config_script_path;
static ObserverMapping _observer_mapping;
static FontHandler* _font_handler = nullptr;
static ImageHandler* _image_handler = nullptr;
static TextureHandler* _texture_handler = nullptr;

namespace pekwm
{
	const std::string& configScriptPath()
	{
		return _config_script_path;
	}

	ObserverMapping* observerMapping()
	{
		return &_observer_mapping;
	}

	FontHandler* fontHandler()
	{
		return _font_handler;
	}

	ImageHandler* imageHandler()
	{
		return _image_handler;
	}

	TextureHandler* textureHandler()
	{
		return _texture_handler;
	}
}

#ifdef PEKWM_HAVE_PAM
static int
_pam_conv_func(int num_msg, const struct pam_message **msg,
	       struct pam_response **resp, void *appdata_ptr)
{
	struct pam_response *replies = new struct pam_response[num_msg];
	if (!replies) {
		return PAM_CONV_ERR;
	}
	memset(replies, '\0', sizeof(struct pam_response) * num_msg);
	const char *password = reinterpret_cast<char*>(appdata_ptr);
	for (int i = 0; i < num_msg; i++) {
	    replies[i].resp = strdup(password ? password : "");
	}
	*resp = replies;
	return PAM_SUCCESS;
}
#endif // PEKWM_HAVE_PAM

PekwmLock::PekwmLock(Os *os,
		     const std::string &theme_dir,
		     const std::string &theme_variant,
		     Geometry gm,
		     ulong attr_mask, XSetWindowAttributes *attr)
	: X11App(gm, 0, "pekwm_lock", "pekwm_lock", "pekwm_lock",
		 WINDOW_TYPE_SPLASH, attr_mask, attr),
	  _theme(_font_handler, _image_handler, _texture_handler,
		 theme_dir, theme_variant),
	  _data(_theme.getDialogData()),
	  _auth_pid(-1),
	  _user(os->getUsername()),
	  _host(os->getHostname()),
	  _fail(0),
	  _dpms_enabled(X11_Dpms_IsEnabled()),
	  _is_caps(false),
	  _flag(nullptr)
{
	X11_Dpms_SetEnabled(true);
	X11_Xkb_SelectInput();
	X11::grabKeyboard(X11::getRoot(), true);
	X11::grabPointer(X11::getRoot(), ButtonPressMask, CURSOR_NONE, false);
	setLockedHint(1);

	uint istate, locked_istate;
	if (X11_Xkb_GetIndicatorState(istate, locked_istate)) {
		_is_caps = locked_istate & LockMask;
	}

	_locked_time = new TkText(_data, *this,
				  "Locked since " + _locked.toString(), false);
	_widgets.push_back(_locked_time);
	_time = new TkText(_data, *this, Calendar().toString(), false);
	_widgets.push_back(_time);
	std::string layout;
	if (X11_Xkb_GetLayout(layout)) {
		layout = "flag-" + layout + ".png";
		_flag = pekwm::imageHandler()->getImage(layout);
	}
	if (_flag == nullptr) {
		_flag = pekwm::imageHandler()->getImage("flag.png");
	}
	_lang = new TkImage(_data, *this, _flag);
	_widgets.push_back(_lang);
	_banner = new TkText(_data, *this, _user + "@" + _host, true);
	_widgets.push_back(_banner);
	_status = new TkText(_data, *this, "", true);
	_widgets.push_back(_status);

}

PekwmLock::~PekwmLock()
{
	std::vector<TkWidget*>::iterator it(_widgets.begin());
	for (; it != _widgets.end(); ++it) {
		delete *it;
	}
	if (_flag != nullptr) {
		pekwm::imageHandler()->returnImage(_flag);
	}

	setLockedHint(0);
	X11::ungrabKeyboard();
	X11::ungrabPointer();
	X11_Dpms_SetEnabled(_dpms_enabled);
}

void
PekwmLock::handleEvent(XEvent *ev)
{
	uint state, locked_state;
	if (ev->type == KeyPress) {
		if (_auth_pid != -1) {
			return;
		}

		char buf[32];
		KeySym ks;
		int n = XLookupString(&ev->xkey, buf, sizeof(buf), &ks,
				      nullptr);
		if (ks == XK_Return) {
			startAuthenticate();
			clearPassword();
			render();
		} else if (ks == XK_BackSpace) {
			if (_password.size() > 0) {
				_password[_password.size() - 1] = '\0';
				_password.resize(_password.size() - 1);
			}
		} else if (n > 0) {
			_password.push_back(buf[0]);
		}
	} else if (ev->type == Expose) {
		Geometry area(ev->xexpose.x, ev->xexpose.y,
			      ev->xexpose.width, ev->xexpose.height);
		std::vector<TkWidget*>::iterator it(_widgets.begin());
		for (; it != _widgets.end(); ++it) {
			(*it)->expose(area);
		}
		render();
	} else if (X11_Xkb_HandleStateNotify(ev, state, locked_state)) {
		_is_caps = locked_state & LockMask;
		render();
        }
}

void
PekwmLock::refresh(bool timed_out)
{
	_time->setText(Calendar().toString());
	render();
}

void
PekwmLock::handleChildDone(pid_t pid, int status)
{
	if (pid == _auth_pid) {
		_auth_pid = -1;

		if (status == 0) {
			stop(0);
		} else {
			_fail++;
		}
		render();
	}
}

void
PekwmLock::screenChanged(const ScreenChangeNotification &scn)
{
	resize(scn.width, scn.height);
	render();
}

void
PekwmLock::render()
{
	std::string status;
	if (_is_caps || _auth_pid != -1 || _fail > 0) {
		if (_is_caps) {
			status += "Caps Lock ON";
			if (_auth_pid != -1 || _fail > 0) {
				status += ". ";
			}
		}

		if (_auth_pid != -1) {
			status += "Authenticating...";
		} else if (_fail == 1) {
			status += "1 Failed attempt";
		} else if (_fail > 1) {
			status += std::to_string(_fail) + " Failed attempts";
		}
	}
	_status->setText(status);

	// Locked Since     [L] Time
	//
	//           User
	//          Status
	//
	//
	X11Render rend(getWindow(), None);
	RenderSurface surface(rend, _gm);

	_locked_time->renderDirty(rend, surface);

	uint time_width = _time->getTextWidth();
	_time->place(getWidth() - time_width, 0, time_width, 0);
	_time->renderDirty(rend, surface);

	uint lang_width = _lang->widthReq();
	_lang->place(getWidth() - time_width - lang_width, 0, lang_width, 0);
	_lang->renderDirty(rend, surface);

	_banner->center(getWidth(), getHeight());
	_banner->renderDirty(rend, surface);

	uint status_width = _status->widthReq();
	_status->place((getWidth() - status_width) / 2,
		       _banner->getY() + _banner->getHeight(),
		       status_width, 0);
	_status->renderDirty(rend, surface);
}

void
PekwmLock::startAuthenticate()
{
	pid_t pid = fork();
	if (pid == -1) {
		// fork failed, can't authenticate
		return;
	}

	if (pid == 0) {
		// child, run authentication
		if (authenticate()) {
			exit(0);
		}
		exit(1);
	}

	P_TRACE("waiting for authentication pid " << pid);
	_auth_pid = pid;
}

bool
PekwmLock::authenticate()
{
#ifdef PEKWM_HAVE_PAM
	struct pam_conv conv = {
		_pam_conv_func,
		reinterpret_cast<void*>(const_cast<char*>(_password.c_str()))
	};

	pam_handle_t *pamh = nullptr;
	int ret = pam_start("login", _user.c_str(), &conv, &pamh);
	if (ret != PAM_SUCCESS) {
		P_TRACE("pam_start failed " << ret);
		return false;
	}

	ret = pam_authenticate(pamh, 0);
	if (ret == PAM_SUCCESS) {
		ret = pam_acct_mgmt(pamh, 0);
		if (ret != PAM_SUCCESS) {
			P_TRACE("pam_acct_mgmt failed " << ret);
		}
	} else {
		P_TRACE("pam_authenticate failed " << ret);
	}

	pam_end(pamh, ret);
	return ret == PAM_SUCCESS;
#else // ! PEKWM_HAVE_PAM
	return false;
#endif // PEKWM_HAVE_PAM
}

void
PekwmLock::setLockedHint(int locked)
{
#ifdef PEKWM_HAVE_LIBSYSTEMD
	char *session_ptr;
	if (sd_pid_get_session(0, &session_ptr) < 0) {
		return;
	}
	Destruct<char> session(session_ptr, free_wrapper<char>);

	sd_bus *bus_ptr = nullptr;
	if (sd_bus_open_system(&bus_ptr) < 0) {
		return;
	}
	Destruct<sd_bus> bus(bus_ptr, sd_bus_unref);

	std::string path("/org/freedesktop/login1/session/");
	path += *session;

	sd_bus_message *msg = nullptr;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_call_method(*bus,
			   "org.freedesktop.login1",
			   path.c_str(),
			   "org.freedesktop.login1.Session",
			   "SetLockedHint",
			   &error, &msg, "b", locked);

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
#endif // PEKWM_HAVE_LIBSYSTEMD
}

void
PekwmLock::clearPassword()
{
	for (size_t i = 0; i < _password.size(); i++) {
		_password[i] = '\0';
	}
	_password.resize(0);
}

static void
usage(const char* name, int ret)
{
	std::cout << "usage: " << name;
	std::cout << " [-cdfhl]" << std::endl;
	std::cout << "  -c --config path     Configuration file" << std::endl;
	std::cout << "  -d --display dpy     Display" << std::endl;
	std::cout << "  -h --help            Display this information"
		  << std::endl;
	std::cout << "  -f --log-file        Set log file" << std::endl;
	std::cout << "  -l --log-level       Set log level" << std::endl;
	exit(ret);
}

int
main(int argc, char **argv)
{
	pledge_x11_required("");

	const char *display = nullptr;
	std::string config_file = Util::getEnv("PEKWM_CONFIG_FILE");

	const struct option opts[] = {
		{const_cast<char*>("config"), required_argument, nullptr, 'c'},
		{const_cast<char*>("display"), required_argument, nullptr,
		 'd'},
		{const_cast<char*>("help"), no_argument, nullptr, 'h'},
		{const_cast<char*>("log-level"), required_argument, nullptr,
		 'l'},
		{const_cast<char*>("log-file"), required_argument, nullptr,
		 'f'},
		{nullptr, 0, nullptr, 0}
	};

	Charset::init();
	int ch;
	while ((ch = getopt_long(argc, argv, "c:d:hl:f:",
				 opts, nullptr)) != -1) {
		switch (ch) {
		case 'c':
			config_file = optarg;
			break;
		case 'd':
			display = optarg;
			break;
		case 'h':
			usage(argv[0], 0);
			break;
		case 'f':
			if (! Debug::setLogFile(optarg)) {
				std::cerr << "Failed to open log file "
					  << optarg << std::endl;
			}
			break;
		case 'l':
			Debug::setLevel(pekwm::str_to_debug_level(optarg));
			break;
		default:
			usage(argv[0], 1);
			break;
		}
	}

	if (config_file.empty()) {
		config_file = Util::getConfigDir() + "/config";
	}
	Util::expandFileName(config_file);

	if (! X11::init(display, std::cerr)) {
		return 1;
	}

	if (setgid(getgid()) != 0 || setuid(getuid()) != 0) {
		P_TRACE("failed to drop privileges");
		return 1;
	}

	std::string theme_dir, theme_variant;
	ThemedX11App::init(config_file, theme_dir, theme_variant,
			   &_font_handler, &_image_handler, &_texture_handler);

	pledge_x("stdio", nullptr);
	{
		ulong attr_mask = CWBackPixel|CWEventMask|CWOverrideRedirect;
		XSetWindowAttributes attr;
		attr.background_pixel = X11::getWhitePixel();
		attr.event_mask = ExposureMask|KeyPressMask;
		attr.override_redirect = True;

		Geometry gm(0, 0, X11::getWidth(), X11::getHeight());
		Destruct<Os> os(mkOs());
		PekwmLock lock(*os, theme_dir, theme_variant, gm,
			       attr_mask, &attr);
		lock.mapWindowRaised();
		lock.main(1);
	}

	X11::destruct();
	Charset::destruct();
	return 0;
}

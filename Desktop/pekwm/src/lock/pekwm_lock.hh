//
// pekwm_lock.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_LOCK_HH_
#define _PEKWM_LOCK_HH_

#include "Calendar.hh"
#include "Os.hh"
#include "tk/TkImage.hh"
#include "tk/TkText.hh"
#include "tk/Theme.hh"
#include "tk/X11App.hh"

#include <vector>

extern "C" {
#include <unistd.h>
}

class PekwmLock : public X11App {
public:
	PekwmLock(Os *os, const std::string &theme_dir,
		  const std::string &theme_variant, Geometry gm,
		  ulong attr_mask, XSetWindowAttributes *attr);
	virtual ~PekwmLock();

protected:
	virtual void handleEvent(XEvent *ev);
	virtual void refresh(bool timed_out);
	virtual void handleChildDone(pid_t pid, int status);

private:
	PekwmLock(const PekwmLock&);
	PekwmLock& operator=(const PekwmLock&);

	virtual void screenChanged(const ScreenChangeNotification &scn);

	virtual void themeChanged(const std::string& name,
				  const std::string& variant, float scale) { }

	void render();

	void startAuthenticate();
	bool authenticate();
	void setLockedHint(int locked);
	void clearPassword();

	Theme _theme;
	Theme::DialogData* _data;

	pid_t _auth_pid;
	std::string _user;
	std::string _host;
	std::string _password;
	int _fail;
	Calendar _locked;
	bool _dpms_enabled;
	bool _is_caps;

	TkText *_locked_time;
	TkText *_time;
	TkImage *_lang;
	PImage *_flag;
	TkText *_banner;
	TkText *_status;

	std::vector<TkWidget*> _widgets;
};

#endif // _PEKWM_LOCK_HH_

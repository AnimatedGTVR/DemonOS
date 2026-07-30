//
// SysDbusScreensaver.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_SYS_DBUS_SCREENSAVER_HH_
#define _PEKWM_SYS_DBUS_SCREENSAVER_HH_

#include "config.h"

#include "Dbus.hh"
#include "Observable.hh"

class SysDbusScreensaverInhibitChanged : public Observation {
};


class SysDbusScreensaverCallback : public DbusCallback,
				   public Observable {
public:
	struct Inhibit {
		std::string owner;
		std::string name;
		std::string reason;
		uint cookie;
	};

	SysDbusScreensaverCallback();
	virtual ~SysDbusScreensaverCallback();

	bool isInhibited() const { return !_inhibit.empty(); }

	virtual bool process(DbusMessage &msg);
	virtual void ownerChanged(const std::string &old_owner,
				  const std::string &new_owner);

private:
	uint inhibit(const std::string &owner, const std::string &name,
		     const std::string &reason);
	void uninhibit(uint cookie);
	void notify();

	std::vector<Inhibit> _inhibit;
};

#endif // _PEKWM_SYS_DBUS_SCREENSAVER_HH_

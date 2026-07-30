//
// SysDbusScreensaver.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "SysDbusScreensaver.hh"
#include "Debug.hh"

extern "C" {
#include <stdlib.h>
}

#define INTERFACE "org.freedesktop.ScreenSaver"

static SysDbusScreensaverInhibitChanged inhibit_changed;

SysDbusScreensaverCallback::SysDbusScreensaverCallback()
	: DbusCallback(
		INTERFACE,
		"/org/freedesktop/ScreenSaver",
		// NOTE: Only interested in Inhibit and Unhibit
		"interface='" INTERFACE "'")
{
}

SysDbusScreensaverCallback::~SysDbusScreensaverCallback()
{
	pekwm::observerMapping()->removeObservable(this);
}

bool
SysDbusScreensaverCallback::process(DbusMessage &msg)
{
	bool handled = false;
#ifdef PEKWM_HAVE_DBUS
	DbusError err;
	const char *sender = dbus_message_get_sender(*msg);
	if (dbus_message_is_method_call(*msg, INTERFACE, "Inhibit")) {
		DbusMessage reply(msg.getConn(),
				  dbus_message_new_method_return(*msg), true);

		const char *name, *reason;
		if (dbus_message_get_args(*msg, *err,
					  DBUS_TYPE_STRING, &name,
					  DBUS_TYPE_STRING, &reason,
					  DBUS_TYPE_INVALID)) {
			uint cookie = inhibit(sender, name, reason);
			P_TRACE(sender << " Inhibit message " << name << " "
				<< reason << " -> " << cookie);
			dbus_message_append_args(
				*reply,
				 DBUS_TYPE_UINT32, &cookie,
				 DBUS_TYPE_INVALID);
		} else {
			P_TRACE("Failed to get Inhibit message arguments");
		}
		dbus_connection_send(msg.getConn(), *reply, nullptr);
		dbus_connection_flush(msg.getConn()); 
		handled = true;
	} else if (dbus_message_is_method_call(*msg, INTERFACE, "UnInhibit")) {
		DbusMessage reply(msg.getConn(),
				  dbus_message_new_method_return(*msg), true);

		unsigned int cookie;
		if (dbus_message_get_args(*msg, *err,
					  DBUS_TYPE_UINT32, &cookie,
					  DBUS_TYPE_INVALID)) {
			P_TRACE(sender << " UnInhibit message " << cookie);
			uninhibit(cookie);
		} else {
			P_TRACE("Failed to get UnInhibit message arguments");
		}
		dbus_connection_send(msg.getConn(), *reply, nullptr);
		dbus_connection_flush(msg.getConn()); 
		handled = true;
	}
#endif // PEKWM_HAVE_DBUS
	return handled;
}

uint
SysDbusScreensaverCallback::inhibit(const std::string &owner,
				    const std::string &name,
				    const std::string &reason)
{
	bool inhibited = isInhibited();
	Inhibit inhibit;
	inhibit.owner = owner;
	inhibit.name = name;
	inhibit.reason = reason;
	inhibit.cookie = static_cast<unsigned int>(rand());
	if (inhibited != isInhibited()) {
		notify();
	}
	return inhibit.cookie;
}

void
SysDbusScreensaverCallback::uninhibit(uint cookie)
{
	bool inhibited = isInhibited();
	std::vector<Inhibit>::iterator it(_inhibit.begin());
	while (it != _inhibit.end()) {
		if (it->cookie == cookie) {
			it = _inhibit.erase(it);
		} else {
			++it;
		}
	}
	if (inhibited != isInhibited()) {
		notify();
	}
}

void
SysDbusScreensaverCallback::ownerChanged(const std::string &old_owner,
					 const std::string &new_owner)
{
	std::vector<Inhibit>::iterator it(_inhibit.begin());
	while (it != _inhibit.end()) {
		if (it->owner == old_owner) {
			if (new_owner.empty()) {
				it = _inhibit.erase(it);
			} else {
				++it;
			}
		} else {
			++it;
		}
	}
	notify();
}

void
SysDbusScreensaverCallback::notify()
{
	pekwm::observerMapping()->notifyObservers(this, &inhibit_changed);
}

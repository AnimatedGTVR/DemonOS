//
// Dbus.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_DBUS_HH_
#define _PEKWM_DBUS_HH_

#include "config.h"

#ifdef PEKWM_HAVE_DBUS

extern "C" {
#include <dbus/dbus.h>
}

#endif // PEKWM_HAVE_DBUS

#include "Compat.hh"
#include "Callback.hh"
#include "Os.hh"

#include <map>
#include <string>
#include <vector>

/**
 * Wrapper for DBusMessage with resource management.
 */
class DbusMessage {
public:
#ifdef PEKWM_HAVE_DBUS
	DbusMessage(DBusConnection *conn, DBusMessage *msg,
		    bool reference = false)
		: _conn(conn),
		  _msg(msg)
	{
		if (_msg && reference) {
			dbus_message_ref(_msg);
		}
	}
	~DbusMessage()
	{
		if (_msg) {
			dbus_message_unref(_msg);
		}
	}

	DBusConnection *getConn() const { return _conn; }
	const char *getPath() const {
		return _msg ? dbus_message_get_path(_msg) : nullptr;
	}
	dbus_uint32_t getReplySerial() const {
		return _msg ? dbus_message_get_reply_serial(_msg) : 0;
	}

	bool getStringArray(std::vector<std::string> &values) const;

	// populate, must be called before use of get and print
	void populate(const char *prefix = nullptr);
	bool get(const std::string &key, std::string &value) const;
	void print(std::ostream &str) const;


	DBusMessage* operator*() const { return _msg; }
	DbusMessage& operator=(DBusMessage *msg)
	{
		if (_msg) {
			dbus_message_unref(_msg);
		}
		_msg = msg;
		return *this;
	}
#endif // PEKWM_HAVE_DBUS

private:
	DbusMessage(const DbusMessage&);
	DbusMessage& operator=(DbusMessage&);

	void populate(const std::string &parent, DBusMessageIter *iter);
	void populateDictEntry(const std::string &parent,
			       DBusMessageIter *iter);
	void populateRecurse(const std::string &parent, DBusMessageIter *iter,
			     const char *prefix);
	bool getValueStr(std::string &value_str, DBusMessageIter *iter) const;

#ifdef PEKWM_HAVE_DBUS
	DBusConnection *_conn;
	DBusMessage *_msg;
#endif // PEKWM_HAVE_DBUS
	std::map<std::string, std::string> _values;
};

class DbusCallback {
public:
	DbusCallback(const std::string &name, const std::string &path,
		     const std::string &match)
		: _name(name),
		  _path(path),
		  _match(match)
	{
	}
	virtual ~DbusCallback() { }

	bool hasName() const { return _name.size() > 0; }
	const std::string &getName() const { return _name; }
	const std::string &getPath() const { return _path; }
	const std::string &getMatch() const { return _match; }

	virtual bool call(const std::string &method, DbusMessage &msg) {
		return false;
	}
	virtual bool process(DbusMessage &msg) = 0;
	virtual void ownerChanged(const std::string&, const std::string&) { }


private:
	std::string _name;
	std::string _path;
	std::string _match;
};

#ifdef PEKWM_HAVE_DBUS

/**
 * Wrapper for DBusError with resource management.
 */
class DbusError {
public:
	DbusError()
	{
		dbus_error_init(&_err);
	}

	~DbusError()
	{
		dbus_error_free(&_err);
	}

	DBusError* operator*() { return &_err; }

	const char *getMessage() const
	{
		return isError() ? _err.message : "";
	}

	bool isError() const { return dbus_error_is_set(&_err); }

private:
	DbusError(DbusError&);
	DbusError& operator=(const DbusError&);

	DBusError _err;
};



struct DbusWatch {
	int fd;
	int flags;
	DBusWatch *watch;
};

/**
 * Handler for NameOwnerChanged signals.
 */
class DbusNameOwnerChangedCallback : public DbusCallback {
public:
	DbusNameOwnerChangedCallback(const std::map<std::string,
				     DbusCallback*> &callbacks);
	virtual ~DbusNameOwnerChangedCallback() { }

	virtual bool process(DbusMessage &msg);

private:
	const std::map<std::string, DbusCallback*> &_callbacks;
};

class Dbus {
public:
	typedef std::vector<std::string> string_vector;

	Dbus(OsSelect *select_);
	~Dbus();

	bool isConnected() const { return _conn != nullptr; }
	std::string getError() const;

	bool matchAdd(DbusCallback *cb);
	void matchRemove(DbusCallback *cb);

	bool call(DbusCallback *callback, const std::string &method);
	bool listNames(CallbackStringVector *callback);

	void selectProcess();
	bool handleMessage(DbusMessage &msg);

	void watchAdd(DBusWatch *watch);
	void watchRemove(DBusWatch *watch);

	DBusConnection* operator*() { return _conn; }

private:
	Dbus(const Dbus&);
	Dbus &operator=(const Dbus&);

	OsSelect *_select;
	DBusConnection *_conn;
	DbusError _err;
	std::vector<DbusWatch> _watches;
	std::map<std::string, DbusCallback*> _callbacks;
	std::map<dbus_uint32_t, DbusCallback*> _reply_handlers;
	DbusNameOwnerChangedCallback _name_owner_change;
};

#endif // PEKWM_HAVE_DBUS

#endif // _PEKWM_DBUS_HH_

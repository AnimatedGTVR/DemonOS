//
// Dbus.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Dbus.hh"
#include "Debug.hh"
#include "Mem.hh"

extern "C" {
#include <assert.h>
}

#ifdef PEKWM_HAVE_DBUS

#define NAME_OWNER_CHANGED_MATCH \
	"type='signal',sender='org.freedesktop.DBus'," \
	"interface='org.freedesktop.DBus',member='NameOwnerChanged'"

static dbus_bool_t
_dbus_watch_add(DBusWatch *watch, void *opaque)
{
	Dbus *dbus = static_cast<Dbus*>(opaque);
	if (dbus_watch_get_enabled(watch)) {
		dbus->watchAdd(watch);
	}
	return TRUE;
}

static void
_dbus_watch_remove(DBusWatch *watch, void *opaque)
{
	Dbus *dbus = static_cast<Dbus*>(opaque);
	dbus->watchRemove(watch);
}

static void
_dbus_watch_toggle(DBusWatch *watch, void *opaque)
{
	Dbus *dbus = static_cast<Dbus*>(opaque);
	dbus->watchRemove(watch);
	if (dbus_watch_get_enabled(watch)) {
		dbus->watchAdd(watch);
	}
}

class DbusListNamesCallback : public DbusCallback {
public:
	DbusListNamesCallback(CallbackStringVector *callback)
		: DbusCallback("", "", ""),
		  _callback(callback)
	{
	}
	virtual ~DbusListNamesCallback() { }

	virtual bool process(DbusMessage &msg)
	{
		std::vector<std::string> names;
		if (msg.getStringArray(names)) {
			_callback->callback(names);
		}
		return true;
	}

	virtual bool call(const std::string &method, DbusMessage &msg)
	{
		if (method != "ListNames") {
			return false;
		}
		msg = dbus_message_new_method_call("org.freedesktop.DBus",
						   "/org/freedesktop/DBus",
						   "org.freedesktop.DBus",
						   "ListNames");
		return true;
	}

private:
	CallbackStringVector *_callback;
};

DbusNameOwnerChangedCallback::DbusNameOwnerChangedCallback(
		const std::map<std::string, DbusCallback*> &callbacks)
	: DbusCallback("",
		       "/org/freedesktop/DBus",
		       NAME_OWNER_CHANGED_MATCH),
	  _callbacks(callbacks)
{
}

bool
DbusNameOwnerChangedCallback::process(DbusMessage &msg)
{
	if (! dbus_message_is_signal(*msg, "org.freedesktop.DBus",
				     "NameOwnerChanged")) {
		return false;
	}

	DbusError err;
	const char *name, *old_owner, *new_owner;
	if (dbus_message_get_args(*msg, *err,
				  DBUS_TYPE_STRING, &name,
				  DBUS_TYPE_STRING, &old_owner,
				  DBUS_TYPE_STRING, &new_owner,
				  DBUS_TYPE_INVALID)) {
		P_TRACE("NameOwnerChanged " << name << " " << old_owner
			<< " -> " << new_owner);
		std::map<std::string, DbusCallback*>::const_iterator it =
			_callbacks.begin();
		for (; it != _callbacks.end(); ++it) {
			it->second->ownerChanged(old_owner, new_owner);
		}
	} else {
		P_TRACE("Failed to get NameOwnerChanged args");
	}
	return true;
}

bool
DbusMessage::getStringArray(std::vector<std::string> &values) const
{
	char **names;
	int num;
	DbusError error;
	if (! _msg
	    || ! dbus_message_get_args(_msg, *error, 
				       DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, 
				       &names, &num, 
				       DBUS_TYPE_INVALID)) {
		return false;
	}
	for (int i = 0; i < num; i++) {
		values.push_back(names[i]);
	}
	dbus_free_string_array(names);
	return true;
}

bool
DbusMessage::get(const std::string &key, std::string &value) const
{
	std::map<std::string, std::string>::const_iterator it =
		_values.find(key);
	if (it == _values.end()) {
		return false;
	}
	value = it->second;
	return true;
}

void
DbusMessage::print(std::ostream &out) const
{
	std::map<std::string, std::string>::const_iterator it(_values.begin());
	for (; it != _values.end(); ++it) {
		std::cout << it->first << " = " << it->second << std::endl;
	}
}

void
DbusMessage::populate(const char *prefix)
{
	DBusMessageIter iter;
	if (_values.empty()
	    && _msg != nullptr
	    && dbus_message_iter_init(_msg, &iter)) {
		// Expect the first part of the message to be the name, to
		// be used as the base key
		std::string key;
		if (prefix) {
			key = prefix;
		} else if (getValueStr(key, &iter)) {
			dbus_message_iter_next(&iter);
		}
		populate(key, &iter);
	}
}

void
DbusMessage::populate(const std::string &key, DBusMessageIter *iter)
{
	int type;
	std::string value_str;
	while ((type = dbus_message_iter_get_arg_type(iter))
	       != DBUS_TYPE_INVALID) {
		switch (type) {
		case DBUS_TYPE_ARRAY:
			populateRecurse(key, iter, "");
			break;
		case DBUS_TYPE_STRUCT:
			populateRecurse(key, iter, ".");
			break;
		case DBUS_TYPE_VARIANT:
			populateRecurse(key, iter, "");
			break;
		case DBUS_TYPE_DICT_ENTRY:
			populateDictEntry(key, iter);
			break;
		case DBUS_TYPE_OBJECT_PATH:
		case DBUS_TYPE_SIGNATURE:
		case DBUS_TYPE_UNIX_FD:
			break;
		default:
			if (getValueStr(value_str, iter)) {
				_values[key] = value_str;
			}
			break;
		}
		dbus_message_iter_next(iter);
	}
}

void
DbusMessage::populateDictEntry(const std::string &key, DBusMessageIter *iter)
{
	DBusMessageIter dict_iter;
	dbus_message_iter_recurse(iter, &dict_iter);

	std::string dict_key;
	getValueStr(dict_key, &dict_iter);
	dict_key = key + "." + dict_key;

	dbus_message_iter_next(&dict_iter);
	std::string dict_value;
	if (getValueStr(dict_value, &dict_iter)) {
		_values[dict_key] = dict_value;
	} else {
		populate(dict_key, &dict_iter);
	}
}

void
DbusMessage::populateRecurse(const std::string &key, DBusMessageIter *iter,
			     const char *prefix)
{
	std::string sub_key(key);
	sub_key += prefix;
	DBusMessageIter sub_iter;
	dbus_message_iter_recurse(iter, &sub_iter);
	populate(sub_key, &sub_iter);
}

bool
DbusMessage::getValueStr(std::string &value_str, DBusMessageIter *iter) const
{
	DBusBasicValue value;
	std::ostringstream value_buf;
	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.byt;
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.byt;
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.i16;
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.u16;
		break;
	case DBUS_TYPE_INT32:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.i32;
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.u32;
		break;
	case DBUS_TYPE_INT64:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.i64;
		break;
	case DBUS_TYPE_UINT64:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.u64;
		break;
	case DBUS_TYPE_DOUBLE:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.dbl;
		break;
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(iter, &value);
		value_buf << value.str;
		break;
	default:
		return false;
	}
	value_str = value_buf.str();
	return true;
}

Dbus::Dbus(OsSelect *select_)
	: _select(select_),
	  _conn(nullptr),
	  _name_owner_change(_callbacks)
{
	_conn = dbus_bus_get(DBUS_BUS_SESSION, *_err);
	if (! _conn) {
		return;
	}

	dbus_connection_set_watch_functions(
		_conn,
		_dbus_watch_add, _dbus_watch_remove, _dbus_watch_toggle,
		reinterpret_cast<void*>(this), nullptr);

	// always listen to NameOwnerChanged signals to allow for handlers
	// to get notified if requested
	matchAdd(&_name_owner_change);
}

Dbus::~Dbus()
{
}

std::string
Dbus::getError() const
{
	return _err.getMessage();
}

bool
Dbus::matchAdd(DbusCallback *cb)
{
	if (! _conn) {
		return false;
	}
	std::map<std::string, DbusCallback*>::iterator it =
		_callbacks.find(cb->getPath());
	if (it != _callbacks.end()) {
		return false;
	}

	if (cb->hasName()) {
		int res =
			dbus_bus_request_name(_conn, cb->getName().c_str(),
					      DBUS_NAME_FLAG_REPLACE_EXISTING, 
					      *_err);
		if (res != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
			P_TRACE("failed to request DBus name "
				<< cb->getName());
		}
	}

	dbus_bus_add_match(_conn, cb->getMatch().c_str(), *_err);
	dbus_connection_flush(_conn);
	if (_err.isError()) {
		// FIXME: unrequest name? delay request until acquired?
		return false;
	}
	_callbacks[cb->getPath()] = cb;
	return true;
}

void
Dbus::matchRemove(DbusCallback *cb)
{
	std::map<std::string, DbusCallback*>::iterator it =
		_callbacks.find(cb->getPath());
	if (it != _callbacks.end()) {
		_callbacks.erase(it);
	}
}

bool
Dbus::call(DbusCallback *callback, const std::string &method)
{
	DbusMessage msg(_conn, nullptr, false);
	callback->call(method, msg);
	if (! *msg) {
		return false;
	}

	dbus_uint32_t serial = 0;
	dbus_connection_send(_conn, *msg, &serial);
	if (serial != 0) {
		_reply_handlers[serial] = callback;
	}
	return true;
}

bool
Dbus::listNames(CallbackStringVector *callback)
{
	DbusListNamesCallback *list_names_callback =
		new DbusListNamesCallback(callback);
	if (! call(list_names_callback, "ListNames")) {
		delete list_names_callback;
		return false;
	}
	return true;
}

void
Dbus::selectProcess()
{
	std::vector<DbusWatch>::iterator it(_watches.begin());
	for (; it != _watches.end(); ++it) {
		unsigned int flags = 0;
		assert(it->fd == dbus_watch_get_unix_fd(it->watch));
		if (_select->isSet(it->fd, OsSelect::OS_SELECT_READ)) {
			flags |= DBUS_WATCH_READABLE;
		}
		if (_select->isSet(it->fd, OsSelect::OS_SELECT_WRITE)) {
			flags |= DBUS_WATCH_WRITABLE;
		}
		if (flags) {
			dbus_watch_handle(it->watch, flags);
		}
	}

	DbusMessage msg(_conn, dbus_connection_pop_message(_conn));
	while (*msg) {
		handleMessage(msg);
		msg = dbus_connection_pop_message(_conn);
	}
}

bool
Dbus::handleMessage(DbusMessage &msg)
{
	std::map<dbus_uint32_t, DbusCallback*>::iterator r_it =
		_reply_handlers.find(msg.getReplySerial());
	if (r_it != _reply_handlers.end()) {
		r_it->second->process(msg);
		_reply_handlers.erase(r_it);
		return true;
	}

	if (msg.getPath() == nullptr) {
		return false;
	}

	std::map<std::string, DbusCallback*>::iterator c_it =
		_callbacks.find(msg.getPath());
	if (c_it != _callbacks.end()) {
		return c_it->second->process(msg);
	}

	P_TRACE("unhandled DBusMessage "
		<< dbus_message_get_path(*msg) << " "
		<< dbus_message_get_interface(*msg) << " "
		<< dbus_message_get_member(*msg));
	return false;
}

void
Dbus::watchAdd(DBusWatch *watch_)
{
	DbusWatch watch;
	watch.fd = dbus_watch_get_unix_fd(watch_);
	watch.flags = 0;
	watch.watch = watch_;

	unsigned int flags = dbus_watch_get_flags(watch_);
	if (flags & DBUS_WATCH_READABLE) {
		watch.flags |= OsSelect::OS_SELECT_READ;
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		watch.flags |= OsSelect::OS_SELECT_WRITE;
	}

	P_TRACE("add DBusWatch " << static_cast<void*>(watch_) << " flags "
		<< watch.flags);
	_watches.push_back(watch);
	_select->add(watch.fd, watch.flags);
}

void
Dbus::watchRemove(DBusWatch *watch_)
{
	P_TRACE("remove DBusWatch " << static_cast<void*>(watch_));
	std::vector<DbusWatch>::iterator it(_watches.begin());
	for (; it != _watches.end(); ++it) {
		if (it->watch == watch_) {
			_select->remove(it->fd);
			_watches.erase(it);
			break;
		}
	}
}

#endif // PEKWM_HAVE_DBUS

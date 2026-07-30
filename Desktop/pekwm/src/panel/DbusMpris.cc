//
// DbusMpris.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "DbusMpris.hh"
#include "String.hh"

extern "C" {
#include <string>
}

#define PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_MATCH "member='PropertiesChanged'"
#define PLAYER_PREFIX "org.mpris.MediaPlayer2.Player"

struct dbus_mapping {
	const char *dbus_key;
	const char *var_key;
};
static struct dbus_mapping MAPPING[] = {
	{PLAYER_PREFIX ".Metadata.xesam:album", "MUSIC_ALBUM"},
	{PLAYER_PREFIX ".Metadata.xesam:artist", "MUSIC_ARTIST"},
	{PLAYER_PREFIX ".Metadata.xesam:title", "MUSIC_TITLE"},
	{PLAYER_PREFIX ".PlaybackStatus", "MUSIC_STATUS"},
	{nullptr, nullptr}
};

DbusMprisCallback::DbusMprisCallback(VarData &data)
	: DbusCallback(
		PROPERTIES_INTERFACE,
		MPRIS_PATH,
		MPRIS_MATCH),
	  _data(data)
{
	// FIXME: send initial message getting the properties of the player
}

DbusMprisCallback::~DbusMprisCallback()
{
}

bool
DbusMprisCallback::call(const std::string &method, DbusMessage &msg)
{
	StringView dest(nullptr, 0, 0);
	const char *property;
	if (pekwm::str_starts_with(method, "Get:")) {
		dest = StringView(method, 0, 4);
		property = "Metadata";
		_prefix.push_back(PLAYER_PREFIX ".Metadata");
	} else if (pekwm::str_starts_with(method, "Status:")) {
		dest = StringView(method, 0, 7);
		property = "PlaybackStatus";
		_prefix.push_back(PLAYER_PREFIX ".PlaybackStatus");
	} else {
		return false;
	}

	msg = dbus_message_new_method_call(
		dest.str().c_str(),
		MPRIS_PATH,
		PROPERTIES_INTERFACE,
		"Get");
	const char *interface = MPRIS_PLAYER_INTERFACE;
	dbus_message_append_args(*msg, DBUS_TYPE_STRING,
				 &interface,
				 DBUS_TYPE_STRING,
				 &property,
				 DBUS_TYPE_INVALID);
	return true;
}

/**
 * Process signals on /org/mpris/MediaPlayer2 extracting playback status
 * and metadata of the currently playing song.
 *
 * string "org.mpris.MediaPlayer2.Player"
 *
 * Metadata (dict)
 *
 * mpris:artUrl (string, URL to album art, file:// for local)
 * xesam:title (string)
 * xesam:album (string)
 * xesam:artist (array of string)
 * xesam:url (string, URL to media if non-local)
 *
 * PlaybackStatus (string)
 *
 * string Playing | Paused
 *
 */
bool
DbusMprisCallback::process(DbusMessage &msg)
{
	if (msg.getPath() || _prefix.size() == 0) {
		msg.populate(nullptr);
	} else {
		msg.populate(_prefix.front());
		_prefix.erase(_prefix.begin());
	}

	std::string value;
	for (int i = 0; MAPPING[i].dbus_key != nullptr; i++) {
		if (msg.get(MAPPING[i].dbus_key, value)) {
			_data.set(MAPPING[i].var_key, value);
		}
	}
	return true;
}

void
DbusMprisCallback::ownerChanged(const std::string &old_owner,
				const std::string &new_owner)
{
}

void
DbusMprisCallback::clear()
{
	for (int i = 0; MAPPING[i].dbus_key != nullptr; i++) {
		_data.set(MAPPING[i].var_key, "");
	}
}

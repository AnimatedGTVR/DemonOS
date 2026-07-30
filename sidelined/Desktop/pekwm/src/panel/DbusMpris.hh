//
// DbusMpris.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_DBUS_MPRIS_HH_
#define _PEKWM_DBUS_MPRIS_HH_

#include "Dbus.hh"
#include "VarData.hh"

#include <vector>

class DbusMprisCallback : public DbusCallback {
public:
	DbusMprisCallback(VarData &data);
	virtual ~DbusMprisCallback();

	virtual bool call(const std::string &method, DbusMessage &msg);
	virtual bool process(DbusMessage &msg);
	virtual void ownerChanged(const std::string &old_owner,
				  const std::string &new_owner);

private:
	void clear();

	VarData &_data;
	std::vector<const char*> _prefix;
};

#endif // _PEKWM_DBUS_MPRIS_HH_

// NOTE: this file is auto-generated, do not edit

#include "Util.hh"
#include "WmNames.hh"

namespace pekwm {

static Util::StringTo<PropertyType> property_type_map[] = {
	{"AllowedActions", PROPERTY_TYPE_ALLOWED_ACTIONS},
	{"Border", PROPERTY_TYPE_BORDER},
	{"CfgDeny", PROPERTY_TYPE_CFG_DENY},
	{"ClientGeometry", PROPERTY_TYPE_CLIENT_GEOMETRY},
	{"Decor", PROPERTY_TYPE_DECOR},
	{"DisallowedActions", PROPERTY_TYPE_DISALLOWED_ACTIONS},
	{"FocusNew", PROPERTY_TYPE_FOCUS_NEW},
	{"Focusable", PROPERTY_TYPE_FOCUSABLE},
	{"FrameGeometry", PROPERTY_TYPE_FRAME_GEOMETRY},
	{"Fullscreen", PROPERTY_TYPE_FULLSCREEN},
	{"Icon", PROPERTY_TYPE_ICON},
	{"Iconified", PROPERTY_TYPE_ICONIFIED},
	{"Layer", PROPERTY_TYPE_LAYER},
	{"MaximizedHorizontal", PROPERTY_TYPE_MAXIMIZED_HORIZONTAL},
	{"MaximizedVertical", PROPERTY_TYPE_MAXIMIZED_VERTICAL},
	{"Opacity", PROPERTY_TYPE_OPACITY},
	{"PlaceNew", PROPERTY_TYPE_PLACE_NEW},
	{"Placement", PROPERTY_TYPE_PLACEMENT},
	{"Shaded", PROPERTY_TYPE_SHADED},
	{"Skip", PROPERTY_TYPE_SKIP},
	{"Sticky", PROPERTY_TYPE_STICKY},
	{"Titlebar", PROPERTY_TYPE_TITLEBAR},
	{"Workspace", PROPERTY_TYPE_WORKSPACE},
	{nullptr, PROPERTY_TYPE_NO},
};

PropertyType
str_to_property_type(const std::string &str)
{
	return Util::StringToGet(property_type_map, str);
}

const char*
property_type_to_str(PropertyType value)
{
	return Util::StringToGetStr(property_type_map, value);
}

void
get_property_type_names(std::vector<const char*> &names)
{
	for (int i = 0; property_type_map[i].name != nullptr; i++) {
		names.push_back(property_type_map[i].name);
	}
}

static Util::StringTo<PropertyGroup> property_group_map[] = {
	{"Behind", PROPERTY_GROUP_BEHIND},
	{"FocusedFirst", PROPERTY_GROUP_FOCUSED_FIRST},
	{"Global", PROPERTY_GROUP_GLOBAL},
	{"Raise", PROPERTY_GROUP_RAISE},
	{"Size", PROPERTY_GROUP_SIZE},
	{nullptr, PROPERTY_GROUP_NO},
};

PropertyGroup
str_to_property_group(const std::string &str)
{
	return Util::StringToGet(property_group_map, str);
}

const char*
property_group_to_str(PropertyGroup value)
{
	return Util::StringToGetStr(property_group_map, value);
}

void
get_property_group_names(std::vector<const char*> &names)
{
	for (int i = 0; property_group_map[i].name != nullptr; i++) {
		names.push_back(property_group_map[i].name);
	}
}

static Util::StringTo<WinLayouterType> win_layouter_type_map[] = {
	{"Centered", WIN_LAYOUTER_TYPE_CENTERED},
	{"Centeredonparent", WIN_LAYOUTER_TYPE_CENTEREDONPARENT},
	{"Mousecentered", WIN_LAYOUTER_TYPE_MOUSECENTERED},
	{"Mousenotunder", WIN_LAYOUTER_TYPE_MOUSENOTUNDER},
	{"Mousetopleft", WIN_LAYOUTER_TYPE_MOUSETOPLEFT},
	{"Smart", WIN_LAYOUTER_TYPE_SMART},
	{nullptr, WIN_LAYOUTER_TYPE_NO},
};

WinLayouterType
str_to_win_layouter_type(const std::string &str)
{
	return Util::StringToGet(win_layouter_type_map, str);
}

const char*
win_layouter_type_to_str(WinLayouterType value)
{
	return Util::StringToGetStr(win_layouter_type_map, value);
}

void
get_win_layouter_type_names(std::vector<const char*> &names)
{
	for (int i = 0; win_layouter_type_map[i].name != nullptr; i++) {
		names.push_back(win_layouter_type_map[i].name);
	}
}

bool
wm_enum_name_to_names(const char *name, std::vector<const char*> &names)
{
	if (pekwm::ascii_ncase_equal(name, "PropertyType")) {
	    get_property_type_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "PropertyGroup")) {
	    get_property_group_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "WinLayouterType")) {
	    get_win_layouter_type_names(names);
	    return true;
	}
	return false;
}


};

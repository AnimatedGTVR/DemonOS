// NOTE: this file is auto-generated, do not edit

#include "Util.hh"
#include "LibNames.hh"

namespace pekwm {

static Util::StringTo<NetWmSource> net_wm_source_map[] = {
	{"Client", NET_WM_SOURCE_CLIENT},
	{"Legacy", NET_WM_SOURCE_LEGACY},
	{"Pager", NET_WM_SOURCE_PAGER},
	{nullptr, NET_WM_SOURCE_NO},
};

NetWmSource
str_to_net_wm_source(const std::string &str)
{
	return Util::StringToGet(net_wm_source_map, str);
}

const char*
net_wm_source_to_str(NetWmSource value)
{
	return Util::StringToGetStr(net_wm_source_map, value);
}

void
get_net_wm_source_names(std::vector<const char*> &names)
{
	for (int i = 0; net_wm_source_map[i].name != nullptr; i++) {
		names.push_back(net_wm_source_map[i].name);
	}
}

static Util::StringTo<Layer> layer_map[] = {
	{"AboveDock", LAYER_ABOVE_DOCK},
	{"Below", LAYER_BELOW},
	{"Desktop", LAYER_DESKTOP},
	{"Dock", LAYER_DOCK},
	{"Menu", LAYER_MENU},
	{"Normal", LAYER_NORMAL},
	{"Ontop", LAYER_ONTOP},
	{nullptr, LAYER_NONE},
};

Layer
str_to_layer(const std::string &str)
{
	return Util::StringToGet(layer_map, str);
}

const char*
layer_to_str(Layer value)
{
	return Util::StringToGetStr(layer_map, value);
}

void
get_layer_names(std::vector<const char*> &names)
{
	for (int i = 0; layer_map[i].name != nullptr; i++) {
		names.push_back(layer_map[i].name);
	}
}

static Util::StringTo<ApplyOn> apply_on_map[] = {
	{"Always", APPLY_ON_ALWAYS},
	{"New", APPLY_ON_NEW},
	{"Reload", APPLY_ON_RELOAD},
	{"Start", APPLY_ON_START},
	{"Transient", APPLY_ON_TRANSIENT},
	{"TransientOnly", APPLY_ON_TRANSIENT_ONLY},
	{"Workspace", APPLY_ON_WORKSPACE},
	{nullptr, APPLY_ON_NO},
};

ApplyOn
str_to_apply_on(const std::string &str)
{
	return Util::StringToGet(apply_on_map, str);
}

const char*
apply_on_to_str(ApplyOn value)
{
	return Util::StringToGetStr(apply_on_map, value);
}

void
get_apply_on_names(std::vector<const char*> &names)
{
	for (int i = 0; apply_on_map[i].name != nullptr; i++) {
		names.push_back(apply_on_map[i].name);
	}
}

static Util::StringTo<Skip> skip_map[] = {
	{"FocusToggle", SKIP_FOCUS_TOGGLE},
	{"Menus", SKIP_MENUS},
	{"Pager", SKIP_PAGER},
	{"Snap", SKIP_SNAP},
	{"Taskbar", SKIP_TASKBAR},
	{nullptr, SKIP_NONE},
};

Skip
str_to_skip(const std::string &str)
{
	return Util::StringToGet(skip_map, str);
}

const char*
skip_to_str(Skip value)
{
	return Util::StringToGetStr(skip_map, value);
}

void
get_skip_names(std::vector<const char*> &names)
{
	for (int i = 0; skip_map[i].name != nullptr; i++) {
		names.push_back(skip_map[i].name);
	}
}

static Util::StringTo<Border> border_map[] = {
	{"Bottom", BORDER_BOTTOM},
	{"BottomLeft", BORDER_BOTTOM_LEFT},
	{"BottomRight", BORDER_BOTTOM_RIGHT},
	{"Left", BORDER_LEFT},
	{"Right", BORDER_RIGHT},
	{"Top", BORDER_TOP},
	{"TopLeft", BORDER_TOP_LEFT},
	{"TopRight", BORDER_TOP_RIGHT},
	{nullptr, BORDER_NO},
};

Border
str_to_border(const std::string &str)
{
	return Util::StringToGet(border_map, str);
}

const char*
border_to_str(Border value)
{
	return Util::StringToGetStr(border_map, value);
}

void
get_border_names(std::vector<const char*> &names)
{
	for (int i = 0; border_map[i].name != nullptr; i++) {
		names.push_back(border_map[i].name);
	}
}

static Util::StringTo<ImageType> image_type_map[] = {
	{"Fixed", IMAGE_TYPE_FIXED},
	{"Scaled", IMAGE_TYPE_SCALED},
	{"Tiled", IMAGE_TYPE_TILED},
	{nullptr, IMAGE_TYPE_NO},
};

ImageType
str_to_image_type(const std::string &str)
{
	return Util::StringToGet(image_type_map, str);
}

const char*
image_type_to_str(ImageType value)
{
	return Util::StringToGetStr(image_type_map, value);
}

void
get_image_type_names(std::vector<const char*> &names)
{
	for (int i = 0; image_type_map[i].name != nullptr; i++) {
		names.push_back(image_type_map[i].name);
	}
}

static Util::StringTo<FontJustify> font_justify_map[] = {
	{"Center", FONT_JUSTIFY_CENTER},
	{"Left", FONT_JUSTIFY_LEFT},
	{"Right", FONT_JUSTIFY_RIGHT},
	{nullptr, FONT_JUSTIFY_NO},
};

FontJustify
str_to_font_justify(const std::string &str)
{
	return Util::StringToGet(font_justify_map, str);
}

const char*
font_justify_to_str(FontJustify value)
{
	return Util::StringToGetStr(font_justify_map, value);
}

void
get_font_justify_names(std::vector<const char*> &names)
{
	for (int i = 0; font_justify_map[i].name != nullptr; i++) {
		names.push_back(font_justify_map[i].name);
	}
}

static Util::StringTo<HarbourPlacement> harbour_placement_map[] = {
	{"Bottom", HARBOUR_PLACEMENT_BOTTOM},
	{"Left", HARBOUR_PLACEMENT_LEFT},
	{"Right", HARBOUR_PLACEMENT_RIGHT},
	{"Top", HARBOUR_PLACEMENT_TOP},
	{nullptr, HARBOUR_PLACEMENT_NO},
};

HarbourPlacement
str_to_harbour_placement(const std::string &str)
{
	return Util::StringToGet(harbour_placement_map, str);
}

const char*
harbour_placement_to_str(HarbourPlacement value)
{
	return Util::StringToGetStr(harbour_placement_map, value);
}

void
get_harbour_placement_names(std::vector<const char*> &names)
{
	for (int i = 0; harbour_placement_map[i].name != nullptr; i++) {
		names.push_back(harbour_placement_map[i].name);
	}
}

static Util::StringTo<WorkspaceChange> workspace_change_map[] = {
	{"Down", WORKSPACE_CHANGE_DOWN},
	{"Last", WORKSPACE_CHANGE_LAST},
	{"Left", WORKSPACE_CHANGE_LEFT},
	{"LeftN", WORKSPACE_CHANGE_LEFT_N},
	{"Next", WORKSPACE_CHANGE_NEXT},
	{"NextN", WORKSPACE_CHANGE_NEXT_N},
	{"NextV", WORKSPACE_CHANGE_NEXT_V},
	{"Prev", WORKSPACE_CHANGE_PREV},
	{"PrevN", WORKSPACE_CHANGE_PREV_N},
	{"PrevV", WORKSPACE_CHANGE_PREV_V},
	{"Right", WORKSPACE_CHANGE_RIGHT},
	{"RightN", WORKSPACE_CHANGE_RIGHT_N},
	{"Up", WORKSPACE_CHANGE_UP},
	{nullptr, WORKSPACE_CHANGE_NO},
};

WorkspaceChange
str_to_workspace_change(const std::string &str)
{
	return Util::StringToGet(workspace_change_map, str);
}

const char*
workspace_change_to_str(WorkspaceChange value)
{
	return Util::StringToGetStr(workspace_change_map, value);
}

void
get_workspace_change_names(std::vector<const char*> &names)
{
	for (int i = 0; workspace_change_map[i].name != nullptr; i++) {
		names.push_back(workspace_change_map[i].name);
	}
}

static Util::StringTo<Edge> edge_map[] = {
	{"BottomCenterEdge", EDGE_BOTTOM_CENTER_EDGE},
	{"BottomEdge", EDGE_BOTTOM_EDGE},
	{"BottomLeft", EDGE_BOTTOM_LEFT},
	{"BottomRight", EDGE_BOTTOM_RIGHT},
	{"Center", EDGE_CENTER},
	{"LeftCenterEdge", EDGE_LEFT_CENTER_EDGE},
	{"LeftEdge", EDGE_LEFT_EDGE},
	{"RightCenterEdge", EDGE_RIGHT_CENTER_EDGE},
	{"RightEdge", EDGE_RIGHT_EDGE},
	{"TopCenterEdge", EDGE_TOP_CENTER_EDGE},
	{"TopEdge", EDGE_TOP_EDGE},
	{"TopLeft", EDGE_TOP_LEFT},
	{"TopRight", EDGE_TOP_RIGHT},
	{nullptr, EDGE_NO},
};

Edge
str_to_edge(const std::string &str)
{
	return Util::StringToGet(edge_map, str);
}

const char*
edge_to_str(Edge value)
{
	return Util::StringToGetStr(edge_map, value);
}

void
get_edge_names(std::vector<const char*> &names)
{
	for (int i = 0; edge_map[i].name != nullptr; i++) {
		names.push_back(edge_map[i].name);
	}
}

static Util::StringTo<Raise> raise_map[] = {
	{"AlwaysRaise", RAISE_ALWAYS_RAISE},
	{"EndRaise", RAISE_END_RAISE},
	{"NeverRaise", RAISE_NEVER_RAISE},
	{"TempRaise", RAISE_TEMP_RAISE},
	{nullptr, RAISE_NO},
};

Raise
str_to_raise(const std::string &str)
{
	return Util::StringToGet(raise_map, str);
}

const char*
raise_to_str(Raise value)
{
	return Util::StringToGetStr(raise_map, value);
}

void
get_raise_names(std::vector<const char*> &names)
{
	for (int i = 0; raise_map[i].name != nullptr; i++) {
		names.push_back(raise_map[i].name);
	}
}

static Util::StringTo<Orientation> orientation_map[] = {
	{"BottomToTop", ORIENTATION_BOTTOM_TO_TOP},
	{"LeftToRight", ORIENTATION_LEFT_TO_RIGHT},
	{"RightToLeft", ORIENTATION_RIGHT_TO_LEFT},
	{"TopToBottom", ORIENTATION_TOP_TO_BOTTOM},
	{nullptr, ORIENTATION_NO},
};

Orientation
str_to_orientation(const std::string &str)
{
	return Util::StringToGet(orientation_map, str);
}

const char*
orientation_to_str(Orientation value)
{
	return Util::StringToGetStr(orientation_map, value);
}

void
get_orientation_names(std::vector<const char*> &names)
{
	for (int i = 0; orientation_map[i].name != nullptr; i++) {
		names.push_back(orientation_map[i].name);
	}
}

static Util::StringTo<MouseEvent> mouse_event_map[] = {
	{"ButtonPress", MOUSE_EVENT_BUTTON_PRESS},
	{"ButtonRelease", MOUSE_EVENT_BUTTON_RELEASE},
	{"DoubleClick", MOUSE_EVENT_DOUBLE_CLICK},
	{"Enter", MOUSE_EVENT_ENTER},
	{"EnterMoving", MOUSE_EVENT_ENTER_MOVING},
	{"Leave", MOUSE_EVENT_LEAVE},
	{"Motion", MOUSE_EVENT_MOTION},
	{"MotionPressed", MOUSE_EVENT_MOTION_PRESSED},
	{nullptr, MOUSE_EVENT_NO},
};

MouseEvent
str_to_mouse_event(const std::string &str)
{
	return Util::StringToGet(mouse_event_map, str);
}

const char*
mouse_event_to_str(MouseEvent value)
{
	return Util::StringToGetStr(mouse_event_map, value);
}

void
get_mouse_event_names(std::vector<const char*> &names)
{
	for (int i = 0; mouse_event_map[i].name != nullptr; i++) {
		names.push_back(mouse_event_map[i].name);
	}
}

static Util::StringTo<Mod> mod_map[] = {
	{"Any", MOD_ANY},
	{"Ctrl", MOD_CTRL},
	{"Mod1", MOD_MOD1},
	{"Mod2", MOD_MOD2},
	{"Mod3", MOD_MOD3},
	{"Mod4", MOD_MOD4},
	{"Mod5", MOD_MOD5},
	{"None", MOD_NONE},
	{"Shift", MOD_SHIFT},
	{nullptr, MOD_NO},
};

Mod
str_to_mod(const std::string &str)
{
	return Util::StringToGet(mod_map, str);
}

const char*
mod_to_str(Mod value)
{
	return Util::StringToGetStr(mod_map, value);
}

void
get_mod_names(std::vector<const char*> &names)
{
	for (int i = 0; mod_map[i].name != nullptr; i++) {
		names.push_back(mod_map[i].name);
	}
}

static Util::StringTo<CfgDeny> cfg_deny_map[] = {
	{"ActiveWindow", CFG_DENY_ACTIVE_WINDOW},
	{"Position", CFG_DENY_POSITION},
	{"ResizeInc", CFG_DENY_RESIZE_INC},
	{"Size", CFG_DENY_SIZE},
	{"Stacking", CFG_DENY_STACKING},
	{"StateAbove", CFG_DENY_STATE_ABOVE},
	{"StateBelow", CFG_DENY_STATE_BELOW},
	{"StateFullscreen", CFG_DENY_STATE_FULLSCREEN},
	{"StateHidden", CFG_DENY_STATE_HIDDEN},
	{"StateMaximizedHorz", CFG_DENY_STATE_MAXIMIZED_HORZ},
	{"StateMaximizedVert", CFG_DENY_STATE_MAXIMIZED_VERT},
	{"Strut", CFG_DENY_STRUT},
	{nullptr, CFG_DENY_NO},
};

CfgDeny
str_to_cfg_deny(const std::string &str)
{
	return Util::StringToGet(cfg_deny_map, str);
}

const char*
cfg_deny_to_str(CfgDeny value)
{
	return Util::StringToGetStr(cfg_deny_map, value);
}

void
get_cfg_deny_names(std::vector<const char*> &names)
{
	for (int i = 0; cfg_deny_map[i].name != nullptr; i++) {
		names.push_back(cfg_deny_map[i].name);
	}
}

static Util::StringTo<CurrHeadSelector> curr_head_selector_map[] = {
	{"Cursor", CURR_HEAD_SELECTOR_CURSOR},
	{"FocusedWindow", CURR_HEAD_SELECTOR_FOCUSED_WINDOW},
	{nullptr, CURR_HEAD_SELECTOR_NO},
};

CurrHeadSelector
str_to_curr_head_selector(const std::string &str)
{
	return Util::StringToGet(curr_head_selector_map, str);
}

const char*
curr_head_selector_to_str(CurrHeadSelector value)
{
	return Util::StringToGetStr(curr_head_selector_map, value);
}

void
get_curr_head_selector_names(std::vector<const char*> &names)
{
	for (int i = 0; curr_head_selector_map[i].name != nullptr; i++) {
		names.push_back(curr_head_selector_map[i].name);
	}
}

static Util::StringTo<FocusSelector> focus_selector_map[] = {
	{"Pointer", FOCUS_SELECTOR_POINTER},
	{"Root", FOCUS_SELECTOR_ROOT},
	{"Top", FOCUS_SELECTOR_TOP},
	{"WorkspaceLastFocused", FOCUS_SELECTOR_WORKSPACE_LAST_FOCUSED},
	{nullptr, FOCUS_SELECTOR_NO},
};

FocusSelector
str_to_focus_selector(const std::string &str)
{
	return Util::StringToGet(focus_selector_map, str);
}

const char*
focus_selector_to_str(FocusSelector value)
{
	return Util::StringToGetStr(focus_selector_map, value);
}

void
get_focus_selector_names(std::vector<const char*> &names)
{
	for (int i = 0; focus_selector_map[i].name != nullptr; i++) {
		names.push_back(focus_selector_map[i].name);
	}
}

static Util::StringTo<OnCloseFocusRaise> on_close_focus_raise_map[] = {
	{"Always", ON_CLOSE_FOCUS_RAISE_ALWAYS},
	{"IfCovered", ON_CLOSE_FOCUS_RAISE_IF_COVERED},
	{"Never", ON_CLOSE_FOCUS_RAISE_NEVER},
	{nullptr, ON_CLOSE_FOCUS_RAISE_NO},
};

OnCloseFocusRaise
str_to_on_close_focus_raise(const std::string &str)
{
	return Util::StringToGet(on_close_focus_raise_map, str);
}

const char*
on_close_focus_raise_to_str(OnCloseFocusRaise value)
{
	return Util::StringToGetStr(on_close_focus_raise_map, value);
}

void
get_on_close_focus_raise_names(std::vector<const char*> &names)
{
	for (int i = 0; on_close_focus_raise_map[i].name != nullptr; i++) {
		names.push_back(on_close_focus_raise_map[i].name);
	}
}

static Util::StringTo<WarpOn> warp_on_map[] = {
	{"FocusChange", WARP_ON_FOCUS_CHANGE},
	{"New", WARP_ON_NEW},
	{nullptr, WARP_ON_NO},
};

WarpOn
str_to_warp_on(const std::string &str)
{
	return Util::StringToGet(warp_on_map, str);
}

const char*
warp_on_to_str(WarpOn value)
{
	return Util::StringToGetStr(warp_on_map, value);
}

void
get_warp_on_names(std::vector<const char*> &names)
{
	for (int i = 0; warp_on_map[i].name != nullptr; i++) {
		names.push_back(warp_on_map[i].name);
	}
}

static Util::StringTo<DebugLevel> debug_level_map[] = {
	{"Debug", DEBUG_LEVEL_DEBUG},
	{"Error", DEBUG_LEVEL_ERROR},
	{"Info", DEBUG_LEVEL_INFO},
	{"Warning", DEBUG_LEVEL_WARNING},
	{nullptr, DEBUG_LEVEL_TRACE},
};

DebugLevel
str_to_debug_level(const std::string &str)
{
	return Util::StringToGet(debug_level_map, str);
}

const char*
debug_level_to_str(DebugLevel value)
{
	return Util::StringToGetStr(debug_level_map, value);
}

void
get_debug_level_names(std::vector<const char*> &names)
{
	for (int i = 0; debug_level_map[i].name != nullptr; i++) {
		names.push_back(debug_level_map[i].name);
	}
}

static Util::StringTo<Direction> direction_map[] = {
	{"Down", DIRECTION_DOWN},
	{"Left", DIRECTION_LEFT},
	{"No", DIRECTION_NO},
	{"Right", DIRECTION_RIGHT},
	{"Up", DIRECTION_UP},
	{nullptr, DIRECTION_IGNORED},
};

Direction
str_to_direction(const std::string &str)
{
	return Util::StringToGet(direction_map, str);
}

const char*
direction_to_str(Direction value)
{
	return Util::StringToGetStr(direction_map, value);
}

void
get_direction_names(std::vector<const char*> &names)
{
	for (int i = 0; direction_map[i].name != nullptr; i++) {
		names.push_back(direction_map[i].name);
	}
}

bool
lib_enum_name_to_names(const char *name, std::vector<const char*> &names)
{
	if (pekwm::ascii_ncase_equal(name, "NetWmSource")) {
	    get_net_wm_source_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Layer")) {
	    get_layer_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "ApplyOn")) {
	    get_apply_on_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Skip")) {
	    get_skip_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Border")) {
	    get_border_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "ImageType")) {
	    get_image_type_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "FontJustify")) {
	    get_font_justify_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "HarbourPlacement")) {
	    get_harbour_placement_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "WorkspaceChange")) {
	    get_workspace_change_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Edge")) {
	    get_edge_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Raise")) {
	    get_raise_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Orientation")) {
	    get_orientation_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "MouseEvent")) {
	    get_mouse_event_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Mod")) {
	    get_mod_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "CfgDeny")) {
	    get_cfg_deny_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "CurrHeadSelector")) {
	    get_curr_head_selector_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "FocusSelector")) {
	    get_focus_selector_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "OnCloseFocusRaise")) {
	    get_on_close_focus_raise_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "WarpOn")) {
	    get_warp_on_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "DebugLevel")) {
	    get_debug_level_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Direction")) {
	    get_direction_names(names);
	    return true;
	}
	return false;
}


};

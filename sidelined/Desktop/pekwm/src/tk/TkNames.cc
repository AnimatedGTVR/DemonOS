// NOTE: this file is auto-generated, do not edit

#include "Util.hh"
#include "TkNames.hh"

namespace pekwm {

static Util::StringTo<ActionAccess> action_access_map[] = {
	{"Close", ACTION_ACCESS_CLOSE},
	{"Fullscreen", ACTION_ACCESS_FULLSCREEN},
	{"Iconify", ACTION_ACCESS_ICONIFY},
	{"MaximizeHorizontal", ACTION_ACCESS_MAXIMIZE_HORIZONTAL},
	{"MaximizeVertical", ACTION_ACCESS_MAXIMIZE_VERTICAL},
	{"Move", ACTION_ACCESS_MOVE},
	{"Resize", ACTION_ACCESS_RESIZE},
	{"SetWorkspace", ACTION_ACCESS_SET_WORKSPACE},
	{"Shade", ACTION_ACCESS_SHADE},
	{"Stick", ACTION_ACCESS_STICK},
	{nullptr, ACTION_ACCESS_NO},
};

ActionAccess
str_to_action_access(const std::string &str)
{
	return Util::StringToGet(action_access_map, str);
}

const char*
action_access_to_str(ActionAccess value)
{
	return Util::StringToGetStr(action_access_map, value);
}

void
get_action_access_names(std::vector<const char*> &names)
{
	for (int i = 0; action_access_map[i].name != nullptr; i++) {
		names.push_back(action_access_map[i].name);
	}
}

static Util::StringTo<ActionType> action_type_map[] = {
	{"ActivateClient", ACTION_TYPE_ACTIVATE_CLIENT},
	{"ActivateClientNum", ACTION_TYPE_ACTIVATE_CLIENT_NUM},
	{"ActivateClientRel", ACTION_TYPE_ACTIVATE_CLIENT_REL},
	{"ActivateOrRaise", ACTION_TYPE_ACTIVATE_OR_RAISE},
	{"AttachClientInNextFrame", ACTION_TYPE_ATTACH_CLIENT_IN_NEXT_FRAME},
	{"AttachClientInPrevFrame", ACTION_TYPE_ATTACH_CLIENT_IN_PREV_FRAME},
	{"AttachFrameInNextFrame", ACTION_TYPE_ATTACH_FRAME_IN_NEXT_FRAME},
	{"AttachFrameInPrevFrame", ACTION_TYPE_ATTACH_FRAME_IN_PREV_FRAME},
	{"AttachMarked", ACTION_TYPE_ATTACH_MARKED},
	{"Close", ACTION_TYPE_CLOSE},
	{"CloseFrame", ACTION_TYPE_CLOSE_FRAME},
	{"Debug", ACTION_TYPE_DEBUG},
	{"Detach", ACTION_TYPE_DETACH},
	{"DetachSplitHorz", ACTION_TYPE_DETACH_SPLIT_HORZ},
	{"DetachSplitVert", ACTION_TYPE_DETACH_SPLIT_VERT},
	{"Exec", ACTION_TYPE_EXEC},
	{"Exit", ACTION_TYPE_EXIT},
	{"FillEdge", ACTION_TYPE_FILL_EDGE},
	{"FindClient", ACTION_TYPE_FIND_CLIENT},
	{"Focus", ACTION_TYPE_FOCUS},
	{"FocusDirectional", ACTION_TYPE_FOCUS_DIRECTIONAL},
	{"FocusWithSelector", ACTION_TYPE_FOCUS_WITH_SELECTOR},
	{"GotoClient", ACTION_TYPE_GOTO_CLIENT},
	{"GotoClientId", ACTION_TYPE_GOTO_CLIENT_ID},
	{"GotoWorkspace", ACTION_TYPE_GOTO_WORKSPACE},
	{"GroupingDrag", ACTION_TYPE_GROUPING_DRAG},
	{"GrowDirection", ACTION_TYPE_GROW_DIRECTION},
	{"HideAllMenus", ACTION_TYPE_HIDE_ALL_MENUS},
	{"HideWorkspaceIndicator", ACTION_TYPE_HIDE_WORKSPACE_INDICATOR},
	{"Kill", ACTION_TYPE_KILL},
	{"Lower", ACTION_TYPE_LOWER},
	{"Maxfill", ACTION_TYPE_MAXFILL},
	{"MenuDyn", ACTION_TYPE_MENU_DYN},
	{"MenuEnterSubmenu", ACTION_TYPE_MENU_ENTER_SUBMENU},
	{"MenuGoto", ACTION_TYPE_MENU_GOTO},
	{"MenuLeaveSubmenu", ACTION_TYPE_MENU_LEAVE_SUBMENU},
	{"MenuNext", ACTION_TYPE_MENU_NEXT},
	{"MenuPrev", ACTION_TYPE_MENU_PREV},
	{"MenuSelect", ACTION_TYPE_MENU_SELECT},
	{"MenuSub", ACTION_TYPE_MENU_SUB},
	{"Move", ACTION_TYPE_MOVE},
	{"MoveClientRel", ACTION_TYPE_MOVE_CLIENT_REL},
	{"MoveResize", ACTION_TYPE_MOVE_RESIZE},
	{"MoveToEdge", ACTION_TYPE_MOVE_TO_EDGE},
	{"MoveToHead", ACTION_TYPE_MOVE_TO_HEAD},
	{"NextFrame", ACTION_TYPE_NEXT_FRAME},
	{"NextFrameMru", ACTION_TYPE_NEXT_FRAME_MRU},
	{"PrevFrame", ACTION_TYPE_PREV_FRAME},
	{"PrevFrameMru", ACTION_TYPE_PREV_FRAME_MRU},
	{"Raise", ACTION_TYPE_RAISE},
	{"Reload", ACTION_TYPE_RELOAD},
	{"Resize", ACTION_TYPE_RESIZE},
	{"Restart", ACTION_TYPE_RESTART},
	{"RestartOther", ACTION_TYPE_RESTART_OTHER},
	{"SendKey", ACTION_TYPE_SEND_KEY},
	{"SendToWorkspace", ACTION_TYPE_SEND_TO_WORKSPACE},
	{"Set", ACTION_TYPE_SET},
	{"SetGeometry", ACTION_TYPE_SET_GEOMETRY},
	{"SetOpacity", ACTION_TYPE_SET_OPACITY},
	{"Setenv", ACTION_TYPE_SETENV},
	{"ShellExec", ACTION_TYPE_SHELL_EXEC},
	{"ShowCmdDialog", ACTION_TYPE_SHOW_CMD_DIALOG},
	{"ShowMenu", ACTION_TYPE_SHOW_MENU},
	{"ShowSearchDialog", ACTION_TYPE_SHOW_SEARCH_DIALOG},
	{"Sys", ACTION_TYPE_SYS},
	{"Toggle", ACTION_TYPE_TOGGLE},
	{"Unfocus", ACTION_TYPE_UNFOCUS},
	{"Unset", ACTION_TYPE_UNSET},
	{"WarpPointer", ACTION_TYPE_WARP_POINTER},
	{"WarpToWorkspace", ACTION_TYPE_WARP_TO_WORKSPACE},
	{"WmSet", ACTION_TYPE_WM_SET},
	{nullptr, ACTION_TYPE_NO},
};

ActionType
str_to_action_type(const std::string &str)
{
	return Util::StringToGet(action_type_map, str);
}

const char*
action_type_to_str(ActionType value)
{
	return Util::StringToGetStr(action_type_map, value);
}

void
get_action_type_names(std::vector<const char*> &names)
{
	for (int i = 0; action_type_map[i].name != nullptr; i++) {
		names.push_back(action_type_map[i].name);
	}
}

static Util::StringTo<ActionState> action_state_map[] = {
	{"AlwaysBelow", ACTION_STATE_ALWAYS_BELOW},
	{"AlwaysOntop", ACTION_STATE_ALWAYS_ONTOP},
	{"CfgDeny", ACTION_STATE_CFG_DENY},
	{"Decor", ACTION_STATE_DECOR},
	{"DecorBorder", ACTION_STATE_DECOR_BORDER},
	{"DecorTitlebar", ACTION_STATE_DECOR_TITLEBAR},
	{"Fullscreen", ACTION_STATE_FULLSCREEN},
	{"GlobalGrouping", ACTION_STATE_GLOBAL_GROUPING},
	{"HarbourHidden", ACTION_STATE_HARBOUR_HIDDEN},
	{"Iconified", ACTION_STATE_ICONIFIED},
	{"Marked", ACTION_STATE_MARKED},
	{"Maximized", ACTION_STATE_MAXIMIZED},
	{"Opaque", ACTION_STATE_OPAQUE},
	{"Shaded", ACTION_STATE_SHADED},
	{"Skip", ACTION_STATE_SKIP},
	{"Sticky", ACTION_STATE_STICKY},
	{"Tagged", ACTION_STATE_TAGGED},
	{"Title", ACTION_STATE_TITLE},
	{nullptr, ACTION_STATE_NO},
};

ActionState
str_to_action_state(const std::string &str)
{
	return Util::StringToGet(action_state_map, str);
}

const char*
action_state_to_str(ActionState value)
{
	return Util::StringToGetStr(action_state_map, value);
}

void
get_action_state_names(std::vector<const char*> &names)
{
	for (int i = 0; action_state_map[i].name != nullptr; i++) {
		names.push_back(action_state_map[i].name);
	}
}

static Util::StringTo<MoveResize> move_resize_map[] = {
	{"Cancel", MOVE_RESIZE_CANCEL},
	{"End", MOVE_RESIZE_END},
	{"MoveHorizontal", MOVE_RESIZE_MOVE_HORIZONTAL},
	{"MoveSnap", MOVE_RESIZE_MOVE_SNAP},
	{"MoveVertical", MOVE_RESIZE_MOVE_VERTICAL},
	{"ResizeHorizontal", MOVE_RESIZE_RESIZE_HORIZONTAL},
	{"ResizeVertical", MOVE_RESIZE_RESIZE_VERTICAL},
	{nullptr, MOVE_RESIZE_NO},
};

MoveResize
str_to_move_resize(const std::string &str)
{
	return Util::StringToGet(move_resize_map, str);
}

const char*
move_resize_to_str(MoveResize value)
{
	return Util::StringToGetStr(move_resize_map, value);
}

void
get_move_resize_names(std::vector<const char*> &names)
{
	for (int i = 0; move_resize_map[i].name != nullptr; i++) {
		names.push_back(move_resize_map[i].name);
	}
}

static Util::StringTo<Input> input_map[] = {
	{"Clear", INPUT_CLEAR},
	{"Clearfromcursor", INPUT_CLEARFROMCURSOR},
	{"Cleartocursor", INPUT_CLEARTOCURSOR},
	{"Close", INPUT_CLOSE},
	{"Complete", INPUT_COMPLETE},
	{"CompleteAbort", INPUT_COMPLETE_ABORT},
	{"CursBegin", INPUT_CURS_BEGIN},
	{"CursEnd", INPUT_CURS_END},
	{"CursNext", INPUT_CURS_NEXT},
	{"CursPrev", INPUT_CURS_PREV},
	{"Erase", INPUT_ERASE},
	{"Exec", INPUT_EXEC},
	{"HistNext", INPUT_HIST_NEXT},
	{"HistPrev", INPUT_HIST_PREV},
	{"Insert", INPUT_INSERT},
	{nullptr, INPUT_NO_ACTION},
};

Input
str_to_input(const std::string &str)
{
	return Util::StringToGet(input_map, str);
}

const char*
input_to_str(Input value)
{
	return Util::StringToGetStr(input_map, value);
}

void
get_input_names(std::vector<const char*> &names)
{
	for (int i = 0; input_map[i].name != nullptr; i++) {
		names.push_back(input_map[i].name);
	}
}

static Util::StringTo<ColorPaletteMode> color_palette_mode_map[] = {
	{"Analogous", COLOR_PALETTE_MODE_ANALOGOUS},
	{"Complementary", COLOR_PALETTE_MODE_COMPLEMENTARY},
	{"Single", COLOR_PALETTE_MODE_SINGLE},
	{"Split", COLOR_PALETTE_MODE_SPLIT},
	{"Square", COLOR_PALETTE_MODE_SQUARE},
	{"Tetrad", COLOR_PALETTE_MODE_TETRAD},
	{"Triad", COLOR_PALETTE_MODE_TRIAD},
	{nullptr, COLOR_PALETTE_MODE_NO},
};

ColorPaletteMode
str_to_color_palette_mode(const std::string &str)
{
	return Util::StringToGet(color_palette_mode_map, str);
}

const char*
color_palette_mode_to_str(ColorPaletteMode value)
{
	return Util::StringToGetStr(color_palette_mode_map, value);
}

void
get_color_palette_mode_names(std::vector<const char*> &names)
{
	for (int i = 0; color_palette_mode_map[i].name != nullptr; i++) {
		names.push_back(color_palette_mode_map[i].name);
	}
}

static Util::StringTo<BaseColor> base_color_map[] = {
	{"Blue", BASE_COLOR_BLUE},
	{"BlueGreen", BASE_COLOR_BLUE_GREEN},
	{"BluePurple", BASE_COLOR_BLUE_PURPLE},
	{"Green", BASE_COLOR_GREEN},
	{"Orange", BASE_COLOR_ORANGE},
	{"Purple", BASE_COLOR_PURPLE},
	{"Red", BASE_COLOR_RED},
	{"RedOrange", BASE_COLOR_RED_ORANGE},
	{"RedPurple", BASE_COLOR_RED_PURPLE},
	{"Yellow", BASE_COLOR_YELLOW},
	{"YellowGreen", BASE_COLOR_YELLOW_GREEN},
	{"YellowOrange", BASE_COLOR_YELLOW_ORANGE},
	{nullptr, BASE_COLOR_NO},
};

BaseColor
str_to_base_color(const std::string &str)
{
	return Util::StringToGet(base_color_map, str);
}

const char*
base_color_to_str(BaseColor value)
{
	return Util::StringToGetStr(base_color_map, value);
}

void
get_base_color_names(std::vector<const char*> &names)
{
	for (int i = 0; base_color_map[i].name != nullptr; i++) {
		names.push_back(base_color_map[i].name);
	}
}

static Util::StringTo<FontType> font_type_map[] = {
	{"Auto", FONT_TYPE_AUTO},
	{"Empty", FONT_TYPE_EMPTY},
	{"Pango", FONT_TYPE_PANGO},
	{"PangoCairo", FONT_TYPE_PANGO_CAIRO},
	{"PangoXft", FONT_TYPE_PANGO_XFT},
	{"X11", FONT_TYPE_X11},
	{"Xft", FONT_TYPE_XFT},
	{"Xmb", FONT_TYPE_XMB},
	{nullptr, FONT_TYPE_NO},
};

FontType
str_to_font_type(const std::string &str)
{
	return Util::StringToGet(font_type_map, str);
}

const char*
font_type_to_str(FontType value)
{
	return Util::StringToGetStr(font_type_map, value);
}

void
get_font_type_names(std::vector<const char*> &names)
{
	for (int i = 0; font_type_map[i].name != nullptr; i++) {
		names.push_back(font_type_map[i].name);
	}
}

bool
tk_enum_name_to_names(const char *name, std::vector<const char*> &names)
{
	if (pekwm::ascii_ncase_equal(name, "ActionAccess")) {
	    get_action_access_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "ActionType")) {
	    get_action_type_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "ActionState")) {
	    get_action_state_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "MoveResize")) {
	    get_move_resize_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "Input")) {
	    get_input_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "ColorPaletteMode")) {
	    get_color_palette_mode_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "BaseColor")) {
	    get_base_color_names(names);
	    return true;
	}
	if (pekwm::ascii_ncase_equal(name, "FontType")) {
	    get_font_type_names(names);
	    return true;
	}
	return false;
}


};

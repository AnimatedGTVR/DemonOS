//
// Action.hh for pekwm
// Copyright (C) 2003-2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_ACTION_HH_
#define _PEKWM_ACTION_HH_

#include "config.h"

#include "CfgParser.hh"
#include "Types.hh"
#include "X11.hh"
#include "pekwm_types.hh"

#include <vector>
#include <string>
#include <cstring>

class PWinObj;

//! @brief Masks used to set Action context validity.
enum ActionOk {
	KEYGRABBER_OK = (1<<1), //!< Keygrabber ok.
	FRAME_OK = (1<<2), //!< Frame title ok.
	CLIENT_OK = (1<<3), //!< Client click ok.
	ROOTCLICK_OK = (1<<4), //!< Root window click ok.
	BUTTONCLICK_OK = (1<<5), //!< Button{Press,Release} ok.
	WINDOWMENU_OK = (1<<6), //!< Ok from WindowMenu.
	ROOTMENU_OK = (1<<7), //!< Ok from RootMenu.
	FRAME_BORDER_OK  = (1<<8), //!< Frame border ok.
	SCREEN_EDGE_OK = (1<<9), //!< ScreenEdge ok.
	CMD_OK = (1<<10) //!< Command from pekwm_ctrl
};

/**
 * Mask used in auto properties granting/disallowing actions.
 */
enum ActionAccess { // Map
	ACTION_ACCESS_MOVE = 1<<1,
	ACTION_ACCESS_RESIZE = 1<<2,
	ACTION_ACCESS_ICONIFY = 1<<3,
	ACTION_ACCESS_SHADE = 1<<4,
	ACTION_ACCESS_STICK = 1<<5,
	ACTION_ACCESS_MAXIMIZE_HORIZONTAL = 1<<6,
	ACTION_ACCESS_MAXIMIZE_VERTICAL = 1<<7,
	ACTION_ACCESS_FULLSCREEN = 1<<8,
	ACTION_ACCESS_SET_WORKSPACE = 1<<9,
	ACTION_ACCESS_CLOSE = 1<<10,
	ACTION_ACCESS_NO = 0
};

enum ActionType { // Map
	ACTION_TYPE_UNSET = 0,
	ACTION_TYPE_SET = 1,
	ACTION_TYPE_TOGGLE,

	ACTION_TYPE_FOCUS,
	ACTION_TYPE_UNFOCUS,

	ACTION_TYPE_GROW_DIRECTION,
	ACTION_TYPE_MAXFILL,
	ACTION_TYPE_RESIZE,
	ACTION_TYPE_MOVE_RESIZE,
	ACTION_TYPE_RAISE,
	ACTION_TYPE_LOWER,
	ACTION_TYPE_ACTIVATE_OR_RAISE,
	ACTION_TYPE_CLOSE,
	ACTION_TYPE_CLOSE_FRAME,
	ACTION_TYPE_KILL,
	ACTION_TYPE_SET_GEOMETRY,
	ACTION_TYPE_MOVE_TO_HEAD,
	ACTION_TYPE_MOVE_TO_EDGE,
	ACTION_TYPE_FILL_EDGE,
	ACTION_TYPE_NEXT_FRAME,
	ACTION_TYPE_NEXT_FRAME_MRU,
	ACTION_TYPE_PREV_FRAME,
	ACTION_TYPE_PREV_FRAME_MRU,
	ACTION_TYPE_FOCUS_WITH_SELECTOR,
	ACTION_TYPE_FOCUS_DIRECTIONAL,
	ACTION_TYPE_GOTO_CLIENT,
	ACTION_TYPE_ACTIVATE_CLIENT_REL,
	ACTION_TYPE_MOVE_CLIENT_REL,
	ACTION_TYPE_ACTIVATE_CLIENT,
	ACTION_TYPE_ACTIVATE_CLIENT_NUM,
	ACTION_TYPE_SEND_TO_WORKSPACE,
	ACTION_TYPE_GOTO_WORKSPACE,
	ACTION_TYPE_WARP_TO_WORKSPACE,
	ACTION_TYPE_SHOW_MENU,
	ACTION_TYPE_HIDE_ALL_MENUS,
	ACTION_TYPE_DETACH,
	ACTION_TYPE_DETACH_SPLIT_HORZ,
	ACTION_TYPE_DETACH_SPLIT_VERT,
	ACTION_TYPE_ATTACH_MARKED,
	ACTION_TYPE_ATTACH_CLIENT_IN_NEXT_FRAME,
	ACTION_TYPE_ATTACH_CLIENT_IN_PREV_FRAME,
	ACTION_TYPE_ATTACH_FRAME_IN_NEXT_FRAME,
	ACTION_TYPE_ATTACH_FRAME_IN_PREV_FRAME,

	ACTION_TYPE_GOTO_CLIENT_ID,
	ACTION_TYPE_FIND_CLIENT,

	ACTION_TYPE_EXEC,
	ACTION_TYPE_SHELL_EXEC,
	ACTION_TYPE_SETENV,
	ACTION_TYPE_RELOAD,
	ACTION_TYPE_RESTART,
	ACTION_TYPE_RESTART_OTHER,
	ACTION_TYPE_EXIT,

	ACTION_TYPE_MENU_NEXT,
	ACTION_TYPE_MENU_PREV,
	ACTION_TYPE_MENU_GOTO,
	ACTION_TYPE_MENU_SELECT,
	ACTION_TYPE_MENU_ENTER_SUBMENU,
	ACTION_TYPE_MENU_LEAVE_SUBMENU,

	ACTION_TYPE_MENU_SUB,
	ACTION_TYPE_MENU_DYN,

	ACTION_TYPE_MOVE,
	ACTION_TYPE_GROUPING_DRAG,
	ACTION_TYPE_SHOW_CMD_DIALOG,
	ACTION_TYPE_SHOW_SEARCH_DIALOG,

	ACTION_TYPE_SEND_KEY,
	ACTION_TYPE_WARP_POINTER,
	ACTION_TYPE_SET_OPACITY,

	ACTION_TYPE_DEBUG,
	ACTION_TYPE_SYS,
	ACTION_TYPE_WM_SET,
	ACTION_TYPE_HIDE_WORKSPACE_INDICATOR,

	ACTION_TYPE_NO
};

enum ActionState { // Map
	ACTION_STATE_MAXIMIZED,
	ACTION_STATE_FULLSCREEN,
	ACTION_STATE_SHADED,
	ACTION_STATE_STICKY,
	ACTION_STATE_ALWAYS_ONTOP,
	ACTION_STATE_ALWAYS_BELOW,
	ACTION_STATE_DECOR_BORDER,
	ACTION_STATE_DECOR_TITLEBAR,
	ACTION_STATE_DECOR,
	ACTION_STATE_TITLE,
	ACTION_STATE_ICONIFIED,
	ACTION_STATE_TAGGED,
	ACTION_STATE_MARKED,
	ACTION_STATE_SKIP,
	ACTION_STATE_CFG_DENY,
	ACTION_STATE_OPAQUE,
	ACTION_STATE_HARBOUR_HIDDEN,
	ACTION_STATE_GLOBAL_GROUPING,

	ACTION_STATE_NO
};

enum StateAction {
	STATE_SET = ACTION_TYPE_SET,
	STATE_UNSET = ACTION_TYPE_UNSET,
	STATE_TOGGLE = ACTION_TYPE_TOGGLE,
	STATE_NO
};

enum MoveResize { // Map
	MOVE_RESIZE_MOVE_HORIZONTAL = 1,
	MOVE_RESIZE_MOVE_VERTICAL,
	MOVE_RESIZE_RESIZE_HORIZONTAL,
	MOVE_RESIZE_RESIZE_VERTICAL,
	MOVE_RESIZE_MOVE_SNAP,
	MOVE_RESIZE_CANCEL,
	MOVE_RESIZE_END,
	MOVE_RESIZE_NO = 0
};

enum Input { // Map
	INPUT_INSERT,
	INPUT_ERASE,
	INPUT_CLEAR,
	INPUT_CLEARFROMCURSOR,
	INPUT_CLEARTOCURSOR,
	INPUT_EXEC,
	INPUT_CLOSE,
	INPUT_COMPLETE,
	INPUT_COMPLETE_ABORT,
	INPUT_CURS_NEXT,
	INPUT_CURS_PREV,
	INPUT_CURS_END,
	INPUT_CURS_BEGIN,
	INPUT_HIST_NEXT,
	INPUT_HIST_PREV,
	INPUT_NO_ACTION
};

enum ActionParamUnit {
	UNIT_PIXEL,
	UNIT_PERCENT,
	UNIT_NONE
};

/**
 * Last byte of data buffer in _PEKWM_CMD messages, indicates if the
 * message is part of a larger message.
 */
enum PekwmCmdBuf {
	PEKWM_CMD_SINGLE = 0,
	PEKWM_CMD_MULTI_FIRST = 1,
	PEKWM_CMD_MULTI_CONT = 2,
	PEKWM_CMD_MULTI_END = 3,
	PEKWM_CMD_NO
};

// Action Utils
namespace ActionUtil {
	//! @brief Determines if state needs toggling.
	//! @return true if state needs toggling, else false.
	inline bool needToggle(StateAction sa, bool state) {
		if ((state && (sa == STATE_SET))
		    || (! state && (sa == STATE_UNSET))) {
			return false;
		}
		return true;
	}
}

// Structs and Classes

class Action {
public:
	Action(ActionType action = ACTION_TYPE_UNSET);
	~Action(void);

	inline uint getAction() const { return _action; }
	inline int getParamI() const {
		return getParamI(0, 0);
	}
	inline int getParamI(uint n) const {
		return getParamI(n, 0);
	}
	inline int getParamI(uint n, int def) const {
		return n < _i.size() ? _i[n] : def;
	}
	size_t numParamI(void) const { return _i.size(); }
	inline const std::string &getParamS(void) const {
		return getParamS(0);
	}
	inline const std::string &getParamS(uint n) const {
		return n < _s.size() ? _s[n] : _empty_string;
	}
	size_t numParamS(void) const { return _s.size(); }

	inline void setAction(uint action) { _action = action; }
	inline void setParamI(uint n, int param) {
		while (n >= _i.size()) {
			_i.push_back(0);
		}
		_i[n] = param;
	}
	inline void setParamS(const std::string& param) {
		setParamS(0, param);
	}
	inline void setParamS(uint n, const std::string& param) {
		while (n >= _s.size()) {
			_s.push_back("");
		}
		_s[n] = param;
	}

	inline void clear()
	{
		_action = ACTION_TYPE_UNSET;
		_i.clear();
		_s.clear();
	}
private:
	uint _action;
	std::vector<int> _i;
	std::vector<std::string> _s;

	static std::string _empty_string;
};

class ActionEvent {
public:
	typedef std::vector<Action> action_vector;
	typedef action_vector::const_iterator it;

	ActionEvent();
	ActionEvent(const Action &action);
	ActionEvent(uint num, ...);
	~ActionEvent();

	inline bool isOnlyAction(ActionType action) const {
		return ((action_list.size() == 1) &&
			(action_list.front().getAction() == action));
	}

	inline bool isAnyModifier(void) {
		return mod == 0 || mod == MOD_ANY;
	}
	inline bool isButtonEvent(void) {
		return type == MOUSE_EVENT_BUTTON_PRESS
			|| type == MOUSE_EVENT_BUTTON_RELEASE;
	}

	uint mod, sym; // event matching
	uint type, threshold; // more matching, press, release etc
	action_vector action_list;
};

class ActionPerformed {
public:
	ActionPerformed(PWinObj *w, const ActionEvent &a)
		: wo(w), ae(a), type(0)
	{
	}
	virtual ~ActionPerformed(void)
	{
	}

	PWinObj *wo;
	const ActionEvent &ae;

	int type;
	union _event {
		XButtonEvent *button;
		XKeyEvent *key;
		XMotionEvent *motion;
		XCrossingEvent *crossing;
		XExposeEvent *expose;
		XClientMessageEvent *client;
	} event;
};

class ActionPerformedWithOffset : public ActionPerformed
{
public:
	ActionPerformedWithOffset(PWinObj *w, const ActionEvent &a,
				  int _offset_x, int _offset_y)
		: ActionPerformed(w, a),
		  offset_x(_offset_x),
		  offset_y(_offset_y)
	{
	}

	int offset_x;
	int offset_y;
};

namespace ActionConfig
{
	bool parseKey(const std::string &key_string, uint& mod, uint &key);

	bool parseAction(const std::string &action_string,
			 Action &action, uint mask);
	bool parseActions(const std::string &action_string,
			  ActionEvent &ae, uint mask);
	bool parseActionEvent(CfgParser::Entry *section, ActionEvent &ae,
			      uint mask, bool is_button);

	void parseActionSetGeometry(Action& action, const std::string &str);

	ActionType getAction(const std::string &name, uint mask);
	const char *getActionName(uint action);
	uint getMouseButton(const std::string &name);

	std::vector<std::string> getActionNameList(void);
}

#endif // _PEKWM_ACTION_HH_

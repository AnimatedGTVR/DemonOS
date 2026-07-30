//
// MenuHandler.cc for pekwm
// Copyright (C) 2009-2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <cassert>
#include <map>
#include <string>

#include "PMenu.hh"
#include "MenuHandler.hh"
#include "ActionHandler.hh"

#include "WORefMenu.hh"
#include "ActionMenu.hh"
#include "FrameListMenu.hh"

#include "tk/PWinObj.hh"
#include "tk/TkGlobals.hh"

TimeFiles MenuHandler::_cfg_files;
bool MenuHandler::_config_loaded = false;
std::map<std::string, PMenu*> MenuHandler::_menu_map;

PMenu*
MenuHandler::getMenu(const std::string &name)
{
	menu_map_cit it = _menu_map.find(name);
	if (it != _menu_map.end()) {
		return it->second;
	}
	PMenu *menu = createMenu(pekwm::actionHandler(), name);
	if (menu == nullptr && ! _config_loaded) {
		_config_loaded = true;
		createMenusLoadConfiguration(pekwm::actionHandler());
		menu = getMenu(name);
	}
	return menu;
}

/**
 * Create reserved menu and populates _menu_map
 */
PMenu*
MenuHandler::createMenu(ActionHandler *act, const std::string &name)
{
	PMenu *menu = nullptr;
	if (pekwm::ascii_ncase_equal("ATTACHCLIENTINFRAME", name)) {
		menu = new FrameListMenu(MENU_TYPE_ATTACH_CLIENT_IN_FRAME,
					 "Attach Client In Frame",
					 "AttachClientInFrame");
		_menu_map["ATTACHCLIENTINFRAME"] = menu;
		return menu;
	}
	if (pekwm::ascii_ncase_equal("ATTACHCLIENT", name)) {
		menu = new FrameListMenu(MENU_TYPE_ATTACH_CLIENT,
					 "Attach Client", "AttachClient");
		_menu_map["ATTACHCLIENT"] = menu;
		return menu;
	}
	if (pekwm::ascii_ncase_equal("ATTACHFRAMEINFRAME", name)) {
		menu = new FrameListMenu(MENU_TYPE_ATTACH_FRAME_IN_FRAME,
					 "Attach Frame In Frame",
					 "AttachFrameInFrame");
		_menu_map["ATTACHFRAMEINFRAME"] = menu;
	}
	if (pekwm::ascii_ncase_equal("ATTACHFRAME", name)) {
		menu = new FrameListMenu(MENU_TYPE_ATTACH_FRAME,
					 "Attach Frame", "AttachFrame");
		_menu_map["ATTACHFRAME"] = menu;
	}
	if (pekwm::ascii_ncase_equal("GOTOCLIENT", name)) {
		menu = new FrameListMenu(MENU_TYPE_GOTO_CLIENT,
					 "Focus Client", "GotoClient");
		_menu_map["GOTOCLIENT"] = menu;
		return menu;
	}
	if (pekwm::ascii_ncase_equal("GOTO", name)) {
		menu = new FrameListMenu(MENU_TYPE_GOTO,
					"Focus Frame", "Goto");
		_menu_map["GOTO"] = menu;
		return menu;
	}
	if (pekwm::ascii_ncase_equal("ICON", name)) {
		menu = new FrameListMenu(MENU_TYPE_ICON,
					 "Focus Iconified Frame", "Icon");
		_menu_map["ICON"] = menu;
		return menu;
	}

	// create menu but do not return it, will cause configuration
	// to be loaded (same as for non-standard menus) and fetch of meny
	// retried
	if (pekwm::ascii_ncase_equal("ROOT", name)
	    || pekwm::ascii_ncase_equal("WINDOW", name)) {
		_menu_map["ROOT"] =
			new ActionMenu(MENU_TYPE_ROOT, act, "", "RootMenu");
		_menu_map["WINDOW"] =
			new ActionMenu(MENU_TYPE_WINDOW, act, "", "WindowMenu");
	}
	return nullptr;
}

/**
 * Initial load of menu configuration.
 */
void
MenuHandler::createMenusLoadConfiguration(ActionHandler *act)
{
	// Load configuration, pass specific section to loading
	CfgParser menu_cfg(pekwm::configScriptPath());
	if (menu_cfg.parse(pekwm::config()->getMenuFile())
	    || menu_cfg.parse(std::string(SYSCONFDIR "/menu"))) {
		_cfg_files = menu_cfg.getCfgFiles();
		CfgParser::Entry *root_entry = menu_cfg.getEntryRoot();

		// Load standard menus
		menu_map_it it = _menu_map.begin();
		for (; it != _menu_map.end(); ++it) {
			CfgParser::Entry* section =
				root_entry->findSection(it->second->getName());
			it->second->reload(section);
		}

		// Load standalone menus
		reloadStandaloneMenus(act, menu_cfg.getEntryRoot());
	}
}

/**
 * (re)loads the menus in the menu configuration if the file has been
 * updated since last load.
 */
void
MenuHandler::reloadMenus(ActionHandler *act)
{
	std::string menu_file(pekwm::config()->getMenuFile());
	if (! _cfg_files.requireReload(menu_file)) {
		return;
	}

	CfgParser cfg(pekwm::configScriptPath());
	bool cfg_ok = loadMenuConfig(menu_file, cfg);
	CfgParser::Entry *root = cfg.getEntryRoot();

	// Update, delete standalone root menus, load decors on others
	menu_map_it it(_menu_map.begin());
	while (it != _menu_map.end()) {
		if (it->second->getMenuType() == MENU_TYPE_ROOT_STANDALONE) {
			delete it->second;
			_menu_map.erase(it++);
			continue;
		} else if (cfg_ok) {
			// Only reload the menu if we got a ok configuration
			CfgParser::Entry* section =
				root->findSection(it->second->getName());
			it->second->reload(section);
		}
		++it;
	}

	// Update standalone root menus (name != ROOTMENU)
	reloadStandaloneMenus(act, root);
}

/**
 * Load menu configuration from menu_file resetting menu state.
 */
bool
MenuHandler::loadMenuConfig(const std::string &menu_file, CfgParser &menu_cfg)
{
	bool cfg_ok = true;

	if (! menu_cfg.parse(menu_file)) {
		if (! menu_cfg.parse(std::string(SYSCONFDIR "/menu"))) {
			cfg_ok = false;
		}
	}

	// Make sure menu is reloaded next time as content is dynamically
	// generated from the configuration file.
	if (! cfg_ok || menu_cfg.isDynamicContent()) {
		_cfg_files.clear();
	} else {
		_cfg_files = menu_cfg.getCfgFiles();
	}

	return cfg_ok;
}

/**
 * Updates standalone root menus
 */
void
MenuHandler::reloadStandaloneMenus(ActionHandler *act,
				   CfgParser::Entry *section)
{
	// Go through all but reserved section names and create menus
	CfgParser::Entry::entry_cit it(section->begin());
	for (; it != section->end(); ++it) {
		// Uppercase name
		std::string menu_name_upper = (*it)->getName();
		Util::to_upper(menu_name_upper);

		// Create new menu if the name is not used
		if (! getMenu(menu_name_upper)) {
			// Create, parse and add to map
			ActionMenu *menu =
				new ActionMenu(MENU_TYPE_ROOT_STANDALONE, act,
					       "", (*it)->getName());
			menu->reload((*it)->getSection());
			_menu_map[menu_name_upper] = menu;
		}
	}
}

/**
 * Clears the menu map and frees up resources used by menus
 */
void
MenuHandler::deleteMenus(void)
{
	menu_map_it it = _menu_map.begin();
	for (; it != _menu_map.end(); ++it) {
		delete it->second;
	}
	_menu_map.clear();
}

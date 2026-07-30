// NOTE: this file is auto-generated, do not edit

#ifndef _PEKWM_TK_NAMES_HH_
#define _PEKWM_TK_NAMES_HH_

#include <string>
#include <vector>

#include "TkNames.hh"
#include "Action.hh"
#include "ColorPalette.hh"
#include "PFont.hh"

namespace pekwm {

ActionAccess str_to_action_access(const std::string &str);
const char *action_access_to_str(ActionAccess value);
void get_action_access_names(std::vector<const char*> &names);
ActionType str_to_action_type(const std::string &str);
const char *action_type_to_str(ActionType value);
void get_action_type_names(std::vector<const char*> &names);
ActionState str_to_action_state(const std::string &str);
const char *action_state_to_str(ActionState value);
void get_action_state_names(std::vector<const char*> &names);
MoveResize str_to_move_resize(const std::string &str);
const char *move_resize_to_str(MoveResize value);
void get_move_resize_names(std::vector<const char*> &names);
Input str_to_input(const std::string &str);
const char *input_to_str(Input value);
void get_input_names(std::vector<const char*> &names);
ColorPaletteMode str_to_color_palette_mode(const std::string &str);
const char *color_palette_mode_to_str(ColorPaletteMode value);
void get_color_palette_mode_names(std::vector<const char*> &names);
BaseColor str_to_base_color(const std::string &str);
const char *base_color_to_str(BaseColor value);
void get_base_color_names(std::vector<const char*> &names);
FontType str_to_font_type(const std::string &str);
const char *font_type_to_str(FontType value);
void get_font_type_names(std::vector<const char*> &names);
bool tk_enum_name_to_names(const char *name, std::vector<const char*> &names);

};

#endif // _PEKWM__TK_NAMES_HH_

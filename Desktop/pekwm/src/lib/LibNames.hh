// NOTE: this file is auto-generated, do not edit

#ifndef _PEKWM_LIB_NAMES_HH_
#define _PEKWM_LIB_NAMES_HH_

#include <string>
#include <vector>

#include "LibNames.hh"
#include "pekwm_types.hh"
#include "Debug.hh"
#include "X11.hh"

namespace pekwm {

NetWmSource str_to_net_wm_source(const std::string &str);
const char *net_wm_source_to_str(NetWmSource value);
void get_net_wm_source_names(std::vector<const char*> &names);
Layer str_to_layer(const std::string &str);
const char *layer_to_str(Layer value);
void get_layer_names(std::vector<const char*> &names);
ApplyOn str_to_apply_on(const std::string &str);
const char *apply_on_to_str(ApplyOn value);
void get_apply_on_names(std::vector<const char*> &names);
Skip str_to_skip(const std::string &str);
const char *skip_to_str(Skip value);
void get_skip_names(std::vector<const char*> &names);
Border str_to_border(const std::string &str);
const char *border_to_str(Border value);
void get_border_names(std::vector<const char*> &names);
ImageType str_to_image_type(const std::string &str);
const char *image_type_to_str(ImageType value);
void get_image_type_names(std::vector<const char*> &names);
FontJustify str_to_font_justify(const std::string &str);
const char *font_justify_to_str(FontJustify value);
void get_font_justify_names(std::vector<const char*> &names);
HarbourPlacement str_to_harbour_placement(const std::string &str);
const char *harbour_placement_to_str(HarbourPlacement value);
void get_harbour_placement_names(std::vector<const char*> &names);
WorkspaceChange str_to_workspace_change(const std::string &str);
const char *workspace_change_to_str(WorkspaceChange value);
void get_workspace_change_names(std::vector<const char*> &names);
Edge str_to_edge(const std::string &str);
const char *edge_to_str(Edge value);
void get_edge_names(std::vector<const char*> &names);
Raise str_to_raise(const std::string &str);
const char *raise_to_str(Raise value);
void get_raise_names(std::vector<const char*> &names);
Orientation str_to_orientation(const std::string &str);
const char *orientation_to_str(Orientation value);
void get_orientation_names(std::vector<const char*> &names);
MouseEvent str_to_mouse_event(const std::string &str);
const char *mouse_event_to_str(MouseEvent value);
void get_mouse_event_names(std::vector<const char*> &names);
Mod str_to_mod(const std::string &str);
const char *mod_to_str(Mod value);
void get_mod_names(std::vector<const char*> &names);
CfgDeny str_to_cfg_deny(const std::string &str);
const char *cfg_deny_to_str(CfgDeny value);
void get_cfg_deny_names(std::vector<const char*> &names);
CurrHeadSelector str_to_curr_head_selector(const std::string &str);
const char *curr_head_selector_to_str(CurrHeadSelector value);
void get_curr_head_selector_names(std::vector<const char*> &names);
FocusSelector str_to_focus_selector(const std::string &str);
const char *focus_selector_to_str(FocusSelector value);
void get_focus_selector_names(std::vector<const char*> &names);
OnCloseFocusRaise str_to_on_close_focus_raise(const std::string &str);
const char *on_close_focus_raise_to_str(OnCloseFocusRaise value);
void get_on_close_focus_raise_names(std::vector<const char*> &names);
WarpOn str_to_warp_on(const std::string &str);
const char *warp_on_to_str(WarpOn value);
void get_warp_on_names(std::vector<const char*> &names);
DebugLevel str_to_debug_level(const std::string &str);
const char *debug_level_to_str(DebugLevel value);
void get_debug_level_names(std::vector<const char*> &names);
Direction str_to_direction(const std::string &str);
const char *direction_to_str(Direction value);
void get_direction_names(std::vector<const char*> &names);
bool lib_enum_name_to_names(const char *name, std::vector<const char*> &names);

};

#endif // _PEKWM__LIB_NAMES_HH_

// NOTE: this file is auto-generated, do not edit

#ifndef _PEKWM_WM_NAMES_HH_
#define _PEKWM_WM_NAMES_HH_

#include <string>
#include <vector>

#include "WmNames.hh"
#include "AutoProperties.hh"
#include "WinLayouter.hh"

namespace pekwm {

PropertyType str_to_property_type(const std::string &str);
const char *property_type_to_str(PropertyType value);
void get_property_type_names(std::vector<const char*> &names);
PropertyGroup str_to_property_group(const std::string &str);
const char *property_group_to_str(PropertyGroup value);
void get_property_group_names(std::vector<const char*> &names);
WinLayouterType str_to_win_layouter_type(const std::string &str);
const char *win_layouter_type_to_str(WinLayouterType value);
void get_win_layouter_type_names(std::vector<const char*> &names);
bool wm_enum_name_to_names(const char *name, std::vector<const char*> &names);

};

#endif // _PEKWM__WM_NAMES_HH_

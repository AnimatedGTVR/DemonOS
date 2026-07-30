//
// ThemedX11App.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_THEMED_X11APP_HH_
#define _PEKWM_THEMED_X11APP_HH_

#include "FontHandler.hh"
#include "ImageHandler.hh"
#include "TextureHandler.hh"

#include <string>

/**
 * Base for X11 applications using theme data.
 */
class ThemedX11App {
public:
	static void init(const std::string &config_file,
			 std::string &theme_dir, std::string &theme_variant,
			 FontHandler **font_handler,
			 ImageHandler **image_handler,
			 TextureHandler **texture_handler);
	static void cleanup(FontHandler *font_handler,
			    ImageHandler *image_handler,
			    TextureHandler *texture_handler);
};

#endif // _PEKWM_THEMED_X11APP_HH_

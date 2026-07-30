//
// ThemedX11App.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "CfgParser.hh"
#include "CfgUtil.hh"
#include "ThemedX11App.hh"

void
ThemedX11App::init(const std::string &config_file,
		   std::string &theme_dir, std::string &theme_variant,
		   FontHandler **font_handler,
		   ImageHandler **image_handler,
		   TextureHandler **texture_handler)
{
	float scale;
	bool font_default_x11;
	std::string font_charset_override;
	CfgParser cfg(CfgParserOpt(""));
	cfg.parse(config_file, CfgParserSource::SOURCE_FILE, true);
	std::string theme_path;
	CfgUtil::getScreenScale(cfg.getEntryRoot(), scale);
	CfgUtil::getThemeDir(cfg.getEntryRoot(),
			     theme_dir, theme_variant, theme_path);
	CfgUtil::getFontSettings(cfg.getEntryRoot(),
				 font_default_x11,
				 font_charset_override);

	*font_handler = new FontHandler(scale, font_default_x11,
					font_charset_override);
	*image_handler = new ImageHandler(scale);
	*texture_handler = new TextureHandler(scale);
}

void
ThemedX11App::cleanup(FontHandler *font_handler,
		      ImageHandler *image_handler,
		      TextureHandler *texture_handler)
{
	delete texture_handler;
	delete image_handler;
	delete font_handler;
}


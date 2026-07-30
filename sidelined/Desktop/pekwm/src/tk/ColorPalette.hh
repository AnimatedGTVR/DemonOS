//
// ColorPalette.hh for pekwm
// Copyright (C) 2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_COLOR_PALETTE_HH_
#define _PEKWM_COLOR_PALETTE_HH_

#include "config.h"
#include "Compat.hh"
#include "Types.hh"
#include "X11.hh"

#include <vector>
#include <string>

enum ColorPaletteMode { // Map
	COLOR_PALETTE_MODE_SINGLE,
	COLOR_PALETTE_MODE_COMPLEMENTARY,
	COLOR_PALETTE_MODE_TRIAD,
	COLOR_PALETTE_MODE_ANALOGOUS,
	COLOR_PALETTE_MODE_SPLIT,
	COLOR_PALETTE_MODE_TETRAD,
	COLOR_PALETTE_MODE_SQUARE,
	COLOR_PALETTE_MODE_NO
};

enum BaseColor { // Map
	BASE_COLOR_RED,
	BASE_COLOR_RED_PURPLE,
	BASE_COLOR_PURPLE,
	BASE_COLOR_BLUE_PURPLE,
	BASE_COLOR_BLUE,
	BASE_COLOR_BLUE_GREEN,
	BASE_COLOR_GREEN,
	BASE_COLOR_YELLOW_GREEN,
	BASE_COLOR_YELLOW,
	BASE_COLOR_YELLOW_ORANGE,
	BASE_COLOR_ORANGE,
	BASE_COLOR_RED_ORANGE,
	BASE_COLOR_NO
};

namespace ColorPalette {

const uint MAX_INTENSITY = 4;

bool getColors(ColorPaletteMode mode, BaseColor base, uint intensity,
	       float brightness, std::vector<XColor*> &colors);
bool getColors(ColorPaletteMode mode, BaseColor base, uint intensity,
	       float brightness, std::vector<std::string> &colors);

};

#endif // _PEKWM_COLOR_PALETTE_HH_

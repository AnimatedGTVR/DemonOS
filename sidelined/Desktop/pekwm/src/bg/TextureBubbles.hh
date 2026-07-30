//
// TextureBubbles.hh for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _TEXTURE_BUBBLES_HH_
#define _TEXTURE_BUBBLES_HH_

#include <cmath>

#include "../tk/Color.hh"
#include "../tk/PTexture.hh"

class Random
{
public:
	Random(uint seed)
		: _value(seed)
	{
	}

	int next(int range)
	{
		_value = (_value * MULTIPLIER + INCREMENT ) & 0xFFFFFF;
		float value = static_cast<float>(_value) / 16777216.0;
		return static_cast<int>(value * (range + 1));
	};

private:
	uint _value;

	static const uint INCREMENT = 12820163;
	static const uint MULTIPLIER = 1140671485;
};

/**
 * Render filled circles at "random" position and sizes, set by a random seed. 
 *
 * -----------------
 * | )   (  )      |
 * |)   (    )     |
 * |     (  )    ( |
 * | ( )        (  |
 * -----------------
 *
 */
class TextureBubbles : public PTexture {
public:
	TextureBubbles(int seed, uint num,
		       std::vector<std::string>::const_iterator cbegin,
		       std::vector<std::string>::const_iterator cend)
		: _seed(seed),
		  _num(num)
	{
		setColors(cbegin, cend);
	}

	virtual ~TextureBubbles()
	{
	}

	virtual void doRender(Render &rend,
			      int x, int y, size_t width, size_t height)
	{
		Random rand(_seed);

		rend.setColor(_colors[0]->pixel);
		rend.fill(x, y, width, height);

		int max_size = ((width > height) ? height : width) / 2;
		size_t color = 0;
		for (uint i = 0; i < _num; i++) {
			if (++color >= _colors.size()) {
				color = 1;
			}
			rend.setColor(_colors[color]->pixel);

			int cx = x + rand.next(width);
			int cy = y + rand.next(height);
			int diameter = rand.next(max_size);
			rend.circle(cx, cy, diameter, true);
		}
	}

	virtual bool getPixel(ulong &pixel) const
	{
		return false;
	}

private:
	void setColors(std::vector<std::string>::const_iterator cbegin,
		       std::vector<std::string>::const_iterator cend)
	{
		unsetColors();

		std::vector<std::string>::const_iterator it(cbegin);
		for (; it != cend; ++it) {
			_colors.push_back(pekwm::getColor(*it));
		}
		_ok = ! _colors.empty();
	}

	void unsetColors()
	{
		std::vector<XColor*>::iterator it(_colors.begin());
		for (; it != _colors.end(); ++it) {
			X11::returnColor(*it);
		}
		_colors.clear();
	}

	int _seed;
	uint _num;
	std::vector<XColor*> _colors;
};

PTexture*
parseBubbles(float scale, const std::string &,
	     const std::vector<std::string> &tok)
{
	// seed number background color1 [colorN]
	if (tok.size() < 4) {
		return nullptr;
	}

	int seed = std::stoi(tok[0]);
	uint num = std::stoi(tok[1]);

	std::vector<std::string>::const_iterator cend(tok.end());
	uint width = 0;
	uint height = 0;
	if (X11::parseSize(tok.back(), width, height)) {
		--cend;
	}

	PTexture *tex = new TextureBubbles(seed, num, tok.begin() + 2, cend);
	return tex;
}

#endif // _TEXTURE_BUBBLES_HH_

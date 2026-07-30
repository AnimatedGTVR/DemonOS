//
// TkImage.hh for pekwm
// Copyright (C) 2023 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_TK_IMAGE_HH_
#define _PEKWM_TK_IMAGE_HH_

#include "TkWidget.hh"
#include "PImage.hh"

class TkImage : public TkWidget {
public:
	TkImage(Theme::DialogData* data, PWinObj& parent, PImage* image)
		: TkWidget(data, parent),
		  _image(image)
	{
	}
	virtual ~TkImage() { }

	virtual uint widthReq()
	{
		return _image ? _image->getWidth() : 0;
	}

	virtual uint heightReq(uint width)
	{
		if (! _image) {
			return 0;
		}
		if (_image->getWidth() > width) {
			float aspect = float(_image->getWidth())
					     / _image->getHeight();
			return static_cast<uint>(width / aspect);
		}
		return _image->getHeight();
	}

	virtual void render(Render &rend, PSurface &surface)
	{
		TkWidget::render(rend, surface);

		if (_image && _image->getWidth() > _gm.width) {
			float aspect = float(_image->getWidth())
					     / _image->getHeight();
			_image->draw(rend, &surface, _gm.x, _gm.y,
				     _gm.width,
				     static_cast<uint>(_gm.width / aspect));
		} else if (_image) {
			// render image centered on available width
			uint x = _gm.x + (_gm.width - _image->getWidth()) / 2;
			_image->draw(rend, &surface, x, _gm.y,
				     _image->getWidth(),
				     _image->getHeight());
		}
	}

private:
	PImage *_image;
};

#endif // _PEKWM_TK_IMAGE_HH_

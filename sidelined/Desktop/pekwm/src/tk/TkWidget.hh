//
// TkWidget.hh for pekwm
// Copyright (C) 2023-2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_TK_WIDGET_HH_
#define _PEKWM_TK_WIDGET_HH_

#include "Types.hh"
#include "X11.hh"
#include "pekwm_types.hh"

#include "PSurface.hh"
#include "Render.hh"
#include "PWinObj.hh"
#include "Theme.hh"

typedef void(*stop_fun)(int);

class TkWidget {
public:
	virtual ~TkWidget()
	{
		if (_window != None) {
			X11::destroyWindow(_window);
		}
	}

	int getX() const { return _gm.x; }
	int getY() const { return _gm.y; }
	uint getHeight(void) const { return _gm.height; }
	virtual void setHeight(uint height) { _gm.height = height; }

	virtual bool setState(Window, ButtonState) {
		return false;
	}
	virtual bool click(Window) { return false; }
	virtual void render(Render &rend, PSurface&) {
		rend.clear(_render_gm.x, _render_gm.y,
			   _render_gm.width, _render_gm.height);
		_render_gm = _gm;
		_dirty = false;
	}
	virtual void renderDirty(Render &rend, PSurface &surface) {
		if (_dirty) {
			render(rend, surface);
		}
	}
	virtual void expose(const Geometry &area) {
		if (! _dirty && _gm.isOverlap(area)) {
			_dirty = true;
		}
	}
	virtual bool isDirty() const { return _dirty; }

	virtual void center(uint width, uint height)
	{
		uint width_req = widthReq();
		uint height_req = heightReq(width_req);
		int x = (width - width_req) / 2;
		int y = (height - height_req) / 2;
		place(x, y, width_req, height_req);
	}

	virtual void place(int x, int y, uint width, uint)
	{
		Geometry new_gm(x, y, width, heightReq(width));
		if (_gm != new_gm) {
			_gm = new_gm;
			_dirty = true;
		}
	}

	/**
	 * Get requested width, 0 means adapt to given width.
	 */
	virtual uint widthReq() { return 0; }

	/**
	 * Get requested height, given the provided width.
	 */
	virtual uint heightReq(uint width) = 0;

protected:
	TkWidget(Theme::DialogData* data, PWinObj &parent)
		: _data(data),
		  _window(None),
		  _parent(parent),
		  _width_req(0),
		  _dirty(true)
	{
	}

	void setWindow(Window window) { _window = window; }

	Theme::DialogData *_data;
	Window _window;
	PWinObj &_parent;
	/** Widget geometry relative to dialog window */
	Geometry _gm;
	/** Widget geometry at the time of last render. */
	Geometry _render_gm;
	/** Requested width. */
	uint _width_req;
	/** Set to true if widget updated since last render. */
	bool _dirty;
};

#endif // _PEKWM_TK_WIDGET_HH_

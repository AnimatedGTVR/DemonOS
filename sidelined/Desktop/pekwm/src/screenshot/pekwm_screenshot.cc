//
// pekwm_screenshot.cc for pekwm
// Copyright (C) 2021-2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Compat.hh"
#include "Util.hh"
#include "X11.hh"

#include "../tk/ImageHandler.hh"
#include "../tk/PImageLoaderPng.hh"

#include <iostream>
#include <iomanip>
#include <sstream>

extern "C" {
#include <sys/types.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xutil.h>
}

static ImageHandler* _image_handler = nullptr;

namespace pekwm
{
	ImageHandler* imageHandler()
	{
		return _image_handler;
	}
}

static void init(Display* dpy)
{
	_image_handler = new ImageHandler(1.0);
}

static void cleanup()
{
	delete _image_handler;
}

static void usage(const char* name, int ret)
{
	std::cout << "usage: " << name << " [-dhw] [screenshot.png]"
		  << std::endl;
	std::cout << "  -d --display dpy    Display" << std::endl;
	std::cout << "  -h --help           Display this information"
		  << std::endl;
	std::cout << "  -w --wait seconds   Wait seconds before taking "
		     "screenshot" << std::endl;
	exit(ret);
}

static std::string get_screenhot_name(const Geometry& gm)
{
	time_t t = time(nullptr);
	tm tm;
	localtime_r(&t, &tm);

	std::ostringstream name;
	name << "pekwm_screenshot-";
	name << std::put_time(&tm, "%Y%m%dT%H%M%S");
	name << "-" << gm.width << "x" << gm.height;
	name << ".png";
	return name.str();
}

static bool save_screenshot_and_free(const std::string& output, XImage *ximage)
{
	PImage image(ximage);
	X11::destroyImage(ximage);
	return PImageLoaderPng::save(output, image.getData(),
				     image.getWidth(), image.getHeight());
}

static int take_screenshot(const std::string& output, const Geometry &gm)
{
	XImage *ximage = X11::getImage(X11::getRoot(),
				       gm.x, gm.y, gm.width, gm.height,
				       AllPlanes, ZPixmap);
	if (ximage == nullptr) {
		std::cerr << "Failed to take a screenshot" << std::endl;
		return 1;
	}
	return save_screenshot_and_free(output, ximage) ? 0 : 1;
}

static int take_screenshot(const std::string& output)
{
	Geometry gm = X11::getScreenGeometry();
	return take_screenshot(output, gm);
}

static void update_region(const Geometry &start_gm, Geometry &gm,
			  int x, int y)
{
	gm = start_gm;
	if (gm.x > x) {
		gm.width = gm.x - x;
		gm.x = x;
	} else {
		gm.width = x - gm.x;
	}
	if (gm.y > y) {
		gm.height = gm.y - y;
		gm.y = y;
	} else {
		gm.height = y - gm.y;
	}
}

static int take_region(const std::string& output)
{
	XGCValues gv;
	gv.function = GXinvert;
	gv.subwindow_mode = IncludeInferiors;
	gv.line_width = 1;
	ulong gv_mask = GCFunction|GCSubwindowMode|GCLineWidth;
	X11_GC gc(X11::getRoot(), gv_mask, &gv);

	X11::grabPointer(X11::getRoot(),
			 ButtonPressMask|ButtonReleaseMask|ButtonMotionMask,
			 CURSOR_CROSS, false);
	X11::grabServer();
	XEvent ev;
	bool pressed = false;
	Geometry gm, start_gm;
	while (X11::getNextEvent(ev)) {
		if (ev.type == ButtonPress) {
			pressed = true;
			start_gm.x = ev.xbutton.x_root;
			start_gm.y = ev.xbutton.y_root;
		} else if (pressed && ev.type == ButtonRelease) {
			X11::drawRectangle(X11::getRoot(), *gc, gm);
			update_region(start_gm, gm,
				      ev.xbutton.x_root,
				      ev.xbutton.y_root);
			break;
		} else if (pressed && ev.type == MotionNotify) {
			X11::drawRectangle(X11::getRoot(), *gc, gm);
			update_region(start_gm, gm,
				      ev.xbutton.x_root,
				      ev.xbutton.y_root);
			X11::drawRectangle(X11::getRoot(), *gc, gm);
		}
	}
	X11::ungrabServer(true);

	return take_screenshot(output, gm);
}

int main(int argc, char* argv[])
{
	// Limit access, limit further after X11 connection is setup.
	pledge_x11_required("");

	const char* display = NULL;
	bool region = false;
	int wait_seconds = 0;

	static struct option opts[] = {
		{const_cast<char*>("display"), required_argument, nullptr,
		 'd'},
		{const_cast<char*>("help"), no_argument, nullptr, 'h'},
		{const_cast<char*>("region"), no_argument, nullptr, 'r'},
		{const_cast<char*>("wait"), required_argument, nullptr, 'w'},
		{nullptr, 0, nullptr, 0}
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "d:hrw:", opts, nullptr)) != -1) {
		switch (ch) {
		case 'd':
			display = optarg;
			break;
		case 'h':
			usage(argv[0], 0);
			break;
		case 'r':
			if (wait_seconds) {
				usage(argv[0], 1);
			}
			region = true;
			break;
		case 'w':
			if (region) {
				usage(argv[0], 1);
			}
			try {
				wait_seconds = std::stoi(optarg);
			} catch (std::invalid_argument&) {
				usage(argv[0], 1);
			}
			break;
		default:
			usage(argv[0], 1);
			break;
		}
	}

	if (! X11::init(display, std::cerr)) {
		return 1;
	}

	// X11 connection has been setup, limit access further
	pledge_x("stdio rpath wpath cpath", "");

	init(X11::getDpy());

	std::string output;
	if (optind < argc) {
		output = argv[optind];
	} else {
		output = get_screenhot_name(X11::getScreenGeometry());
	}

	if (wait_seconds > 0) {
		if (wait_seconds > 3) {
			sleep(wait_seconds - 3);
			wait_seconds = 3;
		}
		for (int i = wait_seconds; i > 0; i--) {
			std::cout << i << "..." << std::flush;
			sleep(1);
		}
		std::cout << std::endl;
	}

	int ret;
	if (region) {
		ret = take_region(output);
	} else {
		ret = take_screenshot(output);
	}
	if (ret) {
		std::cerr << "failed to write screenshot to " << output
			  << std::endl;
	} else {
		std::cout << "screenshot written to " << output << std::endl;
	}

	cleanup();
	X11::destruct();

	return ret;
}

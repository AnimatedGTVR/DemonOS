/**
 * Client used for testing pekwm.
 */

#include <iostream>

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <unistd.h>
}

#include "test_util.hh"

void
query_pointer(Display *dpy, int screen, Window root)
{
	Window root_ret, child_ret;
	int root_x, root_y, win_x, win_y;
	unsigned int mask;
	if (XQueryPointer(dpy, root, &root_ret, &child_ret,
			  &root_x, &root_y, &win_x, &win_y,
			  &mask)) {
		std::cout << "root"
			  << " x " << root_x << " y " << root_y << std::endl;
		std::cout << "child " << child_ret
			  << " x " << win_x << " y " << win_y << std::endl;
	} else {
		std::cerr << "ERROR: failed to query pointer" << std::endl;
	}
}

void
visual_info(Display *dpy, int screen)
{
	Visual *visual = DefaultVisual(dpy, screen);
	std::cout << "DefaultVisual" << std::endl;
	std::cout << std::endl;
	std::cout << "id " << visual->visualid << std::endl;
	std::cout << "class " << visual->c_class << std::endl;
	std::cout << "red_mask " << visual->red_mask << std::endl;
	std::cout << "green_mask " << visual->green_mask << std::endl;
	std::cout << "blue_mask " << visual->blue_mask << std::endl;
	std::cout << "bits_per_rgb " << visual->bits_per_rgb << std::endl;
	std::cout << "map_entries " << visual->map_entries << std::endl;
}

void
pixmap_formats(Display *dpy)
{
	int num = 0;
	XPixmapFormatValues *xpfv = XListPixmapFormats(dpy, &num);
	if (xpfv) {
		for (int i = 0; i < num; i++) {
			std::cout << "Format " << i << std::endl;
			std::cout << "depth: " << xpfv[i].depth << std::endl;
			std::cout << "bits_per_pixel: "
				  << xpfv[i].bits_per_pixel << std::endl;
			std::cout << "scanline_pad: "
				  << xpfv[i].scanline_pad << std::endl;
			std::cout << std::endl;
		}
		XFree(xpfv);
	}
}

void
window(Display *dpy, int screen, Window root, const char *wm_name,
       long net_wm_user_time)
{
	XSetWindowAttributes attrs = {0};
	attrs.event_mask = PropertyChangeMask;
	unsigned long attrs_mask = CWEventMask;

	Window win = XCreateWindow(dpy, root,
				   0, 0, 100, 100, 0,
				   CopyFromParent, //depth
				   InputOutput, // class
				   CopyFromParent, // visual
				   attrs_mask,
				   &attrs);

	if (net_wm_user_time >= 0) {
		Atom atom = XInternAtom(dpy, "_NET_WM_USER_TIME", False);
		XChangeProperty(dpy, win, atom, XA_CARDINAL, 32,
				PropModeReplace,
				reinterpret_cast<unsigned char*>(
					&net_wm_user_time),
				1);
		std::cout << "_NET_WM_USER_TIME set to " << net_wm_user_time
			  << std::endl;
	}

	char wm_class[] = "pekwm";
	XClassHint hint = {const_cast<char*>(wm_name), wm_class};
	XSetClassHint(dpy, win, &hint);
	XMapWindow(dpy, win);

	std::cout << "Window " << win << std::endl;
	std::cout << "WindowHex " << std::showbase << std::hex << win
		  << std::endl;

	XEvent ev;
	while (next_event(dpy, &ev)) {
	}
}

int
main(int argc, char *argv[])
{
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		std::cerr << "ERROR: unable to open display" << std::endl;
		return 1;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);

	char ch;
	const char *name = "test_client";
	while ((ch = getopt(argc, argv, "n:")) != -1) {
		switch (ch) {
		case 'n':
			name = optarg;
			break;
		default:
			break;
		}
	}

	argc -= optind;
	argv += optind;
	std::string arg2 = argc > 0 ? argv[0] : "";
	if (arg2 == "query_pointer") {
		query_pointer(dpy, screen, root);
	} else if (arg2 == "visual_info") {
		visual_info(dpy, screen);
	} else if (arg2 == "pixmap_formats") {
		pixmap_formats(dpy);
	} else if (argc == 2 && arg2 == "net_wm_user_time") {
		long net_wm_user_time = atoi(argv[1]);
		window(dpy, screen, root, name, net_wm_user_time);
	} else {
		window(dpy, screen, root, name, -1);
	}

	XCloseDisplay(dpy);

	return 0;
}

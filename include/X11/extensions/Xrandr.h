#ifndef DEMONOS_X11_EXTENSIONS_XRANDR_H
#define DEMONOS_X11_EXTENSIONS_XRANDR_H

#include <X11/Xlib.h>

typedef XID RROutput;
typedef XID RRCrtc;
typedef XID RRMode;
typedef unsigned short Rotation;
typedef struct { RRMode id; unsigned int width, height; char *name; } XRRModeInfo;
typedef struct {
    int ncrtc, noutput, nmode;
    RRCrtc *crtcs;
    RROutput *outputs;
    XRRModeInfo *modes;
} XRRScreenResources;
typedef struct {
    int x, y;
    unsigned int width, height;
    RRMode mode;
    Rotation rotation, rotations;
    int noutput;
    RROutput *outputs;
} XRRCrtcInfo;
typedef struct {
    char *name;
    int nameLen;
    int connection;
    int ncrtc, nmode;
    RRCrtc *crtcs;
    RRMode *modes;
} XRROutputInfo;

#define RR_Connected 0
#define RR_Rotate_0 1

Bool XRRQueryExtension(Display *, int *, int *);
Status XRRQueryVersion(Display *, int *, int *);
XRRScreenResources *XRRGetScreenResources(Display *, Window);
XRRCrtcInfo *XRRGetCrtcInfo(Display *, XRRScreenResources *, RRCrtc);
XRROutputInfo *XRRGetOutputInfo(Display *, XRRScreenResources *, RROutput);
void XRRFreeCrtcInfo(XRRCrtcInfo *);
void XRRFreeOutputInfo(XRROutputInfo *);
void XRRFreeScreenResources(XRRScreenResources *);
Status XRRSetCrtcConfig(Display *, XRRScreenResources *, RRCrtc,
                        unsigned long, int, int, RRMode, Rotation,
                        RROutput *, int);
void XRRSelectInput(Display *, Window, int);
int XRRUpdateConfiguration(XEvent *);
RROutput XRRGetOutputPrimary(Display *, Window);
int XRRGetOutputProperty(Display *, RROutput, Atom, long, long, Bool, Bool,
                         Atom, Atom *, int *, unsigned long *,
                         unsigned long *, unsigned char **);
void XRRSetScreenSize(Display *, Window, int, int, int, int);

#endif

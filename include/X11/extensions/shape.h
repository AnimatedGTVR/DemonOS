#ifndef DEMONOS_X11_EXTENSIONS_SHAPE_H
#define DEMONOS_X11_EXTENSIONS_SHAPE_H

#include <X11/Xlib.h>

Bool XShapeQueryExtension(Display *, int *, int *);
void XShapeCombineRectangles(Display *, Window, int, int, int,
                             XRectangle *, int, int, int);
void XShapeCombineShape(Display *, Window, int, int, int, Window, int, int);
void XShapeCombineMask(Display *, Window, int, int, int, Pixmap, int);
Status XShapeQueryExtents(Display *, Window, Bool *, int *, int *,
                          unsigned int *, unsigned int *, Bool *, int *, int *,
                          unsigned int *, unsigned int *);
void XShapeSelectInput(Display *, Window, unsigned long);

#endif

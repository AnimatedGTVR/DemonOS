/*
Freestanding shadow of <math.h> for the C++ engine units. libstdc++'s real
<math.h>/<cmath> refuses to compile under -ffreestanding (it requires a
hosted C++ standard library). NXEngine's engine units only need a handful
of libm function declarations, backed by the same freestanding
implementations (ports/quake/platform/libm_demonos.c) the Quake port
already uses -- so shadow the system header with just those declarations
instead of dragging in libstdc++'s machinery.
*/
#ifndef NXENGINE_DEMONOS_MATH_H
#define NXENGINE_DEMONOS_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double fabs(double x);
double atan(double x);
double atan2(double y, double x);
double pow(double x, double y);

#ifdef __cplusplus
}
#endif

#endif

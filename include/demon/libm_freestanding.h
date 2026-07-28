#ifndef DEMON_LIBM_FREESTANDING_H
#define DEMON_LIBM_FREESTANDING_H

// This kernel's user apps build with -nostdlib -ffreestanding: no libc, no
// libm. EDE's ede-calc engine (Wave 1 of the EDE port, see
// Desktop/EDE/ede-2.1/ede-calc/SciCalc.cpp) needs sin/cos/log/exp/pow/sqrt
// for its scientific functions, so this is the minimal set those call
// sites actually use, implemented from scratch with Newton/Taylor-series
// methods. Not a general-purpose libm: no NaN/Inf handling, no edge-case
// hardening beyond what a calculator UI needs.
#ifdef __cplusplus
extern "C" {
#endif

#define M_PI 3.14159265358979323846

double fs_fabs(double x);
double fs_floor(double x);
double fs_copysign(double x, double y);
double fs_sqrt(double x);
double fs_exp(double x);
double fs_log(double x);
double fs_log10(double x);
double fs_pow(double base, double exponent);
double fs_sin(double x);
double fs_cos(double x);
double fs_tan(double x);
double fs_asin(double x);
double fs_acos(double x);
double fs_atan(double x);

#ifdef __cplusplus
}
#endif

#endif

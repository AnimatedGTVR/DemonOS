/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

libm_demonos.c -- freestanding subset of libm for the Quake port.

WinQuake's mathlib.c and zone.c call sin/cos/tan/sqrt/fabs/floor from
<math.h>.  The DemonOS user environment has no libm, so provide compact
single-precision implementations of just the symbols those units pull in.
*/

#include <stddef.h>
#include <stdint.h>

double fabs(double x)
{
	union { double d; uint64_t u; } v;
	v.d = x;
	v.u &= 0x7FFFFFFFFFFFFFFFULL;
	return v.d;
}

double floor(double x)
{
	double r = (double)(long long)x;
	if (x < 0.0 && r != x) r -= 1.0;
	return r;
}

double sqrt(double x)
{
	double r, half;
	union { double d; uint64_t u; } magic;

	if (x <= 0.0) return 0.0;

	half = 0.5 * x;
	magic.d = x;
	magic.u = 0x5FE6EB50C7B537A9ULL - (magic.u >> 1);
	r = magic.d;
	r = r * (1.5 - half * r * r);
	r = r * (1.5 - half * r * r);
	r = r * (1.5 - half * r * r);
	r = r * (1.5 - half * r * r);
	return r * x;
}

/* sin/cos/tan reduced to [-pi, pi], accurate to ~1e-9 for game angles. */

static double qsin_reduce(double x, int *neg)
{
	*neg = 0;
	while (x < -3.14159265358979323846) x += 6.28318530717958647692;
	while (x >  3.14159265358979323846) x -= 6.28318530717958647692;
	if (x < 0.0) { x = -x; *neg = 1; }
	if (x > 1.57079632679489661923)
		x = 3.14159265358979323846 - x;
	return x;
}

double sin(double x)
{
	double t, y;
	int neg;

	x = qsin_reduce(x, &neg);
	t = x * x;
	y = x * (1.0 + t * (-1.6666666666666666e-1
		+ t * (8.333333333333333e-3
		+ t * (-1.984126984126984e-4
		+ t * (2.755731922398589e-6
		+ t * (-2.505210838544172e-8
		+ t * 1.6059043836821613e-10))))));
	return neg ? -y : y;
}

double cos(double x)
{
	return sin(x + 1.57079632679489661923);
}

double tan(double x)
{
	return sin(x) / cos(x);
}

double ceil(double x)
{
	double r = (double)(long long)x;
	if (r < x) r += 1.0;
	return r;
}

/* atan/atan2 accurate to ~1e-5, plenty for game angles. */

double atan(double x)
{
	int sign = 0;
	double x2, poly, result;

	if (x < 0.0) { x = -x; sign = 1; }
	if (x > 1.0)
	{
		x = 1.0 / x;
		x2 = x * x;
		poly = x * (0.9998660 + x2 * (-0.3302995 +
			x2 * (0.1801410 + x2 * (-0.0851330 +
			x2 * 0.0208351))));
		result = 1.57079632679489661923 - poly;
	}
	else
	{
		x2 = x * x;
		result = x * (0.9998660 + x2 * (-0.3302995 +
			x2 * (0.1801410 + x2 * (-0.0851330 +
			x2 * 0.0208351))));
	}
	return sign ? -result : result;
}

double atan2(double y, double x)
{
	double a;

	if (x > 0.0)
		return atan(y / x);
	if (x < 0.0)
	{
		a = atan(y / x);
		return y >= 0.0 ? a + 3.14159265358979323846
				: a - 3.14159265358979323846;
	}
	if (y > 0.0)
		return 1.57079632679489661923;
	if (y < 0.0)
		return -1.57079632679489661923;
	return 0.0;
}

/* exp/ln used by pow below (the engine only calls pow for gamma ramps). */

static const double d_ln2 = 0.693147180559945309417232121458;

static double d_exp(double x)
{
	double n, r, p;
	union { double d; uint64_t u; } v;

	n = floor(x / d_ln2 + 0.5);
	r = x - n * d_ln2;
	p = 1.0 + r * (1.0 + r * (0.5 + r * (0.16666666666666666 +
		r * (0.041666666666666664 + r * (0.008333333333333333 +
		r * (0.001388888888888889 + r * (0.0001984126984126984 +
		r * (0.000024801587301587302 +
		r * 0.000002755731922398589))))))));
	v.d = p;
	v.u += ((uint64_t)(long long)n) << 52;
	return v.d;
}

static double d_ln(double x)
{
	double m, z, z2, s, term;
	union { double d; uint64_t u; } v;
	int e, k;

	if (x <= 0.0)
		return -1.0e300;

	v.d = x;
	e = (int)((v.u >> 52) & 0x7FFu) - 1023;
	v.u = (v.u & 0xFFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
	m = v.d;

	z = (m - 1.0) / (m + 1.0);
	z2 = z * z;
	s = z;
	term = z;
	for (k = 3; k <= 25; k += 2)
	{
		term *= z2;
		s += term / (double)k;
	}
	return (double)e * d_ln2 + 2.0 * s;
}

double pow(double x, double y)
{
	if (x <= 0.0)
		return 0.0;
	return d_exp(y * d_ln(x));
}

double atof(const char *str)
{
	double sign = 1.0, value = 0.0, frac = 0.1;
	int expo = 0, expsign = 1;

	if (str == NULL)
		return 0.0;
	while (*str == ' ' || *str == '\t') str++;
	if (*str == '-') { sign = -1.0; str++; }
	else if (*str == '+') str++;
	while (*str >= '0' && *str <= '9')
	{
		value = value * 10.0 + (double)(*str - '0');
		str++;
	}
	if (*str == '.')
	{
		str++;
		while (*str >= '0' && *str <= '9')
		{
			value += (double)(*str - '0') * frac;
			frac *= 0.1;
			str++;
		}
	}
	if (*str == 'e' || *str == 'E')
	{
		str++;
		if (*str == '-') { expsign = -1; str++; }
		else if (*str == '+') str++;
		while (*str >= '0' && *str <= '9')
		{
			expo = expo * 10 + (*str - '0');
			str++;
		}
	}
	expo *= expsign;
	while (expo > 0) { value *= 10.0; expo--; }
	while (expo < 0) { value *= 0.1; expo++; }
	return sign * value;
}

#include <demon/libm_freestanding.h>

#include <stdint.h>

typedef union { double d; uint64_t u; } fs_bits;

double fs_fabs(double x) {
    fs_bits c; c.d = x;
    c.u &= ~(1ULL << 63);
    return c.d;
}

double fs_copysign(double x, double y) {
    fs_bits cx; cx.d = x;
    fs_bits cy; cy.d = y;
    cx.u = (cx.u & ~(1ULL << 63)) | (cy.u & (1ULL << 63));
    return cx.d;
}

double fs_floor(double x) {
    long long i = (long long)x;
    double t = (double)i;
    if (t > x) t -= 1.0;
    return t;
}

double fs_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    fs_bits c; c.d = x;
    // Rough order-of-magnitude seed (2^floor(exponent/2)); Newton below
    // converges quadratically from there regardless of how coarse this is.
    int e = (int)((c.u >> 52) & 0x7FFu) - 1023;
    int half = e >> 1;
    fs_bits g; g.u = (uint64_t)(half + 1023) << 52;
    double guess = g.d > 0.0 ? g.d : 1.0;
    for (int i = 0; i < 60; ++i) guess = 0.5 * (guess + x / guess);
    return guess;
}

static double fs_scale_pow2(double m, int k) {
    fs_bits c; c.d = m;
    int e = (int)((c.u >> 52) & 0x7FFu) + k;
    if (e <= 0) return 0.0;
    if (e >= 0x7FF) e = 0x7FE;
    c.u = (c.u & ~(0x7FFULL << 52)) | ((uint64_t)e << 52);
    return c.d;
}

static double fs_reduce_mantissa(double x, int *exp2) {
    fs_bits c; c.d = x;
    *exp2 = (int)((c.u >> 52) & 0x7FFu) - 1023;
    c.u = (c.u & ~(0x7FFULL << 52)) | (1023ULL << 52);
    return c.d;
}

double fs_exp(double x) {
    if (x == 0.0) return 1.0;
    int reciprocal = 0;
    double ax = x;
    if (ax < 0.0) { ax = -ax; reciprocal = 1; }
    const double ln2 = 0.6931471805599453;
    long long k = (long long)(ax / ln2 + 0.5);
    double r = ax - (double)k * ln2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 25; ++i) {
        term *= r / (double)i;
        sum += term;
    }
    double result = fs_scale_pow2(sum, (int)k);
    return reciprocal ? 1.0 / result : result;
}

double fs_log(double x) {
    if (x <= 0.0) return 0.0;
    int e;
    double m = fs_reduce_mantissa(x, &e);
    const double sqrt2 = 1.4142135623730951;
    if (m > sqrt2) { m *= 0.5; e += 1; }
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double term = y, sum = 0.0;
    for (int i = 0; i < 20; ++i) {
        sum += term / (double)(2 * i + 1);
        term *= y2;
    }
    const double ln2 = 0.6931471805599453;
    return 2.0 * sum + (double)e * ln2;
}

double fs_log10(double x) {
    const double inv_ln10 = 0.4342944819032518;
    return fs_log(x) * inv_ln10;
}

double fs_pow(double base, double exponent) {
    if (exponent == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    if (base < 0.0) {
        double fy = fs_floor(exponent);
        if (fy != exponent) return 0.0; // domain error: non-integer power of a negative base
        long long yi = (long long)fy;
        double r = fs_exp(exponent * fs_log(-base));
        return (yi % 2 == 0) ? r : -r;
    }
    return fs_exp(exponent * fs_log(base));
}

static double fs_reduce_angle(double x) {
    const double two_pi = 6.283185307179586;
    double k = fs_floor(x / two_pi + 0.5);
    return x - k * two_pi;
}

double fs_sin(double x) {
    double r = fs_reduce_angle(x);
    double r2 = r * r, term = r, sum = r;
    for (int i = 1; i <= 20; ++i) {
        term *= -r2 / ((double)(2 * i) * (double)(2 * i + 1));
        sum += term;
    }
    return sum;
}

double fs_cos(double x) {
    double r = fs_reduce_angle(x);
    double r2 = r * r, term = 1.0, sum = 1.0;
    for (int i = 1; i <= 20; ++i) {
        term *= -r2 / ((double)(2 * i - 1) * (double)(2 * i));
        sum += term;
    }
    return sum;
}

double fs_tan(double x) {
    double c = fs_cos(x);
    return c == 0.0 ? 0.0 : fs_sin(x) / c;
}

double fs_atan(double x) {
    int negative = 0;
    if (x < 0.0) { x = -x; negative = 1; }
    int reciprocal = 0;
    if (x > 1.0) { x = 1.0 / x; reciprocal = 1; }
    // Halve the argument three times (atan(x) = 2*atan(x/(1+sqrt(1+x^2))))
    // so the Taylor series below only ever sees a small angle.
    double h = x;
    for (int i = 0; i < 3; ++i) h = h / (1.0 + fs_sqrt(1.0 + h * h));
    double y2 = h * h, term = h, sum = h;
    for (int i = 1; i <= 12; ++i) {
        term *= -y2;
        sum += term / (double)(2 * i + 1);
    }
    double result = sum * 8.0;
    if (reciprocal) result = M_PI / 2.0 - result;
    return negative ? -result : result;
}

double fs_asin(double x) {
    if (x >= 1.0) return M_PI / 2.0;
    if (x <= -1.0) return -M_PI / 2.0;
    return fs_atan(x / fs_sqrt(1.0 - x * x));
}

double fs_acos(double x) {
    return M_PI / 2.0 - fs_asin(x);
}

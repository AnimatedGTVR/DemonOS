#include "engine.h"

#include <demon/libm_freestanding.h>

typedef unsigned long size_type;

// --- Hand-rolled number formatting -----------------------------------
// The freestanding user CFLAGS give us no libc, so there is no sprintf.
// These mirror exactly the sprintf format strings SciCalc.cpp used
// ("%.Nf", "%.9g", "%x", "%d") closely enough for a calculator display.

static void append_char(char *buf, size_type *len, char c) {
    buf[*len] = c;
    ++*len;
    buf[*len] = 0;
}

static void append_str(char *buf, size_type *len, const char *s) {
    while (*s) append_char(buf, len, *s++);
}

static void append_int(char *buf, size_type *len, long long v) {
    if (v < 0) { append_char(buf, len, '-'); v = -v; }
    char tmp[24];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (t > 0) append_char(buf, len, tmp[--t]);
}

static void append_fixed(char *buf, size_type *len, double val, int decimals) {
    if (val < 0.0) { append_char(buf, len, '-'); val = -val; }
    double scale = 1.0;
    for (int i = 0; i < decimals; ++i) scale *= 10.0;
    long long total = (long long)(val * scale + 0.5);
    long long divisor = 1;
    for (int i = 0; i < decimals; ++i) divisor *= 10;
    append_int(buf, len, total / divisor);
    if (decimals > 0) {
        append_char(buf, len, '.');
        long long fracpart = total % divisor;
        char tmp[16];
        for (int i = decimals - 1; i >= 0; --i) {
            tmp[i] = (char)('0' + (int)(fracpart % 10));
            fracpart /= 10;
        }
        for (int i = 0; i < decimals; ++i) append_char(buf, len, tmp[i]);
    }
}

static void strip_trailing_zeros(char *buf, size_type *len) {
    while (*len > 0 && buf[*len - 1] == '0') --*len;
    if (*len > 0 && buf[*len - 1] == '.') --*len;
    buf[*len] = 0;
}

// Approximates "%.9g": fixed notation for a normal magnitude range,
// scientific notation (mantissa 'e' exponent) outside it, trailing zeros
// stripped either way -- behaviorally equivalent to what a calculator
// display needs, not a bit-exact reimplementation of glibc's %g.
static void append_general(char *buf, size_type *len, double val) {
    if (val == 0.0) { append_char(buf, len, '0'); return; }
    const bool negative = val < 0.0;
    const double magnitude = negative ? -val : val;
    int exp10 = (int)fs_floor(fs_log10(magnitude));
    if (exp10 >= -4 && exp10 < 9) {
        int decimals = 8 - exp10;
        if (decimals < 0) decimals = 0;
        if (decimals > 12) decimals = 12;
        char tmp[48];
        size_type tl = 0;
        append_fixed(tmp, &tl, val, decimals);
        if (decimals > 0) strip_trailing_zeros(tmp, &tl);
        append_str(buf, len, tmp);
    } else {
        double mantissa = magnitude / fs_pow(10.0, (double)exp10);
        if (mantissa >= 10.0) { mantissa /= 10.0; exp10 += 1; }
        char tmp[48];
        size_type tl = 0;
        append_fixed(tmp, &tl, mantissa, 8);
        strip_trailing_zeros(tmp, &tl);
        if (negative) append_char(buf, len, '-');
        append_str(buf, len, tmp);
        append_char(buf, len, 'e');
        append_int(buf, len, exp10);
    }
}

static double gammaln(double xx) {
    static const double cof[6] = {
        76.18009172947146,    -86.50532032941677,   24.01409824083091,
        -1.231739572450155,   0.1208650973866179e-2, -0.5395239384953e-5,
    };
    double y = xx, x = xx;
    double tmp = x + 5.5;
    tmp -= (x + 0.5) * fs_log(tmp);
    double ser = 1.000000000190015;
    for (int j = 0; j < 6; ++j) ser += cof[j] / ++y;
    return -tmp + fs_log(2.5066282746310005 * ser / x);
}

CalcEngine::CalcEngine() {
    for (int i = 0; i < 6; ++i) priority_[i] = 0;
    mem_ = 0.0;
    currentbrkt_ = 0;
    base_ = 10;
    drgmode_ = 0;
    inv_ = 0;
    init_value(0);
    mem_label_[0] = 0;
    brkt_label_[0] = 0;
    set_drgdisp();
    set_display(0.0, NORM);
}

void CalcEngine::init_value(int lev) {
    top_ = lev;
    value_[top_] = 0.0;
    ready_ = 0;
    emode_ = 0;
    dot_ = 0;
    diver_ = 1.0;
    behind_ = 0;
    if (inv_) inv_ = 0;
}

void CalcEngine::handle_number(double numb) {
    if (ready_) init_value(top_);

    if (numb == -1.0) { // DOT
        if (dot_) return;
        dot_ = 1;
        set_display(value_[top_], DOT);
        return;
    }

    if (emode_) {
        double sign = fs_copysign(1.0, (double)exponent_);
        int aexp = exponent_ < 0 ? -exponent_ : exponent_;
        if (aexp * 10 + (int)numb > 999) {
            int first = (int)fs_floor((double)aexp / 100.0);
            aexp -= 100 * first;
            exponent_ = aexp * (int)sign;
        }
        exponent_ = exponent_ * 10 + (int)(sign * numb);
        value_[top_] = mantissa_ * fs_pow(10.0, (double)exponent_);
        set_display(mantissa_, EXP);
    } else if (numb < base_) {
        double sign = fs_copysign(1.0, value_[top_]);
        if (dot_ && behind_ < 9) {
            behind_++;
            diver_ = diver_ / (double)base_;
            value_[top_] += sign * diver_ * numb;
        } else if (!dot_ && value_[top_] < 1.0e10) {
            value_[top_] = (double)base_ * value_[top_] + sign * numb;
        }
        set_display(value_[top_], behind_);
    }
}

void CalcEngine::handle_operator(Operator kind) {
    switch (kind) {
        case PLUS: case MINUS: case MULT: case DIV: case POW: case INVPOW: {
            int finished = 0;
            do {
                if (top_ == startbrkt_[currentbrkt_]) {
                    finished = 1;
                } else {
                    int prevop = oper_[top_ - 1];
                    if (priority_[prevop] < priority_[kind]) {
                        finished = 1;
                    } else {
                        top_--;
                        calc(top_);
                    }
                }
            } while (!finished);
            oper_[top_] = kind;
            init_value(top_ + 1);
            set_display(value_[top_ - 1], NORM);
            break;
        }
        case EVAL: {
            while (currentbrkt_ > 0) add_right_bracket();
            for (int i = top_; i > 0; --i) calc(i - 1);
            top_ = 0;
            ready_ = 1;
            set_display(value_[top_], NORM);
            break;
        }
    }
}

void CalcEngine::calc(int i) {
    switch (oper_[i]) {
        case PLUS: value_[i] += value_[i + 1]; break;
        case MINUS: value_[i] -= value_[i + 1]; break;
        case MULT: value_[i] *= value_[i + 1]; break;
        case DIV: value_[i] /= value_[i + 1]; break;
        case POW: value_[i] = fs_pow(value_[i], value_[i + 1]); break;
        case INVPOW: value_[i] = fs_pow(value_[i], 1.0 / value_[i + 1]); break;
        default: break;
    }
}

void CalcEngine::cvttobase(char *str, size_type *len, double num, int base, int behind) {
    *len = 0;
    double sign = num < 0.0 ? -1.0 : 1.0;
    num *= sign;
    if (sign == -1.0) append_char(str, len, '-');

    if (num == 0.0) {
        append_char(str, len, '0');
        if (behind > 0) {
            append_char(str, len, '.');
            for (int i = 0; i < behind; ++i) append_char(str, len, '0');
        }
        return;
    }

    static const char digits[] = "0123456789abcdef";
    int place = (int)(fs_log(num) / fs_log((double)base));
    if (place < 0) place = 0;
    for (;;) {
        double div = fs_pow((double)base, (double)place);
        int digit = (int)(num / div);
        num -= (double)digit * div;
        if (place == -1) append_char(str, len, '.');
        place--;
        append_char(str, len, digits[digit]);
        if (*len > 18) {
            *len = 0;
            append_str(str, len, "can't display");
            return;
        }
        if (!((place >= 0) || ((place >= -9) && (num != 0.0)))) break;
    }

    if ((place == -1) && ((behind == DOT) || (behind > 0)))
        append_char(str, len, '.');
    while ((behind > 0) && (behind >= -place)) {
        append_char(str, len, '0');
        place--;
    }
}

void CalcEngine::set_display(double val, int behind) {
    char buf[48];
    size_type len = 0;
    if (behind >= 0) {
        if (base_ == 10) {
            emode_ = 0;
            append_fixed(buf, &len, val, behind);
        } else {
            cvttobase(buf, &len, val, base_, behind);
        }
    } else if (behind == DOT) {
        if (base_ == 10) {
            emode_ = 0;
            char tmp[48];
            size_type tl = 0;
            append_fixed(tmp, &tl, val, 1);
            if (tl > 0) --tl; // drop the single fractional digit, keep the trailing '.'
            tmp[tl] = 0;
            append_str(buf, &len, tmp);
        } else {
            cvttobase(buf, &len, val, base_, behind);
        }
    } else if (behind == NORM) {
        if (base_ == 10) {
            emode_ = 0;
            append_general(buf, &len, val);
        } else {
            cvttobase(buf, &len, val, base_, behind);
        }
    } else { // EXP: exponent entry display
        char tmp[48];
        size_type tl = 0;
        append_fixed(tmp, &tl, val, 8);
        strip_trailing_zeros(tmp, &tl);
        append_str(buf, &len, tmp);
        append_char(buf, &len, 'e');
        append_int(buf, &len, exponent_);
    }
    append_char(buf, &len, ' ');
    if (len > 17) len = 17;
    buf[len] = 0;

    size_type i = 0;
    for (; buf[i]; ++i) display_[i] = buf[i];
    display_[i] = 0;
}

void CalcEngine::set_memdisp() {
    mem_label_[0] = mem_ != 0.0 ? 'M' : 0;
    mem_label_[1] = 0;
}

void CalcEngine::set_drgdisp() {
    const char *s = drgmode_ == 0 ? "DEG" : (drgmode_ == 1 ? "RAD" : "GRAD");
    size_type i = 0;
    for (; s[i]; ++i) drg_label_[i] = s[i];
    drg_label_[i] = 0;
}

void CalcEngine::set_brktdisp() {
    if (currentbrkt_ > 0) {
        size_type len = 0;
        append_int(brkt_label_, &len, currentbrkt_);
        // The bitmap font used to draw this (libs/graphics/graphics.c's
        // rows_for()) only has glyphs for space/-/./: /digits/uppercase
        // A-Z -- same constraint as the kernel console -- so this avoids
        // punctuation like '[' that would silently render as blank.
        append_str(brkt_label_, &len, " OF ");
        append_int(brkt_label_, &len, MaxNumBrkts);
    } else {
        brkt_label_[0] = 0;
    }
}

void CalcEngine::add_left_bracket() {
    if (currentbrkt_ < MaxNumBrkts) {
        currentbrkt_++;
        startbrkt_[currentbrkt_] = top_;
        ready_ = 1;
        set_brktdisp();
    }
}

void CalcEngine::add_right_bracket() {
    if (currentbrkt_ > 0) {
        for (int i = top_; i > startbrkt_[currentbrkt_]; --i) calc(i - 1);
        top_ = startbrkt_[currentbrkt_];
        currentbrkt_--;
        ready_ = 1;
    }
    set_display(value_[top_], NORM);
    set_brktdisp();
}

double CalcEngine::to_drg(double angle) {
    if (drgmode_ == 0) return M_PI * angle / 180.0;
    if (drgmode_ == 2) return M_PI * angle / 100.0;
    return angle;
}

double CalcEngine::from_drg(double angle) {
    if (drgmode_ == 0) return 180.0 * angle / M_PI;
    if (drgmode_ == 2) return 100.0 * angle / M_PI;
    return angle;
}

// --- Public key handlers (ported 1:1 from SciCalc.cpp's cb_but_*_i bodies) --

void CalcEngine::digit(double numb) { handle_number(numb); }
void CalcEngine::dot() { handle_number(-1.0); }

void CalcEngine::sign() {
    if (!emode_) {
        value_[top_] = -value_[top_];
        set_display(value_[top_], NORM);
    } else {
        exponent_ = -exponent_;
        value_[top_] = mantissa_ * fs_pow(10.0, (double)exponent_);
        set_display(mantissa_, EXP);
    }
}

void CalcEngine::clear_entry() {
    init_value(top_);
    set_display(0.0, NORM);
}

void CalcEngine::clear_all() {
    init_value(0);
    set_display(0.0, NORM);
    currentbrkt_ = 0;
    brkt_label_[0] = 0;
}

void CalcEngine::op(Operator kind) { handle_operator(kind); }

void CalcEngine::change_base(int newbase) {
    int oldbase = base_;
    base_ = newbase;
    set_display(value_[top_], NORM);
    ready_ = 1;
    (void)oldbase;
}

void CalcEngine::sqrt_key() {
    if (base_ > 10) { handle_number(10.0); return; }
    if (!inv_) {
        value_[top_] = fs_sqrt(value_[top_]);
    } else {
        value_[top_] = fs_pow(value_[top_], 2.0);
    }
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::pow_key() {
    if (base_ > 10) { handle_number(11.0); return; }
    handle_operator(inv_ ? INVPOW : POW);
}

void CalcEngine::sin_key() {
    if (base_ > 10) { handle_number(12.0); return; }
    if (!inv_) value_[top_] = fs_sin(to_drg(value_[top_]));
    else value_[top_] = from_drg(fs_asin(value_[top_]));
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::cos_key() {
    if (base_ > 10) { handle_number(13.0); return; }
    if (!inv_) value_[top_] = fs_cos(to_drg(value_[top_]));
    else value_[top_] = from_drg(fs_acos(value_[top_]));
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::tan_key() {
    if (base_ > 10) { handle_number(14.0); return; }
    if (!inv_) value_[top_] = fs_tan(to_drg(value_[top_]));
    else value_[top_] = from_drg(fs_atan(value_[top_]));
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::log_key() {
    if (base_ > 10) { handle_number(15.0); return; }
    if (!inv_) value_[top_] = fs_log10(value_[top_]);
    else value_[top_] = fs_pow(10.0, value_[top_]);
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::ln_key() {
    if (!inv_) value_[top_] = fs_log(value_[top_]);
    else value_[top_] = fs_exp(value_[top_]);
    set_display(value_[top_], NORM);
    ready_ = 1;
}

static double fs_trunc(double x) {
    return x < 0.0 ? -fs_floor(-x) : fs_floor(x);
}

void CalcEngine::int_key() {
    if (!inv_) value_[top_] = fs_trunc(value_[top_]);
    else value_[top_] = value_[top_] - fs_trunc(value_[top_]);
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::dr_key() {
    if (!inv_) value_[top_] = M_PI * value_[top_] / 180.0;
    else value_[top_] = 180.0 * value_[top_] / M_PI;
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::drg_key() {
    drgmode_++;
    drgmode_ %= 3;
    set_drgdisp();
}

void CalcEngine::left_bracket() { add_left_bracket(); }
void CalcEngine::right_bracket() { add_right_bracket(); }

void CalcEngine::exchange() {
    if (top_ > startbrkt_[currentbrkt_]) {
        double temp = value_[top_];
        value_[top_] = value_[top_ - 1];
        value_[top_ - 1] = temp;
        set_display(value_[top_], NORM);
        ready_ = 1;
    }
}

void CalcEngine::invx() {
    value_[top_] = 1.0 / value_[top_];
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::factorial() {
    double alpha = value_[top_] + 1.0;
    if ((fs_floor(alpha) == alpha) && (alpha <= 0.0)) {
        init_value(0);
        char buf[24];
        size_type len = 0;
        append_str(buf, &len, "ERROR ");
        size_type i = 0;
        for (; buf[i]; ++i) display_[i] = buf[i];
        display_[i] = 0;
        return;
    }
    if (alpha > 32) {
        value_[top_] = fs_exp(gammaln(alpha));
    } else if (alpha > 1.0) {
        int n = (int)fs_trunc(alpha);
        double lg = 1.0;
        for (int i = 1; i < n; ++i) lg *= (double)i;
        value_[top_] = lg;
    } else {
        return;
    }
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::mplus() {
    if (!inv_) mem_ += value_[top_]; else mem_ -= value_[top_];
    set_display(value_[top_], NORM);
    ready_ = 1;
    set_memdisp();
}

void CalcEngine::mmult() {
    if (!inv_) mem_ *= value_[top_]; else mem_ /= value_[top_];
    set_display(value_[top_], NORM);
    ready_ = 1;
    set_memdisp();
}

void CalcEngine::mclear() {
    if (!inv_) {
        mem_ = 0.0;
        set_display(value_[top_], NORM);
        ready_ = 1;
        set_memdisp();
    } else {
        double temp = mem_;
        mem_ = value_[top_];
        value_[top_] = temp;
        set_display(value_[top_], NORM);
        ready_ = 1;
        set_memdisp();
    }
}

void CalcEngine::mst() {
    mem_ = value_[top_];
    set_display(value_[top_], NORM);
    ready_ = 1;
    set_memdisp();
}

void CalcEngine::mrc() {
    value_[top_] = mem_;
    set_display(value_[top_], NORM);
    ready_ = 1;
}

void CalcEngine::toggle_inv() { inv_ = !inv_; }

void CalcEngine::pi_key() {
    if ((value_[top_] == 0.0) || ready_) {
        value_[top_] = M_PI;
        set_display(value_[top_], NORM);
        ready_ = 1;
    } else if (!emode_ && base_ == 10) {
        emode_ = 1;
        exponent_ = 0;
        mantissa_ = value_[top_];
        set_display(mantissa_, EXP);
    }
}

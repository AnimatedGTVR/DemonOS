#ifndef EDE_CALC_ENGINE_H
#define EDE_CALC_ENGINE_H

// Wave 1 of the EDE port: the calculation engine ported out of
// Desktop/EDE/ede-2.1/ede-calc/SciCalc.{h,cpp}. That file mixes two things
// together -- FLTK widget plumbing (button callbacks, Fl_Box labels) and a
// self-contained numeric engine (stack-based operator evaluation, base
// conversion, memory, brackets, trig/log). This class keeps the numeric
// engine's logic and control flow intact (including its exact quirks --
// see the priority_ comment below) and replaces every FLTK call with a
// plain char-buffer field main.cpp can read for its own native-surface
// button grid and LED display.
#include <stdint.h>

class CalcEngine {
public:
    enum Operator { PLUS, MINUS, MULT, DIV, POW, INVPOW, EVAL };

    CalcEngine();

    void digit(double numb);   // 0..15 (hex digit value), or -1 for handle_number's dot path
    void dot();
    void sign();
    void clear_entry();
    void clear_all();
    void op(Operator kind);
    void change_base(int newbase);
    void sqrt_key();
    void pow_key();
    void sin_key();
    void cos_key();
    void tan_key();
    void log_key();
    void ln_key();
    void int_key();
    void dr_key();
    void drg_key();
    void left_bracket();
    void right_bracket();
    void exchange();
    void invx();
    void factorial();
    void mplus();
    void mmult();
    void mclear();
    void mst();
    void mrc();
    void toggle_inv();
    void pi_key();

    const char *display() const { return display_; }
    const char *mem_label() const { return mem_label_; }
    const char *drg_label() const { return drg_label_; }
    const char *brkt_label() const { return brkt_label_; }
    bool inverse_active() const { return inv_ != 0; }
    int base() const { return base_; }

private:
    enum { MaxNumBrkts = 10 };
    enum Mode { NONE = 0, DOT = -1, NORM = -2, EXP = -3 };

    double value_[4 * (MaxNumBrkts + 1)];
    // Never initialized in upstream SciCalc either (see SciCalc.h:16) --
    // reads as operator-priority-vs-priority comparisons that are always
    // "not lower", which upstream's zero-filled-BSS process loaders turned
    // into deterministic strict left-to-right evaluation (no algebraic
    // operator precedence). This kernel's ELF loader also zero-fills a
    // fresh process image before loading it, so explicitly zeroing here
    // reproduces that same real-world behavior rather than leaving it as
    // true undefined-behavior-flavored garbage.
    int priority_[6];
    int oper_[3 * (MaxNumBrkts + 1)];
    int top_;
    int startbrkt_[MaxNumBrkts + 1];
    int currentbrkt_;
    double mem_;
    int ready_;
    int dot_;
    double diver_;
    int behind_;
    int inv_;
    int emode_;
    int exponent_;
    double mantissa_;
    int base_;
    int drgmode_;

    char display_[24];
    char mem_label_[4];
    char drg_label_[8];
    char brkt_label_[24];

    void handle_number(double numb);
    void handle_operator(Operator kind);
    void set_display(double val, int behind);
    void set_memdisp();
    void set_drgdisp();
    void set_brktdisp();
    void add_left_bracket();
    void add_right_bracket();
    void init_value(int lev);
    void cvttobase(char *str, unsigned long *len, double num, int base, int behind);
    double to_drg(double angle);
    double from_drg(double angle);
    void calc(int i);
};

#endif

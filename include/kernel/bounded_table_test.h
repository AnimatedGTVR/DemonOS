#ifndef KERNEL_BOUNDED_TABLE_TEST_H
#define KERNEL_BOUNDED_TABLE_TEST_H

/* Plain-C-includable prototype for the K1 self-test implemented in
   src/kernel_bounded_table_test.cpp. Unlike kernel/cxx_runtime.h (whose
   contents are already C-safe and declare their own self-test directly),
   kernel/bounded_table.h itself is C++-only (templates, namespaces) and
   cannot be included from kernel.c (compiled as C11) -- so the prototype
   kernel.c needs lives in this separate, minimal header instead. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool kernel_bounded_table_self_test(void);

#ifdef __cplusplus
}
#endif

#endif

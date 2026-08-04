#ifndef KERNEL_SLOT_TABLE_TEST_H
#define KERNEL_SLOT_TABLE_TEST_H

/* Plain-C-includable prototype for the K3 self-test implemented in
   src/kernel_slot_table_test.cpp -- kernel/slot_table.h itself is
   C++-only (templates, namespaces) and cannot be included from kernel.c
   (compiled as C11), same split as kernel/bounded_table_test.h. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool kernel_slot_table_self_test(void);

#ifdef __cplusplus
}
#endif

#endif

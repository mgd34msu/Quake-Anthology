/* Port of src/guest/runtime/windows/crt.ts. GPL-2.0-or-later. */
#ifndef QA_GUEST_RUNTIME_WINDOWS_CRT_H
#define QA_GUEST_RUNTIME_WINDOWS_CRT_H

#include "qa/guest/runtime/windows/contracts.h"

void qa_install_crt(qa_windows_service_host *host);
/* TS ucrtQuickSort: the UCRT qsort (misc/qsort.cpp) over element indices. */
void qa_ucrt_quick_sort(int64_t length, int (*compare)(void *ctx, int64_t left, int64_t right),
                        void (*swap)(void *ctx, int64_t left, int64_t right), void *ctx);

#endif

/*
 * Port of src/guest/runtime/system-v/stdio.ts: glibc _IO_FILE layouts and
 * operations over the System V capabilities. Context parameters mirror the
 * TS GuestCallContext arguments (only forwarded to nested invokes).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_STDIO_H
#define QA_GUEST_RUNTIME_SYSTEM_V_STDIO_H

#include "qa/guest/runtime/system_v/contracts.h"

typedef struct qa_system_v_file_layout {
  int size, descriptor, mode, lock, offset, wide_data, old_offset;
} qa_system_v_file_layout;
qa_system_v_file_layout qa_system_v_file_layout_for(int width); /* 4 | 8 */

typedef struct qa_system_v_stdio {
  qa_system_v_service_host *host;
  qa_guest_address stdin_, stdout_, stderr_;
  qa_system_v_file_layout layout;
} qa_system_v_stdio;

void qa_system_v_stdio_init(qa_system_v_stdio *stdio, qa_system_v_service_host *host);
int qa_system_v_stdio_flush(qa_system_v_stdio *stdio, qa_guest_address file,
                            const qa_guest_call_context *context);
int qa_system_v_stdio_put(qa_system_v_stdio *stdio, qa_guest_address file, int value, bool wide,
                          const qa_guest_call_context *context);
/* Returns TS number of bytes written. */
size_t qa_system_v_stdio_write(qa_system_v_stdio *stdio, qa_guest_address file,
                               const uint8_t *bytes, size_t len,
                               const qa_guest_call_context *context);
int qa_system_v_stdio_get(qa_system_v_stdio *stdio, qa_guest_address file, bool wide,
                          const qa_guest_call_context *context);
int qa_system_v_stdio_unget(qa_system_v_stdio *stdio, qa_guest_address file, int value, bool wide);

#endif

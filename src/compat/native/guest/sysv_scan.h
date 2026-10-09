#ifndef QA_NATIVE_GUEST_SYSV_SCAN_H
#define QA_NATIVE_GUEST_SYSV_SCAN_H

#include "sysv_libc_private.h"

bool sysv_scan_install(guest_sysv_runtime *, qa_error *);
bool sysv_scan_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);

#endif

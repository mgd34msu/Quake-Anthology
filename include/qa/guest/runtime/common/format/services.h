/*
 * Port of src/guest/runtime/common/format/services.ts: __stdio_common
 * vsscanf/vsprintf and glibc vsnprintf service installation.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_FORMAT_SERVICES_H
#define QA_GUEST_RUNTIME_COMMON_FORMAT_SERVICES_H

#include "qa/guest/abi/values.h"
#include "qa/guest/runtime/system_v/contracts.h"
#include "qa/guest/runtime/windows/contracts.h"

void qa_install_windows_format(qa_windows_service_host *host, qa_guest_address errno_address);
void qa_install_system_v_format(qa_system_v_service_host *host);

#endif

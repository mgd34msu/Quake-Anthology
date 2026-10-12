#ifndef QA_NATIVE_Q3_SYSTEM_INFO_H
#define QA_NATIVE_Q3_SYSTEM_INFO_H

#include "qa/console.h"

typedef struct native_q3_system_info_policy {
    bool skip_time_controls, skip_same_timescale;
    bool (*after_set)(void *);
    void *context;
    const char *memory_error;
} native_q3_system_info_policy;

bool native_q3_system_info_apply(qa_cvars *, char **retained, const char *received,
    native_q3_system_info_policy, qa_error *);

#endif

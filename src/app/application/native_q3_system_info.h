#ifndef QA_NATIVE_Q3_SYSTEM_INFO_H
#define QA_NATIVE_Q3_SYSTEM_INFO_H

#include "qa/console.h"
#include "qa/network_q3.h"

typedef struct native_q3_system_info {
    char text[QA_Q3_GAMESTATE_CHARS];
    bool present;
} native_q3_system_info;

typedef struct native_q3_system_info_policy {
    bool skip_time_controls, skip_same_timescale;
    bool (*after_set)(void *);
    void *context;
} native_q3_system_info_policy;

bool native_q3_system_info_apply(qa_cvars *, native_q3_system_info *, const char *received,
    native_q3_system_info_policy, qa_error *);

#endif

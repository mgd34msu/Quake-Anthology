#ifndef APPLICATION_QC_SPAWN_H
#define APPLICATION_QC_SPAWN_H

#include "guest_qc_internal.h"

bool application_qc_spawn_call(void *, qa_qc_instance *, const qa_qc_call_event *,
    qa_qc_call_next, qa_error *);

#endif

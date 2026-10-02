#ifndef QA_APPLICATION_NETWORK_UNIFIED_INPUTS_SAVE_H
#define QA_APPLICATION_NETWORK_UNIFIED_INPUTS_SAVE_H

#include "network_unified_private.h"
#include "unified_save_internal.h"

bool application_unified_inputs_save(qa_source_save_io *, application_unified_inputs **,
    qa_application *, qa_network_runtime *, qa_net_client_id, qa_net_seat_id,
    uint32_t epoch, const application_unified_source *, const qa_unified_session_player *);

#endif

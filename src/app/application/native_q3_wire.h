#ifndef QA_APPLICATION_NATIVE_Q3_WIRE_INTERNAL_H
#define QA_APPLICATION_NATIVE_Q3_WIRE_INTERNAL_H

#include "internal.h"
#include "qa/application_network.h"

bool application_q3_wire_time(const application_provider *, int32_t *, qa_error *);
bool application_native_q3_wire_bind_sources(application_provider *, qa_error *);
bool application_native_q3_wire_current_view(application_provider *, uint32_t source_client,
    qa_q3_player *, qa_q3_visible_entities *, qa_error *);
/* Capture the linked physical GAME rows at accepted Connect, before Begin.
 * Configstrings and peer/channel state are untouched. */
bool application_q3_wire_host_baselines(application_provider *, qa_q3_gamestate *, qa_error *);
bool application_q3_wire_host_snapshot(application_provider *, uint32_t source_client,
    int32_t message_number, int32_t server_command_number, uint8_t flags,
    qa_application_network_q3_frame *, qa_error *);

/* Once after the actual ordinary frame, or the final round settlement frame.
 * DrawActiveFrame consumes these retained records without publishing them. */
bool application_q3_publish_local_snapshots(qa_application *, qa_error *);

#endif

#ifndef QA_APPLICATION_NATIVE_Q1_WIRE_QW_H
#define QA_APPLICATION_NATIVE_Q1_WIRE_QW_H
#include "native_q1_wire.h"
#include "qa/application_network_qw.h"
#include "qa/network_q1_nq.h"
bool application_native_q1_qw_selected(qa_application *);
bool application_native_q1_qw_admission(qa_application *, bool spectator, bool *allowed, qa_error *);
bool application_native_q1_qw_source(qa_application *, qa_application_network_qw_source *, qa_error *);
bool application_native_q1_qw_world(qa_application *, qa_application_network_qw_world *, qa_error *);
bool application_native_q1_qw_client(qa_application *, qa_actor_id, qa_application_network_qw_client *, qa_error *);
bool application_native_q1_qw_client_next(qa_application *, uint32_t *, bool *, qa_application_network_qw_client *, qa_error *);
bool application_native_q1_qw_entity_next(qa_application *, uint32_t *, bool *, qa_actor_id *, qa_application_network_qw_entity *, qa_error *);
bool application_native_q1_qw_visible(qa_application *, qa_actor_id, qa_actor_id, qa_bytes, bool *, qa_error *);
bool application_native_q1_qw_receives(qa_application *, qa_actor_id, const qa_application_protocol_event *, bool *, qa_error *);
bool application_native_q1_qw_prepare(qa_application *, qa_actor_id, qa_error *);
bool application_native_q1_qw_commands(qa_application *, const qa_network_command_group *, qa_error *);
bool application_native_q1_qw_kill(qa_application *, qa_actor_id, bool *, qa_error *);
bool application_native_q1_qw_pause(qa_application *, qa_actor_id, qa_buffer *, bool *, qa_error *);
bool application_native_q1_qw_userinfo(qa_application *, qa_actor_id, const char *, qa_error *);
bool application_native_q1_qw_flush(qa_application *, qa_error *);
bool application_native_q1_qw_setangle(application_provider *, qa_actor_id, qa_vec3, qa_error *);
bool application_native_q1_qw_retire_capture(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_qw_precache(qa_application *, bool, const char *[255], size_t *, qa_error *);
qa_vfs *application_native_q1_qw_content(qa_application *, qa_error *);
bool application_native_q1_qw_emit(application_provider *, const qa_builtin_event *, const qa_nq_message *, qa_actor_id, bool, bool, const qa_application_protocol_reference *, qa_error *);
#endif

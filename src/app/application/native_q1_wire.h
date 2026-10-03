#ifndef QA_APPLICATION_NATIVE_Q1_WIRE_H
#define QA_APPLICATION_NATIVE_Q1_WIRE_H
#include "internal.h"
#include "qa/application_network.h"
#include "qa/application_language.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q1_bots.h"

typedef struct application_native_q1_wire_source {
    application_provider *provider;
    qa_q1_wire_receipt receipt;
} application_native_q1_wire_source;
bool application_native_q1_wire_begin(qa_application *, qa_actor_owner,
    application_native_q1_wire_source *, qa_error *);
void application_native_q1_wire_end(application_native_q1_wire_source *);
bool application_native_q1_wire_qw_begin(qa_application *, application_native_q1_wire_source *, qa_error *);
qa_vfs *application_native_q1_wire_content(application_provider *,qa_error *);
bool application_native_q1_wire_cache_flush(application_provider *,qa_error *);
bool application_native_q1_wire_retain(application_provider *, application_native_q1_wire_source *, qa_error *);
bool application_native_q1_wire_host(qa_application *, qa_application_network_q1_host *, qa_error *);
bool application_native_q1_wire_source_player(qa_application *, qa_actor_id,
    qa_actor_owner *, uint32_t *, qa_net_protocol_id *, qa_error *);
bool application_native_q1_wire_extents(qa_application *, qa_actor_owner, uint32_t *, uint32_t *, qa_error *);
qa_cvars *application_native_q1_wire_cvars(qa_application *, qa_actor_owner, qa_error *);
bool application_native_q1_wire_entity(qa_application *, qa_actor_id, qa_actor_id, qa_q1_entity *, qa_error *);
bool application_native_q1_wire_entity_next(qa_application *, qa_actor_id, uint32_t *, bool *,
    qa_actor_id *, qa_q1_entity *, qa_error *);
bool application_native_q1_wire_eye(qa_application *, qa_actor_id, qa_vec3 *, qa_error *);
bool application_native_q1_check_client(void *, qa_actor_id, qa_actor_id *);
bool application_native_q1_check_client_retire(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_wire_bounds(qa_application *, qa_actor_id, qa_actor_id,
    qa_bounds *, bool *, qa_error *);
bool application_native_q1_wire_precache(qa_application *, qa_actor_owner, bool,
    const char *[255], size_t *, qa_error *);
bool application_native_q1_wire_world(qa_application *, qa_actor_owner,
    qa_application_network_q1_world *, qa_error *);
bool application_native_q1_wire_clientdata(qa_application *, qa_actor_id, qa_q1_clientdata *, qa_error *);
bool application_native_q1_wire_status(qa_application *, qa_actor_owner,
    qa_application_network_q1_status_player [255], size_t *, qa_error *);
/* The caller holds this receipt through every use of the borrowed Source name. */
bool application_native_q1_wire_chat(application_native_q1_wire_source *, qa_actor_id, bool, const char *,
    const char **, qa_actor_id [255], size_t *, qa_error *);
bool application_native_q1_wire_pause(qa_application *, qa_actor_id, qa_buffer *, bool *, qa_error *);
bool application_native_q1_wire_name(qa_application *, qa_actor_id, const char *, qa_error *);
bool application_native_q1_wire_colors(qa_application *, qa_actor_id, int32_t, int32_t, qa_error *);
bool application_native_q1_wire_feedback(qa_application *, qa_actor_id,
    qa_application_network_q1_feedback *, qa_error *);
bool application_native_q1_wire_baseline(qa_application *, qa_actor_id,
    const qa_q1_entity *, qa_q1_entity *, qa_error *);
bool application_native_q1_wire_client_baseline(qa_application *, qa_actor_id, uint32_t,
    qa_q1_entity *, qa_error *);
bool application_native_q1_wire_emit(qa_application *, const qa_builtin_event *, qa_error *);
bool application_native_q1_wire_damage(qa_application *, const qa_damage_outcome *, qa_error *);
bool application_native_q1_wire_inflictor_center(qa_application *, const qa_damage_request *,
    double [3], bool *, qa_error *);
bool application_native_q1_wire_observe(qa_application *, qa_error *);
bool application_native_q1_wire_client_publish(void *, const qa_q1_source_client_view *, qa_error *);
/* Admit source userinfo before MODE and selected player callbacks. The final
 * admission performs only the source spawned reset after actual placement. */
bool application_native_q1_wire_client_userinfo(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_wire_client_admit(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_wire_client_observer(void *, qa_actor_id, bool, qa_error *);
bool application_native_q1_wire_create(application_provider *, qa_error *);
void application_native_q1_wire_destroy(application_provider *);
bool application_native_q1_wire_idle(const application_provider *);
/* Account only admissions held by these actual phase-owned tickets. Active
 * wire readers and any unaccounted admission remain non-idle. */
bool application_native_q1_wire_language_idle(const application_provider *,
    const qa_application_language_ticket *const *, size_t);
void application_native_q1_wire_actor_released(qa_application *, qa_actor_id);
bool application_native_q1_wire_resources_prepare(application_provider *, qa_error *);
/* A successfully registered declaration returns an owned resource reference.
 * Missing registration is found=false; no resource is opened by this read. */
bool application_native_q1_wire_sound_resource(qa_application *, qa_actor_owner,
    qa_string_id path, qa_resource **, const qa_product **, bool *found, qa_error *);
bool application_native_q1_wire_reconnect(qa_application *, qa_error *);
typedef struct application_native_q1_wire_language_ticket application_native_q1_wire_language_ticket;
/* Prepare retains every actual source catalog without publishing a language.
 * Commit consumes the ticket after the authoritative language admission;
 * abort consumes it when that admission fails. Both retain true GAME owners. */
bool application_native_q1_wire_language_prepare(qa_application *, qa_actor_id, const char *,
    application_native_q1_wire_language_ticket **, qa_error *);
void application_native_q1_wire_language_commit(application_native_q1_wire_language_ticket *);
void application_native_q1_wire_language_abort(application_native_q1_wire_language_ticket *);
#endif

#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_H

#include "guest_q3_weapons.h"
#include "qa/movement.h"
#include "qa/modes.h"
#include "qa/game_q3.h"
#include "qa/game_q2.h"

typedef struct application_q3_weapons_services application_q3_weapons_services;

/* A real GAME owns this callback and request continuation before Init. Native
 * selected arsenals and inventory remain their existing publication owners. */
bool application_q3_weapons_services_create(struct q3g_role *,
    application_q3_weapons_services **, qa_error *);
bool application_q3_weapons_services_destroy(application_q3_weapons_services **, qa_error *);
bool application_q3_weapons_services_idle(const application_q3_weapons_services *);
application_q3_weapon_services application_q3_weapons_services_callbacks(application_q3_weapons_services *);
void application_q3_weapons_services_actor_released(application_q3_weapons_services *, qa_actor_record);
/* Host admission checks the actual selected provider, declaration and owned or
 * active item. It queues the genuine Source command ordinal and does not invoke
 * the movement request function or report Source action acceptance. */
bool application_q3_weapons_services_select_intent(application_q3_weapons_services *,
    qa_actor_id, qa_actor_owner arsenal, qa_item_id, bool *admitted, qa_error *);
/* Source commands retain requested int32 selection until that same located
 * full actor's actual Source selection reaches it. Only physical usercmd
 * publication narrows to its actual ABI byte; the retained request stays int32. */
bool application_q3_weapons_services_request(application_q3_weapons_services *, qa_actor_id,
    int32_t *, bool *, qa_error *);
bool application_q3_weapons_services_request_completed(application_q3_weapons_services *,
    qa_actor_id, qa_error *);
bool application_q3_weapons_services_slice_finish(struct q3g_role *, qa_actor_id,
    const qa_usercmd *, const qa_q3_player *, bool reached, qa_error *);
bool application_q3_weapons_services_match_admit(application_q3_weapons_services *, qa_actor_id, qa_error *);
/* Restored modes obtain the actual callback descriptor before creating their
 * own lease. The core retains and saves that lease's sole serial. */
bool application_q3_weapons_services_match_binding(application_q3_weapons_services *,
    qa_mode_id, qa_actor_id, qa_actor_owner, qa_match_binding *, qa_error *);
bool application_q3_weapons_services_match_close(application_q3_weapons_services *, qa_error *);
bool application_q3_weapons_services_pose(void *actual_role, qa_actor_id,
    qa_q3_selected_source_pose *, qa_error *);
bool application_q3_weapons_services_selected_damage(void *actual_native_q3_provider, qa_actor_id,
    float *, bool *handled, qa_error *);
bool application_q3_weapons_services_selected_spawn(void *actual_native_q3_provider, qa_actor_id,
    qa_vec3 *, qa_vec3 *, bool *handled, qa_error *);
bool application_q3_weapons_services_selected_drop(void *actual_native_q3_provider, qa_actor_id,
    bool *handled, qa_error *);
bool application_q3_weapons_services_selected_client_effects(void *actual_native_q3_provider,
    qa_actor_id, const qa_q3_selected_client_effects *, const qa_q3_selected_client_effects *,
    bool *handled, qa_error *);
bool application_q3_weapons_services_grenade_interval(void *actual_application, qa_actor_id,
    qa_actor_owner equipment_source, uint64_t native_ns, uint64_t *, bool *handled, qa_error *);
bool application_q3_weapons_services_equipment_animation(void *actual_native_provider,
    qa_actor_id, bool reverse, bool melee, bool *handled, qa_error *);
bool application_q3_weapons_services_grapple_frame(void *actual_native_q1_provider,
    qa_actor_id, int32_t frame, qa_error *);
bool application_q3_weapons_services_selected_fired(void *actual_native_provider,
    qa_actor_id, qa_item_id actual_weapon, qa_error *);
bool application_q3_weapons_services_q2_muzzle(void *actual_application,
    const qa_builtin_event *, qa_error *);
bool application_q3_weapons_services_selected_delay(void *actual_native_provider,
    qa_actor_id, uint64_t native_ns, uint64_t *, bool *handled, qa_error *);
bool application_q3_weapons_services_q2_damage(void *actual_native_q2_provider,
    qa_actor_id, float *, bool *handled, qa_error *);
bool application_q3_weapons_services_q2_powerups(void *actual_native_q2_provider,
    qa_actor_id, qa_builtin_powerups *, bool *handled, qa_error *);
bool application_q3_weapons_services_q2_input(void *actual_native_q2_provider,
    qa_actor_id, qa_q2_weapon_input *, qa_error *);
bool application_q3_weapons_services_q1_damage(void *actual_native_q1_provider,
    qa_damage_request *, qa_error *);
/* Request payload imports only into the isolated restored constructor; full
 * actor references are checked after Source RAM and clients have been adopted. */
bool application_q3_weapons_services_checkpoint(application_q3_weapons_services *, qa_buffer *, qa_error *);
bool application_q3_weapons_services_restore(application_q3_weapons_services *, qa_bytes, qa_error *);
bool application_q3_weapons_services_validate(application_q3_weapons_services *, qa_error *);

#endif

#ifndef QA_APPLICATION_NATIVE_Q2_ATTACK_H
#define QA_APPLICATION_NATIVE_Q2_ATTACK_H
#include "qa/native_host.h"
struct application_native_q2;
struct application_native_q2_attack;
struct application_native_q2_attack_restore;
#include "qa/movement.h"
#include "qa/modes.h"
bool application_native_q2_player_spawn_point(struct application_native_q2 *, qa_actor_id,
    qa_vec3 *, qa_vec3 *, bool *found, bool *handled, qa_error *);
bool application_native_q2_player_objectives_drop(struct application_native_q2 *, qa_actor_id,
    bool *handled, qa_error *);
bool application_native_q2_player_max_health_read(struct application_native_q2 *, qa_actor_id,
    int32_t *, bool *handled, qa_error *);
bool application_native_q2_player_max_health_write(struct application_native_q2 *, qa_actor_id,
    int32_t, bool *handled, qa_error *);
bool application_native_q2_player_score_read(struct application_native_q2 *, qa_actor_id,
    int32_t *, bool *handled, qa_error *);
bool application_native_q2_player_score_write(struct application_native_q2 *, qa_actor_id,
    int32_t, bool *handled, qa_error *);
bool application_native_q2_player_teleport(struct application_native_q2 *, qa_actor_id,
    qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, uint32_t hold_ms, bool *handled, qa_error *);
bool application_native_q2_player_match_admit(struct application_native_q2 *, qa_actor_id, qa_error *);
bool application_native_q2_player_match_binding(struct application_native_q2 *, qa_mode_id,
    qa_actor_id, qa_actor_owner, qa_match_binding *, qa_error *);
bool application_native_q2_attack_input_fields(struct application_native_q2 *,uint32_t,
    qa_actor_id,qa_movement_result *,qa_vec3 *command_angles,qa_error *);
bool application_native_q2_attack_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_attack_close(struct application_native_q2 *, qa_error *);
void application_native_q2_attack_released(struct application_native_q2 *, qa_actor_id);
bool application_native_q2_attack_read(struct application_native_q2 *, qa_actor_id attacker,
    qa_actor_id inflictor, qa_actor_id target, bool weapon_damage, qa_attack *, qa_error *);
/* Readonly loan from the already prepared original descriptor roster. */
bool application_native_q2_attack_weapon_read(struct application_native_q2 *, uint32_t source_slot,
    qa_actor_id, qa_item_id *, qa_error *);
bool application_native_q2_attack_item_read(struct application_native_q2 *, uint32_t source_slot,
    qa_actor_id, qa_native_address descriptor, qa_item_id *, qa_error *);
bool application_native_q2_attack_descriptor_item(struct application_native_q2 *,
    qa_native_address descriptor,qa_item_id *,qa_error *);
bool application_native_q2_attack_weapon_contains(struct application_native_q2 *,
    qa_item_id, bool *, qa_error *);
bool application_native_q2_attack_delay(struct application_native_q2 *, qa_actor_id,
    qa_item_id item, uint64_t native_ns, uint64_t *, bool *handled, qa_error *);
bool application_native_q2_attack_hand_grenade_item(struct application_native_q2 *,
    qa_item_id *, qa_error *);
bool application_native_q2_attack_available(struct application_native_q2 *, qa_actor_id,
    bool *, qa_error *);
bool application_native_q2_attack_continuing(struct application_native_q2 *, qa_actor_id,
    bool *, qa_error *);
bool application_native_q2_attack_clock_step(struct application_native_q2 *, qa_actor_id,
    uint64_t *time_ns, uint64_t *elapsed_ns, qa_error *);
bool application_native_q2_attack_clock_reset(struct application_native_q2 *, qa_actor_id,
    qa_error *);
const qa_json_document *application_native_q2_attack_declaration_read(const struct application_native_q2 *);
bool application_native_q2_attack_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_attack_restore_prepare(struct application_native_q2 *, qa_bytes,
    struct application_native_q2_attack_restore **, qa_error *);
void application_native_q2_attack_restore_commit(struct application_native_q2 *,
    struct application_native_q2_attack_restore *);
void application_native_q2_attack_restore_abort(struct application_native_q2_attack_restore *);
#endif

#ifndef QA_Q1_BOSS_INTERNAL_H
#define QA_Q1_BOSS_INTERNAL_H

#include "internal.h"

extern const qa_vec3 q1_boss_sphere_points[100];
bool q1_boss_first_player(qa_q1_game *, q1_ref *, qa_error *);
bool q1_final_end(qa_q1_game *, qa_error *);
qa_vec3 q1_boss_angles(qa_vec3);
bool q1_boss_colored_explosion(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_damageable(qa_q1_game *, q1_actor *, bool, qa_error *);
bool q1_boss_child_create(qa_q1_game *, qa_string_id, q1_boss_child_kind, qa_actor_id owner,
                          q1_actor **, qa_error *);
bool q1_boss_child_schedule(qa_q1_game *, q1_actor *, q1_boss_child_kind, double, qa_error *);
q1_ref q1_boss_enemy(const q1_actor *);
qa_vec3 q1_boss_target(qa_q1_game *, const q1_actor *);
bool q1_boss_shot(qa_q1_game *, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction,
                  qa_vec3 velocity, const char *model, q1_projectile_kind, q1_actor **, qa_error *);
bool q1_boss_sphere_manager(qa_q1_game *, q1_actor *, int32_t count, bool chunk, qa_error *);
bool q1_boss_autogun(qa_q1_game *, q1_actor *, qa_vec3 origin, float offset, qa_error *);
bool q1_boss_sphere_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_sphere_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_child_spawn(qa_q1_game *, q1_actor *, q1_boss_child_kind, qa_error *);
bool q1_boss_cleanup(qa_q1_game *, qa_error *);
bool q1_boss_gib_vectors(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_teledeath(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_teledeath_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_targets(qa_q1_game *, q1_actor *, qa_actor_id, qa_string_id, qa_error *);
bool q1_boss_child_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_child_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_child_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_boss_blast_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_major_boss_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_major_boss_fields(qa_q1_game *, q1_actor *, const qa_q1_boss_fields *, qa_error *);
bool q1_major_boss_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_major_boss_pain(qa_q1_game *, q1_actor *, qa_actor_id, float, qa_error *);
bool q1_major_boss_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_major_boss_effect(qa_q1_game *, qa_damage_effect_stage, const qa_damage_request *,
                          qa_damage_effect *, qa_error *);
bool q1_final_awake(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_final_child_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_final_rock_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *,
                         qa_error *);
bool q1_final_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_final_pain(qa_q1_game *, q1_actor *, qa_error *);
bool q1_final_map_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_final_teleport(qa_q1_game *, bool, qa_error *);

static inline float q1_boss_flat_dot(qa_vec3 a, qa_vec3 b) {
    a.z = b.z = 0;
    return qa_vec_dot(qa_vec_normalize(a), qa_vec_normalize(b));
}

#endif

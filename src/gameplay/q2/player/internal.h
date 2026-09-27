#ifndef QA_Q2_PLAYER_INTERNAL_H
#define QA_Q2_PLAYER_INTERNAL_H
#include "../items/internal.h"
#include "qa/game_q2_player.h"
#include <ctype.h>
#include <stdio.h>

typedef qa_q2_player_state q2_client_state;
typedef struct q2_players {
    qa_q2_player_rules rules;
    qa_q2_player_services services;
    char *rule_strings[5];
    qa_actor_id corpses[8];
    unsigned corpse_index, death_animation, pain_animation;
    bool intermission, exit, camera_set, has_landmark, deadly_killbox;
    uint32_t intermission_flags;
    uint64_t intermission_ns, fade_ns, restart_ns;
    qa_string_id next_map;
    qa_q2_landmark landmark;
    qa_vec3 camera_origin, camera_angles;
    qa_q2_player_noise_record noise[2];
} q2_players;
q2_actor *q2_client(qa_q2_game *, qa_actor_id, qa_error *);
bool q2_map_camera_player(qa_q2_game *, qa_actor_id, qa_vec3, qa_vec3, bool, qa_error *);
bool q2_player_animate_reference(qa_q2_game *, qa_actor_id, const qa_body_state *, qa_q2_visual *,
                                 qa_error *);
bool q2_player_emit(qa_q2_game *, const qa_q2_player_event *, qa_error *);
bool q2_player_print(qa_q2_game *, qa_actor_id, int, const char *, qa_error *);
bool q2_player_sound(qa_q2_game *, qa_actor_id, const char *, int, qa_error *);
bool q2_player_observe(qa_q2_game *, q2_actor *, qa_q2_player_movement *, qa_error *);
bool q2_player_move(qa_q2_game *, q2_actor *, const qa_q2_player_motion *, qa_error *);
bool q2_player_collision(qa_q2_game *, q2_actor *, bool, qa_error *);
bool q2_player_inventory_copy(qa_q2_game *, qa_actor_id, qa_inventory_entry **, size_t *,
                              qa_error *);
bool q2_player_inventory_set(qa_q2_game *, qa_actor_id, const qa_inventory_entry *, size_t,
                             qa_error *);
bool q2_player_spawn_select(qa_q2_game *, q2_actor *, const qa_q2_player_movement *,
                            const qa_q2_landmark *, qa_body_state *, bool *, qa_error *);
bool q2_player_environment(qa_q2_game *, q2_actor *, const qa_q2_player_movement *, qa_error *);
bool q2_player_falling(qa_q2_game *, q2_actor *, const qa_q2_player_movement *, qa_error *);
bool q2_player_environment_damage(qa_q2_game *, q2_actor *, float, int, uint32_t, qa_error *);
bool q2_player_build_view(qa_q2_game *, q2_actor *, const qa_q2_player_movement *, qa_error *);
bool q2_player_death(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_player_copy_corpse(qa_q2_game *, q2_actor *, qa_error *);
bool q2_player_reserve_corpses(qa_q2_game *, qa_error *);
bool q2_killbox(qa_q2_game *, qa_actor_id, qa_actor_id, bool spawning, bool exact, bool *,
                qa_error *);
bool q2_player_scoreboard(qa_q2_game *, q2_actor *, bool, qa_error *);
bool q2_player_publish_inventory(qa_q2_game *, q2_actor *, qa_error *);
bool q2_player_update_chase(qa_q2_game *, q2_actor *, qa_error *);
bool q2_player_coop_respawn(qa_q2_game *, q2_actor *, qa_error *);
bool q2_player_obituary(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_map_find(qa_q2_game *, const char *classname, qa_string_id target, size_t ordinal,
                 qa_actor_id *);
uint32_t q2_map_flags(qa_q2_game *, qa_actor_id);
bool q2_player_trace(qa_q2_game *, qa_actor_id, qa_vec3, qa_vec3, const qa_bounds *, uint32_t,
                     qa_trace_result *, qa_error *);
bool q2_player_fix_stuck(qa_q2_game *, qa_actor_id, qa_vec3, qa_bounds, qa_vec3 *, bool *,
                         qa_error *);
bool q2_map_navigation(qa_q2_game *, qa_actor_id, qa_vec3, qa_vec3, qa_vec3 *, size_t, size_t *,
                       bool *, qa_error *);
bool q2_map_searching(qa_q2_game *, qa_actor_id);
bool q2_map_transition(qa_q2_game *, qa_actor_id, qa_actor_id, qa_string_id, const qa_q2_landmark *,
                       bool, qa_error *);
bool q2_map_poi(qa_q2_game *, qa_actor_id, qa_vec3 *, qa_string_id *, bool *, qa_error *);
bool q2_map_in_phs(qa_q2_game *, qa_vec3, qa_vec3);
bool q2_player_compass_update(qa_q2_game *, q2_actor *, bool, qa_error *);
static inline float q2_clamp(float x, float low, float high) { return fminf(high, fmaxf(low, x)); }
static inline float q2_seconds_left(uint64_t end, uint64_t now) {
    return end > now ? (float)((double)(end - now) / Q2_NS) : 0;
}
#endif

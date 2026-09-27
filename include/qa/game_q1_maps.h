#ifndef QA_GAME_Q1_MAPS_H
#define QA_GAME_Q1_MAPS_H

#include "qa/campaign_q1_sources.h"

/* Authored values are borrowed only during spawn. The map provider interns
 * strings and retains typed fields beside the native actor continuation. */
typedef struct qa_q1_map_fields {
    const char *model, *map, *noise, *noise1, *noise2, *noise3;
    const char *endtext, *intermissiontext, *netname, *event;
    const char *spawn_function, *spawn_classname;
    qa_vec3 mangle, movedir, view_offset;
    bool has_movedir, has_view_offset;
    float height, lip, width, length, pause_time;
    float volume, duration, distance, next_think_seconds;
    float spawn_multi, spawn_silent, gravity;
    int32_t sounds, style, world_type, color_map, impulse;
    float counter_value;
    int32_t particle_color;
} qa_q1_map_fields;

typedef struct qa_q1_static_model {
    qa_string_id model;
    qa_vec3 origin, angles;
    int32_t frame, skin, color_map;
} qa_q1_static_model;
typedef struct qa_q1_map_finale_view {
    uint32_t stage;
    qa_string_id map, text;
    qa_vec3 origin, angles;
    double exit_after;
} qa_q1_map_finale_view;
typedef enum qa_q1_map_ending { QA_Q1_MAP_END_DOPA, QA_Q1_MAP_END_MG3 } qa_q1_map_ending;
typedef struct qa_q1_path_state {
    qa_actor_id move_target, enemy, old_enemy, previous_corner, owner;
    qa_string_id path;
    double pause_until, follow_until;
    bool monster;
} qa_q1_path_state;
typedef enum qa_q1_path_change_kind {
    QA_Q1_PATH_OWNER,
    QA_Q1_PATH_VISIT,
    QA_Q1_PATH_DESTINATION,
    QA_Q1_PATH_PAUSE_END,
    QA_Q1_PATH_CANCEL_PAUSE,
    QA_Q1_PATH_STAND,
    QA_Q1_PATH_FOLLOW_BEGIN,
    QA_Q1_PATH_FOLLOW_UNTIL,
    QA_Q1_PATH_FOUND
} qa_q1_path_change_kind;
typedef struct qa_q1_path_change {
    qa_q1_path_change_kind kind;
    qa_actor_id reference;
    qa_string_id target;
    double pause_until, follow_until;
} qa_q1_path_change;
typedef struct qa_q1_map_options {
    qa_targets *targets;
    qa_q1_level *level;
    qa_q1_campaign_source *campaign_source;
    uint32_t *server_flags;
    qa_string_id current_map;
    bool registered;
    void *context;
    bool (*static_model)(void *, const qa_q1_static_model *, qa_error *);
    bool (*ambient)(void *, qa_vec3, qa_string_id sound, float volume, float attenuation,
                    qa_error *);
    bool (*lightstyle)(void *, int32_t style, qa_string_id pattern, qa_error *);
    bool (*set_skill)(void *, int32_t, qa_error *);
    bool (*player_exited)(void *, qa_actor_id, qa_error *);
    bool (*secret_found)(void *, qa_actor_id source, qa_actor_id player, uint32_t total,
                         uint32_t found, qa_error *);
    /* An attached foreign character can follow authored Q1 monster paths. */
    bool (*path_touch)(void *, qa_actor_id corner, qa_actor_id follower, bool *handled, qa_error *);
    /* Path controls read and mutate the actual foreign continuation. Read is
     * nonmutating. A pause change invokes an installed path-end callback;
     * cancel restores normal monster use. Hipnotic follow preserves the old
     * enemy, pending walk frame and cooldown; STAND always enters the stand
     * callback and FOUND invokes the actual owner's found-target callback.
     * Supply both callbacks together. */
    bool (*path_read)(void *, qa_actor_id, qa_q1_path_state *);
    bool (*path_change)(void *, qa_actor_id, const qa_q1_path_change *, qa_error *);
    bool (*finale)(void *, const qa_q1_map_finale_view *, qa_error *);
    bool (*finale_finished)(void *);
    bool (*finish_campaign)(void *, qa_error *);
    bool (*server_command)(void *, qa_string_id command, qa_error *);
} qa_q1_map_options;

/* Bind before authored spawning. Service owners remain valid until the next
 * begin_map call or destruction of the native game. */
bool qa_q1_game_maps_bind(qa_q1_game *, const qa_q1_map_options *, qa_error *);
/* After session world retirement and geometry publication, before spawning:
 * requires an empty registry, completed release fanout and a session safe
 * point. Replaces map services and resets map clocks, counters and actor
 * continuations. Retains provider RNG, attack sequence, resources and pooled
 * storage. The caller owns campaign state and player carry across retirement. */
bool qa_q1_game_begin_map(qa_q1_game *, const qa_q1_map_options *, qa_error *);
/* Link authored door groups after all map entities have been admitted. */
bool qa_q1_game_maps_finish(qa_q1_game *, qa_error *);
/* These adapters handle native actors only, for application owner dispatch. */
bool qa_q1_game_path_read(const qa_q1_game *, qa_actor_id, qa_q1_path_state *);
bool qa_q1_game_path_change(qa_q1_game *, qa_actor_id, const qa_q1_path_change *, qa_error *);
/* Threewave spectator door/teleporter passage, after its velocity update. */
bool qa_q1_game_map_observer_nearby(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_game_map_after_physics(qa_q1_game *, qa_actor_id, qa_error *);
/* Native gameplay resets precede this campaign transition. The new-game flag
 * changes departing travel health only; current live health remains intact. */
bool qa_q1_game_map_finish_addon(qa_q1_game *, qa_q1_map_ending, qa_error *);
bool qa_q1_game_map_new_game_travel(const qa_q1_game *);
bool qa_q1_game_map_finale(qa_q1_game *, qa_actor_id oldone, bool finish, qa_error *);
void qa_q1_game_map_dismiss_finale(qa_q1_game *);
qa_string_id qa_q1_game_map_name(const qa_q1_game *);
uint32_t qa_q1_game_campaign_flags(const qa_q1_game *);
qa_string_id qa_q1_game_map_text(const qa_q1_game *, qa_actor_id, qa_q1_campaign_text);
bool qa_q1_game_map_set_text(qa_q1_game *, qa_actor_id, qa_q1_campaign_text, qa_string_id,
                             qa_error *);
bool qa_q1_game_map_defer_targets(qa_q1_game *, const qa_target_use *, qa_error *);
bool qa_q1_game_map_defer_level(qa_q1_game *, double delay_seconds, qa_error *);
bool qa_q1_game_map_defer_finale(qa_q1_game *, qa_q1_campaign_timer, double delay_seconds,
                                 qa_error *);
void qa_q1_game_map_secrets(const qa_q1_game *, uint32_t *total, uint32_t *found);

#endif

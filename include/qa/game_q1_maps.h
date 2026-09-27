#ifndef QA_GAME_Q1_MAPS_H
#define QA_GAME_Q1_MAPS_H

#include "qa/campaign_q1_sources.h"

/* Authored values are borrowed only during spawn. The map provider interns
 * strings and retains typed fields beside the native actor continuation. */
typedef struct qa_q1_map_fields {
    const char *model, *map, *noise, *noise1, *noise2, *noise3;
    const char *endtext, *intermissiontext;
    qa_vec3 mangle, movedir;
    bool has_movedir;
    float height, lip, width, length, pause_time;
    float volume, duration, distance;
    int32_t sounds, style, world_type, color_map;
} qa_q1_map_fields;

typedef struct qa_q1_static_model {
    qa_string_id model;
    qa_vec3 origin, angles;
    int32_t frame, skin, color_map;
} qa_q1_static_model;
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
} qa_q1_map_options;

/* Bind before authored spawning. All service owners outlive the native game;
 * binding is immutable for that game instance. */
bool qa_q1_game_maps_bind(qa_q1_game *, const qa_q1_map_options *, qa_error *);
/* Link authored door groups after all map entities have been admitted. */
bool qa_q1_game_maps_finish(qa_q1_game *, qa_error *);
/* Threewave spectator door/teleporter passage, after its velocity update. */
bool qa_q1_game_map_observer_nearby(qa_q1_game *, qa_actor_id, qa_error *);
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

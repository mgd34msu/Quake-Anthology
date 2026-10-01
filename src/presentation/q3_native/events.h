#ifndef QA_Q3_NATIVE_EVENTS_H
#define QA_Q3_NATIVE_EVENTS_H

#include "effects.h"

typedef struct q3n_event_settings {
    bool footsteps, autoswitch, demo_playback, no_predict, synchronous_clients;
    bool single_player_active, camera_orbit, debug_events;
    bool blood, gibs, score_plum, no_projectile_trail, add_marks, ragepro;
} q3n_event_settings;
typedef struct q3n_event_state {
    float land_change, step_change;
    int32_t land_time, step_time, item_pickup, item_pickup_time, item_pickup_blend_time;
    int32_t powerup_active, powerup_time;
    char killer_name[32];
} q3n_event_state;
typedef struct q3n_mark_fragment { uint32_t first, count; } q3n_mark_fragment;
typedef struct q3n_event_options {
    qa_q3_product product;
    qa_q3_presentation_assets *assets;
    void *context;
    void (*print)(void *, const char *);
    bool (*center_print)(void *, const q3n_frame *, const char *, int32_t y, int32_t width, qa_error *);
    bool (*voice_chat)(void *, const q3n_frame *, int32_t mode, bool voice_only,
        int32_t client, int32_t color, const char *command, qa_error *);
    bool (*trace)(void *, const q3n_frame *, qa_vec3 start, qa_vec3 end,
        qa_bounds, int32_t skip, uint32_t mask, qa_trace_result *, qa_error *);
    bool (*point_contents)(void *, const q3n_frame *, qa_vec3, int32_t pass,
        uint32_t *, qa_error *);
    bool (*mark_fragments)(void *, const q3n_frame *, const qa_vec3 *points, size_t count,
        qa_vec3 projection, qa_vec3 *output, size_t point_capacity,
        q3n_mark_fragment *, size_t fragment_capacity, size_t *returned, qa_error *);
    /* Primary CGAME delegates to its selected composition before source
     * fallback. Supplemental source scenes have no primary replacement. */
    bool (*event_replacement)(void *, const q3n_frame *, q3n_entity *,
        const qa_q3_entity *scratch, qa_vec3 position, bool *suppressed, qa_error *);
    /* Weapon-specific effects belong to the real weapon child. The scratch S
     * carries source event-only number/rail weapon writes without changing GAME. */
    bool (*weapon_event)(void *, const q3n_frame *, q3n_entity *,
        const qa_q3_entity *scratch, int32_t event, qa_vec3 position, qa_error *);
} q3n_event_options;
bool q3n_events_create(const q3n_event_options *, q3n_events **, qa_error *);
/* The native remote constructor retains the same real event/UI/weapon/world
 * services, qualified by Network receipts instead of a local GAME reader. */
bool q3n_events_create_remote(const q3n_event_options *, q3n_events **, qa_error *);
/* Standalone ClientEffects/LocalEntitySystem owns genuine world collision and
 * mark projection services, with no client event dispatcher or UI callbacks. */
bool q3n_events_create_effects(const q3n_event_options *, q3n_events **, qa_error *);
void q3n_events_destroy(q3n_events *);
bool q3n_events_idle(const q3n_events *);
const q3n_event_state *q3n_events_state(const q3n_events *);
void q3n_events_clear_pickup_time(q3n_events *);
void q3n_events_clear_killer(q3n_events *);
/* One genuine CGAME rand stream, shared with all presentation children. */
int32_t q3n_events_rand(q3n_events *);
float q3n_events_random(q3n_events *);
float q3n_events_crandom(q3n_events *);
qa_vec3 q3n_events_direction(int32_t source_byte);
bool q3n_events_apply(const q3n_frame *, const qa_application_native_q3_entity *, q3n_entity *, qa_error *);
/* The snapshot owner has already performed CG_CheckEvents deduplication and
 * authored this scratch ES. Dispatch it once against that exact cache row. */
bool q3n_events_apply_remote(const q3n_frame *, const q3n_remote_entity *,
    const qa_q3_entity *scratch, qa_vec3 position, qa_error *);
/* Playerstate transitions use the same source dispatcher but own their event
 * sequence deduplication in the caller's retained playerstate continuation. */
bool q3n_events_player(const q3n_frame *, const qa_q3_entity *, q3n_entity *, qa_error *);
bool q3n_events_pain(const q3n_frame *, q3n_entity *, int32_t source_number, int32_t health, qa_error *);
bool q3n_events_trace(const q3n_frame *, qa_vec3, qa_vec3, qa_bounds,
    int32_t skip, uint32_t mask, qa_trace_result *, qa_error *);
bool q3n_events_point_contents(const q3n_frame *, qa_vec3, int32_t pass, uint32_t *, qa_error *);
bool q3n_events_trace_number(const q3n_frame *, const qa_trace_result *, int32_t *, qa_error *);
bool q3n_events_buffer(q3n_events *, int32_t sound, qa_error *);
/* Buffer playback only. The real frame submits marks, particles, then local
 * entities before this call, preserving the source ordering. */
bool q3n_events_finish(const q3n_frame *, qa_error *);
void q3n_events_round(q3n_events *);
/* Aggregate imports the same real asset registry first and holds its capture
 * lease. Codecs preserve every pool slot, linked order, RNG and audio cursor. */
bool q3n_events_checkpoint(const q3n_events *, qa_buffer *, qa_error *);
bool q3n_events_restore(q3n_events *, qa_bytes, qa_error *);

#endif

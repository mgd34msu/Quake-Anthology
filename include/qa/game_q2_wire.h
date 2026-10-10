#ifndef QA_GAME_Q2_WIRE_H
#define QA_GAME_Q2_WIRE_H

#include "qa/game_q2_player.h"
#include "qa/movement.h"

bool qa_q2_wire_lightstyle_revision(const qa_q2_game *, uint64_t *, qa_error *);

typedef struct qa_q2_wire_binding {
    qa_actor_id actor;
    qa_actor_owner source_owner;
    uint32_t source_slot;
    bool in_use;
} qa_q2_wire_binding;
typedef struct qa_q2_wire_view {
    qa_q2_player_view view;
    uint64_t frame, time_ns;
    bool present;
} qa_q2_wire_view;
typedef struct qa_q2_wire_map {
    qa_string_id music, sky;
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto, music_present;
} qa_q2_wire_map;
typedef struct qa_q2_wire_shadow_light {
    qa_actor_id actor;
    uint32_t source_slot, type, resolution;
    float radius, intensity, fade_start, fade_end, cone_angle;
    int32_t style;
    qa_vec3 direction;
    bool present;
} qa_q2_wire_shadow_light;
typedef struct qa_q2_wire_origin {
    uint64_t source_frame;
    qa_vec3 origin;
    bool present;
} qa_q2_wire_origin;
typedef struct qa_q2_wire_lifetime {
    qa_vec3 creation_origin;
    uint64_t creation_frame, link_count;
    qa_q2_wire_origin origins[8];
    bool present;
} qa_q2_wire_lifetime;
typedef struct qa_q2_wire_source_entity {
    qa_q2_wire_binding binding;
    qa_q2_visual visual;
    qa_body_state body;
    qa_vec3 previous_origin;
    qa_actor_id owner;
    qa_string_id classname, loop_sound, precache_sound, flare_image;
    qa_q2_weapon_state weapon;
    qa_q2_wire_lifetime lifetime;
    uint64_t body_serial;
    uint32_t event, server_flags, spawn_flags;
    qa_physics_solid solid;
    float volume, attenuation;
    float flare_start, flare_end;
    bool has_visual, has_weapon, flare, model_beam;
} qa_q2_wire_source_entity;

/* Source construction reserves its genuine client edicts before authored or
 * dynamic admission. Configure only an empty, idle GAME namespace. */
bool qa_q2_wire_configure(qa_q2_game *, uint32_t entity_capacity,
    uint32_t client_slots, qa_error *);
/* Reads the actual next G_Spawn slot without admitting an actor. The caller
 * uses it immediately for canonical Source provenance before physical birth. */
bool qa_q2_wire_spawn_slot(const qa_q2_game *, uint32_t *, qa_error *);
bool qa_q2_wire_extent(const qa_q2_game *, uint32_t *, qa_error *);
bool qa_q2_wire_policy(const qa_q2_game *, uint32_t *entity_capacity,
    uint32_t *client_slots, qa_error *);
/* The actual one-world birth/link owner supplies real canonical generations.
 * This admits only an Engine namespace row; no actor or GAME behavior is made. */
bool qa_q2_wire_admit_actor(qa_q2_game *, qa_actor_id, qa_error *);
/* Emission-time Engine admission retains full-generation provenance through
 * retirement. This does not grant live access to a retired actor. */
bool qa_q2_wire_entity_number(qa_q2_game *, qa_actor_id, uint32_t *, qa_error *);
bool qa_q2_wire_linked(qa_q2_game *, const qa_linked_body *, qa_error *);
bool qa_q2_wire_binding_read(const qa_q2_game *, uint32_t,
    qa_q2_wire_binding *, qa_error *);
bool qa_q2_wire_actor(const qa_q2_game *, qa_actor_id,
    qa_q2_wire_binding *, qa_error *);
bool qa_q2_wire_next(const qa_q2_game *, uint64_t *source_order,
    qa_q2_wire_binding *, bool *found, qa_error *);
bool qa_q2_wire_entity_read(qa_q2_game *, uint32_t,
    qa_q2_wire_source_entity *, qa_error *);
bool qa_q2_wire_view_read(const qa_q2_game *, qa_actor_id,
    qa_q2_wire_view *, qa_error *);
bool qa_q2_wire_lightstyle_read(const qa_q2_game *, uint32_t,
    qa_string_id *, qa_error *);
/* Original PM fields are a temporary projection of common movement and the
 * Source-only rule tail. No second current movement record is retained. */
bool qa_q2_player_movement_read(const qa_q2_game *, qa_actor_id,
    qa_movement_result *, qa_vec3 *command_angles, qa_error *);
bool qa_q2_player_movement_prepare(qa_q2_game *, qa_actor_id,
    const qa_usercmd *, qa_movement_result *, bool *run_pmove, qa_error *);
bool qa_q2_player_movement_complete(qa_q2_game *, qa_actor_id,
    const qa_movement_result *, const qa_usercmd *, bool source_movement,
    bool was_grounded, qa_error *);
bool qa_q2_player_movement_restore(qa_q2_game *, qa_actor_id, const qa_body_state *,
    const qa_movement_result *, const qa_vec3 *command_angles, qa_error *);
bool qa_q2_wire_map_read(const qa_q2_game *, qa_q2_wire_map *, qa_error *);
bool qa_q2_wire_shadow_read(const qa_q2_game *, uint32_t,
    qa_q2_wire_shadow_light *, qa_error *);

typedef struct qa_q2_wire_stat_resources {
    void *context;
    uint32_t items_base;
    bool (*image)(void *, const char *, uint32_t *, qa_error *);
} qa_q2_wire_stat_resources;
/* One movement-independent native player-view projection for local and wire HUDs. */
bool qa_q2_wire_stats(const qa_q2_game *, const qa_q2_player_view *,
    const qa_q2_wire_stat_resources *, int16_t stats[64], qa_error *);
const char *qa_q2_wire_statusbar(const qa_q2_game *);

#endif

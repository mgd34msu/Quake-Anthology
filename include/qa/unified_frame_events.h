#ifndef QA_UNIFIED_FRAME_EVENTS_H
#define QA_UNIFIED_FRAME_EVENTS_H

#include "qa/builtin.h"
#include "qa/event_ring.h"
#include "qa/game_q2_player.h"
#include "qa/network_q2_messages.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_player.h"
#include "qa/unified_frame_visuals.h"
#include "qa/network_q3.h"
#include "qa/qvm.h"

/* Names stay interned until the wire codec resolves them. Payload pointers are
 * owned by the event record or its retained source page leases. */
typedef struct qa_unified_q2_campaign_level {
    char *map, *name;
    uint32_t visit_order, total_secrets, found_secrets, total_monsters, killed_monsters;
    double time_seconds;
} qa_unified_q2_campaign_level;
typedef struct qa_unified_q2_map_event {
    qa_q2_map_event_kind kind;
    qa_actor_id actor, recipient, target;
    char *text, *resource;
    qa_vec3 origin, direction, color;
    qa_q2_fog fog;
    float value, duration, radius, alpha, intensity, fade_start, fade_end, cone_cosine;
    int32_t count, style, slot;
    uint32_t flags, resolution;
    bool visible;
    const qa_builtin_message_arg *arguments;
    size_t argument_count;
    qa_unified_q2_campaign_level *levels;
    size_t level_count;
    uint64_t button_time_ns;
} qa_unified_q2_map_event;

/* Decoded foreign temporary fields keep their real protocol discriminator.
 * Entity fields have full Source actor receipts as well as original numbers. */
typedef struct qa_unified_q2_temp_field {
    qa_q2_temp_field_kind kind;
    qa_q2_temp_field_name name;
    int32_t integer;
    qa_vec3 vector;
    qa_actor_id actor;
} qa_unified_q2_temp_field;
typedef struct qa_unified_q2_temporary {
    uint8_t type;
    bool rerelease;
    qa_unified_q2_temp_field *fields;
    size_t field_count;
} qa_unified_q2_temporary;
typedef struct qa_unified_q2_muzzle {
    qa_actor_id actor;
    uint16_t entity, flash;
    bool monster, silenced, has_pose;
    qa_vec3 origin, direction, angles;
    float scale;
} qa_unified_q2_muzzle;
typedef struct qa_unified_q2_poi {
    qa_actor_id actor;
    int32_t key, image;
    qa_vec3 position;
    uint16_t duration;
    uint8_t color, flags;
    bool remove;
} qa_unified_q2_poi;
typedef struct qa_unified_q2_protocol_event {
    qa_q2_server_event_kind kind;
    qa_actor_id actor;
    char *text, *resource;
    int32_t level;
    uint32_t index;
    bool instant, reliable;
    const qa_builtin_message_arg *arguments;
    size_t argument_count;
    qa_unified_q2_temporary temporary;
    qa_q2_fog fog;
    float transition_ms;
    qa_unified_q2_muzzle muzzle;
    qa_unified_q2_poi poi;
    qa_vec3 origin, direction;
    float volume, attenuation, damage;
    int32_t channel;
    bool health, armor, shield, first;
} qa_unified_q2_protocol_event;

typedef struct qa_unified_mod_identity {
    char *id, *artifact_path;
} qa_unified_mod_identity;
typedef enum qa_unified_q3_event_kind {
    QA_UNIFIED_Q3_PRINT, QA_UNIFIED_Q3_LOG, QA_UNIFIED_Q3_SERVER_COMMAND,
    QA_UNIFIED_Q3_DROP_CLIENT, QA_UNIFIED_Q3_CONFIGSTRING,
    QA_UNIFIED_Q3_ENTITY_EVENT, QA_UNIFIED_Q3_PLAYER_EVENT,
    QA_UNIFIED_Q3_CONSOLE_COMMAND, QA_UNIFIED_Q3_SOUND
} qa_unified_q3_event_kind;
typedef struct qa_unified_q3_event {
    qa_unified_q3_event_kind kind;
    qa_actor_id actor;
    char *text, *resource;
    int32_t client, time_ms, event, parameter, source_sequence;
    uint32_t index;
    bool execute_now, external;
    qa_vec3 origin, velocity;
    qa_q3_entity entity;
    qa_q3_player player;
    qa_unified_mod_identity module;
    qa_qvm_abi abi;
    int32_t sound, channel;
    float volume;
    bool loop;
} qa_unified_q3_event;
typedef struct qa_unified_q3_character_event {
    qa_actor_id actor;
    int32_t event, parameter, time_ms;
} qa_unified_q3_character_event;
typedef enum qa_unified_q3_ballistic_kind {
    QA_UNIFIED_Q3_REMOVE, QA_UNIFIED_Q3_FIRE, QA_UNIFIED_Q3_PROJECTILE,
    QA_UNIFIED_Q3_BOUNCE, QA_UNIFIED_Q3_TRAIL, QA_UNIFIED_Q3_IMPACT,
    QA_UNIFIED_Q3_CONTACT, QA_UNIFIED_Q3_SHOTGUN, QA_UNIFIED_Q3_RAIL,
    QA_UNIFIED_Q3_RAIL_AWARD
} qa_unified_q3_ballistic_kind;
typedef struct qa_unified_q3_ballistic_event {
    qa_unified_q3_ballistic_kind kind;
    qa_actor_id actor, target;
    int32_t weapon, time_ms, surface, contact, count, until_ms;
    qa_vec3 origin, end, normal, point, start, direction;
    qa_q3_trajectory trajectory;
    uint32_t seed;
    float volume;
    bool flesh, rail_surface;
} qa_unified_q3_ballistic_event;

typedef struct qa_unified_presentation_owner {
    char *provider;
    uint64_t generation;
} qa_unified_presentation_owner;
typedef enum qa_unified_owner_event_kind { QA_UNIFIED_OWNER_RETIRED, QA_UNIFIED_OWNER_REFRESHED } qa_unified_owner_event_kind;
typedef struct qa_unified_owner_event {
    qa_unified_owner_event_kind kind;
    qa_unified_presentation_owner owner;
    qa_actor_id recipient;
} qa_unified_owner_event;
typedef struct qa_unified_visibility_event { qa_actor_id actor; bool visible; } qa_unified_visibility_event;
typedef enum qa_unified_presentation_kind {
    QA_UNIFIED_PRESENTATION_BUILTIN, QA_UNIFIED_PRESENTATION_Q2_PLAYER,
    QA_UNIFIED_PRESENTATION_Q2_MAP, QA_UNIFIED_PRESENTATION_Q2_PROTOCOL,
    QA_UNIFIED_PRESENTATION_Q2_TEMPORARY, QA_UNIFIED_PRESENTATION_MODEL,
    QA_UNIFIED_PRESENTATION_Q3, QA_UNIFIED_PRESENTATION_Q3_CHARACTER,
    QA_UNIFIED_PRESENTATION_Q3_BALLISTIC, QA_UNIFIED_PRESENTATION_OWNER,
    QA_UNIFIED_PRESENTATION_VISIBILITY
} qa_unified_presentation_kind;
typedef struct qa_unified_presentation_payload {
    qa_unified_presentation_kind kind;
    union {
        qa_builtin_event builtin;
        qa_q2_player_event q2_player;
        qa_unified_q2_map_event q2_map;
        qa_unified_q2_protocol_event q2_protocol;
        qa_unified_q2_temporary q2_temporary;
        qa_unified_model_state model;
        qa_unified_q3_event q3;
        qa_unified_q3_character_event q3_character;
        qa_unified_q3_ballistic_event q3_ballistic;
        qa_unified_owner_event owner;
        qa_unified_visibility_event visibility;
    } value;
} qa_unified_presentation_payload;
typedef struct qa_unified_presentation_event {
    uint64_t sequence;
    double seconds;
    char *content, *provider;
    qa_game_family family;
    qa_unified_presentation_owner owner;
    qa_actor_id recipient;
    uint8_t q2_profile;
    uint64_t q2_interval_ns;
    int32_t source_entity;
    bool has_source_entity;
    qa_unified_presentation_payload payload;
} qa_unified_presentation_event;

typedef struct qa_unified_sound_event {
    char *resource;
    qa_actor_id actor;
    qa_vec3 origin;
    int32_t channel;
    float volume, attenuation;
} qa_unified_sound_event;
typedef enum qa_unified_message_kind {
    QA_UNIFIED_MESSAGE_PRINT, QA_UNIFIED_MESSAGE_CENTER_PRINT,
    QA_UNIFIED_MESSAGE_COMMAND_TEXT, QA_UNIFIED_MESSAGE_CONFIG_STRING,
    QA_UNIFIED_MESSAGE_Q2_LAYOUT, QA_UNIFIED_MESSAGE_Q2_INVENTORY,
    QA_UNIFIED_MESSAGE_Q2_MUZZLE_FLASH, QA_UNIFIED_MESSAGE_DISCONNECT
} qa_unified_message_kind;
typedef struct qa_unified_message_event {
    qa_unified_message_kind kind;
    char *text;
    int32_t level;
    uint32_t index;
    int16_t *counts;
    size_t count;
    uint16_t entity, flash;
    bool monster;
} qa_unified_message_event;
typedef enum qa_unified_simulation_kind { QA_UNIFIED_SIMULATION_SOUND, QA_UNIFIED_SIMULATION_MESSAGE, QA_UNIFIED_SIMULATION_DAMAGE } qa_unified_simulation_kind;
typedef struct qa_unified_simulation_payload {
    qa_unified_simulation_kind kind;
    bool linked_presentation;
    uint64_t source_presentation_sequence;
    union {
        qa_unified_sound_event sound;
        qa_unified_message_event message;
        qa_damage_outcome damage;
    } value;
} qa_unified_simulation_payload;
typedef struct qa_unified_simulation_event {
    uint64_t sequence;
    double time;
    bool milliseconds, private_audience;
    uint32_t client_slot;
    uint64_t client_generation;
    qa_unified_simulation_payload payload;
} qa_unified_simulation_event;
typedef struct qa_unified_frame_events {
    qa_strings *strings;
    uint32_t epoch;
    uint64_t frame;
    qa_unified_presentation_event *presentation;
    size_t presentation_count;
    qa_unified_simulation_event *simulation;
    size_t simulation_count;
    /* Storage custody is not part of the encoded record layout. */
    qa_unified_frame_lease *lease;
    qa_event_lease **dependencies;
    size_t dependency_count;
} qa_unified_frame_events;

/* The shared compiled record layout owns clone/dispose, external decoding,
 * and cold actor rebinding for all event variants. */
void qa_unified_presentation_payload_dispose(qa_unified_presentation_payload *);
void qa_unified_simulation_payload_dispose(qa_unified_simulation_payload *);
bool qa_unified_presentation_payload_clone(const qa_unified_presentation_payload *, qa_unified_presentation_payload *, qa_error *);
bool qa_unified_simulation_payload_clone(const qa_unified_simulation_payload *, qa_unified_simulation_payload *, qa_error *);
void qa_unified_frame_events_destroy(qa_unified_frame_events *);
void qa_unified_presentation_event_dispose(qa_unified_presentation_event *);
bool qa_unified_presentation_event_clone(const qa_unified_presentation_event *, qa_unified_presentation_event *, qa_error *);
bool qa_unified_document_create_events(qa_unified_frame_events **owned, qa_unified_document **out, qa_error *);
const qa_unified_frame_events *qa_unified_document_events(const qa_unified_document *);

#endif

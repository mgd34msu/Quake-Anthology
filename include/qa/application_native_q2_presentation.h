#ifndef QA_APPLICATION_NATIVE_Q2_PRESENTATION_H
#define QA_APPLICATION_NATIVE_Q2_PRESENTATION_H

#include "qa/application.h"
#include "qa/game_q2.h"
#include "qa/native_host.h"
#include "qa/game_q2_wire.h"
#include "qa/native_host_q2_wire.h"
#include "qa/hud_controls.h"

typedef enum qa_application_native_q2_source_kind {
    QA_APPLICATION_NATIVE_Q2_BUILTIN,
    QA_APPLICATION_NATIVE_Q2_ORIGINAL
} qa_application_native_q2_source_kind;

typedef struct qa_application_native_q2_presentation {
    qa_session *session;
    const qa_launch_snapshot *publication;
    const qa_launch_instance *launch;
    qa_vfs *content;
    qa_actor_owner source_owner;
    qa_product_id content_product;
    qa_q2_edition edition;
    qa_application_native_q2_source_kind kind;
    union {
        const qa_q2_game *game;
        struct {
            const qa_native_host *host;
            qa_native_profile profile;
        } original;
    } source;
    qa_clock_config clock_config;
    qa_clock_state clock;
    uint64_t server_time_ns;
    uint64_t publication_generation, map_revision;
    bool retained;
} qa_application_native_q2_presentation;

typedef struct qa_application_native_q2_entity_prefix {
    qa_actor_id actor;
    uint32_t source_slot;
    union {
        qa_q2_wire_source_entity builtin;
        qa_native_host_q2_entity original;
    } source;
} qa_application_native_q2_entity_prefix;

typedef struct qa_application_native_q2_entity_sample {
    qa_actor_id actor;
    uint32_t source_slot;
    qa_vec3 origin, angles, previous_origin;
    uint32_t models[4], frame, old_frame, render_flags, event;
    uint64_t effects;
    qa_bounds solid_bounds;
    float solid_radius;
} qa_application_native_q2_entity_sample;

/* Actual completed GAME table observation. These prefixes grant no received
 * CLIENT frame or model bounds; the local CLIENT captures those separately. */
bool qa_application_native_q2_presentation_extent(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t *, qa_error *);
bool qa_application_native_q2_presentation_entity(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t source_slot,
    qa_application_native_q2_entity_prefix *, bool *found, qa_error *);
/* Local Source dictionary sample, independent of a HOST transport frame.
 * Solid bounds decode the actual API's packed solid, never model bounds. */
bool qa_application_native_q2_presentation_sample(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t source_slot,
    qa_application_native_q2_entity_sample *, bool *found, qa_error *);

/* Borrows the completed physical ENTITIES source. These server timestamps
 * grant no client sample time, recipient visibility, or source execution.
 * A different primary family returns found=false and leaves out unchanged. */
bool qa_application_native_q2_presentation_selected(qa_application *,
    qa_application_native_q2_presentation *, bool *found, qa_error *);
/* Pure restore/capture observation under the actual retained content lease. */
bool qa_application_native_q2_presentation_retained_selected(qa_application *,
    qa_application_native_q2_presentation *, bool *found, qa_error *);
bool qa_application_native_q2_presentation_current(qa_application *,
    const qa_application_native_q2_presentation *);
/* Reads the emitting GAME's actual compiled rules or original API profile,
 * including during its source callback. This grants no transport dialect or
 * PresentationOwner. Other source families leave edition unchanged. */
bool qa_application_native_q2_source_profile_read(qa_application *, qa_actor_owner,
    qa_q2_edition *edition, bool *found, qa_error *);
bool qa_application_native_q2_source_clock_read(qa_application *, qa_actor_owner,
    qa_q2_edition *edition, uint64_t *interval_ns, bool *found, qa_error *);

typedef struct qa_application_native_q2_hud_source {
    qa_actor_owner provider, data_provider;
    uint64_t config_revision;
    qa_q2_edition edition;
    bool deathmatch, cooperative, original;
    const qa_q2_game *game;
    const char *statusbar;
    const char *const *configstrings;
    uint32_t configstring_count;
} qa_application_native_q2_hud_source;
/* Selects the actor's actual HUD/data owners without reading mutable player state. */
bool qa_application_native_q2_hud_source_read(qa_application *, qa_actor_id,
    qa_application_native_q2_hud_source *, bool *found, qa_error *);

typedef struct qa_application_native_q2_hud {
    qa_actor_owner provider, data_provider;
    uint64_t config_revision;
    bool deathmatch, cooperative;
    qa_q2_edition edition;
    const qa_cvars *cvars;
    const qa_hud_cvar_handles *hud_cvars;
    const qa_q2_game *game;
    qa_q2_player_view view;
    const char *statusbar, *layout;
    const char *const *configstrings;
    uint32_t configstring_count;
    int16_t stats[64];
    int32_t inventory[256];
    uint64_t time_ns, frame_ns;
    int32_t server_frame, player_number;
    bool original;
} qa_application_native_q2_hud;
/* The actor's chosen HUD and CHARACTER supply data independently of map/mover. */
bool qa_application_native_q2_hud_read(qa_application *, qa_actor_id,
    qa_application_native_q2_hud *, bool *found, qa_error *);

#endif

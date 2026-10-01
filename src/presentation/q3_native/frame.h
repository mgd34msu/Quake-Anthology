#ifndef QA_Q3_NATIVE_FRAME_H
#define QA_Q3_NATIVE_FRAME_H

#include "entity.h"
#include "media.h"
#include "remote_frame.h"
#include "qa/ui_preferences.h"
#include "qa/application_selected_effects.h"

typedef struct q3n_weapons q3n_weapons;
typedef struct q3n_events q3n_events;
typedef struct q3n_weapon_settings q3n_weapon_settings;
typedef struct q3n_event_settings q3n_event_settings;
typedef struct q3n_particles q3n_particles;
typedef struct q3n_view q3n_view;
typedef struct q3n_player_state q3n_player_state;
typedef struct q3n_server_commands q3n_server_commands;
typedef struct qa_native_q3_client_service qa_native_q3_client_service;

/* Actual source observations borrow this completed frame only. The viewing
 * physical client/full actor qualifies recipient visibility; raw local PS
 * keeps its followed clientNum for source camera/body/viewweapon semantics. */
typedef struct q3n_frame {
    qa_application *application;
    qa_application_native_q3_presentation source;
    /* Mutually exclusive with local GAME and standalone selected EFFECTS.
     * Remote rows and clocks borrow the actual Network/cache receipt. */
    const q3n_remote_frame *remote;
    /* A standalone selected effect borrows its independent native producer.
     * It has no primary GAME snapshot, physical client or invented player S. */
    const qa_application_selected_effects *effects_source;
    const qa_application_effect_event *effect_event;
    void *effect_output_context;
    bool (*effect_entity_output)(void *, const qa_q3_ref_entity *, float cull_radius, qa_error *);
    bool (*effect_sound_output)(void *, const struct q3n_frame *, qa_audio_asset *,
        const qa_vec3 *fixed_origin, int32_t channel, qa_error *);
    bool (*effect_pose_current)(void *);
    qa_q3_presentation *presentation;
    qa_q3_presentation_assets *assets;
    q3n_clients *clients;
    q3n_media *media;
    q3n_weapons *weapons;
    q3n_events *events;
    q3n_particles *particles;
    q3n_view *view;
    q3n_player_state *player_state;
    q3n_server_commands *server_commands;
    qa_native_q3_client_service *client_service;
    qa_native_q3_wire_reader *reader;
    const q3n_weapon_settings *weapon_settings;
    const q3n_event_settings *event_settings;
    q3n_entity *entities;
    uint32_t seat, viewing_client, physical_presentation_seat;
    qa_actor_id viewing_actor;
    qa_q3_player local_player;
    bool has_local_player;
    int32_t time, frame_milliseconds, client_frame;
    qa_q3_refdef refdef;
    qa_vec3 view_angles;
    bool third_person;
    qa_ui_preferences preferences;
} q3n_frame;

#endif

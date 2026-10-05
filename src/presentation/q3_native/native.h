#ifndef QA_Q3_NATIVE_H
#define QA_Q3_NATIVE_H

#include "hud.h"
#include "packet.h"
#include "particles.h"
#include "player_fx.h"
#include "qa/application_native_q3_visibility.h"

typedef struct q3n_native q3n_native;
typedef struct q3n_native_frame_options q3n_native_frame_options;
typedef struct q3n_native_options {
    qa_application *application;
    /* Consumed only on successful construction; the backend remains borrowed
     * from the real frontend seat owner. Restore supplies an imported client. */
    qa_native_q3_client_service *client;
    qa_native_q3_wire_reader *reader; /* Borrowed genuine reached-gamestate lease. */
    qa_q3_presentation *presentation;
    /* Display/input/audio ordinal, independent of the authored launch seat. */
    uint32_t physical_presentation_seat;
    q3n_event_options events;
    q3n_weapon_options weapons;
    q3n_view_options view;
    q3n_player_state_options player_state;
    q3n_hud_options hud;
    q3n_server_command_options commands;
    q3n_player_fx_backend player_fx;
    void *frame_context;
    /* Prepare true selected composition after reached commands and before
     * snapshot events/PS warnings. End releases its borrowed frame even when
     * begin or any subsequent source callback fails. It submits no source. */
    bool (*begin_frame)(void *,const q3n_frame *,qa_error *);
    void (*end_frame)(void *);
    bool (*camera_ready)(void *,const q3n_frame *,qa_error *);
    /* Actual selected outputs finish after source packet/local processing and
     * before RenderScene collects world lighting. The entered frame remains
     * borrowed through this callback and the existing unconditional unwind. */
    bool (*before_render)(void *,const q3n_frame *,qa_error *);
    void *packet_context;
    bool (*packet_body)(void *, const q3n_frame *, const qa_application_native_q3_entity *,
        q3n_entity *, const qa_q3_ref_entity *, bool *consumed, qa_error *);
    void *settings_context;
    /* Read only after the actual service refresh and CG_UpdateCvars stages. */
    bool (*frame_settings)(void *, const q3n_native *,
        const qa_application_native_q3_presentation *, q3n_native_frame_options *, qa_error *);
} q3n_native_options;

/* Every setting borrows one frame's projection of the genuine CGAME cache.
 * The native client service alone saves cache values and modification counts. */
struct q3n_native_frame_options {
    q3n_client_settings clients;
    q3n_weapon_settings weapons;
    q3n_event_settings events;
    q3n_view_settings view;
    q3n_hud_settings hud;
    q3n_player_fx_settings player_fx;
    q3n_packet_options packet;
    float swing_speed;
    bool no_player_animations, animations_disabled;
    uint32_t stereo; /* 0=center, 1=left, 2=right */
    float stereo_separation;
};

typedef struct q3n_native_owners {
    qa_native_q3_client_service *client;
    qa_native_q3_wire_reader *reader;
    qa_q3_presentation *presentation;
    qa_q3_presentation_assets *assets;
    q3n_clients *clients;
    q3n_media *media;
    q3n_weapons *weapons;
    q3n_events *events;
    q3n_particles *particles;
    q3n_view *view;
    q3n_player_state *player_state;
    q3n_hud *hud;
    q3n_server_commands *commands;
    const qa_launch_instance *source_launch;
    qa_vfs *content;
    qa_actor_owner source_owner;
    uint32_t seat, physical_client, physical_presentation_seat;
    qa_actor_id viewing_actor;
} q3n_native_owners;

bool q3n_native_create(const q3n_native_options *, q3n_native **, qa_error *);
bool q3n_native_idle(const q3n_native *);
bool q3n_native_retire_ready(const q3n_native *, qa_error *);
bool q3n_native_destroy(q3n_native *, qa_error *);
/* Video restart returns the actual CG children while its parent keeps the
 * installed client service, wire reader, GAME and input owners. */
bool q3n_native_video_close(q3n_native **, qa_error *);
/* Pure installed physical owners; no source clock, registration or callback
 * submission is replayed during aggregate graph collection. */
bool q3n_native_owners_read(const q3n_native *, q3n_native_owners *, qa_error *);
bool q3n_native_recipient(q3n_native *, qa_application_q3_client_context *, qa_error *);
bool q3n_native_current(const q3n_native *);
/* Actual completed source/recipient and retained camera state for an idle
 * native console invocation. Raw PS is borrowed from this observation only. */
bool q3n_native_command_frame(q3n_native *, q3n_frame *, qa_error *);
bool q3n_native_initialize(q3n_native *, int32_t actual_command_baseline, qa_error *);
bool q3n_native_initialize_video(q3n_native *,int32_t actual_reached_command_baseline,qa_error *);
bool q3n_native_draw(q3n_native *, int32_t actual_latest_command, bool *rendered, qa_error *);
/* The actual CGAME cvar callback reloads a physical CS_PLAYERS row. */
bool q3n_native_reload_client(q3n_native *, uint32_t physical_client,
    const q3n_client_settings *, qa_error *);
bool q3n_native_round(q3n_native *, qa_error *);

#endif

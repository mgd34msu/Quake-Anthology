#ifndef QA_Q3_NATIVE_INTERNAL_H
#define QA_Q3_NATIVE_INTERNAL_H

#include "native.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_assets_save.h"

struct q3n_native {
    q3n_native_options options;
    qa_launch_instance_lease *source_lease;
    const qa_q3_game *source_game;
    qa_session *session;
    qa_actor_owner source_owner;
    qa_product_id content_product;
    qa_q3_product product;
    uint64_t map_revision;
    uint32_t seat, physical_client, physical_presentation_seat;
    qa_actor_id viewing_actor;
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
    q3n_entity entities[QA_Q3_SOURCE_ENTITIES];
    bool seen[QA_Q3_SOURCE_ENTITIES];
    uint64_t source_frame_number;
    int32_t old_time, frame_milliseconds, client_frame;
    qa_q3_refdef previous_refdef;
    bool has_source_frame, initialized, busy, faulted;
    const q3n_native_frame_options *frame_options;
};

bool q3nn_fail(qa_error *, qa_status, const char *);
bool q3nn_children_idle(const q3n_native *);
bool q3nn_allocate(const q3n_native_options *, bool restoring, q3n_native **, qa_error *);
void q3nn_free_children(q3n_native *);
bool q3nn_source(q3n_native *, qa_application_native_q3_presentation *, qa_error *);
bool q3nn_entity(q3n_native *, const q3n_frame *, uint32_t,
    qa_application_native_q3_entity *, q3n_entity **, qa_error *);

#endif

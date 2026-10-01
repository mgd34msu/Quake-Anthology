#ifndef QA_APPLICATION_NATIVE_Q3_PRESENTATION_H
#define QA_APPLICATION_NATIVE_Q3_PRESENTATION_H

#include "qa/application.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_wire.h"

typedef struct qa_application_native_q3_presentation {
    qa_session *session;
    const qa_q3_game *source_game;
    const qa_launch_snapshot *publication;
    const qa_launch_instance *launch;
    qa_vfs *content;
    qa_actor_owner source_owner;
    qa_product_id content_product;
    qa_q3_product product;
    qa_source_frame source_frame;
    int32_t source_time_ms, match_start_time_ms, game_type;
    uint32_t max_clients, entity_count;
    uint64_t publication_generation, map_revision;
} qa_application_native_q3_presentation;

typedef struct qa_application_native_q3_entity {
    qa_q3_source_binding binding;
    qa_q3_entity state;
    qa_q3_wire_visibility visibility;
    qa_q3_wire_native_visibility native_visibility;
    bool present;
} qa_application_native_q3_entity;

typedef struct qa_application_native_q3_client {
    qa_q3_source_binding binding;
    qa_q3_native_client client;
    qa_q3_player player;
    /* Player is available only for a real CONNECTED client with its body. */
    bool present;
} qa_application_native_q3_client;

/* Borrow the exact published native GAME at its completed source frame. The
 * cut owns no execution lease, media, input seat or CGAME role. Metadata may be
 * retained through qa_launch_instance_retain_metadata; source state remains
 * borrowed and every observation must qualify this cut again. */
bool qa_application_native_q3_presentation_read(qa_application *, qa_actor_owner,
    qa_application_native_q3_presentation *, qa_error *);
/* Discover the published primary ENTITIES source independently of HUD. A
 * different source family or external GAME returns found=false. */
bool qa_application_native_q3_presentation_selected(qa_application *,
    qa_application_native_q3_presentation *, bool *found, qa_error *);
bool qa_application_native_q3_presentation_current(qa_application *,
    const qa_application_native_q3_presentation *);

/* Indices are physical GAME rows, including client zero. Actor identity comes
 * from the real binding and includes registry and generation. No observation
 * performs BG conversion, consumes events or advances native behavior. */
bool qa_application_native_q3_presentation_entity(qa_application *,
    const qa_application_native_q3_presentation *, uint32_t physical_entity,
    qa_application_native_q3_entity *, qa_error *);
bool qa_application_native_q3_presentation_client(qa_application *,
    const qa_application_native_q3_presentation *, uint32_t physical_client,
    qa_application_native_q3_client *, qa_error *);
/* Text borrows the authoritative slot until mutation; revision distinguishes
 * nested writes even when the source restores identical text. */
bool qa_application_native_q3_presentation_configstring(qa_application *,
    const qa_application_native_q3_presentation *, uint32_t index,
    const char **text, uint64_t *revision, qa_error *);
/* A missing, remote, bot or pre-Begin viewing seat returns found=false. */
bool qa_application_native_q3_presentation_local(qa_application *,
    const qa_application_native_q3_presentation *, uint32_t seat,
    uint32_t *physical_client, qa_actor_id *, qa_q3_player *, bool *found, qa_error *);

#endif

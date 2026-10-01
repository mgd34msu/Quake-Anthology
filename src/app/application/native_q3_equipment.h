#ifndef QA_APPLICATION_NATIVE_Q3_EQUIPMENT_H
#define QA_APPLICATION_NATIVE_Q3_EQUIPMENT_H

#include "internal.h"
#include "qa/game_q3_wire.h"

typedef struct application_native_q3_equipment_view {
    qa_actor_id actor;
    qa_actor_owner provider;
    const qa_q3_game *game;
    const qa_launch_instance *launch;
    const qa_product *product;
    uint64_t publication_generation, map_revision;
    qa_source_frame source_frame;
    int32_t source_time_ms;
    uint32_t source_slot;
    qa_q3_source_binding binding;
    qa_q3_player player;
    qa_q3_player_state arsenal;
} application_native_q3_equipment_view;

/* The selected ARSENAL's fixed physical client may be independent of the
 * primary ENTITIES GAME. No engine Connect admission is manufactured. */
bool application_native_q3_equipment_read(application_provider *, qa_actor_id,
    application_native_q3_equipment_view *, qa_error *);
bool application_native_q3_equipment_current(application_provider *,
    const application_native_q3_equipment_view *);

#endif

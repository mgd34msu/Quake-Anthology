#ifndef QA_APPLICATION_MAP_TRAVEL_PRIVATE_H
#define QA_APPLICATION_MAP_TRAVEL_PRIVATE_H
#include "map_private.h"

struct application_map_state {
    qa_travel_route route;
    size_t cursor;
    uint64_t revision;
    qa_actor_owner provider;
    qa_actor_id cause;
    qa_product_id geometry;
    qa_q2_landmark landmark;
    qa_string_id nextserver;
    bool pending, busy, has_landmark, carry_players, complete_campaign;
    bool loading, load_carry, load_new_unit;
    bool publication_complete, match_finished;
    uint64_t load_revision;
    qa_string_id load_nextserver;
};
void application_map_load_finish(qa_application *, bool published);
bool application_map_checkpoint_capture(qa_application *, qa_buffer *, qa_error *);
bool application_map_checkpoint_restore(qa_application *candidate, qa_bytes, qa_error *);
bool application_source_queue_map_travel(qa_application *,
    const qa_application_travel_request *, qa_error *);
bool application_source_queue_travel(qa_application *,
    const qa_application_travel_request *, qa_error *);
#endif

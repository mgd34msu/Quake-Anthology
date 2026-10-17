#ifndef QA_APPLICATION_MAP_TRAVEL_PRIVATE_H
#define QA_APPLICATION_MAP_TRAVEL_PRIVATE_H
#include "map_private.h"
#include "qa/team_arena_campaign_progress.h"

typedef enum application_travel_origin {
    APPLICATION_TRAVEL_MANUAL,
    APPLICATION_TRAVEL_CAMPAIGN,
    APPLICATION_TRAVEL_MATCH
} application_travel_origin;
typedef enum application_transition_kind {
    APPLICATION_TRANSITION_ROUTE,
    APPLICATION_TRANSITION_CAMPAIGN_COMPLETE
} application_transition_kind;

struct application_map_state {
    qa_launch_draft *restart_draft;
    qa_travel_route route;
    size_t cursor;
    uint64_t revision;
    qa_actor_owner provider;
    qa_actor_id cause;
    qa_product_id geometry;
    qa_q2_landmark landmark;
    qa_string_id nextserver;
    qa_map_launch_policy map_launch;
    application_travel_origin origin;
    application_transition_kind transition;
    bool pending, busy, has_landmark, carry_players, complete_campaign;
    bool gates_blocked, level_recorded, completion_emitted;
    bool loading, load_carry, load_new_unit;
    bool publication_complete, match_finished;
    bool start_match;
    size_t match_mode_index;
    uint64_t load_revision;
    qa_string_id load_nextserver;
    uint64_t entry_generation, save_request_revision;
    bool save_requested, fresh_entry, authored_autosave;
    qa_team_arena_override_phase team_overrides;
};
bool application_map_campaign_active(const qa_application *);
bool application_map_travel_prepare(qa_application *, qa_error *);
bool application_map_travel_retained_read(const qa_application *, qa_application_travel_view *);
void application_match_intents_travel_override(struct application_match_intents *, const qa_application *);
bool application_source_complete_campaign(application_provider *, qa_error *);
bool application_map_campaign_complete_event(application_provider *, qa_error *);
bool application_map_level_entry(qa_application *, bool fresh, qa_error *);
bool application_map_autosave_request(qa_application *, qa_error *);
void application_map_load_finish(qa_application *, bool published);
bool application_map_stop_prepare(qa_application *, qa_error *);
bool application_map_checkpoint_capture(qa_application *, bool departed, qa_buffer *, qa_error *);
bool application_map_checkpoint_restore(qa_application *candidate, qa_bytes, qa_error *);
bool application_source_queue_map_travel(qa_application *,
    const qa_application_travel_request *, qa_error *);
bool application_source_queue_travel(qa_application *,
    const qa_application_travel_request *, qa_error *);
bool application_campaign_location(qa_application *, qa_product_id, const char *,
    qa_campaign_location *, qa_error *);
const qa_q2_save_level *application_campaign_q2_level(const qa_application *);
#endif

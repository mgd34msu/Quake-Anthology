#ifndef QA_CAMPAIGN_H
#define QA_CAMPAIGN_H
#include "qa/strings.h"

typedef enum qa_travel_kind {
    QA_TRAVEL_MAP,
    QA_TRAVEL_CINEMATIC,
    QA_TRAVEL_PICTURE,
    QA_TRAVEL_DEMO
} qa_travel_kind;
typedef struct qa_travel_target {
    qa_travel_kind kind;
    const char *name, *spawn_point;
    bool new_unit;
} qa_travel_target;
typedef struct qa_travel_route {
    qa_travel_target *targets;
    size_t count;
    char *storage;
} qa_travel_route;
/* Parses the authored Q2 SV_Map chain, preserving unit and spawn markers. */
bool qa_q2_travel_parse(const char *expression, qa_travel_route *, qa_error *);
void qa_travel_route_free(qa_travel_route *);
bool qa_q2_nextserver(const qa_travel_route *, size_t current, qa_buffer *, qa_error *);

typedef struct qa_campaign_location {
    qa_string_id content, map;
} qa_campaign_location;
bool qa_campaign_location_make(qa_strings *, qa_string_id content, qa_bytes map,
                               qa_campaign_location *, qa_error *);
bool qa_campaign_location_equal(qa_campaign_location, qa_campaign_location);
typedef struct qa_campaign_world qa_campaign_world;
bool qa_campaign_world_create(qa_campaign_location, qa_bytes snapshot,
                              qa_campaign_world **out, qa_error *);
/* Successful admission transfers the already encoded buffer without copying. */
bool qa_campaign_world_take(qa_campaign_location, qa_buffer *snapshot,
                            qa_campaign_world **out, qa_error *);
/* Relocate only the session-owned location. Immutable state keeps its one
 * original byte owner across candidate application publication. */
bool qa_campaign_world_relocate(const qa_campaign_world *, qa_campaign_location,
                                qa_campaign_world **out, qa_error *);
void qa_campaign_world_retain(qa_campaign_world *);
void qa_campaign_world_release(qa_campaign_world *);
qa_campaign_location qa_campaign_world_location(const qa_campaign_world *);
qa_bytes qa_campaign_world_bytes(const qa_campaign_world *);
typedef struct qa_campaign_unit qa_campaign_unit;
typedef struct qa_campaign_visit qa_campaign_visit;
typedef struct qa_campaign_unit_checkpoint {
    bool has_current;
    qa_campaign_location current;
    qa_campaign_world **worlds;
    size_t count;
} qa_campaign_unit_checkpoint;
qa_campaign_unit *qa_campaign_unit_create(qa_strings *, qa_error *);
void qa_campaign_unit_destroy(qa_campaign_unit *);
bool qa_campaign_unit_current(const qa_campaign_unit *, qa_campaign_location *);
/* Unit and strings outlive every visit. The candidate retains snapshot handles,
 * not duplicate world bytes; the active world is never kept in its world cache. */
bool qa_campaign_unit_stage(qa_campaign_unit *, qa_campaign_location destination, bool new_unit,
                            qa_campaign_world *departure, qa_campaign_visit **out, qa_error *);
const qa_campaign_world *qa_campaign_visit_restore(const qa_campaign_visit *);
bool qa_campaign_visit_capture(const qa_campaign_visit *, qa_campaign_unit_checkpoint *, qa_error *);
bool qa_campaign_visit_commit(qa_campaign_visit *, qa_error *);
void qa_campaign_visit_destroy(qa_campaign_visit *);
bool qa_campaign_unit_capture(const qa_campaign_unit *, qa_campaign_unit_checkpoint *, qa_error *);
void qa_campaign_unit_checkpoint_free(qa_campaign_unit_checkpoint *);
bool qa_campaign_unit_restore(qa_campaign_unit *, const qa_campaign_unit_checkpoint *, qa_error *);
#endif

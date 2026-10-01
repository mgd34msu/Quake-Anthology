#ifndef QA_BOT_GOALS_H
#define QA_BOT_GOALS_H

#include "qa/bot_library.h"
#include "qa/bot_navigation.h"

#define QA_BOT_GOAL_STACK 8
#define QA_BOT_AVOID_GOALS 256
#define QA_BOT_SOURCE_GOAL_MIN INT32_C(0x40000000)
enum { QA_BOT_GOAL_ITEM = 1, QA_BOT_GOAL_ROAM = 2, QA_BOT_GOAL_DROPPED = 4 };
typedef struct qa_bot_goal {
    qa_vec3 origin;
    int32_t area;
    qa_vec3 mins, maxs;
    int32_t entity, number, flags, item_info;
} qa_bot_goal;
typedef struct qa_bot_avoid_goal {
    int32_t number;
    float expires;
} qa_bot_avoid_goal;
typedef struct qa_bot_goal_state {
    int32_t client, last_reachability_area;
    uint32_t stack_top;
    qa_bot_goal stack[QA_BOT_GOAL_STACK];
    qa_bot_avoid_goal avoid[QA_BOT_AVOID_GOALS];
} qa_bot_goal_state;
typedef struct qa_bot_goal_entity {
    qa_actor_id actor;
    int32_t number, type, model_index;
    qa_vec3 origin, last_visible_origin;
    float last_update_time;
} qa_bot_goal_entity;
typedef struct qa_bot_pickup_goal {
    qa_actor_id actor;
    int32_t entity;
    qa_vec3 origin;
    qa_bounds bounds;
    const char *name;
    float utility;
} qa_bot_pickup_goal;
typedef struct qa_bot_goal_services {
    void *context;
    qa_bot_navigation *(*navigation)(void *, int32_t client);
    /* Views follow ascending source entity order and remain borrowed until the
     * paired end. A snapshot may contain retired entities for visibility age. */
    bool (*entities)(void *, const qa_bot_goal_entity **, size_t *, void **lease, qa_error *);
    void (*entities_end)(void *, void *lease);
    bool (*entity)(void *, int32_t number, qa_bot_goal_entity *, bool *, qa_error *);
    float (*dropped_weight)(void *);
    /* Pickup order is the shared source-provider order. Inspect revalidates the
     * exact actor generation immediately before evaluation. */
    bool (*pickups)(void *, int32_t client, const qa_actor_id **, size_t *, void **lease,
                    qa_error *);
    void (*pickups_end)(void *, void *lease);
    bool (*pickup)(void *, int32_t client, qa_actor_id, qa_bot_pickup_goal *, bool *, qa_error *);
    bool (*owns_item)(void *, int32_t client, int32_t entity, bool *, qa_error *);
    void (*diagnostic)(void *, const char *);
    bool (*report)(void *, qa_script_severity, const char *,qa_error *);
    bool (*log)(void *, const char *, qa_error *);
    bool (*developer)(void *);
    bool (*debug)(void *);
} qa_bot_goal_services;
typedef struct qa_bot_goal_options {
    uint32_t maximum_states, maximum_level_items;
    int32_t game_type;
    float dropped_weight;
    qa_bot_random_source random;
} qa_bot_goal_options;
typedef struct qa_bot_goals qa_bot_goals;
bool qa_bot_goals_create(qa_bot_items *, const qa_bot_goal_options *, const qa_bot_goal_services *,
                         qa_bot_goals **, qa_error *);
void qa_bot_goals_destroy(qa_bot_goals *);
bool qa_bot_goals_active(const qa_bot_goals *);
/* Same-map round rebinding retires source actor goal identities. Static map
 * goals, locations, camps and their allocation order remain library-owned. */
bool qa_bot_goals_rebind_world(qa_bot_goals *, const qa_entities *, qa_error *);
bool qa_bot_goals_reconfigure(qa_bot_goals *, qa_bot_items *, int32_t game_type,
                              uint32_t next_map_capacity, qa_error *);
/* A NULL config suspends source item queries while retaining prior immutable
 * metadata needed by existing checkpoints. Reconfiguration enables it again. */
bool qa_bot_goals_time(qa_bot_goals *, float seconds, qa_error *);
bool qa_bot_goals_allocate(qa_bot_goals *, int32_t client, uint32_t *handle, qa_error *);
bool qa_bot_goals_has_handle(const qa_bot_goals *, uint32_t);
bool qa_bot_goals_free(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_reset(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_weights(qa_bot_goals *, uint32_t, qa_bot_weights *, qa_error *);
bool qa_bot_goals_load_weights(qa_bot_goals *, uint32_t, qa_bot_library *, const char *path,
                               int32_t *source_result, qa_error *);
bool qa_bot_goals_dump_stack(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_dump_avoid(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_interbreed(qa_bot_goals *, uint32_t first, uint32_t second, uint32_t child,
                             bool *matched, qa_error *);
bool qa_bot_goals_mutate(qa_bot_goals *, uint32_t, qa_error *);
/* Source SaveGoalFuzzyLogic validates the handle without writing a file. */
bool qa_bot_goals_save_weights(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_push(qa_bot_goals *, uint32_t, const qa_bot_goal *, bool *pushed, qa_error *);
bool qa_bot_goals_push_from(qa_bot_goals *, uint32_t, void *context,
                            bool (*read)(void *, qa_bot_goal *, qa_error *), bool *, qa_error *);
/* Reads occur after the source stack increment. Copy the first 56 bytes of
 * the supplied view without decoding float payloads. */
bool qa_bot_goals_push_source_from(qa_bot_goals *,uint32_t,void *,
                                   bool (*read)(void *,qa_bytes *,qa_error *),bool *,qa_error *);
bool qa_bot_goals_pop(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_empty(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_top(const qa_bot_goals *, uint32_t, bool second, qa_bot_goal *, bool *,
                      qa_error *);
/* Borrowed from the actual source allocation until that allocation is freed. */
bool qa_bot_goals_top_source(const qa_bot_goals *,uint32_t,bool second,qa_bytes *,bool *,qa_error *);
bool qa_bot_goals_avoid_clear(qa_bot_goals *, uint32_t, qa_error *);
bool qa_bot_goals_avoid_set(qa_bot_goals *, uint32_t, int32_t number, float duration, qa_error *);
bool qa_bot_goals_avoid_remove(qa_bot_goals *, uint32_t, int32_t number, qa_error *);
bool qa_bot_goals_avoid_time(const qa_bot_goals *, uint32_t, int32_t number, float *, qa_error *);
bool qa_bot_goals_capture(const qa_bot_goals *, uint32_t, qa_bot_goal_state *,
                          qa_bot_weights **borrowed_weights, qa_error *);
bool qa_bot_goals_restore(qa_bot_goals *, uint32_t, const qa_bot_goal_state *, qa_bot_weights *,
                          qa_error *);
typedef struct qa_bot_goal_choice {
    qa_vec3 origin;
    const int32_t *inventory;
    size_t inventory_count;
    const qa_bot_inventory_view *inventory_source;
    uint32_t travel_flags;
    bool nearby;
    const qa_bot_goal *long_term;
    float maximum_time;
} qa_bot_goal_choice;
bool qa_bot_goals_choose(qa_bot_goals *, uint32_t, const qa_bot_goal_choice *, bool *, qa_error *);
typedef enum qa_bot_source_goal_status {
    QA_BOT_GOAL_NATIVE,
    QA_BOT_GOAL_AVAILABLE,
    QA_BOT_GOAL_UNAVAILABLE
} qa_bot_source_goal_status;
bool qa_bot_goals_source_status(qa_bot_goals *, int32_t client, const qa_bot_goal *,
                                qa_bot_source_goal_status *, qa_error *);
bool qa_bot_goals_missing_visible(qa_bot_goals *, int32_t client, qa_vec3 eye, const qa_bot_goal *,
                                  bool *, qa_error *);
bool qa_bot_goal_touching(qa_vec3 origin, const qa_bot_goal *);
const char *qa_bot_goals_name(const qa_bot_goals *, int32_t number);
/* Load borrows the map's immutable entity table; repeated same-map restarts may
 * retain it. Live entity updates are obtained from services.entities. */
bool qa_bot_goals_load_map(qa_bot_goals *, const qa_entities *, qa_bot_navigation *, qa_error *);
bool qa_bot_goals_update_items(qa_bot_goals *, qa_bot_navigation *, qa_error *);
bool qa_bot_goals_find_entity(qa_bot_goals *, int32_t number, qa_error *);
bool qa_bot_goals_level_item(const qa_bot_goals *, int32_t after, const char *name,
                             qa_bot_goal *in_out, bool *, qa_error *);
bool qa_bot_goals_location(const qa_bot_goals *, const char *name, qa_bot_goal *in_out, bool *,
                           qa_error *);
bool qa_bot_goals_camp(const qa_bot_goals *, int32_t index, qa_bot_goal *in_out, int32_t *next,
                       qa_error *);

#endif

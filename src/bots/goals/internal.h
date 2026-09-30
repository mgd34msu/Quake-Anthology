#ifndef QA_BOT_GOALS_INTERNAL_H
#define QA_BOT_GOALS_INTERNAL_H
#include "qa/bot_goals.h"
#include "qa/bot_bsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

typedef struct bot_goal_weights {
    qa_bot_weights *weights;
    int32_t *indices;
    size_t users;
    struct bot_goal_weights *next;
} bot_goal_weights;
typedef struct bot_goal_slot {
    bool used;
    qa_bot_goal_state state;
    bot_goal_weights *weights;
} bot_goal_slot;
typedef struct bot_level_item {
    int32_t number, entity;
    uint32_t info, flags, previous, next;
    float weight, timeout;
    qa_vec3 origin, goal_origin;
    uint32_t goal_area;
} bot_level_item;
typedef struct bot_map_goal {
    char name[128];
    qa_vec3 origin;
    uint32_t area;
    float range, weight, wait, random;
} bot_map_goal;
typedef struct bot_source_goal {
    qa_actor_id actor;
    char *name;
    size_t name_capacity;
    qa_bot_goal goal;
    uint32_t hash_next;
} bot_source_goal;
struct qa_bot_goals {
    qa_bot_items *items;
    qa_bot_goal_options options;
    qa_bot_goal_services services;
    bot_goal_slot *states;
    bot_goal_weights *weights;
    qa_bot_weight_workspace *workspace;
    float time;
    bool busy, configured;
    const qa_entities *entities;
    bot_level_item *level;
    size_t level_capacity;
    uint32_t level_head, free_head;
    int32_t initial_count, next_source;
    bot_map_goal *locations, *camps;
    size_t location_count, camp_count;
    bot_source_goal *source;
    size_t source_count, source_capacity;
    uint32_t source_buckets[256];
};
bool bot_goal_fail(qa_error *, const char *);
bot_goal_slot *bot_goal_slot_get(const qa_bot_goals *, uint32_t, qa_error *);
bool bot_goal_mutable(qa_bot_goals *, qa_error *);
bool bot_goal_allowed(const qa_bot_goals *, uint32_t);
void bot_goal_avoid(qa_bot_goals *, bot_goal_slot *, int32_t, float);
float bot_goal_default_avoid(const qa_bot_item_info *);
qa_bot_goal bot_goal_item(const qa_bot_goals *, const bot_level_item *);
bot_level_item *bot_goal_find(const qa_bot_goals *, int32_t);
void bot_goal_map_clear(qa_bot_goals *);
bool bot_goal_push(qa_bot_goals *, bot_goal_slot *, const qa_bot_goal *, bool *);
bool bot_goal_dump_stack(qa_bot_goals *, const bot_goal_slot *, qa_error *);
void bot_goal_report(qa_bot_goals *, qa_script_severity, const char *);
void bot_goal_log(qa_bot_goals *, const char *);
bool bot_goal_position_report(qa_bot_goals *, const char *prefix, qa_vec3,
                                const char *suffix, qa_error *);
bool bot_goal_source_status(qa_bot_goals *, int32_t, const qa_bot_goal *,
                            qa_bot_source_goal_status *, qa_error *);
bool bot_goal_entities(qa_bot_goals *, const qa_bot_goal_entity **, size_t *, void **, qa_error *);
void bot_goal_entities_end(qa_bot_goals *, void *);
bool bot_goal_equal_name(const char *, const char *);
#endif

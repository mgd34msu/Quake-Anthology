#ifndef QA_BOT_GOALS_INTERNAL_H
#define QA_BOT_GOALS_INTERNAL_H
#include "qa/bot_goals.h"
#include "qa/bot_bsp.h"
#include "qa/bots_allocator.h"
#include "qa/id_index.h"
#include "qa/arena.h"
#include "source_state.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

typedef struct bot_goal_weights {
    qa_bot_weights *weights;
    uint32_t pointer;
    struct bot_goal_weights *next;
} bot_goal_weights;
typedef struct bot_goal_indexes {
    qa_bot_memory_allocation allocation;
    uint32_t pointer;
    size_t prepared_users;
    struct bot_goal_indexes *next;
} bot_goal_indexes;
typedef struct bot_goal_slot {
    bool used;
    bot_goal_record record;
} bot_goal_slot;
typedef struct bot_level_item {
    int32_t number, entity;
    uint32_t info, flags, previous, next;
    float weight, timeout;
    qa_vec3 origin, goal_origin;
    uint32_t goal_area;
} bot_level_item;
typedef struct bot_map_info {
    qa_bot_memory_allocation allocation;
    uint32_t pointer;
    bool camp;
} bot_map_info;
typedef struct bot_source_goal {
    qa_actor_id actor;
    qa_string_id name;
    qa_bot_goal goal;
} bot_source_goal;
enum {BOT_GOAL_ALIAS_CONFIG,BOT_GOAL_ALIAS_INDEX,BOT_GOAL_ALIAS_INFO,BOT_GOAL_ALIAS_WEIGHTS};
struct qa_bot_goals {
    qa_bot_items *items;
    qa_bot_goal_options options;
    qa_bot_goal_services services;
    qa_string_id info_camp, target_location;
    bot_goal_slot *states;
    bot_goal_weights *weights;
    qa_bot_memory *memory;
    qa_id_index aliases;
    bot_goal_indexes *indexes,*last_indexes,*prepared_indexes;
    uint64_t next_pointer;
    qa_bot_weight_workspace *workspace;
    float time;
    bool busy, configured,shared_memory;
    const qa_entities *entities;
    qa_bot_memory_allocation level_allocation;
    size_t level_capacity;
    uint32_t level_head, free_head;
    int32_t initial_count, next_source;
    bot_map_info *info;
    size_t info_count, info_capacity;
    uint64_t next_info;
    uint32_t location_head, camp_head;
    bot_source_goal *source;
    size_t source_count, source_capacity;
    qa_arena source_storage;
    qa_actor_index source_actors;
};
static inline uint64_t bot_goal_weights_discriminator(const qa_bot_weights *weights) {
    return (((uint64_t)(uintptr_t)weights >> 32) << 2) | BOT_GOAL_ALIAS_WEIGHTS;
}
static inline bool bot_goal_alias_read(const qa_bot_goals *goals,uint32_t pointer,
    uint64_t discriminator,size_t *out) {
    qa_id_index_cursor cursor=qa_id_index_begin(&goals->aliases,pointer,discriminator);
    return qa_id_index_next(&goals->aliases,&cursor,out);
}
bool bot_goal_aliases_reindex(qa_bot_goals *,qa_error *);
bool bot_goal_fail(qa_error *, const char *);
bot_goal_slot *bot_goal_slot_get(const qa_bot_goals *, uint32_t, qa_error *);
bool bot_goal_mutable(qa_bot_goals *, qa_error *);
bool bot_goal_allowed(const qa_bot_goals *, uint32_t);
bool bot_goal_avoid(qa_bot_goals *, bot_goal_slot *, int32_t, float,qa_error *);
float bot_goal_default_avoid(const qa_bot_item_info *);
bool bot_goal_item(const qa_bot_goals *, const bot_level_item *, qa_bot_goal *, qa_error *);
bool bot_goal_find(const qa_bot_goals *, int32_t, uint32_t *, bot_level_item *, bool *, qa_error *);
bool bot_goal_level_read(const qa_bot_goals *, uint32_t, bot_level_item *, qa_error *);
bool bot_goal_level_word(const qa_bot_goals *, uint32_t, uint32_t, uint32_t, qa_error *);
bool bot_goal_level_vector(const qa_bot_goals *, uint32_t, uint32_t, qa_vec3, qa_error *);
bool bot_goal_level_topology(const qa_bot_goals *, qa_error *);
void bot_goal_map_clear(qa_bot_goals *);
bool bot_goal_map_info_load(qa_bot_goals *,qa_bot_navigation *,qa_error *);
bool bot_goal_info_free(qa_bot_goals *, qa_error *);
bool bot_goal_info_span(const qa_bot_goals *, uint32_t, qa_bot_memory_span *, bool *, qa_error *);
bool bot_goal_info_topology(const qa_bot_goals *, qa_error *);
bool bot_goal_push(qa_bot_goals *, bot_goal_slot *, const qa_bot_goal *, bool *, qa_error *);
bool bot_goal_dump_stack(qa_bot_goals *, const bot_goal_slot *, qa_error *);
bool bot_goal_report(qa_bot_goals *, qa_script_severity, const char *,qa_error *);
bool bot_goal_log(qa_bot_goals *, const char *, qa_error *);
bool bot_goal_position_report(qa_bot_goals *, const char *prefix, qa_vec3,
                                const char *suffix, qa_error *);
bool bot_goal_source_status(qa_bot_goals *, int32_t, const qa_bot_goal *,
                            qa_bot_source_goal_status *, qa_error *);
bool bot_goal_entities(qa_bot_goals *, const qa_bot_goal_entity **, size_t *, void **, qa_error *);
void bot_goal_entities_end(qa_bot_goals *, void *);
bool bot_goal_equal_name(const char *, const char *);
void bot_goal_sources_clear(qa_bot_goals *);
bool bot_goal_sources_reserve(qa_bot_goals *,size_t,qa_error *);
bool bot_goal_sources_reindex(qa_bot_goals *,qa_error *);
const bot_source_goal *bot_goal_source_number(const qa_bot_goals *,int32_t);
bool bot_goal_memory_bind(qa_bot_goals *,qa_bot_memory *,qa_error *);
bool bot_goal_config_set(qa_bot_goals *,bot_goal_slot *,qa_bot_weights *,qa_error *);
bool bot_goal_config_get(const qa_bot_goals *,const bot_goal_slot *,bot_goal_weights **,qa_error *);
bool bot_goal_indexes_create(qa_bot_goals *,uint32_t,qa_bot_memory_allocation *,qa_error *);
bool bot_goal_indexes_write(qa_bot_goals *,qa_bot_memory_allocation,uint32_t,int32_t,qa_error *);
bool bot_goal_indexes_read(qa_bot_goals *,const bot_goal_slot *,int32_t,int32_t *,qa_error *);
bool bot_goal_indexes_publish(qa_bot_goals *,bot_goal_slot *,qa_bot_memory_allocation,qa_error *);
bool bot_goal_indexes_get(const qa_bot_goals *,const bot_goal_slot *,qa_bot_memory_allocation *,bool *,qa_error *);
bool bot_goal_indexes_drop(qa_bot_goals *,bot_goal_slot *,qa_error *);
void bot_goal_indexes_clear(qa_bot_goals *);
#endif

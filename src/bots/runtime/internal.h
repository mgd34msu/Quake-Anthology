#ifndef QA_BOT_RUNTIME_INTERNAL_H
#define QA_BOT_RUNTIME_INTERNAL_H
#include "qa/bot_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

typedef struct bot_weapon_state {
    bool used;
    qa_bot_weights *weights;
    qa_bot_weapon_selector *selector;
} bot_weapon_state;
typedef struct bot_observation_link {
    size_t previous, next, hash_next;
} bot_observation_link;
struct qa_bot_runtime {
    qa_bot_runtime_options options;
    qa_bot_runtime_services services;
    qa_bot_library *library;
    qa_bot_actions *actions;
    qa_bot_goals *goals;
    qa_bot_moves *moves;
    qa_bot_chat_system *chat_system;
    qa_bot_chat **chats;
    qa_bot_character **characters;
    bot_weapon_state *weapons;
    qa_bot_weapons *weapon_config;
    qa_bot_entity_info *entities;
    qa_bot_goal_entity *goal_entities;
    size_t entity_capacity, observation_leases, owner_leases;
    bot_observation_link *observation_links;
    size_t *observation_buckets;
    size_t observation_head, observation_tail, observation_free;
    qa_bot_bsp *bsp;
    qa_bot_runtime_map map;
    char *map_name;
    float time;
    bool initialized, library_initialized, loaded, bsp_loaded, closed, busy, restore_pending;
};
bool bot_runtime_fail(qa_error *, const char *);
bool bot_runtime_mutable(qa_bot_runtime *, qa_error *);
bool bot_runtime_owners_idle(qa_bot_runtime *, qa_error *);
bool bot_runtime_variable(qa_bot_runtime *, const char *, const char *, const qa_bot_variable **, qa_error *);
bool bot_runtime_integer(qa_bot_runtime *, const char *, const char *, int32_t *, qa_error *);
void bot_runtime_handles_close(qa_bot_runtime *);
bool bot_runtime_observations_resize(qa_bot_runtime *, size_t, qa_error *);
bool bot_runtime_owners_create(qa_bot_runtime *, qa_error *);
void bot_runtime_observations_clear(qa_bot_runtime *);
void bot_runtime_observations_close(qa_bot_runtime *);
const qa_bot_entity_info *bot_runtime_observation(const qa_bot_runtime *, int32_t);
size_t bot_runtime_observation_bucket(int32_t, size_t);
qa_bot_goal_services bot_runtime_goal_services(qa_bot_runtime *);
#endif

#ifndef QA_BOT_RUNTIME_H
#define QA_BOT_RUNTIME_H

#include "qa/bot_actions.h"
#include "qa/bot_bsp.h"
#include "qa/bot_chat.h"
#include "qa/bot_goals.h"
#include "qa/bot_movement.h"

typedef struct qa_bot_runtime qa_bot_runtime;
typedef enum qa_bot_observation_profile {
    QA_BOT_OBSERVATION_NATIVE, QA_BOT_OBSERVATION_MODULE
} qa_bot_observation_profile;
typedef struct qa_bot_entity_update {
    qa_actor_id actor;
    int32_t type, flags;
    qa_vec3 origin, angles, old_origin, mins, maxs;
    int32_t ground_entity, solid, model_index, model_index2, frame, event, event_parameter,
            powerups, weapon, legs_animation, torso_animation;
} qa_bot_entity_update;
typedef struct qa_bot_entity_info {
    qa_bot_entity_update state;
    bool valid;
    int32_t number;
    qa_vec3 last_visible_origin;
    float last_update_time, update_interval;
} qa_bot_entity_info;
typedef struct qa_bot_runtime_services {
    void *context;
    qa_bot_random_source random;
    qa_bot_navigation *(*navigation)(void *, int32_t client);
    bool (*command)(void *, int32_t client, const char *, qa_error *);
    void (*diagnostic)(void *, qa_script_severity, const char *);
    qa_bot_goal_services goals;
    qa_bot_move_services movement;
} qa_bot_runtime_services;
typedef struct qa_bot_runtime_options {
    qa_bot_library_options library;
    qa_bot_observation_profile observations;
    uint32_t maximum_states; /* Zero selects source 64; native sessions may raise it. */
    uint32_t minimum_clients; /* Canonical actor client namespace reserved by the application. */
    bool debug;
} qa_bot_runtime_options;
typedef struct qa_bot_runtime_map {
    const char *name;
    const qa_entities *entities;
    qa_bytes source_entities; /* When supplied, source botlib grammar is used. */
    qa_bot_navigation *navigation;
} qa_bot_runtime_map;
bool qa_bot_runtime_create(const qa_bot_runtime_options *, const qa_bot_runtime_services *,
                            qa_bot_runtime **, qa_error *);
/* Fails without retiring storage when a callback or borrowed owner is active. */
bool qa_bot_runtime_destroy(qa_bot_runtime *, qa_error *);
bool qa_bot_runtime_can_destroy(const qa_bot_runtime *);
/* Keep borrowed bot owners alive across observed source reads and writes.
 * Ordinary bot calls may nest; owner replacement and teardown require no
 * outstanding leases. End each successful begin exactly once. */
bool qa_bot_runtime_lease_begin(qa_bot_runtime *, qa_error *);
void qa_bot_runtime_lease_end(qa_bot_runtime *);
bool qa_bot_runtime_setup(qa_bot_runtime *, int32_t *source_result, qa_error *);
bool qa_bot_runtime_shutdown(qa_bot_runtime *, qa_error *);
bool qa_bot_runtime_initialized(const qa_bot_runtime *);
bool qa_bot_runtime_loaded(const qa_bot_runtime *);
bool qa_bot_runtime_closed(const qa_bot_runtime *);
float qa_bot_runtime_time(const qa_bot_runtime *);
bool qa_bot_runtime_debug(const qa_bot_runtime *);
qa_bot_random_source qa_bot_runtime_random_source(const qa_bot_runtime *);
qa_bot_library *qa_bot_runtime_library(qa_bot_runtime *);
bool qa_bot_runtime_variable_get_from(qa_bot_runtime *, void *context,
                                       bool (*name_byte)(void *, size_t, uint8_t *, qa_error *),
                                       const char **, qa_error *);
bool qa_bot_runtime_variable_set_from(qa_bot_runtime *, void *context,
                                       bool (*name_byte)(void *, size_t, uint8_t *, qa_error *),
                                       bool (*name)(void *, const char **, qa_error *),
                                       bool (*value)(void *, const char **, qa_error *), qa_error *);
qa_bot_actions *qa_bot_runtime_actions(qa_bot_runtime *);
qa_bot_goals *qa_bot_runtime_goals(qa_bot_runtime *);
qa_bot_moves *qa_bot_runtime_moves(qa_bot_runtime *);
qa_bot_chat_system *qa_bot_runtime_chat_system(qa_bot_runtime *);
qa_bot_navigation *qa_bot_runtime_navigation(qa_bot_runtime *, int32_t client);
bool qa_bot_runtime_predict_movement(qa_bot_runtime *, int32_t client,
                                      const qa_bot_movement_prediction_query *,
                                      qa_bot_movement_prediction *, qa_error *);
/* Attach borrows immutable map metadata and the selected shared navigation.
 * It does not spawn actors or construct another collision world. */
bool qa_bot_runtime_attach_map(qa_bot_runtime *, const qa_bot_runtime_map *, qa_error *);
/* Replace round observers after GAME/AI shutdown while retaining the exact
 * map, actual library initialization state and existing static goal metadata. */
bool qa_bot_runtime_rebind_round(qa_bot_runtime *, const qa_bot_runtime_map *, qa_error *);
bool qa_bot_runtime_load_map(qa_bot_runtime *, const char *name, qa_error *);
const qa_entities *qa_bot_runtime_bsp(const qa_bot_runtime *);
bool qa_bot_runtime_start_frame(qa_bot_runtime *, float time, qa_error *);
bool qa_bot_runtime_update_entity(qa_bot_runtime *, int32_t number,
                                   const qa_bot_entity_update *, qa_error *);
bool qa_bot_runtime_entity(const qa_bot_runtime *, int32_t number, qa_bot_entity_info *, bool *found,
                            qa_error *);
int32_t qa_bot_runtime_next_entity(const qa_bot_runtime *, int32_t after);
/* Source insertion order for module observations, numeric order for native.
 * NULL begins enumeration; a missing previous key ends it. Values are copied. */
bool qa_bot_runtime_entity_after(const qa_bot_runtime *, const int32_t *previous,
                                  qa_bot_entity_info *, bool *found, qa_error *);
/* Native observations invalidate visibility once per source frame; module
 * observations retain validity until explicit removal, matching that provider. */
bool qa_bot_runtime_invalidate_entities(qa_bot_runtime *, qa_error *);
bool qa_bot_runtime_invalidate_entity_range(qa_bot_runtime *, int32_t first, uint32_t count, qa_error *);
bool qa_bot_runtime_character_load(qa_bot_runtime *, const char *, float skill,
                                   uint32_t *handle, qa_error *);
bool qa_bot_runtime_character_free(qa_bot_runtime *, uint32_t, qa_error *);
const qa_bot_character *qa_bot_runtime_character(const qa_bot_runtime *, uint32_t);
/* Source invalid handles/indices/types diagnose and produce zero. A string
 * query reports written=false instead of modifying the guest destination. */
bool qa_bot_runtime_character_float(qa_bot_runtime *, uint32_t, uint32_t, float *, qa_error *);
bool qa_bot_runtime_character_integer(qa_bot_runtime *, uint32_t, uint32_t, int32_t *, qa_error *);
bool qa_bot_runtime_character_string(qa_bot_runtime *, uint32_t, uint32_t,
                                      const char **, bool *written, qa_error *);
bool qa_bot_runtime_character_bounded_float(qa_bot_runtime *, uint32_t, uint32_t,
                                            float, float, float *, qa_error *);
bool qa_bot_runtime_character_bounded_integer(qa_bot_runtime *, uint32_t, uint32_t,
                                              int32_t, int32_t, int32_t *, qa_error *);
bool qa_bot_runtime_weapon_allocate(qa_bot_runtime *, uint32_t *, qa_error *);
bool qa_bot_runtime_weapon_has_handle(const qa_bot_runtime *, uint32_t);
bool qa_bot_runtime_weapon_free(qa_bot_runtime *, uint32_t, qa_error *);
bool qa_bot_runtime_weapon_reset(qa_bot_runtime *, uint32_t, qa_error *);
/* Private weapon AI has only learned weights and their derived index map.
 * Capture returns an owned clone; restore builds its selector before swap. */
bool qa_bot_runtime_weapon_capture(const qa_bot_runtime *, uint32_t, qa_bot_weights **, qa_error *);
bool qa_bot_runtime_weapon_restore(qa_bot_runtime *, uint32_t, qa_bot_weights *, qa_error *);
bool qa_bot_runtime_weapon_weights(qa_bot_runtime *, uint32_t, const char *,
                                    int32_t *source_result, qa_error *);
bool qa_bot_runtime_weapon_weights_from(qa_bot_runtime *, uint32_t, void *context,
                                          bool (*path)(void *, const char **, qa_error *),
                                          int32_t *source_result, qa_error *);
bool qa_bot_runtime_weapon_info(qa_bot_runtime *, uint32_t, uint32_t weapon,
                                 qa_bot_weapon_info *, qa_bot_projectile_info *, bool *, qa_error *);
bool qa_bot_runtime_weapon_choose(qa_bot_runtime *, uint32_t, const int32_t *inventory, size_t,
                                   uint32_t *weapon, qa_error *);
bool qa_bot_runtime_weapon_choose_view(qa_bot_runtime *, uint32_t,
                                        const qa_bot_inventory_view *, uint32_t *, qa_error *);
bool qa_bot_runtime_weapon_weight(qa_bot_runtime *, uint32_t, uint32_t weapon,
                                   const int32_t *inventory, size_t, float *, bool *, qa_error *);
bool qa_bot_runtime_chat_allocate(qa_bot_runtime *, uint32_t *, qa_error *);
bool qa_bot_runtime_chat_free(qa_bot_runtime *, uint32_t, qa_error *);
qa_bot_chat *qa_bot_runtime_chat(qa_bot_runtime *, uint32_t);
bool qa_bot_runtime_chat_load(qa_bot_runtime *, uint32_t, const char *path, const char *name,
                               int32_t *source_result, qa_error *);
bool qa_bot_runtime_goal_weights(qa_bot_runtime *, uint32_t, const char *path,
                                  int32_t *source_result, qa_error *);
bool qa_bot_runtime_init_level_items(qa_bot_runtime *, qa_error *);

#endif

#ifndef QA_APPLICATION_BOTS_PRIVATE_H
#define QA_APPLICATION_BOTS_PRIVATE_H

#include "internal.h"
#include "qa/bots.h"
#include <math.h>

static inline int32_t application_bot_angle_word(float angle) {
    float reduced=fmodf(angle,360.0f);
    return (int32_t)(uint16_t)(int32_t)(reduced*(65536.0f/360.0f));
}

typedef struct application_bot_graph {
    application_provider *movement;
    qa_movement_profile profile;
    qa_bounds bounds;
    qa_nav_graph *graph;
    qa_navigation *navigation;
    struct application_bot_graph *next;
} application_bot_graph;
typedef struct application_bot_seat {
    qa_actor_id actor;
    uint32_t seat;
    qa_bot_navigation *navigation;
    uint64_t last_command_ns;
    bool retired;
} application_bot_seat;
typedef struct application_bot_target {
    qa_actor_id actor;
    application_provider *movement;
    qa_movement_profile profile;
    qa_bounds bounds;
    qa_bot_navigation *navigation;
    bool predicting;
    struct application_bot_target *next;
} application_bot_target;
typedef struct application_bot_guest {
    application_provider *provider;
    uint32_t client_base, entity_base;
    struct application_bot_guest *next;
} application_bot_guest;
typedef struct application_bots {
    qa_application *application;
    qa_bot_runtime *runtime;
    qa_bots *population;
    qa_vfs *files;
    qa_resource *map_resource;
    qa_bsp_view geometry;
    qa_entities entities;
    application_bot_graph *graphs;
    qa_bot_navigation *map_navigation;
    application_bot_target *targets;
    application_bot_guest *guests;
    application_bot_seat *seats;
    uint32_t capacity,metadata_weapon;
    qa_bot_weapon_knowledge knowledge[QA_Q2_WEAPON_COUNT];
    size_t knowledge_count,arsenal_leases;
    qa_bot_controls controls;
    size_t calls;
    qa_builtin_actor_snapshot pickup_snapshot;
    bool pickup_borrowed,mover_borrowed;
    qa_nav_train_stop *train_stops;
    size_t train_capacity;
} application_bots;

bool application_bot_player(void *,qa_actor_id,qa_bot_player *,qa_error *);
bool application_bot_entity(void *,qa_actor_id,qa_bot_entity *,qa_error *);
bool application_bot_arsenal(void *,qa_actor_id,const qa_bot_weapon_knowledge **,size_t *,void **,qa_error *);
void application_bot_arsenal_end(void *,void *);
bool application_bot_submit(void *,qa_actor_id,const qa_bot_input *,const qa_movement_command *,qa_error *);
bool application_bot_navigation_bind(application_bots *,application_bot_seat *,qa_error *);
bool application_bot_navigation_prepare(application_bots *,qa_error *);
bool application_bots_prepare(qa_application *,const qa_launch_choices *,const qa_bsp_view *,const qa_entities *,qa_error *);
qa_bot_runtime *application_bots_runtime(qa_application *);
bool application_bots_guest_admit(qa_application *,qa_actor_id,qa_error *);
bool application_bots_guest_bind(qa_application *,application_provider *,qa_q3_host *,qa_error *);
qa_actor_id application_bot_client_actor(application_bots *,int32_t);
qa_bot_navigation *application_bot_navigation(void *,int32_t);
bool application_bot_movement_input(void *,qa_actor_id,qa_movement_input *,qa_error *);
bool application_bot_inventory(application_bots *,qa_actor_id,int32_t [QA_BOT_INVENTORY_SIZE],qa_error *);
qa_actor_id application_bot_actor(void *,int32_t);
bool application_bot_travel_model(void *,int32_t,qa_bot_travel_model *,bool *,qa_error *);
bool application_bot_activation(void *,qa_actor_id,int32_t,qa_bot_activation *,bool *,qa_error *);
bool application_bot_travel_weapon(void *,int32_t,qa_nav_travel,int32_t *,bool *,qa_error *);
bool application_bot_grapple_state(void *,int32_t,qa_bot_grapple_observation *,qa_error *);
bool application_bot_predict_motion(void *,qa_actor_id,const qa_bot_movement_prediction_query *,
                                     qa_bot_movement_prediction *,bool *,qa_error *);

#endif

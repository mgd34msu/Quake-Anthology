#ifndef QA_APPLICATION_BOTS_PRIVATE_H
#define QA_APPLICATION_BOTS_PRIVATE_H

#include "internal.h"
#include "qa/bots.h"
#include "qa/bots_save.h"
#include "qa/bots_catalog.h"
#include "qa/arena.h"
#include "qa/pool.h"
#include <math.h>

static inline bool application_bot_integer(double value,int32_t *out,qa_error *error) {
    if (!isfinite(value) || value<=-2147483649.0 || value>=2147483648.0) {
        (void)application_fail(error,QA_ERROR_ARGUMENT,"Bot observation exceeds its native integer field");
        return false;
    }
    *out=(int32_t)value; return true;
}

typedef struct application_bot_graph {
    size_t storage_slot;
    application_provider *movement;
    qa_movement_profile profile;
    qa_bounds bounds;
    qa_nav_graph *graph;
    qa_navigation *navigation;
    qa_resource *asset_resource;
    qa_vfs_acquisition asset_acquisition;
    struct application_bot_graph *next;
} application_bot_graph;
typedef struct application_bot_seat {
    qa_actor_id actor;
    uint32_t seat, library_client;
    qa_bot_navigation *navigation;
} application_bot_seat;
typedef enum application_bot_round_phase {
    APPLICATION_BOT_ROUND_ACTIVE, APPLICATION_BOT_ROUND_DETACHED,
    APPLICATION_BOT_ROUND_BOUND, APPLICATION_BOT_ROUND_FAILED
} application_bot_round_phase;
typedef struct application_bot_target {
    size_t storage_slot;
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
    qa_bot_runtime *runtime;
    qa_bots_save_requirements saved_requirements;
    qa_bytes saved_runtime;
    uint32_t client_base, entity_base;
    struct application_bot_guest *next;
} application_bot_guest;
typedef struct application_bot_script_file {
    qa_resource *resource;
    qa_vfs_acquisition acquisition;
    uint64_t position;
    struct application_bot_script_file *next;
} application_bot_script_file;
typedef struct application_bots {
    qa_application *application;
    application_provider *source;
    qa_bot_runtime *runtime;
    qa_bots *population;
    qa_bot_catalog *catalogue;
    char catalogue_name[36];
    bool catalogue_spawn,catalogue_ready;
    const qa_launch_seat *catalogue_seat;
    struct application_bot_world *shared_world;
    struct application_bot_world_binding *shared_binding;
    struct application_bot_transport *transport;
    qa_vfs *files;
    qa_ruleset_id behavior;
    qa_bot_source_knowledge *source_knowledge;
    qa_application_event_subscription hearing_cursor;
    qa_unified_frame_lease *hearing_storage;
    struct application_bot_heard_sound *hearing_rows;
    size_t hearing_count;
    uint64_t hearing_retire,hearing_persist_ns;
    float walk_speed;
    application_bot_script_file *script_files;
    qa_vfs *navigation_files;
    bool files_launch;
    qa_string_id files_product;
    qa_resource *map_resource;
    qa_resource *source_map_resource;
    qa_bsp_view geometry;
    qa_entities entities;
    application_bot_graph *graphs;
    qa_arena navigation_owner_storage;
    qa_pool graph_records, target_records;
    qa_bot_navigation *map_navigation;
    application_bot_target *targets;
    qa_actor_index actor_targets;
    application_bot_guest *guests;
    application_bot_seat *seats;
    qa_actor_index actor_seats;
    uint32_t *client_seats;
    uint32_t capacity, seat_capacity;
    qa_bot_weapon_knowledge knowledge[QA_Q2_WEAPON_COUNT];
    struct application_bot_knowledge_owner *knowledge_owner;
    size_t knowledge_count,arsenal_leases;
    size_t calls;
    qa_arena observation_storage;
    qa_world_actor_snapshot pickup_snapshot;
    bool pickup_borrowed,mover_borrowed;
    qa_nav_train_stop *train_stops;
    size_t train_capacity;
    qa_bots_save_requirements saved_requirements;
    bool restoring, navigation_restored, runtime_restored;
    qa_bytes saved_bot_record, saved_navigation_record, saved_runtime, saved_population;
    qa_bytes saved_shared_world,saved_transport;
    qa_bytes saved_catalogue;
    application_bot_round_phase round_phase;
    struct application_bots_round *round;
    struct application_bots_original *original;
    qa_source_frame producer_frame;
    uint64_t producer_host_ns;
    bool producing;
} application_bots;

static inline bool application_bot_seat_live(const application_bots *bots, const application_bot_seat *seat)
{
    return qa_actors_get(qa_session_actors(bots->application->session), seat->actor) != NULL;
}
static inline application_bot_seat *application_bot_seat_find(const application_bots *bots, qa_actor_id actor)
{
    size_t row;
    if (!bots || !qa_actor_index_find(&bots->actor_seats, actor, &row) || row >= bots->capacity) return NULL;
    application_bot_seat *seat = bots->seats + row;
    return application_bot_seat_live(bots, seat) ? seat : NULL;
}
static inline application_bot_target *application_bot_target_find(const application_bots *bots, qa_actor_id actor)
{
    size_t value;
    return bots && qa_actor_index_find(&bots->actor_targets, actor, &value)
        ? (application_bot_target *)(uintptr_t)value : NULL;
}
static inline void application_bot_target_index_bind(application_bots *bots, application_bot_target *target)
{
    qa_actor_index_put(&bots->actor_targets, target->actor, (size_t)(uintptr_t)target);
}
bool application_bots_seats_prepare(application_bots *, qa_error *);
bool application_bots_observations_prepare(application_bots *, qa_error *);
application_bot_graph *application_bot_graph_take(application_bots *, qa_error *);
void application_bot_graph_drop(application_bots *, application_bot_graph *);
application_bot_target *application_bot_target_take(application_bots *, qa_error *);
void application_bot_target_drop(application_bots *, application_bot_target *);
void application_bot_seat_index_bind(application_bots *, application_bot_seat *);
void application_bot_seat_index_remove(application_bots *, application_bot_seat *);

bool application_bot_player(void *,qa_actor_id,qa_bot_player *,qa_q3_player *,qa_error *);
bool application_bot_inventory_update(void *,qa_actor_id,const qa_bot_player *,const qa_bot_player_state_view *,const qa_bot_inventory_target *,qa_error *);
bool application_bot_source_weapon(application_bots *,qa_actor_id,int32_t *,int32_t *,qa_error *);
bool application_bot_entity(void *,qa_actor_id,qa_bot_entity *,qa_error *);
bool application_bot_arsenal(void *,qa_actor_id,const qa_bot_weapon_knowledge **,size_t *,void **,qa_error *);
void application_bot_arsenal_end(void *,void *);
bool application_bot_submit(void *,qa_actor_id,const qa_bot_input *,const qa_usercmd *,qa_error *);
bool application_bot_weapon_apply(qa_application *,qa_actor_id,qa_actor_owner,qa_item_id,qa_error *);
bool application_bot_navigation_bind(application_bots *,application_bot_seat *,qa_error *);
bool application_bot_navigation_prepare(application_bots *,qa_error *);
qa_navigation_services application_bot_navigation_services(application_bots *);
bool application_bot_navigation_rebuild(application_bots *,application_bot_graph *,qa_error *);
bool application_bot_navigation_load(void *,qa_bot_runtime *,qa_error *);
bool application_bot_navigation_restore_binding(application_bots *,qa_navigation *,qa_actor_id,
                                                 qa_bot_navigation **,qa_error *);
bool application_bots_construct_restored(application_bots *,qa_error *);
qa_bot_services application_bots_services(application_bots *);
application_provider *application_bot_source(application_bots *);
bool application_bot_entity_number(application_bots *, qa_actor_id, int32_t *, qa_error *);
bool application_bots_frame_at(qa_application *,const qa_source_frame *,size_t,uint64_t,qa_error *);
bool application_native_q3_match_bots_end(application_provider *,qa_error *);
bool application_bots_test_aas(application_provider *,qa_vec3,qa_error *);
bool application_bots_native_q3_initialize(application_provider *,qa_error *);
bool application_bots_native_q3_connect(application_provider *,qa_actor_id,bool,bool *,qa_error *);
bool application_bots_client_prepare(qa_application *,qa_actor_id,uint32_t,qa_error *);
bool application_bots_shared_connect(application_bots *,uint32_t,bool,bool *,qa_error *);
bool application_bots_shared_construct(application_bots *,bool,qa_error *);
bool application_bots_prepare(qa_application *,const qa_launch_choices *,const qa_bsp_view *,const qa_entities *,qa_error *);
qa_bot_runtime *application_bots_runtime(qa_application *);
bool application_bots_runtime_create(application_bots *,const qa_bot_runtime_options *,qa_bot_runtime **,qa_error *);
qa_bot_runtime *application_bots_guest_runtime(qa_application *,application_provider *);
bool application_bots_guest_admit(qa_application *,qa_actor_id,qa_error *);
bool application_bots_guest_bind(qa_application *,application_provider *,qa_q3_host *,qa_error *);
qa_actor_id application_bot_client_actor(application_bots *,int32_t);
qa_bot_navigation *application_bot_navigation(void *,int32_t);
bool application_bot_movement_input(void *,qa_actor_id,qa_movement_input *,qa_error *);
bool application_bot_inventory(application_bots *,qa_actor_id,const qa_bot_inventory_target *,qa_error *);
bool application_bot_weapon_slot(application_provider *,int32_t source,int32_t *,qa_error *);
bool application_bot_weapon_resolve(application_bots *,qa_actor_id,int32_t slot,qa_item_id *,qa_error *);
qa_actor_id application_bot_actor(void *,int32_t);
bool application_bot_travel_model(void *,int32_t,qa_bot_travel_model *,bool *,qa_error *);
bool application_bot_static_ground(void *,int32_t,bool *,qa_error *);
bool application_bot_lift_ground(application_bots *,qa_actor_id,qa_nav_entity_state *,bool *,qa_error *);
bool application_bot_activation(void *,qa_actor_id,int32_t,qa_bot_activation *,bool *,qa_error *);
bool application_bot_source_activation(void *,qa_actor_id,int32_t,const qa_bot_activation_query *,
    struct qa_bot_source_activation *,int32_t *,qa_error *);
bool application_bot_source_model_bounds(void *,int32_t,int32_t,int32_t,qa_vec3 *,qa_vec3 *,int32_t *,qa_error *);
bool application_bot_travel_weapon(void *,int32_t,qa_nav_travel,int32_t *,bool *,qa_error *);
bool application_bot_grapple_state(void *,int32_t,qa_bot_grapple_observation *,qa_error *);
bool application_bot_predict_motion(void *,qa_actor_id,const qa_bot_movement_prediction_query *,
                                     qa_bot_movement_prediction *,bool *,qa_error *);

#endif

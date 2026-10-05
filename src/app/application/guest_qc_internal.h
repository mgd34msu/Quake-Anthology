#ifndef APPLICATION_GUEST_QC_INTERNAL_H
#define APPLICATION_GUEST_QC_INTERNAL_H
#include "internal.h"
#include "client_outputs.h"
#include "qa/qc_host.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q1_qw.h"
#include "qa/network_q1_channel.h"
#include "qa/console_cvar_observer.h"
#include "qa/model.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct application_qc_resource {
    char *name;
    qa_resource *source;
    qa_vfs_acquisition acquisition;
    qa_qc_game_resource value;
    qa_qc_resource_kind kind;
    uint32_t inline_model;
    bool world_model, has_inline_model;
} application_qc_resource;
typedef struct application_qc_client {
    qa_actor_id actor;
    uint32_t seat;
    float parms[16];
    uint8_t colors;
    bool connected, spawned, prepared, has_parms;
    bool spectator;
    bool primary_character;
    bool output_published;
    bool receipt_seen;
    uint64_t receipt_sequence;
    uint64_t receipt_ordinal;
    qa_item_id pending_weapon;
    bool pending_weapon_following;
    application_client_outputs outputs;
} application_qc_client;
typedef struct application_qc_message {
    uint32_t destination;
    qa_actor_id recipient;
    uint8_t *data;
    size_t size, capacity;
    bool overflowed;
    qa_application_protocol_reference *references;
    size_t reference_count, reference_capacity;
} application_qc_message;
typedef struct application_qc_actor {
    struct application_qc_state *engine;
    qa_actor_id actor;
    int32_t reference;
    bool collision_bound;
} application_qc_actor;
struct application_qc_state {
    struct application_qc_combat *combat;
    struct application_qc_protection *protection;
    struct application_qc_item_weapon_actor *item_weapon_actors;
    struct application_qc_item_actor *item_actors;
    struct application_qc_pickup_actor *pickup_actors;
    application_provider *provider;
    qa_world *world;
    qa_builtin_services services;
    qa_builtin_random random;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    qa_qc_profile profile;
    qa_net_protocol_id protocol;
    application_qc_resource *resources;
    size_t resource_count, resource_capacity;
    application_qc_client *clients;
    uint32_t max_clients, check_slot;
    double check_time;
    int32_t check_cluster;
    uint64_t source_time_ns;
    qa_source_frame frame;
    bool has_frame;
    bool loading, projecting;
    bool initialized, console_prepared, callbacks_active;
    uint8_t output_channels;
    struct application_qc_input_scope *input_scope;
    struct application_qc_parked_input *parked_inputs;
    const float *client_think_time;
    float serverflags;
    qa_buffer original_extension;
    qa_buffer npc_restore;
    struct application_qc_rerelease *rerelease;
    char *lightstyles[64];
    uint64_t lightstyle_revision;
    application_qc_message *messages;
    size_t message_count, message_capacity;
    qa_builtin_actor_snapshot observations;
    application_qc_actor *actors;
    uint32_t actor_capacity;
};
static inline bool application_qc_has_source_admission(const struct application_qc_state *engine)
{
    qa_source_frame frame;
    qa_source_command command;
    return qa_session_active_frame(engine->services.session, engine->provider->owner, &frame) ||
        qa_session_active_command(engine->services.session, engine->provider->owner, &command);
}
bool application_qc_import(void *, qa_qc_instance *, qa_qc_builtin, const char *, qa_error *);
bool application_qc_capture_engine(void *, qa_buffer *, qa_error *);
bool application_qc_restore_engine(void *, qa_bytes, qa_error *);
bool application_qc_npc_restore_finish(application_provider *, qa_error *);
bool application_qc_flush(struct application_qc_state *, qa_error *);
bool application_qc_write_message(struct application_qc_state *, qa_qc_instance *, qa_qc_builtin, qa_error *);
bool application_qc_multicast(struct application_qc_state *, qa_qc_instance *, qa_error *);
bool application_qc_resource_lookup(void *, qa_qc_resource_kind, const char *, bool,
                                    qa_qc_game_resource *, qa_error *);
bool application_qc_resource_resolve_inline(application_qc_resource *, qa_error *);
const qa_qc_definition *application_qc_field(struct application_qc_state *, const char *, qa_qc_value_type, qa_error *);
bool application_qc_float(struct application_qc_state *, int32_t, const char *, float *, qa_error *);
bool application_qc_set_float(struct application_qc_state *, int32_t, const char *, float, qa_error *);
bool application_qc_reference(struct application_qc_state *, qa_actor_id, int32_t *, qa_error *);
bool application_qc_named(struct application_qc_state *, const char *, qa_actor_id, qa_error *);
bool application_qc_water_transition(application_provider *, qa_actor_id, qa_error *);
bool application_qc_spectator_callback(struct application_qc_state *, const char *, qa_actor_id, qa_error *);
bool application_qc_create_console(struct application_qc_state *, qa_cvars *, qa_console **, qa_error *);
bool application_qc_prepare_entity(void *, qa_qc_instance *, const qa_qc_entity_access *, qa_error *);
bool application_qc_may_move(void *, qa_actor_id);
bool application_qc_input_idle(const application_provider *);
bool application_qc_console_command(application_provider *, qa_actor_id, const char *,
                                      bool client_command, bool *handled, qa_error *);
bool application_qc_think_binding(application_provider *,qa_actor_id,uint32_t,
                                  qa_think_fn *,void **,qa_error *);
bool application_qc_input_abort(application_provider *, qa_actor_id, bool, qa_error *);
bool application_qc_reserve_player(application_provider *, uint32_t slot, uint32_t seat,
    qa_actor_id, const char *name, bool spectator, bool new_player, bool primary_character, qa_error *);
bool application_qc_begin_player(application_provider *, qa_actor_id, qa_error *);
bool application_qc_prepare_player(application_provider *, qa_actor_id, qa_error *);
bool application_qc_source_clients_initialize(struct application_qc_state *, qa_error *);
bool application_qc_source_client_released(struct application_qc_state *, qa_actor_record, qa_error *);
bool application_qc_control_source_client(const application_provider *, qa_actor_id, bool *, qa_error *);
bool application_qc_player_source_actor(application_provider *, uint32_t, qa_actor_id *, qa_error *);
bool application_qc_client_colors(application_provider *, qa_actor_id, int32_t top, int32_t bottom, qa_error *);
bool application_qc_player_receive(application_provider *, qa_actor_id, uint64_t ordinal, qa_movement_command *, qa_error *);
struct qa_application_qc_weapon_ui_binding;
bool application_qc_weapon_binding_at(struct application_qc_state *,size_t,
    struct qa_application_qc_weapon_ui_binding *,uint32_t *,qa_error *);
bool application_qc_pending_weapon_ready(struct application_qc_state *,const application_qc_client *,qa_error *);
bool application_qc_client_postthink(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_weapon_before_postthink(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_weapon_after_postthink(struct application_qc_state *,qa_actor_id,qa_error *);
void application_qc_weapon_command(struct application_qc_state *,qa_actor_id);
bool application_qc_project_body_store(struct application_qc_state *, qa_qc_instance *, const qa_qc_store_event *, qa_error *);
#endif

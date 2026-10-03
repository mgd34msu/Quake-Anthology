#ifndef APPLICATION_QC_PROFILE_H
#define APPLICATION_QC_PROFILE_H
#include "guest_qc_internal.h"
#include "client_outputs.h"
#include "guest_q3_mod_operations.h"

typedef enum application_qc_input_id {
    QC_INPUT_SELF, QC_INPUT_OTHER, QC_INPUT_TIME, QC_INPUT_ELAPSED,
    QC_INPUT_ANGLES, QC_INPUT_ATTACK, QC_INPUT_JUMP, QC_INPUT_IMPULSE,
    QC_INPUT_FORWARD, QC_INPUT_SIDE, QC_INPUT_UP, QC_INPUT_RESULT, QC_INPUT_ACTIVATOR,
    QC_INPUT_ATTACKER, QC_INPUT_INFLICTOR, QC_INPUT_AMOUNT, QC_INPUT_KNOCKBACK, QC_INPUT_POINT, QC_INPUT_COUNT
} application_qc_input_id;
typedef enum application_qc_value_kind {
    QC_VALUE_CONSTANT, QC_VALUE_INPUT, QC_VALUE_ARGUMENT,
    QC_VALUE_ARGUMENTS_TEXT, QC_VALUE_ARGUMENT_COUNT
} application_qc_value_kind;
typedef struct application_qc_value {
    application_qc_value_kind kind;
    application_qc_input_id source;
    size_t argument;
    qa_qc_game_value constant;
} application_qc_value;
typedef struct application_qc_global {
    const qa_qc_definition *definition;
    application_qc_value value;
} application_qc_global;
typedef struct application_qc_call {
    uint32_t function;
    application_qc_value arguments[8];
    size_t argument_count;
    application_qc_global *globals;
    size_t global_count;
} application_qc_call;
typedef struct application_qc_calls { application_qc_call *values; size_t count; } application_qc_calls;
typedef struct application_qc_command {
    char *name;
    application_qc_call call;
} application_qc_command;
typedef struct application_qc_callback {
    qa_string_id id;
    application_q3_mod_operation operation;
    qa_operation_hook_kind stage;
    application_qc_call call;
    struct application_qc_state *engine;
    application_q3_mod_operation_services services;
    qa_operation_registration registration;
} application_qc_callback;
typedef enum application_qc_field_kind {
    QC_FIELD_PRIVATE, QC_FIELD_CONSTANT, QC_FIELD_HEALTH, QC_FIELD_ORIGIN,
    QC_FIELD_VELOCITY, QC_FIELD_ANGLES, QC_FIELD_MIN, QC_FIELD_MAX,
    QC_FIELD_THINK, QC_FIELD_NEXTTHINK, QC_FIELD_CLASSNAME, QC_FIELD_VIEW,
    QC_FIELD_CLIENT_FLAGS, QC_FIELD_INPUT, QC_FIELD_INVENTORY
} application_qc_field_kind;
typedef struct application_qc_bound_field {
    const qa_qc_definition *definition;
    application_qc_field_kind kind;
    application_qc_value constant;
    application_qc_input_id input;
    qa_item_id item;
    float scale;
    uint32_t private_mask;
    bool nonzero, grounded;
} application_qc_bound_field;
typedef struct application_qc_output {
    const application_qc_bound_field *field;
    uint32_t function;
    uint64_t consume;
} application_qc_output;
typedef struct application_qc_input_binding {
    bool before, movement_slice;
    application_qc_calls calls;
    application_qc_output *outputs;
    size_t output_count;
} application_qc_input_binding;
typedef struct application_qc_cvar { char *name, *value; } application_qc_cvar;
typedef struct application_qc_weapon_value {
    float value;
    qa_item_id item, ammo;
    char *label;
    uint32_t bit, via;
    int32_t impulse;
    bool ui_declared, ammo_declared;
} application_qc_weapon_value;
typedef struct application_qc_client_output_value {
    double value;
    union { qa_movement_mode mode; bool crouched; } output;
} application_qc_client_output_value;
typedef struct application_qc_client_output {
    application_client_output_channel channel;
    const application_qc_bound_field *field, *maximum;
    bool height, masked;
    uint32_t mask;
    application_qc_client_output_value *values;
    size_t value_count;
} application_qc_client_output;
struct application_qc_profile {
    application_qc_bound_field *fields;
    size_t field_count;
    uint32_t maximum_clients;
    bool clients;
    application_qc_calls initialize, admit, userinfo, disconnect, client_frame, frame;
    application_qc_input_binding *input;
    size_t input_count;
    application_qc_client_output client_outputs[APPLICATION_CLIENT_OUTPUT_COUNT];
    size_t client_output_count;
    uint8_t client_output_channels;
    application_qc_cvar *cvars;
    size_t cvar_count;
    application_qc_command *commands;
    size_t command_count;
    application_qc_callback *callbacks;
    size_t callback_count;
    const qa_qc_definition *weapon_field;
    application_qc_weapon_value *weapon_values;
    size_t weapon_count;
};
typedef struct application_qc_inputs {
    qa_actor_id self, other, activator, attacker, inflictor;
    const qa_movement_command *command;
    const qa_command_invocation *console;
    uint64_t time_ns, elapsed_ns;
    float result;
    float amount, knockback;
    qa_vec3 point;
} application_qc_inputs;
bool application_qc_run_calls(struct application_qc_state *, const application_qc_calls *,
                                const application_qc_inputs *, qa_error *);
bool application_qc_command_name_equal(const char *, const char *);
bool application_qc_declared_command(void *, const qa_command_invocation *, qa_error *);
bool application_qc_callbacks_register(application_provider *, qa_error *);
bool application_qc_callbacks_suspend(application_provider *, qa_error *);
bool application_qc_callbacks_ready(const struct application_qc_state *, qa_error *);
bool application_qc_project_declared(struct application_qc_state *, qa_qc_instance *,
                                      const qa_qc_entity_access *, qa_error *);
bool application_qc_store_declared(struct application_qc_state *, qa_qc_instance *,
                                    const qa_qc_store_event *, qa_error *);
bool application_qc_seed_fields(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_prepare_markers(struct application_qc_state *, qa_error *);
bool application_qc_authored_map_ready(const application_provider *, qa_error *);
bool application_qc_initialize_declared(struct application_qc_state *, qa_error *);
bool application_qc_initialize_addition(application_provider *, qa_error *);
bool application_qc_entered(void *, qa_qc_instance *, const qa_qc_call_event *, qa_error *);
float application_qc_input_scalar(const qa_movement_command *, application_qc_input_id);
bool application_qc_load_declared_map(struct application_qc_state *, const qa_bsp_view *,
                                        const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_qc_client_think(struct application_qc_state *, qa_actor_id,
                                  const qa_source_frame *, qa_error *);
bool application_qc_publish_client_outputs(struct application_qc_state *, qa_error *);
bool application_qc_admit_client_outputs(struct application_qc_state *, uint32_t, qa_error *);
bool application_qc_restore_client_outputs(struct application_qc_state *, qa_error *);
bool application_qc_output_field_owned(const struct application_qc_state *, qa_actor_id,
                                        const application_qc_bound_field *);
bool application_qc_output_claim_available(const struct application_qc_state *, uint8_t, qa_error *);
bool application_qc_capture_client_outputs(const struct application_qc_state *, qa_error *);
#endif

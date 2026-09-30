#ifndef APPLICATION_QC_PROFILE_H
#define APPLICATION_QC_PROFILE_H
#include "guest_qc_internal.h"

typedef enum application_qc_input_id {
    QC_INPUT_SELF, QC_INPUT_OTHER, QC_INPUT_TIME, QC_INPUT_ELAPSED,
    QC_INPUT_ANGLES, QC_INPUT_ATTACK, QC_INPUT_JUMP, QC_INPUT_IMPULSE,
    QC_INPUT_FORWARD, QC_INPUT_SIDE, QC_INPUT_UP, QC_INPUT_COUNT
} application_qc_input_id;
typedef struct application_qc_value {
    bool input;
    application_qc_input_id source;
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
typedef struct application_qc_weapon_value { float value; qa_item_id item; } application_qc_weapon_value;
struct application_qc_profile {
    application_qc_bound_field *fields;
    size_t field_count;
    uint32_t maximum_clients;
    bool clients;
    application_qc_calls initialize, admit, userinfo, disconnect, client_frame, frame;
    application_qc_input_binding *input;
    size_t input_count;
    application_qc_cvar *cvars;
    size_t cvar_count;
    const qa_qc_definition *weapon_field;
    application_qc_weapon_value *weapon_values;
    size_t weapon_count;
};
typedef struct application_qc_inputs {
    qa_actor_id self, other;
    const qa_movement_command *command;
    uint64_t time_ns, elapsed_ns;
} application_qc_inputs;
bool application_qc_run_calls(struct application_qc_state *, const application_qc_calls *,
                                const application_qc_inputs *, qa_error *);
bool application_qc_project_declared(struct application_qc_state *, qa_qc_instance *,
                                      const qa_qc_entity_access *, qa_error *);
bool application_qc_store_declared(struct application_qc_state *, qa_qc_instance *,
                                    const qa_qc_store_event *, qa_error *);
bool application_qc_seed_fields(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_prepare_markers(struct application_qc_state *, qa_error *);
bool application_qc_entered(void *, qa_qc_instance *, const qa_qc_call_event *, qa_error *);
float application_qc_input_scalar(const qa_movement_command *, application_qc_input_id);
bool application_qc_load_declared_map(struct application_qc_state *, const qa_bsp_view *,
                                        const qa_entities *, qa_string_id, qa_string_id, qa_error *);
bool application_qc_client_think(struct application_qc_state *, qa_actor_id,
                                  const qa_source_frame *, qa_error *);
#endif

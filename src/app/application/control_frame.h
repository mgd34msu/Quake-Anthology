#ifndef APPLICATION_CONTROL_FRAME_H
#define APPLICATION_CONTROL_FRAME_H

#include "internal.h"
#include "qa/source_save.h"
#include "qa/game_q3_wire.h"
#include "client_outputs.h"

typedef enum application_control_outcome {
    APPLICATION_CONTROL_FAILED,
    APPLICATION_CONTROL_COMPLETED,
    APPLICATION_CONTROL_SKIPPED
} application_control_outcome;

typedef enum application_control_source_path {
    APPLICATION_CONTROL_NQ_TURN,
    APPLICATION_CONTROL_QW_GROUP,
    APPLICATION_CONTROL_MIXED
} application_control_source_path;
typedef enum application_control_stage {
    APPLICATION_CONTROL_COMMAND,
    APPLICATION_CONTROL_PREPARE,
    APPLICATION_CONTROL_PHYSICS
} application_control_stage;
typedef struct application_control_context {
    qa_actor_id actor;
    qa_source_frame frame;
    application_control_source_path path;
    application_control_stage stage;
    bool retained, defer_postthink;
    qa_actor_owner arsenal;
    qa_item_id weapon;
    bool unified_command, unified_intent, unified_holdable;
    bool unified_has_impulse;
    uint8_t unified_impulse;
    bool command_only;
    qa_source_command command;
    uint64_t source_elapsed_ns;
    bool source_usercmd;
    bool source_holdable;
    bool source_guestcmd;
    bool source_qwcmd, source_nqcmd, source_q2cmd, source_input_applied;
    qa_movement_command source_command;
} application_control_context;
static inline qa_actor_owner application_control_provider(const application_control_context *context)
{ return context->command_only ? context->command.provider : context->frame.provider; }
static inline qa_ruleset_id application_control_kind(const application_control_context *context)
{ return context->command_only ? context->command.kind : context->frame.kind; }
static inline uint64_t application_control_time(const application_control_context *context)
{ return context->command_only ? context->command.time_ns : context->frame.time_ns; }
static inline uint64_t application_control_start(const application_control_context *context)
{ return context->command_only ? context->command.time_ns : context->frame.start_ns; }
static inline uint64_t application_control_elapsed(const application_control_context *context)
{ return context->path == APPLICATION_CONTROL_MIXED || context->source_qwcmd ? context->source_elapsed_ns :
    context->command_only ? context->command.elapsed_ns : context->frame.elapsed_ns; }
struct application_control_turn;
struct application_control_frames;
struct application_qc_parked_input;
struct application_source_input_scope;
struct application_control_mod_input;
struct application_control_mod_input *application_control_mod_head(const qa_application *);
void application_control_mod_head_set(qa_application *, struct application_control_mod_input *);
bool application_control_mod_abort_all(qa_application *, qa_error *);
struct application_q3_mod_inputs;
bool application_control_last_mod_command(const qa_application *, qa_actor_id,
    const qa_q3_player *, qa_q3_usercmd *, qa_error *);
bool application_control_mod_usercmd(void *, qa_actor_id,
    const struct application_q3_mod_inputs *, const qa_q3_player *, qa_q3_usercmd *, qa_error *);
/* Borrowed only during an actual retained NQ PHYSICS turn. External GAME
 * hooks use this owner instead of synthesizing a nested command admission. */
typedef struct application_control_external_stage {
    qa_application *application;
    qa_actor_id actor;
    application_control_context source;
    void *state;
    bool (*current)(const struct application_control_external_stage *);
    bool (*input)(const struct application_control_external_stage *, qa_movement_state *,
        qa_movement_command *, const qa_vec3 *, struct application_source_input_scope *,
        bool before, bool slice, uint64_t elapsed_ns, qa_error *);
    bool (*locomotion)(const struct application_control_external_stage *,
        const qa_movement_command *, qa_movement_command *, qa_error *);
    bool (*complete)(const struct application_control_external_stage *,
        const qa_movement_command *, const qa_q3_player *, qa_error *);
} application_control_external_stage;
bool application_arsenal_guest_stage_ready(qa_application *, qa_actor_id);
bool application_qc_input_park(application_provider *, qa_actor_id, struct application_qc_parked_input **, qa_error *);
bool application_qc_input_resume(application_provider *, struct application_qc_parked_input *, qa_error *);
bool application_qc_input_parked_abort(application_provider *, struct application_qc_parked_input *, qa_error *);
bool application_qc_control_reserved(application_provider *, qa_actor_id, bool *, qa_error *);
bool application_qc_control_source_client(const application_provider *, qa_actor_id, bool *, qa_error *);
bool application_qc_control_receipt_time(const application_provider *, qa_actor_id, uint64_t *, qa_error *);
bool application_qc_control_input_active(const application_provider *);
bool application_player_bot(const qa_application *, qa_actor_id);
bool application_control_q1_world_begin(application_provider *, qa_q1_game_operation *, qa_error *);
void application_control_frames_consume_impulse(qa_application *, qa_actor_id, uint64_t);
bool application_control_q1_source_prethink(qa_application *, qa_actor_id,
    const qa_movement_command *, qa_error *);
bool application_control_frames_q1_prepared(const qa_application *, qa_actor_id, qa_actor_owner);
bool application_control_frames_q1_command_ready(const qa_application *, qa_actor_id,
    qa_actor_owner, const qa_movement_command *);
bool application_control_frames_q1_complete(qa_application *, qa_actor_id, qa_actor_owner, qa_error *);
bool application_arsenal_prepare_frame(qa_application *, const qa_source_frame *, size_t, qa_error *);

bool application_control_frames_create(qa_application *, qa_error *);
void application_control_frames_free(struct application_control_frames *);
bool application_control_frames_idle(const qa_application *);
bool application_control_frames_abort(qa_application *, qa_error *);
void application_control_frames_release(qa_application *, qa_actor_id);
bool application_control_frames_receive(qa_application *, qa_actor_id,
                                         const qa_movement_command *, size_t, qa_error *);
bool application_control_frames_receive_bot(qa_application *, qa_actor_id,
                                             const qa_movement_command *, qa_actor_owner, qa_item_id, qa_error *);
bool application_bot_weapon_apply(qa_application *, qa_actor_id, qa_actor_owner, qa_item_id, qa_error *);
bool application_bots_frame_at(qa_application *, const qa_source_frame *, size_t, uint64_t, qa_error *);
bool application_control_frames_sequence(const qa_application *, qa_actor_id, bool *, uint64_t *);
const application_control_context *application_control_frame_current(const qa_application *, qa_actor_id);
bool application_control_frames_owns(const qa_application *, qa_actor_id);
bool application_control_frames_apply_nested(qa_application *, qa_actor_id,
                                              const qa_movement_command *, qa_movement_command *, qa_error *);
bool application_control_last_qw_command(const qa_application *, qa_actor_id,
    qa_movement_command *, uint64_t *, bool *, qa_error *);
bool application_control_q3_flags(application_provider *, qa_actor_id, uint32_t, uint32_t, qa_error *);
application_control_outcome application_control_frames_q3_move(qa_application *, const qa_source_command *,
    const qa_movement_command *, bool use_holdable, qa_error *);
bool application_guest_input_interval(const qa_application *, qa_actor_id, uint64_t *);
void application_control_frames_state(qa_application *, qa_actor_id, qa_movement_state *);
qa_movement_state *application_control_frames_state_current(const qa_application *, qa_actor_id);
const qa_movement_call *application_control_frames_call_swap(qa_application *, const qa_movement_call *);
const qa_movement_call *application_control_frames_call_current(const qa_application *, qa_actor_id);
bool application_control_q3_policy(application_provider *, qa_actor_id, uint8_t,
                                   const qa_q3_wire_policy *, qa_error *);
const qa_movement_result *application_control_q3_result(application_provider *, qa_actor_id, qa_error *);
bool application_arsenal_guest_output_admit(application_provider *, uint8_t, qa_error *);
bool application_arsenal_guest_source_command(qa_application *, qa_actor_id,
    const qa_movement_command *, qa_error *);
/* Only an entered original Q2 weapon decision can lend this synchronous turn. */
bool application_control_native_q2_weapon_step(application_provider *, qa_actor_id,
    const qa_movement_command *, uint64_t source_time_ns, qa_error *);
bool application_control_group_touch_once(qa_application *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool application_control_frames_prepare(void *, qa_session *, const qa_source_frame *, size_t,
                                         uint64_t, qa_error *);
bool application_control_frames_commands(void *, qa_session *, const qa_source_frame *, size_t,
                                          uint64_t, qa_error *);
bool application_control_frames_end(void *, qa_session *, const qa_source_frame *, size_t,
                                     uint64_t, qa_error *);
bool application_control_frames_actor(void *, qa_session *, qa_actor_id,
                                       const qa_source_frame *, bool *, qa_error *);
bool application_control_frames_fields(qa_source_save_io *, qa_application *,
                                        const application_control_record *,
                                        struct application_control_frames **, qa_error *);

application_control_outcome application_control_stage_move(qa_application *, qa_actor_id, const qa_movement_command *,
                                     struct application_control_turn **, qa_error *);
application_control_outcome application_control_finish(qa_application *, qa_actor_id,
    application_control_outcome, const char *site, qa_error *);
bool application_control_turn_abort(struct application_control_turn *, qa_error *);
bool application_control_turn_resume(struct application_control_turn *, qa_error *);
bool application_control_group_post(qa_application *, qa_actor_id, qa_error *);
qa_movement_input application_control_character_postures(const application_provider *, qa_actor_id);
bool application_control_outputs(const qa_application *, qa_actor_id,
    application_client_outputs *, qa_error *);
bool application_control_body_request(qa_application *, qa_actor_id, qa_bounds current,
    application_client_outputs *, qa_error *);
void application_control_body_reset(qa_application *, qa_actor_id);
void application_control_publish_motion(application_control_record *, const qa_movement_result *);
bool application_arsenal_guest_outputs(application_provider *, qa_actor_id,
    application_client_outputs *, qa_error *);
bool application_arsenal_guest_crouched(application_provider *, qa_actor_id, bool *, qa_error *);
bool application_qc_control_state(application_provider *, qa_actor_id, qa_movement_state *,
                                    qa_bounds *, qa_movement_environment *, qa_vec3 *, qa_error *);
bool application_qc_control_phase(application_provider *, qa_actor_id, application_control_source_path,
                                    qa_movement_phase, const application_control_context *, qa_movement_call *, qa_error *);
bool application_qc_control_body(application_provider *, qa_actor_id, const qa_movement_state *, qa_movement_ground, qa_body_state *, qa_error *);
bool application_qc_control_profile(application_provider *, qa_actor_id, qa_movement_profile *, qa_error *);
bool application_qc_control_before_actor(application_provider *, qa_actor_id, const qa_source_frame *, qa_error *);

#endif

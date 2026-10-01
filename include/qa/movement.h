#ifndef QA_MOVEMENT_H
#define QA_MOVEMENT_H

#include "qa/world.h"

typedef enum qa_movement_kind {
    QA_MOVEMENT_NETQUAKE, QA_MOVEMENT_QUAKEWORLD, QA_MOVEMENT_Q2_CLASSIC,
    QA_MOVEMENT_Q2_RERELEASE, QA_MOVEMENT_Q3
} qa_movement_kind;
typedef enum qa_q1_edition { QA_Q1_CLASSIC, QA_Q1_RERELEASE, QA_Q1_QUAKE64 } qa_q1_edition;
typedef struct qa_q1_water_transition_result {
    int32_t water_type, water_level;
    bool splash;
} qa_q1_water_transition_result;
/* Source initialization and exit levels are meaningful, including negative
 * water_level values. Callers emit the splash before committing this state. */
static inline qa_q1_water_transition_result qa_q1_water_transition(int32_t previous_type,
                                                                   int32_t contents) {
    if (previous_type == 0)
        return (qa_q1_water_transition_result){contents, 1, false};
    if (contents <= -3)
        return (qa_q1_water_transition_result){contents, 1, previous_type == -1};
    return (qa_q1_water_transition_result){-1, contents, previous_type != -1};
}
typedef enum qa_q1_solid { QA_Q1_SOLID_NOT, QA_Q1_SOLID_TRIGGER, QA_Q1_SOLID_BOX, QA_Q1_SOLID_SLIDEBOX, QA_Q1_SOLID_BSP, QA_Q1_SOLID_CORPSE } qa_q1_solid;
typedef struct qa_movement_ground {
    qa_trace_hit hit;
    qa_actor_id actor;
    uint32_t model;
} qa_movement_ground;

/* The selected movement family owns the command interpretation. Network
 * codecs fill these fields without choosing a player's movement provider.
 * angle_words are raw source 16-bit words for Q2 classic and Q3; angles are
 * degrees for NQ, QW and Q2 rerelease. side_move is Q3 rightmove.
 * Axes retain Q2 rerelease fractions. Input and protocol adapters apply the
 * integer encoding of classic commands; signed byte/short values fit exactly. */
typedef struct qa_movement_command {
    qa_movement_kind kind;
    uint64_t sequence;
    uint32_t milliseconds;
    int32_t server_time_ms, server_frame;
    double acknowledged_server_seconds;
    qa_vec3 angles;
    int32_t angle_words[3];
    float forward_move, side_move, up_move;
    uint32_t buttons;
    uint8_t impulse, light_level, weapon;
} qa_movement_command;

typedef struct qa_nq_movement_state {
    qa_vec3 origin, velocity, angles, old_origin, angular_velocity;
    qa_vec3 view_angles, punch_angles, water_jump_direction;
    int32_t move_type;
    float health;
    uint32_t flags;
    qa_movement_ground ground;
    int32_t water_level, water_type;
    double teleport_time_seconds;
    float ideal_pitch;
    bool fix_angle;
} qa_nq_movement_state;
/* QW input centers an authored float origin with its authored float mins in
 * binary64. It can retain that value when SpectatorMove returns before any
 * vector store. QW vector operations still store float components. */
typedef struct qa_qw_origin { double x, y, z; } qa_qw_origin;
static inline qa_qw_origin qa_qw_origin_from_vec3(qa_vec3 value) {
    return (qa_qw_origin){value.x, value.y, value.z};
}
/* Projection at a float body/collision boundary; it does not replace the
 * authoritative QW origin with the projected components. */
static inline qa_vec3 qa_qw_origin_to_vec3(qa_qw_origin value) {
    return qa_v3((float)value.x, (float)value.y, (float)value.z);
}
typedef struct qa_qw_movement_state {
    qa_qw_origin origin;
    qa_vec3 velocity, angles;
    uint32_t old_buttons;
    float water_jump_time_seconds;
    bool dead;
    int32_t spectator;
    qa_movement_ground ground;
} qa_qw_movement_state;
typedef struct qa_q2_movement_state {
    int32_t type;
    int16_t origin_eighths[3], velocity_eighths[3];
    uint32_t flags;
    uint8_t time_eight_ms;
    int16_t gravity, delta_angle_shorts[3];
} qa_q2_movement_state;
typedef struct qa_q2r_movement_state {
    int32_t type;
    qa_vec3 origin, velocity;
    uint32_t flags;
    uint32_t time_ms;
    int16_t gravity;
    qa_vec3 delta_angles;
    float view_height;
} qa_q2r_movement_state;
typedef struct qa_q3_movement_state {
    int32_t command_time_ms, movement_type, bob_cycle;
    uint32_t movement_flags;
    int32_t movement_time_ms;
    qa_vec3 origin, velocity;
    int32_t gravity, speed, delta_angle_words[3], movement_direction;
    qa_vec3 grapple_point;
    uint32_t flags;
    qa_vec3 view_angles;
    float view_height;
    qa_movement_ground ground;
    uint32_t event_sequence;
    qa_actor_id jump_pad;
    int32_t movement_frame, jump_pad_frame;
} qa_q3_movement_state;
typedef struct qa_movement_state {
    qa_movement_kind kind;
    union {
        qa_nq_movement_state nq;
        qa_qw_movement_state qw;
        qa_q2_movement_state q2;
        qa_q2r_movement_state q2r;
        qa_q3_movement_state q3;
    } data;
} qa_movement_state;

typedef struct qa_q1_movement_parameters {
    float gravity, stop_speed, max_speed, spectator_max_speed;
    float accelerate, air_accelerate, water_accelerate, friction, water_friction;
    float entity_gravity;
} qa_q1_movement_parameters;
typedef struct qa_movement_profile {
    qa_movement_kind kind;
    union {
        struct {
            qa_q1_movement_parameters parameters;
            qa_q1_edition edition;
            float edge_friction, max_velocity, ideal_pitch_scale, roll_speed, roll_angle;
            bool no_clip_angle_hack, no_step, source_jump_authority, preserve_fixangle_roll;
        } nq;
        struct {
            qa_q1_movement_parameters parameters;
            uint32_t maximum_command_ms;
            /* Mixed Anthology player controls admit selected-character crouch
             * and map positive upmove to jump. Native QW leaves these off. */
            bool shared_controls;
        } qw;
        struct { float air_accelerate; bool snap_initial, strafejump_hack; } q2;
        struct { float air_accelerate; bool n64_physics; } q2r;
        struct { bool missionpack, no_footsteps; uint32_t fixed_ms; } q3;
    } data;
} qa_movement_profile;

typedef struct qa_movement_posture { qa_bounds bounds; float view_height; } qa_movement_posture;
typedef enum qa_movement_mode { QA_MOVEMENT_MODE_NORMAL, QA_MOVEMENT_MODE_NOCLIP, QA_MOVEMENT_MODE_FREEZE } qa_movement_mode;
typedef struct qa_movement_environment {
    float health;
    bool flight, haste, invulnerable;
    float gravity_multiplier, speed_multiplier;
    bool fixed_pose, fixed_crouched;
    qa_movement_posture pose;
    bool has_body_bounds;
    qa_bounds body_bounds;
    bool has_mode;
    qa_movement_mode mode;
    bool has_stance, crouched;
} qa_movement_environment;
typedef struct qa_movement_input {
    qa_actor_id actor;
    qa_movement_state state;
    qa_movement_command command;
    qa_movement_profile profile;
    qa_trace_shape shape;
    qa_bounds current_bounds;
    bool has_current_bounds;
    qa_movement_posture standing, crouched, dead;
    qa_bounds invulnerability_bounds;
    qa_movement_environment environment;
    uint64_t time_ns, elapsed_ns;
    /* Private NQ prediction may follow a signed foreign source clock. This
     * retains that real seconds value independently of event nanoseconds. */
    bool has_source_seconds;
    double source_seconds;
    bool prediction, snap_initial;
    qa_vec3 view_offset;
    /* Original rerelease generic slide retains this shared pml.origin scratch.
     * Session/prediction owners capture it alongside their movement state. */
    qa_vec3 *q2r_pml_origin;
    bool has_source_punch_angles;
    qa_vec3 source_punch_angles;
    qa_q1_solid q1_solid;
    qa_trace_policy trace_policy;
    bool has_trace_policy;
} qa_movement_input;

typedef enum qa_movement_status { QA_MOVEMENT_ACTIVE, QA_MOVEMENT_ACTOR_REMOVED } qa_movement_status;
typedef struct qa_movement_contact { qa_trace_result trace; uint32_t substep; } qa_movement_contact;
typedef struct qa_movement_result {
    qa_movement_status status;
    qa_actor_id actor;
    uint64_t command_sequence;
    qa_movement_state state;
    qa_bounds bounds;
    qa_vec3 view_angles, view_offset;
    float view_height, horizontal_speed;
    qa_movement_ground ground;
    int32_t water_level, water_type;
    qa_movement_contact *contacts;
    size_t contact_count, contact_capacity;
    uint64_t effect_count;
    float screen_blend[4], impact_delta;
    uint32_t render_flags;
    bool jump_sound, step_clip;
} qa_movement_result;

typedef enum qa_movement_control { QA_MOVEMENT_CONTINUE, QA_MOVEMENT_REMOVED, QA_MOVEMENT_ERROR } qa_movement_control;
typedef enum qa_movement_phase {
    QA_MOVE_INPUT_BEGIN, QA_MOVE_INPUT_END, QA_MOVE_INPUT_ABORT,
    QA_MOVE_PRETHINK, QA_MOVE_THINK, QA_MOVE_POSTTHINK,
    QA_MOVE_LINK, QA_MOVE_LINK_TRIGGERS, QA_MOVE_WEAPON, QA_MOVE_TORSO,
    QA_MOVE_DROP_TIMERS, QA_MOVE_GESTURE, QA_MOVE_POSTURE
} qa_movement_phase;
typedef enum qa_movement_effect_kind {
    QA_MOVE_EFFECT_EVENT, QA_MOVE_EFFECT_TOUCH, QA_MOVE_EFFECT_ANIMATION,
    QA_MOVE_EFFECT_SOUND, QA_MOVE_EFFECT_PLAYER_ACTION
} qa_movement_effect_kind;
typedef enum qa_movement_animation_kind { QA_MOVE_ANIMATION_LEGS, QA_MOVE_ANIMATION_LEGS_TIMER, QA_MOVE_ANIMATION_LOCOMOTION } qa_movement_animation_kind;
typedef enum qa_movement_locomotion {
    QA_MOVE_IDLE, QA_MOVE_WALK, QA_MOVE_RUN, QA_MOVE_BACKWARD,
    QA_MOVE_CROUCH, QA_MOVE_JUMP, QA_MOVE_LAND, QA_MOVE_SWIM
} qa_movement_locomotion;
typedef struct qa_movement_effect {
    qa_movement_effect_kind kind;
    uint64_t sequence, time_ns;
    uint32_t substep, event_sequence;
    int32_t source_time_ms;
    int32_t value, parameter;
    qa_movement_animation_kind animation_kind;
    bool force, backwards;
    const char *sound;
    const qa_trace_result *trace; /* Borrowed only for this synchronous callback. */
} qa_movement_effect;
typedef struct qa_movement_call {
    qa_actor_id actor;
    qa_movement_state *state;
    qa_movement_command *command;
    qa_bounds *bounds;
    float *view_height;
    int32_t *water_level, *water_type;
    uint64_t time_ns;
    uint32_t milliseconds, substep;
    /* Actual interval at this call site: NQ frame, QW slice/final frame,
     * Q2 command or Q3 substep. Command milliseconds alone is not NQ time. */
    float elapsed_seconds;
    bool prediction;
    /* WEAPON adapters set this when they return an authoritative movement
     * continuation. A no-op hook leaves computed ground/water output intact. */
    bool state_replaced;
    int32_t source_time_ms;
    const qa_movement_profile *profile;
    /* Callbacks refresh health, mode, stance and authored bounds here after
     * source code runs. The next source phase reads this command-local copy. */
    qa_movement_environment *environment;
} qa_movement_call;
typedef struct qa_movement_services {
    void *context;
    bool (*trace)(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
    bool (*point_contents)(void *, const qa_point_query *, qa_point_contents *, qa_error *);
    /* Authoritative callbacks publish/re-read their owner state. They may
     * teleport or remove the actor; removal prevents subsequent writeback.
     * Weapon/animation adapters run at source call sites in prediction too. */
    qa_movement_control (*phase)(void *, qa_movement_phase, qa_movement_call *, qa_error *);
    qa_movement_control (*touch)(void *, const qa_trace_result *, qa_movement_call *, qa_error *);
    qa_movement_control (*effect)(void *, const qa_movement_effect *, qa_movement_call *, qa_error *);
    bool (*firing)(void *, const qa_movement_call *);
    bool (*is_bsp)(void *, const qa_trace_result *, bool *, qa_error *);
} qa_movement_services;

qa_movement_profile qa_movement_profile_default(qa_movement_kind);
qa_movement_environment qa_movement_environment_default(void);
qa_movement_state qa_movement_state_default(qa_movement_kind, qa_vec3 origin);
qa_movement_input qa_movement_input_default(qa_movement_kind, qa_actor_id);
qa_vec3 qa_movement_origin(const qa_movement_state *);
qa_vec3 qa_movement_velocity(const qa_movement_state *);
bool qa_movement_set_origin(qa_movement_state *, qa_vec3, qa_error *);
bool qa_movement_set_velocity(qa_movement_state *, qa_vec3, qa_error *);
/* One accepted source command, including its original subdivision. No world
 * clock is advanced and no second physics simulation is created. Output is
 * published on success, including ACTOR_REMOVED; callbacks may already have
 * committed effects when an error is reported. Zero-initialize the result once,
 * reuse it between commands, and free it at owner teardown. Contact storage is
 * retained; an error preserves prior scalar fields but invalidates contacts.
 * Nested movement calls require distinct results. */
bool qa_movement_move(const qa_movement_input *, const qa_movement_services *, qa_movement_result *, qa_error *);
/* Native NQ accepts all client commands before the entity traversal. These
 * split entries share the same kernel but do not begin/end input application:
 * the session holds that lifecycle across prepare and the client's later
 * physics slot, rereading authoritative state at each entry. Prepare publishes
 * client-think state; physics runs prethink/think/links/postthink/weapon. */
bool qa_movement_prepare_netquake(const qa_movement_input *, const qa_movement_services *, qa_movement_result *, qa_error *);
bool qa_movement_physics_netquake(const qa_movement_input *, const qa_movement_services *, qa_movement_result *, qa_error *);
/* Results own their contact buffer. Free a previous result before replacing
 * it with a new move result. Empty/zeroed results may be freed. */
void qa_movement_result_free(qa_movement_result *);
/* Q2 contacts are deliberately deferred until the owner commits and links
 * the body and runs trigger touches. Pass the current result after triggers. */
bool qa_movement_apply_q2_contacts(const qa_movement_input *, const qa_movement_services *, qa_movement_result *, qa_error *);
/* Collision callbacks for the same kernel in a shared world. Context must be
 * qa_world*. Other callbacks remain owned by the caller's gameplay adapter. */
bool qa_movement_world_trace(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
bool qa_movement_world_contents(void *, const qa_point_query *, qa_point_contents *, qa_error *);
void qa_movement_q3_jump_pad(qa_movement_state *, qa_actor_id pad, qa_vec3 velocity,
                             bool flight, int32_t *event, int32_t *parameter);
void qa_movement_q3_finish_jump_pads(qa_movement_state *);

/* Rerelease Pmove and server entity physics use the same source slide/stuck
 * kernels. Their owners supply the actual body trace and persistent scratch. */
typedef struct qa_q2r_slide {
    void *context;
    bool (*trace)(void *, qa_vec3 start, qa_vec3 end, qa_bounds, qa_trace_result *, qa_error *);
    qa_vec3 *origin, *velocity, *pml_origin;
    qa_bounds bounds;
    float elapsed;
    bool has_time;
    qa_trace_result contacts[32];
    size_t contact_count;
} qa_q2r_slide;
typedef enum qa_q2r_stuck_result { QA_Q2R_GOOD_POSITION, QA_Q2R_FIXED_POSITION, QA_Q2R_NO_GOOD_POSITION } qa_q2r_stuck_result;
bool qa_move_q2r_slide(qa_q2r_slide *, qa_error *);
bool qa_move_q2r_fix_stuck(qa_q2r_slide *, qa_q2r_stuck_result *, qa_error *);

/* The zeroed source command ring survives map_restart. Unsigned sequence
 * arithmetic keeps its 64-command window valid across source-number wrap. */
typedef struct qa_q3_command_history {
    qa_movement_command commands[64];
    uint32_t current_number;
} qa_q3_command_history;
void qa_q3_command_history_init(qa_q3_command_history *);
bool qa_q3_command_history_append(qa_q3_command_history *, const qa_movement_command *, uint32_t *number, qa_error *);
bool qa_q3_command_history_read(const qa_q3_command_history *, uint32_t number, qa_movement_command *, bool *available, qa_error *);

typedef struct qa_q3_prediction_snapshot {
    int32_t server_time_ms;
    qa_movement_state movement;
    /* Borrowed selected arsenal/animation/other-owner checkpoint. The host
     * restores this together with movement before replay, never separately. */
    const void *owners;
} qa_q3_prediction_snapshot;
typedef struct qa_q3_prediction_settings {
    bool demo_playback, no_predict, synchronous_clients, fixed;
    uint32_t movement_ms;
    int32_t error_decay_integer, show_miss;
    float error_decay_value;
} qa_q3_prediction_settings;
typedef struct qa_q3_prediction_frame {
    int32_t time_ms, previous_time_ms;
    const qa_q3_prediction_snapshot *snapshot, *next_snapshot;
    bool next_frame_teleport, this_frame_teleport;
    int32_t health;
} qa_q3_prediction_frame;
typedef enum qa_q3_prediction_status {
    QA_PREDICTION_PREDICTED, QA_PREDICTION_INTERPOLATED, QA_PREDICTION_UNCHANGED,
    QA_PREDICTION_COMMAND_BACKUP_EXCEEDED, QA_PREDICTION_ACTOR_REMOVED
} qa_q3_prediction_status;
typedef struct qa_q3_prediction_output {
    qa_q3_prediction_status status;
    qa_movement_state movement;
    bool hyperspace, consumed_teleport;
    /* INTERPOLATED returns the previous snapshot's arsenal/animation owners,
     * even when movement origin is blended towards the next snapshot. Replay
     * uses the live host owners instead. Borrowed for the snapshot lifetime. */
    const void *interpolated_owners;
} qa_q3_prediction_output;
typedef struct qa_q3_prediction {
    /* Reusable contacts, excluded from prediction checkpoints. */
    qa_movement_result scratch;
    bool initialized;
    qa_movement_state predicted;
    qa_movement_command command;
    qa_vec3 error;
    int32_t error_time_ms;
} qa_q3_prediction;
typedef struct qa_q3_prediction_host {
    void *context;
    const qa_q3_command_history *commands;
    qa_movement_services movement_services;
    /* before_replay retains previous owner state for transition. restore
     * replaces every prediction-owned provider with the selected snapshot. */
    bool (*before_replay)(void *, qa_error *);
    bool (*restore)(void *, const qa_q3_prediction_snapshot *, qa_error *);
    bool (*movement_input)(void *, const qa_movement_state *, const qa_movement_command *,
                            uint32_t command_number, int32_t physics_time_ms, qa_movement_input *, qa_error *);
    qa_movement_control (*touch_triggers)(void *, qa_movement_state *, qa_bounds,
                                          int32_t physics_time_ms, bool *hyperspace, qa_error *);
    bool (*adjust_mover)(void *, qa_vec3, qa_movement_ground, int32_t from_ms, int32_t to_ms, qa_vec3 *, qa_error *);
    bool (*transition)(void *, const qa_movement_state *current, const qa_movement_state *previous, qa_error *);
    void (*set_movement_ms)(void *, uint32_t);
    void (*warning)(void *, const char *);
} qa_q3_prediction_host;
void qa_q3_prediction_init(qa_q3_prediction *);
void qa_q3_prediction_free(qa_q3_prediction *);
bool qa_q3_prediction_view(qa_movement_state *, int32_t health, const qa_movement_command *, qa_error *);
/* Uses qa_movement_move and the host's ordinary prediction trigger services.
 * Ordered effects go through movement_services.effect; no second actor world
 * or arsenal/animation authority is created by the predictor. */
bool qa_q3_predict(qa_q3_prediction *, const qa_q3_prediction_host *, const qa_q3_prediction_settings *,
                    const qa_q3_prediction_frame *, qa_q3_prediction_output *, qa_error *);

#endif

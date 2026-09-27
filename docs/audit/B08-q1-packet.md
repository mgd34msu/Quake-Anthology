# B08 frozen source acceptance evidence

Snapshot HEAD: 9b1dfaef2d781f1246e244e039b97c01bdc8be1d. Includes dirty source frozen for audit. This packet contains explicit source selections, not a truncated full-source dump. Read scope and findings are recorded in docs/audit/q1-shared.md.

Goal and criteria:
{
  "id": "B08",
  "label": "Movement and player commands",
  "depends_on": [
    "B06",
    "B07"
  ],
  "goal": "Implement all selected Q1/QW/Q2/Q3 and rerelease movement policies, player command processing, and prediction kernels.",
  "criteria": [
    "Native C kernels support independent per-player movement with required source cadence, bounds, and numeric decisions."
  ],
  "status": "pending"
}

Reviewer assessment:
Native per-player tagged movement states and Q1/QW/Q2 classic/rerelease/Q3 kernels are present. Explicit Quake64 rejection is inherited from the donor, not a new C regression; the original profile remains unavailable. Concrete B34 callers are not yet implemented. Other detailed kernel bodies were read during the audit but are not all reproduced in this bounded packet, so this packet does not prove every source cadence/branch. Ordinary C float arithmetic is explicitly authorized. No project execution is permitted at this stage.

Snapshot selected SHA256 hashes:
6b1d901ba8b416faa382912b1b7296a276c7c4d9dbd8c1030e3960da4e5eaa5b  src/gameplay/q1/runtime.c
376d43df9d7812bd88873b2ecbe065902ee8bb24250f9e5bf37aa99a58d57d8d  src/gameplay/q1/queries.c
8c36feb2b149d7c98062f265351167d6c526a1f185a4813ecbb80ec1b73409b5  src/gameplay/q1/maps/runtime.c
eee0e959ad883ab18796a346de634b5d181333dbd3785688f365edaaa26dd17f  src/gameplay/policies.c
1a4be8c0b34459135d887f664dcb5ee5fe61f51d6793d132fb2f680032019517  src/gameplay/armor.c
33909f58875e89f0bfb43c1c851c26f0f3d87a086f169299dcc72777018d69cc  src/movement/common.c
c0b1c679af11906858a9e26211e7e3399cec31849fc29f1050c10f2af1272fc8  src/campaign/targets.c
1e48a455cf33dca65deefb3661ff1c8ae77c33f17b034302de64b63b97a874fa  src/campaign/q1/spawn.c
e5997203de73384986f6ffe9e3b6396651a6ba05cd09846cec5c8aee61c69f7f  src/main.c
915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae  docs/dependencies.json


## Source selection: cat include/qa/movement.h

```text
#ifndef QA_MOVEMENT_H
#define QA_MOVEMENT_H

#include "qa/world.h"

typedef enum qa_movement_kind {
    QA_MOVEMENT_NETQUAKE, QA_MOVEMENT_QUAKEWORLD, QA_MOVEMENT_Q2_CLASSIC,
    QA_MOVEMENT_Q2_RERELEASE, QA_MOVEMENT_Q3
} qa_movement_kind;
typedef enum qa_q1_edition { QA_Q1_CLASSIC, QA_Q1_RERELEASE, QA_Q1_QUAKE64 } qa_q1_edition;
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
typedef struct qa_qw_movement_state {
    qa_vec3 origin, velocity, angles;
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
    bool (*is_bsp)(void *, const qa_trace_result *);
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
```

## Source selection: cat src/movement/common.c

```text
#include "internal.h"
#include <limits.h>
#include <stdlib.h>

static bool valid_kind(qa_movement_kind kind) {
    return kind >= QA_MOVEMENT_NETQUAKE && kind <= QA_MOVEMENT_Q3;
}
static bool fail(qa_move_context *c, const char *message) {
    c->failed = true;
    qa_error_set(c->error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool valid_bounds(qa_bounds b) {
    return qa_vec_finite(b.mins) && qa_vec_finite(b.maxs) &&
        b.mins.x <= b.maxs.x && b.mins.y <= b.maxs.y && b.mins.z <= b.maxs.z;
}
static qa_collision_family family(qa_movement_kind kind) {
    return kind <= QA_MOVEMENT_QUAKEWORLD ? QA_COLLISION_Q1 :
        kind == QA_MOVEMENT_Q3 ? QA_COLLISION_Q3 : QA_COLLISION_Q2;
}

float qa_move_component(qa_vec3 v, unsigned axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
void qa_move_set_component(qa_vec3 *v, unsigned axis, float x) {
    if (axis == 0) v->x = x; else if (axis == 1) v->y = x; else v->z = x;
}
int16_t qa_move_short(int32_t value) {
    uint32_t word = (uint32_t)value & UINT32_C(65535);
    return (int16_t)(word >= 32768 ? (int32_t)word - 65536 : (int32_t)word);
}
float qa_move_short_angle(int32_t word) { return (float)qa_move_short(word) * (360.0f / 65536.0f); }
float qa_move_angle_mod(float value) {
    float word = truncf(fmodf(value, 360.0f) * (65536.0f / 360.0f));
    if (!isfinite(word)) return 0;
    return (float)((uint32_t)(int32_t)word & 65535u) * (360.0f / 65536.0f);
}
void qa_move_angles(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up) {
    const float radians = 0.01745329251994329577f;
    float sy = sinf(angles.y*radians), cy = cosf(angles.y*radians);
    float sp = sinf(angles.x*radians), cp = cosf(angles.x*radians);
    float sr = sinf(angles.z*radians), cr = cosf(angles.z*radians);
    if (forward) *forward = qa_v3(cp*cy, cp*sy, -sp);
    if (right) *right = qa_v3(-sr*sp*cy+cr*sy, -sr*sp*sy-cr*cy, -sr*cp);
    if (up) *up = qa_v3(cr*sp*cy+sr*sy, cr*sp*sy-sr*cy, cr*cp);
}
qa_vec3 qa_move_clip(qa_vec3 velocity, qa_vec3 normal, float overbounce, float epsilon) {
    float backoff = qa_vec_dot(velocity, normal) * overbounce;
    qa_vec3 out = qa_vec_sub(velocity, qa_vec_scale(normal, backoff));
    if (fabsf(out.x) < epsilon) out.x = 0;
    if (fabsf(out.y) < epsilon) out.y = 0;
    if (fabsf(out.z) < epsilon) out.z = 0;
    return out;
}

qa_movement_profile qa_movement_profile_default(qa_movement_kind kind) {
    qa_movement_profile p = {.kind=kind};
    qa_q1_movement_parameters q1 = {800,100,320,500,10,10,10,4,4,1};
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE:
        p.data.nq.parameters=q1; p.data.nq.edition=QA_Q1_CLASSIC;
        p.data.nq.edge_friction=2; p.data.nq.max_velocity=2000;
        p.data.nq.ideal_pitch_scale=0.8f; p.data.nq.roll_speed=200;
        p.data.nq.roll_angle=2; p.data.nq.preserve_fixangle_roll=false;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        q1.air_accelerate=0.7f; p.data.qw.parameters=q1;
        p.data.qw.maximum_command_ms=50; break;
    case QA_MOVEMENT_Q2_CLASSIC: p.data.q2.air_accelerate=0; break;
    case QA_MOVEMENT_Q2_RERELEASE: p.data.q2r.air_accelerate=0; break;
    case QA_MOVEMENT_Q3: break;
    }
    return p;
}
qa_movement_environment qa_movement_environment_default(void) {
    return (qa_movement_environment){.health=100,.gravity_multiplier=1,.speed_multiplier=1};
}
qa_movement_state qa_movement_state_default(qa_movement_kind kind, qa_vec3 origin) {
    qa_movement_state s = {.kind=kind};
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE:
        s.data.nq.origin=origin; s.data.nq.old_origin=origin;
        s.data.nq.move_type=3; s.data.nq.health=100; s.data.nq.flags=4096;
        s.data.nq.water_type=-1; break;
    case QA_MOVEMENT_QUAKEWORLD: s.data.qw.origin=origin; break;
    case QA_MOVEMENT_Q2_CLASSIC:
        s.data.q2.gravity=800; (void)qa_movement_set_origin(&s,origin,NULL); break;
    case QA_MOVEMENT_Q2_RERELEASE:
        s.data.q2r.origin=origin; s.data.q2r.gravity=800; s.data.q2r.view_height=22; break;
    case QA_MOVEMENT_Q3:
        s.data.q3.origin=origin; s.data.q3.gravity=800; s.data.q3.speed=320;
        s.data.q3.view_height=26; break;
    }
    return s;
}
qa_movement_input qa_movement_input_default(qa_movement_kind kind, qa_actor_id actor) {
    qa_movement_input in = {.actor=actor,.state=qa_movement_state_default(kind,qa_v3(0,0,0)),
        .command={.kind=kind},.profile=qa_movement_profile_default(kind),
        .environment=qa_movement_environment_default(),.q1_solid=QA_Q1_SOLID_SLIDEBOX};
    float radius=kind==QA_MOVEMENT_Q3?15.0f:16.0f;
    in.standing=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,32)},kind==QA_MOVEMENT_Q3?26.0f:22.0f};
    in.crouched=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,kind==QA_MOVEMENT_Q3?16.0f:4.0f)},kind==QA_MOVEMENT_Q3?12.0f:-2.0f};
    in.dead=(qa_movement_posture){{qa_v3(-radius,-radius,-24),qa_v3(radius,radius,-8)},-16};
    in.invulnerability_bounds=(qa_bounds){qa_v3(-42,-42,-42),qa_v3(42,42,42)};
    in.shape=(qa_trace_shape){QA_SHAPE_BOX,in.standing.bounds};
    in.current_bounds=in.shape.bounds;
    in.trace_policy=qa_collision_default_policy(family(kind));
    return in;
}
qa_vec3 qa_movement_origin(const qa_movement_state *s) {
    if (!s) return qa_v3(0,0,0);
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE: return s->data.nq.origin;
    case QA_MOVEMENT_QUAKEWORLD: return s->data.qw.origin;
    case QA_MOVEMENT_Q2_CLASSIC: return qa_v3(s->data.q2.origin_eighths[0]*0.125f,s->data.q2.origin_eighths[1]*0.125f,s->data.q2.origin_eighths[2]*0.125f);
    case QA_MOVEMENT_Q2_RERELEASE: return s->data.q2r.origin;
    case QA_MOVEMENT_Q3: return s->data.q3.origin;
    }
    return qa_v3(0,0,0);
}
qa_vec3 qa_movement_velocity(const qa_movement_state *s) {
    if (!s) return qa_v3(0,0,0);
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE: return s->data.nq.velocity;
    case QA_MOVEMENT_QUAKEWORLD: return s->data.qw.velocity;
    case QA_MOVEMENT_Q2_CLASSIC: return qa_v3(s->data.q2.velocity_eighths[0]*0.125f,s->data.q2.velocity_eighths[1]*0.125f,s->data.q2.velocity_eighths[2]*0.125f);
    case QA_MOVEMENT_Q2_RERELEASE: return s->data.q2r.velocity;
    case QA_MOVEMENT_Q3: return s->data.q3.velocity;
    }
    return qa_v3(0,0,0);
}
static bool write_vector(qa_movement_state *s, qa_vec3 value, bool velocity, qa_error *error) {
    if (!s || !valid_kind(s->kind) || !qa_vec_finite(value)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid movement vector or family"); return false;
    }
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE:
        if (velocity) s->data.nq.velocity=value; else s->data.nq.origin=value; break;
    case QA_MOVEMENT_QUAKEWORLD:
        if (velocity) s->data.qw.velocity=value; else s->data.qw.origin=value; break;
    case QA_MOVEMENT_Q2_CLASSIC: {
        int16_t words[3];
        for (unsigned axis=0;axis<3;axis++) {
            float scaled=qa_move_component(value,axis)*8.0f;
            if (!isfinite(scaled)) { qa_error_set(error,QA_ERROR_ARGUMENT,axis,"Movement eighth conversion overflow"); return false; }
            words[axis]=qa_move_short((int32_t)fmodf(truncf(scaled),65536.0f));
        }
        memcpy(velocity?s->data.q2.velocity_eighths:s->data.q2.origin_eighths,words,sizeof(words)); break;
    }
    case QA_MOVEMENT_Q2_RERELEASE:
        if (velocity) s->data.q2r.velocity=value; else s->data.q2r.origin=value; break;
    case QA_MOVEMENT_Q3:
        if (velocity) s->data.q3.velocity=value; else s->data.q3.origin=value; break;
    }
    return true;
}
bool qa_movement_set_origin(qa_movement_state *s, qa_vec3 v, qa_error *e) { return write_vector(s,v,false,e); }
bool qa_movement_set_velocity(qa_movement_state *s, qa_vec3 v, qa_error *e) { return write_vector(s,v,true,e); }

qa_movement_ground qa_move_ground(const qa_trace_result *t) {
    return (qa_movement_ground){t->hit,t->actor,t->model};
}
bool qa_move_same_ground(qa_movement_ground a, qa_movement_ground b) {
    return a.hit==b.hit && (a.hit==QA_TRACE_HIT_NONE ||
        (a.hit==QA_TRACE_HIT_WORLD?a.model==b.model:qa_actor_id_equal(a.actor,b.actor)));
}
static qa_trace_policy policy(qa_move_context *c, uint32_t mask) {
    qa_collision_family f=family(c->state->kind);
    qa_trace_policy p=c->input->has_trace_policy?c->input->trace_policy:qa_collision_default_policy(f);
    p.family=f; p.contents_mask=mask;
    if (f==QA_COLLISION_Q2) p.q2_merged_contents=c->state->kind==QA_MOVEMENT_Q2_RERELEASE;
    return p;
}
static bool trace_query(qa_move_context *c, const qa_trace_query *q, qa_trace_result *out) {
    if (c->failed||c->removed) return false;
    if (!c->services->trace(c->services->context,q,out,c->error)) { c->failed=true; return false; }
    if (out->family!=q->policy.family || !isfinite(out->fraction) || out->fraction<0 || out->fraction>1 || !qa_vec_finite(out->end))
        return fail(c,"Movement trace returned an invalid family, fraction or position");
    return true;
}
bool qa_move_trace(qa_move_context *c, qa_vec3 start, qa_vec3 end, qa_bounds bounds, uint32_t mask, bool world_only, qa_trace_result *out) {
    qa_trace_query q={.start=start,.end=end,.shape={c->input->shape.kind,bounds},
        .policy=policy(c,mask),.pass_actor=c->input->actor};
    if (c->input->environment.fixed_pose) q.shape.kind=QA_SHAPE_BOX;
    if (world_only) { q.target.inline_model=true; q.target.model=0; }
    return trace_query(c,&q,out);
}
bool qa_move_trace_q1(qa_move_context *c, qa_vec3 start, qa_vec3 end, qa_trace_shape shape, qa_q1_move_kind move, qa_trace_result *out) {
    qa_trace_query q={.start=start,.end=end,.shape=shape,.policy=policy(c,UINT32_MAX),.pass_actor=c->input->actor};
    q.policy.q1_move=move; q.policy.q1_hull=-1;
    return trace_query(c,&q,out);
}
bool qa_move_contents(qa_move_context *c, qa_vec3 point, int32_t *out) {
    if (c->failed||c->removed) return false;
    qa_point_query q={.point=point,.policy=policy(c,UINT32_MAX),.pass_actor=c->input->actor};
    qa_point_contents result;
    if (!c->services->point_contents(c->services->context,&q,&result,c->error)) { c->failed=true; return false; }
    if (result.family!=q.policy.family) return fail(c,"Movement contents returned another collision family");
    *out=result.family==QA_COLLISION_Q2?(q.policy.q2_merged_contents?result.merged:result.stored):result.contents;
    if (result.family==QA_COLLISION_Q1 && *out<=-9 && *out>=-14) *out=-3;
    return true;
}
static qa_movement_call call(qa_move_context *c) {
    return (qa_movement_call){.actor=c->input->actor,.state=c->state,.command=&c->command,
        .bounds=&c->result->bounds,.view_height=c->state->kind==QA_MOVEMENT_Q3?&c->state->data.q3.view_height:
            c->state->kind==QA_MOVEMENT_Q2_RERELEASE?&c->state->data.q2r.view_height:&c->result->view_height,
        .water_level=&c->result->water_level,.water_type=&c->result->water_type,
        .time_ns=c->time_ns,.milliseconds=c->milliseconds,.substep=c->substep,.elapsed_seconds=c->dt,.prediction=c->input->prediction,
        .source_time_ms=c->command.server_time_ms,.profile=&c->input->profile,.environment=&c->input->environment};
}
static bool control(qa_move_context *c, qa_movement_control result) {
    if (result==QA_MOVEMENT_ERROR) { c->failed=true; return false; }
    if (result==QA_MOVEMENT_REMOVED) { c->removed=true; c->result->status=QA_MOVEMENT_ACTOR_REMOVED; return false; }
    if (result!=QA_MOVEMENT_CONTINUE) return fail(c,"Movement callback returned an invalid continuation");
    if (c->state->kind!=c->input->profile.kind || c->command.kind!=c->input->profile.kind)
        return fail(c,"Movement callback changed the selected family inside a command");
    return !c->failed&&!c->removed;
}
bool qa_move_phase(qa_move_context *c, qa_movement_phase phase) {
    c->phase_state_replaced=false;
    bool input=phase==QA_MOVE_INPUT_BEGIN||phase==QA_MOVE_INPUT_END||phase==QA_MOVE_INPUT_ABORT;
    bool closing=phase==QA_MOVE_INPUT_END||phase==QA_MOVE_INPUT_ABORT;
    if (input&&c->input->prediction) return !c->failed&&!c->removed;
    if ((c->failed||c->removed)&&!closing) return false;
    if (phase==QA_MOVE_INPUT_BEGIN) c->input_open=true;
    if (closing) c->input_open=false;
    if (!c->services->phase) return !c->failed&&!c->removed;
    qa_movement_call invocation=call(c);
    qa_movement_control result=c->services->phase(c->services->context,phase,&invocation,c->error);
    c->phase_state_replaced=invocation.state_replaced;
    return control(c,result);
}
bool qa_move_firing(qa_move_context *c) {
    if (!c->services->firing||c->failed||c->removed) return false;
    qa_movement_call invocation=call(c);
    return c->services->firing(c->services->context,&invocation);
}
bool qa_move_emit(qa_move_context *c, qa_movement_effect effect) {
    if (c->failed||c->removed) return false;
    effect.sequence=c->result->effect_count++; effect.time_ns=c->time_ns; effect.substep=c->substep;
    effect.source_time_ms=c->command.server_time_ms;
    if (!c->services->effect) return true;
    qa_movement_call invocation=call(c);
    return control(c,c->services->effect(c->services->context,&effect,&invocation,c->error));
}
bool qa_move_event(qa_move_context *c, int32_t event, int32_t parameter) {
    uint32_t sequence=0;
    if (c->state->kind==QA_MOVEMENT_Q3) sequence=c->state->data.q3.event_sequence++;
    return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_EVENT,.event_sequence=sequence,.value=event,.parameter=parameter});
}
bool qa_move_animation(qa_move_context *c, qa_movement_animation_kind kind, int32_t value, bool force, bool backwards) {
    return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_ANIMATION,.animation_kind=kind,.value=value,.force=force,.backwards=backwards});
}
bool qa_move_touch(qa_move_context *c, const qa_trace_result *trace) {
    if (trace->hit==QA_TRACE_HIT_NONE) return !c->failed&&!c->removed;
    if (!qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_TOUCH,.trace=trace})) return false;
    if (!c->services->touch) return true;
    qa_movement_call invocation=call(c);
    return control(c,c->services->touch(c->services->context,trace,&invocation,c->error));
}
bool qa_move_contact(qa_move_context *c, const qa_trace_result *trace, bool touch_now, bool unique) {
    if (c->failed||c->removed) return false;
    if (trace->hit==QA_TRACE_HIT_NONE&&(c->state->kind!=QA_MOVEMENT_QUAKEWORLD||touch_now)) return true;
    qa_movement_result *r=c->result;
    if (unique) for (size_t i=0;i<r->contact_count;i++)
        if (qa_move_same_ground(qa_move_ground(&r->contacts[i].trace),qa_move_ground(trace))) return true;
    if (r->contact_count==r->contact_capacity) {
        size_t capacity=r->contact_capacity?r->contact_capacity*2:32;
        if (capacity<r->contact_capacity||capacity>SIZE_MAX/sizeof(*r->contacts)) {
            qa_error_set(c->error,QA_ERROR_MEMORY,0,"Movement contact capacity overflow"); c->failed=true; return false;
        }
        qa_movement_contact *contacts=realloc(r->contacts,capacity*sizeof(*contacts));
        if (!contacts) { qa_error_set(c->error,QA_ERROR_MEMORY,0,"Allocating movement contacts"); c->failed=true; return false; }
        r->contacts=contacts; r->contact_capacity=capacity;
    }
    r->contacts[r->contact_count++]=(qa_movement_contact){*trace,c->substep};
    if (touch_now) return qa_move_touch(c,trace);
    if (c->state->kind==QA_MOVEMENT_Q3) return qa_move_emit(c,(qa_movement_effect){.kind=QA_MOVE_EFFECT_TOUCH,.trace=trace});
    return true;
}
bool qa_move_bounds(qa_move_context *c, qa_bounds requested, uint32_t mask, qa_bounds *out) {
    qa_bounds previous=c->result->bounds;
    bool expands=requested.mins.x<previous.mins.x||requested.mins.y<previous.mins.y||requested.mins.z<previous.mins.z||
        requested.maxs.x>previous.maxs.x||requested.maxs.y>previous.maxs.y||requested.maxs.z>previous.maxs.z;
    if (!valid_bounds(requested)) return fail(c,"Invalid requested movement bounds");
    if (expands) {
        qa_vec3 origin=qa_movement_origin(c->state); qa_trace_result trace;
        if (!qa_move_trace(c,origin,origin,requested,mask,false,&trace)) return false;
        if (trace.all_solid||(c->state->kind!=QA_MOVEMENT_Q3&&trace.start_solid)) { *out=previous; return true; }
    }
    *out=requested; return true;
}
int32_t qa_move_mode_type(qa_movement_kind kind, qa_movement_mode mode) {
    if (kind==QA_MOVEMENT_NETQUAKE) return mode==QA_MOVEMENT_MODE_NORMAL?3:mode==QA_MOVEMENT_MODE_NOCLIP?8:0;
    if (kind==QA_MOVEMENT_QUAKEWORLD) return mode==QA_MOVEMENT_MODE_NOCLIP?1:0;
    if (kind==QA_MOVEMENT_Q2_RERELEASE) return mode==QA_MOVEMENT_MODE_NORMAL?0:mode==QA_MOVEMENT_MODE_NOCLIP?2:6;
    return mode==QA_MOVEMENT_MODE_NORMAL?0:mode==QA_MOVEMENT_MODE_NOCLIP?1:4;
}
bool qa_move_apply_stance(qa_move_context *c) {
    const qa_movement_environment *e=&c->input->environment;
    if (!e->has_stance) return true;
    if (c->state->kind==QA_MOVEMENT_NETQUAKE) return fail(c,"NetQuake has no source crouch command");
    if (c->state->kind==QA_MOVEMENT_Q2_RERELEASE) {
        if (e->crouched) c->command.buttons|=16u; else c->command.buttons&=~16u;
    } else {
        float up=fabsf(c->command.up_move);
        if (up==0) up=1;
        if (e->crouched) c->command.up_move=-up;
        else if (c->command.up_move<0) c->command.up_move=0;
        if (c->state->kind==QA_MOVEMENT_QUAKEWORLD&&e->crouched) c->command.buttons&=~2u;
    }
    return true;
}

bool qa_movement_world_trace(void *world, const qa_trace_query *q, qa_trace_result *r, qa_error *e) { return qa_world_trace(world,q,r,e); }
bool qa_movement_world_contents(void *world, const qa_point_query *q, qa_point_contents *r, qa_error *e) { return qa_world_point_contents(world,q,r,e); }
void qa_movement_result_free(qa_movement_result *r) { if (r) { free(r->contacts); memset(r,0,sizeof(*r)); } }

static bool move_stage(const qa_movement_input *input, const qa_movement_services *services, qa_movement_result *out, qa_error *error, unsigned stage) {
    if (!input||!services||!out||!services->trace||!services->point_contents||
        !valid_kind(input->profile.kind)||(stage&&input->profile.kind!=QA_MOVEMENT_NETQUAKE)||
        input->state.kind!=input->profile.kind||input->command.kind!=input->profile.kind||
        !isfinite(input->command.forward_move)||!isfinite(input->command.side_move)||!isfinite(input->command.up_move)||
        (input->state.kind!=QA_MOVEMENT_NETQUAKE&&
         (!qa_vec_finite(qa_movement_origin(&input->state))||!qa_vec_finite(qa_movement_velocity(&input->state))))||
        input->shape.kind<QA_SHAPE_POINT||input->shape.kind>QA_SHAPE_CAPSULE||
        (input->shape.kind!=QA_SHAPE_POINT&&!valid_bounds(input->shape.bounds))||
        (input->has_current_bounds&&!valid_bounds(input->current_bounds))||
        (input->environment.has_body_bounds&&!valid_bounds(input->environment.body_bounds))||
        (input->environment.fixed_pose&&(!valid_bounds(input->environment.pose.bounds)||!isfinite(input->environment.pose.view_height)))||
        !valid_bounds(input->standing.bounds)||!isfinite(input->standing.view_height)||
        !valid_bounds(input->crouched.bounds)||!isfinite(input->crouched.view_height)||
        !valid_bounds(input->dead.bounds)||!isfinite(input->dead.view_height)||
        !isfinite(input->environment.gravity_multiplier)||input->environment.gravity_multiplier<0||
        !isfinite(input->environment.speed_multiplier)||input->environment.speed_multiplier<0) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid movement input, selected family or services"); return false;
    }
    qa_movement_result result={.contacts=out->contacts,.contact_capacity=out->contact_capacity,
        .actor=input->actor,.command_sequence=input->command.sequence,.state=input->state,
        .bounds=input->has_current_bounds?input->current_bounds:input->shape.bounds,.view_height=input->standing.view_height,.view_offset=input->view_offset};
    if (input->shape.kind==QA_SHAPE_POINT) result.bounds=(qa_bounds){0};
    qa_movement_input active_input=*input;
    qa_move_context c={.input=&active_input,.services=services,.result=&result,.state=&result.state,.command=input->command,.error=error,
        .milliseconds=input->command.milliseconds,.time_ns=input->time_ns,.dt=(float)((double)input->elapsed_ns/1000000000.0)};
    bool q2=input->profile.kind==QA_MOVEMENT_Q2_CLASSIC||input->profile.kind==QA_MOVEMENT_Q2_RERELEASE;
    if (q2) {
        if (c.milliseconds>255) fail(&c,"Q2 command duration must fit its source byte");
        c.dt=(float)c.milliseconds*0.001f;
        if (!c.failed) (void)qa_move_phase(&c,QA_MOVE_INPUT_BEGIN);
        c.milliseconds=c.command.milliseconds; c.dt=(float)c.milliseconds*0.001f;
        if (!c.failed&&!c.removed&&c.milliseconds>255) fail(&c,"Q2 input callback returned an invalid command duration");
        if (!c.failed&&!c.removed) (void)qa_move_apply_stance(&c);
    }
    bool ok=true;
    if (!c.failed&&!c.removed) switch (input->profile.kind) {
    case QA_MOVEMENT_NETQUAKE: ok=stage==1?qa_move_nq_prepare(&c):stage==2?qa_move_nq_physics(&c):qa_move_nq(&c); break;
    case QA_MOVEMENT_QUAKEWORLD: ok=qa_move_qw(&c); break;
    case QA_MOVEMENT_Q2_CLASSIC: ok=qa_move_q2(&c); break;
    case QA_MOVEMENT_Q2_RERELEASE: ok=qa_move_q2r(&c); break;
    case QA_MOVEMENT_Q3: ok=qa_move_q3(&c); break;
    }
    if (!ok&&!c.removed) c.failed=true;
    if (c.input_open) (void)qa_move_phase(&c,c.failed?QA_MOVE_INPUT_ABORT:QA_MOVE_INPUT_END);
    if (c.failed) {
        out->contacts=result.contacts; out->contact_capacity=result.contact_capacity;
        out->contact_count=0;
        return false;
    }
    result.status=c.removed?QA_MOVEMENT_ACTOR_REMOVED:QA_MOVEMENT_ACTIVE;
    *out=result; return true;
}

bool qa_movement_move(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,0);
}
bool qa_movement_prepare_netquake(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,1);
}
bool qa_movement_physics_netquake(const qa_movement_input *in, const qa_movement_services *services, qa_movement_result *out, qa_error *error) {
    return move_stage(in,services,out,error,2);
}
bool qa_movement_apply_q2_contacts(const qa_movement_input *input, const qa_movement_services *services, qa_movement_result *result, qa_error *error) {
    if (!input||!services||!result||!qa_actor_id_equal(input->actor,result->actor)||
        (input->profile.kind!=QA_MOVEMENT_Q2_CLASSIC&&input->profile.kind!=QA_MOVEMENT_Q2_RERELEASE)||result->state.kind!=input->profile.kind) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 contact result belongs to another player or movement family"); return false;
    }
    if (result->status==QA_MOVEMENT_ACTOR_REMOVED) return true;
    qa_movement_input active_input=*input;
    qa_move_context c={.input=&active_input,.services=services,.result=result,.state=&result->state,.command=input->command,
        .error=error,.milliseconds=input->command.milliseconds,.time_ns=input->time_ns,.dt=(float)input->command.milliseconds*0.001f};
    for (size_t i=0;i<result->contact_count&&!c.removed&&!c.failed;i++) {
        c.substep=result->contacts[i].substep;
        (void)qa_move_touch(&c,&result->contacts[i].trace);
    }
    return !c.failed;
}
void qa_movement_q3_jump_pad(qa_movement_state *state, qa_actor_id pad, qa_vec3 velocity, bool flight, int32_t *event, int32_t *parameter) {
    if (event) *event=0;
    if (parameter) *parameter=0;
    if (!state||state->kind!=QA_MOVEMENT_Q3||state->data.q3.movement_type!=0||flight) return;
    qa_q3_movement_state *s=&state->data.q3;
    bool changed=s->jump_pad.registry==0||!qa_actor_id_equal(s->jump_pad,pad);
    s->jump_pad=pad; s->jump_pad_frame=s->movement_frame; s->velocity=velocity;
    if (changed) {
        float pitch=velocity.x==0&&velocity.y==0?(velocity.z>0?-90.0f:-270.0f):
            -atan2f(velocity.z,hypotf(velocity.x,velocity.y))*57.29577951308232f;
        pitch=qa_move_angle_mod(pitch);
        pitch=fabsf(pitch>180?pitch-360:pitch);
        s->event_sequence++;
        if (event) *event=13;
        if (parameter) *parameter=pitch<45?0:1;
    }
}
```

## Source selection: sed -n '425,485p' src/movement/q1/netquake.c

```text
                c->failed = true;
                return false;
            }
            if (!nq_toss(m)) return false;
            break;
        case Q1_MOVE_STEP:
            if (!(s->flags & (Q1_FLAG_ONGROUND | Q1_FLAG_FLY | Q1_FLAG_SWIM))) {
                bool hit_sound = s->velocity.z < c->input->profile.data.nq.parameters.gravity * -0.1f;
                nq_gravity(m);
                nq_velocity_bounds(m);
                q1_fly_result clip;
                if (!q1_fly(&m->base, m->dt, &clip) || !q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS)) return false;
                if (hit_sound && (s->flags & Q1_FLAG_ONGROUND) && !q1_sound(&m->base, "demon/dland2.wav")) return false;
            }
            if (!nq_water_transition(m)) return false;
            break;
        default:
            qa_error_set(c->error, QA_ERROR_UNSUPPORTED, 0, "Unsupported NetQuake player movetype %d", s->move_type);
            c->failed = true;
            return false;
        }
    }
    if (!q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS) || !q1_phase(&m->base, QA_MOVE_POSTTHINK)) return false;
    if (!nq_ideal_pitch(m)) return false;
    return q1_finish(&m->base, (s->flags & Q1_FLAG_ONGROUND) ? s->ground : q1_no_ground(), s->water_level, s->water_type);
}

static bool nq_profile(qa_move_context *c) {
    if (c->input->profile.data.nq.edition == QA_Q1_QUAKE64) {
        qa_error_set(c->error, QA_ERROR_UNSUPPORTED, 0, "Quake64 movement requires a qualified source physics profile");
        c->failed = true;
        return false;
    }
    return true;
}

bool qa_move_nq_prepare(qa_move_context *c) {
    if (!nq_profile(c)) return false;
    nq_move move = { .dt = c->dt, .time = (double)c->time_ns / 1000000000.0 };
    q1_init(&move.base, c, false);
    c->state->data.nq.view_angles = c->command.angles;
    bool ok = nq_client_think(&move);
    if (!c->removed) q1_restore_mode(&move.base);
    const qa_nq_movement_state *s = &c->state->data.nq;
    c->result->view_angles = s->view_angles;
    c->result->ground = (s->flags & Q1_FLAG_ONGROUND) ? s->ground : q1_no_ground();
    c->result->water_level = s->water_level;
    c->result->water_type = s->water_type;
    c->result->horizontal_speed = hypotf(s->velocity.x, s->velocity.y);
    return ok;
}

bool qa_move_nq_physics(qa_move_context *c) {
    if (!nq_profile(c)) return false;
    nq_move move = { .dt = c->dt, .time = (double)c->time_ns / 1000000000.0 };
    q1_init(&move.base, c, false);
    bool ok = nq_physics(&move);
    if (!c->removed) q1_restore_mode(&move.base);
    return ok;
}

```

## Source selection: sed -n '570,641p' src/movement/q3/move.c

```text
static uint32_t q3_elapsed(int32_t command, int32_t previous) {
    int64_t elapsed = (int64_t)command - previous;
    return elapsed < 1 ? 1 : elapsed > 200 ? 200 : (uint32_t)elapsed;
}

bool qa_move_q3(qa_move_context *context) {
    qa_q3_movement_state *state = &context->state->data.q3;
    qa_movement_result *result = context->result;
    int32_t final_time = context->command.server_time_ms;
    uint32_t fixed = context->input->profile.data.q3.fixed_ms;
    if (fixed == 0) fixed = 66;
    if (fixed > INT32_MAX || context->command.forward_move < -128 || context->command.forward_move > 127 ||
        context->command.side_move < -128 || context->command.side_move > 127 ||
        context->command.up_move < -128 || context->command.up_move > 127 ||
        context->command.forward_move != truncf(context->command.forward_move) ||
        context->command.side_move != truncf(context->command.side_move) ||
        context->command.up_move != truncf(context->command.up_move)) {
        qa_error_set(context->error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 movement command or fixed subdivision");
        context->failed = true;
        return false;
    }
    result->bounds = (qa_bounds){0};
    result->contact_count = 0;
    result->water_level = result->water_type = 0;
    result->horizontal_speed = 0;
    result->view_height = state->view_height;
    result->view_angles = state->view_angles;
    result->ground = state->ground;
    if (final_time < state->command_time_ms) return true;
    if ((int64_t)final_time > (int64_t)state->command_time_ms + 1000)
        state->command_time_ms = final_time - 1000;
    state->movement_frame = (int32_t)(((uint32_t)state->movement_frame + 1) & 63);
    uint32_t substep = 0;
    while (state->command_time_ms != final_time) {
        int64_t remaining = (int64_t)final_time - state->command_time_ms;
        int64_t next = (int64_t)state->command_time_ms + (remaining < fixed ? remaining : fixed);
        context->command.server_time_ms = (int32_t)next;
        context->milliseconds = q3_elapsed(context->command.server_time_ms, state->command_time_ms);
        context->dt = (float)context->milliseconds * 0.001f;
        context->time_ns = context->command.server_time_ms < 0 ? 0 :
            (uint64_t)context->command.server_time_ms * UINT64_C(1000000);
        context->substep = substep++;
        if (!qa_move_phase(context, QA_MOVE_INPUT_BEGIN)) break;
        if (!qa_move_apply_stance(context)) break;
        qa_q3_step step = { .context = context,
            .previous_origin = state->origin, .previous_velocity = state->velocity,
            .milliseconds = q3_elapsed(context->command.server_time_ms, state->command_time_ms) };
        step.dt = (float)step.milliseconds * 0.001f;
        step.mask = context->input->has_trace_policy ? context->input->trace_policy.contents_mask :
            UINT32_C(0x10001) | (context->input->state.data.q3.movement_type == Q3_SPECTATOR ? 0 : (uint32_t)Q3_CONTENTS_BODY);
        if (context->input->environment.health <= 0) step.mask &= ~(uint32_t)Q3_CONTENTS_BODY;
        if (substep == 1) result->bounds = context->input->has_current_bounds
            ? context->input->current_bounds : q3_standing_bounds(&step);
        result->contact_count = 0;
        q3_run_step(&step);
        result->water_level = step.water_level;
        result->water_type = step.water_type;
        result->horizontal_speed = step.horizontal_speed;
        result->view_height = state->view_height;
        result->view_angles = state->view_angles;
        result->ground = state->ground;
        if (context->failed) break;
        if (!qa_move_phase(context, QA_MOVE_INPUT_END)) break;
        if (state->movement_flags & Q3_JUMP_HELD) context->command.up_move = 20;
    }
    result->view_height = state->view_height;
    result->view_angles = state->view_angles;
    result->ground = state->ground;
    return !context->failed;
}
```

## Source selection: sed -n '1,65p' ../quake-typescript/src/movement/q1/netquake.ts

```text
import { clientMovementMode, clientMovementType } from "../client-outputs.ts";
import { sweepBody } from "../swept-body.ts";
import { q1WaterTransition } from "./water-transition.ts";
/* Ported from WinQuake/sv_user.c, sv_phys.c and rerelease donor extensions.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later. */
import type { ProviderId } from "../../contracts/identity.ts";
import type { Vec3 } from "../../contracts/math.ts";
import type { MovementProvider, MovementServices, MovementState, Q1MovementInput, Q1MovementResult, Q1MovementState } from "../../contracts/movement.ts";
import type { TraceResult } from "../../contracts/scene.ts";
import { MovementContext, NONE, ZERO, seconds } from "./common.ts";
import { finishMovement } from "./result.ts";
import { q1CheckWaterJump, q1PlayerJump } from "./player-actions.ts";
import { Q1_CONTENTS_EMPTY, Q1_CONTENTS_WATER, Q1_FLAG_FLY, Q1_FLAG_JUMPRELEASED, Q1_FLAG_ONGROUND, Q1_FLAG_SWIM, Q1_FLAG_WATERJUMP,
  Q1_MOVE_BOUNCE, Q1_MOVE_FLY, Q1_MOVE_FLYMISSILE, Q1_MOVE_GIB, Q1_MOVE_NOCLIP, Q1_MOVE_NONE,
  Q1_MOVE_STEP, Q1_MOVE_TOSS, Q1_MOVE_WALK, Q1_STEP_HEIGHT, type Q1MovementOptions } from "./types.ts";

type MutableState = { -readonly [K in keyof Q1MovementState]: Q1MovementState[K] };
interface FlyResult { readonly blocked: number; readonly stepTrace: TraceResult | null; }

class NetQuakeMove {
  state: MutableState;
  readonly context: MovementContext;
  readonly frameSeconds: number;
  readonly timeSeconds: number;
  constructor(readonly input: Q1MovementInput, services: MovementServices, options: Q1MovementOptions) {
    if (input.profile.edition === "quake64") throw new Error("Quake64 movement requires a qualified source physics profile");
    if (input.profile.clock.kind !== "q1-netquake") throw new Error("NetQuake movement requires a NetQuake frame clock");
    this.state = { ...input.state };
    if (input.environment.flight && input.environment.health > 0 && this.state.moveType === Q1_MOVE_WALK) this.state.moveType = Q1_MOVE_FLY;
    this.context = new MovementContext(input, services, options);
    this.applyClientMode();
    // The shared frame owner already applies host minimum/maximum/fixed frame rules.
    this.frameSeconds = seconds(input.frame.elapsed);
    this.timeSeconds = seconds(input.frame.time);
    if (!Number.isFinite(this.frameSeconds) || this.frameSeconds < 0) throw new RangeError("Invalid NetQuake frame interval");
  }
  private applyClientMode(): void {
    const mode = clientMovementMode(this.input.environment.clientOutputs, this.input.environment.health);
    if (mode !== undefined) { this.context.projectClientMode(); this.state.moveType = clientMovementType(this.input.kind, mode); }
  }
  private setState(state: MovementState): void {
    if (state.kind !== "q1-netquake") throw new Error("NetQuake callback changed the movement provider during a frame");
    Object.assign(this.state, state);
    this.applyClientMode();
    if (this.input.environment.flight && this.input.environment.health > 0 && this.state.moveType === Q1_MOVE_WALK) this.state.moveType = Q1_MOVE_FLY;
  }
  private link(touchTriggers: boolean): void { this.setState(this.context.link(this.state, touchTriggers)); }
  private impact(trace: TraceResult): void { this.setState(this.context.touch(trace, this.state)); }
  private think(): boolean {
    this.setState(this.context.lifecycle(this.state, "think"));
    return !this.context.removed;
  }
  private velocityBounds(): void {
    const m = this.context.math, s = this.state, maximum = this.context.options.maxVelocity ?? 2000;
    const coordinate = (value: number) => Number.isFinite(m.n.store(value)) ? value : 0;
    const velocity = (value: number) => Math.max(-maximum, Math.min(maximum, coordinate(value)));
    s.origin = m.vec(coordinate(s.origin.x), coordinate(s.origin.y), coordinate(s.origin.z));
    s.velocity = m.vec(velocity(s.velocity.x), velocity(s.velocity.y), velocity(s.velocity.z));
  }
  private gravity(): void {
    const m = this.context.math, n = m.n, s = this.state, p = this.input.profile.parameters;
    const entityGravity = p.entityGravity === 0 ? 1 : p.entityGravity;
    const gravity = n.multiply(n.multiply(n.multiply(entityGravity, this.input.environment.gravityMultiplier), p.gravity), this.frameSeconds);
    s.velocity = m.vec(s.velocity.x, s.velocity.y, n.subtract(s.velocity.z, gravity));
  }
```

## Source selection: cat src/main.c

```text
#include "qa/archive.h"
#include "qa/bsp.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *stream)
{
    fputs("Quake Anthology native C engine\n"
          "Usage: quake-anthology --help | --version\n"
          "       quake-anthology --inspect-bsp FILE\n"
          "       quake-anthology --list ARCHIVE\n"
          "       quake-anthology --inspect-bsp ARCHIVE MEMBER\n"
          "The baseline engine is under construction.\n", stream);
}

static int report_error(const char *source, const qa_error *error)
{
    fprintf(stderr, "%s:%zu: %s\n", source, error->offset, error->message);
    return 1;
}

static int inspect_bsp(const char *name, qa_bytes bytes)
{
    qa_error error = {0};
    qa_bsp_view map;
    qa_entities entities = {0};
    if (!qa_bsp_open(bytes, &map, &error))
        return report_error(name, &error);
    qa_entity_syntax syntax = map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1;
    if (!qa_entities_parse(map.lumps[QA_BSP_ENTITIES].bytes, syntax,
                           &entities, &error))
        return report_error(name, &error);
    printf("format: %s\nentities: %zu\n", qa_bsp_format_name(map.format), entities.count);
    for (int kind = 0; kind < QA_BSP_LUMP_COUNT; ++kind) {
        const qa_bsp_lump *lump = &map.lumps[kind];
        if (lump->present)
            printf("%s: %zu bytes, %zu records\n",
                   qa_bsp_lump_name((qa_bsp_lump_kind)kind), lump->bytes.size,
                   qa_bsp_record_count(&map, (qa_bsp_lump_kind)kind));
    }
    printf("extensions: %u\ndiagnostics: %u\n", map.extension_count, map.diagnostics);
    qa_entities_free(&entities);
    return 0;
}

static int inspect_bsp_file(const char *path)
{
    qa_error error = {0};
    qa_buffer file = {0};
    if (!qa_file_read_all(path, &file, &error))
        return report_error(path, &error);
    int result = inspect_bsp(path, (qa_bytes){file.data, file.size});
    qa_buffer_free(&file);
    return result;
}

static int inspect_archive(const char *path, const char *member)
{
    qa_error error = {0};
    qa_archive *archive = NULL;
    if (!qa_archive_open_file(path, QA_ARCHIVE_AUTO, &archive, &error))
        return report_error(path, &error);
    int result = 0;
    if (member == NULL) {
        for (size_t i = 0; i < qa_archive_count(archive); ++i) {
            const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
            printf("%zu\t%zu\t%s\n", entry->ordinal, entry->size, entry->path);
        }
    } else {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find(archive, member, QA_ARCHIVE_EXACT, 0, &entry, &error)) {
            result = report_error(path, &error);
        } else if (entry == NULL) {
            fprintf(stderr, "%s: archive member not found: %s\n", path, member);
            result = 1;
        } else {
            qa_archive_data data = {0};
            if (!qa_archive_read(archive, entry->ordinal, &data, &error))
                result = report_error(path, &error);
            else
                result = inspect_bsp(member, data.bytes);
            qa_archive_data_free(&data);
        }
    }
    qa_archive_close(archive);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Quake Anthology %s (baseline development)\n", QA_VERSION);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_bsp_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--list") == 0)
        return inspect_archive(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_archive(argv[2], argv[3]);
    usage(stderr);
    return 2;
}
```


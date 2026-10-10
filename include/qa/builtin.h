#ifndef QA_BUILTIN_H
#define QA_BUILTIN_H

#include "qa/inventory.h"
#include "qa/physics.h"
#include "qa/player_state.h"
#include "qa/session.h"
#include "qa/world.h"

typedef enum qa_builtin_event_kind {
    QA_BUILTIN_SOUND,
    QA_BUILTIN_STOP_SOUND,
    QA_BUILTIN_SHOT,
    QA_BUILTIN_IMPACT,
    QA_BUILTIN_EXPLOSION,
    QA_BUILTIN_BEAM,
    QA_BUILTIN_TELEPORT,
    QA_BUILTIN_ITEM,
    QA_BUILTIN_ANIMATION,
    QA_BUILTIN_MESSAGE,
    QA_BUILTIN_TARGET,
    QA_BUILTIN_DEATH,
    QA_BUILTIN_MUZZLE,
    QA_BUILTIN_TRAIL,
    QA_BUILTIN_LIGHT,
    QA_BUILTIN_CENTERPRINT,
    QA_BUILTIN_ACHIEVEMENT,
    QA_BUILTIN_PARTICLES,
    QA_BUILTIN_EFFECT,
    QA_BUILTIN_LOG,
    QA_BUILTIN_CTF_STATUS,
    QA_BUILTIN_SOURCE_LOG,
    QA_BUILTIN_CTF_CAPTURE,
    QA_BUILTIN_Q1_POWERUP,
    QA_BUILTIN_SOURCE_PROMPT,
    QA_BUILTIN_CLEAR_PROMPT,
    QA_BUILTIN_Q2_PLAYER_ANIMATION,
    QA_BUILTIN_Q2_ENTITY_EVENT
} qa_builtin_event_kind;

enum { QA_BUILTIN_SOUND_POSITIONED = 4u };

typedef enum qa_builtin_message_arg_kind {
    QA_BUILTIN_MESSAGE_STRING,
    QA_BUILTIN_MESSAGE_NUMBER
} qa_builtin_message_arg_kind;
typedef struct qa_builtin_message_arg {
    qa_builtin_message_arg_kind kind;
    union {
        qa_string_id text;
        double number;
    } value;
} qa_builtin_message_arg;

typedef struct qa_builtin_ctf_status {
    double red, blue, flags, rune_items;
} qa_builtin_ctf_status;
typedef struct qa_builtin_ctf_capture {
    double total;
    bool blue;
} qa_builtin_ctf_capture;
typedef struct qa_builtin_prompt_choice {
    qa_string_id label;
    int32_t impulse;
} qa_builtin_prompt_choice;

typedef struct qa_builtin_q1_powerup {
    /* The actual qa_q1_power ordinal; the Q1 owner admits this declaration. */
    uint32_t power;
    double expires;
} qa_builtin_q1_powerup;

/* Resource and text IDs use the session string table. Events and arguments are
 * synchronous borrows; a queue copies arguments and resolves or retains string
 * storage before returning. PARTICLES uses origin, direction, code (palette
 * color) and count. EFFECT uses family, resource and code for authored effects
 * without a more specific shared event kind. LOG retains source-wide text,
 * provider and source time; it has no actor, message arguments or HUD effect.
 * CTF_STATUS retains the source's four finite numbers for the addressed actor;
 * flags and rune_items undergo bit conversion only at the HUD consumer.
 * SOURCE_LOG retains the Q1 source player's full actor and action in text.
 * CTF_CAPTURE retains the Q1 source team's finite total; it has no actor or
 * message arguments. Source clocks and providers qualify both records.
 * SOURCE_PROMPT addresses a full source actor, uses text for its title and
 * borrows ordered prompt_choices until the queue copies them. CLEAR_PROMPT
 * addresses that actor without title or choices. */
typedef enum qa_builtin_q2_multicast_kind {
    QA_BUILTIN_Q2_MULTICAST_NONE,
    QA_BUILTIN_Q2_MULTICAST_PVS,
    QA_BUILTIN_Q2_MULTICAST_PHS,
    QA_BUILTIN_Q2_MULTICAST_ALL,
    QA_BUILTIN_Q2_MULTICAST_PHS_LINE
} qa_builtin_q2_multicast_kind;

typedef struct qa_builtin_q2_multicast {
    qa_builtin_q2_multicast_kind kind;
    qa_vec3 origin;
} qa_builtin_q2_multicast;

typedef struct qa_builtin_event {
    qa_builtin_event_kind kind;
    qa_game_family family;
    qa_actor_owner provider;
    qa_actor_id actor, other;
    uint64_t time_ns;
    qa_string_id resource, text;
    /* Q2 pickup's authored canonical item, distinct from icon and display name. */
    qa_item_id item;
    qa_vec3 origin, end, direction;
    /* Transient Source multicast input; the application retains actual recipients. */
    qa_builtin_q2_multicast q2_multicast;
    /* Monster muzzle pose at emission, before later Source frames can move it. */
    qa_vec3 muzzle_angles;
    float muzzle_scale;
    bool has_muzzle_pose;
    float volume, attenuation, value;
    int32_t code, channel, count, frame;
    uint32_t flags;
    const qa_builtin_message_arg *arguments;
    size_t argument_count;
    qa_builtin_ctf_status ctf_status;
    qa_builtin_ctf_capture ctf_capture;
    qa_builtin_q1_powerup q1_powerup;
    const qa_builtin_prompt_choice *prompt_choices;
    size_t prompt_choice_count;
} qa_builtin_event;

typedef struct qa_builtin_actor_traits {
    qa_string_id classname;
    qa_actor_id owner;
    bool player, monster, spectator, no_target, invisible, aimed_damage, laser_immune;
    bool damageable_target, no_source_friendly_fire, grounded;
    float view_height, gib_health, max_health;
    uint64_t hostile_until_ns;
    /* Semantic source births, qualified by the selected CHARACTER owner. */
    uint64_t birth_epoch;
    bool has_life, dead;
} qa_builtin_actor_traits;

/* Borrowed connection/selected-character projection, never a second player
 * store. Text remains valid until a mutating service callback. Scores belong
 * to an explicitly selected mode and are queried through that mode's owner. */
typedef struct qa_builtin_player_info {
    const char *name, *skin;
    uint32_t slot;
    int32_t ping;
    uint64_t entered_ns;
    float view_height, killer_yaw;
    bool connected, spectator, dead;
} qa_builtin_player_info;

/* Selected effects owners project absolute session deadlines; zero means the
 * effect is absent. This view does not grant effects or own their timers. */
typedef struct qa_builtin_powerups {
    uint64_t quad_until_ns, double_until_ns, invulnerability_until_ns;
    uint64_t quad_fire_until_ns;
} qa_builtin_powerups;

/* Borrowed current owner and its existing command-local continuation. */
typedef struct qa_builtin_player_control {
    qa_player_state *player;
    qa_movement_state *state;
    qa_vec3 *view_angles;
    float *view_height;
    qa_movement_ground *ground;
    bool source_movement;
} qa_builtin_player_control;

typedef enum qa_builtin_motion_reason {
    QA_BUILTIN_MOTION_TELEPORT,
    QA_BUILTIN_MOTION_LAUNCH,
    QA_BUILTIN_MOTION_RESET
} qa_builtin_motion_reason;
typedef struct qa_builtin_motion_change {
    qa_builtin_motion_reason reason;
    qa_body_state body;
    qa_vec3 view_angles, angular_kick;
    qa_vec3 command_view_angles;
    int32_t source_command_angle_words[3];
    uint64_t hold_ns;
    bool force_view_angles, apply_angular_kick;
    bool preserve_command_angles;
    bool has_command_view_angles, has_source_command_angle_words;
} qa_builtin_motion_change;

typedef enum qa_builtin_projectile_role {
    QA_BUILTIN_ROCKET,
    QA_BUILTIN_GRENADE,
    QA_BUILTIN_NAIL,
    QA_BUILTIN_BOLT,
    QA_BUILTIN_PLASMA,
    QA_BUILTIN_ENERGY,
    QA_BUILTIN_GRAPPLE
} qa_builtin_projectile_role;
typedef struct qa_builtin_weapon_launch {
    qa_actor_id projectile, shooter;
    qa_item_id weapon;
    qa_actor_owner provider;
    qa_builtin_projectile_role role;
    uint64_t time_ns;
    qa_body_state body;
} qa_builtin_weapon_launch;
typedef struct qa_builtin_trajectory_update {
    qa_vec3 origin, velocity, angles;
} qa_builtin_trajectory_update;

typedef enum qa_builtin_actor_callback_kind {
    QA_BUILTIN_ACTOR_USE, QA_BUILTIN_ACTOR_PAIN, QA_BUILTIN_ACTOR_DIE,
    QA_BUILTIN_ACTOR_THINK
} qa_builtin_actor_callback_kind;
typedef struct qa_builtin_actor_callback_request {
    qa_game_family family;
    qa_actor_owner provider;
    qa_actor_id self;
    union {
        struct { uint64_t time_ns, elapsed_ns; } think;
        struct { qa_actor_id other, activator; } use;
        struct { qa_actor_id attacker; float damage, kick; } pain;
        struct { qa_actor_id attacker, inflictor; float damage, kick; qa_vec3 point; } die;
    } source;
} qa_builtin_actor_callback_request;
typedef bool (*qa_builtin_actor_callback_body)(void *,
    const qa_builtin_actor_callback_request *, bool *, qa_error *);

/* Borrowed capabilities, never an alternate world or outer game loop. The
 * application owns these services and forwards actor retirement to all of them.
 * A provider keeps its own typed extension state indexed by full actor IDs. */
typedef struct qa_builtin_services {
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_pickups *pickups;
    qa_physics *physics;
    void *context;
    void *cvar_context;
    bool (*emit)(void *, const qa_builtin_event *, qa_error *);
    bool (*use_targets)(void *, qa_actor_id source, qa_actor_id activator, qa_string_id target,
                        qa_string_id killtarget, float delay, qa_error *);
    bool (*cvar)(void *, qa_string_id name, float *value, qa_error *);
    bool (*actor_traits)(void *, qa_actor_id, qa_builtin_actor_traits *);
    /* Read-only roster query in client order, including selected foreign
     * characters. The caller owns the output storage; never retain its pointer. */
    bool (*players)(void *, qa_actor_id *, size_t capacity, size_t *count, qa_error *);
    /* Read-only; false means this actor has no connected player projection. */
    bool (*player_info)(void *, qa_actor_id, qa_builtin_player_info *);
    bool (*player_control)(void *, qa_actor_owner observer, qa_actor_id,
                           qa_builtin_player_control *, qa_error *);
    /* Read-only. Success returns zero deadlines when no selected effect applies. */
    /* Deadlines are translated to the observer's source clock. */
    bool (*powerups)(void *, qa_actor_owner observer, qa_actor_id,
                     qa_builtin_powerups *, qa_error *);
    /* Body storage is already committed. The selected movement owner updates
     * its continuation and command-angle delta before subsequent commands. */
    bool (*motion_changed)(void *, qa_actor_id, const qa_builtin_motion_change *, qa_error *);
    bool (*weapon_launch)(void *, const qa_builtin_weapon_launch *, qa_builtin_trajectory_update *,
                          bool *changed, qa_error *);
    bool (*controls_trajectory)(void *, qa_actor_id projectile);
    bool (*weapon_trajectory)(void *, qa_actor_id projectile, const qa_body_state *,
                              uint64_t time_ns, qa_builtin_trajectory_update *, bool *changed,
                              qa_error *);
    bool (*actor_callback)(void *, qa_builtin_actor_callback_kind,
        const qa_builtin_actor_callback_request *, qa_builtin_actor_callback_body,
        void *body_context, bool *result, qa_error *);
} qa_builtin_services;

typedef struct qa_builtin_spawn {
    qa_actor_owner owner;
    qa_actor_definition definition;
    bool has_source;
    uint32_t source_slot;
    qa_body_state body;
    const qa_actor_collision *collision;
    const qa_combat_state *combat;
    const qa_inventory_entry *inventory;
    size_t inventory_count;
    bool link;
} qa_builtin_spawn;

bool qa_builtin_services_validate(const qa_builtin_services *, qa_error *);

typedef struct qa_builtin_check_client_row {
    bool present, no_target;
    float health;
} qa_builtin_check_client_row;
/* Client slots are physical Source indices 1..capacity. A selection read
 * requires no_target; a return read needs only presence and health. The eye
 * reader also reads inactive reserved rows. Cache fields remain host-owned,
 * and result slot zero denotes the Source world. */
typedef struct qa_builtin_check_client_query {
    qa_session *session;
    qa_actor_owner provider;
    qa_world *world;
    uint32_t capacity;
    uint32_t *slot;
    double *time;
    int32_t *cluster;
    void *context;
    bool (*client)(void *, uint32_t, bool selection, qa_builtin_check_client_row *, qa_error *);
    bool (*client_eye)(void *, uint32_t, qa_vec3 *, qa_error *);
    bool (*observer_eye)(void *, qa_vec3 *, qa_error *);
} qa_builtin_check_client_query;
bool qa_builtin_check_client(const qa_builtin_check_client_query *, uint32_t *, qa_error *);
bool qa_builtin_spawn_actor(const qa_builtin_services *, const qa_builtin_spawn *, qa_actor_id *,
                            qa_error *);
bool qa_builtin_emit(const qa_builtin_services *, const qa_builtin_event *, qa_error *);
bool qa_builtin_resource(const qa_builtin_services *, const char *, qa_string_id *, qa_error *);
/* Zero-initialize once, reuse between calls, and free at owner teardown. Each
 * nested gameplay query needs its own retained snapshot. IDs are observations;
 * recheck their generations after callbacks before using live actor state. */
typedef struct qa_builtin_actor_snapshot {
    qa_actor_id *ids, *sort;
    size_t count, capacity;
} qa_builtin_actor_snapshot;
bool qa_builtin_snapshot_reserve(qa_builtin_actor_snapshot *, size_t, qa_error *);
void qa_builtin_snapshot_free(qa_builtin_actor_snapshot *);
/* Zero-initialize a provider's pool. Nested queries borrow distinct retained
 * frames until release; free the pool only after all queries have returned. */
typedef struct qa_builtin_snapshot_frame {
    struct qa_builtin_snapshot_frame *next;
    qa_builtin_actor_snapshot snapshot;
    bool active;
} qa_builtin_snapshot_frame;
qa_builtin_snapshot_frame *qa_builtin_snapshot_acquire(qa_builtin_snapshot_frame **,
                                                       size_t, qa_error *);
void qa_builtin_snapshot_release(qa_builtin_snapshot_frame *);
void qa_builtin_snapshot_pool_free(qa_builtin_snapshot_frame **);
/* Nearby includes bodies without collision membership and measures origins,
 * then sorts using physics.source_order. Players preserves client order. */
bool qa_builtin_nearby(const qa_builtin_services *, qa_vec3 origin, float radius,
                       qa_builtin_actor_snapshot *, qa_error *);
bool qa_builtin_observations(const qa_builtin_services *, qa_builtin_actor_snapshot *, qa_error *);
/* Sort retained observations using the configured source-order query. */
void qa_builtin_sort_observations(const qa_builtin_services *, qa_builtin_actor_snapshot *);
bool qa_builtin_players(const qa_builtin_services *, qa_builtin_actor_snapshot *, qa_error *);
int qa_builtin_source_order(const qa_builtin_services *, qa_actor_id, qa_actor_id);
/* Update authoritative body fields only; each source retains its own trace and
 * link timing, so a trajectory update does not publish a premature link. */
bool qa_builtin_launch_projectile(const qa_builtin_services *, const qa_builtin_weapon_launch *,
                                  bool *changed, qa_error *);
bool qa_builtin_step_projectile(const qa_builtin_services *, qa_actor_id, uint64_t time_ns,
                                bool *changed, qa_error *);

typedef struct qa_builtin_hitscan {
    qa_trace_query trace;
    qa_attack attack;
    float damage, knockback;
    /* Piercing uses caller-owned retained scratch. A zero capacity is an
     * ordinary single trace. Damage callbacks can mutate or retire any actor. */
    qa_actor_id *excluded;
    size_t excluded_capacity;
    void *context;
    bool (*prepare)(void *, const qa_trace_result *, qa_damage_request *, bool *apply_damage,
                    qa_error *);
    bool (*hit)(void *, const qa_trace_result *, const qa_damage_outcome *, bool *continue_trace,
                qa_error *);
} qa_builtin_hitscan;
bool qa_builtin_fire_hitscan(const qa_builtin_services *, const qa_builtin_hitscan *,
                             qa_trace_result *last, qa_error *);

typedef enum qa_builtin_radius_distance {
    QA_RADIUS_CENTER,
    QA_RADIUS_BOUNDS
} qa_builtin_radius_distance;
typedef struct qa_builtin_radius {
    qa_attack attack;
    qa_vec3 origin;
    float radius, damage, distance_scale, self_scale, knockback_scale;
    float direction_z_bias;
    qa_actor_id ignore;
    qa_actor_id visibility_pass;
    qa_trace_policy trace;
    qa_builtin_radius_distance distance;
    /* Original families test four horizontal offsets. Eight-corner visibility
     * is available for selected mod policies that explicitly request it. */
    bool check_visibility, corner_visibility;
    /* Optional caller-owned actor snapshot. When supplied,
     * only these candidates are considered, with generations rechecked. */
    bool has_candidates;
    /* Candidates already passed the source radius test (for example Q2 uses
     * origins for admission but centers for falloff). Do not test it twice. */
    bool candidate_radius_only;
    const qa_actor_id *candidates;
    size_t candidate_count;
    void *context;
    bool (*adjust)(void *, qa_actor_id, float *damage, float *knockback, bool *allowed, qa_error *);
    bool (*after)(void *, const qa_damage_outcome *, qa_error *);
    bool (*prepare)(void *, qa_damage_request *, bool *allowed, qa_error *);
} qa_builtin_radius;
bool qa_builtin_radius_damage(const qa_builtin_services *, const qa_builtin_radius *,
                              size_t *damaged, qa_error *);
bool qa_builtin_can_damage(const qa_builtin_services *, qa_vec3 origin, qa_actor_id target,
                           qa_actor_id pass, qa_trace_policy, bool corners, bool *, qa_error *);

/* Source providers own trajectories and impact policy. This shared step traces
 * a proposed position, commits it, links once, then invokes the source impact.
 * A callback may bounce, penetrate, attach, explode, or release the projectile. */
typedef struct qa_builtin_projectile_step {
    qa_actor_id actor, owner;
    qa_vec3 end;
    qa_trace_policy trace;
    bool point;
    void *context;
    bool (*impact)(void *, qa_actor_id, const qa_trace_result *, qa_error *);
} qa_builtin_projectile_step;
bool qa_builtin_projectile_move(const qa_builtin_services *, const qa_builtin_projectile_step *,
                                qa_trace_result *, qa_error *);

void qa_builtin_angle_vectors(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up);
float qa_builtin_angle_mod(float);
float qa_builtin_angle_delta(float target, float current);

typedef struct qa_builtin_random {
    uint32_t words[31];
    uint8_t front, rear;
    uint64_t draws;
} qa_builtin_random;
void qa_builtin_random_seed(qa_builtin_random *, uint32_t);
uint32_t qa_builtin_random_integer(qa_builtin_random *);
float qa_builtin_random_unit(qa_builtin_random *);

#endif

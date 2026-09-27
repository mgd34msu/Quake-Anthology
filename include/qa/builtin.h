#ifndef QA_BUILTIN_H
#define QA_BUILTIN_H

#include "qa/inventory.h"
#include "qa/physics.h"
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
    QA_BUILTIN_ACHIEVEMENT
} qa_builtin_event_kind;

/* Resource and text IDs use the session string table. Events are synchronous
 * borrows; a consumer that queues one copies the value before returning. */
typedef struct qa_builtin_event {
    qa_builtin_event_kind kind;
    qa_game_family family;
    qa_actor_owner provider;
    qa_actor_id actor, other;
    uint64_t time_ns;
    qa_string_id resource, text;
    qa_vec3 origin, end, direction;
    float volume, attenuation, value;
    int32_t code, channel, count, frame;
    uint32_t flags;
} qa_builtin_event;

typedef struct qa_builtin_actor_traits {
    qa_string_id classname;
    qa_actor_id owner;
    bool player, monster, spectator, no_target, invisible, aimed_damage, laser_immune;
    bool damageable_target, no_source_friendly_fire, grounded;
    float view_height, gib_health, max_health;
    uint64_t hostile_until_ns;
} qa_builtin_actor_traits;

typedef enum qa_builtin_motion_reason {
    QA_BUILTIN_MOTION_TELEPORT,
    QA_BUILTIN_MOTION_LAUNCH,
    QA_BUILTIN_MOTION_RESET
} qa_builtin_motion_reason;
typedef struct qa_builtin_motion_change {
    qa_builtin_motion_reason reason;
    qa_body_state body;
    qa_vec3 view_angles, angular_kick;
    uint64_t hold_ns;
    bool force_view_angles, apply_angular_kick;
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
    bool (*emit)(void *, const qa_builtin_event *, qa_error *);
    bool (*use_targets)(void *, qa_actor_id source, qa_actor_id activator, qa_string_id target,
                        qa_string_id killtarget, float delay, qa_error *);
    bool (*cvar)(void *, qa_string_id name, float *value, qa_error *);
    bool (*actor_traits)(void *, qa_actor_id, qa_builtin_actor_traits *);
    /* Read-only roster query in client order, including selected foreign
     * characters. The caller owns the output storage; never retain its pointer. */
    bool (*players)(void *, qa_actor_id *, size_t capacity, size_t *count, qa_error *);
    /* Body storage is already committed. The selected movement owner updates
     * its continuation and command-angle delta before subsequent commands. */
    bool (*motion_changed)(void *, qa_actor_id, const qa_builtin_motion_change *, qa_error *);
    bool (*weapon_launch)(void *, const qa_builtin_weapon_launch *, qa_builtin_trajectory_update *,
                          bool *changed, qa_error *);
    bool (*controls_trajectory)(void *, qa_actor_id projectile);
    bool (*weapon_trajectory)(void *, qa_actor_id projectile, const qa_body_state *,
                              uint64_t time_ns, qa_builtin_trajectory_update *, bool *changed,
                              qa_error *);
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
/* Nearby includes bodies without collision membership and measures origins,
 * then sorts using physics.source_order. Players preserves client order. */
bool qa_builtin_nearby(const qa_builtin_services *, qa_vec3 origin, float radius,
                       qa_builtin_actor_snapshot *, qa_error *);
bool qa_builtin_observations(const qa_builtin_services *, qa_builtin_actor_snapshot *, qa_error *);
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

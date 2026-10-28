/* Port of src/app/bootstrap/simulation/arsenal/q3-source-legacy.ts.
 * Restore writes through the host entity/client views below (the
 * wiring layer adapts landed records/pool/missiles). Read results are
 * malloc'd arrays. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_LEGACY_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_LEGACY_H
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/contracts/session.h"

typedef struct qa_app_q3_weapon_statistics {
  qa_q3_rail_statistics rail;
  qa_owned_actor actor;
  double shots;
} qa_app_q3_weapon_statistics;

typedef enum qa_app_q3_projectile_phase_kind {
  QA_APP_Q3_PROJECTILE_FLIGHT, QA_APP_Q3_PROJECTILE_EVENT, QA_APP_Q3_PROJECTILE_ATTACHED
} qa_app_q3_projectile_phase_kind;
typedef struct qa_app_q3_projectile_state {
  qa_q3_projectile base;
  double expires;
  qa_app_q3_projectile_phase_kind phase;
  double phase_time;                 /* event */
  qa_q3_projectile_impact impact;    /* event */
  bool has_attached_target;          /* attached */
  qa_actor_id attached_target;
  double next_think;
  qa_q3_base_trajectory saved_trajectory;
} qa_app_q3_projectile_state;

typedef qa_owned_actor (*qa_app_saved_actor_fn)(void *ctx, const qa_save_reader *reader);
typedef qa_actor_id (*qa_app_saved_reference_fn)(void *ctx, qa_saved_actor_id saved);

qa_app_q3_projectile_state *qa_app_read_q3_projectile_states(const qa_save_reader *reader,
    qa_app_saved_actor_fn actor, void *actor_ctx, qa_app_saved_reference_fn reference, void *reference_ctx,
    size_t *out_count);
qa_app_q3_weapon_statistics *qa_app_read_q3_weapon_statistics(const qa_save_reader *reader,
    qa_app_saved_actor_fn actor, void *actor_ctx, size_t *out_count);

typedef struct qa_app_legacy_q3_source {
  double milliseconds, random_seed;
  qa_app_q3_projectile_state *projectiles;
  size_t projectile_count;
  qa_app_q3_weapon_statistics *statistics;
  size_t statistics_count;
  qa_owned_actor *hook_held;
  size_t hook_held_count;
} qa_app_legacy_q3_source;
qa_app_legacy_q3_source qa_app_read_legacy_q3_source(const qa_save_reader *reader,
    qa_app_saved_actor_fn owner, void *owner_ctx, qa_app_saved_reference_fn reference, void *reference_ctx);
void qa_app_legacy_q3_source_destroy(qa_app_legacy_q3_source *source);

/* map "q1"|"q2"; returned supply_profile points into out_profile storage. */
typedef struct qa_composition_owned_profile qa_composition_owned_profile;
qa_app_q3_selected_arsenal_checkpoint qa_app_migrate_legacy_q3_arsenal(qa_app_q3_selected_arsenal_checkpoint checkpoint,
    const char *map, qa_composition_owned_profile *out_profile);

typedef struct qa_app_legacy_entity_write {
  double slot;
  const char *classname;
  double weapon, e_flags, e_type, sv_flags, owner_num, clipmask;
  double damage, splash_damage, splash_radius, method_of_death, splash_method_of_death;
  qa_vec3 origin;
  qa_q3_base_trajectory trajectory;
  const char *think; /* callback name */
  double nextthink;
  bool has_parent;
  double parent_slot, other_entity_num;
  bool free_after_event;
  double event_time, event, event_parm;
} qa_app_legacy_entity_write;
typedef struct qa_app_legacy_client_write {
  double slot;
  bool set_hook;
  double hook_slot;
  bool grapple_pull;
  qa_vec3 grapple_point;
  bool set_accuracy;
  double accuracy_shots, accuracy_hits, accurate_count, impressive_count, reward_until;
  bool reward_flag;
  bool fire_held;
} qa_app_legacy_client_write;
typedef struct qa_app_legacy_continuation {
  double entity;
  qa_saved_actor_id actor, owner;
  bool has_pass;
  qa_saved_actor_id pass;
  bool has_attachment;
  qa_saved_actor_id attachment_actor;
} qa_app_legacy_continuation;

typedef struct qa_app_legacy_q3_source_host {
  void *ctx;
  const char *product;
  qa_provider_id provider;
  double max_clients;
  void (*assert_owned)(void *ctx, qa_owned_actor actor);
  bool (*native_slot)(void *ctx, qa_actor_id actor, double *out_slot, bool *out_has_client);
  bool (*body_exists)(void *ctx, qa_actor_id actor);
  bool (*has_player)(void *ctx, qa_actor_id actor);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  bool (*pool_inuse)(void *ctx, double slot);
  double (*pool_num_entities)(void *ctx);
  void (*pool_restore_counts)(void *ctx, double num_entities, double max_clients);
  void (*adopt)(void *ctx, double slot, qa_owned_actor actor);
  bool (*entity_origin)(void *ctx, double slot, qa_vec3 *out);
  void (*entity_write)(void *ctx, qa_app_legacy_entity_write write);
  void (*client_write)(void *ctx, qa_app_legacy_client_write write);
  void (*missiles_restore)(void *ctx, const qa_app_legacy_continuation *continuations, size_t count);
  void (*random_reset)(void *ctx, double seed);
} qa_app_legacy_q3_source_host;

void qa_app_restore_legacy_q3_source(qa_app_legacy_q3_source_host host, qa_app_legacy_q3_source saved,
    void (*project)(void *ctx, qa_actor_id actor), void *project_ctx);
#endif

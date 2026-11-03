/* Port of src/app/bootstrap/simulation/arsenal/q3-source-legacy.ts.
 * Read results are malloc'd arrays (release with
 * qa_app_legacy_q3_source_destroy). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_LEGACY_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_LEGACY_H
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/game/state.h"
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

struct qa_app_q3_selected_source;
/* TS restoreLegacyQ3Source(source, saved, project): a one-time import into the
 * source's own records, pool, MissileRuntime and GameRandom. project returns
 * NULL for TS null. */
void qa_app_restore_legacy_q3_source(struct qa_app_q3_selected_source *source, const qa_app_legacy_q3_source *saved,
    qa_q3_game_entity *(*project)(void *ctx, qa_actor_id actor), void *project_ctx);
#endif

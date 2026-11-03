/* Port of src/app/bootstrap/simulation/monster-checkpoint.ts: selected
 * monster and foreign-world adjunct checkpoint shapes and their readers.
 * Read checkpoints borrow strings from the reader's value tree and keep
 * arrays in `owner` (release with qa_app_sim_*_checkpoint_free, then free
 * the tree). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/base/entities/index.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/monsters/checkpoint.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/weapons/checkpoint.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/contracts/content.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/value.h"

typedef struct qa_app_sim_saved_monster_barrier { qa_saved_actor_id actor; qa_vec3 origin; } qa_app_sim_saved_monster_barrier;
/* TS SavedAuthoredMonster. placement.kind "ready" | "teleport" (origin) |
 * "waiting" (barriers, activator); activation.kind "active" | "dormant" |
 * "scheduled" (at, activator). has_* false = TS null. */
typedef struct qa_app_sim_saved_authored_monster {
  qa_saved_actor_id actor;
  qa_monster_definition_reference definition;
  const char *classname;
  double source_ordinal, spawnflags;
  const char *targetname, *target, *killtarget, *message;
  double delay;
  const char *death_target, *drop_item, *route;
  bool has_route_goal; qa_saved_actor_id route_goal;
  bool route_resolved, counted_death;
  const char *combat_target;
  bool has_combat_goal; qa_saved_actor_id combat_goal;
  bool stand_ground;
  struct {
    const char *kind;
    qa_vec3 origin;
    const qa_app_sim_saved_monster_barrier *barriers; size_t barrier_count;
    bool has_activator; qa_saved_actor_id activator;
  } placement;
  struct {
    const char *kind;
    double at;
    bool has_activator; qa_saved_actor_id activator;
  } activation;
} qa_app_sim_saved_authored_monster;

/* TS ReturnType<Q2Ballistics["captureProjectiles"]>. */
typedef struct qa_app_sim_ballistics_checkpoint {
  const qa_q2w_cause_entry *blaster_causes; size_t blaster_cause_count;
} qa_app_sim_ballistics_checkpoint;
typedef struct qa_app_sim_monster_pack_checkpoint { const char *pack; qa_q2mpm_monsters_checkpoint state; } qa_app_sim_monster_pack_checkpoint;

/* TS MonsterSourceCheckpoint union on kind "q1" | "q2". */
typedef struct qa_app_sim_monster_source_checkpoint {
  const char *kind;
  qa_provider_reference reference;
  qa_frame_context frame;
  qa_random_state random;
  qa_persist_q1_foundation_checkpoint q1_entities; /* q1 */
  qa_q2_foundation_checkpoint q2_entities;          /* q2 */
  qa_q2m_monsters_checkpoint monsters;              /* q2 */
  qa_app_sim_ballistics_checkpoint ballistics;      /* q2 */
  bool has_movers; qa_q2_movers_checkpoint movers;  /* q2; false = TS null */
  qa_app_sim_monster_pack_checkpoint *packs; size_t pack_count; /* q2 */
} qa_app_sim_monster_source_checkpoint;

/* TS SelectedMonstersCheckpoint. */
typedef struct qa_app_sim_selected_monsters_checkpoint {
  double version; /* literal 2 */
  qa_app_sim_saved_authored_monster *authored; size_t authored_count;
  qa_app_sim_monster_source_checkpoint *sources; size_t source_count;
  qa_persist_decoded owner;
} qa_app_sim_selected_monsters_checkpoint;

typedef struct qa_app_sim_foreign_q2_pack_checkpoint {
  const char *pack; qa_q2mpm_monsters_checkpoint monsters; double steam_id;
} qa_app_sim_foreign_q2_pack_checkpoint;
/* TS ForeignQ2WorldCheckpoint. */
typedef struct qa_app_sim_foreign_q2_world_checkpoint {
  double version; /* literal 1 */
  qa_provider_reference reference;
  const char *edition; /* classic|rerelease */
  const char *program; /* baseq2|xatrix|rogue|mg2 */
  qa_frame_context frame;
  qa_random_state random;
  qa_q2_foundation_checkpoint entities;
  qa_q2m_monsters_checkpoint monsters;
  qa_app_sim_ballistics_checkpoint ballistics;
  qa_q2_movers_checkpoint movers;
  qa_q2_items_checkpoint items;
  qa_q2_base_entities_module_checkpoint base_entities;
  qa_app_sim_foreign_q2_pack_checkpoint *packs; size_t pack_count;
  qa_persist_decoded owner;
} qa_app_sim_foreign_q2_world_checkpoint;

typedef struct qa_app_sim_adjunct_cvar { const char *name; const char *value; } qa_app_sim_adjunct_cvar;
/* TS ForeignQ1WorldCheckpoint; has_adjunct false = absent (older saves). */
typedef struct qa_app_sim_foreign_q1_world_checkpoint {
  double version; /* literal 1 */
  qa_provider_reference reference;
  const char *edition; /* classic|rerelease */
  const char *program; /* id1|hipnotic|rogue|dopa|mg1|mg3|ctf */
  qa_frame_context frame;
  qa_random_state random;
  qa_persist_q1_foundation_checkpoint entities;
  bool has_adjunct;
  struct { double flags; double skill; qa_app_sim_adjunct_cvar *cvars; size_t cvar_count; } adjunct;
  qa_persist_decoded owner;
} qa_app_sim_foreign_q1_world_checkpoint;

qa_app_sim_foreign_q2_world_checkpoint qa_app_sim_read_foreign_q2_world_checkpoint(const qa_save_reader *reader);
qa_app_sim_foreign_q1_world_checkpoint qa_app_sim_read_foreign_q1_world_checkpoint(const qa_save_reader *reader);
qa_app_sim_selected_monsters_checkpoint qa_app_sim_read_selected_monsters_checkpoint(const qa_save_reader *reader);
void qa_app_sim_foreign_q2_world_checkpoint_free(qa_app_sim_foreign_q2_world_checkpoint *checkpoint);
void qa_app_sim_foreign_q1_world_checkpoint_free(qa_app_sim_foreign_q1_world_checkpoint *checkpoint);
void qa_app_sim_selected_monsters_checkpoint_free(qa_app_sim_selected_monsters_checkpoint *checkpoint);
#endif

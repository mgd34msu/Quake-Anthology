/* Port of src/content/q2/foundation/checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_CHECKPOINT_H
#define QA_CONTENT_Q2_FOUNDATION_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"
bool qa_q2_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out);
qa_owned_actor qa_q2_restore_actor(qa_q2_game_services *g, qa_saved_actor_id actor);
/* TS Q2AttackCheckpoint: AttackProvenance with saved ids. Pointers in
 * qa_attack_provenance are retargeted to caller scratch on restore. */
typedef struct qa_q2_attack_checkpoint {
  qa_attack_provenance values;
  bool has_attacker, has_inflictor;
  qa_saved_actor_id attacker, inflictor;
  bool has_originating_projectile;
  qa_saved_actor_id originating_projectile;
} qa_q2_attack_checkpoint;
qa_q2_attack_checkpoint qa_q2_save_attack(qa_attack_provenance attack);
qa_attack_provenance qa_q2_restore_attack(qa_q2_attack_checkpoint in,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id a), void *ctx,
    qa_actor_id *attacker_scratch, qa_actor_id *inflictor_scratch);
typedef struct qa_q2_saved_link { bool has; qa_saved_actor_id v; } qa_q2_saved_link;
/** TS Q2EntityCheckpoint. values = entity minus actor/spawn/links/callbacks/flare/lastAttack. */
typedef struct qa_q2_entity_checkpoint {
  qa_saved_actor_id actor;
  bool has_source_slot; double source_slot;
  qa_q2_spawn_fields spawn;
  qa_q2_entity values;
  qa_q2_saved_link activator, enemy, owner, goal, team_master, team_chain, chain, beam, beam2, proboscus;
  bool has_last_attack; qa_q2_attack_checkpoint last_attack;
  const char *think, *prethink, *postthink, *use, *touch, *pain, *die, *blocked;
} qa_q2_entity_checkpoint;
typedef struct qa_q2_freed_slot { double slot, time; } qa_q2_freed_slot;
typedef struct qa_q2_foundation_checkpoint {
  double version; /* 1 */
  double next_source_slot, sequence;
  const qa_q2_freed_slot *freed_slots; size_t freed_slot_count;
  qa_q2_counters counters;
  const qa_q2_entity_checkpoint *entities; size_t entity_count;
} qa_q2_foundation_checkpoint;
#endif

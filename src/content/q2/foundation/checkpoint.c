/* Port of src/content/q2/foundation/checkpoint.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/foundation/support.h"

bool qa_q2_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out) {
  if (actor == NULL) return false;
  *out = (qa_saved_actor_id){(double)actor->slot, (double)actor->generation};
  return true;
}

qa_owned_actor qa_q2_restore_actor(qa_q2_game_services *g, qa_saved_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_saved(g->host->actors, actor, &owner))
    qa_range_error("Q2 checkpoint references missing actor %.17g:%.17g", actor.slot, actor.generation);
  return owner;
}

qa_q2_attack_checkpoint qa_q2_save_attack(qa_attack_provenance a) {
  qa_q2_attack_checkpoint out = {0};
  out.values = a;
  out.has_attacker = qa_q2_save_actor(a.attacker, &out.attacker);
  out.has_inflictor = qa_q2_save_actor(a.inflictor, &out.inflictor);
  out.has_originating_projectile = a.has_originating_projectile;
  if (a.has_originating_projectile)
    out.originating_projectile = (qa_saved_actor_id){(double)a.originating_projectile.slot, (double)a.originating_projectile.generation};
  out.values.attacker = NULL;
  out.values.inflictor = NULL;
  return out;
}

qa_attack_provenance qa_q2_restore_attack(qa_q2_attack_checkpoint in,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id a), void *ctx,
    qa_actor_id *attacker_scratch, qa_actor_id *inflictor_scratch) {
  qa_attack_provenance out = in.values;
  if (in.has_attacker) { *attacker_scratch = reference(ctx, in.attacker); out.attacker = attacker_scratch; }
  if (in.has_inflictor) { *inflictor_scratch = reference(ctx, in.inflictor); out.inflictor = inflictor_scratch; }
  out.has_originating_projectile = in.has_originating_projectile;
  if (in.has_originating_projectile) out.originating_projectile = reference(ctx, in.originating_projectile);
  return out;
}

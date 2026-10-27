/* Port of src/content/q2/foundation/monsters/checkpoint.ts (types only)
 * + foundation/checkpoint.ts attack save/restore mirror. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/checkpoint.h"
#include <string.h>

qa_q2m_attack_checkpoint qa_q2m_save_attack(const qa_attack_provenance *a) {
  qa_q2m_attack_checkpoint out;
  memset(&out, 0, sizeof out);
  out.sequence = a->sequence; out.time = a->time;
  out.has_attacker = qa_q2f_save_actor(a->attacker, &out.attacker);
  out.has_inflictor = qa_q2f_save_actor(a->inflictor, &out.inflictor);
  out.has_originating_projectile = a->has_originating_projectile;
  if (a->has_originating_projectile) (void)qa_q2f_save_actor(&a->originating_projectile, &out.originating_projectile);
  out.weapon = a->weapon; out.weapon_provider = a->weapon_provider;
  out.has_damage_powerup_owner = a->has_damage_powerup_owner; out.damage_powerup_owner = a->damage_powerup_owner;
  out.combat_provider = a->combat_provider; out.inventory_provider = a->inventory_provider;
  out.movement_provider = a->movement_provider; out.cause = a->cause;
  return out;
}
qa_attack_provenance qa_q2m_restore_attack(const qa_q2m_attack_checkpoint *c, qa_q2m_actor_reference reference,
                                           qa_actor_id *attacker_storage, qa_actor_id *inflictor_storage) {
  qa_attack_provenance out;
  memset(&out, 0, sizeof out);
  out.sequence = c->sequence; out.time = c->time;
  if (c->has_attacker) { *attacker_storage = reference.fn(reference.ctx, c->attacker); out.attacker = attacker_storage; }
  if (c->has_inflictor) { *inflictor_storage = reference.fn(reference.ctx, c->inflictor); out.inflictor = inflictor_storage; }
  out.has_originating_projectile = c->has_originating_projectile;
  if (c->has_originating_projectile) out.originating_projectile = reference.fn(reference.ctx, c->originating_projectile);
  out.weapon = c->weapon; out.weapon_provider = c->weapon_provider;
  out.has_damage_powerup_owner = c->has_damage_powerup_owner; out.damage_powerup_owner = c->damage_powerup_owner;
  out.combat_provider = c->combat_provider; out.inventory_provider = c->inventory_provider;
  out.movement_provider = c->movement_provider; out.cause = c->cause;
  return out;
}

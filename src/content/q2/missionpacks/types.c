/* Port of src/content/q2/missionpacks/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/types.h"

#include <string.h>

#include "missionpacks_internal.h"
#include "qa/core/numeric.h"
#include "qa/world/gameplay/inventory.h"

const char *qa_q2mp_pack_name(qa_q2mp_pack pack) { return pack == QA_Q2MP_PACK_XATRIX ? "xatrix" : "rogue"; }
const char *qa_q2mp_player_effect_kind_name(qa_q2mp_player_effect_kind kind) {
  switch (kind) {
    case QA_Q2MP_EFFECT_TRACKER_PAIN: return "tracker-pain";
    case QA_Q2MP_EFFECT_NUKE_BLIND: return "nuke-blind";
    case QA_Q2MP_EFFECT_IR: return "ir";
    case QA_Q2MP_EFFECT_SPHERE_CAMERA: return "sphere-camera";
  }
  return "tracker-pain";
}

bool qa_q2mp_kill_box(qa_q2f_entity *entity, qa_q2f_game *game) {
  for (;;) {
    qa_body_state body = q2mp_body(game, entity);
    qa_trace_result trace = q2mp_trace(game, body.origin, body.origin, &body.bounds, &entity->actor.id, 0x2010003);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) return !trace.start_solid && !trace.all_solid;
    qa_actor_id victim = trace.hit.actor;
    if (!q2mp_cread(game, victim).has_value) return false;
    q2mp_damage(game, victim, entity, entity->actor.id, &entity->actor.id, 100000, 0, q2mp_zero(), body.origin,
                q2mp_zero(), 21, true, 32, NULL);
    qa_q2f_entity *live = q2mp_ent(game, true, victim);
    if (live == NULL) {
      qa_trace_result next = q2mp_trace(game, body.origin, body.origin, &body.bounds, &entity->actor.id, 0x2010003);
      return next.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(next.hit.actor, victim);
    }
    if (live->solid != QA_Q2F_SOLID_NONE) return false;
  }
}

double qa_q2mp_inventory_give(qa_q2f_game *game, qa_owned_actor actor, qa_item_id item, double count) {
  size_t n = 0;
  const qa_inventory_entry *entries = game->host->inventory.entries(game->host->inventory.ctx, actor.id, &n);
  const qa_inventory_entry *entry = NULL;
  for (size_t i = 0; i < n; i++)
    if (strcmp(entries[i].item, item) == 0) { entry = &entries[i]; break; }
  if (entry == NULL) return 0;
  qa_inventory_give_transition transition = qa_inventory_give(*entry, count);
  if (!transition.is_write) return 0;
  game->host->inventory.configure(game->host->inventory.ctx, actor, transition.entry);
  return transition.given;
}

double qa_q2mp_integer_field(const qa_q2f_spawn_fields *fields, const char *key, double fallback) {
  const char *value = qa_q2f_spawn_value(fields, key);
  return value == NULL ? fallback : qa_native_atoi(value);
}
qa_vec3 qa_q2mp_movedir(qa_vec3 angles) {
  if (angles.x == 0 && angles.y == -1 && angles.z == 0) return q2mp_v3(0, 0, 1);
  if (angles.x == 0 && angles.y == -2 && angles.z == 0) return q2mp_v3(0, 0, -1);
  return qa_math_angle_vectors_rule(q2mp_v3(angles.x, angles.y, 0), QA_ANGLE_RULE_NATIVE).forward;
}

/* Port of src/content/q2/rerelease/killbox.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/killbox.h"
#include "qa/content/q2/foundation/fields.h"
#include "rr_internal.h"
#include <string.h>

bool qa_q2_kill_rerelease_box(qa_q2_entity *e, qa_q2_game_services *g, qa_q2_rerelease_players *p, bool spawning, bool exact) {
  qa_q2rr_player_state *self = qa_q2rr_players_get_state(p, e->actor.id);
  if (self && self->noclip) return true;
  qa_body_state body = rr_body_of(g, e);
  qa_vec3 mn = qa_q2_add(body.origin, body.bounds.min), mx = qa_q2_add(body.origin, body.bounds.max);
  size_t n = 0;
  const qa_actor_observation *obs = qa_session_actor_registry_observations(g->host->actors, &n);
  for (size_t i = 0; i < n; i++) {
    qa_actor_id actor = obs[i].id;
    if (qa_same_actor(actor, e->actor.id)) continue;
    qa_q2_entity *target = rr_ent(g, &actor);
    qa_linked_body_result linked = qa_shared_body_table_linked(g->host->bodies, actor);
    if (!linked.has_value || !rr_can_take_damage(g, actor)) continue;
    if (target && (!target->solid || strcmp(target->solid, "box") != 0)) continue;
    qa_bounds b = linked.value.absolute_bounds;
    if (b.max.x < mn.x || b.min.x > mx.x || b.max.y < mn.y || b.min.y > mx.y || b.max.z < mn.z || b.min.z > mx.z) continue;
    if (spawning && g->options.mode == QA_Q2_MODE_COOP && !p->rerelease_options.coop_player_collision &&
        g->host->is_player(g->host->ctx, actor)) continue;
    if (exact && e->model && e->model[0] == '*' && !p->rerelease_hooks.clip_trigger(p->rerelease_hooks.ctx, e, actor, g)) continue;
    if (g->options.mode == QA_Q2_MODE_COOP && g->host->is_player(g->host->ctx, e->actor.id) &&
        g->host->is_player(g->host->ctx, actor)) {
      e->clip_mask = rr_bnot_and(e->clip_mask, 0x40000000);
      if (target) target->clip_mask = rr_bnot_and(target->clip_mask, 0x40000000);
      if (p->rerelease_hooks.player_collision) {
        p->rerelease_hooks.player_collision(p->rerelease_hooks.ctx, e->actor.id, false);
        p->rerelease_hooks.player_collision(p->rerelease_hooks.ctx, actor, false);
      }
      continue;
    }
    g->damage(g->ctx, actor, e, e->actor.id, &e->actor.id, 100000, 0, QA_Q2_ZERO, body.origin, QA_Q2_ZERO, spawning ? 57 : 21, 32, NULL);
  }
  return true;
}

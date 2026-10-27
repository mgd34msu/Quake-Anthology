/* Port of src/content/q2/missionpacks/monsters/dabeam.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/dabeam.h"

#include "mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void beam_hit_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
qa_q2f_think qa_q2mpm_dabeam_hit_think(void) {
  qa_q2f_think think = {NULL, beam_hit_fn};
  return think;
}
static void beam_hit_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  qa_vec3 end = qa_add3_double(origin, qa_scale3_double(entity->movedir, 2048));
  qa_vec3 start = origin;
  qa_actor_id ignore = entity->actor.id;
  qa_vec3 endpoint = end;
  for (;;) {
    qa_trace_result trace = q2mpm_trace(game, start, end, NULL, &ignore, (double)(1u | 0x2000000u | 0x4000000u));
    endpoint = trace.end;
    qa_actor_id actor;
    if (!qa_q2m_trace_ground_actor(trace, game, &actor)) break;
    qa_q2f_entity *target = game->entity(game->ctx, &actor);
    qa_combat_state_result state = game->host->combat.read(game->host->combat.ctx, actor);
    bool owner_is_actor = entity->has_owner && qa_same_actor(actor, entity->owner);
    if (state.has_value && state.value.can_take_damage && (!target || !target->laser_immune)
        && ((uint32_t)(target ? target->flags : 0) & 4u) == 0 && !owner_is_actor) {
      const qa_actor_id *attacker = entity->has_owner ? &entity->owner : NULL;
      (void)q2mpm_damage(game, actor, entity, attacker, entity->damage, game->options.skill, entity->movedir,
                         trace.end, q2mpm_zero(), 30, true, 4);
    }
    qa_combat_state_result updated = game->host->combat.read(game->host->combat.ctx, actor);
    if (entity->damage < 0 && game->host->is_player(game->host->ctx, actor) && updated.has_value
        && updated.value.health > 100) {
      qa_q2f_owned_result owned = game->host->actors.resolve_owned(game->host->actors.ctx, actor);
      if (owned.has_value)
        game->host->combat.set_health(game->host->combat.ctx, owned.value, updated.value.health + entity->damage);
    }
    if (!game->host->is_monster(game->host->ctx, actor) && !game->host->is_player(game->host->ctx, actor)) {
      if (((uint32_t)entity->spawnflags & 0x80000000u) != 0) {
        entity->spawnflags = (double)((uint32_t)entity->spawnflags & ~0x80000000u);
        qa_vec3 direction = trace.contact.has_plane ? trace.contact.plane.normal : q2mpm_zero();
        q2mpm_emit_effect(game, "q2:laser-sparks", trace.end, direction, 10, (double)((uint32_t)entity->skin & 255u));
      }
      break;
    }
    ignore = actor;
    start = trace.end;
  }
  qa_q2mpm_beam_event beam;
  memset(&beam, 0, sizeof beam);
  beam.actor = entity->actor.id;
  beam.start = origin;
  beam.end = endpoint;
  beam.width = 2;
  beam.color = entity->skin;
  beam.visible = true;
  qa_q2mpm_emit_beam(beam);
  game->schedule(game->ctx, entity, 0.1, qa_q2f_free_entity_think());
}
static qa_q2f_named_think dabeam_thinks[2];
static bool dabeam_thinks_ready;
qa_q2f_callback_definitions qa_q2mpm_monster_dabeam_callbacks(void) {
  if (!dabeam_thinks_ready) {
    dabeam_thinks[0].name = "dabeam_hit";
    dabeam_thinks[0].cb = qa_q2mpm_dabeam_hit_think();
    dabeam_thinks[1].name = "G_FreeEdict";
    dabeam_thinks[1].cb = qa_q2f_free_entity_think();
    dabeam_thinks_ready = true;
  }
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = dabeam_thinks;
  definitions.think_count = 2;
  return definitions;
}
qa_q2f_entity *qa_q2mpm_monster_dabeam(qa_q2f_entity *owner, qa_q2f_game *game, const qa_actor_id *target,
                                       qa_vec3 origin, qa_vec3 angles, double damage, bool medic) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mpm_monster_dabeam_callbacks());
  qa_q2f_entity *beam = game->create(game->ctx, "dabeam", NULL);
  beam->has_owner = true;
  beam->owner = owner->actor.id;
  beam->has_enemy = target != NULL;
  if (target) beam->enemy = *target;
  beam->damage = damage;
  beam->render_flags = 128 | 32;
  beam->frame = 2;
  beam->skin = medic ? (double)0xf3f3f1f1u : (double)0xf2f2f0f0u;
  qa_body_state_result enemy;
  memset(&enemy, 0, sizeof enemy);
  if (target) enemy = game->host->bodies.read(game->host->bodies.ctx, *target);
  if (enemy.has_value) {
    qa_vec3 point = qa_add3_double(enemy.value.origin,
                                   qa_scale3_double(qa_add3_double(enemy.value.bounds.min, enemy.value.bounds.max), 0.5));
    qa_vec3 aimed = point;
    if (medic) aimed.x += sin(q2mpm_now(game)) * 8;
    beam->movedir = qa_normalize3_double_hypot(qa_sub3_double(aimed, origin));
  } else {
    beam->movedir = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
  }
  beam->spawnflags = (double)((uint32_t)beam->spawnflags | 0x80000001u);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = angles;
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-8, -8, -8);
  change.bounds.max = q2mpm_v3(8, 8, 8);
  q2mpm_move_body(game, beam, change, true, false);
  game->motion(game->ctx, beam, QA_Q2F_MOTION_STATIONARY);
  game->solid(game->ctx, beam, QA_Q2F_SOLID_NONE);
  game->schedule(game->ctx, beam, 0.1, qa_q2mpm_dabeam_hit_think());
  return beam;
}

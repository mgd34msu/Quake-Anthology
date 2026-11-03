/* Port of src/content/q2/missionpacks/monsters/dabeam.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/foundation/js.h"

#include "mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void beam_hit_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void free_beam_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_think qa_q2mpm_dabeam_hit_think(void) {
  qa_q2_think think = {NULL, beam_hit_fn};
  return think;
}
static void beam_hit_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  qa_vec3 end = qa_add3_double(origin, qa_scale3_double(entity->movedir, 2048));
  qa_vec3 start = origin;
  qa_actor_id ignore = entity->actor.id;
  qa_vec3 endpoint = end;
  for (;;) {
    qa_trace_result trace = q2mpm_trace(game, start, end, NULL, &ignore, (double)(qa_js_to_int32(1u | 0x2000000u | 0x4000000u)));
    endpoint = trace.end;
    qa_actor_id actor;
    if (!qa_q2m_trace_ground_actor(trace, game, &actor)) break;
    qa_q2_entity *target = game->entity(game->ctx, &actor);
    qa_combat_state_result state = qa_gameplay_authority_read(game->host->combat, actor);
    bool owner_is_actor = entity->has_owner && qa_same_actor(actor, entity->owner);
    if (state.has_value && state.value.can_take_damage && (!target || !target->laser_immune)
        && (qa_js_to_uint32((target ? target->flags : 0)) & 4u) == 0 && !owner_is_actor) {
      const qa_actor_id *attacker = entity->has_owner ? &entity->owner : NULL;
      (void)q2mpm_damage(game, actor, entity, attacker, entity->damage, game->options.skill, entity->movedir,
                         trace.end, q2mpm_zero(), 30, true, 4);
    }
    qa_combat_state_result updated = qa_gameplay_authority_read(game->host->combat, actor);
    if (entity->damage < 0 && game->host->is_player(game->host->ctx, actor) && updated.has_value
        && updated.value.health > 100) {
      qa_owned_actor owned; bool has_owned = qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owned);
      if (has_owned)
        qa_gameplay_authority_set_health(game->host->combat, owned, updated.value.health + entity->damage);
    }
    if (!game->host->is_monster(game->host->ctx, actor) && !game->host->is_player(game->host->ctx, actor)) {
      if ((qa_js_to_uint32(entity->spawnflags) & 0x80000000u) != 0) {
        entity->spawnflags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->spawnflags) & ~0x80000000u));
        qa_vec3 direction = trace.contact.has_plane ? trace.contact.plane.normal : q2mpm_zero();
        q2mpm_emit_effect(game, "q2:laser-sparks", trace.end, direction, 10, (double)(qa_js_to_int32(qa_js_to_uint32(entity->skin) & 255u)));
      }
      break;
    }
    ignore = actor;
    start = trace.end;
  }
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "beam"; beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = origin;
  beam.end = endpoint;
  beam.width = 2;
  beam.color = entity->skin;
  beam.visible = true;
  game->host->emit(game->host->ctx, beam);
  entity->pos2 = endpoint;
  game->schedule(game->ctx, entity, 0.1, (qa_q2_think){NULL, free_beam_fn});
}
/* TS freeBeam (dabeam_free): hides the beam drawn by the last hit, then frees the entity. */
static void free_beam_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "beam"; beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = q2mpm_body(game, entity).origin;
  beam.end = entity->pos2;
  beam.width = 2;
  beam.color = entity->skin;
  beam.visible = false;
  game->host->emit(game->host->ctx, beam);
  game->remove(game->ctx, entity);
}
static qa_q2_named_think dabeam_thinks[2];
static bool dabeam_thinks_ready;
qa_q2_callback_definitions qa_q2mpm_monster_dabeam_callbacks(void) {
  if (!dabeam_thinks_ready) {
    dabeam_thinks[0].name = "dabeam_hit";
    dabeam_thinks[0].cb = qa_q2mpm_dabeam_hit_think();
    dabeam_thinks[1].name = "dabeam_free";
    dabeam_thinks[1].cb = (qa_q2_think){NULL, free_beam_fn};
    dabeam_thinks_ready = true;
  }
  qa_q2_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = dabeam_thinks;
  definitions.think_count = 2;
  return definitions;
}
qa_q2_entity *qa_q2mpm_monster_dabeam(qa_q2_entity *owner, qa_q2_game_services *game, const qa_actor_id *target,
                                       qa_vec3 origin, qa_vec3 angles, double damage, bool medic) {
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mpm_monster_dabeam_callbacks());
  qa_q2_entity *beam = game->create(game->ctx, "dabeam", NULL, 0);
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
  if (target) enemy = qa_shared_body_table_read(game->host->bodies, *target);
  if (enemy.has_value) {
    qa_vec3 point = qa_add3_double(enemy.value.origin,
                                   qa_scale3_double(qa_add3_double(enemy.value.bounds.min, enemy.value.bounds.max), 0.5));
    qa_vec3 aimed = point;
    if (medic) aimed.x += sin(q2mpm_now(game)) * 8;
    beam->movedir = qa_normalize3_double_hypot(qa_sub3_double(aimed, origin));
  } else {
    beam->movedir = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
  }
  beam->spawnflags = (double)(qa_js_to_int32(qa_js_to_uint32(beam->spawnflags) | 0x80000001u));
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = angles;
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-8, -8, -8);
  change.bounds.max = q2mpm_v3(8, 8, 8);
  q2mpm_move_body(game, beam, change, true, false);
  game->motion(game->ctx, beam, "stationary");
  game->solid(game->ctx, beam, "none");
  game->schedule(game->ctx, beam, 0.1, qa_q2mpm_dabeam_hit_think());
  return beam;
}

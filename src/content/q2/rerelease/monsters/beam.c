/* Port of src/content/q2/rerelease/monsters/beam.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/beam.h"

#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "rrm_internal.h"

static void emit_beam(qa_q2f_entity *beam, qa_vec3 start, qa_vec3 end, bool visible) {
  qa_q2mpm_beam_event event;
  memset(&event, 0, sizeof event);
  event.actor = beam->actor.id;
  event.start = start; event.end = end;
  event.width = beam->frame; event.color = beam->skin; event.visible = visible;
  qa_q2mpm_emit_beam(event);
}

void qa_q2rm_update_monster_beam(qa_q2f_entity *beam, qa_q2f_game *game, bool damage) {
  qa_vec3 start = q2rm_body(game, beam).origin;
  qa_vec3 end = qa_add3_double(start, qa_scale3_double(beam->movedir, 2048));
  qa_actor_id exclude[16];
  size_t exclude_count = 0;
  qa_vec3 endpoint = end;
  for (;;) {
    qa_q2f_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = start; request.end = end; request.ignore = &beam->actor.id;
    request.exclude = exclude; request.exclude_count = exclude_count;
    request.mask = (double)(3 | 0x2000000 | 0x4000000 | 0x40000000);
    qa_trace_result trace = game->host->trace(game->host->ctx, request);
    qa_vec3 normal = trace.contact.has_plane ? trace.contact.plane.normal : q2rm_zero();
    endpoint = qa_add3_double(trace.end, normal);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) break;
    qa_actor_id target = trace.hit.actor;
    qa_q2f_entity *entity = game->entity(game->ctx, &target);
    qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, target);
    bool excluded = false;
    for (size_t i = 0; i < exclude_count; i++) if (qa_same_actor(exclude[i], target)) excluded = true;
    if (excluded) break;
    if (damage && beam->damage > 0 && combat.has_value && combat.value.can_take_damage
        && (!entity || !entity->laser_immune)
        && !(beam->has_owner && qa_same_actor(beam->owner, target))) {
      const qa_actor_id *attacker = beam->has_owner ? &beam->owner : NULL;
      (void)q2rm_damage(game, target, beam, attacker, beam->damage, game->options.skill,
                        beam->movedir, trace.end, q2rm_zero(), 30, true, 4);
    } else if (damage && beam->damage < 0 && combat.has_value && entity && combat.value.health < entity->max_health) {
      double healed = combat.value.health - beam->damage;
      game->host->combat.set_health(game->host->combat.ctx, entity->actor,
                                    healed < entity->max_health ? healed : entity->max_health);
    }
    if (!game->host->is_monster(game->host->ctx, target) && !game->host->is_player(game->host->ctx, target)) {
      if (damage) q2rm_emit_effect(game, "q2:laser-sparks", trace.end, normal, 10, beam->skin);
      break;
    }
    if (exclude_count == 16) break;
    exclude[exclude_count++] = target;
  }
  beam->pos2 = endpoint;
  emit_beam(beam, start, endpoint, true);
}

void qa_q2rm_free_monster_beam_fn(void *ctx, qa_q2f_entity *beam, qa_q2f_game *game) {
  (void)ctx;
  qa_q2f_entity *owner = game->entity(game->ctx, beam->has_owner ? &beam->owner : NULL);
  if (owner) {
    if (q2rm_has(beam->spawnflags, 1)) owner->has_beam2 = false;
    else owner->has_beam = false;
  }
  emit_beam(beam, q2rm_body(game, beam).origin, beam->pos2, false);
  game->remove(game->ctx, beam);
}
qa_q2f_think qa_q2rm_free_monster_beam(void) {
  qa_q2f_think think = {NULL, qa_q2rm_free_monster_beam_fn};
  return think;
}

void qa_q2rm_fire_monster_beam(qa_q2m_context *context, double damage, bool secondary, qa_q2f_think update) {
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_q2f_named_think named[1];
  named[0].name = "beam_think";
  named[0].cb = qa_q2rm_free_monster_beam();
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = named; definitions.think_count = 1;
  game->source_callbacks.register_defs(game->source_callbacks.ctx, definitions);
  const qa_actor_id *existing = secondary ? (entity->has_beam2 ? &entity->beam2 : NULL)
                                          : (entity->has_beam ? &entity->beam : NULL);
  qa_q2f_entity *beam = game->entity(game->ctx, existing);
  if (!beam) {
    beam = game->create(game->ctx, "dabeam", NULL);
    if (secondary) { entity->beam2 = beam->actor.id; entity->has_beam2 = true; }
    else { entity->beam = beam->actor.id; entity->has_beam = true; }
    beam->owner = entity->actor.id; beam->has_owner = true;
    beam->damage = damage; beam->frame = 2; beam->spawnflags = secondary ? 1 : 0;
    beam->skin = (double)(uint32_t)(context->state->medic ? 0xf3f3f1f1u : 0xf2f2f0f0u);
    beam->render_flags = q2rm_or(beam->render_flags, 128);
    beam->postthink = update;
    beam->sound_text = "misc/lasfly.wav";
    game->motion(game->ctx, beam, QA_Q2F_MOTION_STATIONARY);
    game->solid(game->ctx, beam, QA_Q2F_SOLID_NONE);
    q2rm_emit_loop(game, beam, beam->sound_text, 0, "start");
  }
  game->schedule(game->ctx, beam, 0.2, qa_q2rm_free_monster_beam());
  update.fn(update.ctx, beam, game);
  qa_q2rm_update_monster_beam(beam, game, true);
}

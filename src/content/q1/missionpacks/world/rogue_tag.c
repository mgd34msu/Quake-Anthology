/* Port of src/content/q1/missionpacks/world/rogue-tag.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_tag.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"

static void announce(qa_q1mp_rogue_tag *tag, const char *text, qa_actor_id actor) {
  qa_q1_services *game = tag->game;
  qa_owned_actor owner;
  /* The token holder may have left. */
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner)) return;
  if (tag->hooks == NULL || tag->hooks->player_name == NULL) qa_range_error("Rogue token messages require shared player names");
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_q1_msg_arg arg = {.is_number = false, .text = tag->hooks->player_name(tag->hooks->ctx, actor)};
    qa_q1_event event = {0};
    event.kind = "message"; event.has_player = true; event.player = players[i]; event.text = text;
    event.args = &arg; event.arg_count = 1; event.center = false;
    qa_q1_emit(game, event);
  }
}
static qa_q1_actor *token_of(qa_q1_services *game) {
  for (size_t i = 0; i < game->entity_count; i++)
    if (qa_q1mp_streq(game->entities[i]->classname, "dmatch_tag_token")) return game->entities[i];
  return NULL;
}
static bool drop_floor(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_trace_request request = {0};
  request.start = body.origin; request.end = qa_q1_vadd(body.origin, QA_Q1MP_V3(0, 0, -256));
  request.bounds = body.bounds; request.has_ignore = true; request.ignore = entity->actor.id; request.monsters = true;
  const qa_q1_trace hit = game->host.trace(game->host.ctx, request);
  if (hit.fraction == 1 || hit.all_solid) return false;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = hit.end;
  patch.has_ground = true; patch.ground = hit.has_actor ? &hit.actor : NULL;
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  return true;
}
static void set_owner_reference(qa_q1_services *game, const qa_actor_id *actor) {
  if (game->world != NULL && !qa_q1_actor_reference_set(game->world, "rogue:tag_token_owner", actor)) qa_range_error("Out of memory");
}
static void take(qa_q1mp_rogue_tag *tag, qa_q1_actor *token, qa_actor_id actor, double announcement_delay) {
  qa_q1_services *game = tag->game;
  set_owner_reference(game, &actor);
  token->has_owner = true; token->owner = actor;
  qa_q1mp_world_number(token, "tag_frags", 0);
  qa_q1mp_world_number(token, "tag_message_time", game->time + announcement_delay);
  token->solid = "none"; token->touch = (qa_q1_bound_touch){0};
  qa_q1mp_world_later(game, token, 0.1, "rogue:tag_think");
}
static void respawn(qa_q1mp_rogue_tag *tag) {
  qa_q1_actor *token = token_of(tag->game);
  if (token == NULL) return;
  qa_q1_services *game = tag->game;
  qa_q1_actor *point = qa_q1_spawn_selector_select(qa_q1_base_spawn_selector(qa_q1_base_for(game)), true);
  if (point == NULL) qa_range_error("Tag token has no respawn point");
  qa_q1_set_origin(game, token, qa_q1_get_body(game, point).origin);
  set_owner_reference(game, NULL);
  token->solid = "trigger";
  token->touch = qa_q1_callback_registry_touch(game->named, token, "rogue:tag_touch");
  token->think = qa_q1_callback_registry_action(game->named, token, "SUB_Null");
  token->has_owner = false;
  qa_q1mp_world_number(token, "tag_frags", 0);
  (void)drop_floor(game, token);
}
static void tag_respawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)game; (void)entity; respawn(ctx); }
static void tag_fall(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_number(entity, "tag_frags", 0);
  (void)drop_floor(game, entity);
  qa_q1mp_world_later(game, entity, 30, "rogue:tag_respawn");
}
static void tag_place(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->movement = "toss"; entity->solid = "trigger";
  qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_get_body(game, entity).origin, QA_Q1MP_V3(0, 0, 6)));
  if (!drop_floor(game, entity)) qa_q1_remove(game, entity);
}
static void tag_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1mp_rogue_tag *tag = ctx;
  if (entity->has_owner && qa_q1_health(game, entity->owner) > 0) {
    if (qa_q1_actor_number(entity, "tag_message_time") < game->time) {
      announce(tag, "$qc_has_token", entity->owner);
      qa_q1mp_world_number(entity, "tag_message_time", game->time + 30);
    }
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, entity->owner);
    qa_q1_set_origin(game, entity, qa_q1_vadd(body.has_value ? body.value.origin : QA_Q1_ZERO, QA_Q1MP_V3(0, 0, 48)));
    qa_q1mp_world_later(game, entity, 0.1, "rogue:tag_think");
    return;
  }
  if (entity->has_owner) announce(tag, "$qc_lost_token", entity->owner);
  qa_q1mp_world_number(entity, "tag_frags", 0);
  entity->solid = "trigger"; entity->has_owner = false;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:tag_touch");
  qa_q1mp_world_later(game, entity, 0.1, "rogue:tag_fall");
}
static void tag_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                      const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1mp_rogue_tag *tag = ctx;
  if (!qa_q1_is_player(game, &other)) return;
  take(tag, entity, other, 30);
  qa_q1_sound(game, entity->actor.id, "runes/end1.wav", "voice", 1, 1);
  announce(tag, "$qc_got_token", other);
}
static void token_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (!(qa_q1_option_teamplay(game) == 3)) { qa_q1_remove(game, entity); return; }
  qa_q1mp_set_string(&entity->model, "progs/sphere.mdl"); entity->skin = 1;
  entity->effects = qa_js_bit_or(entity->effects, 8);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:tag_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -16}, {16, 16, 16}});
  qa_q1mp_world_later(game, entity, 0.2, "rogue:tag_place");
}
void qa_q1mp_rogue_tag_init(qa_q1mp_rogue_tag *tag, qa_q1_services *game, const qa_q1mp_world_hooks *hooks) {
  tag->game = game; tag->hooks = hooks;
  qa_q1_callback_registry_register(game->named, "rogue:tag_respawn", (qa_q1_callback_handlers){.ctx = tag, .action = tag_respawn});
  qa_q1_callback_registry_register(game->named, "rogue:tag_fall", (qa_q1_callback_handlers){.ctx = tag, .action = tag_fall});
  qa_q1_callback_registry_register(game->named, "rogue:tag_place", (qa_q1_callback_handlers){.ctx = tag, .action = tag_place});
  qa_q1_callback_registry_register(game->named, "rogue:tag_think", (qa_q1_callback_handlers){.ctx = tag, .action = tag_think});
  qa_q1_callback_registry_register(game->named, "rogue:tag_touch", (qa_q1_callback_handlers){.ctx = tag, .touch = tag_touch});
  qa_q1_register_spawn(game, "dmatch_tag_token", token_spawn, tag);
}
double qa_q1mp_tag_score(qa_q1mp_rogue_tag *tag, qa_actor_id victim, qa_actor_id attacker) {
  qa_q1_actor *token = token_of(tag->game);
  if (token == NULL) return 1;
  qa_q1_services *game = tag->game;
  qa_actor_id owner;
  const bool has_owner = game->world != NULL && qa_q1_actor_reference_get(game->world, "rogue:tag_token_owner", &owner);
  if (has_owner && qa_same_actor(attacker, owner)) {
    qa_q1mp_world_number(token, "tag_frags", qa_q1_actor_number(token, "tag_frags") + 1);
    if (qa_q1_actor_number(token, "tag_frags") == 5) {
      qa_q1_player_state *player = qa_q1_player(game, attacker);
      if (player != NULL) {
        qa_q1_message(game, &attacker, "$qc_got_quad", false, NULL, 0);
        qa_q1_give_powerup(game, player, "quad", 30);
        qa_q1_sound(game, player->actor.id, "items/damage.wav", "item", 1, 1);
        const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, attacker);
        if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &attacker, 1);
      }
    } else if (qa_q1_actor_number(token, "tag_frags") == 10) {
      announce(tag, "$qc_lost_token", attacker);
      respawn(tag);
    }
    return 3;
  }
  if (has_owner && qa_same_actor(victim, owner)) {
    qa_owned_actor source;
    if (qa_session_actor_registry_resolve_owned(game->host.actors, victim, &source)) qa_q1_sound(game, source.id, "runes/end1.wav", "voice", 1, 1);
    if (qa_q1_is_player(game, &attacker)) take(tag, token, attacker, 0.5);
    return 5;
  }
  return 1;
}

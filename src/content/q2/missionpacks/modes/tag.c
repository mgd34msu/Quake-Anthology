/* Port of src/content/q2/missionpacks/modes/tag.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/modes/tag.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <string.h>

#include "../missionpacks_internal.h"

static void bonus(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game) {
  double health = q2mp_health(game, entity->actor.id);
  double maximum = entity->max_health != 0 ? entity->max_health : 100;
  if (health < maximum)
    qa_gameplay_authority_set_health(game->host->combat, entity->actor, health + 200 < maximum ? health + 200 : maximum);
  qa_q2_entity *armor = game->create(game->ctx, "item_armor_body", NULL, 0);
  armor->spawnflags = (double)(qa_js_to_int32(q2mp_u32(armor->spawnflags) | 0x10000u));
  /* TS keeps reading armor.actor.id after the touch removed the entity; the
   * C entity record is freed then, so keep the id. */
  qa_actor_id armor_id = armor->actor.id;
  (void)qa_q2_item_module_spawn(tag->hooks.items, armor, game);
  if (q2mp_live(game, armor_id)) qa_q2_item_module_touch(tag->hooks.items, armor, game, entity->actor.id);
  if (q2mp_live(game, armor_id)) game->remove(game->ctx, armor);
}
static bool token_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  qa_q2mp_tag *tag = ctx;
  tag->token = entity->actor.id; tag->has_token = true;
  tag->owner = player.id; tag->has_owner = true;
  tag->count = 0;
  (void)qa_shared_inventory_table_give(game->host->inventory, player, "q2:dm_tag_token", 1);
  qa_q2_entity *owner = q2mp_ent(game, true, player.id);
  if (owner != NULL) bonus(tag, owner, game);
  return true;
}

static void touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact);
static void respawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void make_touchable_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2mp_tag_init(qa_q2mp_tag *tag, qa_q2mp_tag_hooks hooks) {
  memset(tag, 0, sizeof *tag);
  tag->hooks = hooks;
  tag->respawn = (qa_q2_think){tag, respawn_fn};
  tag->make_touchable = (qa_q2_think){tag, make_touchable_fn};
  tag->touch = (qa_q2_touch){tag, touch_fn};
  tag->cb_think[0] = (qa_q2_named_think){"Tag_Respawn", tag->respawn};
  tag->cb_think[1] = (qa_q2_named_think){"Tag_MakeTouchable", tag->make_touchable};
  tag->cb_touch[0] = (qa_q2_named_touch){"Tag_TouchItem", tag->touch};
  qa_q2_item_definition d;
  memset(&d, 0, sizeof d);
  d.kind = "custom"; d.console_give = "forbidden"; d.classname = "dm_tag_token"; d.name = "Tag Token";
  d.icon = "i_tagtoken"; d.model = "models/items/tagtoken/tris.md2"; d.sound = "items/pkup.wav";
  d.rotate = true; d.respawn = 0; d.capacity = 32767; d.quantity = 1;
  d.has_coop_stay = true; d.coop_stay = false; d.droppable = false;
  d.custom_ctx = tag; d.pickup = token_pickup; d.has_use = false;
  qa_q2_item_module_register(hooks.items, d);
}

qa_q2mp_tag_checkpoint qa_q2mp_tag_capture(qa_q2mp_tag *tag) {
  qa_q2mp_tag_checkpoint cp;
  memset(&cp, 0, sizeof cp);
  cp.has_token = qa_q2_save_actor(tag->has_token ? &tag->token : NULL, &cp.token);
  cp.has_owner = qa_q2_save_actor(tag->has_owner ? &tag->owner : NULL, &cp.owner);
  cp.count = tag->count;
  return cp;
}
static bool restore_actor(qa_q2_game_services *game, bool has, qa_saved_actor_id saved, qa_actor_id *out) {
  if (!has) return false;
  qa_owned_actor resolved; bool has_resolved = qa_session_actor_registry_resolve_saved(game->host->actors, saved, &resolved);
  *out = has_resolved ? resolved.id : qa_session_actor_registry_reference_saved(game->host->actors, saved, NULL);
  return true;
}
void qa_q2mp_tag_restore(qa_q2mp_tag *tag, qa_q2_game_services *game, qa_q2mp_tag_checkpoint saved) {
  tag->has_token = restore_actor(game, saved.has_token, saved.token, &tag->token);
  tag->has_owner = restore_actor(game, saved.has_owner, saved.owner, &tag->owner);
  tag->count = saved.count;
}
bool qa_q2mp_tag_owner_actor(qa_q2mp_tag *tag, qa_actor_id *out) {
  if (!tag->has_owner) return false;
  *out = tag->owner;
  return true;
}
double qa_q2mp_tag_effects(qa_q2mp_tag *tag, qa_actor_id actor) {
  return tag->has_owner && qa_same_actor(actor, tag->owner) ? (double)0x20000000 : 0;
}
const char *qa_q2mp_tag_dog_tag(qa_q2mp_tag *tag, qa_actor_id actor) {
  return tag->has_owner && qa_same_actor(actor, tag->owner) ? "tag3" : NULL;
}
double qa_q2mp_tag_change_damage(qa_q2mp_tag *tag, qa_actor_id target, bool has_attacker, qa_actor_id attacker,
                                 double damage) {
  bool target_is_owner = tag->has_owner && qa_same_actor(target, tag->owner);
  bool attacker_is_owner = has_attacker && tag->has_owner && qa_same_actor(attacker, tag->owner);
  return !target_is_owner && !attacker_is_owner ? trunc(damage * 3 / 4) : damage;
}
void qa_q2mp_tag_player_death(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (tag->has_token && tag->has_owner && qa_same_actor(entity->actor.id, tag->owner)) qa_q2mp_tag_drop(tag, entity, game);
}
void qa_q2mp_tag_disconnect(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_tag_player_death(tag, entity, game);
}
void qa_q2mp_tag_score(qa_q2mp_tag *tag, qa_q2_entity *attacker, qa_q2_entity *victim, qa_q2_game_services *game,
                       double change, double means_of_death) {
  if (tag->has_token && tag->has_owner) {
    if (change > 0 && qa_same_actor(attacker->actor.id, tag->owner)) {
      change = 3;
      if (++tag->count == 5) {
        (void)qa_shared_inventory_table_give(game->host->inventory, attacker->actor, "q2:item_quad", 1);
        (void)qa_q2_item_module_use(tag->hooks.items, attacker->actor, "q2:item_quad", game, 30);
        tag->count = 0;
      }
    } else if (qa_same_actor(victim->actor.id, tag->owner) && !qa_same_actor(attacker->actor.id, tag->owner)) {
      change = 5;
      double mod = (double)(qa_js_to_int32(q2mp_u32(means_of_death) & ~0x8000000u));
      if (mod == 49 || mod == 53 || mod == 54 || mod == 55 || q2mp_health(game, attacker->actor.id) <= 0)
        qa_q2mp_tag_drop(tag, victim, game);
      else {
        bonus(tag, attacker, game);
        tag->owner = attacker->actor.id; tag->has_owner = true;
        tag->count = 0;
      }
    }
  }
  tag->hooks.add_score(tag->hooks.ctx, attacker->actor.id, change);
}

static void touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2mp_tag *tag = ctx;
  qa_q2_item_module_touch(tag->hooks.items, entity, game, contact.other);
}
static void respawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_tag *tag = ctx;
  qa_q2_entity *spot = tag->hooks.farthest_spawn(tag->hooks.ctx, game);
  if (spot == NULL) {
    game->schedule(game->ctx, entity, 1, tag->respawn);
    return;
  }
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = q2mp_body(game, spot).origin;
  q2mp_move(game, entity, c, false, false);
}
static void make_touchable_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_tag *tag = ctx;
  entity->touch = tag->touch;
  qa_q2_entity *token = q2mp_ent(game, tag->has_token, tag->token);
  if (token == NULL) return;
  bool wet = ((q2mp_u32(game->host->point_contents(game->host->ctx, q2mp_body(game, entity).origin)) & 24u) != 0);
  game->schedule(game->ctx, token, wet ? 3 : 30, tag->respawn);
}
qa_q2_callback_definitions qa_q2mp_tag_callbacks(qa_q2mp_tag *tag) {
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = tag->cb_think; defs.think_count = 2;
  defs.touch = tag->cb_touch; defs.touch_count = 1;
  return defs;
}

void qa_q2mp_tag_drop(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game) {
  tag->count = 0;
  tag->has_owner = false;
  qa_q2_entity *token = game->create(game->ctx, "dm_tag_token", NULL, 0);
  tag->token = token->actor.id; tag->has_token = true;
  token->spawnflags = 0x10000;
  (void)qa_q2_item_module_spawn(tag->hooks.items, token, game);
  token->effects = 1 | 0x20000000;
  token->render_flags = 512;
  token->owner = entity->actor.id; token->has_owner = true;
  token->touch = (qa_q2_touch){NULL, NULL};
  qa_bounds bounds = {q2mp_v3(-15, -15, -15), q2mp_v3(15, 15, 15)};
  qa_body_state body = q2mp_body(game, entity);
  qa_q2_player_view_result view = game->host->player_view_state(game->host->ctx, entity->actor.id);
  qa_vec3 forward = q2mp_axes(view.has_value ? view.value.view_angles : body.angles).forward;
  qa_vec3 end = q2mp_add(q2mp_add(body.origin, q2mp_scale(forward, 24)), q2mp_v3(0, 0, -16));
  qa_vec3 origin = q2mp_trace(game, body.origin, end, &bounds, &entity->actor.id, 1).end;
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = origin;
  c.has_bounds = true; c.bounds = bounds;
  qa_vec3 velocity = q2mp_scale(forward, 100);
  velocity.z = 300;
  c.has_velocity = true; c.velocity = velocity;
  q2mp_move(game, token, c, false, false);
  game->solid(game->ctx, token, "trigger");
  game->motion(game->ctx, token, "toss");
  game->show(game->ctx, token);
  game->schedule(game->ctx, token, 1, tag->make_touchable);
  (void)qa_shared_inventory_table_consume(game->host->inventory, entity->actor, "q2:dm_tag_token", 1);
}
void qa_q2mp_tag_post_spawn(qa_q2mp_tag *tag, qa_q2_game_services *game) {
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++)
    if (strcmp(qa_q2_game_entity_at(game, i)->classname, "dm_tag_token") == 0) return;
  qa_q2_entity *token = game->create(game->ctx, "dm_tag_token", NULL, 0);
  qa_q2mp_spawn_point spot = tag->hooks.select_spawn(tag->hooks.ctx, token, game);
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = spot.origin;
  c.has_angles = true; c.angles = spot.angles;
  c.has_velocity = true; c.velocity = q2mp_zero();
  q2mp_move(game, token, c, false, false);
  (void)qa_q2mp_tag_spawn(tag, token, game);
}
bool qa_q2mp_tag_spawn(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (strcmp(entity->classname, "dm_tag_token") != 0) return false;
  if (!q2mp_dm(game)) {
    game->remove(game->ctx, entity);
    return true;
  }
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_tag_callbacks(tag));
  tag->token = entity->actor.id; tag->has_token = true;
  tag->count = 0;
  entity->model = "models/items/tagtoken/tris.md2";
  entity->count = 1;
  (void)qa_q2_item_module_spawn(tag->hooks.items, entity, game);
  entity->effects = (double)(qa_js_to_int32(q2mp_u32(entity->effects) | 0x20000000u));
  game->show(game->ctx, entity);
  return true;
}

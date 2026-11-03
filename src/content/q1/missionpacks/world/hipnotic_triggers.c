/* Port of src/content/q1/missionpacks/world/hipnotic-triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_triggers.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||` on a number */
static bool flag(double spawnflags, double bit) { return qa_js_bit_and(spawnflags, bit) != 0; }
static const qa_actor_id *activator_of(const qa_q1_actor *entity) { return entity->has_activator ? &entity->activator : NULL; }

static void counter_off(qa_q1_services *game, qa_q1_actor *entity) {
  if (qa_q1_actor_number(entity, "cnt") != 0 && flag(entity->spawnflags, 32)) { qa_q1mp_world_number(entity, "aflag", 1); return; }
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:counter_start");
  qa_q1mp_world_number(entity, "aflag", 0);
  qa_q1_cancel(game, entity);
}
static void counter_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const double count = qa_q1_actor_number(entity, "cnt") + 1;
  qa_q1mp_world_number(entity, "cnt", count);
  qa_q1mp_world_number(entity, "counter_state", flag(entity->spawnflags, 16) ? floor(game->host.random(game->host.ctx) * entity->count) + 1 : count);
  qa_q1_use_targets(game, entity, activator_of(entity));
  qa_q1mp_world_later(game, entity, entity->wait, "hip:counter_tick");
  if (flag(entity->spawnflags, 4)) counter_off(game, entity);
  if (count >= entity->count) {
    qa_q1mp_world_number(entity, "cnt", 0);
    if (qa_q1_actor_number(entity, "aflag") != 0 || !flag(entity->spawnflags, 2)) {
      if (flag(entity->spawnflags, 1)) counter_off(game, entity); else qa_q1_remove(game, entity);
    }
  }
}
static void counter_start(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
  qa_q1mp_world_number(entity, "aflag", 0);
  entity->use = flag(entity->spawnflags, 1) ? qa_q1_callback_registry_use(game->named, entity, "hip:counter_stop") : (qa_q1_bound_use){0};
  if (flag(entity->spawnflags, 8)) { qa_q1mp_world_number(entity, "cnt", 0); qa_q1mp_world_number(entity, "counter_state", 0); }
  if (entity->delay != 0) qa_q1mp_world_later(game, entity, entity->delay, "hip:counter_tick");
  else counter_tick(NULL, game, entity);
}
static void counter_start_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; counter_start(game, entity, activator);
}
static void counter_start_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; counter_start(game, entity, NULL); }
static void counter_stop(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; counter_off(game, entity);
}
static void on_count(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)activator;
  qa_q1_actor *counter = qa_q1_get_entity(game, other);
  const double count = counter != NULL && qa_q1mp_streq(counter->classname, "func_counter") ? qa_q1_actor_number(counter, "counter_state") : 0;
  if (count == entity->count) qa_q1_use_targets(game, entity, other);
}
static void counter_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (falsy(e->wait)) e->wait = 1;
  e->count = floor(e->count);
  if (e->count <= 0) e->count = 10;
  qa_q1mp_world_number(e, "cnt", 0); qa_q1mp_world_number(e, "counter_state", 0);
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:counter_start");
  if (flag(e->spawnflags, 64)) qa_q1mp_world_later(game, e, 0.1, "hip:counter_start");
}
static void oncount_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->count = floor(e->count);
  if (e->count <= 0) e->count = 1;
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:oncount");
}
static void use_key(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  if (activator == NULL || !qa_q1_is_player(game, activator) || entity->attack_finished > game->time) return;
  entity->attack_finished = game->time + 2;
  const bool gold = flag(entity->spawnflags, 1);
  const char *key = gold ? "q1:key/gold" : "q1:key/silver";
  qa_owned_actor owner;
  const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, *activator, &owner);
  const char *sound = game->world_type == 2 ? "base" : game->world_type == 1 ? "rune" : "med";
  char path[64];
  if (!has_owner || !qa_shared_inventory_table_consume(game->host.inventory, owner, key, 1)) {
    const char *message = entity->message[0] != '\0' ? entity->message
        : game->world_type == 2 ? "$qc_need_gold_keycard" : game->world_type == 1 ? "$qc_need_gold_runekey" : "$qc_need_gold_key";
    qa_q1_message(game, activator, message, true, NULL, 0);
    (void)snprintf(path, sizeof path, "doors/%stry.wav", sound);
    qa_q1_sound(game, entity->actor.id, path, "voice", 1, 1);
    return;
  }
  entity->touch = (qa_q1_bound_touch){0}; entity->use = (qa_q1_bound_use){0};
  qa_q1mp_set_string(&entity->message, "");
  qa_q1mp_world_later(game, entity, 0.1, "SUB_Remove");
  (void)snprintf(path, sizeof path, "doors/%suse.wav", sound);
  qa_q1_sound(game, entity->actor.id, path, "voice", 1, 1);
  qa_q1_use_targets(game, entity, activator);
}
static void key_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; use_key(game, entity, activator);
}
static void key_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                      const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; use_key(game, entity, &other);
}
static void usekey_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_trigger(game, e);
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:key");
  e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:key");
}
static void remove_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if ((qa_q1_is_player(game, &other) && !flag(e->spawnflags, 2)) ||
      (qa_q1mp_starts(game->host.classname(game->host.ctx, other), "monster_") && !flag(e->spawnflags, 1))) return;
  qa_q1_actor *victim = qa_q1_get_entity(game, &other);
  if (victim != NULL) { victim->touch = (qa_q1_bound_touch){0}; qa_q1mp_set_string(&victim->model, ""); }
  /* The shipped QC removes self here, after hiding other. */
  qa_q1_remove(game, e);
}
static void remove_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_trigger(game, e);
  e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:remove_touch");
}
static void set_gravity_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                              const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (qa_q1_is_player(game, &other))
    qa_q1_set_gravity(game, other, qa_q1_actor_number(e, "gravity") == -1 ? 1 : qa_q1_actor_number(e, "gravity"));
}
static void setgravity_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_trigger(game, e);
  qa_q1mp_world_number(e, "gravity", qa_q1_actor_number(e, "gravity") == 0 ? -1 : (qa_q1_actor_number(e, "gravity") - 1) / 100);
  e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:set_gravity");
}
/* Shipped trigger_command binds oncount_use; preserve the actual binding. */
static void command_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx; e->use = qa_q1_callback_registry_use(game->named, e, "hip:oncount");
}
static void decoy_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1mp_streq(game->host.classname(game->host.ctx, other), "monster_decoy")) return;
  e->touch = (qa_q1_bound_touch){0};
  qa_q1mp_world_later(game, e, 0.1, "SUB_Remove");
  qa_q1_use_targets(game, e, &other);
}
static void decoy_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, e); return; }
  qa_q1mp_world_trigger(game, e);
  e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:decoy_trigger");
}
static void waterfall_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                            const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  qa_owned_actor owner;
  const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner);
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  if (!has_owner || !body.has_value) return;
  const qa_vec3 velocity = qa_q1_vadd(body.value.velocity, e->movedir);
  qa_vec3 next = velocity;
  next.x = qa_q1mp_fround(velocity.x + e->count * (game->host.random(game->host.ctx) - 0.5));
  next.y = qa_q1mp_fround(velocity.y + e->count * (game->host.random(game->host.ctx) - 0.5));
  body.value.velocity = next;
  qa_shared_body_table_write(game->host.bodies, owner, body.value);
}
static void waterfall_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_trigger(game, e);
  if (falsy(e->count)) e->count = 100;
  const double speed = falsy(e->speed) ? 50 : e->speed;
  e->movedir = QA_Q1MP_V3(qa_q1mp_fround(e->movedir.x * speed), qa_q1mp_fround(e->movedir.y * speed), qa_q1mp_fround(e->movedir.z * speed));
  e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:waterfall");
}
static void threshold_pain(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_gameplay_authority_set_health(game->host.combat, e->actor, e->max_health);
}
static void threshold_die(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *attacker) {
  (void)ctx;
  qa_gameplay_authority_set_health(game->host.combat, e->actor, e->max_health);
  qa_q1_actor_set_damageable(e, false);
  qa_q1_use_targets(game, e, attacker);
  qa_q1_actor_set_damageable(e, true);
  if (!flag(e->spawnflags, 1)) qa_q1_remove(game, e);
}
static void threshold_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_brush(game, e);
  if (flag(e->spawnflags, 2)) qa_q1mp_set_string(&e->model, "");
  if (falsy(e->max_health)) e->max_health = 60;
  qa_gameplay_authority_set_health(game->host.combat, e->actor, e->max_health);
  qa_q1_actor_set_damageable(e, true);
  e->pain = qa_q1_callback_registry_pain(game->named, e, "hip:threshold_pain");
  e->die = qa_q1_callback_registry_die(game->named, e, "hip:threshold_die");
}
static void breakaway_use(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; qa_q1_remove(game, e);
}
static void breakaway_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_brush(game, e);
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:breakaway");
  qa_q1_body_patch patch = {0}; patch.has_angles = true; patch.angles = QA_Q1_ZERO; qa_q1_set_body(game, e, &patch);
}
void qa_q1mp_register_hipnotic_triggers(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, "hip:counter_tick", (qa_q1_callback_handlers){.action = counter_tick});
  qa_q1_callback_registry_register(named, "hip:counter_start", (qa_q1_callback_handlers){.use = counter_start_use, .action = counter_start_action});
  qa_q1_callback_registry_register(named, "hip:counter_stop", (qa_q1_callback_handlers){.use = counter_stop});
  qa_q1_callback_registry_register(named, "hip:oncount", (qa_q1_callback_handlers){.use = on_count});
  qa_q1_register_spawn(game, "func_counter", counter_spawn, NULL);
  qa_q1_register_spawn(game, "func_oncount", oncount_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:key", (qa_q1_callback_handlers){.use = key_use, .touch = key_touch});
  qa_q1_register_spawn(game, "trigger_usekey", usekey_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:remove_touch", (qa_q1_callback_handlers){.touch = remove_touch});
  qa_q1_register_spawn(game, "trigger_remove", remove_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:set_gravity", (qa_q1_callback_handlers){.touch = set_gravity_touch});
  qa_q1_register_spawn(game, "trigger_setgravity", setgravity_spawn, NULL);
  qa_q1_register_spawn(game, "trigger_command", command_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:decoy_trigger", (qa_q1_callback_handlers){.touch = decoy_touch});
  qa_q1_register_spawn(game, "trigger_decoy_use", decoy_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:waterfall", (qa_q1_callback_handlers){.touch = waterfall_touch});
  qa_q1_register_spawn(game, "trigger_waterfall", waterfall_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:threshold_pain", (qa_q1_callback_handlers){.pain = threshold_pain});
  qa_q1_callback_registry_register(named, "hip:threshold_die", (qa_q1_callback_handlers){.die = threshold_die});
  qa_q1_register_spawn(game, "trigger_damagethreshold", threshold_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:breakaway", (qa_q1_callback_handlers){.use = breakaway_use});
  qa_q1_register_spawn(game, "func_breakawaywall", breakaway_spawn, NULL);
}

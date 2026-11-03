/* Port of src/content/q1/missionpacks/world/shooters.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/shooters.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/monsters/dragon.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/world/common.h"

/* The TS closures capture the pack; ctx is the pack's static record. */
typedef struct shooter_pack { const char *pack; } shooter_pack;
static shooter_pack HIPNOTIC = {"hipnotic"}, ROGUE = {"rogue"};
static bool is_rogue(void *ctx) { return qa_q1mp_streq(((const shooter_pack *)ctx)->pack, "rogue"); }
static bool flag(double spawnflags, double bit) { return qa_js_bit_and(spawnflags, bit) != 0; }
static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||=` on a number */

static void laser_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->has_owner && qa_same_actor(other, entity->owner)) return;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  if (qa_q1mp_streq(game->host.contents(game->host.ctx, body.origin), "sky")) { qa_q1_remove(game, entity); return; }
  if (!flag(entity->spawnflags, 16)) qa_q1_sound(game, entity->actor.id, "enforcer/enfstop.wav", "weapon", 3, 1);
  const qa_vec3 origin = qa_q1_vsub(body.origin, qa_q1_vscale(qa_q1_normalize(body.velocity), 8));
  if (qa_q1_health(game, other) != 0) {
    qa_q1_effect(game, "blood", origin, &other, 15);
    (void)qa_q1_damage(game, other, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 15, NULL, "direct", "", NULL);
  } else qa_q1_effect(game, "gunshot", origin, NULL, 1);
  qa_q1_remove(game, entity);
}
static void fire(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  const qa_actor_id self = entity->actor.id;
  if (is_rogue(ctx)) {
    if (flag(entity->spawnflags, 1) || entity->spawnflags == 0) {
      qa_q1_sound(game, self, "weapons/spike2.wav", "voice", 1, 1);
      (void)qa_q1_launch_spike(game, &self, origin, qa_q1_vscale(entity->movedir, 500), flag(entity->spawnflags, 1) ? "superspike" : "spike");
    } else if (flag(entity->spawnflags, 2)) {
      qa_q1_sound(game, self, "enforcer/enfire.wav", "voice", 1, 1);
      (void)qa_q1_launch_laser(game, &self, origin, entity->movedir);
    } else if (flag(entity->spawnflags, 32)) (void)qa_q1mpm_launch_dragon_fireball(game, self, origin, entity->movedir);
    else {
      qa_q1_sound(game, self, "weapons/spike2.wav", "voice", 1, 1);
      qa_q1_actor *spike = qa_q1mp_launch_rogue_lava_spike(game, self, origin, entity->movedir,
          flag(entity->spawnflags, 8) || (flag(entity->spawnflags, 16) && qa_q1_option_skill(game) > 1));
      qa_q1_body_patch patch = {0}; patch.has_velocity = true; patch.velocity = qa_q1_vscale(entity->movedir, 500);
      qa_q1_set_body(game, spike, &patch);
    }
    return;
  }
  const bool audible = !flag(entity->spawnflags, 16);
  if (flag(entity->spawnflags, 2)) {
    if (audible) qa_q1_sound(game, self, "enforcer/enfire.wav", "voice", 1, 1);
    qa_q1_actor *laser = qa_q1_launch_laser(game, &self, origin, entity->movedir);
    laser->movement = "fly"; laser->spawnflags = entity->spawnflags;
    laser->touch = qa_q1_callback_registry_touch(game->named, laser, "hip:shooter_laser_touch");
  } else if (flag(entity->spawnflags, 4)) {
    if (audible) qa_q1_sound(game, self, "misc/spike.wav", "voice", 1, 1);
    qa_q1_actor *ball = qa_q1_create_missile(game, &self, "lavaball", "lavarock", origin, qa_q1_vscale(entity->movedir, 300));
    ball->projectile = "rocket"; ball->touch = qa_q1_callback_registry_touch(game->named, ball, "projectile_touch");
    ball->angular_velocity = QA_Q1MP_V3(0, 0, 400);
    qa_q1_set_bounds(game, ball, (qa_bounds){{-4, -4, -4}, {4, 4, 4}});
  } else if (flag(entity->spawnflags, 8)) {
    if (audible) qa_q1_sound(game, self, "weapons/sgun1.wav", "voice", 1, 1);
    qa_q1_sound(game, self, "weapons/sgun1.wav", "weapon", 1, 1);
    qa_q1_actor *rocket = qa_q1_create_missile(game, &self, "missile", "missile", qa_q1_vadd(origin, qa_q1_vscale(entity->movedir, 8)),
                                               qa_q1_vscale(entity->movedir, 1000));
    rocket->projectile = "rocket"; rocket->touch = qa_q1_callback_registry_touch(game->named, rocket, "projectile_touch");
  } else {
    if (audible) qa_q1_sound(game, self, "weapons/spike2.wav", "voice", 1, 1);
    (void)qa_q1_launch_spike(game, &self, origin, qa_q1_vscale(entity->movedir, 500), flag(entity->spawnflags, 1) ? "superspike" : "spike");
  }
}
static void fire_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator; fire(ctx, game, entity);
}
static void shooter_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  if (is_rogue(ctx) || qa_q1_actor_number(entity, "shooter_state") != 0) fire(ctx, game, entity);
  qa_q1mp_world_later(game, entity, entity->wait, "mission:shooter_think");
}
static void shooter_switch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  qa_q1mp_world_number(entity, "shooter_state", 1 - qa_q1_actor_number(entity, "shooter_state"));
}
static void shooter_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->movedir = qa_q1_move_direction(qa_q1_get_body(game, entity).angles, game);
  qa_q1_body_patch patch = {0}; patch.has_angles = true; patch.angles = QA_Q1_ZERO; qa_q1_set_body(game, entity, &patch);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "mission:shooter_fire");
  if (qa_q1mp_streq(entity->classname, "trap_spikeshooter")) return;
  if (falsy(entity->wait)) entity->wait = 1;
  qa_q1mp_world_number(entity, "shooter_state", qa_q1mp_streq(entity->classname, "trap_shooter") ? 1 : qa_q1_actor_number(entity, "state"));
  if (qa_q1mp_streq(entity->classname, "trap_switched_shooter")) entity->use = qa_q1_callback_registry_use(game->named, entity, "mission:shooter_switch");
  double left = qa_q1_actor_number(entity, "nextthink") + entity->wait;
  double right = qa_q1_actor_number(entity, "ltime");
  qa_q1mp_world_later(game, entity, left + right - game->time,
                      "mission:shooter_think");
}
static void push_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  entity->spawnflags = qa_js_bit_xor(entity->spawnflags, 4);
}
static void push_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!flag(entity->spawnflags, 4)) return;
  const char *classname = game->host.classname(game->host.ctx, other);
  const bool grenade = qa_q1mp_streq(classname, "grenade") || qa_q1mp_streq(classname, "MiniGrenade") || qa_q1mp_streq(classname, "MultiGrenade");
  if (grenade || qa_q1_health(game, other) > 0) {
    qa_owned_actor owner;
    const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner);
    qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
    if (has_owner && body.has_value) {
      body.value.velocity = qa_q1_vscale(entity->movedir, entity->speed * 10);
      qa_shared_body_table_write(game->host.bodies, owner, body.value);
    }
    if (!grenade && qa_q1_is_player(game, &other)) {
      qa_q1_actor *state = NULL;
      for (size_t i = 0; i < game->entity_count && state == NULL; i++) {
        qa_q1_actor *candidate = game->entities[i];
        if (qa_q1mp_streq(candidate->classname, "rogue_team_state") && candidate->has_owner && qa_same_actor(candidate->owner, other)) state = candidate;
      }
      if (state != NULL && qa_q1_actor_number(state, "fly_sound") < game->time && has_owner) {
        qa_q1mp_world_number(state, "fly_sound", game->time + 1.5);
        qa_q1_sound(game, owner.id, "ambience/windfly.wav", "voice", 1, 1);
      }
    }
  }
  if (flag(entity->spawnflags, 1)) qa_q1_remove(game, entity);
}
static void push_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (flag(entity->spawnflags, 2)) entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:push");
  else entity->spawnflags += 4;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:push");
  if (falsy(entity->speed)) entity->speed = 1000;
  qa_q1mp_world_trigger(game, entity);
}
void qa_q1mp_register_mission_shooters(qa_q1_services *game, qa_q1mp_pack pack) {
  shooter_pack *ctx = qa_q1mp_streq(pack, "rogue") ? &ROGUE : &HIPNOTIC;
  qa_q1_callback_registry_register(game->named, "hip:shooter_laser_touch", (qa_q1_callback_handlers){.ctx = ctx, .touch = laser_touch});
  qa_q1_callback_registry_register(game->named, "mission:shooter_fire", (qa_q1_callback_handlers){.ctx = ctx, .use = fire_use});
  qa_q1_callback_registry_register(game->named, "mission:shooter_think", (qa_q1_callback_handlers){.ctx = ctx, .action = shooter_think});
  qa_q1_callback_registry_register(game->named, "mission:shooter_switch", (qa_q1_callback_handlers){.ctx = ctx, .use = shooter_switch});
  qa_q1_replace_spawn(game, "trap_spikeshooter", shooter_spawn, ctx);
  qa_q1_replace_spawn(game, "trap_shooter", shooter_spawn, ctx);
  if (qa_q1mp_streq(pack, "hipnotic")) qa_q1_register_spawn(game, "trap_switched_shooter", shooter_spawn, ctx);
  if (qa_q1mp_streq(pack, "rogue")) {
    qa_q1_callback_registry_register(game->named, "rogue:push", (qa_q1_callback_handlers){.ctx = ctx, .use = push_use, .touch = push_touch});
    qa_q1_register_spawn(game, "trigger_push", push_spawn, ctx);
  }
}

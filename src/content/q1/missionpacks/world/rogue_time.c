/* Port of src/content/q1/missionpacks/world/rogue-time.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_time.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static double random_of(qa_q1_services *game) { return game->host.random(game->host.ctx); }
static const qa_actor_id *activator_of(const qa_q1_actor *entity) { return entity->has_activator ? &entity->activator : NULL; }

static void chunk(qa_q1_services *game, qa_q1_actor *explosion, qa_q1_actor *machine) {
  const qa_q1_basis basis = qa_q1_vectors(qa_q1_get_body(game, machine).angles);
  qa_q1_actor *gib = qa_q1_create(game, "time_machine_gib", NULL, NULL);
  gib->solid = "none"; gib->movement = "toss"; qa_q1mp_set_string(&gib->model, "progs/timegib.mdl");
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vsub(qa_q1_vadd(qa_q1_get_body(game, machine).origin, qa_q1_vscale(basis.forward, 84)), qa_q1_vscale(basis.up, 136));
  patch.has_velocity = true; patch.velocity = qa_q1_vscale(basis.up, -50);
  patch.has_angles = true; patch.angles = qa_q1_get_body(game, machine).angles;
  qa_q1_set_body(game, gib, &patch);
  gib->angular_velocity = QA_Q1MP_V3(300, 300, 300);
  qa_q1_sound(game, explosion->actor.id, "weapons/r_exp3.wav", "weapon", 0, 1);
  qa_q1_effect(game, "explosion", qa_q1_get_body(game, gib).origin, NULL, 1);
  machine->frame = 1; qa_q1mp_world_later(game, gib, 5, "SUB_Remove");
  qa_q1_link(game, gib);
}
static void pain(qa_q1_services *game, qa_q1_actor *machine) {
  const double health = qa_q1_health(game, machine->actor.id);
  if (health > 1100 && qa_q1_actor_number(machine, "pain_finished") > game->time) return;
  if (random_of(game) < 0.4) {
    qa_q1mp_world_number(machine, "pain_finished", game->time + 2);
    const double random = random_of(game);
    const qa_q1_basis basis = qa_q1_vectors(qa_q1_get_body(game, machine).angles);
    qa_q1_actor *explosion = qa_q1_create(game, "time_machine_pain", NULL, NULL);
    explosion->has_owner = true; explosion->owner = machine->actor.id;
    qa_q1mp_set_string(&explosion->target, machine->target);
    const qa_vec3 offset = random < 0.33 ? qa_q1_vsub(qa_q1_vscale(basis.forward, 80), qa_q1_vscale(basis.up, 64))
        : random < 0.66 ? qa_q1_vsub(qa_q1_vscale(basis.right, 80), qa_q1_vscale(basis.up, 24))
        : qa_q1_vsub(qa_q1_vsub(qa_q1_vscale(basis.forward, 64), qa_q1_vscale(basis.up, 48)), qa_q1_vscale(basis.right, 48));
    qa_q1_set_origin(game, explosion, qa_q1_vadd(qa_q1_get_body(game, machine).origin, offset));
    qa_q1mp_world_later(game, explosion, 0.2 + random_of(game) * 0.3, "rogue:time_boom");
  }
  if (health < 1000) {
    qa_q1mp_world_number(machine, "pain_finished", 0);
    machine->pain = (qa_q1_bound_pain){0}; machine->die = (qa_q1_bound_die){0};
    if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:cutscene_running", 1);
  }
}
static void pain_cb(void *ctx, qa_q1_services *game, qa_q1_actor *machine, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage; pain(game, machine);
}
static void pain_die_cb(void *ctx, qa_q1_services *game, qa_q1_actor *machine, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker; pain(game, machine);
}
void qa_q1mp_crash_time_machine(qa_q1_services *game) {
  qa_actor_id reference;
  qa_q1_actor *machine = game->world != NULL && qa_q1_actor_reference_get(game->world, "rogue:theMachine", &reference)
      ? qa_q1_get_entity(game, &reference) : NULL;
  if (machine == NULL) qa_range_error("Rogue time_crash requires item_time_machine");
  qa_q1_actor_set_damageable(machine, false); machine->movement = "fly"; machine->solid = "none";
  machine->angular_velocity = QA_Q1MP_V3(15, 0, 5);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = QA_Q1MP_V3(0, 0, -50);
  patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, machine, &patch);
  qa_q1mp_world_later(game, machine, 0.1, "rogue:time_fall");
  qa_q1mp_set_string(&machine->target, "timeramp");
  qa_q1_use_targets(game, machine, activator_of(machine));
}
static void stop_shake(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, activator_of(entity));
  qa_q1_remove(game, entity);
}
static void time_boom(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, activator_of(entity));
  qa_q1_actor *machine = qa_q1_get_entity(game, entity->has_owner ? &entity->owner : NULL);
  if (machine == NULL) qa_range_error("Time machine explosion lost its machine");
  if (qa_q1_health(game, machine->actor.id) < 1250 && machine->frame > 0) {
    if (machine->skin < 2) { machine->frame = 2; machine->skin = 2; }
  } else if (qa_q1_health(game, machine->actor.id) < 1500 && machine->frame == 0) {
    chunk(game, entity, machine); machine->frame = 1; machine->skin = 1;
  }
  qa_q1_sound(game, entity->actor.id, "weapons/r_exp3.wav", "weapon", 0, 1);
  if (random_of(game) < 0.5) qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  else {
    qa_q1_event event = {0};
    event.kind = "colored-explosion"; event.has_origin = true; event.origin = qa_q1_get_body(game, entity).origin;
    event.color_start = 244; event.color_length = 3;
    qa_q1_emit(game, event);
  }
  qa_q1mp_set_string(&entity->model, "progs/s_explod.spr"); entity->frame = 0; entity->solid = "none"; entity->movement = "none";
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
  qa_q1_set_body(game, entity, &patch);
  qa_q1mp_world_later(game, entity, 0.1, "base:explosion_frame"); qa_q1_link(game, entity);
  qa_q1_actor *stop = qa_q1_create(game, "time_stop_shake", NULL, NULL);
  qa_q1mp_set_string(&stop->target, entity->target);
  qa_q1mp_world_later(game, stop, 0.7, "rogue:time_stop_shake");
}
static void time_fall(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  if (qa_q1_actor_number(entity, "pain_finished") == 0) {
    if (body.origin.z < -20) {
      qa_q1_effect(game, "lava-splash", qa_q1_vadd(body.origin, QA_Q1MP_V3(0, 0, -80)), NULL, 1);
      qa_q1mp_world_number(entity, "pain_finished", 1);
    } else if (random_of(game) < 0.3) qa_q1_effect(game, "explosion", body.origin, NULL, 1);
  }
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = body.velocity; patch.velocity.z = body.velocity.z - 5;
  qa_q1_set_body(game, entity, &patch);
  qa_q1mp_world_later(game, entity, 0.1, "rogue:time_fall");
}
static void crash_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; (void)entity; qa_q1mp_crash_time_machine(game); }
static void crash_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)entity; (void)other; (void)activator; qa_q1mp_crash_time_machine(game);
}
static void crash_pain(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)entity; (void)attacker; (void)damage; qa_q1mp_crash_time_machine(game);
}
static void crash_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx; (void)entity; (void)attacker; qa_q1mp_crash_time_machine(game);
}
static void machine_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  qa_q1mp_set_string(&entity->model, "progs/timemach.mdl"); entity->solid = "slidebox"; entity->movement = "fly";
  entity->max_health = 1600; qa_q1_actor_set_damageable(entity, true);
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  entity->angular_velocity = QA_Q1MP_V3(0, 60, 0);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 1600);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "rogue:time_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "rogue:time_pain");
  if (game->world != NULL && !qa_q1_actor_reference_set(game->world, "rogue:theMachine", &entity->actor.id)) qa_range_error("Out of memory");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-64, -64, -144}, {64, 64, 0}});
}
static void core_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  qa_q1mp_set_string(&entity->model, "progs/timecore.mdl"); entity->solid = "none"; entity->movement = "fly";
  entity->angular_velocity = QA_Q1MP_V3(60, 60, 60);
}
void qa_q1mp_register_rogue_time(qa_q1_services *game) {
  qa_q1_callback_registry_register(game->named, "rogue:time_pain", (qa_q1_callback_handlers){.pain = pain_cb, .die = pain_die_cb});
  qa_q1_callback_registry_register(game->named, "rogue:time_stop_shake", (qa_q1_callback_handlers){.action = stop_shake});
  qa_q1_callback_registry_register(game->named, "rogue:time_boom", (qa_q1_callback_handlers){.action = time_boom});
  qa_q1_callback_registry_register(game->named, "rogue:time_fall", (qa_q1_callback_handlers){.action = time_fall});
  qa_q1_callback_registry_register(game->named, "rogue:time_crash",
                                   (qa_q1_callback_handlers){.action = crash_action, .use = crash_use, .pain = crash_pain, .die = crash_die});
  qa_q1_register_spawn(game, "item_time_machine", machine_spawn, NULL);
  qa_q1_register_spawn(game, "item_time_core", core_spawn, NULL);
}

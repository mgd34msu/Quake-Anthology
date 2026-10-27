/* Port of src/content/q1/missionpacks/monsters/gremlin-weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"

static const char *const weapons[] = {"axe", "shotgun", "supershotgun", "nailgun", "supernailgun",
                                      "grenadelauncher", "rocketlauncher", "lightning",
                                      "hipnotic:laser", "hipnotic:proximity", "hipnotic:mjolnir"};
const char *qa_q1mpm_gremlin_weapon(qa_q1mpm_mission_monster *monster) {
  const char *value = qa_q1mpm_fields_get(monster->entity, "gremlin:weapon");
  if (value == NULL) return NULL;
  for (size_t i = 0; i < sizeof(weapons) / sizeof(weapons[0]); i++)
    if (strcmp(weapons[i], value) == 0) return weapons[i];
  return NULL;
}
static void ammo(qa_q1mpm_mission_monster *monster, qa_item_id item, double used) {
  double count = monster->game->host->inventory_adjust_source_counter(
      monster->game->host->ctx, monster->entity->actor, item, -used);
  qa_q1mpm_number(monster, "currentammo", count);
}
bool qa_q1mpm_gremlin_has_ammo(qa_q1mpm_mission_monster *monster) {
  if (qa_q1mpm_actor_number(monster->entity, "currentammo") > 0) return true;
  qa_q1mpm_number(monster, "stoleweapon", 0);
  return false;
}
static void configure(qa_q1mpm_game *game, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  qa_q1mpm_inventory_entry entry = {item, count, capacity, NULL, NULL};
  game->host->inventory_configure(game->host->ctx, actor, &entry);
}
bool qa_q1mpm_gremlin_steal(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const qa_q1mpm_host *host = game->host;
  if (qa_q1mpm_actor_number(entity, "stoleweapon") != 0 || !monster->state.enemy.has ||
      !game->is_player(game, monster->state.enemy) || qa_q1mpm_monster_distance(monster) > 100 ||
      host->random(host->ctx) < 0.5)
    return false;
  qa_q1mpm_player_state *victim = game->player(game, monster->state.enemy.id);
  if (victim == NULL) return false;
  const char *weapon = victim->weapon;
  if (qa_q1mpm_str_eq(weapon, "axe") || qa_q1mpm_str_eq(weapon, "shotgun") ||
      qa_q1mpm_str_eq(weapon, "hipnotic:mjolnir"))
    return false;
  qa_item_id weapon_item = game->weapon_item(game, weapon);
  host->inventory_consume(host->ctx, victim->actor, weapon_item,
                          host->inventory_count(host->ctx, victim->actor.id, weapon_item));
  configure(game, entity->actor, weapon_item, 1, 1);
  qa_q1mpm_fields_set(entity, "gremlin:weapon", weapon);
  const char *item = game->weapon_ammo(game, weapon);
  if (item != NULL) {
    double cap = qa_q1mpm_str_eq(weapon, "supershotgun") ? 20
                 : qa_q1mpm_str_eq(weapon, "grenadelauncher") || qa_q1mpm_str_eq(weapon, "rocketlauncher") ||
                       qa_q1mpm_str_eq(weapon, "hipnotic:proximity")
                     ? 5
                     : 40;
    double amount = fmin(host->inventory_count(host->ctx, victim->actor.id, item), cap);
    host->inventory_consume(host->ctx, victim->actor, item, amount);
    configure(game, entity->actor, item, host->inventory_count(host->ctx, entity->actor.id, item) + amount, 1000000);
    qa_q1mpm_number(monster, "currentammo", host->inventory_count(host->ctx, entity->actor.id, item));
    const char *label_rerelease = NULL, *label_classic = NULL;
    if (qa_q1mpm_str_eq(weapon, "supershotgun")) { label_rerelease = "$qc_gremlin_ssg"; label_classic = "Gremlin stole your Super Shotgun\n"; }
    else if (qa_q1mpm_str_eq(weapon, "nailgun")) { label_rerelease = "$qc_gremlin_ng"; label_classic = "Gremlin stole your Nailgun\n"; }
    else if (qa_q1mpm_str_eq(weapon, "supernailgun")) { label_rerelease = "$qc_gremlin_sng"; label_classic = "Gremlin stole your Super Nailgun\n"; }
    else if (qa_q1mpm_str_eq(weapon, "grenadelauncher")) { label_rerelease = "$qc_gremlin_gl"; label_classic = "Gremlin stole your Grenade Launcher\n"; }
    else if (qa_q1mpm_str_eq(weapon, "rocketlauncher")) { label_rerelease = "$qc_gremlin_rl"; label_classic = "Gremlin stole your Rocket Launcher\n"; }
    else if (qa_q1mpm_str_eq(weapon, "lightning")) { label_rerelease = "$qc_gremlin_lg"; label_classic = "Gremlin stole your Lightning Gun\n"; }
    else if (qa_q1mpm_str_eq(weapon, "hipnotic:laser")) { label_rerelease = "$qc_gremlin_lc"; label_classic = "Gremlin stole your Laser Cannon\n"; }
    else if (qa_q1mpm_str_eq(weapon, "hipnotic:proximity")) { label_rerelease = "$qc_gremlin_prox"; label_classic = "Gremlin stole your Proximity Gun\n"; }
    if (label_rerelease != NULL)
      game->message(game, QA_Q1MPM_SOME(victim->actor.id),
                    qa_q1mpm_str_eq(game->options.edition, "rerelease") ? label_rerelease : label_classic, false);
  }
  game->select_weapon(game, victim->actor, game->choose_best(game, victim->actor));
  qa_q1mpm_number(monster, "stoleweapon", 1);
  monster->state.attack_finished = game->time;
  qa_q1mpm_references_set(entity, "lastvictim",
                          host->random(host->ctx) > 0.65 ? QA_Q1MPM_SOME(victim->actor.id)
                                                         : QA_Q1MPM_SOME(entity->actor.id));
  qa_q1mpm_opt_actor next = qa_q1mpm_gremlin_find_victim(monster);
  if (next.has) {
    qa_q1mpm_monster_found(monster, next.id);
    monster->state.attack_finished = game->time;
    monster->state.search_until = game->time + 1;
  }
  return true;
}
static qa_vec3 aim(qa_q1mpm_mission_monster *monster, double spread) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_vec3 target;
  qa_vec3 target_or_zero = qa_q1mpm_monster_target(monster, &target) ? target : QA_Q1MPM_ZERO;
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target_or_zero, qa_q1mpm_monster_origin(monster)));
  qa_vec3 angles = qa_q1mpm_velocity_angles(direction);
  char buf[96];
  char fx[32], fy[32], fz[32];
  qa_q1mpm_format_number(fx, sizeof(fx), angles.x);
  qa_q1mpm_format_number(fy, sizeof(fy), angles.y);
  qa_q1mpm_format_number(fz, sizeof(fz), angles.z);
  snprintf(buf, sizeof(buf), "%s %s %s", fx, fy, fz);
  qa_q1mpm_fields_set(entity, "v_angle", buf);
  qa_q1mpm_basis basis = game->make_vectors(game, angles);
  const qa_q1mpm_host *host = game->host;
  return qa_q1mpm_normalize(qa_q1mpm_vadd(
      qa_q1mpm_vadd(direction, qa_q1mpm_vscale(basis.right, (host->random(host->ctx) * 2 - 1) * spread)),
      qa_q1mpm_vscale(game->basis.up, (host->random(host->ctx) * 2 - 1) * spread)));
}
void qa_q1mpm_gremlin_fire_nail(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/nails", 1);
  monster->entity->effects |= 2;
  monster->game->sound(monster->game, monster->entity, "weapons/rocket1i.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  qa_q1mpm_ext_launch_spike(monster->game, QA_Q1MPM_SOME(monster->entity->actor.id),
                            qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 16)),
                            qa_q1mpm_vscale(direction, 1000));
}
void qa_q1mpm_gremlin_fire_laser(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/cells", 1);
  monster->entity->effects |= 2;
  monster->game->sound(monster->game, monster->entity, "weapons/rocket1i.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  qa_q1mpm_ext_launch_hipnotic_laser(monster->game, monster->entity->actor.id,
                                     qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 16)),
                                     direction, false);
}
static void shotgun(qa_q1mpm_mission_monster *monster, bool is_double) {
  ammo(monster, "q1:ammo/shells", is_double ? 2 : 1);
  monster->entity->effects |= 2;
  monster->game->sound(monster->game, monster->entity, is_double ? "weapons/shotgn2.wav" : "weapons/guncock.wav",
                        QA_Q1MPM_CH("weapon"), 1, 1);
  qa_vec3 direction = aim(monster, is_double ? 0.3 : 0.1);
  qa_vec3 angles = qa_q1mpm_velocity_angles(direction);
  char buf[96];
  char fx[32], fy[32], fz[32];
  qa_q1mpm_format_number(fx, sizeof(fx), angles.x);
  qa_q1mpm_format_number(fy, sizeof(fy), angles.y);
  qa_q1mpm_format_number(fz, sizeof(fz), angles.z);
  snprintf(buf, sizeof(buf), "%s %s %s", fx, fy, fz);
  qa_q1mpm_fields_set(monster->entity, "v_angle", buf);
  qa_q1mpm_ext_fire_bullets(monster->game, monster->entity->actor, direction, angles, is_double ? 14 : 6,
                            is_double ? 0.14 : 0.04, is_double ? 0.08 : 0.04,
                            is_double ? "supershotgun" : "shotgun");
}
static void rocket(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  ammo(monster, "q1:ammo/rockets", 1);
  entity->effects |= 2;
  game->sound(game, entity, "weapons/sgun1.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_fields_set(entity, "punchangle", "-2 0 0");
  qa_vec3 direction = aim(monster, 0.1);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster),
                                               qa_q1mpm_vscale(game->basis.forward, 8)),
                                 QA_Q1MPM_V3(0, 0, 16));
  qa_q1mpm_actor *shot = qa_q1mpm_missile(game, entity->actor.id, "missile", "progs/missile.mdl", origin,
                                          qa_q1mpm_vscale(direction, 1000), "projectile_touch", 5);
  shot->projectile = "rocket";
  shot->projectile_weapon = "rocketlauncher";
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1mpm_velocity_angles(game->body(game, shot).velocity);
  game->set_body(game, shot, &patch);
}
void qa_q1mpm_gremlin_fire_lightning(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const qa_q1mpm_host *host = game->host;
  if (entity->water_type <= -3) {
    double cells = host->inventory_count(host->ctx, entity->actor.id, "q1:ammo/cells");
    configure(game, entity->actor, "q1:ammo/cells", 0, 1000000);
    qa_q1mpm_opt_actor ignore = game->world == NULL ? QA_Q1MPM_NONE : QA_Q1MPM_SOME(game->world->actor.id);
    game->radius_damage(game, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 35 * cells, ignore, "lightning", "discharge");
    return;
  }
  entity->effects |= 2;
  qa_q1mpm_monster_face(monster);
  ammo(monster, "q1:ammo/cells", 2);
  qa_vec3 origin = qa_q1mpm_monster_origin(monster);
  qa_vec3 start = qa_q1mpm_vadd(origin, QA_Q1MPM_V3(0, 0, 16));
  qa_vec3 direction = aim(monster, 0.1);
  qa_q1mpm_trace_request wall_request = {start, qa_q1mpm_vadd(origin, qa_q1mpm_vscale(direction, 600)),
                                         QA_Q1MPM_POINT, QA_Q1MPM_SOME(entity->actor.id), false, false};
  qa_q1mpm_trace wall = host->trace(host->ctx, &wall_request);
  qa_q1mpm_event beam = {0};
  beam.kind = "beam";
  beam.style = "lightning2";
  beam.actor = QA_Q1MPM_SOME(entity->actor.id);
  beam.start = start;
  beam.end = wall.end;
  host->emit(host->ctx, &beam);
  qa_vec3 end = qa_q1mpm_vadd(wall.end, qa_q1mpm_vscale(direction, 4));
  qa_vec3 delta = qa_q1mpm_vsub(end, start);
  qa_vec3 side = {-delta.y * 16, -delta.y * 16, 0};
  qa_vec3 offsets[3];
  offsets[0] = QA_Q1MPM_ZERO;
  offsets[1] = side;
  offsets[2] = qa_q1mpm_vscale(side, -1);
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1mpm_trace_request request = {qa_q1mpm_vadd(start, offsets[i]), qa_q1mpm_vadd(end, offsets[i]),
                                      QA_Q1MPM_POINT, QA_Q1MPM_SOME(entity->actor.id), true, false};
    qa_q1mpm_trace trace = host->trace(host->ctx, &request);
    if (!trace.actor.has) continue;
    bool seen = false;
    for (size_t j = 0; j < hit_count; j++) if (qa_same_actor(hit[j], trace.actor.id)) seen = true;
    if (seen) continue;
    hit[hit_count++] = trace.actor.id;
    qa_q1mpm_combat_read combat;
    if (host->combat_read(host->ctx, trace.actor.id, &combat) && combat.can_take_damage) {
      game->effect(game, "blood", trace.end, trace.actor, 120);
      game->damage(game, trace.actor.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), 30,
                   "lightning", "direct", "electric");
    }
  }
}
static void proximity(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/rockets", 1);
  monster->game->sound(monster->game, monster->entity, "weapons/grenade.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  qa_vec3 velocity = qa_q1mpm_vscale(direction, 600);
  velocity.z = 200;
  qa_q1mpm_ext_launch_hipnotic_proximity(monster->game, monster->entity->actor.id,
                                         qa_q1mpm_monster_origin(monster), velocity);
}
bool qa_q1mpm_gremlin_weapon_attack(qa_q1mpm_mission_monster *monster) {
  if (!qa_q1mpm_gremlin_has_ammo(monster)) return false;
  qa_q1mpm_number(monster, "show_hostile", monster->game->time + 1);
  const char *weapon = qa_q1mpm_gremlin_weapon(monster);
  if (qa_q1mpm_str_eq(weapon, "shotgun")) {
    qa_q1mpm_monster_play(monster, "gremlin_shot1");
    shotgun(monster, false);
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "supershotgun")) {
    qa_q1mpm_monster_play(monster, "gremlin_shot1");
    shotgun(monster, true);
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "nailgun") || qa_q1mpm_str_eq(weapon, "supernailgun")) {
    qa_q1mpm_monster_play(monster, "gremlin_nail3");
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "grenadelauncher")) {
    qa_q1mpm_monster_play(monster, "gremlin_rocket1");
    qa_q1mpm_ext_launch_ogre_grenade(monster);
    ammo(monster, "q1:ammo/rockets", 1);
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "rocketlauncher")) {
    qa_q1mpm_monster_play(monster, "gremlin_rocket1");
    rocket(monster);
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "lightning")) {
    qa_q1mpm_monster_play(monster, "gremlin_light1");
    qa_q1mpm_monster_attack_finished(monster, 1);
    monster->game->sound(monster->game, monster->entity, "weapons/lstart.wav", QA_Q1MPM_CH("auto"), 1, 1);
  } else if (qa_q1mpm_str_eq(weapon, "hipnotic:laser")) {
    qa_q1mpm_monster_play(monster, "gremlin_laser3");
    qa_q1mpm_monster_attack_finished(monster, 1);
  } else if (qa_q1mpm_str_eq(weapon, "hipnotic:proximity")) {
    qa_q1mpm_monster_play(monster, "gremlin_rocket1");
    proximity(monster);
    qa_q1mpm_monster_attack_finished(monster, 1);
  }
  return true;
}
void qa_q1mpm_gremlin_drop_backpack(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const qa_q1mpm_host *host = game->host;
  qa_q1mpm_actor *pack = game->create(game, "item_backpack");
  const char *selected = NULL;
  for (size_t i = 0; i < sizeof(weapons) / sizeof(weapons[0]); i++) {
    if (host->inventory_count(host->ctx, entity->actor.id, game->weapon_item(game, weapons[i])) > 0) {
      selected = weapons[i];
      break;
    }
  }
  monster->runtime->base->backpacks_set(
      monster->runtime->base->ctx, pack->actor, selected, NULL, 0,
      fmax(0, host->inventory_count(host->ctx, entity->actor.id, "q1:ammo/shells")),
      fmax(0, host->inventory_count(host->ctx, entity->actor.id, "q1:ammo/nails")),
      fmax(0, host->inventory_count(host->ctx, entity->actor.id, "q1:ammo/rockets")),
      fmax(0, host->inventory_count(host->ctx, entity->actor.id, "q1:ammo/cells")));
  snprintf(pack->model, sizeof(pack->model), "%s", "progs/backpack.mdl");
  pack->solid = "trigger";
  pack->movement = "toss";
  pack->movement_flags = 256;
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, -24));
  patch.has_velocity = true;
  patch.velocity = QA_Q1MPM_V3(-100 + host->random(host->ctx) * 200, -100 + host->random(host->ctx) * 200, 300);
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
  game->set_body(game, pack, &patch);
  pack->touch = game->named.touch(game->named.ctx, pack, "base:backpack_touch");
  game->schedule(game, pack, 120, game->named.action(game->named.ctx, pack, "SUB_Remove"));
  game->link(game, pack);
}

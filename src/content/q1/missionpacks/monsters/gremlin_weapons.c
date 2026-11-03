/* Port of src/content/q1/missionpacks/monsters/gremlin-weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "monsters_internal.h"
#include "qa/foundation/js.h"

static const char *const weapons[] = {"axe", "shotgun", "supershotgun", "nailgun", "supernailgun",
                                      "grenadelauncher", "rocketlauncher", "lightning",
                                      "hipnotic:laser", "hipnotic:proximity", "hipnotic:mjolnir"};
const char *qa_q1mpm_gremlin_weapon(qa_q1mpm_mission_monster *monster) {
  const char *value = qa_q1_actor_field_get(monster->base.entity, "gremlin:weapon");
  if (value == NULL) return NULL;
  for (size_t i = 0; i < sizeof(weapons) / sizeof(weapons[0]); i++)
    if (strcmp(weapons[i], value) == 0) return weapons[i];
  return NULL;
}
static void ammo(qa_q1mpm_mission_monster *monster, qa_item_id item, double used) {
  const double count = qa_shared_inventory_table_adjust_source_counter(monster->base.game->host.inventory,
                                                                     monster->base.entity->actor, item, -used);
  qa_q1mpm_number(monster, "currentammo", count);
}
bool qa_q1mpm_gremlin_has_ammo(qa_q1mpm_mission_monster *monster) {
  if (qa_q1_actor_number(monster->base.entity, "currentammo") > 0) return true;
  qa_q1mpm_number(monster, "stoleweapon", 0);
  return false;
}
/* TS game.host.inventory.configure(actor, { item, count, capacity }). */
static void configure(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item;
  entry.count = count;
  entry.capacity = capacity;
  qa_shared_inventory_table_configure(game->host.inventory, actor, entry, NULL, NULL);
}
bool qa_q1mpm_gremlin_steal(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_shared_inventory_table *inventory = game->host.inventory;
  if (qa_q1_actor_number(entity, "stoleweapon") != 0 || !monster->base.state->has_enemy ||
      !qa_q1_is_player(game, qa_q1_base_monster_enemy(&monster->base)) || qa_q1_base_monster_distance(&monster->base) > 100 ||
      qa_q1_host_random(game) < 0.5)
    return false;
  qa_q1_player_state *victim = qa_q1_player(game, monster->base.state->enemy);
  if (victim == NULL) return false;
  const char *weapon = victim->weapon;
  if (q1mpm_str_eq(weapon, "axe") || q1mpm_str_eq(weapon, "shotgun") ||
      q1mpm_str_eq(weapon, "hipnotic:mjolnir"))
    return false;
  qa_owned_actor actor2 = victim->actor;
  qa_item_id item2 = qa_q1_services_weapon_item(game, weapon);
  double count = qa_shared_inventory_table_count(inventory, victim->actor.id,
                                                                          qa_q1_services_weapon_item(game, weapon));
  (void)qa_shared_inventory_table_consume(inventory, actor2, item2, count);
  configure(game, entity->actor, qa_q1_services_weapon_item(game, weapon), 1, 1);
  (void)qa_q1_actor_field_set(entity, "gremlin:weapon", weapon);
  const qa_item_id item = qa_q1_services_weapon_ammo(game, weapon);
  if (item != NULL) {
    const double cap = q1mpm_str_eq(weapon, "supershotgun") ? 20
                       : q1mpm_str_eq(weapon, "grenadelauncher") || q1mpm_str_eq(weapon, "rocketlauncher") ||
                               q1mpm_str_eq(weapon, "hipnotic:proximity")
                           ? 5
                           : 40;
    const double amount = qa_js_min(qa_shared_inventory_table_count(inventory, victim->actor.id, item), cap);
    (void)qa_shared_inventory_table_consume(inventory, victim->actor, item, amount);
    configure(game, entity->actor, item, qa_shared_inventory_table_count(inventory, entity->actor.id, item) + amount, 1000000);
    qa_q1mpm_number(monster, "currentammo", qa_shared_inventory_table_count(inventory, entity->actor.id, item));
    const char *label_rerelease = NULL, *label_classic = NULL;
    if (q1mpm_str_eq(weapon, "supershotgun")) { label_rerelease = "$qc_gremlin_ssg"; label_classic = "Gremlin stole your Super Shotgun\n"; }
    else if (q1mpm_str_eq(weapon, "nailgun")) { label_rerelease = "$qc_gremlin_ng"; label_classic = "Gremlin stole your Nailgun\n"; }
    else if (q1mpm_str_eq(weapon, "supernailgun")) { label_rerelease = "$qc_gremlin_sng"; label_classic = "Gremlin stole your Super Nailgun\n"; }
    else if (q1mpm_str_eq(weapon, "grenadelauncher")) { label_rerelease = "$qc_gremlin_gl"; label_classic = "Gremlin stole your Grenade Launcher\n"; }
    else if (q1mpm_str_eq(weapon, "rocketlauncher")) { label_rerelease = "$qc_gremlin_rl"; label_classic = "Gremlin stole your Rocket Launcher\n"; }
    else if (q1mpm_str_eq(weapon, "lightning")) { label_rerelease = "$qc_gremlin_lg"; label_classic = "Gremlin stole your Lightning Gun\n"; }
    else if (q1mpm_str_eq(weapon, "hipnotic:laser")) { label_rerelease = "$qc_gremlin_lc"; label_classic = "Gremlin stole your Laser Cannon\n"; }
    else if (q1mpm_str_eq(weapon, "hipnotic:proximity")) { label_rerelease = "$qc_gremlin_prox"; label_classic = "Gremlin stole your Proximity Gun\n"; }
    if (label_rerelease != NULL)
      qa_q1_message(game, &victim->actor.id,
                    q1mpm_str_eq(game->options.edition, "rerelease") ? label_rerelease : label_classic, false, NULL, 0);
  }
  (void)qa_q1_select_weapon(game, victim->actor, qa_q1_choose_best(game, victim->actor));
  qa_q1mpm_number(monster, "stoleweapon", 1);
  monster->base.state->attack_finished = game->time;
  (void)qa_q1_actor_reference_set(entity, "lastvictim",
                                  qa_q1_host_random(game) > 0.65 ? &victim->actor.id : &entity->actor.id);
  qa_actor_id next;
  if (qa_q1mpm_gremlin_find_victim(monster, &next)) {
    qa_q1_base_monster_found(&monster->base, next);
    monster->base.state->attack_finished = game->time;
    monster->base.state->search_until = game->time + 1;
  }
  return true;
}
/* TS entity.fields.set(key, `${angles.x} ${angles.y} ${angles.z}`). */
static void set_angles_field(qa_q1_actor *entity, const char *key, qa_vec3 angles) {
  char x[40], y[40], z[40], text[128];
  qa_net_js_number(angles.x, x, sizeof x);
  qa_net_js_number(angles.y, y, sizeof y);
  qa_net_js_number(angles.z, z, sizeof z);
  snprintf(text, sizeof text, "%s %s %s", x, y, z);
  (void)qa_q1_actor_field_set(entity, key, text);
}
static qa_vec3 aim(qa_q1mpm_mission_monster *monster, double spread) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_vec3 target;
  qa_vec3 target_or_zero = qa_q1_base_monster_target(&monster->base, &target) ? target : QA_Q1_ZERO;
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target_or_zero, qa_q1_base_monster_origin(&monster->base)));
  const qa_vec3 angles = qa_q1mp_velocity_angles(direction);
  set_angles_field(entity, "v_angle", angles);
  const qa_q1_basis basis = qa_q1_make_vectors(game, angles);
  const qa_vec3 right = qa_q1_vadd(direction, qa_q1_vscale(basis.right, (qa_q1_host_random(game) * 2 - 1) * spread));
  return qa_q1_normalize(qa_q1_vadd(right, qa_q1_vscale(game->basis.up, (qa_q1_host_random(game) * 2 - 1) * spread)));
}
void qa_q1mpm_gremlin_fire_nail(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/nails", 1);
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "weapons/rocket1i.wav", "weapon", 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  (void)qa_q1_launch_spike(monster->base.game, &monster->base.entity->actor.id,
                           qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 16}),
                           qa_q1_vscale(direction, 1000), "spike");
}
void qa_q1mpm_gremlin_fire_laser(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/cells", 1);
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "weapons/rocket1i.wav", "weapon", 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  (void)qa_q1mp_launch_hipnotic_laser(monster->base.game, monster->base.entity->actor.id,
                                      qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 16}),
                                      direction, false, NULL);
}
static void shotgun(qa_q1mpm_mission_monster *monster, bool is_double) {
  ammo(monster, "q1:ammo/shells", is_double ? 2 : 1);
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, is_double ? "weapons/shotgn2.wav" : "weapons/guncock.wav", "weapon", 1, 1);
  qa_vec3 direction = aim(monster, is_double ? 0.3 : 0.1);
  const qa_vec3 angles = qa_q1mp_velocity_angles(direction);
  set_angles_field(monster->base.entity, "v_angle", angles);
  qa_q1_fire_bullets(monster->base.game, monster->base.entity->actor, direction, angles, is_double ? 14 : 6,
                            is_double ? 0.14 : 0.04, is_double ? 0.08 : 0.04,
                            is_double ? "supershotgun" : "shotgun");
}
static void rocket(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  ammo(monster, "q1:ammo/rockets", 1);
  entity->effects = qa_js_bit_or(entity->effects, 2);
  qa_q1_sound(game, entity->actor.id, "weapons/sgun1.wav", "weapon", 1, 1);
  (void)qa_q1_actor_field_set(entity, "punchangle", "-2 0 0");
  qa_vec3 direction = aim(monster, 0.1);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_base_monster_origin(&monster->base),
                                               qa_q1_vscale(game->basis.forward, 8)),
                                 (qa_vec3){0, 0, 16});
  qa_q1_actor *shot = qa_q1mpm_missile(game, entity->actor.id, "missile", "progs/missile.mdl", origin,
                                          qa_q1_vscale(direction, 1000), "projectile_touch", 5);
  shot->projectile = "rocket";
  shot->projectile_weapon = "rocketlauncher";
  qa_q1_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = qa_q1mp_velocity_angles(qa_q1_get_body(game, shot).velocity);
  qa_q1_set_body(game, shot, &patch);
}
void qa_q1mpm_gremlin_fire_lightning(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (entity->water_type <= -3) {
    const double cells = qa_shared_inventory_table_count(game->host.inventory, entity->actor.id, "q1:ammo/cells");
    configure(game, entity->actor, "q1:ammo/cells", 0, 1000000);
    qa_q1_radius_damage(game, entity->actor.id, &entity->actor.id, 35 * cells,
                        game->world == NULL ? NULL : &game->world->actor.id, "lightning", "discharge");
    return;
  }
  entity->effects = qa_js_bit_or(entity->effects, 2);
  qa_q1_base_monster_face(&monster->base);
  ammo(monster, "q1:ammo/cells", 2);
  const qa_vec3 start = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 16});
  const qa_vec3 direction = aim(monster, 0.1);
  const qa_q1_trace wall = q1mpm_trace(game, start, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base),
                                                               qa_q1_vscale(direction, 600)),
                                       QA_Q1_POINT, &entity->actor.id, false);
  qa_q1_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "beam";
  beam.beam_style = "lightning2";
  beam.has_actor = true;
  beam.actor = entity->actor.id;
  beam.start = start;
  beam.end = wall.end;
  game->host.emit(game->host.ctx, beam);
  qa_vec3 end = qa_q1_vadd(wall.end, qa_q1_vscale(direction, 4));
  qa_vec3 delta = qa_q1_vsub(end, start);
  qa_vec3 side = {-delta.y * 16, -delta.y * 16, 0};
  qa_vec3 offsets[3];
  offsets[0] = QA_Q1_ZERO;
  offsets[1] = side;
  offsets[2] = qa_q1_vscale(side, -1);
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (int i = 0; i < 3; i++) {
    const qa_q1_trace trace = q1mpm_trace(game, qa_q1_vadd(start, offsets[i]), qa_q1_vadd(end, offsets[i]), QA_Q1_POINT,
                                          &entity->actor.id, true);
    if (!trace.has_actor) continue;
    const qa_actor_id target = trace.actor;
    bool seen = false;
    for (size_t j = 0; j < hit_count; j++) if (qa_same_actor(hit[j], target)) seen = true;
    if (seen) continue;
    hit[hit_count++] = target;
    if (q1mpm_can_take_damage(game, target)) {
      qa_q1_event particles;
      memset(&particles, 0, sizeof particles);
      particles.kind = "particles";
      particles.has_origin = true;
      particles.origin = trace.end;
      particles.direction = (qa_vec3){0, 0, 100};
      particles.color = 225;
      particles.count = 120;
      game->host.emit(game->host.ctx, particles);
      (void)qa_q1_damage(game, target, entity->actor.id, &entity->actor.id, 30, "lightning", "direct", "electric", NULL);
    }
  }
}
static void proximity(qa_q1mpm_mission_monster *monster) {
  ammo(monster, "q1:ammo/rockets", 1);
  qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  qa_vec3 direction = aim(monster, 0.1);
  qa_vec3 velocity = qa_q1_vscale(direction, 600);
  velocity.z = 200;
  (void)qa_q1mp_launch_hipnotic_proximity(monster->base.game, monster->base.entity->actor.id,
                                          qa_q1_base_monster_origin(&monster->base), velocity);
}
bool qa_q1mpm_gremlin_weapon_attack(qa_q1mpm_mission_monster *monster) {
  if (!qa_q1mpm_gremlin_has_ammo(monster)) return false;
  qa_q1mpm_number(monster, "show_hostile", monster->base.game->time + 1);
  const char *weapon = qa_q1mpm_gremlin_weapon(monster);
  if (q1mpm_str_eq(weapon, "shotgun")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_shot1");
    shotgun(monster, false);
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "supershotgun")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_shot1");
    shotgun(monster, true);
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "nailgun") || q1mpm_str_eq(weapon, "supernailgun")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_nail3");
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "grenadelauncher")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_rocket1");
    qa_q1_launch_ogre_grenade(&monster->base);
    ammo(monster, "q1:ammo/rockets", 1);
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "rocketlauncher")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_rocket1");
    rocket(monster);
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "lightning")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_light1");
    qa_q1_base_monster_attack_finished(&monster->base, 1);
    qa_q1_sound(monster->base.game, monster->base.entity->actor.id, "weapons/lstart.wav", "auto", 1, 1);
  } else if (q1mpm_str_eq(weapon, "hipnotic:laser")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_laser3");
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  } else if (q1mpm_str_eq(weapon, "hipnotic:proximity")) {
    qa_q1_base_monster_play(&monster->base, "gremlin_rocket1");
    proximity(monster);
    qa_q1_base_monster_attack_finished(&monster->base, 1);
  }
  return true;
}
void qa_q1mpm_gremlin_drop_backpack(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_shared_inventory_table *inventory = game->host.inventory;
  qa_q1_actor *pack = qa_q1_create(game, "item_backpack", NULL, NULL);
  const char *selected = NULL;
  for (size_t i = 0; i < sizeof weapons / sizeof weapons[0] && selected == NULL; i++)
    if (qa_shared_inventory_table_count(inventory, entity->actor.id, qa_q1_services_weapon_item(game, weapons[i])) > 0)
      selected = weapons[i];
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = selected;
  contents.has_extra = true; /* TS extra: [] */
  contents.shells = qa_js_max(0, qa_shared_inventory_table_count(inventory, entity->actor.id, "q1:ammo/shells"));
  contents.nails = qa_js_max(0, qa_shared_inventory_table_count(inventory, entity->actor.id, "q1:ammo/nails"));
  contents.rockets = qa_js_max(0, qa_shared_inventory_table_count(inventory, entity->actor.id, "q1:ammo/rockets"));
  contents.cells = qa_js_max(0, qa_shared_inventory_table_count(inventory, entity->actor.id, "q1:ammo/cells"));
  qa_q1_creatures_backpacks_set(qa_q1_base_creatures(monster->runtime->base), pack->actor, &contents);
  q1mpm_set_text(&pack->model, "progs/backpack.mdl");
  pack->solid = "trigger";
  pack->movement = "toss";
  pack->movement_flags = 256;
  const double x = -100 + qa_q1_host_random(game) * 200;
  const double y = -100 + qa_q1_host_random(game) * 200;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, -24});
  patch.has_velocity = true;
  patch.velocity = (qa_vec3){x, y, 300};
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
  qa_q1_set_body(game, pack, &patch);
  pack->touch = qa_q1_callback_registry_touch(game->named, pack, "base:backpack_touch");
  qa_q1_schedule(game, pack, 120, qa_q1_callback_registry_action(game->named, pack, "SUB_Remove"));
  qa_q1_link(game, pack);
}

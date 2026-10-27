/* Port of src/content/q1/base/projectiles.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/projectiles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/creatures.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_vec3 ZERO3 = {0, 0, 0};
static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static double host_random(qa_q1b_game *game) { return game->host->random(game->host->ctx); }
static bool edition_is(qa_q1b_game *game, const char *edition) {
  return strcmp(game->options.edition, edition) == 0;
}
static void set_body_origin_velocity_bounds(qa_q1b_game *game, qa_q1b_actor *entity,
                                            qa_vec3 origin, qa_vec3 velocity, qa_bounds bounds) {
  qa_q1b_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = bounds;
  game->set_body(game, entity, &patch);
}

static qa_vec3 damage_velocity(qa_q1b_game *game, double damage) {
  qa_vec3 base = V3(100 * (host_random(game) * 2 - 1),
                    100 * (host_random(game) * 2 - 1),
                    200 + 100 * host_random(game));
  return qa_scale3(base, damage > -50 ? 0.7 : damage > -200 ? 2 : 10);
}
qa_q1b_actor *qa_q1_throw_gib(qa_q1b_game *game, qa_vec3 origin, const char *model, double damage) {
  qa_q1b_actor *gib = game->create(game, "gib");
  (void)snprintf(gib->model, sizeof gib->model, "progs/%s.mdl", model);
  gib->movement = "bounce";
  set_body_origin_velocity_bounds(game, gib, origin, damage_velocity(game, damage), POINT_BOUNDS);
  gib->angular_velocity = V3(host_random(game) * 600, host_random(game) * 600, host_random(game) * 600);
  game->schedule(game, gib, 10 + host_random(game) * 10, game->named.action(game->named.ctx, gib, "SUB_Remove"));
  game->link(game, gib);
  return gib;
}
void qa_q1_throw_head_damage(qa_q1b_game *game, qa_q1b_actor *entity, const char *model, double damage) {
  game->cancel(game, entity);
  (void)snprintf(entity->model, sizeof entity->model, "progs/%s.mdl", model);
  entity->frame = 0; entity->movement = "bounce";
  qa_q1b_actor_set_damageable(game, entity, false);
  entity->solid = "none";
  qa_vec3 origin = game->body(game, entity).origin;
  qa_bounds bounds = {V3(-16, -16, 0), V3(16, 16, 56)};
  qa_q1b_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_add3(origin, V3(0, 0, -24));
  patch.has_velocity = true; patch.velocity = damage_velocity(game, damage);
  patch.has_bounds = true; patch.bounds = bounds;
  patch.has_ground = true; patch.ground = QA_Q1B_NO_ACTOR;
  game->set_body(game, entity, &patch);
  entity->angular_velocity = V3(0, (host_random(game) * 2 - 1) * 600, 0);
  game->link(game, entity);
}
void qa_q1_throw_head(qa_q1b_game *game, qa_q1b_actor *entity, const char *model) {
  qa_q1_throw_head_damage(game, entity, model, game->health(game, entity->actor.id));
}
qa_q1b_actor *qa_q1_spawn_meat_spray(qa_q1b_game *game, qa_q1b_actor *owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1b_actor *missile = game->create(game, "meat_spray");
  qa_vec3 angles = game->body(game, owner).angles;
  missile->owner = qa_q1b_some_actor(owner->actor.id);
  missile->movement = "bounce"; missile->solid = "none";
  if (edition_is(game, "rerelease")) angles.x = -angles.x;
  (void)game->make_vectors(game, angles);
  qa_vec3 adjusted = velocity;
  adjusted.z = velocity.z + 250 + 50 * host_random(game);
  set_body_origin_velocity_bounds(game, missile, origin, adjusted, POINT_BOUNDS);
  missile->angular_velocity = V3(3000, 1000, 2000);
  (void)snprintf(missile->model, sizeof missile->model, "progs/zom_gib.mdl");
  game->schedule(game, missile, 1, game->named.action(game->named.ctx, missile, "SUB_Remove"));
  game->link(game, missile);
  return missile;
}

typedef struct weapon_name { const char *weapon; const char *classic; const char *localized; } weapon_name;
static const weapon_name BACKPACK_WEAPONS[] = {
  {"axe", "Axe", "$qc_axe"}, {"shotgun", "Shotgun", "$qc_shotgun"},
  {"supershotgun", "Double-barrelled Shotgun", "$qc_double_shotgun"},
  {"nailgun", "Nailgun", "$qc_nailgun"},
  {"supernailgun", "Super Nailgun", "$qc_super_nailgun"},
  {"grenadelauncher", "Grenade Launcher", "$qc_grenade_launcher"},
  {"rocketlauncher", "Rocket Launcher", "$qc_rocket_launcher"},
  {"lightning", "Thunderbolt", "$qc_thunderbolt"},
  {"hipnotic:laser", "Laser Cannon", "$qc_laser_cannon"},
  {"hipnotic:proximity", "Proximity Gun", "$qc_prox_gun"},
  {"hipnotic:mjolnir", "Mjolnir", "$qc_mjolnir"},
};
typedef struct ammo_name { const char *classic; const char *localized; double count; } ammo_name;
qa_q1_backpack_message qa_q1_backpack_message_make(const qa_q1_backpack_contents *contents,
                                                   const char *edition, bool new_weapon,
                                                   char *text_out, size_t text_size,
                                                   qa_q1b_message_part *parts_out, size_t parts_capacity,
                                                   qa_q1b_message_arg *args_out, size_t args_capacity) {
  ammo_name entries[8];
  size_t entry_count = 0;
  entries[entry_count++] = (ammo_name){"shells", "$qc_backpack_shells", contents->shells};
  entries[entry_count++] = (ammo_name){"nails", "$qc_backpack_nails", contents->nails};
  entries[entry_count++] = (ammo_name){"rockets", "$qc_backpack_rockets", contents->rockets};
  entries[entry_count++] = (ammo_name){"cells", "$qc_backpack_cells", contents->cells};
  size_t extra_count = contents->has_extra ? contents->extra_count : 0;
  for (size_t i = 0; i < extra_count && entry_count < 8; i++) {
    const qa_q1_backpack_extra *extra = &contents->extra[i];
    if (strcmp(extra->item, "rogue:ammo/lava-nails") == 0)
      entries[entry_count++] = (ammo_name){"lava nails", "$qc_backpack_lava_nails", extra->count};
    else if (strcmp(extra->item, "rogue:ammo/multi-rockets") == 0)
      entries[entry_count++] = (ammo_name){"multi rockets", "$qc_backpack_multi_rockets", extra->count};
    else if (strcmp(extra->item, "rogue:ammo/plasma") == 0)
      entries[entry_count++] = (ammo_name){"plasma balls", "$qc_backpack_plasma_balls", extra->count};
  }
  const weapon_name *weapon = NULL;
  if (new_weapon && contents->weapon != NULL) {
    for (size_t i = 0; i < sizeof BACKPACK_WEAPONS / sizeof *BACKPACK_WEAPONS; i++)
      if (strcmp(BACKPACK_WEAPONS[i].weapon, contents->weapon) == 0) { weapon = &BACKPACK_WEAPONS[i]; break; }
  }
  qa_q1_backpack_message result = {0};
  if (strcmp(edition, "classic") == 0) {
    size_t used = 0;
    used += (size_t)snprintf(text_out + used, used < text_size ? text_size - used : 0, "You get ");
    bool first = true;
    if (weapon != NULL) {
      used += (size_t)snprintf(text_out + used, used < text_size ? text_size - used : 0, "the %s", weapon->classic);
      first = false;
    }
    for (size_t i = 0; i < entry_count; i++) {
      if (entries[i].count <= 0) continue;
      char count[32];
      qa_q1b_format_number(count, sizeof count, entries[i].count);
      used += (size_t)snprintf(text_out + used, used < text_size ? text_size - used : 0,
                               "%s%s %s", first ? "" : ", ", count, entries[i].classic);
      first = false;
    }
    result.text = text_out;
    result.parts = parts_out;
    result.part_count = 0;
    return result;
  }
  size_t part_count = 0, arg_count = 0;
  if (part_count < parts_capacity) parts_out[part_count++] = (qa_q1b_message_part){"$qc_backpack_got", NULL, 0};
  bool first_item = true;
  if (weapon != NULL) {
    if (part_count < parts_capacity) parts_out[part_count++] = (qa_q1b_message_part){weapon->localized, NULL, 0};
    first_item = false;
  }
  for (size_t i = 0; i < entry_count; i++) {
    if (entries[i].count <= 0) continue;
    if (!first_item && part_count < parts_capacity) parts_out[part_count++] = (qa_q1b_message_part){", ", NULL, 0};
    first_item = false;
    const qa_q1b_message_arg *args = NULL;
    if (arg_count < args_capacity) {
      args_out[arg_count] = (qa_q1b_message_arg){true, entries[i].count, NULL};
      args = &args_out[arg_count++];
    }
    if (part_count < parts_capacity)
      parts_out[part_count++] = (qa_q1b_message_part){entries[i].localized, args, args != NULL ? 1 : 0};
  }
  result.text = "$qc_backpack_got";
  result.parts = parts_out;
  result.part_count = part_count;
  return result;
}

qa_q1b_actor *qa_q1_drop_backpack(qa_q1b_game *game, qa_vec3 origin,
                                  const qa_q1_backpack_contents *contents,
                                  const qa_q1_backpack_launch *launch) {
  double extra_total = 0;
  if (contents->has_extra)
    for (size_t i = 0; i < contents->extra_count; i++) extra_total += contents->extra[i].count;
  if (contents->shells + contents->nails + contents->rockets + contents->cells + extra_total == 0) return NULL;
  qa_q1b_actor *pack = game->create(game, "item_backpack");
  (void)snprintf(pack->model, sizeof pack->model, "progs/backpack.mdl");
  pack->solid = "trigger";
  pack->movement = launch != NULL ? launch->movement : "toss";
  const char *weapon = contents->weapon;
  bool rerelease = edition_is(game, "rerelease");
  qa_q1_backpack_contents stored = *contents;
  stored.has_extra = true;
  if (!contents->has_extra) { stored.extra = NULL; stored.extra_count = 0; }
  stored.selection = contents->has_selection ? contents->selection : "source-default";
  stored.has_selection = true;
  stored.avoid_underwater_lightning = contents->has_avoid_underwater_lightning
                                           ? contents->avoid_underwater_lightning : rerelease;
  stored.has_avoid_underwater_lightning = true;
  stored.owner_pickup_delay = contents->has_owner_pickup_delay ? contents->owner_pickup_delay : 0;
  stored.has_owner_pickup_delay = true;
  stored.shells = fmax(contents->shells, rerelease && weapon != NULL &&
                       (strcmp(weapon, "shotgun") == 0 || strcmp(weapon, "supershotgun") == 0) ? 5 : 0);
  stored.nails = fmax(contents->nails, rerelease && weapon != NULL &&
                      (strcmp(weapon, "nailgun") == 0 || strcmp(weapon, "supernailgun") == 0) ? 20 : 0);
  stored.rockets = fmax(contents->rockets, rerelease && weapon != NULL &&
                        (strcmp(weapon, "rocketlauncher") == 0 || strcmp(weapon, "grenadelauncher") == 0) ? 5 : 0);
  stored.cells = fmax(contents->cells, rerelease && weapon != NULL && strcmp(weapon, "lightning") == 0 ? 15 : 0);
  qa_q1_creatures_backpacks_set(qa_q1_creatures_for(game), pack->actor, &stored);
  qa_bounds bounds = {V3(-16, -16, 0), V3(16, 16, 56)};
  qa_vec3 pack_origin = launch != NULL ? launch->origin : qa_add3(origin, V3(0, 0, -24));
  qa_vec3 velocity = launch != NULL ? launch->velocity
                     : V3(-100 + host_random(game) * 200, -100 + host_random(game) * 200, 300);
  set_body_origin_velocity_bounds(game, pack, pack_origin, velocity, bounds);
  pack->touch = game->named.touch(game->named.ctx, pack, "base:backpack_touch");
  game->schedule(game, pack, 120, game->named.action(game->named.ctx, pack, "SUB_Remove"));
  game->link(game, pack);
  return pack;
}

static double weapon_rank(qa_q1b_game *game, const char *weapon) {
  static const char *const ORDER[] = {"lightning", "rocketlauncher", "supernailgun",
                                      "grenadelauncher", "supershotgun", "nailgun"};
  double base = -1;
  for (size_t i = 0; i < sizeof ORDER / sizeof *ORDER; i++)
    if (strcmp(ORDER[i], weapon) == 0) { base = (double)i; break; }
  if (game->pickup_rules != NULL && game->pickup_rules->has_weapon_rank)
    return game->pickup_rules->weapon_rank(game->pickup_rules->ctx, weapon);
  return base < 0 ? 7 : base + 1;
}
static bool pickup_auto_switch(qa_q1b_game *game, qa_q1b_player_state *player, bool owned) {
  if (game->pickup_rules != NULL && game->pickup_rules->has_auto_switch)
    return game->pickup_rules->auto_switch(game->pickup_rules->ctx, game, player, owned);
  return edition_is(game, "classic") || strcmp(player->auto_switch, "always") == 0 ||
         (strcmp(player->auto_switch, "new") == 0 && !owned);
}
static void backpack_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *pack, qa_actor_id other,
                           bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  const qa_q1_backpack_contents *stored = qa_q1_creatures_backpacks_get(qa_q1_creatures_for(game), pack->actor);
  if (stored == NULL) qa_range_error("Missing source backpack contents");
  qa_q1_backpack_contents contents = *stored;
  const char *weapon = contents.weapon;
  typedef struct ammo_entry { qa_item_id item; double count; } ammo_entry;
  ammo_entry ammo[16];
  size_t ammo_count = 0;
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/shells", contents.shells};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/nails", contents.nails};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/rockets", contents.rockets};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/cells", contents.cells};
  if (contents.has_extra)
    for (size_t i = 0; i < contents.extra_count && ammo_count < 16; i++)
      ammo[ammo_count++] = (ammo_entry){contents.extra[i].item, contents.extra[i].count};
  qa_q1b_player_state *player = game->player(game, other);
  qa_owned_actor actor;
  bool have_actor = game->host->actors_resolve_owned(game->host->ctx, other, &actor);
  if (!have_actor || !game->is_player(game, qa_q1b_some_actor(other)) ||
      game->health(game, other) <= 0 || !game->live(game, pack)) return;
  double owner_delay = contents.has_owner_pickup_delay ? contents.owner_pickup_delay : 0;
  if (pack->owner.has && qa_same_actor(other, pack->owner.id) &&
      pack->next_think - game->time > 120 - owner_delay) return;
  char weapon_item[128];
  bool new_weapon = false;
  if (weapon != NULL) {
    qa_q1b_weapon_item(weapon, weapon_item, sizeof weapon_item);
    bool owns = game->pickup_admission != NULL
                    ? game->pickup_admission->owns(game->pickup_admission->ctx, other, weapon_item)
                    : game->host->inventory_count(game->host->ctx, other, weapon_item) > 0;
    new_weapon = !owns;
  }
  char text[512];
  qa_q1b_message_part parts[24];
  qa_q1b_message_arg args[24];
  qa_q1_backpack_message message = qa_q1_backpack_message_make(
      &contents, game->options.edition, new_weapon, text, sizeof text,
      parts, 24, args, 24);
  const qa_pickup_admission *admission = game->pickup_admission;
  if (admission != NULL) {
    qa_pickup_cargo_entry cargo[17];
    size_t cargo_count = 0;
    for (size_t i = 0; i < ammo_count; i++)
      cargo[cargo_count++] = (qa_pickup_cargo_entry){"counter", ammo[i].item, ammo[i].count};
    qa_pickup_selection selection = QA_PICKUP_SELECTION_NEVER;
    if (weapon != NULL) {
      cargo[cargo_count++] = (qa_pickup_cargo_entry){"weapon", weapon_item, 1};
      bool owned = admission->owns(admission->ctx, other, weapon_item);
      bool auto_switch = player == NULL || pickup_auto_switch(game, player, owned);
      const char *selection_name = contents.has_selection ? contents.selection : "source-default";
      bool always = strcmp(selection_name, "rank") != 0 && edition_is(game, "classic") &&
                    game->options.deathmatch == 0;
      bool avoid = contents.has_avoid_underwater_lightning ? contents.avoid_underwater_lightning
                                                            : edition_is(game, "rerelease");
      bool underwater = !always && player != NULL && avoid && player->water_level != 0 &&
                        strcmp(weapon, "lightning") == 0;
      selection = !auto_switch || underwater ? QA_PICKUP_SELECTION_NEVER
                  : always ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_BETTER;
    }
    (void)admission->cargo(admission->ctx, actor, cargo, cargo_count, selection);
    qa_owned_actor again;
    if (!game->live(game, pack) || !game->host->actors_resolve_owned(game->host->ctx, other, &again) ||
        !qa_q1b_owned_equal(again, actor)) return;
    qa_q1b_event event = {0};
    event.kind = "message"; event.player = qa_q1b_some_actor(other);
    event.text = message.text; event.center = false;
    event.parts = message.part_count > 0 ? message.parts : NULL;
    event.part_count = message.part_count;
    game->host->emit(game->host->ctx, &event);
    qa_q1b_game_sound_owned_ch(game, actor, "weapons/lock4.wav", qa_q1b_channel_named("item"));
    qa_q1b_game_effect_full(game, "pickup", game->body(game, pack).origin, qa_q1b_some_actor(other), 1);
    if (game->live(game, pack)) game->remove(game, pack);
    return;
  }
  if (player == NULL) return;
  bool had_weapon = weapon == NULL || game->host->inventory_count(game->host->ctx, other, weapon_item) > 0;
  for (size_t i = 0; i < ammo_count; i++)
    game->host->inventory_give(game->host->ctx, player->actor, ammo[i].item, ammo[i].count);
  if (weapon != NULL) game->host->inventory_give(game->host->ctx, player->actor, weapon_item, 1);
  const char *selected = weapon != NULL ? weapon : player->weapon;
  if (game->pickup_rules != NULL && game->pickup_rules->weapon_granted != NULL)
    (void)game->pickup_rules->weapon_granted(game->pickup_rules->ctx, game, player, selected);
  {
    qa_q1b_event event = {0};
    event.kind = "message"; event.player = qa_q1b_some_actor(other);
    event.text = message.text; event.center = false;
    event.parts = message.part_count > 0 ? message.parts : NULL;
    event.part_count = message.part_count;
    game->host->emit(game->host->ctx, &event);
    qa_q1b_game_sound_owned_ch(game, actor, "weapons/lock4.wav", qa_q1b_channel_named("item"));
    qa_q1b_game_effect_full(game, "pickup", game->body(game, pack).origin, qa_q1b_some_actor(other), 1);
  }
  if (pickup_auto_switch(game, player, had_weapon)) {
    const char *selection_name = contents.has_selection ? contents.selection : "source-default";
    bool avoid = contents.has_avoid_underwater_lightning ? contents.avoid_underwater_lightning
                                                          : edition_is(game, "rerelease");
    bool source_default = strcmp(selection_name, "rank") != 0 && edition_is(game, "classic") &&
                          game->options.deathmatch == 0;
    bool better = weapon_rank(game, selected) < weapon_rank(game, player->weapon) &&
                  (!avoid || player->water_level == 0 || strcmp(selected, "lightning") != 0);
    if (source_default || better) (void)game->select_weapon(game, player->actor, selected);
  }
  game->remove(game, pack);
}

qa_q1b_actor *qa_q1_create_missile_lifetime(qa_q1b_game *game, qa_q1b_opt_actor owner, const char *classname,
                                            const char *model, qa_vec3 origin, qa_vec3 velocity, double lifetime) {
  qa_q1b_actor *missile = game->create(game, classname);
  missile->owner = owner;
  (void)snprintf(missile->model, sizeof missile->model, "progs/%s.mdl", model);
  missile->solid = "bbox"; missile->movement = "flymissile";
  qa_vec3 angles = V3(-atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / 3.141592653589793,
                      qa_q1b_yaw_for(velocity), 0);
  qa_q1b_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = POINT_BOUNDS;
  patch.has_angles = true; patch.angles = angles;
  game->set_body(game, missile, &patch);
  game->schedule(game, missile, lifetime, game->named.action(game->named.ctx, missile, "SUB_Remove"));
  game->link(game, missile);
  return missile;
}
qa_q1b_actor *qa_q1_create_missile(qa_q1b_game *game, qa_q1b_opt_actor owner, const char *classname,
                                   const char *model, qa_vec3 origin, qa_vec3 velocity) {
  return qa_q1_create_missile_lifetime(game, owner, classname, model, origin, velocity, 5);
}
qa_q1b_actor *qa_q1_launch_spike(qa_q1b_game *game, qa_q1b_opt_actor owner, qa_vec3 origin, qa_vec3 velocity, const char *kind) {
  bool wizard = strcmp(kind, "wizard") == 0, knight = strcmp(kind, "knight") == 0;
  qa_q1b_actor *missile = qa_q1_create_missile_lifetime(
      game, owner, wizard ? "wizard_spike" : knight ? "knight_spike" : kind,
      wizard ? "w_spike" : knight ? "k_spike" : "spike", origin, velocity, 6);
  missile->projectile = strcmp(kind, "superspike") == 0 ? "superspike" : "spike";
  missile->touch = game->named.touch(game->named.ctx, missile, "projectile_touch");
  return missile;
}
qa_q1b_actor *qa_q1_launch_laser(qa_q1b_game *game, qa_q1b_opt_actor owner, qa_vec3 origin, qa_vec3 direction) {
  qa_q1b_actor *missile = qa_q1_create_missile(game, owner, "enforcer_laser", "laser", origin,
                                               qa_scale3(qa_q1b_normalize(direction), 600));
  missile->effects = 8;
  missile->touch = game->named.touch(game->named.ctx, missile, "base:laser_touch");
  return missile;
}
static void laser_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *missile, qa_actor_id other,
                        bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  qa_q1b_opt_actor owner = missile->owner;
  if (owner.has && qa_same_actor(other, owner.id)) return;
  qa_q1b_body body = game->body(game, missile);
  if (strcmp(game->host->contents(game->host->ctx, body.origin), "sky") == 0) { game->remove(game, missile); return; }
  game->sound(game, missile, "enforcer/enfstop.wav", qa_q1b_channel_named("weapon"), 3, 1);
  qa_vec3 hit = qa_sub3(body.origin, qa_scale3(qa_q1b_normalize(body.velocity), 8));
  if (game->health(game, other) != 0) {
    qa_q1b_game_effect_full(game, "blood", hit, qa_q1b_some_actor(other), 15);
    qa_q1b_game_damage(game, other, missile->actor.id, owner, 15);
  } else {
    qa_q1b_game_effect(game, "gunshot", hit);
  }
  game->remove(game, missile);
}
void qa_q1_sprite_explosion(qa_q1b_game *game, qa_q1b_actor *missile) {
  qa_q1b_game_effect(game, "explosion", game->body(game, missile).origin);
  missile->touch = (qa_q1b_touch){0};
  missile->solid = "none"; missile->movement = "none";
  (void)snprintf(missile->model, sizeof missile->model, "progs/s_explod.spr");
  missile->frame = 0;
  qa_q1b_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = ZERO3;
  game->set_body(game, missile, &patch);
  game->link(game, missile);
  game->schedule(game, missile, 0.1, game->named.action(game->named.ctx, missile, "base:explosion_frame"));
}
static void ogre_grenade_explode(qa_q1b_game *game, qa_q1b_actor *missile) {
  game->radius_damage(game, missile->actor.id, missile->owner, 40, QA_Q1B_NO_ACTOR, NULL, "");
  qa_q1b_game_sound(game, missile, "weapons/r_exp3.wav");
  qa_q1_sprite_explosion(game, missile);
}
void qa_q1_launch_ogre_grenade(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_q1b_game_effect_full(game, "muzzleflash", origin, qa_q1b_some_actor(entity->actor.id), 1);
  qa_q1b_game_sound_ch(game, entity, "weapons/grenade.wav", qa_q1b_channel_named("weapon"));
  (void)qa_q1_base_monster_make_vectors(monster);
  qa_vec3 direction = qa_q1b_normalize(qa_sub3(target, origin));
  qa_q1b_actor *missile = qa_q1_create_missile_lifetime(
      game, qa_q1b_some_actor(entity->actor.id), "ogre_grenade", "grenade", origin,
      V3(direction.x * 600, direction.y * 600, 200), 2.5);
  missile->movement = "bounce";
  missile->angular_velocity = V3(300, 300, 300);
  missile->touch = game->named.touch(game->named.ctx, missile, "base:ogre_grenade_touch");
  game->schedule(game, missile, 2.5, game->named.action(game->named.ctx, missile, "base:ogre_grenade_explode"));
}
static void ogre_grenade_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *missile, qa_actor_id other,
                               bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (missile->owner.has && qa_same_actor(other, missile->owner.id)) return;
  qa_q1b_actor *other_entity = game->entity(game, qa_q1b_some_actor(other));
  if (game->is_player(game, qa_q1b_some_actor(other)) ||
      (other_entity != NULL && other_entity->aimed_damage)) { ogre_grenade_explode(game, missile); return; }
  qa_q1b_game_sound(game, missile, "weapons/bounce.wav");
  if (qa_length3(game->body(game, missile).velocity) == 0) missile->angular_velocity = ZERO3;
}
void qa_q1_launch_zombie_grenade(qa_q1_base_monster *monster, qa_vec3 offset) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_q1b_basis axes = game->basis;
  qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster),
      qa_add3(qa_scale3(axes.forward, offset.x),
              qa_add3(qa_scale3(axes.right, offset.y), qa_scale3(axes.up, offset.z - 24))));
  qa_q1b_game_sound_ch(game, entity, "zombie/z_shot1.wav", qa_q1b_channel_named("weapon"));
  (void)qa_q1_base_monster_make_vectors(monster);
  qa_vec3 direction = qa_q1b_normalize(qa_sub3(target, origin));
  qa_q1b_actor *missile = qa_q1_create_missile_lifetime(
      game, qa_q1b_some_actor(entity->actor.id), "zombie_grenade", "zom_gib", origin,
      V3(direction.x * 600, direction.y * 600, 200), 2.5);
  missile->movement = "bounce";
  missile->angular_velocity = V3(3000, 1000, 2000);
  missile->touch = game->named.touch(game->named.ctx, missile, "base:zombie_grenade_touch");
}
static void zombie_grenade_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *missile, qa_actor_id other,
                                 bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (missile->owner.has && qa_same_actor(other, missile->owner.id)) return;
  qa_combat_state combat;
  bool damageable = game->host->combat_read(game->host->ctx, other, &combat) && combat.can_take_damage;
  if (damageable) {
    qa_q1b_game_damage(game, other, missile->actor.id, missile->owner, 10);
    qa_q1b_game_sound_ch(game, missile, "zombie/z_hit.wav", qa_q1b_channel_named("weapon"));
    game->remove(game, missile);
    return;
  }
  qa_q1b_game_sound_ch(game, missile, "zombie/z_miss.wav", qa_q1b_channel_named("weapon"));
  qa_q1b_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = ZERO3;
  game->set_body(game, missile, &patch);
  missile->angular_velocity = ZERO3;
  missile->touch = game->named.touch(game->named.ctx, missile, "base:remove_touch");
}
void qa_q1_launch_vore_ball(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state.enemy.has) return;
  qa_actor_id enemy = monster->state.enemy.id;
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_vec3 direction = qa_q1b_normalize(qa_sub3(qa_add3(target, V3(0, 0, 10)), origin));
  qa_q1b_actor *missile = qa_q1_create_missile(game, qa_q1b_some_actor(entity->actor.id), "vore_ball",
                                               "v_spike", qa_add3(origin, V3(0, 0, 10)),
                                               qa_scale3(direction, 400));
  missile->angular_velocity = V3(300, 300, 300);
  qa_q1b_game_effect_full(game, "muzzleflash", origin, qa_q1b_some_actor(entity->actor.id), 1);
  qa_q1b_game_sound_ch(game, entity, "shalrath/attack2.wav", qa_q1b_channel_named("weapon"));
  qa_q1_creatures_targets_set(qa_q1_creatures_for(game), missile->actor, enemy);
  missile->touch = game->named.touch(game->named.ctx, missile, "base:vore_touch");
  double delay = qa_q1_base_monster_distance(monster) * 0.002;
  game->schedule(game, missile, delay > 0.1 ? delay : 0.1,
                 game->named.action(game->named.ctx, missile, "base:vore_home"));
}
static void vore_home(void *ctx, qa_q1b_game *game, qa_q1b_actor *missile) {
  (void)ctx;
  qa_actor_id enemy;
  if (!qa_q1_creatures_targets_get(qa_q1_creatures_for(game), missile->actor, &enemy))
    qa_range_error("Vore missile has no source enemy");
  qa_q1b_body body;
  if (!game->host->bodies_read(game->host->ctx, enemy, &body) || game->health(game, enemy) < 1) {
    game->remove(game, missile);
    return;
  }
  double speed = edition_is(game, "classic") && game->options.skill == 3 ? 350 : 250;
  qa_vec3 velocity = qa_scale3(qa_q1b_normalize(qa_sub3(qa_add3(body.origin, V3(0, 0, 10)),
                                                        game->body(game, missile).origin)), speed);
  qa_q1b_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = velocity;
  game->set_body(game, missile, &patch);
  game->schedule(game, missile, 0.2, game->named.action(game->named.ctx, missile, "base:vore_home"));
}
static void vore_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *missile, qa_actor_id other,
                       bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)has_normal; (void)normal; (void)surface;
  if (missile->owner.has && qa_same_actor(other, missile->owner.id)) return;
  if (strcmp(game->host->classname(game->host->ctx, other), "monster_zombie") == 0)
    qa_q1b_game_damage(game, other, missile->actor.id, qa_q1b_some_actor(missile->actor.id), 110);
  game->radius_damage(game, missile->actor.id, missile->owner, 40, QA_Q1B_NO_ACTOR, NULL, "");
  qa_q1b_game_sound_ch(game, missile, "weapons/r_exp3.wav", qa_q1b_channel_named("weapon"));
  qa_q1_sprite_explosion(game, missile);
}
static void remove_touch(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other,
                         bool has_normal, qa_vec3 normal, const void *surface) {
  (void)ctx; (void)other; (void)has_normal; (void)normal; (void)surface;
  game->remove(game, entity);
}
static void ogre_grenade_explode_cb(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)ctx;
  ogre_grenade_explode(game, entity);
}
static void explosion_frame(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity) {
  (void)ctx;
  entity->frame++;
  if (entity->frame >= 6) game->remove(game, entity);
  else game->schedule(game, entity, 0.1, game->named.action(game->named.ctx, entity, "base:explosion_frame"));
}
void qa_q1_register_projectile_callbacks(qa_q1b_game *game) {
  qa_q1b_handlers handlers;
  handlers = (qa_q1b_handlers){.touch = backpack_touch};
  game->named.register_named(game->named.ctx, "base:backpack_touch", &handlers);
  handlers = (qa_q1b_handlers){.touch = laser_touch};
  game->named.register_named(game->named.ctx, "base:laser_touch", &handlers);
  handlers = (qa_q1b_handlers){.touch = ogre_grenade_touch};
  game->named.register_named(game->named.ctx, "base:ogre_grenade_touch", &handlers);
  handlers = (qa_q1b_handlers){.action = ogre_grenade_explode_cb};
  game->named.register_named(game->named.ctx, "base:ogre_grenade_explode", &handlers);
  handlers = (qa_q1b_handlers){.touch = zombie_grenade_touch};
  game->named.register_named(game->named.ctx, "base:zombie_grenade_touch", &handlers);
  handlers = (qa_q1b_handlers){.touch = remove_touch};
  game->named.register_named(game->named.ctx, "base:remove_touch", &handlers);
  handlers = (qa_q1b_handlers){.touch = vore_touch};
  game->named.register_named(game->named.ctx, "base:vore_touch", &handlers);
  handlers = (qa_q1b_handlers){.action = vore_home};
  game->named.register_named(game->named.ctx, "base:vore_home", &handlers);
  handlers = (qa_q1b_handlers){.action = explosion_frame};
  game->named.register_named(game->named.ctx, "base:explosion_frame", &handlers);
}
void qa_q1_cast_lightning(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_q1_base_monster_face(monster);
  qa_vec3 m_origin = qa_q1_base_monster_origin(monster);
  qa_q1b_game_effect_full(game, "muzzleflash", m_origin, qa_q1b_some_actor(entity->actor.id), 1);
  monster->lightning_count++;
  qa_vec3 origin = qa_add3(m_origin, V3(0, 0, 40));
  qa_vec3 direction = qa_q1b_normalize(qa_sub3(qa_add3(target, V3(0, 0, 16)), origin));
  qa_q1b_trace_request wall_request = {origin, qa_add3(m_origin, qa_scale3(direction, 600)), POINT_BOUNDS,
                                       qa_q1b_some_actor(entity->actor.id), false, false};
  qa_q1b_trace wall = game->host->trace(game->host->ctx, &wall_request);
  qa_q1b_event beam = {0};
  beam.kind = "beam"; beam.beam_style = "lightning1";
  beam.actor = qa_q1b_some_actor(entity->actor.id);
  beam.start = origin; beam.end = wall.end;
  game->host->emit(game->host->ctx, &beam);
  qa_vec3 delta = qa_sub3(wall.end, origin);
  qa_vec3 side = V3(-delta.y * 16, -delta.y * 16, 0);
  qa_vec3 offsets[3] = {ZERO3, side, qa_scale3(side, -1)};
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1b_trace_request request = {qa_add3(origin, offsets[i]), qa_add3(wall.end, offsets[i]),
                                    POINT_BOUNDS, qa_q1b_some_actor(entity->actor.id), true, false};
    qa_q1b_trace trace = game->host->trace(game->host->ctx, &request);
    if (!trace.actor.has) continue;
    qa_combat_state combat;
    if (!game->host->combat_read(game->host->ctx, trace.actor.id, &combat) || !combat.can_take_damage) continue;
    bool duplicate = false;
    for (size_t h = 0; h < hit_count; h++) if (qa_same_actor(hit[h], trace.actor.id)) duplicate = true;
    if (duplicate) continue;
    hit[hit_count++] = trace.actor.id;
    qa_q1b_game_damage(game, trace.actor.id, entity->actor.id, qa_q1b_some_actor(entity->actor.id), 10);
    qa_q1b_game_effect_full(game, "blood", trace.end, trace.actor, 40);
  }
}

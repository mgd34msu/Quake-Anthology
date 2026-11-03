/* Port of src/content/q1/base/projectiles.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/projectiles.h"

#include "base_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/creatures.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_vec3 ZERO3 = {0, 0, 0};
static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static double host_random(qa_q1_services *game) { return game->host.random(game->host.ctx); }
static bool edition_is(qa_q1_services *game, const char *edition) {
  return strcmp(game->options.edition, edition) == 0;
}
static void set_body_origin_velocity_bounds(qa_q1_services *game, qa_q1_actor *entity,
                                            qa_vec3 origin, qa_vec3 velocity, qa_bounds bounds) {
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = bounds;
  qa_q1_set_body(game, entity, &patch);
}

static qa_vec3 damage_velocity(qa_q1_services *game, double damage) {
  qa_vec3 base; /* TS literal order: x, y, z draws */
  base.x = 100 * (host_random(game) * 2 - 1);
  base.y = 100 * (host_random(game) * 2 - 1);
  base.z = 200 + 100 * host_random(game);
  return qa_scale3(base, damage > -50 ? 0.7 : damage > -200 ? 2 : 10);
}
qa_q1_actor *qa_q1_throw_gib(qa_q1_services *game, qa_vec3 origin, const char *model, double damage) {
  qa_q1_actor *gib = qa_q1_create(game, "gib", NULL, NULL);
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/%s.mdl", model); q1base_set_text(&gib->model, model_text); }
  gib->movement = "bounce";
  set_body_origin_velocity_bounds(game, gib, origin, damage_velocity(game, damage), POINT_BOUNDS);
  gib->angular_velocity.x = host_random(game) * 600; /* TS literal order */
  gib->angular_velocity.y = host_random(game) * 600;
  gib->angular_velocity.z = host_random(game) * 600;
  double delay = 10 + host_random(game) * 10;
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, gib, "SUB_Remove");
  qa_q1_schedule(game, gib, delay, callback);
  qa_q1_link(game, gib);
  return gib;
}
void qa_q1_throw_head_damage(qa_q1_services *game, qa_q1_actor *entity, const char *model, double damage) {
  qa_q1_cancel(game, entity);
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/%s.mdl", model); q1base_set_text(&entity->model, model_text); }
  entity->frame = 0; entity->movement = "bounce";
  qa_q1_actor_set_damageable(entity, false);
  entity->solid = "none";
  qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_bounds bounds = {V3(-16, -16, 0), V3(16, 16, 56)};
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_add3(origin, V3(0, 0, -24));
  patch.has_velocity = true; patch.velocity = damage_velocity(game, damage);
  patch.has_bounds = true; patch.bounds = bounds;
  patch.has_ground = true; patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
  entity->angular_velocity = V3(0, (host_random(game) * 2 - 1) * 600, 0);
  qa_q1_link(game, entity);
}
void qa_q1_throw_head(qa_q1_services *game, qa_q1_actor *entity, const char *model) {
  qa_q1_throw_head_damage(game, entity, model, qa_q1_health(game, entity->actor.id));
}
qa_q1_actor *qa_q1_spawn_meat_spray(qa_q1_services *game, qa_q1_actor *owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1_actor *missile = qa_q1_create(game, "meat_spray", NULL, NULL);
  qa_vec3 angles = qa_q1_get_body(game, owner).angles;
  missile->has_owner = true;
  missile->owner = owner->actor.id;
  missile->movement = "bounce"; missile->solid = "none";
  if (edition_is(game, "rerelease")) angles.x = -angles.x;
  (void)qa_q1_make_vectors(game, angles);
  qa_vec3 adjusted = velocity;
  adjusted.z = velocity.z + 250 + 50 * host_random(game);
  set_body_origin_velocity_bounds(game, missile, origin, adjusted, POINT_BOUNDS);
  missile->angular_velocity = V3(3000, 1000, 2000);
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/zom_gib.mdl"); q1base_set_text(&missile->model, model_text); }
  qa_q1_schedule(game, missile, 1, qa_q1_callback_registry_action(game->named, missile, "SUB_Remove"));
  qa_q1_link(game, missile);
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
static void *backpack_alloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
/* Growable text for the classic message (TS string concatenation). */
typedef struct backpack_text { char *data; size_t length, capacity; } backpack_text;
static void backpack_text_append(backpack_text *text, const char *value) {
  size_t count = strlen(value);
  if (text->length + count + 1 > text->capacity) {
    size_t capacity = text->capacity == 0 ? 64 : text->capacity;
    while (text->length + count + 1 > capacity) capacity *= 2;
    char *grown = realloc(text->data, capacity);
    if (grown == NULL) qa_range_error("out of memory");
    text->data = grown;
    text->capacity = capacity;
  }
  memcpy(text->data + text->length, value, count + 1);
  text->length += count;
}
void qa_q1_backpack_message_free(qa_q1_backpack_message *message) {
  free(message->owned_text);
  free(message->owned_parts);
  free(message->owned_args);
  memset(message, 0, sizeof *message);
}
qa_q1_backpack_message qa_q1_backpack_message_make(const qa_q1_backpack_contents *contents,
                                                   const char *edition, bool new_weapon) {
  /* TS entries: the four counters, then every recognised extra (no limit). */
  size_t extra_count = contents->has_extra ? contents->extra_count : 0;
  ammo_name *entries = backpack_alloc((4 + extra_count) * sizeof *entries);
  size_t entry_count = 0;
  entries[entry_count++] = (ammo_name){"shells", "$qc_backpack_shells", contents->shells};
  entries[entry_count++] = (ammo_name){"nails", "$qc_backpack_nails", contents->nails};
  entries[entry_count++] = (ammo_name){"rockets", "$qc_backpack_rockets", contents->rockets};
  entries[entry_count++] = (ammo_name){"cells", "$qc_backpack_cells", contents->cells};
  for (size_t i = 0; i < extra_count; i++) {
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
    backpack_text text = {NULL, 0, 0};
    backpack_text_append(&text, "You get ");
    bool first = true;
    if (weapon != NULL) {
      backpack_text_append(&text, "the ");
      backpack_text_append(&text, weapon->classic);
      first = false;
    }
    for (size_t i = 0; i < entry_count; i++) {
      if (entries[i].count <= 0) continue;
      char count[32];
      qa_net_js_number(entries[i].count, count, sizeof count);
      if (!first) backpack_text_append(&text, ", ");
      backpack_text_append(&text, count);
      backpack_text_append(&text, " ");
      backpack_text_append(&text, entries[i].classic);
      first = false;
    }
    free(entries);
    result.owned_text = text.data;
    result.text = text.data;
    result.parts = NULL;
    result.part_count = 0;
    return result;
  }
  /* One leading part, the weapon, and per entry a separator plus the entry. */
  qa_q1_message_part *parts = backpack_alloc((2 + 2 * entry_count) * sizeof *parts);
  qa_q1_msg_arg *args = backpack_alloc(entry_count * sizeof *args);
  size_t part_count = 0, arg_count = 0;
  parts[part_count++] = (qa_q1_message_part){"$qc_backpack_got", NULL, 0};
  bool first_item = true;
  if (weapon != NULL) {
    parts[part_count++] = (qa_q1_message_part){weapon->localized, NULL, 0};
    first_item = false;
  }
  for (size_t i = 0; i < entry_count; i++) {
    if (entries[i].count <= 0) continue;
    if (!first_item) parts[part_count++] = (qa_q1_message_part){", ", NULL, 0};
    first_item = false;
    args[arg_count] = (qa_q1_msg_arg){true, entries[i].count, NULL};
    parts[part_count++] = (qa_q1_message_part){entries[i].localized, &args[arg_count], 1};
    arg_count++;
  }
  free(entries);
  result.text = "$qc_backpack_got";
  result.owned_parts = parts;
  result.owned_args = args;
  result.parts = parts;
  result.part_count = part_count;
  return result;
}

qa_q1_actor *qa_q1_drop_backpack(qa_q1_services *game, qa_vec3 origin,
                                  const qa_q1_backpack_contents *contents,
                                  const qa_q1_backpack_launch *launch) {
  double extra_total = 0;
  if (contents->has_extra)
    for (size_t i = 0; i < contents->extra_count; i++) extra_total += contents->extra[i].count;
  if (contents->shells + contents->nails + contents->rockets + contents->cells + extra_total == 0) return NULL;
  qa_q1_actor *pack = qa_q1_create(game, "item_backpack", NULL, NULL);
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/backpack.mdl"); q1base_set_text(&pack->model, model_text); }
  pack->solid = "trigger";
  pack->movement = launch != NULL ? launch->movement : "toss";
  pack->movement_flags = qa_js_to_int32(pack->movement_flags) | 256;
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
  stored.shells = qa_js_max(contents->shells, rerelease && weapon != NULL &&
                       (strcmp(weapon, "shotgun") == 0 || strcmp(weapon, "supershotgun") == 0) ? 5 : 0);
  stored.nails = qa_js_max(contents->nails, rerelease && weapon != NULL &&
                      (strcmp(weapon, "nailgun") == 0 || strcmp(weapon, "supernailgun") == 0) ? 20 : 0);
  stored.rockets = qa_js_max(contents->rockets, rerelease && weapon != NULL &&
                        (strcmp(weapon, "rocketlauncher") == 0 || strcmp(weapon, "grenadelauncher") == 0) ? 5 : 0);
  stored.cells = qa_js_max(contents->cells, rerelease && weapon != NULL && strcmp(weapon, "lightning") == 0 ? 15 : 0);
  qa_q1_creatures_backpacks_set(qa_q1_creatures_for(game), pack->actor, &stored);
  qa_bounds bounds = {V3(-16, -16, 0), V3(16, 16, 56)};
  qa_vec3 pack_origin = launch != NULL ? launch->origin : qa_add3(origin, V3(0, 0, -24));
  qa_vec3 velocity;
  if (launch != NULL) {
    velocity = launch->velocity;
  } else { /* TS literal order: x then y draws */
    velocity.x = -100 + host_random(game) * 200;
    velocity.y = -100 + host_random(game) * 200;
    velocity.z = 300;
  }
  set_body_origin_velocity_bounds(game, pack, pack_origin, velocity, bounds);
  pack->touch = qa_q1_callback_registry_touch(game->named, pack, "base:backpack_touch");
  qa_q1_schedule(game, pack, 120, qa_q1_callback_registry_action(game->named, pack, "SUB_Remove"));
  qa_q1_link(game, pack);
  return pack;
}

static double weapon_rank(qa_q1_services *game, const char *weapon) {
  static const char *const ORDER[] = {"lightning", "rocketlauncher", "supernailgun",
                                      "grenadelauncher", "supershotgun", "nailgun"};
  double base = -1;
  for (size_t i = 0; i < sizeof ORDER / sizeof *ORDER; i++)
    if (strcmp(ORDER[i], weapon) == 0) { base = (double)i; break; }
  if (game->pickup_rules != NULL && game->pickup_rules->weapon_rank != NULL)
    return game->pickup_rules->weapon_rank(game->pickup_rules->ctx, weapon);
  return base < 0 ? 7 : base + 1;
}
static bool pickup_auto_switch(qa_q1_services *game, qa_q1_player_state *player, bool owned) {
  if (game->pickup_rules != NULL && game->pickup_rules->auto_switch != NULL)
    return game->pickup_rules->auto_switch(game->pickup_rules->ctx, game, player, owned);
  return edition_is(game, "classic") || strcmp(player->auto_switch, "always") == 0 ||
         (strcmp(player->auto_switch, "new") == 0 && !owned);
}
static const char *stable_weapon(qa_q1_services *game, const char *weapon) {
  if (weapon == NULL) return NULL;
  for (size_t i = 0; i < sizeof QA_Q1_WEAPON_IDS / sizeof *QA_Q1_WEAPON_IDS; i++)
    if (strcmp(QA_Q1_WEAPON_IDS[i], weapon) == 0) return QA_Q1_WEAPON_IDS[i];
  for (size_t i = 0; i < game->registered_weapon_count; i++)
    if (strcmp(game->registered_weapons[i].def->id, weapon) == 0) return game->registered_weapons[i].def->id;
  return weapon; /* not a known id: the stored string (caller-owned contents) */
}
typedef struct touch_scratch {
  void *ammo;
  qa_pickup_cargo_entry *cargo;
  qa_q1_backpack_message message;
} touch_scratch;
static void free_touch_scratch(void *arg) {
  touch_scratch *scratch = arg;
  free(scratch->ammo);
  free(scratch->cargo);
  qa_q1_backpack_message_free(&scratch->message);
  scratch->ammo = NULL;
  scratch->cargo = NULL;
}
static void touch_done(qa_cleanup *node, touch_scratch *scratch) {
  qa_cleanup_pop(node);
  free_touch_scratch(scratch);
}
static void backpack_touch(void *ctx, qa_q1_services *game, qa_q1_actor *pack, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const qa_q1_backpack_contents *stored = qa_q1_creatures_backpacks_get(qa_q1_creatures_for(game), pack->actor);
  if (stored == NULL) qa_range_error("Missing source backpack contents");
  qa_q1_backpack_contents contents = *stored;
  /* The stored contents own their weapon string and are released with the
   * pack; the weapon the player keeps must be the interned id (TS strings
   * are values). */
  const char *weapon = stable_weapon(game, contents.weapon);
  typedef struct ammo_entry { qa_item_id item; double count; } ammo_entry;
  /* TS ammo list: the four counters and every extra (no limit). The scratch is
   * released on every exit, including a raise out of the grant (TS GC). */
  size_t extra_count = contents.has_extra ? contents.extra_count : 0;
  touch_scratch scratch = {NULL, NULL, {0}};
  qa_cleanup scratch_node;
  qa_cleanup_push(&scratch_node, free_touch_scratch, &scratch);
  ammo_entry *ammo = scratch.ammo = backpack_alloc((4 + extra_count) * sizeof *ammo);
  size_t ammo_count = 0;
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/shells", contents.shells};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/nails", contents.nails};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/rockets", contents.rockets};
  ammo[ammo_count++] = (ammo_entry){"q1:ammo/cells", contents.cells};
  for (size_t i = 0; i < extra_count; i++)
    ammo[ammo_count++] = (ammo_entry){contents.extra[i].item, contents.extra[i].count};
  qa_q1_player_state *player = qa_q1_player(game, other);
  qa_owned_actor actor;
  bool have_actor = qa_session_actor_registry_resolve_owned(game->host.actors, other, &actor);
  if (!have_actor || !qa_q1_is_player(game, &other) ||
      qa_q1_health(game, other) <= 0 || !qa_q1_live(game, pack)) { touch_done(&scratch_node, &scratch); return; }
  double owner_delay = contents.has_owner_pickup_delay ? contents.owner_pickup_delay : 0;
  if (pack->has_owner && qa_same_actor(other, pack->owner) &&
      pack->next_think - game->time > 120 - owner_delay) { touch_done(&scratch_node, &scratch); return; }
  char weapon_item[128] = "";
  bool new_weapon = false;
  if (weapon != NULL) {
    (void)snprintf(weapon_item, sizeof weapon_item, "q1:weapon/%s", weapon); /* TS weaponItem(weapon) */
    bool owns = game->pickup_admission != NULL
                    ? game->pickup_admission->owns(game->pickup_admission->ctx, other, weapon_item)
                    : qa_shared_inventory_table_count(game->host.inventory, other, weapon_item) > 0;
    new_weapon = !owns;
  }
  scratch.message = qa_q1_backpack_message_make(&contents, game->options.edition, new_weapon);
  const qa_q1_backpack_message message = scratch.message;
  const qa_pickup_admission *admission = game->pickup_admission;
  if (admission != NULL) {
    qa_pickup_cargo_entry *cargo = scratch.cargo = backpack_alloc((ammo_count + 1) * sizeof *cargo);
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
    if (!qa_q1_live(game, pack) || !qa_session_actor_registry_resolve_owned(game->host.actors, other, &again) ||
        !qa_same_owned_actor(again, actor)) { touch_done(&scratch_node, &scratch); return; }
    qa_q1_event event = {0};
    event.kind = "message"; event.has_player = true; event.player = other;
    event.text = message.text; event.center = false;
    event.parts = message.part_count > 0 ? message.parts : NULL;
    event.part_count = message.part_count;
    qa_q1_emit(game, event);
    qa_q1_sound(game, actor.id, "weapons/lock4.wav", "item", 1, 1);
    qa_q1_effect(game, "pickup", qa_q1_get_body(game, pack).origin, &other, 1);
    if (qa_q1_live(game, pack)) qa_q1_remove(game, pack);
    touch_done(&scratch_node, &scratch);
    return;
  }
  if (player == NULL) { touch_done(&scratch_node, &scratch); return; }
  bool had_weapon = weapon == NULL || qa_shared_inventory_table_count(game->host.inventory, other, weapon_item) > 0;
  for (size_t i = 0; i < ammo_count; i++)
    qa_shared_inventory_table_give(game->host.inventory, player->actor, ammo[i].item, ammo[i].count);
  if (weapon != NULL) qa_shared_inventory_table_give(game->host.inventory, player->actor, weapon_item, 1);
  const char *selected = weapon != NULL ? weapon : player->weapon;
  if (game->pickup_rules != NULL && game->pickup_rules->weapon_granted != NULL)
    (void)game->pickup_rules->weapon_granted(game->pickup_rules->ctx, game, player, selected);
  {
    qa_q1_event event = {0};
    event.kind = "message"; event.has_player = true; event.player = other;
    event.text = message.text; event.center = false;
    event.parts = message.part_count > 0 ? message.parts : NULL;
    event.part_count = message.part_count;
    qa_q1_emit(game, event);
    qa_q1_sound(game, actor.id, "weapons/lock4.wav", "item", 1, 1);
    qa_q1_effect(game, "pickup", qa_q1_get_body(game, pack).origin, &other, 1);
  }
  if (pickup_auto_switch(game, player, had_weapon)) {
    const char *selection_name = contents.has_selection ? contents.selection : "source-default";
    bool avoid = contents.has_avoid_underwater_lightning ? contents.avoid_underwater_lightning
                                                          : edition_is(game, "rerelease");
    bool source_default = strcmp(selection_name, "rank") != 0 && edition_is(game, "classic") &&
                          game->options.deathmatch == 0;
    double left = weapon_rank(game, selected);
    double right = weapon_rank(game, player->weapon);
    bool better = left < right &&
                  (!avoid || player->water_level == 0 || strcmp(selected, "lightning") != 0);
    if (source_default || better) (void)qa_q1_select_weapon(game, player->actor, selected);
  }
  qa_q1_remove(game, pack);
  touch_done(&scratch_node, &scratch);
}

qa_q1_actor *qa_q1_create_missile_lifetime(qa_q1_services *game, const qa_actor_id *owner, const char *classname, const char *model, qa_vec3 origin, qa_vec3 velocity, double lifetime) {
  qa_q1_actor *missile = qa_q1_create(game, classname, NULL, NULL);
  missile->has_owner = owner != NULL;
  if (owner != NULL) missile->owner = *owner;
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/%s.mdl", model); q1base_set_text(&missile->model, model_text); }
  missile->solid = "bbox"; missile->movement = "flymissile";
  qa_vec3 angles = V3(-atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / 3.141592653589793,
                      qa_q1_yaw_for(velocity), 0);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = POINT_BOUNDS;
  patch.has_angles = true; patch.angles = angles;
  qa_q1_set_body(game, missile, &patch);
  qa_q1_schedule(game, missile, lifetime, qa_q1_callback_registry_action(game->named, missile, "SUB_Remove"));
  qa_q1_link(game, missile);
  return missile;
}
qa_q1_actor *qa_q1_create_missile(qa_q1_services *game, const qa_actor_id *owner, const char *classname, const char *model, qa_vec3 origin, qa_vec3 velocity) {
  return qa_q1_create_missile_lifetime(game, owner, classname, model, origin, velocity, 5);
}
qa_q1_actor *qa_q1_launch_spike(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 velocity, const char *kind) {
  const bool wizard2 = strcmp(kind, "wizard2") == 0;
  const bool wizard = strcmp(kind, "wizard") == 0 || wizard2, knight = strcmp(kind, "knight") == 0;
  qa_q1_actor *missile = qa_q1_create_missile_lifetime(game, owner, wizard ? "wizard_spike" : knight ? "knight_spike" : kind,
                                                       wizard2 ? "w_spike2" : wizard ? "w_spike" : knight ? "k_spike" : "spike",
                                                       origin, velocity, 6);
  missile->projectile = strcmp(kind, "superspike") == 0 ? "superspike" : "spike";
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "projectile_touch");
  return missile;
}
qa_q1_actor *qa_q1_launch_laser(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 direction) {
  qa_q1_actor *missile = qa_q1_create_missile(game, owner, "enforcer_laser", "laser", origin, qa_scale3(qa_q1_normalize(direction), 600));
  missile->effects = 8;
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:laser_touch");
  return missile;
}
/* mg3 LaunchLaserAlt: the Bloody Nightmare second bolt has no glow and no impact sound. */
qa_q1_actor *qa_q1_launch_laser_alt(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 direction) {
  qa_q1_actor *missile = qa_q1_create_missile(game, owner, "enforcer_laser", "laser", origin, qa_scale3(qa_q1_normalize(direction), 600));
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:laser_alt_touch");
  return missile;
}
static void laser_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const qa_actor_id *owner = missile->has_owner ? &missile->owner : NULL;
  if ((owner != NULL) && qa_same_actor(other, (*owner))) return;
  qa_q1_body body = qa_q1_get_body(game, missile);
  if (strcmp(game->host.contents(game->host.ctx, body.origin), "sky") == 0) { qa_q1_remove(game, missile); return; }
  qa_q1_sound(game, (missile)->actor.id, "enforcer/enfstop.wav", "weapon", 3, 1);
  qa_vec3 hit = qa_sub3(body.origin, qa_scale3(qa_q1_normalize(body.velocity), 8));
  if (qa_q1_health(game, other) != 0) {
    qa_q1_effect(game, "blood", hit, &other, 15);
    (void)qa_q1_damage(game, other, missile->actor.id, owner, 15, NULL, "direct", "", NULL);
  } else {
    qa_q1_effect(game, "gunshot", hit, NULL, 1);
  }
  qa_q1_remove(game, missile);
}
static void laser_alt_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                            const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  const qa_actor_id *owner = missile->has_owner ? &missile->owner : NULL;
  if (owner != NULL && qa_same_actor(other, *owner)) return;
  qa_q1_body body = qa_q1_get_body(game, missile);
  if (strcmp(game->host.contents(game->host.ctx, body.origin), "sky") == 0) { qa_q1_remove(game, missile); return; }
  qa_vec3 hit = qa_sub3(body.origin, qa_scale3(qa_q1_normalize(body.velocity), 8));
  if (qa_q1_health(game, other) != 0) {
    qa_q1_effect(game, "blood", hit, &other, 15);
    (void)qa_q1_damage(game, other, missile->actor.id, owner, 15, NULL, "direct", "", NULL);
  } else {
    qa_q1_effect(game, "gunshot", hit, NULL, 1);
  }
  qa_q1_remove(game, missile);
}
void qa_q1_sprite_explosion(qa_q1_services *game, qa_q1_actor *missile) {
  qa_q1_sprite_explosion_plain_effect(game, missile, true);
}
/* TS spriteExplosion(game, missile, plainEffect = true). */
void qa_q1_sprite_explosion_plain_effect(qa_q1_services *game, qa_q1_actor *missile, bool plain_effect) {
  if (plain_effect) qa_q1_effect(game, "explosion", qa_q1_get_body(game, missile).origin, NULL, 1);
  missile->touch = (qa_q1_bound_touch){0};
  missile->solid = "none"; missile->movement = "none";
  { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/s_explod.spr"); q1base_set_text(&missile->model, model_text); }
  missile->frame = 0;
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = ZERO3;
  qa_q1_set_body(game, missile, &patch);
  qa_q1_link(game, missile);
  qa_q1_schedule(game, missile, 0.1, qa_q1_callback_registry_action(game->named, missile, "base:explosion_frame"));
}
static void ogre_grenade_explode(qa_q1_services *game, qa_q1_actor *missile) {
  qa_q1_radius_damage(game, missile->actor.id, (missile->has_owner ? &missile->owner : NULL), 40, NULL, NULL, "");
  qa_q1_sound_defaults(game, missile->actor.id, "weapons/r_exp3.wav");
  qa_q1_sprite_explosion(game, missile);
}
void qa_q1_launch_ogre_grenade(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_q1_effect(game, "muzzleflash", origin, &entity->actor.id, 1);
  qa_q1_sound(game, entity->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  const qa_q1_basis axes = qa_q1_base_monster_make_vectors(monster);
  qa_vec3 direction = qa_q1_normalize(qa_sub3(target, origin));
  qa_vec3 velocity = V3(direction.x * 600, direction.y * 600, 200);
  qa_q1_body enemy_body;
  const bool has_enemy_body = monster->state->has_enemy && q1base_read_body(game, monster->state->enemy, &enemy_body);
  if (game->source_bloody_nightmare.fn != NULL && game->source_bloody_nightmare.fn(game->source_bloody_nightmare.ctx) &&
      qa_q1_actor_number(entity, "projectiles") == 1 && has_enemy_body) {
    const double boost = enemy_body.origin.z - origin.z;
    const double distance = qa_js_hypot2(enemy_body.origin.x - origin.x, enemy_body.origin.y - origin.y);
    if (boost > 30 || distance > 500) velocity.z = 300 + (host_random(game) * 2 - 1) * 50;
    else if (boost < -30 || distance < 100) velocity.z = 100 + (host_random(game) * 2 - 1) * 25;
    velocity = qa_add3(velocity, qa_scale3(axes.right, (host_random(game) * 2 - 1) * 64));
  }
  qa_q1_actor *missile = qa_q1_create_missile_lifetime(game, &entity->actor.id, "ogre_grenade", "grenade", origin, velocity, 2.5);
  missile->movement = "bounce";
  missile->angular_velocity = V3(300, 300, 300);
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:ogre_grenade_touch");
  qa_q1_schedule(game, missile, 2.5, qa_q1_callback_registry_action(game->named, missile, "base:ogre_grenade_explode"));
}
static void ogre_grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                               const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (missile->has_owner && qa_same_actor(other, missile->owner)) return;
  qa_q1_actor *other_entity = qa_q1_get_entity(game, &other);
  if (qa_q1_is_player(game, &other) ||
      (other_entity != NULL && other_entity->aimed_damage)) { ogre_grenade_explode(game, missile); return; }
  qa_q1_sound_defaults(game, missile->actor.id, "weapons/bounce.wav");
  if (qa_length3(qa_q1_get_body(game, missile).velocity) == 0) missile->angular_velocity = ZERO3;
}
void qa_q1_launch_zombie_grenade(qa_q1_base_monster *monster, qa_vec3 offset) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_q1_basis axes = game->basis;
  qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster),
      qa_add3(qa_scale3(axes.forward, offset.x),
              qa_add3(qa_scale3(axes.right, offset.y), qa_scale3(axes.up, offset.z - 24))));
  qa_q1_sound(game, entity->actor.id, "zombie/z_shot1.wav", "weapon", 1, 1);
  (void)qa_q1_base_monster_make_vectors(monster);
  qa_vec3 direction = qa_q1_normalize(qa_sub3(target, origin));
  qa_q1_actor *missile = qa_q1_create_missile_lifetime(game, &entity->actor.id, "zombie_grenade", "zom_gib", origin, V3(direction.x * 600, direction.y * 600, 200), 2.5);
  missile->movement = "bounce";
  missile->angular_velocity = V3(3000, 1000, 2000);
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:zombie_grenade_touch");
}
static void zombie_grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                                 const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (missile->has_owner && qa_same_actor(other, missile->owner)) return;
  qa_combat_state combat;
  bool damageable = q1base_read_combat(game, other, &combat) && combat.can_take_damage;
  if (damageable) {
    (void)qa_q1_damage(game, other, missile->actor.id, (missile->has_owner ? &missile->owner : NULL), 10, NULL, "direct", "", NULL);
    qa_q1_sound(game, missile->actor.id, "zombie/z_hit.wav", "weapon", 1, 1);
    qa_q1_remove(game, missile);
    return;
  }
  qa_q1_sound(game, missile->actor.id, "zombie/z_miss.wav", "weapon", 1, 1);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = ZERO3;
  qa_q1_set_body(game, missile, &patch);
  missile->angular_velocity = ZERO3;
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:remove_touch");
}
void qa_q1_launch_vore_ball(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state->has_enemy) return;
  qa_actor_id enemy = monster->state->enemy;
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_vec3 direction = qa_q1_normalize(qa_sub3(qa_add3(target, V3(0, 0, 10)), origin));
  qa_q1_actor *missile = qa_q1_create_missile(game, &entity->actor.id, "vore_ball", "v_spike", qa_add3(origin, V3(0, 0, 10)), qa_scale3(direction, 400));
  missile->angular_velocity = V3(300, 300, 300);
  qa_q1_effect(game, "muzzleflash", origin, &entity->actor.id, 1);
  qa_q1_sound(game, entity->actor.id, "shalrath/attack2.wav", "weapon", 1, 1);
  qa_q1_creatures_targets_set(qa_q1_creatures_for(game), missile->actor, enemy);
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:vore_touch");
  double delay = qa_q1_base_monster_distance(monster) * 0.002;
  qa_q1_schedule(game, missile, delay > 0.1 ? delay : 0.1,
                 qa_q1_callback_registry_action(game->named, missile, "base:vore_home"));
}
static void vore_home(void *ctx, qa_q1_services *game, qa_q1_actor *missile) {
  (void)ctx;
  qa_actor_id enemy;
  if (!qa_q1_creatures_targets_get(qa_q1_creatures_for(game), missile->actor, &enemy))
    qa_range_error("Vore missile has no source enemy");
  qa_q1_body body;
  if (!q1base_read_body(game, enemy, &body) || qa_q1_health(game, enemy) < 1) {
    qa_q1_remove(game, missile);
    return;
  }
  double speed = edition_is(game, "classic") && qa_q1_option_skill(game) == 3 ? 350 : 250;
  qa_vec3 velocity = qa_scale3(qa_q1_normalize(qa_sub3(qa_add3(body.origin, V3(0, 0, 10)),
                                                        qa_q1_get_body(game, missile).origin)), speed);
  qa_q1_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = velocity;
  qa_q1_set_body(game, missile, &patch);
  qa_q1_schedule(game, missile, 0.2, qa_q1_callback_registry_action(game->named, missile, "base:vore_home"));
}
static void vore_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (missile->has_owner && qa_same_actor(other, missile->owner)) return;
  if (strcmp(game->host.classname(game->host.ctx, other), "monster_zombie") == 0)
    (void)qa_q1_damage(game, other, missile->actor.id, &missile->actor.id, 110, NULL, "direct", "", NULL);
  qa_q1_radius_damage(game, missile->actor.id, (missile->has_owner ? &missile->owner : NULL), 40, NULL, NULL, "");
  qa_q1_sound(game, missile->actor.id, "weapons/r_exp3.wav", "weapon", 1, 1);
  qa_q1_sprite_explosion(game, missile);
}
static void remove_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)other; (void)normal; (void)surface;
  qa_q1_remove(game, entity);
}
static void ogre_grenade_explode_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  ogre_grenade_explode(game, entity);
}
void qa_q1_register_projectile_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers handlers;
  handlers = (qa_q1_callback_handlers){.touch = backpack_touch};
  qa_q1_callback_registry_register(game->named, "base:backpack_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = laser_touch};
  qa_q1_callback_registry_register(game->named, "base:laser_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = laser_alt_touch};
  qa_q1_callback_registry_register(game->named, "base:laser_alt_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = ogre_grenade_touch};
  qa_q1_callback_registry_register(game->named, "base:ogre_grenade_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = ogre_grenade_explode_cb};
  qa_q1_callback_registry_register(game->named, "base:ogre_grenade_explode", handlers);
  handlers = (qa_q1_callback_handlers){.touch = zombie_grenade_touch};
  qa_q1_callback_registry_register(game->named, "base:zombie_grenade_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = remove_touch};
  qa_q1_callback_registry_register(game->named, "base:remove_touch", handlers);
  handlers = (qa_q1_callback_handlers){.touch = vore_touch};
  qa_q1_callback_registry_register(game->named, "base:vore_touch", handlers);
  handlers = (qa_q1_callback_handlers){.action = vore_home};
  qa_q1_callback_registry_register(game->named, "base:vore_home", handlers);
}
void qa_q1_cast_lightning(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_q1_base_monster_face(monster);
  qa_vec3 m_origin = qa_q1_base_monster_origin(monster);
  qa_q1_effect(game, "muzzleflash", m_origin, &entity->actor.id, 1);
  qa_vec3 origin = qa_add3(m_origin, V3(0, 0, 40));
  qa_vec3 direction = qa_q1_normalize(qa_sub3(qa_add3(target, V3(0, 0, 16)), origin));
  qa_q1_trace_request wall_request;
  memset(&wall_request, 0, sizeof wall_request);
  wall_request.start = origin;
  wall_request.end = qa_add3(m_origin, qa_scale3(direction, 600));
  wall_request.bounds = POINT_BOUNDS;
  wall_request.has_ignore = true;
  wall_request.ignore = entity->actor.id;
  wall_request.monsters = false;
  qa_q1_trace wall = qa_q1_host_trace(game, wall_request);
  qa_q1_event beam = {0};
  beam.kind = "beam"; beam.beam_style = "lightning1";
  beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = origin; beam.end = wall.end;
  qa_q1_emit(game, beam);
  qa_vec3 delta = qa_sub3(wall.end, origin);
  qa_vec3 side = V3(-delta.y * 16, -delta.y * 16, 0);
  qa_vec3 offsets[3] = {ZERO3, side, qa_scale3(side, -1)};
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = qa_add3(origin, offsets[i]);
    request.end = qa_add3(wall.end, offsets[i]);
    request.bounds = POINT_BOUNDS;
    request.has_ignore = true;
    request.ignore = entity->actor.id;
    request.monsters = true;
    qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (!trace.has_actor) continue;
    qa_combat_state combat;
    if (!q1base_read_combat(game, trace.actor, &combat) || !combat.can_take_damage) continue;
    bool duplicate = false;
    for (size_t h = 0; h < hit_count; h++) if (qa_same_actor(hit[h], trace.actor)) duplicate = true;
    if (duplicate) continue;
    hit[hit_count++] = trace.actor;
    (void)qa_q1_damage(game, trace.actor, entity->actor.id, &entity->actor.id, 10, "lightning", "direct", "", NULL);
    qa_q1_effect(game, "blood", trace.end, &trace.actor, 40);
  }
}

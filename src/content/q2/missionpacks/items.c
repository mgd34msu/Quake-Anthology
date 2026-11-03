/* Port of src/content/q2/missionpacks/items.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "missionpacks_internal.h"
#include "qa/content/q2/missionpacks/weapons/definitions.h"
#include "qa/foundation/support.h"
#include "qa/content/q2/foundation/rerelease_ammo.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

#define AMMO(cls, nm, ic, mdl, rot, qty, cap) \
  .kind = "ammo", .classname = cls, .name = nm, .icon = ic, .model = mdl, .sound = "misc/am_pkup.wav", .rotate = rot, \
  .respawn = 30, .quantity = qty, .capacity = cap
#define KEY(cls, nm, ic, mdl) \
  .kind = "key", .classname = cls, .name = nm, .icon = ic, .model = mdl, .sound = "items/pkup.wav", .rotate = true, .respawn = 0
static const qa_q2_item_definition xatrix_ammo[] = {
  {AMMO("ammo_magslug", "Mag Slug", "a_mslugs", "models/objects/ammo/tris.md2", false, 10, 50)},
  {AMMO("ammo_trap", "Trap", "a_trap", "models/weapons/g_trap/tris.md2", true, 1, 5), .weapon_ammo = true},
  {KEY("key_green_key", "Green Key", "k_green", "models/items/keys/green_key/tris.md2")},
};
static const qa_q2_item_definition rogue_ammo[] = {
  {AMMO("ammo_flechettes", "Flechettes", "a_flechettes", "models/ammo/am_flechette/tris.md2", false, 50, 200)},
  {AMMO("ammo_prox", "Prox", "a_prox", "models/ammo/am_prox/tris.md2", false, 5, 50)},
  {AMMO("ammo_tesla", "Tesla", "a_tesla", "models/ammo/am_tesl/tris.md2", false, 5, 50), .weapon_ammo = true,
   .has_infinite_ammo_quantity = true, .infinite_ammo_is_null = true},
  {AMMO("ammo_disruptor", "Rounds", "a_disruptor", "models/ammo/am_disr/tris.md2", false, 15, 100)},
  {KEY("key_nuke_container", "Antimatter Pod", "i_contain", "models/weapons/g_nuke/tris.md2")},
  {KEY("key_nuke", "Antimatter Bomb", "i_nuke", "models/weapons/g_nuke/tris.md2")},
};
#undef AMMO
#undef KEY
#define N(a) (sizeof(a) / sizeof((a)[0]))
typedef struct display { const char *key, *name, *icon; } display;
static const display names[] = {
  {"ionripper", "Ionripper", "w_ripper"}, {"phalanx", "Phalanx", "w_phallanx"},
  {"etf_rifle", "ETF Rifle", "w_etf_rifle"}, {"heatbeam", "Plasma Beam", "w_heatbeam"},
  {"chainfist", "Chainfist", "w_chainfist"}, {"disintegrator", "Disruptor", "w_disintegrator"},
  {"proxlauncher", "Prox Launcher", "w_proxlaunch"},
};
static const display *find_display(const char *name) {
  for (size_t i = 0; i < N(names); i++) if (strcmp(names[i].key, name) == 0) return &names[i];
  return NULL;
}

const char *qa_q2mp_mission_weapon_display_name(const char *name) {
  const display *found = find_display(name);
  if (found != NULL) return found->name;
  char classname[64];
  (void)snprintf(classname, sizeof classname, "ammo_%s", name);
  for (size_t i = 0; i < N(xatrix_ammo); i++) if (strcmp(xatrix_ammo[i].classname, classname) == 0) return xatrix_ammo[i].name;
  for (size_t i = 0; i < N(rogue_ammo); i++) if (strcmp(rogue_ammo[i].classname, classname) == 0) return rogue_ammo[i].name;
  return NULL;
}
const qa_q2mp_weapon_inventory_entry *qa_q2mp_mission_weapon_inventory(qa_q2mp_pack pack, size_t *out_count) {
  static qa_q2mp_weapon_inventory_entry entries[16];
  static char ids[16][48];
  const qa_q2_item_definition *ammo = pack == QA_Q2MP_PACK_XATRIX ? xatrix_ammo : rogue_ammo;
  size_t ammo_count = pack == QA_Q2MP_PACK_XATRIX ? N(xatrix_ammo) : N(rogue_ammo);
  const qa_q2w_definition *weapons = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definitions
                                                                  : qa_q2mp_rogue_weapon_definitions;
  size_t weapon_count = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definition_count
                                                    : qa_q2mp_rogue_weapon_definition_count;
  size_t count = 0;
  for (size_t i = 0; i < ammo_count; i++) {
    if (strcmp(ammo[i].kind, "ammo") != 0) continue;
    (void)snprintf(ids[count], sizeof ids[count], "q2:%s", ammo[i].classname);
    entries[count] = (qa_q2mp_weapon_inventory_entry){ids[count], ammo[i].capacity};
    count++;
  }
  for (size_t i = 0; i < weapon_count; i++) {
    if (weapons[i].ammo != NULL && strcmp(weapons[i].item, weapons[i].ammo) == 0) continue;
    entries[count] = (qa_q2mp_weapon_inventory_entry){weapons[i].item, 1};
    count++;
  }
  *out_count = count;
  return entries;
}
size_t qa_q2mp_mission_weapon_inventory_edition(qa_q2mp_pack pack, qa_q2_edition edition, qa_q2mp_weapon_inventory_entry *out, size_t capacity) {
  const qa_q2_item_definition *ammo = pack == QA_Q2MP_PACK_XATRIX ? xatrix_ammo : rogue_ammo;
  size_t ammo_count = pack == QA_Q2MP_PACK_XATRIX ? N(xatrix_ammo) : N(rogue_ammo);
  const qa_q2w_definition *weapons = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definitions : qa_q2mp_rogue_weapon_definitions;
  size_t weapon_count = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definition_count : qa_q2mp_rogue_weapon_definition_count;
  static char ids[2][16][48];
  size_t count = 0, id = 0;
  for (size_t i = 0; i < ammo_count; i++) {
    if (strcmp(ammo[i].kind, "ammo") != 0) continue;
    /* rereleaseCapacity(item, edition) */
    const qa_q2_ammo_limits *limits = edition == QA_Q2_EDITION_RERELEASE ? qa_q2_rerelease_ammo_limits(ammo[i].classname) : NULL;
    char *name = ids[pack == QA_Q2MP_PACK_XATRIX ? 0 : 1][id++ % 16];
    (void)snprintf(name, 48, "q2:%s", ammo[i].classname);
    if (count < capacity) out[count] = (qa_q2mp_weapon_inventory_entry){name, limits != NULL ? limits->capacity : ammo[i].capacity};
    count++;
  }
  for (size_t i = 0; i < weapon_count; i++) {
    if (weapons[i].ammo != NULL && strcmp(weapons[i].item, weapons[i].ammo) == 0) continue;
    if (count < capacity) out[count] = (qa_q2mp_weapon_inventory_entry){weapons[i].item, 1};
    count++;
  }
  return count;
}
const qa_q2mp_weapon_icon *qa_q2mp_mission_weapon_icons(size_t *out_count) {
  static qa_q2mp_weapon_icon icons[16];
  static char ids[16][48];
  size_t count = 0;
  for (int table = 0; table < 2; table++) {
    const qa_q2_item_definition *ammo = table == 0 ? xatrix_ammo : rogue_ammo;
    size_t ammo_count = table == 0 ? N(xatrix_ammo) : N(rogue_ammo);
    for (size_t i = 0; i < ammo_count; i++) {
      if (strcmp(ammo[i].kind, "ammo") != 0) continue;
      (void)snprintf(ids[count], sizeof ids[count], "q2:%s", ammo[i].classname);
      icons[count] = (qa_q2mp_weapon_icon){ids[count], ammo[i].icon};
      count++;
    }
  }
  for (int table = 0; table < 2; table++) {
    const qa_q2w_definition *weapons = table == 0 ? qa_q2mp_xatrix_weapon_definitions : qa_q2mp_rogue_weapon_definitions;
    size_t weapon_count = table == 0 ? qa_q2mp_xatrix_weapon_definition_count : qa_q2mp_rogue_weapon_definition_count;
    for (size_t i = 0; i < weapon_count; i++) {
      const display *found = find_display(weapons[i].name);
      if (found == NULL) continue;
      icons[count] = (qa_q2mp_weapon_icon){weapons[i].item, found->icon};
      count++;
    }
  }
  *out_count = count;
  return icons;
}

static qa_q2mp_powerups *power_entry(qa_q2mp_items *items, qa_actor_id actor, bool create) {
  for (size_t i = 0; i < items->power_count; i++)
    if (qa_same_actor(items->powers[i].actor, actor)) return &items->powers[i].state;
  if (!create) return NULL;
  if (items->power_count == items->power_cap) {
    size_t next = items->power_cap ? items->power_cap * 2 : 8;
    qa_q2mp_actor_powerups *grown = realloc(items->powers, next * sizeof *grown);
    if (grown == NULL) return NULL;
    items->powers = grown; items->power_cap = next;
  }
  items->powers[items->power_count] = (qa_q2mp_actor_powerups){actor, {0, 0, 0}};
  return &items->powers[items->power_count++].state;
}
qa_q2mp_powerups qa_q2mp_items_powerups(qa_q2mp_items *items, qa_actor_id actor) {
  qa_q2mp_powerups *found = power_entry(items, actor, false);
  return found != NULL ? *found : (qa_q2mp_powerups){0, 0, 0};
}
qa_q2w_input qa_q2mp_items_input(qa_q2mp_items *items, qa_actor_id actor, qa_q2w_input input) {
  qa_q2mp_powerups powers = qa_q2mp_items_powerups(items, actor);
  qa_q2w_input out = input;
  out.quad_fire_until = input.quad_fire_until > powers.quad_fire_until ? input.quad_fire_until : powers.quad_fire_until;
  out.double_until = input.double_until > powers.double_until ? input.double_until : powers.double_until;
  return out;
}
void qa_q2mp_items_reset(qa_q2mp_items *items, qa_actor_id actor) {
  for (size_t i = 0; i < items->power_count; i++)
    if (qa_same_actor(items->powers[i].actor, actor)) {
      items->powers[i] = items->powers[items->power_count - 1];
      items->power_count--;
      return;
    }
}
void qa_q2mp_items_init(qa_q2mp_items *items, qa_q2mp_item_hooks hooks, qa_q2mp_pack pack, qa_q2_item_module *shared_items) {
  memset(items, 0, sizeof *items);
  items->hooks = hooks;
  items->pack = pack;
  items->shared_items = shared_items;
}
void qa_q2mp_items_destroy(qa_q2mp_items *items) {
  free(items->powers);
  items->powers = NULL; items->power_count = 0; items->power_cap = 0;
}

bool qa_q2mp_items_spawn(qa_q2mp_items *items, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_item_module *shared = items->shared_items;
  if (items->pack == QA_Q2MP_PACK_ROGUE || q2mp_rr(game)) {
    if (strcmp(entity->classname, "weapon_nailgun") == 0) entity->classname = "weapon_etf_rifle";
    else if (strcmp(entity->classname, "ammo_nails") == 0) entity->classname = "ammo_flechettes";
    else if (strcmp(entity->classname, "weapon_heatbeam") == 0) entity->classname = "weapon_plasmabeam";
  }
  const char *classname = entity->classname;
  bool classic_rogue = items->pack == QA_Q2MP_PACK_ROGUE && game->options.edition == QA_Q2_EDITION_CLASSIC;
  const char *descriptor = classname;
  if (classic_rogue) {
    if (strcmp(classname, "ammo_magslug") == 0) descriptor = "ammo_flechettes";
    else if (strcmp(classname, "ammo_trap") == 0) descriptor = "weapon_proxlauncher";
    else if (strcmp(classname, "weapon_boomer") == 0) descriptor = "weapon_etf_rifle";
    else if (strcmp(classname, "weapon_phalanx") == 0) descriptor = "weapon_plasmabeam";
    else if (strcmp(classname, "item_quadfire") == 0) {
      double chance = q2mp_rnd(game);
      descriptor = chance < 0.2 ? "item_sphere_hunter" : chance < 0.6 ? "item_sphere_vengeance" : "item_sphere_defender";
    }
  }
  if (qa_q2_item_module_item_name(shared, descriptor) == NULL) return false;
  bool deathmatch = q2mp_dm(game);
  uint32_t flags = q2mp_u32(qa_q2_options_deathmatch_flags(&game->options));
  bool sphere = strncmp(descriptor, "item_sphere_", 12) == 0;
  bool power = strcmp(descriptor, "item_quadfire") == 0 || strcmp(descriptor, "item_double") == 0 ||
               strcmp(descriptor, "item_ir_goggles") == 0 || (strcmp(descriptor, "item_compass") == 0 && classic_rogue);
  bool remove = (deathmatch &&
      (((flags & 1u) != 0 && strcmp(classname, "item_foodcube") == 0) ||
       ((flags & 2u) != 0 && (power || sphere || strcmp(classname, "item_doppleganger") == 0)) ||
       (items->pack == QA_Q2MP_PACK_ROGUE &&
        (((flags & 0x20000u) != 0 && (strcmp(classname, "ammo_prox") == 0 || strcmp(classname, "ammo_tesla") == 0)) ||
         ((flags & 0x80000u) != 0 && strcmp(classname, "ammo_nuke") == 0) ||
         ((flags & 0x100000u) != 0 && sphere))))) ||
      (classic_rogue && (strcmp(classname, "ammo_disruptor") == 0 || strcmp(classname, "weapon_disintegrator") == 0 ||
       (!deathmatch && (strcmp(descriptor, "ammo_nuke") == 0 || strcmp(descriptor, "item_doppleganger") == 0 ||
                        strcmp(descriptor, "item_sphere_hunter") == 0 || strcmp(descriptor, "item_sphere_vengeance") == 0))));
  if (remove) {
    game->remove(game->ctx, entity);
    return true;
  }
  if (classic_rogue && entity->spawnflags > 1 && strcmp(classname, "key_power_cube") != 0) entity->spawnflags = 0;
  if (!qa_q2_item_module_spawn_item(shared, entity, game, descriptor)) return false;
  if (strcmp(classname, "item_foodcube") == 0) {
    entity->spawnflags = (double)(qa_js_to_int32(q2mp_u32(entity->spawnflags) | 0x10000u));
    entity->classname = "foodcube";
  }
  return true;
}
qa_q2_entity *qa_q2mp_items_random_respawn(qa_q2mp_items *items, qa_q2_entity *entity, qa_q2_game_services *game,
                                            qa_q2mp_random_item_settings settings) {
  qa_q2_item_module *shared = items->shared_items;
  if (game->options.edition == QA_Q2_EDITION_CLASSIC && items->pack != QA_Q2MP_PACK_ROGUE) return NULL;
  const qa_q2_item_definition *item = qa_q2_item_module_item_definition(shared, entity);
  if (item == NULL) return NULL;
  const char *classname = qa_q2mp_random_item(entity, item, game, settings);
  if (classname == NULL) return NULL;
  if (q2mp_rr(game)) {
    qa_q2_item_module_replace_item(shared, entity, game, classname);
    return entity;
  }
  qa_q2_entity *replacement = game->create(game->ctx, classname, NULL, 0);
  qa_body_state body = q2mp_body(game, entity);
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = body.origin;
  c.has_bounds = true; c.bounds = body.bounds;
  q2mp_move(game, replacement, c, true, false);
  replacement->gravity_vector = q2mp_v3(0, 0, -1);
  if (!qa_q2mp_items_spawn(items, replacement, game)) qa_raise("Error", 0, 0, "Rogue random item is not registered: %s", classname);
  replacement->render_flags = (double)(qa_js_to_int32(q2mp_u32(replacement->render_flags) | 0x8000u));
  return replacement;
}
/* TS q2MissionAmmoPackCapacity: the ceiling a Bandolier (or the Ammo Pack when `full`) raises a mission pack ammo to;
 * `classic` is the original DLL's value. */
double qa_q2mp_mission_ammo_pack_capacity(const char *classname, bool full, qa_q2_edition edition, double classic) {
  const qa_q2_ammo_limits *limits = edition == QA_Q2_EDITION_RERELEASE ? qa_q2_rerelease_ammo_limits(classname) : NULL;
  if (limits == NULL) return classic;
  if (full) return limits->has_pack ? limits->pack : classic;
  return limits->has_bandolier ? limits->bandolier : classic;
}
void qa_q2mp_items_ammo_pack(qa_q2mp_items *items, qa_owned_actor player, qa_q2_game_services *game, bool full, qa_q2mp_pack pack) {
  (void)items;
  typedef struct change { qa_item_id item; double capacity, give; } change;
  const qa_q2_edition edition = game->options.edition;
  change changes[2];
  size_t count;
  if (pack == QA_Q2MP_PACK_XATRIX) {
    changes[0] = (change){"q2:ammo_magslug", qa_q2mp_mission_ammo_pack_capacity("ammo_magslug", full, edition, full ? 100 : 75), full ? 10 : 0};
    count = 1;
  } else {
    changes[0] = (change){"q2:ammo_flechettes", qa_q2mp_mission_ammo_pack_capacity("ammo_flechettes", full, edition, full ? 200 : 250), full ? 50 : 0};
    changes[1] = (change){"q2:ammo_disruptor", qa_q2mp_mission_ammo_pack_capacity("ammo_disruptor", full, edition, full ? 200 : 150), full ? 15 : 0};
    count = 2;
  }
  for (size_t i = 0; i < count; i++) {
    size_t n = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, player.id, &n);
    const qa_inventory_entry *current = NULL;
    for (size_t j = 0; j < n && current == NULL; j++) if (strcmp(entries[j].item, changes[i].item) == 0) current = &entries[j];
    qa_inventory_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.item = changes[i].item;
    entry.count = current != NULL ? current->count : 0;
    double capacity = current != NULL ? current->capacity : 0;
    entry.capacity = capacity > changes[i].capacity ? capacity : changes[i].capacity;
    qa_shared_inventory_table_configure(game->host->inventory, player, entry, NULL, NULL);
    (void)qa_shared_inventory_table_give(game->host->inventory, player, changes[i].item, changes[i].give);
  }
}
bool qa_q2mp_items_capture(qa_q2mp_items *items, qa_q2_game_services *game, qa_q2mp_items_checkpoint *out) {
  memset(out, 0, sizeof *out);
  size_t count = 0;
  for (size_t i = 0; i < items->power_count; i++)
    if (q2mp_live(game, items->powers[i].actor)) count++;
  if (count > 0) {
    out->powers = malloc(count * sizeof *out->powers);
    if (out->powers == NULL) return false;
  }
  for (size_t i = 0; i < items->power_count; i++) {
    if (!q2mp_live(game, items->powers[i].actor)) continue;
    out->powers[out->power_count] = (qa_q2mp_item_power_checkpoint){
        {items->powers[i].actor.slot, items->powers[i].actor.generation}, items->powers[i].state};
    out->power_count++;
  }
  return true;
}
void qa_q2mp_items_checkpoint_free(qa_q2mp_items_checkpoint *checkpoint) {
  free(checkpoint->powers);
  checkpoint->powers = NULL; checkpoint->power_count = 0;
}
void qa_q2mp_items_restore(qa_q2mp_items *items, qa_q2_game_services *game, const qa_q2mp_items_checkpoint *checkpoint) {
  items->power_count = 0;
  for (size_t i = 0; i < checkpoint->power_count; i++) {
    qa_owned_actor restored = qa_q2_restore_actor(game, checkpoint->powers[i].actor);
    qa_q2mp_powerups *entry = power_entry(items, restored.id, true);
    if (entry != NULL) *entry = checkpoint->powers[i].state;
  }
}

static double *power_field(qa_q2mp_powerups *powers, int field) {
  return field == 0 ? &powers->quad_fire_until : field == 1 ? &powers->double_until : &powers->ir_until;
}
static bool power_use_timeout(qa_q2mp_power_env *env, qa_owned_actor player, qa_q2_game_services *game, double timeout) {
  qa_q2mp_items *items = env->items;
  if (!qa_shared_inventory_table_consume(game->host->inventory, player, env->item, 1)) return false;
  qa_q2mp_powerups *powers = power_entry(items, player.id, true);
  if (powers == NULL) return false; /* OOM only; TS has no counterpart */
  double *field = power_field(powers, env->field);
  *field = (q2mp_now(game) > *field ? q2mp_now(game) : *field) + timeout;
  qa_q2_entity *entity = q2mp_ent(game, true, player.id);
  if (entity != NULL) q2mp_sound(game, entity, env->sound, true, 3, false, 0, false, 0);
  if (env->field == 2) {
    qa_q2mp_player_effect fx;
    memset(&fx, 0, sizeof fx);
    fx.kind = QA_Q2MP_EFFECT_IR; fx.actor = player.id; fx.until = powers->ir_until;
    items->hooks.player_effect(items->hooks.ctx, fx);
  }
  return true;
}
static bool power_use(void *ctx, qa_owned_actor player, qa_q2_game_services *game) {
  qa_q2mp_power_env *env = ctx;
  return power_use_timeout(env, player, game, env->duration);
}
static bool power_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  qa_q2mp_power_env *env = ctx;
  double count = qa_shared_inventory_table_count(game->host->inventory, player.id, env->item);
  if ((game->options.skill == 1 && count >= 2) || (game->options.skill >= 2 && count >= 1)) return false;
  if (qa_shared_inventory_table_give(game->host->inventory, player, env->item, 1) == 0) return false;
  bool dropped_quad_fire = env->field == 0 && ((q2mp_u32(entity->spawnflags) & 0x20000u) != 0);
  if (((q2mp_u32(qa_q2_options_deathmatch_flags(&game->options)) & 16u) != 0) || dropped_quad_fire) {
    double timeout = dropped_quad_fire && entity->has_next_think
        ? (entity->next_think - q2mp_now(game) > 0 ? entity->next_think - q2mp_now(game) : 0) : env->duration;
    (void)power_use_timeout(env, player, game, timeout);
  }
  return true;
}
static qa_q2_item_definition power_item(qa_q2mp_items *items, const char *classname, qa_item_id item, const char *name,
                                          const char *icon, const char *model, const char *sound, int field, double duration) {
  qa_q2mp_power_env *env = &items->power_envs[field];
  *env = (qa_q2mp_power_env){items, item, sound, field, duration};
  qa_q2_item_definition d;
  memset(&d, 0, sizeof d);
  d.kind = "custom"; d.classname = classname; d.name = name; d.icon = icon; d.model = model;
  d.sound = "items/pkup.wav"; d.rotate = true; d.respawn = 60; d.capacity = 32767; d.quantity = 1;
  d.has_coop_stay = true; d.coop_stay = false; d.droppable = true;
  d.custom_ctx = env; d.pickup = power_pickup; d.use = power_use; d.has_use = true;
  return d;
}
static bool foodcube_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)ctx;
  qa_combat_state_result combat = q2mp_cread(game, player.id);
  if (!combat.has_value) return false;
  qa_gameplay_authority_set_health(game->host->combat, player, combat.value.health + entity->count);
  return true;
}
static bool compass_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)ctx; (void)entity;
  double count = qa_shared_inventory_table_count(game->host->inventory, player.id, "q2:item_compass");
  return !((game->options.skill == 1 && count >= 2) || (game->options.skill >= 2 && count >= 1)) &&
         qa_shared_inventory_table_give(game->host->inventory, player, "q2:item_compass", 1) > 0;
}
static bool compass_use(void *ctx, qa_owned_actor player, qa_q2_game_services *game) {
  qa_q2mp_items *items = ctx;
  qa_body_state_result body = q2mp_bread(game, player.id);
  if (!body.has_value) return false;
  const qa_q2w_input *input = qa_q2w_ballistics_input(&items->env_weapons->base, player.id);
  double yaw = trunc(input != NULL ? input->angles.y : body.value.angles.y);
  if (yaw < 0) yaw += 360;
  /* toFixed(0) rounds exact .5 ties up and prints -0 as "0"; "%.0f" did neither. Dir is `${yaw}`. */
  char x[QA_JS_TO_FIXED_CAPACITY], y[QA_JS_TO_FIXED_CAPACITY], z[QA_JS_TO_FIXED_CAPACITY];
  qa_js_to_fixed(body.value.origin.x, 0, x, sizeof x);
  qa_js_to_fixed(body.value.origin.y, 0, y, sizeof y);
  qa_js_to_fixed(body.value.origin.z, 0, z, sizeof z);
  char text[3 * QA_JS_TO_FIXED_CAPACITY + 64];
  (void)snprintf(text, sizeof text, "Origin: %s,%s,%s    Dir: %s\n", x, y, z, QA_JS_NUMBER(yaw));
  qa_q2_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = "print"; event.has_actor = true; event.actor = player.id; event.level = "high"; event.text = text;
  game->host->emit(game->host->ctx, event);
  return true;
}
static bool dopple_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)ctx; (void)entity;
  return q2mp_dm(game) && qa_shared_inventory_table_give(game->host->inventory, player, "q2:item_doppleganger", 1) > 0;
}
static bool dopple_use(void *ctx, qa_owned_actor player, qa_q2_game_services *game) {
  qa_q2mp_items *items = ctx;
  qa_q2_entity *owner = q2mp_ent(game, true, player.id);
  return owner != NULL && qa_q2mp_doppleganger_use(items->env_doppleganger, owner, game);
}
static bool nuke_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)ctx; (void)entity;
  return qa_shared_inventory_table_give(game->host->inventory, player, "q2:ammo_nuke", 1) > 0;
}
static bool nuke_use(void *ctx, qa_owned_actor player, qa_q2_game_services *game) {
  qa_q2mp_items *items = ctx;
  qa_q2_entity *entity = q2mp_ent(game, true, player.id);
  const qa_q2w_input *input = qa_q2w_ballistics_input(&items->env_weapons->base, player.id);
  if (entity == NULL || !qa_shared_inventory_table_consume(game->host->inventory, player, "q2:ammo_nuke", 1)) return false;
  qa_q2mp_powerups powers = qa_q2mp_items_powerups(items, player.id);
  bool quad = (input != NULL ? input->quad_until : 0) > q2mp_now(game);
  double double_until = powers.double_until > (input != NULL ? input->double_until : 0) ? powers.double_until
                                                                                        : (input != NULL ? input->double_until : 0);
  double multiplier = (quad ? 4 : 1) *
      (double_until > q2mp_now(game) && !(quad && input != NULL && input->no_stack_double) ? 2 : 1);
  qa_vec3 angles = input != NULL ? input->angles : q2mp_body(game, entity).angles;
  (void)qa_q2mp_nuke_fire_nuke(&items->env_projectiles->base, entity->actor, game, q2mp_body(game, entity).origin,
                               q2mp_axes(angles).forward, 100, multiplier);
  return true;
}
static bool sphere_use_env(qa_q2mp_sphere_env *env, qa_owned_actor player, qa_q2_game_services *game) {
  qa_q2mp_items *items = env->items;
  qa_q2_entity *entity = q2mp_ent(game, true, player.id);
  if (entity == NULL || qa_q2mp_spheres_owned_sphere(items->env_spheres, player.id, game) != NULL ||
      !qa_shared_inventory_table_consume(game->host->inventory, player, env->item, 1)) return false;
  (void)qa_q2mp_spheres_launch(items->env_spheres, entity, game, env->kind, false);
  return true;
}
static bool sphere_use(void *ctx, qa_owned_actor player, qa_q2_game_services *game) { return sphere_use_env(ctx, player, game); }
static bool sphere_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)entity;
  qa_q2mp_sphere_env *env = ctx;
  qa_q2mp_items *items = env->items;
  double count = qa_shared_inventory_table_count(game->host->inventory, player.id, env->item);
  if (qa_q2mp_spheres_owned_sphere(items->env_spheres, player.id, game) != NULL ||
      (game->options.skill == 1 && count >= 2) || (game->options.skill >= 2 && count >= 1)) return false;
  if (qa_shared_inventory_table_give(game->host->inventory, player, env->item, 1) == 0) return false;
  if (q2mp_dm(game) && ((q2mp_u32(qa_q2_options_deathmatch_flags(&game->options)) & 16u) != 0)) (void)sphere_use_env(env, player, game);
  return true;
}

void qa_q2mp_items_register(qa_q2mp_items *items, qa_q2_item_module *module, qa_q2w_weapons *weapons,
                            qa_q2mp_pack pack, qa_q2mp_projectiles *projectiles, qa_q2mp_spheres *spheres,
                            qa_q2mp_doppleganger *doppleganger, qa_q2_edition edition) {
  items->env_weapons = weapons;
  items->env_projectiles = projectiles;
  items->env_spheres = spheres;
  items->env_doppleganger = doppleganger;
  const qa_q2_item_definition *ammo = pack == QA_Q2MP_PACK_XATRIX ? xatrix_ammo : rogue_ammo;
  size_t ammo_count = pack == QA_Q2MP_PACK_XATRIX ? N(xatrix_ammo) : N(rogue_ammo);
  for (size_t i = 0; i < ammo_count; i++) {
    qa_q2_item_definition item = ammo[i];
    /* TS rereleaseQuantity: { ammo_tesla: 3, ammo_disruptor: 3 }. */
    bool rerelease = edition == QA_Q2_EDITION_RERELEASE;
    /* rereleaseCapacity(item, edition): the rerelease ammo limits size the stack. */
    const qa_q2_ammo_limits *limits = rerelease && strcmp(item.kind, "ammo") == 0 ? qa_q2_rerelease_ammo_limits(item.classname) : NULL;
    if (limits != NULL) item.capacity = limits->capacity;
    bool has_quantity = rerelease && (strcmp(item.classname, "ammo_tesla") == 0 || strcmp(item.classname, "ammo_disruptor") == 0);
    if (strcmp(item.kind, "ammo") == 0 && strcmp(item.classname, "ammo_trap") == 0 && rerelease) {
      item.has_infinite_ammo_quantity = true; item.infinite_ammo_is_null = true;
    } else if (strcmp(item.kind, "ammo") == 0 && has_quantity) {
      item.quantity = 3;
    }
    qa_q2_item_module_register(module, item);
  }
  const qa_q2w_definition *defs = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definitions
                                                              : qa_q2mp_rogue_weapon_definitions;
  size_t def_count = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definition_count
                                                 : qa_q2mp_rogue_weapon_definition_count;
  for (size_t i = 0; i < def_count; i++) {
    const display *visual = find_display(defs[i].name);
    if (visual == NULL) continue;
    qa_q2_item_definition d;
    memset(&d, 0, sizeof d);
    d.kind = "weapon"; d.classname = defs[i].classname; d.model = defs[i].world_model;
    d.icon = visual->icon; d.name = visual->name; d.sound = "misc/w_pkup.wav";
    d.rotate = true; d.respawn = 30; d.ammo = defs[i].ammo;
    d.has_coop_stay = true; d.coop_stay = edition == QA_Q2_EDITION_RERELEASE;
    qa_q2_item_module_register(module, d);
  }
  if (pack == QA_Q2MP_PACK_XATRIX) {
    qa_q2_item_module_register(module, power_item(items, "item_quadfire", "q2:item_quadfire", "DualFire Damage",
                                                  "p_quadfire", "models/items/quadfire/tris.md2", "items/quadfire1.wav", 0, 30));
    qa_q2_item_definition d;
    memset(&d, 0, sizeof d);
    d.kind = "custom"; d.classname = "item_foodcube"; d.name = "Health"; d.icon = "i_health";
    d.model = "models/objects/trapfx/tris.md2"; d.sound = "items/s_health.wav"; d.rotate = false; d.respawn = 0;
    d.capacity = 0; d.quantity = 0; d.has_coop_stay = true; d.coop_stay = false; d.droppable = false;
    d.custom_ctx = items; d.pickup = foodcube_pickup; d.has_use = false;
    qa_q2_item_module_register(module, d);
  } else {
    if (edition == QA_Q2_EDITION_CLASSIC) {
      qa_q2_item_definition d;
      memset(&d, 0, sizeof d);
      d.kind = "custom"; d.classname = "item_compass"; d.name = "compass"; d.icon = "p_compass";
      d.model = "models/objects/fire/tris.md2"; d.sound = "items/pkup.wav"; d.rotate = true; d.respawn = 60;
      d.capacity = 32767; d.quantity = 1; d.has_coop_stay = true; d.coop_stay = false; d.droppable = false;
      d.custom_ctx = items; d.pickup = compass_pickup; d.use = compass_use; d.has_use = true;
      qa_q2_item_module_register(module, d);
    }
    {
      qa_q2_item_definition d;
      memset(&d, 0, sizeof d);
      d.kind = "custom"; d.classname = "item_doppleganger"; d.name = "Doppleganger"; d.icon = "p_doppleganger";
      d.model = "models/items/dopple/tris.md2"; d.sound = "items/pkup.wav"; d.rotate = true; d.respawn = 90;
      d.capacity = 1; d.quantity = 1; d.has_coop_stay = true; d.coop_stay = false; d.droppable = true;
      d.custom_ctx = items; d.pickup = dopple_pickup; d.use = dopple_use; d.has_use = true;
      qa_q2_item_module_register(module, d);
    }
    qa_q2_item_module_register(module, power_item(items, "item_double", "q2:item_double", "Double Damage", "p_double",
                                                  "models/items/ddamage/tris.md2", "misc/ddamage1.wav", 1, 30));
    qa_q2_item_module_register(module, power_item(items, "item_ir_goggles", "q2:item_ir_goggles", "IR Goggles", "p_ir",
                                                  "models/items/goggles/tris.md2", "misc/ir_start.wav", 2, 60));
    {
      qa_q2_item_definition d;
      memset(&d, 0, sizeof d);
      d.kind = "custom"; d.classname = "ammo_nuke"; d.name = "A-M Bomb"; d.icon = "p_nuke";
      d.model = "models/weapons/g_nuke/tris.md2"; d.sound = "misc/am_pkup.wav"; d.rotate = true; d.respawn = 300;
      d.capacity = 1; d.quantity = 1; d.has_coop_stay = true; d.coop_stay = false; d.droppable = true;
      d.custom_ctx = items; d.pickup = nuke_pickup; d.use = nuke_use; d.has_use = true;
      qa_q2_item_module_register(module, d);
    }
    static const struct { qa_q2mp_sphere_kind kind; const char *classname, *item, *name, *icon, *model; double respawn; } sphere_items[] = {
      {QA_Q2MP_SPHERE_DEFENDER, "item_sphere_defender", "q2:item_sphere_defender", "defender sphere", "p_defender", "models/items/defender/tris.md2", 60},
      {QA_Q2MP_SPHERE_HUNTER, "item_sphere_hunter", "q2:item_sphere_hunter", "hunter sphere", "p_hunter", "models/items/hunter/tris.md2", 120},
      {QA_Q2MP_SPHERE_VENGEANCE, "item_sphere_vengeance", "q2:item_sphere_vengeance", "vengeance sphere", "p_vengeance", "models/items/vengnce/tris.md2", 60},
    };
    for (size_t i = 0; i < N(sphere_items); i++) {
      qa_q2mp_sphere_env *env = &items->sphere_envs[sphere_items[i].kind];
      *env = (qa_q2mp_sphere_env){items, sphere_items[i].item, sphere_items[i].kind};
      qa_q2_item_definition d;
      memset(&d, 0, sizeof d);
      d.kind = "custom"; d.classname = sphere_items[i].classname; d.name = sphere_items[i].name;
      d.icon = sphere_items[i].icon; d.model = sphere_items[i].model; d.sound = "items/pkup.wav";
      d.rotate = true; d.respawn = sphere_items[i].respawn; d.capacity = 32767; d.quantity = 1;
      d.has_coop_stay = true; d.coop_stay = false; d.droppable = false;
      d.custom_ctx = env; d.pickup = sphere_pickup; d.use = sphere_use; d.has_use = true;
      qa_q2_item_module_register(module, d);
    }
  }
}

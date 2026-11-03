/* Port of src/content/q2/foundation/items.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/items.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/infinite_ammo.h"
#include "qa/content/q2/foundation/start_items.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/pickups.h"

#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

typedef qa_q2_item_definition item;

static const item AMMUNITION[] = {
  {.kind = "ammo", .classname = "ammo_shells", .model = "models/items/ammo/shells/medium/tris.md2", .icon = "a_shells", .name = "Shells", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 10, .capacity = 100},
  {.kind = "ammo", .classname = "ammo_bullets", .model = "models/items/ammo/bullets/medium/tris.md2", .icon = "a_bullets", .name = "Bullets", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 50, .capacity = 200},
  {.kind = "ammo", .classname = "ammo_cells", .model = "models/items/ammo/cells/medium/tris.md2", .icon = "a_cells", .name = "Cells", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 50, .capacity = 200},
  {.kind = "ammo", .classname = "ammo_rockets", .model = "models/items/ammo/rockets/medium/tris.md2", .icon = "a_rockets", .name = "Rockets", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 5, .capacity = 50},
  {.kind = "ammo", .classname = "ammo_slugs", .model = "models/items/ammo/slugs/medium/tris.md2", .icon = "a_slugs", .name = "Slugs", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 10, .capacity = 50},
  {.kind = "ammo", .classname = "ammo_grenades", .model = "models/items/ammo/grenades/medium/tris.md2", .icon = "a_grenades", .name = "Grenades", .sound = "misc/am_pkup.wav", .respawn = 30, .quantity = 5, .capacity = 50, .weapon_ammo = true},
};
static const item KEYS[] = {
  {.kind = "key", .classname = "key_data_cd", .model = "models/items/keys/data_cd/tris.md2", .icon = "k_datacd", .name = "Data CD", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_power_cube", .model = "models/items/keys/power/tris.md2", .icon = "k_powercube", .name = "Power Cube", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_pyramid", .model = "models/items/keys/pyramid/tris.md2", .icon = "k_pyramid", .name = "Pyramid Key", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_data_spinner", .model = "models/items/keys/spinner/tris.md2", .icon = "k_dataspin", .name = "Data Spinner", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_pass", .model = "models/items/keys/pass/tris.md2", .icon = "k_security", .name = "Security Pass", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_blue_key", .model = "models/items/keys/key/tris.md2", .icon = "k_bluekey", .name = "Blue Key", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_red_key", .model = "models/items/keys/red_key/tris.md2", .icon = "k_redkey", .name = "Red Key", .sound = "items/pkup.wav", .rotate = true},
  {.kind = "key", .classname = "key_commander_head", .model = "models/monsters/commandr/head/tris.md2", .icon = "k_comhead", .name = "Commander's Head", .sound = "items/pkup.wav"},
  {.kind = "key", .classname = "key_airstrike_target", .model = "models/items/keys/target/tris.md2", .icon = "i_airstrike", .name = "Airstrike Marker", .sound = "items/pkup.wav", .rotate = true},
};
/* TS Q2_BASE_WEAPONS (owner weapons/definitions.ts) weapon rows. */
typedef struct weapon_row { const char *name; qa_item_id item_id; const char *classname; qa_item_id ammo; const char *view_model, *world_model; const char *display, *icon; } weapon_row;
static const weapon_row BASE_WEAPONS[] = {
  {"blaster", "q2:weapon_blaster", "weapon_blaster", NULL, "models/weapons/v_blast/tris.md2", "", "Blaster", "w_blaster"},
  {"shotgun", "q2:weapon_shotgun", "weapon_shotgun", "q2:ammo_shells", "models/weapons/v_shotg/tris.md2", "models/weapons/g_shotg/tris.md2", "Shotgun", "w_shotgun"},
  {"supershotgun", "q2:weapon_supershotgun", "weapon_supershotgun", "q2:ammo_shells", "models/weapons/v_shotg2/tris.md2", "models/weapons/g_shotg2/tris.md2", "Super Shotgun", "w_sshotgun"},
  {"machinegun", "q2:weapon_machinegun", "weapon_machinegun", "q2:ammo_bullets", "models/weapons/v_machn/tris.md2", "models/weapons/g_machn/tris.md2", "Machinegun", "w_machinegun"},
  {"chaingun", "q2:weapon_chaingun", "weapon_chaingun", "q2:ammo_bullets", "models/weapons/v_chain/tris.md2", "models/weapons/g_chain/tris.md2", "Chaingun", "w_chaingun"},
  {"grenades", "q2:ammo_grenades", "ammo_grenades", "q2:ammo_grenades", "models/weapons/v_handgr/tris.md2", "models/items/ammo/grenades/medium/tris.md2", "Grenades", "a_grenades"},
  {"grenadelauncher", "q2:weapon_grenadelauncher", "weapon_grenadelauncher", "q2:ammo_grenades", "models/weapons/v_launch/tris.md2", "models/weapons/g_launch/tris.md2", "Grenade Launcher", "w_glauncher"},
  {"rocketlauncher", "q2:weapon_rocketlauncher", "weapon_rocketlauncher", "q2:ammo_rockets", "models/weapons/v_rocket/tris.md2", "models/weapons/g_rocket/tris.md2", "Rocket Launcher", "w_rlauncher"},
  {"hyperblaster", "q2:weapon_hyperblaster", "weapon_hyperblaster", "q2:ammo_cells", "models/weapons/v_hyperb/tris.md2", "models/weapons/g_hyperb/tris.md2", "HyperBlaster", "w_hyperblaster"},
  {"railgun", "q2:weapon_railgun", "weapon_railgun", "q2:ammo_slugs", "models/weapons/v_rail/tris.md2", "models/weapons/g_rail/tris.md2", "Railgun", "w_railgun"},
  {"bfg", "q2:weapon_bfg", "weapon_bfg", "q2:ammo_cells", "models/weapons/v_bfg/tris.md2", "models/weapons/g_bfg/tris.md2", "BFG10K", "w_bfg"},
};
static const item REST[] = {
  {.kind = "ammo-pack", .classname = "item_bandolier", .model = "models/items/band/tris.md2", .icon = "p_bandolier", .name = "Bandolier", .sound = "items/pkup.wav", .rotate = true, .respawn = 60},
  {.kind = "ammo-pack", .classname = "item_pack", .model = "models/items/pack/tris.md2", .icon = "i_pack", .name = "Ammo Pack", .sound = "items/pkup.wav", .rotate = true, .respawn = 180, .full = true},
  {.kind = "health", .classname = "item_health", .model = "models/items/healing/medium/tris.md2", .icon = "i_health", .name = "Health", .sound = "items/n_health.wav", .respawn = 30, .amount = 10},
  {.kind = "health", .classname = "item_health_small", .model = "models/items/healing/stimpack/tris.md2", .icon = "i_health", .name = "Health", .sound = "items/s_health.wav", .respawn = 30, .amount = 2, .ignore_maximum = true},
  {.kind = "health", .classname = "item_health_large", .model = "models/items/healing/large/tris.md2", .icon = "i_health", .name = "Health", .sound = "items/l_health.wav", .respawn = 30, .amount = 25},
  {.kind = "health", .classname = "item_health_mega", .model = "models/items/mega_h/tris.md2", .icon = "i_health", .name = "Health", .sound = "items/m_health.wav", .respawn = 20, .amount = 100, .ignore_maximum = true, .timed = true},
  {.kind = "armor", .classname = "item_armor_jacket", .model = "models/items/armor/jacket/tris.md2", .icon = "i_jacketarmor", .name = "Jacket Armor", .sound = "misc/ar1_pkup.wav", .rotate = true, .respawn = 20, .points = 25, .maximum = 50, .normal = 0.3, .energy = 0},
  {.kind = "armor", .classname = "item_armor_combat", .model = "models/items/armor/combat/tris.md2", .icon = "i_combatarmor", .name = "Combat Armor", .sound = "misc/ar1_pkup.wav", .rotate = true, .respawn = 20, .points = 50, .maximum = 100, .normal = 0.6, .energy = 0.3},
  {.kind = "armor", .classname = "item_armor_body", .model = "models/items/armor/body/tris.md2", .icon = "i_bodyarmor", .name = "Body Armor", .sound = "misc/ar1_pkup.wav", .rotate = true, .respawn = 20, .points = 100, .maximum = 200, .normal = 0.8, .energy = 0.6},
  {.kind = "shard", .classname = "item_armor_shard", .model = "models/items/armor/shard/tris.md2", .icon = "i_jacketarmor", .name = "Armor Shard", .sound = "misc/ar2_pkup.wav", .rotate = true, .respawn = 20},
  {.kind = "power", .classname = "item_quad", .model = "models/items/quaddama/tris.md2", .icon = "p_quad", .name = "Quad Damage", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .has_coop_stay = true},
  {.kind = "power", .classname = "item_invulnerability", .model = "models/items/invulner/tris.md2", .icon = "p_invulnerability", .name = "Invulnerability", .sound = "items/pkup.wav", .rotate = true, .respawn = 300, .has_coop_stay = true},
  {.kind = "power", .classname = "item_silencer", .model = "models/items/silencer/tris.md2", .icon = "p_silencer", .name = "Silencer", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .has_coop_stay = true},
  {.kind = "power", .classname = "item_breather", .model = "models/items/breather/tris.md2", .icon = "p_rebreather", .name = "Rebreather", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .has_coop_stay = true, .coop_stay = true},
  {.kind = "power", .classname = "item_enviro", .model = "models/items/enviro/tris.md2", .icon = "p_envirosuit", .name = "Environment Suit", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .has_coop_stay = true, .coop_stay = true},
  {.kind = "power-armor", .classname = "item_power_screen", .model = "models/items/armor/screen/tris.md2", .icon = "i_powerscreen", .name = "Power Screen", .sound = "misc/ar3_pkup.wav", .rotate = true, .respawn = 60, .armor = "screen"},
  {.kind = "power-armor", .classname = "item_power_shield", .model = "models/items/armor/shield/tris.md2", .icon = "i_powershield", .name = "Power Shield", .sound = "misc/ar3_pkup.wav", .rotate = true, .respawn = 60, .armor = "shield"},
  {.kind = "maximum-health", .classname = "item_adrenaline", .model = "models/items/adrenal/tris.md2", .icon = "p_adrenaline", .name = "Adrenaline", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .increase = 1, .fill = true},
  {.kind = "maximum-health", .classname = "item_ancient_head", .model = "models/items/c_head/tris.md2", .icon = "i_fixme", .name = "Ancient Head", .sound = "items/pkup.wav", .rotate = true, .respawn = 60, .increase = 2},
};

/* Retained id strings per catalog classname (leaked-by-design registry). */
static const char *retained_id(const char *classname) {
  static char *cache[512];
  static size_t cache_n;
  for (size_t i = 0; i < cache_n; i++) if (strcmp(cache[i] + 3, classname) == 0) return cache[i];
  if (cache_n >= 512) qa_range_error("out of memory");
  size_t len = strlen(classname) + 4;
  char *id = malloc(len);
  if (!id) qa_range_error("out of memory");
  snprintf(id, len, "q2:%s", classname);
  cache[cache_n++] = id;
  return id;
}
#define ID(it) retained_id((it)->classname)

static bool is_dropped(const qa_q2_entity *e) { return (qa_js_to_int32(e->spawnflags) & 0x30000) != 0; }
static bool stays_coop(const item *it) {
  if (strcmp(it->kind, "key") == 0) return true;
  if (strcmp(it->kind, "weapon") == 0) return it->has_coop_stay ? it->coop_stay : true;
  if (strcmp(it->kind, "power") == 0 || strcmp(it->kind, "custom") == 0) return it->coop_stay;
  return false;
}

typedef struct pickup_state { qa_q2_entity *e; const item *it; bool targets_used, retained; bool has_expires; double expires_at; } pickup_state;
typedef struct power_state { qa_actor_id id; qa_q2_player_powerups v; } power_state;

struct qa_q2_item_module {
  qa_q2_item_hooks hooks;
  item *catalog; size_t catalog_count, catalog_cap;
  const char **foreign; size_t foreign_count, foreign_cap;
  qa_q2_foreign_item_services foreign_services;
  qa_q2_foreign_item_list foreign_list;
  pickup_state *pickups; size_t pickup_count, pickup_cap;
  power_state *powers; size_t power_count, power_cap;
  qa_actor_id *bindings; size_t binding_count, binding_cap;
  double power_cube_count;
  qa_q2_pickup_policy policy; bool has_policy;
  const qa_pickup_admission *admission;
  qa_q2_named_think thinks[6];
  qa_q2_named_touch touches[2];
  qa_q2_named_use uses[2];
  qa_q2_callback_definitions defs;
  qa_q2_inventory_item *list_scratch; size_t list_cap;
  qa_q2_weapon_inventory_entry *winv_scratch; size_t winv_cap;
  const char **path_scratch; char **path_bufs; size_t path_cap;
  qa_q2_item_pickup_checkpoint *cp_pickups;
  qa_q2_item_power_checkpoint *cp_powers;
  qa_saved_actor_id *cp_bindings;
  struct cell_ctx { qa_q2_game_services *g; qa_owned_actor actor; } *cell_ctxs; size_t cell_count, cell_cap;
};

static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n); if (!q) qa_range_error("out of memory"); return q; }

static const item *catalog_get(qa_q2_item_module *m, const char *classname) {
  for (size_t i = 0; i < m->catalog_count; i++) if (strcmp(m->catalog[i].classname, classname) == 0) return &m->catalog[i];
  return NULL;
}
static void catalog_set(qa_q2_item_module *m, item it) {
  for (size_t i = 0; i < m->catalog_count; i++) if (strcmp(m->catalog[i].classname, it.classname) == 0) { m->catalog[i] = it; return; }
  if (m->catalog_count == m->catalog_cap) { m->catalog_cap = m->catalog_cap ? m->catalog_cap * 2 : 64; m->catalog = xrealloc(m->catalog, m->catalog_cap * sizeof *m->catalog); }
  m->catalog[m->catalog_count++] = it;
}
/* TS applyRereleaseRows: game_x64.dll body armor sound and shard icon. */
static void apply_rerelease_rows(qa_q2_item_module *m) {
  const item *body = catalog_get(m, "item_armor_body"), *shard = catalog_get(m, "item_armor_shard");
  if (body != NULL) { item changed = *body; changed.sound = "misc/ar3_pkup.wav"; catalog_set(m, changed); }
  if (shard != NULL) { item changed = *shard; changed.icon = "i_armor_shard"; catalog_set(m, changed); }
}
static bool is_foreign(qa_q2_item_module *m, const char *classname) {
  for (size_t i = 0; i < m->foreign_count; i++) if (strcmp(m->foreign[i], classname) == 0) return true;
  return false;
}
static pickup_state *find_pickup(qa_q2_item_module *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->pickup_count; i++) if (m->pickups[i].e == e) return &m->pickups[i];
  return NULL;
}
static pickup_state *pickup_of(qa_q2_item_module *m, qa_q2_entity *e) {
  pickup_state *p = find_pickup(m, e);
  if (!p) qa_range_error("Q2 item callback has no pickup state");
  return p;
}
static void set_pickup(qa_q2_item_module *m, qa_q2_entity *e, pickup_state v) {
  pickup_state *p = find_pickup(m, e);
  if (p) { *p = v; return; }
  if (m->pickup_count == m->pickup_cap) { m->pickup_cap = m->pickup_cap ? m->pickup_cap * 2 : 16; m->pickups = xrealloc(m->pickups, m->pickup_cap * sizeof *m->pickups); }
  m->pickups[m->pickup_count++] = v;
}
static qa_q2_player_powerups *powers_for(qa_q2_item_module *m, qa_actor_id player) {
  for (size_t i = 0; i < m->power_count; i++) if (qa_same_actor(m->powers[i].id, player)) return &m->powers[i].v;
  if (m->power_count == m->power_cap) { m->power_cap = m->power_cap ? m->power_cap * 2 : 8; m->powers = xrealloc(m->powers, m->power_cap * sizeof *m->powers); }
  m->powers[m->power_count] = (power_state){.id = player, .v = {0,0,0,0,0,0,0,0}};
  return &m->powers[m->power_count++].v;
}
static qa_q2_player_powerups *powers_find(qa_q2_item_module *m, qa_actor_id player) {
  for (size_t i = 0; i < m->power_count; i++) if (qa_same_actor(m->powers[i].id, player)) return &m->powers[i].v;
  return NULL;
}

static void ensure_entry(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, qa_item_id it, double capacity) {
  (void)m;
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor.id, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(entries[i].item, it) == 0) return;
  qa_shared_inventory_table_configure(g->host->inventory, actor, (qa_inventory_entry){.item = it, .count = 0, .capacity = capacity}, NULL, NULL);
}
static const qa_inventory_entry *entry_of(qa_q2_game_services *g, qa_actor_id actor, qa_item_id it, qa_inventory_entry *copy) {
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(entries[i].item, it) == 0) { *copy = entries[i]; return copy; }
  return NULL;
}

static double ammo_quantity(qa_q2_entity *e, qa_q2_game_services *g, const item *it);
static size_t weapon_ammo(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const item *it, qa_pickup_ammo_grant *out);
static double weapon_owned(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id player, const item *it);
static bool weapon_eligible(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const item *it, double previous);
static bool grant(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player, const item *it);
static void finish_grant(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player, const item *it);
static void set_respawn(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, double seconds);

static void regen_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void respawn_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void drop_floor_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void make_touchable_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void mega_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void invuln_expiry_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void touch_pickup_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void temp_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void use_item_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void remove_powerups_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);

/* TS Q2ForeignItemServices bound to this module. */
static const qa_q2_item_definition *foreign_item_fn(void *ctx, const char *classname) { return catalog_get(ctx, classname); }
static qa_item_id foreign_item_id_fn(void *ctx, const qa_q2_item_definition *it) { (void)ctx; return ID(it); }
static const qa_q2_item_definition *foreign_ammunition_fn(void *ctx, size_t *out_count) {
  (void)ctx;
  *out_count = sizeof AMMUNITION / sizeof AMMUNITION[0];
  return AMMUNITION;
}
static void foreign_ensure_fn(void *ctx, qa_owned_actor actor, qa_q2_game_services *g, qa_item_id it, double capacity) {
  ensure_entry(ctx, actor, g, it, capacity);
}
static double foreign_weapon_owned_fn(void *ctx, qa_q2_game_services *g, qa_actor_id player, const qa_q2_item_definition *it) {
  return weapon_owned(ctx, g, player, it);
}
static size_t foreign_weapon_ammo_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2_item_definition *it, qa_pickup_ammo_grant *out) {
  return weapon_ammo(ctx, e, g, it, out);
}
static void foreign_weapon_picked_fn(void *ctx, qa_actor_id player, qa_item_id it, bool first) {
  qa_q2_item_module *m = ctx;
  m->hooks.weapon_picked(m->hooks.ctx, player, it, first);
}
static qa_q2_player_powerups *foreign_powers_fn(void *ctx, qa_actor_id player) { return powers_for(ctx, player); }

/* TS registerForeignItems: composition-supplied items whose classnames Q2 does
 * not already own; a collision keeps the native definition. */
static void register_foreign_items(qa_q2_item_module *m) {
  if (m->hooks.foreign_items == NULL) return;
  m->foreign_services = (qa_q2_foreign_item_services){
    .ctx = m, .item = foreign_item_fn, .item_id = foreign_item_id_fn, .ammunition = foreign_ammunition_fn,
    .ensure = foreign_ensure_fn, .weapon_owned = foreign_weapon_owned_fn, .weapon_ammo = foreign_weapon_ammo_fn,
    .weapon_picked = foreign_weapon_picked_fn, .powers = foreign_powers_fn, .regen_think = {m, regen_fn}};
  m->foreign_list = m->hooks.foreign_items(m->hooks.foreign_items_ctx, &m->foreign_services);
  for (size_t i = 0; i < m->foreign_list.count; i++) {
    const item *definition = &m->foreign_list.items[i];
    if (catalog_get(m, definition->classname)) continue;
    if (m->foreign_count == m->foreign_cap) { m->foreign_cap = m->foreign_cap ? m->foreign_cap * 2 : 32; m->foreign = xrealloc(m->foreign, m->foreign_cap * sizeof *m->foreign); }
    m->foreign[m->foreign_count++] = definition->classname;
    catalog_set(m, *definition);
  }
}

static void build_base_catalog(qa_q2_item_module *m) {
  for (size_t i = 0; i < sizeof AMMUNITION / sizeof AMMUNITION[0]; i++) catalog_set(m, AMMUNITION[i]);
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) catalog_set(m, KEYS[i]);
  for (size_t i = 0; i < sizeof REST / sizeof REST[0]; i++) catalog_set(m, REST[i]);
  for (size_t i = 0; i < sizeof BASE_WEAPONS / sizeof BASE_WEAPONS[0]; i++) {
    if (strcmp(BASE_WEAPONS[i].name, "grenades") == 0) continue;
    item w = {0};
    w.kind = "weapon"; w.classname = BASE_WEAPONS[i].classname; w.model = BASE_WEAPONS[i].world_model;
    w.icon = BASE_WEAPONS[i].icon; w.name = BASE_WEAPONS[i].display; w.sound = "misc/w_pkup.wav";
    w.rotate = true; w.respawn = 30; w.ammo = BASE_WEAPONS[i].ammo;
    catalog_set(m, w);
  }
}

static bool base_catalog_has(const char *classname, const item **out) {
  for (size_t i = 0; i < sizeof AMMUNITION / sizeof AMMUNITION[0]; i++) if (strcmp(AMMUNITION[i].classname, classname) == 0) { *out = &AMMUNITION[i]; return true; }
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) if (strcmp(KEYS[i].classname, classname) == 0) { *out = &KEYS[i]; return true; }
  for (size_t i = 0; i < sizeof REST / sizeof REST[0]; i++) if (strcmp(REST[i].classname, classname) == 0) { *out = &REST[i]; return true; }
  return false;
}

const char *qa_q2_item_pickup_name(const char *classname) {
  const item *it = NULL;
  if (base_catalog_has(classname, &it)) return it->name;
  for (size_t i = 0; i < sizeof BASE_WEAPONS / sizeof BASE_WEAPONS[0]; i++)
    if (strcmp(BASE_WEAPONS[i].classname, classname) == 0 && strcmp(BASE_WEAPONS[i].name, "grenades") != 0) return BASE_WEAPONS[i].display;
  return NULL;
}

static qa_q2_weapon_inventory_entry BASE_WINV[32];
static size_t BASE_WINV_COUNT;
static void build_base_winv(void) {
  if (BASE_WINV_COUNT) return;
  for (size_t i = 0; i < sizeof AMMUNITION / sizeof AMMUNITION[0]; i++)
    BASE_WINV[BASE_WINV_COUNT++] = (qa_q2_weapon_inventory_entry){ID(&AMMUNITION[i]), AMMUNITION[i].capacity};
  for (size_t i = 0; i < sizeof BASE_WEAPONS / sizeof BASE_WEAPONS[0]; i++) {
    if (strcmp(BASE_WEAPONS[i].name, "grenades") == 0) continue;
    BASE_WINV[BASE_WINV_COUNT++] = (qa_q2_weapon_inventory_entry){retained_id(BASE_WEAPONS[i].classname), 32767};
  }
}
const qa_q2_weapon_inventory_entry *qa_q2_base_weapon_inventory(size_t *n) {
  build_base_winv();
  *n = BASE_WINV_COUNT;
  return BASE_WINV;
}
static qa_q2_base_item_icon BASE_ICONS[32];
static size_t BASE_ICON_COUNT;
const qa_q2_base_item_icon *qa_q2_base_item_icons(size_t *n) {
  if (!BASE_ICON_COUNT) {
    for (size_t i = 0; i < sizeof AMMUNITION / sizeof AMMUNITION[0]; i++)
      BASE_ICONS[BASE_ICON_COUNT++] = (qa_q2_base_item_icon){ID(&AMMUNITION[i]), AMMUNITION[i].icon};
    for (size_t i = 0; i < sizeof BASE_WEAPONS / sizeof BASE_WEAPONS[0]; i++)
      BASE_ICONS[BASE_ICON_COUNT++] = (qa_q2_base_item_icon){BASE_WEAPONS[i].item_id, BASE_WEAPONS[i].icon};
  }
  *n = BASE_ICON_COUNT;
  return BASE_ICONS;
}
const char *qa_q2_base_weapon_display_name(qa_item_id item_id) {
  for (size_t i = 0; i < sizeof BASE_WEAPONS / sizeof BASE_WEAPONS[0]; i++)
    if (strcmp(BASE_WEAPONS[i].item_id, item_id) == 0) return BASE_WEAPONS[i].display;
  return NULL;
}

static void regen_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_item_module *m = c;
  qa_owned_actor owner;
  if (!e->has_owner || !qa_session_actor_registry_resolve_owned(g->host->actors, e->owner, &owner)) { g->remove(g->ctx, e); return; }
  qa_q2_player_powerups *st = powers_find(m, owner.id);
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, owner.id);
  double health = cs.has_value ? cs.value.health : 0;
  if (!st || st->regen_until <= g->host->now(g->host->ctx) || health < 1) { g->remove(g->ctx, e); return; }
  if (health < 200) qa_gameplay_authority_set_health(g->host->combat, owner, qa_js_min(200, health + 5));
  g->schedule(g->ctx, e, 1, (qa_q2_think){m, regen_fn});
}
static void remove_powerups_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)e; (void)g; (void)o;
  qa_q2_item_module *m = c;
  if (!a) return;
  qa_q2_player_powerups *st = powers_find(m, *a);
  if (st) *st = (qa_q2_player_powerups){0};
}

void qa_q2_item_module_register(qa_q2_item_module *m, qa_q2_item_definition it) {
  if (catalog_get(m, it.classname)) {
    static _Thread_local char buf[512];
    snprintf(buf, sizeof buf, "Q2 item already registered: %s", it.classname);
    qa_range_error("%s", buf);
  }
  catalog_set(m, it);
}
const char *qa_q2_item_module_item_name(qa_q2_item_module *m, const char *classname) {
  const item *it = catalog_get(m, classname);
  return it ? it->name : NULL;
}
const char *const *qa_q2_item_module_resource_paths(qa_q2_item_module *m, const char *const *classnames, size_t n, size_t *out_n) {
  size_t count = 0;
  for (size_t i = 0; i < m->catalog_count; i++) {
    bool wanted = false;
    for (size_t j = 0; j < n; j++) if (strcmp(classnames[j], m->catalog[i].classname) == 0) wanted = true;
    if (!wanted) continue;
    if (count + 2 > m->path_cap) {
      size_t cap = m->path_cap ? m->path_cap * 2 : 16;
      while (cap < count + 2) cap *= 2;
      m->path_scratch = xrealloc(m->path_scratch, cap * sizeof *m->path_scratch);
      m->path_bufs = xrealloc(m->path_bufs, cap * sizeof *m->path_bufs);
      for (size_t j = m->path_cap; j < cap; j++) m->path_bufs[j] = NULL;
      m->path_cap = cap;
    }
    m->path_scratch[count++] = m->catalog[i].model;
    char *buf = malloc(strlen(m->catalog[i].sound) + 7);
    if (!buf) qa_range_error("out of memory");
    snprintf(buf, strlen(m->catalog[i].sound) + 7, "sound/%s", m->catalog[i].sound);
    free(m->path_bufs[count]);
    m->path_bufs[count] = buf;
    m->path_scratch[count++] = buf;
  }
  *out_n = count;
  return m->path_scratch;
}
void qa_q2_item_module_set_pickup_policy(qa_q2_item_module *m, qa_q2_pickup_policy p) { m->policy = p; m->has_policy = true; }
void qa_q2_item_module_set_pickup_admission(qa_q2_item_module *m, const qa_pickup_admission *a) { m->admission = a; }
bool qa_q2_item_module_maps_supply(qa_q2_item_module *m, qa_item_id it) {
  return m->admission && (m->admission->maps(m->admission->ctx, "ammo", it) || m->admission->maps(m->admission->ctx, "weapons", it));
}

qa_q2_items_checkpoint qa_q2_item_module_capture(qa_q2_item_module *m, qa_q2_game_services *g) {
  free(m->cp_pickups); free(m->cp_powers); free(m->cp_bindings);
  m->cp_pickups = NULL; m->cp_powers = NULL; m->cp_bindings = NULL;
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  m->cp_pickups = malloc((n ? n : 1) * sizeof *m->cp_pickups);
  m->cp_powers = malloc((n ? n : 1) * sizeof *m->cp_powers);
  m->cp_bindings = malloc((n ? n : 1) * sizeof *m->cp_bindings);
  if (!m->cp_pickups || !m->cp_powers || !m->cp_bindings) qa_range_error("out of memory");
  size_t pn = 0, wn = 0, bn = 0;
  for (size_t i = 0; i < n; i++) {
    qa_saved_actor_id actor = {(double)entities[i]->actor.id.slot, (double)entities[i]->actor.id.generation};
    pickup_state *p = find_pickup(m, entities[i]);
    if (p) m->cp_pickups[pn++] = (qa_q2_item_pickup_checkpoint){actor, p->it->classname, p->targets_used, p->retained, p->has_expires, p->expires_at};
    qa_q2_player_powerups *pw = powers_find(m, entities[i]->actor.id);
    if (pw) m->cp_powers[wn++] = (qa_q2_item_power_checkpoint){actor, *pw};
    for (size_t j = 0; j < m->binding_count; j++)
      if (qa_same_actor(m->bindings[j], entities[i]->actor.id)) m->cp_bindings[bn++] = actor;
  }
  return (qa_q2_items_checkpoint){m->power_cube_count, m->cp_pickups, pn, m->cp_powers, wn, m->cp_bindings, bn};
}

static void bind_power_armor(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g);
void qa_q2_item_module_restore(qa_q2_item_module *m, qa_q2_game_services *g, qa_q2_items_checkpoint cp) {
  m->pickup_count = 0; m->power_count = 0; m->binding_count = 0;
  m->power_cube_count = cp.power_cube_count;
  for (size_t i = 0; i < cp.pickup_count; i++) {
    qa_owned_actor actor = qa_q2_restore_actor(g, cp.pickups[i].actor);
    qa_q2_entity *e = g->entity(g->ctx, &actor.id);
    const item *it = catalog_get(m, cp.pickups[i].classname);
    if (!e || !it) {
      static _Thread_local char buf[512];
      snprintf(buf, sizeof buf, "Q2 item checkpoint cannot resolve %s", cp.pickups[i].classname);
      qa_range_error("%s", buf);
    }
    set_pickup(m, e, (pickup_state){e, it, cp.pickups[i].targets_used, cp.pickups[i].retained, cp.pickups[i].has_expires_at, cp.pickups[i].expires_at});
  }
  for (size_t i = 0; i < cp.power_count; i++) {
    qa_owned_actor actor = qa_q2_restore_actor(g, cp.powers[i].actor);
    *powers_for(m, actor.id) = cp.powers[i].state;
  }
  for (size_t i = 0; i < cp.binding_count; i++) bind_power_armor(m, qa_q2_restore_actor(g, cp.power_armor_bindings[i]), g);
}

const qa_q2_inventory_item *qa_q2_item_module_list(qa_q2_item_module *m, size_t *n) {
  size_t count = 0;
  for (size_t i = 0; i < m->catalog_count; i++) {
    const item *it = &m->catalog[i];
    if (is_foreign(m, it->classname)) continue;
    if (count == m->list_cap) { m->list_cap = m->list_cap ? m->list_cap * 2 : 32; m->list_scratch = xrealloc(m->list_scratch, m->list_cap * sizeof *m->list_scratch); }
    bool ammo = strcmp(it->kind, "ammo") == 0, custom = strcmp(it->kind, "custom") == 0, weapon = strcmp(it->kind, "weapon") == 0;
    m->list_scratch[count++] = (qa_q2_inventory_item){
      .id = ID(it), .classname = it->classname, .name = it->name, .kind = it->kind,
      .quantity = (ammo || custom) ? it->quantity : 1,
      .usable = custom ? it->has_use : (strcmp(it->kind, "power") == 0 || strcmp(it->kind, "power-armor") == 0 || weapon || (ammo && it->weapon_ammo)),
      .droppable = custom ? it->droppable : (strcmp(it->kind, "key") == 0 || ammo || strcmp(it->kind, "power") == 0 || strcmp(it->kind, "power-armor") == 0 || (weapon && strcmp(it->classname, "weapon_blaster") != 0)),
      .stay_coop = stays_coop(it),
      .capacity = (ammo || custom) ? it->capacity : 32767,
      .weapon = weapon || (ammo && it->weapon_ammo),
      .console_give = it->console_give ? it->console_give : "pickup"};
  }
  *n = count;
  return m->list_scratch;
}
static bool ieq2(const char *a, const char *b) {
  return qa_js_equals_ignore_case(a, b); /* TS toLowerCase() === key */
}
bool qa_q2_item_module_lookup(qa_q2_item_module *m, const char *value, qa_q2_inventory_item *out) {
  size_t n = 0;
  const qa_q2_inventory_item *list = qa_q2_item_module_list(m, &n);
  for (size_t i = 0; i < n; i++)
    if (ieq2(list[i].id, value) || ieq2(list[i].classname, value) || ieq2(list[i].name, value)) { *out = list[i]; return true; }
  return false;
}

static bool instanced_coop(qa_q2_item_module *m, qa_q2_game_services *g) {
  return m->has_policy && m->policy.instanced_coop && m->policy.instanced_coop(m->policy.ctx, g);
}

void qa_q2_item_module_give_start_items(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, const char *expression) {
  size_t count = 0;
  qa_q2_start_item *grants = qa_q2_parse_start_items(expression, &count);
  const item **items = malloc((count ? count : 1) * sizeof *items);
  if (!items) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    /* TS lowercases the classname for catalog lookup */
    const item *found = NULL;
    for (size_t j = 0; j < m->catalog_count; j++) if (ieq2(m->catalog[j].classname, grants[i].classname)) { found = &m->catalog[j]; break; }
    if (!found || (found->console_give && strcmp(found->console_give, "inventory-only") == 0)) {
      static _Thread_local char buf[512];
      snprintf(buf, sizeof buf, "Invalid Q2 starting item: %s", grants[i].classname);
      free(items); qa_q2_start_items_free(grants, count);
      qa_range_error("%s", buf);
    }
    items[i] = found;
  }
  for (size_t i = 0; i < count; i++) {
    const item *it = items[i];
    double cnt = grants[i].count;
    if (cnt == 0) {
      qa_item_id dests[16];
      size_t dn = 0;
      dests[dn++] = ID(it);
      if (m->admission && strcmp(it->kind, "ammo") == 0 && m->admission->maps(m->admission->ctx, "ammo", ID(it))) {
        qa_pickup_supply_offer offer = {0};
        offer.kind = QA_PICKUP_SUPPLY_AMMO; offer.grant = (qa_pickup_ammo_grant){ID(it), 0};
        qa_pickup_supply_preview pv = m->admission->preview(m->admission->ctx, player.id, offer);
        dn = 0;
        for (size_t j = 0; j < pv.ammo_count && dn < 16; j++) dests[dn++] = pv.ammo[j].item;
        if (it->weapon_ammo) {
          qa_pickup_supply_offer woffer = {0};
          woffer.kind = QA_PICKUP_SUPPLY_WEAPON; woffer.item = ID(it);
          qa_pickup_supply_preview wpv = m->admission->preview(m->admission->ctx, player.id, woffer);
          for (size_t j = 0; j < wpv.weapon_count && dn < 16; j++) dests[dn++] = wpv.weapons[j].item;
        }
      } else if (m->admission && strcmp(it->kind, "weapon") == 0 && m->admission->maps(m->admission->ctx, "weapons", ID(it)) && strcmp(ID(it), "q2:weapon_blaster") != 0) {
        qa_pickup_supply_offer woffer = {0};
        woffer.kind = QA_PICKUP_SUPPLY_WEAPON; woffer.item = ID(it);
        qa_pickup_supply_preview wpv = m->admission->preview(m->admission->ctx, player.id, woffer);
        dn = 0;
        for (size_t j = 0; j < wpv.weapon_count && dn < 16; j++) dests[dn++] = wpv.weapons[j].item;
      }
      for (size_t j = 0; j < dn; j++) {
        bool dup = false;
        for (size_t k = 0; k < j; k++) if (strcmp(dests[k], dests[j]) == 0) dup = true;
        if (dup) continue;
        qa_inventory_entry entry;
        if (entry_of(g, player.id, dests[j], &entry)) {
          entry.count = 0;
          qa_shared_inventory_table_configure(g->host->inventory, player, entry, NULL, NULL);
        }
      }
      continue;
    }
    qa_q2_entity *temporary = g->create(g->ctx, it->classname, NULL, 0);
    temporary->count = cnt;
    temporary->spawnflags = (double)(qa_js_to_int32(temporary->spawnflags) | 0x10000);
    set_pickup(m, temporary, (pickup_state){temporary, it, false, false, false, 0});
    if (grant(m, temporary, g, player, it)) finish_grant(m, temporary, g, player, it);
    if (qa_session_actor_registry_is_live(g->host->actors, temporary->actor.id)) g->remove(g->ctx, temporary);
  }
  free(items);
  qa_q2_start_items_free(grants, count);
}

void qa_q2_item_module_give_ammo_count(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, qa_item_id item_id, const double *count) {
  qa_q2_inventory_item found;
  if (!qa_q2_item_module_lookup(m, item_id, &found) || strcmp(found.kind, "ammo") != 0)
    qa_range_error("Console ammo grant requires a source ammo item");
  bool mapped = m->admission && m->admission->maps(m->admission->ctx, "ammo", found.id);
  qa_item_id dests[16];
  size_t dn = 0;
  if (mapped) {
    qa_pickup_supply_offer offer = {0};
    offer.kind = QA_PICKUP_SUPPLY_AMMO; offer.grant = (qa_pickup_ammo_grant){found.id, 0};
    qa_pickup_supply_preview pv = m->admission->preview(m->admission->ctx, player.id, offer);
    for (size_t i = 0; i < pv.ammo_count && dn < 16; i++) dests[dn++] = pv.ammo[i].item;
  } else dests[dn++] = found.id;
  for (size_t i = 0; i < dn; i++) {
    qa_inventory_entry entry;
    bool has = entry_of(g, player.id, dests[i], &entry) != NULL;
    if (mapped && !has) qa_range_error("Console ammo destination was not admitted");
    double next = count ? *count : (has ? entry.count : 0) + found.quantity;
    if (mapped && has) {
      entry.count = (entry.has_count_policy && strcmp(entry.count_policy.kind, "source-counter") == 0) ? next : qa_js_max(0, next);
      qa_shared_inventory_table_configure(g->host->inventory, player, entry, NULL, NULL);
    } else {
      qa_inventory_entry ne = {.item = dests[i], .count = next, .capacity = has ? entry.capacity : found.capacity,
        .has_count_policy = true, .count_policy = {.kind = "source-counter", .arithmetic = "int32"}};
      qa_shared_inventory_table_configure(g->host->inventory, player, ne, NULL, NULL);
    }
  }
}

void qa_q2_item_module_clear_powerups(qa_q2_item_module *m, qa_actor_id player) {
  for (size_t i = 0; i < m->power_count; i++)
    if (qa_same_actor(m->powers[i].id, player)) { memmove(&m->powers[i], &m->powers[i + 1], (m->power_count - i - 1) * sizeof *m->powers); m->power_count--; return; }
}

static qa_q2_entity *drop_source(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, const item *it, qa_q2_drop_options options, double count) {
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, actor.id);
  if (!body.has_value) qa_range_error("Item drop owner has no shared body");
  qa_q2_entity *dropped = g->create(g->ctx, it->classname, NULL, 0);
  dropped->model = it->model;
  dropped->has_owner = true; dropped->owner = actor.id;
  dropped->spawnflags = options.player_death ? 0x20000 : 0x10000;
  dropped->effects = it->rotate ? 1 : 0;
  dropped->render_flags = 512 | 0x8000;
  dropped->count = count;
  set_pickup(m, dropped, (pickup_state){dropped, it, false, false, options.has_expires_at, options.expires_at});
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, actor.id);
  qa_vec3 angles = view.has_value ? view.value.view_angles : body.value.angles;
  qa_vec3 mang = angles;
  mang.y += options.has_yaw_offset ? options.yaw_offset : 0;
  qa_vec3 forward = qa_q2_movedir(mang);
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_vec3 origin;
  if (g->host->is_player(g->host->ctx, actor.id)) {
    qa_q2_trace_request tr = {.start = body.value.origin,
      .end = qa_q2_add(qa_q2_add(body.value.origin, qa_q2_scale(forward, 24)), (qa_vec3){0, 0, -16}),
      .bounds = &bounds, .ignore = &actor.id, .mask = 1};
    origin = g->host->trace(g->host->ctx, tr).end;
  } else origin = body.value.origin;
  qa_q2_body_changes ch = {0};
  ch.has_origin = true; ch.origin = origin;
  ch.has_bounds = true; ch.bounds = bounds;
  qa_vec3 vel = qa_q2_scale(forward, 100);
  vel.z = 300;
  ch.has_velocity = true; ch.velocity = vel;
  g->move(g->ctx, dropped, ch, false);
  dropped->touch = (qa_q2_touch){m, temp_touch_fn};
  g->solid(g->ctx, dropped, "trigger");
  g->motion(g->ctx, dropped, "toss");
  g->show(g->ctx, dropped);
  if (options.immediate_touch) make_touchable_fn(m, dropped, g);
  else g->schedule(g->ctx, dropped, 1, (qa_q2_think){m, make_touchable_fn});
  return dropped;
}

qa_q2_entity *qa_q2_item_module_drop(qa_q2_item_module *m, qa_q2_entity *self, qa_q2_game_services *g, qa_item_id item_id, qa_q2_drop_options options) {
  const item *it = strncmp(item_id, "q2:", 3) == 0 ? catalog_get(m, item_id + 3) : NULL;
  qa_q2_inventory_item descriptor;
  bool has = qa_q2_item_module_lookup(m, item_id, &descriptor);
  if (g->options.instagib && it != NULL && strcmp(it->kind, "weapon") == 0) return NULL;
  if (it != NULL && strcmp(it->kind, "ammo") == 0 && g->options.edition == QA_Q2_EDITION_RERELEASE &&
      (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 8192) != 0 && !qa_q2_no_infinite_ammo(&g->options, ID(it)))
    return NULL;
  if (!it || !has || !descriptor.droppable ||
      (g->options.mode == QA_Q2_MODE_COOP && !instanced_coop(m, g) && descriptor.stay_coop)) return NULL;
  double count = qa_shared_inventory_table_count(g->host->inventory, self->actor.id, item_id);
  return drop_source(m, self->actor, g, it, options, strcmp(it->kind, "ammo") == 0 ? qa_js_min(count, it->quantity) : 0);
}
qa_q2_entity *qa_q2_item_module_drop_monster(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, const char *classname) {
  qa_q2_inventory_item descriptor;
  if (!qa_q2_item_module_lookup(m, classname, &descriptor)) return NULL;
  const item *it = catalog_get(m, descriptor.classname);
  if (!it) return NULL;
  return drop_source(m, actor, g, it, (qa_q2_drop_options){0}, 0);
}

qa_q2_player_powerups qa_q2_item_module_player_powerups(qa_q2_item_module *m, qa_actor_id player) {
  qa_q2_player_powerups *p = powers_find(m, player);
  return p ? *p : (qa_q2_player_powerups){0};
}

const qa_q2_weapon_inventory_entry *qa_q2_item_module_weapon_inventory(qa_q2_item_module *m, size_t *n) {
  size_t count = 0;
  for (size_t i = 0; i < m->catalog_count; i++) {
    const item *it = &m->catalog[i];
    bool ammo = strcmp(it->kind, "ammo") == 0, weapon = strcmp(it->kind, "weapon") == 0;
    if (!ammo && !weapon) continue;
    if (count == m->winv_cap) { m->winv_cap = m->winv_cap ? m->winv_cap * 2 : 16; m->winv_scratch = xrealloc(m->winv_scratch, m->winv_cap * sizeof *m->winv_scratch); }
    m->winv_scratch[count++] = (qa_q2_weapon_inventory_entry){ID(it), ammo ? it->capacity : 32767};
  }
  *n = count;
  return m->winv_scratch;
}

static double cell_read(void *ctx) {
  struct cell_ctx *c = ctx;
  return qa_shared_inventory_table_count(c->g->host->inventory, c->actor.id, "q2:ammo_cells");
}
static void cell_write(void *ctx, double count) {
  struct cell_ctx *c = ctx;
  qa_inventory_entry entry;
  if (!entry_of(c->g, c->actor.id, "q2:ammo_cells", &entry)) qa_range_error("Q2 power armor requires its admitted cell inventory");
  entry.count = count;
  qa_shared_inventory_table_configure(c->g->host->inventory, c->actor, entry, NULL, NULL);
}
static void bind_power_armor(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g) {
  for (size_t i = 0; i < m->binding_count; i++) if (qa_same_actor(m->bindings[i], actor.id)) return;
  if (m->cell_count == m->cell_cap) { m->cell_cap = m->cell_cap ? m->cell_cap * 2 : 8; m->cell_ctxs = xrealloc(m->cell_ctxs, m->cell_cap * sizeof *m->cell_ctxs); }
  m->cell_ctxs[m->cell_count] = (struct cell_ctx){g, actor};
  struct cell_ctx *ctx = &m->cell_ctxs[m->cell_count++];
  qa_power_armor_cell_binding binding = {.ctx = ctx, .read = cell_read, .write = cell_write};
  qa_gameplay_authority_bind_power_armor_cells(g->host->combat, actor, binding);
  if (m->binding_count == m->binding_cap) { m->binding_cap = m->binding_cap ? m->binding_cap * 2 : 8; m->bindings = xrealloc(m->bindings, m->binding_cap * sizeof *m->bindings); }
  m->bindings[m->binding_count++] = actor.id;
}

void qa_q2_item_module_configure_player(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, bool give_blaster) {
  size_t wn = 0;
  const qa_q2_weapon_inventory_entry *weapons = qa_q2_item_module_weapon_inventory(m, &wn);
  for (size_t i = 0; i < m->catalog_count; i++) {
    const item *it = &m->catalog[i];
    const char *k = it->kind;
    if (strcmp(k, "ammo") != 0 && strcmp(k, "weapon") != 0 && strcmp(k, "power") != 0 && strcmp(k, "power-armor") != 0 && strcmp(k, "key") != 0 && strcmp(k, "custom") != 0) continue;
    double capacity = strcmp(k, "custom") == 0 ? it->capacity : 32767;
    for (size_t j = 0; j < wn; j++) if (strcmp(weapons[j].item, ID(it)) == 0) { capacity = weapons[j].capacity; break; }
    ensure_entry(m, actor, g, ID(it), capacity);
  }
  if (g->options.instagib) {
    if (give_blaster && qa_shared_inventory_table_count(g->host->inventory, actor.id, "q2:weapon_railgun") == 0)
      qa_shared_inventory_table_give(g->host->inventory, actor, "q2:weapon_railgun", 1);
    qa_inventory_entry slugs;
    if (give_blaster && entry_of(g, actor.id, "q2:ammo_slugs", &slugs) != NULL && slugs.count < 99) {
      slugs.count = 99;
      slugs.capacity = qa_js_max(slugs.capacity, 99);
      qa_shared_inventory_table_configure(g->host->inventory, actor, slugs, NULL, NULL);
    }
  } else if (give_blaster && qa_shared_inventory_table_count(g->host->inventory, actor.id, "q2:weapon_blaster") == 0)
    qa_shared_inventory_table_give(g->host->inventory, actor, "q2:weapon_blaster", 1);
  bind_power_armor(m, actor, g);
}

bool qa_q2_item_module_spawn_item(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const char *descriptor) {
  const item *it = catalog_get(m, descriptor);
  if (!it) return false;
  int32_t flags = qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)); /* TS dmflags & x: ToInt32 */
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && g->options.edition == QA_Q2_EDITION_RERELEASE && (flags & 8192) != 0 &&
      strcmp(it->classname, "weapon_bfg") == 0 && catalog_get(m, "weapon_disintegrator") != NULL) {
    e->classname = "weapon_disintegrator";
    return qa_q2_item_module_spawn_item(m, e, g, "weapon_disintegrator");
  }
  if (g->options.instagib && strcmp(it->kind, "key") != 0 && strcmp(it->kind, "custom") != 0) {
    g->remove(g->ctx, e);
    return true;
  }
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && (
      ((flags & 1) && (strcmp(it->kind, "health") == 0 || strcmp(it->kind, "maximum-health") == 0)) ||
      ((flags & 2) && strcmp(it->kind, "power") == 0) ||
      ((flags & 2048) && (strcmp(it->kind, "armor") == 0 || strcmp(it->kind, "shard") == 0 || strcmp(it->kind, "power-armor") == 0)) ||
      ((flags & 8192) && ((strcmp(it->kind, "ammo") == 0 && !it->weapon_ammo && !qa_q2_no_infinite_ammo(&g->options, ID(it))) ||
                           strcmp(it->classname, "weapon_bfg") == 0)))) {
    g->remove(g->ctx, e);
    return true;
  }
  set_pickup(m, e, (pickup_state){e, it, false, false, false, 0});
  if (g->options.mode == QA_Q2_MODE_COOP && (strcmp(it->classname, "key_power_cube") == 0 || (g->options.edition == QA_Q2_EDITION_RERELEASE && strcmp(it->classname, "key_explosive_charges") == 0)))
    /* spawnflags |= 1 << (8 + powerCubeCount++): JS masks the shift count to 5 bits, so
     * the 25th cube wraps to bit 1; a C int shift by 32 or more is undefined. */
    e->spawnflags = qa_js_bit_or(e->spawnflags, qa_js_shl(1, 8 + m->power_cube_count++));
  if (e->model[0] == '\0') e->model = it->model;
  if (it->rotate) e->effects = (double)(qa_js_to_int32(e->effects) | 1);
  e->render_flags = (double)(qa_js_to_int32(e->render_flags) | 512);
  if (strcmp(it->classname, "key_commander_head") == 0) e->effects = (double)(qa_js_to_int32(e->effects) | 2);
  g->schedule(g->ctx, e, 2 * g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, drop_floor_fn});
  return true;
}
bool qa_q2_item_module_spawn(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  if (strcmp(e->classname, "target_remove_powerups") == 0) { e->use = (qa_q2_use){m, remove_powerups_fn}; return true; }
  return qa_q2_item_module_spawn_item(m, e, g, e->classname);
}
static bool spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_item_module_spawn(c, e, g); }
static const char *item_name_cb(void *c, const char *classname) { return qa_q2_item_module_item_name(c, classname); }

const qa_q2_item_definition *qa_q2_item_module_item_definition(qa_q2_item_module *m, qa_q2_entity *e) {
  pickup_state *p = find_pickup(m, e);
  return p ? p->it : NULL;
}

static double ammo_quantity(qa_q2_entity *e, qa_q2_game_services *g, const item *it) {
  if (it->weapon_ammo && !it->infinite_ammo_is_null && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 8192) != 0 &&
      !qa_q2_no_infinite_ammo(&g->options, ID(it)))
    return it->has_infinite_ammo_quantity ? it->infinite_ammo_quantity : 1000;
  return e->count != 0 ? e->count : it->quantity;
}
static size_t weapon_ammo(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const item *it, qa_pickup_ammo_grant *out) {
  if ((qa_js_to_int32(e->spawnflags) & 0x10000) != 0 || !it->ammo) return 0;
  const item *ammo = strncmp(it->ammo, "q2:", 3) == 0 ? catalog_get(m, it->ammo + 3) : NULL;
  if (!ammo || strcmp(ammo->kind, "ammo") != 0) return 0;
  /* TS { item: id(ammo), amount: ... }: the id before the dmflags getter. */
  qa_item_id ammo_id = ID(ammo);
  double amount = (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 8192) != 0 &&
                  !qa_q2_no_infinite_ammo(&g->options, ammo_id) ? 1000 : ammo->quantity;
  out[0] = (qa_pickup_ammo_grant){ammo_id, amount};
  return 1;
}
static double weapon_owned(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id player, const item *it) {
  if (m->admission && m->admission->maps(m->admission->ctx, "weapons", ID(it)))
    return m->admission->owns(m->admission->ctx, player, ID(it)) ? 1 : 0;
  return qa_shared_inventory_table_count(g->host->inventory, player, ID(it));
}
static bool weapon_eligible(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const item *it, double previous) {
  if (m->admission && m->admission->maps(m->admission->ctx, "weapons", ID(it)) && strcmp(ID(it), "q2:weapon_blaster") == 0) return false;
  bool stays = g->options.mode == QA_Q2_MODE_COOP ? !instanced_coop(m, g)
      : (g->options.mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 4) != 0);
  return !stays || previous <= 0 || is_dropped(e);
}
static bool owner_can_touch(qa_q2_entity *e, qa_actor_id player) {
  return !(e->has_owner && qa_same_actor(e->owner, player));
}

bool qa_q2_item_module_observe_supply(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id pickup_actor, qa_actor_id recipient, qa_pickup_supply_observation *out) {
  qa_q2_entity *e = g->entity(g->ctx, &pickup_actor);
  pickup_state *p = e ? find_pickup(m, e) : NULL;
  if (!e || !p) return false;
  const item *it = p->it;
  qa_pickup_supply_offer offer = {0};
  static _Thread_local qa_pickup_ammo_grant grants[1];
  if (strcmp(it->kind, "ammo") == 0) {
    const char * item2 = ID(it);
    double amount = ammo_quantity(e, g, it);
    offer.grant = (qa_pickup_ammo_grant){item2, amount};
    if (it->weapon_ammo) { offer.kind = QA_PICKUP_SUPPLY_AMMO_WEAPON; offer.weapon = ID(it); }
    else offer.kind = QA_PICKUP_SUPPLY_AMMO;
  } else if (strcmp(it->kind, "weapon") == 0) {
    offer.kind = QA_PICKUP_SUPPLY_WEAPON;
    offer.item = ID(it);
    offer.ammo_count = weapon_ammo(m, e, g, it, grants);
    offer.ammo = grants;
  } else return false;
  out->actor = pickup_actor;
  out->offer = offer;
  if (!e->visible || strcmp(e->solid, "trigger") != 0) {
    if (qa_q2_spawn_has(&e->spawn, "team") || m->hooks.random_respawn) { out->availability_kind = "inactive"; return true; }
    if (e->think.fn == respawn_fn && e->think.ctx == m && e->has_next_think) {
      out->availability_kind = "respawning"; out->at_seconds = e->next_think; return true;
    }
    out->availability_kind = "inactive";
    return true;
  }
  bool is_touch = (e->touch.fn == touch_pickup_fn || e->touch.fn == temp_touch_fn) && e->touch.ctx == m;
  if (!is_touch) { out->availability_kind = "inactive"; return true; }
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, recipient);
  bool eligible = g->host->is_player(g->host->ctx, recipient) && qa_session_actor_registry_is_live(g->host->actors, recipient) &&
      (cs.has_value ? cs.value.health : 0) >= 1 &&
      (!(e->touch.fn == temp_touch_fn && e->touch.ctx == m) || owner_can_touch(e, recipient)) &&
      (!m->has_policy || !m->policy.can_pickup || m->policy.can_pickup(m->policy.ctx, e, g, recipient)) &&
      (strcmp(it->kind, "weapon") != 0 || weapon_eligible(m, e, g, it, weapon_owned(m, g, recipient, it)));
  out->availability_kind = "ready";
  out->eligible = eligible;
  return true;
}

bool qa_q2_item_module_preview_supply(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id pickup_actor, qa_actor_id recipient, qa_pickup_supply_preview *out) {
  qa_pickup_supply_observation obs = {0};
  if (!qa_q2_item_module_observe_supply(m, g, pickup_actor, recipient, &obs)) return false;
  qa_pickup_supply_offer offer = obs.offer;
  if (m->admission) {
    if (offer.kind == QA_PICKUP_SUPPLY_WEAPON && strcmp(offer.item, "q2:weapon_blaster") == 0) {
      *out = (qa_pickup_supply_preview){0};
      return true;
    }
    *out = m->admission->preview(m->admission->ctx, recipient, offer);
    return true;
  }
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, recipient, &n);
  qa_inventory_entry *inventory = malloc((n + 4) * sizeof *inventory);
  if (!inventory) qa_range_error("out of memory");
  memcpy(inventory, entries, n * sizeof *inventory);
  size_t inv_count = n;
  qa_pickup_ammo_grant ammo[2];
  size_t ammo_n = 0;
  if (offer.kind == QA_PICKUP_SUPPLY_WEAPON) { for (size_t i = 0; i < offer.ammo_count; i++) ammo[ammo_n++] = offer.ammo[i]; }
  else ammo[ammo_n++] = offer.grant;
  for (size_t i = 0; i < ammo_n; i++) {
    const item *descriptor = strncmp(ammo[i].item, "q2:", 3) == 0 ? catalog_get(m, ammo[i].item + 3) : NULL;
    if (!descriptor || strcmp(descriptor->kind, "ammo") != 0) { free(inventory); qa_range_error("Weapon grant has no source ammo descriptor"); }
    bool exists = false;
    for (size_t j = 0; j < inv_count; j++) if (strcmp(inventory[j].item, ammo[i].item) == 0) exists = true;
    if (!exists) inventory[inv_count++] = (qa_inventory_entry){.item = ammo[i].item, .count = 0, .capacity = descriptor->capacity};
  }
  qa_pickup_supply_preview result;
  if (offer.kind == QA_PICKUP_SUPPLY_WEAPON) {
    bool exists = false;
    for (size_t j = 0; j < inv_count; j++) if (strcmp(inventory[j].item, offer.item) == 0) exists = true;
    if (!exists) inventory[inv_count++] = (qa_inventory_entry){.item = offer.item, .count = 0, .capacity = 32767};
    qa_pickup_ammo_grant weapon_grant = {offer.item, 1};
    qa_pickup_grant_plan plan = {.is_weapon = true, .weapons = &weapon_grant, .weapon_count = 1, .ammo = ammo, .ammo_count = ammo_n};
    result = qa_preview_pickup_grants(inventory, inv_count, plan);
  } else {
    qa_item_id shared = offer.kind == QA_PICKUP_SUPPLY_AMMO_WEAPON ? offer.weapon : NULL;
    qa_pickup_grant_plan plan = {.is_weapon = false, .ammo = ammo, .ammo_count = ammo_n, .acceptance = "nonzero",
      .weapons_shared_ammo = true, .shared_items = shared ? &shared : NULL, .shared_item_count = shared ? 1 : 0};
    result = qa_preview_pickup_grants(inventory, inv_count, plan);
  }
  free(inventory);
  *out = result;
  return true;
}

void qa_q2_item_module_replace_item(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const char *descriptor) {
  const item *it = catalog_get(m, descriptor);
  if (!it) {
    static _Thread_local char buf[512];
    snprintf(buf, sizeof buf, "Q2 replacement item is not registered: %s", descriptor);
    qa_range_error("%s", buf);
  }
  pickup_state state = *pickup_of(m, e);
  state.it = it;
  set_pickup(m, e, state);
  e->classname = it->classname;
  e->model = it->model;
  e->effects = it->rotate ? 1 : 0;
  g->show(g->ctx, e);
}

static void respawn_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_item_module *m = c;
  const char *team = qa_q2_spawn_get(&e->spawn, "team");
  qa_q2_entity *candidates[64];
  size_t cn = 0;
  if (!team) candidates[cn++] = e;
  else {
    for (qa_q2_entity *member = e->has_team_master ? g->entity(g->ctx, &e->team_master) : NULL; member && cn < 64;
         member = member->has_chain ? g->entity(g->ctx, &member->chain) : NULL)
      candidates[cn++] = member;
  }
  size_t idx = cn ? (size_t)floor(g->host->random(g->host->ctx) * (double)cn) : 0;
  qa_q2_entity *selected = cn && idx < cn ? candidates[idx] : e;
  if (g->options.edition == QA_Q2_EDITION_CLASSIC && m->hooks.random_respawn) {
    qa_q2_entity *replacement = m->hooks.random_respawn(m->hooks.ctx, selected, g);
    if (replacement && replacement != selected) { g->remove(g->ctx, selected); selected = replacement; }
  }
  if (!qa_session_actor_registry_is_live(g->host->actors, selected->actor.id)) return;
  selected->visible = true;
  g->solid(g->ctx, selected, "trigger");
  g->show(g->ctx, selected);
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = "q2:item-respawn"; ev.origin = qa_q2_game_body(g, selected).origin; ev.direction = QA_Q2_ZERO; ev.count = 1;
  g->host->emit(g->host->ctx, ev);
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && m->hooks.random_respawn) m->hooks.random_respawn(m->hooks.ctx, selected, g);
}
static void set_respawn(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, double seconds) {
  pickup_of(m, e)->retained = true;
  e->visible = false;
  g->solid(g->ctx, e, "none");
  if (!qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) return;
  g->show(g->ctx, e);
  if (!qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) return;
  g->schedule(g->ctx, e, seconds, (qa_q2_think){m, respawn_fn});
}
static void drop_floor_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_item_module *m = c;
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_q2_body_changes ch = {0}; ch.has_bounds = true; ch.bounds = bounds;
  g->move(g->ctx, e, ch, false);
  qa_vec3 origin = qa_q2_game_body(g, e).origin;
  qa_q2_trace_request tr = {.start = origin, .end = qa_q2_add(origin, (qa_vec3){0, 0, -128}), .bounds = &bounds, .ignore = &e->actor.id, .mask = 3};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  if (trace.start_solid) {
    static _Thread_local char buf[512];
    snprintf(buf, sizeof buf, "Q2 %s starts solid", e->classname);
    g->host->diagnostic(g->host->ctx, buf);
    g->remove(g->ctx, e);
    return;
  }
  qa_q2_body_changes oc = {0}; oc.has_origin = true; oc.origin = trace.end;
  g->move(g->ctx, e, oc, true);
  e->touch = (qa_q2_touch){m, touch_pickup_fn};
  g->solid(g->ctx, e, "trigger");
  g->motion(g->ctx, e, "toss");
  if (qa_q2_spawn_has(&e->spawn, "team")) {
    e->flags = (double)(qa_js_to_int32(e->flags) & ~0x400);
    e->has_chain = e->has_team_chain; e->chain = e->team_chain;
    e->has_team_chain = false;
    e->visible = false;
    g->solid(g->ctx, e, "none");
    if (e->has_team_master && qa_same_actor(e->team_master, e->actor.id))
      g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, respawn_fn});
  }
  if ((qa_js_to_int32(e->spawnflags) & 2) != 0) {
    e->touch = (qa_q2_touch){0};
    e->effects = (double)(qa_js_to_int32(e->effects) & ~1);
    e->render_flags = (double)(qa_js_to_int32(e->render_flags) & ~512);
    g->solid(g->ctx, e, "box");
  }
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0) {
    e->visible = false;
    g->solid(g->ctx, e, "none");
    e->use = (qa_q2_use){m, use_item_fn};
  }
  g->show(g->ctx, e);
}

typedef struct touch_ctx {
  qa_q2_item_module *m; qa_q2_entity *e; qa_q2_game_services *g;
  qa_actor_id player; qa_owned_actor owner; pickup_state *state; const item *it; bool original_ran;
} touch_ctx;
static bool touch_live(touch_ctx *t) {
  qa_owned_actor cur;
  return t->g->entity(t->g->ctx, &t->e->actor.id) == t->e &&
         qa_session_actor_registry_resolve_owned(t->g->host->actors, t->player, &cur) && qa_same_owned_actor(cur, t->owner);
}
static bool touch_eligible(void *c) {
  touch_ctx *t = c;
  bool before = !t->m->has_policy || !t->m->policy.before_pickup || t->m->policy.before_pickup(t->m->policy.ctx, t->e, t->g, t->player);
  return before && touch_live(t);
}
static bool touch_original(void *c) {
  touch_ctx *t = c;
  t->original_ran = true;
  t->state->retained = false;
  return grant(t->m, t->e, t->g, t->owner, t->it);
}
static void touch_complete(void *c, bool taken) {
  touch_ctx *t = c;
  qa_q2_item_module *m = t->m;
  qa_q2_game_services *g = t->g;
  qa_q2_entity *e = t->e;
  if (!touch_live(t)) return;
  if (taken) {
    if (!t->original_ran) t->state->retained = false;
    finish_grant(m, e, g, t->owner, t->it);
    if (!touch_live(t)) return;
    qa_q2_presentation_event ev = {0};
    ev.kind = "pickup"; ev.player = t->player; ev.item = ID(t->it); ev.icon = t->it->icon; ev.name = t->it->name;
    g->host->emit(g->host->ctx, ev);
    if (!touch_live(t)) return;
    if (m->hooks.picked_up != NULL) {
      qa_q2_inventory_item looked;
      bool usable = qa_q2_item_module_lookup(m, t->it->classname, &looked) && looked.usable &&
                    qa_shared_inventory_table_count(g->host->inventory, t->player, ID(t->it)) != 0;
      m->hooks.picked_up(m->hooks.ctx, t->player, ID(t->it), usable);
    }
    if (!touch_live(t)) return;
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, t->player);
    if (body.has_value) {
      qa_q2_presentation_event sev = {0};
      sev.kind = "sound"; sev.has_actor = true; sev.actor = t->player; sev.origin = body.value.origin;
      sev.path = t->it->sound; sev.channel = 3; sev.volume = 1; sev.attenuation = 1; sev.loop = "once";
      g->host->emit(g->host->ctx, sev);
    }
    if (!touch_live(t)) return;
  }
  if (m->has_policy && m->policy.before_targets) m->policy.before_targets(m->policy.ctx, e, g, t->player, taken);
  if (!touch_live(t)) return;
  if (!t->state->targets_used) {
    qa_q2_game_use_targets_entity(g, e, &t->player, false);
    /* state pointer may be stale after callbacks reallocating pickups */
    pickup_state *fresh = find_pickup(m, e);
    if (fresh) fresh->targets_used = true;
    t->state = fresh ? fresh : t->state;
  }
  if (!touch_live(t)) return;
  if (m->has_policy && m->policy.after_pickup) m->policy.after_pickup(m->policy.ctx, e, g, t->player, taken);
  if (!taken || !touch_live(t)) return;
  bool stays = g->options.mode == QA_Q2_MODE_COOP && stays_coop(t->it);
  pickup_state *fresh = find_pickup(m, e);
  bool retained = fresh ? fresh->retained : true;
  if ((!stays || is_dropped(e)) && !retained &&
      !(m->has_policy && m->policy.keep_after_pickup && m->policy.keep_after_pickup(m->policy.ctx, e, g, t->player)) && touch_live(t))
    g->remove(g->ctx, e);
}

void qa_q2_item_module_touch(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player) {
  if (!g->host->is_player(g->host->ctx, player)) return;
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, player);
  if ((cs.has_value ? cs.value.health : 0) < 1) return;
  pickup_state *state = pickup_of(m, e);
  const item *it = state->it;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(g->host->actors, player, &owner)) return;
  touch_ctx t = {m, e, g, player, owner, state, it, false};
  if (!g->host->original_pickups) {
    if (touch_eligible(&t)) touch_complete(&t, touch_original(&t));
    return;
  }
  qa_pickup_resource resource = {0};
  const qa_pickup_resource *default_resource = NULL;
  if (strcmp(it->kind, "armor") == 0 || strcmp(it->kind, "shard") == 0) { resource.kind = QA_PICKUP_RESOURCE_PROTECTION; resource.channel = QA_PROTECTION_REGULAR; default_resource = &resource; }
  else if (strcmp(it->kind, "power-armor") == 0) { resource.kind = QA_PICKUP_RESOURCE_PROTECTION; resource.channel = QA_PROTECTION_POWERED; default_resource = &resource; }
  else if (strcmp(it->kind, "ammo") == 0 || strcmp(it->kind, "weapon") == 0 || strcmp(it->kind, "key") == 0) { resource.kind = QA_PICKUP_RESOURCE_INVENTORY; resource.item = ID(it); default_resource = &resource; }
  qa_actor_id value2 = e->actor.id;
  qa_provider_id value3 = e->actor.owner;
  const char * value4 = ID(it);
  qa_pickup_count value5 = {.is_override = e->count != 0, .amount = e->count};
  bool value6 = is_dropped(e);
  qa_source_time value7 = {QA_SOURCE_TIME_SECONDS, g->host->now(g->host->ctx)};
  qa_original_pickup_offer offer = {.recipient = player, .pickup = value2, .source = value3, .item = value4,
    .default_resource = default_resource, .count = value5,
    .dropped = value6, .time = value7};
  qa_original_pickup_continuation cont = {.ctx = &t, .eligible = touch_eligible, .original = touch_original, .complete = touch_complete};
  g->host->original_pickups->touch(g->host->original_pickups->ctx, offer, cont);
}

static void finish_grant(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player, const item *it) {
  if (strcmp(it->kind, "key") == 0) return;
  if (strcmp(it->kind, "weapon") == 0 && !is_dropped(e) &&
      (g->options.mode == QA_Q2_MODE_COOP || (g->options.mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 4) != 0))) {
    pickup_of(m, e)->retained = true;
    /* g_items.cpp SetRespawn with hide_self false: the weapon stays, and DoRespawn still runs after the delay. */
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && g->options.mode == QA_Q2_MODE_DEATHMATCH &&
        qa_session_actor_registry_is_live(g->host->actors, e->actor.id))
      g->schedule(g->ctx, e, m->hooks.has_weapon_respawn_seconds ? m->hooks.weapon_respawn_seconds(m->hooks.ctx) : it->respawn,
                  (qa_q2_think){m, respawn_fn});
    return;
  }
  if (strcmp(it->kind, "health") == 0 && it->timed) {
    e->has_owner = true; e->owner = player.id;
    pickup_of(m, e)->retained = true;
    e->visible = false;
    g->solid(g->ctx, e, "none");
    if (!qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) return;
    g->show(g->ctx, e);
    if (qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) g->schedule(g->ctx, e, 5, (qa_q2_think){m, mega_fn});
    return;
  }
  double respawn = strcmp(it->kind, "weapon") == 0
      ? (m->hooks.has_weapon_respawn_seconds ? m->hooks.weapon_respawn_seconds(m->hooks.ctx) : it->respawn) : it->respawn;
  if (!is_dropped(e) && g->options.mode == QA_Q2_MODE_DEATHMATCH && qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) set_respawn(m, e, g, respawn);
}

static bool pickup_armor(const item *it, qa_regular_armor_state old, qa_regular_armor_state *out) {
  if (old.kind == QA_REGULAR_ARMOR_SOURCE) return false;
  if (strcmp(it->kind, "shard") == 0) {
    if (old.kind == QA_REGULAR_ARMOR_Q2 && old.points > 0) { *out = old; out->points = old.points + 2; return true; }
    *out = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q2, .item = "q2:item_armor_jacket", .points = 2, .normal_protection = 0.3, .energy_protection = 0};
    return true;
  }
  if (old.kind != QA_REGULAR_ARMOR_Q2 || old.points == 0) {
    *out = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q2, .item = ID(it), .points = it->points, .normal_protection = it->normal, .energy_protection = it->energy};
    return true;
  }
  if (it->normal > old.normal_protection) {
    *out = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q2, .item = ID(it),
      .points = qa_js_min(it->maximum, it->points + trunc(old.points * old.normal_protection / it->normal)),
      .normal_protection = it->normal, .energy_protection = it->energy};
    return true;
  }
  double maximum = old.item && strcmp(old.item, "q2:item_armor_jacket") == 0 ? 50 : old.item && strcmp(old.item, "q2:item_armor_combat") == 0 ? 100 : 200;
  double points = qa_js_min(maximum, old.points + trunc(it->points * it->normal / old.normal_protection));
  if (points <= old.points) return false;
  *out = old; out->points = points;
  return true;
}

static bool grant(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player, const item *it) {
  qa_combat_state_result current = qa_gameplay_authority_read(g->host->combat, player.id);
  if (!current.has_value) return false;
  qa_q2_entity *pe = g->entity(g->ctx, &player.id);
  double maximum = pe && pe->max_health != 0 ? pe->max_health : 100;
  const char *kind = it->kind;
  if (strcmp(kind, "custom") == 0) {
    ensure_entry(m, player, g, ID(it), it->capacity);
    if (!it->pickup || !it->pickup(it->custom_ctx, e, g, player)) return false;
    return true;
  }
  if (strcmp(kind, "key") == 0) {
    ensure_entry(m, player, g, ID(it), 32767);
    if (g->options.mode == QA_Q2_MODE_COOP) {
      if (strcmp(it->classname, "key_power_cube") == 0 || (g->options.edition == QA_Q2_EDITION_RERELEASE && strcmp(it->classname, "key_explosive_charges") == 0)) {
        if (!pe) qa_range_error("Q2 cooperative key pickup requires admitted source player fields");
        int32_t cubes = (qa_js_to_int32(e->spawnflags) & 0xff00) >> 8; /* TS (spawnflags & 0xff00) >> 8 */
        if ((qa_js_to_int32(pe->power_cubes) & cubes) != 0) return false;
        pe->power_cubes = (double)(qa_js_to_int32(pe->power_cubes) | cubes);
      } else if (qa_shared_inventory_table_count(g->host->inventory, player.id, ID(it)) != 0) return false;
    }
    qa_shared_inventory_table_give(g->host->inventory, player, ID(it), 1);
    return true;
  }
  if (strcmp(kind, "ammo-pack") == 0) {
    for (size_t i = 0; i < sizeof AMMUNITION / sizeof AMMUNITION[0]; i++) {
      const item *ammo = &AMMUNITION[i];
      double capacity = (strcmp(ammo->classname, "ammo_bullets") == 0 || strcmp(ammo->classname, "ammo_cells") == 0) ? (it->full ? 300 : 250)
          : strcmp(ammo->classname, "ammo_shells") == 0 ? (it->full ? 200 : 150)
          : strcmp(ammo->classname, "ammo_slugs") == 0 ? (it->full ? 100 : 75) : (it->full ? 100 : ammo->capacity);
      ensure_entry(m, player, g, ID(ammo), ammo->capacity);
      qa_inventory_entry entry;
      if (!entry_of(g, player.id, ID(ammo), &entry)) qa_range_error("Q2 ammo pack has no admitted ammo inventory");
      entry.capacity = qa_js_max(entry.capacity, capacity);
      qa_shared_inventory_table_configure(g->host->inventory, player, entry, NULL, NULL);
      if (it->full || strcmp(ammo->classname, "ammo_bullets") == 0 || strcmp(ammo->classname, "ammo_shells") == 0)
        qa_shared_inventory_table_give(g->host->inventory, player, ID(ammo), ammo->quantity);
    }
    if (m->hooks.ammo_pack) m->hooks.ammo_pack(m->hooks.ctx, player, g, it->full);
    return true;
  }
  if (strcmp(kind, "ammo") == 0) {
    double quantity = ammo_quantity(e, g, it);
    if (m->admission && m->admission->maps(m->admission->ctx, "ammo", ID(it))) {
      bool taken = it->weapon_ammo
          ? m->admission->ammo_weapon(m->admission->ctx, player,
              (qa_pickup_ammo_weapon_offer){{ID(it), quantity}, ID(it)},
              (qa_ammo_weapon_selection){QA_PICKUP_SELECTION_ALWAYS, "empty-ammo"})
          : m->admission->ammo(m->admission->ctx, player, (qa_pickup_ammo_grant){ID(it), quantity}, false, false);
      return taken;
    }
    ensure_entry(m, player, g, ID(it), it->capacity);
    double old = qa_shared_inventory_table_count(g->host->inventory, player.id, ID(it));
    if (qa_shared_inventory_table_give(g->host->inventory, player, ID(it), quantity) == 0) return false;
    if (it->weapon_ammo && old == 0) m->hooks.weapon_picked(m->hooks.ctx, player.id, ID(it), true);
    return true;
  }
  if (strcmp(kind, "weapon") == 0) {
    bool has_admission = m->admission && m->admission->maps(m->admission->ctx, "weapons", ID(it));
    const qa_pickup_admission *admission = has_admission ? m->admission : NULL;
    if (admission && strcmp(ID(it), "q2:weapon_blaster") == 0) return false;
    if (!admission) ensure_entry(m, player, g, ID(it), 32767);
    double previous = weapon_owned(m, g, player.id, it);
    if (!weapon_eligible(m, e, g, it, previous)) return false;
    if (!admission) qa_shared_inventory_table_give(g->host->inventory, player, ID(it), 1);
    qa_pickup_ammo_grant grants[1];
    size_t gn = weapon_ammo(m, e, g, it, grants);
    if (!admission) {
      for (size_t i = 0; i < gn; i++) {
        const item *ammo = strncmp(grants[i].item, "q2:", 3) == 0 ? catalog_get(m, grants[i].item + 3) : NULL;
        if (!ammo || strcmp(ammo->kind, "ammo") != 0) qa_range_error("Weapon grant has no source ammo descriptor");
        ensure_entry(m, player, g, grants[i].item, ammo->capacity);
        qa_shared_inventory_table_give(g->host->inventory, player, grants[i].item, grants[i].amount);
      }
      m->hooks.weapon_picked(m->hooks.ctx, player.id, ID(it), previous == 0);
    } else {
      qa_pickup_weapon_offer offer = {ID(it), grants, gn};
      if (!admission->weapon(admission->ctx, player, offer, previous == 0 ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_NEVER)) return false;
    }
    return true;
  }
  if (strcmp(kind, "health") == 0) {
    if (!it->ignore_maximum && current.value.health >= maximum) return false;
    double amount = e->count != 0 ? e->count : it->amount;
    qa_gameplay_authority_set_health(g->host->combat, player, it->ignore_maximum ? current.value.health + amount : qa_js_min(maximum, current.value.health + amount));
    return true;
  }
  if (strcmp(kind, "armor") == 0 || strcmp(kind, "shard") == 0) {
    qa_regular_armor_state next;
    if (!pickup_armor(it, current.value.armor.regular, &next)) return false;
    qa_gameplay_authority_set_regular_armor(g->host->combat, player, next);
    return true;
  }
  if (strcmp(kind, "maximum-health") == 0) {
    double increase = (it->fill && g->options.mode == QA_Q2_MODE_DEATHMATCH) ? 0 : it->increase;
    if (pe) pe->max_health = maximum + increase;
    if (it->fill && current.value.health < maximum + increase) qa_gameplay_authority_set_health(g->host->combat, player, maximum + increase);
    return true;
  }
  if (strcmp(kind, "power") == 0) {
    ensure_entry(m, player, g, ID(it), 32767);
    double quantity = qa_shared_inventory_table_count(g->host->inventory, player.id, ID(it));
    if ((g->options.edition == QA_Q2_EDITION_RERELEASE && g->options.skill == 0 && quantity >= 3) ||
        (g->options.skill == 1 && quantity >= 2) || (g->options.skill >= 2 && quantity >= 1) ||
        (g->options.mode == QA_Q2_MODE_COOP && !instanced_coop(m, g) && it->coop_stay && quantity > 0)) return false;
    qa_shared_inventory_table_give(g->host->inventory, player, ID(it), 1);
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH && ((qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 16) != 0 || (strcmp(it->classname, "item_quad") == 0 && (qa_js_to_int32(e->spawnflags) & 0x20000) != 0))) {
      pickup_state *ps = pickup_of(m, e);
      double duration = ps->has_expires ? qa_js_max(0, ps->expires_at - g->host->now(g->host->ctx)) : 30;
      qa_q2_item_module_use(m, player, ID(it), g, duration);
    }
    return true;
  }
  if (strcmp(kind, "power-armor") == 0) {
    ensure_entry(m, player, g, ID(it), 32767);
    double old = qa_shared_inventory_table_count(g->host->inventory, player.id, ID(it));
    qa_shared_inventory_table_give(g->host->inventory, player, ID(it), 1);
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH && old == 0) qa_q2_item_module_use(m, player, ID(it), g, 30);
    else qa_q2_item_module_sync_power_armor(m, player, g, NULL);
    return true;
  }
  return true;
}

static void mega_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_item_module *m = c;
  qa_owned_actor owner;
  bool has_owner = e->has_owner && qa_session_actor_registry_resolve_owned(g->host->actors, e->owner, &owner);
  qa_combat_state_result health = has_owner ? qa_gameplay_authority_read(g->host->combat, owner.id) : (qa_combat_state_result){0};
  qa_q2_entity *oe = e->has_owner ? g->entity(g->ctx, &e->owner) : NULL;
  double maximum = oe && oe->max_health != 0 ? oe->max_health : 100;
  if (has_owner && health.has_value && health.value.health > maximum) {
    qa_gameplay_authority_set_health(g->host->combat, owner, health.value.health - 1);
    g->schedule(g->ctx, e, 1, (qa_q2_think){m, mega_fn});
    return;
  }
  if (!is_dropped(e) && g->options.mode == QA_Q2_MODE_DEATHMATCH) set_respawn(m, e, g, 20);
  else g->remove(g->ctx, e);
}

static void set_power_armor(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, const char *kind) {
  qa_powered_protection_state powered = {.kind = kind,
    .cells = strcmp(kind, "none") == 0 ? 0 : qa_shared_inventory_table_count(g->host->inventory, player.id, "q2:ammo_cells")};
  qa_gameplay_authority_set_powered_protection(g->host->combat, player, powered);
  m->hooks.power_armor(m->hooks.ctx, player.id, kind);
}
/* TS syncPowerArmor: PowerArmorType derives the active kind from the inventory,
 * shield first, and the armor stops once neither is held. requested NULL is TS null. */
void qa_q2_item_module_sync_power_armor(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, const char *requested) {
  qa_combat_state_result armor = qa_gameplay_authority_read(g->host->combat, player.id);
  if (requested == NULL && (!armor.has_value || strcmp(armor.value.armor.powered.kind, "none") == 0)) return;
  const char *held = qa_shared_inventory_table_count(g->host->inventory, player.id, "q2:item_power_shield") > 0 ? "shield"
                     : qa_shared_inventory_table_count(g->host->inventory, player.id, "q2:item_power_screen") > 0 ? "screen"
                     : NULL;
  const char *kind = held != NULL ? held : requested == NULL ? "none" : requested;
  if (armor.has_value && strcmp(armor.value.armor.powered.kind, kind) == 0) return;
  set_power_armor(m, player, g, kind);
}

bool qa_q2_item_module_use(qa_q2_item_module *m, qa_owned_actor player, qa_item_id item_id, qa_q2_game_services *g, double duration) {
  const item *it = strncmp(item_id, "q2:", 3) == 0 ? catalog_get(m, item_id + 3) : NULL;
  if (!it || qa_shared_inventory_table_count(g->host->inventory, player.id, item_id) == 0) return false;
  if (strcmp(it->kind, "custom") == 0) return it->has_use && it->use ? it->use(it->custom_ctx, player, g) : false;
  if (strcmp(it->kind, "power-armor") == 0) {
    qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, player.id);
    bool active = cs.has_value && strcmp(cs.value.armor.powered.kind, "none") != 0;
    if (!active && qa_shared_inventory_table_count(g->host->inventory, player.id, "q2:ammo_cells") == 0) {
      qa_q2_presentation_event ev = {0};
      ev.kind = "print"; ev.has_actor = true; ev.actor = player.id; ev.level = "high"; ev.text = "No cells for power armor.\n";
      g->host->emit(g->host->ctx, ev);
      return false;
    }
    g->sound(g->ctx, player, active ? "misc/power2.wav" : "misc/power1.wav", 0, 1, 1);
    if (active) set_power_armor(m, player, g, "none");
    else qa_q2_item_module_sync_power_armor(m, player, g, it->armor);
    return true;
  }
  if (strcmp(it->kind, "power") != 0 || !qa_shared_inventory_table_consume(g->host->inventory, player, item_id, 1)) return false;
  if (strcmp(it->classname, "item_quad") == 0) g->sound(g->ctx, player, "items/damage.wav", 3, 1, 1);
  else if (strcmp(it->classname, "item_invulnerability") == 0) g->sound(g->ctx, player, "items/protect.wav", 3, 1, 1);
  qa_q2_player_powerups *st = powers_for(m, player.id);
  double now = g->host->now(g->host->ctx);
  if (strcmp(it->classname, "item_quad") == 0) st->quad_until = qa_js_max(st->quad_until, now) + duration;
  else if (strcmp(it->classname, "item_silencer") == 0) m->hooks.silencer(m->hooks.ctx, player.id, 30);
  else if (strcmp(it->classname, "item_breather") == 0) st->breather_until = qa_js_max(st->breather_until, now) + 30;
  else if (strcmp(it->classname, "item_enviro") == 0) st->enviro_until = qa_js_max(st->enviro_until, now) + 30;
  else if (strcmp(it->classname, "item_invulnerability") == 0) {
    st->invulnerability_until = qa_js_max(st->invulnerability_until, now) + 30;
    qa_combat_trait_changes tc = {0}; tc.has_invulnerable = true; tc.invulnerable = true;
    qa_gameplay_authority_set_traits(g->host->combat, player, tc);
    qa_q2_entity *timer = g->create(g->ctx, "invulnerability_expiry", NULL, 0);
    timer->has_owner = true; timer->owner = player.id;
    g->schedule(g->ctx, timer, st->invulnerability_until - now, (qa_q2_think){m, invuln_expiry_fn});
  }
  return true;
}

static void touch_pickup_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_item_module_touch(c, e, g, contact.other);
}
static void temp_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  if (owner_can_touch(e, contact.other)) qa_q2_item_module_touch(c, e, g, contact.other);
}
static void use_item_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  e->visible = true;
  e->use = (qa_q2_use){0};
  g->solid(g->ctx, e, (qa_js_to_int32(e->spawnflags) & 2) != 0 ? "box" : "trigger");
  g->show(g->ctx, e);
}
static void make_touchable_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_item_module *m = c;
  e->touch = (qa_q2_touch){m, touch_pickup_fn};
  pickup_state *ps = pickup_of(m, e);
  if (ps->has_expires) g->schedule(g->ctx, e, qa_js_max(0, ps->expires_at - g->host->now(g->host->ctx)), QA_Q2_FREE_ENTITY);
  else if (g->options.mode == QA_Q2_MODE_DEATHMATCH) g->schedule(g->ctx, e, 29, QA_Q2_FREE_ENTITY);
}
static void invuln_expiry_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  double left;
  double right;
  qa_q2_item_module *m = c;
  qa_owned_actor player;
  if (e->has_owner && qa_session_actor_registry_resolve_owned(g->host->actors, e->owner, &player) &&
      (left = g->host->now(g->host->ctx), right = qa_q2_item_module_player_powerups(m, player.id).invulnerability_until, left >= right)) {
    qa_combat_trait_changes tc = {0}; tc.has_invulnerable = true; tc.invulnerable = false;
    qa_gameplay_authority_set_traits(g->host->combat, player, tc);
  }
  g->remove(g->ctx, e);
}

qa_q2_item_module *qa_q2_item_module_create(qa_q2_item_hooks hooks) {
  qa_q2_item_module *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->hooks = hooks;
  build_base_catalog(m);
  register_foreign_items(m);
  if (hooks.edition == QA_Q2_EDITION_RERELEASE) apply_rerelease_rows(m);
  m->thinks[0] = (qa_q2_named_think){"q2_items_respawn", {m, respawn_fn}};
  m->thinks[1] = (qa_q2_named_think){"q2_items_drop_to_floor", {m, drop_floor_fn}};
  m->thinks[2] = (qa_q2_named_think){"q2_items_make_touchable", {m, make_touchable_fn}};
  m->thinks[3] = (qa_q2_named_think){"q2_items_mega_health", {m, mega_fn}};
  m->thinks[4] = (qa_q2_named_think){"q2_items_invulnerability_expiry", {m, invuln_expiry_fn}};
  m->thinks[5] = (qa_q2_named_think){"q2_items_regen", {m, regen_fn}};
  m->touches[0] = (qa_q2_named_touch){"Touch_Item", {m, touch_pickup_fn}};
  m->touches[1] = (qa_q2_named_touch){"drop_temp_touch", {m, temp_touch_fn}};
  m->uses[0] = (qa_q2_named_use){"Use_Item", {m, use_item_fn}};
  m->uses[1] = (qa_q2_named_use){"q2_items_remove_powerups", {m, remove_powerups_fn}};
  m->defs = (qa_q2_callback_definitions){.think = m->thinks, .think_count = 6, .touch = m->touches, .touch_count = 2, .use = m->uses, .use_count = 2};
  return m;
}
qa_q2_item_module *qa_q2_create_item_module(qa_q2_item_hooks hooks) { return qa_q2_item_module_create(hooks); }
void qa_q2_item_module_destroy(qa_q2_item_module *m) {
  if (!m) return;
  free(m->catalog); free(m->foreign); free(m->foreign_list.items); free(m->foreign_list.storage); free(m->pickups); free(m->powers);
  free(m->bindings); free(m->list_scratch); free(m->winv_scratch);
  if (m->path_bufs) for (size_t i = 0; i < m->path_cap; i++) free(m->path_bufs[i]);
  free(m->path_scratch); free(m->path_bufs);
  free(m->cp_pickups); free(m->cp_powers); free(m->cp_bindings); free(m->cell_ctxs);
  free(m);
}
qa_q2_spawn_module qa_q2_item_module_as_spawn(qa_q2_item_module *m) {
  return (qa_q2_spawn_module){.ctx = m, .callbacks = &m->defs, .item_name = item_name_cb, .spawn = spawn_cb};
}
qa_q2_callback_definitions qa_q2_item_module_callbacks(qa_q2_item_module *m) { return m->defs; }

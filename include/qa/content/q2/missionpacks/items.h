/* Port of src/content/q2/missionpacks/items.ts. GPL-2.0-or-later.
 * qa_q2mp_item_definition / qa_q2mp_item_module mirror TS
 * Q2ItemDefinition / Q2ItemModule (owner foundation/items.ts; the
 * landed C item module runs on the incompatible qa_q2_* host). Only
 * the surface used by this slice is mirrored. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ITEMS_H
#define QA_CONTENT_Q2_MISSIONPACKS_ITEMS_H
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/doppleganger.h"
#include "qa/content/q2/missionpacks/random_items.h"
#include "qa/content/q2/missionpacks/spheres.h"
#include "qa/contracts/session.h"

/* TS Q2ItemDefinition union on kind (exact strings). Optional TS
 * fields use has_* flags; custom pickup/use are fn+ctx (use may be
 * TS null when has_use is false). */
typedef struct qa_q2mp_item_definition {
  const char *kind, *classname, *model, *icon, *name, *sound;
  bool rotate; double respawn;
  const char *console_give; /* nullable */
  double quantity, capacity; bool weapon_ammo;
  bool has_infinite_ammo_quantity; bool infinite_ammo_is_null; double infinite_ammo_quantity;
  qa_item_id ammo; bool has_coop_stay; bool coop_stay;
  double amount; bool ignore_maximum, timed;
  double points, maximum, normal, energy;
  const char *armor; /* "screen"|"shield" */
  double increase; bool fill, full, droppable;
  void *custom_ctx;
  bool (*pickup)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_owned_actor player);
  bool (*use)(void *ctx, qa_owned_actor player, qa_q2f_game *game);
  bool has_use;
} qa_q2mp_item_definition;

/* TS Q2ItemModule surface used by this slice. */
typedef struct qa_q2mp_item_module {
  void *ctx;
  void (*register_item)(void *ctx, qa_q2mp_item_definition item);
  const char *(*item_name)(void *ctx, const char *classname); /* NULL = TS null */
  bool (*spawn)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
  bool (*spawn_item)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const char *descriptor);
  const qa_q2mp_item_definition *(*item_definition)(void *ctx, qa_q2f_entity *entity); /* NULL = TS null */
  void (*replace_item)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const char *descriptor);
  void (*touch)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_actor_id player);
  /* TS use(player, item, game, duration = 30). */
  bool (*use)(void *ctx, qa_owned_actor player, qa_item_id item, qa_q2f_game *game, double duration);
} qa_q2mp_item_module;

/* TS Q2MissionPackPowerups. */
typedef struct qa_q2mp_powerups { double quad_fire_until, double_until, ir_until; } qa_q2mp_powerups;
/* TS Q2MissionPackItemsCheckpoint. capture() fills malloc'd powers;
 * free with qa_q2mp_items_checkpoint_free. */
typedef struct qa_q2mp_item_power_checkpoint { qa_saved_actor_id actor; qa_q2mp_powerups state; } qa_q2mp_item_power_checkpoint;
typedef struct qa_q2mp_items_checkpoint {
  qa_q2mp_item_power_checkpoint *powers; size_t power_count;
} qa_q2mp_items_checkpoint;
/* TS Q2MissionPackItemHooks. print mirrors game.host.emit "print"
 * (owner foundation/host.ts), absent from qa_q2f_presentation_event. */
typedef struct qa_q2mp_item_hooks {
  void *ctx;
  void (*player_effect)(void *ctx, qa_q2mp_player_effect effect);
  void (*print)(void *ctx, qa_actor_id actor, const char *level, const char *text);
} qa_q2mp_item_hooks;

typedef struct qa_q2mp_actor_powerups { qa_actor_id actor; qa_q2mp_powerups state; } qa_q2mp_actor_powerups;
/* TS power() closure captures (field: 0 quadFire, 1 double, 2 ir). */
typedef struct qa_q2mp_power_env {
  struct qa_q2mp_items *items; qa_item_id item; const char *sound; int field; double duration;
} qa_q2mp_power_env;
/* TS sphere item closure captures. */
typedef struct qa_q2mp_sphere_env {
  struct qa_q2mp_items *items; qa_item_id item; qa_q2mp_sphere_kind kind;
} qa_q2mp_sphere_env;
/* TS Q2MissionPackItems. Registered custom-item closures capture the
 * register() arguments in the *_env fields below (TS closures). */
typedef struct qa_q2mp_items qa_q2mp_items;
struct qa_q2mp_items {
  qa_q2mp_item_hooks hooks;
  qa_q2mp_pack pack;
  qa_q2mp_item_module shared_items;
  qa_q2mp_actor_powerups *powers; size_t power_count, power_cap;
  qa_q2w_weapons *env_weapons;
  qa_q2mp_projectiles *env_projectiles;
  qa_q2mp_spheres *env_spheres;
  qa_q2mp_doppleganger *env_doppleganger;
  qa_q2mp_power_env power_envs[3];
  qa_q2mp_sphere_env sphere_envs[3];
};

/* NULL return is TS null. */
const char *qa_q2mp_mission_weapon_display_name(const char *name);
typedef struct qa_q2mp_weapon_inventory_entry { qa_item_id item; double capacity; } qa_q2mp_weapon_inventory_entry;
typedef struct qa_q2mp_weapon_icon { qa_item_id item; const char *icon; } qa_q2mp_weapon_icon;
/* Returned arrays are static storage. */
const qa_q2mp_weapon_inventory_entry *qa_q2mp_mission_weapon_inventory(qa_q2mp_pack pack, size_t *out_count);
const qa_q2mp_weapon_icon *qa_q2mp_mission_weapon_icons(size_t *out_count);

void qa_q2mp_items_init(qa_q2mp_items *items, qa_q2mp_item_hooks hooks, qa_q2mp_pack pack, qa_q2mp_item_module shared_items);
void qa_q2mp_items_destroy(qa_q2mp_items *items);
bool qa_q2mp_items_spawn(qa_q2mp_items *items, qa_q2f_entity *entity, qa_q2f_game *game);
/* NULL return is TS null. */
qa_q2f_entity *qa_q2mp_items_random_respawn(qa_q2mp_items *items, qa_q2f_entity *entity, qa_q2f_game *game,
                                            qa_q2mp_random_item_settings settings);
/* TS register(..., edition = "classic"). */
void qa_q2mp_items_register(qa_q2mp_items *items, qa_q2mp_item_module *module, qa_q2w_weapons *weapons,
                            qa_q2mp_pack pack, qa_q2mp_projectiles *projectiles, qa_q2mp_spheres *spheres,
                            qa_q2mp_doppleganger *doppleganger, qa_q2f_edition edition);
qa_q2mp_powerups qa_q2mp_items_powerups(qa_q2mp_items *items, qa_actor_id actor);
qa_q2w_input qa_q2mp_items_input(qa_q2mp_items *items, qa_actor_id actor, qa_q2w_input input);
void qa_q2mp_items_reset(qa_q2mp_items *items, qa_actor_id actor);
void qa_q2mp_items_ammo_pack(qa_q2mp_items *items, qa_owned_actor player, qa_q2f_game *game, bool full, qa_q2mp_pack pack);
/* False is OOM only (TS capture returns the checkpoint). */
bool qa_q2mp_items_capture(qa_q2mp_items *items, qa_q2f_game *game, qa_q2mp_items_checkpoint *out);
void qa_q2mp_items_checkpoint_free(qa_q2mp_items_checkpoint *checkpoint);
void qa_q2mp_items_restore(qa_q2mp_items *items, qa_q2f_game *game, const qa_q2mp_items_checkpoint *checkpoint);
#endif

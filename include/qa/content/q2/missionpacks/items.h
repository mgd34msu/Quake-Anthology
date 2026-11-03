/* Port of src/content/q2/missionpacks/items.ts. GPL-2.0-or-later.
 * Registers into the one Q2 item module (TS Q2ItemModule,
 * foundation/items.h qa_q2_item_module / qa_q2_item_definition). */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ITEMS_H
#define QA_CONTENT_Q2_MISSIONPACKS_ITEMS_H
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/doppleganger.h"
#include "qa/content/q2/missionpacks/random_items.h"
#include "qa/content/q2/missionpacks/spheres.h"
#include "qa/contracts/session.h"

/* TS Q2MissionPackPowerups. */
typedef struct qa_q2mp_powerups { double quad_fire_until, double_until, ir_until; } qa_q2mp_powerups;
/* TS Q2MissionPackItemsCheckpoint. capture() fills malloc'd powers;
 * free with qa_q2mp_items_checkpoint_free. */
typedef struct qa_q2mp_item_power_checkpoint { qa_saved_actor_id actor; qa_q2mp_powerups state; } qa_q2mp_item_power_checkpoint;
typedef struct qa_q2mp_items_checkpoint {
  qa_q2mp_item_power_checkpoint *powers; size_t power_count;
} qa_q2mp_items_checkpoint;
/* TS Q2MissionPackItemHooks. */
typedef struct qa_q2mp_item_hooks {
  void *ctx;
  void (*player_effect)(void *ctx, qa_q2mp_player_effect effect);
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
  qa_q2_item_module *shared_items; /* TS sharedItems (borrowed) */
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
/* TS q2MissionWeaponInventory(pack, edition): the classic list with the
 * rerelease ammo capacities (rereleaseCapacity). Writes at most capacity
 * entries into out (items are static strings) and returns the TS count. */
/* TS q2MissionAmmoPackCapacity(classname, full, edition, classic). */
double qa_q2mp_mission_ammo_pack_capacity(const char *classname, bool full, qa_q2_edition edition, double classic);
size_t qa_q2mp_mission_weapon_inventory_edition(qa_q2mp_pack pack, qa_q2_edition edition, qa_q2mp_weapon_inventory_entry *out, size_t capacity);
const qa_q2mp_weapon_icon *qa_q2mp_mission_weapon_icons(size_t *out_count);

void qa_q2mp_items_init(qa_q2mp_items *items, qa_q2mp_item_hooks hooks, qa_q2mp_pack pack, qa_q2_item_module *shared_items);
void qa_q2mp_items_destroy(qa_q2mp_items *items);
bool qa_q2mp_items_spawn(qa_q2mp_items *items, qa_q2_entity *entity, qa_q2_game_services *game);
/* NULL return is TS null. */
qa_q2_entity *qa_q2mp_items_random_respawn(qa_q2mp_items *items, qa_q2_entity *entity, qa_q2_game_services *game,
                                            qa_q2mp_random_item_settings settings);
/* TS register(..., edition = "classic"). */
void qa_q2mp_items_register(qa_q2mp_items *items, qa_q2_item_module *module, qa_q2w_weapons *weapons,
                            qa_q2mp_pack pack, qa_q2mp_projectiles *projectiles, qa_q2mp_spheres *spheres,
                            qa_q2mp_doppleganger *doppleganger, qa_q2_edition edition);
qa_q2mp_powerups qa_q2mp_items_powerups(qa_q2mp_items *items, qa_actor_id actor);
qa_q2w_input qa_q2mp_items_input(qa_q2mp_items *items, qa_actor_id actor, qa_q2w_input input);
void qa_q2mp_items_reset(qa_q2mp_items *items, qa_actor_id actor);
void qa_q2mp_items_ammo_pack(qa_q2mp_items *items, qa_owned_actor player, qa_q2_game_services *game, bool full, qa_q2mp_pack pack);
/* False is OOM only (TS capture returns the checkpoint). */
bool qa_q2mp_items_capture(qa_q2mp_items *items, qa_q2_game_services *game, qa_q2mp_items_checkpoint *out);
void qa_q2mp_items_checkpoint_free(qa_q2mp_items_checkpoint *checkpoint);
void qa_q2mp_items_restore(qa_q2mp_items *items, qa_q2_game_services *game, const qa_q2mp_items_checkpoint *checkpoint);
#endif

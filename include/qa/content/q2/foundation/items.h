/* Port of src/content/q2/foundation/items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_ITEMS_H
#define QA_CONTENT_Q2_FOUNDATION_ITEMS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/contracts/pickups.h"
/* TS Q2ItemDefinition union on kind (exact strings). custom pickup/use are fn+ctx. */
typedef struct qa_q2_item_definition {
  const char *kind, *classname, *model, *icon, *name, *sound;
  bool rotate; double respawn;
  const char *console_give; /* optional exact string */
  double quantity, capacity; bool weapon_ammo; bool has_infinite_ammo_quantity; double infinite_ammo_quantity; bool infinite_ammo_is_null;
  qa_item_id ammo; bool has_coop_stay; bool coop_stay;
  double amount; bool ignore_maximum, timed;
  double points, maximum, normal, energy;
  const char *armor; /* "screen"|"shield" */
  double increase; bool fill, full, droppable;
  void *custom_ctx;
  bool (*pickup)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_owned_actor player);
  bool (*use)(void *ctx, qa_owned_actor player, qa_q2_game_services *g);
  bool has_use;
} qa_q2_item_definition;
typedef struct qa_q2_player_powerups { double quad_until, invulnerability_until, breather_until, enviro_until, haste_until, regen_until, flight_until, invis_until; } qa_q2_player_powerups;
/* TS Q2ForeignItemServices: the module services a composition-supplied item
 * definition needs for pickups, powerups and regeneration. ctx is the item
 * module; the struct lives in the module and outlives every definition. */
typedef struct qa_q2_foreign_item_services {
  void *ctx;
  const qa_q2_item_definition *(*item)(void *ctx, const char *classname); /* NULL = TS undefined */
  qa_item_id (*item_id)(void *ctx, const qa_q2_item_definition *item);
  const qa_q2_item_definition *(*ammunition)(void *ctx, size_t *out_count);
  void (*ensure)(void *ctx, qa_owned_actor actor, qa_q2_game_services *game, qa_item_id item, double capacity);
  double (*weapon_owned)(void *ctx, qa_q2_game_services *game, qa_actor_id player, const qa_q2_item_definition *item);
  /* Writes at most one grant to out (TS [] or [grant]) and returns the count. */
  size_t (*weapon_ammo)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_q2_item_definition *item, qa_pickup_ammo_grant *out);
  void (*weapon_picked)(void *ctx, qa_actor_id player, qa_item_id item, bool first);
  qa_q2_player_powerups *(*powers)(void *ctx, qa_actor_id player);
  qa_q2_think regen_think;
} qa_q2_foreign_item_services;
/* TS foreignItems result. The item module takes ownership and releases
 * items and storage with free() when it is destroyed. */
typedef struct qa_q2_foreign_item_list { qa_q2_item_definition *items; size_t count; void *storage; } qa_q2_foreign_item_list;
typedef struct qa_q2_item_hooks {
  void *ctx;
  void (*weapon_picked)(void *ctx, qa_actor_id player, qa_item_id item, bool first);
  void (*silencer)(void *ctx, qa_actor_id player, double charges);
  void (*power_armor)(void *ctx, qa_actor_id player, const char *kind);
  void (*ammo_pack)(void *ctx, qa_owned_actor player, qa_q2_game_services *g, bool full);
  qa_q2_entity *(*random_respawn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  double (*weapon_respawn_seconds)(void *ctx);
  bool has_weapon_respawn_seconds;
  /* TS pickedUp?(player, item, usable): optional (NULL = TS undefined). */
  void (*picked_up)(void *ctx, qa_actor_id player, qa_item_id item, bool usable);
  /* TS edition?: zero (QA_Q2_EDITION_CLASSIC) is TS undefined/"classic";
   * "rerelease" applies the rerelease item rows at construction. */
  qa_q2_edition edition;
  /* TS foreignItems?(services): items authored by another game, supplied by
   * composition so the Q2 foundation stays independent of foreign content.
   * NULL = TS undefined. */
  void *foreign_items_ctx;
  qa_q2_foreign_item_list (*foreign_items)(void *ctx, const qa_q2_foreign_item_services *services);
} qa_q2_item_hooks;
typedef struct qa_q2_pickup_policy {
  void *ctx;
  bool (*instanced_coop)(void *ctx, qa_q2_game_services *g);
  bool (*can_pickup)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
  bool (*before_pickup)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
  void (*before_targets)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken);
  void (*after_pickup)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken);
  bool (*keep_after_pickup)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
} qa_q2_pickup_policy;
typedef struct qa_q2_inventory_item {
  qa_item_id id; const char *classname, *name, *kind;
  double quantity; bool usable, droppable, stay_coop; double capacity; bool weapon;
  const char *console_give;
} qa_q2_inventory_item;
typedef struct qa_q2_drop_options { bool immediate_touch, player_death; bool has_yaw_offset; double yaw_offset; bool has_expires_at; double expires_at; } qa_q2_drop_options;
typedef struct qa_q2_item_pickup_checkpoint { qa_saved_actor_id actor; const char *classname; bool targets_used, retained; bool has_expires_at; double expires_at; } qa_q2_item_pickup_checkpoint;
typedef struct qa_q2_item_power_checkpoint { qa_saved_actor_id actor; qa_q2_player_powerups state; } qa_q2_item_power_checkpoint;
typedef struct qa_q2_items_checkpoint {
  double power_cube_count;
  const qa_q2_item_pickup_checkpoint *pickups; size_t pickup_count;
  const qa_q2_item_power_checkpoint *powers; size_t power_count;
  const qa_saved_actor_id *power_armor_bindings; size_t binding_count;
} qa_q2_items_checkpoint;
typedef struct qa_q2_weapon_inventory_entry { qa_item_id item; double capacity; } qa_q2_weapon_inventory_entry;
const char *qa_q2_item_pickup_name(const char *classname);
const qa_q2_weapon_inventory_entry *qa_q2_base_weapon_inventory(size_t *n);
typedef struct qa_q2_base_item_icon { qa_item_id item; const char *icon; } qa_q2_base_item_icon;
const qa_q2_base_item_icon *qa_q2_base_item_icons(size_t *n);
const char *qa_q2_base_weapon_display_name(qa_item_id item);
typedef struct qa_q2_item_module qa_q2_item_module;
qa_q2_item_module *qa_q2_item_module_create(qa_q2_item_hooks hooks);
qa_q2_item_module *qa_q2_create_item_module(qa_q2_item_hooks hooks);
void qa_q2_item_module_destroy(qa_q2_item_module *m);
qa_q2_spawn_module qa_q2_item_module_as_spawn(qa_q2_item_module *m);
qa_q2_callback_definitions qa_q2_item_module_callbacks(qa_q2_item_module *m);
void qa_q2_item_module_register(qa_q2_item_module *m, qa_q2_item_definition item);
const char *qa_q2_item_module_item_name(qa_q2_item_module *m, const char *classname);
const char *const *qa_q2_item_module_resource_paths(qa_q2_item_module *m, const char *const *classnames, size_t n, size_t *out_n);
void qa_q2_item_module_set_pickup_policy(qa_q2_item_module *m, qa_q2_pickup_policy p);
void qa_q2_item_module_set_pickup_admission(qa_q2_item_module *m, const qa_pickup_admission *a);
bool qa_q2_item_module_maps_supply(qa_q2_item_module *m, qa_item_id item);
qa_q2_items_checkpoint qa_q2_item_module_capture(qa_q2_item_module *m, qa_q2_game_services *g);
void qa_q2_item_module_restore(qa_q2_item_module *m, qa_q2_game_services *g, qa_q2_items_checkpoint cp);
const qa_q2_inventory_item *qa_q2_item_module_list(qa_q2_item_module *m, size_t *n);
bool qa_q2_item_module_lookup(qa_q2_item_module *m, const char *value, qa_q2_inventory_item *out);
void qa_q2_item_module_give_start_items(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, const char *expression);
void qa_q2_item_module_give_ammo_count(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, qa_item_id item, const double *count);
void qa_q2_item_module_clear_powerups(qa_q2_item_module *m, qa_actor_id player);
qa_q2_entity *qa_q2_item_module_drop(qa_q2_item_module *m, qa_q2_entity *self, qa_q2_game_services *g, qa_item_id item, qa_q2_drop_options o);
qa_q2_entity *qa_q2_item_module_drop_monster(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, const char *classname);
qa_q2_player_powerups qa_q2_item_module_player_powerups(qa_q2_item_module *m, qa_actor_id player);
const qa_q2_weapon_inventory_entry *qa_q2_item_module_weapon_inventory(qa_q2_item_module *m, size_t *n);
void qa_q2_item_module_configure_player(qa_q2_item_module *m, qa_owned_actor actor, qa_q2_game_services *g, bool give_blaster);
bool qa_q2_item_module_spawn(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_item_module_spawn_item(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const char *descriptor);
const qa_q2_item_definition *qa_q2_item_module_item_definition(qa_q2_item_module *m, qa_q2_entity *e);
bool qa_q2_item_module_preview_supply(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id pickup, qa_actor_id recipient, qa_pickup_supply_preview *out);
bool qa_q2_item_module_observe_supply(qa_q2_item_module *m, qa_q2_game_services *g, qa_actor_id pickup, qa_actor_id recipient, qa_pickup_supply_observation *out);
void qa_q2_item_module_replace_item(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, const char *descriptor);
void qa_q2_item_module_touch(qa_q2_item_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
bool qa_q2_item_module_use(qa_q2_item_module *m, qa_owned_actor player, qa_item_id item, qa_q2_game_services *g, double duration);
/* TS syncPowerArmor(player, game, requested = null): requested NULL is TS null, else "screen"|"shield". */
void qa_q2_item_module_sync_power_armor(qa_q2_item_module *m, qa_owned_actor player, qa_q2_game_services *g, const char *requested);
#endif

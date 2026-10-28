/* Port of src/app/bootstrap/simulation/arsenal/q1.ts.
 * Q1EntityServices is the host mirror below: entity_services.h declares
 * the runtime but its definitions land with the foundation runtime
 * slice, so the surface this file calls is fn+ctx (player records are
 * landed qa_q1_player_state). Inventory mirrors game.host.inventory.
 * Travel extension bytes are malloc'd copies. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q1_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q1_H
#include <stdint.h>
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q1/foundation/types.h"

typedef struct qa_app_q1_travel_extension {
  const char *id;
  uint8_t *bytes; /* malloc'd copy in capture; borrowed in admit input */
  size_t byte_count;
} qa_app_q1_travel_extension;
typedef struct qa_app_q1_selected_arsenal_travel {
  qa_q1_weapon weapon;
  const qa_inventory_entry *inventory;
  size_t inventory_count;
  const qa_app_q1_travel_extension *extensions;
  size_t extension_count;
} qa_app_q1_selected_arsenal_travel;
void qa_app_q1_selected_arsenal_travel_destroy(qa_app_q1_selected_arsenal_travel *travel);

typedef struct qa_app_q1_game_host {
  void *ctx;
  qa_provider_id provider;
  qa_app_inventory_access inventory;
  double time;
  qa_q1_player_state *(*player)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  const qa_q1_player_state *const *(*players)(void *ctx, size_t *out_count);
  void (*delete_player)(void *ctx, qa_owned_actor actor);
  void (*initialize_weapon_inventory)(void *ctx, qa_owned_actor actor);
  /* has_weapon false = TS omitted weapon; max_health already resolved. */
  qa_q1_player_state *(*attach_player)(void *ctx, qa_owned_actor actor, bool has_weapon, qa_q1_weapon weapon, double max_health);
  qa_item_id (*weapon_item)(void *ctx, qa_q1_weapon weapon);
  qa_item_id (*weapon_ammo)(void *ctx, qa_q1_weapon weapon);
  bool (*weapon_available)(void *ctx, qa_q1_player_state *player, qa_q1_weapon weapon);
  bool (*select_weapon)(void *ctx, qa_owned_actor actor, qa_q1_weapon weapon);
  bool (*weapon_input)(void *ctx, qa_owned_actor actor, bool pressed, qa_vec3 view_angles, double seconds, double water_level);
  void (*weapon_frame)(void *ctx, qa_owned_actor actor, double seconds);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  const char *(*weapon_model)(void *ctx, qa_q1_weapon weapon, qa_q1_player_state *player);
  bool (*inventory_capacity)(void *ctx, qa_actor_id actor, qa_item_id item, double *out);
  /* registered weapon ids in registration order */
  const qa_q1_weapon *(*registered_weapons)(void *ctx, size_t *out_count);
  /* TS playerExtensions values in insertion order. capture: false = undefined. */
  size_t (*extension_count)(void *ctx);
  const char *(*extension_id)(void *ctx, size_t index);
  bool (*extension_capture)(void *ctx, size_t index, qa_q1_player_state *player, const uint8_t **out, size_t *out_count);
  bool (*extension_restore)(void *ctx, const char *id, qa_q1_player_state *player, const uint8_t *bytes, size_t count); /* false = no restoreTravel */
  /* TS chooseBest(actor, countFn). */
  qa_q1_weapon (*choose_best)(void *ctx, qa_owned_actor actor, double (*count)(void *count_ctx, qa_item_id item), void *count_ctx);
  /* TS q1AmmoPickupSelection / q1WeaponPickupSelection / q1WeaponImpulse. */
  void (*ammo_pickup_selection)(void *ctx, qa_q1_player_state *player, qa_q1_weapon before, bool auto_switch);
  void (*weapon_pickup_selection)(void *ctx, qa_q1_player_state *player, qa_q1_weapon weapon, const char *selection);
  bool (*weapon_impulse)(void *ctx, qa_q1_player_state *player, double value);
  qa_app_primary_weapon_handoff (*primary_handoff)(void *ctx, qa_owned_actor actor);
} qa_app_q1_game_host;

typedef struct qa_app_q1_native_player {
  bool has_value;
  qa_q1_weapon weapon;
  double max_health;
  bool has_auto_switch;
  const char *auto_switch;
} qa_app_q1_native_player;

typedef struct qa_app_q1_selected_arsenal_options {
  qa_app_q1_game_host game;
  void *ctx;
  qa_app_q1_native_player (*native_player)(void *ctx, qa_actor_id actor); /* NULL fn = TS undefined */
  const qa_item_id *replaced_items;
  size_t replaced_item_count;
  qa_actor_animation_state (*fired)(void *ctx, qa_actor_id actor, qa_q1_weapon weapon, qa_actor_animation_state animation);
  bool (*impulse)(void *ctx, qa_q1_player_state *player, double value);
  bool has_impulse;
  void (*prepare_pickup)(void *ctx, qa_q1_player_state *player);
  void (*observe)(void *ctx, qa_actor_id actor, qa_vec3 *out_view_angles, double *out_water_level);
} qa_app_q1_selected_arsenal_options;

typedef struct qa_app_q1_selected_arsenal qa_app_q1_selected_arsenal;
qa_app_q1_selected_arsenal *qa_app_q1_selected_arsenal_create(qa_app_q1_selected_arsenal_options options);
void qa_app_q1_selected_arsenal_destroy(qa_app_q1_selected_arsenal *arsenal);
qa_app_selected_arsenal qa_app_q1_selected_arsenal_interface(qa_app_q1_selected_arsenal *arsenal);
const qa_q1_weapon *qa_app_q1_selected_arsenal_weapons(const qa_app_q1_selected_arsenal *arsenal, size_t *out_count);
bool qa_app_q1_selected_arsenal_has(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
/* read/admit ammo arrays are malloc'd in the result (free()). */
qa_arsenal_state qa_app_q1_selected_arsenal_admit(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor, double max_health);
qa_app_q1_selected_arsenal_travel qa_app_q1_selected_arsenal_capture_travel(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
qa_arsenal_state qa_app_q1_selected_arsenal_admit_travel(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    double max_health, qa_app_q1_selected_arsenal_travel travel);
qa_arsenal_state qa_app_q1_selected_arsenal_read(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
bool qa_app_q1_selected_arsenal_select(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item);
qa_item_id qa_app_q1_selected_arsenal_pending_weapon(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
bool qa_app_q1_selected_arsenal_impulse(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, double value);
qa_app_primary_weapon_handoff qa_app_q1_selected_arsenal_handoff(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q1_selected_arsenal_pickup_ammo(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch);
void qa_app_q1_selected_arsenal_pickup_weapons(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection);
qa_weapon_step_result qa_app_q1_selected_arsenal_step(qa_app_q1_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent);
void qa_app_q1_selected_arsenal_frame(qa_app_q1_selected_arsenal *arsenal, double seconds);
void qa_app_q1_selected_arsenal_remove(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_arsenal_ui qa_app_q1_selected_arsenal_ui(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity);
qa_app_arsenal_view qa_app_q1_selected_arsenal_view(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor);
size_t qa_app_q1_selected_arsenal_catalog(qa_app_q1_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity);
#endif

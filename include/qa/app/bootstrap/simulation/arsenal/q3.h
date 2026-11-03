/* Port of src/app/bootstrap/simulation/arsenal/q3.ts. Uses landed
 * qa_q3_step_arsenal/spawn helpers; resolveQ3ArsenalControls mirrors
 * arsenal-intent.ts (sibling) as a host hook (NULL = command-derived).
 * read checkpoint ammo is malloc'd. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_H
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/persistence/value.h"

typedef struct qa_app_q3_equipment_state {
  double max_health, persistent_powerup_tag, holdable_item, holdable_tag;
} qa_app_q3_equipment_state;
typedef struct qa_app_q3_equipment {
  void *ctx;
  bool has_owns_holdables;
  bool owns_holdables;
  qa_app_q3_equipment_state (*read)(void *ctx, qa_owned_actor actor);
  void (*consume)(void *ctx, qa_owned_actor actor, double item);
  void (*advance)(void *ctx, qa_owned_actor actor, double milliseconds);   /* NULL absent */
  void (*end_command)(void *ctx, qa_owned_actor actor, double milliseconds);
  void (*restore)(void *ctx, qa_owned_actor actor, qa_app_q3_equipment_state state);
} qa_app_q3_equipment;

typedef struct qa_app_q3_selected_arsenal_options {
  qa_provider_id provider;
  const char *product; /* "baseq3"|"missionpack" */
  qa_app_inventory_access inventory;
  bool has_supply;
  qa_item_id supply_profile;
  qa_arsenal_state supply_loadout;
  const qa_item_id *supply_replaced_items;
  size_t supply_replaced_item_count;
  void *ctx;
  double (*firing_delay)(void *ctx, qa_owned_actor actor, double milliseconds); /* NULL absent */
  qa_arsenal_state (*loadout)(void *ctx, qa_owned_actor actor, qa_arsenal_state defaults);
  bool has_equipment;
  qa_app_q3_equipment equipment;
  void (*fire)(void *ctx, qa_owned_actor actor, double weapon, qa_weapon_step_input input);
  void (*use_holdable)(void *ctx, qa_owned_actor actor, double event, qa_weapon_step_input input);
  qa_q3_arsenal_controls (*resolve_controls)(void *ctx, qa_arsenal_state arsenal,
      const qa_arsenal_intent *intent, qa_user_command command, const char *product);
} qa_app_q3_selected_arsenal_options;

typedef struct qa_app_q3_selected_arsenal_checkpoint {
  qa_item_id supply_profile; /* nullable */
  qa_arsenal_state arsenal;
  qa_q3_arsenal_runtime_state runtime;
  double torso_animation;
  bool has_last_fire_milliseconds;
  double last_fire_milliseconds;
  qa_item_id pending_use; /* nullable */
  /* C-only key order of arsenal: false is the q3SpawnLoadout literal { provider, activeWeapon, state, ammo }; true is
   * readQ3SelectedArsenalCheckpoint's { provider, activeWeapon, ammo, state }, which a restored player keeps (read()
   * and the step results spread it). */
  bool arsenal_read_order;
} qa_app_q3_selected_arsenal_checkpoint;

typedef struct qa_app_q3_selected_arsenal qa_app_q3_selected_arsenal;
qa_app_q3_selected_arsenal *qa_app_q3_selected_arsenal_create(qa_app_q3_selected_arsenal_options options);
void qa_app_q3_selected_arsenal_destroy(qa_app_q3_selected_arsenal *arsenal);
qa_app_selected_arsenal qa_app_q3_selected_arsenal_interface(qa_app_q3_selected_arsenal *arsenal);
size_t qa_app_q3_selected_arsenal_catalog(qa_app_q3_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity);
bool qa_app_q3_selected_arsenal_has(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
qa_arsenal_state qa_app_q3_selected_arsenal_admit(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    double max_health, bool team_deathmatch);
qa_arsenal_state qa_app_q3_selected_arsenal_read(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
/* TS setSpawnWeapon(actor, weapon): raises when weapon is not a Q3 weapon. */
void qa_app_q3_selected_arsenal_set_spawn_weapon(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, double weapon);
bool qa_app_q3_selected_arsenal_select(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item);
bool qa_app_q3_selected_arsenal_owns_holdable_input(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q3_selected_arsenal_observe_holdable_input(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    qa_user_command command, const qa_arsenal_intent *intent);
bool qa_app_q3_selected_arsenal_use_item(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item);
qa_item_id qa_app_q3_selected_arsenal_pending_weapon(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_app_q3_selected_arsenal_handoff(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q3_selected_arsenal_pickup_ammo(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch);
void qa_app_q3_selected_arsenal_pickup_weapons(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection);
qa_weapon_step_result qa_app_q3_selected_arsenal_step(qa_app_q3_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent);
void qa_app_q3_selected_arsenal_remove(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_q3_selected_arsenal_checkpoint qa_app_q3_selected_arsenal_capture(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q3_selected_arsenal_restore(qa_app_q3_selected_arsenal *arsenal, qa_owned_actor actor,
    qa_app_q3_selected_arsenal_checkpoint checkpoint);
qa_app_arsenal_ui qa_app_q3_selected_arsenal_ui(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity);
void qa_app_q3_selected_arsenal_view_state(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor,
    double *out_torso_animation, bool *out_has_last_fire, double *out_last_fire);
qa_app_arsenal_view qa_app_q3_selected_arsenal_view(qa_app_q3_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_q3_selected_arsenal_checkpoint qa_app_read_q3_selected_arsenal_checkpoint(const qa_save_reader *reader);
#endif

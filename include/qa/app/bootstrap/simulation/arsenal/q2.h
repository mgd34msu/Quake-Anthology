/* Port of src/app/bootstrap/simulation/arsenal/q2.ts. Uses landed
 * qa_q2w_weapons / qa_q2f_game; inventory mirrors game.host.inventory
 * (entries/count/configure/give). Actor release cleanup is explicit
 * via qa_app_q2_selected_arsenal_release_actor (TS onRelease hook).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q2_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q2_H
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/foundation/weapons/turn.h"

typedef struct qa_app_q2_selected_weapon_turn_state {
  qa_q2w_turn_state turn;
  bool has_firing;
  qa_q2w_name firing_weapon;
  double firing_credit;
} qa_app_q2_selected_weapon_turn_state;

typedef struct qa_app_q2_inventory_definition { qa_item_id item; double capacity; } qa_app_q2_inventory_definition;
typedef struct qa_app_q2_loadout_entry { qa_item_id item; double count; } qa_app_q2_loadout_entry;

typedef struct qa_app_q2_selected_arsenal_options {
  qa_q2f_game *game;          /* borrowed */
  qa_provider_id provider;    /* TS game.options.provider */
  const char *edition;        /* "classic"|"rerelease" */
  qa_q2w_weapons *weapons;    /* borrowed */
  qa_app_inventory_access inventory;
  const qa_app_q2_inventory_definition *inventory_definitions;
  size_t inventory_definition_count;
  const qa_item_id *pickup_order; /* NULL = Q2_BASE_WEAPONS items */
  size_t pickup_order_count;
  const qa_item_id *replaced_items;
  size_t replaced_item_count;
  bool has_loadout;
  qa_item_id loadout_weapon;
  const qa_app_q2_loadout_entry *loadout_inventory;
  size_t loadout_inventory_count;
  void *ctx;
  void (*observe)(void *ctx, qa_actor_id actor, qa_q2w_owner *out_owner, qa_q2w_input *out_input);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  double (*frame_seconds)(void *ctx);
  bool (*combat_health)(void *ctx, qa_actor_id actor, double *out_health); /* false = TS undefined */
} qa_app_q2_selected_arsenal_options;

/* TS projectQ2Arsenal; inventory entries borrowed from access. */
qa_arsenal_state qa_app_project_q2_arsenal(qa_actor_id actor, qa_provider_id provider, qa_q2w_weapons *weapons,
    qa_app_inventory_access inventory);

typedef struct qa_app_q2_selected_arsenal qa_app_q2_selected_arsenal;
qa_app_q2_selected_arsenal *qa_app_q2_selected_arsenal_create(qa_app_q2_selected_arsenal_options options);
void qa_app_q2_selected_arsenal_destroy(qa_app_q2_selected_arsenal *arsenal);
qa_app_selected_arsenal qa_app_q2_selected_arsenal_interface(qa_app_q2_selected_arsenal *arsenal);
void qa_app_q2_selected_arsenal_release_actor(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
size_t qa_app_q2_selected_arsenal_catalog(qa_app_q2_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity);
bool qa_app_q2_selected_arsenal_has(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
qa_arsenal_state qa_app_q2_selected_arsenal_admit(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor, double max_health);
qa_arsenal_state qa_app_q2_selected_arsenal_read(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
bool qa_app_q2_selected_arsenal_select(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item);
qa_item_id qa_app_q2_selected_arsenal_pending_weapon(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_app_q2_selected_arsenal_handoff(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q2_selected_arsenal_pickup_ammo(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch);
void qa_app_q2_selected_arsenal_pickup_weapons(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *items, size_t item_count, qa_pickup_selection selection);
qa_weapon_step_result qa_app_q2_selected_arsenal_step(qa_app_q2_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent);
void qa_app_q2_selected_arsenal_frame(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_q2_selected_weapon_turn_state qa_app_q2_selected_arsenal_capture_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
void qa_app_q2_selected_arsenal_restore_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor,
    qa_app_q2_selected_weapon_turn_state turn);
void qa_app_q2_selected_arsenal_remove(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
qa_app_arsenal_ui qa_app_q2_selected_arsenal_ui(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity);
qa_app_arsenal_view qa_app_q2_selected_arsenal_view(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor);
#endif

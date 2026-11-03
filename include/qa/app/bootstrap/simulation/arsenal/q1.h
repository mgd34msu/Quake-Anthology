/* Port of src/app/bootstrap/simulation/arsenal/q1.ts. The arsenal runs over
 * the real Q1EntityServices (qa_q1_services, borrowed; it must outlive the
 * arsenal) and its host inventory. Travel extension bytes are malloc'd
 * copies. TS Errors are qa_raise("Error"). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q1_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q1_H
#include <stdint.h>
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q1/foundation/entity_services.h"

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


typedef struct qa_app_q1_native_player {
  bool has_value;
  qa_q1_weapon weapon;
  double max_health;
  bool has_auto_switch;
  const char *auto_switch;
} qa_app_q1_native_player;

typedef struct qa_app_q1_selected_arsenal_options {
  qa_q1_services *game;
  void *ctx; /* passed to every option callback */
  qa_app_q1_native_player (*native_player)(void *ctx, qa_actor_id actor); /* NULL fn = TS undefined */
  const qa_item_id *replaced_items;
  size_t replaced_item_count;
  qa_actor_animation_state (*fired)(void *ctx, qa_actor_id actor, qa_q1_weapon weapon, qa_actor_animation_state animation);
  bool (*impulse)(void *ctx, qa_q1_player_state *player, double value); /* NULL = TS undefined */
  void (*prepare_pickup)(void *ctx, qa_q1_player_state *player);      /* NULL = TS undefined */
  void (*observe)(void *ctx, qa_actor_id actor, qa_vec3 *out_view_angles, double *out_water_level);
} qa_app_q1_selected_arsenal_options;

typedef struct qa_app_q1_selected_arsenal qa_app_q1_selected_arsenal;
qa_app_q1_selected_arsenal *qa_app_q1_selected_arsenal_create(qa_app_q1_selected_arsenal_options options);
void qa_app_q1_selected_arsenal_destroy(qa_app_q1_selected_arsenal *arsenal);
qa_app_selected_arsenal qa_app_q1_selected_arsenal_interface(qa_app_q1_selected_arsenal *arsenal);
/* TS get game(). */
qa_q1_services *qa_app_q1_selected_arsenal_game(const qa_app_q1_selected_arsenal *arsenal);
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

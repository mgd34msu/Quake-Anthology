/* Port of src/app/bootstrap/simulation/q3/guest-player.ts.
 * PlayerUi mirrors simulation/types.ts (sibling slice). Q3PlayerState
 * is contracts qa_q3_player_state with qa_double_view slot views.
 * Result arrays are caller buffers. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_PLAYER_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_PLAYER_H
#include "qa/app/bootstrap/simulation/arsenal/selected.h"
#include "qa/content/q3/guest_items.h"

typedef struct qa_app_guest_player_ui {
  qa_active_powerup_timer *powerups;
  size_t powerup_capacity, powerup_count;
  double health;
  qa_armor_state armor;
  qa_item_id active_weapon;
  bool has_ammo;
  qa_item_id ammo_item;
  double ammo_count;
  qa_inventory_entry *inventory;
  size_t inventory_capacity, inventory_count;
  bool has_weapon_status;
  qa_weapon_hud_status weapon_status;
  qa_arsenal_ammo_warning arsenal_warning;
  qa_app_player_ui_item *items;
  size_t item_capacity, item_count;
} qa_app_guest_player_ui;

/* catalog NULL uses base/standard by product; inventory NULL derives
 * from state; active NULL + has_active false = TS undefined. */
qa_app_guest_player_ui qa_app_q3_guest_player_ui(qa_q3_player_state state, qa_provider_reference source,
    double now_milliseconds, const qa_q3_guest_weapon *catalog, size_t catalog_count, const char *product,
    const qa_inventory_entry *inventory, size_t inventory_count, bool has_inventory,
    qa_item_id active, bool has_active, qa_app_guest_player_ui out);
#endif

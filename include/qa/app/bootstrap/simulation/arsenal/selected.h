/* Port of src/app/bootstrap/simulation/arsenal/selected.ts (interface).
 * PlayerUi pick + PrimaryWeaponHandoff mirror simulation/types.ts and
 * weapon-slot.ts (sibling slices). Inventory access mirrors
 * SharedInventoryTable's entries/count/configure/give surface used by
 * the selected arsenals. UI item arrays are caller buffers filled by
 * ui(); counts returned in the result. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_SELECTED_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_SELECTED_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/pickups.h"
#include "qa/contracts/ui.h"
#include "qa/world/gameplay/pickups.h"

typedef struct qa_app_inventory_access {
  void *ctx;
  const qa_inventory_entry *(*entries)(void *ctx, qa_actor_id actor, size_t *out_count);
  double (*count)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*configure)(void *ctx, qa_owned_actor actor, qa_inventory_entry entry);
  double (*give)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
} qa_app_inventory_access;

typedef struct qa_app_player_ui_item {
  qa_item_id id;
  const char *label;
  const char *kind; /* "weapon" */
  double source_ordinal;
  bool owned, has_ammo;
  bool has_count;
  double count, warning_count;
} qa_app_player_ui_item;

typedef struct qa_app_arsenal_ui {
  bool has_weapon_status;
  qa_weapon_hud_status weapon_status;
  qa_arsenal_ammo_warning arsenal_warning;
  qa_item_id active_weapon; /* nullable */
  bool has_ammo;
  qa_item_id ammo_item;
  double ammo_count;
  qa_app_player_ui_item *items; /* caller buffer */
  size_t item_capacity, item_count;
} qa_app_arsenal_ui;

typedef struct qa_app_arsenal_view {
  bool has_value; /* false = TS null */
  const char *path;
  double frame;
} qa_app_arsenal_view;

/* TS PrimaryWeaponHandoff (immediate kind used by q2/q3 selected). */
typedef struct qa_app_primary_weapon_handoff {
  void *ctx;
  const char *kind; /* "immediate" */
  qa_provider_id provider;
  bool (*accepts)(void *ctx, qa_item_id item);
  bool (*select)(void *ctx, qa_item_id item);
  void (*holster)(void *ctx);
  bool (*is_holstered)(void *ctx);
  bool (*resume)(void *ctx, qa_item_id item); /* item NULL = TS null */
} qa_app_primary_weapon_handoff;

/* TS SelectedArsenal as fn table + ctx. catalog/out arrays caller-owned. */
typedef struct qa_app_selected_arsenal {
  void *ctx;
  const char *family; /* "q1"|"q2"|"q3" */
  qa_provider_id provider;
  size_t (*catalog)(void *ctx, qa_selected_pickup_weapon *out, size_t capacity);
  bool (*has)(void *ctx, qa_actor_id actor);
  qa_arsenal_state (*admit)(void *ctx, qa_owned_actor actor, double max_health, bool team_deathmatch);
  qa_arsenal_state (*read)(void *ctx, qa_actor_id actor);
  bool (*select)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*pickup_ammo)(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch);
  void (*pickup_weapons)(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection);
  qa_item_id (*pending_weapon)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  qa_app_primary_weapon_handoff (*handoff)(void *ctx, qa_actor_id actor);
  qa_weapon_step_result (*step)(void *ctx, qa_weapon_step_input input, const qa_arsenal_intent *intent);
  void (*remove)(void *ctx, qa_actor_id actor);
  qa_app_arsenal_ui (*ui)(void *ctx, qa_actor_id actor, qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity);
  qa_app_arsenal_view (*view)(void *ctx, qa_actor_id actor);
} qa_app_selected_arsenal;
#endif

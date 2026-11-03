/* Port of src/app/bootstrap/simulation/native-q2-travel.ts (types only):
 * transferred only after the application commits to retiring the current
 * source world. TS ReadonlyMap fields become arrays in Map order; the
 * selected arsenal keeps the SimulationTravel player spelling (types.h).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_TRAVEL_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_TRAVEL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/dropped_pickups.h"
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/source_items.h"
#include "qa/persistence/q2_classic_guest.h"

typedef struct qa_app_sim_native_q2_travel_client {
  qa_client_id client; const char *phase; /* "connected"|"active" */
  const qa_app_sim_hand_grenade_travel *hand_grenades;   /* optional */
  const qa_weapon_reference *weapon_slot;                /* optional */
  bool has_native_inventory_selection; bool native_inventory_selection_null; qa_item_id native_inventory_selection;
  const void *selected_arsenal; const char *selected_arsenal_kind; /* optional (SimulationTravel player) */
} qa_app_sim_native_q2_travel_client;
typedef struct qa_app_sim_native_q2_travel_clients {
  const qa_app_sim_dropped_pickup_level *dropped_pickups; size_t dropped_pickup_level_count; bool has_dropped_pickups;
  const qa_app_sim_native_q2_travel_client *clients; size_t client_count;
  const char *spawn_point;
} qa_app_sim_native_q2_travel_clients;
typedef struct qa_app_sim_classic_visited_entry { const char *map; qa_q2_classic_visited_level level; } qa_app_sim_classic_visited_entry;
typedef struct qa_app_sim_rerelease_visited_entry { const char *map; qa_app_sim_q2_rerelease_visited_level level; } qa_app_sim_rerelease_visited_entry;
typedef struct qa_app_sim_classic_native_q2_travel {
  qa_app_sim_native_q2_travel_clients base;
  qa_app_sim_classic_guest_world *world;
  qa_classic_original_save_files *files;
  const qa_app_sim_classic_visited_entry *visited; size_t visited_count;
} qa_app_sim_classic_native_q2_travel;
typedef struct qa_app_sim_rerelease_native_q2_travel {
  qa_app_sim_native_q2_travel_clients base;
  qa_app_sim_rerelease_guest_world *world;
  const qa_app_sim_rerelease_visited_entry *visited; size_t visited_count;
} qa_app_sim_rerelease_native_q2_travel;
/* TS NativeQ2Travel union on edition. */
typedef struct qa_app_sim_native_q2_travel {
  bool rerelease;
  qa_app_sim_classic_native_q2_travel classic;
  qa_app_sim_rerelease_native_q2_travel rerelease_travel;
} qa_app_sim_native_q2_travel;
#endif

/* Port of src/app/bootstrap/simulation/native-q2-travel.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_TRAVEL_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_TRAVEL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
typedef struct qa_app_sim_native_q2_travel_client {
  qa_client_id client; const char *phase; /* "connected"|"active" */
  bool has_hand_grenades; bool has_weapon_slot; bool has_native_inventory_selection;
  qa_item_id native_inventory_selection;
} qa_app_sim_native_q2_travel_client;
typedef struct qa_app_sim_native_q2_travel {
  const char *edition; /* "classic"|"rerelease" */
  const void *world; const char *spawn_point;
  const qa_app_sim_native_q2_travel_client *clients; size_t client_count;
  const void *dropped_pickups; const void *files; const void *visited;
} qa_app_sim_native_q2_travel;
typedef qa_app_sim_native_q2_travel qa_app_sim_classic_native_q2_travel;
typedef qa_app_sim_native_q2_travel qa_app_sim_rerelease_native_q2_travel;
#endif

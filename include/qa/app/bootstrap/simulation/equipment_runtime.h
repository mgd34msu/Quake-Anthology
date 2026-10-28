/* Port of src/app/bootstrap/simulation/equipment-runtime.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/numeric.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_hand_grenades.h"
typedef struct qa_app_sim_hand_grenade_control { qa_saved_actor_id actor; bool held; bool pressed; bool released; } qa_app_sim_hand_grenade_control;
typedef struct qa_app_sim_hand_grenade_runtime_checkpoint {
  double version;
  qa_persist_hand_grenades_checkpoint controller;
  qa_app_sim_hand_grenade_control *controls; size_t control_count; /* owned */
  qa_persist_q2_foundation_checkpoint entities;
  qa_random_state random;
} qa_app_sim_hand_grenade_runtime_checkpoint;
void qa_app_sim_hand_grenade_runtime_checkpoint_destroy(qa_app_sim_hand_grenade_runtime_checkpoint *c);
typedef struct qa_app_sim_hand_grenade_travel { const void *opaque; } qa_app_sim_hand_grenade_travel;
typedef struct qa_app_sim_hand_grenade_runtime qa_app_sim_hand_grenade_runtime;
qa_app_sim_hand_grenade_runtime *qa_app_sim_hand_grenade_runtime_create(const void *host);
void qa_app_sim_hand_grenade_runtime_destroy(qa_app_sim_hand_grenade_runtime *r);
#endif

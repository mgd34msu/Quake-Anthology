/* Port of src/app/bootstrap/simulation/equipment-runtime.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_EQUIPMENT_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/content/q2/equipment/hand_grenades.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/contracts/content.h"
#include "qa/contracts/session.h"
#include "qa/persistence/decoded.h"
/* TS HandControl plus the saved actor of a checkpoint entry. */
typedef struct qa_app_sim_hand_grenade_control { qa_saved_actor_id actor; bool held; bool pressed; bool released; } qa_app_sim_hand_grenade_control;
/** TS HandGrenadeRuntimeCheckpoint. controller.actors points at
 * controller_actors (owned); controls is owned; entities borrows its
 * arrays from entities_owner when read, or from the source game's
 * capture storage (valid until its next capture) when captured. */
typedef struct qa_app_sim_hand_grenade_runtime_checkpoint {
  double version;
  qa_hand_grenades_checkpoint controller;
  qa_hand_grenade_checkpoint_entry *controller_actors;
  qa_app_sim_hand_grenade_control *controls; size_t control_count;
  qa_q2_foundation_checkpoint entities;
  qa_persist_decoded entities_owner;
  qa_app_sim_source_random_state random;
} qa_app_sim_hand_grenade_runtime_checkpoint;
void qa_app_sim_hand_grenade_runtime_checkpoint_destroy(qa_app_sim_hand_grenade_runtime_checkpoint *c);
/** TS HandGrenadeTravel. */
typedef struct qa_app_sim_hand_grenade_travel {
  qa_hand_grenade_equipment_state state;
  qa_inventory_entry ammo;
  double seconds;
} qa_app_sim_hand_grenade_travel;
/** TS HandGrenadeRuntime. selection must be enabled; controller and
 * the independent source game/random are borrowed and must outlive it. */
typedef struct qa_app_sim_hand_grenade_runtime qa_app_sim_hand_grenade_runtime;
qa_app_sim_hand_grenade_runtime *qa_app_sim_hand_grenade_runtime_create(qa_hand_grenade_selection selection,
    qa_q2_hand_grenade_equipment *controller, qa_q2_entity_services *independent_game,
    qa_app_sim_source_random *independent_random);
void qa_app_sim_hand_grenade_runtime_destroy(qa_app_sim_hand_grenade_runtime *r);
const qa_hand_grenade_selection *qa_app_sim_hand_grenade_runtime_selection(const qa_app_sim_hand_grenade_runtime *r);
/* TS readonly independent.game (the runtime's own Q2 entity services). */
qa_q2_entity_services *qa_app_sim_hand_grenade_runtime_independent_game(const qa_app_sim_hand_grenade_runtime *r);
qa_q2_hand_grenade_equipment *qa_app_sim_hand_grenade_runtime_controller(const qa_app_sim_hand_grenade_runtime *r);
/* carry NULL is the TS undefined. */
void qa_app_sim_hand_grenade_runtime_admit(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                           const qa_app_sim_hand_grenade_travel *carry);
void qa_app_sim_hand_grenade_runtime_respawn(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor);
void qa_app_sim_hand_grenade_runtime_input(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor, bool held);
/* input's pressed/held/released are replaced by the latched controls
 * (TS Omit<...>); enabled defaults to true in TS. */
void qa_app_sim_hand_grenade_runtime_step(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                          qa_hand_grenade_equipment_input input, bool enabled);
/* False is the TS undefined. */
bool qa_app_sim_hand_grenade_runtime_travel(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                            qa_app_sim_hand_grenade_travel *out);
qa_app_sim_hand_grenade_runtime_checkpoint qa_app_sim_hand_grenade_runtime_capture(qa_app_sim_hand_grenade_runtime *r);
void qa_app_sim_hand_grenade_runtime_restore(qa_app_sim_hand_grenade_runtime *r,
                                             const qa_app_sim_hand_grenade_runtime_checkpoint *checkpoint);
#endif

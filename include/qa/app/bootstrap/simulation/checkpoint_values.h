/* The plain records SharedSimulation.checkpointFromSource (runtime.ts)
 * hands to encodeCheckpointValue, as checkpoint-value trees. TS has no
 * writer functions: it encodes each live object, so key order is the
 * order in which TS built that object (the capture literal, a spread, or
 * the content constructor). Each builder below names the TS expression
 * it reproduces; each has a reader in its owner module. Trees are owned
 * by the caller (qa_save_value_free); out of memory aborts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CHECKPOINT_VALUES_H
#define QA_APP_BOOTSTRAP_SIMULATION_CHECKPOINT_VALUES_H
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include "qa/app/bootstrap/simulation/monster_checkpoint.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/movement.h"
#include "qa/persistence/value.h"

/* world:q3-runtime arsenals[].state: Q3ArsenalRuntimeState (q3SpawnArsenalRuntime order). */
qa_save_value *qa_app_q3_arsenal_runtime_state_value(const qa_q3_arsenal_runtime_state *state);
/* world:q3-runtime movement[].state: player.sourceMovement (ClientMovementOptions | null). */
qa_save_value *qa_app_sim_movement_player_source_movement_value(const qa_app_sim_movement_player *player);
/* world:q3-runtime lastAttacks[].attack: saveQ2Attack(attack). */
qa_save_value *qa_app_sim_save_q2_attack_value(const qa_attack_provenance *attack);
/* HandGrenadeRuntime.capture(). */
qa_save_value *qa_app_sim_hand_grenade_runtime_checkpoint_value(const qa_app_sim_hand_grenade_runtime_checkpoint *checkpoint);
/* GrappleRuntime.capture(). */
qa_save_value *qa_app_sim_grapple_runtime_checkpoint_value(const qa_app_sim_grapple_runtime_checkpoint *checkpoint);
/* QvmGrappleSource.capture(). */
qa_save_value *qa_app_sim_qvm_grapple_source_checkpoint_value(const qa_app_sim_qvm_grapple_source_checkpoint *checkpoint);
/* WeaponSlot.snapshot(). */
qa_save_value *qa_app_sim_weapon_slot_state_value(qa_app_sim_weapon_slot_state state);
/* SharedSimulation.captureForeignWorld(). */
qa_save_value *qa_app_sim_foreign_q2_world_checkpoint_value(const qa_app_sim_foreign_q2_world_checkpoint *checkpoint);
/* Q3SelectedArsenal.capture(actor). */
qa_save_value *qa_app_q3_selected_arsenal_checkpoint_value(const qa_app_q3_selected_arsenal_checkpoint *checkpoint);
/* ArsenalState in the literal order of its family (Q3 state before ammo). */
qa_save_value *qa_app_sim_arsenal_state_value(const qa_arsenal_state *arsenal);
/* Q3CharacterActor.capture(). */
qa_save_value *qa_q3_character_checkpoint_value(const qa_q3_character_checkpoint *checkpoint);
/* Q1TravelState (newQ1Travel / captureQ1Travel order). */
qa_save_value *qa_q1_travel_state_value(const qa_q1_travel_state *state);
/* sourceModels entry: { ...model, actor: savedActorId(model.actor) } of a Q2 "model" event. */
qa_save_value *qa_q2_presentation_model_value(const qa_q2_presentation_event *model);
/* read_order: the sourceModels checkpoint reader's record (alpha last); false is the emitted "model" literal. */
qa_save_value *qa_q2_presentation_model_value_ordered(const qa_q2_presentation_event *model, bool read_order);
/* Q2PlayerView (q2BuildView order; helpIcon appended when true). */
qa_save_value *qa_q2_player_view_value(const qa_q2_base_player_view *view);
#endif

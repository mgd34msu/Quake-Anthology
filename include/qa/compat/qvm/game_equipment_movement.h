/* Port of src/compat/qvm/game-equipment-movement.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_EQUIPMENT_MOVEMENT_H
#define QA_COMPAT_QVM_GAME_EQUIPMENT_MOVEMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/movement/body_shape.h"

typedef struct qa_qvm_equipment_movement_profile {
  double move, slice, duck, movement_global;
  double locomotion_entry, locomotion_join;
  double mins, maxs;
  bool has_body_trace;
  double body_trace_callback, body_trace_mask;
} qa_qvm_equipment_movement_profile;

/* TS QvmEquipmentMotion. pose NULL is TS null; body NULL is TS undefined
 * (both borrowed from the services implementation). */
typedef struct qa_qvm_equipment_motion {
  double speed_multiplier;
  const qa_fixed_movement_pose *pose;
  bool owns_holdable_input;
  const qa_movement_body_shape *body;
} qa_qvm_equipment_motion;

typedef struct qa_qvm_equipment_movement_services {
  void *ctx;
  bool (*actor)(void *ctx, double slot, qa_actor_id *out); /* false is TS null */
  bool (*live)(void *ctx, qa_actor_id actor);
  bool (*equipment)(void *ctx, qa_actor_id actor, qa_qvm_equipment_motion *out);
} qa_qvm_equipment_movement_services;

typedef struct qa_qvm_equipment_movement qa_qvm_equipment_movement;

/* NULL only on OOM. `game`/`artifact` are borrowed. */
qa_qvm_equipment_movement *qa_qvm_equipment_movement_create(
    qa_qvm_game *game, const qa_resolved_qvm_artifact *artifact,
    qa_qvm_equipment_movement_profile profile, qa_qvm_equipment_movement_services services);
void qa_qvm_equipment_movement_destroy(qa_qvm_equipment_movement *movement);
void qa_qvm_equipment_movement_close(qa_qvm_equipment_movement *movement);

/* TS movement(): kind keeps its exact strings ("client-command" |
 * "movement-slice"); run is the TS run closure. */
int32_t qa_qvm_equipment_movement_movement(qa_qvm_equipment_movement *movement,
                                           qa_qvm_module_function_call *call, const char *kind,
                                           int32_t (*run)(void *ctx), void *run_ctx);

/* TS prepareWeapon()'s returned closure state; call restores the word. */
typedef struct qa_qvm_equipment_weapon_restore {
  qa_qvm_equipment_movement *movement;
  qa_qvm_memory *guest;
  int64_t address;
  double bytes;
  int32_t previous, projected;
  qa_actor_id actor;
} qa_qvm_equipment_weapon_restore;

/* False is TS undefined. */
bool qa_qvm_equipment_movement_prepare_weapon(qa_qvm_equipment_movement *movement,
                                              qa_actor_id actor, qa_qvm_module_function_call *call,
                                              qa_qvm_equipment_weapon_restore *out);
void qa_qvm_equipment_weapon_restore_call(qa_qvm_equipment_weapon_restore *restore);

#endif

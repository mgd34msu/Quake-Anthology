/*
 * Port of src/compat/qvm/bot-navigation-syscalls.ts: Q3 AAS and bot movement
 * imports over the real SourceBotNavigation, BotLibrary and AasBspEntities.
 * C accommodation: the SourceBotNavigation operations take goal, move-result and
 * target values where TS passes live guest references, so the dispatcher reads
 * the records before the call and stores the fields the operation wrote back
 * through the guest view (a guest that aliases these records with one another
 * observes the stores after the operation, not during it).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_BOT_NAVIGATION_SYSCALLS_H
#define QA_COMPAT_QVM_BOT_NAVIGATION_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS QvmAasMovementPrediction. */
typedef struct qa_qvm_aas_movement_prediction {
  bool success;
  qa_vec3 end;
  double end_area;
  qa_vec3 velocity;
  struct {
    bool start_solid;
    double fraction;
    qa_vec3 end;
    double entity_num, last_area, area, plane;
  } trace;
  double presence, stop_event, end_contents, time, frames;
} qa_qvm_aas_movement_prediction;

/* TS QvmBotNavigationServices. navigation, library and bsp_entities are borrowed.
 * Every callback receives ctx. entity_info false is TS null. */
typedef struct qa_qvm_bot_navigation_services {
  qa_source_bot_navigation_bots_a *navigation;
  qa_bot_library_bots_a *library;
  qa_aas_bsp_entities *bsp_entities;
  void *ctx;
  double (*time)(void *ctx);
  bool (*initialized)(void *ctx);
  bool (*entity_info)(void *ctx, double number, qa_qvm_aas_entity_info *out);
  qa_qvm_aas_movement_prediction (*predict_client_movement)(void *ctx, qa_bot_movement_prediction query);
} qa_qvm_bot_navigation_services;

/* TS (actor: ActorId) => number. */
typedef struct qa_qvm_entity_number_fn {
  void *ctx;
  double (*call)(void *ctx, qa_actor_id actor);
} qa_qvm_entity_number_fn;

/* TS qvmBotMovementPrediction. */
qa_qvm_aas_movement_prediction qa_qvm_bot_movement_prediction(qa_source_bot_navigation_bots_a *navigation,
                                                              qa_bot_movement_prediction query,
                                                              qa_qvm_entity_number_fn entity_number);

bool qa_qvm_bot_navigation_syscall(const qa_qvm_host_call *call, qa_qvm_bot_navigation_services *services,
                                   qa_qvm_host_result *out);

#endif

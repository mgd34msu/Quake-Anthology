/* Port of src/bots/behavior/q3/navigation-services.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_NAVIGATION_SERVICES_H
#define QA_BOTS_BEHAVIOR_Q3_NAVIGATION_SERVICES_H
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation.h"
/* TS SelectedBotNavigation: runtime, forClient, crouchedBounds,
 * predictClientMovement and travelWeapon of the host, with ctx for each. */
typedef struct qa_selected_bot_navigation_bots_a {
  void *ctx;
  qa_navigation_runtime *runtime;
  qa_navigation_runtime *(*for_client)(void *ctx, int64_t client);
  qa_bounds crouched_bounds;
  qa_bot_travel_prediction_result (*predict_client_movement)(void *ctx,
                                                             qa_bot_movement_prediction query);
  bool (*travel_weapon)(void *ctx, int64_t client, const char *mode, int64_t *out);
} qa_selected_bot_navigation_bots_a;
/* TS q3BotNavigation: source queries borrow existing game bindings; the caller
 * owns the selected movement projections. game, library, selected and
 * bsp_entities must outlive out, which must not move after this call. */
typedef struct qa_q3_bot_navigation_bots_a {
  qa_source_bot_navigation_bots_a navigation;
  qa_source_bot_game_bots_a *game;
  qa_bot_library_bots_a *library;
  qa_selected_bot_navigation_bots_a selected;
} qa_q3_bot_navigation_bots_a;
void qa_q3_bot_navigation_bots_a_init(qa_q3_bot_navigation_bots_a *out,
                                      qa_source_bot_game_bots_a *game,
                                      qa_bot_library_bots_a *library,
                                      const qa_selected_bot_navigation_bots_a *selected,
                                      qa_aas_bsp_entities *bsp_entities);
#endif

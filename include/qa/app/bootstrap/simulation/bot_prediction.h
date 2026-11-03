/* Port of src/app/bootstrap/simulation/bot-prediction.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_PREDICTION_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_PREDICTION_H
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/behavior/prediction.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/runtime.h"
/* TS predictApplicationBotMovement(simulation, player, query, navigation):
 * navigation is NavigationRuntime | AasAsset; pass exactly one of runtime and
 * asset. Gameplay and AAS authoring share detached selected movement and the
 * caller's exact area graph. Release the result with
 * qa_bot_travel_prediction_result_destroy. */
qa_bot_travel_prediction_result qa_app_sim_predict_application_bot_movement(const qa_app_sim_bot_simulation *simulation,
    const qa_app_sim_movement_prediction_player *player, qa_bot_movement_prediction query,
    qa_navigation_runtime *runtime, const qa_aas_asset *asset);
#endif

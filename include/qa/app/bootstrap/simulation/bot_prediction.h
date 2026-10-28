/* Port of src/app/bootstrap/simulation/bot-prediction.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_PREDICTION_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_PREDICTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_bot_prediction qa_app_sim_bot_prediction;
typedef struct qa_app_sim_bot_prediction_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_bot_prediction_options;
qa_app_sim_bot_prediction *qa_app_sim_bot_prediction_create(qa_app_sim_bot_prediction_options options);
void qa_app_sim_bot_prediction_destroy(qa_app_sim_bot_prediction *value);
#endif

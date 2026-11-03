/* Port of src/app/bootstrap/simulation/bot-rerelease.ts: native rerelease
 * bot decisions drive existing shared player commands; this owns no
 * simulation or collision state. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include "qa/app/bootstrap/simulation/bot_objectives.h"
#include "qa/app/bootstrap/simulation/bot_rerelease_world.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/behavior/rerelease/profile.h"
typedef struct qa_app_sim_rerelease_bots qa_app_sim_rerelease_bots;
/* TS new ApplicationRereleaseBots(options, assets): assets must be kind
 * "rerelease"; options and assets are borrowed. TS throws raise. */
qa_app_sim_rerelease_bots *qa_app_sim_rerelease_bots_create(const qa_app_sim_bots_options *options,
    const qa_app_sim_application_bot_assets *assets);
void qa_app_sim_rerelease_bots_destroy(qa_app_sim_rerelease_bots *bots);
const qa_app_sim_bot_service *qa_app_sim_rerelease_bots_service(qa_app_sim_rerelease_bots *bots);
/* TS createApplicationBots(options, assets): the rerelease or Q3 transport.
 * out_rerelease / out_q3 receive the owner to destroy (one is NULL). */
const qa_app_sim_bot_service *qa_app_sim_create_application_bots(const qa_app_sim_bots_options *options,
    const qa_app_sim_application_bot_assets *assets, qa_app_sim_rerelease_bots **out_rerelease,
    qa_app_sim_application_bots **out_q3);
/* The behavior entry of a saved native bot client: the TS
 * RereleaseBehaviorCheckpoint object (brain memory in the brain's property
 * order). Returns an owned tree. */
qa_save_value *qa_app_sim_encode_rerelease_behavior(const qa_rerelease_behavior_checkpoint *checkpoint);
#endif

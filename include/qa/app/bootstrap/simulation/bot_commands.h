/* Port of src/app/bootstrap/simulation/bot-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_COMMANDS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_COMMANDS_H
#include "qa/bots/behavior/rerelease/world.h"
#include "qa/contracts/protocol.h"
/* dialect is a MovementProfile kind string. */
qa_user_command qa_app_sim_rerelease_bot_command(qa_bot_usercmd source, const char *dialect,
    double milliseconds, double server_time);
#endif

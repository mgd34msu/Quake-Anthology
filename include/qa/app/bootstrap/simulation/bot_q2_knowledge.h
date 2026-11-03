/* Port of src/app/bootstrap/simulation/bot-q2-knowledge.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_Q2_KNOWLEDGE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_Q2_KNOWLEDGE_H
#include "qa/app/bootstrap/simulation/bots.h"
/* TS createQ2BotKnowledge({ simulation, actorForClient }). The simulation
 * and the actor_for_client ctx are borrowed. Raises when the selected Q2
 * arsenal is absent. */
qa_app_sim_bot_arsenal *qa_app_sim_create_q2_bot_knowledge(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx);
#endif

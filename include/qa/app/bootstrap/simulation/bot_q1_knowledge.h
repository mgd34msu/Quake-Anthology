/* Port of src/app/bootstrap/simulation/bot-q1-knowledge.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_Q1_KNOWLEDGE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_Q1_KNOWLEDGE_H
#include "qa/app/bootstrap/simulation/bots.h"
/* TS createQ1BotKnowledge({ simulation, actorForClient }). The simulation
 * and the actor_for_client ctx are borrowed. Raises when the selected Q1
 * arsenal is absent. */
qa_app_sim_bot_arsenal *qa_app_sim_create_q1_bot_knowledge(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx);
#endif

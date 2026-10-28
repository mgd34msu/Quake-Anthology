/* Port of src/app/bootstrap/simulation/bot-q1-knowledge.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_Q1_KNOWLEDGE_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_Q1_KNOWLEDGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_q1_bot_knowledge qa_app_sim_q1_bot_knowledge;
typedef struct qa_app_sim_q1_bot_knowledge_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_q1_bot_knowledge_options;
qa_app_sim_q1_bot_knowledge *qa_app_sim_q1_bot_knowledge_create(qa_app_sim_q1_bot_knowledge_options options);
void qa_app_sim_q1_bot_knowledge_destroy(qa_app_sim_q1_bot_knowledge *value);
#endif

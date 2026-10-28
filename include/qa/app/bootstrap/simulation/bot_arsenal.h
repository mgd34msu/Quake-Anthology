/* Port of src/app/bootstrap/simulation/bot-arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_ARSENAL_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_ARSENAL_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_app_sim_bot_arsenal_binding qa_app_sim_bot_arsenal_binding;
typedef struct qa_app_sim_bot_arsenal_source {
  void *ctx;
  bool (*has_q2)(void *ctx); bool (*has_q1)(void *ctx); bool (*has_q3)(void *ctx);
  qa_app_sim_bot_arsenal_binding *(*create_q1)(void *ctx);
  qa_app_sim_bot_arsenal_binding *(*create_q2)(void *ctx);
  qa_app_sim_bot_arsenal_binding *(*create_q3)(void *ctx);
} qa_app_sim_bot_arsenal_source;
/* TS createBotArsenalBinding: Q2 wins, then Q1, then Q3; NULL = TS null. */
qa_app_sim_bot_arsenal_binding *qa_app_sim_create_bot_arsenal_binding(qa_app_sim_bot_arsenal_source source);
void qa_app_sim_bot_arsenal_binding_destroy(qa_app_sim_bot_arsenal_binding *b);
#endif

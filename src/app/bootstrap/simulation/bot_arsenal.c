/* Port of src/app/bootstrap/simulation/bot-arsenal.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_arsenal.h"
#include <stdlib.h>
struct qa_app_sim_bot_arsenal_binding { int unused; };
qa_app_sim_bot_arsenal_binding *qa_app_sim_create_bot_arsenal_binding(qa_app_sim_bot_arsenal_source source) {
  if (source.has_q2 != NULL && source.has_q2(source.ctx)) return source.create_q2 != NULL ? source.create_q2(source.ctx) : NULL;
  if (source.has_q1 != NULL && source.has_q1(source.ctx)) return source.create_q1 != NULL ? source.create_q1(source.ctx) : NULL;
  if (source.has_q3 != NULL && source.has_q3(source.ctx)) return source.create_q3 != NULL ? source.create_q3(source.ctx) : NULL;
  return NULL;
}
void qa_app_sim_bot_arsenal_binding_destroy(qa_app_sim_bot_arsenal_binding *b) { free(b); }

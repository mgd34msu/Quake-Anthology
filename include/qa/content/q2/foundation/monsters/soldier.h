/* Port of src/content/q2/foundation/monsters/soldier.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_SOLDIER_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_SOLDIER_H
#include "qa/content/q2/foundation/monsters/types.h"
void qa_q2m_soldier_stand_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_soldier_walk_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_soldier_run_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_soldier_attack_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_soldier_sight_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_soldier_pain_handler(void *ctx, qa_q2m_context *context, qa_pain_reaction reaction);
void qa_q2m_soldier_die_handler(void *ctx, qa_q2m_context *context, qa_death_reaction reaction);
bool qa_q2m_soldier_sidestep(void *ctx, qa_q2m_context *context);
bool qa_q2m_soldier_duck_fn(void *ctx, qa_q2m_context *context, double eta);
/* TS soldierStand/soldierWalk/... plain forms. */
#define qa_q2m_soldier_stand(c) qa_q2m_soldier_stand_handler(NULL, c)
extern const qa_q2m_named_handler qa_q2m_soldier_callbacks[];
extern const size_t qa_q2m_soldier_callback_count;
const qa_q2m_handler *qa_q2m_soldier_callback(const char *name);
#endif

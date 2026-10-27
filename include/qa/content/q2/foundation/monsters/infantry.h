/* Port of src/content/q2/foundation/monsters/infantry.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_INFANTRY_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_INFANTRY_H
#include "qa/content/q2/foundation/monsters/types.h"
void qa_q2m_infantry_stand_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_infantry_walk_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_infantry_run_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_infantry_attack_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_infantry_sight_handler(void *ctx, qa_q2m_context *context);
void qa_q2m_infantry_pain_handler(void *ctx, qa_q2m_context *context, qa_pain_reaction reaction);
void qa_q2m_infantry_die_handler(void *ctx, qa_q2m_context *context, qa_death_reaction reaction);
bool qa_q2m_infantry_duck_fn(void *ctx, qa_q2m_context *context, double eta);
bool qa_q2m_infantry_sidestep(void *ctx, qa_q2m_context *context);
extern const qa_q2m_named_handler qa_q2m_infantry_callbacks[];
extern const size_t qa_q2m_infantry_callback_count;
const qa_q2m_handler *qa_q2m_infantry_callback(const char *name);
#endif

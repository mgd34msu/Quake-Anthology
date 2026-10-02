#ifndef QA_BOT_AI_SOURCE_ACTIVATION_H
#define QA_BOT_AI_SOURCE_ACTIVATION_H
#include "qa/bots_memory.h"
struct qa_bots;
struct bot_ai_state;
bool bot_ai_activation_validate(const struct bot_ai_state *,qa_error *);
bool bot_ai_activation_top(const struct bot_ai_state *,uint32_t *,bool *,qa_error *);
qa_bot_source_activation bot_ai_activation_read(const struct bot_ai_state *,uint32_t);
void bot_ai_activation_time_set(struct bot_ai_state *,uint32_t,float);
void bot_ai_activation_weapon_set(struct bot_ai_state *,uint32_t,int32_t);
bool bot_ai_activation_contains(const struct bot_ai_state *,int32_t,float,bool *,qa_error *);
bool bot_ai_activation_push(struct bot_ai_state *,const qa_bot_source_activation *,float,bool *,qa_error *);
bool bot_ai_activation_pop(struct qa_bots *,struct bot_ai_state *,qa_error *);
bool bot_ai_activation_clear(struct qa_bots *,struct bot_ai_state *,qa_error *);
#endif

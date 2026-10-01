#ifndef QA_BOT_AI_SOURCE_STORAGE_H
#define QA_BOT_AI_SOURCE_STORAGE_H
#include "qa/bots_memory.h"

struct qa_bots;
struct bot_ai_state;
bool bot_ai_storage_i32(struct qa_bots *,struct bot_ai_state *,uint32_t,int32_t *,bool,qa_error *);
bool bot_ai_storage_u32(struct qa_bots *,struct bot_ai_state *,uint32_t,uint32_t *,bool,qa_error *);
bool bot_ai_storage_f32(struct qa_bots *,struct bot_ai_state *,uint32_t,float *,bool,qa_error *);
bool bot_ai_storage_bool(struct qa_bots *,struct bot_ai_state *,uint32_t,bool *,bool,qa_error *);
bool bot_ai_storage_vec3(struct qa_bots *,struct bot_ai_state *,uint32_t,qa_vec3 *,bool,qa_error *);
bool bot_ai_storage_goal(struct qa_bots *,struct bot_ai_state *,uint32_t,qa_bot_goal *,bool,qa_error *);
bool bot_ai_storage_publish(struct qa_bots *,struct bot_ai_state *,qa_error *);
bool bot_ai_storage_settings(struct qa_bots *,struct bot_ai_state *,const qa_bot_admission *,qa_error *);
#endif

#ifndef QA_BOT_AI_SOURCE_COMMAND_H
#define QA_BOT_AI_SOURCE_COMMAND_H
#include "internal.h"
bool bot_ai_source_command_read(qa_bots *, bot_ai_state *, qa_movement_command *, qa_error *);
bool bot_ai_source_command_write(qa_bots *, bot_ai_state *, const qa_movement_command *, qa_error *);
bool bot_ai_source_command_pause(qa_bots *, bot_ai_state *, int32_t time, qa_error *);
bool bot_ai_source_action(qa_bots *,bot_ai_state *,uint32_t,qa_error *);
bool bot_ai_source_action_text(qa_bots *,bot_ai_state *,qa_bot_text_action,int32_t,const char *,qa_error *);
bool bot_ai_source_action_view(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_source_action_weapon(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_source_action_input(qa_bots *,bot_ai_state *,float,qa_bot_input *,qa_error *);
#endif

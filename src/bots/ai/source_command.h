#ifndef QA_BOT_AI_SOURCE_COMMAND_H
#define QA_BOT_AI_SOURCE_COMMAND_H
#include "internal.h"
bool bot_ai_source_command_read(qa_bots *, bot_ai_state *, qa_movement_command *, qa_error *);
bool bot_ai_source_command_write(qa_bots *, bot_ai_state *, const qa_movement_command *, qa_error *);
bool bot_ai_source_command_pause(qa_bots *, bot_ai_state *, int32_t time, qa_error *);
#endif

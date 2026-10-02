#ifndef QA_BOT_WEAPONS_SOURCE_H
#define QA_BOT_WEAPONS_SOURCE_H
#include "qa/bot_runtime.h"

/* Borrowed exact source weapon_info_t bytes, including its embedded projectile.
 * The view ends when its actual BotMemory allocation is reset or disposed.
 * The source number and live handle checks precede any byte access. */
bool qa_bot_runtime_weapon_source_info(qa_bot_runtime *,uint32_t handle,uint32_t number,
    qa_bytes *,bool *found,qa_error *);
bool qa_bot_runtime_weapon_dump(qa_bot_runtime *,qa_error *);
#endif

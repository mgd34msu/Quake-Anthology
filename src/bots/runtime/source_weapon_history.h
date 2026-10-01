#ifndef QA_BOT_SOURCE_WEAPON_HISTORY_H
#define QA_BOT_SOURCE_WEAPON_HISTORY_H
#include "source_weapon_state.h"
#include "../library/source_fuzzy_checkpoint.h"
#include "qa/bots_allocator_checkpoint.h"

typedef struct bot_weapon_pointer_history bot_weapon_pointer_history;
typedef struct bot_weapon_pointer_restore bot_weapon_pointer_restore;
bool bot_weapon_pointer_capture(bot_weapon_pointers *,bot_weapon_state *,uint32_t,qa_bot_weapons *,
    bot_fuzzy_history *,bot_weapon_pointer_history **,qa_error *);
void bot_weapon_pointer_history_destroy(bot_weapon_pointer_history *);
bool bot_weapon_pointer_prepare(bot_weapon_pointers *,bot_weapon_state *,uint32_t,qa_bot_weapons **,
    const bot_weapon_pointer_history *,
    const qa_bot_memory_prepared *,bot_weapon_pointer_restore **,qa_error *);
/* Finish MEMORY and fuzzy configurations before publishing these aliases. */
void bot_weapon_pointer_finish(bot_weapon_pointer_restore *,bool);
#endif

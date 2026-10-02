#ifndef QA_BOT_SOURCE_WEAPON_STANDALONE_SAVE_H
#define QA_BOT_SOURCE_WEAPON_STANDALONE_SAVE_H
#include "qa/bot_library.h"
#include "qa/source_save.h"

bool bot_weapons_standalone_fields(qa_source_save_io *,const qa_bot_weapons *,qa_bot_weapons **);
bool bot_weapons_alias_fields(qa_source_save_io *,const qa_bot_weapons *,
    const qa_bot_weapons *,qa_bot_weapons **);
#endif

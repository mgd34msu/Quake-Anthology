#ifndef QA_BOT_SOURCE_WEAPON_LIBRARY_SAVE_H
#define QA_BOT_SOURCE_WEAPON_LIBRARY_SAVE_H
#include "source_weapon_library.h"
#include "qa/source_save.h"

/* The actual library MEMORY has already been imported. Partial readers and
 * source hunk aliases retain the same library's services and globals. */
bool bot_weapons_library_fields(qa_source_save_io *,qa_bot_library *,
    const bot_weapon_resource_host *,const qa_bot_weapons *,qa_bot_weapons **);
#endif

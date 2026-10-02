#ifndef QA_BOT_SOURCE_WEAPON_STANDALONE_H
#define QA_BOT_SOURCE_WEAPON_STANDALONE_H
#include "source_weapon_resource.h"

/* Isolated construction for value imports. There is no fabricated resolver or
 * runtime. The caller binds its actual restored hunk before publication. */
bool bot_weapon_resource_pure(qa_bot_memory *,bot_weapon_resource **,qa_error *);
bool bot_weapon_standalone_restore(const qa_bot_weapons_view *,bot_weapon_resource **,qa_error *);
#endif

#ifndef QA_BOT_SOURCE_WEAPON_RESOURCE_HISTORY_H
#define QA_BOT_SOURCE_WEAPON_RESOURCE_HISTORY_H
#include "source_weapon_resource.h"

/* Pure native metadata/PC copy. Capture rebinds bound array membership from
 * the bytes being captured. Prepare copies that captured membership and
 * remaps its allocation before MEMORY commit. Neither opens a resource. */
bool bot_weapon_resource_clone(const bot_weapon_resource *,bool,
    bot_weapon_resource **,qa_error *);
#endif

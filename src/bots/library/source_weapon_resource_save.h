#ifndef QA_BOT_SOURCE_WEAPON_RESOURCE_SAVE_H
#define QA_BOT_SOURCE_WEAPON_RESOURCE_SAVE_H
#include "source_weapon_resource.h"
#include "qa/source_save.h"

typedef struct bot_weapon_resource_factory {
    qa_bot_memory *memory;
    const qa_script_services *services;
    const qa_script_options *options;
    const bot_weapon_resource_host *host;
} bot_weapon_resource_factory;
/* MEMORY is imported first. This component encodes allocation aliases and
 * the reached PC state; it never opens a source or replays a diagnostic. */
bool bot_weapon_resource_fields(qa_source_save_io *,const bot_weapon_resource *,
    const bot_weapon_resource_factory *,bot_weapon_resource **);
#endif

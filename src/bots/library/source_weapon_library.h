#ifndef QA_BOT_SOURCE_WEAPON_LIBRARY_H
#define QA_BOT_SOURCE_WEAPON_LIBRARY_H
#include "source_weapon_resource.h"

bool bot_weapons_load_source(qa_bot_library *,const char *,size_t,size_t,
    const bot_weapon_resource_host *,qa_bot_weapons **,bool *,qa_error *);
const qa_bot_weapons_view *bot_weapons_source_view(qa_bot_weapons *,qa_error *);
bool bot_weapons_source_name(const qa_bot_weapons *,uint32_t,char [81],qa_error *);
bool bot_weapons_source_member(const qa_bot_weapons *,uint32_t,bool *,qa_error *);
bool bot_weapons_source_valid(const qa_bot_weapons *,uint32_t,bool *,qa_error *);
bool bot_weapons_source_capacity(const qa_bot_weapons *,int32_t *,qa_error *);
bool bot_weapons_source_bytes(const qa_bot_weapons *,uint32_t,qa_bytes *,qa_error *);
bool bot_weapons_source_info(const qa_bot_weapons *,uint32_t,qa_bot_weapon_info *,
    qa_bot_projectile_info *,qa_error *);
bool bot_weapons_source_free(qa_bot_weapons *,qa_error *);
bot_weapon_resource_host bot_weapons_library_host(qa_bot_library *);
#endif

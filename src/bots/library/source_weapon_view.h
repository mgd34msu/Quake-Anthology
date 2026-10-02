#ifndef QA_BOT_SOURCE_WEAPON_VIEW_H
#define QA_BOT_SOURCE_WEAPON_VIEW_H
#include "source_weapon_resource.h"

/* Public metadata strings retain their existing 80-byte C representation.
 * Algorithms needing the complete source string read its 81-byte getter. */
bool bot_weapon_projectile_value(const bot_weapon_config_cell *,qa_bot_projectile_info *,qa_error *);
bool bot_weapon_info_value(const bot_weapon_config_cell *,qa_bot_weapon_info *,
    qa_bot_projectile_info *,qa_error *);
bool bot_weapon_resource_info(const bot_weapon_resource *,uint32_t,qa_bot_weapon_info *,
    qa_bot_projectile_info *,qa_error *);
#endif

#ifndef QA_BOT_RUNTIME_SOURCE_WEAPON_SETUP_H
#define QA_BOT_RUNTIME_SOURCE_WEAPON_SETUP_H
#include "internal.h"
#include "qa/source_save.h"

bot_weapon_resource_host bot_runtime_weapon_host(qa_bot_runtime *);
bool bot_runtime_weapon_setup(qa_bot_runtime *,int32_t *,qa_error *);
bool bot_runtime_weapon_emit(qa_bot_runtime *,qa_script_severity,const char *,const char *,qa_error *);
void bot_runtime_weapon_diagnostics_clear(qa_bot_runtime *);
bool bot_runtime_weapon_diagnostics_fields(qa_source_save_io *,qa_bot_runtime *);
bool bot_weapon_diagnostics_copy(const bot_weapon_diagnostic *,size_t,bot_weapon_diagnostic **,qa_error *);
void bot_weapon_diagnostics_dispose(bot_weapon_diagnostic *,size_t);
#endif

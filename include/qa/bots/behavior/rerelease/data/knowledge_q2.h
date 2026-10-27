/* Port of src/bots/behavior/rerelease/data/knowledge-q2.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_Q2_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_Q2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/assets.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
typedef struct qa_bot_weapon_binding { const char *name; int64_t bit; const char *classname, *ammo_classname; } qa_bot_weapon_binding;
extern const qa_bot_weapon_binding QA_BOT_WEAPON_BINDINGS[21];
#define QA_BOT_WEAPON_BINDING_COUNT 21
extern const char *const QA_BOT_AMMO_CLASSNAMES[13];
#define QA_BOT_AMMO_CLASSNAME_COUNT 13
typedef struct qa_bot_objective_binding { const char *classname; int64_t team; } qa_bot_objective_binding;
extern const qa_bot_objective_binding QA_BOT_OBJECTIVE_BINDINGS[2];
bool qa_bot_build_knowledge(qa_bot_knowledge *out, qa_bot_data_files files);
qa_bot_game_mode qa_bot_get_game_mode_q2(const qa_bot_knowledge *k, double (*cvar_value)(void *ctx, const char *name), void *ctx);
/* platform: "PC"|"Consoles"|"Nintendo". Missing weapons/settings aborts (TS Error). */
bool qa_bot_load_knowledge(qa_bot_knowledge *out, qa_bot_asset_files *files, const char *platform);
#endif

/* Port of src/app/bootstrap/simulation/bot-knowledge-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_KNOWLEDGE_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_KNOWLEDGE_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
typedef struct qa_app_sim_bot_knowledge_actor { double handle; qa_saved_actor_id actor; } qa_app_sim_bot_knowledge_actor;
typedef struct qa_app_sim_bot_knowledge_checkpoint {
  double version; const char *source; /* "q1"|"q2"|"q3" */
  qa_app_sim_bot_knowledge_actor *actors; size_t actor_count; /* owned array */
} qa_app_sim_bot_knowledge_checkpoint;
typedef struct qa_app_sim_bot_knowledge_entry { double handle; qa_actor_id actor; } qa_app_sim_bot_knowledge_entry;
typedef struct qa_app_sim_bot_knowledge_persistence qa_app_sim_bot_knowledge_persistence;
qa_app_sim_bot_knowledge_persistence *qa_app_sim_bot_knowledge_persistence_create(const char *source,
    qa_app_sim_bot_knowledge_entry *entries, size_t entry_count);
void qa_app_sim_bot_knowledge_persistence_destroy(qa_app_sim_bot_knowledge_persistence *p);
qa_app_sim_bot_knowledge_checkpoint qa_app_sim_bot_knowledge_checkpoint_of(qa_app_sim_bot_knowledge_persistence *p);
void qa_app_sim_bot_knowledge_checkpoint_destroy(qa_app_sim_bot_knowledge_checkpoint *c);
/* actor/ weaponHandle closures as fn+ctx. weaponHandle returns NAN-free handle or negative on invalid. */
typedef qa_actor_id (*qa_app_sim_saved_actor_fn)(void *ctx, qa_saved_actor_id saved);
typedef double (*qa_app_sim_weapon_handle_fn)(void *ctx, double saved);
void qa_app_sim_bot_knowledge_restore(qa_app_sim_bot_knowledge_persistence *p, const qa_save_value *value,
    qa_app_sim_saved_actor_fn actor_fn, void *actor_ctx, qa_app_sim_weapon_handle_fn weapon_fn, void *weapon_ctx);
size_t qa_app_sim_bot_knowledge_entry_count(const qa_app_sim_bot_knowledge_persistence *p);
const qa_app_sim_bot_knowledge_entry *qa_app_sim_bot_knowledge_entries(const qa_app_sim_bot_knowledge_persistence *p);
#endif

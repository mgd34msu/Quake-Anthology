#ifndef QA_BOT_CHECKPOINT_INTERNAL_H
#define QA_BOT_CHECKPOINT_INTERNAL_H
#include "qa/bot_runtime.h"
#include "qa/bots_allocator.h"
#include "qa/bots_allocator_checkpoint.h"
#include "library/source_fuzzy_checkpoint.h"

typedef struct bot_goal_history bot_goal_history;
typedef struct bot_goal_history_restore bot_goal_history_restore;
bool bot_goal_history_capture(qa_bot_goals *,bot_fuzzy_history *,bot_goal_history **,qa_error *);
void bot_goal_history_destroy(bot_goal_history *);
bool bot_goal_history_prepare(qa_bot_goals *,const bot_goal_history *,const qa_bot_memory_prepared *,
    bot_goal_history_restore **,qa_error *);
void bot_goal_history_finish(bot_goal_history_restore *,bool);

typedef struct bot_weapon_restore bot_weapon_restore;
typedef struct bot_weapon_history bot_weapon_history;
typedef struct bot_chat_restore bot_chat_restore;
typedef struct bot_chat_restore_entry {
    qa_bot_chat *chat;
    const qa_bot_chat_state *state;
} bot_chat_restore_entry;

bool bot_runtime_restore_begin(qa_bot_runtime *, qa_error *);
void bot_runtime_restore_end(qa_bot_runtime *);
void bot_goal_restore_lock(qa_bot_goals *, bool);
void bot_move_restore_lock(qa_bot_moves *, bool);
bool bot_move_restore_validate(qa_bot_moves *, uint32_t, const qa_bot_move_state *, qa_error *);
void bot_move_restore_commit(qa_bot_moves *, uint32_t, const qa_bot_move_state *);
void bot_action_restore_lock(qa_bot_actions *, bool);
qa_bot_input *bot_action_restore_input(qa_bot_actions *, uint32_t, qa_error *);
bool bot_weapon_checkpoint_capture(qa_bot_runtime *,uint32_t,bot_fuzzy_history *,bot_weapon_history **,qa_error *);
void bot_weapon_history_destroy(bot_weapon_history *);
bool bot_weapon_restore_prepare(qa_bot_runtime *, uint32_t, const bot_weapon_history *,
                                bot_weapon_restore **, qa_error *);
void bot_weapon_restore_finish(bot_weapon_restore *, bool commit);
bool bot_chat_restore_prepare(const bot_chat_restore_entry *, size_t, bot_chat_restore **, qa_error *);
void bot_chat_restore_finish(bot_chat_restore *, bool commit);
#endif

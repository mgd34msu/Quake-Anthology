#ifndef QA_BOT_CHECKPOINT_INTERNAL_H
#define QA_BOT_CHECKPOINT_INTERNAL_H
#include "qa/bot_runtime.h"

typedef struct bot_goal_restore bot_goal_restore;
typedef struct bot_weapon_restore bot_weapon_restore;
typedef struct bot_chat_restore bot_chat_restore;
typedef struct bot_chat_restore_entry {
    qa_bot_chat *chat;
    const qa_bot_chat_state *state;
} bot_chat_restore_entry;

bool bot_runtime_restore_begin(qa_bot_runtime *, qa_error *);
void bot_runtime_restore_end(qa_bot_runtime *);
void bot_goal_restore_lock(qa_bot_goals *, bool);
bool bot_goal_restore_prepare(qa_bot_goals *, uint32_t, const qa_bot_goal_state *,
                              qa_bot_weights *, bot_goal_restore **, qa_error *);
void bot_goal_restore_finish(bot_goal_restore *, bool commit);
void bot_move_restore_lock(qa_bot_moves *, bool);
bool bot_move_restore_validate(qa_bot_moves *, uint32_t, const qa_bot_move_state *, qa_error *);
void bot_move_restore_commit(qa_bot_moves *, uint32_t, const qa_bot_move_state *);
void bot_action_restore_lock(qa_bot_actions *, bool);
qa_bot_input *bot_action_restore_input(qa_bot_actions *, uint32_t, qa_error *);
bool bot_weapon_restore_prepare(qa_bot_runtime *, uint32_t, qa_bot_weights *,
                                bot_weapon_restore **, qa_error *);
void bot_weapon_restore_finish(bot_weapon_restore *, bool commit);
bool bot_chat_restore_prepare(const bot_chat_restore_entry *, size_t, bot_chat_restore **, qa_error *);
void bot_chat_restore_finish(bot_chat_restore *, bool commit);
#endif

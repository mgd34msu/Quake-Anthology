#ifndef QA_BOT_ACTIONS_PRIVATE_H
#define QA_BOT_ACTIONS_PRIVATE_H

#include "qa/bot_actions.h"
#include "qa/bots_allocator_checkpoint.h"

struct qa_bot_actions {
    qa_bot_action_services services;
    qa_bot_memory *memory;
    qa_bot_memory_allocation inputs;
    bool owns_memory;
    uint32_t capacity;
    bool initialized;
    bool restoring;
    bool busy;
    size_t operations;
};

typedef struct bot_action_snapshot {
    qa_bot_memory_allocation inputs;
    uint32_t capacity;
    bool initialized;
} bot_action_snapshot;
bool bot_action_snapshot_capture(qa_bot_actions *,bot_action_snapshot *,qa_error *);
bool bot_action_snapshot_prepare(qa_bot_actions *,const bot_action_snapshot *,
    const qa_bot_memory_prepared *,bot_action_snapshot *,qa_error *);
void bot_action_snapshot_finish(qa_bot_actions *,const bot_action_snapshot *,bool);

#endif

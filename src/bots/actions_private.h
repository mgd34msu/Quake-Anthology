#ifndef QA_BOT_ACTIONS_PRIVATE_H
#define QA_BOT_ACTIONS_PRIVATE_H

#include "qa/bot_actions.h"

struct qa_bot_actions {
    qa_bot_action_services services;
    qa_bot_input *inputs;
    uint32_t capacity;
    bool initialized;
    bool restoring;
};

#endif

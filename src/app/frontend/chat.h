#ifndef QA_FRONTEND_CHAT_H
#define QA_FRONTEND_CHAT_H
#include "internal.h"
bool frontend_chat_send(frontend_seat *, const char *, bool team, bool targeted,
    int32_t target, qa_error *);
#endif

#ifndef QA_APPLICATION_BOT_ADMISSION_H
#define QA_APPLICATION_BOT_ADMISSION_H

#include "internal.h"

bool application_players_bot_allocate(qa_application *, const qa_launch_seat *, int32_t *, qa_error *);
bool application_players_bot_userinfo(qa_application *, uint32_t, const char *, qa_error *);
bool application_players_bot_connect(qa_application *, uint32_t, bool first_time,
    bool bot, bool *accepted, qa_error *);
bool application_players_bot_begin(qa_application *, uint32_t, qa_error *);

#endif

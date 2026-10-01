#ifndef QA_FRONTEND_CAMPAIGN_MENU_H
#define QA_FRONTEND_CAMPAIGN_MENU_H
#include "internal.h"
enum { FRONTEND_ARENA_PROGRESS=102, FRONTEND_ARENA_SELECTION, FRONTEND_ARENA_SKILL,
    FRONTEND_ARENA_RESET, FRONTEND_ARENA_RESULT, FRONTEND_TEAM_RESULT };
bool frontend_campaign_menu_create(frontend_seat *,qa_error *);
bool frontend_campaign_menu_sync(qa_frontend *,qa_error *);
bool frontend_campaign_menu_available(frontend_seat *,bool *,qa_error *);
#endif

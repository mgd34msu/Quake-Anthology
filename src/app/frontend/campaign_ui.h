#ifndef QA_FRONTEND_CAMPAIGN_UI_H
#define QA_FRONTEND_CAMPAIGN_UI_H
#include "campaign.h"
#include "qa/application_q3_campaign_result.h"
typedef struct frontend_campaign_podium_player { int32_t client, rank, score; } frontend_campaign_podium_player;
typedef struct frontend_campaign_ui_view {
    bool installed, team, result, team_score_recorded, action_pending;
    qa_application_q3_campaign source;
    qa_application_q3_client_context client;
    const qa_base_arena_catalog *catalog;
    const qa_arena_progress *progression;
    int32_t selection, skill;
    qa_arena_result game;
    qa_arena_postgame postgame;
    frontend_campaign_podium_player players[8];
    size_t player_count;
    int32_t player_client;
    qa_team_arena_score_result team_score;
    qa_application_q3_campaign_result team_live;
} frontend_campaign_ui_view;
/* Borrows the actual published local CGAME recipient and its physical GAME.
 * Missing campaigns or recipients succeed with installed=false. All catalog
 * and progression borrows expire when their source owner changes. */
bool frontend_campaign_ui_read(qa_frontend *,uint32_t,frontend_campaign_ui_view *,qa_error *);
typedef enum frontend_campaign_ui_action {
    FRONTEND_CAMPAIGN_PLAY, FRONTEND_CAMPAIGN_RETRY, FRONTEND_CAMPAIGN_NEXT,
    FRONTEND_CAMPAIGN_RESET, FRONTEND_CAMPAIGN_QUIT
} frontend_campaign_ui_action;
/* Stage against the actual recipient. PLAY uses the authored arena number
 * and skill 1..5; execution follows source and UI callback unwind. */
bool frontend_campaign_ui_stage(qa_frontend *,uint32_t,frontend_campaign_ui_action,int32_t,int32_t,qa_error *);
bool frontend_campaign_ui_drain(qa_frontend *,qa_error *);
#endif

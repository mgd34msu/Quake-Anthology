/* Port of src/ui/settings/rankings.ts. The TS async actions are
 * synchronous here and return an error message (borrowed) or NULL,
 * mirroring the landed provider convention. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_RANKINGS_H
#define QA_UI_SETTINGS_RANKINGS_H

#include "qa/network/services/rankings.h"

/** TS RankingAccountActions. */
typedef struct qa_ui_ranking_account_actions {
  void *ctx;
  qa_ranking_service_state (*service)(void *ctx);
  qa_ranking_player_state (*player)(void *ctx);
  const char *(*submit)(void *ctx, qa_ranking_account_request request);
  const char *(*reset)(void *ctx);
  const char *(*spectate)(void *ctx);
} qa_ui_ranking_account_actions;

/** TS RankingAccountView: kind "disabled"|"unavailable"|"busy"|"account". */
typedef struct qa_ui_ranking_account_view {
  const char *kind;
  const char *message; /* unavailable */
  qa_ranking_player_state player; /* account */
} qa_ui_ranking_account_view;

qa_ui_ranking_account_view qa_ui_ranking_account_view_for(
    const qa_ui_ranking_account_actions *actions);

#endif

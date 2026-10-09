#ifndef QA_GAME_Q2_SOURCE_H
#define QA_GAME_Q2_SOURCE_H
#include "qa/game_q2_player.h"
#include "qa/console.h"

/* Copy current source values into the compiled GAME consumers. This does not
 * dispatch commands, apply latches, emit events, or replace host services.
 * Reset rotation only for an actual map-list current-value change; ordinary
 * refresh and import retain the source's shuffled order. Applying rules does
 * not replace the configured cvar service or bind a Source table. */
bool qa_q2_source_apply(qa_q2_game *, const qa_cvars *, bool reset_rotation, qa_error *);
bool qa_q2_source_player_rules(const qa_cvars *, qa_q2_player_rules *, qa_error *);
uint32_t qa_q2_source_deathmatch_flags(const qa_cvars *);
typedef enum qa_q2_source_setting {
    QA_Q2_SOURCE_GRAVITY,
    QA_Q2_SOURCE_SELECT_EMPTY,
    QA_Q2_SOURCE_FAST_SWITCH,
    QA_Q2_SOURCE_STRONG_MINES,
    QA_Q2_SOURCE_TEAMPLAY,
    QA_Q2_SOURCE_INSTAGIB,
    QA_Q2_SOURCE_CTF_FLAGS,
    QA_Q2_SOURCE_DISABLED_WEAPONS,
    QA_Q2_SOURCE_NO_QUADFIRE_DROP,
    QA_Q2_SOURCE_SETTING_COUNT
} qa_q2_source_setting;
/* The application adopts its actual Source view after successful rule apply.
 * Rebind when replacing that view; the GAME must not outlive it. */
void qa_q2_source_bind(qa_q2_game *, const qa_cvars *);
/* Read the bound Source table. Standalone GAME users retain their configured
 * source service, or the supplied default when that service is absent. */
bool qa_q2_source_value(qa_q2_game *, qa_q2_source_setting, float default_value,
                        float *, qa_error *);
#endif

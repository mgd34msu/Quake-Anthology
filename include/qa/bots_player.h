#ifndef QA_BOTS_PLAYER_H
#define QA_BOTS_PLAYER_H
#include "qa/network_q3.h"

/* Borrowed view of the actual 468-byte curPs inside the GAME-owned BotState.
 * The view retains no copy. Its lifetime is the current source callback. */
typedef struct qa_bot_player_state_view {
    const uint8_t *bytes;
    qa_q3_product product;
} qa_bot_player_state_view;
typedef enum qa_bot_player_state_array {
    QA_BOT_PS_DELTA_ANGLES=56,QA_BOT_PS_EVENTS=112,QA_BOT_PS_EVENT_PARAMETERS=120,
    QA_BOT_PS_STATS=184,QA_BOT_PS_PERSISTENT=248,QA_BOT_PS_POWERUPS=312,QA_BOT_PS_AMMO=376
} qa_bot_player_state_array;
bool qa_bot_player_state_slot(const qa_bot_player_state_view *,qa_bot_player_state_array,
    int32_t,int32_t *,qa_error *);
bool qa_bot_player_state_generic(const qa_bot_player_state_view *,int32_t *,qa_error *);
#endif

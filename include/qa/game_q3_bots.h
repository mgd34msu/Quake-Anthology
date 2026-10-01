#ifndef QA_GAME_Q3_BOTS_H
#define QA_GAME_Q3_BOTS_H
#include "qa/game_q3.h"
#include "qa/game_q3_supply.h"
/* Only the actual baseq3 selected-supply observation produces receipts. The
 * caller owns them and calls qa_supply_preview_free after scoring them. */
bool qa_q3_bot_supply_preview(qa_q3_game *,qa_actor_id pickup,qa_actor_id recipient,
                              const qa_q3_supply_services *,qa_supply_preview_result *,
                              bool *eligible,bool *found,qa_error *);
#endif

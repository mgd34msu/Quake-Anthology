#ifndef QA_GAME_Q3_SUPPLY_H
#define QA_GAME_Q3_SUPPLY_H
#include "qa/inventory.h"

typedef struct qa_q3_supply_descriptor {
    qa_actor_id pickup,recipient;
    const struct qa_q3_item *item;
    int32_t count,generic1,game_type;
    float weapon_respawn_seconds,team_weapon_respawn_seconds;
    bool dropped;
} qa_q3_supply_descriptor;
typedef enum qa_q3_supply_kind {
    QA_Q3_SUPPLY_NATIVE,QA_Q3_SUPPLY_REJECTED,QA_Q3_SUPPLY_SELECTED
} qa_q3_supply_kind;
typedef struct qa_q3_supply_services {
    void *context;
    /* Borrow the actual selected pickup admission. This callback is read-only
     * and transfers allocated receipts only for a selected offer. */
    bool (*preview)(void *,const qa_q3_supply_descriptor *,qa_q3_supply_kind *,
                     qa_supply_preview_result *,qa_error *);
} qa_q3_supply_services;
#endif

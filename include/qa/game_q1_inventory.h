#ifndef QA_GAME_Q1_INVENTORY_H
#define QA_GAME_Q1_INVENTORY_H
#include "qa/game_q1.h"
#include "qa/inventory.h"

/* Pure qualification of the player's actual published weapon definition lease. */
bool qa_q1_game_weapon_definitions_current(qa_q1_game *, qa_actor_id);

typedef struct qa_q1_weapon_ui_definition {
    qa_q1_weapon weapon;
    qa_item_id item, ammo;
    const char *label;
    size_t ordinal;
    double quantity;
} qa_q1_weapon_ui_definition;
/* Declared UI ammunition and quantity from this actual player's retained
 * definition lease. No shot/body observation or source callback runs. */
bool qa_q1_game_weapon_ui_definition_read(qa_q1_game *, qa_actor_id, qa_item_id,
    qa_q1_weapon_ui_definition *, bool *found, qa_error *);

/* Read the actual declared weapon's source availability without selecting it. */
bool qa_q1_game_weapon_item_read(qa_q1_game *, qa_actor_id, qa_item_id,
    qa_q1_weapon_view *, bool *found, qa_error *);

/* Source UI availability uses the registered weapon's best-selection rules. */
bool qa_q1_game_weapon_item_available(qa_q1_game *, qa_actor_id, qa_item_id,
    bool *available, bool *found, qa_error *);

/* Pure callback reconstruction from the actual restored player declaration
 * and its captured definition lease; no admission or weapon selection. */
bool qa_q1_game_inventory_group(qa_q1_game *, qa_actor_id, uint64_t serial,
    const qa_inventory_source_group *, qa_inventory_items *, qa_error *);
#endif

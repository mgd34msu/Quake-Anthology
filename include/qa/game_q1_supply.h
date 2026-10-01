#ifndef QA_GAME_Q1_SUPPLY_H
#define QA_GAME_Q1_SUPPLY_H
#include "qa/game_q1.h"

/* Source controls remain readable when another arsenal owns the player. */
bool qa_q1_player_auto_switch_read(const qa_q1_game *, qa_actor_id,
    qa_q1_auto_switch *, qa_error *);
bool qa_q1_source_arsenal_spawn_read(qa_q1_game *, qa_actor_id,
    qa_q1_weapon *, float *max_health, qa_q1_auto_switch *, qa_error *);
/* Reset the selected arsenal's source weapon continuation after a genuine
 * player spawn. Inventory and character state keep their actual owners. */
bool qa_q1_selected_arsenal_spawn(qa_q1_game *, qa_actor_id, qa_q1_weapon,
    float max_health, const qa_q1_auto_switch *source_preference, qa_error *);
/* Receipts refer to the selected arsenal's actual canonical inventory. An
 * optional source preference replaces the selected player's preference. */
bool qa_q1_selected_pickup_ammo(qa_q1_game *, qa_actor_id,
    const qa_pickup_receipt *, size_t, bool auto_switch,
    const qa_q1_auto_switch *source_preference, qa_error *);
bool qa_q1_selected_pickup_weapons(qa_q1_game *, qa_actor_id,
    const qa_item_id *, size_t, qa_pickup_selection_mode,
    const qa_q1_auto_switch *source_preference, qa_error *);
#endif

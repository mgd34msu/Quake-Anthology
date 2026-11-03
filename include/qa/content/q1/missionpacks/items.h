/* Port of src/content/q1/missionpacks/items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_ITEMS_H
#define QA_CONTENT_Q1_MISSIONPACKS_ITEMS_H
#include "qa/content/q1/missionpacks/types.h"

/* TS MissionItemServices (a host interface: fn + ctx). */
typedef struct qa_q1mp_mission_item_services {
  qa_q1mp_pack pack;
  void *ctx;
  void (*horn)(void *ctx, qa_q1_actor *item, qa_q1_player_state *player);
  bool (*sphere)(void *ctx, qa_q1_actor *item, qa_q1_player_state *player);
  void (*powerup)(void *ctx, qa_q1_player_state *player, qa_q1_powerup powerup, double seconds);
  void (*enable_combos)(void *ctx, qa_q1_player_state *player);
} qa_q1mp_mission_item_services;
double qa_q1mp_hipnotic_weapon_rank(qa_q1_weapon weapon);
/* services is borrowed for the life of the game (the item touch uses it). */
void qa_q1mp_register_mission_pack_items(qa_q1_services *game, const qa_q1mp_mission_item_services *services);

#endif

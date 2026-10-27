/* Port of src/content/q1/missionpacks/items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_ITEMS_H
#define QA_CONTENT_Q1_MISSIONPACKS_ITEMS_H
#include "qa/content/q1/missionpacks/types.h"

/* TS MissionItemServices (fn+ctx). */
typedef struct qa_q1mp_mission_item_services {
  qa_q1mp_pack pack; void *ctx;
  void (*horn)(void *ctx, qa_q1mp_actor *item, qa_q1mp_player_state *player);
  bool (*sphere)(void *ctx, qa_q1mp_actor *item, qa_q1mp_player_state *player);
  void (*powerup)(void *ctx, qa_q1mp_player_state *player, const char *powerup, double seconds);
  void (*enable_combos)(void *ctx, qa_q1mp_player_state *player);
} qa_q1mp_mission_item_services;
double qa_q1mp_hipnotic_weapon_rank(const char *weapon);
void qa_q1mp_register_mission_pack_items(qa_q1mp_game *game, const qa_q1mp_mission_item_services *services);

#endif

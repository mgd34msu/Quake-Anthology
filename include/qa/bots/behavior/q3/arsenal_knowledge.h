/* Port of src/bots/behavior/q3/arsenal-knowledge.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_ARSENAL_KNOWLEDGE_H
#define QA_BOTS_BEHAVIOR_Q3_ARSENAL_KNOWLEDGE_H
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/game_host.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_weapon_candidate_bots_a {
  int64_t weapon_number;
  bool valid, melee;
  bool has_max_range;
  double max_range;
  bool has_role;
  int64_t role;
  double projectile_gravity, speed;
  int64_t damage_type;
  int64_t weapon_inventory_index, ammo_inventory_index, ammo_amount;
  double damage, projectile_count, reload;
} qa_bot_weapon_candidate_bots_a;
typedef struct qa_bot_arsenal_knowledge_bots_a {
  qa_bot_weapon_candidate_bots_a *candidates;
  size_t candidate_count;
  void *ctx;
  double (*evaluate_fight_weapon)(void *ctx, int64_t handle, int64_t role, const double *inventory,
                                  size_t n);
  bool missionpack;
} qa_bot_arsenal_knowledge_bots_a;
int64_t qa_bot_arsenal_personality_role(const qa_bot_weapon_candidate_bots_a *c);
bool qa_bot_arsenal_owned(const qa_bot_state_bots_a *s, const qa_bot_weapon_candidate_bots_a *c);
double qa_bot_arsenal_ammunition(const qa_bot_state_bots_a *s,
                                 const qa_bot_weapon_candidate_bots_a *c);
int64_t qa_bot_arsenal_choose_weapon(qa_bot_arsenal_knowledge_bots_a *k, qa_bot_state_bots_a *state,
                                     int64_t handle);
int64_t qa_bot_arsenal_activation_weapon(qa_bot_arsenal_knowledge_bots_a *k,
                                         qa_bot_state_bots_a *state);
double qa_bot_arsenal_aggression(qa_bot_arsenal_knowledge_bots_a *k, qa_bot_state_bots_a *state,
                                 int64_t current_weapon);
qa_bot_weapon_tactics_bots_a qa_bot_arsenal_tactics(qa_bot_arsenal_knowledge_bots_a *k,
                                                    int64_t weapon);
#endif

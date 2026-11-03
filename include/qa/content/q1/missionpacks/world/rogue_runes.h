/* Port of src/content/q1/missionpacks/world/rogue-runes.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_RUNES_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_RUNES_H
#include <stdint.h>

#include "qa/content/q1/missionpacks/types.h"

/* TS RogueRunes. players is the TS Map<OwnedActor, RuneState> in insertion
 * order. Must not move after init (the game borrows the registered
 * extension, effects and rules, and the callbacks receive the struct). */
typedef struct qa_q1mp_rune_state { qa_owned_actor actor; double rune, notice, earth_noise, black_noise, hell_noise, regeneration; } qa_q1mp_rune_state;
typedef struct qa_q1mp_rogue_runes {
  qa_q1_services *game;
  void *gamecfg_ctx; double (*gamecfg)(void *ctx); /* NULL = TS () => 0 */
  qa_q1mp_rune_state *players; size_t player_count, player_capacity;
  qa_q1_state_extension state_extension;
  qa_q1_weapon_rules weapon_rules;
  int64_t release_subscription;
} qa_q1mp_rogue_runes;
void qa_q1mp_rogue_runes_init(qa_q1mp_rogue_runes *runes, qa_q1_services *game, double (*gamecfg)(void *ctx), void *gamecfg_ctx);
void qa_q1mp_rogue_runes_destroy(qa_q1mp_rogue_runes *runes);
void qa_q1mp_runes_frame(qa_q1mp_rogue_runes *runes, qa_actor_id actor);
void qa_q1mp_runes_drop(qa_q1mp_rogue_runes *runes, qa_actor_id actor);
double qa_q1mp_runes_damage(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount);
double qa_q1mp_runes_resistance(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount);
void qa_q1mp_runes_attack_sound(qa_q1mp_rogue_runes *runes, qa_actor_id actor);
double qa_q1mp_runes_attack_delay(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double delay);
bool qa_q1mp_runes_has_regeneration(qa_q1mp_rogue_runes *runes, qa_actor_id actor);

#endif

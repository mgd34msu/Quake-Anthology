/*
 * Port of src/content/composition/q1/commands.ts: weapons.qc
 * W_ChangeWeapon, CycleWeaponCommand (base and ThreeWave CTF) and base
 * ImpulseCommands, on the one Q1 game (qa_q1_services).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_COMMANDS_H
#define QA_CONTENT_COMPOSITION_Q1_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/entity_services.h"

/* The Q1SourceComposition fields baseQ1Impulse destructures:
 * game, services, selection.program and base.campaign. */
typedef struct qa_comp_q1_impulse_source {
  qa_q1_services *game;
  const qa_comp_q1_services *services;
  qa_comp_q1_program program;
  qa_q1_campaign_binding campaign;
} qa_comp_q1_impulse_source;

/** TS q1WeaponImpulse(game, player, impulse). */
bool qa_comp_q1_weapon_impulse(qa_q1_services *game, qa_q1_player_state *player, double impulse);
/** TS q1CtfWeaponCycle: the hook sits between the axe and the shotgun.
 * select(item, weapon) is the TS closure; weapon NULL = TS null. */
typedef bool (*qa_comp_q1_cycle_select_fn)(void *ctx, qa_item_id item, qa_q1_base_weapon weapon);
bool qa_comp_q1_ctf_weapon_cycle(qa_q1_services *game, qa_q1_player_state *player, qa_item_id selected,
                                 bool reverse, qa_item_id grapple, qa_comp_q1_cycle_select_fn select,
                                 void *select_ctx);
/** TS baseQ1Impulse(source, player, impulse). */
bool qa_comp_base_q1_impulse(const qa_comp_q1_impulse_source *source, qa_q1_player_state *player,
                             double impulse);

#endif

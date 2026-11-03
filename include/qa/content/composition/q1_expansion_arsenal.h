/*
 * Port of src/content/composition/q1-expansion-arsenal.ts on the one Q1
 * game: registers a selected expansion's weapons (hipnotic weapons, the
 * rogue mission-pack arsenal, or MG3 items on a fresh Q1Base +
 * Q1AddonContext) and answers impulse/frame/preparePickup. The TS
 * result object's closures become the qa_comp_selected_q1_mission_weapons_*
 * functions over the state struct. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_EXPANSION_ARSENAL_H
#define QA_CONTENT_COMPOSITION_Q1_EXPANSION_ARSENAL_H

#include <stdbool.h>

#include "qa/content/composition/q1/commands.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/arsenal.h"

/* TS SelectedQ1Program, in declaration order. */
typedef enum qa_comp_selected_q1_program {
  QA_COMP_SELECTED_Q1_ID1, QA_COMP_SELECTED_Q1_HIPNOTIC, QA_COMP_SELECTED_Q1_ROGUE,
  QA_COMP_SELECTED_Q1_DOPA, QA_COMP_SELECTED_Q1_MG1, QA_COMP_SELECTED_Q1_MG3
} qa_comp_selected_q1_program;
const char *qa_comp_selected_q1_program_name(qa_comp_selected_q1_program program);

/* Owns what registration created (TS closures keep them alive): the rogue
 * arsenal, or the MG3 base and addon context. Must not move after
 * registration (the game keeps pointers into it). */
typedef struct qa_comp_selected_q1_mission_weapons {
  qa_q1_services *game;
  qa_comp_selected_q1_program program;
  qa_q1ad_services services; /* TS Q1AddonServices */
  qa_q1mp_mission_pack_arsenal arsenal; /* rogue */
  qa_q1_base *base;                     /* mg3 */
  qa_q1ad_context *context;             /* mg3 */
} qa_comp_selected_q1_mission_weapons;

/** TS registerSelectedQ1MissionWeapons(game, program, services). */
void qa_comp_register_selected_q1_mission_weapons(qa_comp_selected_q1_mission_weapons *out,
                                                  qa_q1_services *game,
                                                  qa_comp_selected_q1_program program,
                                                  qa_q1ad_services services);
void qa_comp_selected_q1_mission_weapons_destroy(qa_comp_selected_q1_mission_weapons *weapons);
bool qa_comp_selected_q1_mission_weapons_impulse(qa_comp_selected_q1_mission_weapons *weapons,
                                                 qa_q1_player_state *player, double value);
void qa_comp_selected_q1_mission_weapons_frame(qa_comp_selected_q1_mission_weapons *weapons,
                                               double elapsed_seconds);
void qa_comp_selected_q1_mission_weapons_prepare_pickup(qa_comp_selected_q1_mission_weapons *weapons,
                                                        qa_q1_player_state *player);

#endif

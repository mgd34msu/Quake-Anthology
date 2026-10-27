/*
 * Port of src/content/composition/q1-expansion-arsenal.ts.
 * Mirrored surfaces from unported modules (qa_comp_* hooks; TS owners:
 * src/content/q1/missionpacks/arsenal.ts + selection.ts,
 * src/content/q1/addons/context.ts + items/, src/content/q1/base/
 * provider.ts). The TS result object's closures become the three
 * qa_comp_selected_q1_mission_weapons_* functions over the state struct.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_EXPANSION_ARSENAL_H
#define QA_CONTENT_COMPOSITION_Q1_EXPANSION_ARSENAL_H

#include <stdbool.h>

#include "qa/content/composition/q1/commands.h"
#include "qa/content/composition/q1/types.h"

/* TS SelectedQ1Program, in declaration order. */
typedef enum qa_comp_selected_q1_program {
  QA_COMP_SELECTED_Q1_ID1, QA_COMP_SELECTED_Q1_HIPNOTIC, QA_COMP_SELECTED_Q1_ROGUE,
  QA_COMP_SELECTED_Q1_DOPA, QA_COMP_SELECTED_Q1_MG1, QA_COMP_SELECTED_Q1_MG3
} qa_comp_selected_q1_program;
const char *qa_comp_selected_q1_program_name(qa_comp_selected_q1_program program);

/* Q1AddonServices surface observed here: developer-message emits (the
 * opaque pointer is forwarded to the addon-context constructor). */
typedef struct qa_comp_q1_addon_services {
  void *ctx;
  void (*emit_developer_message)(void *ctx, const char *text);
} qa_comp_q1_addon_services;

typedef struct qa_comp_q1_arsenal_hooks {
  void *ctx;
  void (*register_hipnotic_weapons)(void *ctx, qa_comp_q1_game *game);
  bool (*mission_weapon_impulse)(void *ctx, qa_comp_q1_game *game, qa_comp_q1_player_state *player,
                                 const char *pack, double value);
  void *(*register_mission_pack_arsenal)(void *ctx, qa_comp_q1_game *game, const char *program);
  bool (*arsenal_impulse)(void *ctx, void *arsenal, qa_actor_id actor, double value);
  void (*arsenal_enable_combos)(void *ctx, void *arsenal, qa_comp_q1_player_state *player);
  /* new Q1Base(game, { officialCampaign }) */
  void *(*create_base_provider)(void *ctx, qa_comp_q1_game *game, bool official_campaign);
  void *(*new_addon_context)(void *ctx, void *base, const char *program,
                             const qa_comp_q1_addon_services *services);
  void (*register_mg3_items)(void *ctx, void *context);
  bool (*mg3_item_impulse)(void *ctx, void *context, qa_actor_id actor, double value,
                           void (*emit_text)(void *text_ctx, const char *text), void *text_ctx);
  void (*context_frame)(void *ctx, void *context, double elapsed_seconds);
} qa_comp_q1_arsenal_hooks;

typedef struct qa_comp_selected_q1_mission_weapons {
  qa_comp_q1_game *game;
  qa_comp_selected_q1_program program;
  const qa_comp_q1_addon_services *services;
  const qa_comp_q1_arsenal_hooks *hooks;
  void *arsenal; /* rogue only */
  void *context; /* mg3 only */
} qa_comp_selected_q1_mission_weapons;

/** TS registerSelectedQ1MissionWeapons(). */
void qa_comp_register_selected_q1_mission_weapons(qa_comp_selected_q1_mission_weapons *out,
                                                  qa_comp_q1_game *game,
                                                  qa_comp_selected_q1_program program,
                                                  const qa_comp_q1_addon_services *services,
                                                  const qa_comp_q1_arsenal_hooks *hooks);
bool qa_comp_selected_q1_mission_weapons_impulse(qa_comp_selected_q1_mission_weapons *weapons,
                                                 qa_comp_q1_player_state *player, double value);
void qa_comp_selected_q1_mission_weapons_frame(qa_comp_selected_q1_mission_weapons *weapons,
                                               double elapsed_seconds);
void qa_comp_selected_q1_mission_weapons_prepare_pickup(qa_comp_selected_q1_mission_weapons *weapons,
                                                        qa_comp_q1_player_state *player);

#endif

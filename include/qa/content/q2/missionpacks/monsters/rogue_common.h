/* Port of src/content/q2/missionpacks/monsters/rogue-common.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_COMMON_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_COMMON_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/contracts/scene.h"

bool qa_q2mpm_source_trace_world(qa_q2_game_services *game, qa_trace_result trace);
double qa_q2mpm_monster_mass(qa_q2m_context *context);
/* False is TS null. */
bool qa_q2mpm_rogue_parasite_drain_trace(qa_q2m_context *context, qa_trace_result *out);
bool qa_q2mpm_rogue_blocked_check_shot(qa_q2m_context *context, double chance, qa_q2mpm_monster_state *source);
void qa_q2mpm_rogue_duck_down(qa_q2m_context *context);
void qa_q2mpm_rogue_duck_hold(qa_q2m_context *context);
void qa_q2mpm_rogue_duck_up(qa_q2m_context *context);
void qa_q2mpm_rogue_duck_down_handler(void *ctx, qa_q2m_context *context);
void qa_q2mpm_rogue_duck_hold_handler(void *ctx, qa_q2m_context *context);
void qa_q2mpm_rogue_duck_up_handler(void *ctx, qa_q2m_context *context);

typedef struct qa_q2mpm_duck_fn {
  void *ctx;
  void (*fn)(void *ctx, qa_q2m_context *context, double eta); /* NULL fn = TS null */
} qa_q2mpm_duck_fn;
/* trace NULL mirrors TS null; duck/sidestep NULL fn mirrors TS null. */
void qa_q2mpm_rogue_monster_dodge(qa_q2m_context *context, qa_q2m_monsters *monsters, qa_actor_id attacker,
                                  double eta, const qa_trace_result *trace, qa_q2mpm_duck_fn duck,
                                  qa_q2m_handler sidestep);

#endif

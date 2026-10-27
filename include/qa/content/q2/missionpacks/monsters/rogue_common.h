/* Port of src/content/q2/missionpacks/monsters/rogue-common.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_COMMON_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_COMMON_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/contracts/scene.h"

bool qa_q2mpm_source_trace_world(qa_q2f_game *game, qa_trace_result trace);
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

/* Mirrored unported surface (TS owner: src/content/q2/rerelease/
 * monsters/common.ts). Implemented faithfully in rogue_common.c;
 * replace with the rerelease slice when it lands. */
void qa_q2mpm_monster_flash(qa_q2m_context *context, double flash, qa_vec3 origin, qa_vec3 direction);
typedef struct qa_q2mpm_predicted_aim { qa_vec3 direction, point; } qa_q2mpm_predicted_aim;
bool qa_q2mpm_predict_aim(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                          qa_q2mpm_predicted_aim *out);
bool qa_q2mpm_predicted_direction(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                                  qa_vec3 *out);
bool qa_q2mpm_blocked_check_platform(qa_q2m_context *context, double distance);
bool qa_q2mpm_monster_jump_finished(qa_q2m_context *context);
typedef enum qa_q2mpm_jump_result {
  QA_Q2MPM_JUMP_NONE, QA_Q2MPM_JUMP_UP, QA_Q2MPM_JUMP_DOWN, QA_Q2MPM_JUMP_TURN
} qa_q2mpm_jump_result;
typedef struct qa_q2mpm_jump_navigation {
  bool is_path;
  bool traversal_pending;
  qa_vec3 first, second;
} qa_q2mpm_jump_navigation;
qa_q2mpm_jump_result qa_q2mpm_blocked_check_jump(qa_q2m_context *context, double distance, double drop_height,
                                                double jump_height, bool can_jump, qa_q2mpm_jump_navigation navigation);
qa_q2mpm_jump_result qa_q2mpm_blocked_check_jump_default(qa_q2m_context *context, double distance,
                                                         double drop_height, double jump_height);

#endif

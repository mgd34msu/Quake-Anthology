/* Port of src/content/q2/rerelease/monsters/common.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_COMMON_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_COMMON_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/core/random/q2_rerelease.h"
#include "qa/world/gameplay/authority.h"

/* Mirrored unported surface (TS owner: src/content/q2/foundation/
 * host.ts Q2FoundationHost.gravity(); the qa_q2f host mirror has no
 * gravity member). The host wires the hook with
 * qa_q2rm_set_gravity_hook; use without it aborts. */
typedef struct qa_q2rm_gravity_hook {
  void *ctx;
  double (*gravity)(void *ctx);
} qa_q2rm_gravity_hook;
void qa_q2rm_set_gravity_hook(qa_q2rm_gravity_hook hook);
double qa_q2rm_host_gravity(qa_q2f_game *game);

/* Mirrored unported surface (TS owners: src/world/gameplay/
 * authority.ts GameplayAuthority.bindPowerArmorCells/
 * setPoweredProtection and src/world/gameplay/inventory.ts
 * SharedInventoryTable.has/create; the qa_q2f combat/inventory
 * mirrors lack these members). The host wires hooks with
 * qa_q2rm_set_power_hooks; use without them aborts. kind is
 * "none" | "screen" | "shield". */
typedef struct qa_q2rm_power_hooks {
  void *ctx;
  void (*bind_cells)(void *ctx, qa_owned_actor actor, qa_power_armor_cell_binding cells);
  void (*set_powered)(void *ctx, qa_owned_actor actor, const char *kind, double cells);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_create_empty)(void *ctx, qa_owned_actor actor);
} qa_q2rm_power_hooks;
void qa_q2rm_set_power_hooks(qa_q2rm_power_hooks hooks);
/* TS setPoweredProtection(actor, {kind, cells}) via the mirror. */
void qa_q2rm_set_powered(qa_q2f_game *game, qa_owned_actor actor, const char *kind, double cells);

/* TS rereleaseRandom: aborts (TS Error) without the session stream. */
qa_q2_rerelease_random *qa_q2rm_rerelease_random(qa_q2m_context *context);
/* False is TS null. destroy_on_touch defaults false in the _default form. */
bool qa_q2rm_calculate_pitch_to_fire(qa_q2m_context *context, qa_vec3 target, qa_vec3 start, qa_vec3 aim,
                                     double speed, double seconds, bool mortar, bool destroy_on_touch,
                                     qa_vec3 *out);
bool qa_q2rm_calculate_pitch_to_fire_default(qa_q2m_context *context, qa_vec3 target, qa_vec3 start, qa_vec3 aim,
                                             double speed, double seconds, bool mortar, qa_vec3 *out);
bool qa_q2rm_chainfist(qa_q2m_context *context);
bool qa_q2rm_reacts_to_pain(qa_q2m_context *context);
bool qa_q2rm_check_gib(qa_q2m_context *context);
void qa_q2rm_monster_flash(qa_q2m_context *context, double flash, qa_vec3 origin, qa_vec3 direction);
/* False is TS null. offset defaults 0 in the _default forms. */
bool qa_q2rm_predicted_direction(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                                 qa_vec3 *out);
bool qa_q2rm_predicted_direction_default(qa_q2m_context *context, qa_vec3 start, double speed, bool eye,
                                         qa_vec3 *out);
typedef struct qa_q2rm_predicted_aim { qa_vec3 direction, point; } qa_q2rm_predicted_aim;
bool qa_q2rm_predict_aim(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                         qa_q2rm_predicted_aim *out);
bool qa_q2rm_predict_aim_default(qa_q2m_context *context, qa_vec3 start, double speed, bool eye,
                                 qa_q2rm_predicted_aim *out);
bool qa_q2rm_blocked_check_platform(qa_q2m_context *context, double distance);
/* qa_q2m_definition blocked-slot form of blockedCheckPlatform. */
bool qa_q2rm_blocked_check_platform_handler(void *ctx, qa_q2m_context *context, double distance);
bool qa_q2rm_monster_jump_finished(qa_q2m_context *context);

/* TS JumpNavigation / JumpResult. */
typedef struct qa_q2rm_jump_navigation {
  bool is_path;
  bool traversal_pending;
  qa_vec3 first, second;
} qa_q2rm_jump_navigation;
typedef enum qa_q2rm_jump_result {
  QA_Q2RM_JUMP_NONE, QA_Q2RM_JUMP_UP, QA_Q2RM_JUMP_DOWN, QA_Q2RM_JUMP_TURN
} qa_q2rm_jump_result;
const char *qa_q2rm_jump_result_name(qa_q2rm_jump_result result);
qa_q2rm_jump_result qa_q2rm_blocked_check_jump(qa_q2m_context *context, double distance, double drop_height,
                                               double jump_height, bool can_jump,
                                               qa_q2rm_jump_navigation navigation);
/* Defaults: can_jump true, navigation {kind:"none"}. */
qa_q2rm_jump_result qa_q2rm_blocked_check_jump_default(qa_q2m_context *context, double distance,
                                                       double drop_height, double jump_height);

#endif

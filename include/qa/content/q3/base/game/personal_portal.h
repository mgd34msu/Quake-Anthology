/* Port of src/content/q3/base/game/personal-portal.ts. GPL-2.0-or-later.
 * Items are ../shared/items.ts; save state reduces to the
 * portal sequence value. mapTravel variant: has_map_travel selects
 * host drop/teleport instead of the native flag drop + teleportPlayer. */
#ifndef QA_CONTENT_Q3_BASE_GAME_PERSONAL_PORTAL_H
#define QA_CONTENT_Q3_BASE_GAME_PERSONAL_PORTAL_H
#include <stdbool.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/item_motion.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_personal_portal_host {
  void *ctx; qa_q3_game_combat_context *combat;
  qa_q3_base_server_world world; qa_q3_kill_box_host kill_box;
  double (*model_index)(void *ctx, const char *name);
  double (*random_value)(void *ctx);
  bool has_map_travel;
  void (*map_drop_carried_flag)(void *ctx, qa_q3_game_entity *player);
  void (*map_teleport)(void *ctx, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles);
  /* TS host.items (absent with mapTravel). */
  qa_q3_entity_callback touch_item, dropped_flag_think, check_dropped_team_item;
  bool spatial_is_world;
} qa_q3_personal_portal_host;
typedef struct qa_q3_personal_portal_runtime {
  qa_q3_personal_portal_host host; double portal_sequence;
} qa_q3_personal_portal_runtime;
void qa_q3_personal_portal_runtime_init(qa_q3_personal_portal_runtime *runtime, qa_q3_personal_portal_host host);
void qa_q3_personal_portal_bind_save_callbacks(qa_q3_personal_portal_runtime *runtime);
double qa_q3_personal_portal_capture_sequence(const qa_q3_personal_portal_runtime *runtime);
void qa_q3_personal_portal_restore_sequence(qa_q3_personal_portal_runtime *runtime, double sequence);
void qa_q3_personal_portal_drop_destination(qa_q3_personal_portal_runtime *runtime, qa_q3_game_entity *player);
void qa_q3_personal_portal_drop_source(qa_q3_personal_portal_runtime *runtime, qa_q3_game_entity *player);
#endif

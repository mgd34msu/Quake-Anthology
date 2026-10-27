/* Port of src/bots/behavior/rerelease/q2-exports.ts. Host is fn+ctx;
 * view getters return false for TS null. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_Q2_EXPORTS_H
#define QA_BOTS_BEHAVIOR_RERELEASE_Q2_EXPORTS_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/math.h"
typedef struct qa_q2_bot_client_view { bool inuse, bot, has_client; int64_t current_weapon, pending_weapon, selected_item; qa_bot_vec3 origin, view_offset, command_angles; } qa_q2_bot_client_view;
typedef struct qa_q2_bot_item_view { int64_t id; const char *classname; bool weapon; const char *use_callback; } qa_q2_bot_item_view;
typedef struct qa_q2_bot_entity_view { bool inuse; const char *use_callback, *touch_callback; } qa_q2_bot_entity_view;
typedef struct qa_q2_bot_exports_host {
  void *ctx; int64_t item_count;
  bool (*get_bot)(void *ctx, int64_t entity, qa_q2_bot_client_view *out);
  bool (*get_entity)(void *ctx, int64_t entity, qa_q2_bot_entity_view *out);
  bool (*get_item)(void *ctx, int64_t item, qa_q2_bot_item_view *out);
  int64_t (*inventory)(void *ctx, int64_t entity, int64_t item);
  void (*set_selected_item)(void *ctx, int64_t entity, int64_t item);
  void (*validate_selected_item)(void *ctx, int64_t entity);
  void (*disable_weapon_chains)(void *ctx, int64_t entity);
  void (*use_item)(void *ctx, const char *callback, int64_t entity, int64_t item);
  void (*change_weapon_instantly)(void *ctx, int64_t entity);
  void (*trigger_use)(void *ctx, const char *callback, int64_t target, int64_t other, int64_t activator);
  void (*trigger_touch)(void *ctx, const char *callback, int64_t target, int64_t other, bool other_touching_self);
  void (*force_look)(void *ctx, int64_t entity, qa_bot_vec3 delta_angles);
  bool (*picked_up_by)(void *ctx, int64_t item_entity, int64_t source_client);
} qa_q2_bot_exports_host;
void qa_q2_bot_set_weapon(qa_q2_bot_exports_host *host, int64_t entity, int64_t weapon, bool instant_switch);
void qa_q2_bot_trigger_edict(qa_q2_bot_exports_host *host, int64_t entity, int64_t target);
void qa_q2_bot_use_item(qa_q2_bot_exports_host *host, int64_t entity, int64_t item_id);
int64_t qa_q2_bot_get_item_id(qa_q2_bot_exports_host *host, const char *classname);
void qa_q2_edict_force_look_at_point(qa_q2_bot_exports_host *host, int64_t entity, qa_bot_vec3 point);
bool qa_q2_bot_picked_up_item(qa_q2_bot_exports_host *host, int64_t entity, int64_t item_entity);
#endif

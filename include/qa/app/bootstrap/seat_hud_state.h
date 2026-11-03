/* Port of src/app/bootstrap/seat-hud-state.ts. EventRing/BoundedQueue
 * become bounded deques with the same capacities; pending events are owned
 * copies. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SEAT_HUD_STATE_H
#define QA_APP_BOOTSTRAP_SEAT_HUD_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/app/bootstrap/weapon_hud.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/ui/hud/index.h"
typedef struct qa_app_objective_print { const char *text; double seconds; } qa_app_objective_print;
typedef struct qa_app_hud_pickup {
  const char *name; const char *path; qa_content_id content;
  qa_resource_id icon; double expires_milliseconds;
} qa_app_hud_pickup;
typedef struct qa_app_hud_presentation {
  const qa_ui_hud_health_bar *health_bars; size_t health_bar_count;
  const qa_ui_hud_help *help; /* NULL = TS null, storage owned by hud */
  const qa_ui_hud_prompt *prompts; size_t prompt_count;
  bool inventory_is_null;
  const qa_ui_hud_inventory_item *inventory; size_t inventory_count;
  const qa_app_hud_pickup *pickup; /* NULL = TS null */
  const qa_ui_hud_damage_indicator *damage_indicators; size_t damage_indicator_count;
  bool has_help_path; qa_vec3 help_path_origin, help_path_direction;
} qa_app_hud_presentation;
typedef struct qa_app_seat_source_hud qa_app_seat_source_hud;
/* localize returns malloc'd text; has_localize false mirrors undefined. */
qa_app_seat_source_hud *qa_app_seat_source_hud_create(qa_actor_id actor,
    char *(*localize)(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t arg_count), void *localize_ctx);
void qa_app_seat_source_hud_destroy(qa_app_seat_source_hud *h);
/* Borrowed array valid until next drain. */
const qa_app_objective_print *qa_app_seat_source_hud_drain_objective_prints(qa_app_seat_source_hud *h, size_t *out_count);
bool qa_app_seat_source_hud_inventory_items(qa_app_seat_source_hud *h, const qa_ui_hud_inventory_item **out, size_t *out_count);
void qa_app_seat_source_hud_scores(qa_app_seat_source_hud *h, bool down);
void qa_app_seat_source_hud_prepare(qa_app_seat_source_hud *h, qa_app_weapon_hud_assets *icons);
const qa_ui_hud_point_of_interest *qa_app_seat_source_hud_points(qa_app_seat_source_hud *h, size_t *out_count);
void qa_app_seat_source_hud_receive(qa_app_seat_source_hud *h, const qa_app_sim_presentation_event *source);
qa_app_hud_presentation qa_app_seat_source_hud_presentation(qa_app_seat_source_hud *h, double now_milliseconds);
#endif

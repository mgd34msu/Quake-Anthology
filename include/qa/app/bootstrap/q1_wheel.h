/* Port of src/app/bootstrap/q1-wheel.ts. PlayerUi / ApplicationAssets
 * (other bootstrap slices) are carried as the minimal views below.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_WHEEL_H
#define QA_APP_BOOTSTRAP_Q1_WHEEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/ui/hud/q1_wheel.h"
/* TS q1WheelSlotItem: borrowed static item id or NULL. */
qa_item_id qa_app_q1_wheel_slot_item(const qa_ui_q1_wheel_slot *slot, const char *campaign);
/* Minimal PlayerUi view (simulation/types.ts). */
typedef struct qa_app_q1_wheel_player_item {
  qa_item_id id;
  bool owned, has_ammo;
  double count;
  const char *label; /* borrowed, may be NULL */
} qa_app_q1_wheel_player_item;
typedef struct qa_app_q1_wheel_player {
  bool has_weapon_source;
  qa_content_id weapon_source_content;
  const qa_app_q1_wheel_player_item *items; size_t item_count;
  qa_item_id active_weapon; /* borrowed, may be NULL */
} qa_app_q1_wheel_player;
/* Minimal ApplicationAssets + ApplicationWeaponHudAssets host. */
typedef struct qa_app_q1_wheel_assets {
  void *ctx;
  /* catalog expectation for content; false when not q1 rerelease. */
  bool (*q1_rerelease_campaign)(void *ctx, qa_content_id content, const char **campaign_out);
  /* mounts.open: malloc'd bytes (caller frees) or NULL when absent. */
  uint8_t *(*open)(void *ctx, qa_content_id content, const char *path, size_t *out_length);
  /* icons.load(image resource): borrowed resource id. */
  qa_resource_id (*load_icon)(void *ctx, qa_content_id content, const char *path);
} qa_app_q1_wheel_assets;
typedef struct qa_app_application_q1_wheel qa_app_application_q1_wheel;
qa_app_application_q1_wheel *qa_app_application_q1_wheel_create(void);
void qa_app_application_q1_wheel_destroy(qa_app_application_q1_wheel *wheel);
/* Aborts (TS Error) on an invalid authored wheel. */
void qa_app_application_q1_wheel_prepare(qa_app_application_q1_wheel *wheel,
    const qa_app_q1_wheel_assets *assets, qa_app_q1_wheel_player player);
/* Borrowed item id or NULL. */
qa_item_id qa_app_application_q1_wheel_switch_weapon(qa_app_application_q1_wheel *wheel,
    qa_app_q1_wheel_player player, double first, double second);
/* Owned result; NULL count 0 is the TS null. Free with the destroy fn. */
typedef struct qa_app_q1_wheel_items_result {
  qa_ui_wheel_item *items; size_t count;
  qa_ui_q1_wheel_items owned; /* backing store */
  const char **labels;         /* backing label scratch */
} qa_app_q1_wheel_items_result;
bool qa_app_application_q1_wheel_items(qa_app_application_q1_wheel *wheel,
    qa_app_q1_wheel_player player, qa_app_q1_wheel_items_result *out);
void qa_app_q1_wheel_items_result_destroy(qa_app_q1_wheel_items_result *result);
#endif

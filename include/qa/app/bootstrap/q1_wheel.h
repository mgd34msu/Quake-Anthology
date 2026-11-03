/* Port of src/app/bootstrap/q1-wheel.ts: the Q1 rerelease authored weapon
 * wheel over ApplicationAssets, ApplicationWeaponHudAssets and PlayerUi.
 * Async prepare -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_WHEEL_H
#define QA_APP_BOOTSTRAP_Q1_WHEEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/app/bootstrap/weapon_hud.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/ui/hud/q1_wheel.h"
/* TS q1WheelSlotItem: borrowed static item id or NULL. */
qa_item_id qa_app_q1_wheel_slot_item(const qa_ui_q1_wheel_slot *slot, const char *campaign);
typedef struct qa_app_application_q1_wheel qa_app_application_q1_wheel;
qa_app_application_q1_wheel *qa_app_application_q1_wheel_create(void);
void qa_app_application_q1_wheel_destroy(qa_app_application_q1_wheel *wheel);
/* TS prepare(assets, icons, player). An invalid authored wheel or an unavailable
 * icon is reported once through assets.warnOnce, as TS. */
void qa_app_application_q1_wheel_prepare(qa_app_application_q1_wheel *wheel, qa_app_application_assets *assets,
    qa_app_weapon_hud_assets *icons, const qa_app_sim_player_ui *player);
/* TS switchWeapon: borrowed item id (the player's) or NULL. */
qa_item_id qa_app_application_q1_wheel_switch_weapon(qa_app_application_q1_wheel *wheel,
    const qa_app_sim_player_ui *player, double first, double second);
/* TS items(player): owned result; false is TS null. Free with the destroy fn. */
typedef struct qa_app_q1_wheel_items_result {
  qa_ui_wheel_item *items; size_t count;
  qa_ui_q1_wheel_items owned; /* backing store */
  const char **labels;         /* NULL-terminated label scratch */
} qa_app_q1_wheel_items_result;
bool qa_app_application_q1_wheel_items(qa_app_application_q1_wheel *wheel,
    const qa_app_sim_player_ui *player, qa_app_q1_wheel_items_result *out);
void qa_app_q1_wheel_items_result_destroy(qa_app_q1_wheel_items_result *result);
#endif

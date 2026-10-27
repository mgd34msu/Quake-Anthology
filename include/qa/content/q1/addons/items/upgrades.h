/* Port of src/content/q1/addons/items/upgrades.ts. Mg3Upgrade keeps
 * its exact TS strings. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_UPGRADES_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_UPGRADES_H
#include "qa/content/q1/addons/context.h"
uint8_t *qa_q1ad_capture_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1ad_player *player, size_t *out_length);
void qa_q1ad_restore_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1ad_player *player,
                                        const uint8_t *bytes, size_t length);
double qa_q1ad_mg3_upgrade_flag(const char *map);
double qa_q1ad_mg3_upgraded_maximum(double base, double flags);
void qa_q1ad_initialize_mg3_capacities(qa_q1ad_context *context, qa_q1ad_player *player);
bool qa_q1ad_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double *out);
void qa_q1ad_set_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double capacity);
void qa_q1ad_register_mg3_upgrades(qa_q1ad_context *context);
bool qa_q1ad_give_next_mg3_upgrade(qa_q1ad_context *context, const char *type, qa_q1ad_player *player);
#endif

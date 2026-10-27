/* Port of src/content/q1/addons/items/common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_COMMON_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_COMMON_H
#include "qa/content/q1/addons/context.h"
#define QA_Q1AD_MG3_ITEM_PREFIX "mg3:items:"
#define QA_Q1AD_MG3_SPAWNED_ITEM 4
void qa_q1ad_start_mg3_item(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_finish_mg3_pickup(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_actor_id other,
                               const char *message, const char *sound,
                               const qa_q1ad_msg_arg *args, size_t arg_count);
void qa_q1ad_register_mg3_item_callbacks(qa_q1ad_context *context);
#endif

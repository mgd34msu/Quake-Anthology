/* Port of src/content/q1/addons/items/commands.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_COMMANDS_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_COMMANDS_H
#include "qa/content/q1/addons/context.h"
typedef void (*qa_q1ad_developer_message_fn)(void *ctx, const char *text);
bool qa_q1ad_handle_mg3_item_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse,
                                     qa_q1ad_developer_message_fn developer_message, void *message_ctx);
#endif

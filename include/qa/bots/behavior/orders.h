/* Port of src/bots/behavior/orders.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_ORDERS_H
#define QA_BOTS_BEHAVIOR_ORDERS_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_bot_order_entity {
  int64_t number;
  int64_t generation;
} qa_bot_order_entity;
typedef enum qa_bot_order_kind { QA_BOT_ORDER_POINT, QA_BOT_ORDER_FOLLOW } qa_bot_order_kind;
typedef struct qa_bot_order {
  qa_bot_order_kind kind;
  qa_vec3 point;
  qa_bot_order_entity entity;
} qa_bot_order;
typedef enum qa_bot_order_progress {
  QA_BOT_ORDER_IN_PROGRESS,
  QA_BOT_ORDER_SUCCESS,
  QA_BOT_ORDER_ERROR
} qa_bot_order_progress;
typedef struct qa_bot_order_state {
  qa_bot_order order;
  qa_bot_order_progress progress;
} qa_bot_order_state;
typedef int32_t qa_bot_goal_status;
qa_bot_goal_status qa_bot_order_status(const qa_bot_order_state *state);
bool qa_bot_order_active(const qa_bot_order_state *state);
bool qa_same_bot_order(qa_bot_order left, qa_bot_order right);
#endif

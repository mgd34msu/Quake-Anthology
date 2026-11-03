/* See include/qa/bots/behavior/orders.h. GPL-2.0-or-later. */
#include "qa/bots/behavior/orders.h"
#include <math.h>
#include "qa/foundation/js.h"
qa_bot_goal_status qa_bot_order_status(const qa_bot_order_state *s) {
  return (s == NULL || s->progress == QA_BOT_ORDER_ERROR) ? 0
         : s->progress == QA_BOT_ORDER_SUCCESS            ? 1
                                                          : 2;
}
bool qa_bot_order_active(const qa_bot_order_state *s) {
  return s != NULL && s->progress != QA_BOT_ORDER_ERROR &&
         (s->progress == QA_BOT_ORDER_IN_PROGRESS || s->order.kind == QA_BOT_ORDER_FOLLOW);
}
bool qa_same_bot_order(qa_bot_order l, qa_bot_order r) {
  if (l.kind == QA_BOT_ORDER_POINT && r.kind == QA_BOT_ORDER_POINT) {
    double dx = l.point.x - r.point.x, dy = l.point.y - r.point.y, dz = l.point.z - r.point.z;
    return qa_js_hypot3(dx, dy, dz) < 8.0;
  }
  return l.kind == QA_BOT_ORDER_FOLLOW && r.kind == QA_BOT_ORDER_FOLLOW &&
         l.entity.number == r.entity.number && l.entity.generation == r.entity.generation;
}

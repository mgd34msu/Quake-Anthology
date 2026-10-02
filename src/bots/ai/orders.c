#include "internal.h"
#include "source_activation.h"
#include "source_player.h"

static bool same_order(const qa_bot_order *a, const qa_bot_order *b) {
    if (a->kind != b->kind) return false;
    return a->kind == QA_BOT_ORDER_POINT ? qa_vec_length(qa_vec_sub(a->point, b->point)) < 8 :
        a->kind == QA_BOT_ORDER_FOLLOW ? qa_actor_id_equal(a->target, b->target) : true;
}
static bool set_order(qa_bots *b, qa_actor_id actor, const qa_bot_order *order,
                         qa_bot_order_status *out, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (!out) return bot_ai_fail(e, "missing explicit bot order status");
    *out = QA_BOT_ORDER_ERROR;
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s || !bot_ai_live(b, actor)) return true;
    if (!same_order(&s->view.order, order)) {
        if(!bot_ai_activation_clear(b,s,e)) return false;
        if (s->view.decision == QA_BOT_ACTIVATING) s->view.decision = QA_BOT_SEEK_LONG_TERM;
        s->view.order = *order;
        s->view.order.status = QA_BOT_ORDER_ACTIVE;
        if (!qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime), s->movement, false, e)) return false;
    }
    *out = qa_bots_order_status(b, actor);
    return true;
}
bool qa_bots_move_to(qa_bots *b, qa_actor_id actor, qa_vec3 point,
                       qa_bot_order_status *out, qa_error *e) {
    if (!out) return bot_ai_fail(e, "missing explicit bot order status");
    if (!qa_vec_finite(point)) { *out = QA_BOT_ORDER_ERROR; return true; }
    qa_bot_order order = {.kind = QA_BOT_ORDER_POINT, .point = point};
    return set_order(b, actor, &order, out, e);
}
bool qa_bots_follow(qa_bots *b, qa_actor_id actor, qa_actor_id target,
                      qa_bot_order_status *out, qa_error *e) {
    if (!b || !out) return bot_ai_fail(e, "missing bot follow owner/status");
    if (!bot_ai_live(b, target)) {
        *out = QA_BOT_ORDER_ERROR;
        return true;
    }
    qa_bot_order order = {.kind = QA_BOT_ORDER_FOLLOW, .target = target};
    return set_order(b, actor, &order, out, e);
}
bool qa_bots_clear_order(qa_bots *b, qa_actor_id actor, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (!s || !bot_ai_live(b, actor) || s->view.order.kind == QA_BOT_ORDER_NONE) return true;
    if (bot_ai_order_active(s)) {
        if(!bot_ai_activation_clear(b,s,e)) return false;
        if (s->view.decision == QA_BOT_ACTIVATING) s->view.decision = QA_BOT_SEEK_LONG_TERM;
    }
    s->view.order = (qa_bot_order){0};
    return qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime), s->movement, false, e);
}
qa_bot_order_status qa_bots_order_status(const qa_bots *b, qa_actor_id actor) {
    bot_ai_state *s = bot_ai_actor(b, actor);
    if (s && s->view.order.kind == QA_BOT_ORDER_FOLLOW && !bot_ai_live(b, s->view.order.target))
        s->view.order.status = QA_BOT_ORDER_ERROR;
    return s && bot_ai_live(b, actor) ? s->view.order.status : QA_BOT_ORDER_ERROR;
}
bool bot_ai_order_active(const bot_ai_state *s) {
    const qa_bot_order *o = &s->view.order;
    return o->kind != QA_BOT_ORDER_NONE && o->status != QA_BOT_ORDER_ERROR &&
        (o->status == QA_BOT_ORDER_ACTIVE || o->kind == QA_BOT_ORDER_FOLLOW);
}
static float radius(qa_bounds bounds) {
    return fmaxf(fmaxf(fabsf(bounds.mins.x), fabsf(bounds.mins.y)),
                    fmaxf(bounds.maxs.x, bounds.maxs.y));
}
bool bot_ai_order_goal(qa_bots *b, bot_ai_state *s, qa_bot_goal *goal, bool *found, qa_error *e) {
    *found = false;
    if (!bot_ai_order_active(s)) return true;
    qa_body_state self, target;
    qa_bot_order *order = &s->view.order;
    bool follow = order->kind == QA_BOT_ORDER_FOLLOW;
    if (follow && !bot_ai_live(b, order->target)) {
        order->status = QA_BOT_ORDER_ERROR;
        return true;
    }
    if (!qa_world_body_read(b->services.shared.world, s->view.actor, &self, e) ||
        (follow && !qa_world_body_read(b->services.shared.world, order->target, &target, e))) return false;
    if (!bot_ai_live(b, s->view.actor)) return true;
    qa_vec3 origin = follow ? target.origin : order->point;
    float own_radius = radius(self.bounds);
    float clearance = follow ? sqrtf(2) * (own_radius + radius(target.bounds)) : 0;
    if (qa_vec_length(qa_vec_sub(origin, bot_ai_origin(s))) <=
        (follow ? clearance + own_radius : own_radius * 2)) {
        order->status = QA_BOT_ORDER_SUCCESS;
        return true;
    }
    qa_vec3 toward = qa_vec_sub(bot_ai_origin(s), origin);
    float horizontal = hypotf(toward.x, toward.y);
    float stand_off = follow && horizontal ? (clearance + own_radius * .5f) / horizontal : 0;
    qa_vec3 destination = qa_v3(origin.x + toward.x * stand_off,
                                  origin.y + toward.y * stand_off, origin.z);
    uint32_t area;
    if (!bot_ai_point_area(b, s, destination, &area, e)) return false;
    if (!area) { order->status = QA_BOT_ORDER_ERROR; return true; }
    int32_t entity = -1;
    if (follow) {
        qa_bot_entity observed;
        if (!b->services.entity(b->services.context, order->target, &observed, e)) return false;
        if (!observed.present || !bot_ai_live(b, order->target)) {
            order->status = QA_BOT_ORDER_ERROR;
            return true;
        }
        entity = observed.number;
    }
    order->status = QA_BOT_ORDER_ACTIVE;
    *goal = (qa_bot_goal){.origin = destination, .area = (int32_t)area,
        .mins = qa_v3(-own_radius, -own_radius, -own_radius),
        .maxs = qa_v3(own_radius, own_radius, own_radius), .entity = entity};
    *found = true;
    return true;
}

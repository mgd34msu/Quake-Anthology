#include "internal.h"

bool q2m_medic_blocked(q2m_context *context, float distance, bool *accepted,
                       qa_error *error) {
    *accepted = false;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (!q2m_alive(context) || context->combat.health <= 0 ||
        (rerelease && context->monster->target_anger))
        return true;
    if (!q2_actor_live(context->game, context->monster->enemy))
        return true;
    if (!rerelease) {
        if (!q2m_blocked_tesla(context, accepted, error))
            return false;
        if (!q2m_alive(context) || *accepted)
            return true;
    }
    qa_actor_id target = context->monster->enemy;
    if (!q2_actor_live(context->game, target))
        return true;
    qa_body_state enemy;
    if (!qa_world_body_read(context->game->services.world, target, &enemy, error))
        return !q2m_alive(context) || !q2_actor_live(context->game, target);
    if (!q2m_alive(context) || !q2_actor_live(context->game, target) ||
        !qa_actor_id_equal(context->monster->enemy, target))
        return true;
    if (!qa_world_body_read(context->game->services.world, context->actor->id,
                            &context->body, error))
        return !q2m_alive(context) || !q2_actor_live(context->game, target);
    if (!q2m_alive(context) || !q2_actor_live(context->game, target) ||
        !qa_actor_id_equal(context->monster->enemy, target))
        return true;
    return q2m_blocked_platform(context, &enemy, distance, accepted, error);
}

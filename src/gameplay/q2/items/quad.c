#include "internal.h"

typedef struct quad_call {
    qa_q2_game *game;
    uint64_t duration_ns;
    bool stack;
} quad_call;

static bool set_quad(void *context, qa_actor_id actor, qa_error *error) {
    quad_call *call = context;
    q2_power_state *powers = q2_powers(call->game, actor, error);
    if (!powers) return false;
    uint64_t start = call->game->now_ns;
    if (call->stack && powers->values.quad_until_ns > start)
        start = powers->values.quad_until_ns;
    powers->values.quad_until_ns = q2_deadline(start, call->duration_ns);
    return true;
}

bool qa_q2_player_quad(qa_q2_game *game, qa_actor_id actor, uint64_t duration_ns,
                       qa_error *error) {
    if (!game) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 quad effects owner");
        return false;
    }
    quad_call call = {.game = game, .duration_ns = duration_ns};
    return qa_q2_run_actor(game, actor, set_quad, &call, error);
}

bool qa_q2_player_quad_stack(qa_q2_game *game, qa_actor_id actor, uint64_t duration_ns,
                             qa_error *error) {
    if (!game) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 quad effects owner");
        return false;
    }
    quad_call call = {.game = game, .duration_ns = duration_ns, .stack = true};
    return qa_q2_run_actor(game, actor, set_quad, &call, error);
}

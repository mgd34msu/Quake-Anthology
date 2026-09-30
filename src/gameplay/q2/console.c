#include "player/internal.h"
#include "qa/console.h"

typedef struct console_call {
    qa_q2_game *game;
    const qa_command_invocation *command;
    bool *handled;
} console_call;
static bool dispatch(void *context, qa_actor_id actor, qa_error *error) {
    console_call *call = context;
    return q2_player_command(call->game, actor, call->command->argv[0], call->command->argc - 1,
                              call->command->argv + 1, call->handled, error);
}
bool qa_q2_game_console_command(qa_q2_game *game, qa_actor_id actor,
                                 const qa_command_invocation *command, bool *handled,
                                 qa_error *error) {
    if (!game || !command || !handled || !command->argc || !command->argv ||
        !command->argv[0]) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 console invocation");
        return false;
    }
    *handled = false;
    q2_actor *source = q2_actor_get(game, actor, false, NULL);
    if (!source || !q2_actor_live(game, actor))
        return true;
    console_call call = {.game = game, .command = command, .handled = handled};
    return qa_q2_run_actor(game, actor, dispatch, &call, error);
}

#include "internal.h"

bool bot_ai_reset(qa_bots *b, bot_ai_state *s, qa_error *e) {
    bot_ai_state fresh = {.view = {.actor = s->view.actor, .client = s->view.client,
        .entity = s->view.entity, .mode = s->view.mode, .decision = QA_BOT_SEEK_LONG_TERM,
        .enter_time = s->view.enter_time}, .player = s->player,
        .character = s->character, .goals = s->goals, .weapons = s->weapons,
        .chat = s->chat, .movement = s->movement, .team_arena = s->team_arena,
        .command_sequence = s->command_sequence, .walker=s->walker};
    memcpy(fresh.name, s->name, sizeof(fresh.name));
    *s = fresh;
    return qa_bot_moves_reset(qa_bot_runtime_moves(b->runtime), s->movement, e) &&
        qa_bot_goals_reset(qa_bot_runtime_goals(b->runtime), s->goals, e) &&
        qa_bot_runtime_weapon_reset(b->runtime, s->weapons, e) &&
        qa_bot_goals_avoid_clear(qa_bot_runtime_goals(b->runtime), s->goals, e) &&
        qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime), s->movement, false, e);
}
bool qa_bots_level_reset(qa_bots *b, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy = true;
    bool ok = true;
    for (uint32_t i = 0; ok && i < b->client_capacity; ++i) {
        bot_ai_state *s = b->clients[i];
        if (!s || s->retired) continue;
        ok = bot_ai_reset(b, s, e);
        if (ok) s->setup_count = 4;
    }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}

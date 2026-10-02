#include "internal.h"
#include "source_storage.h"

bool qa_bots_source_begin(qa_bots *b,qa_actor_id actor,qa_vec3 angles,int32_t weapon,qa_error *e) {
    if(!bot_ai_mutable(b,e)) return false;
    if(!bot_ai_live(b,actor)) return true;
    bot_ai_state *state=bot_ai_actor(b,actor);
    if(!state) return true;
    if(!bot_ai_storage_vec3(b,state,QA_BOT_SOURCE_VIEW_ANGLES,&angles,true,e) ||
       !bot_ai_storage_vec3(b,state,QA_BOT_SOURCE_IDEAL_VIEW_ANGLES,&angles,true,e) ||
       !bot_ai_storage_i32(b,state,QA_BOT_SOURCE_WEAPON_NUMBER,&weapon,true,e)) return false;
    return true;
}

bool bot_ai_reset(qa_bots *b, bot_ai_state *s, qa_error *e) {
    bot_ai_source_order_clear(b,s);
    if(!qa_bot_source_record_clear(&b->services.memory,s->source_record,true,e)) return false;
    bot_ai_state fresh = {.acquired_source_client=s->acquired_source_client,
        .source_record=s->source_record,
        .source_span=s->source_span,
        .view = {.actor = s->view.actor, .client = s->view.client,
        .source_client=s->view.source_client,
        .entity = s->view.entity, .mode = s->view.mode, .decision = QA_BOT_SEEK_LONG_TERM}, .player = s->player,
        .character = s->character, .goals = s->goals, .weapons = s->weapons,
        .chat = s->chat, .movement = s->movement, .team_arena = s->team_arena,
        .command_sequence = s->command_sequence, .admitted_skill=s->admitted_skill,
        .admitted_character=s->admitted_character,.admitted_name=s->admitted_name,
        .source_setup=s->source_setup,.inuse=s->inuse,.counted=s->counted};
    fresh.source_setup.map_restart=false;
    memcpy(fresh.name, s->name, sizeof(fresh.name));
    fresh.player.velocity=(qa_vec3){0};
    fresh.player.carrying_objective=false;
    bot_ai_source_order_init(&fresh.source_order);
    *s = fresh;
    return (!s->movement || qa_bot_moves_reset(qa_bot_runtime_moves(b->runtime), s->movement, e)) &&
        qa_bot_goals_reset(qa_bot_runtime_goals(b->runtime), s->goals, e) &&
        qa_bot_runtime_weapon_reset(b->runtime, s->weapons, e) &&
        qa_bot_goals_avoid_clear(qa_bot_runtime_goals(b->runtime), s->goals, e) &&
        (!s->movement || qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime), s->movement, false, e));
}
bool qa_bots_level_reset(qa_bots *b, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->busy = true;
    bool ok = true;
    for (uint32_t i = 0; ok && i < b->client_capacity; ++i) {
        bot_ai_state *s = b->clients[i];
        if (!s || !s->inuse || s->retired) continue;
        ok = bot_ai_reset(b, s, e);
        if (ok) {
            int32_t setup_count=4;
            ok=bot_ai_storage_i32(b,s,QA_BOT_SOURCE_SETUP_COUNT,&setup_count,true,e);
        }
    }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}

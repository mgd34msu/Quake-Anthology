#include "internal.h"
#include "source_alias.h"
#include "source_storage.h"

bool bot_ai_source_alias_bind(qa_bots *bots, bot_ai_state *state, qa_error *error)
{
    if (!bots || !state || state->source_record.length != QA_BOT_STATE_SOURCE_BYTES)
        return bot_ai_fail(error, "BotState alias requires its actual complete source allocation");
    qa_bot_source_span span;
    if (!qa_bot_source_record_span(&bots->services.memory, state->source_record, &span, error)) return false;
    state->source_span = span;
    return true;
}

bool qa_bots_source_memory_bind(qa_bots *bots,qa_error *error)
{
    if(!bot_ai_mutable(bots,error)) return false;
    for(uint32_t source=0;source<64;++source)
        if(bots->source_cells[source] &&
           !bot_ai_source_alias_bind(bots,bots->source_cells[source],error)) return false;
    return true;
}

bool qa_bots_source_weapon_handle(qa_bots *bots,qa_actor_id actor,uint32_t *out,qa_error *error)
{
    bot_ai_state *state=bot_ai_actor(bots,actor);
    if(!bots || !state || !out || bots->restore_pending || !bot_ai_live(bots,actor))
        return bot_ai_fail(error,"Weapon knowledge requires its actual live BotState source record");
    return bot_ai_storage_u32(bots,state,QA_BOT_SOURCE_WEAPONS,out,false,error);
}

#include "bots_memory_bind.h"
#include "bot_world.h"
#include "qa/game_q3_source.h"

bool application_bots_memory_span(void *context,uint32_t offset,uint32_t size,uint8_t **out,qa_error *error)
{
    application_bots *bots=context;
    application_provider *source=bots?application_bot_source(bots):NULL;
    if(!bots || !out || !source || !source->constructed || !source->attached ||
       !source->map_bound || source->close_pending ||
       source!=application_world_provider(bots->application,QA_ROLE_ENTITIES,""))
        return application_fail(error,QA_ERROR_ARGUMENT,"BotState byte span has no actual mutable source module owner");
    if(bots->shared_world && (source->kind==APPLICATION_PROVIDER_Q1 || source->kind==APPLICATION_PROVIDER_Q2)) {
        application_bot_memory_view span;
        if(!application_bot_world_memory_alias(bots->shared_world,
            (application_bot_memory_alias){offset,size},&span,error)) return false;
        *out=span.data;return true;
    }
    if(source->kind==APPLICATION_PROVIDER_Q3 && source->state.q3)
        return qa_q3_source_memory_span(source->state.q3,offset,size,out,error);
    return application_fail(error,QA_ERROR_ARGUMENT,"BotState byte span requires its actual native or shared GAME pool");
}

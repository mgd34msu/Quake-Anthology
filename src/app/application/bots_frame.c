#include "bots_private.h"
#include "bots_frame.h"
#include "bot_world.h"
#include "qa/game_q3_configstrings.h"

bool application_bots_configstring_set(void *opaque,uint32_t index,const char *text,qa_error *error)
{
    application_bots *bots=opaque;
    if(!bots) return application_fail(error,QA_ERROR_ARGUMENT,"bot configstring write has no actual source owner");
    if(bots->shared_world)
        return application_bot_world_configstring_set(bots->shared_world,index,text,error);
    application_provider *source=application_bot_source(bots);
    if(source && source->kind==APPLICATION_PROVIDER_Q3 && source->state.q3)
        return qa_q3_configstring_write(source->state.q3,index,text,error);
    return application_fail(error,QA_ERROR_UNSUPPORTED,"bot configstring write requires its native or shared source GAME");
}

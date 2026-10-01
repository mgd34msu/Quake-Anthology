#include "source_inventory.h"
#include "source_storage.h"

bool bot_ai_source_inventory_read(void *context,int32_t index,int32_t *out,qa_error *error)
{
    bot_source_inventory *inventory=context;
    if(!inventory || !inventory->bots || !inventory->state || !out)
        return bot_ai_fail(error,"Inventory read requires its actual BotState allocation");
    if(index<0 || index>=QA_BOT_INVENTORY_SIZE)
        return bot_ai_fail(error,"Fuzzy inventory index is outside the actual BotState array");
    return bot_ai_storage_i32(inventory->bots,inventory->state,
        QA_BOT_SOURCE_INVENTORY+(uint32_t)index*4,out,false,error);
}

bool bot_ai_source_inventory_write(bot_source_inventory *inventory,int32_t index,int32_t value,qa_error *error)
{
    if(!inventory || !inventory->bots || !inventory->state || index<0 || index>=QA_BOT_INVENTORY_SIZE)
        return bot_ai_fail(error,"Inventory write exceeds its actual BotState array");
    return bot_ai_storage_i32(inventory->bots,inventory->state,
        QA_BOT_SOURCE_INVENTORY+(uint32_t)index*4,&value,true,error);
}

qa_bot_inventory_view bot_ai_source_inventory_view(bot_source_inventory *inventory)
{
    return (qa_bot_inventory_view){.count=QA_BOT_INVENTORY_SIZE,
        .context=inventory,.read=bot_ai_source_inventory_read};
}

static bool write(void *context,int32_t index,int32_t value,qa_error *error)
{
    return bot_ai_source_inventory_write(context,index,value,error);
}

qa_bot_inventory_target bot_ai_source_inventory_target(bot_source_inventory *inventory)
{
    return (qa_bot_inventory_target){.count=QA_BOT_INVENTORY_SIZE,
        .context=inventory,.write=write};
}

bool bot_ai_source_inventory_snapshot(bot_source_inventory *inventory,
                                      int32_t out[QA_BOT_INVENTORY_SIZE],qa_error *error)
{
    if(!out) return bot_ai_fail(error,"Inventory snapshot requires its explicit caller storage");
    for(int32_t index=0;index<QA_BOT_INVENTORY_SIZE;++index)
        if(!bot_ai_source_inventory_read(inventory,index,out+index,error)) return false;
    return true;
}

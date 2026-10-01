#include "source_weapon_save.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

static bool allocation_fields(qa_source_save_io *io,qa_bot_memory *memory,qa_bot_memory_allocation *allocation) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t reference=0;
    bool ok=reading || qa_bot_memory_reference(memory,*allocation,&reference,io->error);
    if(ok) ok=qa_source_save_count(io,&reference,SIZE_MAX);
    if(ok && reading) ok=qa_bot_memory_resolve(memory,reference,allocation,io->error);
    if(!ok) io->failed=true;return ok;
}
bool bot_weapon_record_fields(qa_source_save_io *io,qa_bot_memory *memory,bot_weapon_record *record) {
    qa_bot_memory_allocation allocation=record->allocation;
    if(!allocation_fields(io,memory,&allocation)) return false;
    bot_weapon_record qualified;
    if(!bot_weapon_record_bind(memory,allocation,&qualified,io->error)) {io->failed=true;return false;}
    if(io->direction==QA_SOURCE_SAVE_READ) *record=qualified;
    return true;
}
static bool config_fields(qa_source_save_io *io,const bot_weapon_weight_refs *refs,
    qa_bot_weights **config,bool optional,bool *present) {
    uint64_t id=0;bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=reading || refs->reference(refs->context,*config,&id,present,io->error);
    if(ok && optional) ok=qa_source_save_bool(io,present);
    if(ok && !optional && !reading && !*present)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Live weapon pointer references a retired weight configuration");
    if(ok && (!optional || *present)) {
        ok=qa_source_save_u64(io,&id);
        if(ok && reading) ok=refs->resolve(refs->context,id,config,io->error);
    }
    if(!ok) io->failed=true;return ok;
}
static bool fields(qa_source_save_io *io,qa_bot_memory *memory,bot_weapon_pointers *pointers,
    const bot_weapon_weight_refs *refs) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_u64(io,&pointers->next_pointer) || !pointers->next_pointer ||
       pointers->next_pointer>UINT64_C(4294967296))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon pointer counter exceeds its source domain");
    size_t count=0;
    if(!reading) for(bot_weapon_pointer *row=pointers->first;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && count>(io->input.size-io->offset)/8)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated weapon pointer rows");
    bot_weapon_pointer *row=pointers->first;
    for(size_t i=0;i<count;++i) {
        if(reading) {
            row=calloc(1,sizeof(*row));
            if(!row) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring source weapon pointer row");
            if(pointers->last) pointers->last->next=row;else pointers->first=row;pointers->last=row;
        }
        uint32_t kind=(uint32_t)row->kind;
        if(!qa_source_save_u32(io,&row->pointer) || !row->pointer || row->pointer>=pointers->next_pointer ||
           !qa_source_save_u32(io,&kind) || kind>BOT_WEAPON_POINTER_INDEXES)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid source weapon pointer identity/kind");
        row->kind=(bot_weapon_pointer_kind)kind;
        for(bot_weapon_pointer *prior=pointers->first;prior!=row;prior=prior->next)
            if(prior->pointer==row->pointer) return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate source weapon pointer");
        if(row->kind==BOT_WEAPON_POINTER_CONFIG) {
            bool present=true;qa_bot_weights *config=row->config;
            if(!config_fields(io,refs,&config,false,&present)) return false;
            if(reading) {qa_bot_weights_retain(config);row->config=config;}
            if(!qa_source_save_u64(io,&row->references) || !row->references ||
               row->references>UINT64_C(9007199254740991))
                return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid live weapon configuration reference");
        } else if(!allocation_fields(io,memory,&row->indexes)) return false;
        if(!reading) row=row->next;
    }
    count=0;
    if(!reading) for(bot_weapon_config_identity *identity=pointers->configs;identity;identity=identity->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && count>(io->input.size-io->offset))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated weak weapon configuration identities");
    bot_weapon_config_identity *identity=pointers->configs;
    for(size_t i=0;i<count;++i) {
        bot_weapon_config_identity candidate=reading?(bot_weapon_config_identity){0}:*identity;
        bool present=true;
        if(!config_fields(io,refs,&candidate.config,true,&present)) return false;
        if(present) {
            if(!qa_source_save_u32(io,&candidate.pointer) || !candidate.pointer || candidate.pointer>=pointers->next_pointer)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid weak weapon configuration identity");
            for(bot_weapon_pointer *pointer=pointers->first;pointer;pointer=pointer->next)
                if(pointer->pointer==candidate.pointer &&
                   (pointer->kind!=BOT_WEAPON_POINTER_CONFIG || pointer->config!=candidate.config))
                    return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon configuration identity disagrees with live pointer");
            for(bot_weapon_config_identity *prior=pointers->configs;reading?prior!=NULL:prior!=identity;prior=prior->next)
                if(prior->config==candidate.config)
                    return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate weak weapon configuration identity");
            if(reading) {
                bot_weapon_config_identity *created=malloc(sizeof(*created));
                if(!created) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring weak weapon configuration identity");
                *created=candidate;created->next=NULL;qa_bot_weights_retain(created->config);
                if(pointers->last_config) pointers->last_config->next=created;else pointers->configs=created;
                pointers->last_config=created;
            }
        }
        if(!reading) identity=identity->next;
    }
    return true;
}
bool bot_weapon_pointer_fields(qa_source_save_io *io,qa_bot_memory *memory,bot_weapon_pointers *pointers,
    const bot_weapon_weight_refs *refs) {
    if(!io || !memory || !pointers || !refs || !refs->reference || !refs->resolve)
        return io?bot_save_fail(io,QA_ERROR_ARGUMENT,"Missing weapon pointer codec owners"):false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return fields(io,memory,pointers,refs);
    bot_weapon_pointers candidate;bot_weapon_pointers_init(&candidate);
    if(!fields(io,memory,&candidate,refs)) {bot_weapon_pointers_clear(&candidate);return false;}
    bot_weapon_pointers old=*pointers;*pointers=candidate;bot_weapon_pointers_clear(&old);return true;
}

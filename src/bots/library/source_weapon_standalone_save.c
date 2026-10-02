#include "internal.h"
#include "source_weapon_standalone_save.h"
#include "source_weapon_resource_save.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

static bool memory_fields(qa_source_save_io *io,qa_bot_memory *memory) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || qa_bot_memory_capture(memory,&bytes,io->error);
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone weapon MEMORY");
        else {
            ok=qa_bot_memory_restore(memory,(qa_bytes){io->input.data+io->offset,extent},io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_buffer_free(&bytes);if(!ok) io->failed=true;return ok;
}
static bool resource_fields(qa_source_save_io *io,const qa_bot_weapons *source,
    qa_bot_memory *memory,qa_bot_weapons **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!reading && (!source || !source->source || !source->source->bound ||
       source->source->memory!=memory))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Standalone weapon alias has no actual bound source owner");
    qa_bot_weapons *config=(qa_bot_weapons *)source;
    if(reading) {
        config=calloc(1,sizeof(*config));
        if(!config) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring standalone weapon binding");
        atomic_init(&config->references,1);
    }
    bot_weapon_resource_factory factory={.memory=memory};
    bool ok=bot_weapon_resource_fields(io,reading?NULL:config->source,&factory,
        reading?&config->source:NULL);
    if(reading) {
        if(ok) *out=config;
        else qa_bot_weapons_release(config);
    }
    return ok;
}
bool bot_weapons_standalone_fields(qa_source_save_io *io,const qa_bot_weapons *source,
    qa_bot_weapons **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_bot_memory *memory=NULL;
    if((reading && (!out || *out)) || (!reading && (!source || !source->source || !source->source->bound)))
        return bot_save_fail(io,QA_ERROR_ARGUMENT,"Standalone weapon codec requires actual owners/output");
    bool ok=!reading || qa_bot_memory_create(NULL,&memory,io->error);
    if(!reading) memory=source->source->memory;
    if(ok) ok=memory_fields(io,memory) && resource_fields(io,source,memory,out);
    if(reading && memory) (void)qa_bot_memory_release(memory,NULL);
    if(!ok) io->failed=true;
    return ok;
}
bool bot_weapons_alias_fields(qa_source_save_io *io,const qa_bot_weapons *first,
    const qa_bot_weapons *source,qa_bot_weapons **out) {
    if(!first || !first->source || !first->source->bound)
        return bot_save_fail(io,QA_ERROR_FORMAT,"Shared weapon alias has no genuine first source owner");
    return resource_fields(io,source,first->source->memory,out);
}

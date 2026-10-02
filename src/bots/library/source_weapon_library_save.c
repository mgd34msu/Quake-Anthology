#include "internal.h"
#include "source_weapon_library_save.h"
#include "source_weapon_resource_save.h"
#include "../save_fields.h"

bool bot_weapons_library_fields(qa_source_save_io *io,qa_bot_library *library,
    const bot_weapon_resource_host *host,const qa_bot_weapons *source,qa_bot_weapons **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!library || (reading && (!out || *out)) ||
       (!reading && (!source || !source->source || source->source->memory!=library->memory)))
        return bot_save_fail(io,QA_ERROR_ARGUMENT,"Weapon aliases require their actual imported library MEMORY");
    qa_bot_weapons *config=(qa_bot_weapons *)source;
    if(reading) {
        config=calloc(1,sizeof(*config));
        if(!config) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring source weapon library binding");
        atomic_init(&config->references,1);
    }
    bot_weapon_resource_host fallback=bot_weapons_library_host(library);
    bot_weapon_resource_host actual=host?*host:fallback;
    bool qualified=!reading && source->source->host.qualify!=NULL;
    if(!reading) {
        const bot_weapon_resource_host *held=&source->source->host;
        const bot_weapon_resource_host *expected=qualified?host:&fallback;
        if(!expected || held->context!=expected->context || held->current!=expected->current ||
           held->report!=expected->report || held->qualify!=expected->qualify) {
            return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon resource has another actual service owner");
        }
    }
    if(!qa_source_save_bool(io,&qualified) || (reading && qualified && (!host || !host->qualify))) {
        if(reading) qa_bot_weapons_release(config);
        return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon setup qualifier has no actual runtime host");
    }
    if(!qualified) actual=fallback;
    if(!reading) {
        actual.generation=source->source->host.generation;
        actual.revision=source->source->host.revision;
    }
    if(!qa_source_save_u64(io,&actual.generation) || actual.generation>UINT64_C(9007199254740991) ||
       !qa_source_save_u64(io,&actual.revision) || actual.revision>UINT64_C(9007199254740991) ||
       (!qualified && (actual.generation || actual.revision)) ||
       (qualified && (!actual.revision || actual.generation>host->generation || actual.revision>host->revision))) {
        if(reading) qa_bot_weapons_release(config);
        return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained weapon setup qualification");
    }
    bot_weapon_resource_factory factory={library->memory,&library->options.scripts,
        &library->options.preprocessor,&actual};
    bool ok=bot_weapon_resource_fields(io,reading?NULL:config->source,&factory,
        reading?&config->source:NULL);
    if(reading) {
        if(ok) *out=config;
        else qa_bot_weapons_release(config);
    }
    return ok;
}

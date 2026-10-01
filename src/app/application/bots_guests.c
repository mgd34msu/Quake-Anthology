#include "bots_guests.h"
#include "guest_q3_restart.h"
#include "qa/bot_runtime_save.h"
#include "../../bots/save_fields.h"

qa_bot_runtime *application_bots_guest_runtime(qa_application *app,application_provider *provider)
{
    application_bots *bots=app?app->bots:NULL;
    for(application_bot_guest *guest=bots?bots->guests:NULL;guest;guest=guest->next)
        if(guest->provider==provider) return guest->runtime;
    return NULL;
}

bool application_bots_guests_create(application_bots *bots,const qa_bot_runtime_options *base,qa_error *error)
{
    const char *name=qa_strings_cstr(qa_session_strings(bots->application->session),bots->application->current_map);
    qa_bot_runtime_map map={.name=name,.entities=&bots->entities,.navigation=bots->map_navigation};
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        qa_bot_runtime_options options=*base;
        options.maximum_states=64;options.minimum_clients=guest->client_base+64;
        options.observations=QA_BOT_OBSERVATION_MODULE;
        if(!application_bots_runtime_create(bots,&options,&guest->runtime,error) ||
           !qa_bot_runtime_attach_map(guest->runtime,&map,error)) return false;
    }
    return true;
}

bool application_bots_guests_can_destroy(const application_bots *bots)
{
    for(const application_bot_guest *guest=bots?bots->guests:NULL;guest;guest=guest->next)
        if(!qa_bot_runtime_can_destroy(guest->runtime)) return false;
    return true;
}

bool application_bots_guests_destroy(application_bots *bots,qa_error *error)
{
    if(!application_bots_guests_can_destroy(bots))
        return application_fail(error,QA_ERROR_ARGUMENT,"Actual guest bot libraries are executing callbacks");
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(application_q3_guest_bots_borrowed(bots->application,guest->runtime))
            return application_fail(error,QA_ERROR_ARGUMENT,"Original GAME still borrows its actual guest bot library");
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        if(!qa_bot_runtime_destroy(guest->runtime,error)) return false;
        guest->runtime=NULL;qa_bots_save_requirements_free(&guest->saved_requirements);
    }
    return true;
}

static bool section(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(size>io->input.size-io->offset)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated actual guest bot library owner");
        *bytes=(qa_bytes){io->input.data+io->offset,size};io->offset+=size;return true;
    }
    return qa_source_save_bytes(io,(void *)bytes->data,size);
}

bool application_bots_guest_fields(qa_source_save_io *io,application_bot_guest *guest)
{
    qa_buffer requirements={0},runtime={0};bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool okay=reading || (qa_bots_save_requirements_capture(guest->runtime,NULL,&requirements,io->error) &&
        qa_bot_runtime_save_capture(io->session,guest->runtime,&runtime,io->error));
    qa_bytes required={requirements.data,requirements.size},saved={runtime.data,runtime.size};
    if(okay) okay=section(io,&required) && required.size && section(io,&saved) && saved.size;
    if(okay && reading) {
        okay=qa_bots_save_requirements_read(required,&guest->saved_requirements,io->error) &&
            !guest->saved_requirements.population && guest->saved_requirements.runtime.maximum_states==64 &&
            guest->saved_requirements.runtime.minimum_clients==guest->client_base+64 &&
            guest->saved_requirements.runtime.observations==QA_BOT_OBSERVATION_MODULE;
        if(okay) guest->saved_runtime=saved;
    }
    qa_buffer_free(&requirements);qa_buffer_free(&runtime);
    if(!okay && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid actual guest bot library continuation");
    return okay;
}

bool application_bots_guests_construct_restored(application_bots *bots,qa_error *error)
{
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next)
        if(!application_bots_runtime_create(bots,&guest->saved_requirements.runtime,&guest->runtime,error)) return false;
    return true;
}

bool application_bots_guests_restore(application_bots *bots,qa_error *error)
{
    qa_session *session=bots->application->session;
    const char *name=qa_strings_cstr(qa_session_strings(session),bots->application->current_map);
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        qa_bot_runtime_saved_map saved={0};
        if(!qa_bot_runtime_save_map_read(guest->saved_runtime,&saved,error)) return false;
        qa_bot_runtime_map map={.name=name,.entities=saved.entities?&bots->entities:NULL,
            .source_entities=saved.source?bots->geometry.lumps[QA_BSP_ENTITIES].bytes:(qa_bytes){0},
            .navigation=saved.navigation?bots->map_navigation:NULL};
        bool okay=(!saved.name || (name && !strcmp(saved.name,name))) &&
            qa_bot_runtime_save_restore(session,guest->runtime,guest->saved_runtime,saved.name?&map:NULL,error);
        qa_bot_runtime_saved_map_free(&saved);
        if(!okay) {
            if(!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Actual guest bot map differs from its retained source");
            return false;
        }
    }
    return true;
}

void application_bots_guests_finish(application_bots *bots)
{
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        guest->saved_runtime=(qa_bytes){0};qa_bots_save_requirements_free(&guest->saved_requirements);
    }
}

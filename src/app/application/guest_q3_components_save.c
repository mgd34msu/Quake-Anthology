#include "guest_q3_components_private.h"
#include "guest_q3_component_private.h"
#include "qa/persistence_fields.h"
#include "qa/binary.h"
#include <limits.h>

static bool prefix(application_q3_components *owner,qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','G','C','P'};
    uint64_t world=owner->options.world_source->owner;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,"QGCP",4)||
        !qa_source_save_u64(io,&world)||world!=owner->options.world_source->owner||
        !qa_source_save_count(io,count,UINT32_MAX))
        return application_fail(io->error,QA_ERROR_FORMAT,"Component roster differs from its actual WORLD source");
    return true;
}
static bool fields(qa_source_save_io *io,component_saved_row *row)
{
    size_t size=row->game.size;
    if(!qa_source_save_text(io,&row->instance)||!row->instance||!qa_source_save_text(io,&row->key)||!row->key||
        !qa_source_save_u64(io,&row->owner)||!row->owner||!qa_source_save_u64(io,&row->generation)||!row->generation||
        !qa_source_save_u64(io,&row->services)||!row->services||row->services==row->owner||
        !qa_source_save_count(io,&size,UINT32_MAX)||!size) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        row->game.data=malloc(size); row->game.size=size;
        if(!row->game.data) return application_fail(io->error,QA_ERROR_MEMORY,"Retaining physical component continuation");
    }
    return qa_source_save_bytes(io,row->game.data,size);
}
bool q3components_saved_read(application_q3_components *owner,qa_bytes bytes,qa_error *e)
{
    qa_source_save_io io={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,owner->options.application->session,bytes,e)&&prefix(owner,&io,&count);
    if(ok) {
        if(count>qa_launch_snapshot_choices(owner->options.snapshot)->mod_count||count>SIZE_MAX/sizeof(*owner->saved))
            ok=application_fail(e,QA_ERROR_FORMAT,"Saved components exceed the actual enabled declaration roster");
        else if(count) {
            owner->saved=calloc(count,sizeof(*owner->saved));
            if(!owner->saved) ok=application_fail(e,QA_ERROR_MEMORY,"Retaining restored component chronology");
        }
        owner->saved_count=count;
    }
    for(size_t i=0;ok&&i<count;++i) {
        component_saved_row *row=owner->saved+i;
        ok=fields(&io,row);
        for(size_t j=0;ok&&j<i;++j) {
            component_saved_row *other=owner->saved+j;
            if(row->owner==other->owner||row->services==other->services||row->owner==other->services||row->services==other->owner||
                (!strcmp(row->instance,other->instance)&&!strcmp(row->key,other->key)))
                ok=application_fail(e,QA_ERROR_FORMAT,"Saved component roster duplicates an actual physical owner");
        }
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
static bool identity(void *context,qa_console_save_identity kind,uint64_t saved,uint64_t *out,qa_error *e)
{
    component_game_row *row=context;
    if(saved&&(kind!=QA_CONSOLE_SAVE_OWNER||saved!=row->publication.owner))
        return application_fail(e,QA_ERROR_FORMAT,"Component console identity differs from its retained namespace");
    *out=saved; return true;
}
static bool command_context(void *context,uint64_t registry,const qa_command_context *saved,qa_command_context *out,qa_error *e)
{
    component_game_row *row=context; qa_session *session=row->roster->options.application->session;
    qa_command_context actual;
    if(!registry||saved->session||saved->client||saved->owner!=row->publication.owner||saved->dialect!=QA_RULESET_Q3)
        return application_fail(e,QA_ERROR_FORMAT,"Component queued command lost its saved physical context");
    if(!row->publication.game||!qa_q3_host_console(row->publication.game->host,NULL,&actual))
        return application_fail(e,QA_ERROR_FORMAT,"Component queued command lost its actual restored host");
    *out=*saved;
    out->session=actual.session;
    out->cvar_view=actual.cvar_view;
    if(saved->registry==registry) out->registry=qa_actors_identity(qa_session_actors(session));
    else if(saved->registry) out->registry=0;
    return true;
}
bool q3components_saved_import(component_game_row *row,qa_error *e)
{
    application_q3_components *owner=row->roster;
    for(size_t i=0;i<owner->saved_count;++i) {
        component_saved_row *saved=owner->saved+i;
        if(saved->owner!=row->publication.owner) continue;
        qa_console_save_resolvers resolver={.context=row,.identity=identity,.command_context=command_context};
        bool ok=application_q3_component_restore(row->publication.game,(qa_bytes){saved->game.data,saved->game.size},&resolver,e);
        if(ok) row->initialized=true;
        return ok;
    }
    return application_fail(e,QA_ERROR_FORMAT,"Restored component has no actual saved physical row");
}
bool application_q3_components_checkpoint(qa_application *app,qa_buffer *out,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!out||out->data||out->size||(owner&&(owner->closing||owner->options.restoring||!application_q3_components_idle(owner))))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component capture requires its returned installed roster");
    application_q3_components empty={.options={.application=app,.world_source=application_world_provider(app,QA_ROLE_ENTITIES,"")}};
    if(!owner) owner=&empty;
    if(!owner->options.world_source) return application_fail(e,QA_ERROR_ARGUMENT,"Component capture lost its actual WORLD source");
    qa_source_save_io io={0}; size_t count=owner->count;
    bool ok=qa_source_save_writer(&io,owner->options.application->session,e)&&prefix(owner,&io,&count);
    for(size_t i=0;ok&&i<count;++i) {
        component_game_row *row=owner->rows[i];
        component_saved_row saved={.instance=row->publication.descriptor->selection.instance,.key=row->publication.metadata->key,
            .owner=row->publication.owner,.generation=row->publication.generation,.services=row->services};
        ok=row->attached&&row->initialized&&application_q3_component_checkpoint(row->publication.game,&saved.game,e)&&
            fields(&io,&saved);
        qa_buffer_free(&saved.game);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_components_finish_restore(application_q3_components *owner,qa_error *e)
{
    if(!owner||!owner->options.restoring||owner->closing||!application_q3_components_idle(owner))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component activation requires its real restored physical roster");
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(row->activated&&row->registered) continue;
        if(!row->attached||!row->initialized||!application_q3_component_finish_restore(row->publication.game,e)) return false;
        row->activated=row->registered=true;
    }
    owner->options.restoring=false; return true;
}

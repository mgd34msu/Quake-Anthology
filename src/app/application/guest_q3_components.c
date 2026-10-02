#include "guest_q3_components_private.h"
#include "qa/json.h"
#include <limits.h>
#include <stdio.h>

bool q3components_storage(void *context)
{
    component_game_row *row=context;
    return row&&row->roster&&row->provider&&row->provider->constructed&&row->metadata_lease&&
        row->publication.descriptor==qa_launch_instance_lease_view(row->metadata_lease)&&
        row->publication.content==row->publication.descriptor->content&&
        row->roster->options.application->session&&row->roster->options.world&&row->program&&row->declaration;
}
bool q3components_current(void *context)
{ component_game_row *row=context; return q3components_storage(row)&&!row->roster->closing&&!row->provider->close_pending; }
static bool frame(void *context,qa_session *session,const qa_source_frame *source,qa_error *e)
{
    component_game_row *row=context;
    return session==row->roster->options.application->session&&row->attached&&row->initialized&&
        application_q3_component_frame(row->publication.game,source,e);
}
static application_provider *provider(application_q3_components *owner,const char *instance)
{
    for(size_t i=0;i<owner->options.provider_count;++i) {
        application_provider *row=owner->options.providers[i];
        if(row&&row->launch&&!strcmp(row->launch->selection.instance,instance)) return row;
    }
    return NULL;
}
static bool selected_row(application_q3_components *owner,component_game_row *row)
{
    const qa_launch_choices *choices=qa_launch_snapshot_choices(owner->options.snapshot);
    for(size_t i=0;i<choices->mod_count;++i) {
        const qa_launch_mod_selection *selection=choices->mods+i;
        if(!selection->enabled||strcmp(selection->instance,row->publication.descriptor->selection.instance)||
            strcmp(selection->component,row->publication.metadata->key)) continue;
        application_provider *selected=provider(owner,selection->instance);
        const qa_catalog_mod *metadata=selected?qa_catalog_mod_find(selected->product_catalog,selection->component):NULL;
        return selected==row->provider&&metadata&&!metadata->unavailable&&metadata->runtime==QA_PROGRAM_QVM&&
            qa_sha256_equal(&metadata->program_digest,&row->publication.metadata->program_digest)&&
            qa_sha256_equal(&metadata->declaration_digest,&row->publication.metadata->declaration_digest)&&
            qa_sha256_equal(&selected->launch->identity,&row->publication.descriptor->identity);
    }
    return false;
}
static bool namespace(component_game_row *row,qa_error *e)
{
    qa_strings *strings=qa_session_strings(row->roster->options.application->session);
    char digest[65]; qa_sha256_hex(&row->publication.metadata->declaration_digest,digest);
    const char *component=row->publication.metadata->key;
    size_t size=strlen(component)+strlen(row->provider->launch->selection.instance)+160;
    char *name=malloc(size),*service=malloc(size+16);
    if(!name||!service) { free(name); free(service); return application_fail(e,QA_ERROR_MEMORY,"Retaining component namespace identity"); }
    bool ok=false;
    for(uint64_t generation=1;generation;++generation) {
        int n=snprintf(name,size,"qvm-component:%s:%s:%s:%llu",row->provider->launch->selection.instance,component,digest,(unsigned long long)generation);
        if(n<0||(size_t)n>=size) break;
        if(qa_strings_find(strings,(qa_bytes){(const uint8_t *)name,(size_t)n})) continue;
        qa_string_id owner,services;
        snprintf(service,size+16,"%s:services",name);
        ok=qa_strings_intern_cstr(strings,name,&owner,e)&&qa_strings_intern_cstr(strings,service,&services,e);
        if(ok) { row->publication.owner=owner; row->publication.generation=generation; row->services=services; }
        break;
    }
    free(name); free(service);
    return ok||(e&&e->code!=QA_OK?false:application_fail(e,QA_ERROR_FORMAT,"Component namespace generations are exhausted"));
}
bool application_q3_components_create(const application_q3_components_options *options,application_q3_components **out,qa_error *e)
{
    if(!options||!out||*out||!options->application||!options->snapshot||!options->world||!options->world_source||
        (options->provider_count&&!options->providers)||!options->application->mod_operations)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component roster requires actual application/content/world owners");
    application_q3_components *owner=calloc(1,sizeof(*owner));
    if(!owner) return application_fail(e,QA_ERROR_MEMORY,"Retaining external component roster");
    owner->options=*options; *out=owner;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(options->snapshot);
    owner->rows=choices->mod_count?calloc(choices->mod_count,sizeof(*owner->rows)):NULL;
    owner->retained=choices->mod_count?calloc(choices->mod_count,sizeof(*owner->retained)):NULL;
    if(choices->mod_count&&(!owner->rows||!owner->retained)) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual enabled component rows");
    if(options->previous) {
        if(options->previous->closing||options->previous->options.world!=options->world||!application_q3_components_idle(options->previous))
            return application_fail(e,QA_ERROR_ARGUMENT,"Retained components require their same returned WORLD owner");
        for(size_t i=0;i<options->previous->count;++i) {
            component_game_row *row=options->previous->rows[i];
            if(row&&selected_row(owner,row)) { owner->rows[owner->count]=row; owner->retained[owner->count++]=true; }
        }
    }
    for(size_t i=0;i<choices->mod_count;++i) {
        const qa_launch_mod_selection *selection=choices->mods+i;
        if(!selection->enabled) continue;
        application_provider *selected=provider(owner,selection->instance);
        if(!selected||!selected->constructed) return application_fail(e,QA_ERROR_ARGUMENT,"Enabled component has no constructed selected content provider");
        const qa_catalog_mod *metadata=qa_catalog_mod_find(selected->product_catalog,selection->component);
        if(!metadata||metadata->unavailable) return application_fail(e,QA_ERROR_UNSUPPORTED,"Enabled component has no genuine available catalog declaration");
        if(metadata->runtime!=QA_PROGRAM_QVM) continue;
        bool retained=false;
        for(size_t j=0;j<owner->count;++j) if(owner->rows[j]->provider==selected&&!strcmp(owner->rows[j]->publication.metadata->key,metadata->key)) retained=true;
        if(retained) continue;
        component_game_row *row=calloc(1,sizeof(*row));
        if(!row) return application_fail(e,QA_ERROR_MEMORY,"Retaining physical component GAME row");
        owner->rows[owner->count++]=row; row->roster=owner; row->provider=selected; row->publication.metadata=metadata;
        if(!qa_launch_instance_retain_metadata(selected->launch,&row->metadata_lease,e)) return false;
        row->publication.descriptor=qa_launch_instance_lease_view(row->metadata_lease); row->publication.content=row->publication.descriptor->content;
        if(!qa_vfs_acquire(row->publication.content,metadata->program_path,&row->program,&row->program_acquisition,e)||
            !qa_vfs_acquire(row->publication.content,metadata->declaration_path,&row->declaration,&row->declaration_acquisition,e)) return false;
        row->publication.program=row->program; row->publication.declaration=row->declaration;
        if(!qa_sha256_equal(qa_resource_digest(row->program),&metadata->program_digest)||!qa_sha256_equal(qa_resource_digest(row->declaration),&metadata->declaration_digest))
            return application_fail(e,QA_ERROR_FORMAT,"Enabled component resources differ from the actual catalog discovery");
        if(!qa_qvm_image_load(qa_resource_bytes(row->program),&row->image,e)||!namespace(row,e)||!q3components_identity(row,e)||!q3components_create_game(row,e)) return false;
        row->participant=(qa_component){.owner=row->publication.owner,.clock=qa_clock_defaults(QA_CLOCK_Q3),.state=row,.begin_frame=frame};
        row->participant.clock.initial_time_ns=options->world_source->component.clock.initial_time_ns;
    }
    return true;
}
bool application_q3_components_prepare(application_q3_components *owner,qa_error *e)
{
    if(!owner||owner->closing||!application_q3_components_idle(owner)) return application_fail(e,QA_ERROR_ARGUMENT,"Component preparation requires its real idle roster");
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(owner->retained[i]) continue;
        if(row->attached||row->admission||!qa_session_prepare_component(owner->options.application->session,&row->participant,0,&row->admission,e)) return false;
    }
    return true;
}
bool application_q3_components_commit(application_q3_components *owner,qa_error *e)
{
    if(!owner||owner->closing) return false;
    for(size_t i=0;i<owner->count;++i) if(!owner->retained[i]&&(!owner->rows[i]->admission||!qa_component_admission_validate(owner->rows[i]->admission,e))) return false;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(owner->retained[i]) continue;
        if(!qa_component_admission_commit(row->admission,e)) return false;
        row->admission=NULL; row->attached=true;
        qa_clock_state clock;
        if(!qa_session_clock(owner->options.application->session,owner->options.world_source->owner,&clock))
            return application_fail(e,QA_ERROR_ARGUMENT,"Component registration lost its actual shared WORLD clock");
        clock.frame.provider=row->publication.owner; clock.frame.kind=QA_CLOCK_Q3;
        if(!qa_session_restore_clock(owner->options.application->session,row->publication.owner,&clock,e)) return false;
    }
    return true;
}
bool application_q3_components_initialize(application_q3_components *owner,qa_error *e)
{
    if(!owner||owner->closing) return false;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(!row->initialized) {
            if(!row->attached||!application_q3_component_initialize(row->publication.game,e)) return false;
            row->initialized=true;
        }
        if(!row->activated) {
            if(!application_q3_component_activate(row->publication.game,e)) return false;
            row->activated=true;
        }
        if(!row->registered) {
            if(!application_q3_component_callbacks_register(row->publication.game,e)) return false;
            row->registered=true;
        }
    }
    return true;
}
bool application_q3_components_idle(const application_q3_components *owner)
{
    if(!owner) return true;
    for(size_t i=0;i<owner->count;++i) if(owner->rows[i]&&(!q3components_scenes_idle(owner->rows[i])||(owner->rows[i]->publication.game&&!application_q3_component_idle(owner->rows[i]->publication.game)))) return false;
    return true;
}
bool application_q3_components_destroy(application_q3_components **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    application_q3_components *owner=*slot; owner->closing=true;
    if(owner->retired&&!application_q3_components_destroy(&owner->retired,e)) return false;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(!row) continue;
        if(owner->retained[i]) continue;
        if(!q3components_scenes_destroy(row,e)||!application_q3_component_destroy(&row->publication.game,e)) return false;
        row->publication.source=NULL;
        if(row->attached) {
            bool removed=qa_session_remove(owner->options.application->session,row->publication.owner,e); qa_clock_state clock;
            if(removed||!qa_session_clock(owner->options.application->session,row->publication.owner,&clock)) row->attached=false;
            if(!removed) return false;
        }
        qa_component_admission_abort(row->admission); row->admission=NULL;
        qa_qvm_image_release(row->image); row->image=NULL;
        qa_resource_release(row->program); row->program=NULL; qa_resource_release(row->declaration); row->declaration=NULL;
        qa_vfs_acquisition_dispose(&row->program_acquisition); qa_vfs_acquisition_dispose(&row->declaration_acquisition);
        qa_launch_instance_lease_release(row->metadata_lease); row->metadata_lease=NULL;
        free(row->presentation_runtime); row->presentation_runtime=NULL;
        qa_unified_document_destroy(row->identity); row->identity=NULL;
        free(row); owner->rows[i]=NULL;
    }
    free(owner->retained); free(owner->rows); free(owner); *slot=NULL; return true;
}
bool application_q3_components_adopt(qa_application *app,application_q3_components **slot,qa_error *e)
{
    if(!app||!slot||!*slot||(*slot)->options.application!=app) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption lost its actual application owner");
    application_q3_components *owner=*slot,*previous=app->components;
    if(owner->options.previous&&owner->options.previous!=previous) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption changed its retained roster");
    if(previous&&!application_q3_components_idle(previous)) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption retains active old output");
    for(size_t i=0;i<owner->count;++i) if(owner->retained[i]) {
        component_game_row *row=owner->rows[i]; bool found=false;
        for(size_t j=0;previous&&j<previous->count;++j) if(previous->rows[j]==row) { previous->rows[j]=NULL; found=true; break; }
        if(!found) return application_fail(e,QA_ERROR_FORMAT,"Retained component adoption lost its physical row");
        row->roster=owner; owner->retained[i]=false;
    }
    owner->options.previous=NULL; owner->retired=previous; app->components=owner; *slot=NULL;
    return !owner->retired||application_q3_components_destroy(&owner->retired,e);
}
size_t application_q3_components_count(const qa_application *app)
{ return app&&app->components&&!app->components->closing?app->components->count:0; }
bool application_q3_components_at(qa_application *app,size_t index,application_q3_component **out,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!out||index>=owner->count||!owner->rows[index]->attached||!owner->rows[index]->initialized||!q3components_current(owner->rows[index]))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component index does not name a genuine installed GAME owner");
    *out=owner->rows[index]->publication.game; return true;
}
size_t application_q3_components_publication_count(const application_q3_components *owner)
{ return owner&&!owner->closing?owner->count:0; }
bool application_q3_components_publication_at(application_q3_components *owner,size_t index,application_q3_component_publication *out,qa_error *e)
{
    if(!owner||!out||index>=owner->count||!q3components_current(owner->rows[index])||!owner->rows[index]->attached||!owner->rows[index]->initialized)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component publication has no actual initialized source");
    *out=owner->rows[index]->publication; return true;
}
bool application_q3_components_admit(application_q3_components *owner,qa_actor_id actor,qa_error *e)
{
    if(!owner||owner->closing) return false;
    for(size_t i=0;i<owner->count;++i) {
        application_q3_component *game=owner->rows[i]->publication.game;
        uint32_t maximum; const char *entity,*player;
        if(application_q3_mod_clients(application_q3_component_profile(game),&maximum,&entity,&player)&&
            !application_q3_component_admit(game,actor,e)) return false;
    }
    return true;
}
bool application_q3_components_actor_released(application_q3_components *owner,qa_actor_record actor,qa_error *e)
{
    if(!owner) return true;
    for(size_t i=0;i<owner->count;++i) if(owner->rows[i]&&owner->rows[i]->publication.game&&!application_q3_component_actor_released(owner->rows[i]->publication.game,actor,e)) return false;
    return true;
}
bool application_q3_components_content_visit(const application_q3_components *owner,const qa_application_content_visitor *visitor,qa_error *e)
{
    if(!owner||!visitor||!visitor->view||!visitor->catalog) return false;
    for(size_t i=0;i<owner->count;++i) {
        const component_game_row *row=owner->rows[i];
        if(!visitor->catalog(visitor->context,row->provider->product_catalog,e)||!visitor->view(visitor->context,row->publication.content,e)) return false;
    }
    return true;
}

#include "guest_q3_components_private.h"
#include "qa/json.h"
#include "unified_events.h"
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
            metadata->program_resource == row->program&&
            metadata->declaration_resource == row->declaration&&
            (selected->launch->identity == row->publication.descriptor->identity);
    }
    return false;
}
static bool namespace(component_game_row *row,qa_error *e)
{
    qa_strings *strings=qa_session_strings(row->roster->options.application->session);
    const char *component=row->publication.metadata->key;
    size_t size=strlen(component)+strlen(row->provider->launch->selection.instance)+160;
    char *name=malloc(size),*service=malloc(size+16);
    if(!name||!service) { free(name); free(service); return application_fail(e,QA_ERROR_MEMORY,"Retaining component namespace identity"); }
    bool ok=false;
    if(row->roster->options.restoring) {
        for(size_t i=0;i<row->roster->saved_count;++i) {
            component_saved_row *saved=row->roster->saved+i;
            if(strcmp(saved->instance,row->provider->launch->selection.instance)||strcmp(saved->key,component)) continue;
            int n=snprintf(name,size,"qvm-component:%s:%s:%llu",saved->instance,component,(unsigned long long)saved->generation);
            snprintf(service,size+16,"%s:services",name);
            ok=saved->owner<=UINT32_MAX&&n>=0&&(size_t)n<size&&qa_strings_find(strings,(qa_bytes){(const uint8_t *)name,(size_t)n})==saved->owner&&
                qa_strings_find(strings,(qa_bytes){(const uint8_t *)service,strlen(service)})==saved->services;
            if(ok) { row->publication.owner=(qa_actor_owner)saved->owner; row->publication.generation=saved->generation; row->services=saved->services; }
            break;
        }
        free(name); free(service);
        return ok||application_fail(e,QA_ERROR_FORMAT,"Saved component namespace differs from its exact retained declaration");
    }
    for(uint64_t generation=1;generation;++generation) {
        int n=snprintf(name,size,"qvm-component:%s:%s:%llu",row->provider->launch->selection.instance,component,(unsigned long long)generation);
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
    qa_launch_snapshot_retain(options->snapshot);
    if(options->entity_text.size) {
        owner->entity_text.data=malloc(options->entity_text.size);
        if(!owner->entity_text.data) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component map entity bytes");
        memcpy(owner->entity_text.data,options->entity_text.data,options->entity_text.size);
        owner->entity_text.size=options->entity_text.size;
    }
    owner->options.entity_text=(qa_bytes){owner->entity_text.data,owner->entity_text.size};
    if(options->previous) {
        if(options->previous->options.world_source!=options->world_source||!options->previous->clients_adapter)
            return application_fail(e,QA_ERROR_ARGUMENT,"Retained component clients changed their physical WORLD source");
        owner->clients_adapter=options->previous->clients_adapter;
    } else {
        if(!application_q3_component_client_adapter_create(options->application,options->world_source,options->world,&owner->clients_adapter,e)) return false;
        owner->owns_clients=true;
    }
    owner->options.clients=application_q3_component_client_adapter_services(owner->clients_adapter);
    if(options->application->q3_component_client_drop&&
        !application_q3_component_client_adapter_transport_bind(owner->clients_adapter,
            options->application->guest_context,options->application->q3_component_client_drop,e)) return false;
    owner->options.context=owner->clients_adapter;
    owner->options.match_read=application_q3_component_client_match_read;
    owner->options.match_write=application_q3_component_client_match_write;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(options->snapshot);
    if(options->restoring&&!q3components_saved_read(owner,options->saved,e)) return false;
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
    size_t loop_count=options->restoring?owner->saved_count:choices->mod_count;
    for(size_t i=0;i<loop_count;++i) {
        const qa_launch_mod_selection *selection=options->restoring?NULL:choices->mods+i;
        if(options->restoring) for(size_t j=0;j<choices->mod_count;++j) {
            const qa_launch_mod_selection *candidate=choices->mods+j;
            if(candidate->enabled&&!strcmp(candidate->instance,owner->saved[i].instance)&&!strcmp(candidate->component,owner->saved[i].key)) { selection=candidate; break; }
        }
        if(!selection) return application_fail(e,QA_ERROR_FORMAT,"Restored component is absent from the actual enabled choices");
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
        row->publication.catalog=selected->product_catalog; row->publication.product=selected->product;
        if(!qa_launch_instance_retain_metadata(selected->launch,&row->metadata_lease,e)) return false;
        row->publication.descriptor=qa_launch_instance_lease_view(row->metadata_lease); row->publication.content=row->publication.descriptor->content;
        if(!qa_vfs_acquire_receipt(row->publication.content,metadata->program_path,&row->program,&row->program_acquisition,e)||
            !qa_vfs_acquire_receipt(row->publication.content,metadata->declaration_path,&row->declaration,&row->declaration_acquisition,e)) return false;
        row->publication.program=row->program; row->publication.declaration=row->declaration;
        if(row->program != metadata->program_resource || row->declaration != metadata->declaration_resource)
            return application_fail(e,QA_ERROR_FORMAT,"Enabled component resources differ from the actual catalog discovery");
        if(!qa_qvm_image_load(qa_resource_bytes(row->program),&row->image,e)||!namespace(row,e)||!q3components_identity(row,e)||!q3components_create_game(row,e)||
            (options->restoring&&!q3components_saved_import(row,e))) return false;
        row->participant=(qa_component){.owner=row->publication.owner,.clock=qa_clock_defaults(QA_CLOCK_Q3),.state=row,.begin_frame=frame};
        row->participant.clock.initial_time_ns=options->world_source->component.clock.initial_time_ns;
    }
    if(options->restoring) {
        size_t expected=0;
        for(size_t i=0;i<choices->mod_count;++i) if(choices->mods[i].enabled) {
            application_provider *selected=provider(owner,choices->mods[i].instance);
            const qa_catalog_mod *metadata=selected?qa_catalog_mod_find(selected->product_catalog,choices->mods[i].component):NULL;
            if(metadata&&metadata->runtime==QA_PROGRAM_QVM) ++expected;
        }
        if(expected!=owner->count) return application_fail(e,QA_ERROR_FORMAT,"Saved component chronology omits an enabled physical QVM owner");
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
        if(owner->options.restoring) continue;
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
            if(!row->attached) return false;
            row->initializing=true;
            bool initialized=application_unified_event_component_owner_bind(owner->options.application,row->publication.owner,false,e)&&
                application_q3_component_initialize(row->publication.game,e);
            row->initializing=false;
            if(!initialized) return false;
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
    if(owner->video&&!owner->video_entering) return false;
    if(!application_q3_component_client_adapter_idle(owner->clients_adapter)) return false;
    for(size_t i=0;i<owner->count;++i) if(owner->rows[i]&&(!q3components_scenes_idle(owner->rows[i])||(owner->rows[i]->publication.game&&!application_q3_component_idle(owner->rows[i]->publication.game)))) return false;
    return true;
}
bool application_q3_components_destroy(application_q3_components **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    application_q3_components *owner=*slot;
    if(owner->video) return application_fail(e,QA_ERROR_ARGUMENT,"Component retirement retains its real video reconstruction ticket");
    if(!application_q3_component_client_adapter_idle(owner->clients_adapter))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component retirement retains client commands or deferred drops and their actual content owners");
    owner->closing=true;
    if(owner->retired&&!application_q3_components_destroy(&owner->retired,e)) return false;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(!row) continue;
        if(owner->retained[i]) continue;
        if(!row->retirement_clock_held) {
            qa_clock_state clock;
            if(qa_session_clock(owner->options.application->session,owner->options.world_source->owner,&clock)) {
                row->retirement_clock=clock.frame; row->retirement_clock_held=true;
            }
        }
        row->destroying=true;
        bool destroyed=q3components_scenes_destroy(row,e)&&application_q3_component_destroy(&row->publication.game,e);
        row->destroying=false;
        if(!destroyed) return false;
        row->publication.source=NULL;
        if(!row->events_retired) {
            if(!application_unified_event_owner_retire(owner->options.application,row->publication.owner,
                row->retirement_clock_held?&row->retirement_clock:NULL,e)||
                !application_unified_event_registration_clear(owner->options.application,row->publication.owner,e)) return false;
            row->events_retired=true;
        }
        if(row->attached) {
            bool removed=qa_session_remove(owner->options.application->session,row->publication.owner,e); qa_clock_state clock;
            if(removed||!qa_session_clock(owner->options.application->session,row->publication.owner,&clock)) row->attached=false;
            if(!removed) return false;
        }
        qa_component_admission_abort(row->admission); row->admission=NULL;
        qa_qvm_image_release(row->image); row->image=NULL;
        qa_resource_release(row->program); row->program=NULL; qa_resource_release(row->declaration); row->declaration=NULL;
        qa_vfs_acquisition_dispose(&row->program_acquisition); qa_vfs_acquisition_dispose(&row->declaration_acquisition);
        qa_catalog_write_resolver_destroy(row->write_resolver); row->write_resolver=NULL;
        qa_launch_instance_lease_release(row->metadata_lease); row->metadata_lease=NULL;
        free(row->presentation_runtime); row->presentation_runtime=NULL;
        qa_unified_component_identity_dispose(&row->identity);
        row->publication.identity=NULL; row->publication.module=NULL;
        free(row); owner->rows[i]=NULL;
    }
    if(owner->owns_clients&&!application_q3_component_client_adapter_destroy(&owner->clients_adapter,e)) return false;
    qa_buffer_free(&owner->entity_text);
    for(size_t i=0;i<owner->saved_count;++i) qa_buffer_free(&owner->saved[i].game);
    free(owner->saved);
    qa_launch_snapshot_release(owner->options.snapshot);
    free(owner->retained); free(owner->rows); free(owner); *slot=NULL; return true;
}
bool application_q3_components_drain(application_q3_components *owner,qa_error *e)
{
    return !owner||application_q3_component_client_adapter_drain(owner->clients_adapter,e);
}
bool application_q3_components_adopt(qa_application *app,application_q3_components **slot,qa_error *e)
{
    if(!app||!slot||!*slot||(*slot)->options.application!=app) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption lost its actual application owner");
    application_q3_components *owner=*slot,*previous=app->components;
    if(owner->options.previous&&owner->options.previous!=previous) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption changed its retained roster");
    if(previous&&!application_q3_components_idle(previous)) return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption retains active old output");
    for(size_t i=0;i<owner->count;++i) if(owner->retained[i]) {
        bool found=false;
        for(size_t j=0;previous&&j<previous->count;++j) if(previous->rows[j]==owner->rows[i]) { found=true; break; }
        if(!found) return application_fail(e,QA_ERROR_FORMAT,"Retained component adoption lost its physical row");
    }
    if(!owner->owns_clients&&previous) {
        if(owner->clients_adapter!=previous->clients_adapter||!previous->owns_clients)
            return application_fail(e,QA_ERROR_ARGUMENT,"Component adoption lost its retained client services owner");
        previous->clients_adapter=NULL; previous->owns_clients=false; owner->owns_clients=true;
    }
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
bool application_q3_components_event_source_read(const qa_application *app,qa_actor_owner id,application_q3_component_publication *out,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    for(;owner&&out;owner=owner->retired) for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(row&&row->publication.owner==id&&row->attached&&(row->initialized||row->initializing)&&
            (q3components_current(row)||(row->destroying&&q3components_storage(row)))) {
            *out=row->publication; return true;
        }
    }
    return application_fail(e,QA_ERROR_NOT_FOUND,"Component event has no genuine installed or entered Init source owner");
}
bool application_q3_components_item_read(qa_application *app,qa_actor_id actor,qa_actor_owner id,qa_item_id item,
    application_q3_component_item_metadata *out,bool *found,qa_error *e)
{
    if(!app||!out||!found||!id||!item||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component item metadata requires its actual source and full actor");
    *found=false;
    application_q3_components *owner=app->components;
    for(size_t i=0;owner&&i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(!row||row->publication.owner!=id) continue;
        if(!row->initialized||!row->attached||!q3components_current(row))
            return application_fail(e,QA_ERROR_ARGUMENT,"Component item metadata lost its actual admitted source");
        application_q3_component_item_metadata value={.source=row->publication};
        if(!application_q3_component_item_read(row->publication.game,actor,item,&value.admission,&value.icon,&value.held,found,e)) return false;
        if(*found) *out=value;
        return true;
    }
    return true;
}
bool application_q3_components_checkpoint_publication_read(const qa_application *app,qa_actor_owner id,
    application_q3_component_publication *out,bool *found,qa_error *e)
{
    if(!app||!out||!found) return application_fail(e,QA_ERROR_ARGUMENT,"Component checkpoint lookup requires its real application");
    *found=false;
    application_q3_components *owner=app->components;
    if(!owner) return true;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(!row||row->publication.owner!=id) continue;
        if(owner->closing||!row->publication.game||!row->initialized||!q3components_storage(row))
            return application_fail(e,QA_ERROR_ARGUMENT,"Component checkpoint lost its imported physical continuation");
        *out=row->publication; *found=true; return true;
    }
    return true;
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
application_q3_component *application_q3_components_actor_owner(const qa_application *app,qa_actor_id actor)
{
    const qa_actor_record *actual=app?qa_actors_get(qa_session_actors(app->session),actor):NULL;
    application_q3_components *owner=app?app->components:NULL;
    if(!actual||!owner) return NULL;
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *row=owner->rows[i];
        if(row&&row->publication.owner==actual->owner&&q3components_storage(row)) return row->publication.game;
    }
    return NULL;
}
bool application_q3_components_content_visit(const application_q3_components *owner,const qa_application_content_visitor *visitor,qa_error *e)
{
    if(!visitor||!visitor->view||!visitor->catalog) return false;
    if(!owner) return true;
    for(size_t i=0;i<owner->count;++i) {
        const component_game_row *row=owner->rows[i];
        if(!visitor->catalog(visitor->context,row->provider->product_catalog,e)||!visitor->view(visitor->context,row->publication.content,e)) return false;
    }
    return true;
}

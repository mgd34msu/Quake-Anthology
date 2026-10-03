#include "guest_q3_components_private.h"
#include "guest_q3_component_private.h"
#include "unified_q3_events.h"
#include "qa/json.h"
#include "qa/application_q3_components.h"
#include "guest_q3_components_video.h"
#include "qa/cvars_save.h"
#include "qa/console_save.h"
#include "qa/persistence_fields.h"
#include <stdio.h>

typedef struct component_pending_event { application_q3_scene_player_event event; uint64_t sequence; } component_pending_event;
struct component_scene_row {
    component_scene_row *next;
    component_game_row *game;
    qa_actor_id viewer;
    uint32_t seat;
    qa_actor_owner services;
    application_q3_component_view view;
    application_q3_scene_source source;
    application_q3_scene *scene;
    application_q3_scene_profile *profile;
    qa_resource *artifact;
    qa_vfs_acquisition acquisition;
    qa_qvm_image *image;
    qa_cvars *cvars;
    qa_console *console;
    qa_q3_host_options host;
    qa_q3_presentation_assets *assets;
    application_q3_component_scene_frontend frontend;
    uint64_t sequence,frontend_identity;
    component_pending_event *events;
    size_t event_count,event_cursor;
    qa_buffer saved_scene,saved_cvars,saved_console;
    bool restore_pending,restore_constructed,restore_consoles,restore_imported;
    bool initialized,host_entered,advanced;
};
bool q3components_player_event(void *context,const application_q3_scene_player_event *event,uint64_t sequence,qa_error *e)
{
    component_game_row *game=context;
    if(!game||!event||!sequence||!q3components_current(game)) return application_fail(e,QA_ERROR_ARGUMENT,"Player event lost its admitted component Source");
    if(!application_unified_q3_component_player(game->roster->options.application,&game->publication,event,e)) return false;
    for(component_scene_row *row=game->scenes;row;row=row->next) {
        if(!row->profile||!row->profile->player_events||!row->initialized) continue;
        if(row->event_count==SIZE_MAX/sizeof(*row->events)) return application_fail(e,QA_ERROR_MEMORY,"Player event delivery queue overflows");
        component_pending_event *events=realloc(row->events,(row->event_count+1)*sizeof(*events));
        if(!events) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component CG player event delivery");
        row->events=events; events[row->event_count++]=(component_pending_event){*event,sequence};
    }
    return true;
}
static bool retained(void *context)
{ component_scene_row *row=context; return row&&q3components_storage(row->game); }
static bool published(void *context)
{ component_scene_row *row=context; return retained(row)&&row->game->roster->options.application->components==row->game->roster&&
    row->game->attached&&row->game->initialized&&q3components_current(row->game); }
static bool entered(void *context,application_q3_scene_context *out)
{ component_scene_row *row=context; return retained(row)&&application_q3_scene_entered_context(row->scene,out); }
static void print(void *context,const char *text)
{
    component_scene_row *row=context;
    qa_command_context command={.owner=row->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_LOCAL};
    application_console_print(row->game->roster->options.application,&command,text);
}
static void console_print(void *context,const qa_command_context *command,const char *text)
{ (void)command; print(context,text); }
static bool cheats(void *context)
{ component_scene_row *row=context; const qa_cvar_view *v=qa_cvars_find(row->cvars,"sv_cheats"); return v&&v->integer!=0; }
static bool active(void *context,const qa_command_context *command)
{
    component_scene_row *row=context;
    return command&&command->owner==row->services&&command->dialect==QA_CONSOLE_Q3&&q3components_current(row->game);
}
static bool capture(void *context,const qa_command_context *command,qa_command_context *out,qa_error *e)
{
    if(!active(context,command)) return application_fail(e,QA_ERROR_ARGUMENT,"Component scene console left its actual namespace");
    *out=*command; return true;
}
static qa_cvars *cvar_owner(void *context,const qa_command_context *command,const char *name)
{ (void)name; component_scene_row *row=context; return active(row,command)?row->cvars:NULL; }
static qa_command_result console_command(void *context,const qa_command_invocation *invocation,qa_error *e)
{
    component_scene_row *row=context;
    if(!row->initialized) return QA_COMMAND_UNHANDLED;
    qa_command_tokens tokens={.count=invocation->argc,.args_text=(char *)invocation->args_text};
    tokens.values=tokens.count?calloc(tokens.count,sizeof(*tokens.values)):NULL;
    if(tokens.count&&!tokens.values) { application_fail(e,QA_ERROR_MEMORY,"Retaining actual component console arguments"); return QA_COMMAND_FAILED; }
    for(size_t i=0;i<tokens.count;++i) tokens.values[i]=(char *)invocation->argv[i];
    bool handled=false;
    bool ok=application_q3_scene_console(row->scene,&tokens,&handled,e);
    free(tokens.values); return !ok?QA_COMMAND_FAILED:handled?QA_COMMAND_HANDLED:QA_COMMAND_UNHANDLED;
}
static bool scene_identity(component_scene_row *row,qa_error *e)
{
    qa_strings *strings=qa_session_strings(row->game->roster->options.application->session);
    const char *owner=qa_strings_cstr(strings,row->game->publication.owner);
    if(!owner) return application_fail(e,QA_ERROR_FORMAT,"Component scene lost its actual GAME namespace");
    size_t size=strlen(owner)+128; char *name=malloc(size);
    if(!name) return application_fail(e,QA_ERROR_MEMORY,"Retaining real component viewer namespace");
    int count=snprintf(name,size,"%s:client:%u:%llu:%llu:%u",owner,row->seat,(unsigned long long)row->viewer.registry,(unsigned long long)row->viewer.generation,row->viewer.slot);
    bool ok=count>=0&&(size_t)count<size&&qa_strings_intern_cstr(strings,name,&row->services,e);
    free(name); return ok;
}
static bool open_scene(component_scene_row *row,qa_error *e)
{
    component_game_row *game=row->game; application_q3_components_options *options=&game->roster->options;
    if(!row->profile) {
    qa_json_document *document=NULL;
    if(!qa_json_parse(qa_resource_bytes(game->declaration),&document,e)) return false;
    qa_json_id presentation=qa_json_get(document,qa_json_root(document),"presentation");
    if((!qa_json_string_equal(document,qa_json_get(document,presentation,"runtime"),"qvm-scene")&&!qa_json_string_equal(document,qa_json_get(document,presentation,"runtime"),"qvm-player-events"))) {
        qa_json_destroy(document); return application_fail(e,QA_ERROR_UNSUPPORTED,"Component has no declared original scene presentation");
    }
    qa_buffer path={0};
    bool ok=qa_json_string(document,qa_json_get(document,qa_json_get(document,presentation,"cgame"),"path"),&path,e);
    char *normalized=ok&&!memchr(path.data,0,path.size)?qa_vfs_normalize_path((char *)path.data,e):NULL;
    qa_buffer_free(&path);
    if(ok) ok=normalized&&qa_vfs_acquire_receipt(game->publication.content,normalized,&row->artifact,&row->acquisition,e)&&
        qa_qvm_image_load(qa_resource_bytes(row->artifact),&row->image,e)&&
        application_q3_scene_profile_create(row->image,game->publication.abi,normalized,game->image,
            game->publication.metadata->program_path,qa_json_source(document,presentation),&row->profile,e);
    free(normalized); qa_json_destroy(document);
    if(!ok) return false;
    }
    qa_actor_owner saved_services=row->services;
    if(!scene_identity(row,e)) return false;
    if(row->restore_pending&&row->services!=saved_services)
        return application_fail(e,QA_ERROR_FORMAT,"Restored component CG service namespace differs from its actual viewer");
    qa_cvar_options cvars={.dialect=QA_CONSOLE_Q3,.user=row,.print=print,.cheats_allowed=cheats};
    if(!row->cvars) row->cvars=qa_cvars_create(&cvars,e);
    if(!row->cvars) return false;
    qa_console_options console={.context={.owner=row->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_LOCAL},
        .cvars=row->cvars,.user=row,.print=console_print,.cvar_owner=cvar_owner,.source_command=console_command,
        .capture_context=capture,.context_active=active};
    if(!row->console) row->console=qa_console_create(&console,e);
    if(!row->console) return false;
    row->host=(qa_q3_host_options){.role=QA_QVM_CGAME,.abi=game->publication.abi,.session=options->application->session,
        .world=options->world,.owner=game->publication.owner,.service_owner=row->services,.mounts=game->publication.content,
        .write_view={.root=qa_catalog_write_resolver_root(game->write_resolver),
            .resolver=qa_catalog_write_resolver_services(game->write_resolver)},
        .cvars=row->cvars,.console=row->console,.command_context={.owner=row->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_LOCAL},.common={.context=row,.print=print}};
    if(!options->scene_factory.prepare) return application_fail(e,QA_ERROR_UNSUPPORTED,"Component scene lacks its real renderer factory");
    application_q3_component_scene_preparation preparation={.component=game->publication.metadata,.descriptor=game->publication.descriptor,
        .restoring=row->restore_pending,.frontend_identity=row->restore_pending?row->frontend_identity:0,
        .catalog=game->provider->product_catalog,.owner=game->publication.owner,.generation=game->publication.generation,
        .service_owner=row->services,.physical_seat=row->seat,.viewer=row->viewer,.content=game->publication.content,
        .artifact=row->artifact,.acquisition=&row->acquisition,.profile=row->profile,.source=row->source,
        .host=&row->host,.assets=&row->assets,.frontend=&row->frontend,.context=row,.retained=retained,.published=published,.publication_read=entered};
    if(!options->scene_factory.prepare(options->scene_factory.context,&preparation,e)) return false;
    if(!row->frontend.owner||!row->frontend.idle||!row->frontend.destroy||!row->frontend.begin||!row->frontend.finish||!row->frontend.completed||!row->assets)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component scene factory omitted its actual renderer lifetime");
    if(!row->frontend.identity_read||!row->frontend.identity_read(row->frontend.owner,&row->frontend_identity)||!row->frontend_identity)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component scene factory omitted its actual physical identity");
    application_q3_scene_options create={.profile=row->profile,.host=row->host,.assets=row->assets,.viewer=row->viewer,.source=row->source,
        .output_context=row->frontend.owner,.finish_output=row->frontend.finish};
    bool created=application_q3_scene_create(&create,row->restore_pending,&row->scene,e);
    row->host_entered=row->scene!=NULL;
    if(!created) return false;
    if(row->restore_pending) { row->restore_constructed=true; return true; }
    if(!row->frontend.begin(row->frontend.owner,0,e)||!application_q3_scene_initialize(row->scene,e)) return false;
    row->initialized=true; return true;
}
bool application_q3_components_scene_prepare(application_q3_components *owner,size_t index,uint32_t seat,
    qa_actor_id viewer,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time,int32_t elapsed,application_q3_scene **out,qa_error *e)
{
    application_q3_component_publication publication;
    if(!out||!origin||!axis||!qa_vec_finite(*origin)||!qa_vec_finite(axis[0])||!qa_vec_finite(axis[1])||!qa_vec_finite(axis[2])||time<0||elapsed<0||
        !application_q3_components_publication_at(owner,index,&publication,e)) return false;
    if(owner->video&&!owner->video_entering) return application_fail(e,QA_ERROR_ARGUMENT,"Component Draw retains its real video reconstruction");
    component_game_row *game=owner->rows[index]; component_scene_row *row=game->scenes;
    while(row&&(row->seat!=seat||!qa_actor_id_equal(row->viewer,viewer))) row=row->next;
    if(!row) {
        row=calloc(1,sizeof(*row)); if(!row) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component viewer scene");
        row->game=game; row->viewer=viewer; row->seat=seat;
        row->next=game->scenes; game->scenes=row;
        row->view=(application_q3_component_view){.source=publication.source,.viewer=viewer,
            .weapon_presented=owner->options.weapon_presented,.weapon_context=owner->options.weapon_context};
        row->source=application_q3_component_view_services(&row->view);
    } else if(!row->initialized) return application_fail(e,QA_ERROR_ARGUMENT,"Component scene retains a failed physical constructor");
    row->view.origin=*origin; memcpy(row->view.axis,axis,sizeof(row->view.axis)); row->view.time_ms=time; row->view.frame_ms=elapsed;
    if(!row->scene&&!open_scene(row,e)) return false;
    *out=row->scene; return true;
}
bool application_q3_components_scene_advance(application_q3_components *owner,size_t index,uint32_t seat,
    qa_actor_id viewer,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time,int32_t elapsed,uint64_t sequence,
    application_q3_scene **scene,const qa_scene_frame **frame,qa_error *e)
{
    if(!frame||!application_q3_components_scene_prepare(owner,index,seat,viewer,origin,axis,time,elapsed,scene,e)) return false;
    component_scene_row *row=owner->rows[index]->scenes;
    while(row&&row->scene!=*scene) row=row->next;
    if(!row) return application_fail(e,QA_ERROR_ARGUMENT,"Component advance lost its actual viewer row");
    if(!row->advanced||sequence>row->sequence) {
        if(!row->frontend.begin(row->frontend.owner,sequence,e)) return false;
        while(row->event_cursor<row->event_count) {
            const component_pending_event *event=row->events+row->event_cursor;
            if(!application_q3_scene_consume(*scene,&event->event,event->sequence,e)) return false;
            ++row->event_cursor;
        }
        free(row->events); row->events=NULL; row->event_count=row->event_cursor=0;
        if(!application_q3_scene_advance(*scene,sequence,e)) return false;
        row->sequence=sequence; row->advanced=true;
    } else if(sequence!=row->sequence) return application_fail(e,QA_ERROR_ARGUMENT,"Component advance sequence moved backwards");
    return row->frontend.completed(row->frontend.owner,sequence,frame,e);
}
bool q3components_scenes_idle(const component_game_row *game)
{
    for(component_scene_row *row=game->scenes;row;row=row->next)
        if((row->scene&&!application_q3_scene_idle(row->scene))||(row->frontend.owner&&(!row->frontend.idle||!row->frontend.idle(row->frontend.owner)))) return false;
    return true;
}
bool q3components_scenes_destroy(component_game_row *game,qa_error *e)
{
    while(game->scenes) {
        component_scene_row *row=game->scenes;
        if(!application_q3_scene_destroy(&row->scene,e)) return false;
        if(!row->host_entered&&row->host.frontend_lifetime&&row->host.release_frontend) {
            row->host.release_frontend(row->host.frontend_lifetime); row->host.frontend_lifetime=NULL;
        }
        if(row->frontend.owner&&(!row->frontend.destroy||!row->frontend.destroy(&row->frontend.owner,e))) return false;
        qa_console_destroy(row->console); qa_cvars_destroy(row->cvars);
        application_q3_scene_profile_destroy(row->profile); qa_qvm_image_release(row->image);
        qa_resource_release(row->artifact); qa_vfs_acquisition_dispose(&row->acquisition);
        qa_buffer_free(&row->saved_scene); qa_buffer_free(&row->saved_cvars); qa_buffer_free(&row->saved_console);
        game->scenes=row->next; free(row->events); free(row);
    }
    return true;
}
size_t qa_application_q3_component_scene_count(const qa_application *app)
{
    application_q3_components *owner=app?app->components:NULL; size_t count=0;
    for(size_t i=0;owner&&!owner->closing&&i<owner->count;++i) {
        const char *runtime=owner->rows[i]->publication.presentation_runtime;
        if(runtime&&(!strcmp(runtime,"qvm-scene")||!strcmp(runtime,"qvm-player-events"))) ++count;
    }
    return count;
}
bool qa_application_q3_component_draw_prepare(qa_application *app,size_t ordinal,uint32_t seat,
    qa_actor_id viewer,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time,int32_t elapsed,
    uint64_t sequence,qa_application_q3_component_draw *out,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL; size_t index=0,found=0;
    if(!owner||owner->closing||!out) return application_fail(e,QA_ERROR_ARGUMENT,"Component draw has no actual installed roster");
    for(;index<owner->count;++index) {
        const char *runtime=owner->rows[index]->publication.presentation_runtime;
        if(runtime&&(!strcmp(runtime,"qvm-scene")||!strcmp(runtime,"qvm-player-events"))&&found++==ordinal) break;
    }
    if(index==owner->count) return application_fail(e,QA_ERROR_NOT_FOUND,"Component draw ordinal has no actual declared scene");
    application_q3_scene *scene=NULL; const qa_scene_frame *frame=NULL;
    if(!application_q3_components_scene_advance(owner,index,seat,viewer,origin,axis,time,elapsed,sequence,&scene,&frame,e)) return false;
    component_scene_row *row=owner->rows[index]->scenes;
    while(row&&row->scene!=scene) row=row->next;
    if(!row||!row->frontend_identity)
        return application_fail(e,QA_ERROR_FORMAT,"Component draw lacks its actual frontend identity");
    *out=(qa_application_q3_component_draw){.owner=row->game->publication.owner,.generation=row->game->publication.generation,
        .sequence=sequence,.frontend_identity=row->frontend_identity,.scene=scene,.bodies=application_q3_scene_bodies(scene),.assets=row->assets,.frame=frame};
    return row->profile->player_events||out->bodies!=NULL;
}
bool qa_application_q3_component_draw_current(const qa_application *app,const qa_application_q3_component_draw *draw)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!draw) return false;
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) {
        if(row->scene!=draw->scene) continue;
        return q3components_current(row->game)&&row->game->publication.owner==draw->owner&&row->game->publication.generation==draw->generation&&
            row->advanced&&row->sequence==draw->sequence&&row->assets==draw->assets&&application_q3_scene_bodies(row->scene)==draw->bodies&&
            row->frontend_identity==draw->frontend_identity;
    }
    return false;
}
static component_scene_row *hud_row(const qa_application *app,uint64_t identity,uint32_t seat,
    qa_actor_id viewer,uint64_t sequence,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!identity||!viewer.registry)
        { application_fail(e,QA_ERROR_ARGUMENT,"Component HUD has no actual installed viewer roster"); return NULL; }
    for(size_t i=0;i<owner->count;++i) {
        component_game_row *game=owner->rows[i];
        for(component_scene_row *row=game->scenes;row;row=row->next) {
            if(row->frontend_identity!=identity) continue;
            if(row->seat!=seat||!qa_actor_id_equal(row->viewer,viewer)||!game->attached||!game->initialized||
                !q3components_current(game)||!row->initialized||!row->advanced||row->sequence!=sequence||
                !row->scene||!row->profile||!row->frontend.idle(row->frontend.owner))
                { application_fail(e,QA_ERROR_ARGUMENT,"Component HUD lost its actual completed scene and viewer"); return NULL; }
            return row;
        }
    }
    application_fail(e,QA_ERROR_NOT_FOUND,"Component HUD identity has no actual admitted scene"); return NULL;
}
bool qa_application_q3_component_scene_hud(qa_application *app,uint64_t identity,uint32_t seat,
    qa_actor_id viewer,uint64_t sequence,qa_error *e)
{
    component_scene_row *row=hud_row(app,identity,seat,viewer,sequence,e);
    return row&&(!row->profile->has_hud||application_q3_scene_hud(row->scene,sequence,e));
}
bool qa_application_q3_component_scene_hud_read(const qa_application *app,uint64_t identity,uint32_t seat,
    qa_actor_id viewer,uint64_t sequence,bool *replace_status,qa_error *e)
{
    if(!replace_status) return application_fail(e,QA_ERROR_ARGUMENT,"Component HUD policy requires its output");
    component_scene_row *row=hud_row(app,identity,seat,viewer,sequence,e);
    if(!row) return false;
    *replace_status=row->profile->has_hud&&row->profile->replace_status; return true;
}
bool qa_application_q3_component_scene_association_read(const qa_application *app,uint64_t identity,
    qa_application_q3_component_scene_association *out)
{
    if(!app||!identity||!out) return false;
    for(application_q3_components *owner=app->components;owner;owner=owner->retired)
        for(size_t i=0;i<owner->count;++i) {
            component_game_row *game=owner->rows[i]; if(!game) continue;
            for(component_scene_row *row=game->scenes;row;row=row->next) {
                if(row->frontend_identity!=identity||!row->frontend.owner||!row->assets) continue;
                *out=(qa_application_q3_component_scene_association){.owner=game->publication.owner,
                    .service_owner=row->services,.generation=game->publication.generation,.frontend_identity=identity,
                    .physical_seat=row->seat,.time_ms=row->view.time_ms,.viewer=row->viewer,.descriptor=game->publication.descriptor,
                    .frontend_owner=row->frontend.owner,.scene=row->scene,.assets=row->assets};
                return true;
            }
        }
    return false;
}
struct application_q3_components_video {
    qa_application *app;
    application_q3_components *owner;
    component_scene_row **rows;
    bool *reopened;
    size_t count;
    bool busy;
};
static bool video_close(component_scene_row *row,qa_error *e)
{
    if(!application_q3_scene_destroy(&row->scene,e)) return false;
    if(!row->host_entered&&row->host.frontend_lifetime&&row->host.release_frontend) {
        row->host.release_frontend(row->host.frontend_lifetime); row->host.frontend_lifetime=NULL;
    }
    if(row->frontend.owner&&!row->frontend.destroy(&row->frontend.owner,e)) return false;
    row->frontend=(application_q3_component_scene_frontend){0}; row->assets=NULL; row->frontend_identity=0;
    row->host=(qa_q3_host_options){0}; row->initialized=row->host_entered=row->advanced=false; row->sequence=0;
    return true;
}
bool application_q3_components_video_current(const application_q3_components_video *ticket,qa_error *e)
{
    if(!ticket||ticket->busy||ticket->app->components!=ticket->owner||ticket->owner->video!=ticket||ticket->owner->closing)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component video ticket lost its genuine installed roster");
    for(size_t i=0;i<ticket->count;++i) {
        component_scene_row *row=ticket->rows[i]; bool found=false;
        for(size_t j=0;j<ticket->owner->count;++j) for(component_scene_row *actual=ticket->owner->rows[j]->scenes;actual;actual=actual->next)
            if(actual==row) found=true;
        if(!found||!q3components_current(row->game)||!q3components_scenes_idle(row->game)||
            !application_q3_component_idle(row->game->publication.game))
            return application_fail(e,QA_ERROR_ARGUMENT,"Component video ticket retains entered or retired physical source parents");
    }
    return true;
}
bool application_q3_components_video_prepare(qa_application *app,application_q3_components_video **out,qa_error *e)
{
    if(!app||!out||*out||!qa_session_safe(app->session)) return application_fail(e,QA_ERROR_ARGUMENT,"Component video preparation requires a returned application");
    application_q3_components *owner=app->components;
    if(!owner) return true;
    if(owner->video||owner->closing||!application_q3_components_idle(owner))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component video preparation retains active source users");
    size_t count=0;
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) {
        if(!row->initialized||!row->scene||!row->frontend.owner)
            return application_fail(e,QA_ERROR_ARGUMENT,"Component video preparation retains an incomplete physical constructor");
        if(count==SIZE_MAX/sizeof(component_scene_row *)) return application_fail(e,QA_ERROR_MEMORY,"Component video roster exceeds storage");
        ++count;
    }
    if(!count) return true;
    application_q3_components_video *ticket=calloc(1,sizeof(*ticket));
    if(!ticket) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component video reconstruction");
    ticket->rows=calloc(count,sizeof(*ticket->rows)); ticket->reopened=calloc(count,sizeof(*ticket->reopened));
    if(!ticket->rows||!ticket->reopened) { free(ticket->rows); free(ticket->reopened); free(ticket); return application_fail(e,QA_ERROR_MEMORY,"Retaining component video recipes"); }
    ticket->app=app; ticket->owner=owner;
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) ticket->rows[ticket->count++]=row;
    owner->video=ticket; *out=ticket;
    ticket->busy=true; owner->video_entering=true; bool ok=true;
    for(size_t i=0;ok&&i<count;++i) ok=video_close(ticket->rows[i],e);
    owner->video_entering=false; ticket->busy=false; return ok;
}
bool application_q3_components_video_reopen(application_q3_components_video *ticket,qa_error *e)
{
    if(!application_q3_components_video_current(ticket,e)) return false;
    ticket->busy=true; ticket->owner->video_entering=true; bool ok=true;
    for(size_t i=0;ok&&i<ticket->count;++i) {
        if(ticket->reopened[i]) continue;
        component_scene_row *row=ticket->rows[i];
        ok=video_close(row,e)&&open_scene(row,e);
        if(ok) ticket->reopened[i]=true;
    }
    ticket->owner->video_entering=false; ticket->busy=false; return ok;
}
bool application_q3_components_video_finish(application_q3_components_video **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    application_q3_components_video *ticket=*slot;
    if(!application_q3_components_video_current(ticket,e)) return false;
    for(size_t i=0;i<ticket->count;++i) if(!ticket->reopened[i]||!ticket->rows[i]->initialized)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component video replacement lacks its actual successful Init");
    ticket->owner->video=NULL; free(ticket->rows); free(ticket->reopened); free(ticket); *slot=NULL; return true;
}
bool application_q3_components_video_abort(application_q3_components_video **slot,qa_error *e)
{
    return !slot||!*slot||(application_q3_components_video_reopen(*slot,e)&&application_q3_components_video_finish(slot,e));
}
static bool scene_blob(qa_source_save_io *io,qa_buffer *buffer)
{
    size_t size=buffer->size;
    if(!qa_source_save_count(io,&size,UINT32_MAX)||!size) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        buffer->data=malloc(size); buffer->size=size;
        if(!buffer->data) return application_fail(io->error,QA_ERROR_MEMORY,"Retaining actual component scene continuation");
    }
    return qa_source_save_bytes(io,buffer->data,size);
}
static bool scene_fields(qa_source_save_io *io,component_scene_row *row)
{
    uint64_t services=row->services;
    if(!qa_source_save_actor(io,&row->viewer)||!row->viewer.registry||!qa_source_save_u32(io,&row->seat)||
        row->seat>=qa_launch_snapshot_choices(row->game->roster->options.snapshot)->seat_count||
        !qa_source_save_u64(io,&services)||!services||services>UINT32_MAX||
        !qa_source_save_u64(io,&row->frontend_identity)||!row->frontend_identity||
        !qa_source_save_vec3(io,&row->view.origin)||!qa_vec_finite(row->view.origin)) return false;
    row->services=(qa_actor_owner)services;
    for(size_t i=0;i<3;++i) if(!qa_source_save_vec3(io,row->view.axis+i)||!qa_vec_finite(row->view.axis[i])) return false;
    if(!qa_source_save_i32(io,&row->view.time_ms)||row->view.time_ms<0||
        !qa_source_save_i32(io,&row->view.frame_ms)||row->view.frame_ms<0||
        !qa_source_save_u64(io,&row->sequence)||!qa_source_save_bool(io,&row->advanced)||
        !scene_blob(io,&row->saved_scene)||!scene_blob(io,&row->saved_cvars)||!scene_blob(io,&row->saved_console)) return false;
    size_t count=row->event_count-row->event_cursor;
    if(!qa_source_save_count(io,&count,UINT32_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        row->events=count?calloc(count,sizeof(*row->events)):NULL; row->event_count=count; row->event_cursor=0;
        if(count&&!row->events) return application_fail(io->error,QA_ERROR_MEMORY,"Retaining saved original CG player event queue");
    }
    for(size_t i=0;i<count;++i) {
        component_pending_event *event=row->events+row->event_cursor+i;
        if(!qa_source_save_u64(io,&event->sequence)||!event->sequence||
            !q3component_player_event_fields(row->game->publication.abi,io,&event->event)||
            (i&&event[-1].sequence>=event->sequence)) return false;
    }
    return true;
}
bool q3components_scenes_checkpoint(component_game_row *game,qa_buffer *out,qa_error *e)
{
    if(!game||!out||out->data||out->size||!q3components_scenes_idle(game))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component scene capture requires returned physical children");
    size_t count=0;
    for(component_scene_row *row=game->scenes;row;row=row->next) ++count;
    qa_source_save_io io={0}; uint8_t magic[4]={'Q','G','C','S'};
    bool ok=qa_source_save_writer(&io,game->roster->options.application->session,e)&&qa_source_save_bytes(&io,magic,4)&&
        qa_source_save_count(&io,&count,UINT32_MAX);
    for(component_scene_row *row=game->scenes;ok&&row;row=row->next) {
        component_scene_row saved=*row; saved.saved_scene=saved.saved_cvars=saved.saved_console=(qa_buffer){0};
        ok=row->initialized&&!row->restore_pending&&application_q3_scene_checkpoint(row->scene,&saved.saved_scene,e)&&
            qa_cvars_save_capture(row->cvars,&saved.saved_cvars,e)&&
            qa_console_save_capture(row->console,game->roster->options.application->session,&saved.saved_console,e)&&scene_fields(&io,&saved);
        qa_buffer_free(&saved.saved_scene); qa_buffer_free(&saved.saved_cvars); qa_buffer_free(&saved.saved_console);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool q3components_scenes_saved_read(component_game_row *game,qa_bytes bytes,qa_error *e)
{
    if(!game||game->scenes) return application_fail(e,QA_ERROR_ARGUMENT,"Component scene topology requires an empty actual roster");
    qa_source_save_io io={0}; uint8_t magic[4]={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,game->roster->options.application->session,bytes,e)&&qa_source_save_bytes(&io,magic,4)&&
        !memcmp(magic,"QGCS",4)&&qa_source_save_count(&io,&count,UINT32_MAX);
    component_scene_row **tail=&game->scenes;
    for(size_t i=0;ok&&i<count;++i) {
        component_scene_row *row=calloc(1,sizeof(*row));
        if(!row) { ok=application_fail(e,QA_ERROR_MEMORY,"Retaining real restored component viewer"); break; }
        row->game=game; *tail=row; tail=&row->next; row->restore_pending=true;
        row->view=(application_q3_component_view){.source=game->publication.source,
            .weapon_presented=game->roster->options.weapon_presented,.weapon_context=game->roster->options.weapon_context};
        ok=scene_fields(&io,row); row->view.viewer=row->viewer;
        row->source=application_q3_component_view_services(&row->view);
        for(component_scene_row *prior=game->scenes;ok&&prior!=row;prior=prior->next)
            if(prior->frontend_identity==row->frontend_identity||prior->services==row->services||
                (prior->seat==row->seat&&qa_actor_id_equal(prior->viewer,row->viewer)))
                ok=application_fail(e,QA_ERROR_FORMAT,"Component scene topology duplicates a physical viewer or renderer");
        for(size_t k=0;ok&&k<game->roster->count;++k) {
            component_game_row *other=game->roster->rows[k]; if(!other||other==game) continue;
            for(component_scene_row *prior=other->scenes;ok&&prior;prior=prior->next)
                if(prior->frontend_identity==row->frontend_identity||prior->services==row->services)
                    ok=application_fail(e,QA_ERROR_FORMAT,"Component scene topology aliases another actual private renderer");
        }
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
static bool restored_identity(void *context,qa_console_save_identity kind,uint64_t saved,uint64_t *out,qa_error *e)
{
    component_scene_row *row=context;
    if(saved&&(kind!=QA_CONSOLE_SAVE_OWNER||saved!=row->services))
        return application_fail(e,QA_ERROR_FORMAT,"Component CG console identity differs from its actual retained viewer");
    *out=saved; return true;
}
static bool restored_command(void *context,uint64_t registry,const qa_command_context *saved,qa_command_context *out,qa_error *e)
{
    component_scene_row *row=context;
    if(!registry||saved->session||saved->client||saved->owner!=row->services||saved->dialect!=QA_CONSOLE_Q3)
        return application_fail(e,QA_ERROR_FORMAT,"Component CG command lost its literal saved receiver");
    *out=*saved;
    if(saved->registry==registry) out->registry=qa_actors_identity(qa_session_actors(row->game->roster->options.application->session));
    else if(saved->registry) out->registry=0;
    return true;
}
bool application_q3_components_scenes_restore_prepare(qa_application *app,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner) return true;
    if(owner->closing||owner->video||!application_q3_components_idle(owner))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component scene restore requires its actual returned candidate roster");
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) {
        if(!row->restore_pending||row->restore_consoles) continue;
        if(!row->restore_constructed&&!open_scene(row,e)) return false;
        qa_cvars_restore *cvars=NULL;
        bool ok=qa_cvars_save_prepare(row->cvars,(qa_bytes){row->saved_cvars.data,row->saved_cvars.size},&cvars,e)&&qa_cvars_save_validate(cvars,e);
        if(ok) { ok=qa_cvars_save_commit(cvars,e); if(ok) cvars=NULL; }
        qa_cvars_save_abort(cvars);
        qa_console_save_resolvers console={row,restored_identity,restored_command};
        if(!ok||!qa_console_save_restore(row->console,app->session,&console,(qa_bytes){row->saved_console.data,row->saved_console.size},e)) return false;
        row->restore_consoles=true;
    }
    return true;
}
bool application_q3_components_scenes_restore_finish(qa_application *app,qa_error *e)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner) return true;
    if(owner->closing||owner->video||!application_q3_components_idle(owner))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component CG activation requires restored private dictionaries");
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) {
        if(!row->restore_pending) continue;
        if(!row->restore_constructed||!row->restore_consoles) return application_fail(e,QA_ERROR_ARGUMENT,"Component CG restore omitted its actual renderer constructor or private console");
        if(!row->restore_imported) {
            if(!application_q3_scene_restore(row->scene,(qa_bytes){row->saved_scene.data,row->saved_scene.size},e)) return false;
            row->restore_imported=true;
        }
        if(!application_q3_scene_finish_restore(row->scene,e)) return false;
        row->restore_pending=false; row->initialized=true;
        qa_buffer_free(&row->saved_scene); qa_buffer_free(&row->saved_cvars); qa_buffer_free(&row->saved_console);
    }
    return true;
}

#include "guest_q3_components_private.h"
#include "qa/json.h"
#include "qa/application_q3_components.h"
#include <stdio.h>

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
    uint64_t sequence;
    bool initialized,host_entered,advanced;
};
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
    qa_command_context command={.owner=row->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_CLIENT};
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
    qa_json_document *document=NULL;
    if(!qa_json_parse(qa_resource_bytes(game->declaration),&document,e)) return false;
    qa_json_id presentation=qa_json_get(document,qa_json_root(document),"presentation");
    if(!qa_json_string_equal(document,qa_json_get(document,presentation,"runtime"),"qvm-scene")) {
        qa_json_destroy(document); return application_fail(e,QA_ERROR_UNSUPPORTED,"Component has no declared original scene presentation");
    }
    qa_buffer path={0};
    bool ok=qa_json_string(document,qa_json_get(document,qa_json_get(document,presentation,"cgame"),"path"),&path,e);
    char *normalized=ok&&!memchr(path.data,0,path.size)?qa_vfs_normalize_path((char *)path.data,e):NULL;
    qa_buffer_free(&path);
    if(ok) ok=normalized&&qa_vfs_acquire(game->publication.content,normalized,&row->artifact,&row->acquisition,e)&&
        qa_qvm_image_load(qa_resource_bytes(row->artifact),&row->image,e)&&
        application_q3_scene_profile_create(row->image,game->publication.abi,normalized,game->image,
            game->publication.metadata->program_path,qa_json_source(document,presentation),&row->profile,e);
    free(normalized); qa_json_destroy(document);
    if(!ok||!scene_identity(row,e)) return false;
    qa_cvar_options cvars={.dialect=QA_CONSOLE_Q3,.user=row,.print=print,.cheats_allowed=cheats};
    row->cvars=qa_cvars_create(&cvars,e); if(!row->cvars) return false;
    qa_console_options console={.context={.owner=row->services,.dialect=QA_CONSOLE_Q3,.origin=QA_COMMAND_CLIENT},
        .cvars=row->cvars,.user=row,.print=console_print,.cvar_owner=cvar_owner,.source_command=console_command,
        .capture_context=capture,.context_active=active};
    row->console=qa_console_create(&console,e); if(!row->console) return false;
    row->host=(qa_q3_host_options){.role=QA_QVM_CGAME,.abi=game->publication.abi,.session=options->application->session,
        .world=options->world,.owner=game->publication.owner,.service_owner=row->services,.mounts=game->publication.content,
        .cvars=row->cvars,.console=row->console,.command_context=console.context,.common={.context=row,.print=print}};
    if(!options->scene_factory.prepare) return application_fail(e,QA_ERROR_UNSUPPORTED,"Component scene lacks its real renderer factory");
    application_q3_component_scene_preparation preparation={.component=game->publication.metadata,.descriptor=game->publication.descriptor,
        .catalog=game->provider->product_catalog,.owner=game->publication.owner,.generation=game->publication.generation,
        .service_owner=row->services,.physical_seat=row->seat,.viewer=row->viewer,.content=game->publication.content,
        .artifact=row->artifact,.acquisition=&row->acquisition,.profile=row->profile,.source=row->source,
        .host=&row->host,.assets=&row->assets,.frontend=&row->frontend,.context=row,.retained=retained,.published=published,.publication_read=entered};
    if(!options->scene_factory.prepare(options->scene_factory.context,&preparation,e)) return false;
    if(!row->frontend.owner||!row->frontend.idle||!row->frontend.destroy||!row->frontend.begin||!row->frontend.finish||!row->frontend.completed||!row->assets)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component scene factory omitted its actual renderer lifetime");
    application_q3_scene_options create={.profile=row->profile,.host=row->host,.assets=row->assets,.viewer=row->viewer,.source=row->source,
        .output_context=row->frontend.owner,.finish_output=row->frontend.finish};
    bool created=application_q3_scene_create(&create,false,&row->scene,e);
    row->host_entered=row->scene!=NULL;
    if(!created) return false;
    if(!row->frontend.begin(row->frontend.owner,0,e)||!application_q3_scene_initialize(row->scene,e)) return false;
    row->initialized=true; return true;
}
bool application_q3_components_scene_prepare(application_q3_components *owner,size_t index,uint32_t seat,
    qa_actor_id viewer,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time,int32_t elapsed,application_q3_scene **out,qa_error *e)
{
    application_q3_component_publication publication;
    if(!out||!origin||!axis||!qa_vec_finite(*origin)||!qa_vec_finite(axis[0])||!qa_vec_finite(axis[1])||!qa_vec_finite(axis[2])||time<0||elapsed<0||
        !application_q3_components_publication_at(owner,index,&publication,e)) return false;
    component_game_row *game=owner->rows[index]; component_scene_row *row=game->scenes;
    while(row&&(row->seat!=seat||!qa_actor_id_equal(row->viewer,viewer))) row=row->next;
    if(!row) {
        row=calloc(1,sizeof(*row)); if(!row) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual component viewer scene");
        row->game=game; row->viewer=viewer; row->seat=seat;
        row->next=game->scenes; game->scenes=row;
        row->view=(application_q3_component_view){.source=publication.source,.viewer=viewer,
            .weapon_presented=owner->options.weapon_presented,.weapon_context=owner->options.context};
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
        if(!row->frontend.begin(row->frontend.owner,sequence,e)||!application_q3_scene_advance(*scene,sequence,e)) return false;
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
        game->scenes=row->next; free(row);
    }
    return true;
}
size_t qa_application_q3_component_scene_count(const qa_application *app)
{
    application_q3_components *owner=app?app->components:NULL; size_t count=0;
    for(size_t i=0;owner&&!owner->closing&&i<owner->count;++i) {
        const char *runtime=owner->rows[i]->publication.presentation_runtime;
        if(runtime&&!strcmp(runtime,"qvm-scene")) ++count;
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
        if(runtime&&!strcmp(runtime,"qvm-scene")&&found++==ordinal) break;
    }
    if(index==owner->count) return application_fail(e,QA_ERROR_NOT_FOUND,"Component draw ordinal has no actual declared scene");
    application_q3_scene *scene=NULL; const qa_scene_frame *frame=NULL;
    if(!application_q3_components_scene_advance(owner,index,seat,viewer,origin,axis,time,elapsed,sequence,&scene,&frame,e)) return false;
    component_scene_row *row=owner->rows[index]->scenes;
    while(row&&row->scene!=scene) row=row->next;
    uint64_t identity;
    if(!row||!row->frontend.identity_read||!row->frontend.identity_read(row->frontend.owner,&identity)||!identity)
        return application_fail(e,QA_ERROR_FORMAT,"Component draw lacks its actual frontend identity");
    *out=(qa_application_q3_component_draw){.owner=row->game->publication.owner,.generation=row->game->publication.generation,
        .sequence=sequence,.frontend_identity=identity,.scene=scene,.bodies=application_q3_scene_bodies(scene),.assets=row->assets,.frame=frame};
    return out->bodies!=NULL;
}
bool qa_application_q3_component_draw_current(const qa_application *app,const qa_application_q3_component_draw *draw)
{
    application_q3_components *owner=app?app->components:NULL;
    if(!owner||owner->closing||!draw||!draw->bodies) return false;
    for(size_t i=0;i<owner->count;++i) for(component_scene_row *row=owner->rows[i]->scenes;row;row=row->next) {
        if(row->scene!=draw->scene) continue;
        uint64_t identity;
        return q3components_current(row->game)&&row->game->publication.owner==draw->owner&&row->game->publication.generation==draw->generation&&
            row->advanced&&row->sequence==draw->sequence&&row->assets==draw->assets&&application_q3_scene_bodies(row->scene)==draw->bodies&&
            row->frontend.identity_read&&row->frontend.identity_read(row->frontend.owner,&identity)&&identity==draw->frontend_identity;
    }
    return false;
}

#include "internal.h"
#include "unified_q3_runtime_factory.h"
#include "music_sources.h"
#include "system_cinematic.h"
#include "material_movies.h"
#include "cinematic_roles.h"
#include "remote_unified_save.h"
#include "qa/bsp.h"
#include "qa/audio_save.h"
#include "qa/audio_music_prepare.h"
#include "qa/scene_world_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/q3_source_scene_bank.h"
#include <math.h>
#include <stdio.h>

struct frontend_unified_q3_runtime_factory {
    frontend_unified_q3_runtime_factory_options options;
    frontend_unified_q3_runtime_services *services;
    frontend_unified_q3_runtime *runtime;
    frontend_unified_q3_commands *commands;
    char *source_instance,*source_content;
    qa_audio_bank *sounds;
    qa_media_library *movies;
    qa_q3_cinematic_source *cinematics;
    qa_audio_music *music;
    char *intro,*loop;
    qa_audio_listener listener;
    size_t movie_references,calls;
    bool has_listener,looping,retiring,cleanup_entered;
    bool reset_constructor;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static char *copy_text(const char *text)
{ if(!text)return NULL;size_t n=strlen(text)+1;char *out=malloc(n);if(out)memcpy(out,text,n);return out; }
bool frontend_unified_q3_runtime_factory_current(const frontend_unified_q3_runtime_factory *o)
{
    return o && !o->retiring && o->options.current(o->options.context,&o->options,false) &&
        frontend_remote_unified_current(o->options.replica,NULL) &&
        frontend_unified_q3_client_current(o->options.client) && frontend_unified_media_current(o->options.media);
}
static bool operation_current(void *context,const frontend_unified_q3_runtime_options *options)
{
    frontend_unified_q3_runtime_factory *o=context;
    return options && options->frontend==o->options.frontend && options->replica==o->options.replica &&
        options->client==o->options.client && frontend_unified_q3_runtime_factory_current(o);
}
static const q3n_compiled_frame *entered(void *context)
{ return frontend_unified_q3_runtime_entered(((frontend_unified_q3_runtime_factory *)context)->runtime); }
static const frontend_unified_q3_client_frame *checkpoint_frame(void *context)
{ return frontend_unified_q3_runtime_rebind_frame(((frontend_unified_q3_runtime_factory *)context)->runtime); }
static bool cut(frontend_unified_q3_runtime_factory *o,const q3n_frame *f,qa_error *e)
{
    return f && f->compiled==entered(o) && f->compiled && q3n_frame_current(f) &&
        frontend_unified_q3_runtime_factory_current(o)?true:fail(e,"Compiled CG factory lost its entered Source frame");
}
static bool frame_settings(void *context,const q3n_compiled_frame *frame,bool loading,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;(void)loading;(void)stereo;
    if(!out || !frame || frame!=entered(o) || !q3n_compiled_frame_current(frame) ||
        !frontend_unified_q3_runtime_factory_current(o))return fail(e,"CG status requires its entered received Source frame");
    bool replacement=false;
    if(o->options.primary_view && o->options.status_replacement &&
        !o->options.status_replacement(o->options.context,frame,&replacement,e))return false;
    if(replacement)out->hud.draw_status=false;
    return q3n_compiled_frame_current(frame) && frontend_unified_q3_runtime_factory_current(o);
}

static bool command_current(void *context,const frontend_unified_q3_commands_options *options,bool checkpoint)
{
    frontend_unified_q3_runtime_factory *o=context;
    return options && options->runtime==o->runtime && options->client==o->options.client &&
        options->replica==o->options.replica && o->options.current(o->options.context,&o->options,checkpoint) &&
        (checkpoint?frontend_unified_q3_client_checkpoint_stage_current(o->options.client,
            frontend_unified_q3_runtime_rebind_frame(o->runtime)):frontend_unified_q3_client_current(o->options.client));
}
static bool send_client(void *context,frontend_unified_q3_client *client,const qa_command_context *origin,const char *text,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return client==o->options.client &&
    o->options.send_client(o->options.context,client,origin,text,e); }
static bool registered(void *context,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return cut(o,f,e) &&
    frontend_unified_q3_commands_register(o->commands,f,e) && cut(o,f,e); }
static bool reliable(void *context,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return cut(o,f,e) &&
    frontend_unified_q3_commands_client_command(o->commands,f,text,e) && cut(o,f,e); }
static bool command_values(void *context,int32_t weapon,float sensitivity,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;frontend_unified_input *input=NULL;
    return frontend_unified_q3_runtime_factory_current(o) &&
        o->options.input_read(o->options.context,o->options.replica,&input,e) && input &&
        frontend_unified_input_command_values(input,weapon,sensitivity,e) && frontend_unified_q3_runtime_factory_current(o); }
static bool oldest(void *context,const q3n_frame *f,int32_t *time,bool *available,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;qa_movement_command command;frontend_unified_input *input=NULL;
    if(!time || !available || !cut(o,f,e) || !o->options.input_read(o->options.context,o->options.replica,&input,e) ||
       !input || !cut(o,f,e) || !frontend_unified_input_oldest_q3(input,&command,available,e))return false;
    if(*available)*time=command.server_time_ms;
    return cut(o,f,e);
}
static bool trace_number(void *context,const q3n_compiled_frame *frame,const qa_trace_result *hit,int32_t *out,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;
    if(!hit || !out || frame!=entered(o) || !q3n_compiled_frame_current(frame))return fail(e,"Compiled trace lost its physical hit receipt");
    if(hit->hit==QA_TRACE_HIT_WORLD){*out=1022;return true;}
    if(hit->hit==QA_TRACE_HIT_NONE){*out=1023;return true;}
    for(uint32_t i=0;i<1022;++i){q3n_compiled_entity row;
        if(!q3n_compiled_frame_entity(frame,i,&row,e))return false;
        if(row.published && qa_actor_id_equal(row.actor,hit->actor)){*out=(int32_t)i;return true;}}
    return fail(e,"Compiled trace actor has no genuine physical Source entity");
}
static bool listener(void *context,const qa_audio_listener *value,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;const q3n_compiled_frame *frame=entered(o);
    if(!value || !frame || !q3n_compiled_frame_current(frame) || !frontend_unified_q3_runtime_factory_current(o))
        return fail(e,"Compiled listener requires its entered CG frame");
    o->listener=*value;o->listener.gain=1.0f/(float)o->options.frontend->options.seats;o->has_listener=true;return true;
}
static bool music_stop(void *context,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;qa_audio_engine *engine=o->options.frontend->audio;
    qa_audio_music *bus=qa_audio_engine_bus_music(engine,o->options.audio_owner);
    if(!o->music || !qa_audio_music_idle(o->music) || (bus && bus!=o->music))return fail(e,"Compiled music stop lost its retained player");
    qa_audio_music_stop(o->music);if(bus)qa_audio_engine_remove_music(engine,o->options.audio_owner);
    if(qa_audio_engine_bus_music(engine,o->options.audio_owner))return fail(e,"Compiled soundtrack still has an engine attachment");
    free(o->intro);free(o->loop);o->intro=o->loop=NULL;o->looping=false;return true;
}
static bool music_current(void *context,const frontend_music_origin *origin)
{
    frontend_unified_q3_runtime_factory *o=context;
    qa_audio_music *bus=qa_audio_engine_bus_music(o->options.frontend->audio,o->options.audio_owner);
    return origin && origin->context==o && origin->music==o->music && o->music && (!bus || bus==o->music) &&
        origin->recipe==frontend_unified_media_recipe(o->options.media) && origin->recipe_provider==o->options.source.provider &&
        origin->files==o->options.source.files && origin->receiver==o->options.receiver &&
        (o->cleanup_entered ? o->options.retirement_current(o->options.context,&o->options) :
            frontend_unified_q3_runtime_factory_current(o));
}
static bool music_checkpoint_current(void *context,const frontend_music_origin *origin)
{
    frontend_unified_q3_runtime_factory *o=context;q3n_compiled_source_view source;
    if(!o || !origin || (!o->options.frontend->capture && !o->options.frontend->source_restoring) ||
       !o->options.current(o->options.context,&o->options,true) ||
       !frontend_unified_q3_client_checkpoint_stage_current(o->options.client,
            frontend_unified_q3_runtime_rebind_frame(o->runtime)) ||
       !frontend_remote_unified_checkpoint_current(o->options.replica,NULL) ||
       !q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,NULL))return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    qa_audio_music *bus=qa_audio_engine_bus_music(o->options.frontend->audio,o->options.audio_owner);
    return d && origin->kind==FRONTEND_MUSIC_REMOTE && origin->context==o && origin->music==o->music && o->music &&
        (!bus || bus==o->music) && origin->bus==o->options.audio_owner &&
        origin->physical_seat==d->physical_seat && origin->physical_seat==source.basis.physical_seat &&
        origin->receiver==o->options.receiver && origin->receiver==source.basis.receiver &&
        !origin->descriptor && origin->recipe==frontend_unified_media_recipe(o->options.media) &&
        origin->recipe_provider==o->options.source.provider && origin->catalog==d->catalog &&
        origin->product==o->options.source.provider->selection.product &&
        origin->files==o->options.source.files && origin->files==source.basis.content &&
        source.basis.assets==o->options.source.assets && q3n_compiled_source_checkpoint_current(&source);
}
static frontend_music_origin music_origin(frontend_unified_q3_runtime_factory *o)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    return (frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.bus=o->options.audio_owner,.physical_seat=d->physical_seat,
        .receiver=o->options.receiver,.recipe=frontend_unified_media_recipe(o->options.media),.recipe_provider=o->options.source.provider,
        .catalog=d->catalog,.product=o->options.source.provider->selection.product,.files=o->options.source.files,
        .music=o->music,.context=o,.current=music_current,.checkpoint_current=music_checkpoint_current,.stop=music_stop};
}
static bool music_returned(frontend_unified_q3_runtime_factory *o,const frontend_music_origin *origin,
    const q3n_compiled_frame *frame,const char *intro,const char *loop,bool looping,qa_error *e)
{ return frontend_music_sources_explicit(o->options.frontend->music_sources,origin,intro,loop,looping,e) &&
    q3n_compiled_frame_current(frame) && frontend_unified_q3_runtime_factory_current(o); }
static bool music(void *context,const char *intro,const char *loop,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;qa_frontend *f=o->options.frontend;
    const q3n_compiled_frame *frame=entered(o);
    if(!frame || !q3n_compiled_frame_current(frame) || !f->audio)return fail(e,"Compiled music requires its actual entered audio Source");
    qa_audio_music *bus=qa_audio_engine_bus_music(f->audio,o->options.audio_owner);
    if(bus && bus!=o->music)return fail(e,"Compiled soundtrack bus changed its actual player");
    if(o->music) {
        frontend_music_origin retained=music_origin(o);
        if(!frontend_music_sources_explicit_selected(f->music_sources,&retained)) {
            if(bus || !qa_audio_music_idle(o->music))return fail(e,"Selecting compiled music retains an old player operation");
            qa_audio_music *fresh=NULL;
            qa_audio_music_controls *shared=frontend_music_sources_controls(f->music_sources);
            if(!shared || !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_GAME_Q3,true,&fresh,e))return false;
            if(!qa_audio_music_controls_bind(fresh,shared,e)){qa_audio_music_release(fresh);return false;}
            qa_audio_music_release(o->music);o->music=fresh;
            free(o->intro);free(o->loop);o->intro=o->loop=NULL;o->looping=false;
        }
    }
    if(!o->music && !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_GAME_Q3,true,&o->music,e))return false;
    qa_audio_music_controls *controls=frontend_music_sources_controls(f->music_sources);
    if(!controls || (!qa_audio_music_controls_is(o->music,controls) && !qa_audio_music_controls_bind(o->music,controls,e)))return false;
    frontend_music_origin origin=music_origin(o);
    if(!frontend_music_sources_explicit_begin(f->music_sources,&origin,e))return false;
    bool enabled;if(!qa_audio_music_controls_enabled(controls,&enabled))return fail(e,"Compiled music lost its application controls");
    const char *first=intro?intro:"",*tail=loop?loop:"";
    if(!enabled && *first)return music_returned(o,&origin,frame,"","",false,e);
    if(o->intro && o->loop && o->looping && !strcmp(first,o->intro) && !strcmp(tail,o->loop) && qa_audio_music_playing(o->music))
        return music_returned(o,&origin,frame,first,tail,true,e);
    if(!music_stop(o,e))return false;
    if(!*first)return music_returned(o,&origin,frame,"","",false,e);
    o->intro=malloc(strlen(first)+1);o->loop=malloc(strlen(tail)+1);
    if(!o->intro || !o->loop){free(o->intro);free(o->loop);o->intro=o->loop=NULL;return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled soundtrack names");}
    strcpy(o->intro,first);strcpy(o->loop,tail);
    if(!qa_audio_music_retain(o->music,e))return false;
    if(!qa_audio_engine_music(f->audio,o->options.audio_owner,origin.physical_seat,1,o->music,e)){
        qa_audio_music_release(o->music);return false;}
    qa_audio_stream *a=NULL,*b=NULL;
    if(!qa_audio_bank_music_cue(o->sounds,first,QA_GAME_Q3,NULL,NULL,&a,e))return false;
    if(!a)return music_returned(o,&origin,frame,first,tail,false,e);
    b=a;if(*tail && strcmp(first,tail) && !qa_audio_bank_music_cue(o->sounds,tail,QA_GAME_Q3,NULL,NULL,&b,e)){qa_audio_stream_close(a);return false;}
    qa_audio_music_start(o->music,a,b);o->looping=b!=NULL;
    return music_returned(o,&origin,frame,first,tail,o->looping,e);
}
static bool timescale(void *context,int32_t elapsed,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;qa_native_q3_client_cvar finish,speed,value;
    if(!frontend_unified_q3_client_cvar_read(o->options.client,QA_NATIVE_Q3_CVAR_cg_timescaleFadeEnd,&finish,e) ||
        !frontend_unified_q3_client_cvar_read(o->options.client,QA_NATIVE_Q3_CVAR_cg_timescaleFadeSpeed,&speed,e) ||
        !frontend_unified_q3_client_cvar_read(o->options.client,QA_NATIVE_Q3_CVAR_cg_timescale,&value,e))return false;
    if(value.number==finish.number)return true;
    float product=speed.number*(float)elapsed,delta=product/1000.0f;
    float sum=value.number<finish.number?value.number+delta:value.number-delta;
    float next=value.number<finish.number?fminf(finish.number,sum):fmaxf(finish.number,sum);
    if(!frontend_unified_q3_client_cvar_number(o->options.client,QA_NATIVE_Q3_CVAR_cg_timescale,next,e))return false;
    return speed.number==0 || qa_cvars_set_number(qa_application_cvars(o->options.frontend->application),"timescale",next,e);
}
static bool initialize_stage(void *context,const q3n_frame *f,q3n_command_init_stage stage,const char *map,int32_t physical,uint32_t *extent,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;(void)map;(void)physical;
    if(!cut(o,f,e))return false;
    if(stage==Q3N_INIT_CONSOLE_COMMANDS)return frontend_unified_q3_commands_registration_ready(o->commands,f,e) && cut(o,f,e);
    if(stage!=Q3N_INIT_COLLISION_MAP || !extent)return fail(e,"Compiled constructor requested an unowned stage");
    qa_executable_recipe *recipe=frontend_unified_media_recipe(o->options.media);qa_bsp_view bsp;
    qa_resource *resource=qa_executable_recipe_map(recipe);qa_scene_world *world=frontend_unified_media_world(o->options.media);
    qa_collision_geometry *geometry=(qa_collision_geometry *)frontend_remote_unified_geometry(o->options.replica);
    if(!resource || !world || !geometry || !qa_bsp_open(qa_resource_bytes(resource),&bsp,e) ||
        !qa_q3_presentation_world(f->presentation,world,geometry,bsp.lumps[QA_BSP_ENTITIES].bytes,e))return false;
    size_t count=qa_scene_world_model_count(world);if(!count || count>UINT32_MAX)return fail(e,"Compiled world has no physical inline model extent");
    *extent=(uint32_t)count;return cut(o,f,e);
}
static bool movie_parent(frontend_unified_q3_runtime_factory *o,q3n_compiled_source_view *out)
{
    bool cold=o && o->options.frontend->capture;
    return o && (!o->retiring || cold) && o->movies && o->options.current(o->options.context,&o->options,cold) &&
        (cold?q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),out,NULL):
            q3n_compiled_source_read(frontend_unified_q3_client_source(o->options.client),out,NULL)) &&
        out->basis.receiver==o->options.receiver && out->basis.content==o->options.source.files &&
        out->basis.assets==o->options.source.assets &&
        (cold?q3n_compiled_source_checkpoint_current(out):q3n_compiled_source_current(out));
}
static bool movie_current(void *context,const frontend_system_cinematic_source *view)
{
    frontend_unified_q3_runtime_factory *o=context;q3n_compiled_source_view source;
    qa_q3_cinematic_source *cinematics=NULL;
    if(!view || !o || !o->movie_references || !movie_parent(o,&source) ||
        !frontend_unified_q3_runtime_factory_cinematic_read(o,&cinematics,NULL) || !cinematics)return false;
    const frontend_system_cinematic_identity *id=&view->identity;
    qa_cvars *vars=o->options.frontend->capture?frontend_unified_q3_client_checkpoint_stage_cvars(o->options.client,
        frontend_unified_q3_runtime_rebind_frame(o->runtime)):
        frontend_unified_q3_client_cvars(o->options.client);
    return view->context==o && view->files==o->options.source.files && view->movies==o->movies && view->cvars==vars &&
        view->cinematics==o->cinematics && o->cinematics &&
        id->source_group==o->options.receiver && id->source_owner==o->options.receiver &&
        id->service_owner==o->options.audio_owner && id->audio_bus==o->options.audio_owner &&
        id->role==QA_QVM_CGAME && id->physical_seat==source.basis.physical_seat && id->launch_seat==source.basis.seat;
}
static void movie_release(void *context)
{ frontend_unified_q3_runtime_factory *o=context;if(o->movie_references)--o->movie_references; }
static bool movie_append(void *context,const char *text,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;q3n_compiled_source_view source;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->options.replica);
    const qa_command_context *base=frontend_unified_q3_client_context(o->options.client);
    if(!text || !d || !base || !o->movie_references || o->calls==SIZE_MAX ||
       !movie_parent(o,&source))return fail(e,"Compiled movie completion lost its actual CLIENT namespace");
    qa_command_context command=*base;command.script="q3-system-movie";command.direct=false;command.console_text=false;
    ++o->calls;bool okay=qa_console_append(d->console,&command,text,e) && movie_parent(o,&source);--o->calls;return okay;
}
static frontend_system_cinematic_source movie_source(frontend_unified_q3_runtime_factory *o,
    const q3n_compiled_source_view *source)
{ return (frontend_system_cinematic_source){.identity={o->options.receiver,o->options.audio_owner,o->options.audio_owner,
        o->options.receiver,QA_QVM_CGAME,source->basis.physical_seat,source->basis.seat},
    .files=o->options.source.files,.movies=o->movies,.cinematics=o->cinematics,
    .cvars=frontend_unified_q3_client_cvars(o->options.client),
    .context=o,.current=movie_current,.append=movie_append,.release=movie_release}; }

static bool system_movie(void *context,const qa_q3_movie_request *request,qa_q3_system_movie *out,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;const q3n_compiled_frame *frame=entered(o);
    q3n_compiled_source_view source;
    if(!request || !out || !frame || !q3n_compiled_frame_current(frame) || !movie_parent(o,&source) ||
       o->calls==SIZE_MAX || o->movie_references==SIZE_MAX)return fail(e,"Compiled system movie requires its actual entered CG role");
    frontend_system_cinematic_source value=movie_source(o,&source);++o->movie_references;++o->calls;
    bool okay=frontend_system_cinematic_open(o->options.frontend,&value,request,out,e);
    if(!okay)--o->movie_references;
    --o->calls;return okay;
}
static bool video_shutdown(void *context,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return frontend_unified_q3_commands_video_reset(o->commands,e); }

bool frontend_unified_q3_runtime_factory_create(const frontend_unified_q3_runtime_factory_options *options,
    frontend_unified_q3_runtime_factory **out,qa_error *e)
{
    if(!options || !out || *out || !options->current || !options->retirement_current || !options->send_client || !options->frontend || !options->replica ||
        !options->media || !options->client || !options->input_read || !options->prediction || !options->events ||
        !options->receiver || !options->audio_owner || !options->audio_actor ||
        !options->source.instance || !options->source.content || !options->source.provider || !options->composition.body_hidden || !options->composition.body_submit ||
        !options->composition.player_weapon || options->frontend->source_restoring ||
        !options->current(options->context,options,false))return fail(e,"Compiled factory requires its real retained Source services");
    frontend_unified_q3_runtime_factory *o=calloc(1,sizeof(*o));if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled CG factory");
    o->options=*options;*out=o;
    o->options.source.instance=NULL;o->options.source.content=NULL;
    o->options.source.source=NULL;o->options.source.entities=NULL;o->options.source.players=NULL;
    o->options.source.game_state=NULL;o->options.source.configstring_revisions=NULL;
    o->options.source.visible_entities=NULL;o->options.source.area_mask=NULL;
    o->source_instance=copy_text(options->source.instance);o->options.source.instance=o->source_instance;
    o->source_content=copy_text(options->source.content);o->options.source.content=o->source_content;
    if(!o->source_instance || !o->source_content)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled Source activation names");
    for(size_t i=0;i<frontend_unified_media_bank_count(options->media);++i){frontend_unified_bank_view bank;
        if(!frontend_unified_media_bank_read(options->media,i,&bank))return fail(e,"Compiled factory media inventory lost an actual bank");
        if(bank.content && !strcmp(bank.content,options->source.content) && bank.files==options->source.files &&
           bank.q3_assets==options->source.assets){
            frontend_material_movies *provider=NULL; qa_q3_cinematic_source *parent=NULL;
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(options->replica);
            o->sounds=bank.sounds;o->movies=bank.movies;
            if(!domain || bank.cinematic_seat!=domain->physical_seat || !bank.cinematic_audio_owner)
                return fail(e,"Compiled cinematic role has no actual physical bank namespace");
            if(!frontend_material_movies_library_owner(bank.materials,&provider,e) ||
                !frontend_material_movies_cinematic_read(provider,&parent,e) || !parent ||
                !qa_q3_cinematic_source_create_role(parent,bank.cinematic_seat,options->audio_owner,&o->cinematics,e)) return false;
            break;
        }}
    if(!o->sounds || !o->movies || !o->cinematics)return fail(e,"Compiled factory requires its already imported Source sound/movie and numeric role bank");
    frontend_unified_q3_runtime_options operations={.frontend=options->frontend,.replica=options->replica,.client=options->client,
        .scene_only=!options->primary_view,
        .context=o,.current=operation_current,.frame_settings=frame_settings,.trace_number=trace_number,.command_values=command_values,.timescale=timescale,
        .video_shutdown=video_shutdown,.player_fx=options->composition};
    operations.weapons.context=options->context;operations.weapons.view_replacement=options->view_replacement;
    operations.view.camera_context=options->context;
    operations.view.camera_override=options->primary_view?options->camera_override:NULL;
    operations.presentation.context=o;operations.presentation.music=music;operations.presentation.listener=listener;
    operations.presentation.system_movie=system_movie;
    operations.presentation.cinematics=o->cinematics;
    operations.presentation.near_clip=4;operations.presentation.far_clip=8192;operations.presentation.identity_light=1;
    operations.presentation.lod_scale=5;operations.presentation.rail_core_width=6;operations.presentation.rail_ring_width=16;
    operations.presentation.rail_segment_length=32;
    operations.commands.context=o;operations.commands.compiled_register=registered;operations.commands.initialize_stage=initialize_stage;
    operations.hud.context=o;operations.hud.compiled_oldest_command=oldest;operations.hud.client_command=reliable;
    frontend_unified_q3_runtime_services_options services={.frontend=options->frontend,.replica=options->replica,.media=options->media,
        .client=options->client,.source=options->source,.events=options->events,.receiver=options->receiver,.audio_owner=options->audio_owner,
        .audio_context=options->audio_context,.audio_actor=options->audio_actor,.entered_frame=entered,
        .checkpoint_frame=checkpoint_frame,.operations=operations};
    frontend_unified_q3_runtime_options actual;
    if(!frontend_unified_q3_runtime_services_create(&services,&o->services,e) ||
        !frontend_unified_q3_runtime_services_read(o->services,&actual,e) ||
        !frontend_unified_q3_runtime_create(&actual,&o->runtime,e))return false;
    frontend_unified_q3_commands_options commands={.frontend=options->frontend,.replica=options->replica,.client=options->client,
        .runtime=o->runtime,.context=o,.current=command_current,.send_client=send_client};
    bool okay=frontend_unified_q3_commands_create(&commands,&o->commands,e);
    /* Only the actual CLIENT retains observed state/history. These immutable
     * names identify the factory's activation without borrowing a FRAME row. */
    return okay;
}

bool frontend_unified_q3_runtime_factory_idle(const frontend_unified_q3_runtime_factory *o)
{ return !o || (!o->calls && frontend_unified_q3_runtime_idle(o->runtime) && frontend_unified_q3_commands_idle(o->commands) && (!o->music || qa_audio_music_idle(o->music))); }
static bool checkpoint_returned(const frontend_unified_q3_runtime_factory *o)
{
    return o && !o->calls &&
        (!o->runtime || frontend_unified_q3_runtime_checkpoint_current(o->runtime)) &&
        frontend_unified_q3_commands_idle(o->commands) && (!o->music || qa_audio_music_idle(o->music)) &&
        frontend_unified_q3_client_checkpoint_stage_current(o->options.client,
            frontend_unified_q3_runtime_rebind_frame(o->runtime));
}
static bool constructor_closed(const void *context)
{
    const frontend_unified_q3_runtime_factory *o=context;
    return o && o->reset_constructor && o->retiring && !o->cleanup_entered &&
        !o->runtime && !o->commands && !o->services && !o->cinematics && !o->movie_references && !o->calls &&
        !o->intro && !o->loop &&
        !qa_audio_engine_bus_music(o->options.frontend->audio,o->options.audio_owner);
}
bool frontend_unified_q3_runtime_factory_destroy(frontend_unified_q3_runtime_factory **out,qa_error *e)
{
    if(!out || !*out)return true;
    frontend_unified_q3_runtime_factory *o=*out;
    if(!frontend_unified_q3_runtime_factory_idle(o))return fail(e,"Compiled factory retirement retains an entered child");
    if(!o->options.retirement_current(o->options.context,&o->options))
        return fail(e,"Compiled factory retirement lost its actual retained parent custody");
    o->retiring=true;o->cleanup_entered=true;
    bool okay=frontend_unified_q3_commands_destroy(&o->commands,e) &&
        frontend_music_sources_explicit_retire(o->options.frontend->music_sources,o,e) &&
        (!o->music || music_stop(o,e)) && frontend_unified_q3_runtime_destroy(&o->runtime,e) &&
        (!o->cinematics || qa_q3_cinematic_source_systems_close(o->cinematics,e));
    o->cleanup_entered=false;
    if(!okay)return false;
    if(o->movie_references)return fail(e,"Compiled factory retirement still retains actual movie role leases");
    if(!o->options.frontend->source_restoring &&
        !frontend_cinematic_roles_adopt(o->options.frontend,&o->cinematics,NULL,e)) return false;
    if(!qa_q3_cinematic_source_destroy(&o->cinematics,e)) return false;
    if(!frontend_unified_q3_runtime_services_destroy(&o->services,e))return false;
    if(o->reset_constructor && !frontend_unified_q3_client_constructor_reset(o->options.client,o,constructor_closed,e))return false;
    qa_audio_music_release(o->music);free(o->intro);free(o->loop);
    free(o->source_instance);free(o->source_content);free(o);*out=NULL;return true;
}
bool frontend_unified_q3_runtime_factory_constructor_abort(frontend_unified_q3_runtime_factory **out,qa_error *e)
{
    if(!out || !*out)return true;
    frontend_unified_q3_runtime_factory *o=*out;q3n_compiled_source_view source;
    if(!frontend_unified_q3_runtime_factory_idle(o) ||
       !o->options.current(o->options.context,&o->options,false) ||
       !q3n_compiled_source_read(frontend_unified_q3_client_source(o->options.client),&source,e) || source.basis.initialized)
        return fail(e,"Compiled constructor abort requires its actual failed fresh CLIENT Init");
    o->reset_constructor=true;
    return frontend_unified_q3_runtime_factory_destroy(out,e);
}
frontend_unified_q3_runtime *frontend_unified_q3_runtime_factory_runtime(const frontend_unified_q3_runtime_factory *o)
{ return o?o->runtime:NULL; }
bool frontend_unified_q3_runtime_factory_cinematic_read(const frontend_unified_q3_runtime_factory *o,
    qa_q3_cinematic_source **out,qa_error *e)
{
    if(!o || !out || !o->options.frontend || !o->options.client || !o->options.media || !o->movies)
        return fail(e,"Compiled cinematic read lost its actual retained factory");
    if(o->cinematics) {
        const qa_q3_cinematic_source *parent=NULL; uint32_t seat=0; uint64_t bus=0;
        qa_q3_cinematic_source_options source;
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->options.replica);
        if(!domain || !qa_q3_cinematic_source_role_read(o->cinematics,&parent,&seat,&bus) || !parent ||
            !qa_q3_cinematic_source_read(o->cinematics,&source) || source.files!=o->options.source.files ||
            source.media!=o->movies || source.audio!=o->options.frontend->audio ||
            seat!=domain->physical_seat || bus!=o->options.audio_owner ||
            qa_q3_cinematic_source_handles(o->cinematics)!=o->options.frontend->source_cinematics)
            return fail(e,"Compiled cinematic role leaves its true provider, seat or bus");
    }
    *out=o->cinematics; return true;
}

bool frontend_unified_q3_runtime_factory_initialize(frontend_unified_q3_runtime_factory *o,qa_error *e)
{ return o && frontend_unified_q3_runtime_initialize(o->runtime,e); }
bool frontend_unified_q3_runtime_factory_rebind_prepare(frontend_unified_q3_runtime_factory *o,const frontend_unified_q3_client_frame *f,qa_error *e)
{ return o && frontend_unified_q3_runtime_rebind_prepare(o->runtime,f,e); }
bool frontend_unified_q3_runtime_factory_rebind_ready(const frontend_unified_q3_runtime_factory *o,const frontend_unified_q3_client_frame *f)
{ return o && frontend_unified_q3_runtime_rebind_ready(o->runtime,f); }
bool frontend_unified_q3_runtime_factory_rebind_checkpoint_ready(const frontend_unified_q3_runtime_factory *o,
    const frontend_unified_q3_client_frame *f)
{ return o && checkpoint_returned(o) && o->options.current(o->options.context,&o->options,true) &&
    frontend_unified_q3_runtime_rebind_checkpoint_ready(o->runtime,f); }

void frontend_unified_q3_runtime_factory_rebind_commit(frontend_unified_q3_runtime_factory *o,const frontend_unified_q3_client_frame *f)
{ if(o)frontend_unified_q3_runtime_rebind_commit(o->runtime,f); }
void frontend_unified_q3_runtime_factory_rebind_abort(frontend_unified_q3_runtime_factory *o,const frontend_unified_q3_client_frame *f)
{ if(o)frontend_unified_q3_runtime_rebind_abort(o->runtime,f); }
bool frontend_unified_q3_runtime_factory_prepare(frontend_unified_q3_runtime_factory *o,uint32_t stereo,qa_error *e)
{ if(!o)return false;o->has_listener=false;return frontend_unified_q3_runtime_prepare(o->runtime,stereo,e); }
bool frontend_unified_q3_runtime_factory_process(frontend_unified_q3_runtime_factory *o,int32_t time,bool *active,qa_error *e)
{ return o && frontend_unified_q3_runtime_process(o->runtime,time,active,e); }
bool frontend_unified_q3_runtime_factory_prediction_baseline(const frontend_unified_q3_runtime_factory *o,
    frontend_unified_q3_runtime_prediction_baseline *out,qa_error *e)
{ return o && frontend_unified_q3_runtime_prediction_baseline_read(o->runtime,out,e); }
bool frontend_unified_q3_runtime_factory_prediction(frontend_unified_q3_runtime_factory *o,const qa_q3_player *p,
    const frontend_unified_q3_prediction_receipt *r,qa_vec3 correction,int32_t time,bool hyper,qa_error *e)
{ return o && frontend_unified_q3_runtime_prediction(o->runtime,p,r,correction,time,hyper,e); }
bool frontend_unified_q3_runtime_factory_camera_prepare(frontend_unified_q3_runtime_factory *o,
    frontend_unified_q3_runtime_camera *out,bool *active,qa_error *e)
{ return o && frontend_unified_q3_runtime_camera_prepare(o->runtime,out,active,e); }
bool frontend_unified_q3_runtime_factory_scene_camera(frontend_unified_q3_runtime_factory *o,const qa_scene_view *view,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_camera(o->runtime,view,e); }

bool frontend_unified_q3_runtime_factory_scene_prepare(frontend_unified_q3_runtime_factory *o,
    qa_q3_source_scene_bank *bank,bool *active,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_prepare(o->runtime,bank,active,e); }
bool frontend_unified_q3_runtime_factory_scene_lights(const frontend_unified_q3_runtime_factory *o,
    const qa_scene_light **out,size_t *count,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_lights(o->runtime,out,count,e); }
bool frontend_unified_q3_runtime_factory_scene_actor(const frontend_unified_q3_runtime_factory *o,qa_actor_id actor,
    frontend_unified_q3_runtime_scene_owner *out,bool *owned,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_actor(o->runtime,actor,out,owned,e); }
bool frontend_unified_q3_runtime_factory_scene_view_weapon(const frontend_unified_q3_runtime_factory *o,qa_actor_id actor,
    frontend_unified_q3_runtime_scene_owner *out,bool *owned,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_view_weapon(o->runtime,actor,out,owned,e); }
bool frontend_unified_q3_runtime_factory_scene_submit(frontend_unified_q3_runtime_factory *o,
    const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{ return o && frontend_unified_q3_runtime_scene_submit(o->runtime,world,frame,e); }
bool frontend_unified_q3_runtime_factory_hud(frontend_unified_q3_runtime_factory *o,bool *rendered,qa_error *e)
{ return o && frontend_unified_q3_runtime_hud(o->runtime,rendered,e); }
bool frontend_unified_q3_runtime_factory_frame_end(frontend_unified_q3_runtime_factory *o,bool complete,qa_error *e)
{ return o && frontend_unified_q3_runtime_frame_end(o->runtime,complete,e); }
bool frontend_unified_q3_runtime_factory_listener(const frontend_unified_q3_runtime_factory *o,qa_audio_listener *out,bool *present,qa_error *e)
{
    if(!out || !present || !frontend_unified_q3_runtime_factory_idle(o) || !frontend_unified_q3_runtime_factory_current(o))
        return fail(e,"Compiled listener read requires its returned actual factory");
    *present=o->has_listener;if(*present)*out=o->listener;return true; }

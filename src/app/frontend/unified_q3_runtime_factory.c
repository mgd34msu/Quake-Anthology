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
    const qa_q3_movie_checkpoint_refs *movie_refs;
    qa_buffer import_bytes;
    frontend_unified_q3_runtime_factory_refs import_refs;
    unsigned imported_children;
    size_t movie_references,calls;
    bool has_listener,looping,retiring,cleanup_entered,restoring,restored,passive,codec_busy,binding,runtime_bound;
    bool reset_constructor;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static char *copy_text(const char *text)
{ if(!text)return NULL;size_t n=strlen(text)+1;char *out=malloc(n);if(out)memcpy(out,text,n);return out; }
bool frontend_unified_q3_runtime_factory_current(const frontend_unified_q3_runtime_factory *o)
{
    return o && !o->retiring && !o->codec_busy && (!o->restoring || o->binding) && o->options.current(o->options.context,&o->options,false) &&
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
        .receiver=(qa_actor_owner)o->options.receiver,.recipe=frontend_unified_media_recipe(o->options.media),.recipe_provider=o->options.source.provider,
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
            if(!shared || !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&fresh,e))return false;
            if(!qa_audio_music_controls_bind(fresh,shared,e)){qa_audio_music_release(fresh);return false;}
            qa_audio_music_release(o->music);o->music=fresh;
            free(o->intro);free(o->loop);o->intro=o->loop=NULL;o->looping=false;
        }
    }
    if(!o->music && !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&o->music,e))return false;
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
    if(!qa_audio_bank_music_cue(o->sounds,first,QA_AUDIO_Q3,NULL,NULL,&a,e))return false;
    if(!a)return music_returned(o,&origin,frame,first,tail,false,e);
    b=a;if(*tail && strcmp(first,tail) && !qa_audio_bank_music_cue(o->sounds,tail,QA_AUDIO_Q3,NULL,NULL,&b,e)){qa_audio_stream_close(a);return false;}
    qa_audio_music_start(o->music,a,b);o->looping=b!=NULL;
    return music_returned(o,&origin,frame,first,tail,o->looping,e);
}
static bool timescale(void *context,int32_t elapsed,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;qa_native_q3_client_cvar finish,speed,value;
    if(!frontend_unified_q3_client_cvar_read(o->options.client,"cg_timescaleFadeEnd",&finish,e) ||
        !frontend_unified_q3_client_cvar_read(o->options.client,"cg_timescaleFadeSpeed",&speed,e) ||
        !frontend_unified_q3_client_cvar_read(o->options.client,"cg_timescale",&value,e))return false;
    if(value.number==finish.number)return true;
    volatile float product=speed.number*(float)elapsed,delta=product/1000.0f;
    volatile float sum=value.number<finish.number?value.number+delta:value.number-delta;
    float next=value.number<finish.number?fminf(finish.number,sum):fmaxf(finish.number,sum);
    if(!frontend_unified_q3_client_cvar_number(o->options.client,"cg_timescale",next,e))return false;
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
    bool cold=o && ((o->restoring && o->options.frontend->source_restoring) || o->options.frontend->capture);
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
    qa_cvars *vars=o->restoring || o->options.frontend->capture?frontend_unified_q3_client_checkpoint_stage_cvars(o->options.client,
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
    if(!text || !d || !base || !o->movie_references || o->calls==SIZE_MAX || o->restoring || o->codec_busy ||
       !movie_parent(o,&source))return fail(e,"Compiled movie completion lost its actual CLIENT namespace");
    qa_command_context command=*base;command.script="q3-system-movie";command.direct=false;command.console_text=false;
    ++o->calls;bool okay=qa_console_append(d->console,&command,text,e) && movie_parent(o,&source);--o->calls;return okay;
}
static frontend_system_cinematic_source movie_source(frontend_unified_q3_runtime_factory *o,
    const q3n_compiled_source_view *source)
{ return (frontend_system_cinematic_source){.identity={o->options.receiver,o->options.audio_owner,o->options.audio_owner,
        (qa_actor_owner)o->options.receiver,QA_QVM_CGAME,source->basis.physical_seat,source->basis.seat},
    .files=o->options.source.files,.movies=o->movies,.cinematics=o->cinematics,
    .cvars=o->restoring?frontend_unified_q3_client_checkpoint_stage_cvars(o->options.client,
        frontend_unified_q3_runtime_rebind_frame(o->runtime)):frontend_unified_q3_client_cvars(o->options.client),
    .context=o,.current=movie_current,.append=movie_append,.release=movie_release}; }
static bool movie_source_decode(void *context,const frontend_system_cinematic_identity *id,
    frontend_system_cinematic_source *out,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;q3n_compiled_source_view source;
    if(!id || !out || !o->restoring || o->movie_references==SIZE_MAX || !movie_parent(o,&source))
        return fail(e,"Compiled system movie import lost its actual retained role");
    frontend_system_cinematic_source value=movie_source(o,&source);
    const frontend_system_cinematic_identity *actual=&value.identity;
    if(id->source_group!=actual->source_group || id->service_owner!=actual->service_owner || id->audio_bus!=actual->audio_bus ||
       id->source_owner!=actual->source_owner || id->role!=actual->role || id->physical_seat!=actual->physical_seat ||
       id->launch_seat!=actual->launch_seat)return fail(e,"Compiled system movie identity leaves its saved Source role");
    ++o->movie_references;*out=value;return true;
}
static bool movie_asset_encode(void *context,const qa_cinematic_asset *asset,uint64_t *id,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return o->movie_refs && o->movie_refs->asset_encode &&
    o->movie_refs->asset_encode(o->movie_refs->context,asset,id,e); }
static bool movie_asset_decode(void *context,uint64_t id,const char *path,qa_cinematic_asset **out,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return o->movie_refs && o->movie_refs->asset_decode &&
    o->movie_refs->asset_decode(o->movie_refs->context,id,path,out,e); }
static frontend_system_cinematic_refs system_refs(frontend_unified_q3_runtime_factory *o)
{ return (frontend_system_cinematic_refs){.context=o,.source_decode=movie_source_decode,
    .asset_encode=movie_asset_encode,.asset_decode=movie_asset_decode,.publication=o->movie_refs->publication}; }
static bool system_encode(void *context,const qa_q3_system_movie *movie,uint32_t flags,qa_buffer *out,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;frontend_system_cinematic_refs refs=system_refs(o);
    return frontend_system_cinematic_checkpoint(o->options.frontend,movie,flags,&refs,out,e); }
static bool system_decode(void *context,qa_bytes bytes,uint32_t flags,qa_q3_system_movie *out,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;frontend_system_cinematic_refs refs=system_refs(o);
    return frontend_system_cinematic_restore(o->options.frontend,&refs,flags,bytes,out,e); }
static void system_discard(void *context,qa_q3_system_movie *movie)
{ (void)context;frontend_system_cinematic_discard(movie); }
bool frontend_unified_q3_runtime_factory_system_checkpoint(frontend_unified_q3_runtime_factory *o,
    const qa_q3_movie_checkpoint_refs *refs,const qa_q3_system_movie *movie,uint32_t flags,qa_buffer *out,qa_error *e)
{
    if(!o || !refs || o->movie_refs || !frontend_unified_q3_runtime_factory_idle(o) ||
        !o->options.frontend->capture || !o->options.current(o->options.context,&o->options,true))
        return fail(e,"Global system movie capture lost its actual compiled role owner");
    o->movie_refs=refs; bool okay=system_encode(o,movie,flags,out,e); o->movie_refs=NULL; return okay;
}
bool frontend_unified_q3_runtime_factory_system_restore(frontend_unified_q3_runtime_factory *o,
    const qa_q3_movie_checkpoint_refs *refs,qa_bytes bytes,uint32_t flags,qa_q3_system_movie *out,qa_error *e)
{
    if(!o || !refs || o->movie_refs || !o->restoring || !o->options.frontend->source_restoring ||
        !frontend_unified_q3_runtime_factory_idle(o) || !o->options.current(o->options.context,&o->options,true))
        return fail(e,"Global system movie import lost its actual reconstructed compiled role");
    o->movie_refs=refs; bool okay=system_decode(o,bytes,flags,out,e); o->movie_refs=NULL; return okay;
}
static qa_q3_movie_checkpoint_refs backend_refs(frontend_unified_q3_runtime_factory *o)
{ qa_q3_movie_checkpoint_refs refs=*o->movie_refs;refs.context=o;
    refs.asset_encode=movie_asset_encode;refs.asset_decode=movie_asset_decode;
    refs.system_encode=system_encode;refs.system_decode=system_decode;refs.system_discard=system_discard;return refs; }
static bool backend_checkpoint(void *context,const qa_q3_presentation *backend,qa_buffer *out,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;
    if(!o->movie_refs)return fail(e,"Compiled movie capture requires its actual dictionaries");
    qa_q3_movie_checkpoint_refs refs=backend_refs(o);return qa_q3_presentation_media_checkpoint(backend,&refs,out,e); }
static bool backend_restore(void *context,qa_q3_presentation *backend,qa_bytes bytes,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;
    if(!o->movie_refs)return fail(e,"Compiled movie import requires its actual candidate dictionaries");
    qa_q3_movie_checkpoint_refs refs=backend_refs(o);return qa_q3_presentation_media_restore(backend,&refs,o->options.audio_owner,
        (double)o->options.frontend->wall_time_ns/1e6,bytes,e); }
static bool system_movie(void *context,const qa_q3_movie_request *request,qa_q3_system_movie *out,qa_error *e)
{
    frontend_unified_q3_runtime_factory *o=context;const q3n_compiled_frame *frame=entered(o);
    q3n_compiled_source_view source;
    if(!request || !out || !frame || !q3n_compiled_frame_current(frame) || !movie_parent(o,&source) ||
       o->restoring || o->codec_busy || o->calls==SIZE_MAX || o->movie_references==SIZE_MAX)return fail(e,"Compiled system movie requires its actual entered CG role");
    frontend_system_cinematic_source value=movie_source(o,&source);++o->movie_references;++o->calls;
    bool okay=frontend_system_cinematic_open(o->options.frontend,&value,request,out,e);
    if(!okay)--o->movie_references;
    --o->calls;return okay;
}
static bool video_shutdown(void *context,qa_error *e)
{ frontend_unified_q3_runtime_factory *o=context;return frontend_unified_q3_commands_video_reset(o->commands,e); }

static bool create(const frontend_unified_q3_runtime_factory_options *options,bool restoring,
    frontend_unified_q3_runtime_factory **out,qa_error *e)
{
    if(!options || !out || *out || !options->current || !options->retirement_current || !options->send_client || !options->frontend || !options->replica ||
        !options->media || !options->client || !options->input_read || !options->prediction || !options->events ||
        !options->receiver || options->receiver>UINT32_MAX || !options->audio_owner || !options->audio_actor ||
        !options->source.instance || !options->source.content || !options->source.provider || !options->composition.body_hidden || !options->composition.body_submit ||
        !options->composition.player_weapon || options->frontend->source_restoring!=restoring ||
        !options->current(options->context,options,restoring))return fail(e,"Compiled factory requires its real retained Source services");
    frontend_unified_q3_runtime_factory *o=calloc(1,sizeof(*o));if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled CG factory");
    o->options=*options;o->restoring=restoring;*out=o;
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
        .backend_checkpoint=backend_checkpoint,.backend_restore=backend_restore,.video_shutdown=video_shutdown,.player_fx=options->composition};
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
    if(!(restoring?frontend_unified_q3_runtime_services_create_restored(&services,&o->services,e):
            frontend_unified_q3_runtime_services_create(&services,&o->services,e)) ||
        !(restoring?frontend_unified_q3_runtime_services_read_restored(o->services,&actual,e):
            frontend_unified_q3_runtime_services_read(o->services,&actual,e)) ||
        !(restoring?frontend_unified_q3_runtime_create_restored(&actual,&o->runtime,e):
            frontend_unified_q3_runtime_create(&actual,&o->runtime,e)))return false;
    frontend_unified_q3_commands_options commands={.frontend=options->frontend,.replica=options->replica,.client=options->client,
        .runtime=o->runtime,.context=o,.current=command_current,.send_client=send_client};
    bool okay=restoring?frontend_unified_q3_commands_create_restored(&commands,&o->commands,e):
        frontend_unified_q3_commands_create(&commands,&o->commands,e);
    /* Only the actual CLIENT retains observed state/history. These immutable
     * names identify the factory's activation without borrowing a FRAME row. */
    return okay;
}
bool frontend_unified_q3_runtime_factory_create(const frontend_unified_q3_runtime_factory_options *options,
    frontend_unified_q3_runtime_factory **out,qa_error *e)
{ return create(options,false,out,e); }
bool frontend_unified_q3_runtime_factory_create_restored(const frontend_unified_q3_runtime_factory_options *options,
    frontend_unified_q3_runtime_factory **out,qa_error *e)
{ return create(options,true,out,e); }
bool frontend_unified_q3_runtime_factory_idle(const frontend_unified_q3_runtime_factory *o)
{ return !o || (!o->codec_busy && !o->binding && !o->calls && frontend_unified_q3_runtime_idle(o->runtime) && frontend_unified_q3_commands_idle(o->commands) && (!o->music || qa_audio_music_idle(o->music))); }
static bool checkpoint_returned(const frontend_unified_q3_runtime_factory *o)
{
    return o && !o->codec_busy && !o->binding && !o->calls &&
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
    qa_buffer_free(&o->import_bytes);free(o->source_instance);free(o->source_content);free(o);*out=NULL;return true;
}
bool frontend_unified_q3_runtime_factory_constructor_abort(frontend_unified_q3_runtime_factory **out,qa_error *e)
{
    if(!out || !*out)return true;
    frontend_unified_q3_runtime_factory *o=*out;q3n_compiled_source_view source;
    if(o->restoring || !frontend_unified_q3_runtime_factory_idle(o) ||
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
bool frontend_unified_q3_runtime_factory_topology_read(const frontend_unified_q3_runtime_factory *o,
    frontend_unified_q3_runtime_factory_topology *out,qa_error *e)
{
    if(!out || !o || ((!o->services || !o->runtime || !o->commands) && !o->retiring) ||
       !checkpoint_returned(o) ||
       !o->options.current(o->options.context,&o->options,true) ||
       !frontend_unified_q3_client_checkpoint_stage_current(o->options.client,
            frontend_unified_q3_runtime_rebind_frame(o->runtime)))
        return fail(e,"Compiled topology requires its actual returned Source capture parent");
    frontend_unified_q3_runtime_factory_topology value={.services=o->services,.commands=o->commands,
        .music=o->music,.cinematics=o->cinematics,.receiver=o->options.receiver,.audio_owner=o->options.audio_owner};
    if(o->runtime) {
        if(!frontend_unified_q3_runtime_owners_read(o->runtime,&value.children,e))return false;
    } else if(o->services && !frontend_unified_q3_runtime_services_caches(o->services,
        &value.children.media,&value.children.clients,e))return false;
    *out=value;return true;
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
bool frontend_unified_q3_runtime_factory_rebind_restore(frontend_unified_q3_runtime_factory *o,
    const frontend_unified_q3_client_frame *f,qa_error *e)
{
    return o && o->restoring && o->restored && o->options.current(o->options.context,&o->options,true) &&
        frontend_unified_q3_runtime_rebind_restore(o->runtime,f,e);
}
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
bool frontend_unified_q3_runtime_factory_draw(frontend_unified_q3_runtime_factory *o,bool *rendered,qa_error *e)
{ return o && frontend_unified_q3_runtime_draw(o->runtime,rendered,e); }
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
qa_command_result frontend_unified_q3_runtime_factory_command(frontend_unified_q3_runtime_factory *o,const qa_command_invocation *call,qa_error *e)
{ return o?frontend_unified_q3_commands_execute(o->commands,call,e):QA_COMMAND_UNHANDLED; }

static bool codec_ready(const frontend_unified_q3_runtime_factory *o,bool reading,qa_error *e)
{
    if(!o || o->reset_constructor || ((!o->runtime || !o->commands || !o->services) && !o->retiring) ||
       (o->restoring!=reading && !(o->restoring && !reading && o->passive &&
           (frontend_remote_unified_retired(o->options.replica) ||
            frontend_unified_q3_client_retirement_departed(o->options.client)))) || !checkpoint_returned(o) ||
       !o->options.current(o->options.context,&o->options,true) ||
       !frontend_unified_q3_client_checkpoint_stage_current(o->options.client,
            frontend_unified_q3_runtime_rebind_frame(o->runtime)))
        return fail(e,"Compiled factory codec requires its actual retained Source graph");
    return frontend_remote_unified_checkpoint_current(o->options.replica,e);
}
static bool codec_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if(!qa_source_save_count(io,&count,64u*1024u*1024u))return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset)return fail(io->error,"Truncated compiled factory child");
    *bytes=(qa_bytes){io->input.data+io->offset,count};io->offset+=count;return true;
}
static bool listener_actor(qa_source_save_io *io,const qa_audio_checkpoint_refs *refs,uint64_t *actor)
{
    qa_buffer encoded={0};qa_bytes bytes={0};bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    bool okay=writing?refs->encode && refs->encode(refs->context,QA_AUDIO_REFERENCE_ACTOR,*actor,&encoded,io->error):true;
    if(writing)bytes=(qa_bytes){encoded.data,encoded.size};
    if(okay)okay=codec_blob(io,&bytes);
    if(okay && !writing)okay=refs->decode && refs->decode(refs->context,QA_AUDIO_REFERENCE_ACTOR,bytes,actor,io->error);
    qa_buffer_free(&encoded);return okay;
}
static uint32_t factory_presence(const frontend_unified_q3_runtime_factory *o)
{ return (o->services?1u:0u)|(o->runtime?2u:0u)|(o->commands?4u:0u); }
static bool codec_fields(qa_source_save_io *io,frontend_unified_q3_runtime_factory *copy,
    const frontend_unified_q3_runtime_factory_refs *refs,uint32_t *presence,bool *attached,const char **intro,const char **loop)
{
    uint8_t magic[5]={'U','Q','3','F','4'};bool primary_view=copy->options.primary_view;
    q3n_compiled_source_view source;
    uint64_t receiver=copy->options.receiver,audio_owner=copy->options.audio_owner;
    if(!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"UQ3F4",sizeof(magic)) ||
       !qa_source_save_bool(io,&primary_view) || primary_view!=copy->options.primary_view ||
       !q3n_compiled_source_fields(io,frontend_unified_q3_client_source(copy->options.client)) ||
       !qa_source_save_u64(io,&receiver) || receiver!=copy->options.receiver ||
       !qa_source_save_u64(io,&audio_owner) || audio_owner!=copy->options.audio_owner ||
       !q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(copy->options.client),&source,io->error) ||
       !qa_source_save_bool(io,&copy->retiring) || !qa_source_save_u32(io,presence) ||
       (*presence!=7u && (!copy->retiring || (*presence!=3u && *presence!=1u && *presence!=0u))) ||
       !qa_source_save_bool(io,attached) || !qa_source_save_text(io,intro) || !qa_source_save_text(io,loop) ||
       !qa_source_save_bool(io,&copy->looping) || (!!*intro!=!!*loop) || (copy->looping && !*intro) ||
       !qa_source_save_bool(io,&copy->has_listener))return false;
    qa_audio_listener *v=&copy->listener;
    if(copy->has_listener) {
        if(!qa_source_save_u32(io,&v->seat) || v->seat!=source.basis.physical_seat ||
           !listener_actor(io,&refs->audio,&v->actor) || !qa_source_save_vec3(io,&v->origin) || !qa_vec_finite(v->origin))return false;
        for(unsigned i=0;i<3;++i)if(!qa_source_save_vec3(io,v->axis+i) || !qa_vec_finite(v->axis[i]))return false;
        if(!qa_source_save_f32(io,&v->gain) || !isfinite(v->gain) || v->gain<0 ||
           !qa_source_save_bool(io,&v->underwater))return false;
    }
    return q3n_compiled_source_checkpoint_current(&source);
}
bool frontend_unified_q3_runtime_factory_checkpoint(frontend_unified_q3_runtime_factory *o,
    const frontend_unified_q3_runtime_factory_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!refs || !out || out->data || out->size || !codec_ready(o,false,e))return false;
    qa_audio_music *bus=qa_audio_engine_bus_music(o->options.frontend->audio,o->options.audio_owner);
    if(bus && bus!=o->music)return fail(e,"Compiled soundtrack capture lost its genuine engine alias");
    if(o->music && !qa_audio_music_profile_is(o->music,qa_audio_engine_rate(o->options.frontend->audio),QA_AUDIO_Q3,true))
        return fail(e,"Compiled soundtrack capture changed its real source player mode");
    frontend_unified_q3_runtime_factory copy=*o;bool attached=bus!=NULL;uint32_t presence=factory_presence(o);
    const char *intro=o->intro,*loop=o->loop;qa_source_save_io io={0};qa_buffer children[4]={{0}};
    o->codec_busy=true;o->movie_refs=&refs->movies;
    bool okay=qa_source_save_writer(&io,qa_application_session(o->options.frontend->application),e) &&
        codec_fields(&io,&copy,refs,&presence,&attached,&intro,&loop) &&
        (!o->services || frontend_unified_q3_runtime_services_checkpoint(o->services,&refs->clients,children,e)) &&
        (!o->runtime || frontend_unified_q3_runtime_checkpoint(o->runtime,children+1,e)) &&
        (!o->commands || frontend_unified_q3_commands_checkpoint(o->commands,children+2,e)) &&
        (!o->music || qa_audio_music_checkpoint(o->music,children+3,e));
    for(unsigned i=0;okay && i<4;++i){qa_bytes bytes={children[i].data,children[i].size};okay=codec_blob(&io,&bytes);}
    o->movie_refs=NULL;o->codec_busy=false;
    if(okay)okay=codec_ready(o,false,e) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);for(unsigned i=0;i<4;++i)qa_buffer_free(children+i);return okay;
}
static bool import_refs_equal(const frontend_unified_q3_runtime_factory_refs *a,
    const frontend_unified_q3_runtime_factory_refs *b)
{
    return a->clients.context==b->clients.context && a->clients.resource_decode==b->clients.resource_decode &&
        a->audio.context==b->audio.context && a->audio.decode==b->audio.decode &&
        a->movies.context==b->movies.context && a->movies.asset_decode==b->movies.asset_decode &&
        a->movies.playback.context==b->movies.playback.context &&
        a->movies.playback.material_decode==b->movies.playback.material_decode &&
        a->movies.publication.context==b->movies.publication.context &&
        a->movies.publication.decode==b->movies.publication.decode;
}
bool frontend_unified_q3_runtime_factory_restore(frontend_unified_q3_runtime_factory *o,
    const frontend_unified_q3_runtime_factory_refs *refs,qa_bytes bytes,qa_error *e)
{
    if(!refs || !codec_ready(o,true,e) || o->restored || o->music || o->intro || o->loop)return false;
    if(o->import_bytes.data && (bytes.size!=o->import_bytes.size || !bytes.data ||
       memcmp(bytes.data,o->import_bytes.data,bytes.size) || !import_refs_equal(refs,&o->import_refs)))
        return fail(e,"Compiled factory retry changed its actual import or reference owners");
    if(o->import_bytes.data)bytes=(qa_bytes){o->import_bytes.data,o->import_bytes.size};
    frontend_unified_q3_runtime_factory copy=*o;bool attached=false;uint32_t presence=0;const char *intro=NULL,*loop=NULL;
    qa_source_save_io io={0};qa_bytes children[4]={{0}};qa_audio_music *music=NULL;bool held=false;
    char *intro_copy=NULL,*loop_copy=NULL;
    o->codec_busy=true;o->movie_refs=&refs->movies;
    bool okay=qa_source_save_reader(&io,qa_application_session(o->options.frontend->application),bytes,e) &&
        codec_fields(&io,&copy,refs,&presence,&attached,&intro,&loop);
    for(unsigned i=0;okay && i<4;++i)okay=codec_blob(&io,children+i);
    if(okay)okay=qa_source_save_finish(&io,NULL);
    if(okay && !o->import_bytes.data) {
        o->import_bytes.data=malloc(bytes.size);
        if(!o->import_bytes.data)okay=frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled factory import");
        else {memcpy(o->import_bytes.data,bytes.data,bytes.size);o->import_bytes.size=bytes.size;o->import_refs=*refs;}
    }
    if(okay) {
        o->retiring=copy.retiring;
        if(!(presence&4u))okay=frontend_unified_q3_commands_destroy(&o->commands,e);
        if(okay && !(presence&2u))okay=frontend_unified_q3_runtime_destroy(&o->runtime,e);
        if(okay && !(presence&1u))okay=frontend_unified_q3_runtime_services_destroy(&o->services,e);
    }
    if(okay && intro){intro_copy=copy_text(intro);loop_copy=copy_text(loop);
        if(!intro_copy || !loop_copy)okay=frontend_fail(e,QA_ERROR_MEMORY,"Importing compiled soundtrack names");}
    qa_audio_music_controls *controls=frontend_music_sources_controls(o->options.frontend->music_sources);
    if(okay && attached) {
        qa_buffer actual={0};music=qa_audio_engine_bus_music(o->options.frontend->audio,o->options.audio_owner);
        q3n_compiled_source_view source;
        okay=music && children[3].size && controls &&
            q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,e) &&
            qa_audio_engine_music_ready(o->options.frontend->audio,o->options.audio_owner,source.basis.physical_seat,1) &&
            (qa_audio_music_controls_is(music,controls) || qa_audio_music_controls_bind(music,controls,e)) &&
            qa_audio_music_checkpoint(music,&actual,e) && actual.size==children[3].size &&
            !memcmp(actual.data,children[3].data,actual.size);
        qa_buffer_free(&actual);if(okay)okay=held=qa_audio_music_retain(music,e);
    } else if(okay && children[3].size)okay=held=qa_audio_music_restore(children[3],&music,e);
    if(okay && music)okay=controls && qa_audio_music_profile_is(music,qa_audio_engine_rate(o->options.frontend->audio),QA_AUDIO_Q3,true) &&
        (qa_audio_music_controls_is(music,controls) || qa_audio_music_controls_bind(music,controls,e));
    if(okay)okay=(!attached || music) && (!intro || music);
    while(okay && o->imported_children<3) {
        switch(o->imported_children) {
        case 0:okay=presence&1u?frontend_unified_q3_runtime_services_restore(o->services,&refs->clients,children[0],e):!children[0].size;break;
        case 1:okay=presence&2u?frontend_unified_q3_runtime_restore(o->runtime,children[1],e):!children[1].size;break;
        case 2:okay=presence&4u?frontend_unified_q3_commands_restore(o->commands,children[2],e):!children[2].size;break;
        }
        if(okay)++o->imported_children;
    }
    o->movie_refs=NULL;o->codec_busy=false;
    if(okay)okay=codec_ready(o,true,e);
    if(okay){o->music=music;o->intro=intro_copy;o->loop=loop_copy;o->looping=copy.looping;
        o->has_listener=copy.has_listener;o->listener=copy.listener;o->restored=true;
        qa_buffer_free(&o->import_bytes);memset(&o->import_refs,0,sizeof(o->import_refs));o->imported_children=0;}
    else {if(held)qa_audio_music_release(music);free(intro_copy);free(loop_copy);}
    qa_source_save_dispose(&io);return okay;
}
bool frontend_unified_q3_runtime_factory_restore_ready(const frontend_unified_q3_runtime_factory *o,qa_error *e)
{
    frontend_unified_q3_runtime_options options;frontend_unified_q3_runtime_owners children;
    if(!o || !o->restored || !codec_ready(o,true,e))
        return fail(e,"Compiled factory import has not returned its actual owned caches");
    if(o->services && !frontend_unified_q3_runtime_services_read_restored(o->services,&options,e))return false;
    if(o->runtime && (!o->services || !frontend_unified_q3_runtime_owners_read(o->runtime,&children,e) ||
       options.media!=children.media || options.clients!=children.clients ||
       !frontend_unified_q3_runtime_restore_ready(o->runtime,e)))return false;
    return o->retiring || (o->services && o->runtime && o->commands);
}
bool frontend_unified_q3_runtime_factory_restore_bind(frontend_unified_q3_runtime_factory *o,qa_error *e)
{
    const frontend_unified_q3_client_frame *frame=o?frontend_unified_q3_runtime_rebind_frame(o->runtime):NULL;
    if(!o || o->retiring || !o->runtime || !o->restoring || !o->restored ||
       (frame?!checkpoint_returned(o):!frontend_unified_q3_runtime_factory_idle(o)) ||
       !o->options.current(o->options.context,&o->options,frame!=NULL))return false;
    o->binding=true;
    if(!o->runtime_bound){bool okay=frontend_unified_q3_runtime_restore_bind(o->runtime,e);
        if(!okay){o->binding=false;return false;}o->runtime_bound=true;}
    if(o->music){frontend_music_origin origin=music_origin(o);
        if(frontend_music_sources_restore_origin_matches(o->options.frontend->music_sources,&origin) &&
           !frontend_music_sources_restore_origin(o->options.frontend->music_sources,&origin,e)){
            o->binding=false;return false;}}
    o->binding=false;o->restoring=false;
    return frame?checkpoint_returned(o):frontend_unified_q3_runtime_factory_current(o);
}
bool frontend_unified_q3_runtime_factory_restore_passive_finish(frontend_unified_q3_runtime_factory *o,qa_error *e)
{
    if(!o || !o->options.frontend->source_restoring ||
       frontend_remote_unified_restore_pending(o->options.replica) ||
       !(frontend_remote_unified_retired(o->options.replica) ||
         frontend_unified_q3_client_retirement_departed(o->options.client)) ||
       !frontend_unified_q3_runtime_factory_restore_ready(o,e) ||
       (o->runtime && !frontend_unified_q3_runtime_restore_passive_finish(o->runtime,e)))
        return fail(e,"Passive compiled factory requires its actually retired installed replica");
    if(o->music) {
        frontend_music_origin origin=music_origin(o);
        if(frontend_music_sources_restore_origin_matches(o->options.frontend->music_sources,&origin) &&
           !frontend_music_sources_restore_origin(o->options.frontend->music_sources,&origin,e))return false;
    }
    o->passive=true;return true;
}

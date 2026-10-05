#include "received_music.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

struct frontend_received_music {
    qa_frontend *frontend;
    qa_audio_engine *engine;
    frontend_received_music **slot;
    frontend_music_origin declaration,origin;
    qa_launch_instance_lease *metadata;
    qa_audio_music *player;
    uint64_t bus;
    bool importing,saved_attached,saved_selected;
};
static bool fail(qa_error *e,const char *text)
{return frontend_fail(e,QA_ERROR_ARGUMENT,text);}
static bool current(void *context,const frontend_music_origin *origin)
{
    frontend_received_music *o=context;
    if(!o || !origin || !o->slot || *o->slot!=o || o->frontend->audio!=o->engine ||
        origin->music!=o->player || origin->bus!=o->bus || !o->declaration.current)return false;
    frontend_music_origin actual=*origin;actual.context=o->declaration.context;
    return o->declaration.current(actual.context,&actual);
}
static bool stop(void *context,qa_error *e)
{
    frontend_received_music *o=context;
    if(!frontend_received_music_idle(o))return fail(e,"Received music retains an entered player callback");
    qa_audio_music_stop(o->player);
    return !qa_audio_music_playing(o->player) || fail(e,"Received music stop retained actual playback");
}
static void bind(frontend_received_music *o,const frontend_music_origin *declaration)
{
    o->declaration=*declaration;o->origin=*declaration;
    o->origin.bus=o->bus;o->origin.music=o->player;o->origin.context=o;
    o->origin.current=current;o->origin.stop=stop;
}
bool frontend_received_music_create(qa_frontend *f,const frontend_music_origin *declaration,
    frontend_received_music **out,qa_error *e)
{
    if(!f || !f->audio || !f->music_sources || !declaration || !declaration->current ||
        declaration->kind!=FRONTEND_MUSIC_REMOTE || !out || *out || f->capture || f->source_restoring)
        return fail(e,"Received music requires its actual private CLIENT content declaration");
    const qa_product *product=qa_catalog_product(declaration->catalog,declaration->product);
    if(!product || (product->family!=QA_GAME_Q1 && product->family!=QA_GAME_Q2))
        return fail(e,"Received music has no actual Q1 or Q2 product declaration");
    qa_audio_family family=product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q1;
    frontend_received_music *o=calloc(1,sizeof(*o));
    if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining received music player");
    o->frontend=f;o->engine=f->audio;o->slot=out;
    const qa_cvar_view *gain=qa_cvars_find(qa_application_cvars(f->application),"bgmvolume");
    if(!gain || !isfinite(gain->number) || gain->number<0 || gain->number>FLT_MAX){free(o);return fail(e,"Received music lacks its published canonical volume");}
    bool ok=frontend_source_identity_allocate(f,&o->bus,e) &&
        qa_audio_music_create(qa_audio_engine_rate(f->audio),family,true,&o->player,e) &&
        qa_audio_music_controls_bind(o->player,frontend_music_sources_controls(f->music_sources),e) &&
        qa_audio_music_volume(o->player,(float)gain->number,e);
    if(!ok){qa_audio_music_destroy(o->player);free(o);return false;}
    if(declaration->descriptor && !qa_launch_instance_retain_metadata(declaration->descriptor,&o->metadata,e)){
        qa_audio_music_destroy(o->player);free(o);return false;}
    frontend_music_origin held=*declaration;
    if(o->metadata)held.descriptor=qa_launch_instance_lease_view(o->metadata);
    bind(o,&held);*out=o;
    if(current(o,&o->origin))return true;
    *out=NULL;qa_launch_instance_lease_release(o->metadata);qa_audio_music_destroy(o->player);free(o);
    return fail(e,"Received music changed its retained CLIENT declaration");
}
qa_audio_music *frontend_received_music_player(const frontend_received_music *o)
{return o?o->player:NULL;}
bool frontend_received_music_bus(const frontend_received_music *o,uint64_t *out)
{
    if(!o || !out || !o->slot || *o->slot!=o || !o->bus || o->frontend->audio!=o->engine)return false;
    *out=o->bus;return true;
}
bool frontend_received_music_idle(const frontend_received_music *o)
{return !o || (o->slot && *o->slot==o && o->frontend->audio==o->engine &&
    (qa_audio_music_idle(o->player) || (o->importing && (!o->player || qa_audio_music_controls_restore_pending(o->player)))));}
bool frontend_received_music_play(frontend_received_music *o,const char *cue,qa_error *e)
{
    if(!o || o->importing || o->frontend->capture || o->frontend->resource_inventory || o->frontend->source_restoring ||
        !frontend_received_music_idle(o) || !current(o,&o->origin))
        return fail(e,"Received music playback lost its actual returned CLIENT parent");
    return frontend_music_sources_explicit_play(o->frontend->music_sources,&o->origin,cue,e);
}
bool frontend_received_music_pause(frontend_received_music *o,bool paused,qa_error *e)
{
    if(!o || o->importing || o->frontend->capture || o->frontend->resource_inventory || o->frontend->source_restoring ||
        !frontend_received_music_idle(o) || !current(o,&o->origin))
        return fail(e,"Received music pause lost its actual CLIENT parent");
    return frontend_music_sources_explicit_pause(o->frontend->music_sources,&o->origin,paused,e);
}
bool frontend_received_music_selected(const frontend_received_music *o)
{
    return o && !o->importing && frontend_received_music_idle(o) && current((void *)o,&o->origin) &&
        frontend_music_sources_explicit_selected(o->frontend->music_sources,&o->origin);
}
bool frontend_received_music_destroy(frontend_received_music **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_received_music *o=*slot;
    if(!frontend_received_music_idle(o) || !frontend_music_sources_explicit_retire(o->frontend->music_sources,o,e))return false;
    qa_audio_music *attached=qa_audio_engine_bus_music(o->engine,o->bus);
    if(attached && attached!=o->player)return fail(e,"Received music bus belongs to another actual player");
    if(attached){qa_audio_engine_remove_music(o->engine,o->bus);
        if(qa_audio_engine_bus_music(o->engine,o->bus))return fail(e,"Received music retains its actual engine attachment");}
    qa_audio_music_destroy(o->player);qa_launch_instance_lease_release(o->metadata);free(o);*slot=NULL;return true;
}

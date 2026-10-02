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
static bool bytes(qa_source_save_io *io,qa_bytes *value)
{
    size_t size=value->size;
    if(!qa_source_save_count(io,&size,UINT32_C(16777216)))return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)return qa_source_save_bytes(io,(void *)value->data,size);
    if(size>io->input.size-io->offset)return false;
    *value=(qa_bytes){io->input.data+io->offset,size};io->offset+=size;return true;
}
bool frontend_received_music_fields(qa_frontend *f,frontend_received_music **slot,qa_source_save_io *io,
    const qa_audio_checkpoint_refs *refs,qa_error *e)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=*slot!=NULL;
    if(!qa_source_save_bool(io,&present))return false;
    if(!present)return true;
    if(!f || !f->audio || !refs || (reading?!refs->decode:!refs->encode) ||
        (reading?*slot!=NULL:!frontend_received_music_idle(*slot)))return fail(e,"Received music cold state lacks its actual audio graph");
    if(reading){*slot=calloc(1,sizeof(**slot));if(!*slot)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining imported received music");
        (*slot)->frontend=f;(*slot)->engine=f->audio;(*slot)->slot=slot;(*slot)->importing=true;
        if(!frontend_source_identity_allocate(f,&(*slot)->bus,e))return false;}
    frontend_received_music *o=*slot;qa_buffer encoded={0};qa_bytes receipt={0};bool ok=true;
    if(!reading){ok=current(o,&o->origin) && refs->encode(refs->context,QA_AUDIO_REFERENCE_BUS,o->bus,&encoded,e);receipt=(qa_bytes){encoded.data,encoded.size};}
    if(ok)ok=bytes(io,&receipt) && receipt.size;
    if(ok && reading){uint64_t resolved=0;
        ok=refs->decode(refs->context,QA_AUDIO_REFERENCE_BUS,receipt,&resolved,e) && resolved==o->bus;
        if(!ok && (!e || e->code==QA_OK))fail(e,"Received music bus receipt differs from its actual imported group owner");}
    qa_buffer_free(&encoded);
    bool attached=!reading && qa_audio_engine_bus_music(o->engine,o->bus)==o->player;
    bool selected=!reading && frontend_music_sources_explicit_selected(f->music_sources,&o->origin);
    if(ok)ok=qa_source_save_bool(io,&attached) && qa_source_save_bool(io,&selected);
    if(reading){o->saved_attached=attached;o->saved_selected=selected;}
    qa_buffer player={0};qa_bytes state={0};
    if(ok && !reading && !attached){ok=qa_audio_music_checkpoint(o->player,&player,e);state=(qa_bytes){player.data,player.size};}
    if(ok)ok=bytes(io,&state) && (attached?!state.size:state.size!=0);
    if(ok && reading && !attached)ok=qa_audio_music_restore(state,&o->player,e);
    qa_buffer_free(&player);return ok;
}
bool frontend_received_music_restore_finish(frontend_received_music *o,const frontend_music_origin *declaration,qa_error *e)
{
    if(!o)return true;
    if(!o->frontend->source_restoring || o->frontend->audio!=o->engine || !declaration)
        return fail(e,"Received music import lacks its actual retained CLIENT declaration");
    if(!o->importing){
        bool same=o->declaration.catalog==declaration->catalog && o->declaration.product==declaration->product &&
            o->declaration.files==declaration->files && o->declaration.receiver==declaration->receiver &&
            o->declaration.physical_seat==declaration->physical_seat && o->declaration.recipe==declaration->recipe &&
            ((o->declaration.descriptor && declaration->descriptor && o->declaration.descriptor->storage==declaration->descriptor->storage) ||
                (!o->declaration.descriptor && !declaration->descriptor));
        return same && current(o,&o->origin);
    }
    if(o->saved_attached){o->player=qa_audio_engine_bus_music(o->engine,o->bus);
        if(!o->player || !qa_audio_music_retain(o->player,e))return fail(e,"Received music import lost its actual attached audio player");
        o->saved_attached=false;}
    const qa_product *product=qa_catalog_product(declaration->catalog,declaration->product);
    if(!product || (product->family!=QA_GAME_Q1 && product->family!=QA_GAME_Q2))
        return fail(e,"Received music import has no actual Q1 or Q2 product declaration");
    qa_audio_family family=product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q1;
    if(!qa_audio_music_profile_is(o->player,qa_audio_engine_rate(o->engine),family,true) ||
        !qa_audio_music_controls_bind(o->player,frontend_music_sources_controls(o->frontend->music_sources),e))return false;
    if(declaration->descriptor && !o->metadata && !qa_launch_instance_retain_metadata(declaration->descriptor,&o->metadata,e))return false;
    frontend_music_origin held=*declaration;
    if(o->metadata)held.descriptor=qa_launch_instance_lease_view(o->metadata);
    bind(o,&held);
    if(!current(o,&o->origin))return fail(e,"Received music import changed its actual CLIENT/content parent");
    bool matches=frontend_music_sources_restore_origin_matches(o->frontend->music_sources,&o->origin);
    if(matches!=o->saved_selected)return fail(e,"Received music selection does not match its real saved music origin");
    if(matches && !frontend_music_sources_restore_origin(o->frontend->music_sources,&o->origin,e))return false;
    o->importing=false;return true;
}

#include "cinematic_handles_private.h"
#include <math.h>

static bool text(qa_source_save_io *io,char **value)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?strlen(*value):0;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,*value,count);
    char *copy=malloc(count+1);
    if (!copy) return q3cin_fail(io->error,QA_ERROR_MEMORY,"Restoring actual cinematic path");
    if (!qa_source_save_bytes(io,copy,count) || memchr(copy,0,count)) { free(copy); return false; }
    copy[count]=0; *value=copy; return true;
}
static bool blob(qa_source_save_io *io,qa_buffer *value)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?value->size:0;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        value->data=count?malloc(count):NULL; value->size=count;
        if (count && !value->data) return q3cin_fail(io->error,QA_ERROR_MEMORY,"Restoring actual cinematic continuation");
    }
    return qa_source_save_bytes(io,value->data,count);
}
static bool image(qa_source_save_io *io,qa_q3_cinematic_handles *owner,uint32_t index,
    const qa_q3_cinematic_handles_refs *refs)
{
    const qa_cinematic_image_checkpoint_refs *images=&refs->movies.publication;
    uint64_t key=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && (!q3cin_scratch_valid(owner,index,owner->scratch[index]) ||
        !images->encode || !images->encode(images->context,owner->scratch[index],&key,io->error))) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded=NULL;
        if (!images->decode || !images->decode(images->context,key,&decoded,io->error) ||
            !q3cin_scratch_valid(owner,index,decoded))
            return q3cin_fail(io->error,QA_ERROR_FORMAT,"Saved cinematic image differs from its physical scratch slot");
        qa_scene_image_retain(decoded); owner->scratch[index]=decoded;
    }
    return true;
}
static bool state(qa_source_save_io *io,q3cin_movie *movie)
{
    return qa_source_save_u64(io,&movie->uploaded) && qa_source_save_bool(io,&movie->redefine) &&
        qa_source_save_bool(io,&movie->occupied) && qa_source_save_bool(io,&movie->uploaded_shader) &&
        qa_source_save_bool(io,&movie->dirty) && qa_source_save_i32(io,&movie->play_on_walls) &&
        qa_source_save_i32(io,&movie->status) && qa_source_save_u32(io,&movie->width) &&
        qa_source_save_u32(io,&movie->height) && qa_source_save_u32(io,&movie->draw_width) &&
        qa_source_save_u32(io,&movie->draw_height) && movie->width && movie->width<=512 &&
        movie->height && movie->height<=512 && movie->draw_width<=movie->width && movie->draw_height<=movie->height &&
        (movie->status==0 || movie->status==1 || movie->status==2 || movie->status==5);
}
static bool local(qa_source_save_io *io,q3cin_movie *movie,const qa_q3_cinematic_handles_refs *refs,double anchor,
    qa_roq_scratch *scratch)
{
    const qa_q3_movie_checkpoint_refs *m=&refs->movies;
    uint64_t asset=0;
    bool owned=movie->asset!=NULL,playing=movie->playback!=NULL;
    if (!qa_source_save_bool(io,&owned) || !qa_source_save_bool(io,&playing) || (playing && !owned)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE && owned && (!refs->asset_encode ||
        !refs->asset_encode(refs->context,movie->source,movie->asset,&asset,io->error))) return false;
    if (!qa_source_save_u64(io,&asset) || !qa_source_save_u64(io,&movie->bus) || !state(io,movie)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && owned) {
        if (!refs->asset_decode || !refs->asset_decode(refs->context,movie->source,asset,movie->path,&movie->asset,io->error) || !movie->asset ||
            !refs->audio_bus_decode || !refs->audio_bus_decode(refs->context,movie->source,movie->bus,&movie->bus,io->error)) return false;
    }
    if (owned) {
        qa_cinematic_source actual=qa_cinematic_asset_source(movie->asset);
        if (actual.format!=QA_CINEMATIC_ROQ || !actual.name || strcmp(actual.name,movie->path)) return false;
    }
    if (!owned && (asset || movie->bus)) return false;
    if (!playing) return movie->status==0 && movie->uploaded==UINT64_MAX && !movie->redefine &&
        !movie->draw_width && !movie->draw_height;
    qa_cinematic_checkpoint saved={0}; qa_buffer playback={0};
    bool ok=true;
    if (io->direction==QA_SOURCE_SAVE_WRITE) ok=qa_cinematic_capture(movie->playback,&saved,io->error) &&
        qa_cinematic_checkpoint_encode(&saved,&m->playback,&playback,io->error);
    if (ok) ok=blob(io,&playback);
    if (ok && io->direction==QA_SOURCE_SAVE_READ) {
        ok=qa_cinematic_checkpoint_decode((qa_bytes){playback.data,playback.size},&m->playback,&saved,io->error);
        qa_cinematic_options options=q3cin_options(movie->source,movie->flags,movie->bus);
        options.roq_scratch=scratch;
        if (movie->flags&16u) {
            if (saved.target.kind!=QA_CINEMATIC_MATERIAL) ok=false;
            else options.target=saved.target;
        }
        qa_cinematic_source source=qa_cinematic_asset_source(movie->asset); source.name=movie->path;
        double source_anchor=anchor;
        if (ok) ok=refs->source_clock_read &&
            refs->source_clock_read(refs->context,movie->source,&source_anchor,io->error) &&
            isfinite(source_anchor);
        if (ok) ok=qa_cinematic_restore_qualified(&source,&options,&saved,source_anchor,&movie->playback,io->error);
    }
    qa_cinematic_checkpoint_free(&saved); qa_buffer_free(&playback); return ok;
}
static bool system_movie(qa_source_save_io *io,q3cin_movie *movie,const qa_q3_cinematic_handles_refs *refs)
{
    qa_buffer saved={0}; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if ((reading && (!refs->system_decode || !refs->system_discard)) || (!reading && !refs->system_encode))
        return q3cin_fail(io->error,QA_ERROR_ARGUMENT,"Actual system cinematic codec is unbound");
    bool ok=state(io,movie) && movie->occupied && movie->status!=2;
    if (ok) ok=reading || refs->system_encode(refs->context,movie->source,&movie->system,movie->flags,&saved,io->error);
    if (ok) ok=blob(io,&saved);
    if (ok && reading) ok=refs->system_decode(refs->context,movie->source,(qa_bytes){saved.data,saved.size},movie->flags,&movie->system,io->error) &&
        movie->system.status && movie->system.end && movie->system.release;
    qa_buffer_free(&saved); return ok;
}
static bool fields(qa_source_save_io *io,qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_handles_refs *refs,double anchor,qa_q3_cinematic_handles *actual)
{
    uint8_t magic[4]={'Q','3','C','H'}; uint32_t version=2;
    if (!refs || !qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3CH",4) ||
        !qa_source_save_u32(io,&version) || version!=2) return false;
    if (!qa_source_save_i32(io,&owner->selected_handle) || !qa_source_save_i32(io,&owner->decoder_handle) ||
        owner->selected_handle < -1 || owner->selected_handle>=16 ||
        owner->decoder_handle < -1 || owner->decoder_handle>=16) return false;
    for (uint32_t i=0;i<16;++i) {
        if (!image(io,owner,i,refs)) return false;
        q3cin_movie *movie=&owner->movies[i]; bool present=movie->source!=NULL;
        if (movie->pending || !qa_source_save_bool(io,&present)) return false;
        if (!present) continue;
        uint64_t source=0;
        if (io->direction==QA_SOURCE_SAVE_WRITE && (!refs->source_encode ||
            !refs->source_encode(refs->context,movie->source,&source,io->error))) return false;
        if (!qa_source_save_u64(io,&source)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ && (!refs->source_decode ||
            !refs->source_decode(refs->context,source,&movie->source,io->error) ||
            !movie->source || movie->source->handles!=actual)) return false;
        if (!text(io,&movie->path) || !qa_source_save_u32(io,&movie->flags) ||
            !qa_source_save_f32(io,&movie->rect.x) || !qa_source_save_f32(io,&movie->rect.y) ||
            !qa_source_save_f32(io,&movie->rect.width) || !qa_source_save_f32(io,&movie->rect.height) ||
            !isfinite(movie->rect.x) || !isfinite(movie->rect.y) || !isfinite(movie->rect.width) || !isfinite(movie->rect.height)) return false;
        if (movie->flags&1u) { if (!system_movie(io,movie,refs)) return false; }
        else {
            for (uint32_t j=0;movie->occupied && j<i;++j) if (owner->movies[j].occupied && !(owner->movies[j].flags&1u) &&
                owner->movies[j].source->options.files==movie->source->options.files &&
                owner->movies[j].source->options.media==movie->source->options.media &&
                !strcmp(movie->path,owner->movies[j].path)) return false;
            if (!local(io,movie,refs,anchor,owner->decoder_scratch)) return false;
            uint64_t revision=0;
            if (movie->playback && !qa_cinematic_checkpoint_revision_read(movie->playback,&revision)) return false;
            if ((movie->uploaded!=UINT64_MAX && movie->uploaded>revision) ||
                (movie->redefine && movie->uploaded!=UINT64_MAX)) return false;
        }
    }
    qa_buffer shared={0};
    bool ok=io->direction==QA_SOURCE_SAVE_READ || qa_roq_scratch_capture(owner->decoder_scratch,&shared,io->error);
    if (ok) ok=blob(io,&shared);
    if (ok && io->direction==QA_SOURCE_SAVE_READ)
        ok=qa_roq_scratch_restore(owner->decoder_scratch,(qa_bytes){shared.data,shared.size},io->error);
    qa_buffer_free(&shared); return ok;
}
static void discard(qa_q3_cinematic_handles *owner,const qa_q3_cinematic_handles_refs *refs)
{
    for (uint32_t i=0;i<16;++i) {
        q3cin_movie *movie=&owner->movies[i];
        if ((movie->system.context || movie->system.release) && refs && refs->system_discard)
            refs->system_discard(refs->context,movie->source,&movie->system);
        qa_cinematic_restore_discard(movie->playback); qa_cinematic_asset_release(movie->asset);
        free(movie->path); qa_scene_image_release(owner->scratch[i]);
    }
}
bool qa_q3_cinematic_handles_checkpoint(qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_handles_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!qa_q3_cinematic_handles_idle(owner) || !out || out->data || out->size)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Global cinematic capture requires its idle actual pool");
    owner->busy=true; qa_q3_cinematic_handles saved=*owner; qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) { owner->busy=false; return false; }
    bool ok=fields(&io,&saved,refs,0,owner) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) q3cin_fail(error,QA_ERROR_FORMAT,"Actual global cinematic state is inconsistent");
    qa_source_save_dispose(&io); owner->busy=false; return ok;
}
bool qa_q3_cinematic_handles_restore(qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_handles_refs *refs,double anchor,qa_bytes bytes,qa_error *error)
{
    if (!qa_q3_cinematic_handles_idle(owner) || !isfinite(anchor) || anchor<0)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Global cinematic import requires an empty actual candidate pool");
    for (size_t i=0;i<16;++i) if (owner->movies[i].source)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Global cinematic candidate retains live handle slots");
    owner->busy=true; qa_q3_cinematic_handles saved=*owner;
    memset(saved.movies,0,sizeof(saved.movies)); memset(saved.scratch,0,sizeof(saved.scratch));
    /* Fullscreen leases are decoded by their genuine Source role. They borrow
     * this same cold physical workspace before the numeric slots install. */
    saved.decoder_scratch=owner->decoder_scratch;
    qa_roq_scratch_retain(saved.decoder_scratch);
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) {
        qa_roq_scratch_release(saved.decoder_scratch); owner->busy=false; return false;
    }
    bool ok=fields(&io,&saved,refs,anchor,owner) && qa_source_save_finish(&io,NULL);
    if (ok) {
        qa_roq_scratch_release(owner->decoder_scratch);
        owner->decoder_scratch=saved.decoder_scratch; saved.decoder_scratch=NULL;
        owner->selected_handle=saved.selected_handle; owner->decoder_handle=saved.decoder_handle;
        for (size_t i=0;i<16;++i) {
            qa_cinematic_restore_commit(saved.movies[i].playback);
            qa_scene_image_release(owner->scratch[i]); owner->scratch[i]=saved.scratch[i];
            owner->movies[i]=saved.movies[i];
        }
    } else {
        discard(&saved,refs);
        if (error && error->code==QA_OK) q3cin_fail(error,QA_ERROR_FORMAT,"Saved global cinematic ownership is inconsistent");
    }
    qa_roq_scratch_release(saved.decoder_scratch);
    qa_source_save_dispose(&io); owner->busy=false; return ok;
}

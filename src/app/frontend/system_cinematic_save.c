#include "system_cinematic_private.h"
#include "qa/media_save.h"
#include "qa/source_save.h"
#include "material_movies.h"
#include <math.h>

static bool text(qa_source_save_io *io,char **value)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE && *value?strlen(*value):0;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return *value && qa_source_save_bytes(io,*value,size);
    if (size==SIZE_MAX) return false;
    char *copy=malloc(size+1);
    if (!copy) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining cold source cinematic text");
    if (!qa_source_save_bytes(io,copy,size) || (size && memchr(copy,0,size))) { free(copy); return false; }
    copy[size]=0; *value=copy; return true;
}
static bool blob(qa_source_save_io *io,qa_buffer *buffer)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?buffer->size:0;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        buffer->data=size?malloc(size):NULL; buffer->size=size;
        if (size && !buffer->data) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining cold cinematic decoder bytes");
    }
    return qa_source_save_bytes(io,buffer->data,size);
}
static bool identity(qa_source_save_io *io,frontend_system_cinematic_identity *id)
{
    uint32_t role=id->role;
    if (!qa_source_save_u64(io,&id->source_group) || !qa_source_save_u64(io,&id->service_owner) ||
        !qa_source_save_u64(io,&id->audio_bus) || !qa_source_save_u64(io,&id->source_owner) ||
        !qa_source_save_u32(io,&role) || !qa_source_save_u32(io,&id->physical_seat) || !qa_source_save_u32(io,&id->launch_seat) ||
        !id->source_group || !id->service_owner || !id->audio_bus || id->audio_bus==QA_AUDIO_NO_OWNER || !id->source_owner ||
        (role!=QA_QVM_UI && role!=QA_QVM_CGAME) || id->physical_seat>=QA_INPUT_LOCAL_SEATS) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) id->role=(qa_qvm_role)role;
    return true;
}
static bool same_identity(const frontend_system_cinematic_identity *a,const frontend_system_cinematic_identity *b)
{
    return a->source_group==b->source_group && a->service_owner==b->service_owner && a->audio_bus==b->audio_bus &&
        a->source_owner==b->source_owner && a->role==b->role && a->physical_seat==b->physical_seat && a->launch_seat==b->launch_seat;
}
static bool fields(qa_source_save_io *io,frontend_system_cinematic *row,uint32_t flags,
    const frontend_system_cinematic_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','S','C','N'}; uint32_t phase=row->phase,reason=row->ending_reason;
    frontend_system_cinematic_identity id=row->source.identity;
    bool numeric=row->numeric_source!=NULL;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QSCN",4) || !qa_source_save_bool(io,&numeric) || !qa_source_save_i32(io,&row->numeric_handle) ||
        (numeric?(row->numeric_handle<0 || row->numeric_handle>=16):row->numeric_handle!=-1) ||
        !identity(io,&id) || !qa_source_save_u32(io,&phase) || phase>SYSTEM_STOPPED ||
        !qa_source_save_bool(io,&row->screen) || !qa_source_save_bool(io,&row->loop) ||
        !qa_source_save_bool(io,&row->hold) || !qa_source_save_bool(io,&row->silent) ||
        !qa_source_save_f64(io,&row->clock_ms) || !isfinite(row->clock_ms) || row->clock_ms<0 ||
        !qa_source_save_bool(io,&row->ending) || !qa_source_save_u32(io,&reason) || reason>QA_CINEMATIC_STOPPED ||
        !qa_source_save_bool(io,&row->appended) || !qa_source_save_bool(io,&row->focus_paused) ||
        !text(io,&row->path) || (numeric?!*row->path:!frontend_system_cinematic_path_valid(row->path)) || !(flags&1u) ||
        row->loop!=((flags&2u)!=0) || row->hold!=((flags&4u)!=0) || row->silent!=((flags&8u)!=0) ||
        (row->screen!=(phase==SYSTEM_PLAYING)) || (row->appended && !row->ending)) return false;
    bool retained_next=row->nextmap!=NULL;
    if (!qa_source_save_bool(io,&retained_next) || (retained_next && !text(io,&row->nextmap)) ||
        (row->appended && (!row->nextmap || !*row->nextmap)) || (retained_next && !row->ending)) return false;
    if (reading) {
        row->phase=(system_cinematic_phase)phase; row->ending_reason=(qa_cinematic_end)reason;
        frontend_system_cinematic_source source={0};
        if (!refs || !refs->source_decode || !refs->source_decode(refs->context,&id,&source,io->error)) return false;
        row->source=source;
        row->numeric_source=numeric?source.cinematics:NULL;
        if (!same_identity(&id,&source.identity) || !frontend_system_cinematic_source_valid(row->frontend,&source))
            return frontend_fail(io->error,QA_ERROR_FORMAT,"Saved system cinematic leaves its genuine candidate source lease");
        if (numeric) {
            qa_q3_cinematic_source_options actual;
            if (!row->numeric_source || qa_q3_cinematic_source_handles(row->numeric_source)!=row->frontend->source_cinematics ||
                !qa_q3_cinematic_source_read(row->numeric_source,&actual) || actual.files!=source.files ||
                actual.seat!=id.physical_seat || actual.audio_bus(actual.context)!=id.audio_bus)
                return frontend_fail(io->error,QA_ERROR_FORMAT,"Saved fullscreen decoder leaves its genuine global Source lease");
        }
    } else if (!frontend_system_cinematic_source_current(row))
        return frontend_fail(io->error,QA_ERROR_ARGUMENT,"System cinematic capture lost its actual source lease");
    bool active=phase==SYSTEM_PLAYING;
    uint64_t asset=0; qa_buffer playback={0},publication={0}; qa_cinematic_checkpoint saved={0};
    bool ok=true;
    if (active) {
        if (!reading) ok=row->asset && row->movie && refs && refs->asset_encode &&
            refs->asset_encode(refs->context,row->asset,&asset,io->error);
        if (ok) ok=qa_source_save_u64(io,&asset) && asset!=0;
        if (ok && reading) ok=refs->asset_decode &&
            refs->asset_decode(refs->context,asset,row->path,&row->asset,io->error) && row->asset;
        if (ok && !reading) ok=qa_cinematic_capture(row->movie,&saved,io->error) &&
            qa_cinematic_checkpoint_encode(&saved,NULL,&playback,io->error) &&
            qa_cinematic_presentation_checkpoint(row->movie,&row->frontend->frame,&refs->publication,&publication,io->error);
        if (ok) ok=blob(io,&playback) && blob(io,&publication);
        if (ok && reading) {
            ok=qa_cinematic_checkpoint_decode((qa_bytes){playback.data,playback.size},NULL,&saved,io->error);
            qa_cinematic_options options=frontend_system_cinematic_options(row,row->frontend->audio);
            qa_cinematic_source source=qa_cinematic_asset_source(row->asset); source.name=row->path;
            double anchor=row->clock_ms;
            if (ok && numeric) ok=frontend_material_movies_cinematic_source_clock_read(row->frontend,row->numeric_source,&anchor,io->error);
            if (ok) ok=(!numeric || source.format==QA_CINEMATIC_ROQ) &&
                qa_cinematic_restore_qualified(&source,&options,&saved,anchor,&row->movie,io->error) &&
                qa_cinematic_presentation_restore(row->movie,&row->frontend->frame,&refs->publication,
                    (qa_bytes){publication.data,publication.size},io->error);
        }
    } else if (!reading && (row->movie || row->asset)) ok=false;
    qa_cinematic_checkpoint_free(&saved); qa_buffer_free(&playback); qa_buffer_free(&publication); return ok;
}
bool frontend_system_cinematic_checkpoint(qa_frontend *f,const qa_q3_system_movie *handle,uint32_t flags,
    const frontend_system_cinematic_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!f || !handle || !out || out->data || out->size || !frontend_system_cinematic_capture_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic capture requires its actual returned numeric owner");
    frontend_system_cinematic *row=NULL;
    for (frontend_system_cinematic *held=f->system_cinematics;held;held=held->next) if (held==handle->context) { row=held; break; }
    qa_q3_system_movie actual={0}; if (row) frontend_system_cinematic_handle(row,&actual);
    if (!row || handle->status!=actual.status || handle->end!=actual.end || handle->release!=actual.release || handle->playback!=actual.playback)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic handle belongs to another source owner");
    row->busy=true; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && fields(&io,row,flags,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); row->busy=false;
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"System cinematic continuation is inconsistent");
    return ok;
}
bool frontend_system_cinematic_restore(qa_frontend *f,const frontend_system_cinematic_refs *refs,
    uint32_t flags,qa_bytes bytes,qa_q3_system_movie *out,qa_error *error)
{
    if (!f || !out || out->context || out->status || out->end || out->release || out->playback || !f->source_restoring ||
        !frontend_system_cinematic_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic restore requires an empty qualified cold candidate");
    frontend_system_cinematic *row=calloc(1,sizeof(*row));
    if (!row) return frontend_fail(error,QA_ERROR_MEMORY,"Restoring actual source system cinematic owner");
    row->frontend=f; row->numeric_handle=-1; row->restore_pending=true; row->busy=true;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,row,flags,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); row->busy=false;
    if (ok) ok=frontend_system_cinematic_restore_attach(row,error);
    if (!ok) {
        qa_cinematic_restore_discard(row->movie); qa_cinematic_asset_release(row->asset);
        free(row->path); free(row->nextmap);
        if (row->source.release) row->source.release(row->source.context);
        free(row);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved system cinematic is inconsistent");
        return false;
    }
    frontend_system_cinematic_handle(row,out); return true;
}
void frontend_system_cinematic_discard(qa_q3_system_movie *handle)
{
    if (!handle || !handle->context) return;
    frontend_system_cinematic *row=handle->context;
    qa_q3_system_movie actual={0}; frontend_system_cinematic_handle(row,&actual);
    if (!row->restore_pending || row->busy || handle->status!=actual.status ||
        handle->end!=actual.end || handle->release!=actual.release || handle->playback!=actual.playback) return;
    *handle=(qa_q3_system_movie){0}; frontend_system_cinematic_row_free(row,true);
}

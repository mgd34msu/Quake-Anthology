#include "internal.h"
#include "qa/q3_presentation_media_save.h"
#include "qa/cinematic_restore.h"

static bool text(qa_source_save_io *io, char **owned)
{
    bool present=io->direction==QA_SOURCE_SAVE_WRITE && *owned;
    size_t count=present?strlen(*owned):0;
    if (!qa_source_save_bool(io,&present) || !present ||
        !qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,*owned,count);
    if (count==SIZE_MAX) return false;
    char *copy=malloc(count+1);
    if (!copy) return q3p_fail(io->error,QA_ERROR_MEMORY,"Retaining Q3 movie path");
    if (!qa_source_save_bytes(io,copy,count) || (count && memchr(copy,0,count))) { free(copy); return false; }
    copy[count]=0; *owned=copy; return true;
}
static bool asset(qa_source_save_io *io, const qa_q3_movie_checkpoint_refs *refs,
    const char *path, qa_cinematic_asset **value)
{
    uint64_t key=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && (!*value || !refs || !refs->asset_encode ||
        !refs->asset_encode(refs->context,*value,&key,io->error))) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && (!refs || !refs->asset_decode ||
        !refs->asset_decode(refs->context,key,path,value,io->error) || !*value)) return false;
    return true;
}
static bool blob(qa_source_save_io *io, qa_buffer *buffer)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?buffer->size:0;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        buffer->data=count?malloc(count):NULL; buffer->size=count;
        if (count && !buffer->data) return q3p_fail(io->error,QA_ERROR_MEMORY,"Retaining Q3 movie continuation");
    }
    return qa_source_save_bytes(io,buffer->data,count);
}
static bool local(qa_source_save_io *io, qa_q3_presentation *p, q3p_movie *movie,
    const qa_q3_movie_checkpoint_refs *refs, uint64_t bus, double anchor)
{
    if (!text(io,&movie->path) || !asset(io,refs,movie->path,&movie->asset) ||
        !qa_source_save_u32(io,&movie->flags) || (movie->flags&1u) ||
        !qa_source_save_f32(io,&movie->rect.x) || !qa_source_save_f32(io,&movie->rect.y) ||
        !qa_source_save_f32(io,&movie->rect.width) || !qa_source_save_f32(io,&movie->rect.height)) return false;
    qa_cinematic_checkpoint saved={0}; qa_buffer playback={0}, publication={0}; bool ok=true;
    if (io->direction==QA_SOURCE_SAVE_WRITE)
        ok=qa_cinematic_capture(movie->local,&saved,io->error) &&
           qa_cinematic_checkpoint_encode(&saved,refs?&refs->playback:NULL,&playback,io->error) &&
           qa_cinematic_presentation_checkpoint(movie->local,p->frame,refs?&refs->publication:NULL,&publication,io->error);
    if (ok) ok=blob(io,&playback) && blob(io,&publication);
    if (ok && io->direction==QA_SOURCE_SAVE_READ) {
        ok=qa_cinematic_checkpoint_decode((qa_bytes){playback.data,playback.size},refs?&refs->playback:NULL,&saved,io->error);
        qa_cinematic_options options={.clock=p->options.clock,
            .target={.kind=movie->flags&16u?QA_CINEMATIC_MATERIAL:QA_CINEMATIC_SEAT},
            .loop=(movie->flags&2u)!=0,.hold=(movie->flags&4u)!=0,.silent=(movie->flags&8u)!=0,
            .audio=p->options.audio,.audio_bus=bus,.gain=1,
            .audio_audience={QA_CINEMATIC_AUDIO_SEAT,p->options.seat},
            .context=p->options.context,.diagnostic=p->options.print};
        if (movie->flags&16u) options.target.id.material=bus; else options.target.id.seat=p->options.seat;
        qa_cinematic_source source=qa_cinematic_asset_source(movie->asset); source.name=movie->path;
        if (ok) ok=qa_cinematic_restore_qualified(&source,&options,&saved,anchor,&movie->local,io->error) &&
            qa_cinematic_presentation_restore(movie->local,p->frame,refs?&refs->publication:NULL,
                (qa_bytes){publication.data,publication.size},io->error);
    }
    qa_cinematic_checkpoint_free(&saved); qa_buffer_free(&playback); qa_buffer_free(&publication); return ok;
}
static bool sources(qa_source_save_io *io, qa_q3_presentation *p, const qa_q3_movie_checkpoint_refs *refs)
{
    size_t count=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE) for (const q3p_movie_source *s=p->movie_sources;s;s=s->next) ++count;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size/24:SIZE_MAX)) return false;
    q3p_movie_source **tail=&p->movie_sources;
    for (size_t i=0;i<count;++i) {
        q3p_movie_source *source=io->direction==QA_SOURCE_SAVE_WRITE?*tail:NULL;
        char *request=source?source->request:NULL; char *path=source?source->path:NULL; qa_cinematic_asset *value=source?source->asset:NULL;
        if (!text(io,&request) || !text(io,&path) || !asset(io,refs,path,&value)) {
            if (io->direction==QA_SOURCE_SAVE_READ) { free(request); free(path); qa_cinematic_asset_release(value); }
            return false;
        }
        char *qualified=q3p_movie_path(request,io->error);
        bool path_ok=qualified && !strcmp(qualified,path); free(qualified);
        if (!path_ok) {
            if (io->direction==QA_SOURCE_SAVE_READ) { free(request); free(path); qa_cinematic_asset_release(value); }
            return false;
        }
        if (io->direction==QA_SOURCE_SAVE_READ) {
            size_t size=strlen(request)+1;
            if (size>SIZE_MAX-sizeof(*source)) { free(request); free(path); qa_cinematic_asset_release(value); return false; }
            source=calloc(1,sizeof(*source)+size);
            if (!source) { free(request); free(path); qa_cinematic_asset_release(value); return q3p_fail(io->error,QA_ERROR_MEMORY,"Restoring Q3 prepared movie source"); }
            source->asset=value; source->path=path; memcpy(source->request,request,size); free(request); *tail=source;
        }
        tail=&source->next;
    }
    return true;
}
static int source_compare(const void *left, const void *right)
{
    const q3p_movie_source *const *a=left,*const *b=right; return strcmp((*a)->request,(*b)->request);
}
static bool topology(const qa_q3_presentation *p, qa_error *error)
{
    size_t count=0; for (const q3p_movie_source *s=p->movie_sources;s;s=s->next) ++count;
    if (count>SIZE_MAX/sizeof(q3p_movie_source*)) return false;
    const q3p_movie_source **sources=count?malloc(count*sizeof(*sources)):NULL;
    if (count && !sources) return q3p_fail(error,QA_ERROR_MEMORY,"Qualifying Q3 movie request aliases");
    size_t index=0; for (const q3p_movie_source *s=p->movie_sources;s;s=s->next) sources[index++]=s;
    if (count>1) qsort(sources,count,sizeof(*sources),source_compare);
    bool unique=true; for (size_t i=1;i<count;++i) if (!strcmp(sources[i-1]->request,sources[i]->request)) { unique=false; break; }
    free(sources); if (!unique) return false;
    for (size_t i=0;i<16;++i) {
        const q3p_movie *movie=&p->movies[i]; if (movie->kind!=Q3P_MOVIE_LOCAL) continue;
        bool found=false;
        for (const q3p_movie_source *s=p->movie_sources;s;s=s->next)
            if (!strcmp(movie->path,s->path) && movie->asset==s->asset) { found=true; break; }
        if (!found) return false;
        for (size_t j=0;j<i;++j)
            if (p->movies[j].kind==Q3P_MOVIE_LOCAL && !strcmp(movie->path,p->movies[j].path)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_q3_presentation *p, const qa_q3_movie_checkpoint_refs *refs,
    uint64_t bus, double anchor)
{
    uint8_t magic[4]={'Q','3','M','S'}; uint32_t schema=1;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3MS",4) || !qa_source_save_u32(io,&schema) || schema!=1 || !sources(io,p,refs)) return false;
    for (size_t i=0;i<16;++i) {
        uint32_t kind=p->movies[i].kind;
        if (!qa_source_save_u32(io,&kind) || (kind!=Q3P_MOVIE_EMPTY && kind!=Q3P_MOVIE_LOCAL))
            return q3p_fail(io->error,QA_ERROR_UNSUPPORTED,"Q3 delegated/pending movie requires its actual external lifetime owner");
        if (io->direction==QA_SOURCE_SAVE_READ) p->movies[i].kind=(q3p_movie_kind)kind;
        if (kind==Q3P_MOVIE_LOCAL && !local(io,p,&p->movies[i],refs,bus,anchor)) return false;
    }
    return topology(p,io->error);
}
static void discard(qa_q3_presentation *p)
{
    for (size_t i=0;i<16;++i) {
        qa_cinematic_destroy(p->movies[i].local); qa_cinematic_asset_release(p->movies[i].asset); free(p->movies[i].path);
    }
    while (p->movie_sources) {
        q3p_movie_source *source=p->movie_sources; p->movie_sources=source->next;
        qa_cinematic_asset_release(source->asset); free(source->path); free(source);
    }
}
bool qa_q3_presentation_media_checkpoint(const qa_q3_presentation *p,
    const qa_q3_movie_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!p || !out || p->busy || p->options.assets->busy)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 movie capture requires an idle presentation");
    qa_source_save_io io; qa_q3_presentation saved=*p;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=fields(&io,&saved,refs,0,0) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) q3p_fail(error,QA_ERROR_FORMAT,"Q3 retained movie ownership is inconsistent");
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_presentation_media_restore(qa_q3_presentation *p,
    const qa_q3_movie_checkpoint_refs *refs, uint64_t bus, double anchor, qa_bytes bytes, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || p->movie_sources)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 movie restore requires an empty idle candidate");
    for (size_t i=0;i<16;++i) if (p->movies[i].kind!=Q3P_MOVIE_EMPTY)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 candidate has existing movie ownership");
    qa_q3_presentation saved=*p; memset(saved.movies,0,sizeof(saved.movies)); saved.movie_sources=NULL;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=fields(&io,&saved,refs,bus,anchor) && qa_source_save_finish(&io,NULL);
    if (ok) { memcpy(p->movies,saved.movies,sizeof(p->movies)); p->movie_sources=saved.movie_sources; }
    else { discard(&saved); if (error && error->code==QA_OK) q3p_fail(error,QA_ERROR_FORMAT,"Saved Q3 movie ownership is inconsistent"); }
    qa_source_save_dispose(&io); return ok;
}

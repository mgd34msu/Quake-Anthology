#include "cinematic_internal.h"
#include "material_image.h"
#include "qa/cinematic_presentation_save.h"
#include <string.h>

static bool fields(qa_source_save_io *io, const qa_cinematic *movie, const qa_scene_frame *frame,
    const qa_cinematic_image_checkpoint_refs *refs, const qa_scene_image **out,
    bool *bound, uint64_t *revision, uint64_t *sequence)
{
    uint8_t magic[4]={'Q','A','C','P'}; uint32_t schema=1;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QACP",4) ||
        !qa_source_save_u32(io,&schema) || schema!=1) return false;
    bool present=movie->image!=NULL;
    uint64_t key=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && present &&
        (!refs || !refs->encode || !refs->encode(refs->context,movie->image,&key,io->error)))
        return cinematic_fail(io->error,"Cinematic publication image encoder is unavailable");
    if (!qa_source_save_bool(io,&present) || !qa_source_save_u64(io,&key) ||
        !qa_source_save_bool(io,bound) || !qa_source_save_u64(io,revision) || !qa_source_save_u64(io,sequence)) return false;
    if (!present && (key || *bound || *revision!=UINT64_MAX || *sequence))
        return cinematic_fail(io->error,"Absent cinematic publication image contains retained state");
    if (*bound && !frame) return cinematic_fail(io->error,"Cinematic publication frame owner is unavailable");
    bool initial=present && *revision==UINT64_MAX &&
        movie->options.target.kind==QA_CINEMATIC_MATERIAL && movie->revision!=UINT64_MAX;
    if (present && (!*bound || (!initial && *revision>movie->revision)))
        return cinematic_fail(io->error,"Cinematic publication revision/frame is inconsistent");
    if (io->direction==QA_SOURCE_SAVE_READ && present) {
        if (!refs || !refs->decode || !refs->decode(refs->context,key,out,io->error) || !*out)
            return cinematic_fail(io->error,"Cinematic publication image reference is absent");
    }
    if (present) {
        const qa_scene_image *image=*out;
        if (!image->name || strcmp(image->name,movie->name) || image->kind!=QA_SCENE_RGBA8 || image->level_count!=1 || !image->levels ||
            image->wrap!=QA_SCENE_CLAMP || image->filter!=QA_SCENE_LINEAR)
            return cinematic_fail(io->error,"Cinematic publication image has incompatible metadata");
        if (initial || *revision==movie->revision) {
            static const uint8_t black[4]={0,0,0,255};
            uint32_t width=movie->has_picture?movie->picture.width:1, height=movie->has_picture?movie->picture.height:1;
            const void *pixels=movie->has_picture?movie->picture.rgba.data:black;
            size_t bytes=movie->has_picture?movie->picture.rgba.size:sizeof(black);
            bool transparent=initial || (!movie->has_picture && movie->options.target.kind==QA_CINEMATIC_MATERIAL);
            if (transparent && !cinematic_initial_dimensions(movie,&width,&height,&bytes,io->error)) return false;
            if (image->levels[0].width!=width || image->levels[0].height!=height || image->levels[0].bytes!=bytes ||
                (bytes && (!image->levels[0].pixels || (!transparent && (!pixels || memcmp(image->levels[0].pixels,pixels,bytes))))))
                return cinematic_fail(io->error,"Cinematic current publication differs from retained decoded frame");
            if (transparent) {
                const uint8_t *actual=image->levels[0].pixels;
                for (size_t i=0;i<bytes;++i) if (actual[i])
                    return cinematic_fail(io->error,"Initial shader movie pixels differ from the retained transparent surface");
            }
        }
    }
    return true;
}
bool qa_cinematic_presentation_checkpoint(const qa_cinematic *movie, const qa_scene_frame *frame,
    const qa_cinematic_image_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!movie || !out || out->data || out->size || movie->busy || movie->faulted || movie->restore_pending ||
        (movie->image_frame && movie->image_frame!=frame))
        return cinematic_fail(error,"Cinematic publication capture requires an idle matching frame");
    qa_cinematic *owner=(qa_cinematic *)movie;
    owner->busy=true;
    bool bound=movie->image_frame!=NULL; uint64_t revision=movie->image_revision, sequence=movie->image_sequence;
    const qa_scene_image *image=movie->image; qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) { owner->busy=false; return false; }
    bool ok=fields(&io,movie,frame,refs,&image,&bound,&revision,&sequence) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); owner->busy=false; return ok;
}
bool qa_cinematic_presentation_restore(qa_cinematic *movie, const qa_scene_frame *frame,
    const qa_cinematic_image_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!movie || movie->busy || movie->faulted || !movie->restore_pending || movie->image || movie->image_frame ||
        movie->image_revision!=UINT64_MAX || movie->image_sequence)
        return cinematic_fail(error,"Cinematic publication restore requires an empty idle candidate cache");
    movie->busy=true;
    const qa_scene_image *image=NULL; bool bound=false; uint64_t revision=UINT64_MAX,sequence=0;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { movie->busy=false; return false; }
    bool ok=fields(&io,movie,frame,refs,&image,&bound,&revision,&sequence) && qa_source_save_finish(&io,NULL);
    if (ok) {
        qa_scene_image_retain(image); movie->image=(qa_scene_image*)image;
        movie->image_frame=bound?frame:NULL; movie->image_revision=revision; movie->image_sequence=sequence;
    }
    qa_source_save_dispose(&io); movie->busy=false; return ok;
}

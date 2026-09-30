#include "library_internal.h"
#include "qa/media_library_save.h"
#include "qa/scene_save.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

size_t qa_media_library_record_count(const qa_media_library *library)
{
    size_t count=0;
    if (library) for (const qa_cinematic_asset *asset=library->assets;asset;asset=asset->next) ++count;
    return count;
}
const qa_cinematic_asset *qa_media_library_record_at(const qa_media_library *library, size_t index)
{
    const qa_cinematic_asset *asset=library?library->assets:NULL;
    while (asset && index--) asset=asset->next;
    return asset;
}
qa_scene_resources *qa_media_library_resource_owner(const qa_media_library *library)
{ return library?library->resources:NULL; }

static bool refs_ready(const qa_media_library_checkpoint_refs *refs)
{ return refs && refs->resource_encode && refs->resource_decode && refs->image_encode && refs->image_decode; }
static bool text(qa_source_save_io *io, char **value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t length=reading?0:strlen(*value);
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (reading) {
        if (length==SIZE_MAX) return false;
        *value=malloc(length+1);
        if (!*value) {
            qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved media name");
            return false;
        }
        (*value)[length]=0;
    }
    return qa_source_save_bytes(io,*value,length) && !memchr(*value,0,length);
}
static bool provenance(qa_source_save_io *io, const qa_resource *resource)
{
    const char *path=qa_resource_path(resource);
    size_t length=strlen(path), saved_length=length;
    qa_sha256_digest digest=*qa_resource_digest(resource), saved=digest;
    if (!qa_source_save_count(io,&saved_length,SIZE_MAX) || saved_length!=length) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) {
        if (!qa_source_save_bytes(io,(void *)path,length)) return false;
    } else {
        if (length>io->input.size-io->offset || memcmp(io->input.data+io->offset,path,length)) return false;
        io->offset+=length;
    }
    return qa_source_save_bytes(io,saved.bytes,sizeof(saved.bytes)) && qa_sha256_equal(&digest,&saved);
}
static bool image_owner(const qa_media_library *library, const qa_scene_image *image)
{
    const qa_scene_resources *owners[]={library->resources}; size_t owner;
    return image && qa_scene_image_owner_index(owners,1,image,&owner);
}
static bool record(qa_source_save_io *io, qa_media_library *library, qa_cinematic_asset *asset,
                   const qa_media_library_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint32_t kind=asset->source.format;
    if (!qa_source_save_u32(io,&kind) || kind>QA_CINEMATIC_IMAGE || !text(io,&asset->name)) return false;
    qa_cinematic_format extension;
    if (!qa_media_asset_format(asset->name,&extension,io->error) || extension!=(qa_cinematic_format)kind) return false;
    if (reading) { asset->source.format=(qa_cinematic_format)kind; asset->source.name=asset->name; asset->source.asset=asset; }
    uint32_t width=asset->width, height=asset->height;
    uint64_t resource_key=0;
    if (!reading && (!asset->references || !asset->source_record || asset->source.asset!=asset ||
        asset->source.name!=asset->name || !qa_sha256_equal(&asset->digest,qa_resource_digest(asset->source_record)) ||
        !refs->resource_encode(refs->context,asset->source_record,&resource_key,io->error))) return false;
    if (!qa_source_save_u32(io,&width) || !qa_source_save_u32(io,&height) || !width || !height ||
        !qa_source_save_u64(io,&resource_key)) return false;
    if (reading) {
        const qa_resource *resource=NULL;
        if (!refs->resource_decode(refs->context,resource_key,&resource,io->error) || !resource) return false;
        qa_resource_retain((qa_resource *)resource); asset->source_record=(qa_resource *)resource;
        asset->digest=*qa_resource_digest(resource);
    }
    if (!provenance(io,asset->source_record)) return false;
    if (kind==QA_CINEMATIC_IMAGE) {
        uint64_t image_key=0;
        if (!reading && (!image_owner(library,asset->source.data.image) ||
            !refs->image_encode(refs->context,asset->source.data.image,&image_key,io->error))) return false;
        if (!qa_source_save_u64(io,&image_key)) return false;
        if (reading) {
            const qa_scene_image *image=NULL;
            if (!refs->image_decode(refs->context,image_key,&image,io->error) || !image_owner(library,image) ||
                !image->level_count || !image->levels || image->kind!=QA_SCENE_RGBA8) return false;
            qa_scene_image_retain(image); asset->source.data.image=image;
            asset->width=image->levels[0].width; asset->height=image->levels[0].height;
        }
        const qa_scene_image *image=asset->source.data.image;
        if (!image || image->kind!=QA_SCENE_RGBA8 || !image->level_count ||
            image->levels[0].width!=width || image->levels[0].height!=height) return false;
    } else if (reading && !qa_media_asset_load(library,asset->source_record,asset,io->error)) return false;
    return asset->width==width && asset->height==height;
}
static bool same_identity(const qa_cinematic_asset *a, const qa_cinematic_asset *b)
{ return a->source.format==b->source.format && qa_sha256_equal(&a->digest,&b->digest); }

bool qa_media_library_checkpoint(const qa_media_library *library, const qa_media_library_checkpoint_refs *refs,
                                 qa_buffer *out, qa_error *error)
{
    if (!library || !out || !refs_ready(refs)) return cinematic_fail(error,"Media capture requires an owner and resolvers");
    qa_source_save_io io; uint8_t magic[4]={'Q','M','L','B'}; uint32_t schema=1;
    size_t count=qa_media_library_record_count(library);
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=qa_source_save_bytes(&io,magic,4) && qa_source_save_u32(&io,&schema) && qa_source_save_count(&io,&count,SIZE_MAX);
    for (const qa_cinematic_asset *asset=library->assets;ok && asset;asset=asset->next) {
        for (const qa_cinematic_asset *prior=library->assets;prior!=asset;prior=prior->next)
            if (same_identity(prior,asset)) { ok=false; break; }
        if (ok) {
            qa_cinematic_asset saved=*asset;
            /* record checks the real asset self-pointer before writing fields. */
            saved.source.asset=&saved;
            ok=asset->source.asset==asset && record(&io,(qa_media_library *)library,&saved,refs);
        }
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) cinematic_fail(error,"Invalid retained media library");
    qa_source_save_dispose(&io); return ok;
}
bool qa_media_library_restore(qa_media_library *library, qa_bytes bytes, const qa_media_library_checkpoint_refs *refs,
                              qa_error *error)
{
    if (!library || !refs_ready(refs)) return cinematic_fail(error,"Media restore requires a candidate owner and resolvers");
    qa_source_save_io io; uint8_t magic[4]; uint32_t schema=0; size_t count=0;
    qa_cinematic_asset **saved=NULL, **installed=NULL;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QMLB",4) && qa_source_save_u32(&io,&schema) && schema==1 &&
        qa_source_save_count(&io,&count,bytes.size/60) && count<=SIZE_MAX/sizeof(*saved);
    if (ok && count) {
        saved=calloc(count,sizeof(*saved)); installed=calloc(count,sizeof(*installed));
        if (!saved || !installed) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating restored media cache"); ok=false; }
    }
    for (size_t i=0;ok && i<count;++i) {
        saved[i]=calloc(1,sizeof(*saved[i]));
        if (!saved[i]) { qa_error_set(error,QA_ERROR_MEMORY,i,"Allocating restored media asset"); ok=false; break; }
        saved[i]->references=1;
        ok=record(&io,library,saved[i],refs);
        for (size_t j=0;ok && j<i;++j) if (same_identity(saved[j],saved[i])) ok=false;
    }
    for (qa_cinematic_asset *current=library->assets;ok && current;current=current->next) {
        size_t match=0;
        while (match<count && !same_identity(current,saved[match])) ++match;
        if (!current->references || current->source.asset!=current || match==count || installed[match]) { ok=false; break; }
        /* Existing clients retain this heap asset. Its source name is private to
         * the asset; cinematic playback copies it and retains its own decoder. */
        installed[match]=current;
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    if (ok) {
        qa_cinematic_asset *head=NULL, **link=&head;
        for (size_t i=0;i<count;++i) {
            qa_cinematic_asset *asset=saved[i];
            if (installed[i]) {
                qa_cinematic_asset old=*installed[i];
                size_t references=old.references;
                *installed[i]=*asset; installed[i]->references=references;
                installed[i]->source.asset=installed[i];
                *asset=old; asset->references=1; asset->next=NULL; asset->source.asset=asset;
                qa_cinematic_asset_release(asset); saved[i]=NULL;
                asset=installed[i];
            } else saved[i]=NULL;
            asset->next=NULL; *link=asset; link=&asset->next;
        }
        library->assets=head;
    }
    for (size_t i=0;saved && i<count;++i) qa_cinematic_asset_release(saved[i]);
    free(saved); free(installed); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) cinematic_fail(error,"Invalid or unqualified retained media cache");
    return ok;
}

#include "internal.h"
#include "qa/font_save.h"
#include "qa/source_save.h"
#include "qa/scene_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
size_t qa_font_library_record_count(const qa_font_library *library) { return library?library->font_count:0; }
const qa_font *qa_font_library_record_at(const qa_font_library *library, size_t index)
{ return library && index<library->font_count?library->fonts[index]:NULL; }
qa_scene_resources *qa_font_library_resource_owner(const qa_font_library *library)
{ return library?library->resources:NULL; }

static bool owned_text(qa_source_save_io *io, char **value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t length=reading?0:strlen(*value);
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (reading) {
        if (length==SIZE_MAX) return false;
        *value=malloc(length+1);
        if (!*value) return qa_font_fail(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved font name");
        (*value)[length]=0;
    }
    return qa_source_save_bytes(io,*value,length) && !memchr(*value,0,length);
}
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t size)
{
    if (count>SIZE_MAX/size) return false;
    *out=count?calloc(count,size):NULL;
    return !count || *out || qa_font_fail(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved font records");
}
static bool image_owner(const qa_font_library *library, const qa_scene_image *image)
{
    const qa_scene_resources *owners[]={library->resources}; size_t index;
    return qa_scene_image_owner_index(owners,1,image,&index);
}
static bool resource_owner(const qa_font_library *library, const qa_resource *resource)
{
    size_t count=qa_vfs_resource_count(library->vfs);
    for (size_t i=0;i<count;++i) if (qa_vfs_resource_at(library->vfs,i,NULL)==resource) return true;
    return false;
}
static bool same_resource(qa_source_save_io *io, const qa_resource *resource)
{
    const char *path=qa_resource_path(resource); size_t length=strlen(path), saved_length=length;
    qa_sha256_digest digest=*qa_resource_digest(resource), saved=digest;
    if (!qa_source_save_count(io,&saved_length,SIZE_MAX) || saved_length!=length) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) {
        if (!qa_source_save_bytes(io,(void *)path,length)) return false;
    } else {
        if (length>io->input.size-io->offset || memcmp(io->input.data+io->offset,path,length)) return false;
        io->offset+=length;
    }
    return qa_source_save_bytes(io,saved.bytes,sizeof(saved.bytes)) && !memcmp(digest.bytes,saved.bytes,sizeof(saved.bytes));
}
static bool owners(qa_source_save_io *io, qa_font *font, const qa_font_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t maximum=reading?io->input.size/8:SIZE_MAX;
    size_t images=font->image_count, sources=font->source_count;
    if (!qa_source_save_count(io,&images,maximum) || !qa_source_save_count(io,&sources,maximum)) return false;
    if (reading) {
        if (!allocate(io,(void **)&font->images,images,sizeof(*font->images))) return false;
        font->image_count=images;
        if (!allocate(io,(void **)&font->sources,sources,sizeof(*font->sources))) return false;
        font->source_count=sources;
    }
    for (size_t i=0;i<font->image_count;++i) {
        uint64_t key=0;
        if (!reading && (!image_owner(font->library,font->images[i]) ||
            !refs->image_encode(refs->context,font->images[i],&key,io->error))) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (reading) {
            const qa_scene_image *image=NULL;
            if (!refs->image_decode(refs->context,key,&image,io->error) || !image || !image_owner(font->library,image)) return false;
            qa_scene_image_retain(image); font->images[i]=image;
        }
        for (size_t j=0;j<i;++j) if (font->images[j]==font->images[i]) return false;
    }
    for (size_t i=0;i<font->source_count;++i) {
        uint64_t key=0;
        if (!reading && (!resource_owner(font->library,font->sources[i]) ||
            !refs->resource_encode(refs->context,font->sources[i],&key,io->error))) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (reading) {
            const qa_resource *resource=NULL;
            if (!refs->resource_decode(refs->context,key,&resource,io->error) || !resource || !resource_owner(font->library,resource)) return false;
            qa_resource_retain((qa_resource *)resource); font->sources[i]=(qa_resource *)resource;
        }
        if (!same_resource(io,font->sources[i])) return false;
        for (size_t j=0;j<i;++j) if (font->sources[j]==font->sources[i]) return false;
    }
    return true;
}
static bool glyph(qa_source_save_io *io, qa_font *font, qa_font_glyph *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint64_t atlas=UINT64_MAX;
    if (!reading && value->image) {
        for (size_t i=0;i<font->image_count;++i) if (font->images[i]==value->image) { atlas=i; break; }
        if (atlas==UINT64_MAX) return false;
    }
    if (!qa_source_save_u64(io,&atlas) || (atlas!=UINT64_MAX && atlas>=font->image_count)) return false;
    if (reading) { value->font=font; value->image=atlas==UINT64_MAX?NULL:font->images[atlas]; }
    FIELD(u32,value,codepoint); FIELD(f32,&value->uv,x); FIELD(f32,&value->uv,y);
    FIELD(f32,&value->uv,z); FIELD(f32,&value->uv,w);
    FIELD(f32,value,width); FIELD(f32,value,height); FIELD(f32,value,advance);
    FIELD(f32,value,bearing_x); FIELD(f32,value,bearing_y); FIELD(bool,value,visible); FIELD(bool,value,baked_color);
    return qa_font_valid_scalar(value->codepoint) && isfinite(value->width) && value->width>=0 &&
        isfinite(value->height) && value->height>=0 && isfinite(value->advance) &&
        isfinite(value->bearing_x) && isfinite(value->bearing_y);
}
static bool record(qa_source_save_io *io, qa_font *font, const qa_font_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint32_t kind=font->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_FONT_Q3 || !owned_text(io,&font->name)) return false;
    if (reading) font->kind=(qa_font_kind)kind;
    FIELD(f32,font,line_height); FIELD(f32,font,ascent); FIELD(f32,font,descent);
    FIELD(f32,font,cap_top); FIELD(f32,font,cap_height); FIELD(bool,font,has_cap_ink);
    if (!(font->line_height>0) || !isfinite(font->line_height) || !isfinite(font->ascent) || !isfinite(font->descent) ||
        !isfinite(font->cap_top) || !isfinite(font->cap_height) || !owners(io,font,refs)) return false;
    uint64_t source=UINT64_MAX;
    if (!reading && font->truetype_source) {
        for (size_t i=0;i<font->source_count;++i) if (qa_resource_id(font->sources[i])==font->truetype_source) { source=i; break; }
        if (source==UINT64_MAX) return false;
    }
    if (!qa_source_save_u64(io,&source) || (source!=UINT64_MAX && source>=font->source_count)) return false;
    if (reading) font->truetype_source=source==UINT64_MAX?0:qa_resource_id(font->sources[source]);
    FIELD(u32,font,truetype_pixel_size); FIELD(u32,font,truetype_atlas_width); FIELD(u32,font,truetype_atlas_height);
    if (!qa_source_save_count(io,&font->truetype_coverage_count,reading?io->input.size/4:SIZE_MAX) ||
        (reading && !allocate(io,(void **)&font->truetype_coverage,font->truetype_coverage_count,sizeof(*font->truetype_coverage)))) return false;
    for (size_t i=0;i<font->truetype_coverage_count;++i) {
        if (!qa_source_save_u32(io,&font->truetype_coverage[i]) || !qa_font_valid_scalar(font->truetype_coverage[i]) ||
            (i && font->truetype_coverage[i]<=font->truetype_coverage[i-1])) return false;
    }
    if (kind==QA_FONT_TRUETYPE) {
        if (source==UINT64_MAX || !font->truetype_pixel_size || !font->truetype_atlas_width || !font->truetype_atlas_height) return false;
    } else if (source!=UINT64_MAX || font->truetype_pixel_size || font->truetype_atlas_width ||
        font->truetype_atlas_height || font->truetype_coverage_count) return false;
    FIELD(bool,font,has_q3_record);
    if (font->has_q3_record!=(kind==QA_FONT_Q3)) return false;
    if (font->has_q3_record) {
        uint8_t bytes[QA_Q3_FONT_RECORD_BYTES];
        if (!reading && !qa_q3_font_record_encode(&font->q3_record,bytes,io->error)) return false;
        if (!qa_source_save_bytes(io,bytes,sizeof(bytes)) ||
            (reading && !qa_q3_font_record_decode((qa_bytes){bytes,sizeof(bytes)},&font->q3_record,io->error))) return false;
    }
    if (!qa_source_save_count(io,&font->glyph_count,reading?io->input.size/50:SIZE_MAX) ||
        (reading && !allocate(io,(void **)&font->glyphs,font->glyph_count,sizeof(*font->glyphs)))) return false;
    for (size_t i=0;i<font->glyph_count;++i) {
        if (!glyph(io,font,&font->glyphs[i]) || (i && font->glyphs[i].codepoint<font->glyphs[i-1].codepoint)) return false;
    }
    if (reading) {
        font->glyph_capacity=font->glyph_count; font->image_capacity=font->image_count; font->source_capacity=font->source_count;
    }
    return true;
}
static bool refs_ready(const qa_font_checkpoint_refs *refs)
{ return refs && refs->image_encode && refs->image_decode && refs->resource_encode && refs->resource_decode; }
bool qa_font_library_checkpoint(const qa_font_library *library, const qa_font_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!library || !out || !refs_ready(refs)) return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font capture requires an owner and resolvers");
    qa_source_save_io io; uint8_t magic[4]={'Q','F','N','T'}; uint32_t schema=1; size_t count=library->font_count;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=qa_source_save_bytes(&io,magic,4) && qa_source_save_u32(&io,&schema) && qa_source_save_count(&io,&count,SIZE_MAX);
    for (size_t i=0;ok && i<count;++i) {
        qa_font saved=*library->fonts[i];
        if (saved.library!=library) { ok=false; break; }
        for (size_t j=0;j<saved.glyph_count;++j) if (saved.glyphs[j].font!=library->fonts[i]) { ok=false; break; }
        if (!ok) break;
        /* The writer never changes glyph back-pointers or heap collections. */
        ok=record(&io,&saved,refs);
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) qa_font_fail(error,QA_ERROR_FORMAT,0,"Invalid retained font state");
    qa_source_save_dispose(&io); return ok;
}
bool qa_font_library_restore(qa_font_library *library, qa_bytes bytes, const qa_font_checkpoint_refs *refs, qa_error *error)
{
    if (!library || !refs_ready(refs)) return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font restore requires a candidate owner and resolvers");
    qa_source_save_io io; uint8_t magic[4]; uint32_t schema=0; size_t count=0; qa_font **fonts=NULL;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QFNT",4) && qa_source_save_u32(&io,&schema) && schema==1 &&
        qa_source_save_count(&io,&count,bytes.size/64) && count>=library->font_count && allocate(&io,(void **)&fonts,count,sizeof(*fonts));
    for (size_t i=0;ok && i<count;++i) {
        fonts[i]=calloc(1,sizeof(*fonts[i]));
        if (!fonts[i]) { ok=qa_font_fail(error,QA_ERROR_MEMORY,i,"Allocating restored font"); break; }
        fonts[i]->library=library;
        ok=record(&io,fonts[i],refs);
        if (ok && i<library->font_count) {
            const qa_font *current=library->fonts[i];
            ok=current && current->library==library && current->kind==fonts[i]->kind && !strcmp(current->name,fonts[i]->name);
        }
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    if (ok) {
        for (size_t i=0;i<library->font_count;++i) {
            qa_font *current=library->fonts[i], *decoded=fonts[i];
            qa_font previous=*current; *current=*decoded; *decoded=previous;
            for (size_t j=0;j<current->glyph_count;++j) current->glyphs[j].font=current;
            qa_font_internal_destroy(decoded); fonts[i]=current;
        }
        free(library->fonts); library->fonts=fonts; library->font_count=library->font_capacity=count; fonts=NULL;
    }
    if (fonts) {
        for (size_t i=0;i<count;++i) qa_font_internal_destroy(fonts[i]);
        free(fonts);
    }
    if (!ok && error && error->code==QA_OK) qa_font_fail(error,QA_ERROR_FORMAT,0,"Invalid saved font owner or installed font bindings");
    qa_source_save_dispose(&io); return ok;
}
#undef FIELD

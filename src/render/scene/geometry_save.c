#include "qa/scene_geometry_save.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
static bool vertex(qa_source_save_io *io, qa_scene_vertex *value)
{
    FIELD(vec3,value,position); FIELD(vec3,value,normal);
    FIELD(f32,&value->texcoord,x); FIELD(f32,&value->texcoord,y);
    FIELD(f32,&value->lightmap,x); FIELD(f32,&value->lightmap,y);
    FIELD(f32,&value->color,x); FIELD(f32,&value->color,y); FIELD(f32,&value->color,z); FIELD(f32,&value->color,w); return true;
}
static bool failure(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static bool fields(qa_source_save_io *io, qa_scene_geometry_view *saved)
{
    uint8_t magic[4]={'Q','G','E','O'}; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QGEO",4) ||
        !qa_source_save_count(io,&saved->vertex_count,reading?io->input.size/56:SIZE_MAX/sizeof(*saved->vertices)) ||
        !qa_source_save_count(io,&saved->index_count,reading?io->input.size/4:SIZE_MAX/sizeof(*saved->indices))) return false;
    if (reading) {
        size_t vertices=saved->vertex_count, indices=saved->index_count;
        if (vertices>SIZE_MAX/sizeof(*saved->vertices) || indices>SIZE_MAX/sizeof(*saved->indices) ||
            vertices>(io->input.size-io->offset)/56 || indices>((io->input.size-io->offset)-vertices*56)/4) return false;
        saved->vertices=vertices?calloc(vertices,sizeof(*saved->vertices)):NULL;
        saved->indices=indices?calloc(indices,sizeof(*saved->indices)):NULL;
        if ((vertices && !saved->vertices) || (indices && !saved->indices)) return failure(io->error,QA_ERROR_MEMORY,"Allocating saved immutable geometry");
    }
    for (size_t i=0;i<saved->vertex_count;++i) {
        qa_scene_vertex value=reading?(qa_scene_vertex){0}:saved->vertices[i];
        if (!vertex(io,&value)) return false;
        if (reading) ((qa_scene_vertex *)saved->vertices)[i]=value;
    }
    for (size_t i=0;i<saved->index_count;++i) {
        uint32_t value=reading?0:saved->indices[i];
        if (!qa_source_save_u32(io,&value) || value>=saved->vertex_count) return false;
        if (reading) ((uint32_t *)saved->indices)[i]=value;
    }
    return true;
}
bool qa_scene_geometry_checkpoint(const qa_scene_geometry *geometry, qa_buffer *out, qa_error *error)
{
    qa_scene_geometry_view saved;
    if (!out || !qa_scene_geometry_read(geometry,&saved)) return failure(error,QA_ERROR_ARGUMENT,"Geometry capture requires an active allocation");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=fields(&io,&saved) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid retained immutable geometry");
    qa_source_save_dispose(&io); return ok;
}
bool qa_scene_geometry_restore(qa_bytes bytes, qa_scene_geometry **out, qa_error *error)
{
    if (!out || *out) return failure(error,QA_ERROR_ARGUMENT,"Geometry restore requires an empty output");
    qa_source_save_io io; qa_scene_geometry_view saved={0};
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=fields(&io,&saved) && qa_source_save_finish(&io,NULL);
    qa_scene_geometry *geometry=NULL;
    if (ok) {
        geometry=qa_scene_geometry_adopt((qa_scene_vertex *)saved.vertices,saved.vertex_count,
            (uint32_t *)saved.indices,saved.index_count,error);
        ok=geometry!=NULL;
    }
    if (ok) *out=geometry;
    else {
        free((void *)saved.vertices); free((void *)saved.indices);
        if (error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid saved immutable geometry");
    }
    qa_source_save_dispose(&io); return ok;
}
bool qa_scene_geometry_checkpoint_ready(const qa_scene_geometry *geometry, qa_bytes bytes, qa_error *error)
{
    if (bytes.size && !bytes.data) return failure(error,QA_ERROR_ARGUMENT,"Missing geometry qualification bytes");
    qa_buffer current={0};
    if (!qa_scene_geometry_checkpoint(geometry,&current,error)) return false;
    bool ok=current.size==bytes.size && !memcmp(current.data,bytes.data,current.size);
    qa_buffer_free(&current);
    return ok || failure(error,QA_ERROR_FORMAT,"Saved geometry differs from its actual qualified allocation");
}
#undef FIELD

#include "owner_legacy.h"
#include "legacy/internal.h"
#include "qa/scene_save.h"
#include "qa/binary.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define F(type,object,member) do { if (!qa_source_save_##type(io,&(object)->member)) return false; } while (0)
static bool fail(qa_source_save_io *io, qa_status code, const char *message)
{ qa_error_set(io->error,code,0,"%s",message); return false; }
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t stride)
{
    if (io->direction!=QA_SOURCE_SAVE_READ) return !count || *out || fail(io,QA_ERROR_ARGUMENT,"Missing immutable legacy allocation");
    if (count>SIZE_MAX/stride) return fail(io,QA_ERROR_FORMAT,"Legacy allocation exceeds address space");
    *out=count?calloc(count,stride):NULL;
    return !count || *out || fail(io,QA_ERROR_MEMORY,"Allocating detached legacy descriptors");
}
static bool text(qa_source_save_io *io, char **value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=*value?strlen(*value):0;
    if (!qa_source_save_count(io,&count,reading?io->input.size-io->offset:SIZE_MAX) || count==SIZE_MAX) return false;
    if (!reading) return *value && qa_source_save_bytes(io,*value,count);
    if (count>io->input.size-io->offset) return false;
    *value=calloc(count+1,1);
    if (!*value) return fail(io,QA_ERROR_MEMORY,"Allocating retained world texture name");
    if (!qa_source_save_bytes(io,*value,count)) return false;
    return !memchr(*value,0,count) || fail(io,QA_ERROR_FORMAT,"Embedded NUL in world texture name");
}
static bool image(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs, qa_scene_image **value)
{
    bool present=*value!=NULL; uint64_t key=0; size_t ordinal;
    const qa_scene_resources *owners[]={world->resources};
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    if (io->direction==QA_SOURCE_SAVE_WRITE && (!qa_scene_image_owner_index(owners,1,*value,&ordinal) ||
        !refs->images.encode(refs->images.context,*value,&key,io->error))) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded=NULL;
        if (!refs->images.decode(refs->images.context,key,&decoded,io->error) || !decoded ||
            !qa_scene_image_owner_index(owners,1,decoded,&ordinal)) return false;
        qa_scene_image_retain(decoded); *value=(qa_scene_image *)decoded;
    }
    return true;
}
static bool animation_valid(const qawl_world *data, size_t index, bool q1)
{
    const qawl_texture *texture=&data->textures[index];
    size_t expected[2][10]={{0}}, counts[2]={0};
    if (q1 && texture->name[0]=='+') {
        if (!texture->name[1]) return false;
        for (size_t cycle=0;cycle<2;++cycle) for (size_t frame=0;frame<10;++frame) expected[cycle][frame]=SIZE_MAX;
        for (size_t i=0;i<data->texture_count;++i) {
            const char *name=data->textures[i].name;
            if (name[0]!='+' || !name[1] || strcmp(texture->name+2,name+2)) continue;
            unsigned char code=(unsigned char)toupper((unsigned char)name[1]); size_t cycle, frame;
            if (code>='0' && code<='9') { cycle=0; frame=code-'0'; }
            else if (code>='A' && code<='J') { cycle=1; frame=code-'A'; }
            else return false;
            expected[cycle][frame]=i;
            if (counts[cycle]<=frame) counts[cycle]=frame+1;
        }
        for (size_t cycle=0;cycle<2;++cycle) for (size_t frame=0;frame<counts[cycle];++frame)
            if (expected[cycle][frame]==SIZE_MAX) return false;
    }
    return counts[0]==texture->animation_count[0] && counts[1]==texture->animation_count[1] &&
        !memcmp(expected,texture->animation,sizeof(expected));
}
static bool texture_valid(qa_source_save_io *io, const qa_scene_world *world, const qawl_texture *texture, size_t index)
{
    bool q1=world->bsp.family==QA_BSP_Q1; qa_bytes name; qa_bsp_texture source={0}; qa_bsp_texinfo info={0};
    if (q1) { if (!qa_bsp_read_texture(&world->bsp,index,&source,io->error)) return false; name=source.name; }
    else { if (!qa_bsp_read_texinfo(&world->bsp,index,&info,io->error)) return false; name=info.name; }
    if (!name.size) name=(qa_bytes){(const uint8_t *)"*missing",8};
    return strlen(texture->name)==name.size && !memcmp(texture->name,name.data,name.size) &&
        texture->next==(q1?-1:info.next) && texture->image &&
        texture->width==(q1 && source.width?source.width:texture->image->logical_width) &&
        texture->height==(q1 && source.height?source.height:texture->image->logical_height) &&
        texture->quake64_shift==source.quake64_shift;
}
static bool textures(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs)
{
    qawl_world *data=world->legacy_data; bool q1=world->bsp.family==QA_BSP_Q1;
    size_t expected=qa_bsp_record_count(&world->bsp,QA_BSP_TEXINFO);
    if (q1 && !qa_bsp_texture_count(&world->bsp,&expected,io->error)) return false;
    if (!qa_source_save_count(io,&data->texture_count,expected) || data->texture_count!=expected ||
        !allocate(io,(void **)&data->textures,expected?expected:1,sizeof(*data->textures))) return false;
    for (size_t i=0;i<data->texture_count;++i) {
        qawl_texture *texture=&data->textures[i];
        if (!text(io,&texture->name)) return false;
        F(u32,texture,width); F(u32,texture,height); F(u32,texture,quake64_shift); F(i32,texture,next);
        for (size_t cycle=0;cycle<2;++cycle) {
            if (!qa_source_save_count(io,&texture->animation_count[cycle],10)) return false;
            for (size_t frame=0;frame<10;++frame)
                if (!qa_source_save_count(io,&texture->animation[cycle][frame],SIZE_MAX)) return false;
        }
        if (!image(io,world,refs,&texture->image) || !image(io,world,refs,&texture->fullbright) ||
            !image(io,world,refs,&texture->sky[0]) || !image(io,world,refs,&texture->sky[1]) ||
            !texture_valid(io,world,texture,i)) return false;
    }
    for (size_t i=0;i<data->texture_count;++i) if (!animation_valid(data,i,q1)) return false;
    for (size_t i=0;i<6;++i) if (!image(io,world,refs,&data->sky[i])) return false;
    return true;
}
static bool frames_valid(qa_source_save_io *io, const qawl_world *data, const qaw_legacy *light, bool q1)
{
    if (q1) return !light->frame_count && !light->frames;
    uint8_t *seen=calloc(data->texture_count?data->texture_count:1,1);
    if (!seen) return fail(io,QA_ERROR_MEMORY,"Qualifying immutable texture animation chain");
    int64_t current=(int64_t)light->texture; size_t used=0; bool ok=true;
    while (ok && current>=0) {
        if ((uint64_t)current>=data->texture_count) { ok=false; break; }
        if (seen[current]) break;
        seen[current]=1;
        if (used>=light->frame_count || light->frames[used]!=(size_t)current) { ok=false; break; }
        ++used; current=data->textures[current].next;
    }
    free(seen); return ok && used==light->frame_count;
}
static bool styles_valid(const qawl_world *data, const qa_bsp_face *face, size_t index, const qaw_legacy *light)
{
    const uint8_t *bytes=face->styles; size_t count=4, stride=1; uint16_t end=UINT8_MAX;
    if (data->metadata.styles16.data) {
        count=data->metadata.styles16_per_face; stride=2; end=UINT16_MAX;
        bytes=data->metadata.styles16.data+index*count*2;
    } else if (data->metadata.styles.data) {
        count=data->metadata.styles_per_face; bytes=data->metadata.styles.data+index*count;
    }
    size_t used=0;
    for (;used<count;++used) {
        uint16_t value=stride==2?qa_load_u16le(bytes+used*2):bytes[used];
        if (value==end) break;
        if (used>=light->style_count || light->styles[used]!=value) return false;
    }
    return used==light->style_count;
}
static bool surface(qa_source_save_io *io, qa_scene_world *world, qaw_surface *surface)
{
    bool q1=world->bsp.family==QA_BSP_Q1; qawl_world *data=world->legacy_data;
    if (!allocate(io,(void **)&surface->legacy,1,sizeof(*surface->legacy))) return false;
    qaw_legacy *light=surface->legacy;
    if (!qa_source_save_count(io,&light->texture,data->texture_count) || light->texture>=data->texture_count ||
        !qa_source_save_count(io,&light->frame_count,data->texture_count) ||
        !allocate(io,(void **)&light->frames,light->frame_count,sizeof(*light->frames))) return false;
    for (size_t i=0;i<light->frame_count;++i)
        if (!qa_source_save_count(io,&light->frames[i],data->texture_count) || light->frames[i]>=data->texture_count) return false;
    if (!frames_valid(io,data,light,q1)) return false;
    F(bool,light,warp); F(bool,light,flowing); F(bool,light,fence); F(bool,light,lightmapped); F(bool,light,decoupled);
    F(f32,light,alpha); F(u32,light,width); F(u32,light,height);
    for (size_t axis=0;axis<2;++axis) {
        for (size_t element=0;element<4;++element) if (!qa_source_save_f32(io,&light->projection[axis][element])) return false;
        if (!qa_source_save_f32(io,&light->light_step[axis])) return false;
    }
    if (!qa_source_save_count(io,&light->sample_offset,SIZE_MAX) ||
        !qa_source_save_count(io,&light->style_count,io->direction==QA_SOURCE_SAVE_READ?io->input.size/2:SIZE_MAX) ||
        !allocate(io,(void **)&light->styles,light->style_count,sizeof(*light->styles))) return false;
    for (size_t i=0;i<light->style_count;++i) if (!qa_source_save_u16(io,&light->styles[i])) return false;
    bool encoded=light->encoded_pixels!=NULL, accumulation=light->light_accumulation!=NULL;
    if (!qa_source_save_bool(io,&encoded) || !qa_source_save_bool(io,&accumulation) || accumulation!=light->lightmapped ||
        encoded!=(light->lightmapped && (!q1 || world->options.q1_lightmap_encoding!=QA_Q1_LIGHTMAP_RGB))) return false;
    qa_bsp_face face; qa_bsp_texinfo info;
    if (!qa_bsp_read_face(&world->bsp,surface->source_index,&face,io->error) ||
        !qa_bsp_read_texinfo(&world->bsp,face.texinfo,&info,io->error) ||
        light->texture!=(q1?(size_t)info.texture:face.texinfo) || !styles_valid(data,&face,surface->source_index,light)) return false;
    const char *name=data->textures[light->texture].name;
    bool warp=q1?name[0]=='*' && strcmp(name,"*missing")!=0:(info.flags&8)!=0;
    float alpha=q1?(warp?world->options.q1_water_alpha:1):(info.flags&16)?0.33f:(info.flags&32)?0.66f:1;
    if (light->warp!=warp || light->flowing!=(!q1 && (info.flags&64)!=0) || light->fence!=(q1 && name[0]=='{') ||
        memcmp(&alpha,&light->alpha,sizeof(alpha))) return false;
    for (size_t axis=0;axis<2;++axis) {
        if (!isfinite(light->light_step[axis])) return false;
        for (size_t i=0;i<4;++i) if (!isfinite(light->projection[axis][i])) return false;
    }
    if (!light->lightmapped) return true;
    if (!light->width || !light->height || light->width>SIZE_MAX/light->height) return false;
    size_t pixels=(size_t)light->width*light->height;
    if (pixels>SIZE_MAX/(3*sizeof(float)) || pixels>SIZE_MAX/(3*sizeof(uint32_t)) || pixels>SIZE_MAX/4 ||
        (io->direction==QA_SOURCE_SAVE_READ && pixels>io->input.size/4)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if ((encoded && !allocate(io,(void **)&light->encoded_pixels,pixels,4)) ||
            !allocate(io,&light->light_accumulation,pixels,3*(q1?sizeof(uint32_t):sizeof(float)))) return false;
    }
    return true;
}
bool qaw_owner_legacy_fields(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs)
{
    if (!io || !world || world->bsp.family==QA_BSP_Q3 || !refs || !refs->images.encode || !refs->images.decode) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (reading && world->legacy_data) return fail(io,QA_ERROR_ARGUMENT,"Detached legacy owner is already populated");
    if (!allocate(io,&world->legacy_data,1,sizeof(qawl_world))) return false;
    qawl_world *data=world->legacy_data;
    if (world->bsp.family==QA_BSP_Q1 && reading && !qa_bsp_read_q1_metadata(&world->bsp,&data->metadata,io->error)) return false;
    if (!textures(io,world,refs)) return false;
    for (size_t i=0;i<world->surface_count;++i) if (!surface(io,world,&world->surfaces[i])) return false;
    return true;
}

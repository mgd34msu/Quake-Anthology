#include "controls_private.h"
#include "qa/q3_source_scene_bank.h"
#include "qa/scene_world_save.h"
#include "qa/q3_assets_custody.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <inttypes.h>
#include "qa/render_cpu.h"
#include "qa/render_gl.h"

struct qa_render_controls_ticket {
    qa_render_controls *controls;
    qa_render_controls_values original, candidate;
    bool published;
};
struct qa_render_source_images_ticket {
    qa_render_controls *controls;
    qa_gl_source_images_ticket *gl;
    qa_cpu_source_images_ticket *cpu;
    material_source_reset *reset;
    qa_render_source_restart_values restart;
    bool prepared,published;
};
struct material_source_reset {
    qa_render_controls *controls;
    qa_material_source_scratch candidate;
    uint32_t max_polys, max_vertices;
    bool published;
};
bool qa_render_source_texture_alpha(const qa_scene_image *image)
{
    return image && image->kind!=QA_SCENE_DEPTH32F && (image->source_q3?
        image->source_format==QA_Q3_TEXTURE_RGBA || image->source_format==QA_Q3_TEXTURE_RGBA4 ||
        image->source_format==QA_Q3_TEXTURE_RGBA8:image->kind==QA_SCENE_RGBA8);
}
void qa_render_source_texture_init(qa_render_source_texture *texture)
{
    *texture=(qa_render_source_texture){.filter=QA_SCENE_NEAREST_MIPMAP_LINEAR,
        .magnification_linear=true,.wrap=QA_SCENE_REPEAT};
}
void qa_render_source_texture_filter(qa_render_source_texture *texture,qa_scene_filter filter)
{
    texture->filter=filter;
    texture->magnification_linear=filter==QA_SCENE_LINEAR || filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
        filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
}
void qa_render_source_texture_release(qa_render_source_texture *texture)
{
    for (uint32_t i=0;i<texture->count;++i) {
        free(texture->pixels[i]);
        qa_scene_image_release(texture->images[i]);
        qa_scene_resources_destroy(texture->owners[i]);
    }
    qa_render_source_texture_init(texture);
}
bool qa_render_source_texture_upload(qa_render_source_texture *texture,const qa_scene_image *image,
    qa_scene_resources *owner,qa_scene_filter filter,qa_error *error)
{
    if (!texture || !image || !owner || !image->level_count || image->level_count>QA_SOURCE_TEXTURE_LEVELS ||
        !image->levels || (unsigned)filter>QA_SCENE_LINEAR_MIPMAP_LINEAR) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source texture upload exceeds its actual allocated mip levels"); return false;
    }
    uint32_t held=0;
    for (;held<image->level_count;++held) {
        if (!qa_scene_resources_retain(owner,error)) {
            while (held) { --held; qa_scene_image_release(image); qa_scene_resources_destroy(owner); }
            return false;
        }
        qa_scene_image_retain(image);
    }
    for (uint32_t i=0;i<image->level_count;++i) {
        free(texture->pixels[i]); texture->pixels[i]=NULL;
        qa_scene_image_release(texture->images[i]); qa_scene_resources_destroy(texture->owners[i]);
        texture->images[i]=image; texture->owners[i]=owner; texture->levels[i]=image->levels[i];
        texture->kinds[i]=image->kind;
        texture->formats[i]=image->source_format;
    }
    if (texture->count<image->level_count) texture->count=(uint32_t)image->level_count;
    qa_render_source_texture_filter(texture,filter); texture->wrap=image->wrap;
    return true;
}
bool qa_render_source_texture_clone(qa_render_source_texture *out,const qa_render_source_texture *texture,qa_error *error)
{
    qa_render_source_texture_init(out);
    out->filter=texture->filter; out->wrap=texture->wrap; out->border=texture->border;
    out->magnification_linear=texture->magnification_linear;
    for (uint32_t i=0;i<texture->count;++i) {
        if (!texture->images[i] || !texture->owners[i] || !qa_scene_resources_retain(texture->owners[i],error)) {
            qa_render_source_texture_release(out); return false;
        }
        qa_scene_image_retain(texture->images[i]); out->images[i]=texture->images[i];
        out->owners[i]=texture->owners[i]; out->levels[i]=texture->levels[i];
        out->kinds[i]=texture->kinds[i]; ++out->count;
        out->formats[i]=texture->formats[i];
        if (texture->pixels[i]) {
            out->pixels[i]=malloc(texture->levels[i].bytes);
            if (!out->pixels[i]) {
                qa_error_set(error,QA_ERROR_MEMORY,i,"Retaining modified Source texture pixels");
                qa_render_source_texture_release(out); return false;
            }
            memcpy(out->pixels[i],texture->levels[i].pixels,texture->levels[i].bytes);
            out->levels[i].pixels=out->pixels[i];
        }
    }
    return true;
}
const qa_scene_image *qa_render_source_texture_view(qa_render_source_texture *texture)
{
    if (!texture->count || !texture->images[0]) return NULL;
    size_t required=1;
    uint32_t width=texture->levels[0].width,height=texture->levels[0].height;
    qa_scene_image_kind kind=texture->kinds[0];
    if (texture->filter!=QA_SCENE_NEAREST && texture->filter!=QA_SCENE_LINEAR) {
        while (width>1 || height>1) {
            width=width>1?width/2:1; height=height>1?height/2:1;
            if (required>=texture->count || !texture->images[required] || texture->kinds[required]!=kind ||
                texture->formats[required]!=texture->formats[0] ||
                texture->levels[required].width!=width || texture->levels[required].height!=height) return NULL;
            ++required;
        }
    }
    texture->view=*texture->images[0];
    texture->view.levels=texture->levels; texture->view.level_count=required;
    texture->view.filter=texture->filter; texture->view.wrap=texture->wrap; texture->view.border=texture->border;
    texture->view.kind=kind;
    texture->view.source_format=texture->formats[0];
    texture->view.source_q3=true;
    return &texture->view;
}
bool qa_render_source_texture_level(qa_render_source_texture *texture,uint32_t level,const qa_scene_image *image,
    qa_scene_resources *owner,qa_scene_image_kind kind,qa_q3_texture_format format,qa_error *error)
{
    if (!texture || !image || !owner || level>=QA_SOURCE_TEXTURE_LEVELS || level>texture->count ||
        image->level_count<=level || (unsigned)kind>QA_SCENE_DEPTH32F || (unsigned)format>QA_Q3_TEXTURE_RGB4_S3TC ||
        !qa_scene_resources_retain(owner,error)) return false;
    qa_scene_image_retain(image); qa_scene_image_release(texture->images[level]);
    free(texture->pixels[level]); texture->pixels[level]=NULL;
    qa_scene_resources_destroy(texture->owners[level]);
    texture->images[level]=image; texture->owners[level]=owner; texture->levels[level]=image->levels[level];
    texture->kinds[level]=kind;
    texture->formats[level]=format;
    if (texture->count<=level) texture->count=level+1;
    return true;
}
bool qa_render_source_texture_subimage(qa_render_source_texture *texture,const qa_scene_image *image,
    bool *updated,qa_error *error)
{
    *updated=false;
    if (!texture->count || image->levels[0].width>texture->levels[0].width ||
        image->levels[0].height>texture->levels[0].height) return true;
    qa_scene_image_level *storage=texture->levels;
    const qa_scene_image_level *update=image->levels;
    if (!texture->pixels[0]) {
        void *pixels=malloc(storage->bytes);
        if (!pixels) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Updating actual Source texture rectangle"); return false;
        }
        memcpy(pixels,storage->pixels,storage->bytes);
        texture->pixels[0]=pixels; storage->pixels=pixels;
    }
    for (uint32_t row=0;row<update->height;++row)
        memcpy((uint8_t *)texture->pixels[0]+(size_t)row*storage->width*4,
            (const uint8_t *)update->pixels+(size_t)row*update->width*4,(size_t)update->width*4);
    *updated=true;
    return true;
}
void material_source_release(qa_material_source_scratch *source)
{
    material_source_order_detach(source->queued_order, source);
    const qa_material *material = source->material;
    source->material = NULL;
    qa_material_release(material);
    qa_scene_image_release(source->lightmap); source->lightmap = NULL;
    qa_scene_resources_destroy(source->lightmap_owner); source->lightmap_owner = NULL;
    qa_scene_world_release(source->world); source->world = NULL;
    qa_q3_source_scene_bank_destroy(source->scene_bank); source->scene_bank = NULL;
}
bool material_source_lightmap_set(qa_material_source_scratch *source, const qa_scene_image *image, qa_error *error)
{
    qa_scene_resources *owner = qa_scene_image_resource_owner(image);
    if (owner && !qa_scene_resources_retain(owner, error)) return false;
    qa_scene_image_retain(image);
    qa_scene_image_release(source->lightmap);
    qa_scene_resources_destroy(source->lightmap_owner);
    source->lightmap = image; source->lightmap_owner = owner;
    return true;
}
void qa_render_controls_init_cpu(qa_render_controls *controls, qa_cpu_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_CPU, .owner.cpu = owner,
        .values.compiled_vertex_arrays = true, .source_filter = QA_SCENE_LINEAR_MIPMAP_NEAREST,
        .source_cull_type = QA_CULL_FRONT, .source_cull_valid = true};
    controls->source.owner = controls;
    qa_render_source_attributes_init(&controls->attributes);
    qa_render_source_texture_init(&controls->zero_texture);
}
void qa_render_controls_init_gl(qa_render_controls *controls, qa_gl_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_GL, .owner.gl = owner,
        .values.compiled_vertex_arrays = true, .source_filter = QA_SCENE_LINEAR_MIPMAP_NEAREST,
        .source_cull_type = QA_CULL_FRONT, .source_cull_valid = true};
    controls->source.owner = controls;
    qa_render_source_attributes_init(&controls->attributes);
    qa_render_source_texture_init(&controls->zero_texture);
}
void qa_render_source_attributes_init(qa_render_source_attributes *attributes)
{
    *attributes=(qa_render_source_attributes){.color={1,1,1,1},.color_known=true,
        .coordinates_known={true,true},.texture_enabled={true,false},
        .environment={QA_TEXTURE_MODULATE,QA_TEXTURE_MODULATE},.coordinate_bank={0,1}};
}
static bool current(const qa_render_controls *controls)
{
    if (!controls) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_render_controls_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_render_controls_current(controls);
    }
    return false;
}
static bool idle_owner(const qa_render_controls *controls)
{
    return current(controls) || (controls && controls->backend == QA_RENDER_CONTROLS_GL &&
        qa_gl_render_controls_callback_candidate(controls));
}
static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool material_source_reset_prepare(qa_render_controls *controls, int32_t max_polys,
    int32_t max_vertices, material_source_reset **out, qa_error *error)
{
    if (!out || *out || !current(controls) || controls->source.entered ||
        controls->source.frame || controls->source.operations || controls->source.head)
        return fail(error, "Source restart reset requires its actual idle renderer and consumed command queue");
    material_source_reset *reset = calloc(1, sizeof(*reset));
    if (!reset) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing restarted Source tess storage"); return false; }
    reset->controls = controls;
    reset->max_polys = max_polys < 600 ? 600 : (uint32_t)max_polys;
    reset->max_vertices = max_vertices < 3000 ? 3000 : (uint32_t)max_vertices;
    if (!qa_q3_source_scene_bank_create(reset->max_polys, reset->max_vertices,
        &reset->candidate.scene_bank, error)) { free(reset); return false; }
    reset->candidate.owner = controls;
    reset->candidate.runtime_video_frame = controls->source.runtime_video_frame;
    reset->candidate.runtime_video_context = controls->source.runtime_video_context;
    reset->candidate.runtime_diagnostics = controls->source.runtime_diagnostics;
    reset->candidate.runtime_diagnostics_context = controls->source.runtime_diagnostics_context;
    reset->candidate.runtime_frame_policy = controls->source.runtime_frame_policy;
    reset->candidate.runtime_frame_context = controls->source.runtime_frame_context;
    *out = reset; return true;
}
bool material_source_reset_ready(const material_source_reset *reset)
{
    const qa_render_controls *controls = reset ? reset->controls : NULL;
    if (!current(controls) || reset->published || !reset->candidate.scene_bank ||
        controls->source.entered || controls->source.frame || controls->source.operations || controls->source.head) return false;
    const qa_material_source_scratch *a = &controls->source, *b = &reset->candidate;
    return a->runtime_video_frame == b->runtime_video_frame && a->runtime_video_context == b->runtime_video_context &&
        a->runtime_diagnostics == b->runtime_diagnostics && a->runtime_diagnostics_context == b->runtime_diagnostics_context &&
        a->runtime_frame_policy == b->runtime_frame_policy && a->runtime_frame_context == b->runtime_frame_context;
}
void material_source_reset_publish(material_source_reset *reset)
{
    if (!reset || reset->published) return;
    qa_render_controls *controls = reset->controls;
    material_source_release(&controls->source);
    controls->source = reset->candidate;
    reset->candidate.scene_bank = NULL;
    controls->source_max_polys = reset->max_polys;
    controls->source_max_polyverts = reset->max_vertices;
    controls->source_limits_initialized = true;
    reset->published = true;
}
void material_source_reset_dispose(material_source_reset **out)
{
    if (!out || !*out) return;
    material_source_reset *reset = *out;
    qa_q3_source_scene_bank_destroy(reset->candidate.scene_bank);
    free(reset); *out = NULL;
}
static bool source_current(const qa_render_controls *controls)
{
    if (!controls || controls->image_ticket) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_source_scratch_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_source_scratch_current(controls);
    }
    return false;
}
bool qa_material_source_material_read(const qa_material_source_scratch *source,
    const qa_material **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !source_current(owner) || &owner->source != source || owner->ticket || source->entered)
        return fail(error, "Current Source shader requires its actual idle renderer owner");
    *out = source->material;
    return true;
}
bool qa_material_source_material_metadata(const qa_material_source_scratch *source,
    const qa_material **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !idle_owner(owner) || &owner->source != source || source->entered)
        return fail(error, "Source shader metadata requires its actual non-entered allocation");
    *out = source->material;
    return true;
}
bool qa_material_source_lightmap_read(const qa_material_source_scratch *source,
    const qa_scene_image **out,qa_error *error)
{
    const qa_render_controls *owner=source?source->owner:NULL;
    if (!out || !source_current(owner) || &owner->source!=source || owner->ticket || source->entered)
        return fail(error,"Current Source lightmap requires its actual idle renderer owner");
    *out=source->lightmap;
    return true;
}
bool qa_render_controls_source_images_metadata(const qa_render_controls *owner,size_t *out,qa_error *error)
{
    if (!out || !idle_owner(owner) || owner->source.entered)
        return fail(error,"Source image metadata requires its actual non-entered renderer allocation");
    *out=owner->backend==QA_RENDER_CONTROLS_GL?qa_gl_source_images_metadata_count(owner):qa_cpu_source_images_metadata_count(owner);
    return true;
}
bool qa_render_controls_source_image_metadata(const qa_render_controls *owner,size_t ordinal,
    const qa_scene_image **out,qa_error *error)
{
    size_t count=0;
    if (!out || !qa_render_controls_source_images_metadata(owner,&count,error) || ordinal>=count)
        return fail(error,"Source image ordinal is outside its actual admitted image registry");
    *out=owner->backend==QA_RENDER_CONTROLS_GL?qa_gl_source_image_metadata_at(owner,ordinal):
        qa_cpu_source_image_metadata_at(owner,ordinal);
    return *out!=NULL || fail(error,"Source image registry lost its actual admitted image");
}

bool qa_material_source_lightmap_metadata(const qa_material_source_scratch *source,
    const qa_scene_image **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !idle_owner(owner) || &owner->source != source || source->entered)
        return fail(error, "Source lightmap metadata requires its actual non-entered allocation");
    *out = source->lightmap;
    return true;
}
bool qa_render_controls_source_texture_metadata(const qa_render_controls *owner,size_t *out,qa_error *error)
{
    if (!out || !idle_owner(owner) || owner->source.entered)
        return fail(error,"Source texture metadata lost its actual non-entered renderer");
    *out=owner->backend==QA_RENDER_CONTROLS_GL?qa_gl_source_texture_metadata_count(owner):
        qa_cpu_source_texture_metadata_count(owner);
    return true;
}
bool qa_render_controls_source_texture_level_metadata(const qa_render_controls *owner,size_t ordinal,
    const qa_scene_image **out,qa_error *error)
{
    size_t count=0;
    if (!out || !qa_render_controls_source_texture_metadata(owner,&count,error) || ordinal>=count)
        return fail(error,"Source texture metadata ordinal is outside its actual retained mip levels");
    *out=owner->backend==QA_RENDER_CONTROLS_GL?qa_gl_source_texture_metadata_at(owner,ordinal):
        qa_cpu_source_texture_metadata_at(owner,ordinal);
    return true;
}
bool qa_material_source_world_metadata(const qa_material_source_scratch *source,
    const qa_scene_world **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !idle_owner(owner) || &owner->source != source || source->entered)
        return fail(error, "Source world metadata requires its actual non-entered allocation");
    *out = source->world;
    return true;
}
static bool source_entities_current(qa_material_source_scratch *source, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    return (source_current(owner) && &owner->source == source && !owner->ticket &&
        !source->issuing && !source->dispatching && !source->submitting) ||
        fail(error, "Source entity writes require their actual non-dispatching renderer allocation");
}
qa_q3_source_scene_bank *qa_material_source_scene_bank(qa_material_source_scratch *source, qa_error *error)
{
    if (!source_entities_current(source, error)) return NULL;
    qa_render_controls *owner = source->owner;
    if (!owner->source_limits_initialized) {
        fail(error, "Source scene bank requires its actual initialized allocation limits"); return NULL;
    }
    if (!source->scene_bank && !qa_q3_source_scene_bank_create(owner->source_max_polys,
        owner->source_max_polyverts, &source->scene_bank, error)) return NULL;
    return source->scene_bank;
}
bool qa_render_controls_source_runtime_bind(qa_render_controls *owner,
    const qa_scene_image *(*video_frame)(void *, uint64_t, double, qa_error *), void *video_context,
    bool (*diagnostics)(void *, qa_scene_source_diagnostics *, qa_error *), void *diagnostics_context,
    bool (*frame_policy)(void *, qa_scene_frame *, qa_error *), void *frame_context, qa_error *error)
{
    if (!idle_owner(owner) || owner->ticket || owner->image_ticket || owner->source.entered || !diagnostics || !frame_policy ||
        (owner->source.runtime_diagnostics && (owner->source.runtime_diagnostics != diagnostics ||
            owner->source.runtime_diagnostics_context != diagnostics_context ||
            owner->source.runtime_video_frame != video_frame || owner->source.runtime_video_context != video_context ||
            owner->source.runtime_frame_policy != frame_policy || owner->source.runtime_frame_context != frame_context)))
        return fail(error, "Source runtime callbacks require their actual idle renderer lifetime owner");
    owner->source.runtime_video_frame = video_frame; owner->source.runtime_video_context = video_context;
    owner->source.runtime_diagnostics = diagnostics; owner->source.runtime_diagnostics_context = diagnostics_context;
    owner->source.runtime_frame_policy = frame_policy; owner->source.runtime_frame_context = frame_context;
    return true;
}
bool qa_material_source_scene_bank_metadata(const qa_material_source_scratch *source,
    const qa_q3_source_scene_bank **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !idle_owner(owner) || &owner->source != source || source->entered)
        return fail(error, "Source bank metadata requires its actual non-entered allocation");
    *out = source->scene_bank; return true;
}
static bool source_rows_hold_library(const material_source_submission *row, const qa_material_library *library)
{
    for (; row; row = row->next) {
        if ((row->original && row->original->library == library) ||
            (row->held_light_world && qa_scene_world_material_owner(row->held_light_world) == library)) return true;
        size_t count = qa_q3_assets_provider_count(row->context.source_model_assets);
        for (size_t i = 0; i < count; ++i) {
            qa_q3_presentation_provider provider;
            if (qa_q3_assets_provider_at(row->context.source_model_assets, i, &provider) && provider.materials == library) return true;
        }
    }
    return false;
}
bool qa_material_source_holds_library(const qa_material_source_scratch *source, const qa_material_library *library)
{
    if (!source || !source->owner || &source->owner->source != source || !library) return false;
    if ((source->material && source->material->library == library) ||
        (source->world && qa_scene_world_material_owner(source->world) == library) ||
        (source->view.world && qa_scene_world_material_owner(source->view.world) == library) ||
        source_rows_hold_library(source->head, library)) return true;
    for (const material_source_operation *operation = source->operations; operation; operation = operation->next)
        if ((operation->view.world && qa_scene_world_material_owner(operation->view.world) == library) ||
            source_rows_hold_library(operation->head, library)) return true;
    if (source->scene_bank) {
        size_t count = qa_q3_source_scene_bank_registry_count(source->scene_bank);
        for (size_t i = 0; i < count; ++i) {
            qa_q3_presentation_assets *assets = qa_q3_source_scene_bank_registry_at(source->scene_bank, i);
            size_t providers = qa_q3_assets_provider_count(assets);
            for (size_t j = 0; j < providers; ++j) {
                qa_q3_presentation_provider provider;
                if (qa_q3_assets_provider_at(assets, j, &provider) && provider.materials == library) return true;
            }
        }
    }
    return false;
}
typedef struct source_owners {
    size_t wanted, count;
    bool worlds;
    const qa_material_library *library;
    const qa_scene_world *world;
} source_owners;
static void source_owner_library(source_owners *out, const qa_material_library *library)
{
    if (!out->worlds && library) {
        if (out->count == out->wanted) out->library = library;
        ++out->count;
    }
}
static void source_owner_world(source_owners *out, const qa_scene_world *world)
{
    if (!world) return;
    if (out->worlds) {
        if (out->count == out->wanted) out->world = world;
        ++out->count;
    } else source_owner_library(out, qa_scene_world_material_owner(world));
}
static void source_owner_registry(source_owners *out, const qa_q3_presentation_assets *assets)
{
    size_t providers = qa_q3_assets_provider_count(assets);
    for (size_t j = 0; j < providers; ++j) {
        qa_q3_presentation_provider provider;
        if (qa_q3_assets_provider_at(assets, j, &provider)) source_owner_library(out, provider.materials);
    }
    size_t maps = qa_q3_assets_map_count(assets);
    for (size_t j = 0; j < maps; ++j) {
        qa_q3_asset_map_custody map;
        if (qa_q3_assets_map_at(assets, j, &map)) source_owner_world(out, map.world);
    }
}
static void source_owner_rows(source_owners *out, const material_source_submission *row)
{
    for (; row; row = row->next) {
        if (row->original) source_owner_library(out, row->original->library);
        source_owner_world(out, row->held_light_world);
        source_owner_registry(out, row->context.source_model_assets);
    }
}
static source_owners source_owner_inventory(const qa_material_source_scratch *source, size_t wanted, bool worlds)
{
    source_owners out = {.wanted = wanted, .worlds = worlds};
    if (!source || !source->owner || &source->owner->source != source) return out;
    if (source->material) source_owner_library(&out, source->material->library);
    source_owner_world(&out, source->world); source_owner_world(&out, source->view.world);
    source_owner_rows(&out, source->head);
    for (const material_source_operation *op = source->operations; op; op = op->next) {
        source_owner_world(&out, op->view.world); source_owner_rows(&out, op->head);
    }
    size_t count = qa_q3_source_scene_bank_registry_count(source->scene_bank);
    for (size_t i = 0; i < count; ++i) {
        qa_q3_presentation_assets *assets = qa_q3_source_scene_bank_registry_at(source->scene_bank, i);
        source_owner_registry(&out, assets);
    }
    return out;
}
size_t qa_material_source_library_count(const qa_material_source_scratch *source)
{ return source_owner_inventory(source, SIZE_MAX, false).count; }
const qa_material_library *qa_material_source_library_at(const qa_material_source_scratch *source, size_t index)
{ return source_owner_inventory(source, index, false).library; }
size_t qa_material_source_world_count(const qa_material_source_scratch *source)
{ return source_owner_inventory(source, SIZE_MAX, true).count; }
const qa_scene_world *qa_material_source_world_at(const qa_material_source_scratch *source, size_t index)
{ return source_owner_inventory(source, index, true).world; }
bool qa_material_source_entity_scene(qa_material_source_scratch *source, uint32_t *first, qa_error *error)
{
    if (!first || !source_entities_current(source, error)) return false;
    if (source->scene_bank) {
        qa_q3_source_scene_bank_clear(source->scene_bank);
        qa_q3_source_scene_membership membership;
        if (!qa_q3_source_scene_bank_membership(source->scene_bank, &membership)) return false;
        source->entity_count = membership.entities;
        source->submitted_light_count = membership.lights;
    }
    source->first_scene_entity = source->entity_count;
    source->first_scene_light = source->submitted_light_count;
    *first = source->first_scene_entity;
    return true;
}
bool qa_render_controls_read(const qa_render_controls *controls, qa_render_controls_values *out, qa_error *error)
{
    if (!out || !current(controls)) return fail(error, "Renderer controls lost their actual idle renderer owner");
    *out = controls->values;
    return true;
}
bool qa_render_controls_live_primitives(qa_render_controls *controls, int32_t primitives, qa_error *error)
{
    if (!current(controls) || controls->ticket || controls->image_ticket || controls->source.issuing ||
        controls->source.submitting || controls->source.dispatching)
        return fail(error, "Live primitive setting requires its actual waiting renderer owner");
    controls->values.primitives = primitives;
    return true;
}
bool qa_render_controls_source_texture_mode_read(const qa_render_controls *controls,
    qa_scene_filter *filter, bool *initialized, qa_error *error)
{
    if (!filter || !initialized || !idle_owner(controls))
        return fail(error,"Source texture mode requires its actual renderer owner");
    *filter=controls->source_filter;
    *initialized=controls->source_filter_initialized;
    return true;
}
bool qa_render_controls_source_texture_mode(qa_render_controls *controls, qa_scene_filter filter,
    bool no_bind,qa_error *error)
{
    if (!idle_owner(controls) || controls->ticket || controls->image_ticket || controls->source.entered ||
        (unsigned)filter>QA_SCENE_LINEAR_MIPMAP_LINEAR)
        return fail(error,"Source texture mode requires its actual idle image renderer");
    controls->source_filter=filter;
    controls->source_filter_initialized=true;
    return controls->backend==QA_RENDER_CONTROLS_GL?qa_gl_source_texture_filter_apply(controls,no_bind,error):
        qa_cpu_source_texture_filter_apply(controls,no_bind,error);
}
bool qa_render_controls_source_scene_limits_initialize(qa_render_controls *controls,int32_t max_polys,
    int32_t max_polyverts,qa_error *error)
{
    if (!idle_owner(controls) || controls->ticket || controls->image_ticket || controls->source.entered)
        return fail(error,"Source scene allocation requires its actual idle physical renderer");
    if (controls->source_limits_initialized) return true;
    controls->source_max_polys=max_polys<600?600:(uint32_t)max_polys;
    controls->source_max_polyverts=max_polyverts<3000?3000:(uint32_t)max_polyverts;
    controls->source_limits_initialized=true;
    return true;
}
bool qa_render_controls_source_scene_limits_read(const qa_render_controls *controls,uint32_t *max_polys,
    uint32_t *max_polyverts,bool *initialized,qa_error *error)
{
    if (!max_polys || !max_polyverts || !initialized || !idle_owner(controls) || controls->image_ticket)
        return fail(error,"Source scene capacities lost their actual physical renderer");
    *max_polys=controls->source_max_polys;
    *max_polyverts=controls->source_max_polyverts;
    *initialized=controls->source_limits_initialized;
    return true;
}
bool qa_render_controls_source_image_admit(qa_render_controls *controls,const qa_scene_image *image,
    const qa_scene_image *binding,uint32_t unit,qa_error *error)
{
    if (!source_current(controls) || controls->ticket || !image || !image->source_q3 || !binding || unit>1)
        return fail(error,"Source image admission requires its actual completed image and physical renderer");
    bool ok=controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_image_admit(controls,image,binding,unit,error):
        qa_gl_source_image_admit(controls,image,binding,unit,error);
    return ok && (!image->source_after_upload_border ||
        qa_render_controls_source_texture_border(controls,image->source_upload_border,error));
}
bool qa_render_controls_source_dlight_read(const qa_render_controls *controls,const qa_scene_image **out,qa_error *error)
{
    if (!out || !source_current(controls) || controls->ticket)
        return fail(error,"Source dynamic-light binding lost its actual physical renderer");
    size_t count=controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_images_metadata_count(controls):
        qa_gl_source_images_metadata_count(controls);
    *out=NULL;
    while (count) {
        const qa_scene_image *image=controls->backend==QA_RENDER_CONTROLS_CPU?
            qa_cpu_source_image_metadata_at(controls,--count):qa_gl_source_image_metadata_at(controls,--count);
        if (image->source_dlight) { *out=image; break; }
    }
    return true;
}
bool qa_render_controls_source_texture_border(qa_render_controls *controls,qa_scene_vec4 color,qa_error *error)
{
    if (!source_current(controls) || controls->ticket ||
        !controls->attributes.actual_empty[controls->attributes.texture_unit] ||
        !isfinite(color.x) || !isfinite(color.y) || !isfinite(color.z) || !isfinite(color.w))
        return fail(error,"Source post-upload border requires its actual raw-zero texture object");
    if (controls->backend==QA_RENDER_CONTROLS_GL)
        return qa_gl_source_texture_border(controls,color,error);
    controls->attributes.zero_border=color;
    controls->zero_texture.border=color;
    return true;
}
bool qa_render_controls_source_texture_upload(qa_render_controls *controls,const qa_scene_image *slot,
    const qa_scene_image *image,const qa_scene_image *binding,bool redefine,bool dirty,qa_error *error)
{
    if (!source_current(controls) || controls->ticket || !slot || !image || !binding)
        return fail(error,"Source cinematic upload requires its actual registered slot and physical renderer");
    return controls->backend==QA_RENDER_CONTROLS_CPU?
        qa_cpu_source_texture_upload(controls,slot,image,binding,redefine,dirty,error):
        qa_gl_source_texture_upload(controls,slot,image,binding,redefine,dirty,error);
}
bool qa_render_controls_source_images_prepare(qa_render_controls *controls,qa_scene_resource_policy *const *banks,
    size_t count,bool no_bind,const qa_render_source_restart_values *restart,
    qa_render_source_images_ticket **out,qa_error *error)
{
    if (!out || *out || !current(controls) || controls->source.entered || controls->image_ticket || (count && !banks) ||
        (restart && (unsigned)restart->filter>QA_SCENE_LINEAR_MIPMAP_LINEAR))
        return fail(error,"Prepared Source image admission requires its actual renderer and resource children");
    qa_render_source_images_ticket *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual Source image admission"); return false; }
    ticket->controls=controls; controls->image_ticket=ticket; *out=ticket;
    if (restart) {
        ticket->restart=*restart;
        if (!material_source_reset_prepare(controls,restart->max_polys,restart->max_polyverts,&ticket->reset,error))
            return false;
    }
    bool ok=controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_images_prepare(controls,banks,count,no_bind,restart,&ticket->cpu,error):
        qa_gl_source_images_prepare(controls,banks,count,no_bind,restart,&ticket->gl,error);
    ticket->prepared=ok;
    return ok;
}
bool qa_render_controls_source_images_ready_is(const qa_render_source_images_ticket *ticket)
{
    const qa_render_controls *controls=ticket?ticket->controls:NULL;
    return current(controls) && controls->image_ticket==ticket && !controls->source.entered && ticket->prepared &&
        !ticket->published && (!ticket->reset || material_source_reset_ready(ticket->reset)) &&
        (controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_images_ready_is(ticket->cpu):
        qa_gl_source_images_ready_is(ticket->gl));
}
bool qa_render_controls_source_images_ready(const qa_render_source_images_ticket *ticket,qa_error *error)
{ return qa_render_controls_source_images_ready_is(ticket) || fail(error,"Prepared Source image admission changed before publication"); }
void qa_render_controls_source_images_publish(qa_render_source_images_ticket *ticket)
{
    if (!ticket || !ticket->prepared || ticket->published) return;
    if (ticket->controls->backend==QA_RENDER_CONTROLS_GL) qa_gl_source_images_publish(ticket->gl);
    else qa_cpu_source_images_publish(ticket->cpu);
    if (ticket->reset) {
        material_source_reset_publish(ticket->reset);
        ticket->controls->source_filter=ticket->restart.filter;
        ticket->controls->source_filter_initialized=true;
    }
    ticket->published=true;
}
static void source_images_release(qa_render_source_images_ticket **out)
{
    qa_render_source_images_ticket *ticket=*out; qa_render_controls *controls=ticket->controls;
    material_source_reset_dispose(&ticket->reset);
    controls->image_ticket=NULL; free(ticket); *out=NULL;
    if (controls->backend==QA_RENDER_CONTROLS_CPU) qa_cpu_render_controls_close(controls);
    else qa_gl_render_controls_close(controls);
}
bool qa_render_controls_source_images_finish(qa_render_source_images_ticket **out,qa_error *error)
{
    if (!out || !*out) return true;
    qa_render_source_images_ticket *ticket=*out;
    if (ticket->controls->image_ticket!=ticket || !ticket->published)
        return fail(error,"Source image finish requires its actual published recipient");
    if (ticket->gl && !qa_gl_source_images_finish(&ticket->gl,error)) return false;
    if (ticket->cpu && !qa_cpu_source_images_finish(&ticket->cpu,error)) return false;
    source_images_release(out); return true;
}
bool qa_render_controls_source_images_abort(qa_render_source_images_ticket **out,qa_error *error)
{
    if (!out || !*out) return true;
    qa_render_source_images_ticket *ticket=*out;
    if (ticket->controls->image_ticket!=ticket || ticket->published)
        return fail(error,"Source image abort requires its actual unpublished recipient");
    if (ticket->gl && !qa_gl_source_images_abort(&ticket->gl,error)) return false;
    if (ticket->cpu && !qa_cpu_source_images_abort(&ticket->cpu,error)) return false;
    source_images_release(out); return true;
}
qa_scene_filter qa_render_controls_image_filter(const qa_render_controls *controls,const qa_scene_image *image)
{
    if (image->source_q3 && controls->backend==QA_RENDER_CONTROLS_CPU)
        return qa_cpu_source_image_sampling(controls,image,NULL);
    if (image->source_q3 && controls->backend==QA_RENDER_CONTROLS_GL)
        return qa_gl_source_image_filter(controls,image);
    return image->source_q3 && image->source_mipmap ? controls->source_filter : image->filter;
}
void qa_render_source_state_bits(qa_scene_state *state,bool depth_write,bool additive)
{
    state->blend_source=QA_BLEND_ONE;
    state->blend_destination=additive?QA_BLEND_ONE:QA_BLEND_ZERO;
    state->depth_test=QA_DEPTH_LEQUAL;
    state->depth_write=depth_write;
    state->alpha_test=QA_ALPHA_NONE;
    state->wireframe=false;
}
void qa_render_source_direct_state(qa_scene_state *out,const qa_scene_state *current,
    const qa_scene_draw *draw)
{
    *out=*current;
    switch (draw->source_direct) {
    case QA_SOURCE_DIRECT_BEAM: qa_render_source_state_bits(out,false,true); break;
    case QA_SOURCE_DIRECT_AXIS: out->line_width=3; break;
    case QA_SOURCE_DIRECT_SKY:
        qa_render_source_state_bits(out,false,false);
        out->depth_near=draw->state.depth_near;
        out->depth_far=draw->state.depth_far;
        break;
    case QA_SOURCE_DIRECT_RAW:
        out->blend_source=QA_BLEND_SRC_ALPHA;
        out->blend_destination=QA_BLEND_ONE_MINUS_SRC_ALPHA;
        out->depth_test=QA_DEPTH_DISABLED;
        out->depth_write=false;
        out->alpha_test=QA_ALPHA_NONE;
        out->wireframe=false;
        out->cull=QA_CULL_NONE;
        break;
    case QA_SOURCE_DIRECT_IMAGE_GRID:
        break;
    case QA_SOURCE_DIRECT_SHADOW_FINISH:
        qa_render_source_state_bits(out,true,false);
        out->blend_source=QA_BLEND_DST_COLOR;
        out->cull=QA_CULL_NONE;
        out->stencil_enabled=true;
        out->stencil_test=QA_STENCIL_NOTEQUAL;
        out->stencil_reference=0;
        out->stencil_compare_mask=255;
        break;
    case QA_SOURCE_DIRECT_SHADOW_VOLUME_END:
    case QA_SOURCE_DIRECT_NONE:
        *out=draw->state;
        if (draw->source_retain_depth_range) {
            out->depth_near=current->depth_near;
            out->depth_far=current->depth_far;
        }
        if (draw->source_retain_polygon_offset) {
            out->polygon_offset=current->polygon_offset;
            out->offset_factor=current->offset_factor;
            out->offset_units=current->offset_units;
        }
        if (draw->source_stage_state) {
            out->cull=current->cull;
            out->line_width=current->line_width;
            out->color_write=current->color_write;
            out->stencil_enabled=current->stencil_enabled;
            out->stencil_test=current->stencil_test;
            out->stencil_fail=current->stencil_fail;
            out->stencil_depth_fail=current->stencil_depth_fail;
            out->stencil_depth_pass=current->stencil_depth_pass;
            out->stencil_reference=current->stencil_reference;
            out->stencil_compare_mask=current->stencil_compare_mask;
            out->stencil_write_mask=current->stencil_write_mask;
        }
        break;
    }
}
void qa_render_source_stage_state(qa_scene_state *out,const qa_scene_state *state)
{
    out->blend_source=state->blend_source; out->blend_destination=state->blend_destination;
    out->depth_test=state->depth_test; out->depth_write=state->depth_write;
    out->alpha_test=state->alpha_test; out->wireframe=state->wireframe;
}
static bool source_draw_attributes(const qa_scene_draw *draw)
{
    return draw->source_arrays || draw->source_retain_depth_range || draw->source_direct!=QA_SOURCE_DIRECT_NONE;
}
static bool source_direct_coordinates(const qa_scene_draw *draw)
{
    return draw->source_direct==QA_SOURCE_DIRECT_SKY || draw->source_direct==QA_SOURCE_DIRECT_RAW || draw->source_direct==QA_SOURCE_DIRECT_IMAGE_GRID;
}
static bool source_uniform_image(const qa_render_controls *controls,const qa_scene_image *image)
{
    if (!image) return true;
    if (!image->levels || !image->level_count || image->kind==QA_SCENE_DEPTH32F) return false;
    const uint8_t *first=image->levels[0].pixels;
    if (!first) return false;
    size_t components=qa_render_source_texture_alpha(image)?4:3;
    double first_sample[4];
    for (size_t c=0;c<components;++c) first_sample[c]=image->source_q3?
        qa_render_source_texture_component(image->source_format,first[c]):(double)first[c]/255;
    for (size_t level=0;level<image->level_count;++level) {
        const qa_scene_image_level *row=image->levels+level;
        if (!row->pixels || !row->width || !row->height ||
            (size_t)row->width>SIZE_MAX/row->height || (size_t)row->width*row->height>SIZE_MAX/4 ||
            row->bytes<(size_t)row->width*row->height*4) return false;
        const uint8_t *pixels=row->pixels;
        for (size_t i=0;i<(size_t)row->width*row->height;++i)
            for (size_t c=0;c<components;++c) {
                double value=image->source_q3?qa_render_source_texture_component(image->source_format,pixels[i*4+c]):
                    (double)pixels[i*4+c]/255;
                if (value!=first_sample[c]) return false;
            }
    }
    qa_scene_filter filter=qa_render_controls_image_filter(controls,image);
    bool linear=filter==QA_SCENE_LINEAR || filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
        filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
    if (image->wrap==QA_SCENE_CLAMP && linear) {
        const float border[4]={image->border.x,image->border.y,image->border.z,image->border.w};
        for (size_t c=0;c<components;++c) if ((double)border[c]!=first_sample[c]) return false;
    }
    return true;
}
bool qa_render_source_attributes_resolve(qa_render_controls *controls,qa_scene_draw *draw,
    const qa_scene_image *const bound[2],qa_render_primitive_mode mode,qa_error *error)
{
    qa_render_source_attributes *attributes=&controls->attributes;
    if (!source_draw_attributes(draw)) {
        attributes->texture_unit=draw->texture_count>1?1:0;
        attributes->texture_enabled[0]=draw->texture_count!=0;
        attributes->texture_enabled[1]=draw->texture_count>1;
        attributes->color_array=true; attributes->coordinate_array[0]=true;
        attributes->coordinate_array[1]=draw->texture_count>1;
        attributes->environment[1]=draw->environment;
        return true;
    }
    if (source_draw_attributes(draw)) {
        draw->texture_count=attributes->texture_enabled[1]?2:attributes->texture_enabled[0]?1:0;
        draw->environment=attributes->environment[1];
        for (uint32_t unit=0;unit<2;++unit)
            draw->textures[unit]=!attributes->texture_enabled[unit]?NULL:
                controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_texture_image(controls,unit,bound[unit]):
                qa_gl_source_texture_image(controls,unit,bound[unit]);
    }
    if (!draw->mesh.index_count || mode==QA_RENDER_PRIMITIVES_NONE) return true;
    if (draw->source_arrays && draw->mesh.vertex_count>QA_SOURCE_TESS_VERTICES)
        return fail(error,"Source client arrays exceed their actual retained tess cells");
    if (mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS && attributes->texture_unit!=0) {
        uint32_t first=draw->mesh.indices[0];
        if (first>=(draw->source_vertex_storage?draw->source_vertex_storage:draw->mesh.vertex_count) || first>=QA_SOURCE_TESS_VERTICES)
            return fail(error,"Source discrete color index is outside its actual tess cells");
        attributes->color=controls->source.colors[first];
        attributes->color_known=true;
        return fail(error,"Q3 Source discrete multitexture targets 0 and 1 are invalid");
    }
    bool sampled=draw->state.color_write || draw->state.alpha_test!=QA_ALPHA_NONE;
    if (draw->source_arrays && mode!=QA_RENDER_PRIMITIVES_DISCRETE_STRIPS &&
        !attributes->color_array && !attributes->color_known && sampled)
        return fail(error,"Source draw consumes an indeterminate current color");
    if (mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS || !sampled) return true;
    for (size_t unit=0;unit<draw->texture_count;++unit)
        if (!(unit==0 && source_direct_coordinates(draw)) &&
            (!draw->source_arrays || !attributes->coordinate_array[unit]) && !attributes->coordinates_known[unit] &&
            !source_uniform_image(controls,draw->textures[unit]))
            return fail(error,"Source immediate texture has indeterminate coordinates and coordinate-dependent texels");
    return true;
}
void qa_render_source_attributes_vertex(qa_render_controls *controls,const qa_scene_draw *draw,
    qa_render_primitive_mode mode,size_t index,const qa_scene_vertex *vertex,qa_scene_vec4 *color,qa_scene_vec2 uv[2])
{
    qa_render_source_attributes *attributes=&controls->attributes;
    *color=draw->vertex_inputs.constant_color?draw->vertex_inputs.color:vertex->color;
    uv[0]=draw->vertex_inputs.swap_uv?vertex->lightmap:vertex->texcoord;
    uv[1]=draw->vertex_inputs.swap_uv?vertex->texcoord:vertex->lightmap;
    if (!source_draw_attributes(draw)) return;
    if (draw->source_arrays && mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS) {
        *color=controls->source.colors[index]; uv[0]=controls->source.coordinates[0][index];
        uv[1]=attributes->coordinates[1];
        return;
    }
    if (draw->source_arrays && !attributes->color_array)
        *color=attributes->color_known?attributes->color:(qa_scene_vec4){1,1,1,1};
    for (size_t unit=0;unit<2;++unit) {
        if (unit==0 && source_direct_coordinates(draw)) continue;
        if (!draw->source_arrays || !attributes->coordinate_array[unit])
            uv[unit]=attributes->coordinates_known[unit]?attributes->coordinates[unit]:(qa_scene_vec2){0,0};
        else {
            uint32_t bank=attributes->coordinate_bank[unit];
            if (attributes->coordinate_kind[unit]==MATERIAL_SOURCE_COORDINATES_TESS)
                uv[unit]=bank?controls->source.vertices[index].lightmap:controls->source.vertices[index].texcoord;
            else if (attributes->coordinate_kind[unit]==MATERIAL_SOURCE_COORDINATES_DRAW)
                uv[unit]=bank?vertex->lightmap:vertex->texcoord;
            else uv[unit]=controls->source.coordinates[bank][index];
        }
    }
}
void qa_render_source_attributes_finish(qa_render_controls *controls,const qa_scene_draw *draw,qa_render_primitive_mode mode)
{
    qa_render_source_attributes *attributes=&controls->attributes;
    if (!draw->mesh.index_count || mode==QA_RENDER_PRIMITIVES_NONE) return;
    bool source=source_draw_attributes(draw);
    if (source && !draw->source_arrays) {
        const qa_scene_vertex *last=draw->mesh.vertices+draw->mesh.indices[draw->mesh.index_count-1];
        attributes->color=last->color; attributes->color_known=true;
        if (source_direct_coordinates(draw)) {
            attributes->coordinates[0]=last->texcoord; attributes->coordinates_known[0]=true;
        }
        if (draw->source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) attributes->color=(qa_scene_vec4){1,1,1,1};
        return;
    }
    if (mode==QA_RENDER_PRIMITIVES_INDEXED) {
        if (!source || attributes->color_array) attributes->color_known=false;
        for (size_t unit=0;unit<2;++unit)
                if (source?attributes->coordinate_array[unit]:unit<draw->texture_count) attributes->coordinates_known[unit]=false;
        if (!source) {
            attributes->texture_unit=0; attributes->texture_enabled[1]=false; attributes->coordinate_array[1]=false;
        }
        return;
    }
    size_t cursor=0; qa_render_strip strip;
    while (qa_render_strip_next(draw->mesh.indices,draw->mesh.index_count,&cursor,&strip))
        for (size_t ordinal=0;ordinal<strip.triangles+2;++ordinal) {
            uint32_t index=qa_render_strip_vertex(&strip,ordinal);
            qa_scene_vec4 color; qa_scene_vec2 uv[2];
            qa_render_source_attributes_vertex(controls,draw,mode,index,draw->mesh.vertices+index,&color,uv);
            if (mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS || attributes->color_array) {
                attributes->color=color; attributes->color_known=true;
            }
            for (size_t unit=0;unit<2;++unit)
                if (mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS?unit==0:attributes->coordinate_array[unit]) {
                    attributes->coordinates[unit]=uv[unit]; attributes->coordinates_known[unit]=true;
                }
        }
}
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *controls, qa_error *error)
{
    if (!controls || controls->source.owner != controls || controls->ticket ||
        (controls->source.entered && (controls->source.dispatching || controls->source.submitting)) ||
        (controls->backend == QA_RENDER_CONTROLS_CPU ? !controls->owner.cpu :
         controls->backend == QA_RENDER_CONTROLS_GL ? !controls->owner.gl : true)) {
        fail(error, "Source tess requires its actual renderer allocation"); return NULL;
    }
    return &controls->source;
}
const qa_material_source_scratch *qa_render_controls_source_metadata(const qa_render_controls *controls,
    qa_error *error)
{
    if (!idle_owner(controls) || controls->source.owner != controls || controls->source.entered) {
        fail(error, "Source metadata requires its actual non-entered renderer owner"); return NULL;
    }
    return &controls->source;
}
bool material_source_enter(qa_material_source_scratch *source, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!source_current(owner) || &owner->source != source || owner->ticket || source->submitting ||
        (source->entered && !source->dispatching))
        return fail(error, "Source tess lost its actual available renderer owner");
    source->entered = source->submitting = true; return true;
}
bool qa_material_source_vertices(qa_material_source_scratch *source,
    const qa_scene_vertex **out, size_t *count, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !count || !source_current(owner) || &owner->source != source || owner->ticket ||
        (source->entered && !source->collecting))
        return fail(error, "Source vertices require their actual idle renderer allocation");
    *out = source->vertices; *count = QA_SOURCE_TESS_VERTICES; return true;
}
bool material_source_current(const qa_material_source_scratch *source, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    return (source_current(owner) && &owner->source == source && source->entered && !owner->ticket) ||
        fail(error, "Source tess renderer retired during a reached operation");
}
bool material_source_depth_range(qa_material_source_scratch *source, float near_depth, float far_depth, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source depth range requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_depth_range(source->owner, near_depth, far_depth, error) :
        qa_gl_source_depth_range(source->owner, near_depth, far_depth, error);
}
void material_source_leave(qa_material_source_scratch *source)
{
    qa_render_controls *owner = source->owner;
    source->submitting = false;
    if (source->dispatching) return;
    source->entered = false;
    switch (owner->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(owner); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(owner); break;
    }
}
bool material_source_polygon_offset(qa_material_source_scratch *source, bool enabled,
    float factor, float units, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source polygon offset requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_polygon_offset(source->owner, enabled, factor, units, error) :
        qa_gl_source_polygon_offset(source->owner, enabled, factor, units, error);
}
bool material_source_cull(qa_material_source_scratch *source, qa_scene_cull cull, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing || (unsigned)cull > QA_CULL_BACK)
        return fail(error, "Source face culling requires its actual reached command issue");
    qa_render_controls *controls = source->owner;
    if (controls->source_cull_valid && controls->source_cull_type == cull) return true;
    controls->source_cull_type = cull;
    controls->source_cull_valid = true;
    if (source->view_mirror && cull != QA_CULL_NONE)
        cull = cull == QA_CULL_FRONT ? QA_CULL_BACK : QA_CULL_FRONT;
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_cull(source->owner, cull, error) : qa_gl_source_cull(source->owner, cull, error);
}
bool material_source_cull_disable(qa_material_source_scratch *source, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source direct cull disable requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_cull(source->owner, QA_CULL_NONE, error) :
        qa_gl_source_cull(source->owner, QA_CULL_NONE, error);
}
bool material_source_cull_invalidate(qa_material_source_scratch *source, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source cull invalidation requires its actual reached view");
    source->owner->source_cull_valid = false;
    return true;
}
static bool source_attribute_issue(qa_material_source_scratch *source,qa_error *error)
{
    return (material_source_current(source,error) && source->issuing) ||
        fail(error,"Source texture/client state requires its actual reached command issue");
}
bool material_source_color(qa_material_source_scratch *source, qa_scene_vec4 color, qa_error *error)
{
    if (!source_attribute_issue(source, error) || !isfinite(color.x) || !isfinite(color.y) ||
        !isfinite(color.z) || !isfinite(color.w))
        return fail(error, "Source current color requires its actual reached finite components");
    if (source->owner->backend == QA_RENDER_CONTROLS_GL)
        return qa_gl_source_color(source->owner, color, error);
    source->owner->attributes.color = color;
    source->owner->attributes.color_known = true;
    return true;
}
bool material_source_client_arrays(qa_material_source_scratch *source,bool color,bool uv,qa_error *error)
{
    if (!source_attribute_issue(source,error)) return false;
    qa_render_controls *controls=source->owner;
    if (controls->backend==QA_RENDER_CONTROLS_GL) return qa_gl_source_client_arrays(controls,color,uv,error);
    controls->attributes.color_array=color;
    controls->attributes.coordinate_array[controls->attributes.texture_unit]=uv;
    return true;
}
bool material_source_client_coordinate_pointer(qa_material_source_scratch *source,material_source_coordinate_kind kind,
    uint32_t bank,qa_error *error)
{
    if (!source_attribute_issue(source,error) || (unsigned)kind>MATERIAL_SOURCE_COORDINATES_DRAW || bank>1)
        return fail(error,"Source coordinate pointer has no actual retained cell bank");
    qa_render_source_attributes *attributes=&source->owner->attributes;
    attributes->coordinate_kind[attributes->texture_unit]=kind;
    attributes->coordinate_bank[attributes->texture_unit]=bank;
    return true;
}
bool material_source_stage_state(qa_material_source_scratch *source,const qa_scene_state *state,qa_error *error)
{
    if (!source_attribute_issue(source,error) || !state || (unsigned)state->blend_source>QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->blend_destination>QA_BLEND_SRC_ALPHA_SATURATE || state->blend_destination==QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->depth_test>QA_DEPTH_DISABLED || (unsigned)state->alpha_test>QA_ALPHA_GE128)
        return fail(error,"Source GL_State has invalid reached state bits");
    return source->owner->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_stage_state(source->owner,state,error):
        qa_gl_source_stage_state(source->owner,state,error);
}
bool material_source_view_read(qa_material_source_scratch *source,qa_scene_view *out,qa_error *error)
{
    if (!out || !source || !source_current(source->owner) || &source->owner->source!=source)
        return fail(error,"Source view requires its actual physical renderer owner");
    return source->owner->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_view_read(source->owner,out,error):
        qa_gl_source_view_read(source->owner,out,error);
}
bool material_source_texture_select(qa_material_source_scratch *source,uint32_t unit,qa_error *error)
{
    if (!source_attribute_issue(source,error) || unit>1) return fail(error,"Source texture unit is outside its two physical units");
    qa_render_controls *controls=source->owner;
    if (controls->backend==QA_RENDER_CONTROLS_GL) return qa_gl_source_texture_select(controls,unit,error);
    controls->attributes.texture_unit=unit;
    return true;
}
bool material_source_texture_enable(qa_material_source_scratch *source,bool enabled,qa_error *error)
{
    if (!source_attribute_issue(source,error)) return false;
    qa_render_controls *controls=source->owner;
    if (controls->backend==QA_RENDER_CONTROLS_GL) return qa_gl_source_texture_enable(controls,enabled,error);
    controls->attributes.texture_enabled[controls->attributes.texture_unit]=enabled;
    return true;
}
bool material_source_texture_environment(qa_material_source_scratch *source,qa_scene_texture_environment environment,
    qa_error *error)
{
    if (!source_attribute_issue(source,error) || (unsigned)environment>QA_TEXTURE_REPLACE)
        return fail(error,"Source texture environment is invalid");
    qa_render_controls *controls=source->owner;
    if (controls->backend==QA_RENDER_CONTROLS_GL) return qa_gl_source_texture_environment(controls,environment,error);
    controls->attributes.environment[controls->attributes.texture_unit]=environment;
    return true;
}
bool material_source_texture_bind(qa_material_source_scratch *source,const qa_scene_image *image,qa_error *error)
{
    if (!source_attribute_issue(source,error)) return false;
    return source->owner->backend==QA_RENDER_CONTROLS_CPU ? qa_cpu_source_texture_bind(source->owner,image,error) :
        qa_gl_source_texture_bind(source->owner,image,error);
}
bool material_source_execute_prefix(qa_material_source_scratch *source, const qa_scene_frame *frame, bool finish, qa_error *error)
{
    if (!source || !source->issuing || source->frame != frame || source->issued_count > frame->command_count ||
        !material_source_current(source, error)) return false;
    bool begin = !source->issue_started;
    source->issue_started = true;
    size_t first = finish ? frame->command_count : source->issued_count;
    bool ok = source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_execute_prefix(source->owner, frame, first, begin, finish, error) :
        qa_gl_source_execute_prefix(source->owner, frame, first, begin, finish, error);
    if (ok) source->issued_count = frame->command_count;
    return ok;
}
bool qa_render_controls_prepare(qa_render_controls *controls, const qa_render_controls_values *values,
    qa_render_controls_ticket **out, qa_error *error)
{
    if (!values || !out || *out || !current(controls) || controls->ticket || controls->source.entered)
        return fail(error, "Renderer controls require their actual idle owner and empty ticket");
    qa_render_controls_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining renderer controls ticket");
        return false;
    }
    ticket->controls = controls;
    ticket->original = controls->values;
    ticket->candidate = *values;
    controls->ticket = ticket;
    *out = ticket;
    return true;
}
bool qa_render_controls_ready_is(const qa_render_controls_ticket *ticket)
{
    const qa_render_controls *controls = ticket ? ticket->controls : NULL;
    return current(controls) && controls->ticket == ticket && !ticket->published &&
        controls->values.primitives == ticket->original.primitives &&
        controls->values.compiled_vertex_arrays == ticket->original.compiled_vertex_arrays;
}
bool qa_render_controls_ready(const qa_render_controls_ticket *ticket, qa_error *error)
{
    return qa_render_controls_ready_is(ticket) || fail(error, "Renderer controls ticket is no longer ready");
}
void qa_render_controls_publish(qa_render_controls_ticket *ticket)
{
    if (!ticket || ticket->published) return;
    ticket->controls->values = ticket->candidate;
    ticket->published = true;
}
static void release(qa_render_controls_ticket **out)
{
    qa_render_controls_ticket *ticket = *out;
    qa_render_controls *controls = ticket->controls;
    controls->ticket = NULL;
    free(ticket);
    *out = NULL;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(controls); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(controls); break;
    }
}
void qa_render_controls_consume(qa_render_controls_ticket **out)
{
    if (!out || !*out) return;
    qa_render_controls_publish(*out);
    release(out);
}
bool qa_render_controls_finish(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && (!(*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls finish requires its published retained ticket");
    if (*out) release(out);
    return true;
}
bool qa_render_controls_abort(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && ((*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls abort requires its unpublished retained ticket");
    if (*out) release(out);
    return true;
}
qa_render_primitive_mode qa_render_primitives_mode(int32_t requested, bool indexed_arrays)
{
    if (!requested) return indexed_arrays ? QA_RENDER_PRIMITIVES_INDEXED : QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 1) return QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 2) return QA_RENDER_PRIMITIVES_INDEXED;
    if (requested == 3) return QA_RENDER_PRIMITIVES_DISCRETE_STRIPS;
    return QA_RENDER_PRIMITIVES_NONE;
}
bool qa_render_strip_next(const uint32_t *indices, size_t count, size_t *cursor, qa_render_strip *out)
{
    if (*cursor == count) return false;
    size_t first = *cursor;
    uint32_t a = indices[first], b = indices[first + 1], c = indices[first + 2];
    bool even = false;
    size_t next = first + 3;
    while (next < count) {
        uint32_t na = indices[next], nb = indices[next + 1], nc = indices[next + 2];
        if (even ? na != a || nb != c : na != c || nb != b) break;
        a = na; b = nb; c = nc; even = !even;
        next += 3;
    }
    *out = (qa_render_strip){indices + first, (next - first) / 3};
    *cursor = next;
    return true;
}
uint32_t qa_render_strip_vertex(const qa_render_strip *strip, size_t ordinal)
{
    return strip->indices[ordinal < 3 ? ordinal : (ordinal - 2) * 3 + 2];
}

bool qa_render_controls_source_print_bind(qa_render_controls *controls,void (*print)(void *,const char *),
    void *context,qa_error *error)
{
    if (!idle_owner(controls) || controls->ticket || controls->image_ticket || controls->source.entered || !print)
        return fail(error,"Source renderer printing requires its actual idle physical owner");
    controls->source_print=print; controls->source_print_context=context; return true;
}
bool qa_render_controls_source_frame_policy(qa_render_controls *controls,const qa_render_source_frame_values *values,
    qa_error *error)
{
    if (!source_current(controls) || controls->ticket || controls->image_ticket || !values)
        return fail(error,"Source frame diagnostics require their actual renderer owner");
    controls->frame_values=*values;
    return controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_overdraw(controls,values->measure_overdraw,error):
        qa_gl_source_overdraw(controls,values->measure_overdraw,error);
}
bool qa_render_controls_source_begin_frame(qa_render_controls *controls,qa_error *error)
{
    if (!source_current(controls) || controls->ticket || controls->image_ticket)
        return fail(error,"Source BeginFrame diagnostics lost their physical owner");
    controls->finish_called=false;
    return true;
}
bool qa_render_controls_source_image_grid(qa_render_controls *controls,int32_t mode,qa_error *error)
{
    if (!source_current(controls) || controls->ticket || controls->image_ticket)
        return fail(error,"Source image grid lost its physical image owner");
    return controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_image_grid(controls,mode,error):
        qa_gl_source_image_grid(controls,mode,error);
}

void qa_render_source_report(qa_render_controls *controls,uint32_t width,uint32_t height)
{
    qa_render_source_counters *pc=&controls->counters;
    char text[512]={0};
    switch (controls->frame_values.speeds) {
    case 1: {
        uint64_t pixels=0;
        size_t count=controls->backend==QA_RENDER_CONTROLS_CPU?qa_cpu_source_images_metadata_count(controls):
            qa_gl_source_images_metadata_count(controls);
        for (size_t i=0;i<count;++i) {
            if (!controls->image_used[i]) continue;
            const qa_scene_image *image=controls->backend==QA_RENDER_CONTROLS_CPU?
                qa_cpu_source_image_metadata_at(controls,i):qa_gl_source_image_metadata_at(controls,i);
            if (image && image->level_count) pixels+=(uint64_t)image->levels[0].width*image->levels[0].height;
        }
        snprintf(text,sizeof(text),"%" PRIu64 "/%" PRIu64 " shaders/surfs %" PRIu64 " leafs %" PRIu64
            " verts %" PRIu64 "/%" PRIu64 " tris %.2f mtex %.2f dc\n",pc->shaders,pc->surfaces,pc->leaves,
            pc->vertices,pc->indexes/3,pc->total_indexes/3,(double)pixels/1000000,
            width && height?(double)pc->overdraw/((double)width*height):0);
        break;
    }
    case 2:
        snprintf(text,sizeof(text),"(patch) %" PRIu64 " sin %" PRIu64 " sclip %" PRIu64 " sout %" PRIu64
            " bin %" PRIu64 " bclip %" PRIu64 " bout\n(md3) %" PRIu64 " sin %" PRIu64 " sclip %" PRIu64
            " sout %" PRIu64 " bin %" PRIu64 " bclip %" PRIu64 " bout\n",
            pc->patch_sphere[0],pc->patch_sphere[1],pc->patch_sphere[2],pc->patch_box[0],pc->patch_box[1],pc->patch_box[2],
            pc->md3_sphere[0],pc->md3_sphere[1],pc->md3_sphere[2],pc->md3_box[0],pc->md3_box[1],pc->md3_box[2]); break;
    case 3: snprintf(text,sizeof(text),"viewcluster: %" PRId32 "\n",pc->view_cluster); break;
    case 4:
        if (pc->dlight_vertices) snprintf(text,sizeof(text),"dlight srf:%" PRIu64 " culled:%" PRIu64
            " verts:%" PRIu64 " tris:%" PRIu64 "\n",pc->dlight_surfaces,pc->dlight_culled,pc->dlight_vertices,pc->dlight_indexes/3);
        break;
    case 5: snprintf(text,sizeof(text),"zFar: %.0f\n",(double)pc->far_clip); break;
    case 6: snprintf(text,sizeof(text),"flare adds:%" PRIu64 " tests:%" PRIu64 " renders:%" PRIu64 "\n",
        pc->flare_adds,pc->flare_tests,pc->flare_renders); break;
    default: break;
    }
    if (text[0] && controls->source_print) controls->source_print(controls->source_print_context,text);
    *pc=(qa_render_source_counters){0};
    memset(controls->image_used,0,sizeof(controls->image_used));
}

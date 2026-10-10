#include "qa/scene.h"
#include "frame_internal.h"
#include "resources_internal.h"
#include "models/internal.h"
#include "qa/material_source_scratch.h"
#include "../material/source_scratch_private.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool scene_frame_reserve(qa_scene_frame *frame,unsigned array,void **data,
    size_t *capacity,size_t needed,size_t stride,qa_error *error)
{
    if (needed <= *capacity) return true;
    if (stride == 0 || needed > (size_t)PTRDIFF_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene array size overflow");
        return false;
    }
    size_t next = *capacity == 0 ? 64 : *capacity;
    while (next < needed) {
        if (next > (size_t)PTRDIFF_MAX / stride / 2) { next = needed; break; }
        next *= 2;
    }
    qa_scene_frame_storage *storage=frame->reserved;
    size_t slot=0,pages=0;
    void *replacement=NULL;bool reused=false;
    if(storage){
        size_t bytes=next*stride;
        pages=bytes/storage->pages.stride+(bytes%storage->pages.stride!=0);
        if(storage->arrays[array].pages){
            slot=storage->arrays[array].slot;
            replacement=qa_pool_grow_run(&storage->pages,&slot,storage->arrays[array].pages,pages);
            reused=replacement!=NULL;
        }
        if(!replacement)replacement=qa_pool_take_run(&storage->pages,pages,&slot);
    }else replacement=realloc(*data,next*stride);
    if (replacement == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow scene frame storage");
        return false;
    }
    if(storage){
        if(*data && replacement!=*data)memmove(replacement,*data,*capacity*stride);
        if(!reused && storage->arrays[array].pages)
            qa_pool_release_run(&storage->pages,storage->arrays[array].slot,storage->arrays[array].pages);
        storage->arrays[array].slot=slot;storage->arrays[array].pages=pages;
    }
    *data = replacement;
    *capacity = next;
    return true;
}

bool qa_scene_frame_prepare(qa_scene_frame *frame,size_t bytes,qa_error *error)
{
    if(!frame || frame->commands || frame->images || frame->geometries || frame->models ||
        frame->groups || frame->sort_groups || frame->sort_commands || frame->storage.first || frame->reserved){
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"scene reservation requires an empty initialized frame");return false;
    }
    if(!bytes)bytes=64u*1024u*1024u;
    qa_scene_frame_storage *storage=calloc(1,sizeof(*storage));
    if(!storage){qa_error_set(error,QA_ERROR_MEMORY,0,"allocating scene reservation owner");return false;}
    size_t page_bytes=16384,count=bytes/page_bytes+(bytes%page_bytes!=0);
    if(!qa_pool_prepare(&storage->pages,&storage->backing,count,page_bytes,_Alignof(max_align_t),error)){
        qa_arena_destroy(&storage->backing);free(storage);return false;
    }
    qa_arena_seal(&storage->backing);
    qa_arena_init_pool(&frame->storage,&storage->pages);
    frame->reserved=storage;return true;
}

void qa_scene_frame_init(qa_scene_frame *frame, uint64_t owner)
{
    if (frame == NULL) return;
    *frame = (qa_scene_frame){.owner = owner};
    qa_arena_init(&frame->storage, 262144);
}

bool qa_scene_frame_material_order(qa_scene_frame *frame, qa_material_order *order, qa_error *error)
{
    if (!frame || frame->group_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "bind material order before preparing scene groups");
        return false;
    }
    frame->material_order = order; return true;
}

void qa_scene_frame_reset(qa_scene_frame *frame, uint64_t sequence)
{
    if (frame == NULL) return;
    if (frame->source_pending) {
        qa_error ignored = {0};
        (void)qa_material_source_frame_end(frame->source_pending, frame, false, &ignored);
    }
    for (size_t i = 0; i < frame->image_count; ++i) qa_scene_image_release(frame->images[i]);
    frame->image_count = 0;
    frame->image_epoch = 0;
    frame->stream_images = NULL;
    for (size_t i = 0; i < frame->geometry_count; ++i)
        qa_scene_geometry_release(frame->geometries[i]);
    frame->geometry_count = 0;
    for (size_t i = 0; i < frame->model_count; ++i)
        scene_model_frame_release(&frame->models[i]);
    frame->model_count = 0;
    frame->command_count = 0;
    frame->picture_view_end = 0;
    frame->group_count = 0;
    frame->sequence = sequence;
    frame->source_backend = false;
    frame->source_skip_backend = false;
    frame->source_front_buffer = false;
    frame->source_clear_draw_buffer = false;
    frame->source_begin_frame=false;
    frame->source_stereo_frame=0;
    qa_arena_reset(&frame->storage);
}

void qa_scene_frame_destroy(qa_scene_frame *frame)
{
    if (frame == NULL) return;
    qa_scene_frame_reset(frame, 0);
    qa_arena_destroy(&frame->storage);
    if(frame->reserved){
        qa_arena_destroy(&frame->reserved->backing);free(frame->reserved);
    }else{
        free(frame->commands);free(frame->images);free(frame->geometries);free(frame->models);
        free(frame->groups);free(frame->sort_groups);free(frame->sort_commands);
    }
    *frame = (qa_scene_frame){0};
}

static bool pin(qa_scene_frame *frame, const qa_scene_image *image, qa_error *error)
{
    if (image == NULL) return true;
    if (!frame->image_epoch) frame->image_epoch = qa_scene_identity();
    owned_image *owned = (owned_image *)image;
    if (frame->image_epoch) {
        if (owned->frame_pin_epoch == frame->image_epoch) return true;
    } else {
        for (size_t i = 0; i < frame->image_count; ++i)
            if (frame->images[i] == image) return true;
    }
    if (frame->image_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene image reference count overflow");
        return false;
    }
    void *data = frame->images;
    if (!scene_frame_reserve(frame,SCENE_IMAGES,&data, &frame->image_capacity, frame->image_count + 1,
                 sizeof(*frame->images), error)) return false;
    frame->images = data;
    qa_scene_image_retain(image);
    frame->images[frame->image_count++] = image;
    owned->frame_pin_epoch = frame->image_epoch;
    return true;
}

bool qa_scene_frame_geometry(qa_scene_frame *frame, const qa_scene_geometry *geometry, qa_error *error)
{
    if (frame == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene geometry pin requires a frame");
        return false;
    }
    if (geometry == NULL || (frame->geometry_count != 0 &&
        frame->geometries[frame->geometry_count - 1] == geometry)) return true;
    if (frame->geometry_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene geometry reference count overflow");
        return false;
    }
    void *data = frame->geometries;
    if (!scene_frame_reserve(frame,SCENE_GEOMETRIES,&data, &frame->geometry_capacity, frame->geometry_count + 1,
                 sizeof(*frame->geometries), error)) return false;
    frame->geometries = data;
    qa_scene_geometry_retain(geometry);
    frame->geometries[frame->geometry_count++] = geometry;
    return true;
}

bool qa_scene_frame_model(qa_scene_frame *frame, qa_scene_model *model,
    const qa_model_pose *pose, size_t count, const qa_scene_skin_pose **out, qa_error *error)
{
    if (!frame || !model || !pose || !out || !count || count != model->source->bone_count ||
        count > (size_t)PTRDIFF_MAX / sizeof(*pose)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene model pose requires its frame, owner and exact joints");
        return false;
    }
    for (size_t i = 0; i < frame->model_count; ++i)
        if (frame->models[i].model == model && frame->models[i].pose == pose) {
            *out = frame->models[i].prepared;
            return true;
        }
    if (frame->model_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene model reference count overflow");
        return false;
    }
    void *data = frame->models;
    if (!scene_frame_reserve(frame,SCENE_MODELS,&data, &frame->model_capacity, frame->model_count + 1,
        sizeof(*frame->models), error)) return false;
    frame->models = data;
    qa_scene_model_pin pin = {0};
    if (!scene_model_frame_retain(&pin, model, pose, error)) return false;
    qa_scene_skin_pose *prepared = qa_arena_alloc(&frame->storage, sizeof(*prepared),
        _Alignof(qa_scene_skin_pose), error);
    size_t bytes = count * sizeof(*pose);
    qa_model_pose *joints = qa_arena_alloc(&frame->storage, bytes, _Alignof(qa_model_pose), error);
    if (!prepared || !joints) { scene_model_frame_release(&pin); return false; }
    memcpy(joints, pose, bytes);
    *prepared = (qa_scene_skin_pose){.joints = joints, .count = count, .ordinal = frame->command_count};
    pin.prepared = prepared;
    frame->models[frame->model_count++] = pin;
    *out = prepared;
    return true;
}

bool qa_scene_draw_lightmap_split(const qa_scene_draw *draw, qa_scene_draw *base,
                                 qa_scene_draw *lightmap)
{
    if (draw->environment < QA_TEXTURE_LIGHTMAP_MODULATE ||
        draw->environment > QA_TEXTURE_LIGHTMAP_INVERT_ALPHA) return false;
    if (base) {
        *base = *draw;
        base->brush.present = false;
        base->texture_count = 1;
        base->textures[1] = NULL;
        base->environment = QA_TEXTURE_MODULATE;
        base->lighting = QA_LIGHT_VERTEX;
        base->light_pass = QA_LIGHT_PASS_TEXTURE;
        base->lights = NULL; base->light_count = 0; base->shadow_atlas = NULL;
        base->shadow_near = 0; base->shade_scale = 0; base->model_shade_scale = false;
    }
    if (lightmap) {
        *lightmap = *draw;
        lightmap->brush = (qa_scene_brush_surface){0};
        lightmap->texture_count = 1;
        lightmap->textures[0] = draw->textures[1];
        lightmap->textures[1] = NULL;
        lightmap->environment = QA_TEXTURE_MODULATE;
        lightmap->vertex_inputs = (qa_scene_vertex_inputs){.constant_color = true,
            .swap_uv = true, .color = {1, 1, 1, 1}};
        bool direct = draw->environment == QA_TEXTURE_LIGHTMAP_MODULATE;
        lightmap->state.blend_source = direct ? QA_BLEND_DST_COLOR : QA_BLEND_ZERO;
        lightmap->state.blend_destination = direct ? QA_BLEND_ZERO :
            draw->environment == QA_TEXTURE_LIGHTMAP_INVERT_ALPHA
                ? QA_BLEND_ONE_MINUS_SRC_ALPHA : QA_BLEND_ONE_MINUS_SRC_COLOR;
        lightmap->state.depth_test = QA_DEPTH_EQUAL;
        lightmap->state.depth_write = false;
        lightmap->fog = (qa_scene_fog){0};
    }
    return true;
}

static bool lightmap_fold(qa_scene_frame *frame, const qa_scene_draw *lightmap)
{
    if (frame->source_pending || !frame->command_count ||
        frame->commands[frame->command_count - 1].kind != QA_SCENE_COMMAND_DRAW)
        return false;
    qa_scene_draw *base = &frame->commands[frame->command_count - 1].data.draw;
    if (base->texture_count != 1 || lightmap->texture_count != 1 ||
        !base->textures[0] || !lightmap->textures[0] ||
        base->lighting != QA_LIGHT_VERTEX ||
        lightmap->light_pass != QA_LIGHT_PASS_LIGHTMAP || lightmap->light_count ||
        (lightmap->lighting != QA_LIGHT_VERTEX && lightmap->lighting != QA_LIGHT_Q2_WORLD) ||
        base->mesh.primitive != QA_SCENE_TRIANGLES || !base->single_coverage ||
        base->state.blend_source != QA_BLEND_ONE || base->state.blend_destination != QA_BLEND_ZERO ||
        base->state.depth_test != QA_DEPTH_LEQUAL || !base->state.depth_write ||
        !base->state.color_write || base->state.alpha_test != QA_ALPHA_NONE ||
        base->state.stencil_enabled || base->state.polygon_offset || base->state.wireframe ||
        (base->fog.kind != QA_FOG_NONE && base->fog.kind != QA_FOG_Q2 &&
         !(base->fog.kind == QA_FOG_EXP2 && base->fog.density == 0 &&
           base->fog.height_density == 0 && base->fog.effect != QA_FOG_OVERLAY)) ||
        base->luminance_alpha || base->source_primitives || base->source_direct ||
        base->source_stage_state || base->source_arrays || base->source_retain_depth_range ||
        base->source_retain_polygon_offset || base->retain_texture[0]) return false;
    qa_scene_texture_environment environment;
    if (lightmap->state.blend_source == QA_BLEND_DST_COLOR &&
        lightmap->state.blend_destination == QA_BLEND_ZERO)
        environment = QA_TEXTURE_LIGHTMAP_MODULATE;
    else if (lightmap->state.blend_source == QA_BLEND_ZERO &&
             lightmap->state.blend_destination == QA_BLEND_ONE_MINUS_SRC_COLOR)
        environment = QA_TEXTURE_LIGHTMAP_INVERT_COLOR;
    else if (lightmap->state.blend_source == QA_BLEND_ZERO &&
             lightmap->state.blend_destination == QA_BLEND_ONE_MINUS_SRC_ALPHA)
        environment = QA_TEXTURE_LIGHTMAP_INVERT_ALPHA;
    else return false;
    qa_scene_draw fused = *base;
    fused.environment = environment;
    fused.texture_count = 2; fused.textures[1] = lightmap->textures[0];
    fused.lighting = lightmap->lighting; fused.light_pass = lightmap->light_pass;
    fused.lights = lightmap->lights; fused.light_count = lightmap->light_count;
    fused.shadow_atlas = lightmap->shadow_atlas; fused.shadow_near = lightmap->shadow_near;
    qa_scene_draw first, second;
    (void)qa_scene_draw_lightmap_split(&fused, &first, &second);
    /* Replaying this projection must recover both admitted commands exactly;
     * otherwise preserve the original independent passes. */
    if (memcmp(base, &first, sizeof(first)) || memcmp(lightmap, &second, sizeof(second))) return false;
    fused.brush.present = fused.brush.polygon_vertices != 0;
    *base = fused;
    return true;
}

bool qa_scene_frame_emit(qa_scene_frame *frame, const qa_scene_command *command, qa_error *error)
{
    if (frame == NULL || command == NULL || command->kind < QA_SCENE_COMMAND_VIEW ||
        command->kind > QA_SCENE_COMMAND_PARTICLES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene command");
        return false;
    }
    /* The command may be borrowed from this frame; snapshot before growth. */
    qa_scene_command copied = *command;
    frame->picture_view_end = 0;
    if (frame->source_pending && (copied.kind == QA_SCENE_COMMAND_VIEW || copied.kind == QA_SCENE_COMMAND_TARGET ||
        copied.kind == QA_SCENE_COMMAND_OPACITY_BEGIN || copied.kind == QA_SCENE_COMMAND_OUTPUT_DOMAIN ||
        copied.kind == QA_SCENE_COMMAND_PREBLEND_GAMMA || copied.kind == QA_SCENE_COMMAND_PARTICLES) &&
        !qa_material_source_picture_end(frame->source_pending, frame, error)) return false;
    if (frame->command_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene command count overflow");
        return false;
    }
    void *data = frame->commands;
    if (!scene_frame_reserve(frame,SCENE_COMMANDS,&data, &frame->command_capacity, frame->command_count + 1,
                 sizeof(*frame->commands), error)) return false;
    frame->commands = data;
    if (copied.kind == QA_SCENE_COMMAND_DRAW) {
        qa_scene_draw *draw = &copied.data.draw;
        qa_scene_fog *fog = &draw->fog;
        if ((fog->kind == QA_FOG_CONSTANT && fog->amount == 0) ||
            (fog->kind == QA_FOG_EXP2 && (fog->effect == QA_FOG_NO_EFFECT ||
             (fog->density == 0 && (fog->effect == QA_FOG_COLOR ||
              fog->effect == QA_FOG_RGB || fog->effect == QA_FOG_ALPHA ||
              fog->effect == QA_FOG_RGBA)))))
            fog->kind = QA_FOG_NONE;
        if (draw->texture_count > 2 || (draw->mesh.identity != 0 && draw->mesh.geometry == NULL) ||
            !material_source_vertex_storage_valid(draw) ||
            (draw->mesh.vertex_count != 0 && draw->mesh.vertices == NULL) ||
            (draw->mesh.index_count != 0 && draw->mesh.indices == NULL) ||
            (draw->light_count != 0 && draw->lights == NULL)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene draw storage");
            return false;
        }
        for (size_t i = 0; i < draw->texture_count; ++i)
            if (!pin(frame, draw->textures[i], error)) return false;
        if (!pin(frame, draw->shadow_atlas, error)) return false;
        if (draw->light_count != 0) {
            if (draw->light_count > (size_t)PTRDIFF_MAX / sizeof(*draw->lights)) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "scene light array overflow");
                return false;
            }
            size_t bytes = draw->light_count * sizeof(*draw->lights);
            qa_scene_shadow_light *lights = qa_arena_alloc(&frame->storage, bytes,
                _Alignof(qa_scene_shadow_light), error);
            if (lights == NULL) return false;
            memcpy(lights, draw->lights, bytes);
            draw->lights = lights;
        }
        /* Retain separately from commands: shadow extraction and failed surface
         * submissions rewind command_count while keeping frame arena data. */
        if (!qa_scene_frame_geometry(frame, draw->mesh.geometry, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_IMAGE) {
        if (copied.data.image == NULL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene image update requires a version");
            return false;
        }
        if (!pin(frame, copied.data.image, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_IMAGE_REGION) {
        qa_scene_image_region *region = &copied.data.image_region;
        const qa_scene_image *image = region->image;
        qa_scene_rect rect = region->rect;
        if (!scene_image_stream_region_valid(image, rect, error)) return false;
        if (!region->pixels) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image command"); return false;
        }
        if (!pin(frame, image, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_IMAGE_STREAM) {
        if (!copied.data.image_stream || !copied.data.image_stream->image ||
            !copied.data.image_stream->image->streamed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stream admission requires an actual image"); return false;
        }
        if (!pin(frame, copied.data.image_stream->image, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_PARTICLES) {
        if (!pin(frame, copied.data.particles.image, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_TARGET) {
        if (copied.data.target.image != NULL && copied.data.target.image->kind != QA_SCENE_DEPTH32F) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene depth target requires depth pixels");
            return false;
        }
        if (!pin(frame, copied.data.target.image, error)) return false;
    }
    if (copied.kind == QA_SCENE_COMMAND_DRAW && lightmap_fold(frame, &copied.data.draw)) return true;
    frame->commands[frame->command_count++] = copied;
    return !frame->source_pending || qa_material_source_issue_emitted(frame->source_pending, frame, error);
}
bool qa_scene_frame_preblend_gamma(qa_scene_frame *frame, bool enabled, qa_error *error)
{
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_PREBLEND_GAMMA,
        .data.preblend_gamma = {.enabled = enabled}};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_output_domain(qa_scene_frame *frame, qa_scene_rect rect, bool source, qa_error *error)
{
    if (rect.x < 0 || rect.y < 0 || !rect.width || !rect.height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid output color domain region"); return false;
    }
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_OUTPUT_DOMAIN,
        .data.output_domain = {.rect = rect, .source = source}};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_group(qa_scene_frame *frame, size_t first, qa_scene_group_kind kind,
                          const qa_material *material, float priority, uint32_t entity,
                          uint32_t fog, uint32_t dlight, qa_error *error)
{
    if (frame == NULL || first > frame->command_count || kind < QA_SCENE_GROUP_COMPILED ||
        kind > QA_SCENE_GROUP_SEQUENCE || !isfinite(priority) ||
        (kind == QA_SCENE_GROUP_SOURCE && (material == NULL || entity > 1022 || fog > 31 || dlight > 3)) ||
        (frame->group_count != 0 && first < frame->groups[frame->group_count-1].first +
            frame->groups[frame->group_count-1].count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid or overlapping scene group");
        return false;
    }
    if (frame->group_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene group count overflow");
        return false;
    }
    frame->picture_view_end = 0;
    void *data = frame->groups;
    if (!scene_frame_reserve(frame,SCENE_GROUPS,&data, &frame->group_capacity, frame->group_count + 1,
                 sizeof(*frame->groups), error)) return false;
    frame->groups = data;
    size_t ordinal = frame->group_count++;
    frame->groups[ordinal] = (qa_scene_group){.first = first,
        .count = frame->command_count-first, .ordinal = ordinal, .kind = kind,
        .material = material, .priority = priority, .entity = entity, .fog = fog, .dlight = dlight};
    return true;
}

bool qa_scene_frame_draw(qa_scene_frame *frame, const qa_scene_draw *draw, qa_error *error)
{
    if (draw == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene draw is absent");
        return false;
    }
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_DRAW, .data.draw = *draw};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_image(qa_scene_frame *frame, const qa_scene_image *image, qa_error *error)
{
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_IMAGE, .data.image = image};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_image_region(qa_scene_frame *frame, const qa_scene_image *image,
    qa_scene_rect rect, qa_error *error)
{
    if (!scene_image_stream_region_valid(image, rect, error)) return false;
    if (!frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image submission"); return false;
    }
    size_t pitch = (size_t)rect.width * 4;
    uint8_t *pixels = qa_arena_alloc(&frame->storage, pitch * rect.height, 1, error);
    if (!pixels) return false;
    size_t source_pitch = (size_t)image->levels[0].width * 4;
    const uint8_t *source = (const uint8_t *)image->levels[0].pixels +
        (size_t)rect.y * source_pitch + (size_t)rect.x * 4;
    for (size_t y = 0; y < rect.height; ++y) memcpy(pixels + y * pitch, source + y * source_pitch, pitch);
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_IMAGE_REGION,
        .data.image_region = {.image = image, .rect = rect, .pixels = pixels, .writes = image->stream_writes}};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_image_stream(qa_scene_frame *frame, const qa_scene_image *image, qa_error *error)
{
    if (!frame || !image || !image->streamed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stream admission requires frame and image"); return false;
    }
    for (qa_scene_image_stream *row = frame->stream_images; row; row = row->next)
        if (row->image == image) return true;
    qa_scene_image_stream *stream = qa_arena_alloc(&frame->storage, sizeof(*stream),
        _Alignof(qa_scene_image_stream), error);
    if (!stream) return false;
    *stream = (qa_scene_image_stream){.image = image, .initial_writes = image->stream_writes,
        .next = frame->stream_images};
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_IMAGE_STREAM, .data.image_stream = stream};
    if (!qa_scene_frame_emit(frame, &command, error)) return false;
    frame->stream_images = stream;
    return true;
}

bool qa_scene_frame_image_stream_write(qa_scene_frame *frame, qa_scene_image *image,
    qa_scene_rect rect, const uint8_t *pixels, size_t stride, qa_error *error)
{
    if (!scene_image_stream_region_valid(image, rect, error)) return false;
    if (!pixels || stride < (size_t)rect.width * 4 || stride > (size_t)PTRDIFF_MAX / rect.height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image rows"); return false;
    }
    if (!qa_scene_frame_image_stream(frame, image, error)) return false;
    qa_scene_image_stream *stream = frame->stream_images;
    while (stream->image != image) stream = stream->next;
    size_t pitch = (size_t)rect.width * 4;
    qa_scene_image_region *undo = qa_arena_alloc(&frame->storage, sizeof(*undo),
        _Alignof(qa_scene_image_region), error);
    uint8_t *before = qa_arena_alloc(&frame->storage, pitch * rect.height, 1, error);
    if (!undo || !before) return false;
    size_t image_pitch = (size_t)image->levels[0].width * 4;
    const uint8_t *source = (const uint8_t *)image->levels[0].pixels +
        (size_t)rect.y * image_pitch + (size_t)rect.x * 4;
    for (size_t y = 0; y < rect.height; ++y) memcpy(before + y * pitch, source + y * image_pitch, pitch);
    *undo = (qa_scene_image_region){.image = image, .rect = rect, .pixels = before,
        .previous = stream->undo, .writes = image->stream_writes};
    if (!qa_scene_image_stream_write(image, rect, pixels, stride, error)) return false;
    stream->undo = undo;
    return true;
}

void qa_scene_state_default(qa_scene_state *state)
{
    if (state == NULL) return;
    *state = (qa_scene_state){.blend_source = QA_BLEND_ONE, .blend_destination = QA_BLEND_ZERO,
        .depth_test = QA_DEPTH_LEQUAL, .alpha_test = QA_ALPHA_NONE, .cull = QA_CULL_NONE,
        .depth_write = true, .color_write = true, .depth_near = 0, .depth_far = 1,
        .line_width = 1, .stencil_compare_mask = UINT32_MAX, .stencil_write_mask = UINT32_MAX};
}

bool qa_scene_frame_picture(qa_scene_frame *frame, const qa_scene_image *image, qa_scene_rect target,
                           qa_scene_rect rect, qa_vec4 uv, qa_vec4 color, qa_error *error)
{
    qa_scene_rect_f destination = {(float)rect.x, (float)rect.y, (float)rect.width, (float)rect.height};
    return qa_scene_frame_picture_f(frame, image, target, destination, uv, color, error);
}

bool qa_scene_picture_geometry(qa_scene_frame *frame, qa_scene_rect target,
                               qa_scene_rect_f rect, qa_vec4 uv, qa_vec4 color,
                               qa_scene_mesh *out, qa_error *error)
{
    if (frame == NULL || out == NULL || target.width == 0 || target.height == 0 ||
        !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) ||
        !isfinite(rect.x + rect.width) || !isfinite(rect.y + rect.height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "picture geometry requires a frame, output and viewport");
        return false;
    }
    float left = fmaxf(fminf(rect.x, rect.x + rect.width), (float)target.x);
    float top = fmaxf(fminf(rect.y, rect.y + rect.height), (float)target.y);
    float right = fminf(fmaxf(rect.x, rect.x + rect.width), (float)((int64_t)target.x + target.width));
    float bottom = fminf(fmaxf(rect.y, rect.y + rect.height), (float)((int64_t)target.y + target.height));
    if (right <= left || bottom <= top) { *out = (qa_scene_mesh){0}; return true; }
    float s0 = uv.x + (uv.z - uv.x) * (left - rect.x) / rect.width;
    float s1 = uv.x + (uv.z - uv.x) * (right - rect.x) / rect.width;
    float t0 = uv.y + (uv.w - uv.y) * (top - rect.y) / rect.height;
    float t1 = uv.y + (uv.w - uv.y) * (bottom - rect.y) / rect.height;
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices),
                                               _Alignof(qa_scene_vertex), error);
    uint32_t *indices = qa_arena_alloc(&frame->storage, 6 * sizeof(*indices),
                                       _Alignof(uint32_t), error);
    if (vertices == NULL || indices == NULL) return false;
    const uint32_t pattern[6] = {0, 1, 2, 0, 2, 3};
    memcpy(indices, pattern, sizeof(pattern));
    vertices[0] = (qa_scene_vertex){.position = {left, top, 0}, .texcoord = {s0, t0}, .color = color};
    vertices[1] = (qa_scene_vertex){.position = {right, top, 0}, .texcoord = {s1, t0}, .color = color};
    vertices[2] = (qa_scene_vertex){.position = {right, bottom, 0}, .texcoord = {s1, t1}, .color = color};
    vertices[3] = (qa_scene_vertex){.position = {left, bottom, 0}, .texcoord = {s0, t1}, .color = color};
    for (size_t i = 0; i < 4; ++i) vertices[i].normal.z = 1;
    *out = (qa_scene_mesh){.vertices = vertices, .indices = indices, .vertex_count = 4,
        .index_count = 6, .primitive = QA_SCENE_TRIANGLES,
        .bounds = {{left, top, 0}, {right, bottom, 0}}};
    return true;
}

static bool picture_view_current(const qa_scene_frame *frame, qa_scene_rect target)
{
    if (frame->source_pending || !frame->picture_view_end ||
        frame->picture_view_end != frame->command_count ||
        frame->picture_view_index >= frame->command_count) return false;
    const qa_scene_command *command = &frame->commands[frame->picture_view_index];
    if (command->kind != QA_SCENE_COMMAND_VIEW) return false;
    const qa_scene_view *view = &command->data.view;
    return view->viewport.x == target.x && view->viewport.y == target.y &&
        view->viewport.width == target.width && view->viewport.height == target.height;
}

bool qa_scene_frame_picture_f(qa_scene_frame *frame, const qa_scene_image *image, qa_scene_rect target,
                              qa_scene_rect_f rect, qa_vec4 uv, qa_vec4 color, qa_error *error)
{
    if (!image) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "picture requires an image"); return false;
    }
    qa_scene_mesh mesh;
    if (!qa_scene_picture_geometry(frame, target, rect, uv, color, &mesh, error)) return false;
    if (!mesh.vertex_count) return true;
    qa_scene_vertex *vertices = (qa_scene_vertex *)mesh.vertices;
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        vertices[i].normal = qa_v3(0, 0, 0);
        vertices[i].position.x = (vertices[i].position.x - (float)target.x) / (float)target.width * 2 - 1;
        vertices[i].position.y = 1 - (vertices[i].position.y - (float)target.y) / (float)target.height * 2;
    }
    mesh.bounds = (qa_bounds){{vertices[0].position.x, vertices[2].position.y, 0},
        {vertices[2].position.x, vertices[0].position.y, 0}};
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = {.viewport = target}};
    qa_scene_draw draw = {.mesh = mesh, .textures = {image, NULL}, .texture_count = 1};
    qa_scene_matrix_identity(&draw.model);
    qa_scene_matrix_identity(&draw.mvp);
    qa_scene_state_default(&draw.state);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = QA_DEPTH_ALWAYS;
    draw.state.depth_write = false;
    bool ordinary = frame->source_pending == NULL;
    size_t view_index = frame->picture_view_index;
    if (!picture_view_current(frame, target)) {
        if (!qa_scene_frame_emit(frame, &view, error)) return false;
        view_index = frame->command_count - 1;
    }
    if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    if (ordinary && !frame->source_pending) {
        frame->picture_view_index = view_index;
        frame->picture_view_end = frame->command_count;
    }
    return true;
}

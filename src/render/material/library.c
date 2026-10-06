#include "library_internal.h"
#include "../scene/resources_internal.h"
#include "qa/material_library_save.h"
#include "qa/material_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const qa_material *qa_material_library_record_at(const qa_material_library *library, size_t index)
{ return library && index < library->count ? &library->ordered[index]->material : NULL; }
size_t qa_material_library_record_count(const qa_material_library *library)
{ return library ? library->count : 0; }
size_t qa_material_library_video_receipt_count(const qa_material_library *library, size_t record)
{
    if (!library || record >= library->count) return 0;
    size_t count = 0;
    for (const qa_material_video_receipt *receipt = library->ordered[record]->videos;
        receipt; receipt = receipt->next) ++count;
    return count;
}
bool qa_material_library_video_receipt_read(const qa_material_library *library,
    size_t record, size_t index, const char **source, const qa_scene_image **image)
{
    if (!library || record >= library->count || !source || !image) return false;
    const qa_material_video_receipt *receipt = library->ordered[record]->videos;
    while (receipt && index) { receipt = receipt->next; --index; }
    if (!receipt) return false;
    *source = receipt->source; *image = receipt->image; return true;
}
bool qa_material_library_record_read(const qa_material_library *library, size_t index, qa_material_library_record_view *out)
{
    if (!library || !out || index >= library->count) return false;
    const qa_material_record *r = library->ordered[index];
    *out = (qa_material_library_record_view){&r->material, &r->options, r->kind, r->world_identity,
        r->lightmap_index, r->base_name, r->base_image};
    return true;
}
qa_scene_resources *qa_material_library_resource_owner(const qa_material_library *library)
{ return library ? library->resources : NULL; }
const qa_material_order *qa_material_library_order_owner(const qa_material_library *library)
{ return library ? library->order : NULL; }
bool qa_material_library_order_ready(const qa_material_library *library)
{
    if (!library || !library->order) return false;
    for (size_t i = 0; i < library->count; ++i)
        if (!qa_material_order_has_record(library->order, &library->ordered[i]->material)) return false;
    return true;
}
static bool mutation_begin(qa_material_library *library, qa_error *error)
{
    if (!qa_material_library_idle(library)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material library is retained by another operation");
        return false;
    }
    library->mutating = true;
    return true;
}

static bool mutation_end(qa_material_library *library, bool ok)
{
    library->mutating = false;
    return ok;
}

bool qa_material_library_idle(const qa_material_library *library)
{
    return library && !library->capture_depth && !library->mutating && !library->image_policy && !library->policy_sealed &&
        (!library->order || qa_material_order_idle(library->order));
}

bool qa_material_library_capture_begin(const qa_material_library *borrowed, qa_error *error)
{
    qa_material_library *library = (qa_material_library *)borrowed;
    if (!library || !library->resources || !library->catalog_ready || library->mutating || library->image_policy ||
        library->policy_source || library->capture_depth == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material capture requires an idle retained owner");
        return false;
    }
    if (library->order && !qa_material_order_capture_begin(library->order, error)) return false;
    ++library->capture_depth;
    return true;
}

void qa_material_library_capture_end(const qa_material_library *borrowed)
{
    qa_material_library *library = (qa_material_library *)borrowed;
    if (library && library->capture_depth) {
        --library->capture_depth;
        if (library->order) qa_material_order_capture_end(library->order);
    }
}

qa_material_library *qa_material_library_create_detached(qa_scene_resources *resources, qa_error *error)
{
    if (!resources) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached material library requires its actual resource owner");
        return NULL;
    }
    qa_material_library *library = calloc(1, sizeof(*library));
    if (!library) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating detached material library");
        return NULL;
    }
    library->resources = resources;
    library->references = 1;
    if (!qa_scene_resources_retain(resources, error)) { free(library); return NULL; }
    return library;
}

static bool catalog_add(qa_material_library *library, qa_bytes bytes, qa_resource *resource,
    const qa_scene_image_options *scope, qa_error *error)
{
    qa_material_catalog_source *source = calloc(1, sizeof(*source));
    if (!source) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining shader catalog source");
        return false;
    }
    if (scope) {
        source->dependency_scope = true; source->dependency_family = scope->family;
        source->dependency_has_palette = scope->palette_rgb.size != 0;
        if (source->dependency_has_palette) memcpy(source->dependency_palette, scope->palette_rgb.data, 768);
    }
    if (resource) {
        source->resource = resource;
        qa_resource_retain(resource);
        source->bytes = qa_resource_bytes(resource);
    } else {
        source->owned_bytes = bytes.size ? malloc(bytes.size) : NULL;
        if (bytes.size && !source->owned_bytes) {
            free(source);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining inline shader source bytes");
            return false;
        }
        if (bytes.size) memcpy(source->owned_bytes, bytes.data, bytes.size);
        source->bytes = (qa_bytes){source->owned_bytes, bytes.size};
    }
    if (library->catalog_tail) library->catalog_tail->next = source;
    else library->catalog_sources = source;
    library->catalog_tail = source;
    library->catalog_current = source;
    bool ok = qa_material_script_catalog(library, source->bytes, error);
    library->catalog_current = NULL;
    return ok;
}

static qa_scene_image_options script_options(const qa_material_script *script,
    const qa_scene_image_options *original)
{
    qa_scene_image_options options = *original;
    if (script && script->source->dependency_scope) {
        const qa_material_catalog_source *source = script->source;
        options.family = source->dependency_family;
        options.palette_rgb = source->dependency_has_palette ?
            (qa_bytes){source->dependency_palette, sizeof(source->dependency_palette)} : (qa_bytes){0};
        /* Foreign authored pixels keep their decoder family. The actual
         * Source recipient maps their completed immutable image at draw. */
        if (options.family != QA_SCENE_Q3) options.source_q3 = false;
    }
    return options;
}

char *qa_material_string(const char *value, qa_error *error)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material string");
        return NULL;
    }
    memcpy(copy, value, length + 1);
    return copy;
}

enum { MATERIAL_NAME_BYTES = 1024 };

static size_t material_name_write(const char *value, char out[MATERIAL_NAME_BYTES],
    qa_error *error)
{
    if (!value || !*value) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material name is empty");
        return 0;
    }
    /* COM_StripExtension stops at the first dot. Q_stricmp does not fold
     * separators, even though the source hash function does. */
    size_t length = strcspn(value, ".");
    if (!length || length >= MATERIAL_NAME_BYTES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, length, "Invalid material name length");
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        out[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    out[length] = 0;
    return length;
}

char *qa_material_name(const char *value, qa_error *error)
{
    char key[MATERIAL_NAME_BYTES];
    size_t length = material_name_write(value, key, error);
    if (!length) return NULL;
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material name");
        return NULL;
    }
    memcpy(copy, key, length + 1);
    return copy;
}

unsigned qa_material_hash(const char *name)
{
    uint32_t hash = 0;
    for (size_t i = 0; name[i] && name[i] != '.'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '\\') c = '/';
        int32_t signed_c = c < 128 ? c : (int32_t)c - 256;
        hash += (uint32_t)signed_c * (uint32_t)(i + 119);
    }
    return (hash ^ (hash >> 10) ^ (hash >> 20)) & (QA_MATERIAL_BUCKETS - 1);
}

static qa_scene_image_options default_options(void)
{
    return (qa_scene_image_options){ .family = QA_SCENE_Q3,
        .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR,
        .mipmap = true, .transparent_index = -1 };
}

bool qa_material_library_script_read(const qa_material_library *library, const char *name,
    qa_material_script_view *out)
{
    if (!library || !library->catalog_ready || !name || !out) return false;
    char key[MATERIAL_NAME_BYTES];
    if (!material_name_write(name, key, NULL)) return false;
    for (const qa_material_script *script = library->scripts[qa_material_hash(key)]; script; script = script->next) {
        if (strcmp(script->name, key)) continue;
        if (!script->source || script->source_offset > script->source->bytes.size ||
            script->size > script->source->bytes.size - script->source_offset ||
            script->name_offset > script->source->bytes.size ||
            script->name_size > script->source->bytes.size - script->name_offset) return false;
        *out = (qa_material_script_view){.name = script->name,
            .body = {script->text, script->size}, .catalog = script->source->bytes,
            .resource = script->source->resource, .source_offset = script->source_offset,
            .name_offset = script->name_offset, .name_size = script->name_size,
            .dependency_scope = script->source->dependency_scope,
            .dependency_family = script->source->dependency_family,
            .dependency_palette = script->source->dependency_has_palette ?
                (qa_bytes){script->source->dependency_palette, sizeof(script->source->dependency_palette)} : (qa_bytes){0}};
        return true;
    }
    return false;
}

void qa_material_stage_init(qa_material_stage *stage)
{
    memset(stage, 0, sizeof(*stage));
    stage->state = (qa_scene_state){ .blend_source = QA_BLEND_ONE,
        .blend_destination = QA_BLEND_ZERO, .depth_test = QA_DEPTH_LEQUAL,
        .depth_write = true, .color_write = true, .cull = QA_CULL_FRONT,
        .depth_far = 1, .line_width = 1, .stencil_compare_mask = UINT32_MAX,
        .stencil_write_mask = UINT32_MAX };
    stage->rgb = QA_COLOR_BAD;
    stage->alpha = QA_COLOR_IDENTITY;
    stage->rgb_wave.kind = QA_WAVE_NONE;
    stage->alpha_wave.kind = QA_WAVE_NONE;
    stage->tcgen = QA_TC_BAD;
    stage->fog_adjustment = QA_FOG_NO_EFFECT;
}

void qa_material_stage_clear(qa_material_stage *stage)
{
    for (size_t i = 0; stage->images && i < QA_MATERIAL_MAX_ANIMATION; ++i) {
        qa_scene_image_release(stage->images[i]);
        free(stage->image_names[i]);
    }
    free(stage->images);
    free(stage->image_names);
    free(stage->video_name);
    free(stage->tcmods);
    memset(stage, 0, sizeof(*stage));
}

void qa_material_clear(qa_material *material)
{
    for (size_t i = 0; i < material->stage_count; ++i)
        qa_material_stage_clear(&material->stages[i]);
    for (size_t i = 0; i < 6; ++i) {
        qa_scene_image_release(material->sky_outer_images[i]);
        qa_scene_image_release(material->sky_inner_images[i]);
    }
    free(material->name);
    free(material->sky_outer);
    free(material->sky_inner);
    free(material->stages);
    free(material->deforms);
    memset(material, 0, sizeof(*material));
}

bool qa_material_stage_image(qa_material_stage *stage, size_t index,
                             const char *name, qa_scene_image *image, qa_error *error)
{
    if (index >= QA_MATERIAL_MAX_ANIMATION) {
        qa_error_set(error, QA_ERROR_ARGUMENT, index, "Material animation frame exceeds source limit");
        return false;
    }
    char *copy = qa_material_string(name, error);
    if (!copy) return false;
    if (!stage->images) {
        qa_scene_image **images = calloc(QA_MATERIAL_MAX_ANIMATION, sizeof(*images));
        char **names = calloc(QA_MATERIAL_MAX_ANIMATION, sizeof(*names));
        if (!images || !names) {
            free(images); free(names); free(copy);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material image bundle");
            return false;
        }
        stage->images = images;
        stage->image_names = names;
    }
    qa_scene_image_release(stage->images[index]);
    free(stage->image_names[index]);
    stage->images[index] = image;
    stage->image_names[index] = copy;
    if (stage->image_count <= index) stage->image_count = index + 1;
    return true;
}

static bool blended(const qa_material_stage *stage)
{
    return stage->invalid_blend || stage->state.blend_source != QA_BLEND_ONE ||
           stage->state.blend_destination != QA_BLEND_ZERO;
}

static qa_scene_fog_effect fog_adjustment(const qa_material_stage *stage)
{
    if ((stage->state.blend_source == QA_BLEND_ONE && stage->state.blend_destination == QA_BLEND_ONE) ||
        (stage->state.blend_source == QA_BLEND_ZERO && stage->state.blend_destination == QA_BLEND_ONE_MINUS_SRC_COLOR))
        return QA_FOG_RGB;
    if (stage->state.blend_source == QA_BLEND_SRC_ALPHA && stage->state.blend_destination == QA_BLEND_ONE_MINUS_SRC_ALPHA)
        return QA_FOG_ALPHA;
    if (stage->state.blend_source == QA_BLEND_ONE && stage->state.blend_destination == QA_BLEND_ONE_MINUS_SRC_ALPHA)
        return QA_FOG_RGBA;
    return QA_FOG_NO_EFFECT;
}

static bool stage_active(const qa_material_stage *stage)
{
    return stage->image_count && stage->images && stage->images[0];
}

static void truncate_stages(qa_material *material, size_t count)
{
    for (size_t i = count; i < material->stage_count; ++i)
        qa_material_stage_clear(&material->stages[i]);
    material->stage_count = count;
}

static void vertex_lighting_collapse(qa_material *material, float sort, int32_t lightmap_index)
{
    qa_material_stage *first = &material->stages[0];
    if (sort == 3) {
        size_t best = 0;
        int best_rank = -999999;
        for (size_t i = 0; i < material->stage_count; ++i) {
            qa_material_stage *stage = &material->stages[i];
            if (!stage_active(stage)) break;
            int rank = stage->is_lightmap ? -100 : 0;
            if (stage->tcgen != QA_TC_TEXTURE) rank -= 5;
            if (stage->tcmod_count) rank -= 5;
            if (stage->rgb != QA_COLOR_IDENTITY && stage->rgb != QA_COLOR_IDENTITY_LIGHTING) rank -= 3;
            if (rank > best_rank) { best_rank = rank; best = i; }
        }
        if (best) {
            /* Only the selected texture bundle moves; source stage-zero color,
             * depth, alpha-test and fog fields still own this pass. */
            qa_material_stage old = *first;
            *first = material->stages[best];
            material->stages[best] = old;
            first->state = old.state;
            first->constant = old.constant;
            first->rgb_wave = old.rgb_wave;
            first->alpha_wave = old.alpha_wave;
            first->portal_range = old.portal_range;
            first->detail = old.detail;
            first->fog_adjustment = old.fog_adjustment;
        }
        first->state.blend_source = QA_BLEND_ONE;
        first->state.blend_destination = QA_BLEND_ZERO;
        first->state.depth_write = true;
        first->invalid_blend = false;
        first->rgb = lightmap_index == -1 ? QA_COLOR_LIGHTING_DIFFUSE : QA_COLOR_EXACT_VERTEX;
        first->alpha = QA_COLOR_SKIP;
    } else {
        qa_material_stage *second = &material->stages[1];
        if (first->is_lightmap) {
            qa_material_stage old = *first;
            *first = *second;
            *second = old;
            /* The source copied stage one and still compares against stage
             * one. Keep its value rather than the swapped retirement slot. */
            if (first->rgb == QA_COLOR_ONE_MINUS_ENTITY) first->rgb = QA_COLOR_IDENTITY_LIGHTING;
        } else if (first->rgb == QA_COLOR_ONE_MINUS_ENTITY || second->rgb == QA_COLOR_ONE_MINUS_ENTITY) {
            first->rgb = QA_COLOR_IDENTITY_LIGHTING;
        } else if (first->rgb == QA_COLOR_WAVE && second->rgb == QA_COLOR_WAVE &&
            ((first->rgb_wave.kind == QA_WAVE_SAWTOOTH && second->rgb_wave.kind == QA_WAVE_INVERSE_SAWTOOTH) ||
             (first->rgb_wave.kind == QA_WAVE_INVERSE_SAWTOOTH && second->rgb_wave.kind == QA_WAVE_SAWTOOTH))) {
            first->rgb = QA_COLOR_IDENTITY_LIGHTING;
        }
    }
    truncate_stages(material, 1);
}

void qa_material_finish(qa_material *material, int32_t lightmap_index, bool source_profile)
{
    material->lightmap_index = lightmap_index;
    float sort = material->sort;
    if (material->sky) sort = 2;
    if (material->polygon_offset && sort == 0) sort = 4;
    bool has_lightmap = false;
    for (size_t i = 0; i < material->stage_count; ++i) {
        qa_material_stage *stage = &material->stages[i];
        stage->state.cull = material->cull;
        stage->state.polygon_offset = material->polygon_offset;
        if (material->polygon_offset) {
            stage->state.offset_factor = -1;
            stage->state.offset_units = -2;
        }
        stage->portal_range = material->portal_range;
    }
    for (size_t i = 0; i < material->stage_count; ++i) {
        qa_material_stage *stage = &material->stages[i];
        if (!stage_active(stage)) continue;
        if (stage->detail && !material->profile.detail_textures) {
            if (i < 7) {
                if (i + 1 < material->stage_count) {
                    qa_material_stage old = *stage;
                    *stage = material->stages[i + 1];
                    material->stages[i + 1] = old;
                    truncate_stages(material, i + 1);
                } else truncate_stages(material, i);
            }
            break;
        }
        if (stage->tcgen == QA_TC_BAD) stage->tcgen = stage->is_lightmap ? QA_TC_LIGHTMAP : QA_TC_TEXTURE;
        if (stage->is_lightmap) has_lightmap = true;
        if (blended(stage) && blended(&material->stages[0])) {
            stage->fog_adjustment = fog_adjustment(stage);
            if (sort == 0) sort = stage->state.depth_write ? 5 : 9;
        }
    }
    if (sort == 0) sort = 3;
    if (material->stage_count > 1 && ((material->profile.vertex_lighting && !material->profile.ui_fullscreen) ||
                                     material->profile.permedia2))
    {
        vertex_lighting_collapse(material, sort, lightmap_index);
        has_lightmap = false;
    }
    if (source_profile && lightmap_index >= 0 && !has_lightmap) material->lightmap_index = -1;
    if (!material->stage_count) sort = 7;
    material->sort = sort;
}

static bool same_bytes(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

static bool same_options(const qa_scene_image_options *a, const qa_scene_image_options *b)
{
    return a->family == b->family && a->wrap == b->wrap && a->filter == b->filter &&
        a->usage == b->usage &&
        a->mipmap == b->mipmap && a->transparent == b->transparent &&
        a->fullbright_only == b->fullbright_only && a->transparent_index == b->transparent_index &&
        a->source_q3 == b->source_q3 &&
        same_bytes(a->palette_rgb, b->palette_rgb) && same_bytes(a->translation, b->translation);
}

static bool copy_options(qa_material_record *record, const qa_scene_image_options *options,
                          qa_error *error)
{
    record->options = *options;
    if (options->palette_rgb.size) {
        record->palette = malloc(options->palette_rgb.size);
        if (!record->palette) goto memory;
        memcpy(record->palette, options->palette_rgb.data, options->palette_rgb.size);
        record->options.palette_rgb.data = record->palette;
    }
    if (options->translation.size) {
        record->translation = malloc(options->translation.size);
        if (!record->translation) goto memory;
        memcpy(record->translation, options->translation.data, options->translation.size);
        record->options.translation.data = record->translation;
    }
    return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining material palette options");
    return false;
}

void qa_material_videos_clear(qa_material_record *record)
{
    while (record->videos) {
        qa_material_video_receipt *video = record->videos;
        record->videos = video->next;
        qa_scene_image_release(video->image); free(video->source); free(video);
    }
}
static void record_free(qa_material_record *record)
{
    qa_material_order_remove(record->material.order_entry);
    qa_material_clear(&record->material);
    qa_material_videos_clear(record);
    free(record->base_name);
    qa_scene_image_release(record->base_image);
    free(record->palette);
    free(record->translation);
    free(record);
}

static bool publish(qa_material_library *library, qa_material_record *record, qa_error *error)
{
    if (library->count == QA_MATERIAL_MAX_REGISTERED) {
        qa_error_set(error, QA_ERROR_FORMAT, library->count, "Source material registration limit reached");
        return false;
    }
    if (library->count == library->capacity) {
        size_t capacity = library->capacity ? library->capacity * 2 : 64;
        qa_material_record **records = realloc(library->ordered, capacity * sizeof(*records));
        if (!records) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Growing material registration order");
            return false;
        }
        library->ordered = records;
        library->capacity = capacity;
    }
    qa_material *material = &record->material;
    material->library = library;
    material->registration = (uint32_t)library->count;
    material->identity = qa_scene_identity();
    material->revision = 1;
    material->fog_image = library->fog_image;
    material->dlight_image = library->dlight_image;
    if (!qa_material_order_publish(material->order_entry, error)) return false;
    size_t index = library->count;
    while (index && library->ordered[index - 1]->material.sort > material->sort) {
        library->ordered[index] = library->ordered[index - 1];
        library->ordered[index]->material.sorted_index = (uint32_t)index;
        --index;
    }
    library->ordered[index] = record;
    material->sorted_index = (uint32_t)index;
    ++library->count;
    unsigned bucket = qa_material_hash(material->name);
    record->next = library->records[bucket];
    library->records[bucket] = record;
    return true;
}

static bool bind_borrowed(qa_material_stage *stage, const char *name,
                           const qa_scene_image *image, qa_error *error)
{
    qa_scene_image_retain(image);
    if (qa_material_stage_image(stage, 0, name, (qa_scene_image *)image, error)) return true;
    qa_scene_image_release(image);
    return false;
}

bool qa_material_sample_image(qa_material_library *library, const qa_scene_image *source,
                               bool mipmap, qa_scene_wrap wrap, qa_scene_image **out, qa_error *error)
{
    return qa_scene_image_sample(library->resources, source, mipmap, wrap, out, error);
}

typedef bool (*material_image_mapper)(void *, const qa_scene_image *, qa_scene_image **, qa_error *);
static bool material_copy_images(const qa_material *source, qa_material *out,
    material_image_mapper map, void *context, qa_error *error)
{
    *out = *source;
    out->name = NULL; out->sky_outer = NULL; out->sky_inner = NULL;
    out->stages = NULL; out->stage_count = 0; out->deforms = NULL; out->deform_count = 0;
    memset(out->sky_outer_images, 0, sizeof(out->sky_outer_images));
    memset(out->sky_inner_images, 0, sizeof(out->sky_inner_images));
    out->name = qa_material_string(source->name, error);
    if (!out->name) goto failed;
    if (source->sky_outer && !(out->sky_outer = qa_material_string(source->sky_outer, error))) goto failed;
    if (source->sky_inner && !(out->sky_inner = qa_material_string(source->sky_inner, error))) goto failed;
    for (unsigned face = 0; face < 6; ++face) {
        qa_scene_image *image = NULL;
        if (source->sky_outer_images[face]) {
            if (!map(context, source->sky_outer_images[face], &image, error)) goto failed;
            out->sky_outer_images[face] = image;
        }
        image = NULL;
        if (source->sky_inner_images[face]) {
            if (!map(context, source->sky_inner_images[face], &image, error)) goto failed;
            out->sky_inner_images[face] = image;
        }
    }
    if (source->deform_count) {
        out->deforms = malloc(source->deform_count * sizeof(*out->deforms));
        if (!out->deforms) goto memory;
        memcpy(out->deforms, source->deforms, source->deform_count * sizeof(*out->deforms));
        out->deform_count = source->deform_count;
    }
    out->stages = source->stage_count ? calloc(source->stage_count, sizeof(*out->stages)) : NULL;
    if (source->stage_count && !out->stages) goto memory;
    for (size_t i = 0; i < source->stage_count; ++i) {
        const qa_material_stage *original = source->stages + i;
        qa_material_stage *stage = out->stages + i;
        *stage = *original;
        stage->images = NULL; stage->image_names = NULL; stage->image_count = 0;
        stage->tcmods = NULL; stage->tcmod_count = 0; stage->video_name = NULL;
        ++out->stage_count;
        if (original->video_name && !(stage->video_name = qa_material_string(original->video_name, error))) goto failed;
        if (original->tcmod_count) {
            stage->tcmods = malloc(original->tcmod_count * sizeof(*stage->tcmods));
            if (!stage->tcmods) goto memory;
            memcpy(stage->tcmods, original->tcmods, original->tcmod_count * sizeof(*stage->tcmods));
            stage->tcmod_count = original->tcmod_count;
        }
        for (size_t frame = 0; frame < original->image_count; ++frame) {
            qa_scene_image *image = NULL;
            if (!map(context, original->images[frame], &image, error)) goto failed;
            if (!qa_material_stage_image(stage, frame, original->image_names[frame], image, error)) {
                qa_scene_image_release(image); goto failed;
            }
        }
    }
    return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Cloning actual compiled material inputs");
failed:
    qa_material_clear(out); return false;
}
typedef struct source_variant_mapper {
    qa_scene_resources *resources;
    qa_q3_image_upload_options upload;
} source_variant_mapper;
static bool source_variant_image(void *opaque, const qa_scene_image *source,
    qa_scene_image **out, qa_error *error)
{
    source_variant_mapper *mapper = opaque;
    qa_q3_image_upload_options upload = mapper->upload;
    qa_scene_image_request request;
    if (qa_scene_image_request_read(mapper->resources, source, &request))
        upload.mipmap = upload.mipmap && request.options.mipmap;
    return qa_scene_image_source_q3_variant(mapper->resources, source, &upload, out, error);
}
bool qa_material_source_q3_variant(qa_material_library *library, const qa_material *source,
    const qa_q3_image_upload_options *upload, const qa_material **out, qa_error *error)
{
    if (!out || *out || !source || source->library != library ||
        !qa_q3_image_upload_options_valid(upload, error) || !mutation_begin(library, error)) return false;
    qa_material_record *parent = NULL;
    for (size_t i = 0; i < library->count; ++i)
        if (&library->ordered[i]->material == source) parent = library->ordered[i];
    if (!parent) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Recipient material lost its actual registered parent");
        return mutation_end(library, false);
    }
    if (parent->options.source_q3) { *out = source; return mutation_end(library, true); }
    for (size_t i = 0; i < library->count; ++i) {
        qa_material_record *record = library->ordered[i];
        if (record->source_variant_parent == parent && record->source_variant_revision == source->revision &&
            qa_q3_image_upload_options_equal(&record->source_variant_upload, upload)) {
            *out = &record->material; return mutation_end(library, true);
        }
    }
    qa_material_record *record = calloc(1, sizeof(*record));
    if (!record) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Registering actual Source recipient material");
        return mutation_end(library, false);
    }
    record->kind = parent->kind; record->world_identity = parent->world_identity;
    record->lightmap_index = parent->lightmap_index;
    record->source_variant_parent = parent; record->source_variant_revision = source->revision;
    record->source_variant_upload = *upload;
    source_variant_mapper mapper = {.resources = library->resources, .upload = *upload};
    if (source->no_mipmaps) mapper.upload.mipmap = false;
    if (source->no_picmip) mapper.upload.allow_picmip = false;
    bool ok = copy_options(record, &parent->options, error) &&
        material_copy_images(source, &record->material, source_variant_image, &mapper, error);
    record->material.order_entry = NULL;
    record->material.remapped = NULL;
    if (ok) ok = qa_material_order_reserve(library->order, &record->material, &record->material.order_entry, error) &&
        publish(library, record, error);
    if (!ok) record_free(record);
    else *out = &record->material;
    return mutation_end(library, ok);
}

static bool implicit(qa_material_library *library, qa_material_record *record,
                      const char *image_name, qa_error *error)
{
    qa_material *material = &record->material;
    qa_material_registration_kind kind = record->kind;
    qa_scene_image *image = NULL;
    bool internal = kind == QA_MATERIAL_DEFAULT || kind == QA_MATERIAL_STENCIL_SHADOW;
    const qa_material_generated *generated = library->generated;
    while (generated != NULL && strcmp(generated->name, material->name) != 0) generated = generated->next;
    qa_scene_image_options options = record->options;
    if (kind == QA_MATERIAL_PICTURE) options.wrap = QA_SCENE_CLAMP;
    if (generated != NULL && !internal) {
        if (!qa_material_sample_image(library, generated->image, false, QA_SCENE_CLAMP, &image, error)) return false;
        kind = QA_MATERIAL_PICTURE;
    } else if (internal) {
        image = (qa_scene_image *)(library->source_profile ? qa_scene_source_q3_missing(library->resources) :
            qa_scene_missing(library->resources));
        if (!image) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source internal shader has no admitted renderer image"); return false;
        }
        qa_scene_image_retain(image);
    } else {
        qa_error load_error = {0};
        if (!qa_scene_image_load(library->resources, image_name, &options, &image, &load_error)) {
            if (load_error.code == QA_ERROR_MEMORY || (library->source_profile && load_error.code == QA_ERROR_ARGUMENT)) {
                if (error) *error = load_error;
                return false;
            }
            material->default_shader = true;
            image = (qa_scene_image *)(options.source_q3 ? qa_scene_source_q3_missing(library->resources) :
                qa_scene_missing(library->resources));
            if (!image) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source default shader has no admitted renderer image"); return false;
            }
            qa_scene_image_retain(image);
            kind = QA_MATERIAL_DEFAULT;
        }
    }
    size_t count = kind == QA_MATERIAL_LIGHTMAP || kind == QA_MATERIAL_WHITE ? 2 : 1;
    material->stages = calloc(count, sizeof(*material->stages));
    if (!material->stages) {
        qa_scene_image_release(image);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating implicit material stages");
        return false;
    }
    material->stage_count = count;
    for (size_t i = 0; i < count; ++i) qa_material_stage_init(&material->stages[i]);
    qa_material_stage *base = &material->stages[count - 1];
    base->tcgen = QA_TC_TEXTURE;
    if (!qa_material_stage_image(base, 0, image_name, image, error)) {
        qa_scene_image_release(image);
        return false;
    }
    base->rgb = QA_COLOR_LIGHTING_DIFFUSE;
    if (kind == QA_MATERIAL_DEFAULT || kind == QA_MATERIAL_STENCIL_SHADOW)
        base->rgb = QA_COLOR_BAD;
    else if (kind == QA_MATERIAL_VERTEX) {
        base->rgb = QA_COLOR_EXACT_VERTEX;
        base->alpha = QA_COLOR_SKIP;
    } else if (kind == QA_MATERIAL_PICTURE) {
        base->rgb = QA_COLOR_VERTEX;
        base->alpha = QA_COLOR_VERTEX;
        base->state.blend_source = QA_BLEND_SRC_ALPHA;
        base->state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
        base->state.depth_test = library->source_profile ? QA_DEPTH_DISABLED : QA_DEPTH_ALWAYS;
        base->state.depth_write = false;
        base->clamp = true;
    }
    if (count == 2) {
        qa_material_stage *first = &material->stages[0];
        first->lightmap = kind == QA_MATERIAL_LIGHTMAP;
        first->is_lightmap = first->lightmap;
        first->tcgen = first->lightmap ? QA_TC_LIGHTMAP : QA_TC_TEXTURE;
        first->rgb = first->lightmap ? QA_COLOR_IDENTITY : QA_COLOR_IDENTITY_LIGHTING;
        if (!bind_borrowed(first, first->lightmap ? "$lightmap" : "$whiteimage",
                           options.source_q3 ? qa_scene_source_q3_white(library->resources) : qa_scene_white(library->resources), error)) return false;
        base->rgb = QA_COLOR_IDENTITY;
        base->state.blend_source = QA_BLEND_DST_COLOR;
        base->state.blend_destination = QA_BLEND_ZERO;
        base->state.depth_write = false;
    }
    /* Q1/Q2 model lighting is prepared by their scene builders. */
    if (material->family != QA_SCENE_Q3 && kind == QA_MATERIAL_DYNAMIC)
        base->rgb = QA_COLOR_EXACT_VERTEX;
    if (kind == QA_MATERIAL_STENCIL_SHADOW) material->sort = 14;
    qa_material_finish(material, kind == QA_MATERIAL_PICTURE ? -4 : record->lightmap_index, library->source_profile);
    return true;
}

typedef struct material_policy_record {
    qa_material_record *record;
    qa_material material;
    const qa_scene_image *base;
    qa_scene_image_options options;
    qa_q3_image_upload_options variant_upload;
    qa_material_video_receipt *videos;
} material_policy_record;
struct qa_scene_material_image_policy {
    qa_material_library *owner;
    qa_scene_resource_policy *resources;
    material_policy_record *records;
    qa_material_generated *generated;
    qa_scene_image *fog_image, *dlight_image;
    size_t count;
    qa_material_library *destination;
    qa_material_order_image_policy *order;
    qa_material_record **ordered;
    size_t capacity, added;
    qa_material_profile source_profile, profile;
    qa_material_source_upload_fn source_upload;
    void *source_upload_context;
    qa_material_source_ui_fullscreen_fn source_ui_fullscreen;
    void *source_ui_context;
    bool replace_profile, source_profile_bound;
    bool sealed, published;
};
static bool profile_equal(const qa_material_profile *a, const qa_material_profile *b)
{
    return a->detail_textures == b->detail_textures && a->vertex_lighting == b->vertex_lighting &&
        a->ui_fullscreen == b->ui_fullscreen && a->permedia2 == b->permedia2 &&
        a->multitexture == b->multitexture && a->texture_env_add == b->texture_env_add &&
        a->ignore_fast_path == b->ignore_fast_path;
}
static bool material_policy_current(const qa_scene_material_image_policy *ticket)
{
    return ticket && ticket->owner->image_policy == ticket && !ticket->owner->mutating &&
        !ticket->owner->capture_depth && ticket->owner->count == ticket->count + (ticket->published ? ticket->added : 0) &&
        profile_equal(&ticket->owner->profile, ticket->published ? &ticket->profile : &ticket->source_profile) &&
        ticket->owner->source_profile == ticket->source_profile_bound &&
        ticket->owner->source_upload == ticket->source_upload &&
        ticket->owner->source_upload_context == ticket->source_upload_context &&
        ticket->owner->source_ui_fullscreen == ticket->source_ui_fullscreen &&
        ticket->owner->source_ui_context == ticket->source_ui_context &&
        ticket->owner->resources == qa_scene_resource_policy_source(ticket->resources) &&
        (!ticket->sealed || (ticket->destination && ticket->destination->policy_sealed &&
            ticket->destination->count == (ticket->published ? 0 : ticket->added))) &&
        (!ticket->owner->order || qa_material_order_idle(ticket->owner->order) ||
            qa_material_order_image_policy_associated(ticket->owner->order));
}
static void material_policy_dispose(qa_scene_material_image_policy *ticket)
{
    if (ticket->destination) {
        /* Catalog, profile and generated names are borrowed from the held
         * source. Only newly registered records and their order are owned. */
        memset(ticket->destination->scripts, 0, sizeof(ticket->destination->scripts));
        ticket->destination->remaps = NULL; ticket->destination->generated = NULL;
        ticket->destination->fog_image = NULL; ticket->destination->dlight_image = NULL;
        ticket->destination->policy_sealed = false;
        qa_material_library_destroy(ticket->destination);
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        qa_material_clear(&ticket->records[i].material);
        qa_scene_image_release(ticket->records[i].base);
        qa_material_record videos = {.videos = ticket->records[i].videos};
        qa_material_videos_clear(&videos);
    }
    qa_scene_image_release(ticket->fog_image); qa_scene_image_release(ticket->dlight_image);
    qa_material_generated *generated = ticket->generated;
    while (generated) {
        qa_material_generated *next = generated->next;
        qa_scene_image_release(generated->image); free(generated); generated = next;
    }
    free(ticket->ordered); free(ticket->records); ticket->owner->image_policy = NULL; free(ticket);
}
bool qa_scene_material_image_policy_prepare_profile(qa_material_library *library, qa_scene_resource_policy *resources,
    qa_scene_world_image_policy *const *worlds, size_t world_count,
    qa_material_order_image_policy *order, const qa_material_profile *profile,
    qa_scene_material_image_policy **out, qa_error *error)
{
    qa_scene_resources *destination = qa_scene_resource_policy_destination(resources);
    if (!out || *out || !library || library->capture_depth || library->mutating || library->image_policy || !destination ||
        qa_material_order_image_policy_source(order) != library->order ||
        library->resources != qa_scene_resource_policy_source(resources) || (world_count && !worlds) ||
        library->count > SIZE_MAX / sizeof(material_policy_record)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material image preparation requires its actual resource bank"); return false;
    }
    qa_scene_material_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) goto memory;
    ticket->records = library->count ? calloc(library->count, sizeof(*ticket->records)) : NULL;
    if (library->count && !ticket->records) { free(ticket); goto memory; }
    ticket->owner = library; ticket->resources = resources; ticket->count = library->count;
    ticket->source_profile = library->profile;
    ticket->source_profile_bound = library->source_profile;
    ticket->source_upload = library->source_upload; ticket->source_upload_context = library->source_upload_context;
    ticket->source_ui_fullscreen = library->source_ui_fullscreen; ticket->source_ui_context = library->source_ui_context;
    ticket->profile = profile ? *profile : library->profile;
    ticket->replace_profile = profile != NULL;
    ticket->order = order;
    library->image_policy = ticket;
    if ((library->fog_image && !qa_scene_resource_policy_dependency_image(resources, library->fog_image,
            &ticket->fog_image, error)) ||
        (library->dlight_image && !qa_scene_resource_policy_dependency_image(resources, library->dlight_image,
            &ticket->dlight_image, error))) {
        material_policy_dispose(ticket); return false;
    }
    qa_material_generated **tail = &ticket->generated;
    for (const qa_material_generated *current = library->generated; current; current = current->next) {
        qa_material_generated *generated = calloc(1, sizeof(*generated));
        if (!generated) { material_policy_dispose(ticket); goto memory; }
        generated->name = current->name; generated->picture = current->picture;
        *tail = generated; tail = &generated->next;
        qa_scene_image *image = NULL;
        if (!qa_scene_resource_policy_image(resources, current->image, &image, error)) {
            material_policy_dispose(ticket); return false;
        }
        generated->image = image;
    }
    qa_material_library staging = *library;
    staging.resources = destination; staging.generated = ticket->generated;
    staging.fog_image = ticket->fog_image; staging.dlight_image = ticket->dlight_image;
    staging.profile = ticket->profile;
    qa_q3_image_upload_options restart_upload = {0};
    bool source_restart = qa_scene_resource_policy_source_restart_read(resources, &restart_upload);
    for (size_t i = 0; i < ticket->count; ++i) {
        qa_material_record *current = library->ordered[i]; material_policy_record *prepared = &ticket->records[i];
        prepared->record = current;
        prepared->options = current->options;
        prepared->variant_upload = current->source_variant_upload;
        if (source_restart && prepared->options.source_q3) {
            bool mipmap = prepared->options.source_upload.mipmap;
            bool picmip = prepared->options.source_upload.allow_picmip;
            prepared->options.source_upload = restart_upload;
            prepared->options.source_upload.mipmap = mipmap;
            prepared->options.source_upload.allow_picmip = picmip;
        }
        if (source_restart && current->source_variant_parent) {
            prepared->variant_upload = restart_upload;
            prepared->variant_upload.mipmap = current->source_variant_upload.mipmap;
            prepared->variant_upload.allow_picmip = current->source_variant_upload.allow_picmip;
        }
        if (current->material.revision == UINT64_MAX) {
            material_policy_dispose(ticket);
            qa_error_set(error, QA_ERROR_MEMORY, i, "Material image revisions exhausted"); return false;
        }
        if (current->source_variant_parent) continue;
        qa_material_video_receipt **video_tail = &prepared->videos;
        for (const qa_material_video_receipt *video = current->videos; video; video = video->next) {
            qa_material_video_receipt *copy = calloc(1, sizeof(*copy));
            if (!copy) {
                qa_error_set(error, QA_ERROR_MEMORY, i, "Preparing actual shader video receipts");
                material_policy_dispose(ticket); return false;
            }
            *copy = *video; copy->next = NULL; copy->image = NULL;
            copy->source = qa_material_string(video->source, error);
            *video_tail = copy; video_tail = &copy->next;
            qa_scene_image *mapped = NULL;
            if (!copy->source || (video->image &&
                !qa_scene_resource_policy_dependency_image(resources, video->image, &mapped, error))) {
                material_policy_dispose(ticket); return false;
            }
            copy->image = mapped;
        }
        if (current->base_image) {
            const qa_scene_image *base = NULL;
            for (size_t w = 0; !base && w < world_count; ++w)
                (void)qa_scene_world_image_policy_base(worlds[w], current->world_identity,
                    current->base_name, current->base_image, &base);
            if (base) { prepared->base = base; qa_scene_image_retain(base); }
            else {
                qa_scene_image *image = NULL;
                if (!qa_scene_resource_policy_image(resources, current->base_image, &image, error)) {
                    material_policy_dispose(ticket); return false;
                }
                prepared->base = image;
            }
        }
        qa_material_record pending = {.options = prepared->options, .kind = current->kind,
            .world_identity = current->world_identity, .lightmap_index = current->lightmap_index,
            .base_name = current->base_name, .base_image = prepared->base};
        pending.material.name = qa_material_string(current->material.name, error);
        pending.material.family = current->material.family; pending.material.cull = QA_CULL_FRONT;
        pending.material.profile = ticket->replace_profile ? ticket->profile : current->material.profile;
        if (library->source_profile) pending.material.profile.ui_fullscreen = current->material.profile.ui_fullscreen;
        if (!pending.material.name) { material_policy_dispose(ticket); return false; }
        qa_material_record refresh = *current; refresh.videos = prepared->videos;
        staging.refresh_record = &refresh; staging.registration_record = NULL;
        qa_material_script *script = library->scripts[qa_material_hash(current->material.name)];
        while (script && strcmp(script->name, current->material.name)) script = script->next;
        const qa_material_generated *generated = ticket->generated;
        while (generated && strcmp(generated->name, current->material.name)) generated = generated->next;
        const char *request = current->material.name;
        qa_scene_image_options dependency_options = script_options(script, &prepared->options);
        if (current->material.stage_count) {
            const qa_material_stage *stage = &current->material.stages[current->material.stage_count - 1];
            if (stage->image_count && stage->image_names && stage->image_names[0]) request = stage->image_names[0];
        }
        bool ok = script && !generated && current->kind != QA_MATERIAL_DEFAULT &&
            current->kind != QA_MATERIAL_STENCIL_SHADOW
            ? qa_material_script_register(&staging, &pending.material, (qa_bytes){script->text, script->size},
                &dependency_options, current->lightmap_index, current->base_name, prepared->base, error)
            : implicit(&staging, &pending, request, error);
        if (!ok) { qa_material_clear(&pending.material); material_policy_dispose(ticket); return false; }
        if (library->source_profile && pending.material.lightmap_index >= 0) {
            bool lightmap = false;
            for (size_t stage = 0; stage < pending.material.stage_count; ++stage)
                if (pending.material.stages[stage].is_lightmap) lightmap = true;
            if (!lightmap) pending.material.lightmap_index = -1;
        }
        pending.material.identity = current->material.identity;
        pending.material.revision = current->material.revision + 1;
        pending.material.registration = current->material.registration;
        pending.material.sorted_index = current->material.sorted_index;
        pending.material.order_entry = current->material.order_entry;
        pending.material.fog_image = ticket->fog_image; pending.material.dlight_image = ticket->dlight_image;
        pending.material.remapped = current->material.remapped;
        pending.material.remap_time_offset = current->material.remap_time_offset;
        pending.material.source_time_offset = current->material.source_time_offset;
        pending.material.source_remap = current->material.source_remap;
        pending.material.library = library;
        prepared->material = pending.material;
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        material_policy_record *prepared = ticket->records + i;
        qa_material_record *current = prepared->record;
        if (!current->source_variant_parent) continue;
        const qa_material *parent = NULL;
        for (size_t j = 0; j < ticket->count; ++j)
            if (ticket->records[j].record == current->source_variant_parent)
                parent = &ticket->records[j].material;
        if (!parent) {
            material_policy_dispose(ticket);
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Prepared recipient shader lost its actual parent"); return false;
        }
        source_variant_mapper mapper = {.resources = destination, .upload = prepared->variant_upload};
        if (parent->no_mipmaps) mapper.upload.mipmap = false;
        if (parent->no_picmip) mapper.upload.allow_picmip = false;
        if (!material_copy_images(parent, &prepared->material, source_variant_image, &mapper, error)) {
            material_policy_dispose(ticket); return false;
        }
        prepared->material.identity = current->material.identity;
        prepared->material.revision = current->material.revision + 1;
        prepared->material.registration = current->material.registration;
        prepared->material.sorted_index = current->material.sorted_index;
        prepared->material.order_entry = current->material.order_entry;
        prepared->material.remapped = NULL;
        prepared->material.library = library;
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        material_policy_record *prepared = &ticket->records[i];
        if (prepared->record->source_variant_parent) continue;
        if (prepared->material.source_remap) continue;
        for (const qa_material_remap_record *remap = library->remaps; remap; remap = remap->next) {
            if (strcmp(remap->original, prepared->material.name)) continue;
            prepared->material.remapped = NULL;
            for (size_t j = 0; j < ticket->count; ++j) {
                const material_policy_record *target = &ticket->records[j];
                if (!target->record->source_variant_parent && target->record->kind == prepared->record->kind &&
                    target->record->world_identity == prepared->record->world_identity &&
                    target->record->lightmap_index == prepared->record->lightmap_index &&
                    !strcmp(target->material.name, remap->replacement) &&
                    target->record->base_image == prepared->record->base_image &&
                    same_options(&target->record->options, &prepared->record->options) && !target->material.default_shader) {
                    prepared->material.remapped = &target->record->material;
                    prepared->material.remap_time_offset = remap->time_offset; break;
                }
            }
            break;
        }
    }
    ticket->destination = qa_material_library_create_detached(destination, error);
    if (!ticket->destination) { material_policy_dispose(ticket); return false; }
    qa_material_library *prepared_library = ticket->destination;
    prepared_library->order = qa_material_order_create(error);
    if (!prepared_library->order) { material_policy_dispose(ticket); return false; }
    memcpy(prepared_library->scripts, library->scripts, sizeof(library->scripts));
    prepared_library->generated = ticket->generated; prepared_library->remaps = library->remaps;
    prepared_library->profile = ticket->profile; prepared_library->sun_light = library->sun_light;
    prepared_library->source_profile = library->source_profile;
    prepared_library->source_upload = library->source_upload;
    prepared_library->source_upload_context = library->source_upload_context;
    prepared_library->source_ui_fullscreen = library->source_ui_fullscreen;
    prepared_library->source_ui_context = library->source_ui_context;
    prepared_library->sun_direction = library->sun_direction; prepared_library->has_sun = library->has_sun;
    prepared_library->sky_height = library->sky_height; prepared_library->catalog_ready = true;
    prepared_library->fog_image = ticket->fog_image; prepared_library->dlight_image = ticket->dlight_image;
    prepared_library->policy_source = library;
    *out = ticket; return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining material image preparation"); return false;
}
bool qa_scene_material_image_policy_prepare(qa_material_library *library, qa_scene_resource_policy *resources,
    qa_scene_world_image_policy *const *worlds, size_t world_count, qa_material_order_image_policy *order,
    qa_scene_material_image_policy **out, qa_error *error)
{
    return qa_scene_material_image_policy_prepare_profile(library, resources, worlds, world_count,
        order, NULL, out, error);
}
qa_material_library *qa_scene_material_image_policy_source(const qa_scene_material_image_policy *ticket)
{ return material_policy_current(ticket) ? ticket->owner : NULL; }
qa_material_library *qa_scene_material_image_policy_destination(const qa_scene_material_image_policy *ticket)
{ return material_policy_current(ticket) && !ticket->sealed && !ticket->published ? ticket->destination : NULL; }
bool qa_scene_material_image_policy_video_start(qa_scene_material_image_policy *ticket,
    const qa_scene_image *(*start)(void *, const char *, qa_error *), void *context, qa_error *error)
{
    if (!material_policy_current(ticket) || ticket->sealed || ticket->published || !start ||
        !ticket->owner->video_start || !ticket->owner->video_required || ticket->destination->policy_video) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared shader movies require their genuine live and destination owners");
        return false;
    }
    ticket->destination->video_start = start;
    ticket->destination->video_context = context;
    ticket->destination->video_required = true;
    ticket->destination->policy_video = true;
    return true;
}
bool qa_scene_material_image_policy_read(const qa_scene_material_image_policy *ticket,
    const qa_material *current, const qa_material **destination)
{
    if (!destination || !current || !material_policy_current(ticket) || ticket->published) return false;
    for (size_t i = 0; i < ticket->count; ++i)
        if (&ticket->records[i].record->material == current) {
            *destination = &ticket->records[i].material; return true;
        }
    return false;
}
bool qa_scene_material_image_policy_ready(qa_scene_material_image_policy *ticket, qa_error *error)
{
    if (!material_policy_current(ticket) || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared materials lost their actual resource or order owner"); return false;
    }
    for (size_t i = 0; i < ticket->count; ++i)
        if (!isfinite(ticket->records[i].material.sort)) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Prepared material sort is not finite"); return false;
        }
    if (ticket->sealed) return true;
    ticket->added = ticket->destination->count;
    if (ticket->added > QA_MATERIAL_MAX_REGISTERED - ticket->count) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Prepared materials exceed the actual registration limit"); return false;
    }
    ticket->capacity = ticket->owner->capacity;
    size_t count = ticket->count + ticket->added;
    if (!ticket->capacity && count) ticket->capacity = 64;
    while (ticket->capacity < count) ticket->capacity *= 2;
    ticket->ordered = ticket->capacity ? malloc(ticket->capacity * sizeof(*ticket->ordered)) : NULL;
    if (ticket->capacity && !ticket->ordered) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing the complete material registration roster"); return false;
    }
    for (size_t i = 0; i < ticket->count; ++i) ticket->ordered[i] = ticket->owner->ordered[i];
    for (size_t i = 0; i < ticket->added; ++i) ticket->ordered[ticket->count + i] = ticket->destination->ordered[i];
    if (!qa_material_order_image_policy_add(ticket->order, ticket->destination->order, error)) {
        free(ticket->ordered); ticket->ordered = NULL; return false;
    }
    ticket->destination->policy_sealed = true;
    ticket->sealed = true; return true;
}
bool qa_scene_material_image_policy_world(const qa_scene_material_image_policy *ticket, uint64_t world,
    int32_t lightmap, bool has_lightmap, const char *name, const qa_scene_image_options *input,
    const qa_material **current, const qa_material **destination, qa_error *error)
{
    if (!current || !destination || !name || !input || !world || !material_policy_current(ticket) || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "World material receipt requires the actual prepared library"); return false;
    }
    qa_scene_image_options options = *input; options.usage = QA_IMAGE_USAGE_WALL;
    if (ticket->owner->source_profile) options.source_q3 = true;
    qa_material_registration_kind kind = has_lightmap ? QA_MATERIAL_LIGHTMAP :
        lightmap == -3 ? QA_MATERIAL_VERTEX : lightmap == -2 ? QA_MATERIAL_WHITE :
        lightmap == -4 ? QA_MATERIAL_PICTURE : QA_MATERIAL_DYNAMIC;
    char *key = qa_material_name(name, error);
    if (!key) return false;
    for (size_t i = 0; i < ticket->count; ++i) {
        const material_policy_record *prepared = ticket->records + i;
        const qa_material_record *record = prepared->record;
        if (!record->source_variant_parent && record->kind == kind && record->world_identity == world && record->lightmap_index == lightmap &&
            !record->base_image && !strcmp(record->material.name, key) && same_options(&record->options, &options)) {
            *current = &record->material; *destination = &prepared->material;
            free(key); return true;
        }
    }
    free(key);
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "World shader lost its genuine registered material receipt"); return false;
}
bool qa_scene_material_image_policy_ready_is(const qa_scene_material_image_policy *ticket)
{ return material_policy_current(ticket) && ticket->sealed && !ticket->published &&
    qa_scene_resource_policy_ready_is(ticket->resources); }
void qa_scene_material_image_policy_publish(qa_scene_material_image_policy *ticket)
{
    if (!material_policy_current(ticket) || !ticket->sealed || ticket->published) return;
    qa_material_library *library = ticket->owner;
    qa_scene_image *fog_image = library->fog_image, *dlight_image = library->dlight_image;
    library->fog_image = ticket->fog_image; library->dlight_image = ticket->dlight_image;
    ticket->fog_image = fog_image; ticket->dlight_image = dlight_image;
    for (size_t i = 0; i < ticket->count; ++i) {
        material_policy_record *prepared = &ticket->records[i];
        qa_material material = prepared->record->material;
        prepared->record->material = prepared->material; prepared->material = material;
        const qa_scene_image *base = prepared->record->base_image;
        prepared->record->base_image = prepared->base; prepared->base = base;
        qa_scene_image_options options = prepared->record->options;
        prepared->record->options = prepared->options; prepared->options = options;
        qa_q3_image_upload_options upload = prepared->record->source_variant_upload;
        prepared->record->source_variant_upload = prepared->variant_upload; prepared->variant_upload = upload;
        qa_material_video_receipt *videos = prepared->record->videos;
        prepared->record->videos = prepared->videos; prepared->videos = videos;
        qa_material_order_changed(prepared->record->material.order_entry);
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        qa_material_record *record = ticket->records[i].record;
        if (record->source_variant_parent)
            record->source_variant_revision = record->source_variant_parent->material.revision;
    }
    qa_material_generated *prepared = ticket->generated;
    for (qa_material_generated *current = library->generated; current; current = current->next, prepared = prepared->next) {
        const qa_scene_image *image = current->image; current->image = prepared->image; prepared->image = image;
    }
    qa_material_record **old_ordered = library->ordered;
    library->ordered = ticket->ordered; ticket->ordered = old_ordered;
    library->capacity = ticket->capacity;
    for (size_t bucket = 0; bucket < QA_MATERIAL_BUCKETS; ++bucket) {
        qa_material_record *record = ticket->destination->records[bucket];
        while (record) {
            qa_material_record *next = record->next;
            record->material.registration += (uint32_t)ticket->count;
            record->material.library = library;
            record->next = library->records[bucket]; library->records[bucket] = record;
            record = next;
        }
        ticket->destination->records[bucket] = NULL;
    }
    library->count += ticket->added; ticket->destination->count = 0;
    library->profile = ticket->profile;
    library->sun_light = ticket->destination->sun_light; library->sun_direction = ticket->destination->sun_direction;
    library->has_sun = ticket->destination->has_sun; library->sky_height = ticket->destination->sky_height;
    for (size_t i = 1; i < library->count; ++i) {
        qa_material_record *record = library->ordered[i]; size_t j = i;
        while (j && (library->ordered[j - 1]->material.sort > record->material.sort ||
            (library->ordered[j - 1]->material.sort == record->material.sort &&
             library->ordered[j - 1]->material.registration > record->material.registration))) {
            library->ordered[j] = library->ordered[j - 1]; --j;
        }
        library->ordered[j] = record;
    }
    for (size_t i = 0; i < library->count; ++i) library->ordered[i]->material.sorted_index = (uint32_t)i;
    ticket->published = true;
}
bool qa_scene_material_image_policy_finish(qa_scene_material_image_policy **owner, qa_error *error)
{
    if (!owner || !material_policy_current(*owner) || !(*owner)->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material retirement requires its published image preparation"); return false;
    }
    material_policy_dispose(*owner); *owner = NULL; return true;
}
bool qa_scene_material_image_policy_abort(qa_scene_material_image_policy **owner, qa_error *error)
{
    if (!owner || !material_policy_current(*owner) || (*owner)->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material abort requires its unpublished image preparation"); return false;
    }
    material_policy_dispose(*owner); *owner = NULL; return true;
}

static qa_material_remap_record *remap_find(const qa_material_library *library, const char *name)
{
    for (qa_material_remap_record *remap = library->remaps; remap; remap = remap->next)
        if (!strcmp(name, remap->original)) return remap;
    return NULL;
}
static void registration_rollback(qa_material_library *, size_t);

static bool register_material(qa_material_library *library, const char *name,
                                const qa_scene_image_options *input,
                                qa_material_registration_kind kind,
                                uint64_t world_identity, int32_t lightmap_index,
                                const char *base_name, const qa_scene_image *base_image,
                                const qa_material **out, qa_error *error)
{
    if (out) *out = NULL;
    if (!library || !out || (unsigned)kind > QA_MATERIAL_STENCIL_SHADOW) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material registration arguments");
        return false;
    }
    qa_scene_image_options options = input ? *input : default_options();
    if ((options.palette_rgb.size && (options.palette_rgb.size != 768 || !options.palette_rgb.data)) ||
        (options.translation.size && (options.translation.size != 256 || !options.translation.data)) ||
        (unsigned)options.family > QA_SCENE_Q3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material image options");
        return false;
    }
    options.usage = kind == QA_MATERIAL_PICTURE ? QA_IMAGE_USAGE_PICTURE : QA_IMAGE_USAGE_WALL;
    if (library->source_profile) {
        char *source_key = qa_material_name(name, error);
        if (!source_key) return false;
        unsigned source_bucket = qa_material_hash(source_key);
        const qa_material_library *owners[] = {library, library->policy_source};
        for (unsigned owner = 0; owner < 2; ++owner) {
            if (!owners[owner]) continue;
            for (const qa_material_record *cached = owners[owner]->records[source_bucket]; cached; cached = cached->next)
                if (!cached->source_variant_parent && !strcmp(cached->material.name, source_key) &&
                    (cached->material.default_shader || cached->material.lightmap_index == lightmap_index)) {
                    *out = &cached->material; free(source_key); return true;
                }
        }
        free(source_key);
    }
    if (library->source_profile && kind != QA_MATERIAL_DEFAULT && kind != QA_MATERIAL_STENCIL_SHADOW &&
        !options.source_q3) {
        if (!library->source_upload) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source registration has no actual renderer upload producer"); return false;
        }
        qa_material_source_upload_fn producer = library->source_upload;
        void *context = library->source_upload_context;
        if (!producer(context, options.mipmap, options.mipmap, &options.source_upload, error)) return false;
        if (library->source_upload != producer || library->source_upload_context != context ||
            !library->source_profile || !qa_q3_image_upload_options_valid(&options.source_upload, error) ||
            options.source_upload.mipmap != options.mipmap || options.family != QA_SCENE_Q3) {
            if (!error || error->code == QA_OK)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source upload producer lost its actual registration binding");
            return false;
        }
        options.source_q3 = true;
    }
    char *key = qa_material_name(name, error);
    if (!key) return false;
    unsigned bucket = qa_material_hash(key);
    for (qa_material_record *record = library->records[bucket]; record; record = record->next) {
        if (!library->source_profile && !record->source_variant_parent && record->kind == kind && record->world_identity == world_identity &&
            record->lightmap_index == lightmap_index && !strcmp(record->material.name, key) &&
            record->base_image == base_image &&
            same_options(&record->options, &options)) {
            *out = &record->material;
            free(key);
            return true;
        }
    }
    if (library->policy_source) {
        for (qa_material_record *record = library->policy_source->records[bucket]; record; record = record->next)
            if (!library->source_profile && !record->source_variant_parent && record->kind == kind && record->world_identity == world_identity &&
                record->lightmap_index == lightmap_index && !strcmp(record->material.name, key) &&
                record->base_image == base_image && same_options(&record->options, &options)) {
                *out = &record->material; free(key); return true;
            }
    }
    /* A remap target can name a fully compiled parent which has not yet
     * published. Each binding selects that one record, without unfolding its
     * own remap. Public mutation reentry remains rejected by mutation_begin. */
    size_t admitting = 0;
    for (qa_material_record *pending = library->registration_record; pending; pending = pending->admission_parent) {
        if (pending->kind == kind && pending->world_identity == world_identity &&
            pending->lightmap_index == lightmap_index && !strcmp(pending->material.name, key) &&
            pending->base_image == base_image && same_options(&pending->options, &options)) {
            *out = &pending->material; free(key); return true;
        }
        ++admitting;
    }
    if ((!library->source_profile && admitting >= QA_MATERIAL_MAX_REGISTERED - library->count) ||
        admitting >= QA_MATERIAL_MAX_REGISTERED) {
        free(key);
        qa_error_set(error, QA_ERROR_FORMAT, library->count, "Source material registration limit reached");
        return false;
    }
    qa_material_record *record = calloc(1, sizeof(*record));
    if (!record) {
        free(key);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material registration");
        return false;
    }
    record->kind = kind;
    record->world_identity = world_identity;
    record->lightmap_index = lightmap_index;
    record->material.name = key;
    record->material.family = options.family;
    record->material.cull = QA_CULL_FRONT;
    record->material.profile = library->profile;
    if (library->source_profile && kind != QA_MATERIAL_DEFAULT && kind != QA_MATERIAL_STENCIL_SHADOW) {
        qa_material_source_ui_fullscreen_fn producer = library->source_ui_fullscreen;
        void *context = library->source_ui_context;
        if (!producer) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source registration has no actual live UI flag producer");
            record_free(record); return false;
        }
        if (!producer(context, &record->material.profile.ui_fullscreen, error)) { record_free(record); return false; }
        if (library->source_ui_fullscreen != producer || library->source_ui_context != context || !library->source_profile) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source registration lost its actual UI flag binding");
            record_free(record); return false;
        }
    }
    if (!qa_material_order_reserve(library->order, &record->material,
                                      &record->material.order_entry, error)) {
        record_free(record); return false;
    }
    if (base_image != NULL) {
        record->base_name = qa_material_string(base_name != NULL ? base_name : base_image->name, error);
        if (record->base_name == NULL) { record_free(record); return false; }
        record->base_image = base_image; qa_scene_image_retain(base_image);
    }
    if (!copy_options(record, &options, error)) { record_free(record); return false; }
    qa_material_script *script = library->scripts[bucket];
    while (script && strcmp(script->name, key)) script = script->next;
    const qa_material_generated *generated = library->generated;
    while (generated != NULL && strcmp(generated->name, key) != 0) generated = generated->next;
    qa_scene_image_options dependency_options = script_options(script, &options);
    qa_material_record *previous_record = library->registration_record;
    record->admission_parent = previous_record;
    library->registration_record = record;
    bool compiled = script && generated == NULL && kind != QA_MATERIAL_DEFAULT && kind != QA_MATERIAL_STENCIL_SHADOW
        ? qa_material_script_register(library, &record->material,
              (qa_bytes){script->text, script->size}, &dependency_options, lightmap_index,
              record->base_name, record->base_image, error)
        : implicit(library, record, name, error);
    if (!compiled) { library->registration_record = previous_record; record_free(record); return false; }
    if (library->source_profile && record->material.lightmap_index >= 0) {
        bool lightmap = false;
        for (size_t i = 0; i < record->material.stage_count; ++i)
            if (record->material.stages[i].is_lightmap) lightmap = true;
        if (!lightmap) record->material.lightmap_index = -1;
    }
    if (library->source_profile && library->count >= QA_MATERIAL_MAX_REGISTERED) {
        library->registration_record = previous_record;
        record_free(record);
        *out = qa_material_find(library, "*default");
        return *out != NULL;
    }
    size_t registered_before = library->count;
    qa_material_remap_record *remap = remap_find(library, key);
    if (remap) {
        const qa_material *target;
        if (!register_material(library, remap->replacement, &options, kind, world_identity, lightmap_index,
                               record->base_name, record->base_image, &target, error)) {
            library->registration_record = previous_record;
            registration_rollback(library, registered_before);
            record_free(record);
            return false;
        }
        if (!target->default_shader) {
            record->material.remapped = target;
            record->material.remap_time_offset = remap->time_offset;
        }
    }
    library->registration_record = previous_record;
    record->admission_parent = NULL;
    if (library->source_profile && library->count >= QA_MATERIAL_MAX_REGISTERED) {
        record_free(record); *out = qa_material_find(library, "*default");
        return *out != NULL;
    }
    if (!publish(library, record, error)) {
        registration_rollback(library, registered_before);
        record_free(record); return false;
    }
    *out = &record->material;
    return true;
}

bool qa_material_register_kind(qa_material_library *library, const char *name,
                                const qa_scene_image_options *options,
                                qa_material_registration_kind kind,
                                const qa_material **out, qa_error *error)
{
    if (out) *out = NULL;
    if (!mutation_begin(library, error)) return false;
    int32_t index = kind == QA_MATERIAL_LIGHTMAP ? 0 : kind == QA_MATERIAL_WHITE ? -2 :
        kind == QA_MATERIAL_VERTEX ? -3 : kind == QA_MATERIAL_PICTURE ? -4 : -1;
    return mutation_end(library, register_material(library, name, options, kind, 0, index, NULL, NULL, out, error));
}

bool qa_material_register_world(qa_material_library *library, const char *name,
                                 const qa_scene_image_options *options, uint64_t world_identity,
                                 int32_t lightmap_index, qa_material_registration_kind kind,
                                 const char *base_name, const qa_scene_image *base_image,
                                 const qa_material **out, qa_error *error)
{
    if (out) *out = NULL;
    if (!world_identity || lightmap_index < -4) {
        if (out) *out = NULL;
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "World material requires an identity and valid lightmap index");
        return false;
    }
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, register_material(library, name, options, kind, world_identity, lightmap_index,
                              base_name, base_image, out, error));
}

bool qa_material_register(qa_material_library *library, const char *name,
                           const qa_scene_image_options *options, bool lightmapped,
                           const qa_material **out, qa_error *error)
{
    return qa_material_register_kind(library, name, options,
        lightmapped ? QA_MATERIAL_LIGHTMAP : QA_MATERIAL_DYNAMIC, out, error);
}

bool qa_material_library_source_shaders_initialize(qa_material_library *library,
    const qa_scene_image_options *options, qa_error *error)
{
    if (!library || !library->source_profile || !options || !library->catalog_ready ||
        !library->source_upload) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source shader initialization requires its loaded catalog and upload owner");
        return false;
    }
    static const char *const names[] = {"projectionShadow", "flareShader", "sun"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const qa_material *material = NULL;
        if (!qa_material_register_kind(library, names[i], options, QA_MATERIAL_DYNAMIC, &material, error))
            return false;
    }
    return true;
}

static bool generated_picture(qa_material_library *library, const char *name,
                                            const qa_scene_image *image, const qa_material **out,
                                            qa_error *error)
{
    if (library == NULL || image == NULL || out == NULL || image->kind == QA_SCENE_DEPTH32F) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "generated picture requires a color image and library");
        return false;
    }
    char *key = qa_material_name(name, error);
    if (key == NULL) return false;
    qa_material_generated *generated = library->generated;
    while (generated != NULL && strcmp(generated->name, key) != 0) generated = generated->next;
    if (generated == NULL) {
        generated = calloc(1, sizeof(*generated));
        if (generated == NULL) {
            free(key); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain generated material image"); return false;
        }
        generated->name = key; key = NULL;
        generated->image = image; qa_scene_image_retain(image);
        generated->next = library->generated; library->generated = generated;
    }
    free(key);
    if (generated->picture != NULL) { *out = generated->picture; return true; }
    typedef struct prepared_picture { qa_material_record *record; qa_material material; } prepared_picture;
    size_t count = 0;
    for (qa_material_record *record = library->records[qa_material_hash(generated->name)]; record != NULL; record = record->next)
        if (record->material.default_shader && strcmp(record->material.name, generated->name) == 0) ++count;
    prepared_picture *prepared = count != 0 ? calloc(count, sizeof(*prepared)) : NULL;
    if (count != 0 && prepared == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot prepare generated material recovery"); return false;
    }
    size_t ready = 0;
    for (qa_material_record *record = library->records[qa_material_hash(generated->name)]; record != NULL; record = record->next) {
        if (!record->material.default_shader || strcmp(record->material.name, generated->name) != 0) continue;
        if (record->material.revision == UINT64_MAX) {
            for (size_t i = 0; i < ready; ++i) qa_material_clear(&prepared[i].material);
            free(prepared); qa_error_set(error, QA_ERROR_MEMORY, 0, "material revisions exhausted"); return false;
        }
        qa_material_record pending = {.kind = QA_MATERIAL_PICTURE, .lightmap_index = -4, .options = record->options};
        pending.material.name = qa_material_string(record->material.name, error);
        pending.material.family = record->material.family;
        pending.material.profile = library->profile; pending.material.cull = QA_CULL_FRONT;
        if (pending.material.name == NULL || !implicit(library, &pending, generated->name, error)) {
            qa_material_clear(&pending.material);
            for (size_t i = 0; i < ready; ++i) qa_material_clear(&prepared[i].material);
            free(prepared); return false;
        }
        pending.material.identity = record->material.identity;
        pending.material.revision = record->material.revision+1;
        pending.material.registration = record->material.registration;
        pending.material.sorted_index = record->material.sorted_index;
        pending.material.fog_image = library->fog_image; pending.material.dlight_image = library->dlight_image;
        prepared[ready++] = (prepared_picture){record, pending.material};
    }
    const qa_material *recovered = NULL;
    for (size_t i = 0; i < ready; ++i) {
        qa_material *destination = &prepared[i].record->material;
        qa_material_order_entry *entry = destination->order_entry;
        qa_material_clear(destination); *destination = prepared[i].material;
        destination->library = library;
        destination->order_entry = entry;
        qa_material_order_changed(entry);
        if (recovered == NULL || destination->registration < recovered->registration) recovered = destination;
    }
    free(prepared);
    /* Keep published handles and registration order while updating shader rank. */
    for (size_t i = 1; i < library->count; ++i) {
        qa_material_record *record = library->ordered[i]; size_t j = i;
        while (j != 0 && (library->ordered[j-1]->material.sort > record->material.sort ||
            (library->ordered[j-1]->material.sort == record->material.sort &&
             library->ordered[j-1]->material.registration > record->material.registration))) {
            library->ordered[j] = library->ordered[j-1]; --j;
        }
        library->ordered[j] = record;
    }
    for (size_t i = 0; i < library->count; ++i) library->ordered[i]->material.sorted_index = (uint32_t)i;
    if (recovered != NULL) { generated->picture = recovered; *out = recovered; return true; }
    qa_scene_image_options options = default_options(); options.mipmap = false;
    options.wrap = QA_SCENE_CLAMP; options.filter = QA_SCENE_LINEAR;
    if (!register_material(library, name, &options, QA_MATERIAL_PICTURE, 0, -4, NULL, NULL, out, error)) return false;
    generated->picture = *out;
    return true;
}

bool qa_material_register_generated_picture(qa_material_library *library, const char *name,
                                            const qa_scene_image *image, const qa_material **out,
                                            qa_error *error)
{
    if (out) *out = NULL;
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, generated_picture(library, name, image, out, error));
}

bool qa_material_library_animate(qa_material_library *library, double seconds,
                                 qa_scene_frame *frame, qa_error *error)
{
    if (!qa_material_library_idle(library) || frame == NULL || !isfinite(seconds)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid material animation clock"); return false;
    }
    for (size_t i = 0; i < library->count; ++i) {
        const qa_material *material = &library->ordered[i]->material;
        for (size_t s = 0; s < material->stage_count; ++s) {
            const qa_material_stage *stage = &material->stages[s];
            for (size_t j = 0; j < stage->image_count; ++j) {
                const qa_scene_image *image = stage->images[j];
                if (image != NULL && image->animation_count > 1 &&
                    !qa_scene_frame_image(frame, qa_scene_image_at_time(image, seconds), error)) return false;
            }
        }
    }
    return true;
}

const qa_material *qa_material_find(const qa_material_library *library, const char *name)
{
    if (!library) return NULL;
    char key[MATERIAL_NAME_BYTES];
    if (!material_name_write(name, key, NULL)) return NULL;
    const qa_material *result = NULL;
    for (qa_material_record *record = library->records[qa_material_hash(key)]; record; record = record->next)
        if (!record->source_variant_parent && !strcmp(record->material.name, key) && (!result || record->material.registration < result->registration))
            result = &record->material;
    return result;
}

bool qa_material_has_authored(const qa_material_library *library, const char *name)
{
    if (!library) return false;
    char key[MATERIAL_NAME_BYTES];
    if (!material_name_write(name, key, NULL)) return false;
    const qa_material_script *script = library->scripts[qa_material_hash(key)];
    while (script && strcmp(script->name, key)) script = script->next;
    return script != NULL;
}

float qa_material_library_cloud_height(const qa_material_library *library)
{
    return library ? library->sky_height : 0;
}

bool qa_material_library_sun(const qa_material_library *library, qa_vec3 *light, qa_vec3 *direction)
{
    if (!library || !library->has_sun) return false;
    if (light) *light = library->sun_light;
    if (direction) *direction = library->sun_direction;
    return true;
}

void qa_material_library_set_video_start(qa_material_library *library,
                                          qa_material_video_start_fn start, void *context)
{
    if (!qa_material_library_idle(library) || library->policy_source) return;
    library->video_start = start;
    library->video_context = context;
    library->video_required = start != NULL;
}
bool qa_material_library_video_start_is(const qa_material_library *library,
    const qa_scene_image *(*start)(void *, const char *, qa_error *), const void *context)
{
    return library && start && library->video_required && library->video_start == start &&
        library->video_context == context;
}
bool qa_material_library_video_start_read(const qa_material_library *library,
    const qa_scene_image *(**start)(void *, const char *, qa_error *), void **context)
{
    if (!library || !start || !context) return false;
    *start = library->video_start; *context = library->video_context; return true;
}

static bool set_profile(qa_material_library *library, const qa_material_profile *profile,
                                      qa_error *error)
{
    if (!library || !profile) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material profile requires a library and profile");
        return false;
    }
    const qa_material_profile *old = &library->profile;
    bool same = old->detail_textures == profile->detail_textures &&
        old->vertex_lighting == profile->vertex_lighting && old->ui_fullscreen == profile->ui_fullscreen &&
        old->permedia2 == profile->permedia2 && old->multitexture == profile->multitexture &&
        old->texture_env_add == profile->texture_env_add && old->ignore_fast_path == profile->ignore_fast_path;
    if (same) return true;
    if (library->count > 2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Rebuild the material library and worlds to change a registered shader profile");
        return false;
    }
    library->profile = *profile;
    for (size_t i = 0; i < library->count; ++i) library->ordered[i]->material.profile = *profile;
    return true;
}

bool qa_material_library_set_profile(qa_material_library *library, const qa_material_profile *profile,
                                      qa_error *error)
{
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, set_profile(library, profile, error));
}
bool qa_material_library_set_source_profile(qa_material_library *library,
    const qa_material_profile *profile, qa_error *error)
{
    if (!mutation_begin(library, error)) return false;
    if (library->count > 2 && !library->source_profile) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source profile must bind before content registration");
        return mutation_end(library, false);
    }
    bool ok = set_profile(library, profile, error);
    if (ok) library->source_profile = true;
    return mutation_end(library, ok);
}
bool qa_material_library_has_source_profile(const qa_material_library *library)
{ return library && library->source_profile; }
bool qa_material_library_source_profile_read(const qa_material_library *library,
    qa_material_profile *out, qa_error *error)
{
    if (!library || !out || !library->source_profile) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material profile requires its actual Source owner"); return false;
    }
    *out = library->profile; return true;
}
bool qa_material_library_set_source_upload(qa_material_library *library,
    qa_material_source_upload_fn producer, void *context, qa_error *error)
{
    if (!producer) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source upload requires its actual producer");
        return false;
    }
    if (!mutation_begin(library, error)) return false;
    bool ok = library->source_profile && (!library->source_upload ||
        (library->source_upload == producer && library->source_upload_context == context));
    if (ok && !qa_scene_source_q3_missing(library->resources)) {
        qa_q3_image_upload_options profile;
        ok = library->count <= 2 && producer(context, false, false, &profile, error) &&
            qa_scene_resources_source_q3_initialize(library->resources, &profile, error);
        if (!ok && (!error || error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source builtin initialization requires its genuine fresh renderer");
        if (ok) for (size_t i = 0; i < library->count; ++i) {
            qa_material *material = &library->ordered[i]->material;
            for (size_t stage = 0; stage < material->stage_count; ++stage) {
                qa_material_stage *s = &material->stages[stage];
                for (size_t image = 0; image < s->image_count; ++image)
                    if (s->images[image] == qa_scene_missing(library->resources)) {
                        qa_scene_image *next = (qa_scene_image *)qa_scene_source_q3_missing(library->resources);
                        qa_scene_image_retain(next); qa_scene_image_release(s->images[image]); s->images[image] = next;
                    }
            }
        }
    }
    if (ok) {
        const qa_scene_image *fog = qa_scene_source_q3_fog(library->resources);
        const qa_scene_image *dlight = qa_scene_source_q3_dlight(library->resources);
        if (fog && dlight) {
            qa_scene_image_retain(fog); qa_scene_image_retain(dlight);
            qa_scene_image_release(library->fog_image); qa_scene_image_release(library->dlight_image);
            library->fog_image = (qa_scene_image *)fog; library->dlight_image = (qa_scene_image *)dlight;
            for (size_t i = 0; i < library->count; ++i) {
                library->ordered[i]->material.fog_image = library->fog_image;
                library->ordered[i]->material.dlight_image = library->dlight_image;
            }
        }
        library->source_upload = producer; library->source_upload_context = context;
    }
    else if (!error || error->code == QA_OK)
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source upload binding requires its actual constructor or restored owner");
    return mutation_end(library, ok);
}
bool qa_material_library_source_upload_is(const qa_material_library *library,
    qa_material_source_upload_fn producer, const void *context)
{
    return library && library->source_profile && producer && library->source_upload == producer &&
        library->source_upload_context == context;
}
bool qa_material_library_set_source_ui_fullscreen(qa_material_library *library,
    qa_material_source_ui_fullscreen_fn producer, void *context, qa_error *error)
{
    if (!producer) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source UI flags require their actual producer"); return false; }
    if (!mutation_begin(library, error)) return false;
    bool ok = library->source_profile && (!library->source_ui_fullscreen ||
        (library->source_ui_fullscreen == producer && library->source_ui_context == context));
    if (ok) { library->source_ui_fullscreen = producer; library->source_ui_context = context; }
    else qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source UI binding differs from its actual constructor or restored owner");
    return mutation_end(library, ok);
}
bool qa_material_library_source_ui_fullscreen_is(const qa_material_library *library,
    qa_material_source_ui_fullscreen_fn producer, const void *context)
{
    return library && library->source_profile && producer && library->source_ui_fullscreen == producer &&
        library->source_ui_context == context;
}

bool qa_material_library_parse(qa_material_library *library, qa_bytes source,
                                const qa_scene_image_options *options, qa_error *error)
{
    (void)options;
    if (!library || (source.size && !source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid shader script bytes");
        return false;
    }
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, catalog_add(library, source, NULL, NULL, error));
}

bool qa_material_library_parse_scoped(qa_material_library *library, qa_bytes source,
    const qa_scene_image_options *scope, qa_error *error)
{
    if (!library || !scope || (source.size && !source.data) || scope->family > QA_SCENE_Q3 ||
        scope->family < QA_SCENE_Q1 || (scope->palette_rgb.size &&
            (scope->palette_rgb.size != 768 || !scope->palette_rgb.data))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Shader dependency scope requires its actual family and RGB palette"); return false;
    }
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, catalog_add(library, source, NULL, scope, error));
}

static int script_compare(const void *left, const void *right)
{
    const char *const *a = left, *const *b = right;
    return strcmp(*a, *b);
}

static bool load_scripts(qa_material_library *library, qa_vfs *vfs,
                                       const qa_scene_image_options *options, qa_error *error)
{
    (void)options;
    if (!library || !vfs) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Shader loading requires a library and VFS");
        return false;
    }
    qa_vfs_listing listing = {0};
    if (!qa_vfs_list(vfs, "scripts", ".shader", &listing, error)) return false;
    if (listing.count > 1) qsort(listing.names, listing.count, sizeof(*listing.names), script_compare);
    for (size_t i = 0; i < listing.count; ++i) {
        const char *name = listing.names[i];
        if (strchr(name, '/') || strchr(name, '\\')) continue;
        size_t length = strlen(name);
        char *path = malloc(length + sizeof("scripts/"));
        if (!path) {
            qa_vfs_listing_free(&listing);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating shader script path");
            return false;
        }
        memcpy(path, "scripts/", 8);
        memcpy(path + 8, name, length + 1);
        qa_resource *resource = NULL;
        bool ok = qa_vfs_acquire(vfs, path, &resource, NULL, error);
        free(path);
        if (ok) ok = catalog_add(library, qa_resource_bytes(resource), resource, NULL, error);
        qa_resource_release(resource);
        if (!ok) { qa_vfs_listing_free(&listing); return false; }
    }
    qa_vfs_listing_free(&listing);
    return true;
}

bool qa_material_library_load_scripts(qa_material_library *library, qa_vfs *vfs,
                                       const qa_scene_image_options *options, qa_error *error)
{
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, load_scripts(library, vfs, options, error));
}

static void registration_rollback(qa_material_library *library, size_t count)
{
    for (size_t bucket = 0; bucket < QA_MATERIAL_BUCKETS; ++bucket) {
        qa_material_record **link = &library->records[bucket];
        while (*link) {
            qa_material_record *record = *link;
            if (record->material.registration >= count) {
                *link = record->next;
                record_free(record);
            } else link = &record->next;
        }
    }
    size_t kept = 0;
    /* The pointer list may contain retired records, so rebuild from live
     * buckets before sorting; never inspect freed registration metadata. */
    for (size_t bucket = 0; bucket < QA_MATERIAL_BUCKETS; ++bucket) {
        for (qa_material_record *record = library->records[bucket]; record; record = record->next) {
            size_t index = kept;
            while (index && (library->ordered[index - 1]->material.sort > record->material.sort ||
                (library->ordered[index - 1]->material.sort == record->material.sort &&
                 library->ordered[index - 1]->material.registration > record->material.registration))) {
                library->ordered[index] = library->ordered[index - 1];
                --index;
            }
            library->ordered[index] = record;
            ++kept;
        }
    }
    library->count = kept;
    for (size_t i = 0; i < kept; ++i) library->ordered[i]->material.sorted_index = (uint32_t)i;
}

static bool remap_material(qa_material_library *library, const char *original,
                        const char *replacement, float offset, qa_error *error)
{
    if (!library || !isfinite(offset)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material remap arguments");
        return false;
    }
    char *from = qa_material_name(original, error);
    char *to = qa_material_name(replacement, error);
    if (!from || !to) { free(from); free(to); return false; }
    qa_material_remap_record **link = &library->remaps;
    while (*link && strcmp((*link)->original, from)) link = &(*link)->next;
    if (!strcmp(from, to)) {
        qa_material_remap_record *old = *link;
        if (old) { *link = old->next; free(old->original); free(old->replacement); free(old); }
        for (size_t i = 0; i < library->count; ++i) {
            qa_material *material = &library->ordered[i]->material;
            if (!library->ordered[i]->source_variant_parent && !strcmp(material->name, from) &&
                (material->remapped || material->source_remap)) {
                material->remapped = NULL;
                material->remap_time_offset = 0;
                material->source_remap = false;
                ++material->revision;
            }
        }
        free(from); free(to);
        return true;
    }
    size_t count = 0;
    for (qa_material_record *record = library->records[qa_material_hash(from)]; record; record = record->next)
        if (!record->source_variant_parent && !strcmp(record->material.name, from)) ++count;
    qa_material_record **sources = count ? malloc(count * sizeof(*sources)) : NULL;
    const qa_material **targets = count ? malloc(count * sizeof(*targets)) : NULL;
    qa_material_remap_record *prepared = calloc(1, sizeof(*prepared));
    if ((count && (!sources || !targets)) || !prepared) {
        free(sources); free(targets); free(prepared); free(from); free(to);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing material remap");
        return false;
    }
    size_t at = 0;
    for (qa_material_record *record = library->records[qa_material_hash(from)]; record; record = record->next)
        if (!record->source_variant_parent && !strcmp(record->material.name, from)) sources[at++] = record;
    size_t registered_before = library->count;
    qa_vec3 sun_light = library->sun_light, sun_direction = library->sun_direction;
    bool had_sun = library->has_sun;
    float sky_height = library->sky_height;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        ok = register_material(library, replacement, &sources[i]->options,
            sources[i]->kind, sources[i]->world_identity, sources[i]->lightmap_index,
            sources[i]->base_name, sources[i]->base_image, &targets[i], error);
        if (ok && targets[i]->default_shader) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Remap target '%s' defaulted", replacement);
            ok = false;
        }
    }
    if (!count) {
        const qa_material *target;
        ok = register_material(library, replacement, NULL, QA_MATERIAL_DYNAMIC, 0, -1, NULL, NULL, &target, error);
        if (ok && target->default_shader) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Remap target '%s' defaulted", replacement);
            ok = false;
        }
    }
    if (!ok) {
        registration_rollback(library, registered_before);
        library->sun_light = sun_light; library->sun_direction = sun_direction;
        library->has_sun = had_sun; library->sky_height = sky_height;
        free(sources); free(targets); free(prepared); free(from); free(to);
        return false;
    }
    prepared->original = from;
    prepared->replacement = to;
    prepared->time_offset = offset;
    prepared->next = *link ? (*link)->next : NULL;
    qa_material_remap_record *old = *link;
    *link = prepared;
    if (old) { free(old->original); free(old->replacement); free(old); }
    for (size_t i = 0; i < count; ++i) {
        sources[i]->material.remapped = targets[i];
        sources[i]->material.remap_time_offset = offset;
        sources[i]->material.source_remap = false;
        ++sources[i]->material.revision;
    }
    free(sources); free(targets);
    return true;
}

bool qa_material_remap(qa_material_library *library, const char *original,
                        const char *replacement, float offset, qa_error *error)
{
    if (!mutation_begin(library, error)) return false;
    return mutation_end(library, remap_material(library, original, replacement, offset, error));
}

static qa_material *source_shader(qa_material_library *library, const char *key)
{
    for (qa_material_record *record = library->records[qa_material_hash(key)]; record; record = record->next)
        if (!record->source_variant_parent && !strcmp(record->material.name, key)) return &record->material;
    return NULL;
}
static bool source_shader_admit(qa_material_library *library, const char *request, const char *key,
    qa_material **out, qa_error *error)
{
    *out = source_shader(library, key);
    if (*out && !(*out)->default_shader) return true;
    const qa_material *registered = NULL;
    if (!register_material(library, request, NULL, QA_MATERIAL_LIGHTMAP, 0, 0, NULL, NULL,
        &registered, error)) return false;
    *out = (qa_material *)registered; return true;
}
bool qa_material_remap_source(qa_material_library *library, const char *original,
    const char *replacement, float offset, qa_material_source_remap_status *status, qa_error *error)
{
    if (!original || !replacement || !status || !isfinite(offset)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Source shader remap arguments"); return false;
    }
    if (!mutation_begin(library, error)) return false;
    char *from = qa_material_name(original, error), *to = qa_material_name(replacement, error);
    qa_material *source = NULL, *target = NULL;
    bool ok = from && to && source_shader_admit(library, original, from, &source, error);
    if (ok && source->default_shader) *status = QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT;
    else if (ok) {
        ok = source_shader_admit(library, replacement, to, &target, error);
        if (ok && target->default_shader) *status = QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT;
        else if (ok) {
            /* Admission may grow the bucket. Qualify all revisions before
             * changing any live binding or the selected target timestamp. */
            for (qa_material_record *record = library->records[qa_material_hash(from)]; record; record = record->next)
                if (!record->source_variant_parent && !strcmp(record->material.name, from) &&
                    record->material.revision == UINT64_MAX) ok = false;
            if (target->revision == UINT64_MAX) ok = false;
            if (!ok) qa_error_set(error, QA_ERROR_MEMORY, 0, "Source shader revisions exhausted");
            else {
                for (qa_material_record *record = library->records[qa_material_hash(from)]; record; record = record->next) {
                    qa_material *material = &record->material;
                    if (record->source_variant_parent || strcmp(material->name, from)) continue;
                    material->remapped = material != target ? target : NULL;
                    material->source_remap = true;
                    ++material->revision;
                }
                target->source_time_offset = offset;
                if (strcmp(target->name, from)) ++target->revision;
                *status = QA_MATERIAL_SOURCE_REMAP_APPLIED;
            }
        }
    }
    free(from); free(to); return mutation_end(library, ok);
}

qa_material_library *qa_material_library_create(qa_scene_resources *resources,
                                                  qa_material_order *order, qa_error *error)
{
    if (!resources || !qa_material_order_idle(order)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material library requires scene resources and shared renderer order");
        return NULL;
    }
    qa_material_library *library = qa_material_library_create_detached(resources, error);
    if (!library) return NULL;
    if (!qa_material_order_retain(order, error)) { qa_material_library_destroy(library); return NULL; }
    library->resources = resources;
    library->order = order;
    library->profile = (qa_material_profile){.detail_textures = true,
        .multitexture = true, .texture_env_add = true};
    uint8_t fog[256 * 32 * 4], dlight[16 * 16 * 4];
    scene_image_fog_pixels(fog);
    scene_image_dlight_pixels(dlight);
    qa_scene_image_level fog_level = {256, 32, fog, sizeof(fog)};
    qa_scene_image_level light_level = {16, 16, dlight, sizeof(dlight)};
    bool ok = qa_scene_image_create(resources, "*fog", QA_SCENE_RGBA8, &fog_level, 1,
        QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){1, 1, 1, 1}, &library->fog_image, error);
    if (ok) ok = qa_scene_image_create(resources, "*dlight", QA_SCENE_RGBA8, &light_level, 1,
        QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 1}, &library->dlight_image, error);
    const qa_material *internal;
    if (ok) ok = qa_material_register_kind(library, "*default", NULL, QA_MATERIAL_DEFAULT, &internal, error);
    if (ok) ok = qa_material_register_kind(library, "<stencil shadow>", NULL, QA_MATERIAL_STENCIL_SHADOW, &internal, error);
    if (!ok) { qa_material_library_destroy(library); return NULL; }
    library->catalog_ready = true;
    return library;
}

bool qa_material_library_retain(qa_material_library *library, qa_error *error)
{
    if (!library || !library->resources || !library->references || library->references == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material library retention requires its actual live owner");
        return false;
    }
    ++library->references;
    return true;
}
const qa_material_record *qa_material_record_resolve(const qa_material_library *library,
                                                     const qa_material *material)
{
    if (!library || !material || material->library != library) return NULL;
    size_t index = material->sorted_index;
    return index < library->count && &library->ordered[index]->material == material
        ? library->ordered[index] : NULL;
}
bool qa_material_registration_read(const qa_material *material, qa_material_registration_kind *out)
{
    const qa_material_library *library = material ? material->library : NULL;
    if (!out) return false;
    const qa_material_record *record = qa_material_record_resolve(library, material);
    if (!record) return false;
    *out = record->kind;
    return true;
}
bool qa_material_retain(const qa_material *material, qa_error *error)
{
    qa_material_library *library = material ? material->library : NULL;
    if (!qa_material_record_resolve(library, material)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material retention requires its actual registered library row");
        return false;
    }
    return qa_material_library_retain(library, error);
}
void qa_material_release(const qa_material *material)
{
    if (material) qa_material_library_destroy(material->library);
}
void qa_material_library_destroy(qa_material_library *library)
{
    if (library && library->references > 1) { --library->references; return; }
    if (!qa_material_library_idle(library)) return;
    for (size_t i = 0; i < QA_MATERIAL_BUCKETS; ++i) {
        qa_material_script *script = library->scripts[i];
        while (script) {
            qa_material_script *next = script->next;
            free(script->name); free(script->text); free(script);
            script = next;
        }
        qa_material_record *record = library->records[i];
        while (record) {
            qa_material_record *next = record->next;
            record_free(record);
            record = next;
        }
    }
    qa_material_remap_record *remap = library->remaps;
    while (remap) {
        qa_material_remap_record *next = remap->next;
        free(remap->original); free(remap->replacement); free(remap);
        remap = next;
    }
    qa_material_generated *generated = library->generated;
    while (generated != NULL) {
        qa_material_generated *next = generated->next;
        free(generated->name); qa_scene_image_release(generated->image); free(generated);
        generated = next;
    }
    free(library->ordered);
    qa_material_catalog_source *source = library->catalog_sources;
    while (source) {
        qa_material_catalog_source *next = source->next;
        qa_resource_release(source->resource);
        free(source->owned_bytes);
        free(source);
        source = next;
    }
    qa_scene_image_release(library->fog_image);
    qa_scene_image_release(library->dlight_image);
    qa_material_order_destroy(library->order);
    qa_scene_resources_destroy(library->resources);
    free(library);
}

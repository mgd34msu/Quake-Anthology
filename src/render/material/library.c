#include "library_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

char *qa_material_name(const char *value, qa_error *error)
{
    if (!value || !*value) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material name is empty");
        return NULL;
    }
    /* COM_StripExtension stops at the first dot. Q_stricmp does not fold
     * separators, even though the source hash function does. */
    size_t length = strcspn(value, ".");
    if (!length || length >= 1024) {
        qa_error_set(error, QA_ERROR_ARGUMENT, length, "Invalid material name length");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material name");
        return NULL;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        copy[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    copy[length] = 0;
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

void qa_material_finish(qa_material *material, int32_t lightmap_index)
{
    float sort = material->sort;
    if (material->sky) sort = 2;
    if (material->polygon_offset && sort == 0) sort = 4;
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
        if (blended(stage) && blended(&material->stages[0])) {
            stage->fog_adjustment = fog_adjustment(stage);
            if (sort == 0) sort = stage->state.depth_write ? 5 : 9;
        }
    }
    if (sort == 0) sort = 3;
    if (material->stage_count > 1 && ((material->profile.vertex_lighting && !material->profile.ui_fullscreen) ||
                                     material->profile.permedia2))
        vertex_lighting_collapse(material, sort, lightmap_index);
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

static void record_free(qa_material_record *record)
{
    qa_material_clear(&record->material);
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
    material->registration = (uint32_t)library->count;
    material->identity = qa_scene_identity();
    material->revision = 1;
    material->fog_image = library->fog_image;
    material->dlight_image = library->dlight_image;
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
        image = (qa_scene_image *)qa_scene_missing(library->resources);
        qa_scene_image_retain(image);
    } else {
        qa_error load_error = {0};
        if (!qa_scene_image_load(library->resources, image_name, &options, &image, &load_error)) {
            if (load_error.code == QA_ERROR_MEMORY) {
                if (error) *error = load_error;
                return false;
            }
            material->default_shader = true;
            image = (qa_scene_image *)qa_scene_missing(library->resources);
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
        base->state.depth_test = QA_DEPTH_ALWAYS;
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
                           qa_scene_white(library->resources), error)) return false;
        base->rgb = QA_COLOR_IDENTITY;
        base->state.blend_source = QA_BLEND_DST_COLOR;
        base->state.blend_destination = QA_BLEND_ZERO;
        base->state.depth_write = false;
    }
    /* Q1/Q2 model lighting is prepared by their scene builders. */
    if (material->family != QA_SCENE_Q3 && kind == QA_MATERIAL_DYNAMIC)
        base->rgb = QA_COLOR_EXACT_VERTEX;
    if (kind == QA_MATERIAL_STENCIL_SHADOW) material->sort = 14;
    qa_material_finish(material, kind == QA_MATERIAL_PICTURE ? -4 : record->lightmap_index);
    return true;
}

static qa_material_remap_record *remap_find(const qa_material_library *library, const char *name)
{
    for (qa_material_remap_record *remap = library->remaps; remap; remap = remap->next)
        if (!strcmp(name, remap->original)) return remap;
    return NULL;
}

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
    char *key = qa_material_name(name, error);
    if (!key) return false;
    unsigned bucket = qa_material_hash(key);
    for (qa_material_record *record = library->records[bucket]; record; record = record->next) {
        if (record->kind == kind && record->world_identity == world_identity &&
            record->lightmap_index == lightmap_index && !strcmp(record->material.name, key) &&
            record->base_image == base_image &&
            same_options(&record->options, &options)) {
            *out = &record->material;
            free(key);
            return true;
        }
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
    bool compiled = script && generated == NULL && kind != QA_MATERIAL_DEFAULT && kind != QA_MATERIAL_STENCIL_SHADOW
        ? qa_material_script_register(library, &record->material,
              (qa_bytes){script->text, script->size}, &options, lightmap_index,
              record->base_name, record->base_image, error)
        : implicit(library, record, name, error);
    if (!compiled) { record_free(record); return false; }
    qa_material_remap_record *remap = remap_find(library, key);
    if (remap) {
        const qa_material *target;
        if (!register_material(library, remap->replacement, &options, kind, world_identity, lightmap_index,
                               record->base_name, record->base_image, &target, error)) {
            record_free(record);
            return false;
        }
        if (!target->default_shader) {
            record->material.remapped = target;
            record->material.remap_time_offset = remap->time_offset;
        }
    }
    if (!publish(library, record, error)) { record_free(record); return false; }
    *out = &record->material;
    return true;
}

bool qa_material_register_kind(qa_material_library *library, const char *name,
                                const qa_scene_image_options *options,
                                qa_material_registration_kind kind,
                                const qa_material **out, qa_error *error)
{
    int32_t index = kind == QA_MATERIAL_LIGHTMAP ? 0 : kind == QA_MATERIAL_WHITE ? -2 :
        kind == QA_MATERIAL_VERTEX ? -3 : kind == QA_MATERIAL_PICTURE ? -4 : -1;
    return register_material(library, name, options, kind, 0, index, NULL, NULL, out, error);
}

bool qa_material_register_world(qa_material_library *library, const char *name,
                                 const qa_scene_image_options *options, uint64_t world_identity,
                                 int32_t lightmap_index, qa_material_registration_kind kind,
                                 const char *base_name, const qa_scene_image *base_image,
                                 const qa_material **out, qa_error *error)
{
    if (!world_identity || lightmap_index < -4) {
        if (out) *out = NULL;
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "World material requires an identity and valid lightmap index");
        return false;
    }
    return register_material(library, name, options, kind, world_identity, lightmap_index,
                              base_name, base_image, out, error);
}

bool qa_material_register(qa_material_library *library, const char *name,
                           const qa_scene_image_options *options, bool lightmapped,
                           const qa_material **out, qa_error *error)
{
    return qa_material_register_kind(library, name, options,
        lightmapped ? QA_MATERIAL_LIGHTMAP : QA_MATERIAL_DYNAMIC, out, error);
}

bool qa_material_register_generated_picture(qa_material_library *library, const char *name,
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
        qa_material_clear(destination); *destination = prepared[i].material;
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
    if (!qa_material_register_kind(library, name, &options, QA_MATERIAL_PICTURE, out, error)) return false;
    generated->picture = *out;
    return true;
}

bool qa_material_library_animate(qa_material_library *library, double seconds,
                                 qa_scene_frame *frame, qa_error *error)
{
    if (library == NULL || frame == NULL || !isfinite(seconds)) {
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
    char *key = qa_material_name(name, NULL);
    if (!key) return NULL;
    const qa_material *result = NULL;
    for (qa_material_record *record = library->records[qa_material_hash(key)]; record; record = record->next)
        if (!strcmp(record->material.name, key) && (!result || record->material.registration < result->registration))
            result = &record->material;
    free(key);
    return result;
}

bool qa_material_has_authored(const qa_material_library *library, const char *name)
{
    if (!library) return false;
    char *key = qa_material_name(name, NULL);
    if (!key) return false;
    const qa_material_script *script = library->scripts[qa_material_hash(key)];
    while (script && strcmp(script->name, key)) script = script->next;
    free(key);
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
    if (!library) return;
    library->video_start = start;
    library->video_context = context;
}

bool qa_material_library_set_profile(qa_material_library *library, const qa_material_profile *profile,
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

bool qa_material_library_parse(qa_material_library *library, qa_bytes source,
                                const qa_scene_image_options *options, qa_error *error)
{
    (void)options;
    if (!library || (source.size && !source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid shader script bytes");
        return false;
    }
    return qa_material_script_catalog(library, source, error);
}

static int script_compare(const void *left, const void *right)
{
    const char *const *a = left, *const *b = right;
    return strcmp(*a, *b);
}

bool qa_material_library_load_scripts(qa_material_library *library, qa_vfs *vfs,
                                       const qa_scene_image_options *options, qa_error *error)
{
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
        if (ok) ok = qa_material_library_parse(library, qa_resource_bytes(resource), options, error);
        qa_resource_release(resource);
        if (!ok) { qa_vfs_listing_free(&listing); return false; }
    }
    qa_vfs_listing_free(&listing);
    return true;
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

bool qa_material_remap(qa_material_library *library, const char *original,
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
            if (!strcmp(material->name, from) && material->remapped) {
                material->remapped = NULL;
                material->remap_time_offset = 0;
                ++material->revision;
            }
        }
        free(from); free(to);
        return true;
    }
    const char *cursor = to;
    for (size_t depth = 0; ; ++depth) {
        if (!strcmp(cursor, from) || depth > QA_MATERIAL_MAX_REGISTERED) {
            free(from); free(to);
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material remap would form a cycle");
            return false;
        }
        const qa_material_remap_record *next = remap_find(library, cursor);
        if (!next) break;
        cursor = next->replacement;
    }
    size_t count = 0;
    for (qa_material_record *record = library->records[qa_material_hash(from)]; record; record = record->next)
        if (!strcmp(record->material.name, from)) ++count;
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
        if (!strcmp(record->material.name, from)) sources[at++] = record;
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
        ok = qa_material_register(library, replacement, NULL, false, &target, error);
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
        ++sources[i]->material.revision;
    }
    free(sources); free(targets);
    return true;
}

qa_material_library *qa_material_library_create(qa_scene_resources *resources, qa_error *error)
{
    if (!resources) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material library requires scene resources");
        return NULL;
    }
    qa_material_library *library = calloc(1, sizeof(*library));
    if (!library) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material library");
        return NULL;
    }
    library->resources = resources;
    library->profile = (qa_material_profile){.detail_textures = true,
        .multitexture = true, .texture_env_add = true};
    uint8_t fog[256 * 32 * 4], dlight[16 * 16 * 4];
    for (size_t y = 0; y < 32; ++y) for (size_t x = 0; x < 256; ++x) {
        size_t at = (y * 256 + x) * 4;
        fog[at] = fog[at + 1] = fog[at + 2] = 255;
        fog[at + 3] = (uint8_t)(255 * qa_material_fog_factor(((float)x + 0.5f) / 256,
                                                          ((float)y + 0.5f) / 32));
    }
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
        float dx = 7.5f - (float)x, dy = 7.5f - (float)y;
        float brightness = fminf(255, truncf(4000 / (dx * dx + dy * dy)));
        uint8_t value = brightness < 75 ? 0 : (uint8_t)brightness;
        size_t at = (y * 16 + x) * 4;
        dlight[at] = dlight[at + 1] = dlight[at + 2] = value;
        dlight[at + 3] = 255;
    }
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
    return library;
}

void qa_material_library_destroy(qa_material_library *library)
{
    if (!library) return;
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
    qa_scene_image_release(library->fog_image);
    qa_scene_image_release(library->dlight_image);
    free(library);
}

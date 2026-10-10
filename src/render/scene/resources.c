#include "resources_internal.h"
#include "qa/scene_resource_save.h"
#include "qa/scene_save.h"
#include "qa/text.h"
#include "qa/material.h"

#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qa_scene_geometry {
    atomic_size_t active, references;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count;
    qa_scene_skeletal_view skeletal;
};

static const char *const format_extensions[] = {".png", ".jpg", ".tga", ".jpeg", ".bmp", ".gif"};
static bool image_from_rgba(qa_scene_resources *, const char *, const qa_image *,
                            const qa_scene_image_options *, qa_scene_image **, qa_error *);
static bool policy_source_image_admitted(qa_scene_resources *, const qa_scene_image *, qa_error *);

struct qa_scene_resources_capture {
    qa_scene_resources *owner;
    const qa_scene_image **images;
    size_t count;
};
qa_scene_image_kind scene_resource_q3_image_kind(qa_q3_texture_format format)
{
    return format == QA_Q3_TEXTURE_RGBA || format == QA_Q3_TEXTURE_RGBA4 || format == QA_Q3_TEXTURE_RGBA8
        ? QA_SCENE_RGBA8 : QA_SCENE_RGB8;
}
bool qa_scene_image_owner_index(const qa_scene_resources *const *owners, size_t count,
    const qa_scene_image *image, size_t *index)
{
    if (!owners || !image || !index) return false;
    size_t at = 0;
    for (size_t owner = 0; owner < count; ++owner) {
        if (!owners[owner]) return false;
        for (const owned_image *entry = owners[owner]->names->images; entry; entry = entry->next) {
            if (&entry->image == image) { *index = at; return true; }
            if (at == SIZE_MAX) return false;
            ++at;
        }
    }
    return false;
}
bool qa_scene_resources_idle(const qa_scene_resources *owner)
{ return owner && !owner->capture && !owner->continuation_active && !owner->policy_pending; }
static bool admission_ready(const qa_scene_resources *owner, qa_error *error)
{
    if (qa_scene_resources_idle(owner)) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene resources are held by a continuation capture");
    return false;
}
static void recipient_correspondence_changed(qa_scene_resources *owner)
{
    /* Exhausted generations leave correspondence reuse disabled. */
    if (owner->recipient_generation) ++owner->recipient_generation;
}
static void recipient_correspondence_remember(qa_scene_resources *owner,
    const qa_scene_image *source, const qa_q3_image_upload_options *profile,
    const qa_scene_image *mapped, bool canonical)
{
    /* The bank's variant roots or this source retain the mapped version. */
    owned_image *image = (owned_image *)source;
    image->recipient_correspondence = mapped;
    image->recipient_correspondence_profile = *profile;
    image->recipient_correspondence_generation = owner->recipient_generation;
    image->recipient_correspondence_canonical = canonical;
}
void scene_resource_alias_free(image_alias *alias)
{
    if (!alias) return;
    free(alias->name); free(alias->request); free(alias->source_path); free(alias->logical_path);
    qa_resource_release(alias->source); qa_resource_release(alias->logical_source); qa_resource_release(alias->palette_source);
    qa_vfs_acquisition_dispose(&alias->source_opening); qa_vfs_acquisition_dispose(&alias->logical_opening);
    qa_vfs_acquisition_dispose(&alias->palette_opening); free(alias);
}
qa_scene_image_alias_source scene_resource_alias_source(const image_alias *alias)
{
    return (qa_scene_image_alias_source){.source = alias->source, .logical_source = alias->logical_source,
        .palette_source = alias->palette_source, .source_opening = alias->source ? &alias->source_opening : NULL,
        .logical_opening = alias->logical_source ? &alias->logical_opening : NULL,
        .palette_opening = alias->palette_source ? &alias->palette_opening : NULL,
        .request = alias->request, .source_path = alias->source_path, .logical_path = alias->logical_path,
        .decode_options = alias->decode_options, .palette_attempted = alias->palette_attempted,
        .palette_error = alias->palette_error, .source_error = alias->source_error};
}
bool qa_scene_resources_set_source_image_admit(qa_scene_resources *resources,
    qa_scene_source_image_admit_fn admit, void *context, qa_error *error)
{
    if (!resources || !admit || !admission_ready(resources, error) ||
        (resources->source_admit && (resources->source_admit != admit || resources->source_admit_context != context))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source image admission requires its retained renderer binding"); return false;
    }
    resources->source_admit = admit; resources->source_admit_context = context; return true;
}
bool qa_scene_resources_source_image_admit_is(const qa_scene_resources *resources,
    qa_scene_source_image_admit_fn admit, const void *context)
{ return resources && admit && resources->source_admit == admit && resources->source_admit_context == context; }
bool qa_scene_image_source_admit(qa_scene_resources *resources, qa_scene_image *image,
    uint32_t unit, qa_error *error)
{
    if (!resources || !image || unit > 1 || qa_scene_image_resource_owner(image) != resources ||
        !admission_ready(resources, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source image admission lost its actual bank or texture unit"); return false;
    }
    if (strlen(image->name) >= 64) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source completed image name exceeds MAX_QPATH"); return false;
    }
    image->source_q3 = true; image->source_texture_unit = unit;
    if (!resources->source_admit) return policy_source_image_admitted(resources, image, error);
    qa_scene_source_image_admit_fn admit = resources->source_admit;
    void *context = resources->source_admit_context;
    if (!admit(context, image, unit, error)) return false;
    if (resources->source_admit != admit || resources->source_admit_context != context ||
        qa_scene_image_resource_owner(image) != resources) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source image admission changed its retained owner"); return false;
    }
    return policy_source_image_admitted(resources, image, error);
}
bool qa_scene_resources_capture_begin(const qa_scene_resources *borrowed,
    qa_scene_resources_capture **out, qa_error *error)
{
    if (!borrowed || !out || *out || !qa_scene_resources_idle(borrowed)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource capture requires an idle owner and empty token"); return false;
    }
    size_t count = borrowed->names->image_count;
    if (count > SIZE_MAX / sizeof(qa_scene_image *)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "resource capture inventory exceeds addressable storage"); return false;
    }
    qa_scene_resources_capture *capture = calloc(1, sizeof(*capture));
    if (!capture) goto failed;
    capture->images = count ? malloc(count * sizeof(*capture->images)) : NULL;
    if (count && !capture->images) { free(capture); goto failed; }
    for (const owned_image *image = borrowed->names->images; image; image = image->next) {
        if (capture->count == count) { free(capture->images); free(capture); goto failed; }
        capture->images[capture->count++] = &image->image;
    }
    if (capture->count != count) { free(capture->images); free(capture); goto failed; }
    for (size_t i = 0; i < count; ++i) qa_scene_image_retain(capture->images[i]);
    capture->owner = (qa_scene_resources *)borrowed;
    capture->owner->capture = capture; *out = capture;
    return true;
failed:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining the actual resource capture inventory"); return false;
}
void qa_scene_resources_capture_end(qa_scene_resources_capture *capture)
{
    if (!capture) return;
    if (capture->owner->capture == capture) capture->owner->capture = NULL;
    for (size_t i = 0; i < capture->count; ++i) qa_scene_image_release(capture->images[i]);
    free(capture->images); free(capture);
}

qa_scene_geometry *qa_scene_geometry_adopt(const qa_scene_geometry_input *input, qa_error *error)
{
    if (!input || (input->vertex_count && !input->vertices) ||
        (input->index_count && !input->indices) ||
        input->vertex_count > SIZE_MAX / sizeof(*input->vertices) ||
        input->index_count > SIZE_MAX / sizeof(*input->indices)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene geometry allocation extents");
        return NULL;
    }
    const qa_scene_skeletal_input *skin = input->skeletal;
    if (skin) {
        if (!skin->bone_count || skin->bone_count > UINT32_MAX ||
            skin->vertex_count > input->vertex_count ||
            skin->vertex_count > SIZE_MAX / sizeof(*skin->ranges) ||
            skin->weight_count > UINT32_MAX || skin->weight_count > SIZE_MAX / sizeof(*skin->weights) ||
            (skin->vertex_count && (!skin->ranges || !skin->sources)) ||
            (skin->weight_count && !skin->weights)) goto invalid_skin;
        for (size_t i = 0; i < skin->vertex_count; ++i) {
            uint32_t source = skin->sources[i];
            if (source >= skin->source_vertex_count) goto invalid_skin;
            qa_model_weight_range range = skin->ranges[source];
            if (range.first > skin->weight_count || range.count > skin->weight_count - range.first)
                goto invalid_skin;
        }
        for (size_t i = 0; i < skin->weight_count; ++i) {
            const qa_model_weight *weight = &skin->weights[i];
            if (weight->bone >= skin->bone_count || !isfinite(weight->bias) ||
                !isfinite(weight->offset[0]) ||
                !isfinite(weight->offset[1]) || !isfinite(weight->offset[2])) goto invalid_skin;
        }
    }
    qa_scene_geometry *geometry = calloc(1, sizeof(*geometry));
    if (!geometry) goto memory;
    if (skin) {
        qa_model_weight *weights = skin->weight_count ? malloc(skin->weight_count * sizeof(*weights)) : NULL;
        qa_model_weight_range *ranges = skin->vertex_count ? malloc(skin->vertex_count * sizeof(*ranges)) : NULL;
        uint32_t *sources = skin->vertex_count ? malloc(skin->vertex_count * sizeof(*sources)) : NULL;
        if ((skin->weight_count && !weights) || (skin->vertex_count && (!ranges || !sources))) {
            free(weights); free(ranges); free(sources); free(geometry); goto memory;
        }
        if (skin->weight_count) memcpy(weights, skin->weights, skin->weight_count * sizeof(*weights));
        for (size_t i = 0; i < skin->vertex_count; ++i) {
            sources[i] = skin->sources[i]; ranges[i] = skin->ranges[sources[i]];
        }
        geometry->skeletal = (qa_scene_skeletal_view){.weights = weights, .ranges = ranges, .sources = sources,
            .vertex_count = skin->vertex_count, .source_vertex_count = skin->source_vertex_count,
            .weight_count = skin->weight_count, .bone_count = skin->bone_count};
    }
    atomic_init(&geometry->active, 1);
    atomic_init(&geometry->references, 1);
    geometry->vertices = input->vertices;
    geometry->indices = input->indices;
    geometry->vertex_count = input->vertex_count;
    geometry->index_count = input->index_count;
    return geometry;
invalid_skin:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene skeletal geometry spans");
    return NULL;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene geometry ownership");
    return NULL;
}

bool qa_scene_geometry_read(const qa_scene_geometry *geometry, qa_scene_geometry_view *out)
{
    if (!out || !qa_scene_geometry_active(geometry)) return false;
    *out = (qa_scene_geometry_view){geometry->vertices,geometry->indices,geometry->vertex_count,geometry->index_count,geometry->skeletal};
    return true;
}

void qa_scene_geometry_cache_retain(const qa_scene_geometry *borrowed)
{
    if (borrowed == NULL) return;
    qa_scene_geometry *geometry = (qa_scene_geometry *)borrowed;
    atomic_fetch_add_explicit(&geometry->references, 1, memory_order_relaxed);
}

void qa_scene_geometry_cache_release(const qa_scene_geometry *borrowed)
{
    if (borrowed == NULL) return;
    qa_scene_geometry *geometry = (qa_scene_geometry *)borrowed;
    if (atomic_fetch_sub_explicit(&geometry->references, 1, memory_order_acq_rel) == 1)
        free(geometry);
}

void qa_scene_geometry_retain(const qa_scene_geometry *borrowed)
{
    if (borrowed == NULL) return;
    qa_scene_geometry *geometry = (qa_scene_geometry *)borrowed;
    qa_scene_geometry_cache_retain(geometry);
    atomic_fetch_add_explicit(&geometry->active, 1, memory_order_relaxed);
}

void qa_scene_geometry_release(const qa_scene_geometry *borrowed)
{
    if (borrowed == NULL) return;
    qa_scene_geometry *geometry = (qa_scene_geometry *)borrowed;
    if (atomic_fetch_sub_explicit(&geometry->active, 1, memory_order_acq_rel) == 1) {
        free(geometry->vertices);
        free(geometry->indices);
        free((void *)geometry->skeletal.weights);
        free((void *)geometry->skeletal.ranges);
        free((void *)geometry->skeletal.sources);
    }
    qa_scene_geometry_cache_release(geometry);
}

bool qa_scene_geometry_active(const qa_scene_geometry *geometry)
{
    return geometry != NULL && atomic_load_explicit(&geometry->active, memory_order_acquire) != 0;
}
void qa_scene_skin_apply(const qa_scene_skinning *skin, const qa_model_vertex *sampled,
                         const qa_scene_vertex *base, qa_scene_vertex *out)
{
    *out = *base;
    out->position = qa_v3(sampled->position[0], sampled->position[1], sampled->position[2]);
    out->normal = qa_v3(sampled->normal[0], sampled->normal[1], sampled->normal[2]);
    if (skin->shell != 0) out->position = qa_vec_add(out->position, qa_vec_scale(out->normal, skin->shell));
    float incoming = qa_vec_dot(out->normal, skin->shade_direction);
    float shade = skin->shade ? 1 + (incoming < 0 ? incoming * .3f : incoming) : 1;
    out->color = skin->tint;
    out->color.x *= skin->light.x * shade;
    out->color.y *= skin->light.y * shade;
    out->color.z *= skin->light.z * shade;
}
static void policy_add_format(qa_scene_image_policy *policy, qa_scene_image_format format)
{
    for (size_t i = 0; i < policy->format_count; ++i)
        if (policy->formats[i] == format) return;
    policy->formats[policy->format_count++] = format;
}

bool qa_scene_image_policy_controls(int32_t override_level, uint32_t usage_mask,
                                    const char *formats, qa_scene_image_policy *out, qa_error *error)
{
    if (formats == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "image policy requires format controls and output");
        return false;
    }
    qa_scene_image_policy policy = {.override_level = override_level, .override_usages = usage_mask};
    qa_bytes text = {(const uint8_t *)formats, strlen(formats)};
    size_t at = 0, first = SIZE_MAX, last = 0;
    uint32_t scalar;
    while (at < text.size) {
        size_t begin = at;
        if (!qa_utf8_next(text, &at, &scalar)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, begin, "image format controls require valid UTF-8"); return false;
        }
        if (!qa_unicode_whitespace(scalar)) {
            if (first == SIZE_MAX) first = begin;
            last = at;
        }
    }
    const char *start = formats + (first == SIZE_MAX ? 0 : first), *end = formats + last;
    if ((size_t)(end-start) == 6) {
        static const char source[] = "source";
        size_t i = 0;
        while (i < 6 && tolower((unsigned char)start[i]) == source[i]) ++i;
        if (i == 6) { policy.source_formats = true; *out = policy; return true; }
    }
    const unsigned char *cursor = (const unsigned char *)formats;
    while (*cursor != 0) {
        for (;;) {
            while (*cursor != 0 && *cursor <= 32) ++cursor;
            if (cursor[0] != '/' || cursor[1] != '/') break;
            while (*cursor != 0 && *cursor != '\n') ++cursor;
        }
        if (*cursor == 0) break;
        bool quoted = *cursor == '"';
        if (quoted) ++cursor;
        char token[129];
        size_t length = 0;
        while (*cursor != 0 && (quoted ? *cursor != '"' : *cursor > 32)) {
            if (length < 128) token[length++] = (char)tolower(*cursor);
            ++cursor;
        }
        if (quoted && *cursor == '"') ++cursor;
        if (!quoted && length == 128) length = 0;
        token[length] = '\0';
        bool exact = false;
        for (size_t i = 0; i < 6; ++i) if (strcmp(token, format_extensions[i]+1) == 0) {
            policy_add_format(&policy, (qa_scene_image_format)i); exact = true; break;
        }
        if (exact) continue;
        for (size_t letter = 0; letter < length; ++letter)
            for (size_t i = 0; i < 6; ++i) if (token[letter] == format_extensions[i][1]) {
                policy_add_format(&policy, (qa_scene_image_format)i); break;
            }
    }
    *out = policy;
    return true;
}

bool qa_scene_resources_set_image_policy(qa_scene_resources *resources, qa_game_family family,
                                         const qa_scene_image_policy *policy, qa_error *error)
{
    if (resources == NULL || family < QA_GAME_Q1 || family > QA_GAME_Q3 ||
        resources->registrations_started || (policy != NULL && policy->format_count > 6)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "image policy must be valid and set before content loads");
        return false;
    }
    if (!admission_ready(resources, error)) return false;
    qa_scene_image_policy copy = {0};
    if (policy != NULL) {
        copy = *policy;
        copy.format_count = 0;
        for (size_t i = 0; i < policy->format_count; ++i) {
            if (policy->formats[i] < QA_SCENE_IMAGE_PNG || policy->formats[i] > QA_SCENE_IMAGE_GIF) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i, "invalid scene image format preference");
                return false;
            }
            policy_add_format(&copy, policy->formats[i]);
        }
    } else if (family == QA_GAME_Q2) {
        if (!qa_scene_image_policy_controls(1, UINT32_MAX, "png jpg tga jpeg bmp gif", &copy, error)) return false;
    }
    resources->policies[family] = copy;
    resources->has_policy[family] = policy != NULL || family == QA_GAME_Q2;
    return true;
}

bool qa_scene_resources_set_fullbright_first(qa_scene_resources *resources, unsigned first, qa_error *error)
{
    if (resources == NULL || first > 256 || resources->registrations_started) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "fullbright range must be set before content loads and start at 0 through 256");
        return false;
    }
    if (!admission_ready(resources, error)) return false;
    resources->fullbright_first = first;
    return true;
}

unsigned qa_scene_resources_fullbright_first(const qa_scene_resources *resources)
{
    return resources == NULL ? 224 : resources->fullbright_first;
}

static atomic_uint_fast64_t next_identity = 1;
uint64_t qa_scene_identity(void)
{
    uint_fast64_t current = atomic_load_explicit(&next_identity, memory_order_relaxed);
    for (;;) {
        if (current == 0 || current >= UINT64_MAX) return 0;
        if (atomic_compare_exchange_weak_explicit(&next_identity, &current, current + 1,
                                                 memory_order_relaxed, memory_order_relaxed))
            return (uint64_t)current;
    }
}

static void names_release(scene_names *names)
{
    if (--names->references != 0) return;
    qa_strings_destroy(names->strings);
    free(names);
}

void qa_scene_image_retain(const qa_scene_image *image)
{
    if (image != NULL) {
        qa_scene_image *owned = (qa_scene_image *)image;
        if (owned->references == SIZE_MAX) abort();
        ++owned->references;
    }
}

void qa_scene_image_release(const qa_scene_image *image)
{
    if (image == NULL) return;
    owned_image *owned = (owned_image *)image;
    if (--owned->image.references != 0) return;
    if (owned->listed) {
        if (owned->previous) owned->previous->next = owned->next;
        else owned->names->images = owned->next;
        if (owned->next) owned->next->previous = owned->previous;
        --owned->names->image_count;
    }
    for (size_t i = 1; i < image->animation_count; ++i)
        qa_scene_image_release(image->animation[i]);
    free((void *)image->animation);
    qa_scene_image_release(owned->sampling_source);
    qa_scene_image_release(owned->source_variant_source);
    while (owned->recipient_bindings) {
        recipient_image_binding *binding = owned->recipient_bindings;
        owned->recipient_bindings = binding->next;
        qa_scene_image_release(binding->source); free(binding);
    }
    qa_scene_resources *parent_owner = owned->source_variant_owner;
    qa_image_free(&owned->recipient_source);
    if (owned->asset) {
        qa_resource_release(owned->asset->source); qa_resource_release(owned->asset->palette_source);
        free(owned->asset->path); free(owned->asset);
    }
    for (size_t i = 0; i < image->level_count; ++i) free((void *)owned->levels[i].pixels);
    free(owned->levels);
    if (--owned->lineage->references == 0) free(owned->lineage);
    names_release(owned->names);
    free(owned);
    qa_scene_resources_destroy(parent_owner);
}

const qa_scene_image *qa_scene_image_at_time(const qa_scene_image *image, double seconds)
{
    if (image == NULL || image->animation_count < 2 || !isfinite(seconds)) return image;
    double beat = seconds * 10.0;
    if (!isfinite(beat)) beat = fmod(seconds, (double)image->animation_count / 10.0) * 10.0;
    beat = floor(beat);
    double wrapped = fmod(beat, (double)image->animation_count);
    if (wrapped < 0) wrapped += (double)image->animation_count;
    size_t index = (size_t)wrapped;
    return index == 0 || index >= image->animation_count ? image : image->animation[index];
}

bool qa_scene_resources_animate(qa_scene_resources *resources, double seconds,
                                qa_scene_frame *frame, qa_error *error)
{
    if (resources == NULL || frame == NULL || !isfinite(seconds)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "image animation requires resources, frame and finite time");
        return false;
    }
    for (size_t i = 0; i < resources->cache_count; ++i) {
        const qa_scene_image *image = resources->cache[i].image;
        if (image->animation_count < 2) continue;
        if (!qa_scene_frame_image(frame, qa_scene_image_at_time(image, seconds), error)) return false;
    }
    return true;
}

static bool level_valid(const qa_scene_image_level *level, qa_error *error)
{
    if (level->width == 0 || level->height == 0 || level->pixels == NULL ||
        (size_t)level->width > (size_t)PTRDIFF_MAX / 4 / level->height ||
        level->bytes != (size_t)level->width * level->height * 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "image level requires complete positive RGBA/depth storage");
        return false;
    }
    return true;
}

bool qa_scene_image_create(qa_scene_resources *resources, const char *name, qa_scene_image_kind kind,
                           const qa_scene_image_level *levels, size_t count, qa_scene_wrap wrap,
                           qa_scene_filter filter, qa_scene_vec4 border, qa_scene_image **out,
                           qa_error *error)
{
    if (resources == NULL || name == NULL || out == NULL || count == 0 || levels == NULL ||
        count > (size_t)PTRDIFF_MAX / sizeof(*levels) || kind < QA_SCENE_RGBA8 || kind > QA_SCENE_DEPTH32F ||
        wrap < QA_SCENE_REPEAT || wrap > QA_SCENE_CLAMP || filter < QA_SCENE_NEAREST ||
        filter > QA_SCENE_LINEAR_MIPMAP_LINEAR || !isfinite(border.x) || !isfinite(border.y) ||
        !isfinite(border.z) || !isfinite(border.w)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene image declaration");
        return false;
    }
    if (!admission_ready(resources, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!level_valid(&levels[i], error)) return false;
        if (i != 0 && (levels[i].width != (levels[i-1].width > 1 ? levels[i-1].width / 2 : 1) ||
                       levels[i].height != (levels[i-1].height > 1 ? levels[i-1].height / 2 : 1))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "scene mip dimensions are not successive levels");
            return false;
        }
    }
    qa_string_id id;
    if (!qa_strings_intern_cstr(resources->names->strings, name, &id, error)) return false;
    owned_image *owned = calloc(1, sizeof(*owned));
    if (owned == NULL) goto allocation_failed;
    owned->levels = calloc(count, sizeof(*owned->levels));
    if (owned->levels == NULL) { free(owned); goto allocation_failed; }
    owned->lineage = malloc(sizeof(*owned->lineage));
    if (owned->lineage == NULL) { free(owned->levels); free(owned); goto allocation_failed; }
    *owned->lineage = (image_lineage){.revision = 1, .references = 1};
    owned->names = resources->names;
    ++owned->names->references;
    owned->image = (qa_scene_image){.identity = qa_scene_identity(), .revision = 1,
        .name = qa_strings_cstr(resources->names->strings, id), .kind = kind, .wrap = wrap,
        .filter = filter, .border = border, .levels = owned->levels, .level_count = count,
        .logical_width = levels[0].width, .logical_height = levels[0].height, .references = 1};
    if (owned->image.identity == 0) {
        qa_scene_image_release(&owned->image);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene resource identities exhausted");
        return false;
    }
    owned->creation_sequence = owned->image.identity;
    for (size_t i = 0; i < count; ++i) {
        void *pixels = malloc(levels[i].bytes);
        if (pixels == NULL) { qa_scene_image_release(&owned->image); goto allocation_failed; }
        memcpy(pixels, levels[i].pixels, levels[i].bytes);
        owned->levels[i] = levels[i];
        owned->levels[i].pixels = pixels;
    }
    *out = &owned->image;
    owned->next = owned->names->images;
    if (owned->next) owned->next->previous = owned;
    owned->names->images = owned; ++owned->names->image_count; owned->listed = true;
    resources->registrations_started = true;
    return true;
allocation_failed:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene image");
    return false;
}
bool qa_scene_image_stream_create(qa_scene_resources *resources, const char *name,
    uint32_t width, uint32_t height, qa_scene_image **out, qa_error *error)
{
    if (!width || !height || width > (size_t)PTRDIFF_MAX / 4 / height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image dimensions"); return false;
    }
    size_t bytes = (size_t)width * height * 4;
    uint8_t *pixels = calloc(1, bytes);
    if (!pixels) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating streamed image"); return false; }
    for (size_t i = 3; i < bytes; i += 4) pixels[i] = 255;
    qa_scene_image_level level = {width, height, pixels, bytes};
    bool ok = qa_scene_image_create(resources, name, QA_SCENE_RGBA8, &level, 1,
        QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 1}, out, error);
    free(pixels);
    if (ok) (*out)->streamed = true;
    return ok;
}

bool scene_image_stream_region_valid(const qa_scene_image *image, qa_scene_rect rect, qa_error *error)
{
    if (!image || !image->streamed || image->level_count != 1 ||
        rect.x < 0 || rect.y < 0 || !rect.width || !rect.height ||
        (uint32_t)rect.x > image->levels[0].width || rect.width > image->levels[0].width - (uint32_t)rect.x ||
        (uint32_t)rect.y > image->levels[0].height || rect.height > image->levels[0].height - (uint32_t)rect.y) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image region"); return false;
    }
    return true;
}

bool qa_scene_image_stream_write(qa_scene_image *image, qa_scene_rect rect,
    const uint8_t *pixels, size_t stride, qa_error *error)
{
    if (!scene_image_stream_region_valid(image, rect, error)) return false;
    if (!pixels || stride < (size_t)rect.width * 4 || stride > (size_t)PTRDIFF_MAX / rect.height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid streamed image rows"); return false;
    }
    uint8_t *destination = (uint8_t *)((owned_image *)image)->levels[0].pixels;
    size_t pitch = (size_t)image->levels[0].width * 4;
    destination += (size_t)rect.y * pitch + (size_t)rect.x * 4;
    for (size_t y = 0; y < rect.height; ++y)
        memcpy(destination + y * pitch, pixels + y * stride, (size_t)rect.width * 4);
    ++image->stream_writes;
    return true;
}

bool qa_scene_resources_bind_embedded_images(qa_scene_resources *resources,
    const qa_scene_embedded_image *images, size_t count, qa_error *error)
{
    if (!resources || (count && !images) || !admission_ready(resources, error) ||
        (resources->embedded_images && (resources->embedded_images != images || resources->embedded_image_count != count))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Embedded images require an idle bank and its immutable asset table"); return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!images[i].name || !*images[i].name || !images[i].png.data || !images[i].png.size) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Embedded image descriptor requires its name and PNG bytes"); return false;
        }
        for (size_t j = 0; j < i; ++j) if (!strcmp(images[i].name, images[j].name)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Embedded image names must be unique"); return false;
        }
    }
    resources->embedded_images = images; resources->embedded_image_count = count;
    return true;
}
bool qa_scene_image_load_embedded(qa_scene_resources *resources, const char *name,
    qa_scene_wrap wrap, qa_scene_filter filter, qa_scene_vec4 border,
    qa_scene_image **out, qa_error *error)
{
    if (!resources || !name || !out || !admission_ready(resources, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Embedded image load requires its actual bank, name and output"); return false;
    }
    const qa_scene_embedded_image *asset = NULL;
    for (size_t i = 0; i < resources->embedded_image_count; ++i)
        if (!strcmp(resources->embedded_images[i].name, name)) { asset = resources->embedded_images + i; break; }
    if (!asset) { qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Embedded image is not bound: %s", name); return false; }
    qa_image decoded = {0};
    qa_scene_image *image = NULL;
    bool ok = qa_image_decode_png(asset->png, &decoded, error) &&
        qa_scene_image_create(resources, name, QA_SCENE_RGBA8,
            &(qa_scene_image_level){decoded.width, decoded.height, decoded.rgba.data, decoded.rgba.size},
            1, wrap, filter, border, &image, error);
    qa_image_free(&decoded);
    if (ok) { ((owned_image *)image)->embedded_png = true; *out = image; }
    return ok;
}
bool qa_scene_resources_images(const qa_scene_resources *resources, qa_arena *scratch,
                               const qa_scene_image *const **out, size_t *count, qa_error *error)
{
    if (!resources || !scratch || !out || !count || resources->names->image_count > SIZE_MAX / sizeof(qa_scene_image *)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene image inventory observation"); return false;
    }
    size_t n = resources->names->image_count;
    const qa_scene_image **rows = n ? qa_arena_alloc(scratch, n * sizeof(*rows), _Alignof(qa_scene_image *), error) : NULL;
    if (n && !rows) return false;
    size_t at = n;
    for (const owned_image *image = resources->names->images; image; image = image->next) rows[--at] = &image->image;
    *out = rows; *count = n; return true;
}

bool qa_scene_image_replace(qa_scene_resources *resources, const qa_scene_image *source,
                            size_t level, const qa_scene_image_level *replacement,
                            qa_scene_image **out, qa_error *error)
{
    if (resources == NULL || source == NULL || replacement == NULL || out == NULL ||
        level >= source->level_count || ((const owned_image *)source)->lineage->revision == UINT64_MAX ||
        ((const owned_image *)source)->lineage->references == SIZE_MAX ||
        replacement->width != source->levels[level].width || replacement->height != source->levels[level].height ||
        source->level_count > (size_t)PTRDIFF_MAX / sizeof(qa_scene_image_level)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene image replacement");
        return false;
    }
    qa_scene_image_level *levels = malloc(source->level_count * sizeof(*levels));
    if (levels == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate image replacement levels");
        return false;
    }
    memcpy(levels, source->levels, source->level_count * sizeof(*levels));
    levels[level] = *replacement;
    qa_scene_image *image = NULL;
    bool ok = qa_scene_image_create(resources, source->name, source->kind, levels, source->level_count,
                                    source->wrap, source->filter, source->border, &image, error);
    free(levels);
    if (!ok) return false;
    if (source->animation_count > 1) {
        if (source->animation_count > (size_t)PTRDIFF_MAX / sizeof(*source->animation)) {
            qa_scene_image_release(image);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "image replacement animation overflow"); return false;
        }
        const qa_scene_image **frames = calloc(source->animation_count, sizeof(*frames));
        if (frames == NULL) {
            qa_scene_image_release(image);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot preserve image animation"); return false;
        }
        for (size_t i = 1; i < source->animation_count; ++i) {
            frames[i] = source->animation[i];
            qa_scene_image_retain(frames[i]);
        }
        image->animation = frames;
        image->animation_count = source->animation_count;
    }
    owned_image *owned = (owned_image *)image;
    free(owned->lineage);
    owned->lineage = ((const owned_image *)source)->lineage;
    ++owned->lineage->references;
    image->identity = source->identity;
    image->revision = ++owned->lineage->revision;
    image->logical_width = source->logical_width;
    image->logical_height = source->logical_height;
    image->texture_mode = source->texture_mode;
    image->source_q3 = source->source_q3; image->source_mipmap = source->source_mipmap;
    image->source_format = source->source_format;
    image->source_texture_unit = source->source_texture_unit;
    image->recipient_upload_pixels = source->recipient_upload_pixels;
    image->recipient_mipmap = source->recipient_mipmap;
    *out = image;
    return true;
}

bool qa_scene_image_sample(qa_scene_resources *resources, const qa_scene_image *source,
                           bool mipmap, qa_scene_wrap wrap, qa_scene_image **out, qa_error *error)
{
    if (resources == NULL || source == NULL || out == NULL || wrap < QA_SCENE_REPEAT || wrap > QA_SCENE_CLAMP) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid image sampling request"); return false;
    }
    qa_scene_filter filter = mipmap ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_LINEAR;
    if (source->wrap == wrap && source->filter == filter && (mipmap || source->level_count == 1)) {
        qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
    }
    qa_scene_image *image = NULL;
    if (!qa_scene_image_create(resources, source->name, source->kind, source->levels,
        mipmap ? source->level_count : 1, wrap, filter, source->border, &image, error)) return false;
    image->logical_width = source->logical_width; image->logical_height = source->logical_height;
    ((owned_image *)image)->embedded_png = ((const owned_image *)source)->embedded_png;
    ((owned_image *)image)->sampling_source = source;
    ((owned_image *)image)->sampling_mipmap = mipmap;
    image->source_q3 = source->source_q3; image->source_mipmap = source->source_mipmap && mipmap;
    image->texture_mode = source->texture_mode && mipmap;
    image->source_format = source->source_format;
    image->source_texture_unit = source->source_texture_unit;
    image->recipient_mipmap = source->recipient_mipmap && mipmap;
    image->recipient_upload_pixels = source->recipient_upload_pixels;
    qa_scene_image_retain(source);
    if (((const owned_image *)source)->asset &&
        !scene_image_asset_copy(image, ((const owned_image *)source)->asset, error)) {
        qa_scene_image_release(image); return false;
    }
    if (source->animation_count > 1) {
        if (source->animation_count > (size_t)PTRDIFF_MAX / sizeof(qa_scene_image *)) {
            qa_scene_image_release(image);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "image sampling animation exceeds address space"); return false;
        }
        const qa_scene_image **frames = calloc(source->animation_count, sizeof(*frames));
        if (frames == NULL) {
            qa_scene_image_release(image);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate sampled image animation"); return false;
        }
        image->animation = frames; image->animation_count = source->animation_count;
        for (size_t i = 1; i < source->animation_count; ++i) {
            const qa_scene_image *frame = source->animation[i];
            qa_scene_image *sampled = NULL;
            if (!qa_scene_image_create(resources, frame->name, frame->kind, frame->levels,
                mipmap ? frame->level_count : 1, wrap, filter, frame->border, &sampled, error)) {
                qa_scene_image_release(image); return false;
            }
            owned_image *owned = (owned_image *)sampled;
            free(owned->lineage); owned->lineage = ((owned_image *)image)->lineage;
            ++owned->lineage->references;
            sampled->identity = image->identity; sampled->revision = ++owned->lineage->revision;
            sampled->logical_width = source->logical_width; sampled->logical_height = source->logical_height;
            sampled->source_q3 = frame->source_q3; sampled->source_mipmap = frame->source_mipmap && mipmap;
            sampled->texture_mode = frame->texture_mode && mipmap;
            sampled->source_format = frame->source_format;
            sampled->source_texture_unit = frame->source_texture_unit;
            sampled->recipient_mipmap = frame->recipient_mipmap && mipmap;
            sampled->recipient_upload_pixels = frame->recipient_upload_pixels;
            owned->embedded_png = ((const owned_image *)frame)->embedded_png;
            owned->sampling_source = frame; owned->sampling_mipmap = mipmap;
            qa_scene_image_retain(frame);
            frames[i] = sampled;
            if (((const owned_image *)frame)->asset &&
                !scene_image_asset_copy(sampled, ((const owned_image *)frame)->asset, error)) {
                qa_scene_image_release(image); return false;
            }
        }
    }
    *out = image; return true;
}

bool qa_scene_image_source_scratch_is(const qa_scene_resources *resources, size_t slot,
    const qa_scene_image *image)
{
    const qa_scene_image *initial = qa_scene_source_q3_scratch(resources, slot);
    return initial && image && qa_scene_image_resource_owner(image) == resources &&
        (image->kind == QA_SCENE_RGBA8 || image->kind == QA_SCENE_RGB8) && image->source_q3 && !image->source_mipmap &&
        !image->animation_count && image->level_count == 1 && image->identity == initial->identity &&
        ((const owned_image *)image)->lineage == ((const owned_image *)initial)->lineage &&
        image->revision && image->revision <= ((const owned_image *)initial)->lineage->revision;
}

bool qa_scene_image_source_scratch_version(qa_scene_resources *resources, size_t slot,
    const qa_scene_image *previous, const qa_image *pixels, qa_scene_image **out, qa_error *error)
{
    const qa_scene_image *initial = qa_scene_source_q3_scratch(resources, slot);
    if (!out || *out || !initial || !pixels || !admission_ready(resources, error) ||
        !qa_scene_image_source_scratch_is(resources, slot, previous) ||
        ((const owned_image *)initial)->lineage->revision == UINT64_MAX ||
        ((const owned_image *)initial)->lineage->references == SIZE_MAX || !pixels->width || !pixels->height ||
        (uint64_t)pixels->width * pixels->height > SIZE_MAX / 4 || !pixels->rgba.data ||
        pixels->rgba.size != (size_t)pixels->width * pixels->height * 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Source cinematic upload requires its actual scratch slot and owned pixels"); return false;
    }
    qa_scene_image_level level = {pixels->width, pixels->height, pixels->rgba.data, pixels->rgba.size};
    qa_scene_image *image = NULL;
    if (!qa_scene_image_create(resources, initial->name, QA_SCENE_RGBA8, &level, 1,
        initial->wrap, QA_SCENE_LINEAR, initial->border, &image, error)) return false;
    owned_image *owned = (owned_image *)image;
    free(owned->lineage); owned->lineage = ((const owned_image *)initial)->lineage;
    ++owned->lineage->references;
    image->identity = initial->identity; image->revision = ++owned->lineage->revision;
    image->source_q3 = true; image->source_texture_unit = initial->source_texture_unit;
    image->source_format = QA_Q3_TEXTURE_RGB8;
    *out = image; return true;
}

qa_scene_resources *qa_scene_resources_create_detached(qa_vfs *vfs, qa_error *error)
{
    qa_scene_resources *resources = calloc(1, sizeof(*resources));
    if (resources == NULL) goto failed;
    resources->names = calloc(1, sizeof(*resources->names));
    if (resources->names == NULL) { free(resources); goto failed; }
    resources->names->references = 1;
    resources->references = 1; resources->names->owner = resources;
    resources->recipient_generation = 1;
    if (!qa_strings_create(&resources->names->strings, error)) {
        free(resources->names); free(resources); return NULL;
    }
    resources->vfs = vfs;
    if (vfs && !qa_vfs_retain(vfs, error)) {
        names_release(resources->names); free(resources); return NULL;
    }
    resources->detached = true;
    return resources;
failed:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene resource service");
    return NULL;
}
qa_scene_resources *qa_scene_resources_create(qa_vfs *vfs, qa_error *error)
{
    qa_scene_resources *resources = qa_scene_resources_create_detached(vfs, error);
    if (!resources) return NULL;
    resources->fullbright_first = 224;
    if (!qa_scene_resources_set_image_policy(resources, QA_GAME_Q2, NULL, error)) {
        qa_scene_resources_destroy(resources); return NULL;
    }
    const uint8_t white[4] = {255,255,255,255};
    qa_scene_image_level level = {1,1,white,sizeof(white)};
    if (!qa_scene_image_create(resources, "*white", QA_SCENE_RGBA8, &level, 1, QA_SCENE_REPEAT,
                               QA_SCENE_NEAREST, (qa_scene_vec4){1,1,1,1}, &resources->white, error)) {
        qa_scene_resources_destroy(resources); return NULL;
    }
    uint8_t pixels[16 * 16 * 4];
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
        uint8_t value = x == 0 || x == 15 || y == 0 || y == 15 ? 255 : 32;
        size_t offset = (y * 16 + x) * 4;
        pixels[offset] = value; pixels[offset+1] = value; pixels[offset+2] = value; pixels[offset+3] = 255;
    }
    qa_image missing = {.width = 16, .height = 16, .rgba = {pixels,sizeof(pixels)}};
    qa_scene_image_options missing_options = {.family = QA_GAME_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_NEAREST, .mipmap = true};
    if (!image_from_rgba(resources, "*default", &missing, &missing_options, &resources->missing, error)) {
        qa_scene_resources_destroy(resources); return NULL;
    }
    resources->missing->border = (qa_scene_vec4){0,0,0,1};
    resources->registrations_started = false;
    resources->detached = false;
    return resources;
}

qa_vfs *qa_scene_resources_files(const qa_scene_resources *resources)
{
    return resources ? resources->vfs : NULL;
}

bool qa_scene_resources_retain(qa_scene_resources *resources, qa_error *error)
{
    if (!resources || !resources->references || resources->references == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual image bank"); return false;
    }
    ++resources->references; return true;
}
qa_scene_resources *qa_scene_image_owner(const qa_scene_image *image)
{
    if (!image) return NULL;
    const owned_image *owned = (const owned_image *)image;
    return owned->listed && owned->names ? owned->names->owner : NULL;
}
qa_scene_resources *qa_scene_image_resource_owner(const qa_scene_image *image)
{ return qa_scene_image_owner(image); }
size_t qa_scene_resources_parent_count(const qa_scene_resources *resources)
{
    size_t count = 0;
    for (const owned_image *image = resources ? resources->names->images : NULL; image; image = image->next)
        if (image->source_variant_owner) ++count;
    return count;
}
bool qa_scene_resources_parent_at(const qa_scene_resources *resources, size_t index, qa_scene_resources **out)
{
    if (!resources || !out) return false;
    for (const owned_image *image = resources->names->images; image; image = image->next) {
        if (!image->source_variant_owner) continue;
        if (index) { --index; continue; }
        *out = image->source_variant_owner; return true;
    }
    return false;
}
void qa_scene_resources_destroy(qa_scene_resources *resources)
{
    if (resources && resources->references > 1) { --resources->references; return; }
    if (!qa_scene_resources_idle(resources)) return;
    resources->names->owner = NULL;
    while (resources->variants) {
        owned_image *image = resources->variants;
        resources->variants = image->variant_next;
        image->variant_next = NULL; qa_scene_image_release(&image->image);
    }
    for (size_t i = 0; i < resources->cache_count; ++i) {
        qa_scene_image_release(resources->cache[i].image);
        qa_resource_release(resources->cache[i].source_record);
        qa_resource_release(resources->cache[i].logical_record);
        qa_resource_release(resources->cache[i].palette_source);
        free(resources->cache[i].logical_path);
        qa_vfs_acquisition_dispose(&resources->cache[i].source_opening);
        qa_vfs_acquisition_dispose(&resources->cache[i].logical_opening);
        qa_vfs_acquisition_dispose(&resources->cache[i].palette_opening);
    }
    free(resources->cache);
    while (resources->aliases) {
        image_alias *alias = resources->aliases; resources->aliases = alias->next; scene_resource_alias_free(alias);
    }
    for (size_t i = 0; i < 3; ++i) {
        qa_buffer_free(&resources->palettes[i]);
        qa_resource_release(resources->palette_resources[i]);
        qa_vfs_acquisition_dispose(&resources->palette_openings[i]);
    }
    qa_scene_image_release(resources->white);
    qa_scene_image_release(resources->missing);
    qa_scene_image_release(resources->source_white); qa_scene_image_release(resources->source_missing);
    qa_scene_image_release(resources->source_identity);
    for (unsigned i = 0; i < 32; ++i) qa_scene_image_release(resources->source_scratch[i]);
    qa_scene_image_release(resources->source_dlight); qa_scene_image_release(resources->source_fog);
    names_release(resources->names);
    qa_vfs_destroy(resources->vfs);
    free(resources);
}

typedef struct recipient_policy_binding {
    struct recipient_policy_binding *next;
    owned_image *root;
    recipient_image_binding *binding;
} recipient_policy_binding;
struct qa_scene_resource_policy {
    qa_scene_resources *owner, *destination;
    qa_vfs *lookup;
    const qa_scene_image **images;
    qa_scene_image **mapped;
    bool *mapping;
    qa_scene_resource_policy **dependencies;
    size_t dependency_count;
    qa_string_id *names;
    size_t count;
    qa_q3_image_upload_options source_upload;
    const qa_scene_image **source_images;
    size_t source_image_count;
    recipient_policy_binding *recipient_bindings;
    bool source_restart, sealed, published;
};

static bool policy_error(qa_error *error, const char *text)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text);
    return false;
}

static bool policy_held(const qa_scene_resource_policy *ticket)
{
    return ticket && ticket->owner && ticket->destination &&
        ticket->owner->policy_pending == ticket && !ticket->owner->capture &&
        !ticket->owner->continuation_active;
}
static bool policy_current(const qa_scene_resource_policy *ticket)
{ return policy_held(ticket) && qa_vfs_lookup_equal(ticket->owner->vfs, ticket->lookup); }

static bool policy_source_image_admitted(qa_scene_resources *resources,
    const qa_scene_image *image, qa_error *error)
{
    qa_scene_resource_policy *ticket = resources->policy_source ? resources->policy_source->policy_pending : NULL;
    if (!ticket || ticket->destination != resources) return true;
    if (!policy_held(ticket) || ticket->sealed || ticket->published)
        return policy_error(error, "Prepared Source image admission lost its actual mutable destination");
    for (size_t i = 0; i < ticket->source_image_count; ++i)
        if (ticket->source_images[i] == image) return true;
    if (!((const owned_image *)image)->creation_sequence ||
        ticket->source_image_count == SIZE_MAX / sizeof(*ticket->source_images))
        return policy_error(error, "Prepared Source image creation inventory is exhausted");
    const qa_scene_image **rows = realloc(ticket->source_images,
        (ticket->source_image_count + 1) * sizeof(*rows));
    if (!rows) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining completed Source image admissions"); return false; }
    ticket->source_images = rows; rows[ticket->source_image_count++] = image;
    qa_scene_image_retain(image); return true;
}
bool qa_scene_resource_policy_source_image_count(const qa_scene_resource_policy *ticket,
    size_t *out, qa_error *error)
{
    if (!out || !policy_held(ticket) || ticket->published)
        return policy_error(error, "Source image inventory requires its actual prepared bank");
    *out = ticket->source_image_count; return true;
}
bool qa_scene_resource_policy_source_image_at(const qa_scene_resource_policy *ticket, size_t index,
    const qa_scene_image **out, uint64_t *sequence, qa_error *error)
{
    if (!out || !sequence || !policy_held(ticket) || ticket->published || index >= ticket->source_image_count)
        return policy_error(error, "Source image admission ordinal is absent from its actual prepared bank");
    const qa_scene_image *image = ticket->source_images[index];
    if (!image->source_q3 || qa_scene_image_resource_owner(image) != ticket->destination ||
        !((const owned_image *)image)->creation_sequence)
        return policy_error(error, "Source image admission lost its actual completed image");
    *out = image; *sequence = ((const owned_image *)image)->creation_sequence; return true;
}

static void policy_dispose(qa_scene_resource_policy *ticket)
{
    while (ticket->recipient_bindings) {
        recipient_policy_binding *row = ticket->recipient_bindings;
        ticket->recipient_bindings = row->next;
        if (row->binding) { qa_scene_image_release(row->binding->source); free(row->binding); }
        qa_scene_image_release(&row->root->image); free(row);
    }
    ticket->owner->policy_pending = NULL;
    ticket->destination->policy_pending = NULL;
    qa_scene_resources_destroy(ticket->destination);
    for (size_t i = 0; i < ticket->source_image_count; ++i) qa_scene_image_release(ticket->source_images[i]);
    free(ticket->source_images);
    for (size_t i = 0; i < ticket->count; ++i) {
        qa_scene_image_release(ticket->mapped[i]); qa_scene_image_release(ticket->images[i]);
    }
    qa_vfs_destroy(ticket->lookup);
    free(ticket->images); free(ticket->mapped); free(ticket->mapping);
    free(ticket->dependencies); free(ticket->names); free(ticket);
}

static bool cache_add(qa_scene_resources *, qa_string_id, qa_resource *, qa_mount_id,
    qa_resource *, qa_mount_id, const qa_vfs_acquisition *, const qa_vfs_acquisition *, const char *,
    const qa_scene_image_load_receipt *, const qa_scene_image_options *, bool, qa_scene_image *, qa_error *);

static bool policy_prepare(qa_scene_resources *owner,
    const qa_scene_image_policy policies[3], const qa_q3_image_upload_options *source_upload,
    qa_scene_resource_policy **out, qa_error *error)
{
    if (!out || *out || !policies || !qa_scene_resources_idle(owner) || !owner->vfs ||
        !owner->white || !owner->missing || owner->names->image_count > SIZE_MAX / sizeof(qa_scene_image *))
        return policy_error(error, "Image policy preparation requires the idle actual resource bank");
    qa_scene_resource_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining prepared image policy"); return false;
    }
    ticket->owner = owner;
    ticket->source_restart = source_upload != NULL;
    if (source_upload) ticket->source_upload = *source_upload;
    ticket->destination = qa_scene_resources_create_detached(owner->vfs, error);
    ticket->lookup = qa_vfs_clone(owner->vfs, error);
    ticket->images = owner->names->image_count ? malloc(owner->names->image_count * sizeof(*ticket->images)) : NULL;
    ticket->mapped = owner->names->image_count ? calloc(owner->names->image_count, sizeof(*ticket->mapped)) : NULL;
    ticket->mapping = owner->names->image_count ? calloc(owner->names->image_count, sizeof(*ticket->mapping)) : NULL;
    if (!ticket->destination || !ticket->lookup || (owner->names->image_count &&
        (!ticket->images || !ticket->mapped || !ticket->mapping))) {
        qa_scene_resources_destroy(ticket->destination); qa_vfs_destroy(ticket->lookup);
        free(ticket->images); free(ticket->mapped); free(ticket->mapping); free(ticket);
        if (error && !error->code) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual image inventory");
        return false;
    }
    for (owned_image *image = owner->names->images; image; image = image->next) {
        ticket->images[ticket->count++] = &image->image;
        qa_scene_image_retain(&image->image);
    }
    owner->policy_pending = ticket;
    qa_scene_resources *destination = ticket->destination;
    destination->embedded_images = owner->embedded_images;
    destination->embedded_image_count = owner->embedded_image_count;
    destination->fullbright_first = owner->fullbright_first;
    destination->white = owner->white; qa_scene_image_retain(destination->white);
    destination->policy_source = owner;
    destination->missing = owner->missing; qa_scene_image_retain(destination->missing);
    if (!source_upload) {
        destination->source_builtins = owner->source_builtins;
        destination->source_builtins_upload = owner->source_builtins_upload;
        destination->source_white = owner->source_white; qa_scene_image_retain(destination->source_white);
        destination->source_missing = owner->source_missing; qa_scene_image_retain(destination->source_missing);
        destination->source_identity = owner->source_identity; qa_scene_image_retain(destination->source_identity);
        for (unsigned i = 0; i < 32; ++i) {
            destination->source_scratch[i] = owner->source_scratch[i]; qa_scene_image_retain(destination->source_scratch[i]);
        }
        destination->source_dlight = owner->source_dlight; qa_scene_image_retain(destination->source_dlight);
        destination->source_fog = owner->source_fog; qa_scene_image_retain(destination->source_fog);
    }
    bool ok = true;
    for (size_t i = 0; ok && i < qa_strings_count(owner->names->strings); ++i) {
        qa_string_id id = 0;
        ok = qa_strings_intern(destination->names->strings,
            qa_strings_text(owner->names->strings, (qa_string_id)(i + 1)), &id, error) && id == i + 1;
    }
    for (unsigned family = 0; ok && family < 3; ++family) {
        const qa_scene_image_policy *policy = source_upload && !owner->has_policy[family] ? NULL : &policies[family];
        ok = qa_scene_resources_set_image_policy(destination, (qa_game_family)family, policy, error);
        if (ok && owner->palettes[family].size) {
            destination->palettes[family].data = malloc(owner->palettes[family].size);
            if (!destination->palettes[family].data) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining installed image palette"); ok = false;
            } else {
                destination->palettes[family].size = owner->palettes[family].size;
                memcpy(destination->palettes[family].data, owner->palettes[family].data, owner->palettes[family].size);
                if (owner->palette_resources[family]) {
                    ok = qa_vfs_acquisition_copy(&owner->palette_openings[family],
                        &destination->palette_openings[family], error);
                    if (ok) {
                        destination->palette_resources[family] = owner->palette_resources[family];
                        qa_resource_retain(destination->palette_resources[family]);
                    }
                }
            }
        }
    }
    if (ok && source_upload && owner->source_builtins)
        ok = qa_scene_resources_source_q3_initialize(destination, source_upload, error);
    for (const image_alias *alias = owner->aliases; ok && alias; alias = alias->next) {
        qa_scene_image_alias_source source = scene_resource_alias_source(alias);
        ok = qa_scene_image_alias_bind(destination, alias->name, &source, error);
    }
    for (size_t i = 0; ok && !source_upload && i < owner->cache_count; ++i) {
        const image_cache *entry = owner->cache + i;
        if (!entry->options.source_q3) continue;
        qa_scene_image_options options = entry->options;
        if (options.palette_rgb.size) options.palette_rgb.data = entry->palette;
        if (options.translation.size) options.translation.data = entry->translation;
        qa_scene_image_load_receipt observation = {.palette_source = entry->palette_source,
            .palette_opening = entry->palette_opening, .palette_attempted = entry->palette_attempted,
            .palette_error = entry->palette_error};
        ok = cache_add(destination, entry->name, entry->source_record, entry->source_mount,
            entry->logical_record, entry->logical_mount, &entry->source_opening, &entry->logical_opening,
            entry->logical_path, &observation, &options, entry->exact_file, entry->image, error);
    }
    for (size_t i = 0; ok && i < owner->cache_count; ++i) {
        qa_scene_image *image = NULL;
        ok = qa_scene_resource_policy_image(ticket, owner->cache[i].image, &image, error);
        qa_scene_image_release(image);
    }
    if (!ok) { policy_dispose(ticket); return false; }
    *out = ticket; return true;
}

bool qa_scene_resource_policy_prepare(qa_scene_resources *owner,
    const qa_scene_image_policy policies[3], qa_scene_resource_policy **out, qa_error *error)
{ return policy_prepare(owner, policies, NULL, out, error); }

bool qa_scene_resource_policy_prepare_source_restart(qa_scene_resources *owner,
    const qa_q3_image_upload_options *upload, qa_scene_resource_policy **out, qa_error *error)
{
    if (!owner || !upload) return policy_error(error, "Source restart requires its actual bank and physical upload profile");
    if (!qa_q3_image_upload_options_valid(upload, error)) return false;
    return policy_prepare(owner, owner->policies, upload, out, error);
}

bool qa_scene_resource_policy_source_restart_read(const qa_scene_resource_policy *ticket,
    qa_q3_image_upload_options *out)
{
    if (!out || !policy_current(ticket) || !ticket->source_restart) return false;
    *out = ticket->source_upload; return true;
}

static qa_q3_image_upload_options policy_source_upload(const qa_scene_resource_policy *ticket,
    const qa_q3_image_upload_options *original)
{
    qa_q3_image_upload_options upload = ticket->source_restart ? ticket->source_upload : *original;
    upload.mipmap = original->mipmap;
    upload.allow_picmip = original->allow_picmip;
    return upload;
}

qa_scene_resources *qa_scene_resource_policy_destination(const qa_scene_resource_policy *ticket)
{ return policy_current(ticket) && !ticket->published ? ticket->destination : NULL; }
qa_scene_resources *qa_scene_resource_policy_source(const qa_scene_resource_policy *ticket)
{ return policy_held(ticket) ? ticket->owner : NULL; }

bool qa_scene_resource_policy_dependencies(qa_scene_resource_policy *ticket,
    qa_scene_resource_policy *const *banks, size_t count, qa_error *error)
{
    if (!policy_current(ticket) || ticket->sealed || ticket->dependencies || !banks || !count ||
        count > SIZE_MAX / sizeof(*ticket->dependencies))
        return policy_error(error, "Image dependencies require the actual open bank preparations");
    bool self = false;
    for (size_t i = 0; i < count; ++i) {
        if (!policy_current(banks[i]) || banks[i]->sealed)
            return policy_error(error, "Image dependency lost its actual bank preparation");
        if (banks[i] == ticket) self = true;
        for (size_t j = 0; j < i; ++j) if (banks[j]->owner == banks[i]->owner)
            return policy_error(error, "Image dependencies repeat a resource owner");
    }
    if (!self) return policy_error(error, "Image dependencies omit their actual bank");
    ticket->dependencies = malloc(count * sizeof(*ticket->dependencies));
    if (!ticket->dependencies) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual sampled-image bank dependencies"); return false;
    }
    memcpy(ticket->dependencies, banks, count * sizeof(*banks)); ticket->dependency_count = count; return true;
}

static bool policy_parent_image(qa_scene_resource_policy *ticket, const qa_scene_image *image,
    qa_scene_image **out, qa_error *error)
{
    for (size_t i = 0; i < ticket->count; ++i)
        if (ticket->images[i] == image) return qa_scene_resource_policy_image(ticket, image, out, error);
    for (size_t i = 0; i < ticket->dependency_count; ++i) {
        qa_scene_resource_policy *bank = ticket->dependencies[i];
        if (!policy_current(bank)) return policy_error(error, "Sampled image lost its actual parent bank");
        for (size_t j = 0; j < bank->count; ++j)
            if (bank->images[j] == image) return qa_scene_resource_policy_image(bank, image, out, error);
    }
    return policy_error(error, "Sampled image parent is absent from the actual resource roster");
}

bool qa_scene_resource_policy_dependency_image(qa_scene_resource_policy *ticket, const qa_scene_image *image,
    qa_scene_image **out, qa_error *error)
{
    if (!out || *out || !image || !policy_current(ticket) || ticket->sealed)
        return policy_error(error, "Image dependency mapping requires its actual open bank roster");
    return policy_parent_image(ticket, image, out, error);
}

bool qa_scene_resource_policy_image(qa_scene_resource_policy *ticket, const qa_scene_image *image,
    qa_scene_image **out, qa_error *error)
{
    if (out) *out = NULL;
    if (!out || !image || !policy_current(ticket) || ticket->sealed)
        return policy_error(error, "Image rebinding requires the unsealed actual policy preparation");
    size_t ordinal = 0;
    for (; ordinal < ticket->count && ticket->images[ordinal] != image; ++ordinal) {}
    if (ordinal == ticket->count) return policy_error(error, "Image does not belong to the retained source resource bank");
    if (ticket->mapped[ordinal]) {
        qa_scene_image_retain(ticket->mapped[ordinal]); *out = ticket->mapped[ordinal]; return true;
    }
    if (ticket->mapping[ordinal]) return policy_error(error, "Sampled image provenance contains a cycle");
    ticket->mapping[ordinal] = true;
    bool ok = true, found = false;
    if (ticket->source_restart && ticket->owner->source_builtins) {
        if (image == ticket->owner->source_white) *out = ticket->destination->source_white;
        else if (image == ticket->owner->source_missing) *out = ticket->destination->source_missing;
        else if (image == ticket->owner->source_identity) *out = ticket->destination->source_identity;
        else if (image == ticket->owner->source_dlight) *out = ticket->destination->source_dlight;
        else if (image == ticket->owner->source_fog) *out = ticket->destination->source_fog;
        else for (unsigned i = 0; i < 32; ++i)
            if (image == ticket->owner->source_scratch[i]) { *out = ticket->destination->source_scratch[i]; break; }
        if (*out) { qa_scene_image_retain(*out); found = true; }
        if (!found) for (size_t i = 0; i < 32; ++i) {
            const qa_scene_image *initial = ticket->owner->source_scratch[i];
            if (!initial || ((const owned_image *)initial)->lineage != ((const owned_image *)image)->lineage)
                continue;
            const qa_scene_image *next = ticket->destination->source_scratch[i];
            if (!next || !image->source_q3 || image->source_mipmap || image->level_count != 1 ||
                image->animation_count || image->kind == QA_SCENE_DEPTH32F || !image->revision ||
                image->revision > ((const owned_image *)image)->lineage->revision ||
                ((const owned_image *)next)->lineage->references == SIZE_MAX) {
                ok = policy_error(error, "Retained cinematic version lost its actual scratch lineage");
                found = true; break;
            }
            ok = qa_scene_image_create(ticket->destination, next->name, image->kind, image->levels,
                image->level_count, next->wrap, image->filter, next->border, out, error);
            if (ok) {
                owned_image *mapped = (owned_image *)*out;
                free(mapped->lineage); mapped->lineage = ((const owned_image *)next)->lineage;
                ++mapped->lineage->references;
                if (mapped->lineage->revision < ((const owned_image *)image)->lineage->revision)
                    mapped->lineage->revision = ((const owned_image *)image)->lineage->revision;
                mapped->image.identity = next->identity; mapped->image.revision = image->revision;
                mapped->image.logical_width = image->logical_width; mapped->image.logical_height = image->logical_height;
                mapped->image.source_q3 = true; mapped->image.source_texture_unit = next->source_texture_unit;
                mapped->image.source_format = image->source_format;
            }
            found = true; break;
        }
    }
    for (size_t i = 0; !found && i < ticket->owner->cache_count; ++i) {
        const image_cache *entry = &ticket->owner->cache[i];
        if (entry->image != image) continue;
        if (entry->options.source_q3 && !ticket->source_restart) {
            qa_scene_image_retain(image); *out = (qa_scene_image *)image;
            found = true; break;
        }
        qa_scene_image_options options = entry->options;
        if (options.source_q3) options.source_upload = policy_source_upload(ticket, &options.source_upload);
        if (options.palette_rgb.size) options.palette_rgb.data = entry->palette;
        if (options.translation.size) options.translation.data = entry->translation;
        const char *name = qa_strings_cstr(ticket->owner->names->strings, entry->name);
        ok = entry->exact_file ? qa_scene_image_load_exact(ticket->destination, name, &options, out, error) :
            qa_scene_image_load(ticket->destination, name, &options, out, error);
        found = true; break;
    }
    const owned_image *owned = (const owned_image *)image;
    if (!found && owned->source_variant_source) {
        qa_scene_image *parent = NULL;
        qa_q3_image_upload_options upload = policy_source_upload(ticket, &owned->source_variant_upload);
        ok = policy_parent_image(ticket, owned->source_variant_source, &parent, error);
        if (ok) ok = owned->generic_variant ?
            qa_scene_image_generic_variant(ticket->destination, parent, owned->generic_variant_mipmap, out, error) :
            qa_scene_image_source_q3_variant(ticket->destination, parent, &upload, out, error);
        if (ok && owned->recipient_first_upload && ((owned_image *)*out)->source_variant_source &&
            !((owned_image *)*out)->recipient_first_upload) {
            ((owned_image *)*out)->recipient_first_upload = true;
            recipient_correspondence_changed(ticket->destination);
        }
        qa_scene_image_release(parent); found = true;
    }
    if (!found && owned->sampling_source) {
        if (image->source_q3 && !ticket->source_restart) {
            qa_scene_image_retain(image); *out = (qa_scene_image *)image; found = true;
        }
    }
    if (!found && owned->sampling_source) {
        qa_scene_image *parent = NULL;
        ok = policy_parent_image(ticket, owned->sampling_source, &parent, error);
        if (ok) ok = qa_scene_image_sample(ticket->destination, parent, owned->sampling_mipmap, image->wrap, out, error);
        qa_scene_image_release(parent); found = true;
    }
    /* GIF cache entries own the first frame. A retained frame binding follows
     * that exact cache parent's newly decoded frame, never a name lookup. */
    for (size_t i = 0; !found && ok && i < ticket->owner->cache_count; ++i) {
        const qa_scene_image *parent = ticket->owner->cache[i].image;
        for (size_t frame = 1; frame < parent->animation_count; ++frame) {
            if (parent->animation[frame] != image) continue;
            qa_scene_image *prepared = NULL;
            ok = qa_scene_resource_policy_image(ticket, parent, &prepared, error);
            if (ok && frame >= prepared->animation_count)
                ok = policy_error(error, "A retained GIF frame is absent from the newly decoded source");
            if (ok) { *out = (qa_scene_image *)prepared->animation[frame]; qa_scene_image_retain(*out); }
            qa_scene_image_release(prepared); found = true; break;
        }
    }
    if (!found && ok) { qa_scene_image_retain(image); *out = (qa_scene_image *)image; }
    ticket->mapping[ordinal] = false;
    if (ok) { ticket->mapped[ordinal] = *out; qa_scene_image_retain(*out); }
    return ok;
}

bool qa_scene_resource_policy_ready_is(const qa_scene_resource_policy *ticket)
{
    return policy_current(ticket) && ticket->sealed && !ticket->published &&
        ticket->destination->policy_pending == ticket && !ticket->destination->capture &&
        !ticket->destination->continuation_active;
}

static bool same_options(const image_cache *, const qa_scene_image_options *);
static bool recipient_recipe_same(const qa_scene_resources *before, const qa_scene_image *source,
    const qa_scene_resources *after, const qa_scene_image *mapped)
{
    if (source == mapped) return true;
    const owned_image *a = (const owned_image *)source, *b = (const owned_image *)mapped;
    if (a->sampling_source && b->sampling_source)
        return a->sampling_mipmap == b->sampling_mipmap && source->wrap == mapped->wrap &&
            recipient_recipe_same(before, a->sampling_source, after, b->sampling_source);
    const image_cache *old = NULL, *next = NULL;
    for (size_t i = 0; i < before->cache_count; ++i)
        if (before->cache[i].image == source) { old = before->cache + i; break; }
    for (size_t i = 0; i < after->cache_count; ++i)
        if (after->cache[i].image == mapped) { next = after->cache + i; break; }
    if (!old || !next) return false;
    qa_scene_image_options options = next->options;
    if (options.palette_rgb.size) options.palette_rgb.data = next->palette;
    if (options.translation.size) options.translation.data = next->translation;
    return old->source_record == next->source_record && old->source_mount == next->source_mount &&
        old->logical_record == next->logical_record && old->logical_mount == next->logical_mount &&
        old->palette_source == next->palette_source && old->palette_attempted == next->palette_attempted &&
        old->palette_error == next->palette_error && old->exact_file == next->exact_file &&
        !strcmp(qa_strings_cstr(before->names->strings, old->name),
            qa_strings_cstr(after->names->strings, next->name)) && same_options(old, &options);
}

static bool recipient_binding_prepare(qa_scene_resource_policy *ticket, owned_image *root,
    const qa_scene_image *source, qa_error *error)
{
    qa_scene_image *mapped = NULL;
    if (!qa_scene_resource_policy_image(ticket, source, &mapped, error)) return false;
    if (source == mapped || !recipient_recipe_same(ticket->owner, source, ticket->destination, mapped)) {
        qa_scene_image_release(mapped); return true;
    }
    for (recipient_policy_binding *row = ticket->recipient_bindings; row; row = row->next)
        if (row->root == root && row->binding->source == mapped) { qa_scene_image_release(mapped); return true; }
    for (recipient_image_binding *binding = root->recipient_bindings; binding; binding = binding->next)
        if (binding->source == mapped) { qa_scene_image_release(mapped); return true; }
    recipient_policy_binding *row = calloc(1, sizeof(*row));
    recipient_image_binding *binding = calloc(1, sizeof(*binding));
    if (!row || !binding) {
        free(row); free(binding); qa_scene_image_release(mapped);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining first recipient upload correspondence"); return false;
    }
    binding->source = mapped; row->root = root; row->binding = binding;
    qa_scene_image_retain(&root->image);
    row->next = ticket->recipient_bindings; ticket->recipient_bindings = row; return true;
}

bool qa_scene_resource_policy_ready(qa_scene_resource_policy *ticket, qa_error *error)
{
    if (!policy_current(ticket) || ticket->published)
        return policy_error(error, "Prepared image policy lost its actual resource or lookup owner");
    if (ticket->sealed) return qa_scene_resource_policy_ready_is(ticket);
    if (!ticket->source_restart) {
        for (owned_image *root = ticket->owner->variants; root; root = root->variant_next) {
            if (!root->recipient_first_upload) continue;
            if (!recipient_binding_prepare(ticket, root, root->source_variant_source, error)) return false;
            for (recipient_image_binding *binding = root->recipient_bindings; binding; binding = binding->next)
                if (!recipient_binding_prepare(ticket, root, binding->source, error)) return false;
        }
    }
    scene_names *names = ticket->owner->names, *destination = ticket->destination->names;
    if (!qa_scene_resources_idle(ticket->destination) ||
        destination->image_count > SIZE_MAX - names->image_count ||
        destination->image_count > SIZE_MAX - names->references ||
        ticket->count > SIZE_MAX / sizeof(*ticket->names))
        return policy_error(error, "Prepared image inventory cannot transfer to the retained owner");
    ticket->names = ticket->count ? malloc(ticket->count * sizeof(*ticket->names)) : NULL;
    if (ticket->count && !ticket->names) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing retained image name bindings"); return false;
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        const char *name = ticket->images[i]->name;
        ticket->names[i] = qa_strings_find(destination->strings, (qa_bytes){(const uint8_t *)name, strlen(name)});
        if (!ticket->names[i]) {
            free(ticket->names); ticket->names = NULL;
            return policy_error(error, "Prepared names omit an actual retained image");
        }
    }
    ticket->destination->policy_pending = ticket;
    ticket->sealed = true; return true;
}

void qa_scene_resource_policy_publish(qa_scene_resource_policy *ticket)
{
    if (!qa_scene_resource_policy_ready_is(ticket)) return;
    qa_scene_resources *owner = ticket->owner, *destination = ticket->destination;
    recipient_correspondence_changed(owner);
    recipient_correspondence_changed(destination);
    for (recipient_policy_binding *row = ticket->recipient_bindings; row; row = row->next) {
        row->binding->next = row->root->recipient_bindings;
        row->root->recipient_bindings = row->binding; row->binding = NULL;
    }
    qa_strings *strings = owner->names->strings;
    owner->names->strings = destination->names->strings;
    destination->names->strings = strings;
    for (size_t i = 0; i < ticket->count; ++i)
        ((qa_scene_image *)ticket->images[i])->name = qa_strings_cstr(owner->names->strings, ticket->names[i]);
    while (destination->names->images) {
        owned_image *image = destination->names->images;
        destination->names->images = image->next;
        if (image->next) image->next->previous = NULL;
        --destination->names->image_count; --destination->names->references;
        if (image->source_variant_owner == owner) {
            image->source_variant_owner = NULL;
            qa_scene_resources_destroy(owner);
        }
        image->names = owner->names; ++owner->names->references;
        image->recipient_correspondence = NULL;
        image->recipient_correspondence_generation = 0;
        image->previous = NULL; image->next = owner->names->images;
        if (image->next) image->next->previous = image;
        owner->names->images = image; ++owner->names->image_count;
    }
    image_cache *cache = owner->cache; size_t count = owner->cache_count, capacity = owner->cache_capacity;
    owner->cache = destination->cache; owner->cache_count = destination->cache_count;
    owner->cache_capacity = destination->cache_capacity;
    destination->cache = cache; destination->cache_count = count; destination->cache_capacity = capacity;
    image_alias *aliases = owner->aliases; owner->aliases = destination->aliases; destination->aliases = aliases;
    owned_image *variants = owner->variants;
    if (!ticket->source_restart) {
        owned_image **link = &variants;
        while (*link) {
            owned_image *image = *link;
            if (!image->recipient_first_upload) { link = &image->variant_next; continue; }
            *link = image->variant_next;
            image->variant_next = destination->variants; destination->variants = image;
        }
    }
    owner->variants = destination->variants; destination->variants = variants;
    if (ticket->source_restart && owner->source_builtins) {
        qa_scene_image *white = owner->source_white, *missing = owner->source_missing, *identity = owner->source_identity;
        owner->source_white = destination->source_white; owner->source_missing = destination->source_missing;
        owner->source_identity = destination->source_identity;
        destination->source_white = white; destination->source_missing = missing; destination->source_identity = identity;
        for (unsigned i = 0; i < 32; ++i) {
            qa_scene_image *image = owner->source_scratch[i]; owner->source_scratch[i] = destination->source_scratch[i];
            destination->source_scratch[i] = image;
        }
        qa_scene_image *dlight = owner->source_dlight, *fog = owner->source_fog;
        owner->source_dlight = destination->source_dlight; owner->source_fog = destination->source_fog;
        destination->source_dlight = dlight; destination->source_fog = fog;
        qa_q3_image_upload_options upload = owner->source_builtins_upload;
        owner->source_builtins_upload = destination->source_builtins_upload;
        destination->source_builtins_upload = upload;
    }
    for (unsigned family = 0; family < 3; ++family) {
        if (!owner->palettes[family].size) {
            owner->palettes[family] = destination->palettes[family];
            destination->palettes[family] = (qa_buffer){0};
            owner->palette_resources[family] = destination->palette_resources[family];
            destination->palette_resources[family] = NULL;
            owner->palette_openings[family] = destination->palette_openings[family];
            destination->palette_openings[family] = (qa_vfs_acquisition){0};
        }
        owner->policies[family] = destination->policies[family]; owner->has_policy[family] = destination->has_policy[family];
    }
    owner->registrations_started = owner->registrations_started || destination->registrations_started;
    ticket->published = true;
}

bool qa_scene_resource_policy_finish(qa_scene_resource_policy **owner, qa_error *error)
{
    if (!owner || !*owner || !policy_held(*owner) || !(*owner)->published)
        return policy_error(error, "Image policy retirement requires its published actual owner");
    policy_dispose(*owner); *owner = NULL; return true;
}

bool qa_scene_resource_policy_abort(qa_scene_resource_policy **owner, qa_error *error)
{
    if (!owner || !*owner || !policy_held(*owner) || (*owner)->published)
        return policy_error(error, "Image policy abort requires its retained unpublished owner");
    policy_dispose(*owner); *owner = NULL; return true;
}

const qa_scene_image *qa_scene_white(const qa_scene_resources *resources) { return resources->white; }
const qa_scene_image *qa_scene_missing(const qa_scene_resources *resources) { return resources->missing; }
const qa_scene_image *qa_scene_source_q3_white(const qa_scene_resources *resources)
{ return resources && resources->source_builtins ? resources->source_white : NULL; }
const qa_scene_image *qa_scene_source_q3_missing(const qa_scene_resources *resources)
{ return resources && resources->source_builtins ? resources->source_missing : NULL; }
const qa_scene_image *qa_scene_source_q3_dlight(const qa_scene_resources *resources)
{ return resources ? resources->source_dlight : NULL; }
const qa_scene_image *qa_scene_source_q3_fog(const qa_scene_resources *resources)
{ return resources ? resources->source_fog : NULL; }
const qa_scene_image *qa_scene_source_q3_scratch(const qa_scene_resources *resources, size_t index)
{ return resources && index < 32 ? resources->source_scratch[index] : NULL; }

bool qa_scene_resources_palette_read(const qa_scene_resources *resources, qa_game_family family, qa_bytes *out)
{
    if (!resources || !out || family < QA_GAME_Q1 || family > QA_GAME_Q3) return false;
    const qa_buffer *stored = &resources->palettes[family];
    if (!stored->data || !stored->size) return false;
    *out = (qa_bytes){stored->data, stored->size};
    return true;
}

static bool palette_admit(qa_scene_resources *resources, qa_game_family family,
    qa_bytes *out, qa_scene_image_load_receipt *observation, qa_error *error)
{
    if (resources == NULL || out == NULL || family < QA_GAME_Q1 || family > QA_GAME_Q3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene palette request"); return false;
    }
    qa_buffer *stored = &resources->palettes[family];
    if (stored->size != 0) {
        if (observation && resources->palette_resources[family]) {
            if (!qa_vfs_acquisition_copy(&resources->palette_openings[family], &observation->palette_opening, error)) return false;
            observation->palette_source = resources->palette_resources[family];
            qa_resource_retain(observation->palette_source); observation->palette_attempted = true;
        }
        *out = (qa_bytes){stored->data, stored->size}; return true;
    }
    if (!admission_ready(resources, error)) return false;
    if (observation) observation->palette_attempted = true;
    if (resources->vfs == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene has no mounted palette source"); return false;
    }
    qa_resource *resource = NULL; qa_vfs_acquisition opening = {0};
    if (!qa_vfs_acquire_receipt(resources->vfs, family == QA_GAME_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx",
                        &resource, &opening, error)) return false;
    if (observation) {
        if (!qa_vfs_acquisition_copy(&opening, &observation->palette_opening, error)) {
            qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); return false;
        }
        observation->palette_source = resource; qa_resource_retain(resource);
    }
    qa_bytes bytes = qa_resource_bytes(resource);
    uint8_t *palette = malloc(768);
    if (palette == NULL) {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene palette"); return false;
    }
    bool ok = true;
    if (family == QA_GAME_Q1) {
        if (bytes.size != 768) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 palette requires exactly 256 RGB colors"); ok = false; }
        else memcpy(palette, bytes.data, 768);
    } else {
        qa_image decoded = {0};
        ok = qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &decoded, error);
        if (ok && decoded.palette.size < 1024) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 colormap has no complete palette"); ok = false;
        }
        if (ok) for (size_t i = 0; i < 256; ++i) memcpy(palette + i * 3, decoded.palette.data + i * 4, 3);
        qa_image_free(&decoded);
    }
    if (!ok) { free(palette); qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); return false; }
    *stored = (qa_buffer){palette,768};
    resources->palette_resources[family] = resource; resources->palette_openings[family] = opening;
    *out = (qa_bytes){palette,768};
    return true;
}
static bool palette_observed(qa_scene_resources *resources, qa_game_family family,
    qa_bytes *out, qa_scene_image_load_receipt *observation, qa_error *error)
{
    qa_error local = {0};
    bool ok = palette_admit(resources, family, out, observation, &local);
    if (observation && observation->palette_attempted) observation->palette_error = ok ? QA_OK : local.code;
    if (!ok && error) *error = local;
    return ok;
}
bool qa_scene_resources_palette(qa_scene_resources *resources, qa_game_family family,
    qa_bytes *out, qa_error *error)
{ return palette_observed(resources, family, out, NULL, error); }
bool qa_scene_resources_palette_source_read(const qa_scene_resources *resources,
    qa_game_family family, qa_scene_palette_source *out)
{
    if (!resources || !out || family < QA_GAME_Q1 || family > QA_GAME_Q3 ||
        resources->palettes[family].size != 768 || !resources->palette_resources[family]) return false;
    const qa_vfs_acquisition *opening = &resources->palette_openings[family];
    if (!opening->opening_present || opening->resource_id != qa_resource_id(resources->palette_resources[family])) return false;
    *out = (qa_scene_palette_source){resources->palette_resources[family], opening};
    return true;
}

static bool suffix_equal(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    if (n < s) return false;
    for (size_t i = 0; i < s; ++i)
        if (tolower((unsigned char)name[n-s+i]) != tolower((unsigned char)suffix[i])) return false;
    return true;
}

static void alpha_edge_fill(qa_image *image, qa_game_family family, const uint8_t *fallback)
{
    uint8_t *pixels = image->rgba.data;
    size_t width = image->width, height = image->height, count = width * height;
    for (size_t i = 0; i < count; ++i) if (pixels[i * 4 + 3] == 0) {
        uint8_t *destination = pixels + i * 4;
        if (family == QA_GAME_Q2) {
            size_t adjacent[4] = {i > width ? i - width : SIZE_MAX,
                i < count - width ? i + width : SIZE_MAX, i > 0 ? i - 1 : SIZE_MAX,
                i + 1 < count ? i + 1 : SIZE_MAX};
            const uint8_t *color = fallback;
            for (size_t n = 0; n < 4; ++n)
                if (adjacent[n] != SIZE_MAX && pixels[adjacent[n] * 4 + 3] != 0) {
                    color = pixels + adjacent[n] * 4;
                    break;
                }
            if (color) memcpy(destination, color, 3);
        } else {
            size_t x = i % width, y = i / width;
            size_t columns[3] = {x ? x - 1 : width - 1, x, x + 1 < width ? x + 1 : 0};
            size_t rows[3] = {y ? y - 1 : height - 1, y, y + 1 < height ? y + 1 : 0};
            unsigned sum[3] = {0}, neighbors = 0;
            for (size_t row = 0; row < 3; ++row) for (size_t column = 0; column < 3; ++column) {
                if (row == 1 && column == 1) continue;
                const uint8_t *color = pixels + (rows[row] * width + columns[column]) * 4;
                if (color[3] == 0) continue;
                for (size_t channel = 0; channel < 3; ++channel) sum[channel] += color[channel];
                ++neighbors;
            }
            if (neighbors) for (size_t channel = 0; channel < 3; ++channel)
                destination[channel] = (uint8_t)(sum[channel] / neighbors);
        }
    }
}

static bool alpha_mask_mixed(const qa_image *image)
{
    bool visible = false, rejected = false;
    size_t count = (size_t)image->width * image->height;
    for (size_t i = 0; i < count; ++i) {
        /* GT666 passes byte alpha 170 and above. */
        if (image->rgba.data[i * 4 + 3] >= 170) visible = true;
        else rejected = true;
        if (visible && rejected) return true;
    }
    return false;
}

static bool image_mip_chain(const qa_image *source, bool cutout,
    qa_mip_chain *chain, qa_error *error)
{
    if (!qa_image_mip_chain(source, QA_MIP_BOX, chain, error)) return false;
    if (cutout && alpha_mask_mixed(source)) {
        size_t retained = 0;
        while (retained < chain->count && alpha_mask_mixed(chain->levels + retained)) ++retained;
        for (size_t i = retained; i < chain->count; ++i) qa_image_free(chain->levels + i);
        chain->count = retained;
    }
    return true;
}

static bool image_uses_texture_mode(const qa_scene_image_options *options)
{
    bool mipmap = options->source_q3 ? options->source_upload.mipmap : options->mipmap;
    return mipmap && options->filter >= QA_SCENE_NEAREST_MIPMAP_NEAREST &&
        (options->usage == QA_IMAGE_USAGE_DEFAULT || options->usage == QA_IMAGE_USAGE_SKIN ||
         options->usage == QA_IMAGE_USAGE_WALL);
}

static bool image_from_rgba_complete(qa_scene_resources *resources, const char *name, const qa_image *source,
    const qa_scene_image_options *options, bool after_border, qa_scene_vec4 upload_border,
    bool dlight, qa_scene_image **out, qa_error *error)
{
    if (options->source_q3) {
        if (strlen(name) >= 64) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source completed image name exceeds MAX_QPATH"); return false;
        }
        qa_mip_chain uploaded = {0};
        qa_q3_texture_format format;
        if (!qa_q3_image_upload_format(source, &options->source_upload, &uploaded, &format, error)) return false;
        qa_scene_image_level *levels = calloc(uploaded.count, sizeof(*levels));
        if (!levels) {
            qa_mip_chain_free(&uploaded);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual Source upload levels"); return false;
        }
        for (size_t i = 0; i < uploaded.count; ++i) {
            qa_image *image = uploaded.levels + i;
            if (options->family == QA_GAME_Q1 && options->transparent)
                alpha_edge_fill(image, QA_GAME_Q1, NULL);
            levels[i] = (qa_scene_image_level){image->width, image->height, image->rgba.data, image->rgba.size};
        }
        qa_scene_image_kind kind = scene_resource_q3_image_kind(format);
        qa_scene_filter filter = options->source_upload.mipmap ? options->filter : QA_SCENE_LINEAR;
        bool ok = qa_scene_image_create(resources, name, kind, levels, uploaded.count,
            options->wrap, filter, (qa_scene_vec4){0}, out, error);
        if (ok) {
            (*out)->logical_width = source->width; (*out)->logical_height = source->height;
            owned_image *owned = (owned_image *)*out;
            owned->recipient_source.width = source->width;
            owned->recipient_source.height = source->height;
            owned->recipient_source.rgba.data = malloc(source->rgba.size);
            if (!owned->recipient_source.rgba.data) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Source upload pixels"); ok = false;
            } else {
                memcpy(owned->recipient_source.rgba.data, source->rgba.data, source->rgba.size);
                owned->recipient_source.rgba.size = source->rgba.size;
            }
            (*out)->source_q3 = true; (*out)->source_mipmap = options->source_upload.mipmap;
            (*out)->texture_mode = image_uses_texture_mode(options);
            (*out)->source_format = format;
            (*out)->source_after_upload_border = after_border; (*out)->source_upload_border = upload_border;
            (*out)->source_dlight = dlight;
            if (ok) ok = qa_scene_image_source_admit(resources, *out, 0, error);
            if (!ok) { qa_scene_image_release(*out); *out = NULL; }
        }
        free(levels); qa_mip_chain_free(&uploaded); return ok;
    }
    qa_image scaled = {0};
    const qa_image *base = source;
    if (options->family == QA_GAME_Q2 && options->mipmap) {
        qa_gamma_options gamma = {.profile = QA_GAMMA_Q2, .gamma = 1, .intensity = 2};
        if (!qa_image_apply_gamma(source, &gamma, &scaled, error)) return false;
        base = &scaled;
    }
    qa_mip_chain chain = {0};
    if (options->mipmap && !image_mip_chain(base,
        options->family == QA_GAME_Q1 && options->transparent, &chain, error)) {
        qa_image_free(&scaled); return false;
    }
    size_t count = chain.count + 1;
    qa_scene_image_level *levels = calloc(count, sizeof(*levels));
    if (levels == NULL) {
        qa_mip_chain_free(&chain); qa_image_free(&scaled);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate upload mip descriptors"); return false;
    }
    levels[0] = (qa_scene_image_level){base->width,base->height,base->rgba.data,base->rgba.size};
    for (size_t i = 0; i < chain.count; ++i) {
        qa_image *image = &chain.levels[i];
        if (options->family == QA_GAME_Q1 && options->transparent)
            alpha_edge_fill(image, QA_GAME_Q1, NULL);
        levels[i+1] = (qa_scene_image_level){image->width,image->height,image->rgba.data,image->rgba.size};
    }
    bool ok = qa_scene_image_create(resources, name, QA_SCENE_RGBA8, levels, count, options->wrap,
                                    options->filter, (qa_scene_vec4){0}, out, error);
    if (ok) (*out)->texture_mode = image_uses_texture_mode(options);
    free(levels); qa_mip_chain_free(&chain); qa_image_free(&scaled);
    return ok;
}
static bool image_from_rgba(qa_scene_resources *resources, const char *name, const qa_image *source,
    const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{ return image_from_rgba_complete(resources, name, source, options, false, (qa_scene_vec4){0}, false, out, error); }

static bool source_builtin(qa_scene_resources *resources, const char *name,
    const qa_q3_image_upload_options *profile, uint32_t size, uint8_t value,
    bool missing, bool scratch, qa_scene_image **out, qa_error *error)
{
    uint8_t pixels[16 * 16 * 4];
    for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) {
        uint8_t pixel = missing ? (x == 0 || x == 15 || y == 0 || y == 15 ? 255 : 32) : value;
        size_t at = ((size_t)y * size + x) * 4;
        pixels[at] = pixels[at + 1] = pixels[at + 2] = pixel;
        pixels[at + 3] = missing ? pixel : 255;
    }
    qa_image image = {.width = size, .height = size, .rgba = {pixels, (size_t)size * size * 4}};
    qa_scene_image_options options = {.family = QA_GAME_Q3, .wrap = scratch ? QA_SCENE_CLAMP : QA_SCENE_REPEAT,
        .filter = missing ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_LINEAR,
        .mipmap = missing, .source_q3 = true, .source_upload = *profile};
    options.source_upload.mipmap = missing; options.source_upload.allow_picmip = scratch;
    return image_from_rgba(resources, name, &image, &options, out, error);
}
void scene_image_dlight_pixels(uint8_t pixels[16 * 16 * 4])
{
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
        float dx = 7.5f - (float)x, dy = 7.5f - (float)y;
        float brightness = fminf(255, truncf(4000 / (dx * dx + dy * dy)));
        uint8_t value = brightness < 75 ? 0 : (uint8_t)brightness;
        size_t at = ((size_t)y * 16 + x) * 4;
        pixels[at] = pixels[at + 1] = pixels[at + 2] = value; pixels[at + 3] = 255;
    }
}
void scene_image_fog_pixels(uint8_t pixels[256 * 32 * 4])
{
    for (uint32_t y = 0; y < 32; ++y) for (uint32_t x = 0; x < 256; ++x) {
        size_t at = ((size_t)y * 256 + x) * 4;
        pixels[at] = pixels[at + 1] = pixels[at + 2] = 255;
        pixels[at + 3] = (uint8_t)(255 * qa_material_fog_factor(((float)x + .5f) / 256, ((float)y + .5f) / 32));
    }
}
bool scene_resource_source_builtin_create(qa_scene_resources *resources, const char *name,
    const qa_q3_image_upload_options *profile, qa_scene_image **out, qa_error *error)
{
    if (!strcmp(name,"*default")) return source_builtin(resources,name,profile,16,32,true,false,out,error);
    if (!strcmp(name,"*white")) return source_builtin(resources,name,profile,8,255,false,false,out,error);
    if (!strcmp(name,"*identityLight") || !strcmp(name,"*scratch")) {
        qa_q3_color_lighting lighting;
        if (!qa_q3_color_lighting_read(&profile->color.device,profile->color.requested_overbright_bits,&lighting,error)) return false;
        bool scratch=!strcmp(name,"*scratch");
        return source_builtin(resources,name,profile,scratch?16:8,lighting.identity_light_byte,false,scratch,out,error);
    }
    qa_scene_image_options options={.family=QA_GAME_Q3,.wrap=QA_SCENE_CLAMP,
        .filter=QA_SCENE_LINEAR,.source_q3=true,.source_upload=*profile};
    options.source_upload.allow_picmip=false; options.source_upload.mipmap=false;
    if (!strcmp(name,"*dlight")) {
        uint8_t pixels[16*16*4]; scene_image_dlight_pixels(pixels);
        qa_image input={.width=16,.height=16,.rgba={pixels,sizeof(pixels)}};
        return image_from_rgba_complete(resources,name,&input,&options,false,(qa_scene_vec4){0},true,out,error);
    }
    if (!strcmp(name,"*fog")) {
        uint8_t pixels[256*32*4]; scene_image_fog_pixels(pixels);
        qa_image input={.width=256,.height=32,.rgba={pixels,sizeof(pixels)}};
        return image_from_rgba_complete(resources,name,&input,&options,true,(qa_scene_vec4){1,1,1,1},false,out,error);
    }
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unknown Source constructor image"); return false;
}
bool qa_scene_resources_source_q3_initialize(qa_scene_resources *resources,
    const qa_q3_image_upload_options *profile, qa_error *error)
{
    if (!resources || !profile) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source builtin images require their actual bank and upload profile"); return false;
    }
    if (!admission_ready(resources, error) || !qa_q3_image_upload_options_valid(profile, error)) return false;
    if (resources->source_builtins) return true;
    qa_scene_image *white=NULL,*missing=NULL,*identity=NULL,*scratch[32]={0},*dlight=NULL,*fog=NULL;
    bool ok=scene_resource_source_builtin_create(resources,"*default",profile,&missing,error) &&
        scene_resource_source_builtin_create(resources,"*white",profile,&white,error) &&
        scene_resource_source_builtin_create(resources,"*identityLight",profile,&identity,error);
    for (unsigned i=0;ok && i<32;++i)
        ok=scene_resource_source_builtin_create(resources,"*scratch",profile,&scratch[i],error);
    if (ok) ok=scene_resource_source_builtin_create(resources,"*dlight",profile,&dlight,error);
    if (ok) resources->source_dlight=dlight;
    if (ok) ok=scene_resource_source_builtin_create(resources,"*fog",profile,&fog,error);
    if (!ok) {
        resources->source_dlight=NULL;
        qa_scene_image_release(white); qa_scene_image_release(missing); qa_scene_image_release(identity);
        for (unsigned i=0;i<32;++i) qa_scene_image_release(scratch[i]);
        qa_scene_image_release(dlight); qa_scene_image_release(fog); return false;
    }
    resources->source_white=white; resources->source_missing=missing; resources->source_identity=identity;
    memcpy(resources->source_scratch,scratch,sizeof(scratch)); resources->source_fog=fog;
    resources->source_builtins_upload=*profile; resources->source_builtins=true; return true;
}

void scene_image_skin_flood(uint8_t *pixels, uint32_t width, uint32_t height,
    uint8_t fill, uint8_t black, size_t *queue)
{
    size_t head = 0, tail = 1; queue[0] = 0; pixels[0] = 255;
    while (head < tail) {
        size_t pixel = queue[head++], x = pixel % width, y = pixel / width;
        size_t adjacent[4] = {x > 0 ? pixel-1 : SIZE_MAX, x+1 < width ? pixel+1 : SIZE_MAX,
            y > 0 ? pixel-width : SIZE_MAX, y+1 < height ? pixel+width : SIZE_MAX};
        uint8_t color = black;
        for (size_t i = 0; i < 4; ++i) {
            size_t next = adjacent[i]; if (next == SIZE_MAX) continue;
            uint8_t value = pixels[next];
            if (value == fill) { pixels[next] = 255; queue[tail++] = next; }
            else if (value != 255) color = value;
        }
        pixels[pixel] = color;
    }
}
static bool flood_skin(qa_image *image, qa_bytes palette, qa_error *error)
{
    if (image->indices.size == 0) return true;
    uint8_t fill = image->indices.data[0], black = 0;
    for (size_t i = 0; i < 256; ++i) if (palette.data[i*3] == 0 && palette.data[i*3+1] == 0 && palette.data[i*3+2] == 0) { black = (uint8_t)i; break; }
    if (fill == black || fill == 255) return true;
    if (image->indices.size > (size_t)PTRDIFF_MAX / sizeof(size_t)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "skin flood queue overflow"); return false;
    }
    size_t *queue = malloc(image->indices.size * sizeof(*queue));
    if (queue == NULL) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate skin flood queue"); return false; }
    scene_image_skin_flood(image->indices.data, image->width, image->height, fill, black, queue);
    free(queue); return true;
}

typedef struct image_decode_context {
    qa_scene_image_load_receipt *observation;
    const image_alias *alias;
    const qa_resource *source;
    bool generic_upload;
} image_decode_context;
static bool decode_palette(qa_scene_resources *resources, qa_game_family family,
    const image_decode_context *context, uint8_t *storage, qa_bytes *out, qa_error *error)
{
    const image_alias *alias = context ? context->alias : NULL;
    if (!alias)
        return palette_observed(resources, family, out, context ? context->observation : NULL, error);
    if (!alias->palette_attempted) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained indexed image lacks its actual palette attempt"); return false;
    }
    if (!alias->palette_source) {
        qa_error_set(error, alias->palette_error, 0, "Retained source palette lookup failed"); return false;
    }
    qa_bytes bytes = qa_resource_bytes(alias->palette_source);
    if (family == QA_GAME_Q1) {
        if (bytes.size != 768) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 palette requires exactly 256 RGB colors"); return false;
        }
        memcpy(storage, bytes.data, 768);
    } else {
        qa_image decoded = {0};
        bool ok = qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &decoded, error);
        if (ok && decoded.palette.size < 1024) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 colormap has no complete palette"); ok = false;
        }
        if (ok) for (size_t i = 0; i < 256; ++i) memcpy(storage + i * 3, decoded.palette.data + i * 4, 3);
        qa_image_free(&decoded); if (!ok) return false;
    }
    *out = (qa_bytes){storage,768}; return true;
}
static bool indexed_rgba(qa_scene_resources *resources, qa_image *image,
    const qa_scene_image_options *options, bool pcx, const image_decode_context *context, qa_error *error)
{
    qa_bytes palette = options->palette_rgb;
    uint8_t local_palette[768];
    bool q2_indexed = pcx && options->family == QA_GAME_Q2 &&
        (options->usage == QA_IMAGE_USAGE_SKIN || options->usage == QA_IMAGE_USAGE_SPRITE);
    if (q2_indexed && palette.size == 0) {
        qa_error local = {0};
        if (!decode_palette(resources, options->family, context, local_palette, &palette, &local) && local.code != QA_ERROR_NOT_FOUND) {
            if (error != NULL) *error = local;
            return false;
        }
    }
    q2_indexed = q2_indexed && palette.size != 0;
    if ((pcx || palette.size == 0) && image->palette.size >= 1024 && !q2_indexed) {
        for (size_t i = 0; i < 256; ++i) memcpy(local_palette + i*3, image->palette.data + i*4, 3);
        palette = (qa_bytes){local_palette,sizeof(local_palette)};
    }
    if (palette.size == 0 && !decode_palette(resources, options->family, context, local_palette, &palette, error)) return false;
    if (palette.size != 768 || image->index_bytes != 1 || image->indices.size != (size_t)image->width * image->height) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "scene indexed image requires an 8-bit palette image"); return false;
    }
    if (q2_indexed && options->usage == QA_IMAGE_USAGE_SKIN && !flood_skin(image, palette, error)) return false;
    qa_palette_options conversion = {.transparent_index = options->transparent ? options->transparent_index : -1,
        .fullbright_first = -1, .fullbright_last = -1,
        .translation = options->translation.size == 256 ? options->translation.data : NULL,
        .layer = QA_PALETTE_COMBINED};
    if (options->family == QA_GAME_Q2) conversion.transparent_index = 255;
    qa_image expanded = {0};
    qa_indexed_level indexed = {.width = image->width, .height = image->height, .indices = image->indices};
    if (!qa_image_expand_indexed(&indexed, palette, &conversion, &expanded, error)) return false;
    if (options->fullbright_only) for (size_t i = 0; i < image->indices.size; ++i) {
        unsigned index = image->indices.data[i];
        if (index < resources->fullbright_first || (int)index == conversion.transparent_index)
            memset(expanded.rgba.data+i*4, 0, 4);
    }
    if (conversion.transparent_index >= 0)
        alpha_edge_fill(&expanded, options->family, palette.data);
    qa_buffer_free(&image->rgba);
    image->rgba = expanded.rgba;
    expanded.rgba = (qa_buffer){0};
    qa_image_free(&expanded);
    return true;
}

static bool decode_asset_pixels(qa_scene_resources *resources, const char *request, const char *path,
                          qa_bytes bytes, const qa_scene_image_options *options,
                          const qa_q3_image_upload_options *recipient,
                          const image_decode_context *context,
                          qa_scene_image **out, qa_error *error)
{
    const char *image_name = context && context->alias ? context->alias->name : request;
    qa_scene_image_options upload = *options;
    if (context && context->generic_upload) {
        upload.source_q3 = false;
        upload.source_upload = (qa_q3_image_upload_options){0};
    }
    if (recipient) {
        upload.source_q3 = true; upload.source_upload = *recipient;
        upload.mipmap = recipient->mipmap;
    }
    if (options->fullbright_only && !suffix_equal(path, ".lmp") && !suffix_equal(path, ".mip")) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene image has no indexed fullbright layer");
        return false;
    }
    if (suffix_equal(path, ".gif")) {
        qa_gif gif = {0};
        if (!qa_image_decode_gif(bytes, &gif, error)) return false;
        qa_scene_image *first = NULL;
        if (gif.frame_count == 0 || !image_from_rgba(resources, image_name, &gif.frames[0].image, &upload, &first, error)) {
            qa_gif_free(&gif); return false;
        }
        if (gif.frame_count > 1) {
            const qa_scene_image **frames = calloc(gif.frame_count, sizeof(*frames));
            if (frames == NULL) {
                qa_scene_image_release(first); qa_gif_free(&gif);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate GIF frame resources"); return false;
            }
            first->animation = frames; first->animation_count = gif.frame_count;
            for (size_t i = 1; i < gif.frame_count; ++i) {
                qa_scene_image *frame = NULL;
                if (!image_from_rgba(resources, image_name, &gif.frames[i].image, &upload, &frame, error)) {
                    qa_scene_image_release(first); qa_gif_free(&gif); return false;
                }
                owned_image *owned = (owned_image *)frame;
                free(owned->lineage);
                owned->lineage = ((owned_image *)first)->lineage;
                ++owned->lineage->references;
                frame->identity = first->identity;
                frame->revision = ++owned->lineage->revision;
                frames[i] = frame;
            }
        }
        qa_gif_free(&gif); *out = first; return true;
    }
    qa_image decoded = {0};
    bool ok = false, pcx = suffix_equal(path, ".pcx");
    qa_image_policy policy = options->family == QA_GAME_Q3 ? QA_IMAGE_Q3 : QA_IMAGE_FORMAT;
    if (suffix_equal(path, ".wal") || suffix_equal(path, ".mip")) {
        qa_mip_texture mip = {0};
        ok = suffix_equal(path, ".wal") ? qa_image_decode_wal(bytes, &mip, error) : qa_image_decode_mip(bytes, &mip, error);
        if (ok && !mip.external) {
            if (!recipient && !options->source_q3 && (options->family != QA_GAME_Q2 || !options->mipmap)) {
                qa_image images[4] = {0};
                qa_scene_image_level levels[4];
                for (size_t i = 0; i < 4 && ok; ++i) {
                    images[i] = (qa_image){.width = mip.levels[i].width, .height = mip.levels[i].height,
                        .indices = mip.levels[i].indices, .index_bytes = 1};
                    ok = indexed_rgba(resources, &images[i], options, false, context, error);
                    images[i].indices = (qa_buffer){0};
                    if (ok) levels[i] = (qa_scene_image_level){images[i].width,images[i].height,
                        images[i].rgba.data,images[i].rgba.size};
                }
                if (ok) ok = qa_scene_image_create(resources, image_name, QA_SCENE_RGBA8, levels, 4,
                    options->wrap, options->filter, (qa_scene_vec4){0}, out, error);
                if (ok) (*out)->texture_mode = image_uses_texture_mode(options);
                for (size_t i = 0; i < 4; ++i) qa_image_free(&images[i]);
                qa_mip_texture_free(&mip);
                return ok;
            }
            decoded.width = mip.width; decoded.height = mip.height; decoded.index_bytes = 1;
            decoded.indices = mip.levels[0].indices; mip.levels[0].indices = (qa_buffer){0};
        } else if (ok) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "external mip texture has no embedded pixels"); ok = false;
        }
        qa_mip_texture_free(&mip);
    } else if (suffix_equal(path, ".lmp")) ok = qa_image_decode_qpic(bytes, &decoded, error);
    else if (pcx) ok = qa_image_decode_pcx(bytes, options->source_q3 ? QA_IMAGE_Q3 : QA_IMAGE_FORMAT, &decoded, error);
    else if (suffix_equal(path, ".tga")) ok = qa_image_decode_tga(bytes, policy, &decoded, error);
    else if (suffix_equal(path, ".png")) ok = qa_image_decode_png(bytes, &decoded, error);
    else if (suffix_equal(path, ".bmp")) ok = qa_image_decode_bmp(bytes, options->source_q3 ? QA_IMAGE_Q3 : QA_IMAGE_FORMAT, &decoded, error);
    else if (suffix_equal(path, ".jpg") || suffix_equal(path, ".jpeg")) ok = qa_image_decode_jpeg(bytes, &decoded, error);
    else qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unsupported scene image extension: %s", path);
    if (ok && decoded.indices.size != 0 && (pcx || decoded.rgba.size == 0 || options->translation.size != 0 || options->fullbright_only))
        ok = indexed_rgba(resources, &decoded, options, pcx, context, error);
    if (!recipient && suffix_equal(path, ".lmp") && options->family != QA_GAME_Q2 && !options->source_q3) upload.mipmap = false;
    if (ok && options->family == QA_GAME_Q1 && options->transparent)
        alpha_edge_fill(&decoded, QA_GAME_Q1, NULL);
    if (ok) ok = image_from_rgba(resources, image_name, &decoded, &upload, out, error);
    qa_image_free(&decoded);
    return ok;
}
void qaw_q3_shift_color(const uint8_t input[3], uint32_t shift, uint8_t output[3]) {
    uint32_t r = (uint32_t)input[0] << shift, g = (uint32_t)input[1] << shift;
    uint32_t b = (uint32_t)input[2] << shift;
    uint32_t maximum = r > g ? r : g;
    if (b > maximum) maximum = b;
    if (maximum > 255) { r = r * 255 / maximum; g = g * 255 / maximum; b = b * 255 / maximum; }
    output[0] = (uint8_t)r; output[1] = (uint8_t)g; output[2] = (uint8_t)b;
}

void scene_image_asset_palette(qa_scene_resources *resources, image_asset_recipe *recipe,
    const qa_scene_image_options *options)
{
    recipe->options = *options;
    size_t family = (size_t)options->family;
    if (options->palette_rgb.size == 768) {
        if (family < 3 && resources->palette_resources[family] && resources->palettes[family].size == 768 &&
            !memcmp(options->palette_rgb.data, resources->palettes[family].data, 768)) {
            recipe->palette_source = resources->palette_resources[family];
            recipe->palette_attempted = true; recipe->palette_error = QA_OK;
            recipe->options.palette_rgb = (qa_bytes){0};
        } else {
            memcpy(recipe->palette, options->palette_rgb.data, 768);
            recipe->options.palette_rgb.data = recipe->palette;
        }
    }
    if (options->translation.size == 256) {
        memcpy(recipe->translation, options->translation.data, 256);
        recipe->options.translation.data = recipe->translation;
    }
}
bool scene_image_asset_copy(qa_scene_image *image, const image_asset_recipe *source, qa_error *error)
{
    owned_image *owned = (owned_image *)image;
    image_asset_recipe *recipe = malloc(sizeof(*recipe));
    if (!recipe) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining installed image recipe"); return false; }
    *recipe = *source;
    recipe->path = NULL;
    if (source->path) {
        size_t length = strlen(source->path);
        recipe->path = malloc(length + 1);
        if (!recipe->path) { free(recipe); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining image source path"); return false; }
        memcpy(recipe->path, source->path, length + 1);
    }
    if (recipe->options.palette_rgb.size) recipe->options.palette_rgb.data = recipe->palette;
    if (recipe->options.translation.size) recipe->options.translation.data = recipe->translation;
    qa_resource_retain(recipe->source); qa_resource_retain(recipe->palette_source);
    if (owned->asset) {
        qa_resource_release(owned->asset->source); qa_resource_release(owned->asset->palette_source);
        free(owned->asset->path); free(owned->asset);
    }
    owned->asset = recipe;
    return true;
}
bool scene_image_asset_source_bind(qa_scene_image *image, const qa_resource *source, qa_error *error)
{
    image_asset_recipe *recipe = image ? ((owned_image *)image)->asset : NULL;
    if (!recipe || !recipe->kind || recipe->source) return true;
    qa_bytes bytes = qa_resource_bytes(source);
    for (size_t i = 0; i < recipe->level_count; ++i) {
        uint64_t size = (uint64_t)recipe->widths[i] * recipe->heights[i] * (recipe->kind == 2 ? 3 : 1);
        if (recipe->offsets[i] > bytes.size || size > bytes.size - recipe->offsets[i]) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Installed image slice exceeds its actual source"); return false;
        }
    }
    recipe->source = (qa_resource *)source; qa_resource_retain(recipe->source);
    return true;
}
bool scene_resource_indexed_image(qa_scene_resources *resources, const char *name,
    const qa_indexed_level *indices, size_t count, const qa_scene_image_options *options,
    const qa_palette_options *colors, bool generate_mips, qa_scene_vec4 border,
    qa_scene_image **out, qa_error *error)
{
    qa_image expanded[4] = {0}; qa_scene_image_level levels[4]; qa_mip_chain chain = {0};
    bool ok = count && count <= 4;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = qa_image_expand_indexed(indices + i, options->palette_rgb, colors, expanded + i, error);
        if (ok && colors->transparent_index >= 0)
            alpha_edge_fill(expanded + i, options->family, options->palette_rgb.data);
        if (ok) levels[i] = (qa_scene_image_level){expanded[i].width, expanded[i].height,
            expanded[i].rgba.data, expanded[i].rgba.size};
    }
    qa_scene_image_level *generated = NULL;
    if (ok && generate_mips) {
        ok = count == 1 && image_mip_chain(expanded,
            options->family == QA_GAME_Q1 && colors->transparent_index >= 0, &chain, error);
        if (ok) generated = calloc(chain.count + 1, sizeof(*generated));
        if (ok && !generated) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating model skin mip descriptors"); ok = false; }
        if (ok) {
            generated[0] = levels[0];
            for (size_t i = 0; i < chain.count; ++i) {
                if (options->family == QA_GAME_Q1 && colors->transparent_index >= 0)
                    alpha_edge_fill(chain.levels + i, QA_GAME_Q1, NULL);
                generated[i + 1] = (qa_scene_image_level){
                    chain.levels[i].width, chain.levels[i].height, chain.levels[i].rgba.data, chain.levels[i].rgba.size};
            }
        }
    }
    if (ok) ok = qa_scene_image_create(resources, name, QA_SCENE_RGBA8, generated ? generated : levels,
        generated ? chain.count + 1 : count, options->wrap, options->filter, border, out, error);
    if (ok) (*out)->texture_mode = image_uses_texture_mode(options);
    free(generated); qa_mip_chain_free(&chain);
    for (size_t i = 0; i < 4; ++i) qa_image_free(expanded + i);
    return ok;
}
bool scene_resource_sky_layer(qa_scene_resources *resources, const char *name,
    const qa_scene_image *source, qa_bytes indexed, bool quake64, bool overlay,
    qa_scene_image **out, qa_error *error)
{
    const qa_scene_image_level *image = source->levels;
    if ((!quake64 && (image->width != 256 || image->height != 128)) ||
        (quake64 && (image->height < 2 || image->height % 2)) ||
        (source->kind != QA_SCENE_RGBA8 && source->kind != QA_SCENE_RGB8)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid classic sky image"); return false;
    }
    uint32_t width = quake64 ? image->width : 128, height = quake64 ? image->height / 2 : 128;
    if ((uint64_t)width * height > SIZE_MAX / 4) return false;
    size_t count = (size_t)width * height;
    uint8_t *pixels = malloc(count * 4);
    if (!pixels) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Decoding classic sky layer"); return false; }
    const uint8_t *rgba = image->pixels; uint64_t sum[3] = {0};
    for (size_t i = 0; i < count; ++i) {
        size_t front = quake64 ? i : (i / width) * image->width + i % width;
        size_t back = quake64 ? count + i : front + 128;
        for (size_t c = 0; c < 3; ++c) {
            pixels[i * 4 + c] = rgba[(overlay ? front : back) * 4 + c];
            sum[c] += rgba[back * 4 + c];
        }
        pixels[i * 4 + 3] = !overlay ? 255 : quake64 ? 128 : indexed.size && indexed.data[front] == 0 ? 0 :
            source->kind == QA_SCENE_RGBA8 ? rgba[front * 4 + 3] : 255;
    }
    if (overlay && !quake64) for (size_t i = 0; i < count; ++i) if (!pixels[i * 4 + 3])
        for (size_t c = 0; c < 3; ++c) pixels[i * 4 + c] = (uint8_t)(sum[c] / count);
    qa_scene_image_level level = {width, height, pixels, count * 4};
    bool ok = qa_scene_image_create(resources, name, QA_SCENE_RGBA8, &level, 1, QA_SCENE_REPEAT,
        QA_SCENE_LINEAR, (qa_scene_vec4){0}, out, error);
    free(pixels); return ok;
}
static bool image_slice_decode(qa_scene_resources *resources, const char *name,
    const image_asset_recipe *recipe, const image_decode_context *context, qa_scene_image **out, qa_error *error)
{
    qa_bytes bytes = qa_resource_bytes(recipe->source);
    if (!recipe->level_count || recipe->level_count > 4) return false;
    for (size_t i = 0; i < recipe->level_count; ++i) {
        uint64_t size = (uint64_t)recipe->widths[i] * recipe->heights[i] * (recipe->kind == 2 ? 3 : 1);
        if (!recipe->widths[i] || !recipe->heights[i] || recipe->offsets[i] > bytes.size || size > bytes.size - recipe->offsets[i]) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Installed image slice exceeds its actual source"); return false;
        }
    }
    if (recipe->kind == 2) {
        if (recipe->level_count != 1 || recipe->overbright > 15) return false;
        size_t count = (size_t)recipe->widths[0] * recipe->heights[0];
        uint8_t *pixels = malloc(count * 4);
        if (!pixels) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Decoding installed RGB image"); return false; }
        for (size_t i = 0; i < count; ++i) {
            qaw_q3_shift_color(bytes.data + recipe->offsets[0] + i * 3, recipe->overbright, pixels + i * 4);
            pixels[i * 4 + 3] = 255;
        }
        qa_scene_image_level level = {recipe->widths[0], recipe->heights[0], pixels, count * 4};
        bool ok = qa_scene_image_create(resources, name, QA_SCENE_RGB8, &level, 1, recipe->options.wrap,
            recipe->options.filter, (qa_scene_vec4){0}, out, error);
        if (ok) (*out)->texture_mode = image_uses_texture_mode(&recipe->options);
        free(pixels); return ok;
    }
    qa_scene_image_options options = recipe->options;
    uint8_t palette[768];
    if (!options.palette_rgb.size && !decode_palette(resources, options.family, context, palette, &options.palette_rgb, error)) return false;
    qa_indexed_level indexed[4] = {0}; qa_buffer flooded = {0};
    for (size_t i = 0; i < recipe->level_count; ++i) indexed[i] = (qa_indexed_level){recipe->widths[i], recipe->heights[i],
        {(uint8_t *)bytes.data + recipe->offsets[i], (size_t)recipe->widths[i] * recipe->heights[i]}};
    if (recipe->flood_skin) {
        size_t size = indexed[0].indices.size;
        flooded = (qa_buffer){malloc(size), size};
        if (!flooded.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Decoding installed skin flood"); return false; }
        memcpy(flooded.data, indexed[0].indices.data, size);
        qa_image image = {.width = indexed[0].width, .height = indexed[0].height, .indices = flooded};
        bool ok = flood_skin(&image, options.palette_rgb, error);
        if (!ok) { qa_buffer_free(&flooded); return false; }
        indexed[0].indices = flooded;
    }
    qa_palette_options colors = {.transparent_index = options.transparent ? options.transparent_index : -1,
        .fullbright_first = recipe->fullbright_first < 256 ? (int)recipe->fullbright_first : -1,
        .fullbright_last = recipe->fullbright_last, .translation = options.translation.size ? options.translation.data : NULL,
        .layer = recipe->layer};
    bool ok = scene_resource_indexed_image(resources, name, indexed, recipe->level_count, &options, &colors,
        recipe->generate_mips, (qa_scene_vec4){0}, out, error);
    qa_buffer_free(&flooded); return ok;
}
static bool image_asset_bind(qa_scene_resources *resources, qa_scene_image *image, const char *path,
    const qa_scene_image_options *options, const qa_q3_image_upload_options *recipient,
    const image_decode_context *context, uint32_t gif_frame, qa_error *error)
{
    const image_alias *alias = context ? context->alias : NULL;
    const qa_scene_image_load_receipt *receipt = context ? context->observation : NULL;
    const qa_resource *source = alias ? alias->source : context && context->source ? context->source :
        receipt ? receipt->source : NULL;
    if (!source) return true;
    image_asset_recipe recipe = {.path = (char *)path, .source = (qa_resource *)source,
        .palette_source = alias ? alias->palette_source : receipt ? receipt->palette_source : NULL,
        .palette_attempted = alias ? alias->palette_attempted : receipt && receipt->palette_attempted,
        .palette_error = alias ? alias->palette_error : receipt ? receipt->palette_error : QA_OK,
        .fullbright_first = resources->fullbright_first, .gif_frame = gif_frame,
        .generic_upload = context && context->generic_upload, .recipient = recipient != NULL};
    scene_image_asset_palette(resources, &recipe, options);
    if (recipe.generic_upload && recipe.options.source_q3)
        recipe.options.source_upload.mipmap = recipe.options.mipmap;
    if (recipient) recipe.recipient_upload = *recipient;
    return scene_image_asset_copy(image, &recipe, error);
}
static bool decode_asset_upload(qa_scene_resources *resources, const char *request, const char *path,
    qa_bytes bytes, const qa_scene_image_options *options, const qa_q3_image_upload_options *recipient,
    const image_decode_context *context, qa_scene_image **out, qa_error *error)
{
    if (!decode_asset_pixels(resources, request, path, bytes, options, recipient, context, out, error)) return false;
    bool ok = image_asset_bind(resources, *out, path, options, recipient, context, 0, error);
    for (size_t i = 1; ok && i < (*out)->animation_count; ++i)
        ok = i <= UINT32_MAX && image_asset_bind(resources, (qa_scene_image *)(*out)->animation[i], path,
            options, recipient, context, (uint32_t)i, error);
    if (!ok) { qa_scene_image_release(*out); *out = NULL; }
    return ok;
}
bool scene_resource_image_decode(qa_scene_resources *resources, const char *name,
    const image_asset_recipe *recipe, qa_scene_image **out, qa_error *error)
{
    image_alias alias = {.name = (char *)name, .source = recipe->source, .source_path = recipe->path,
        .palette_source = recipe->palette_source, .palette_attempted = recipe->palette_attempted,
        .palette_error = recipe->palette_error};
    image_decode_context context = {.alias = &alias, .generic_upload = recipe->generic_upload};
    unsigned previous_fullbright = resources->fullbright_first;
    resources->fullbright_first = recipe->fullbright_first;
    bool ok;
    if (recipe->kind) {
        ok = image_slice_decode(resources, name, recipe, &context, out, error);
    } else if (suffix_equal(recipe->path, ".gif")) {
        qa_gif gif = {0};
        ok = qa_image_decode_gif(qa_resource_bytes(recipe->source), &gif, error);
        if (ok && recipe->gif_frame >= gif.frame_count) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Installed GIF frame is unavailable"); ok = false;
        }
        qa_scene_image_options upload = recipe->options;
        if (recipe->generic_upload) { upload.source_q3 = false; upload.source_upload = (qa_q3_image_upload_options){0}; }
        if (recipe->recipient && !recipe->post_upload) { upload.source_q3 = true; upload.source_upload = recipe->recipient_upload; upload.mipmap = upload.source_upload.mipmap; }
        if (ok) ok = image_from_rgba(resources, name, &gif.frames[recipe->gif_frame].image, &upload, out, error) &&
            image_asset_bind(resources, *out, recipe->path, &recipe->options,
                recipe->recipient && !recipe->post_upload ? &recipe->recipient_upload : NULL, &context, recipe->gif_frame, error);
        qa_gif_free(&gif);
    } else ok = !recipe->gif_frame && decode_asset_upload(resources, name, recipe->path,
        qa_resource_bytes(recipe->source), &recipe->options, recipe->recipient && !recipe->post_upload ? &recipe->recipient_upload : NULL,
        &context, out, error);
    if (ok && recipe->sky_layer) {
        qa_scene_image *base = *out, *sky = NULL;
        qa_bytes indices = recipe->kind == 1 ? (qa_bytes){qa_resource_bytes(recipe->source).data + recipe->offsets[0],
            (size_t)recipe->widths[0] * recipe->heights[0]} : (qa_bytes){0};
        ok = scene_resource_sky_layer(resources, name, base, indices, recipe->quake64,
            recipe->sky_layer == 2, &sky, error);
        qa_scene_image_release(base); *out = sky;
    }
    if (ok && recipe->post_upload) {
        qa_scene_image *base = *out, *uploaded = NULL;
        qa_image pixels = {.width = base->levels[0].width, .height = base->levels[0].height,
            .rgba = {(uint8_t *)base->levels[0].pixels, base->levels[0].bytes}};
        qa_scene_image_options options = {.family = QA_GAME_Q3, .wrap = base->wrap,
            .filter = base->filter, .mipmap = recipe->post_mipmap,
            .source_q3 = recipe->recipient, .source_upload = recipe->recipient_upload};
        ok = image_from_rgba(resources, name, &pixels, &options, &uploaded, error);
        if (ok) uploaded->texture_mode = base->texture_mode && recipe->post_mipmap;
        qa_scene_image_release(base); *out = uploaded;
    }
    if (ok) ok = scene_image_asset_copy(*out, recipe, error);
    resources->fullbright_first = previous_fullbright;
    if (!ok && *out) { qa_scene_image_release(*out); *out = NULL; }
    return ok;
}
static bool decode_asset(qa_scene_resources *resources, const char *request, const char *path,
    qa_bytes bytes, const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{ return decode_asset_upload(resources, request, path, bytes, options, NULL, NULL, out, error); }

bool qa_scene_image_decode_retained(qa_scene_resources *resources, const char *request,
    const char *path, qa_bytes bytes, const qa_scene_image_options *options,
    qa_scene_image **out, qa_error *error)
{
    if (!resources || !request || !path || !options || !out || *out || !bytes.data ||
        options->family < QA_GAME_Q1 || options->family > QA_GAME_Q3 ||
        options->wrap < QA_SCENE_REPEAT || options->wrap > QA_SCENE_CLAMP ||
        options->filter < QA_SCENE_NEAREST || options->filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        options->usage < QA_IMAGE_USAGE_DEFAULT || options->usage > QA_IMAGE_USAGE_SKY ||
        (options->palette_rgb.size && (options->palette_rgb.size != 768 || !options->palette_rgb.data)) ||
        (options->translation.size && (options->translation.size != 256 || !options->translation.data))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained image decode requires its real bytes and explicit options");
        return false;
    }
    bool external_palette = suffix_equal(path, ".lmp") || suffix_equal(path, ".mip") ||
        suffix_equal(path, ".wal") || (resources->vfs && suffix_equal(path, ".pcx") &&
        options->family == QA_GAME_Q2 &&
        (options->usage == QA_IMAGE_USAGE_SKIN || options->usage == QA_IMAGE_USAGE_SPRITE));
    if (external_palette && options->palette_rgb.size != 768) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained indexed image decode requires the actual source palette");
        return false;
    }
    if (!admission_ready(resources, error)) return false;
    if (options->source_q3 && (options->family != QA_GAME_Q3 ||
        options->source_upload.mipmap != options->mipmap)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained Source upload requires its actual admission profile");
        return false;
    }
    if (options->source_q3 && !qa_q3_image_upload_options_valid(&options->source_upload, error)) return false;
    return decode_asset(resources, request, path, bytes, options, out, error);
}
void qa_scene_image_load_receipt_dispose(qa_scene_image_load_receipt *receipt)
{
    if (!receipt) return;
    qa_resource_release(receipt->source); qa_resource_release(receipt->logical_source);
    qa_resource_release(receipt->palette_source);
    free(receipt->logical_path);
    qa_vfs_acquisition_dispose(&receipt->source_opening);
    qa_vfs_acquisition_dispose(&receipt->logical_opening);
    qa_vfs_acquisition_dispose(&receipt->palette_opening);
    *receipt = (qa_scene_image_load_receipt){0};
}
static bool image_load_receipt(qa_scene_image_load_receipt *receipt, qa_resource *source,
    qa_mount_id source_mount, const qa_vfs_acquisition *source_opening,
    qa_resource *logical_source, qa_mount_id logical_mount, const qa_vfs_acquisition *logical_opening, const char *logical_path,
    qa_error *error)
{
    if (!receipt) return true;
    if (logical_source && logical_path) {
        size_t length = strlen(logical_path);
        receipt->logical_path = length == SIZE_MAX ? NULL : malloc(length + 1);
        if (!receipt->logical_path) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining logical image request"); return false;
        }
        memcpy(receipt->logical_path, logical_path, length + 1);
    }
    if ((source_opening && source_opening->mount && !qa_vfs_acquisition_copy(source_opening, &receipt->source_opening, error)) ||
        (logical_opening && logical_source && logical_opening->mount &&
         !qa_vfs_acquisition_copy(logical_opening, &receipt->logical_opening, error))) {
        qa_scene_image_load_receipt_dispose(receipt); return false;
    }
    qa_resource_retain(source); qa_resource_retain(logical_source);
    receipt->source = source; receipt->logical_source = logical_source;
    receipt->source_mount = source_mount; receipt->logical_mount = logical_source ? logical_mount : 0;
    return true;
}
static bool image_palette_receipt(qa_scene_image_load_receipt *receipt, qa_resource *source,
    const qa_vfs_acquisition *opening, bool attempted, qa_status status, qa_error *error)
{
    if (!receipt) return true;
    if (source && !qa_vfs_acquisition_copy(opening, &receipt->palette_opening, error)) return false;
    receipt->palette_source = source; qa_resource_retain(source);
    receipt->palette_attempted = attempted; receipt->palette_error = status; return true;
}

bool scene_resource_variant_parent_retain(qa_scene_resources *destination, const qa_scene_image *source,
    qa_scene_resources **out, qa_error *error)
{
    qa_scene_resources *parent = qa_scene_image_resource_owner(source);
    if (!parent) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Variant parent lost its actual resource bank"); return false; }
    if (parent->policy_source) parent = parent->policy_source;
    if (destination->policy_source) destination = destination->policy_source;
    *out = NULL;
    if (parent == destination) return true;
    qa_scene_resources **banks = malloc(sizeof(*banks));
    if (!banks) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Inspecting variant bank dependencies"); return false; }
    size_t count = 1; banks[0] = parent; bool ok = true;
    for (size_t at = 0; ok && at < count; ++at) {
        if (banks[at] == destination) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Recipient resource banks form a retention cycle"); ok = false; break;
        }
        qa_scene_resources *rows[2] = {banks[at], banks[at]->policy_pending ? banks[at]->policy_pending->destination : NULL};
        for (unsigned row = 0; ok && row < 2; ++row)
        for (const owned_image *image = rows[row] ? rows[row]->names->images : NULL; ok && image; image = image->next) {
            qa_scene_resources *next = image->source_variant_owner;
            if (!next) continue;
            size_t i = 0; while (i < count && banks[i] != next) ++i;
            if (i < count) continue;
            if (count >= SIZE_MAX / sizeof(*banks)) { ok = false; break; }
            qa_scene_resources **grown = realloc(banks, (count + 1) * sizeof(*banks));
            if (!grown) { ok = false; break; }
            banks = grown; banks[count++] = next;
        }
    }
    free(banks);
    if (!ok) {
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining variant bank dependency graph");
        return false;
    }
    if (!qa_scene_resources_retain(parent, error)) return false;
    *out = parent; return true;
}

bool qa_scene_image_source_q3_variant(qa_scene_resources *resources, const qa_scene_image *source,
    const qa_q3_image_upload_options *profile, qa_scene_image **out, qa_error *error)
{
    if (!out || *out || !source || !admission_ready(resources, error) ||
        !qa_q3_image_upload_options_valid(profile, error)) return false;
    qa_q3_image_upload_options actual = *profile;
    qa_scene_resources *source_owner = qa_scene_image_resource_owner(source);
    if (!source_owner) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Recipient image lost its actual bank"); return false;
    }
    qa_scene_image_request admission;
    if (qa_scene_image_request_read(source_owner, source, &admission)) actual.mipmap = actual.mipmap && admission.options.mipmap;
    if (((const owned_image *)source)->sampling_source)
        actual.mipmap = actual.mipmap && ((const owned_image *)source)->sampling_mipmap;
    if (source->recipient_upload_pixels) actual.mipmap = actual.mipmap && source->recipient_mipmap;
    if (!actual.mipmap) actual.allow_picmip = false;
    profile = &actual;
    const owned_image *original = NULL;
    for (const owned_image *image = resources->names->images; image; image = image->next) {
        if (&image->image == source) original = image;
        if (!image->generic_variant && image->source_variant_source == source &&
            qa_q3_image_upload_options_equal(&image->source_variant_upload, profile)) {
            qa_scene_image_retain(&image->image); *out = (qa_scene_image *)&image->image; return true;
        }
    }
    if (!original) original = (const owned_image *)source;
    if (source->source_q3) {
        qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
    }
    for (size_t i = 0; i < source_owner->cache_count; ++i) {
        const qa_scene_image *first = source_owner->cache[i].image;
        for (size_t frame = 1; frame < first->animation_count; ++frame)
            if (first->animation[frame] == source) {
                qa_scene_image *mapped = NULL;
                if (!qa_scene_image_source_q3_variant(resources, first, profile, &mapped, error)) return false;
                if (frame >= mapped->animation_count || !mapped->animation[frame]) {
                    qa_scene_image_release(mapped);
                    qa_error_set(error, QA_ERROR_ARGUMENT, frame, "Source animation lost its actual frame correspondence"); return false;
                }
                *out = (qa_scene_image *)mapped->animation[frame]; qa_scene_image_retain(*out);
                qa_scene_image_release(mapped); return true;
            }
    }
    if (original->sampling_source) {
        qa_scene_image *parent = NULL;
        bool ok = qa_scene_image_source_q3_variant(resources, original->sampling_source, profile, &parent, error);
        if (ok) ok = qa_scene_image_sample(resources, parent, profile->mipmap && original->sampling_mipmap,
            source->wrap, out, error);
        qa_scene_image_release(parent);
        if (!ok) return false;
        (*out)->source_q3 = true; (*out)->source_mipmap = profile->mipmap && original->sampling_mipmap;
    } else {
        qa_scene_image_request request;
        if (qa_scene_image_request_read(source_owner, source, &request)) {
            if (request.options.source_q3) {
                qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
            }
            const image_alias *alias = NULL;
            for (const image_alias *row = source_owner->aliases; row; row = row->next)
                if (!strcmp(row->name, request.name)) { alias = row; break; }
            if (alias) {
                qa_scene_image_options decode = alias->decode_options;
                decode.wrap = request.options.wrap; decode.filter = request.options.filter; decode.mipmap = profile->mipmap;
                image_decode_context context = {.alias = alias};
                if (!alias->source || !decode_asset_upload(resources, alias->request, alias->source_path,
                    qa_resource_bytes(alias->source), &decode, profile, &context, out, error)) return false;
            } else {
                qa_bytes palette;
                if (!request.options.palette_rgb.size &&
                    qa_scene_resources_palette_read(source_owner, request.options.family, &palette))
                    request.options.palette_rgb = palette;
                if (!decode_asset_upload(resources, request.name, qa_resource_path(request.source),
                    qa_resource_bytes(request.source), &request.options, profile,
                    &(image_decode_context){.source = request.source}, out, error)) return false;
            }
        } else {
            if (!source->recipient_upload_pixels || source->kind == QA_SCENE_DEPTH32F) {
                qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
            }
            qa_image pixels = {.width = source->levels[0].width, .height = source->levels[0].height,
                .rgba = {(uint8_t *)source->levels[0].pixels, source->levels[0].bytes}};
            qa_scene_image_options sampling = {.wrap = source->wrap, .filter = source->filter,
                .source_q3 = true, .source_upload = *profile};
            if (!image_from_rgba(resources, source->name, &pixels, &sampling, out, error)) return false;
            if (original->asset) {
                image_asset_recipe asset = *original->asset;
                asset.recipient = true; asset.recipient_upload = *profile;
                asset.post_upload = true; asset.post_mipmap = profile->mipmap;
                if (!scene_image_asset_copy(*out, &asset, error)) { qa_scene_image_release(*out); *out = NULL; return false; }
            }
        }
    }
    (*out)->logical_width = source->logical_width; (*out)->logical_height = source->logical_height;
    (*out)->texture_mode = source->texture_mode && profile->mipmap;
    owned_image *variant = (owned_image *)*out;
    if (!scene_resource_variant_parent_retain(resources, source, &variant->source_variant_owner, error)) {
        qa_scene_image_release(*out); *out = NULL; return false;
    }
    variant->source_variant_source = source; variant->source_variant_upload = *profile;
    qa_scene_image_retain(source);
    if (original->sampling_source && !qa_scene_image_source_admit(resources, *out,
        source->source_texture_unit, error)) {
        qa_scene_image_release(*out); *out = NULL; return false;
    }
    qa_image_free(&variant->recipient_source);
    for (size_t i = 1; i < variant->image.animation_count; ++i)
        qa_image_free(&((owned_image *)variant->image.animation[i])->recipient_source);
    variant->variant_next = resources->variants; resources->variants = variant;
    recipient_correspondence_changed(resources);
    qa_scene_image_retain(&variant->image);
    return true;
}

bool qa_scene_image_source_q3_recipient_variant(qa_scene_resources *resources,
    const qa_scene_image *source, const qa_q3_image_upload_options *profile,
    qa_scene_source_image_admit_fn receiver, const void *context,
    qa_scene_image **out, qa_error *error)
{
    if (!out || *out || !source || !admission_ready(resources, error) ||
        !qa_q3_image_upload_options_valid(profile, error) ||
        qa_scene_image_resource_owner(source) != resources ||
        !qa_scene_resources_source_image_admit_is(resources, receiver, context)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Recipient upload lost its exact Source receiver"); return false;
    }
    if (source->source_q3) {
        qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
    }
    const owned_image *original = (const owned_image *)source;
    if (resources->recipient_generation &&
        original->recipient_correspondence_generation == resources->recipient_generation &&
        (original->recipient_correspondence_canonical ||
            qa_q3_image_upload_options_equal(&original->recipient_correspondence_profile, profile))) {
        *out = (qa_scene_image *)original->recipient_correspondence;
        qa_scene_image_retain(*out); return true;
    }
    for (const owned_image *root = resources->variants; root; root = root->variant_next) {
        if (!root->recipient_first_upload) continue;
        const qa_scene_image *parent = root->source_variant_source;
        for (const recipient_image_binding *binding = root->recipient_bindings; binding; binding = binding->next) {
            if (binding->source == source) {
                recipient_correspondence_remember(resources, source, profile, &root->image, true);
                *out = (qa_scene_image *)&root->image; qa_scene_image_retain(*out); return true;
            }
            for (size_t frame = 1; frame < binding->source->animation_count; ++frame)
                if (binding->source->animation[frame] == source && frame < root->image.animation_count) {
                    recipient_correspondence_remember(resources, source, profile, root->image.animation[frame], true);
                    *out = (qa_scene_image *)root->image.animation[frame]; qa_scene_image_retain(*out); return true;
                }
        }
        if (parent == source) {
            recipient_correspondence_remember(resources, source, profile, &root->image, true);
            *out = (qa_scene_image *)&root->image; qa_scene_image_retain(*out); return true;
        }
        for (size_t frame = 1; frame < parent->animation_count; ++frame) {
            if (parent->animation[frame] != source) continue;
            if (frame >= root->image.animation_count || !root->image.animation[frame]) {
                qa_error_set(error, QA_ERROR_ARGUMENT, frame, "First recipient upload lost its animation frame"); return false;
            }
            recipient_correspondence_remember(resources, source, profile, root->image.animation[frame], true);
            *out = (qa_scene_image *)root->image.animation[frame]; qa_scene_image_retain(*out); return true;
        }
    }
    if (!qa_scene_image_source_q3_variant(resources, source, profile, out, error)) return false;
    bool promoted = false;
    for (owned_image *root = resources->variants; root; root = root->variant_next) {
        if (root->generic_variant) continue;
        bool root_image = &root->image == *out, reached = root_image;
        if (!root_image)
            for (size_t frame = 1; frame < root->image.animation_count; ++frame)
                if (root->image.animation[frame] == *out) { reached = true; break; }
        if (!reached) continue;
        if (!root->recipient_first_upload) {
            root->recipient_first_upload = true;
            recipient_correspondence_changed(resources);
            promoted = true;
        }
        if (root_image) break;
    }
    if (!promoted) recipient_correspondence_remember(resources, source, profile, *out, false);
    return true;
}

bool qa_scene_image_generic_variant(qa_scene_resources *resources, const qa_scene_image *source,
    bool mipmap, qa_scene_image **out, qa_error *error)
{
    if (!out || *out || !source || !admission_ready(resources, error)) return false;
    qa_scene_resources *source_owner = qa_scene_image_resource_owner(source);
    if (!source_owner) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Generic recipient image lost its actual bank"); return false;
    }
    const qa_scene_image *builtin = source == source_owner->source_missing ? qa_scene_missing(resources) :
        source == source_owner->source_white || source == source_owner->source_identity ? qa_scene_white(resources) : NULL;
    if (builtin) { qa_scene_image_retain(builtin); *out = (qa_scene_image *)builtin; return true; }
    if (!source->source_q3) {
        qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
    }
    const owned_image *original = (const owned_image *)source;
    const image_cache *recipe = NULL;
    for (size_t i = 0; i < source_owner->cache_count; ++i)
        if (source_owner->cache[i].image == source) { recipe = source_owner->cache + i; break; }
    mipmap = mipmap && source->source_mipmap;
    if (recipe) mipmap = mipmap && recipe->options.mipmap;
    if (original->sampling_source) mipmap = mipmap && original->sampling_mipmap;
    for (const owned_image *row = resources->names->images; row; row = row->next)
        if (row->generic_variant && row->source_variant_source == source && row->generic_variant_mipmap == mipmap) {
            qa_scene_image_retain(&row->image); *out = (qa_scene_image *)&row->image; return true;
        }
    if (original->source_variant_source && !original->generic_variant)
        return qa_scene_image_generic_variant(resources, original->source_variant_source, mipmap, out, error);
    for (const owned_image *root = source_owner->names->images; root; root = root->next) {
        if (!root->source_variant_source || root->generic_variant) continue;
        for (size_t frame = 1; frame < root->image.animation_count; ++frame)
            if (root->image.animation[frame] == source) {
                const qa_scene_image *parent = root->source_variant_source;
                if (frame >= parent->animation_count || !parent->animation[frame]) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, frame, "Recipient animation lost its original frame receipt"); return false;
                }
                return qa_scene_image_generic_variant(resources, parent->animation[frame], mipmap, out, error);
            }
    }
    for (size_t i = 0; !recipe && i < source_owner->cache_count; ++i) {
        const qa_scene_image *first = source_owner->cache[i].image;
        for (size_t frame = 1; frame < first->animation_count; ++frame)
            if (first->animation[frame] == source) {
                qa_scene_image *mapped = NULL;
                if (!qa_scene_image_generic_variant(resources, first, mipmap, &mapped, error)) return false;
                if (frame >= mapped->animation_count || !mapped->animation[frame]) {
                    qa_scene_image_release(mapped);
                    qa_error_set(error, QA_ERROR_ARGUMENT, frame, "Generic animation lost its actual frame correspondence"); return false;
                }
                *out = (qa_scene_image *)mapped->animation[frame]; qa_scene_image_retain(*out);
                qa_scene_image_release(mapped); return true;
            }
    }
    if (original->sampling_source) {
        qa_scene_image *parent = NULL;
        bool ok = qa_scene_image_generic_variant(resources, original->sampling_source, mipmap, &parent, error);
        if (ok) ok = qa_scene_image_sample(resources, parent, mipmap, source->wrap, out, error);
        qa_scene_image_release(parent);
        if (!ok) return false;
    } else if (recipe) {
        const image_alias *alias = NULL;
        const char *name = qa_strings_cstr(source_owner->names->strings, recipe->name);
        for (const image_alias *row = source_owner->aliases; row; row = row->next)
            if (!strcmp(row->name, name)) { alias = row; break; }
        image_alias retained = {.name = (char *)source->name, .request = (char *)name,
            .source = recipe->source_record,
            .source_path = (char *)qa_resource_path(recipe->source_record),
            .palette_source = recipe->palette_source, .palette_attempted = recipe->palette_attempted,
            .palette_error = recipe->palette_error};
        qa_scene_image_options options = alias ? alias->decode_options : recipe->options;
        options.wrap = source->wrap; options.filter = source->filter; options.mipmap = mipmap;
        if (!mipmap && options.filter >= QA_SCENE_NEAREST_MIPMAP_NEAREST) options.filter = QA_SCENE_LINEAR;
        if (!alias) {
            if (options.palette_rgb.size) options.palette_rgb.data = recipe->palette;
            if (options.translation.size) options.translation.data = recipe->translation;
        }
        image_decode_context context = {.alias = alias ? alias : &retained, .generic_upload = true};
        const qa_resource *resource = alias ? alias->source : recipe->source_record;
        if (!resource || !decode_asset_upload(resources, alias ? alias->request : name,
            alias ? alias->source_path : retained.source_path, qa_resource_bytes(resource), &options,
            NULL, &context, out, error)) return false;
    } else if (original->recipient_source.rgba.size) {
        qa_scene_image_options options = {.family = QA_GAME_Q3, .wrap = source->wrap,
            .filter = !mipmap && source->filter >= QA_SCENE_NEAREST_MIPMAP_NEAREST ? QA_SCENE_LINEAR : source->filter,
            .mipmap = mipmap};
        if (!image_from_rgba(resources, source->name, &original->recipient_source, &options, out, error)) return false;
        if (original->asset) {
            image_asset_recipe asset = *original->asset;
            asset.generic_upload = true; asset.recipient = false;
            if (asset.post_upload) asset.post_mipmap = mipmap;
            else {
                asset.options.mipmap = mipmap; asset.options.filter = options.filter;
                if (asset.options.source_q3) asset.options.source_upload.mipmap = mipmap;
            }
            if (!scene_image_asset_copy(*out, &asset, error)) { qa_scene_image_release(*out); *out = NULL; return false; }
        }
    } else {
        /* Live cinematic images have no immutable decoder recipe. Their actual
         * publisher continues to own the reached image version. */
        qa_scene_image_retain(source); *out = (qa_scene_image *)source; return true;
    }
    (*out)->logical_width = source->logical_width; (*out)->logical_height = source->logical_height;
    (*out)->texture_mode = source->texture_mode && mipmap;
    owned_image *variant = (owned_image *)*out;
    if (!scene_resource_variant_parent_retain(resources, source, &variant->source_variant_owner, error)) {
        qa_scene_image_release(*out); *out = NULL; return false;
    }
    variant->source_variant_source = source; qa_scene_image_retain(source);
    variant->generic_variant = true; variant->generic_variant_mipmap = mipmap;
    if (source == source_owner->source_fog) variant->image.border = (qa_scene_vec4){1,1,1,1};
    else if (source == source_owner->source_dlight) variant->image.border = (qa_scene_vec4){0,0,0,1};
    variant->variant_next = resources->variants; resources->variants = variant;
    qa_scene_image_retain(*out); return true;
}

static bool same_options(const image_cache *entry, const qa_scene_image_options *options)
{
    const qa_scene_image_options *a = &entry->options;
    return a->family == options->family && a->wrap == options->wrap && a->filter == options->filter &&
        a->usage == options->usage && a->mipmap == options->mipmap && a->transparent == options->transparent &&
        a->fullbright_only == options->fullbright_only && a->transparent_index == options->transparent_index &&
        a->palette_rgb.size == options->palette_rgb.size && a->translation.size == options->translation.size &&
        a->source_q3 == options->source_q3 && (!a->source_q3 ||
            qa_q3_image_upload_options_equal(&a->source_upload, &options->source_upload)) &&
        (options->palette_rgb.size == 0 || memcmp(entry->palette, options->palette_rgb.data, 768) == 0) &&
        (options->translation.size == 0 || memcmp(entry->translation, options->translation.data, 256) == 0);
}

bool qa_scene_image_request_read(const qa_scene_resources *resources, const qa_scene_image *image,
    qa_scene_image_request *out)
{
    if (!resources || !image || !out) return false;
    for (size_t i = 0; i < resources->cache_count; ++i) {
        const image_cache *entry = &resources->cache[i];
        if (entry->image != image) continue;
        const char *name = qa_strings_cstr(resources->names->strings, entry->name);
        if (!name || !entry->source_record || qa_resource_id(entry->source_record) != entry->source) return false;
        *out = (qa_scene_image_request){.name = name, .options = entry->options,
            .source = entry->source_record, .source_mount = entry->source_mount, .exact_file = entry->exact_file};
        if (out->options.palette_rgb.size) out->options.palette_rgb.data = entry->palette;
        if (out->options.translation.size) out->options.translation.data = entry->translation;
        return true;
    }
    return false;
}

static bool cache_add(qa_scene_resources *resources, qa_string_id name, qa_resource *source,
                      qa_mount_id source_mount, qa_resource *logical_source, qa_mount_id logical_mount,
                      const qa_vfs_acquisition *source_opening, const qa_vfs_acquisition *logical_opening,
                      const char *logical_path,
                      const qa_scene_image_load_receipt *observation,
                      const qa_scene_image_options *options, bool exact_file,
                      qa_scene_image *image, qa_error *error)
{
    if (resources->cache_count == resources->cache_capacity) {
        size_t capacity = resources->cache_capacity == 0 ? 64 : resources->cache_capacity * 2;
        if (capacity < resources->cache_capacity || capacity > (size_t)PTRDIFF_MAX / sizeof(*resources->cache)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "scene image cache overflow"); return false;
        }
        image_cache *grown = realloc(resources->cache, capacity * sizeof(*grown));
        if (grown == NULL) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow scene image cache"); return false; }
        resources->cache = grown; resources->cache_capacity = capacity;
    }
    image_cache *entry = &resources->cache[resources->cache_count];
    *entry = (image_cache){.source = qa_resource_id(source), .logical_source = qa_resource_id(logical_source),
        .source_record = source, .logical_record = logical_source, .name = name, .options = *options,
        .exact_file = exact_file, .image = image};
    entry->source_mount = source_mount; entry->logical_mount = logical_source ? logical_mount : 0;
    if (logical_source && logical_path) {
        size_t length = strlen(logical_path);
        entry->logical_path = length == SIZE_MAX ? NULL : malloc(length + 1);
        if (!entry->logical_path) {
            *entry = (image_cache){0}; qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining cached logical image request"); return false;
        }
        memcpy(entry->logical_path, logical_path, length + 1);
    }
    if ((source_opening && !qa_vfs_acquisition_copy(source_opening, &entry->source_opening, error)) ||
        (logical_source && logical_opening && !qa_vfs_acquisition_copy(logical_opening, &entry->logical_opening, error)) ||
        (observation && observation->palette_source &&
         !qa_vfs_acquisition_copy(&observation->palette_opening, &entry->palette_opening, error))) {
        qa_vfs_acquisition_dispose(&entry->source_opening); qa_vfs_acquisition_dispose(&entry->logical_opening);
        qa_vfs_acquisition_dispose(&entry->palette_opening);
        free(entry->logical_path);
        *entry = (image_cache){0}; return false;
    }
    if (observation) {
        entry->palette_source = observation->palette_source; qa_resource_retain(entry->palette_source);
        entry->palette_attempted = observation->palette_attempted; entry->palette_error = observation->palette_error;
    }
    /* Stored span pointers are deliberately not used: cache array relocation
     * cannot invalidate option identity. Compare the embedded bytes above. */
    entry->options.palette_rgb.data = NULL; entry->options.translation.data = NULL;
    if (options->palette_rgb.size != 0) memcpy(entry->palette, options->palette_rgb.data, 768);
    if (options->translation.size != 0) memcpy(entry->translation, options->translation.data, 256);
    qa_scene_image_retain(image);
    qa_resource_retain(source); qa_resource_retain(logical_source);
    ++resources->cache_count;
    /* The completed recipe now owns the immutable decoder inputs. Only
     * uncached Source constructors need a separate pre-upload pixel copy. */
    qa_image_free(&((owned_image *)image)->recipient_source);
    for (size_t i = 1; i < image->animation_count; ++i)
        qa_image_free(&((owned_image *)image->animation[i])->recipient_source);
    return true;
}

static bool logical_dimensions(qa_resource *original, const char *path, uint32_t *width,
                                uint32_t *height, qa_error *error)
{
    if (original == NULL) return true;
    qa_bytes bytes = qa_resource_bytes(original);
    if (suffix_equal(path, ".wal") || suffix_equal(path, ".mip")) {
        qa_mip_texture mip = {0};
        bool ok = suffix_equal(path, ".wal") ? qa_image_decode_wal(bytes, &mip, error) : qa_image_decode_mip(bytes, &mip, error);
        if (!ok) return false;
        *width = mip.width; *height = mip.height; qa_mip_texture_free(&mip); return true;
    }
    if (suffix_equal(path, ".gif")) {
        qa_gif gif = {0};
        if (!qa_image_decode_gif(bytes, &gif, error)) return false;
        *width = gif.width; *height = gif.height; qa_gif_free(&gif); return true;
    }
    qa_image image = {0};
    bool ok = false;
    if (suffix_equal(path, ".lmp")) ok = qa_image_decode_qpic(bytes, &image, error);
    else if (suffix_equal(path, ".pcx")) ok = qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &image, error);
    else if (suffix_equal(path, ".png")) ok = qa_image_decode_png(bytes, &image, error);
    else if (suffix_equal(path, ".tga")) ok = qa_image_decode_tga(bytes, QA_IMAGE_FORMAT, &image, error);
    else if (suffix_equal(path, ".bmp")) ok = qa_image_decode_bmp(bytes, QA_IMAGE_FORMAT, &image, error);
    else if (suffix_equal(path, ".jpg") || suffix_equal(path, ".jpeg")) ok = qa_image_decode_jpeg(bytes, &image, error);
    else qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unsupported original scene image: %s", path);
    if (ok) { *width = image.width; *height = image.height; }
    qa_image_free(&image); return ok;
}

static bool ordinary_mount(qa_mount_id id, void *context)
{
    qa_vfs *vfs = context;
    size_t count = qa_vfs_mount_count(vfs);
    for (size_t i = 0; i < count; ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(vfs, i, &mount) && mount.id == id) return !mount.user_overlay;
    }
    return false;
}

static bool original_image(qa_scene_resources *resources, const char *path, qa_resource *winner,
    qa_mount_id mount, const qa_vfs_acquisition *winner_opening, bool fallback,
    qa_resource **out, qa_mount_id *out_mount, qa_vfs_acquisition *out_opening, qa_error *error)
{
    *out = NULL; *out_mount = 0;
    bool acquired = winner == NULL;
    qa_error local = {0};
    qa_vfs_acquisition opening = {0};
    if (acquired && !qa_vfs_acquire_receipt(resources->vfs, path, &winner, &opening, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error != NULL) *error = local;
        return false;
    }
    if (acquired) { mount = opening.mount; winner_opening = &opening; }
    bool ok = true;
    if (!ordinary_mount(mount, resources->vfs)) {
        ok = qa_vfs_acquire_filtered_receipt(resources->vfs, path, ordinary_mount, resources->vfs, out, out_opening, &local);
        if (ok) *out_mount = out_opening->mount;
        if (!ok && local.code == QA_ERROR_NOT_FOUND) ok = true;
        if (!ok && error != NULL) *error = local;
    }
    if (ok && *out == NULL && fallback) {
        ok = qa_vfs_acquisition_copy(winner_opening, out_opening, error);
        if (ok) { qa_resource_retain(winner); *out = winner; *out_mount = mount; }
    }
    if (acquired) { qa_resource_release(winner); qa_vfs_acquisition_dispose(&opening); }
    return ok;
}

static void candidate_add(const char **candidates, size_t *count, const char *extension)
{
    for (size_t i = 0; i < *count; ++i) if (strcmp(candidates[i], extension) == 0) return;
    candidates[(*count)++] = extension;
}

bool qa_scene_image_alias_bind(qa_scene_resources *resources, const char *name,
    const qa_scene_image_alias_source *source, qa_error *error)
{
    if (!resources || !name || !source || !source->request || !*source->request ||
        !source->source_path || !*source->source_path || !admission_ready(resources, error) ||
        source->decode_options.family < QA_GAME_Q1 || source->decode_options.family > QA_GAME_Q3 ||
        source->decode_options.wrap < QA_SCENE_REPEAT || source->decode_options.wrap > QA_SCENE_CLAMP ||
        source->decode_options.filter < QA_SCENE_NEAREST || source->decode_options.filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        source->decode_options.usage < QA_IMAGE_USAGE_DEFAULT || source->decode_options.usage > QA_IMAGE_USAGE_SKY ||
        source->source_error < QA_OK || source->source_error > QA_ERROR_NOT_FOUND ||
        (!source->source && (source->source_error == QA_OK || source->source_error == QA_ERROR_MEMORY)) ||
        (source->decode_options.palette_rgb.size &&
         (source->decode_options.palette_rgb.size != 768 || !source->decode_options.palette_rgb.data)) ||
        (source->decode_options.translation.size &&
         (source->decode_options.translation.size != 256 || !source->decode_options.translation.data)) ||
        (source->logical_source && (!source->source || !source->logical_path || !*source->logical_path)) ||
        (!source->palette_attempted && (source->palette_source || source->palette_error != QA_OK)) ||
        source->palette_error < QA_OK || source->palette_error > QA_ERROR_NOT_FOUND ||
        (source->palette_attempted && !source->palette_source &&
         (source->palette_error == QA_OK || source->palette_error == QA_ERROR_MEMORY))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Image alias requires its genuine source and decode receipt"); return false;
    }
    char *normalized = qa_vfs_normalize_path(name, error);
    if (!normalized) return false;
    if (strcmp(normalized, name)) {
        free(normalized); qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Image alias must be normalized"); return false;
    }
    const qa_resource *objects[] = {source->source, source->logical_source, source->palette_source};
    const qa_vfs_acquisition *receipts[] = {source->source_opening, source->logical_opening, source->palette_opening};
    for (unsigned i = 0; i < 3; ++i) {
        if (!objects[i]) { if (receipts[i]) goto invalid; continue; }
        if (!resources->vfs || !receipts[i] || !receipts[i]->opening_present ||
            receipts[i]->resource_id != qa_resource_id(objects[i]) ||
            qa_resource_pool_find(qa_vfs_resources(resources->vfs), qa_resource_id(objects[i])) != objects[i] ||
            !qa_vfs_acquisition_retained(resources->vfs, receipts[i], error)) goto invalid;
    }
    for (const image_alias *existing = resources->aliases; existing; existing = existing->next)
        if (!strcmp(existing->name, name)) {
            image_cache key = {.options = existing->decode_options};
            memcpy(key.palette, existing->palette, sizeof(key.palette));
            memcpy(key.translation, existing->translation, sizeof(key.translation));
            bool same = existing->source == source->source && existing->logical_source == source->logical_source &&
                existing->palette_source == source->palette_source && !strcmp(existing->request, source->request) &&
                !strcmp(existing->source_path, source->source_path) &&
                !strcmp(existing->logical_path, source->logical_path ? source->logical_path : "") &&
                existing->palette_attempted == source->palette_attempted && existing->palette_error == source->palette_error &&
                existing->source_error == source->source_error &&
                same_options(&key, &source->decode_options);
            free(normalized);
            if (!same) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Image alias differs from its first retained admission");
            return same;
        }
    image_alias *alias = calloc(1, sizeof(*alias));
    if (!alias) { free(normalized); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining image alias"); return false; }
    alias->name = normalized;
    const char *texts[] = {source->request, source->source_path, source->logical_path ? source->logical_path : ""};
    char **targets[] = {&alias->request, &alias->source_path, &alias->logical_path};
    for (unsigned i = 0; i < 3; ++i) {
        size_t length = strlen(texts[i]);
        if (length == SIZE_MAX || !(*targets[i] = malloc(length + 1))) goto memory;
        memcpy(*targets[i], texts[i], length + 1);
    }
    qa_resource **held[] = {&alias->source, &alias->logical_source, &alias->palette_source};
    qa_vfs_acquisition *owned[] = {&alias->source_opening, &alias->logical_opening, &alias->palette_opening};
    for (unsigned i = 0; i < 3; ++i) if (objects[i]) {
        if (!qa_vfs_acquisition_copy(receipts[i], owned[i], error)) { scene_resource_alias_free(alias); return false; }
        *held[i] = (qa_resource *)objects[i]; qa_resource_retain(*held[i]);
    }
    alias->decode_options = source->decode_options;
    if (alias->decode_options.palette_rgb.size) {
        memcpy(alias->palette, source->decode_options.palette_rgb.data, 768);
        alias->decode_options.palette_rgb.data = alias->palette;
    }
    if (alias->decode_options.translation.size) {
        memcpy(alias->translation, source->decode_options.translation.data, 256);
        alias->decode_options.translation.data = alias->translation;
    }
    alias->palette_attempted = source->palette_attempted; alias->palette_error = source->palette_error;
    alias->source_error = source->source_error;
    image_alias **tail = &resources->aliases; while (*tail) tail = &(*tail)->next;
    *tail = alias; return true;
memory:
    scene_resource_alias_free(alias); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining image alias options"); return false;
invalid:
    free(normalized);
    if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Image alias lost its actual acquisition scope");
    return false;
}

static bool image_alias_load(qa_scene_resources *resources, const image_alias *alias,
    const qa_scene_image_options *options, qa_scene_image **out,
    qa_scene_image_load_receipt *receipt, qa_error *error)
{
    resources->registrations_started = true;
    if (!alias->source) { qa_error_set(error, alias->source_error, 0, "Retained image alias acquisition failed: %s", alias->name); return false; }
    if (!image_load_receipt(receipt, alias->source, alias->source_opening.mount, &alias->source_opening,
        alias->logical_source, alias->logical_opening.mount, &alias->logical_opening, alias->logical_path, error) ||
        !image_palette_receipt(receipt, alias->palette_source, &alias->palette_opening,
            alias->palette_attempted, alias->palette_error, error)) return false;
    qa_string_id name = 0;
    if (!qa_strings_intern_cstr(resources->names->strings, alias->name, &name, error)) return false;
    for (size_t i = 0; i < resources->cache_count; ++i) {
        image_cache *entry = resources->cache + i;
        if (entry->name == name && !entry->exact_file && same_options(entry, options)) {
            qa_scene_image_retain(entry->image); *out = entry->image; return true;
        }
    }
    qa_scene_image_options decode = alias->decode_options;
    decode.wrap = options->wrap; decode.filter = options->filter; decode.mipmap = options->mipmap;
    image_decode_context context = {.alias = alias}; qa_scene_image *image = NULL;
    if (!decode_asset_upload(resources, alias->request, alias->source_path, qa_resource_bytes(alias->source), &decode,
        options->source_q3 ? &options->source_upload : NULL, &context, &image, error)) return false;
    if (alias->logical_source && !logical_dimensions(alias->logical_source, alias->logical_path,
        &image->logical_width, &image->logical_height, error)) { qa_scene_image_release(image); return false; }
    for (size_t i = 1; i < image->animation_count; ++i) {
        qa_scene_image *frame = (qa_scene_image *)image->animation[i];
        frame->logical_width = image->logical_width; frame->logical_height = image->logical_height;
    }
    qa_scene_image_load_receipt palette = {.palette_source = alias->palette_source,
        .palette_opening = alias->palette_opening, .palette_attempted = alias->palette_attempted,
        .palette_error = alias->palette_error};
    if (!cache_add(resources, name, alias->source, alias->source_opening.mount,
        alias->logical_source, alias->logical_opening.mount, &alias->source_opening, &alias->logical_opening,
        alias->logical_path, &palette, options, false, image, error)) { qa_scene_image_release(image); return false; }
    *out = image; return true;
}

static bool image_load(qa_scene_resources *resources, const char *name,
    const qa_scene_image_options *options, bool exact_file, qa_scene_image **out,
    qa_scene_image_load_receipt *receipt, qa_error *error)
{
    if (resources == NULL || name == NULL || options == NULL || out == NULL ||
        options->family < QA_GAME_Q1 || options->family > QA_GAME_Q3 ||
        options->wrap < QA_SCENE_REPEAT || options->wrap > QA_SCENE_CLAMP ||
        options->filter < QA_SCENE_NEAREST || options->filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        options->usage < QA_IMAGE_USAGE_DEFAULT || options->usage > QA_IMAGE_USAGE_SKY ||
        (options->palette_rgb.size != 0 && (options->palette_rgb.size != 768 || options->palette_rgb.data == NULL)) ||
        (options->translation.size != 0 && (options->translation.size != 256 || options->translation.data == NULL))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene image load request"); return false;
    }
    if (!admission_ready(resources, error)) return false;
    if (options->source_q3 && (options->family != QA_GAME_Q3 ||
        options->source_upload.mipmap != options->mipmap)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source image upload requires its real Q3 admission and mip policy"); return false;
    }
    if (options->source_q3 && !qa_q3_image_upload_options_valid(&options->source_upload, error)) return false;
    if (options->source_q3 && !exact_file) {
        qa_string_id cached_name = qa_strings_find(resources->names->strings,
            (qa_bytes){(const uint8_t *)name, strlen(name)});
        for (size_t i = 0; cached_name && i < resources->cache_count; ++i) {
            const image_cache *entry = resources->cache + i;
            if (entry->name == cached_name && entry->options.source_q3 && !entry->exact_file) {
                if (!image_load_receipt(receipt, entry->source_record, entry->source_mount,
                    &entry->source_opening, entry->logical_record, entry->logical_mount,
                    &entry->logical_opening, entry->logical_path, error)) return false;
                if (!image_palette_receipt(receipt, entry->palette_source, &entry->palette_opening,
                    entry->palette_attempted, entry->palette_error, error)) return false;
                qa_scene_image_retain(entry->image); *out = entry->image; return true;
            }
        }
    }
    if (!exact_file) for (const image_alias *alias = resources->aliases; alias; alias = alias->next)
        if (!strcmp(alias->name, name)) return image_alias_load(resources, alias, options, out, receipt, error);
    if (!exact_file && (strcmp(name, "*white") == 0 || strcmp(name, "$whiteimage") == 0)) {
        qa_scene_image *white = options->source_q3 ? resources->source_white : resources->white;
        if (!white) return policy_error(error, "Source white image has no actual renderer admission");
        qa_scene_image_retain(white); *out = white; return true;
    }
    if (!exact_file && strcmp(name, "*default") == 0) {
        qa_scene_image *missing = options->source_q3 ? resources->source_missing : resources->missing;
        if (!missing) return policy_error(error, "Source default image has no actual renderer admission");
        qa_scene_image_retain(missing); *out = missing; return true;
    }
    if (!exact_file && options->source_q3 && !strcmp(name, "*identityLight")) {
        if (!resources->source_identity) return policy_error(error, "Source identity image has no actual renderer admission");
        qa_scene_image_retain(resources->source_identity); *out = resources->source_identity; return true;
    }
    if (!exact_file && options->source_q3) {
        qa_scene_image *builtin = !strcmp(name, "*scratch") ? resources->source_scratch[31] :
            !strcmp(name, "*dlight") ? resources->source_dlight : !strcmp(name, "*fog") ? resources->source_fog : NULL;
        if (builtin) { qa_scene_image_retain(builtin); *out = builtin; return true; }
    }
    resources->registrations_started = true;
    if (resources->vfs == NULL) { qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "no scene VFS for %s", name); return false; }
    size_t length = strlen(name);
    if (length > (size_t)PTRDIFF_MAX - 8) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene image name too long"); return false; }
    const char *slash = strrchr(name, '/'), *dot = strrchr(name, '.');
    bool explicit_extension = dot != NULL && (slash == NULL || dot > slash);
    if (exact_file && (!explicit_extension || !dot[1])) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "exact scene image requires an explicit file extension"); return false;
    }
    size_t base_length = explicit_extension ? (size_t)(dot-name) : length;
    bool wall = options->usage == QA_IMAGE_USAGE_WALL || (options->usage == QA_IMAGE_USAGE_DEFAULT &&
        (strncmp(name, "textures/", 9) == 0 || suffix_equal(name, ".wal")));
    static const char *const q1[] = {".lmp",".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif"};
    const char *const q2[] = {".png",".jpg",".tga",".jpeg",".bmp",".gif",wall ? ".wal" : ".pcx"};
    static const char *const q3[] = {".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif"};
    const char *const *source_extensions = options->family == QA_GAME_Q1 ? q1 : options->family == QA_GAME_Q2 ? q2 : q3;
    size_t source_count = options->family == QA_GAME_Q1 ? 8 : 7;
    const qa_scene_image_policy *policy = resources->has_policy[options->family] ? &resources->policies[options->family] : NULL;
    const char *overrides[6], *extensions[8], *candidates[15];
    size_t override_count = 0, extension_count = 0, candidate_count = 0;
    if (policy != NULL && !policy->source_formats) {
        for (size_t i = 0; i < policy->format_count; ++i)
            overrides[override_count++] = format_extensions[policy->formats[i]];
        for (size_t i = 0; i < override_count; ++i) extensions[extension_count++] = overrides[i];
        if (options->family == QA_GAME_Q1) extensions[extension_count++] = ".lmp";
        extensions[extension_count++] = options->family == QA_GAME_Q2 && wall ? ".wal" : ".pcx";
    } else {
        for (size_t i = 0; i < source_count; ++i) {
            const char *extension = source_extensions[i];
            extensions[extension_count++] = extension;
            if (strcmp(extension, ".lmp") != 0 && strcmp(extension, ".wal") != 0 && strcmp(extension, ".pcx") != 0)
                overrides[override_count++] = extension;
        }
    }
    const char *requested = explicit_extension ? dot : options->family == QA_GAME_Q2 && wall ? ".wal" : NULL;
    bool native = requested != NULL && (suffix_equal(requested, ".pcx") || suffix_equal(requested, ".wal") ||
        (options->family == QA_GAME_Q1 && suffix_equal(requested, ".lmp")));
    bool truecolor = false;
    for (size_t i = 0; requested != NULL && i < 6; ++i)
        if (suffix_equal(requested, format_extensions[i])) truecolor = true;
    qa_scene_image_usage usage = options->usage == QA_IMAGE_USAGE_DEFAULT ?
        (wall ? QA_IMAGE_USAGE_WALL : QA_IMAGE_USAGE_PICTURE) : options->usage;
    bool override = policy != NULL && policy->override_level >= 1 &&
        (policy->override_usages & (1u << (unsigned)(usage-1))) != 0 &&
        (native || (policy->override_level > 1 && truecolor));
    if (!exact_file && override) for (size_t i = 0; i < override_count; ++i) candidate_add(candidates, &candidate_count, overrides[i]);
    if (requested != NULL) candidate_add(candidates, &candidate_count, requested);
    if (!exact_file) for (size_t i = 0; i < extension_count; ++i) candidate_add(candidates, &candidate_count, extensions[i]);
    char *path = malloc(length + 8), *original_path = malloc(length + 8);
    if (path == NULL || original_path == NULL) { free(path); free(original_path); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate image search path"); return false; }
    qa_string_id name_id;
    if (!qa_strings_intern_cstr(resources->names->strings, name, &name_id, error)) {
        free(path); free(original_path); return false;
    }
    bool result = false, failed = false;
    for (size_t candidate = 0; candidate < candidate_count; ++candidate) {
        memcpy(path, name, base_length); strcpy(path + base_length, candidates[candidate]);
        qa_resource *resource = NULL; qa_error local = {0};
        qa_vfs_acquisition opening = {0}, original_opening = {0};
        qa_scene_image_load_receipt palette_observation = {0};
        if (!qa_vfs_acquire_receipt(resources->vfs, path, &resource, &opening, &local)) {
            if (local.code == QA_ERROR_NOT_FOUND) continue;
            if (error != NULL) *error = local;
            failed = true; break;
        }
        qa_mount_id mount = opening.mount;
        qa_resource *original = NULL;
        qa_mount_id original_mount = 0;
        const char *logical_path = original_path;
        bool native_size = false;
        if (options->family == QA_GAME_Q2 && !suffix_equal(path, ".wal") &&
            (suffix_equal(name, ".wal") || (!explicit_extension && wall))) {
            memcpy(original_path, name, base_length); strcpy(original_path+base_length, ".wal");
            native_size = true;
        } else if ((options->family == QA_GAME_Q2 && suffix_equal(name, ".pcx") && !suffix_equal(path, ".pcx")) ||
                   (options->family == QA_GAME_Q1 && requested != NULL && suffix_equal(requested, ".lmp") && !suffix_equal(path, ".lmp"))) {
            memcpy(original_path, name, length+1); native_size = true;
        }
        bool size_ok = exact_file || !native_size || original_image(resources, original_path, NULL, 0, NULL,
            true, &original, &original_mount, &original_opening, error);
        if (!exact_file && size_ok && original == NULL) {
            logical_path = path;
            size_ok = original_image(resources, path, resource, mount, &opening, false,
                &original, &original_mount, &original_opening, error);
        }
        bool observed = image_load_receipt(receipt, resource, mount, &opening, original,
            original_mount, &original_opening, logical_path, error);
        if (!size_ok || !observed) { failed = true; goto image_done; }
        uint64_t source_id = qa_resource_id(resource), logical_id = original == NULL ? 0 : qa_resource_id(original);
        qa_scene_image *image = NULL;
        for (size_t i = 0; i < resources->cache_count; ++i) {
            image_cache *entry = &resources->cache[i];
            if (entry->name == name_id && entry->source == source_id && entry->logical_source == logical_id &&
                entry->exact_file == exact_file && same_options(entry, options)) {
                if (!image_palette_receipt(receipt, entry->palette_source, &entry->palette_opening,
                    entry->palette_attempted, entry->palette_error, error)) { failed = true; goto image_done; }
                qa_scene_image_retain(entry->image); image = entry->image; break;
            }
        }
        if (image != NULL) { *out = image; result = true; goto image_done; }
        image_decode_context context = {.observation = receipt ? receipt : &palette_observation, .source = resource};
        if (!decode_asset_upload(resources, name, path, qa_resource_bytes(resource), options, NULL, &context, &image, error)) {
            failed = true; goto image_done;
        }
        if (original != NULL && !logical_dimensions(original, logical_path, &image->logical_width, &image->logical_height, error)) {
            failed = true; qa_scene_image_release(image); goto image_done;
        }
        for (size_t i = 1; i < image->animation_count; ++i) {
            qa_scene_image *frame = (qa_scene_image *)image->animation[i];
            frame->logical_width = image->logical_width; frame->logical_height = image->logical_height;
        }
        if (!cache_add(resources, name_id, resource, mount, original, original_mount, &opening,
            &original_opening, logical_path, context.observation, options, exact_file, image, error)) {
            failed = true; qa_scene_image_release(image); goto image_done;
        }
        *out = image; result = true;
image_done:
        qa_resource_release(original); qa_resource_release(resource);
        qa_vfs_acquisition_dispose(&opening); qa_vfs_acquisition_dispose(&original_opening);
        qa_scene_image_load_receipt_dispose(&palette_observation);
        break;
    }
    free(path); free(original_path);
    if (!result && !failed) qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene image not found: %s", name);
    return result;
}

bool qa_scene_image_load(qa_scene_resources *resources, const char *name,
    const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{
    return image_load(resources, name, options, false, out, NULL, error);
}

bool qa_scene_image_load_exact(qa_scene_resources *resources, const char *name,
    const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{
    return image_load(resources, name, options, true, out, NULL, error);
}
bool qa_scene_image_load_observed(qa_scene_resources *resources, const char *name,
    const qa_scene_image_options *options, qa_scene_image **out,
    qa_scene_image_load_receipt *receipt, qa_error *error)
{
    if (!receipt || receipt->source || receipt->logical_source || receipt->source_mount || receipt->logical_mount ||
        receipt->logical_path || receipt->palette_source || receipt->palette_attempted || receipt->palette_error != QA_OK ||
        !out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Observed image load requires empty image and receipt outputs");
        return false;
    }
    const qa_vfs_acquisition *openings[] = {&receipt->source_opening, &receipt->logical_opening, &receipt->palette_opening};
    for (unsigned i = 0; i < 3; ++i) {
        const qa_vfs_acquisition *opening = openings[i];
        if (opening->mount || opening->resource_id || opening->path || opening->lookup_path || opening->link_source ||
            opening->link_target || opening->opening_present || opening->opening.rank || opening->opening.order ||
            opening->opening.order_count || opening->opening.prefix || opening->opening.user_overlay) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Observed image load requires empty owned opening receipts"); return false;
        }
    }
    return image_load(resources, name, options, false, out, receipt, error);
}

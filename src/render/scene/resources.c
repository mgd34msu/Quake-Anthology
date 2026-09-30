#include "resources_internal.h"

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
};

static const char *const format_extensions[] = {".png", ".jpg", ".tga", ".jpeg", ".bmp", ".gif"};
static bool image_from_rgba(qa_scene_resources *, const char *, const qa_image *,
                            const qa_scene_image_options *, qa_scene_image **, qa_error *);

qa_scene_geometry *qa_scene_geometry_adopt(qa_scene_vertex *vertices, uint32_t *indices,
                                         qa_error *error)
{
    qa_scene_geometry *geometry = malloc(sizeof(*geometry));
    if (geometry == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene geometry ownership");
        return NULL;
    }
    atomic_init(&geometry->active, 1);
    atomic_init(&geometry->references, 1);
    geometry->vertices = vertices;
    geometry->indices = indices;
    return geometry;
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
    }
    qa_scene_geometry_cache_release(geometry);
}

bool qa_scene_geometry_active(const qa_scene_geometry *geometry)
{
    return geometry != NULL && atomic_load_explicit(&geometry->active, memory_order_acquire) != 0;
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
    const char *start = formats, *end = formats + strlen(formats);
    while (start < end && isspace((unsigned char)*start)) ++start;
    while (end > start && isspace((unsigned char)end[-1])) --end;
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

bool qa_scene_resources_set_image_policy(qa_scene_resources *resources, qa_scene_family family,
                                         const qa_scene_image_policy *policy, qa_error *error)
{
    if (resources == NULL || family < QA_SCENE_Q1 || family > QA_SCENE_Q3 ||
        resources->registrations_started || (policy != NULL && policy->format_count > 6)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "image policy must be valid and set before content loads");
        return false;
    }
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
    } else if (family == QA_SCENE_Q2) {
        if (!qa_scene_image_policy_controls(1, UINT32_MAX, "png jpg tga jpeg bmp gif", &copy, error)) return false;
    }
    resources->policies[family] = copy;
    resources->has_policy[family] = policy != NULL || family == QA_SCENE_Q2;
    return true;
}

bool qa_scene_resources_set_fullbright_first(qa_scene_resources *resources, unsigned first, qa_error *error)
{
    if (resources == NULL || first > 256 || resources->registrations_started) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "fullbright range must be set before content loads and start at 0 through 256");
        return false;
    }
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
    for (size_t i = 0; i < image->level_count; ++i) free((void *)owned->levels[i].pixels);
    free(owned->levels);
    if (--owned->lineage->references == 0) free(owned->lineage);
    names_release(owned->names);
    free(owned);
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
            frames[i] = sampled;
        }
    }
    *out = image; return true;
}

qa_scene_resources *qa_scene_resources_create(qa_vfs *vfs, qa_error *error)
{
    qa_scene_resources *resources = calloc(1, sizeof(*resources));
    if (resources == NULL) goto failed;
    resources->names = calloc(1, sizeof(*resources->names));
    if (resources->names == NULL) { free(resources); goto failed; }
    resources->names->references = 1;
    if (!qa_strings_create(&resources->names->strings, error)) {
        free(resources->names); free(resources); return NULL;
    }
    resources->vfs = vfs;
    resources->fullbright_first = 224;
    if (!qa_scene_resources_set_image_policy(resources, QA_SCENE_Q2, NULL, error)) {
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
    qa_scene_image_options missing_options = {.family = QA_SCENE_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_NEAREST, .mipmap = true};
    if (!image_from_rgba(resources, "*default", &missing, &missing_options, &resources->missing, error)) {
        qa_scene_resources_destroy(resources); return NULL;
    }
    resources->missing->border = (qa_scene_vec4){0,0,0,1};
    resources->registrations_started = false;
    return resources;
failed:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene resource service");
    return NULL;
}

void qa_scene_resources_destroy(qa_scene_resources *resources)
{
    if (resources == NULL) return;
    for (size_t i = 0; i < resources->cache_count; ++i) qa_scene_image_release(resources->cache[i].image);
    free(resources->cache);
    for (size_t i = 0; i < 3; ++i) qa_buffer_free(&resources->palettes[i]);
    qa_scene_image_release(resources->white);
    qa_scene_image_release(resources->missing);
    names_release(resources->names);
    free(resources);
}

const qa_scene_image *qa_scene_white(const qa_scene_resources *resources) { return resources->white; }
const qa_scene_image *qa_scene_missing(const qa_scene_resources *resources) { return resources->missing; }

bool qa_scene_resources_palette(qa_scene_resources *resources, qa_scene_family family,
                                qa_bytes *out, qa_error *error)
{
    if (resources == NULL || out == NULL || family < QA_SCENE_Q1 || family > QA_SCENE_Q3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene palette request"); return false;
    }
    qa_buffer *stored = &resources->palettes[family];
    if (stored->size != 0) { *out = (qa_bytes){stored->data, stored->size}; return true; }
    if (resources->vfs == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene has no mounted palette source"); return false;
    }
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(resources->vfs, family == QA_SCENE_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx",
                        &resource, NULL, error)) return false;
    qa_bytes bytes = qa_resource_bytes(resource);
    uint8_t *palette = malloc(768);
    if (palette == NULL) {
        qa_resource_release(resource); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene palette"); return false;
    }
    bool ok = true;
    if (family == QA_SCENE_Q1) {
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
    qa_resource_release(resource);
    if (!ok) { free(palette); return false; }
    *stored = (qa_buffer){palette,768};
    *out = (qa_bytes){palette,768};
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

static bool image_from_rgba(qa_scene_resources *resources, const char *name, const qa_image *source,
                            const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{
    qa_image scaled = {0};
    const qa_image *base = source;
    if (options->family == QA_SCENE_Q2 && options->mipmap) {
        qa_gamma_options gamma = {.profile = QA_GAMMA_Q2, .gamma = 1, .intensity = 2};
        if (!qa_image_apply_gamma(source, &gamma, &scaled, error)) return false;
        base = &scaled;
    }
    qa_mip_chain chain = {0};
    if (options->mipmap && !qa_image_mip_chain(base, QA_MIP_BOX, &chain, error)) {
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
        const qa_image *image = &chain.levels[i];
        levels[i+1] = (qa_scene_image_level){image->width,image->height,image->rgba.data,image->rgba.size};
    }
    bool ok = qa_scene_image_create(resources, name, QA_SCENE_RGBA8, levels, count, options->wrap,
                                    options->filter, (qa_scene_vec4){0}, out, error);
    free(levels); qa_mip_chain_free(&chain); qa_image_free(&scaled);
    return ok;
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
    size_t head = 0, tail = 1; queue[0] = 0; image->indices.data[0] = 255;
    while (head < tail) {
        size_t pixel = queue[head++], x = pixel % image->width, y = pixel / image->width;
        size_t adjacent[4] = {x > 0 ? pixel-1 : SIZE_MAX, x+1 < image->width ? pixel+1 : SIZE_MAX,
            y > 0 ? pixel-image->width : SIZE_MAX, y+1 < image->height ? pixel+image->width : SIZE_MAX};
        uint8_t color = black;
        for (size_t i = 0; i < 4; ++i) {
            size_t next = adjacent[i]; if (next == SIZE_MAX) continue;
            uint8_t value = image->indices.data[next];
            if (value == fill) { image->indices.data[next] = 255; queue[tail++] = next; }
            else if (value != 255) color = value;
        }
        image->indices.data[pixel] = color;
    }
    free(queue); return true;
}

static bool indexed_rgba(qa_scene_resources *resources, qa_image *image,
                         const qa_scene_image_options *options, bool pcx, qa_error *error)
{
    qa_bytes palette = options->palette_rgb;
    uint8_t local_palette[768];
    bool q2_indexed = pcx && options->family == QA_SCENE_Q2 &&
        (options->usage == QA_IMAGE_USAGE_SKIN || options->usage == QA_IMAGE_USAGE_SPRITE);
    if (q2_indexed && palette.size == 0) {
        qa_error local = {0};
        if (!qa_scene_resources_palette(resources, options->family, &palette, &local) && local.code != QA_ERROR_NOT_FOUND) {
            if (error != NULL) *error = local;
            return false;
        }
    }
    q2_indexed = q2_indexed && palette.size != 0;
    if ((pcx || palette.size == 0) && image->palette.size >= 1024 && !q2_indexed) {
        for (size_t i = 0; i < 256; ++i) memcpy(local_palette + i*3, image->palette.data + i*4, 3);
        palette = (qa_bytes){local_palette,sizeof(local_palette)};
    }
    if (palette.size == 0 && !qa_scene_resources_palette(resources, options->family, &palette, error)) return false;
    if (palette.size != 768 || image->index_bytes != 1 || image->indices.size != (size_t)image->width * image->height) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "scene indexed image requires an 8-bit palette image"); return false;
    }
    if (q2_indexed && options->usage == QA_IMAGE_USAGE_SKIN && !flood_skin(image, palette, error)) return false;
    qa_palette_options conversion = {.transparent_index = options->transparent ? options->transparent_index : -1,
        .fullbright_first = -1, .fullbright_last = -1,
        .translation = options->translation.size == 256 ? options->translation.data : NULL,
        .layer = QA_PALETTE_COMBINED};
    if (options->family == QA_SCENE_Q2 && pcx) conversion.transparent_index = 255;
    qa_image expanded = {0};
    qa_indexed_level indexed = {.width = image->width, .height = image->height, .indices = image->indices};
    if (!qa_image_expand_indexed(&indexed, palette, &conversion, &expanded, error)) return false;
    if (options->fullbright_only) for (size_t i = 0; i < image->indices.size; ++i) {
        unsigned index = image->indices.data[i];
        if (index < resources->fullbright_first || (int)index == conversion.transparent_index)
            memset(expanded.rgba.data+i*4, 0, 4);
    }
    /* Preserve source Q2 RGB beside transparent texels for filtered edges. */
    if (pcx && options->family == QA_SCENE_Q2 && !q2_indexed) {
        size_t count = image->indices.size;
        for (size_t i = 0; i < count; ++i) if (image->indices.data[i] == 255) {
            size_t adjacent[4] = {i > image->width ? i-image->width : SIZE_MAX,
                i < count-image->width ? i+image->width : SIZE_MAX, i > 0 ? i-1 : SIZE_MAX,
                i+1 < count ? i+1 : SIZE_MAX};
            uint8_t color = 0;
            for (size_t n = 0; n < 4; ++n) if (adjacent[n] != SIZE_MAX && image->indices.data[adjacent[n]] != 255) {
                color = image->indices.data[adjacent[n]]; break;
            }
            memcpy(expanded.rgba.data + i*4, palette.data + (size_t)color*3, 3);
        }
    }
    qa_buffer_free(&image->rgba);
    image->rgba = expanded.rgba;
    expanded.rgba = (qa_buffer){0};
    qa_image_free(&expanded);
    return true;
}

static bool decode_asset(qa_scene_resources *resources, const char *request, const char *path,
                          qa_bytes bytes, const qa_scene_image_options *options,
                          qa_scene_image **out, qa_error *error)
{
    if (options->fullbright_only && !suffix_equal(path, ".lmp") && !suffix_equal(path, ".mip")) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene image has no indexed fullbright layer");
        return false;
    }
    if (suffix_equal(path, ".gif")) {
        qa_gif gif = {0};
        if (!qa_image_decode_gif(bytes, &gif, error)) return false;
        qa_scene_image *first = NULL;
        if (gif.frame_count == 0 || !image_from_rgba(resources, request, &gif.frames[0].image, options, &first, error)) {
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
                if (!image_from_rgba(resources, request, &gif.frames[i].image, options, &frame, error)) {
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
    qa_image_policy policy = options->family == QA_SCENE_Q3 ? QA_IMAGE_Q3 : QA_IMAGE_FORMAT;
    if (suffix_equal(path, ".wal") || suffix_equal(path, ".mip")) {
        qa_mip_texture mip = {0};
        ok = suffix_equal(path, ".wal") ? qa_image_decode_wal(bytes, &mip, error) : qa_image_decode_mip(bytes, &mip, error);
        if (ok && !mip.external) {
            if (options->family != QA_SCENE_Q2 || !options->mipmap) {
                qa_image images[4] = {0};
                qa_scene_image_level levels[4];
                for (size_t i = 0; i < 4 && ok; ++i) {
                    images[i] = (qa_image){.width = mip.levels[i].width, .height = mip.levels[i].height,
                        .indices = mip.levels[i].indices, .index_bytes = 1};
                    ok = indexed_rgba(resources, &images[i], options, false, error);
                    images[i].indices = (qa_buffer){0};
                    if (ok) levels[i] = (qa_scene_image_level){images[i].width,images[i].height,
                        images[i].rgba.data,images[i].rgba.size};
                }
                if (ok) ok = qa_scene_image_create(resources, request, QA_SCENE_RGBA8, levels, 4,
                    options->wrap, options->filter, (qa_scene_vec4){0}, out, error);
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
    else if (pcx) ok = qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &decoded, error);
    else if (suffix_equal(path, ".tga")) ok = qa_image_decode_tga(bytes, policy, &decoded, error);
    else if (suffix_equal(path, ".png")) ok = qa_image_decode_png(bytes, &decoded, error);
    else if (suffix_equal(path, ".bmp")) ok = qa_image_decode_bmp(bytes, QA_IMAGE_FORMAT, &decoded, error);
    else if (suffix_equal(path, ".jpg") || suffix_equal(path, ".jpeg")) ok = qa_image_decode_jpeg(bytes, &decoded, error);
    else qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unsupported scene image extension: %s", path);
    if (ok && decoded.indices.size != 0 && (pcx || decoded.rgba.size == 0 || options->translation.size != 0 || options->fullbright_only))
        ok = indexed_rgba(resources, &decoded, options, pcx, error);
    qa_scene_image_options upload = *options;
    if (suffix_equal(path, ".lmp") && options->family != QA_SCENE_Q2) upload.mipmap = false;
    if (ok) ok = image_from_rgba(resources, request, &decoded, &upload, out, error);
    qa_image_free(&decoded);
    return ok;
}

static bool same_options(const image_cache *entry, const qa_scene_image_options *options)
{
    const qa_scene_image_options *a = &entry->options;
    return a->family == options->family && a->wrap == options->wrap && a->filter == options->filter &&
        a->usage == options->usage && a->mipmap == options->mipmap && a->transparent == options->transparent &&
        a->fullbright_only == options->fullbright_only && a->transparent_index == options->transparent_index &&
        a->palette_rgb.size == options->palette_rgb.size && a->translation.size == options->translation.size &&
        (options->palette_rgb.size == 0 || memcmp(entry->palette, options->palette_rgb.data, 768) == 0) &&
        (options->translation.size == 0 || memcmp(entry->translation, options->translation.data, 256) == 0);
}

static bool cache_add(qa_scene_resources *resources, qa_string_id name, uint64_t source,
                      uint64_t logical_source, const qa_scene_image_options *options,
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
    image_cache *entry = &resources->cache[resources->cache_count++];
    *entry = (image_cache){.source = source, .logical_source = logical_source, .name = name, .options = *options, .image = image};
    /* Stored span pointers are deliberately not used: cache array relocation
     * cannot invalidate option identity. Compare the embedded bytes above. */
    entry->options.palette_rgb.data = NULL; entry->options.translation.data = NULL;
    if (options->palette_rgb.size != 0) memcpy(entry->palette, options->palette_rgb.data, 768);
    if (options->translation.size != 0) memcpy(entry->translation, options->translation.data, 256);
    qa_scene_image_retain(image);
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
                            qa_mount_id mount, bool fallback, qa_resource **out, qa_error *error)
{
    *out = NULL;
    bool acquired = winner == NULL;
    qa_error local = {0};
    if (acquired && !qa_vfs_acquire(resources->vfs, path, &winner, &mount, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error != NULL) *error = local;
        return false;
    }
    bool ok = true;
    if (!ordinary_mount(mount, resources->vfs)) {
        ok = qa_vfs_acquire_filtered(resources->vfs, path, ordinary_mount, resources->vfs, out, NULL, &local);
        if (!ok && local.code == QA_ERROR_NOT_FOUND) ok = true;
        if (!ok && error != NULL) *error = local;
    }
    if (ok && *out == NULL && fallback) { qa_resource_retain(winner); *out = winner; }
    if (acquired) qa_resource_release(winner);
    return ok;
}

static void candidate_add(const char **candidates, size_t *count, const char *extension)
{
    for (size_t i = 0; i < *count; ++i) if (strcmp(candidates[i], extension) == 0) return;
    candidates[(*count)++] = extension;
}

bool qa_scene_image_load(qa_scene_resources *resources, const char *name,
                         const qa_scene_image_options *options, qa_scene_image **out, qa_error *error)
{
    if (resources == NULL || name == NULL || options == NULL || out == NULL ||
        options->family < QA_SCENE_Q1 || options->family > QA_SCENE_Q3 ||
        options->wrap < QA_SCENE_REPEAT || options->wrap > QA_SCENE_CLAMP ||
        options->filter < QA_SCENE_NEAREST || options->filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        options->usage < QA_IMAGE_USAGE_DEFAULT || options->usage > QA_IMAGE_USAGE_SKY ||
        (options->palette_rgb.size != 0 && (options->palette_rgb.size != 768 || options->palette_rgb.data == NULL)) ||
        (options->translation.size != 0 && (options->translation.size != 256 || options->translation.data == NULL))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene image load request"); return false;
    }
    if (strcmp(name, "*white") == 0 || strcmp(name, "$whiteimage") == 0) {
        qa_scene_image_retain(resources->white); *out = resources->white; return true;
    }
    if (strcmp(name, "*default") == 0) {
        qa_scene_image_retain(resources->missing); *out = resources->missing; return true;
    }
    resources->registrations_started = true;
    if (resources->vfs == NULL) { qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "no scene VFS for %s", name); return false; }
    size_t length = strlen(name);
    if (length > (size_t)PTRDIFF_MAX - 8) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene image name too long"); return false; }
    const char *slash = strrchr(name, '/'), *dot = strrchr(name, '.');
    bool explicit_extension = dot != NULL && (slash == NULL || dot > slash);
    size_t base_length = explicit_extension ? (size_t)(dot-name) : length;
    bool wall = options->usage == QA_IMAGE_USAGE_WALL || (options->usage == QA_IMAGE_USAGE_DEFAULT &&
        (strncmp(name, "textures/", 9) == 0 || suffix_equal(name, ".wal")));
    static const char *const q1[] = {".lmp",".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif"};
    const char *const q2[] = {".png",".jpg",".tga",".jpeg",".bmp",".gif",wall ? ".wal" : ".pcx"};
    static const char *const q3[] = {".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif"};
    const char *const *source_extensions = options->family == QA_SCENE_Q1 ? q1 : options->family == QA_SCENE_Q2 ? q2 : q3;
    size_t source_count = options->family == QA_SCENE_Q1 ? 8 : 7;
    const qa_scene_image_policy *policy = resources->has_policy[options->family] ? &resources->policies[options->family] : NULL;
    const char *overrides[6], *extensions[8], *candidates[15];
    size_t override_count = 0, extension_count = 0, candidate_count = 0;
    if (policy != NULL && !policy->source_formats) {
        for (size_t i = 0; i < policy->format_count; ++i)
            overrides[override_count++] = format_extensions[policy->formats[i]];
        for (size_t i = 0; i < override_count; ++i) extensions[extension_count++] = overrides[i];
        if (options->family == QA_SCENE_Q1) extensions[extension_count++] = ".lmp";
        extensions[extension_count++] = options->family == QA_SCENE_Q2 && wall ? ".wal" : ".pcx";
    } else {
        for (size_t i = 0; i < source_count; ++i) {
            const char *extension = source_extensions[i];
            extensions[extension_count++] = extension;
            if (strcmp(extension, ".lmp") != 0 && strcmp(extension, ".wal") != 0 && strcmp(extension, ".pcx") != 0)
                overrides[override_count++] = extension;
        }
    }
    const char *requested = explicit_extension ? dot : options->family == QA_SCENE_Q2 && wall ? ".wal" : NULL;
    bool native = requested != NULL && (suffix_equal(requested, ".pcx") || suffix_equal(requested, ".wal") ||
        (options->family == QA_SCENE_Q1 && suffix_equal(requested, ".lmp")));
    bool truecolor = false;
    for (size_t i = 0; requested != NULL && i < 6; ++i)
        if (suffix_equal(requested, format_extensions[i])) truecolor = true;
    qa_scene_image_usage usage = options->usage == QA_IMAGE_USAGE_DEFAULT ?
        (wall ? QA_IMAGE_USAGE_WALL : QA_IMAGE_USAGE_PICTURE) : options->usage;
    bool override = policy != NULL && policy->override_level >= 1 &&
        (policy->override_usages & (1u << (unsigned)(usage-1))) != 0 &&
        (native || (policy->override_level > 1 && truecolor));
    if (override) for (size_t i = 0; i < override_count; ++i) candidate_add(candidates, &candidate_count, overrides[i]);
    if (requested != NULL) candidate_add(candidates, &candidate_count, requested);
    for (size_t i = 0; i < extension_count; ++i) candidate_add(candidates, &candidate_count, extensions[i]);
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
        qa_mount_id mount = 0;
        if (!qa_vfs_acquire(resources->vfs, path, &resource, &mount, &local)) {
            if (local.code == QA_ERROR_NOT_FOUND) continue;
            if (error != NULL) *error = local;
            failed = true; break;
        }
        qa_resource *original = NULL;
        const char *logical_path = original_path;
        bool native_size = false;
        if (options->family == QA_SCENE_Q2 && !suffix_equal(path, ".wal") &&
            (suffix_equal(name, ".wal") || (!explicit_extension && wall))) {
            memcpy(original_path, name, base_length); strcpy(original_path+base_length, ".wal");
            native_size = true;
        } else if ((options->family == QA_SCENE_Q2 && suffix_equal(name, ".pcx") && !suffix_equal(path, ".pcx")) ||
                   (options->family == QA_SCENE_Q1 && requested != NULL && suffix_equal(requested, ".lmp") && !suffix_equal(path, ".lmp"))) {
            memcpy(original_path, name, length+1); native_size = true;
        }
        bool size_ok = !native_size || original_image(resources, original_path, NULL, 0, true, &original, error);
        if (size_ok && original == NULL) {
            logical_path = path;
            size_ok = original_image(resources, path, resource, mount, false, &original, error);
        }
        if (!size_ok) {
            qa_resource_release(original); qa_resource_release(resource); failed = true; break;
        }
        uint64_t source_id = qa_resource_id(resource), logical_id = original == NULL ? 0 : qa_resource_id(original);
        qa_scene_image *image = NULL;
        for (size_t i = 0; i < resources->cache_count; ++i) {
            image_cache *entry = &resources->cache[i];
            if (entry->name == name_id && entry->source == source_id && entry->logical_source == logical_id && same_options(entry, options)) {
                qa_scene_image_retain(entry->image); image = entry->image; break;
            }
        }
        if (image != NULL) { *out = image; result = true; qa_resource_release(original); qa_resource_release(resource); break; }
        if (!decode_asset(resources, name, path, qa_resource_bytes(resource), options, &image, error)) {
            failed = true; qa_resource_release(original); qa_resource_release(resource); break;
        }
        qa_resource_release(resource);
        if (original != NULL && !logical_dimensions(original, logical_path, &image->logical_width, &image->logical_height, error)) {
            failed = true; qa_resource_release(original); qa_scene_image_release(image); break;
        }
        qa_resource_release(original);
        for (size_t i = 1; i < image->animation_count; ++i) {
            qa_scene_image *frame = (qa_scene_image *)image->animation[i];
            frame->logical_width = image->logical_width; frame->logical_height = image->logical_height;
        }
        if (!cache_add(resources, name_id, source_id, logical_id, options, image, error)) {
            failed = true; qa_scene_image_release(image); break;
        }
        *out = image; result = true; break;
    }
    free(path); free(original_path);
    if (!result && !failed) qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "scene image not found: %s", name);
    return result;
}

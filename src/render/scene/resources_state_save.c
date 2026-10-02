#include "resources_internal.h"
#include "qa/scene_resource_save.h"
#include "qa/source_save.h"
#include "qa/vfs_view_save.h"
#include "image_options_save.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }

static bool text_field(qa_source_save_io *io, const char **text, char **owned)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!reading && !*text) return false;
    size_t size = reading ? 0 : *text ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX) || size == SIZE_MAX) return false;
    char *value = reading ? malloc(size + 1) : (char *)*text;
    if (!value) return fail(io->error, QA_ERROR_MEMORY, "allocating resource continuation text");
    if (reading) *owned = value;
    if (!qa_source_save_bytes(io, value, size) || memchr(value, 0, size)) return false;
    if (reading) value[size] = 0, *text = value;
    return true;
}

static bool image_field(qa_source_save_io *io, const qa_scene_resource_checkpoint_refs *refs,
    qa_scene_resources *owner, qa_scene_image **image)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint64_t key = 0;
    if (!reading && (!*image || !refs->image_encode(refs->context, *image, &key, io->error))) return false;
    if (!qa_source_save_u64(io, &key)) return false;
    if (reading) {
        const qa_scene_image *decoded = NULL;
        if (!refs->image_decode(refs->context, key, &decoded, io->error) || !decoded) return false;
        bool found = false;
        for (const owned_image *entry = owner->names->images; entry; entry = entry->next)
            if (&entry->image == decoded) { found = true; break; }
        if (!found) return false;
        qa_scene_image_retain(decoded); *image = (qa_scene_image *)decoded;
    }
    return true;
}
static bool same_image(const qa_scene_image *a, const qa_scene_image *b)
{
    if (!a || !b || strcmp(a->name, b->name) || a->kind != b->kind || a->wrap != b->wrap ||
        a->filter != b->filter || a->revision != b->revision || a->logical_width != b->logical_width ||
        a->logical_height != b->logical_height || a->level_count != b->level_count ||
        a->animation_count != b->animation_count || a->border.x != b->border.x || a->border.y != b->border.y ||
        a->border.z != b->border.z || a->border.w != b->border.w) return false;
    for (size_t i = 0; i < a->level_count; ++i)
        if (a->levels[i].width != b->levels[i].width || a->levels[i].height != b->levels[i].height ||
            a->levels[i].bytes != b->levels[i].bytes || memcmp(a->levels[i].pixels, b->levels[i].pixels, a->levels[i].bytes)) return false;
    return true;
}
static bool builtin_image(const qa_scene_image *image, bool missing)
{
    uint32_t size = missing ? 16 : 1;
    if (!image || !image->name || strcmp(image->name, missing ? "*default" : "*white") ||
        image->kind != QA_SCENE_RGBA8 || image->wrap != QA_SCENE_REPEAT ||
        image->filter != (missing ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_NEAREST) ||
        image->logical_width != size || image->logical_height != size ||
        image->level_count != (missing ? 5 : 1) || image->animation_count || image->animation ||
        image->revision != 1 || image->border.x != (missing ? 0 : 1) ||
        image->border.y != (missing ? 0 : 1) || image->border.z != (missing ? 0 : 1) || image->border.w != 1) return false;
    for (size_t i = 0; i < image->level_count; ++i, size = size > 1 ? size / 2 : 1) {
        const qa_scene_image_level *level = image->levels + i;
        if (level->width != size || level->height != size || level->bytes != (size_t)size * size * 4 || !level->pixels) return false;
        const uint8_t *pixels = level->pixels;
        for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) for (unsigned c = 0; c < 4; ++c) {
            unsigned expected = 255;
            if (missing && c < 3) {
                if (!i) expected = x == 0 || x == 15 || y == 0 || y == 15 ? 255 : 32;
                else {
                    const qa_scene_image_level *previous = image->levels + i - 1;
                    const uint8_t *source = previous->pixels;
                    size_t at = ((size_t)y * 2 * previous->width + x * 2) * 4 + c;
                    expected = ((unsigned)source[at] + source[at + 4] +
                        source[at + (size_t)previous->width * 4] + source[at + (size_t)previous->width * 4 + 4]) / 4;
                }
            }
            if (pixels[((size_t)y * size + x) * 4 + c] != expected) return false;
        }
    }
    return true;
}

static bool source_builtin_image(const qa_scene_image *image, const qa_q3_image_upload_options *profile,
    const char *name, uint32_t size, uint8_t value, bool missing, qa_error *error)
{
    if (!image || !image->name || strcmp(image->name, name) || image->kind != QA_SCENE_RGBA8 ||
        image->wrap != QA_SCENE_REPEAT || image->filter != (missing ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_LINEAR) ||
        image->logical_width != size || image->logical_height != size || image->animation_count || image->animation ||
        image->revision != 1 || image->border.x || image->border.y || image->border.z || image->border.w) return false;
    uint8_t pixels[16 * 16 * 4];
    for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) {
        uint8_t pixel = missing ? (x == 0 || x == 15 || y == 0 || y == 15 ? 255 : 32) : value;
        size_t at = ((size_t)y * size + x) * 4;
        pixels[at] = pixels[at + 1] = pixels[at + 2] = pixel; pixels[at + 3] = missing ? pixel : 255;
    }
    qa_image original = {.width = size, .height = size, .rgba = {pixels, (size_t)size * size * 4}};
    qa_q3_image_upload_options upload = *profile; upload.allow_picmip = false; upload.mipmap = missing;
    qa_mip_chain expected = {0};
    if (!qa_q3_image_upload(&original, &upload, &expected, error)) return false;
    bool ok = image->level_count == expected.count;
    for (size_t i = 0; ok && i < expected.count; ++i) {
        const qa_image *level = expected.levels + i;
        ok = image->levels[i].width == level->width && image->levels[i].height == level->height &&
            image->levels[i].bytes == level->rgba.size && image->levels[i].pixels &&
            !memcmp(image->levels[i].pixels, level->rgba.data, level->rgba.size);
    }
    qa_mip_chain_free(&expected); return ok;
}
static bool source_builtins_fields(qa_source_save_io *io, qa_scene_resources *owner,
    qa_scene_resources *state, const qa_scene_resource_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bool(io, &state->source_builtins)) return false;
    if (!state->source_builtins) return !state->source_white && !state->source_missing && !state->source_identity &&
        (!reading || owner->detached || !owner->source_builtins);
    if (!qa_q3_image_upload_options_codec(io, &state->source_builtins_upload) ||
        !image_field(io, refs, owner, &state->source_white) || !image_field(io, refs, owner, &state->source_missing) ||
        !image_field(io, refs, owner, &state->source_identity)) return false;
    qa_q3_color_lighting lighting;
    if (!qa_q3_color_lighting_read(&state->source_builtins_upload.color.device,
        state->source_builtins_upload.color.requested_overbright_bits, &lighting, io->error) ||
        !source_builtin_image(state->source_white, &state->source_builtins_upload, "*white", 8, 255, false, io->error) ||
        !source_builtin_image(state->source_missing, &state->source_builtins_upload, "*default", 16, 32, true, io->error) ||
        !source_builtin_image(state->source_identity, &state->source_builtins_upload, "*identityLight", 8,
            lighting.identity_light_byte, false, io->error)) return false;
    return !reading || owner->detached || (owner->source_builtins &&
        qa_q3_image_upload_options_equal(&owner->source_builtins_upload, &state->source_builtins_upload) &&
        same_image(owner->source_white, state->source_white) && same_image(owner->source_missing, state->source_missing) &&
        same_image(owner->source_identity, state->source_identity));
}
static bool content_field(qa_source_save_io *io, qa_scene_resources *owner,
    const qa_scene_resource_checkpoint_refs *refs, qa_resource **resource, qa_mount_id *mount,
    uint32_t schema)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = reading ? false : *resource != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return reading ? (*mount = 0, true) : *mount == 0;
    uint64_t pool = 0, version = 0;
    if (!reading && (!owner->vfs || qa_resource_pool_find(qa_vfs_resources(owner->vfs), qa_resource_id(*resource)) != *resource ||
        !refs->resource_encode(refs->context, *resource, &pool, &version, io->error) || version != qa_resource_id(*resource))) return false;
    if (!qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &version) || !version) return false;
    const char *path = reading ? NULL : qa_resource_path(*resource); char *owned = NULL;
    size_t scope = 0; qa_vfs_mount_info selected = {0}; bool found = reading;
    qa_mount_id historical = reading ? 0 : *mount;
    qa_vfs_resource_origin origin = {0};
    if (!reading && schema >= 4) {
        found = qa_vfs_resource_origin_read(owner->vfs, historical, *resource, &origin);
        selected.id = historical; selected.is_archive = origin.archive;
        selected.digest = origin.archive ? &origin.archive_digest : NULL;
    } else if (!reading) for (; scope < qa_vfs_mount_count(owner->vfs); ++scope)
        if (qa_vfs_mount_at(owner->vfs, scope, &selected) && selected.id == *mount) { found = true; break; }
    qa_sha256_digest digest = {{0}}, archive_digest = {{0}};
    size_t ordinal = 0; bool archive = false;
    if (!reading && found) {
        const qa_sha256_digest *actual = qa_resource_digest(*resource);
        if (!actual) return false;
        digest = *actual; archive = qa_resource_archive_origin(*resource, &archive_digest, &ordinal);
        if (selected.is_archive != archive || (archive && (!selected.digest || !qa_sha256_equal(selected.digest, &archive_digest)))) return false;
    }
    bool ok = found && text_field(io, &path, &owned) &&
        (schema >= 4 ? qa_source_save_u64(io, &historical) && historical != 0 :
            qa_source_save_count(io, &scope, SIZE_MAX)) &&
        qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) && qa_source_save_bool(io, &archive);
    if (ok && archive) ok = qa_source_save_bytes(io, archive_digest.bytes, sizeof(archive_digest.bytes)) &&
        qa_source_save_count(io, &ordinal, SIZE_MAX);
    if (ok && reading) {
        const qa_resource *decoded = NULL;
        ok = owner->vfs && refs->resource_decode(refs->context, pool, version, &decoded, io->error) && decoded &&
            qa_resource_id(decoded) == version &&
            qa_resource_pool_find(qa_vfs_resources(owner->vfs), qa_resource_id(decoded)) == decoded &&
            !strcmp(qa_resource_path(decoded), path);
        if (ok && schema >= 4) {
            ok = qa_vfs_resource_origin_read(owner->vfs, historical, decoded, &origin);
            selected.id = historical; selected.is_archive = origin.archive;
            selected.digest = origin.archive ? &origin.archive_digest : NULL;
        } else if (ok) ok = scope < qa_vfs_mount_count(owner->vfs) &&
            qa_vfs_mount_at(owner->vfs, scope, &selected);
        ok = ok && selected.is_archive == archive &&
            (!archive || (selected.digest && qa_sha256_equal(selected.digest, &archive_digest)));
        if (ok) {
            const qa_sha256_digest *actual = qa_resource_digest(decoded);
            qa_sha256_digest actual_archive; size_t actual_ordinal = 0;
            bool actual_origin = qa_resource_archive_origin(decoded, &actual_archive, &actual_ordinal);
            ok = actual && qa_sha256_equal(actual, &digest) && actual_origin == archive &&
                (!archive || (qa_sha256_equal(&actual_archive, &archive_digest) && actual_ordinal == ordinal));
            if (ok) { qa_resource_retain((qa_resource *)decoded); *resource = (qa_resource *)decoded; *mount = selected.id; }
        }
    }
    free(owned); return ok;
}

static bool names_fields(qa_source_save_io *io, const qa_scene_resources *owner, qa_scene_resources *state)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = reading ? 0 : qa_strings_count(owner->names->strings);
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 8 : UINT32_MAX) || count > UINT32_MAX) return false;
    if (reading) {
        state->names = calloc(1, sizeof(*state->names));
        if (!state->names) return fail(io->error, QA_ERROR_MEMORY, "allocating saved resource names");
        if (!qa_strings_create(&state->names->strings, io->error)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *text = reading ? NULL : qa_strings_cstr(owner->names->strings, (qa_string_id)(i + 1));
        char *owned = NULL; bool ok = text_field(io, &text, &owned);
        if (ok && reading) {
            qa_string_id id = 0;
            ok = qa_strings_intern_cstr(state->names->strings, text, &id, io->error) && id == i + 1;
        }
        free(owned); if (!ok) return false;
    }
    if (reading) for (const owned_image *image = owner->names->images; image; image = image->next)
        if (!qa_strings_find(state->names->strings, (qa_bytes){(const uint8_t *)image->image.name, strlen(image->image.name)})) return false;
    return true;
}

static bool cache_options(qa_source_save_io *io, image_cache *entry, uint32_t version)
{
    qa_scene_image_options *options = &entry->options;
    uint32_t family = options->family, wrap = options->wrap, filter = options->filter, usage = options->usage;
    int32_t transparent_index = options->transparent_index;
    size_t palette = options->palette_rgb.size, translation = options->translation.size;
    bool ok = qa_source_save_u32(io, &family) && family <= QA_SCENE_Q3 &&
        qa_source_save_u32(io, &wrap) && wrap <= QA_SCENE_CLAMP &&
        qa_source_save_u32(io, &filter) && filter <= QA_SCENE_LINEAR_MIPMAP_LINEAR &&
        qa_source_save_u32(io, &usage) && usage <= QA_IMAGE_USAGE_SKY &&
        qa_source_save_bool(io, &options->mipmap) && qa_source_save_bool(io, &options->transparent) &&
        qa_source_save_bool(io, &options->fullbright_only) && qa_source_save_i32(io, &transparent_index) &&
        qa_source_save_count(io, &palette, 768) && (!palette || palette == 768) &&
        qa_source_save_count(io, &translation, 256) && (!translation || translation == 256) &&
        qa_source_save_bytes(io, entry->palette, palette) && qa_source_save_bytes(io, entry->translation, translation);
    if (ok && version >= 5) ok = qa_source_save_bool(io, &entry->exact_file);
    if (ok) {
        options->family = (qa_scene_family)family; options->wrap = (qa_scene_wrap)wrap;
        options->filter = (qa_scene_filter)filter; options->usage = (qa_scene_image_usage)usage;
        options->transparent_index = transparent_index;
        options->palette_rgb = (qa_bytes){NULL, palette}; options->translation = (qa_bytes){NULL, translation};
    }
    if (ok && version >= 6) ok = qa_scene_source_upload_fields(io, options);
    return ok;
}
static bool same_cache_key(const image_cache *a, const image_cache *b)
{
    const qa_scene_image_options *x = &a->options, *y = &b->options;
    if (a->name == b->name && x->source_q3 && y->source_q3 && !a->exact_file && !b->exact_file) return true;
    return a->name == b->name && a->source == b->source && a->logical_source == b->logical_source &&
        a->exact_file == b->exact_file &&
        x->family == y->family && x->wrap == y->wrap && x->filter == y->filter && x->usage == y->usage &&
        x->mipmap == y->mipmap && x->transparent == y->transparent && x->fullbright_only == y->fullbright_only &&
        x->transparent_index == y->transparent_index && x->palette_rgb.size == y->palette_rgb.size &&
        x->translation.size == y->translation.size &&
        x->source_q3 == y->source_q3 && (!x->source_q3 ||
            qa_q3_image_upload_options_equal(&x->source_upload, &y->source_upload)) &&
        (!x->palette_rgb.size || !memcmp(a->palette, b->palette, x->palette_rgb.size)) &&
        (!x->translation.size || !memcmp(a->translation, b->translation, x->translation.size));
}

typedef struct sampling_row {
    qa_scene_image *image;
    const qa_scene_image *source;
    bool mipmap;
} sampling_row;
typedef struct sampling_state { sampling_row *rows; size_t count; } sampling_state;
static void sampling_clear(sampling_state *state)
{
    for (size_t i = 0; i < state->count; ++i) {
        qa_scene_image_release(state->rows[i].image); qa_scene_image_release(state->rows[i].source);
    }
    free(state->rows); *state = (sampling_state){0};
}
static bool sampling_copy(const sampling_row *row)
{
    const qa_scene_image *image = row->image, *source = row->source;
    size_t levels = row->mipmap ? source->level_count : 1;
    if (image == source || image->kind != source->kind || image->filter !=
        (row->mipmap ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_LINEAR) ||
        image->logical_width != source->logical_width || image->logical_height != source->logical_height ||
        image->level_count != levels || image->animation_count != source->animation_count ||
        image->border.x != source->border.x || image->border.y != source->border.y ||
        image->border.z != source->border.z || image->border.w != source->border.w) return false;
    for (size_t i = 0; i < levels; ++i) {
        const qa_scene_image_level *a = image->levels + i, *b = source->levels + i;
        if (a->width != b->width || a->height != b->height || a->bytes != b->bytes ||
            memcmp(a->pixels, b->pixels, a->bytes)) return false;
    }
    return true;
}
static const qa_scene_image *sampling_parent(const sampling_state *state, const qa_scene_image *image)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->rows[i].image == image) return state->rows[i].source;
    return ((const owned_image *)image)->sampling_source;
}
typedef struct sampling_visit { const qa_scene_image *image; size_t edge; bool active; } sampling_visit;
static bool sampling_graph(const sampling_state *state, qa_error *error)
{
    sampling_visit *visits = NULL; size_t count = 0;
    size_t *stack = NULL, depth = 0; bool ok = true;
    for (size_t root = 0; ok && root < state->count; ++root) {
        const qa_scene_image *next = state->rows[root].image;
        for (;;) {
            if (next) {
                size_t index = 0;
                for (; index < count && visits[index].image != next; ++index) {}
                if (index < count) {
                    if (visits[index].active) { ok = false; break; }
                } else {
                    if (count == SIZE_MAX / sizeof(*visits) || depth == SIZE_MAX / sizeof(*stack)) {
                        ok = false; break;
                    }
                    sampling_visit *grown = realloc(visits, (count + 1) * sizeof(*visits));
                    if (!grown) { ok = fail(error, QA_ERROR_MEMORY, "validating sampled-image provenance"); break; }
                    visits = grown;
                    size_t *frames = realloc(stack, (depth + 1) * sizeof(*stack));
                    if (!frames) { ok = fail(error, QA_ERROR_MEMORY, "retaining sampled-image traversal"); break; }
                    stack = frames; visits[count] = (sampling_visit){.image = next, .active = true};
                    stack[depth++] = count++;
                }
            }
            next = NULL;
            while (depth) {
                sampling_visit *visit = visits + stack[depth - 1];
                const qa_scene_image *image = visit->image;
                if (!visit->edge++) next = sampling_parent(state, image);
                else if (visit->edge - 1 < image->animation_count) next = image->animation[visit->edge - 1];
                else { visit->active = false; --depth; continue; }
                if (next) break;
            }
            if (!depth && !next) break;
        }
    }
    free(visits); free(stack); return ok;
}
static bool sampling_fields(qa_source_save_io *io, qa_scene_resources *owner,
    const qa_scene_resource_checkpoint_refs *refs, sampling_state *state)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (const owned_image *image = owner->names->images; image; image = image->next)
        if (image->sampling_source) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 17 : SIZE_MAX / sizeof(*state->rows)) ||
        count > SIZE_MAX / sizeof(*state->rows)) return false;
    state->rows = count ? calloc(count, sizeof(*state->rows)) : NULL;
    if (count && !state->rows) return fail(io->error, QA_ERROR_MEMORY, "retaining sampled-image source edges");
    state->count = count;
    const owned_image *image = owner->names->images;
    for (size_t i = 0; i < count; ++i) {
        sampling_row *row = state->rows + i;
        if (!reading) {
            while (image && !image->sampling_source) image = image->next;
            if (!image) return false;
            row->image = (qa_scene_image *)&image->image; row->source = image->sampling_source;
            row->mipmap = image->sampling_mipmap; image = image->next;
            qa_scene_image_retain(row->image); qa_scene_image_retain(row->source);
        }
        /* image_field owns one retained target reference on decode only. */
        if (!image_field(io, refs, owner, &row->image)) return false;
        uint64_t source = 0;
        if (!reading && !refs->image_encode(refs->context, row->source, &source, io->error)) return false;
        if (!qa_source_save_u64(io, &source) || !qa_source_save_bool(io, &row->mipmap)) return false;
        if (reading) {
            const qa_scene_image *decoded = NULL;
            if (!refs->image_decode(refs->context, source, &decoded, io->error) || !decoded) return false;
            qa_scene_image_retain(decoded); row->source = decoded;
        }
        if (!sampling_copy(row)) return false;
        for (size_t j = 0; j < i; ++j) if (state->rows[j].image == row->image) return false;
    }
    return sampling_graph(state, io->error);
}

static bool resource_fields(qa_source_save_io *io, qa_scene_resources *owner, qa_scene_resources *state,
    const qa_scene_resource_checkpoint_refs *refs, sampling_state *sampling)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q', 'A', 'R', 'S'}; uint32_t version = 7, fullbright = state->fullbright_first;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QARS", 4) ||
        !qa_source_save_u32(io, &version) || (version < 2 || version > 7) || !names_fields(io, owner, state) ||
        !qa_source_save_bool(io, &state->registrations_started) || !qa_source_save_u32(io, &fullbright) || fullbright > 256 ||
        !image_field(io, refs, owner, &state->white) || !image_field(io, refs, owner, &state->missing)) return false;
    if (!builtin_image(state->white, false) || !builtin_image(state->missing, true) ||
        (reading && !owner->detached &&
            (!same_image(owner->white, state->white) || !same_image(owner->missing, state->missing)))) return false;
    if (version >= 7 && !source_builtins_fields(io, owner, state, refs)) return false;
    state->fullbright_first = fullbright;
    for (unsigned family = 0; family < 3; ++family) {
        qa_scene_image_policy *policy = &state->policies[family]; size_t palette = state->palettes[family].size;
        if (!qa_source_save_bool(io, &state->has_policy[family]) ||
            !qa_source_save_i32(io, &policy->override_level) || !qa_source_save_u32(io, &policy->override_usages) ||
            !qa_source_save_bool(io, &policy->source_formats) || !qa_source_save_count(io, &policy->format_count, 6)) return false;
        for (size_t i = 0; i < policy->format_count; ++i) {
            uint32_t format = policy->formats[i];
            if (!qa_source_save_u32(io, &format) || format > QA_SCENE_IMAGE_GIF) return false;
            for (size_t j = 0; j < i; ++j) if (policy->formats[j] == (qa_scene_image_format)format) return false;
            policy->formats[i] = (qa_scene_image_format)format;
        }
        if (!qa_source_save_count(io, &palette, 768) || (palette && palette != 768)) return false;
        if (reading && palette) {
            state->palettes[family] = (qa_buffer){malloc(palette), palette};
            if (!state->palettes[family].data) return fail(io->error, QA_ERROR_MEMORY, "allocating saved resource palette");
        }
        if (!qa_source_save_bytes(io, state->palettes[family].data, palette)) return false;
    }
    size_t count = state->cache_count, capacity = state->cache_capacity;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 43 : SIZE_MAX) ||
        !qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(image_cache)) || capacity < count) return false;
    if (reading && capacity) {
        state->cache = calloc(capacity, sizeof(*state->cache));
        if (!state->cache) return fail(io->error, QA_ERROR_MEMORY, "allocating saved image cache");
        state->cache_count = count; state->cache_capacity = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        image_cache *entry = &state->cache[i]; char *owned = NULL;
        const char *name = reading ? NULL : qa_strings_cstr(owner->names->strings, entry->name);
        bool ok = text_field(io, &name, &owned);
        if (ok && reading) {
            entry->name = qa_strings_find(state->names->strings, (qa_bytes){(const uint8_t *)name, strlen(name)});
            ok = entry->name != 0;
        }
        free(owned);
        if (!ok || !cache_options(io, entry, version) ||
            !content_field(io, owner, refs, &entry->source_record, &entry->source_mount, version) || !entry->source_record ||
            !content_field(io, owner, refs, &entry->logical_record, &entry->logical_mount, version) ||
            !image_field(io, refs, owner, &entry->image)) return false;
        const char *cache_name=qa_strings_cstr(reading?state->names->strings:owner->names->strings,entry->name);
        if (!cache_name || strcmp(entry->image->name,cache_name) || entry->image->kind!=QA_SCENE_RGBA8 ||
            entry->image->wrap!=entry->options.wrap || entry->image->filter!=entry->options.filter) return false;
        if (reading) entry->source = qa_resource_id(entry->source_record), entry->logical_source = qa_resource_id(entry->logical_record);
        else if (entry->source != qa_resource_id(entry->source_record) || entry->logical_source != qa_resource_id(entry->logical_record)) return false;
        for (size_t j = 0; j < i; ++j) if (same_cache_key(entry, &state->cache[j])) return false;
    }
    return version < 3 || sampling_fields(io, owner, refs, sampling);
}

static void state_clear(qa_scene_resources *state)
{
    for (size_t i = 0; i < state->cache_count; ++i) {
        qa_scene_image_release(state->cache[i].image);
        qa_resource_release(state->cache[i].source_record); qa_resource_release(state->cache[i].logical_record);
    }
    free(state->cache);
    for (unsigned i = 0; i < 3; ++i) qa_buffer_free(&state->palettes[i]);
    qa_scene_image_release(state->white); qa_scene_image_release(state->missing);
    qa_scene_image_release(state->source_white); qa_scene_image_release(state->source_missing);
    qa_scene_image_release(state->source_identity);
    if (state->names) { qa_strings_destroy(state->names->strings); free(state->names); }
}

bool qa_scene_resources_checkpoint(const qa_scene_resources *owner, const qa_scene_resource_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!owner || owner->detached || owner->continuation_active || owner->policy_pending ||
        !refs || !refs->image_encode || !refs->resource_encode || !out)
        return fail(error, QA_ERROR_ARGUMENT, "resource checkpoint requires idle continuation and actual image/content references");
    qa_source_save_io io = {0}; qa_scene_resources state = *owner; sampling_state sampling = {0};
    /* Cache fields are observed, never normalized in the active owner. */
    image_cache *cache = owner->cache_count ? malloc(owner->cache_count * sizeof(*cache)) : NULL;
    if (owner->cache_count && !cache) return fail(error, QA_ERROR_MEMORY, "copying resource cache observation");
    if (cache) memcpy(cache, owner->cache, owner->cache_count * sizeof(*cache));
    state.cache = cache;
    ((qa_scene_resources *)owner)->continuation_active = true;
    bool ok = qa_source_save_writer(&io, NULL, error) && resource_fields(&io, (qa_scene_resources *)owner, &state, refs, &sampling) &&
        qa_source_save_finish(&io, out);
    sampling_clear(&sampling); free(cache); qa_source_save_dispose(&io);
    ((qa_scene_resources *)owner)->continuation_active = false;
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "unqualified resource cache continuation");
    return ok;
}

bool qa_scene_resources_restore(qa_scene_resources *owner, qa_bytes bytes, const qa_scene_resource_checkpoint_refs *refs,
    qa_error *error)
{
    if (!qa_scene_resources_idle(owner) || owner->cache_count || !refs || !refs->image_decode || !refs->resource_decode)
        return fail(error, QA_ERROR_ARGUMENT, "resource restore requires an empty detached cache and qualified images");
    qa_scene_resources state = {0}; qa_source_save_io io = {0}; sampling_state sampling = {0};
    owner->continuation_active = true;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && resource_fields(&io, owner, &state, refs, &sampling) &&
        qa_source_save_finish(&io, NULL);
    if (ok) {
        qa_strings *old_names = owner->names->strings;
        for (owned_image *image = owner->names->images; image; image = image->next) {
            qa_string_id id = qa_strings_find(state.names->strings,
                (qa_bytes){(const uint8_t *)image->image.name, strlen(image->image.name)});
            image->image.name = qa_strings_cstr(state.names->strings, id);
        }
        owner->names->strings = state.names->strings; free(state.names); state.names = NULL;
        qa_strings_destroy(old_names);
        free(owner->cache);
        for (unsigned i = 0; i < 3; ++i) {
            qa_buffer_free(&owner->palettes[i]); owner->palettes[i] = state.palettes[i];
            owner->policies[i] = state.policies[i]; owner->has_policy[i] = state.has_policy[i];
        }
        qa_scene_image_release(owner->white); qa_scene_image_release(owner->missing);
        owner->white = state.white; owner->missing = state.missing;
        qa_scene_image_release(owner->source_white); qa_scene_image_release(owner->source_missing);
        qa_scene_image_release(owner->source_identity);
        owner->source_white = state.source_white; owner->source_missing = state.source_missing;
        owner->source_identity = state.source_identity; owner->source_builtins = state.source_builtins;
        owner->source_builtins_upload = state.source_builtins_upload;
        owner->cache = state.cache; owner->cache_count = state.cache_count; owner->cache_capacity = state.cache_capacity;
        owner->fullbright_first = state.fullbright_first; owner->registrations_started = state.registrations_started;
        owner->detached = false;
        for (size_t i = 0; i < sampling.count; ++i) {
            sampling_row *row = sampling.rows + i; owned_image *image = (owned_image *)row->image;
            qa_scene_image_release(image->sampling_source);
            image->sampling_source = row->source; image->sampling_mipmap = row->mipmap;
            row->source = NULL;
        }
    } else state_clear(&state);
    sampling_clear(&sampling); qa_source_save_dispose(&io);
    owner->continuation_active = false;
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid saved resource cache");
    return ok;
}

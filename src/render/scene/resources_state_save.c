#include "resources_internal.h"
#include "qa/scene_resource_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }

static bool text_field(qa_source_save_io *io, const char **text, char **owned)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = reading ? 0 : *text ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX) || !size || size == SIZE_MAX) return false;
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

static bool content_field(qa_source_save_io *io, qa_scene_resources *owner, qa_resource **resource, qa_mount_id *mount)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = reading ? false : *resource != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return reading ? (*mount = 0, true) : *mount == 0;
    const char *path = reading ? NULL : qa_resource_path(*resource); char *owned = NULL;
    size_t scope = 0; qa_vfs_mount_info selected = {0}; bool found = reading;
    if (!reading) for (; scope < qa_vfs_mount_count(owner->vfs); ++scope)
        if (qa_vfs_mount_at(owner->vfs, scope, &selected) && selected.id == *mount) { found = true; break; }
    qa_sha256_digest digest = {{0}}, archive_digest = {{0}};
    size_t ordinal = 0; bool archive = false;
    if (!reading && found) {
        const qa_sha256_digest *actual = qa_resource_digest(*resource);
        if (!actual) return false;
        digest = *actual; archive = qa_resource_archive_origin(*resource, &archive_digest, &ordinal);
        if (selected.is_archive != archive || (archive && (!selected.digest || !qa_sha256_equal(selected.digest, &archive_digest)))) return false;
    }
    bool ok = found && text_field(io, &path, &owned) && qa_source_save_count(io, &scope, SIZE_MAX) &&
        qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) && qa_source_save_bool(io, &archive);
    if (ok && archive) ok = qa_source_save_bytes(io, archive_digest.bytes, sizeof(archive_digest.bytes)) &&
        qa_source_save_count(io, &ordinal, SIZE_MAX);
    if (ok && reading) {
        ok = scope < qa_vfs_mount_count(owner->vfs) && qa_vfs_mount_at(owner->vfs, scope, &selected) &&
            selected.is_archive == archive && (!archive || (selected.digest && qa_sha256_equal(selected.digest, &archive_digest))) &&
            qa_vfs_acquire_from(owner->vfs, selected.id, path, resource, io->error);
        if (ok) {
            const qa_sha256_digest *actual = qa_resource_digest(*resource);
            qa_sha256_digest actual_archive; size_t actual_ordinal = 0;
            bool actual_origin = qa_resource_archive_origin(*resource, &actual_archive, &actual_ordinal);
            ok = actual && qa_sha256_equal(actual, &digest) && actual_origin == archive &&
                (!archive || (qa_sha256_equal(&actual_archive, &archive_digest) && actual_ordinal == ordinal));
            *mount = selected.id;
        }
    }
    free(owned); return ok;
}

static bool cache_options(qa_source_save_io *io, image_cache *entry)
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
    if (ok) {
        options->family = (qa_scene_family)family; options->wrap = (qa_scene_wrap)wrap;
        options->filter = (qa_scene_filter)filter; options->usage = (qa_scene_image_usage)usage;
        options->transparent_index = transparent_index;
        options->palette_rgb = (qa_bytes){NULL, palette}; options->translation = (qa_bytes){NULL, translation};
    }
    return ok;
}
static bool same_cache_key(const image_cache *a, const image_cache *b)
{
    const qa_scene_image_options *x = &a->options, *y = &b->options;
    return a->name == b->name && a->source == b->source && a->logical_source == b->logical_source &&
        x->family == y->family && x->wrap == y->wrap && x->filter == y->filter && x->usage == y->usage &&
        x->mipmap == y->mipmap && x->transparent == y->transparent && x->fullbright_only == y->fullbright_only &&
        x->transparent_index == y->transparent_index && x->palette_rgb.size == y->palette_rgb.size &&
        x->translation.size == y->translation.size &&
        (!x->palette_rgb.size || !memcmp(a->palette, b->palette, x->palette_rgb.size)) &&
        (!x->translation.size || !memcmp(a->translation, b->translation, x->translation.size));
}

static bool resource_fields(qa_source_save_io *io, qa_scene_resources *owner, qa_scene_resources *state,
    const qa_scene_resource_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q', 'A', 'R', 'S'}; uint32_t version = 1, fullbright = state->fullbright_first;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QARS", 4) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_bool(io, &state->registrations_started) || !qa_source_save_u32(io, &fullbright) || fullbright > 256 ||
        !image_field(io, refs, owner, &state->white) || !image_field(io, refs, owner, &state->missing)) return false;
    if (reading && (!same_image(owner->white, state->white) || !same_image(owner->missing, state->missing))) return false;
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
    size_t count = state->cache_count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 27 : SIZE_MAX) || count > SIZE_MAX / sizeof(image_cache)) return false;
    if (reading && count) {
        state->cache = calloc(count, sizeof(*state->cache));
        if (!state->cache) return fail(io->error, QA_ERROR_MEMORY, "allocating saved image cache");
        state->cache_count = count; state->cache_capacity = count;
    }
    for (size_t i = 0; i < count; ++i) {
        image_cache *entry = &state->cache[i]; char *owned = NULL;
        const char *name = reading ? NULL : qa_strings_cstr(owner->names->strings, entry->name);
        bool ok = text_field(io, &name, &owned);
        if (ok && reading) ok = qa_strings_intern_cstr(owner->names->strings, name, &entry->name, io->error);
        free(owned);
        if (!ok || !cache_options(io, entry) ||
            !content_field(io, owner, &entry->source_record, &entry->source_mount) || !entry->source_record ||
            !content_field(io, owner, &entry->logical_record, &entry->logical_mount) ||
            !image_field(io, refs, owner, &entry->image)) return false;
        if (reading) entry->source = qa_resource_id(entry->source_record), entry->logical_source = qa_resource_id(entry->logical_record);
        else if (entry->source != qa_resource_id(entry->source_record) || entry->logical_source != qa_resource_id(entry->logical_record)) return false;
        for (size_t j = 0; j < i; ++j) if (same_cache_key(entry, &state->cache[j])) return false;
    }
    return true;
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
}

bool qa_scene_resources_checkpoint(const qa_scene_resources *owner, const qa_scene_resource_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!owner || !refs || !refs->image_encode || !out)
        return fail(error, QA_ERROR_ARGUMENT, "resource checkpoint requires actual image references");
    qa_source_save_io io = {0}; qa_scene_resources state = *owner;
    /* Cache fields are observed, never normalized in the active owner. */
    image_cache *cache = owner->cache_count ? malloc(owner->cache_count * sizeof(*cache)) : NULL;
    if (owner->cache_count && !cache) return fail(error, QA_ERROR_MEMORY, "copying resource cache observation");
    if (cache) memcpy(cache, owner->cache, owner->cache_count * sizeof(*cache));
    state.cache = cache;
    bool ok = qa_source_save_writer(&io, NULL, error) && resource_fields(&io, (qa_scene_resources *)owner, &state, refs) &&
        qa_source_save_finish(&io, out);
    free(cache); qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "unqualified resource cache continuation");
    return ok;
}

bool qa_scene_resources_restore(qa_scene_resources *owner, qa_bytes bytes, const qa_scene_resource_checkpoint_refs *refs,
    qa_error *error)
{
    if (!owner || owner->cache_count || !refs || !refs->image_decode)
        return fail(error, QA_ERROR_ARGUMENT, "resource restore requires an empty detached cache and qualified images");
    qa_scene_resources state = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && resource_fields(&io, owner, &state, refs) &&
        qa_source_save_finish(&io, NULL);
    if (ok) {
        free(owner->cache);
        for (unsigned i = 0; i < 3; ++i) {
            qa_buffer_free(&owner->palettes[i]); owner->palettes[i] = state.palettes[i];
            owner->policies[i] = state.policies[i]; owner->has_policy[i] = state.has_policy[i];
        }
        qa_scene_image_release(owner->white); qa_scene_image_release(owner->missing);
        owner->white = state.white; owner->missing = state.missing;
        owner->cache = state.cache; owner->cache_count = state.cache_count; owner->cache_capacity = state.cache_capacity;
        owner->fullbright_first = state.fullbright_first; owner->registrations_started = state.registrations_started;
    } else state_clear(&state);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid saved resource cache");
    return ok;
}

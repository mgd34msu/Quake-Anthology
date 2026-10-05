#include "resources_internal.h"
#include "qa/scene_resource_save.h"
#include "qa/source_save.h"
#include "qa/vfs_view_save.h"
#include "qa/material.h"
#include "image_options_save.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    const char *name, uint32_t size, uint8_t value, bool missing, bool scratch, qa_error *error)
{
    if (!image || !image->name || strcmp(image->name, name) || image->kind == QA_SCENE_DEPTH32F ||
        image->wrap != (scratch ? QA_SCENE_CLAMP : QA_SCENE_REPEAT) ||
        image->filter != (missing ? QA_SCENE_LINEAR_MIPMAP_NEAREST : QA_SCENE_LINEAR) ||
        image->logical_width != size || image->logical_height != size || image->animation_count || image->animation ||
        image->revision != 1 || image->border.x != 0.0f || image->border.y != 0.0f || image->border.z != 0.0f || image->border.w != 0.0f ||
        image->source_dlight || image->source_after_upload_border || image->source_upload_border.x != 0.0f ||
        image->source_upload_border.y != 0.0f || image->source_upload_border.z != 0.0f || image->source_upload_border.w != 0.0f) return false;
    uint8_t pixels[16 * 16 * 4];
    for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) {
        uint8_t pixel = missing ? (x == 0 || x == 15 || y == 0 || y == 15 ? 255 : 32) : value;
        size_t at = ((size_t)y * size + x) * 4;
        pixels[at] = pixels[at + 1] = pixels[at + 2] = pixel; pixels[at + 3] = missing ? pixel : 255;
    }
    qa_image original = {.width = size, .height = size, .rgba = {pixels, (size_t)size * size * 4}};
    qa_q3_image_upload_options upload = *profile; upload.allow_picmip = scratch; upload.mipmap = missing;
    qa_mip_chain expected = {0};
    qa_q3_texture_format format;
    if (!qa_q3_image_upload_format(&original, &upload, &expected, &format, error)) return false;
    qa_scene_image_kind kind = scene_resource_q3_image_kind(format);
    bool ok = image->level_count == expected.count && image->source_q3 && image->source_format == format && image->kind == kind;
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
    if (!(qa_q3_image_upload_options_precision_codec(io, &state->source_builtins_upload)) ||
        !image_field(io, refs, owner, &state->source_white) || !image_field(io, refs, owner, &state->source_missing) ||
        !image_field(io, refs, owner, &state->source_identity)) return false;
    qa_q3_color_lighting lighting;
    if (!qa_q3_color_lighting_read(&state->source_builtins_upload.color.device,
        state->source_builtins_upload.color.requested_overbright_bits, &lighting, io->error) ||
        !source_builtin_image(state->source_white, &state->source_builtins_upload, "*white", 8, 255, false, false, io->error) ||
        !source_builtin_image(state->source_missing, &state->source_builtins_upload, "*default", 16, 32, true, false, io->error) ||
        !source_builtin_image(state->source_identity, &state->source_builtins_upload, "*identityLight", 8,
            lighting.identity_light_byte, false, false, io->error)) return false;
    return !reading || owner->detached || (owner->source_builtins &&
        qa_q3_image_upload_options_equal(&owner->source_builtins_upload, &state->source_builtins_upload) &&
        same_image(owner->source_white, state->source_white) && same_image(owner->source_missing, state->source_missing) &&
        same_image(owner->source_identity, state->source_identity));
}
static bool source_generated_image(const qa_scene_image *image, const qa_q3_image_upload_options *profile,
    const char *name, uint32_t width, uint32_t height, uint8_t *pixels, bool fog, qa_error *error)
{
    if (!image || !image->name || strcmp(image->name, name) || image->kind == QA_SCENE_DEPTH32F ||
        image->wrap != QA_SCENE_CLAMP || image->filter != QA_SCENE_LINEAR || image->animation_count ||
        image->revision != 1 || image->logical_width != width || image->logical_height != height ||
        image->border.x != 0.0f || image->border.y != 0.0f || image->border.z != 0.0f || image->border.w != 0.0f) return false;
    if (image->source_dlight != !fog || image->source_after_upload_border != fog ||
        (fog && (image->source_upload_border.x != 1 || image->source_upload_border.y != 1 ||
         image->source_upload_border.z != 1 || image->source_upload_border.w != 1))) return false;
    qa_image original = {.width = width, .height = height, .rgba = {pixels,(size_t)width * height * 4}};
    qa_q3_image_upload_options upload = *profile; upload.mipmap = false; upload.allow_picmip = false;
    qa_mip_chain expected = {0};
    qa_q3_texture_format format;
    if (!qa_q3_image_upload_format(&original, &upload, &expected, &format, error)) return false;
    qa_scene_image_kind kind = scene_resource_q3_image_kind(format);
    bool ok = image->level_count == expected.count && image->source_q3 && image->source_format == format && image->kind == kind;
    for (size_t i = 0; ok && i < expected.count; ++i)
        ok = image->levels[i].width == expected.levels[i].width && image->levels[i].height == expected.levels[i].height &&
            image->levels[i].bytes == expected.levels[i].rgba.size && image->levels[i].pixels &&
            !memcmp(image->levels[i].pixels, expected.levels[i].rgba.data, image->levels[i].bytes);
    qa_mip_chain_free(&expected); return ok;
}
static bool source_extended_builtins_fields(qa_source_save_io *io, qa_scene_resources *owner,
    qa_scene_resources *state, const qa_scene_resource_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = state->source_fog != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) {
        if (state->source_dlight || state->source_fog) return false;
        for (unsigned i = 0; i < 32; ++i) if (state->source_scratch[i]) return false;
        if (reading && !owner->detached) {
            if (owner->source_dlight || owner->source_fog) return false;
            for (unsigned i = 0; i < 32; ++i) if (owner->source_scratch[i]) return false;
        }
        return true;
    }
    if (!state->source_builtins) return false;
    qa_q3_color_lighting lighting;
    if (!qa_q3_color_lighting_read(&state->source_builtins_upload.color.device,
        state->source_builtins_upload.color.requested_overbright_bits, &lighting, io->error)) return false;
    for (unsigned i = 0; i < 32; ++i) {
        if (!image_field(io, refs, owner, &state->source_scratch[i]) ||
            !source_builtin_image(state->source_scratch[i], &state->source_builtins_upload, "*scratch", 16,
                lighting.identity_light_byte, false, true, io->error)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (state->source_scratch[i] == state->source_scratch[j] ||
                state->source_scratch[i]->identity == state->source_scratch[j]->identity) return false;
        if (reading && !owner->detached && owner->source_scratch[i] != state->source_scratch[i]) return false;
    }
    if (!image_field(io, refs, owner, &state->source_dlight) || !image_field(io, refs, owner, &state->source_fog)) return false;
    uint8_t light[16 * 16 * 4], fog[256 * 32 * 4];
    scene_image_dlight_pixels(light);
    scene_image_fog_pixels(fog);
    return source_generated_image(state->source_dlight, &state->source_builtins_upload, "*dlight", 16, 16, light, false, io->error) &&
        source_generated_image(state->source_fog, &state->source_builtins_upload, "*fog", 256, 32, fog, true, io->error) &&
        (!reading || owner->detached || (state->source_dlight == owner->source_dlight && state->source_fog == owner->source_fog));
}
static bool content_field(qa_source_save_io *io, qa_scene_resources *owner,
    const qa_scene_resource_checkpoint_refs *refs, qa_resource **resource, qa_mount_id *mount)
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
    qa_vfs_mount_info selected = {0}; bool found = reading;
    qa_mount_id historical = reading ? 0 : *mount;
    qa_vfs_resource_origin origin = {0};
    if (!reading) {
        found = qa_vfs_resource_origin_read(owner->vfs, historical, *resource, &origin);
        selected.id = historical; selected.is_archive = origin.archive;
        selected.identity = origin.archive ? &origin.archive_identity : NULL;
    }
    qa_fs_identity archive_identity = {{0}};
    size_t ordinal = 0; bool archive = false;
    if (!reading && found) {
        archive = qa_resource_archive_origin(*resource, &archive_identity, &ordinal);
        if (selected.is_archive != archive || (archive && (!selected.identity || !qa_fs_identity_equal(selected.identity, &archive_identity)))) return false;
    }
    bool ok = found && text_field(io, &path, &owned) &&
        qa_source_save_u64(io, &historical) && historical != 0 &&
        qa_source_save_bool(io, &archive);
    if (ok && archive) ok = qa_source_save_count(io, &ordinal, SIZE_MAX);
    if (ok && reading) {
        const qa_resource *decoded = NULL;
        ok = owner->vfs && refs->resource_decode(refs->context, pool, version, &decoded, io->error) && decoded &&
            qa_resource_id(decoded) == version &&
            qa_resource_pool_find(qa_vfs_resources(owner->vfs), qa_resource_id(decoded)) == decoded &&
            !strcmp(qa_resource_path(decoded), path);
        if (ok) {
            ok = qa_vfs_resource_origin_read(owner->vfs, historical, decoded, &origin);
            selected.id = historical; selected.is_archive = origin.archive;
            selected.identity = origin.archive ? &origin.archive_identity : NULL;
        } ok = ok && selected.is_archive == archive;
        if (ok) {
            qa_fs_identity actual_archive; size_t actual_ordinal = 0;
            bool actual_origin = qa_resource_archive_origin(decoded, &actual_archive, &actual_ordinal);
            ok = actual_origin == archive && (!archive || (selected.identity &&
                qa_fs_identity_equal(selected.identity, &actual_archive) && actual_ordinal == ordinal));
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
static bool palette_source_fields(qa_source_save_io *io, qa_scene_resources *owner,
    qa_scene_resources *state, unsigned family, const qa_scene_resource_checkpoint_refs *refs)
{
    qa_resource **resource = &state->palette_resources[family];
    qa_vfs_acquisition *opening = &state->palette_openings[family];
    if (!content_field(io, owner, refs, resource, &opening->mount)) return false;
    if (!*resource) return !opening->resource_id && !opening->path && !opening->opening_present;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (reading) {
        state->palettes[family] = (qa_buffer){malloc(768), 768};
        if (!state->palettes[family].data) return fail(io->error, QA_ERROR_MEMORY, "Restoring installed resource palette");
    }
    if (state->palettes[family].size != 768) return false;
    if (reading) opening->resource_id = qa_resource_id(*resource);
    else if (opening->resource_id != qa_resource_id(*resource)) return false;
    char **fields[] = {&opening->path, &opening->lookup_path, &opening->link_source, &opening->link_target};
    for (unsigned i = 0; i < 4; ++i) {
        const char *value = *fields[i]; char *owned = NULL;
        bool ok = text_field(io, &value, &owned);
        if (reading) *fields[i] = owned;
        if (!ok) return false;
    }
    if (strcmp(opening->path, family == QA_SCENE_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx") ||
        !qa_vfs_acquisition_opening_codec(io, owner->vfs, opening) || !opening->opening_present) return false;
    qa_bytes bytes = qa_resource_bytes(*resource);
    if (family == QA_SCENE_Q1) {
        if (bytes.size != 768) return false;
        if (reading) memcpy(state->palettes[family].data, bytes.data, 768);
        return reading || !memcmp(bytes.data, state->palettes[family].data, 768);
    }
    qa_image image = {0};
    bool ok = qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &image, io->error) && image.palette.size >= 1024;
    for (size_t i = 0; ok && i < 256; ++i) {
        if (reading) memcpy(state->palettes[family].data + i * 3, image.palette.data + i * 4, 3);
        else ok = !memcmp(image.palette.data + i * 4, state->palettes[family].data + i * 3, 3);
    }
    qa_image_free(&image); return ok;
}

static bool cache_options(qa_source_save_io *io, image_cache *entry)
{
    return qa_scene_image_options_fields(io, &entry->options, entry->palette, entry->translation,
                                         &entry->exact_file);
}
static bool acquisition_fields(qa_source_save_io *io, qa_scene_resources *owner,
    const qa_resource *resource, qa_mount_id mount, qa_vfs_acquisition *opening)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = !reading && opening->mount != 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return !opening->mount && !opening->resource_id && !opening->path && !opening->lookup_path &&
        !opening->link_source && !opening->link_target && !opening->opening_present && !opening->opening.rank &&
        !opening->opening.order && !opening->opening.order_count && !opening->opening.prefix && !opening->opening.user_overlay;
    if (!resource || !owner->vfs || !mount) return false;
    if (reading) { opening->mount = mount; opening->resource_id = qa_resource_id(resource); }
    else if (opening->mount != mount || opening->resource_id != qa_resource_id(resource)) return false;
    char **fields[] = {&opening->path, &opening->lookup_path, &opening->link_source, &opening->link_target};
    for (unsigned i = 0; i < 4; ++i) {
        const char *value = *fields[i]; char *owned = NULL;
        bool ok = text_field(io, &value, &owned);
        if (reading) *fields[i] = owned;
        if (!ok) return false;
    }
    return qa_vfs_acquisition_opening_codec(io, owner->vfs, opening) && opening->opening_present &&
        qa_vfs_acquisition_retained(owner->vfs, opening, io->error);
}
static bool cache_receipts_fields(qa_source_save_io *io, qa_scene_resources *owner,
    image_cache *entry, const qa_scene_resource_checkpoint_refs *refs)
{
    uint32_t status = entry->palette_error;
    if (!acquisition_fields(io, owner, entry->source_record, entry->source_mount, &entry->source_opening) ||
        !acquisition_fields(io, owner, entry->logical_record, entry->logical_mount, &entry->logical_opening) ||
        !qa_source_save_bool(io, &entry->palette_attempted) || !qa_source_save_u32(io, &status) || status > QA_ERROR_NOT_FOUND ||
        !content_field(io, owner, refs, &entry->palette_source, &entry->palette_opening.mount)) return false;
    entry->palette_error = (qa_status)status;
    bool logical_path = entry->logical_path != NULL;
    if (!qa_source_save_bool(io, &logical_path)) return false;
    if (logical_path) {
        const char *value = entry->logical_path; char *owned = NULL;
        bool ok = text_field(io, &value, &owned);
        if (io->direction == QA_SOURCE_SAVE_READ) entry->logical_path = owned;
        if (!ok || !entry->logical_record || !value || !*value) return false;
    }
    qa_mount_id mount = entry->palette_opening.mount;
    if (io->direction == QA_SOURCE_SAVE_READ) entry->palette_opening.mount = 0;
    if (!acquisition_fields(io, owner, entry->palette_source, mount, &entry->palette_opening)) return false;
    if (!entry->palette_attempted) return !entry->palette_source && status == QA_OK;
    return entry->palette_source ? entry->palette_opening.opening_present : status != QA_OK && status != QA_ERROR_MEMORY;
}
static bool aliases_fields(qa_source_save_io *io, qa_scene_resources *owner,
    qa_scene_resources *state, const qa_scene_resource_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (const image_alias *alias = state->aliases; alias; alias = alias->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 12 : SIZE_MAX)) return false;
    const image_alias *current = state->aliases;
    if (reading) state->vfs = owner->vfs;
    for (size_t i = 0; i < count; ++i) {
        image_alias *alias = reading ? calloc(1, sizeof(*alias)) : (image_alias *)current;
        if (!alias) return fail(io->error, QA_ERROR_MEMORY, "Restoring retained image alias");
        char **texts[] = {&alias->name, &alias->request, &alias->source_path, &alias->logical_path};
        bool ok = true;
        for (unsigned j = 0; ok && j < 4; ++j) {
            const char *value = *texts[j]; char *owned = NULL;
            ok = text_field(io, &value, &owned);
            if (reading) *texts[j] = owned;
        }
        qa_resource **objects[] = {&alias->source, &alias->logical_source, &alias->palette_source};
        qa_vfs_acquisition *receipts[] = {&alias->source_opening, &alias->logical_opening, &alias->palette_opening};
        for (unsigned j = 0; ok && j < 3; ++j) {
            qa_mount_id mount = receipts[j]->mount;
            ok = content_field(io, owner, refs, objects[j], &mount) &&
                acquisition_fields(io, owner, *objects[j], mount, receipts[j]);
            if (ok && *objects[j] && !receipts[j]->opening_present) ok = false;
        }
        uint32_t status = alias->palette_error;
        uint32_t source_status = alias->source_error;
        if (ok) ok = qa_source_save_bool(io, &alias->palette_attempted) &&
            qa_source_save_u32(io, &status) && status <= QA_ERROR_NOT_FOUND &&
            qa_source_save_u32(io, &source_status) && source_status <= QA_ERROR_NOT_FOUND;
        image_cache options = {.options = alias->decode_options};
        if (!reading) {
            memcpy(options.palette, alias->palette, 768); memcpy(options.translation, alias->translation, 256);
        }
        if (ok) ok = cache_options(io, &options) && !options.exact_file;
        if (reading && ok) {
            alias->palette_error = (qa_status)status; alias->decode_options = options.options;
            alias->source_error = (qa_status)source_status;
            memcpy(alias->palette, options.palette, 768); memcpy(alias->translation, options.translation, 256);
            if (alias->decode_options.palette_rgb.size) alias->decode_options.palette_rgb.data = alias->palette;
            if (alias->decode_options.translation.size) alias->decode_options.translation.data = alias->translation;
            qa_scene_image_alias_source source = scene_resource_alias_source(alias);
            for (const image_alias *old = state->aliases; old; old = old->next)
                if (!strcmp(old->name, alias->name)) { ok = false; break; }
            if (ok) ok = qa_scene_image_alias_bind(state, alias->name, &source, io->error);
        }
        if (reading) scene_resource_alias_free(alias);
        else current = current->next;
        if (!ok) return false;
    }
    return true;
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
    bool source_variant;
    bool generic_variant, generic_variant_mipmap;
    bool recipient_first_upload;
    recipient_image_binding *recipient_bindings;
    qa_q3_image_upload_options upload;
    qa_scene_resources *parent_owner;
} sampling_row;
typedef struct sampling_state { sampling_row *rows; size_t count; } sampling_state;
static void sampling_clear(sampling_state *state)
{
    for (size_t i = 0; i < state->count; ++i) {
        qa_scene_image_release(state->rows[i].image); qa_scene_image_release(state->rows[i].source);
        qa_scene_resources_destroy(state->rows[i].parent_owner);
        while (state->rows[i].recipient_bindings) {
            recipient_image_binding *binding = state->rows[i].recipient_bindings;
            state->rows[i].recipient_bindings = binding->next;
            qa_scene_image_release(binding->source); free(binding);
        }
    }
    free(state->rows); *state = (sampling_state){0};
}
static bool sampling_copy(const sampling_row *row)
{
    const qa_scene_image *image = row->image, *source = row->source;
    if (row->source_variant)
        return image != source && image->kind != QA_SCENE_DEPTH32F && source->kind != QA_SCENE_DEPTH32F &&
            (!row->generic_variant || !image->source_q3) &&
            !strcmp(image->name, source->name) && image->wrap == source->wrap &&
            image->filter == (row->generic_variant && !row->generic_variant_mipmap &&
                source->filter >= QA_SCENE_NEAREST_MIPMAP_NEAREST ? QA_SCENE_LINEAR : source->filter) &&
            image->logical_width == source->logical_width && image->logical_height == source->logical_height &&
            image->animation_count == source->animation_count;
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
    const owned_image *owned = (const owned_image *)image;
    return owned->source_variant_source ? owned->source_variant_source : owned->sampling_source;
}
static const recipient_image_binding *sampling_bindings(const sampling_state *state, const qa_scene_image *image)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->rows[i].image == image) return state->rows[i].recipient_bindings;
    return ((const owned_image *)image)->recipient_bindings;
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
                size_t edge = visit->edge++;
                if (!edge) next = sampling_parent(state, image);
                else if (edge < image->animation_count) next = image->animation[edge];
                else {
                    size_t ordinal = edge - (image->animation_count ? image->animation_count : 1);
                    const recipient_image_binding *binding = sampling_bindings(state, image);
                    while (binding && ordinal) { binding = binding->next; --ordinal; }
                    if (binding) next = binding->source;
                    else { visit->active = false; --depth; continue; }
                }
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
        if (image->sampling_source || image->source_variant_source) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 17 : SIZE_MAX / sizeof(*state->rows)) ||
        count > SIZE_MAX / sizeof(*state->rows)) return false;
    state->rows = count ? calloc(count, sizeof(*state->rows)) : NULL;
    if (count && !state->rows) return fail(io->error, QA_ERROR_MEMORY, "retaining sampled-image source edges");
    state->count = count;
    const owned_image *image = owner->names->images;
    for (size_t i = 0; i < count; ++i) {
        sampling_row *row = state->rows + i;
        if (!reading) {
            while (image && !image->sampling_source && !image->source_variant_source) image = image->next;
            if (!image) return false;
            row->image = (qa_scene_image *)&image->image;
            row->source_variant = image->source_variant_source != NULL;
            row->generic_variant = image->generic_variant;
            row->generic_variant_mipmap = image->generic_variant_mipmap;
            row->recipient_first_upload = image->recipient_first_upload;
            row->source = row->source_variant ? image->source_variant_source : image->sampling_source;
            qa_scene_image_retain(row->image); qa_scene_image_retain(row->source);
            recipient_image_binding **link = &row->recipient_bindings;
            for (const recipient_image_binding *binding = image->recipient_bindings; binding; binding = binding->next) {
                *link = calloc(1, sizeof(**link));
                if (!*link) return fail(io->error, QA_ERROR_MEMORY, "retaining first-upload image aliases");
                (*link)->source = binding->source; qa_scene_image_retain(binding->source); link = &(*link)->next;
            }
            row->upload = image->source_variant_upload;
            row->mipmap = row->source_variant ? false : image->sampling_mipmap; image = image->next;
        }
        /* image_field owns one retained target reference on decode only. */
        if (!image_field(io, refs, owner, &row->image)) return false;
        uint64_t source = 0;
        if (!reading && !refs->image_encode(refs->context, row->source, &source, io->error)) return false;
        if (!qa_source_save_u64(io, &source) || !qa_source_save_bool(io, &row->mipmap)) return false;
        {
            if (!qa_source_save_bool(io, &row->source_variant)) return false;
            if ((!qa_source_save_bool(io, &row->generic_variant) ||
                !qa_source_save_bool(io, &row->generic_variant_mipmap) ||
                (!row->generic_variant && row->generic_variant_mipmap) ||
                (row->generic_variant && !row->source_variant))) return false;
            if ((!qa_source_save_bool(io, &row->recipient_first_upload) ||
                (row->recipient_first_upload && (!row->source_variant || row->generic_variant)))) return false;
            {
                size_t bindings = 0;
                if (!reading) for (const recipient_image_binding *binding = row->recipient_bindings;
                    binding; binding = binding->next) ++bindings;
                if (!qa_source_save_count(io, &bindings, reading ? io->input.size / 8 : SIZE_MAX) ||
                    (bindings && !row->recipient_first_upload)) return false;
                recipient_image_binding **link = &row->recipient_bindings;
                for (size_t binding_index = 0; binding_index < bindings; ++binding_index) {
                    if (reading) {
                        *link = calloc(1, sizeof(**link));
                        if (!*link) return fail(io->error, QA_ERROR_MEMORY, "restoring first-upload image aliases");
                    }
                    recipient_image_binding *binding = *link;
                    uint64_t id = 0;
                    if (!reading && !refs->image_encode(refs->context, binding->source, &id, io->error)) return false;
                    if (!qa_source_save_u64(io, &id)) return false;
                    if (reading) {
                        const qa_scene_image *decoded = NULL;
                        if (!refs->image_decode(refs->context, id, &decoded, io->error) || !decoded) return false;
                        qa_scene_image_retain(decoded); binding->source = decoded;
                    }
                    if (binding->source == row->image || binding->source->source_q3 ||
                        strcmp(binding->source->name, row->image->name) ||
                        binding->source->animation_count != row->image->animation_count) return false;
                    for (const recipient_image_binding *previous = row->recipient_bindings; previous != binding;
                        previous = previous->next) if (previous->source == binding->source) return false;
                    link = &binding->next;
                }
            }
            if (row->source_variant && (row->mipmap ||
                (!row->generic_variant && !(qa_q3_image_upload_options_precision_codec(io, &row->upload))))) return false;
        }
        if (reading) {
            const qa_scene_image *decoded = NULL;
            if (!refs->image_decode(refs->context, source, &decoded, io->error) || !decoded) return false;
            qa_scene_image_retain(decoded); row->source = decoded;
        }
        if (!sampling_copy(row)) return false;
        if (reading && row->source_variant &&
            !scene_resource_variant_parent_retain(owner, row->source, &row->parent_owner, io->error)) return false;
        for (size_t j = 0; j < i; ++j) if (state->rows[j].image == row->image) return false;
    }
    return sampling_graph(state, io->error);
}

static bool resource_fields(qa_source_save_io *io, qa_scene_resources *owner, qa_scene_resources *state,
    const qa_scene_resource_checkpoint_refs *refs, sampling_state *sampling)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q', 'A', 'R', 'S'}; uint32_t fullbright = state->fullbright_first;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QARS", 4) || !names_fields(io, owner, state) ||
        !qa_source_save_bool(io, &state->registrations_started) || !qa_source_save_u32(io, &fullbright) || fullbright > 256 ||
        !image_field(io, refs, owner, &state->white) || !image_field(io, refs, owner, &state->missing)) return false;
    if (!builtin_image(state->white, false) || !builtin_image(state->missing, true) ||
        (reading && !owner->detached &&
            (!same_image(owner->white, state->white) || !same_image(owner->missing, state->missing)))) return false;
    if (!source_builtins_fields(io, owner, state, refs)) return false;
    if (!source_extended_builtins_fields(io, owner, state, refs)) return false;
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
        if (!palette_source_fields(io, owner, state, family, refs)) return false;
        if (!state->palette_resources[family]) {
            if (!qa_source_save_count(io, &palette, 768) || (palette && palette != 768)) return false;
            if (reading && palette) {
                state->palettes[family] = (qa_buffer){malloc(palette), palette};
                if (!state->palettes[family].data) return fail(io->error, QA_ERROR_MEMORY, "Restoring generated resource palette");
            }
            if (!qa_source_save_bytes(io, state->palettes[family].data, palette)) return false;
        }
    }
    if (!aliases_fields(io, owner, state, refs)) return false;
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
        if (!ok || !cache_options(io, entry) ||
            !content_field(io, owner, refs, &entry->source_record, &entry->source_mount) || !entry->source_record ||
            !content_field(io, owner, refs, &entry->logical_record, &entry->logical_mount) ||
            (!cache_receipts_fields(io, owner, entry, refs)) ||
            !image_field(io, refs, owner, &entry->image)) return false;
        const char *cache_name=qa_strings_cstr(reading?state->names->strings:owner->names->strings,entry->name);
        bool source_rgb = entry->options.family == QA_SCENE_Q3 && entry->options.source_q3 &&
            entry->image->source_q3 && entry->image->kind == QA_SCENE_RGB8 &&
            (unsigned)entry->image->source_format <= QA_Q3_TEXTURE_RGB4_S3TC &&
            scene_resource_q3_image_kind(entry->image->source_format) == QA_SCENE_RGB8;
        if (!cache_name || strcmp(entry->image->name,cache_name) ||
            (entry->image->kind != QA_SCENE_RGBA8 && !source_rgb) ||
            entry->image->wrap!=entry->options.wrap || entry->image->filter!=entry->options.filter) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset,
                "Resource cache row %zu '%s' image '%s' kind %u Source Q3 %u format %u recipe Q3 %u wrap %u/%u filter %u/%u",
                i, cache_name ? cache_name : "", entry->image->name, (unsigned)entry->image->kind,
                (unsigned)entry->image->source_q3, (unsigned)entry->image->source_format,
                (unsigned)entry->options.source_q3, (unsigned)entry->image->wrap, (unsigned)entry->options.wrap,
                (unsigned)entry->image->filter, (unsigned)entry->options.filter);
            return false;
        }
        if (reading) entry->source = qa_resource_id(entry->source_record), entry->logical_source = qa_resource_id(entry->logical_record);
        else if (entry->source != qa_resource_id(entry->source_record) || entry->logical_source != qa_resource_id(entry->logical_record)) return false;
        for (size_t j = 0; j < i; ++j) if (same_cache_key(entry, &state->cache[j])) return false;
    }
    return sampling_fields(io, owner, refs, sampling);
}

static void state_clear(qa_scene_resources *state)
{
    for (size_t i = 0; i < state->cache_count; ++i) {
        qa_scene_image_release(state->cache[i].image);
        qa_resource_release(state->cache[i].source_record); qa_resource_release(state->cache[i].logical_record);
        qa_resource_release(state->cache[i].palette_source);
        free(state->cache[i].logical_path);
        qa_vfs_acquisition_dispose(&state->cache[i].source_opening);
        qa_vfs_acquisition_dispose(&state->cache[i].logical_opening);
        qa_vfs_acquisition_dispose(&state->cache[i].palette_opening);
    }
    free(state->cache);
    while (state->aliases) {
        image_alias *alias = state->aliases; state->aliases = alias->next; scene_resource_alias_free(alias);
    }
    for (unsigned i = 0; i < 3; ++i) {
        qa_buffer_free(&state->palettes[i]); qa_resource_release(state->palette_resources[i]);
        qa_vfs_acquisition_dispose(&state->palette_openings[i]);
    }
    qa_scene_image_release(state->white); qa_scene_image_release(state->missing);
    qa_scene_image_release(state->source_white); qa_scene_image_release(state->source_missing);
    qa_scene_image_release(state->source_identity);
    for (unsigned i = 0; i < 32; ++i) qa_scene_image_release(state->source_scratch[i]);
    qa_scene_image_release(state->source_dlight); qa_scene_image_release(state->source_fog);
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
    if (!qa_scene_resources_idle(owner) || owner->cache_count || owner->aliases || !refs || !refs->image_decode || !refs->resource_decode)
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
            qa_resource_release(owner->palette_resources[i]);
            qa_vfs_acquisition_dispose(&owner->palette_openings[i]);
            owner->palette_resources[i] = state.palette_resources[i];
            owner->palette_openings[i] = state.palette_openings[i];
            owner->policies[i] = state.policies[i]; owner->has_policy[i] = state.has_policy[i];
        }
        qa_scene_image_release(owner->white); qa_scene_image_release(owner->missing);
        owner->white = state.white; owner->missing = state.missing;
        qa_scene_image_release(owner->source_white); qa_scene_image_release(owner->source_missing);
        qa_scene_image_release(owner->source_identity);
        owner->source_white = state.source_white; owner->source_missing = state.source_missing;
        owner->source_identity = state.source_identity; owner->source_builtins = state.source_builtins;
        for (unsigned i = 0; i < 32; ++i) {
            qa_scene_image_release(owner->source_scratch[i]); owner->source_scratch[i] = state.source_scratch[i];
        }
        qa_scene_image_release(owner->source_dlight); qa_scene_image_release(owner->source_fog);
        owner->source_dlight = state.source_dlight; owner->source_fog = state.source_fog;
        owner->source_builtins_upload = state.source_builtins_upload;
        if (owner->source_builtins) {
            if (!owner->source_white->source_q3) owner->source_white->source_format = QA_Q3_TEXTURE_RGBA8;
            if (!owner->source_missing->source_q3) owner->source_missing->source_format = QA_Q3_TEXTURE_RGBA8;
            if (!owner->source_identity->source_q3) owner->source_identity->source_format = QA_Q3_TEXTURE_RGBA8;
            owner->source_white->source_q3 = true; owner->source_white->source_mipmap = false;
            owner->source_missing->source_q3 = true; owner->source_missing->source_mipmap = true;
            owner->source_identity->source_q3 = true; owner->source_identity->source_mipmap = false;
            for (unsigned i = 0; i < 32; ++i) if (owner->source_scratch[i]) {
                if (!owner->source_scratch[i]->source_q3) owner->source_scratch[i]->source_format = QA_Q3_TEXTURE_RGBA8;
                owner->source_scratch[i]->source_q3 = true; owner->source_scratch[i]->source_mipmap = false;
            }
            if (owner->source_dlight) { owner->source_dlight->source_q3 = true; owner->source_dlight->source_mipmap = false; }
            if (owner->source_fog) { owner->source_fog->source_q3 = true; owner->source_fog->source_mipmap = false; }
        }
        owner->cache = state.cache; owner->cache_count = state.cache_count; owner->cache_capacity = state.cache_capacity;
        owner->aliases = state.aliases;
        for (size_t i = 0; i < owner->cache_count; ++i) {
            const image_cache *entry = owner->cache + i;
            if (!entry->options.source_q3) continue;
            if (!entry->image->source_q3) entry->image->source_format = QA_Q3_TEXTURE_RGBA8;
            entry->image->source_q3 = true; entry->image->source_mipmap = entry->options.source_upload.mipmap;
            for (size_t frame = 1; frame < entry->image->animation_count; ++frame) {
                qa_scene_image *image = (qa_scene_image *)entry->image->animation[frame];
                if (!image->source_q3) image->source_format = QA_Q3_TEXTURE_RGBA8;
                image->source_q3 = true; image->source_mipmap = entry->options.source_upload.mipmap;
            }
        }
        owner->fullbright_first = state.fullbright_first; owner->registrations_started = state.registrations_started;
        owner->detached = false;
        for (size_t i = 0; i < sampling.count; ++i) {
            sampling_row *row = sampling.rows + i; owned_image *image = (owned_image *)row->image;
            qa_scene_image_release(image->sampling_source);
            qa_scene_image_release(image->source_variant_source);
            qa_scene_resources_destroy(image->source_variant_owner);
            image->sampling_source = row->source_variant ? NULL : row->source;
            image->source_variant_source = row->source_variant ? row->source : NULL;
            image->source_variant_owner = row->parent_owner; row->parent_owner = NULL;
            image->source_variant_upload = row->upload; image->sampling_mipmap = row->mipmap;
            image->generic_variant = row->generic_variant; image->generic_variant_mipmap = row->generic_variant_mipmap;
            image->recipient_first_upload = row->recipient_first_upload;
            while (image->recipient_bindings) {
                recipient_image_binding *binding = image->recipient_bindings;
                image->recipient_bindings = binding->next;
                qa_scene_image_release(binding->source); free(binding);
            }
            image->recipient_bindings = row->recipient_bindings; row->recipient_bindings = NULL;
            if (row->source_variant) {
                if (!row->generic_variant && !image->image.source_q3)
                    image->image.source_format = QA_Q3_TEXTURE_RGBA8;
                image->image.source_q3 = !row->generic_variant;
                image->image.source_mipmap = !row->generic_variant && row->upload.mipmap;
                image->variant_next = owner->variants; owner->variants = image;
                qa_scene_image_retain(&image->image);
            }
            row->source = NULL;
        }
        /* Legacy immutable images acquire the actual sampling provenance from
         * these saved parent edges after every edge has been installed. */
        for (size_t pass = 0; pass < sampling.count; ++pass)
            for (size_t i = 0; i < sampling.count; ++i) {
                sampling_row *row = sampling.rows + i;
                const qa_scene_image *parent = ((owned_image *)row->image)->sampling_source;
                if (parent && parent->source_q3) {
                    row->image->source_q3 = true;
                    row->image->source_format = parent->source_format;
                    row->image->source_mipmap = parent->source_mipmap && row->mipmap;
                }
                if (parent && parent->recipient_upload_pixels) {
                    row->image->recipient_upload_pixels = true;
                    row->image->recipient_mipmap = parent->recipient_mipmap && row->mipmap;
                }
            }
    } else state_clear(&state);
    sampling_clear(&sampling); qa_source_save_dispose(&io);
    owner->continuation_active = false;
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid saved resource cache");
    return ok;
}

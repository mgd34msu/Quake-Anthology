#include "resources_internal.h"
#include "qa/scene_save.h"
#include "qa/source_save.h"
#include "image_options_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_scene_image_set { qa_scene_image **images; size_t count; };
typedef struct image_edges { size_t *rows, count; } image_edges;
static bool fail(qa_error *error, const char *text)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false;
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
static bool name_field(qa_source_save_io *io, const char **name, char **owned)
{
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*name) : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX) || size == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)*name, size);
    char *text = malloc(size + 1);
    if (!text) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating image version name"); return false; }
    *owned = text;
    if (!qa_source_save_bytes(io, text, size) || memchr(text, 0, size)) return false;
    text[size] = 0; *name = text; return true;
}
static bool asset_resource_fields(qa_source_save_io *io, const qa_scene_resources *const *owners,
    size_t count, qa_resource **resource)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t id = reading ? 0 : qa_resource_id(*resource);
    if (!qa_source_save_u64(io, &id)) return false;
    if (!id) { if (reading) *resource = NULL; return true; }
    size_t scope = 0;
    if (!reading) {
        for (; scope < count; ++scope)
            if (owners[scope]->vfs && qa_resource_pool_find(qa_vfs_resources(owners[scope]->vfs), id) == *resource) break;
        if (scope == count) return fail(io->error, "Installed image source is outside its saved resource owners");
    }
    if (!qa_source_save_count(io, &scope, count - 1)) return false;
    if (reading) {
        *resource = owners[scope]->vfs ? (qa_resource *)qa_resource_pool_find(qa_vfs_resources(owners[scope]->vfs), id) : NULL;
        if (!*resource) return fail(io->error, "Saved installed image resource is unavailable");
    }
    return true;
}
static bool asset_fields(qa_source_save_io *io, const qa_scene_resources *const *owners,
    size_t count, image_asset_recipe *recipe)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t palette_error = (uint32_t)recipe->palette_error;
    const char *path = reading ? NULL : recipe->path; char *owned_path = NULL;
    bool exact = false;
    bool ok = asset_resource_fields(io, owners, count, &recipe->source) && recipe->source &&
        asset_resource_fields(io, owners, count, &recipe->palette_source) &&
        qa_source_save_u8(io, &recipe->kind) && recipe->kind <= 2;
    if (ok && !recipe->kind) ok = name_field(io, &path, &owned_path);
    if (ok && recipe->kind) {
        ok = qa_source_save_u8(io, &recipe->level_count) && recipe->level_count && recipe->level_count <= 4;
        for (size_t i = 0; ok && i < recipe->level_count; ++i)
            ok = qa_source_save_u64(io, recipe->offsets + i) && qa_source_save_u32(io, recipe->widths + i) &&
                recipe->widths[i] && qa_source_save_u32(io, recipe->heights + i) && recipe->heights[i] &&
                (uint64_t)recipe->widths[i] * recipe->heights[i] <= SIZE_MAX / 4;
        uint32_t layer = recipe->layer;
        ok = ok && qa_source_save_u32(io, &layer) && layer <= QA_PALETTE_FULLBRIGHT &&
            qa_source_save_i32(io, &recipe->fullbright_last) &&
            qa_source_save_bool(io, &recipe->flood_skin) && qa_source_save_bool(io, &recipe->generate_mips) &&
            qa_source_save_u32(io, &recipe->overbright) && recipe->overbright <= 15;
        recipe->layer = (qa_palette_layer)layer;
    }
    ok = ok &&
        qa_scene_image_options_fields(io, &recipe->options, recipe->palette, recipe->translation, &exact) && !exact &&
        qa_source_save_u32(io, &recipe->fullbright_first) && recipe->fullbright_first <= 256 &&
        qa_source_save_u32(io, &recipe->gif_frame) &&
        qa_source_save_bool(io, &recipe->palette_attempted) && qa_source_save_u32(io, &palette_error) &&
        qa_source_save_bool(io, &recipe->generic_upload) && qa_source_save_bool(io, &recipe->recipient);
    if (ok && recipe->recipient) ok = qa_q3_image_upload_options_precision_codec(io, &recipe->recipient_upload);
    if (ok) ok = qa_source_save_u8(io, &recipe->sky_layer) && recipe->sky_layer <= 2 &&
        qa_source_save_bool(io, &recipe->quake64) && qa_source_save_bool(io, &recipe->post_upload) &&
        qa_source_save_bool(io, &recipe->post_mipmap);
    if (reading) {
        recipe->path = owned_path; recipe->palette_error = (qa_status)palette_error;
    }
    if (!ok && (!io->error || io->error->code == QA_OK))
        fail(io->error, "Saved installed image resource is unavailable");
    return ok;
}
static bool image_fields(qa_source_save_io *io, qa_scene_resources *owner,
    const qa_scene_resources *const *owners, size_t owner_count, const qa_scene_image *source,
    qa_scene_image **out, uint64_t *lineage_revision)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    const image_asset_recipe *asset = reading ? NULL : ((const owned_image *)source)->asset;
    const owned_image *parent = reading ? NULL : (const owned_image *)source;
    const qa_resource *asset_source = asset ? asset->source : NULL;
    while (asset && !asset_source && parent) {
        const qa_scene_image *next = parent->sampling_source ? parent->sampling_source : parent->source_variant_source;
        parent = next ? (const owned_image *)next : NULL;
        if (parent && parent->asset) asset_source = parent->asset->source;
    }
    uint8_t storage = reading ? 0 : ((const owned_image *)source)->embedded_png ? 1 : asset_source ? 2 : 0;
    if (!qa_source_save_u8(io, &storage) || storage > 2) return false;
    bool embedded = storage == 1, installed = storage == 2;
    image_asset_recipe recipe = asset ? *asset : (image_asset_recipe){0};
    if (asset_source) recipe.source = (qa_resource *)asset_source;
    const char *name = reading ? NULL : source->name;
    char *owned_name = NULL; uint32_t kind = reading ? 0 : source->kind, wrap = reading ? 0 : source->wrap, filter = reading ? 0 : source->filter;
    uint64_t revision = reading ? 0 : source->revision;
    uint32_t logical_width = reading ? 0 : source->logical_width, logical_height = reading ? 0 : source->logical_height;
    qa_scene_vec4 border = reading ? (qa_scene_vec4){0} : source->border;
    size_t levels = reading ? 0 : source->level_count;
    bool source_q3 = reading ? false : source->source_q3, source_mipmap = reading ? false : source->source_mipmap;
    uint32_t source_format = reading ? (uint32_t)QA_Q3_TEXTURE_RGBA8 : (uint32_t)source->source_format;
    bool recipient_upload_pixels = reading ? false : source->recipient_upload_pixels;
    bool recipient_mipmap = reading ? false : source->recipient_mipmap;
    uint32_t source_texture_unit = reading ? 0 : source->source_texture_unit;
    bool source_after_upload_border = reading ? false : source->source_after_upload_border;
    bool source_dlight = reading ? false : source->source_dlight;
    qa_scene_vec4 source_upload_border = reading ? (qa_scene_vec4){0} : source->source_upload_border;
    qa_image recipient = reading ? (qa_image){0} : ((const owned_image *)source)->recipient_source;
    bool ok = name_field(io, &name, &owned_name) && qa_source_save_u32(io, &kind) && kind <= QA_SCENE_DEPTH32F &&
        qa_source_save_u32(io, &wrap) && wrap <= QA_SCENE_CLAMP && qa_source_save_u32(io, &filter) && filter <= QA_SCENE_LINEAR_MIPMAP_LINEAR &&
        qa_source_save_u64(io, &revision) && revision && qa_source_save_u64(io, lineage_revision) && *lineage_revision >= revision &&
        qa_source_save_u32(io, &logical_width) && logical_width && qa_source_save_u32(io, &logical_height) && logical_height &&
        qa_source_save_f32(io, &border.x) && qa_source_save_f32(io, &border.y) &&
        qa_source_save_f32(io, &border.z) && qa_source_save_f32(io, &border.w) &&
        isfinite(border.x) && isfinite(border.y) && isfinite(border.z) && isfinite(border.w) &&
        qa_source_save_count(io, &levels, reading ? io->input.size / 8 : SIZE_MAX) && levels;
    if (ok) ok = qa_source_save_bool(io, &source_q3) &&
        qa_source_save_bool(io, &source_mipmap) && (source_q3 || !source_mipmap) &&
        qa_source_save_bool(io, &recipient_upload_pixels);
    if (ok) ok = qa_source_save_bool(io, &recipient_mipmap) &&
        (recipient_upload_pixels || !recipient_mipmap);
    if (ok) ok = qa_source_save_u32(io, &source_texture_unit) &&
        source_texture_unit <= 1 && (source_q3 || !source_texture_unit);
    if (ok) ok = qa_source_save_u32(io, &source_format) &&
        source_format <= QA_Q3_TEXTURE_RGB4_S3TC && (source_q3 || source_format == QA_Q3_TEXTURE_RGB);
    if (ok) ok = qa_source_save_bool(io, &source_after_upload_border) &&
        qa_source_save_bool(io, &source_dlight) && (source_q3 || !source_dlight) &&
        qa_source_save_f32(io, &source_upload_border.x) && qa_source_save_f32(io, &source_upload_border.y) &&
        qa_source_save_f32(io, &source_upload_border.z) && qa_source_save_f32(io, &source_upload_border.w) &&
        isfinite(source_upload_border.x) && isfinite(source_upload_border.y) &&
        isfinite(source_upload_border.z) && isfinite(source_upload_border.w) &&
        (source_q3 || !source_after_upload_border) &&
        (source_after_upload_border || (source_upload_border.x == 0.0f && source_upload_border.y == 0.0f &&
         source_upload_border.z == 0.0f && source_upload_border.w == 0.0f));
    if (ok && embedded) ok = kind == QA_SCENE_RGBA8 && levels == 1 && !recipient.rgba.size;
    if (ok && installed) ok = asset_fields(io, owners, owner_count, &recipe);
    qa_scene_image_level *decoded = reading && ok && !embedded && !installed ? calloc(levels, sizeof(*decoded)) : NULL;
    if (reading && ok && !embedded && !installed && !decoded) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating saved mip levels"); ok = false; }
    for (size_t i = 0; ok && !embedded && !installed && i < levels; ++i) {
        qa_scene_image_level level = reading ? (qa_scene_image_level){0} : source->levels[i];
        uint64_t bytes = level.bytes;
        ok = qa_source_save_u32(io, &level.width) && level.width && qa_source_save_u32(io, &level.height) && level.height &&
            qa_source_save_u64(io, &bytes) && (uint64_t)level.width * level.height <= SIZE_MAX / 4 &&
            bytes == (uint64_t)level.width * level.height * 4;
        if (reading && ok) {
            if (bytes > io->input.size - io->offset) { ok = false; break; }
            level.bytes = (size_t)bytes; level.pixels = malloc(level.bytes); decoded[i] = level;
            if (!level.pixels) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating saved image pixels"); ok = false; break; }
        }
        if (!ok) break;
        if (kind == QA_SCENE_DEPTH32F) {
            for (size_t j = 0; ok && j < level.bytes; j += 4) {
                float value = 0;
                if (!reading) memcpy(&value, (const uint8_t *)level.pixels + j, 4);
                ok = qa_source_save_f32(io, &value);
                if (ok && reading) memcpy((uint8_t *)level.pixels + j, &value, 4);
            }
        } else ok = qa_source_save_bytes(io, (void *)level.pixels, level.bytes);
    }
    if (ok && !installed) {
        bool present = recipient.rgba.size != 0;
        ok = qa_source_save_bool(io, &present) && (!present || (source_q3 && !embedded));
        if (ok && present) {
            size_t bytes = recipient.rgba.size;
            ok = qa_source_save_u32(io, &recipient.width) && recipient.width &&
                qa_source_save_u32(io, &recipient.height) && recipient.height &&
                qa_source_save_count(io, &bytes, reading ? io->input.size - io->offset : SIZE_MAX) &&
                (uint64_t)recipient.width * recipient.height <= SIZE_MAX / 4 &&
                bytes == (size_t)recipient.width * recipient.height * 4;
            if (reading && ok) {
                recipient.rgba.data = malloc(bytes); recipient.rgba.size = bytes;
                if (!recipient.rgba.data) {
                    qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Restoring original Source upload pixels"); ok = false;
                }
            }
            if (ok) ok = qa_source_save_bytes(io, recipient.rgba.data, bytes);
        }
    }
    if (reading && ok) {
        ok = installed ? scene_resource_image_decode(owner, name, &recipe, out, io->error) :
            embedded ? qa_scene_image_load_embedded(owner, name, (qa_scene_wrap)wrap,
            (qa_scene_filter)filter, border, out, io->error) :
            qa_scene_image_create(owner, name, (qa_scene_image_kind)kind, decoded, levels,
                (qa_scene_wrap)wrap, (qa_scene_filter)filter, border, out, io->error);
        if (ok && installed) {
            owned_image *image = (owned_image *)*out;
            if (image->image.level_count < levels) { ok = fail(io->error, "Installed image lacks its saved mip levels"); }
            else {
                for (size_t i = levels; i < image->image.level_count; ++i) free((void *)image->levels[i].pixels);
                image->image.level_count = levels;
            }
        }
        if (ok) {
            (*out)->kind = (qa_scene_image_kind)kind; (*out)->wrap = (qa_scene_wrap)wrap;
            (*out)->filter = (qa_scene_filter)filter; (*out)->border = border;
            (*out)->revision = revision; (*out)->logical_width = logical_width; (*out)->logical_height = logical_height;
            (*out)->source_q3 = source_q3; (*out)->source_mipmap = source_mipmap;
            (*out)->source_format = source_q3 ? (qa_q3_texture_format)source_format : QA_Q3_TEXTURE_RGB;
            (*out)->source_texture_unit = source_texture_unit;
            (*out)->source_after_upload_border = source_after_upload_border;
            (*out)->source_dlight = source_dlight;
            (*out)->source_upload_border = source_upload_border;
            (*out)->recipient_upload_pixels = recipient_upload_pixels;
            (*out)->recipient_mipmap = recipient_mipmap;
            ((owned_image *)*out)->lineage->revision = *lineage_revision;
            if (!installed) { ((owned_image *)*out)->recipient_source = recipient; recipient = (qa_image){0}; }
        }
    }
    if (decoded) for (size_t i = 0; i < levels; ++i) free((void *)decoded[i].pixels);
    if (reading) qa_image_free(&recipient);
    if (reading) free(recipe.path);
    free(decoded); free(owned_name); return ok;
}
static bool signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q','A','I','M'};
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QAIM", 4);
}
bool qa_scene_images_checkpoint(const qa_scene_resources *const *owners, size_t count, qa_buffer *out, qa_error *error)
{
    if (!owners || !count || !out) return fail(error, "Image checkpoint requires actual qualified resource owners");
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!owners[i] || owners[i]->names->image_count > SIZE_MAX - total) return fail(error, "Invalid image resource owner inventory");
        for (size_t j = 0; j < i; ++j) if (owners[i] == owners[j]) return fail(error, "Image resource owner is duplicated");
        total += owners[i]->names->image_count;
    }
    if (total > SIZE_MAX / sizeof(owned_image *)) return fail(error, "Image inventory exceeds addressable storage");
    const owned_image **images = total ? malloc(total * sizeof(*images)) : NULL;
    if (total && !images) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Indexing immutable image versions"); return false; }
    size_t at = 0;
    for (size_t i = 0; i < count; ++i) for (const owned_image *image = owners[i]->names->images; image; image = image->next) images[at++] = image;
    qa_source_save_io io = {0}; size_t owner_count = count;
    bool ok = at == total && qa_source_save_writer(&io, NULL, error) && signature(&io) &&
        qa_source_save_count(&io, &owner_count, SIZE_MAX) && qa_source_save_count(&io, &total, SIZE_MAX);
    at = 0;
    for (size_t owner = 0; ok && owner < count; ++owner) for (const owned_image *entry = owners[owner]->names->images; ok && entry; entry = entry->next, ++at) {
        size_t scope = owner, group = at, animation = entry->image.animation_count;
        for (size_t i = 0; i < at; ++i) if (images[i]->lineage == entry->lineage) { group = i; break; }
        uint64_t latest = entry->lineage->revision;
        ok = qa_source_save_count(&io, &scope, SIZE_MAX) && qa_source_save_count(&io, &group, SIZE_MAX) &&
            image_fields(&io, (qa_scene_resources *)owners[owner], owners, count, &entry->image, NULL, &latest) && qa_source_save_count(&io, &animation, SIZE_MAX);
        for (size_t i = 0; ok && i < animation; ++i) {
            uint64_t index = UINT64_MAX;
            const qa_scene_image *frame = entry->image.animation ? entry->image.animation[i] : NULL;
            if (!i) ok = frame == NULL;
            else { size_t row = 0; ok = frame && qa_scene_image_owner_index(owners, count, frame, &row); index = row; }
            ok = ok && qa_source_save_u64(&io, &index);
        }
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid immutable image snapshot");
    qa_source_save_dispose(&io); free(images); return ok;
}
void qa_scene_image_set_destroy(qa_scene_image_set *set)
{
    if (!set) return;
    if (set->images) for (size_t i = 0; i < set->count; ++i) qa_scene_image_release(set->images[i]);
    free(set->images); free(set);
}
const qa_scene_image *qa_scene_image_set_at(const qa_scene_image_set *set, size_t index)
{
    return set && index < set->count ? set->images[index] : NULL;
}
size_t qa_scene_image_set_count(const qa_scene_image_set *set) { return set ? set->count : 0; }
static bool acyclic(image_edges *edges, size_t count, qa_error *error)
{
    if (count > SIZE_MAX / sizeof(size_t)) return false;
    size_t *degree = count ? calloc(count, sizeof(*degree)) : NULL, *queue = count ? malloc(count * sizeof(*queue)) : NULL;
    if (count && (!degree || !queue)) {
        free(degree); free(queue); qa_error_set(error, QA_ERROR_MEMORY, 0, "Validating image animation graph"); return false;
    }
    bool ok = true; size_t begin = 0, end = 0;
    for (size_t i = 0; ok && i < count; ++i) for (size_t j = 1; j < edges[i].count; ++j) {
        size_t child = edges[i].rows[j];
        if (degree[child] == SIZE_MAX) { ok = false; break; }
        ++degree[child];
    }
    for (size_t i = 0; i < count; ++i) if (!degree[i]) queue[end++] = i;
    while (ok && begin < end) {
        size_t parent = queue[begin++];
        for (size_t j = 1; j < edges[parent].count; ++j) {
            size_t child = edges[parent].rows[j]; if (!--degree[child]) queue[end++] = child;
        }
    }
    ok = ok && end == count; free(degree); free(queue); return ok;
}
bool qa_scene_images_restore(qa_scene_resources *const *owners, size_t count, qa_bytes bytes,
    qa_scene_image_set **out, qa_error *error)
{
    if (!owners || !count || !out || count > SIZE_MAX / sizeof(owned_image *)) return fail(error, "Image restore requires actual detached resource owners");
    for (size_t i = 0; i < count; ++i) {
        if (!owners[i]) return fail(error, "Image restore resource owner is absent");
        for (size_t j = 0; j < i; ++j) if (owners[i] == owners[j]) return fail(error, "Image restore resource owner is duplicated");
    }
    qa_source_save_io io = {0}; size_t owner_count = 0, total = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io) &&
        qa_source_save_count(&io, &owner_count, count) && owner_count == count &&
        qa_source_save_count(&io, &total, bytes.size / 16) && total <= SIZE_MAX / sizeof(qa_scene_image *);
    qa_scene_image_set *set = ok ? calloc(1, sizeof(*set)) : NULL;
    image_edges *edges = ok && total ? calloc(total, sizeof(*edges)) : NULL;
    owned_image **previous_heads = ok ? malloc(count * sizeof(*previous_heads)) : NULL;
    if (previous_heads) for (size_t i = 0; i < count; ++i) previous_heads[i] = owners[i]->names->images;
    if (ok && set) { set->count = total; set->images = total ? calloc(total, sizeof(*set->images)) : NULL; }
    if (ok && (!set || !previous_heads || (total && (!set->images || !edges)))) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating immutable image restore table"); ok = false;
    }
    for (size_t i = 0; ok && i < total; ++i) {
        size_t scope = 0, group = 0; uint64_t latest = 0;
        ok = qa_source_save_count(&io, &scope, count - 1) && qa_source_save_count(&io, &group, i) &&
            image_fields(&io, owners[scope], (const qa_scene_resources *const *)owners, count, NULL, &set->images[i], &latest);
        if (!ok) break;
        owned_image *image = (owned_image *)set->images[i];
        if (group != i) {
            owned_image *first = (owned_image *)set->images[group];
            if (first->lineage->revision != latest) { ok = false; break; }
            free(image->lineage); image->lineage = first->lineage; ++image->lineage->references;
            image->image.identity = first->image.identity;
        }
        ok = qa_source_save_count(&io, &edges[i].count, bytes.size / 8) && edges[i].count <= SIZE_MAX / sizeof(size_t);
        if (ok && edges[i].count) {
            edges[i].rows = calloc(edges[i].count, sizeof(*edges[i].rows));
            if (!edges[i].rows) { qa_error_set(error, QA_ERROR_MEMORY, io.offset, "Restoring image animation aliases"); ok = false; break; }
        }
        for (size_t j = 0; ok && j < edges[i].count; ++j) {
            uint64_t row = 0; ok = qa_source_save_u64(&io, &row) && (!j ? row == UINT64_MAX : row < total && row != i);
            if (ok) edges[i].rows[j] = j ? (size_t)row : SIZE_MAX;
        }
    }
    if (ok) for (size_t i = 0; i < count; ++i) {
        owned_image *current = owners[i]->names->images, *previous = previous_heads[i];
        while (current != previous_heads[i]) {
            owned_image *next = current->next;
            current->next = previous;
            if (previous) previous->previous = current;
            previous = current; current = next;
        }
        owners[i]->names->images = previous;
        if (previous) previous->previous = NULL;
    }
    if (ok) ok = qa_source_save_finish(&io, NULL) && acyclic(edges, total, error);
    for (size_t i = 0; ok && i < total; ++i) {
        if (!edges[i].count) continue;
        const qa_scene_image **animation = calloc(edges[i].count, sizeof(*animation));
        if (!animation) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining restored image animation"); ok = false; break; }
        set->images[i]->animation = animation; set->images[i]->animation_count = edges[i].count;
        for (size_t j = 1; j < edges[i].count; ++j) {
            animation[j] = set->images[edges[i].rows[j]]; qa_scene_image_retain(animation[j]);
        }
    }
    if (edges) for (size_t i = 0; i < total; ++i) free(edges[i].rows);
    free(edges); free(previous_heads); qa_source_save_dispose(&io);
    if (ok) *out = set;
    else { qa_scene_image_set_destroy(set); if (!error || error->code == QA_OK) fail(error, "Invalid immutable image continuation"); }
    return ok;
}

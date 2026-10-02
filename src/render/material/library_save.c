#include "library_save_private.h"
#include "qa/scene_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
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
bool qa_material_library_empty_detached(const qa_material_library *library)
{
    if (!library || !library->resources || !qa_material_library_idle(library) || library->catalog_ready ||
        library->order || library->fog_image || library->dlight_image || library->ordered || library->count ||
        library->capacity || library->catalog_sources || library->catalog_tail || library->catalog_current ||
        library->remaps || library->generated || library->video_start || library->video_context || library->video_required)
        return false;
    for (size_t i = 0; i < QA_MATERIAL_BUCKETS; ++i)
        if (library->scripts[i] || library->records[i]) return false;
    return true;
}
bool qa_material_library_order_ready(const qa_material_library *library)
{
    if (!library || !library->order) return false;
    for (size_t i = 0; i < library->count; ++i)
        if (!qa_material_order_has_record(library->order, &library->ordered[i]->material)) return false;
    return true;
}
size_t qa_material_library_catalog_resource_count(const qa_material_library *library)
{
    size_t count = 0;
    if (library) for (const qa_material_catalog_source *source = library->catalog_sources; source; source = source->next)
        if (source->resource) ++count;
    return count;
}
const qa_resource *qa_material_library_catalog_resource_at(const qa_material_library *library, size_t index)
{
    if (library) for (const qa_material_catalog_source *source = library->catalog_sources; source; source = source->next)
        if (source->resource && !index--) return source->resource;
    return NULL;
}
static qa_material_catalog_source *catalog_source_at(const qa_material_library *library, size_t index)
{
    qa_material_catalog_source *source = library->catalog_sources;
    while (source && index) { source = source->next; --index; }
    return source;
}
static bool catalog_source_index(const qa_material_library *library, const qa_material_catalog_source *source, uint64_t *index)
{
    uint64_t i = 0;
    for (const qa_material_catalog_source *entry = library->catalog_sources; entry; entry = entry->next, ++i)
        if (entry == source) { *index = i; return true; }
    return false;
}
static bool catalog_sources(qa_source_save_io *io, qa_material_library *library,
    const qa_material_library_checkpoint_refs *refs, uint32_t schema)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (const qa_material_catalog_source *source = library->catalog_sources; source; source = source->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 41 : SIZE_MAX)) return false;
    qa_material_catalog_source *source = reading ? NULL : library->catalog_sources;
    for (size_t i = 0; i < count; ++i) {
        qa_material_catalog_source copy = source ? *source : (qa_material_catalog_source){0};
        qa_material_catalog_source *entry = reading ? calloc(1, sizeof(*entry)) : &copy;
        if (!entry) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader content source");
        if (reading) {
            if (library->catalog_tail) library->catalog_tail->next = entry;
            else library->catalog_sources = entry;
            library->catalog_tail = entry;
        }
        bool external = entry->resource != NULL;
        if (schema >= 2) {
            uint32_t family = entry->dependency_family;
            if (!qa_source_save_bool(io, &entry->dependency_scope) ||
                !qa_source_save_u32(io, &family) || family > QA_SCENE_Q3 ||
                !qa_source_save_bool(io, &entry->dependency_has_palette) ||
                (!entry->dependency_scope && (family || entry->dependency_has_palette)) ||
                (entry->dependency_has_palette && !qa_source_save_bytes(io,
                    entry->dependency_palette, sizeof(entry->dependency_palette)))) return false;
            if (reading) entry->dependency_family = (qa_scene_family)family;
        }
        uint64_t pool = 0, resource = 0;
        size_t size = reading ? 0 : entry->bytes.size;
        if (!qa_source_save_bool(io, &external)) return false;
        if (external) {
            if (!reading && (!refs->resource_encode ||
                !refs->resource_encode(refs->context, entry->resource, &pool, &resource, io->error))) return false;
            if (!qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource)) return false;
            if (reading) {
                const qa_resource *resolved = NULL;
                if (!refs->resource_decode || !refs->resource_decode(refs->context, pool, resource, &resolved, io->error) || !resolved)
                    return false;
                entry->resource = (qa_resource *)resolved;
                qa_resource_retain(entry->resource);
                entry->bytes = qa_resource_bytes(entry->resource);
            }
        }
        if (!qa_source_save_bytes(io, entry->digest.bytes, sizeof(entry->digest.bytes)) ||
            !qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
        if (reading && !external) {
            entry->owned_bytes = size ? malloc(size) : NULL;
            if (size && !entry->owned_bytes) return fail(io->error, QA_ERROR_MEMORY, "allocating saved inline shader source");
            entry->bytes = (qa_bytes){entry->owned_bytes, size};
        }
        if (entry->bytes.size != size || (size && !entry->bytes.data)) return false;
        if (reading && external) {
            if (size > io->input.size - io->offset ||
                (size && memcmp(entry->bytes.data, io->input.data + io->offset, size))) return false;
            io->offset += size;
        } else if (!qa_source_save_bytes(io, (void *)entry->bytes.data, size)) return false;
        qa_sha256_digest digest;
        qa_sha256(entry->bytes, &digest);
        if (!qa_sha256_equal(&digest, &entry->digest) ||
            (external && !qa_sha256_equal(&digest, qa_resource_digest(entry->resource)))) return false;
        if (!reading) source = source->next;
    }
    return true;
}
static bool catalog_copy_sources(qa_material_library *library, const qa_material_library *qualified, qa_error *error)
{
    for (const qa_material_catalog_source *source = qualified->catalog_sources; source; source = source->next) {
        qa_material_catalog_source *entry = calloc(1, sizeof(*entry));
        if (!entry) return fail(error, QA_ERROR_MEMORY, "retaining qualified shader catalog source");
        *entry = *source;
        entry->next = NULL;
        entry->owned_bytes = NULL;
        if (entry->resource) qa_resource_retain(entry->resource);
        else {
            entry->owned_bytes = source->bytes.size ? malloc(source->bytes.size) : NULL;
            if (source->bytes.size && !entry->owned_bytes) {
                free(entry);
                return fail(error, QA_ERROR_MEMORY, "retaining qualified inline shader bytes");
            }
            if (source->bytes.size) memcpy(entry->owned_bytes, source->bytes.data, source->bytes.size);
            entry->bytes.data = entry->owned_bytes;
        }
        if (library->catalog_tail) library->catalog_tail->next = entry;
        else library->catalog_sources = entry;
        library->catalog_tail = entry;
    }
    return true;
}
static bool material_index(const qa_material_library *library, const qa_material *material, uint64_t *index)
{
    if (!material) { *index = UINT64_MAX; return true; }
    size_t i = material->sorted_index;
    if (i < library->count && &library->ordered[i]->material == material) { *index = i; return true; }
    return false;
}
static void record_free(qa_material_record *record)
{
    if (!record) return;
    qa_material_clear(&record->material); free(record->base_name); qa_scene_image_release(record->base_image);
    qa_material_videos_clear(record);
    free(record->palette); free(record->translation); free(record);
}
static bool canonical(const char *name, qa_error *error)
{
    char *normalized = qa_material_name(name, error);
    bool ok = normalized && !strcmp(normalized, name);
    free(normalized); return ok;
}
static bool same_profile(const qa_material_profile *a, const qa_material_profile *b, bool source)
{
    return a->detail_textures == b->detail_textures && a->vertex_lighting == b->vertex_lighting &&
        (source || a->ui_fullscreen == b->ui_fullscreen) && a->permedia2 == b->permedia2 && a->multitexture == b->multitexture &&
        a->texture_env_add == b->texture_env_add && a->ignore_fast_path == b->ignore_fast_path;
}
static bool registration_capacity(size_t count, size_t capacity)
{
    return count >= 2 && count <= capacity && capacity >= 64 && capacity <= QA_MATERIAL_MAX_REGISTERED &&
        !(capacity & (capacity - 1)) && capacity <= SIZE_MAX / sizeof(qa_material_record *);
}
typedef struct remap_node { const qa_material_remap_record *record; } remap_node;
static int remap_compare(const void *a, const void *b)
{ return strcmp(((const remap_node *)a)->record->original, ((const remap_node *)b)->record->original); }
static bool remap_records_valid(const qa_material_library *library, qa_error *error)
{
    size_t count = 0;
    for (const qa_material_remap_record *r = library->remaps; r; r = r->next) ++count;
    if (count > SIZE_MAX / sizeof(remap_node)) return false;
    remap_node *nodes = count ? calloc(count, sizeof(*nodes)) : NULL;
    if (count && !nodes) return fail(error, QA_ERROR_MEMORY, "qualifying retained shader aliases");
    size_t i = 0; bool ok = true;
    for (const qa_material_remap_record *r = library->remaps; r; r = r->next) {
        if (!canonical(r->original, error) || !canonical(r->replacement, error) || !strcmp(r->original, r->replacement)) { ok = false; break; }
        nodes[i++].record = r;
    }
    if (ok && count > 1) qsort(nodes, count, sizeof(*nodes), remap_compare);
    for (i = 0; ok && i < count; ++i) {
        if (i && !strcmp(nodes[i - 1].record->original, nodes[i].record->original)) { ok = false; break; }
    }
    free(nodes); return ok;
}
static bool library_valid(const qa_material_library *library, qa_error *error)
{
    size_t count = library->count;
    if (!registration_capacity(count, library->capacity) || !library->ordered) return false;
    bool *registrations = count ? calloc(count, sizeof(*registrations)) : NULL;
    if (count && !registrations) return fail(error, QA_ERROR_MEMORY, "qualifying material registrations");
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_material *material = &library->ordered[i]->material; uint64_t edge = UINT64_MAX;
        ok = material->sorted_index == i && material->registration < count &&
            material->library == library &&
            canonical(material->name, error) && same_profile(&material->profile, &library->profile, library->source_profile) &&
            material_index(library, material->remapped, &edge) && edge != i;
        const qa_material_record *record = library->ordered[i];
        if (ok && record->source_variant_parent) {
            const qa_material_record *parent = record->source_variant_parent;
            uint64_t parent_index = UINT64_MAX;
            ok = material_index(library, &parent->material, &parent_index) && parent_index != i &&
                !parent->source_variant_parent && !strcmp(parent->material.name, material->name) &&
                record->kind == parent->kind && record->world_identity == parent->world_identity &&
                record->lightmap_index == parent->lightmap_index && record->source_variant_revision &&
                record->source_variant_revision <= parent->material.revision && !material->remapped &&
                qa_q3_image_upload_options_valid(&record->source_variant_upload, error);
        }
        if (ok) {
            ok = !registrations[material->registration]; registrations[material->registration] = true;
            if (material->registration == 0)
                ok = ok && library->ordered[i]->kind == QA_MATERIAL_DEFAULT && !strcmp(material->name, "*default");
            if (material->registration == 1)
                ok = ok && library->ordered[i]->kind == QA_MATERIAL_STENCIL_SHADOW && !strcmp(material->name, "<stencil shadow>");
        }
        if (ok && i && library->ordered[i - 1]->material.sort > material->sort) ok = false;
    }
    free(registrations);
    if (ok) ok = remap_records_valid(library, error);
    for (const qa_material_generated *g = library->generated; ok && g; g = g->next) {
        uint64_t index;
        ok = canonical(g->name, error) && g->image && material_index(library, g->picture, &index) &&
            (!g->picture || !strcmp(g->picture->name, g->name));
    }
    return ok;
}
static bool script_catalog(qa_source_save_io *io, qa_material_library *state, const qa_material_library *qualified)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    for (unsigned bucket = 0; bucket < QA_MATERIAL_BUCKETS; ++bucket) {
        size_t count = 0;
        if (!reading) for (const qa_material_script *s = state->scripts[bucket]; s; s = s->next) ++count;
        if (!qa_source_save_count(io, &count, reading ? io->input.size / 10 : SIZE_MAX)) return false;
        const qa_material_script *expected = reading ? (qualified ? qualified->scripts[bucket] : NULL) : state->scripts[bucket];
        qa_material_script **tail = &state->scripts[bucket];
        for (size_t i = 0; i < count; ++i) {
            if ((!reading || qualified) && !expected) return false;
            qa_material_script *s = reading ? calloc(1, sizeof(*s)) : (qa_material_script *)expected;
            if (!s) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader definition");
            if (reading) { *tail = s; tail = &s->next; }
            char *name = reading ? NULL : s->name;
            size_t size = reading ? 0 : s->size;
            uint64_t source_index = 0;
            size_t source_offset = reading ? 0 : s->source_offset;
            size_t name_offset = reading ? 0 : s->name_offset, name_size = reading ? 0 : s->name_size;
            if (!qa_material_saved_text(io, &name)) { if (reading) s->name = name; return false; }
            if (reading) s->name = name;
            if (!name || !*name || !canonical(name, io->error) || qa_material_hash(name) != bucket ||
                (!reading && !catalog_source_index(state, s->source, &source_index)) ||
                !qa_source_save_u64(io, &source_index) || source_index > SIZE_MAX ||
                !qa_source_save_count(io, &source_offset, SIZE_MAX) ||
                !qa_source_save_count(io, &name_offset, SIZE_MAX) ||
                !qa_source_save_count(io, &name_size, 1025) || !name_size ||
                !qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
            qa_material_catalog_source *content = catalog_source_at(state, (size_t)source_index);
            if (!content || source_offset > content->bytes.size || size > content->bytes.size - source_offset ||
                name_offset > source_offset || name_size > source_offset - name_offset) return false;
            const uint8_t *name_bytes = content->bytes.data + name_offset;
            size_t token_size = name_size;
            if (name_bytes[0] == '"') {
                if (token_size < 2 || name_bytes[token_size - 1] != '"') return false;
                ++name_bytes; token_size -= 2;
            }
            if (!token_size || token_size >= 1024 || memchr(name_bytes, 0, token_size)) return false;
            char original[1024];
            memcpy(original, name_bytes, token_size); original[token_size] = 0;
            char *normalized = qa_material_name(original, io->error);
            bool matches = normalized && !strcmp(normalized, name);
            free(normalized);
            if (!matches) return false;
            if (reading) {
                s->text = size ? malloc(size) : NULL; s->size = size;
                s->source = content; s->source_offset = source_offset;
                s->name_offset = name_offset; s->name_size = name_size;
                if (size && !s->text) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader source bytes");
            }
            if (!qa_source_save_bytes(io, s->text, size)) return false;
            if (size && memcmp(s->text, content->bytes.data + source_offset, size)) return false;
            if (reading && qualified) {
                uint64_t expected_source = 0;
                if (!catalog_source_index(qualified, expected->source, &expected_source) || source_index != expected_source ||
                    source_offset != expected->source_offset || name_offset != expected->name_offset || name_size != expected->name_size ||
                    strcmp(name, expected->name) || size != expected->size ||
                    (size && memcmp(s->text, expected->text, size))) return false;
            }
            if (reading) for (const qa_material_script *prior = state->scripts[bucket]; prior != s; prior = prior->next)
                if (!strcmp(prior->name, s->name)) return false;
            if (expected) expected = expected->next;
        }
        if (expected) return false;
    }
    return true;
}
static bool builtin_image(const qa_scene_image *image, const char *name, uint32_t width, uint32_t height)
{
    return image && image->identity && image->revision && image->name && !strcmp(image->name, name) &&
        image->kind == QA_SCENE_RGBA8 && image->wrap == QA_SCENE_CLAMP && image->filter == QA_SCENE_LINEAR &&
        image->level_count == 1 && image->levels && image->logical_width == width && image->logical_height == height &&
        (image->source_q3 || (image->levels[0].width == width && image->levels[0].height == height)) &&
        image->levels[0].bytes == (size_t)image->levels[0].width * image->levels[0].height * 4 &&
        image->levels[0].pixels && !image->animation_count;
}
static bool builtin_fields(qa_source_save_io *io, qa_material_library *library,
    const qa_material_library *qualified, const qa_material_library_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    const qa_scene_resources *owners[] = {library->resources};
    size_t owner_index = 0;
    const qa_scene_image *fog = library->fog_image, *dlight = library->dlight_image;
    bool ok = qa_material_saved_image(io, refs, &fog);
    if (reading) library->fog_image = (qa_scene_image *)fog;
    if (!ok || !builtin_image(fog, "*fog", 256, 32) ||
        !qa_scene_image_owner_index(owners, 1, fog, &owner_index)) return false;
    ok = qa_material_saved_image(io, refs, &dlight);
    if (reading) library->dlight_image = (qa_scene_image *)dlight;
    return ok && builtin_image(dlight, "*dlight", 16, 16) &&
        qa_scene_image_owner_index(owners, 1, dlight, &owner_index) &&
        (!reading || !qualified || (fog == qualified->fog_image && dlight == qualified->dlight_image));
}
bool qa_material_library_catalog_checkpoint(const qa_material_library *source,
    const qa_material_library_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!source || !source->catalog_ready || !refs || !out)
        return fail(error, QA_ERROR_ARGUMENT, "material catalog checkpoint requires its actual installed owners");
    if (!qa_material_library_capture_begin(source, error)) return false;
    qa_source_save_io io = {0};
    qa_material_library state = *source;
    uint8_t magic[4] = {'Q', 'A', 'M', 'C'};
    uint32_t version = 2;
    bool video = source->video_required;
    bool ok = qa_source_save_writer(&io, NULL, error) && qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        qa_source_save_u32(&io, &version) && qa_source_save_bool(&io, &video) &&
        builtin_fields(&io, &state, NULL, refs) && catalog_sources(&io, &state, refs, version) &&
        script_catalog(&io, &state, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_material_library_capture_end(source);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid retained material source catalog");
    return ok;
}
bool qa_material_library_catalog_restore(qa_scene_resources *resources, qa_bytes bytes,
    const qa_material_library_checkpoint_refs *refs, qa_material_library **out, qa_error *error)
{
    if (!resources || !refs || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "material catalog restore requires a qualified resource owner and empty output");
    qa_material_library *library = qa_material_library_create_detached(resources, error);
    if (!library) return false;
    qa_source_save_io io = {0};
    uint8_t magic[4] = {0}; uint32_t version = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        !memcmp(magic, "QAMC", sizeof(magic)) && qa_source_save_u32(&io, &version) && version >= 1 && version <= 2 &&
        qa_source_save_bool(&io, &library->video_required) && builtin_fields(&io, library, NULL, refs) &&
        catalog_sources(&io, library, refs, version) && script_catalog(&io, library, NULL) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) { library->catalog_ready = true; *out = library; }
    else {
        qa_material_library_destroy(library);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "saved material catalog differs from retained content");
    }
    return ok;
}
static bool library_header(qa_source_save_io *io, qa_material_library *library, const qa_material_library *qualified,
    const qa_material_library_checkpoint_refs *refs, uint32_t *schema)
{
    uint8_t magic[4] = {'Q', 'A', 'M', 'L'}; uint32_t version = 10;
    bool reading = io->direction == QA_SOURCE_SAVE_READ, video = library->video_required;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QAML", 4) ||
        !qa_source_save_u32(io, &version) || (version < 3 || version > 10) || !qa_source_save_bool(io, &video) ||
        (reading && video != qualified->video_required)) return false;
    *schema = version;
    if (reading) library->video_required = video;
    if (version >= 6) {
        if (!qa_source_save_bool(io, &library->source_profile)) return false;
    } else if (reading) library->source_profile = false;
    qa_material_profile *p = &library->profile;
    if (!qa_source_save_bool(io, &p->detail_textures) || !qa_source_save_bool(io, &p->vertex_lighting) ||
        !qa_source_save_bool(io, &p->ui_fullscreen) || !qa_source_save_bool(io, &p->permedia2) ||
        !qa_source_save_bool(io, &p->multitexture) || !qa_source_save_bool(io, &p->texture_env_add) ||
        !qa_source_save_bool(io, &p->ignore_fast_path) ||
        !qa_source_save_vec3(io, &library->sun_light) || !qa_vec_finite(library->sun_light) ||
        !qa_source_save_vec3(io, &library->sun_direction) || !qa_vec_finite(library->sun_direction) ||
        !qa_source_save_f32(io, &library->sky_height) || !isfinite(library->sky_height) ||
        !qa_source_save_bool(io, &library->has_sun)) return false;
    return builtin_fields(io, library, qualified, refs) && script_catalog(io, library, qualified);
}
static bool videos(qa_source_save_io *io, const qa_material_library *library, qa_material_record *record,
    const qa_material_library_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; size_t count = 0;
    if (!reading) for (const qa_material_video_receipt *video = record->videos; video; video = video->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 35 : SIZE_MAX)) return false;
    const qa_material_script *script = library->scripts[qa_material_hash(record->material.name)];
    while (script && strcmp(script->name, record->material.name)) script = script->next;
    if (count && !script) return false;
    qa_material_video_receipt *video = record->videos, **tail = &record->videos;
    size_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            video = calloc(1, sizeof(*video));
            if (!video) return fail(io->error, QA_ERROR_MEMORY, "importing shader video callback receipt");
            *tail = video; tail = &video->next;
        }
        if (!qa_source_save_count(io, &video->command, script->size) ||
            !qa_source_save_count(io, &video->command_end, script->size) ||
            !qa_source_save_count(io, &video->offset, script->size) ||
            !qa_source_save_count(io, &video->end, script->size) ||
            !qa_material_saved_text(io, &video->source) || !video->source ||
            !qa_material_saved_image(io, refs, &video->image) ||
            video->command_end <= video->command || video->offset < video->command_end ||
            video->end <= video->offset ||
            (i && video->offset <= previous)) return false;
        size_t keyword_begin = video->command, keyword_end = video->command_end;
        if (script->text[keyword_begin] == '"') {
            if (keyword_end - keyword_begin < 2 || script->text[keyword_end - 1] != '"') return false;
            ++keyword_begin; --keyword_end;
        }
        if (keyword_end - keyword_begin != 8) return false;
        static const char keyword[] = "videomap";
        for (size_t j = 0; j < 8; ++j) {
            unsigned char c = script->text[keyword_begin + j];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (unsigned char)keyword[j]) return false;
        }
        size_t begin = video->offset, end = video->end;
        if (script->text[begin] == '"') {
            if (end - begin < 2 || script->text[end - 1] != '"') return false;
            ++begin; --end;
        }
        if (strlen(video->source) != end - begin || memcmp(video->source, script->text + begin, end - begin)) return false;
        previous = video->offset;
        if (!reading) video = video->next;
    }
    return true;
}
static bool record_order(qa_source_save_io *io, qa_material_library *library)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t *rows = reading && library->count ? malloc(library->count * sizeof(*rows)) : NULL;
    bool *seen = reading && library->count ? calloc(library->count, sizeof(*seen)) : NULL;
    size_t sizes[QA_MATERIAL_BUCKETS] = {0}, used = 0;
    if (reading && library->count && (!rows || !seen)) {
        free(rows); free(seen); return fail(io->error, QA_ERROR_MEMORY, "allocating material bucket order");
    }
    bool ok = true;
    for (unsigned bucket = 0; ok && bucket < QA_MATERIAL_BUCKETS; ++bucket) {
        size_t count = 0;
        if (!reading) for (const qa_material_record *record = library->records[bucket]; record; record = record->next) ++count;
        ok = qa_source_save_count(io, &count, library->count - used); sizes[bucket] = count;
        const qa_material_record *record = reading ? NULL : library->records[bucket];
        for (size_t i = 0; ok && i < count; ++i) {
            uint64_t row = 0;
            if (!reading) ok = record && material_index(library, &record->material, &row);
            ok = ok && qa_source_save_u64(io, &row) && row < library->count;
            if (ok && reading) {
                ok = !seen[row] && qa_material_hash(library->ordered[row]->material.name) == bucket;
                if (ok) seen[row] = true, rows[used + i] = (size_t)row;
            }
            if (!reading && record) record = record->next;
        }
        used += count;
    }
    ok = ok && used == library->count;
    if (ok && reading) {
        used = 0;
        for (unsigned bucket = 0; bucket < QA_MATERIAL_BUCKETS; ++bucket) {
            qa_material_record **tail = &library->records[bucket];
            for (size_t i = 0; i < sizes[bucket]; ++i) {
                *tail = library->ordered[rows[used++]]; tail = &(*tail)->next;
            }
            *tail = NULL;
        }
    }
    free(rows); free(seen); return ok;
}
static bool remaps(qa_source_save_io *io, qa_material_library *library)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; size_t count = 0;
    if (!reading) for (const qa_material_remap_record *entry = library->remaps; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 15 : SIZE_MAX)) return false;
    qa_material_remap_record **tail = &library->remaps;
    const qa_material_remap_record *source = library->remaps;
    for (size_t i = 0; i < count; ++i) {
        qa_material_remap_record copy = source ? *source : (qa_material_remap_record){0};
        qa_material_remap_record *entry = reading ? calloc(1, sizeof(*entry)) : &copy;
        if (!entry) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader remap");
        if (reading) { *tail = entry; tail = &entry->next; }
        if (!qa_material_saved_text(io, &entry->original) || !entry->original || !*entry->original ||
            !qa_material_saved_text(io, &entry->replacement) || !entry->replacement || !*entry->replacement ||
            !qa_source_save_f32(io, &entry->time_offset) || !isfinite(entry->time_offset)) return false;
        if (!reading) source = source->next;
    }
    return true;
}
static bool generated(qa_source_save_io *io, qa_material_library *library,
    const qa_material_library_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; size_t count = 0;
    if (!reading) for (const qa_material_generated *entry = library->generated; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 19 : SIZE_MAX)) return false;
    qa_material_generated **tail = &library->generated; const qa_material_generated *source = library->generated;
    for (size_t i = 0; i < count; ++i) {
        qa_material_generated copy = source ? *source : (qa_material_generated){0};
        qa_material_generated *entry = reading ? calloc(1, sizeof(*entry)) : &copy;
        if (!entry) return fail(io->error, QA_ERROR_MEMORY, "allocating retained generated material");
        if (reading) { *tail = entry; tail = &entry->next; }
        uint64_t picture = UINT64_MAX;
        if (!qa_material_saved_text(io, &entry->name) || !entry->name || !*entry->name ||
            !qa_material_saved_image(io, refs, &entry->image) || !entry->image ||
            (!reading && !material_index(library, entry->picture, &picture)) ||
            !qa_source_save_u64(io, &picture) || (picture != UINT64_MAX && picture >= library->count)) return false;
        if (reading) entry->picture = picture == UINT64_MAX ? NULL : &library->ordered[picture]->material;
        for (const qa_material_generated *prior = library->generated; reading && prior != entry; prior = prior->next)
            if (!strcmp(prior->name, entry->name)) return false;
        if (!reading) source = source->next;
    }
    return true;
}
bool qa_material_library_checkpoint(const qa_material_library *source, const qa_material_library_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!source || !source->catalog_ready || !source->order || !refs || !out ||
        source->video_required != (source->video_start != NULL))
        return fail(error, QA_ERROR_ARGUMENT, "material library checkpoint requires installed qualified owners");
    if (!qa_material_library_capture_begin(source, error)) return false;
    if (!library_valid(source, error)) {
        qa_material_library_capture_end(source);
        return error && error->code != QA_OK ? false : fail(error, QA_ERROR_FORMAT, "invalid material library ownership graph");
    }
    qa_source_save_io io = {0}; qa_material_library library = *source;
    uint32_t schema = 0;
    size_t count = source->count, capacity = source->capacity;
    bool ok = qa_source_save_writer(&io, NULL, error) && library_header(&io, &library, source, refs, &schema) &&
        qa_source_save_count(&io, &count, QA_MATERIAL_MAX_REGISTERED) &&
        qa_source_save_count(&io, &capacity, QA_MATERIAL_MAX_REGISTERED);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_material_record copy = *source->ordered[i]; uint64_t remapped = UINT64_MAX, parent = UINT64_MAX;
        ok = copy.material.sorted_index == i && copy.material.registration < count &&
            copy.material.fog_image == source->fog_image && copy.material.dlight_image == source->dlight_image &&
            qa_material_order_has_record(source->order, &source->ordered[i]->material) &&
            qa_material_saved_record(&io, refs, schema, &copy) && videos(&io, source, &copy, refs) &&
            material_index(source, copy.material.remapped, &remapped) &&
            qa_source_save_u64(&io, &remapped);
        if (ok) ok = material_index(source, copy.source_variant_parent ? &copy.source_variant_parent->material : NULL, &parent) &&
            qa_source_save_u64(&io, &parent) &&
            (parent == UINT64_MAX || (qa_source_save_u64(&io, &copy.source_variant_revision) &&
                qa_q3_image_upload_options_precision_codec(&io, &copy.source_variant_upload)));
    }
    if (ok) ok = record_order(&io, &library) && remaps(&io, &library) && generated(&io, &library, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_material_library_capture_end(source);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid retained material library");
    return ok;
}
bool qa_material_library_restore(const qa_material_library *qualified, qa_bytes bytes,
    const qa_material_library_checkpoint_refs *refs, qa_material_library **out, qa_error *error)
{
    if (!qualified || !qualified->catalog_ready || !qualified->resources || !refs || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "material library restore requires qualified detached content");
    if (!qa_material_library_capture_begin(qualified, error)) return false;
    qa_material_library *library = qa_material_library_create_detached(qualified->resources, error);
    if (!library) { qa_material_library_capture_end(qualified); return false; }
    qa_source_save_io io = {0}; size_t count = 0, capacity = 0; uint32_t schema = 0;
    bool ok = catalog_copy_sources(library, qualified, error) &&
        qa_source_save_reader(&io, NULL, bytes, error) && library_header(&io, library, qualified, refs, &schema) &&
        qa_source_save_count(&io, &count, QA_MATERIAL_MAX_REGISTERED) &&
        qa_source_save_count(&io, &capacity, QA_MATERIAL_MAX_REGISTERED) &&
        registration_capacity(count, capacity) && count <= bytes.size / 100;
    uint64_t *remapped = ok && count ? malloc(count * sizeof(*remapped)) : NULL;
    uint64_t *parents = ok && count ? malloc(count * sizeof(*parents)) : NULL;
    if (ok && count) {
        library->ordered = calloc(capacity, sizeof(*library->ordered)); library->capacity = capacity;
        if (!library->ordered || !remapped || !parents) ok = fail(error, QA_ERROR_MEMORY, "allocating restored material registration table");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        qa_material_record *record = calloc(1, sizeof(*record));
        if (!record) { ok = fail(error, QA_ERROR_MEMORY, "allocating restored material record"); break; }
        ok = qa_material_saved_record(&io, refs, schema, record) &&
            (schema < 4 || videos(&io, library, record, refs)) &&
            record->material.registration < count && record->material.sorted_index == i &&
            qa_source_save_u64(&io, &remapped[i]) && (remapped[i] == UINT64_MAX || remapped[i] < count);
        parents[i] = UINT64_MAX;
        if (ok && schema >= 8) ok = qa_source_save_u64(&io, &parents[i]) &&
            (parents[i] == UINT64_MAX || (parents[i] < count && parents[i] != i &&
                qa_source_save_u64(&io, &record->source_variant_revision) && record->source_variant_revision &&
                (schema >= 10 ? qa_q3_image_upload_options_precision_codec(&io, &record->source_variant_upload) :
                    qa_q3_image_upload_options_codec(&io, &record->source_variant_upload))));
        if (ok && i && library->ordered[i - 1]->material.sort > record->material.sort) ok = false;
        if (ok) {
            record->material.identity = qa_scene_identity();
            ok = record->material.identity != 0;
        }
        if (!ok) { record_free(record); break; }
        record->material.fog_image = library->fog_image; record->material.dlight_image = library->dlight_image;
        record->material.library = library;
        if (schema < 8 && library->source_profile && record->material.lightmap_index >= 0) {
            bool lightmap = false;
            for (size_t stage = 0; stage < record->material.stage_count; ++stage)
                if (record->material.stages[stage].is_lightmap) lightmap = true;
            if (!lightmap) record->material.lightmap_index = -1;
        }
        unsigned bucket = qa_material_hash(record->material.name);
        record->next = library->records[bucket]; library->records[bucket] = record;
        library->ordered[library->count++] = record;
    }
    if (ok) {
        for (size_t i = 0; i < count; ++i) {
            library->ordered[i]->material.remapped = remapped[i] == UINT64_MAX ? NULL : &library->ordered[remapped[i]]->material;
            library->ordered[i]->source_variant_parent = parents[i] == UINT64_MAX ? NULL : library->ordered[parents[i]];
        }
        ok = record_order(&io, library) && remaps(&io, library) && generated(&io, library, refs) &&
            qa_source_save_finish(&io, NULL) && library_valid(library, error);
    }
    free(remapped); free(parents); qa_source_save_dispose(&io);
    qa_material_library_capture_end(qualified);
    if (ok) { library->catalog_ready = true; *out = library; }
    else {
        qa_material_library_destroy(library);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "saved material library differs from qualified content");
    }
    return ok;
}
bool qa_material_library_bind_order(qa_material_library *library, qa_material_order *order, qa_error *error)
{
    if (!qa_material_library_idle(library) || !library->catalog_ready || library->order || !order)
        return fail(error, QA_ERROR_ARGUMENT, "material order binding requires a detached restored library");
    for (size_t i = 0; i < library->count; ++i)
        if (!qa_material_order_has_record(order, &library->ordered[i]->material))
            return fail(error, QA_ERROR_ARGUMENT, "restored library record belongs to another renderer order");
    if (!qa_material_order_retain(order, error)) return false;
    library->order = order; return true;
}
bool qa_material_library_restore_into_empty(qa_material_library *target,
    const qa_material_library *qualified, qa_bytes bytes,
    const qa_material_library_checkpoint_refs *refs, qa_error *error)
{
    if (!qa_material_library_empty_detached(target) || !qualified || target == qualified ||
        target->resources != qualified->resources)
        return fail(error, QA_ERROR_ARGUMENT, "material adoption requires the genuine empty stable owner and its qualified catalog");
    target->mutating = true;
    qa_material_library *decoded = NULL;
    bool ok = qa_material_library_restore(qualified, bytes, refs, &decoded, error);
    target->mutating = false;
    if (!ok) return false;
    size_t references = target->references;
    qa_scene_resources_destroy(target->resources);
    *target = *decoded;
    target->references = references;
    for (size_t i = 0; i < target->count; ++i) target->ordered[i]->material.library = target;
    free(decoded);
    return true;
}
bool qa_material_library_bind_video_start(qa_material_library *library,
    qa_material_video_start_fn start, void *context, qa_error *error)
{
    if (!qa_material_library_idle(library) || !library->catalog_ready || !library->order ||
        library->video_start || library->video_required != (start != NULL))
        return fail(error, QA_ERROR_ARGUMENT, "material video binding differs from the restored source owner");
    library->video_start = start;
    library->video_context = start ? context : NULL;
    return true;
}

#include "library_save_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
const qa_material *qa_material_library_record_at(const qa_material_library *library, size_t index)
{ return library && index < library->count ? &library->ordered[index]->material : NULL; }
size_t qa_material_library_record_count(const qa_material_library *library)
{ return library ? library->count : 0; }
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
bool qa_material_library_order_ready(const qa_material_library *library)
{
    if (!library || !library->order) return false;
    for (size_t i = 0; i < library->count; ++i)
        if (!qa_material_order_has_record(library->order, &library->ordered[i]->material)) return false;
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
    free(record->palette); free(record->translation); free(record);
}
static bool canonical(const char *name, qa_error *error)
{
    char *normalized = qa_material_name(name, error);
    bool ok = normalized && !strcmp(normalized, name);
    free(normalized); return ok;
}
static bool same_profile(const qa_material_profile *a, const qa_material_profile *b)
{
    return a->detail_textures == b->detail_textures && a->vertex_lighting == b->vertex_lighting &&
        a->ui_fullscreen == b->ui_fullscreen && a->permedia2 == b->permedia2 && a->multitexture == b->multitexture &&
        a->texture_env_add == b->texture_env_add && a->ignore_fast_path == b->ignore_fast_path;
}
typedef struct remap_node { const qa_material_remap_record *record; size_t edge; uint8_t mark; } remap_node;
static int remap_compare(const void *a, const void *b)
{ return strcmp(((const remap_node *)a)->record->original, ((const remap_node *)b)->record->original); }
static bool remap_graph(const qa_material_library *library, qa_error *error)
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
        size_t low = 0, high = count;
        const char *target = nodes[i].record->replacement;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            if (strcmp(nodes[middle].record->original, target) < 0) low = middle + 1; else high = middle;
        }
        nodes[i].edge = low < count && !strcmp(nodes[low].record->original, target) ? low : SIZE_MAX;
    }
    for (i = 0; ok && i < count; ++i) {
        size_t at = i;
        while (at != SIZE_MAX && !nodes[at].mark) { nodes[at].mark = 1; at = nodes[at].edge; }
        if (at != SIZE_MAX && nodes[at].mark == 1) { ok = false; break; }
        at = i;
        while (at != SIZE_MAX && nodes[at].mark == 1) { nodes[at].mark = 2; at = nodes[at].edge; }
    }
    free(nodes); return ok;
}
static bool library_valid(const qa_material_library *library, qa_error *error)
{
    size_t count = library->count;
    uint8_t *marks = count ? calloc(count, 1) : NULL;
    bool *registrations = count ? calloc(count, sizeof(*registrations)) : NULL;
    size_t *edges = count ? malloc(count * sizeof(*edges)) : NULL;
    if (count && (!marks || !edges || !registrations)) { free(marks); free(edges); free(registrations); return fail(error, QA_ERROR_MEMORY, "qualifying material remap graph"); }
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_material *material = &library->ordered[i]->material; uint64_t edge = UINT64_MAX;
        ok = material->sorted_index == i && material->registration < count &&
            canonical(material->name, error) && same_profile(&material->profile, &library->profile) &&
            material_index(library, material->remapped, &edge);
        if (ok) {
            ok = !registrations[material->registration]; registrations[material->registration] = true;
        }
        if (ok && i && library->ordered[i - 1]->material.sort > material->sort) ok = false;
        edges[i] = edge == UINT64_MAX ? SIZE_MAX : (size_t)edge;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        size_t at = i;
        while (at != SIZE_MAX && !marks[at]) { marks[at] = 1; at = edges[at]; }
        if (at != SIZE_MAX && marks[at] == 1) { ok = false; break; }
        at = i;
        while (at != SIZE_MAX && marks[at] == 1) { marks[at] = 2; at = edges[at]; }
    }
    free(marks); free(edges); free(registrations);
    if (ok) ok = remap_graph(library, error);
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
        const qa_material_script *expected = reading ? qualified->scripts[bucket] : state->scripts[bucket];
        qa_material_script **tail = &state->scripts[bucket];
        for (size_t i = 0; i < count; ++i) {
            if (!expected) return false;
            qa_material_script *s = reading ? calloc(1, sizeof(*s)) : (qa_material_script *)expected;
            if (!s) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader definition");
            if (reading) { *tail = s; tail = &s->next; }
            char *name = reading ? NULL : s->name;
            size_t size = reading ? 0 : s->size;
            if (!qa_material_saved_text(io, &name)) { if (reading) s->name = name; return false; }
            if (reading) s->name = name;
            if (!name || !*name || qa_material_hash(name) != bucket ||
                !qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
            if (reading) {
                s->text = size ? malloc(size) : NULL; s->size = size;
                if (size && !s->text) return fail(io->error, QA_ERROR_MEMORY, "allocating retained shader source bytes");
            }
            if (!qa_source_save_bytes(io, s->text, size)) return false;
            if (reading && (strcmp(name, expected->name) || size != expected->size || memcmp(s->text, expected->text, size))) return false;
            expected = expected->next;
        }
        if (expected) return false;
    }
    return true;
}
static bool same_procedural(const qa_scene_image *a, const qa_scene_image *b)
{
    if (!a || !b || strcmp(a->name, b->name) || a->kind != b->kind || a->wrap != b->wrap || a->filter != b->filter ||
        a->revision != b->revision || a->level_count != b->level_count || a->logical_width != b->logical_width ||
        a->logical_height != b->logical_height || a->animation_count || b->animation_count ||
        a->border.x != b->border.x || a->border.y != b->border.y || a->border.z != b->border.z || a->border.w != b->border.w) return false;
    for (size_t i = 0; i < a->level_count; ++i)
        if (a->levels[i].width != b->levels[i].width || a->levels[i].height != b->levels[i].height ||
            a->levels[i].bytes != b->levels[i].bytes || memcmp(a->levels[i].pixels, b->levels[i].pixels, a->levels[i].bytes)) return false;
    return true;
}
static bool library_header(qa_source_save_io *io, qa_material_library *library, const qa_material_library *qualified,
    const qa_material_library_checkpoint_refs *refs)
{
    uint8_t magic[4] = {'Q', 'A', 'M', 'L'}; uint32_t version = 1;
    bool reading = io->direction == QA_SOURCE_SAVE_READ, video = library->video_start != NULL;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QAML", 4) ||
        !qa_source_save_u32(io, &version) || version != 1 || !qa_source_save_bool(io, &video) ||
        (reading && video != (qualified->video_start != NULL))) return false;
    if (reading) library->video_start = qualified->video_start, library->video_context = qualified->video_context;
    qa_material_profile *p = &library->profile;
    if (!qa_source_save_bool(io, &p->detail_textures) || !qa_source_save_bool(io, &p->vertex_lighting) ||
        !qa_source_save_bool(io, &p->ui_fullscreen) || !qa_source_save_bool(io, &p->permedia2) ||
        !qa_source_save_bool(io, &p->multitexture) || !qa_source_save_bool(io, &p->texture_env_add) ||
        !qa_source_save_bool(io, &p->ignore_fast_path) ||
        !qa_source_save_vec3(io, &library->sun_light) || !qa_vec_finite(library->sun_light) ||
        !qa_source_save_vec3(io, &library->sun_direction) || !qa_vec_finite(library->sun_direction) ||
        !qa_source_save_f32(io, &library->sky_height) || !isfinite(library->sky_height) ||
        !qa_source_save_bool(io, &library->has_sun)) return false;
    const qa_scene_image *fog = library->fog_image, *dlight = library->dlight_image;
    bool ok = qa_material_saved_image(io, refs, &fog);
    if (reading) library->fog_image = (qa_scene_image *)fog;
    if (!ok || !fog) return false;
    ok = qa_material_saved_image(io, refs, &dlight);
    if (reading) library->dlight_image = (qa_scene_image *)dlight;
    return ok && dlight && (!reading || (same_procedural(fog, qualified->fog_image) && same_procedural(dlight, qualified->dlight_image))) &&
        script_catalog(io, library, qualified);
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
    if (!source || !source->order || !refs || !out)
        return fail(error, QA_ERROR_ARGUMENT, "material library checkpoint requires installed qualified owners");
    if (!library_valid(source, error))
        return error && error->code != QA_OK ? false : fail(error, QA_ERROR_FORMAT, "invalid material library ownership graph");
    qa_source_save_io io = {0}; qa_material_library library = *source; size_t count = source->count;
    bool ok = qa_source_save_writer(&io, NULL, error) && library_header(&io, &library, source, refs) &&
        qa_source_save_count(&io, &count, QA_MATERIAL_MAX_REGISTERED);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_material_record copy = *source->ordered[i]; uint64_t remapped = UINT64_MAX;
        ok = copy.material.sorted_index == i && copy.material.registration < count &&
            copy.material.fog_image == source->fog_image && copy.material.dlight_image == source->dlight_image &&
            qa_material_order_has_record(source->order, &source->ordered[i]->material) &&
            qa_material_saved_record(&io, refs, &copy) && material_index(source, copy.material.remapped, &remapped) &&
            qa_source_save_u64(&io, &remapped);
    }
    if (ok) ok = record_order(&io, &library) && remaps(&io, &library) && generated(&io, &library, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid retained material library");
    return ok;
}
bool qa_material_library_restore(const qa_material_library *qualified, qa_bytes bytes,
    const qa_material_library_checkpoint_refs *refs, qa_material_library **out, qa_error *error)
{
    if (!qualified || !qualified->resources || !refs || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "material library restore requires qualified detached content");
    qa_material_library *library = calloc(1, sizeof(*library));
    if (!library) return fail(error, QA_ERROR_MEMORY, "allocating detached material library");
    library->resources = qualified->resources;
    qa_source_save_io io = {0}; size_t count = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && library_header(&io, library, qualified, refs) &&
        qa_source_save_count(&io, &count, QA_MATERIAL_MAX_REGISTERED) && count <= bytes.size / 100;
    uint64_t *remapped = ok && count ? malloc(count * sizeof(*remapped)) : NULL;
    if (ok && count) {
        library->ordered = calloc(count, sizeof(*library->ordered)); library->capacity = count;
        if (!library->ordered || !remapped) ok = fail(error, QA_ERROR_MEMORY, "allocating restored material registration table");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        qa_material_record *record = calloc(1, sizeof(*record));
        if (!record) { ok = fail(error, QA_ERROR_MEMORY, "allocating restored material record"); break; }
        ok = qa_material_saved_record(&io, refs, record) && record->material.registration < count && record->material.sorted_index == i &&
            qa_source_save_u64(&io, &remapped[i]) && (remapped[i] == UINT64_MAX || remapped[i] < count);
        if (ok && i && library->ordered[i - 1]->material.sort > record->material.sort) ok = false;
        if (ok) {
            record->material.identity = qa_scene_identity();
            ok = record->material.identity != 0;
        }
        if (!ok) { record_free(record); break; }
        record->material.fog_image = library->fog_image; record->material.dlight_image = library->dlight_image;
        unsigned bucket = qa_material_hash(record->material.name);
        record->next = library->records[bucket]; library->records[bucket] = record;
        library->ordered[library->count++] = record;
    }
    if (ok) {
        for (size_t i = 0; i < count; ++i)
            library->ordered[i]->material.remapped = remapped[i] == UINT64_MAX ? NULL : &library->ordered[remapped[i]]->material;
        ok = record_order(&io, library) && remaps(&io, library) && generated(&io, library, refs) &&
            qa_source_save_finish(&io, NULL) && library_valid(library, error);
    }
    free(remapped); qa_source_save_dispose(&io);
    if (ok) *out = library;
    else {
        qa_material_library_destroy(library);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "saved material library differs from qualified content");
    }
    return ok;
}
bool qa_material_library_bind_order(qa_material_library *library, qa_material_order *order, qa_error *error)
{
    if (!library || library->order || !order)
        return fail(error, QA_ERROR_ARGUMENT, "material order binding requires a detached restored library");
    for (size_t i = 0; i < library->count; ++i)
        if (!qa_material_order_has_record(order, &library->ordered[i]->material))
            return fail(error, QA_ERROR_ARGUMENT, "restored library record belongs to another renderer order");
    if (!qa_material_order_retain(order, error)) return false;
    library->order = order; return true;
}

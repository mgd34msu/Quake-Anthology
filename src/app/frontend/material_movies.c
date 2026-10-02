#include "material_movies_private.h"
#include <ctype.h>
#include <math.h>

static bool member(const frontend_material_movies *owner)
{
    if (!owner || !owner->linked || !owner->source.frontend) return false;
    for (const frontend_material_movies *row = owner->source.frontend->material_movie_owners; row; row = row->next)
        if (row == owner) return true;
    return false;
}
bool frontend_material_movie_link(frontend_material_movies *owner, qa_error *error)
{
    qa_frontend *frontend = owner ? owner->source.frontend : NULL;
    if (!frontend || owner->linked || owner->next)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie roster requires its exact unlinked provider owner");
    for (const frontend_material_movies *row = frontend->material_movie_owners; row; row = row->next)
        if (row == owner || row->source.frontend != frontend || row->source.materials == owner->source.materials)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie roster repeats or leaves its actual provider library");
    owner->next = frontend->material_movie_owners; frontend->material_movie_owners = owner;
    owner->linked = true; return true;
}
bool frontend_material_movie_unlink(frontend_material_movies *owner, qa_error *error)
{
    if (!member(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie retirement lost its exact frontend roster row");
    frontend_material_movies **row = &owner->source.frontend->material_movie_owners;
    while (*row != owner) row = &(*row)->next;
    *row = owner->next; owner->next = NULL; owner->linked = false; return true;
}
bool frontend_material_movie_source_valid(const frontend_material_movie_source *source)
{
    return source && source->frontend && source->frontend->application &&
        !source->frontend->options.dedicated && source->files && source->images &&
        source->materials && source->media && source->current &&
        qa_scene_resources_files(source->images) == source->files &&
        qa_material_library_resource_owner(source->materials) == source->images &&
        qa_media_library_resource_owner(source->media) == source->images &&
        source->current(source->context, source);
}
bool frontend_material_movies_current(const frontend_material_movies *owner)
{
    return member(owner) && frontend_material_movie_source_valid(&owner->source) &&
        qa_material_movies_resource_owner(owner->registry) == owner->source.images &&
        qa_material_movies_count(owner->registry) == frontend_material_movie_live_count(owner) &&
        qa_material_library_video_start_is(owner->source.materials,
            frontend_material_movies_start, owner);
}
bool frontend_material_movies_idle(const frontend_material_movies *owner)
{ return owner && !owner->busy && !owner->pending; }
bool frontend_material_movies_roster_count(const qa_frontend *frontend, size_t *out, qa_error *error)
{
    if (!frontend || !frontend->application || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie inventory requires its actual frontend");
    size_t count = 0;
    for (const frontend_material_movies *owner = frontend->material_movie_owners; owner; owner = owner->next) {
        if (owner->source.frontend != frontend || !frontend_material_movies_current(owner) || count == SIZE_MAX)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie inventory lost its retained provider tuple");
        ++count;
    }
    *out = count; return true;
}
bool frontend_material_movies_roster_at(const qa_frontend *frontend, size_t index,
    frontend_material_movies **out, qa_error *error)
{
    size_t count = 0;
    if (!out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie inventory requires an empty owner result");
    if (!frontend_material_movies_roster_count(frontend, &count, error)) return false;
    if (index >= count) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Shader movie provider ordinal is absent");
    frontend_material_movies *owner = frontend->material_movie_owners;
    while (index--) owner = owner->next;
    *out = owner; return true;
}
bool frontend_material_movies_source_read(const frontend_material_movies *owner,
    frontend_material_movie_source *out, qa_error *error)
{
    if (!out || !frontend_material_movies_current(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie source read lost its actual provider tuple");
    *out = owner->source; return true;
}
size_t frontend_material_movie_live_count(const frontend_material_movies *owner)
{
    size_t count = 0;
    if (owner) for (size_t i = 0; i < owner->count; ++i)
        if (owner->rows[i] && !owner->rows[i]->failed) ++count;
    return count;
}
const qa_scene_image *frontend_material_movie_cached(const frontend_material_movie_row *row, qa_error *error)
{
    if (row->failed) { if (error) *error = row->failure; return NULL; }
    return row->initial;
}
bool frontend_material_movies_library_owner(const qa_material_library *library,
    frontend_material_movies **out, qa_error *error)
{
    const qa_scene_image *(*start)(void *, const char *, qa_error *) = NULL;
    void *context = NULL;
    if (!out || *out || !qa_material_library_video_start_read(library, &start, &context) ||
        start != frontend_material_movies_start || !context)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Material library has no genuine frontend movie producer");
    frontend_material_movies *owner = context;
    if (owner->source.materials != library || !frontend_material_movies_current(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Material movie callback leaves its actual provider library");
    *out = owner; return true;
}
double frontend_material_movie_clock(void *context)
{
    const frontend_material_movies *owner = context;
    return (double)owner->source.frontend->wall_time_ns / 1000000.0;
}
char *frontend_material_movie_path(const char *name, qa_error *error)
{
    if (!name) { frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie has no source name"); return NULL; }
    const char *prefix = strchr(name, '/') || strchr(name, '\\') ? "" : "video/";
    size_t head = strlen(prefix), tail = strlen(name);
    if (tail > SIZE_MAX - head - 1) {
        frontend_fail(error, QA_ERROR_MEMORY, "Shader movie path exceeds its allocation"); return NULL;
    }
    char *path = malloc(head + tail + 1);
    if (!path) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining provider shader movie path"); return NULL; }
    memcpy(path, prefix, head); memcpy(path + head, name, tail + 1);
    return path;
}
bool frontend_material_movie_path_valid(const char *path)
{
    if (!path || (!strchr(path, '/') && !strchr(path, '\\'))) return false;
    const char *extension = strrchr(path, '.');
    char kind[4] = {0};
    if (extension && strlen(extension) == 4)
        for (size_t i = 0; i < 4; ++i) kind[i] = (char)tolower((unsigned char)extension[i]);
    return !memcmp(kind, ".roq", 4) || !memcmp(kind, ".cin", 4) || !memcmp(kind, ".ogv", 4);
}
qa_cinematic_options frontend_material_movie_options(frontend_material_movies *owner, uint64_t target)
{
    return (qa_cinematic_options){.clock = {owner, frontend_material_movie_clock},
        .target = {.kind = QA_CINEMATIC_MATERIAL, .id.material = target},
        .loop = true, .silent = true, .gain = 1};
}
bool frontend_material_movie_rows_reserve(frontend_material_movie_row ***rows, size_t *capacity,
    size_t needed, qa_error *error)
{
    if (needed <= *capacity) return true;
    if (needed > SIZE_MAX / sizeof(**rows))
        return frontend_fail(error, QA_ERROR_MEMORY, "Shader movie cache exceeds its allocation");
    size_t next = *capacity ? *capacity : 8;
    while (next < needed) {
        if (next > SIZE_MAX / sizeof(**rows) / 2) { next = needed; break; }
        next *= 2;
    }
    frontend_material_movie_row **grown = realloc(*rows, next * sizeof(*grown));
    if (!grown) return frontend_fail(error, QA_ERROR_MEMORY, "Growing real provider shader movie cache");
    *rows = grown; *capacity = next; return true;
}
void frontend_material_movie_row_free(frontend_material_movie_row *row)
{
    if (!row) return;
    qa_scene_frame_destroy(&row->publication);
    qa_scene_image_release(row->initial); qa_cinematic_asset_release(row->asset);
    free(row->path); free(row);
}
bool frontend_material_movie_row_create(frontend_material_movies *owner, qa_media_library *media,
    qa_material_movies *registry, const char *path, uint64_t target,
    frontend_material_movie_row **out, qa_error *error)
{
    frontend_material_movie_row *row = calloc(1, sizeof(*row));
    if (!row) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual shader movie owner");
    size_t size = strlen(path) + 1;
    row->path = malloc(size); row->target = target;
    qa_scene_frame_init(&row->publication, owner->source.frontend->frame.owner);
    bool ok = row->path != NULL;
    if (ok) memcpy(row->path, path, size);
    else { frontend_material_movie_row_free(row); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining shader movie cache key"); }
    qa_error failure = {0};
    if (ok) ok = qa_media_library_load_shader(media, owner->source.files, path, &row->asset, &failure);
    if (ok) {
        qa_cinematic_source source = qa_cinematic_asset_source(row->asset); source.name = row->path;
        qa_cinematic_options options = frontend_material_movie_options(owner, target);
        ok = qa_cinematic_create(&source, &options, NULL, &row->playback, &failure);
    }
    if (ok) ok = qa_material_movies_add(registry, row->playback, &row->publication, &row->initial, &failure);
    if (ok) qa_scene_image_retain(row->initial);
    else {
        qa_cinematic_destroy(row->playback); row->playback = NULL;
        qa_cinematic_asset_release(row->asset); row->asset = NULL; row->initial = NULL;
        qa_scene_frame_destroy(&row->publication);
        qa_scene_frame_init(&row->publication, owner->source.frontend->frame.owner);
        row->target = 0; row->failed = true;
        if (failure.code == QA_OK) frontend_fail(&failure, QA_ERROR_FORMAT, "Shader movie registration failed without an image");
        row->failure = failure; if (error) *error = failure;
    }
    *out = row; return ok;
}
bool frontend_material_movies_create(const frontend_material_movie_source *source,
    frontend_material_movies **out, qa_error *error)
{
    const qa_scene_image *(*start)(void *, const char *, qa_error *) = NULL;
    void *context = NULL;
    if (!out || *out || !frontend_material_movie_source_valid(source) ||
        source->frontend->source_restoring || !qa_material_library_idle(source->materials) ||
        !qa_media_library_idle(source->media) ||
        !qa_material_library_video_start_read(source->materials, &start, &context) || start || context)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movies require their real unregistered provider");
    for (size_t i = 0; i < qa_material_library_record_count(source->materials); ++i) {
        qa_material_library_record_view record;
        if (!qa_material_library_record_read(source->materials, i, &record) ||
            (record.kind != QA_MATERIAL_DEFAULT && record.kind != QA_MATERIAL_STENCIL_SHADOW))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie producer must precede actual content registration");
    }
    frontend_material_movies *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining provider shader movie cache");
    owner->source = *source; owner->next_target = 1;
    owner->registry = qa_material_movies_create(source->images, error);
    if (!owner->registry) { free(owner); return false; }
    if (!qa_material_movies_prepare(owner->registry, &source->frontend->frame, error)) {
        qa_material_movies_destroy(owner->registry); free(owner); return false;
    }
    qa_material_library_set_video_start(source->materials, frontend_material_movies_start, owner);
    if (!frontend_material_movie_link(owner, error) || !frontend_material_movies_current(owner)) {
        if (qa_material_library_video_start_is(source->materials, frontend_material_movies_start, owner))
            qa_material_library_set_video_start(source->materials, NULL, NULL);
        if (owner->linked) frontend_material_movie_unlink(owner, NULL);
        qa_material_movies_destroy(owner->registry); free(owner);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Provider did not retain its genuine shader movie producer");
    }
    *out = owner; return true;
}
const qa_scene_image *frontend_material_movies_start(void *context, const char *name, qa_error *error)
{
    frontend_material_movies *owner = context;
    if (!owner || owner->busy || owner->pending || owner->restore_pending ||
        !frontend_material_movies_current(owner)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie registration lost its real provider"); return NULL;
    }
    char *path = frontend_material_movie_path(name, error);
    if (!path) return NULL;
    for (size_t i = 0; i < owner->count; ++i)
        if (!strcmp(owner->rows[i]->path, path)) {
            free(path); return frontend_material_movie_cached(owner->rows[i], error);
        }
    if (!owner->next_target || owner->count == SIZE_MAX ||
        !frontend_material_movie_rows_reserve(&owner->rows, &owner->capacity, owner->count + 1, error)) {
        free(path); if (error && error->code == QA_OK)
            frontend_fail(error, QA_ERROR_MEMORY, "Shader movie target allocation exhausted");
        return NULL;
    }
    owner->busy = true;
    frontend_material_movie_row *row = NULL;
    bool ok = frontend_material_movie_row_create(owner, owner->source.media, owner->registry,
        path, owner->next_target, &row, error);
    free(path);
    if (row) owner->rows[owner->count++] = row;
    if (ok) ++owner->next_target;
    owner->busy = false;
    return ok ? row->initial : NULL;
}
bool frontend_material_movies_destroy(frontend_material_movies **out, qa_error *error)
{
    if (!out || !*out) return true;
    frontend_material_movies *owner = *out;
    if (!frontend_material_movies_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie teardown retains an active preparation");
    if (!qa_material_library_idle(owner->source.materials) || !qa_material_movies_idle(owner->registry) ||
        !qa_media_library_idle(owner->source.media))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie teardown retains a held resource owner");
    if (!frontend_material_movie_unlink(owner, error)) return false;
    if (qa_material_library_video_start_is(owner->source.materials, frontend_material_movies_start, owner))
        qa_material_library_set_video_start(owner->source.materials, NULL, NULL);
    owner->busy = true;
    qa_material_movies_destroy(owner->registry);
    for (size_t i = 0; i < owner->count; ++i) frontend_material_movie_row_free(owner->rows[i]);
    free(owner->rows); free(owner); *out = NULL; return true;
}
size_t frontend_material_movies_count(const frontend_material_movies *owner)
{ return owner ? owner->count : 0; }
bool frontend_material_movies_read(const frontend_material_movies *owner, size_t index,
    frontend_material_movie_view *out)
{
    if (!owner || !out || index >= owner->count) return false;
    const frontend_material_movie_row *row = owner->rows[index];
    *out = (frontend_material_movie_view){row->path, row->asset, row->initial,
        row->playback, row->failed ? NULL : &row->publication, row->target,
        row->failed, row->failed ? &row->failure : NULL}; return true;
}
bool frontend_material_movies_frame(frontend_material_movies *owner, qa_scene_frame *frame, qa_error *error)
{
    if (!frontend_material_movies_idle(owner) || owner->restore_pending ||
        !frontend_material_movies_current(owner) || frame != &owner->source.frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie frame lost its real frontend owner");
    return qa_material_movies_prepare(owner->registry, frame, error);
}
const qa_scene_image *frontend_material_movies_resolve(void *context, uint64_t initial,
    double seconds, qa_error *error)
{
    frontend_material_movies *owner = context;
    if (!frontend_material_movies_idle(owner) || owner->restore_pending ||
        !frontend_material_movies_current(owner)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Reached shader movie lost its actual provider"); return NULL;
    }
    owner->busy = true;
    const qa_scene_image *image = NULL;
    if (qa_material_movies_prepare(owner->registry, &owner->source.frontend->frame, error))
        image = qa_material_movies_resolve(owner->registry, initial, seconds, error);
    owner->busy = false; return image;
}
const qa_scene_image *frontend_material_movies_frontend_resolve(void *context, uint64_t initial,
    double seconds, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || !frontend->application || frontend->options.dedicated || !initial) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Reached shader movie requires its actual submitted frontend"); return NULL;
    }
    frontend_material_movies *selected = NULL;
    for (frontend_material_movies *owner = frontend->material_movie_owners; owner; owner = owner->next) {
        if (!owner->linked || owner->source.frontend != frontend) {
            frontend_fail(error, QA_ERROR_ARGUMENT, "Reached shader movie roster leaves its actual frontend"); return NULL;
        }
        for (size_t i = 0; i < owner->count; ++i) {
            const frontend_material_movie_row *row = owner->rows[i];
            if (row->failed || row->initial->identity != initial) continue;
            if (selected) {
                frontend_fail(error, QA_ERROR_FORMAT, "Reached shader initial image repeats a physical playback owner"); return NULL;
            }
            selected = owner;
        }
    }
    if (!selected) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Reached shader image has no retained frontend movie owner"); return NULL;
    }
    return frontend_material_movies_resolve(selected, initial, seconds, error);
}

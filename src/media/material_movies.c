#include "material_movies_internal.h"
#include "material_image.h"
#include "qa/cinematic_restore.h"
#include "qa/material_movies_save.h"

#include <stdlib.h>
#include <string.h>

bool qa_material_movies_idle(const qa_material_movies *movies)
{ return movies && !movies->busy && !movies->pending && !movies->stage_sealed; }
size_t qa_material_movies_count(const qa_material_movies *movies)
{ return movies ? movies->count : 0; }
qa_scene_resources *qa_material_movies_resource_owner(const qa_material_movies *movies)
{ return movies ? movies->resources : NULL; }
bool qa_material_movies_read(const qa_material_movies *movies, size_t index, qa_material_movie_record *out)
{
    if (!movies || !out || index >= movies->count) return false;
    const material_movie *row = movies->movies + index;
    *out = (qa_material_movie_record){row->initial, row->playback, row->enabled}; return true;
}
static bool movie_valid(const qa_cinematic *movie, bool cold)
{
    return movie && !movie->busy && !movie->faulted && movie->restore_pending == cold &&
        movie->options.target.kind == QA_CINEMATIC_MATERIAL && movie->options.target.id.material;
}
static bool returned(const qa_material_movies *owner, bool cold, qa_error *error)
{
    if (!qa_material_movies_idle(owner) || owner->restore_pending != cold)
        return cinematic_fail(error, cold ? "Material movie publication requires its unadopted registry" :
            "Material movie completion requires its adopted registry");
    for (size_t i = 0; i < owner->count; ++i)
        if (!movie_valid(owner->movies[i].playback, cold))
            return cinematic_fail(error, cold ? "Cold material movie lost its qualified decoder" :
                "Completed material movie lost its qualified decoder");
    return true;
}
bool qa_material_movies_completed_ready(const qa_material_movies *owner, qa_error *error)
{ return returned(owner, false, error); }
qa_material_movies *qa_material_movies_create(qa_scene_resources *resources, qa_error *error) {
    if (!resources) {
        cinematic_fail(error, "Material movies require shared scene resources");
        return NULL;
    }
    qa_material_movies *movies = calloc(1, sizeof(*movies));
    if (!movies) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material movie registry");
        return NULL;
    }
    movies->resources = resources;
    return movies;
}
void qa_material_movies_destroy(qa_material_movies *movies) {
    if (!movies || movies->busy || movies->pending || movies->stage_sealed)
        return;
    movies->busy = true;
    for (size_t i = 0; i < movies->count; ++i)
        if (movies->restore_pending) qa_cinematic_restore_discard(movies->movies[i].playback);
        else qa_cinematic_destroy(movies->movies[i].playback);
    free(movies->movies);
    free(movies);
}
static material_movie *find(qa_material_movies *movies, uint64_t initial) {
    for (size_t i = 0; i < movies->count; ++i)
        if (movies->movies[i].initial == initial) return &movies->movies[i];
    return NULL;
}
bool qa_material_movies_add(qa_material_movies *movies, qa_cinematic *playback,
                            qa_scene_frame *frame, const qa_scene_image **out, qa_error *error) {
    if (!movies || !playback || !frame || !out || movies->busy || movies->pending ||
        movies->restore_pending || movies->stage_sealed ||
        playback->options.target.kind != QA_CINEMATIC_MATERIAL || !playback->options.target.id.material)
        return cinematic_fail(error, "Invalid material movie registration");
    for (size_t i = 0; i < movies->count; ++i)
        if (movies->movies[i].playback == playback ||
            movies->movies[i].playback->options.target.id.material ==
                playback->options.target.id.material)
            return cinematic_fail(error, "Material movie already has an owner");
    if (movies->count == movies->capacity) {
        size_t capacity = movies->capacity ? movies->capacity * 2 : 8;
        if (capacity < movies->capacity || capacity > SIZE_MAX / sizeof(*movies->movies))
            return cinematic_fail(error, "Material movie registry size overflow");
        material_movie *entries = realloc(movies->movies, capacity * sizeof(*entries));
        if (!entries) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating material movie bindings");
            return false;
        }
        movies->movies = entries;
        movies->capacity = capacity;
    }
    const qa_scene_image *image;
    if (!cinematic_material_initial(playback, movies->resources, frame, &image, error)) {
        return false;
    }
    if (!image || !image->identity || find(movies, image->identity))
        return cinematic_fail(error, "Material initial image already has a playback owner");
    movies->movies[movies->count] =
        (material_movie){.initial = image->identity, .playback = playback, .enabled = true};
    ++movies->count;
    *out = image;
    return true;
}
bool qa_material_movies_remove(qa_material_movies *movies, uint64_t initial, qa_error *error) {
    if (!movies || movies->busy || movies->pending || movies->restore_pending || movies->stage_sealed)
        return cinematic_fail(error, "Material movies are active");
    material_movie *entry = find(movies, initial);
    if (!entry)
        return cinematic_fail(error, "Unknown material movie");
    size_t index = (size_t)(entry - movies->movies);
    qa_cinematic *playback = entry->playback;
    memmove(entry, entry + 1, (movies->count - index - 1) * sizeof(*entry));
    --movies->count;
    movies->busy = true;
    qa_cinematic_destroy(playback);
    movies->busy = false;
    return true;
}
bool qa_material_movies_enable(qa_material_movies *movies, uint64_t initial, bool enabled,
                               qa_error *error) {
    if (!movies || movies->busy || movies->pending || movies->restore_pending || movies->stage_sealed)
        return cinematic_fail(error, "Material movies are active");
    material_movie *entry = find(movies, initial);
    if (!entry)
        return cinematic_fail(error, "Unknown material movie");
    movies->busy = true;
    bool ok = qa_cinematic_pause(entry->playback, !enabled, error);
    movies->busy = false;
    if (!ok)
        return false;
    entry->enabled = enabled;
    return true;
}
bool qa_material_movies_prepare(qa_material_movies *movies, qa_scene_frame *frame,
                                qa_error *error) {
    if (!movies || !frame || movies->busy || movies->pending || movies->restore_pending || movies->stage_sealed)
        return cinematic_fail(error, "Invalid material movie frame");
    movies->prepared = frame;
    movies->sequence = frame->sequence;
    return true;
}
const qa_scene_image *qa_material_movies_resolve(void *context, uint64_t initial, double seconds,
                                                 qa_error *error) {
    (void)seconds;
    qa_material_movies *movies = context;
    if (!movies || movies->busy || movies->pending || movies->restore_pending || movies->stage_sealed ||
        !movies->prepared || movies->prepared->sequence != movies->sequence) {
        cinematic_fail(error, "Material movie registry is unavailable");
        return NULL;
    }
    material_movie *entry = find(movies, initial);
    if (!entry) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Unknown material movie image");
        return NULL;
    }
    movies->busy = true;
    qa_media_tick tick;
    const qa_scene_image *image = NULL;
    bool ok = !entry->enabled || qa_cinematic_tick(entry->playback, &tick, error);
    if (ok) ok = qa_cinematic_image(entry->playback, movies->resources, movies->prepared, &image, error);
    movies->busy = false; return ok ? image : NULL;
}

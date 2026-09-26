#include "cinematic_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct material_movie {
    uint64_t initial;
    qa_cinematic *playback;
    bool enabled;
} material_movie;
struct qa_material_movies {
    qa_scene_resources *resources;
    material_movie *movies;
    size_t count, capacity;
    const qa_scene_frame *prepared;
    uint64_t sequence;
    bool busy;
};
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
    if (!movies || movies->busy)
        return;
    movies->busy = true;
    for (size_t i = 0; i < movies->count; ++i)
        qa_cinematic_destroy(movies->movies[i].playback);
    free(movies->movies);
    free(movies);
}
static size_t position(const qa_material_movies *movies, uint64_t initial) {
    size_t first = 0, end = movies->count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (movies->movies[middle].initial < initial)
            first = middle + 1;
        else
            end = middle;
    }
    return first;
}
static material_movie *find(qa_material_movies *movies, uint64_t initial) {
    size_t index = position(movies, initial);
    return index < movies->count && movies->movies[index].initial == initial
               ? &movies->movies[index]
               : NULL;
}
bool qa_material_movies_add(qa_material_movies *movies, qa_cinematic *playback,
                            qa_scene_frame *frame, const qa_scene_image **out, qa_error *error) {
    if (!movies || !playback || !frame || !out || movies->busy ||
        playback->options.target.kind != QA_CINEMATIC_MATERIAL)
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
    if (!qa_cinematic_image(playback, movies->resources, frame, &image, error)) {
        return false;
    }
    size_t at = position(movies, image->identity);
    memmove(movies->movies + at + 1, movies->movies + at,
            (movies->count - at) * sizeof(*movies->movies));
    movies->movies[at] =
        (material_movie){.initial = image->identity, .playback = playback, .enabled = true};
    ++movies->count;
    movies->prepared = NULL;
    *out = image;
    return true;
}
bool qa_material_movies_remove(qa_material_movies *movies, uint64_t initial, qa_error *error) {
    if (!movies || movies->busy)
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
    movies->prepared = NULL;
    return true;
}
bool qa_material_movies_enable(qa_material_movies *movies, uint64_t initial, bool enabled,
                               qa_error *error) {
    if (!movies || movies->busy)
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
    movies->prepared = NULL;
    return true;
}
bool qa_material_movies_prepare(qa_material_movies *movies, qa_scene_frame *frame,
                                qa_error *error) {
    if (!movies || !frame || movies->busy)
        return cinematic_fail(error, "Invalid material movie frame");
    if (movies->prepared == frame && movies->sequence == frame->sequence)
        return true;
    movies->busy = true;
    bool ok = true;
    for (size_t i = 0; ok && i < movies->count; ++i) {
        material_movie *entry = &movies->movies[i];
        qa_media_tick tick;
        if (entry->enabled)
            ok = qa_cinematic_tick(entry->playback, &tick, error);
        const qa_scene_image *image;
        if (ok)
            ok = qa_cinematic_image(entry->playback, movies->resources, frame, &image, error);
    }
    movies->busy = false;
    if (!ok)
        return false;
    movies->prepared = frame;
    movies->sequence = frame->sequence;
    return true;
}
const qa_scene_image *qa_material_movies_resolve(void *context, uint64_t initial, double seconds,
                                                 qa_error *error) {
    (void)seconds;
    qa_material_movies *movies = context;
    if (!movies || movies->busy) {
        cinematic_fail(error, "Material movie registry is unavailable");
        return NULL;
    }
    material_movie *entry = find(movies, initial);
    if (!entry) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Unknown material movie image");
        return NULL;
    }
    return entry->playback->image;
}

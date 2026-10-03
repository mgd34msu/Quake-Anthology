#include "material_movies_internal.h"
#include "qa/material_movies_save.h"
#include "qa/cinematic_restore.h"
#include "qa/source_save.h"
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
static bool header(qa_source_save_io *io, size_t *count, size_t *capacity)
{
    uint8_t magic[4] = {'Q','M','M','R'};
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QMMR", 4) &&
        qa_source_save_count(io, count, SIZE_MAX / sizeof(material_movie)) &&
        qa_source_save_count(io, capacity, SIZE_MAX / sizeof(material_movie)) && *count <= *capacity;
}
static bool movie_valid(const qa_cinematic *movie, bool cold)
{
    return movie && !movie->busy && !movie->faulted && movie->restore_pending == cold &&
        movie->options.target.kind == QA_CINEMATIC_MATERIAL && movie->options.target.id.material;
}
bool qa_material_movies_checkpoint(const qa_material_movies *movies,
    const qa_material_movies_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !qa_material_movies_idle(movies) || movies->restore_pending ||
        !refs || !refs->movie_encode || !refs->initial_encode || !refs->frame_encode)
        return cinematic_fail(error, "Material movie capture requires the actual returned owners");
    qa_material_movies *owner = (qa_material_movies *)movies; owner->busy = true;
    qa_source_save_io io = {0}; size_t count = owner->count, capacity = owner->capacity;
    bool bound = owner->prepared != NULL; uint64_t frame = 0, sequence = owner->sequence;
    bool ok = (!bound || refs->frame_encode(refs->context, owner->prepared, &frame, error)) &&
        (bound ? frame != 0 : sequence == 0) && qa_source_save_writer(&io, NULL, error) &&
        header(&io, &count, &capacity) && qa_source_save_bool(&io, &bound) &&
        qa_source_save_u64(&io, &frame) && qa_source_save_u64(&io, &sequence);
    for (size_t i = 0; ok && i < count; ++i) {
        const material_movie *row = owner->movies + i;
        uint64_t movie = 0, initial = 0; bool enabled = row->enabled;
        ok = row->initial && movie_valid(row->playback, false) &&
            refs->movie_encode(refs->context, row->playback, &movie, error) && movie &&
            refs->initial_encode(refs->context, row->initial, &initial, error) && initial &&
            qa_source_save_u64(&io, &movie) && qa_source_save_u64(&io, &initial) && qa_source_save_bool(&io, &enabled);
        for (size_t j = 0; ok && j < i; ++j)
            if (owner->movies[j].initial == row->initial || owner->movies[j].playback == row->playback ||
                owner->movies[j].playback->options.target.id.material == row->playback->options.target.id.material) ok = false;
    }
    ok = ok && qa_source_save_finish(&io, out); qa_source_save_dispose(&io); owner->busy = false;
    if (!ok && error && error->code == QA_OK) cinematic_fail(error, "Material movie registry is inconsistent");
    return ok;
}
bool qa_material_movies_restore(qa_scene_resources *resources, qa_bytes bytes,
    const qa_material_movies_checkpoint_refs *refs, qa_material_movies **out, qa_error *error)
{
    if (!resources || !out || *out || !refs || !refs->movie_decode || !refs->initial_decode || !refs->frame_decode)
        return cinematic_fail(error, "Material movie import requires genuine qualified references");
    qa_material_movies *owner = qa_material_movies_create(resources, error);
    if (!owner) return false;
    qa_source_save_io io = {0}; bool bound = false; uint64_t frame = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && header(&io, &owner->count, &owner->capacity) &&
        qa_source_save_bool(&io, &bound) && qa_source_save_u64(&io, &frame) &&
        qa_source_save_u64(&io, &owner->sequence) && (bound ? frame != 0 : frame == 0 && owner->sequence == 0);
    if (ok && owner->count > (io.input.size - io.offset) / 17) ok = false;
    if (ok && owner->capacity) {
        owner->movies = calloc(owner->capacity, sizeof(*owner->movies));
        if (!owner->movies) { qa_error_set(error, QA_ERROR_MEMORY, io.offset, "Restoring material movie extent"); ok = false; }
    }
    if (ok && bound) ok = refs->frame_decode(refs->context, frame, &owner->prepared, error) && owner->prepared;
    for (size_t i = 0; ok && i < owner->count; ++i) {
        material_movie *row = owner->movies + i; uint64_t movie = 0, initial = 0;
        ok = qa_source_save_u64(&io, &movie) && movie && qa_source_save_u64(&io, &initial) && initial &&
            qa_source_save_bool(&io, &row->enabled) && refs->movie_decode(refs->context, movie, &row->playback, error) &&
            movie_valid(row->playback, true) && refs->initial_decode(refs->context, initial, &row->initial, error) &&
            row->initial;
        for (size_t j = 0; ok && j < i; ++j)
            if (owner->movies[j].initial == row->initial || owner->movies[j].playback == row->playback ||
                owner->movies[j].playback->options.target.id.material == row->playback->options.target.id.material) ok = false;
    }
    ok = ok && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (!ok) {
        /* Resolver movies remain owned by their candidate rows on failure. */
        free(owner->movies); free(owner);
        if (error && error->code == QA_OK) cinematic_fail(error, "Invalid saved material movie registry");
        return false;
    }
    owner->restore_pending = true; *out = owner; return true;
}
bool qa_material_movies_publish_ready(const qa_material_movies *owner, qa_error *error)
{
    if (!qa_material_movies_idle(owner) || !owner->restore_pending)
        return cinematic_fail(error, "Material movie publication requires its unadopted registry");
    for (size_t i = 0; i < owner->count; ++i)
        if (!movie_valid(owner->movies[i].playback, true))
            return cinematic_fail(error, "Cold material movie lost its qualified decoder");
    return true;
}
void qa_material_movies_publish(qa_material_movies *owner)
{
    if (!qa_material_movies_idle(owner) || !owner->restore_pending) return;
    for (size_t i = 0; i < owner->count; ++i) qa_cinematic_restore_commit(owner->movies[i].playback);
    owner->restore_pending = false;
}

#include "material_movies_internal.h"
#include "qa/material_movies_prepare.h"
#include "qa/material_movies_save.h"
#include <stdlib.h>

struct qa_material_movies_stage {
    qa_material_movies *owner, *destination;
    qa_scene_resource_policy *bank;
    material_movie *original, *merged;
    size_t count, added, capacity;
    qa_scene_frame *frame;
    uint64_t sequence;
    bool sealed, published;
};
static bool held(const qa_material_movies_stage *stage)
{
    return stage && stage->owner && stage->destination && stage->owner->pending == stage &&
        !stage->owner->busy && !stage->destination->busy &&
        stage->owner->resources == qa_scene_resource_policy_source(stage->bank) &&
        stage->owner->count == stage->count + (stage->published ? stage->added : 0) &&
        stage->owner->prepared == stage->frame && stage->owner->sequence == stage->sequence &&
        stage->destination->stage_sealed == stage->sealed;
}
bool qa_material_movies_stage_prepare(qa_material_movies *owner, qa_scene_resource_policy *bank,
    qa_material_movies_stage **out, qa_error *error)
{
    qa_scene_resources *images = qa_scene_resource_policy_destination(bank);
    if (!out || *out || !images || !qa_material_movies_idle(owner) ||
        owner->resources != qa_scene_resource_policy_source(bank))
        return cinematic_fail(error, "Movie staging requires its true provider resource bank");
    qa_material_movies_stage *stage = calloc(1, sizeof(*stage));
    if (!stage) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing material movie registry"); return false; }
    stage->destination = qa_material_movies_create(images, error);
    if (!stage->destination) { free(stage); return false; }
    stage->owner = owner; stage->bank = bank; stage->original = owner->movies;
    stage->count = owner->count; stage->capacity = owner->capacity;
    stage->frame = owner->prepared; stage->sequence = owner->sequence;
    owner->pending = stage; *out = stage; return true;
}
qa_material_movies *qa_material_movies_stage_destination(const qa_material_movies_stage *stage)
{ return held(stage) && !stage->sealed ? stage->destination : NULL; }
bool qa_material_movies_stage_ready(qa_material_movies_stage *stage, qa_error *error)
{
    if (!held(stage) || stage->published)
        return cinematic_fail(error, "Prepared movies lost their real registry owner");
    if (stage->sealed) return true;
    size_t added = stage->destination->count;
    if (added > SIZE_MAX / sizeof(material_movie) - stage->count)
        return cinematic_fail(error, "Prepared movie roster exceeds its allocation");
    size_t count = stage->count + added, capacity = stage->capacity;
    if (!capacity && count) capacity = 8;
    while (capacity < count) {
        if (capacity > SIZE_MAX / sizeof(material_movie) / 2) { capacity = count; break; }
        capacity *= 2;
    }
    material_movie *merged = capacity ? malloc(capacity * sizeof(*merged)) : NULL;
    if (capacity && !merged) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Sealing movie registry extent"); return false; }
    for (size_t i = 0; i < count; ++i) {
        merged[i] = i < stage->count ? stage->owner->movies[i] : stage->destination->movies[i - stage->count];
        const qa_cinematic *movie = merged[i].playback;
        if (!merged[i].initial ||
            !movie || movie->busy || movie->faulted ||
            movie->options.target.kind != QA_CINEMATIC_MATERIAL || !movie->options.target.id.material) {
            free(merged); return cinematic_fail(error, "Prepared movie roster has an invalid physical owner");
        }
        for (size_t j = 0; j < i; ++j)
            if (merged[j].initial == merged[i].initial || merged[j].playback == movie ||
                merged[j].playback->options.target.id.material == movie->options.target.id.material) {
                free(merged); return cinematic_fail(error, "Prepared movie roster repeats a true target");
            }
    }
    stage->merged = merged; stage->capacity = capacity; stage->added = added;
    stage->sealed = true; stage->destination->stage_sealed = true; return true;
}
bool qa_material_movies_stage_ready_is(const qa_material_movies_stage *stage)
{
    return held(stage) && stage->sealed && !stage->published &&
        stage->destination->count == stage->added && qa_scene_resource_policy_ready_is(stage->bank);
}
void qa_material_movies_stage_publish(qa_material_movies_stage *stage)
{
    if (!held(stage) || !stage->sealed || stage->published) return;
    stage->owner->movies = stage->merged; stage->merged = NULL;
    stage->owner->count += stage->added; stage->owner->capacity = stage->capacity;
    stage->destination->count = 0; stage->published = true;
}
static void dispose(qa_material_movies_stage *stage)
{
    stage->destination->stage_sealed = false; qa_material_movies_destroy(stage->destination);
    free(stage->published ? stage->original : stage->merged);
    stage->owner->pending = NULL; free(stage);
}
bool qa_material_movies_stage_finish(qa_material_movies_stage **out, qa_error *error)
{
    if (!out || !held(*out) || !(*out)->published)
        return cinematic_fail(error, "Movie retirement requires its published source owner");
    dispose(*out); *out = NULL; return true;
}
bool qa_material_movies_stage_abort(qa_material_movies_stage **out, qa_error *error)
{
    if (!out || !held(*out) || (*out)->published)
        return cinematic_fail(error, "Movie abort requires its unpublished source owner");
    dispose(*out); *out = NULL; return true;
}

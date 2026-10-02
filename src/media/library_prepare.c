#include "library_internal.h"
#include "qa/media_library_prepare.h"
#include "qa/scene_resource_save.h"
#include <stdlib.h>

struct qa_media_library_stage {
    qa_media_library *owner, *destination;
    qa_scene_resource_policy *bank;
    qa_cinematic_asset *original, *added, *tail;
    bool sealed, published;
};
bool qa_media_library_idle(const qa_media_library *owner)
{ return owner && !owner->pending && !owner->stage_sealed; }
static bool held(const qa_media_library_stage *stage)
{
    return stage && stage->owner && stage->destination && stage->owner->pending == stage &&
        stage->owner->resources == qa_scene_resource_policy_source(stage->bank) &&
        stage->owner->assets == (stage->published && stage->added ? stage->added : stage->original) &&
        stage->destination->parent == stage->owner &&
        stage->destination->stage_sealed == stage->sealed;
}
bool qa_media_library_stage_prepare(qa_media_library *owner, qa_scene_resource_policy *bank,
    qa_media_library_stage **out, qa_error *error)
{
    qa_scene_resources *images = qa_scene_resource_policy_destination(bank);
    if (!out || *out || !images || !qa_media_library_idle(owner) || owner->parent ||
        owner->resources != qa_scene_resource_policy_source(bank))
        return cinematic_fail(error, "Media staging requires its actual retained provider image bank");
    qa_media_library_stage *stage = calloc(1, sizeof(*stage));
    if (!stage) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining staged media cache"); return false; }
    stage->destination = qa_media_library_create(images, error);
    if (!stage->destination) { free(stage); return false; }
    stage->owner = owner; stage->bank = bank; stage->original = owner->assets;
    stage->destination->parent = owner; owner->pending = stage;
    *out = stage; return true;
}
qa_media_library *qa_media_library_stage_destination(const qa_media_library_stage *stage)
{ return held(stage) && !stage->sealed ? stage->destination : NULL; }
bool qa_media_library_stage_ready(qa_media_library_stage *stage, qa_error *error)
{
    if (!held(stage) || stage->published)
        return cinematic_fail(error, "Staged media cache lost its source owner");
    if (stage->sealed) return true;
    qa_cinematic_asset *tail = NULL;
    for (qa_cinematic_asset *asset = stage->destination->assets; asset; asset = asset->next) {
        if (!asset->references || asset->source.asset != asset || !asset->source_record ||
            !qa_sha256_equal(&asset->digest, qa_resource_digest(asset->source_record)))
            return cinematic_fail(error, "Prepared media cache lost a genuine decoded resource");
        for (qa_cinematic_asset *old = stage->original; old; old = old->next)
            if (old->source.format == asset->source.format && qa_sha256_equal(&old->digest, &asset->digest))
                return cinematic_fail(error, "Prepared media cache duplicates an actual shared asset");
        for (qa_cinematic_asset *prior = stage->destination->assets; prior != asset; prior = prior->next)
            if (prior->source.format == asset->source.format && qa_sha256_equal(&prior->digest, &asset->digest))
                return cinematic_fail(error, "Prepared media cache repeats a decoded asset");
        tail = asset;
    }
    stage->added = stage->destination->assets; stage->tail = tail;
    stage->sealed = true; stage->destination->stage_sealed = true; return true;
}
bool qa_media_library_stage_ready_is(const qa_media_library_stage *stage)
{
    return held(stage) && stage->sealed && !stage->published &&
        stage->destination->assets == stage->added &&
        qa_scene_resource_policy_ready_is(stage->bank);
}
void qa_media_library_stage_publish(qa_media_library_stage *stage)
{
    if (!held(stage) || !stage->sealed || stage->published) return;
    if (stage->tail) { stage->tail->next = stage->original; stage->owner->assets = stage->added; }
    stage->destination->assets = NULL; stage->published = true;
}
static void dispose(qa_media_library_stage *stage)
{
    stage->destination->parent = NULL; stage->destination->stage_sealed = false;
    qa_media_library_destroy(stage->destination); stage->owner->pending = NULL; free(stage);
}
bool qa_media_library_stage_finish(qa_media_library_stage **out, qa_error *error)
{
    if (!out || !held(*out) || !(*out)->published)
        return cinematic_fail(error, "Media retirement requires its published retained cache");
    dispose(*out); *out = NULL; return true;
}
bool qa_media_library_stage_abort(qa_media_library_stage **out, qa_error *error)
{
    if (!out || !held(*out) || (*out)->published)
        return cinematic_fail(error, "Media abort requires its unpublished retained cache");
    dispose(*out); *out = NULL; return true;
}

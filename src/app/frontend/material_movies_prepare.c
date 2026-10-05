#include "material_movies_private.h"
#include "material_movies_prepare.h"
#include "qa/media_library_prepare.h"
#include "qa/material_movies_prepare.h"

struct frontend_material_movies_policy {
    frontend_material_movies *owner;
    qa_scene_resource_policy *bank;
    qa_scene_material_image_policy *materials;
    qa_material_library *destination;
    qa_media_library_stage *media;
    qa_material_movies_stage *movies;
    qa_q3_cinematic_handles_stage *cinematics;
    frontend_material_movie_cinematic_receipt *cinematic_original, *cinematic_receipts;
    size_t cinematic_original_count, cinematic_count, cinematic_capacity;
    frontend_material_movie_row **original, **rows, **merged;
    size_t count, added, capacity, merged_capacity;
    uint64_t original_target, next_target;
    bool busy, closing, sealed, published;
};
static bool held(const frontend_material_movies_policy *policy)
{
    return policy && policy->owner && policy->owner->pending == policy &&
        !policy->owner->busy && !policy->busy &&
        policy->owner->source.images == qa_scene_resource_policy_source(policy->bank) &&
        policy->owner->count == policy->count + (policy->published ? policy->added : 0) &&
        policy->owner->next_target == (policy->published ? policy->next_target : policy->original_target) &&
        policy->owner->rows == (policy->published ? policy->merged : policy->original) &&
        policy->owner->cinematic_receipts==(policy->published?policy->cinematic_receipts:policy->cinematic_original) &&
        policy->owner->cinematic_count==(policy->published?policy->cinematic_count:policy->cinematic_original_count);
}
bool frontend_material_movies_policy_current(const frontend_material_movies_policy *policy,
    const frontend_material_movies *owner, const qa_scene_resource_policy *bank,
    const qa_scene_material_image_policy *materials)
{
    return held(policy) && policy->owner == owner && policy->bank == bank &&
        policy->materials == materials && frontend_material_movies_current(owner) &&
        (!owner->cinematic_source || qa_q3_cinematic_handles_stage_owner(policy->cinematics)==
            qa_q3_cinematic_source_handles(owner->cinematic_source));
}
static const qa_scene_image *prepared_start(void *context, const char *name, qa_error *error)
{
    frontend_material_movies_policy *policy = context;
    if (!held(policy) || policy->closing || policy->sealed || policy->published ||
        !frontend_material_movies_current(policy->owner)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared shader movie lost its held provider"); return NULL;
    }
    if (policy->owner->cinematic_source) {
        frontend_material_movie_cinematic_receipt receipt={0};
        if (policy->cinematic_count==SIZE_MAX ||
            !frontend_material_movie_cinematic_reserve(&policy->cinematic_receipts,&policy->cinematic_capacity,
                policy->cinematic_count+1,error) ||
            !frontend_material_movie_cinematic_receipt_make(name,&receipt,error)) return NULL;
        policy->busy=true;
        bool ok=qa_q3_cinematic_handles_stage_shader(policy->cinematics,policy->owner->cinematic_source,
            policy->media,name,&receipt.handle,&receipt.image,error);
        policy->busy=false;
        if (!ok) { free(receipt.path); return NULL; }
        qa_scene_image_retain(receipt.image);
        policy->cinematic_receipts[policy->cinematic_count++]=receipt;
        return receipt.image;
    }
    char *path = frontend_material_movie_path(name, error);
    if (!path) return NULL;
    for (size_t i = 0; i < policy->count; ++i)
        if (!strcmp(policy->original[i]->path, path)) {
            free(path); return frontend_material_movie_cached(policy->original[i], error);
        }
    for (size_t i = 0; i < policy->added; ++i)
        if (!strcmp(policy->rows[i]->path, path)) { free(path); return frontend_material_movie_cached(policy->rows[i], error); }
    if (!policy->next_target || policy->added == SIZE_MAX ||
        !frontend_material_movie_rows_reserve(&policy->rows, &policy->capacity, policy->added + 1, error)) {
        free(path); if (error && error->code == QA_OK)
            frontend_fail(error, QA_ERROR_MEMORY, "Prepared shader movie targets are exhausted");
        return NULL;
    }
    policy->busy = true;
    frontend_material_movie_row *row = NULL;
    bool ok = frontend_material_movie_row_create(policy->owner,
        qa_media_library_stage_destination(policy->media),
        qa_material_movies_stage_destination(policy->movies), path, policy->next_target, &row, error);
    free(path);
    if (row) policy->rows[policy->added++] = row;
    if (ok) ++policy->next_target;
    policy->busy = false; return ok ? row->initial : NULL;
}
bool frontend_material_movies_policy_prepare(frontend_material_movies *owner,
    qa_scene_resource_policy *bank, qa_scene_material_image_policy *materials,
    qa_q3_cinematic_handles_stage *cinematics,
    frontend_material_movies_policy **out, qa_error *error)
{
    qa_scene_resources *images = qa_scene_resource_policy_destination(bank);
    qa_material_library *destination = qa_scene_material_image_policy_destination(materials);
    if (!out || *out || !frontend_material_movies_idle(owner) ||
        !frontend_material_movies_current(owner) || !images || !destination ||
        owner->source.images != qa_scene_resource_policy_source(bank) ||
        owner->source.materials != qa_scene_material_image_policy_source(materials) ||
        qa_material_library_resource_owner(destination) != images ||
        (owner->cinematic_source && qa_q3_cinematic_handles_stage_owner(cinematics)!=
            qa_q3_cinematic_source_handles(owner->cinematic_source)) ||
        (!owner->cinematic_source && cinematics))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared shader movies require their real material and bank destinations");
    frontend_material_movies_policy *policy = calloc(1, sizeof(*policy));
    if (!policy) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing provider shader movies");
    policy->owner = owner; policy->bank = bank; policy->materials = materials;
    policy->destination = destination; policy->original = owner->rows; policy->count = owner->count;
    policy->cinematics=cinematics;
    policy->cinematic_original=owner->cinematic_receipts; policy->cinematic_original_count=owner->cinematic_count;
    policy->original_target = policy->next_target = owner->next_target;
    bool ok = qa_media_library_stage_prepare(owner->source.media, bank, &policy->media, error) &&
        qa_material_movies_stage_prepare(owner->registry, bank, &policy->movies, error);
    if (ok && owner->cinematic_source) {
        policy->cinematic_capacity=owner->cinematic_capacity;
        if (policy->cinematic_capacity) {
            policy->cinematic_receipts=calloc(policy->cinematic_capacity,sizeof(*policy->cinematic_receipts));
            if (!policy->cinematic_receipts) ok=frontend_fail(error,QA_ERROR_MEMORY,"Preparing actual numeric shader receipt extent");
        }
        qa_scene_resource_policy *scratch_bank=qa_q3_cinematic_handles_stage_bank(cinematics);
        for (size_t i=0;ok && i<owner->cinematic_count;++i) {
            const frontend_material_movie_cinematic_receipt *old=owner->cinematic_receipts+i;
            frontend_material_movie_cinematic_receipt *row=policy->cinematic_receipts+i;
            ok=frontend_material_movie_cinematic_receipt_make(old->path,row,error);
            if (ok) {
                row->handle=old->handle; ++policy->cinematic_count;
                if (old->image) {
                    qa_scene_image *mapped=NULL;
                    ok=qa_scene_resource_policy_image(scratch_bank,old->image,&mapped,error);
                    if (ok) row->image=mapped;
                }
            }
        }
    }
    if (ok) {
        owner->pending = policy;
        ok = qa_scene_material_image_policy_video_start(materials, prepared_start, policy, error);
    }
    if (!ok) {
        owner->pending = policy;
        if (policy->movies && !qa_material_movies_stage_abort(&policy->movies, error)) { *out = policy; return false; }
        if (policy->media && !qa_media_library_stage_abort(&policy->media, error)) { *out = policy; return false; }
        for (size_t i=0;i<policy->cinematic_count;++i) frontend_material_movie_cinematic_receipt_free(policy->cinematic_receipts+i);
        free(policy->cinematic_receipts);
        owner->pending = NULL; free(policy); return false;
    }
    *out = policy; return true;
}
bool frontend_material_movies_policy_ready(frontend_material_movies_policy *policy, qa_error *error)
{
    if (!held(policy) || policy->published || !frontend_material_movies_current(policy->owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared shader movie roster lost its provider");
    if (policy->sealed) return true;
    policy->closing = true;
    if (policy->added > SIZE_MAX - policy->count)
        return frontend_fail(error, QA_ERROR_MEMORY, "Prepared shader movie roster exceeds its extent");
    size_t count = policy->count + policy->added;
    policy->merged_capacity = policy->owner->capacity;
    if (count > SIZE_MAX / sizeof(*policy->merged))
        return frontend_fail(error, QA_ERROR_MEMORY, "Prepared shader movie roster exceeds its allocation");
    if (!policy->merged_capacity && count) policy->merged_capacity = 8;
    while (policy->merged_capacity < count) {
        if (policy->merged_capacity > SIZE_MAX / sizeof(*policy->merged) / 2) {
            policy->merged_capacity = count; break;
        }
        policy->merged_capacity *= 2;
    }
    if (policy->merged_capacity && !policy->merged) {
        policy->merged = malloc(policy->merged_capacity * sizeof(*policy->merged));
        if (!policy->merged) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining prepared shader movie roster");
    }
    if (policy->count) memcpy(policy->merged, policy->original, policy->count * sizeof(*policy->merged));
    if (policy->added) memcpy(policy->merged + policy->count, policy->rows, policy->added * sizeof(*policy->merged));
    if (!qa_media_library_stage_ready(policy->media, error) ||
        !qa_material_movies_stage_ready(policy->movies, error)) return false;
    policy->sealed = true; return true;
}
bool frontend_material_movies_policy_ready_is(const frontend_material_movies_policy *policy)
{
    return held(policy) && policy->sealed && !policy->published &&
        frontend_material_movies_current(policy->owner) &&
        qa_material_library_video_start_is(policy->destination, prepared_start, policy) &&
        (!policy->owner->cinematic_source || qa_q3_cinematic_handles_stage_ready_is(policy->cinematics)) &&
        qa_media_library_stage_ready_is(policy->media) && qa_material_movies_stage_ready_is(policy->movies);
}
void frontend_material_movies_policy_publish(frontend_material_movies_policy *policy)
{
    if (!held(policy) || !policy->sealed || policy->published) return;
    qa_media_library_stage_publish(policy->media); qa_material_movies_stage_publish(policy->movies);
    policy->owner->rows = policy->merged; policy->owner->count += policy->added;
    policy->owner->capacity = policy->merged_capacity; policy->owner->next_target = policy->next_target;
    policy->owner->cinematic_receipts=policy->cinematic_receipts;
    policy->owner->cinematic_count=policy->cinematic_count;
    policy->owner->cinematic_capacity=policy->cinematic_capacity;
    policy->published = true;
}
static bool dispose(frontend_material_movies_policy **out, bool published, qa_error *error)
{
    frontend_material_movies_policy *policy = out ? *out : NULL;
    if (!held(policy) || policy->published != published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Shader movie retirement lost its exact held provider");
    if (policy->movies && !(published ? qa_material_movies_stage_finish(&policy->movies, error) :
        qa_material_movies_stage_abort(&policy->movies, error))) return false;
    if (policy->media && !(published ? qa_media_library_stage_finish(&policy->media, error) :
        qa_media_library_stage_abort(&policy->media, error))) return false;
    if (published) {
        free(policy->original);
        for (size_t i=0;i<policy->cinematic_original_count;++i) frontend_material_movie_cinematic_receipt_free(policy->cinematic_original+i);
        free(policy->cinematic_original);
    }
    else {
        for (size_t i = 0; i < policy->added; ++i) frontend_material_movie_row_free(policy->rows[i]);
        free(policy->merged);
        for (size_t i=0;i<policy->cinematic_count;++i) frontend_material_movie_cinematic_receipt_free(policy->cinematic_receipts+i);
        free(policy->cinematic_receipts);
    }
    free(policy->rows); policy->owner->pending = NULL; free(policy); *out = NULL; return true;
}
bool frontend_material_movies_policy_finish(frontend_material_movies_policy **out, qa_error *error)
{ return dispose(out, true, error); }
bool frontend_material_movies_policy_abort(frontend_material_movies_policy **out, qa_error *error)
{ return dispose(out, false, error); }

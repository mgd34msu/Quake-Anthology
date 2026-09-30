#include "content_refs.h"
#include "internal.h"
static bool view_encode(void *context, const qa_vfs *view, uint64_t *out, qa_error *error)
{
    if (!context || !view || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio content view encoder lacks its actual graph/view/output");
    uint64_t id = qa_application_content_view_id(context, view);
    if (!id) return frontend_fail(error, QA_ERROR_FORMAT, "Audio bank view is not in the retained content graph");
    *out = id; return true;
}
static bool view_decode(void *context, uint64_t id, const qa_vfs **out, qa_error *error)
{
    if (!context || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio content view decoder lacks its actual graph/output");
    const qa_vfs *view = qa_application_content_view(context, id);
    if (!view) return frontend_fail(error, QA_ERROR_FORMAT, "Saved audio bank view is absent from the restored graph");
    *out = view; return true;
}
static bool resource_encode(void *context, const qa_resource *resource, uint64_t *pool,
    uint64_t *version, qa_error *error)
{
    if (!context || !resource || !pool || !version || pool == version)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio resource encoder lacks genuine content identity outputs");
    if (!qa_application_content_resource_id(context, resource, pool, version))
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained audio resource is outside the actual content graph");
    return true;
}
static bool resource_decode(void *context, uint64_t pool, uint64_t version,
    const qa_resource **out, qa_error *error)
{
    if (!context || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio resource decoder lacks its actual graph/output");
    const qa_resource *resource = qa_application_content_resource(context, pool, version);
    if (!resource) return frontend_fail(error, QA_ERROR_FORMAT, "Saved audio source version is absent from the restored content graph");
    *out = resource; return true;
}
qa_audio_bank_checkpoint_refs frontend_audio_content_refs(qa_application_content_graph *graph)
{
    return (qa_audio_bank_checkpoint_refs){.context = graph, .view_encode = view_encode,
        .view_decode = view_decode, .resource_encode = resource_encode, .resource_decode = resource_decode};
}

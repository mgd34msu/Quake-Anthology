#include "internal.h"
#include <string.h>

bool qa_application_model_admit(qa_application *app,
    const qa_application_model_admission_request *request,
    qa_application_model_admission *out, bool *handled, qa_error *error)
{
    if (!app || !request || !out || !handled || !request->provider ||
        !request->request || !*request->request || !request->resource ||
        !request->opening || !request->view || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Model admission needs its actual BODY acquisition");
    *out = (qa_application_model_admission){0}; *handled = false;
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == request->provider) { provider = app->providers[i]; break; }
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->product || provider->product->family != request->family)
        return application_fail(error, QA_ERROR_ARGUMENT, "Model admission lost its selected BODY provider");
    qa_command_context source = {.owner = request->provider, .origin = QA_COMMAND_SERVER,
        .dialect = request->family == QA_GAME_Q2 ? QA_RULESET_Q2_CLASSIC :
            request->family == QA_GAME_Q3 ? QA_RULESET_Q3 : QA_RULESET_NETQUAKE}, captured;
    if (!qa_application_capture_command_context(app, &source, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(app, &captured, NULL);
    if (!files || !qa_vfs_lookup_equal(files, request->view) ||
        request->opening->resource_id != qa_resource_id(request->resource) ||
        !request->opening->path || strcmp(request->opening->path, request->request) ||
        !qa_vfs_acquisition_retained(request->view, request->opening, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Model admission differs from its genuine BODY file scope");
    if (!app->model_admission) return true;
    qa_application_model_admission result = {0};
    if (!app->model_admission(app->guest_context, app, request, &result, error)) return false;
    bool retained = false;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] == provider) { retained = true; break; }
    if (!qa_application_command_context_active(app, &captured) ||
        qa_application_context_files(app, &captured, NULL) != files ||
        !retained || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Model admission changed its actual BODY owner");
    if ((result.palette_rgb.size && (result.palette_rgb.size != 768 || !result.palette_rgb.data)) ||
        (result.images.palette_rgb.size &&
            (result.images.palette_rgb.size != 768 || !result.images.palette_rgb.data)) ||
        (result.images.translation.size &&
            (result.images.translation.size != 256 || !result.images.translation.data)) ||
        (result.palette_source.resource == NULL) != (result.palette_source.opening == NULL) ||
        (result.palette_source.resource && (result.palette_rgb.size != 768 ||
            !result.palette_view || !qa_vfs_lookup_equal(request->view, result.palette_view) ||
            result.palette_source.opening->resource_id != qa_resource_id(result.palette_source.resource) ||
            !qa_vfs_acquisition_retained(result.palette_view, result.palette_source.opening, error))))
        return application_fail(error, QA_ERROR_FORMAT, "Model constructor returned an incomplete palette receipt");
    *out = result; *handled = true; return true;
}

#include "remote_q2_material_movies_bridge.h"
#include "remote_q2_private.h"
#include "remote_q2_restore.h"
#include "remote_q2_source.h"
#include "renderer_materials.h"
#include "qa/media_library_prepare.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/media_library_save.h"

static bool shader_movies_current(void *context, const frontend_material_movie_source *source)
{
    frontend_remote_q2 *row = context;
    frontend_remote_q2_view view;
    qa_error error = {0};
    bool observed = row && row->importing ? frontend_remote_q2_import_read(row, &view, &error) :
        frontend_remote_q2_metadata_read(row, &view, &error);
    if (!observed || !source || !row->frontend ||
        view.owner != row || view.domain.application != row->frontend->application ||
        !row->identity || !row->content_generation || !row->selected || !row->content_admitted ||
        !view.content.catalog || !view.content.mounts || !view.map ||
        !view.images || !view.materials || !row->media ||
        source->frontend != row->frontend || source->context != row ||
        source->current != shader_movies_current || source->files != view.content.mounts ||
        source->images != view.images || source->materials != view.materials || source->media != row->media ||
        qa_scene_resources_files(view.images) != view.content.mounts ||
        qa_material_library_resource_owner(view.materials) != view.images ||
        qa_media_library_resource_owner(row->media) != view.images)
        return false;
    if (row->importing) return row->frontend->source_restoring;
    if (row->retiring) {
        if (row->busy || row->image_policy || !row->shader_movies ||
            !frontend_material_movies_idle(row->shader_movies) || !qa_media_library_idle(row->media) ||
            !qa_material_library_idle(row->materials)) return false;
        if (row->frontend->capture) return remote_q2_capture_owned(row);
        if (row->frontend->resource_inventory)
            return frontend_remote_q2_source_retirement_metadata_current(row, &error);
        return true;
    }
    if (!row->bound || row->retired) return row->retired;
    const frontend_remote_q2_domain *domain = &view.domain;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(domain->runtime), domain->client);
    if (!client || qa_network_epoch(domain->runtime, domain->client) != domain->epoch ||
        client->protocol.kind != domain->protocol.kind || client->protocol.revision != domain->protocol.revision ||
        client->protocol.flags != domain->protocol.flags)
        return false;
    size_t seats = 0;
    for (size_t i = 0; i < client->seat_count; ++i)
        if (client->seats[i].seat.owner == domain->seat.owner && client->seats[i].seat.index == domain->seat.index) {
            if (client->seats[i].remote_index >= QA_Q2_MAX_SEATS) return false;
            ++seats;
        }
    return seats == 1;
}
static frontend_material_movie_source source_view(frontend_remote_q2 *row)
{
    return (frontend_material_movie_source){.frontend = row ? row->frontend : NULL,
        .files = row ? row->content.mounts : NULL, .images = row ? row->images : NULL,
        .materials = row ? row->materials : NULL, .media = row ? row->media : NULL,
        .context = row, .current = shader_movies_current};
}
bool frontend_remote_q2_movie_source_read(frontend_remote_q2 *row,
    frontend_material_movie_source *out, qa_error *error)
{
    frontend_material_movie_source source = source_view(row);
    if (!out || !shader_movies_current(row, &source))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 shader movies lost their retained CLIENT content and media tuple");
    *out = source;
    return true;
}
bool remote_q2_material_movies_create(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->frontend || row->frontend->source_restoring || row->frontend->capture ||
        row->frontend->resource_inventory || row->importing || row->image_policy ||
        row->media || row->shader_movies || !row->images || !row->materials)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 shader movies require their genuine fresh material constructor");
    row->media = qa_media_library_create(row->images, error);
    if (!row->media) return false;
    frontend_material_movie_source source;
    return frontend_remote_q2_movie_source_read(row, &source, error) &&
        frontend_material_movies_create(&source, &row->shader_movies, error);
}
bool remote_q2_material_movies_prepare_restored(frontend_remote_q2 *row, qa_error *error)
{
    frontend_remote_q2_view view;
    if (!row || !frontend_remote_q2_import_read(row, &view, error) ||
        row->media || row->shader_movies || !view.images || !view.materials ||
        !view.content.mounts || qa_scene_resources_files(view.images) != view.content.mounts ||
        qa_material_library_resource_owner(view.materials) != view.images)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 movie cache import requires its empty isolated receiver banks");
    row->media = qa_media_library_create(view.images, error);
    return row->media != NULL;
}
bool remote_q2_material_movies_idle(const frontend_remote_q2 *row)
{
    return row && (!row->shader_movies || frontend_material_movies_idle(row->shader_movies)) &&
        (!row->media || qa_media_library_idle(row->media));
}
bool remote_q2_material_movies_clear(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->frontend || row->frontend->capture || row->frontend->resource_inventory ||
        row->image_policy || !remote_q2_material_movies_idle(row) ||
        (row->materials && !qa_material_library_idle(row->materials)))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 shader movie retirement retains an active provider borrow");
    if (!row->frontend->source_restoring && row->shader_movies && row->media) {
        frontend_material_movie_source expected = source_view(row);
        if (!frontend_renderer_materials_adopt_movies(row->frontend, &expected,
            &row->shader_movies, &row->media, error))
            return false;
    }
    if (!frontend_material_movies_destroy(&row->shader_movies, error)) return false;
    qa_media_library_destroy(row->media);
    row->media = NULL;
    return true;
}
size_t frontend_remote_q2_material_movie_count(const qa_frontend *frontend)
{
    size_t count = 0;
    for (const frontend_remote_q2 *row = frontend ? frontend->remote_q2 : NULL; row; row = row->next)
        if (row->media) ++count;
    return count;
}
frontend_remote_q2 *frontend_remote_q2_material_movie_at(const qa_frontend *frontend, size_t ordinal)
{
    for (frontend_remote_q2 *row = frontend ? frontend->remote_q2 : NULL; row; row = row->next)
        if (row->media && ordinal-- == 0) return row;
    return NULL;
}

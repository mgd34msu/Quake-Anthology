#include "q1_sky_private.h"
#include "q1_sky_save.h"
#include "save_private.h"
#include "resource_bindings.h"
#include "qa/scene_resource_save.h"
#include <math.h>
#include <stdio.h>

static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
static bool image_valid(const frontend_q1_sky_selection *row, unsigned face)
{
    const qa_scene_image *image = row->images[face];
    if (!row->found) return image == NULL;
    if (!(row->found & (1u << face))) return image == qa_scene_missing(row->bank);
    qa_scene_image_request request;
    if (!image || !qa_scene_image_request_read(row->bank, image, &request) || !request.exact_file || !request.source ||
        !request.source_mount || !request.name || strcmp(request.name, image->name) ||
        request.options.family != QA_SCENE_Q1 || request.options.wrap != QA_SCENE_CLAMP ||
        request.options.filter != QA_SCENE_LINEAR || request.options.usage != QA_IMAGE_USAGE_SKY ||
        request.options.mipmap || request.options.transparent_index != -1 ||
        request.options.palette_rgb.size || request.options.translation.size || request.options.transparent ||
        request.options.fullbright_only || image->kind != QA_SCENE_RGBA8 || image->level_count != 1 ||
        image->wrap != QA_SCENE_CLAMP || image->filter != QA_SCENE_LINEAR || !image->levels ||
        !image->levels[0].width || !image->levels[0].height) return false;
    size_t length = strlen(row->name), prefix = strlen("gfx/env/");
    const char *name = request.name;
    if (strncmp(name, "gfx/env/", prefix) || strncmp(name + prefix, row->name, length) ||
        strncmp(name + prefix + length, suffixes[face], 2)) return false;
    const char *extension = name + prefix + length + 2;
    return !strcmp(extension, ".tga") || !strcmp(extension, ".png");
}
static bool selection_valid(const frontend_q1_sky *owner, const frontend_q1_sky_selection *row, bool baseline)
{
    if (!frontend_q1_sky_selection_current(owner, row) || row->found > 63 ||
        (!*row->name && row->found) || (baseline && (row->provider || row->recipient.registry || row->sequence || row->map_revision)) ||
        (!baseline && (!row->sequence || (owner->next_sequence && row->sequence >= owner->next_sequence) ||
            row->map_revision != owner->map_revision))) return false;
    if (row->recipient.registry && !qa_actors_get(qa_world_actors(qa_application_world(owner->application)), row->recipient)) return false;
    for (unsigned i = 0; i < 6; ++i) if (!image_valid(row, i)) return false;
    return true;
}
static bool valid(const frontend_q1_sky *owner)
{
    if (!frontend_q1_sky_current(owner) || !frontend_q1_sky_idle(owner) || !isfinite(owner->fog)) return false;
    const qa_cvar_view *fog = qa_cvars_find(qa_application_cvars(owner->application), "r_skyfog");
    if (!fog || !isfinite(fog->number) || owner->fog_modification > fog->modification_count) return false;
    if (!owner->map) return !owner->retained && !owner->q1_map && !owner->map_fog;
    qa_bsp_view bsp;
    if (!qa_bsp_open(qa_resource_bytes(owner->map), &bsp, NULL) || (bsp.family == QA_BSP_Q1) != owner->q1_map ||
        !selection_valid(owner, owner->baseline, true)) return false;
    uint64_t previous = 0;
    for (const frontend_q1_sky_selection *row = owner->retained; row; row = row->next) {
        if (!selection_valid(owner, row, false) || row->sequence <= previous) return false;
        for (const frontend_q1_sky_selection *other = owner->retained; other != row; other = other->next)
            if (qa_actor_id_equal(other->recipient, row->recipient)) return false;
        previous = row->sequence;
    }
    return true;
}
static bool selection_fields(qa_source_save_io *io, frontend_q1_sky *owner, frontend_scene_namespace *space,
    frontend_q1_sky_selection **link, bool baseline)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (reading) {
        *link = calloc(1, sizeof(**link));
        if (!*link) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual Q1 sky selection");
    }
    frontend_q1_sky_selection *row = *link;
    if (!row || !frontend_save_provider(io, owner->application, &row->provider) ||
        !qa_source_save_actor(io, &row->recipient) || !qa_source_save_owned_text(io, &row->name) || !row->name ||
        !qa_source_save_u64(io, &row->sequence) || !qa_source_save_u64(io, &row->map_revision) ||
        !qa_source_save_u8(io, &row->found) || row->found > 63) return false;
    if (reading) {
        if (row->provider) {
            qa_vfs *files = NULL;
            if (!frontend_event_q1_images_read(owner->frontend, row->provider, &row->bank, &files, io->error)) return false;
        } else row->bank = owner->map_bank;
    }
    for (unsigned face = 0; face < 6; ++face) {
        uint64_t key = 0;
        if ((!reading && row->images[face] && !frontend_scene_image_encode(space, row->images[face], &key, io->error)) ||
            !qa_source_save_u64(io, &key) || ((key != 0) != (row->found != 0))) return false;
        if (reading && key) {
            const qa_scene_image *image = NULL;
            if (!frontend_scene_image_decode(space, key, &image, io->error)) return false;
            qa_scene_image_retain(image); row->images[face] = image;
        }
    }
    return selection_valid(owner, row, baseline);
}
static bool fields(qa_source_save_io *io, frontend_q1_sky *owner, frontend_scene_namespace *space)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, map = owner->map != NULL;
    uint8_t magic[4] = {'Q','F','Q','S'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFQS", 4) || !qa_source_save_bool(io, &map) || !qa_source_save_u64(io, &owner->next_sequence) ||
        !qa_source_save_u64(io, &owner->fog_modification) || !qa_source_save_f32(io, &owner->fog) || !isfinite(owner->fog) ||
        !qa_source_save_bool(io, &owner->map_fog) || !qa_source_save_bool(io, &owner->q1_map) ||
        !qa_source_save_u64(io, &owner->map_revision)) return false;
    qa_application_content_graph *graph = qa_application_content_graph_read(owner->application);
    uint64_t pool = 0, resource = 0;
    if ((!reading && map && (!graph || !qa_application_content_resource_id(graph, owner->map, &pool, &resource))) ||
        !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource) ||
        (map ? !pool || !resource || !owner->map_revision : pool || resource || owner->map_revision)) return false;
    if (reading && map) {
        const qa_resource *actual = graph ? qa_application_content_resource(graph, pool, resource) : NULL;
        if (!actual || actual != owner->frontend->map_resource || owner->map_revision != owner->frontend->map_revision ||
            !owner->frontend->images) return false;
        owner->map = (qa_resource *)actual; qa_resource_retain(owner->map); owner->map_bank = owner->frontend->images;
    }
    if (map && !selection_fields(io, owner, space, &owner->baseline, true)) return false;
    size_t count = 0;
    if (!reading) for (const frontend_q1_sky_selection *row = owner->retained; row; row = row->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size - io->offset : SIZE_MAX) || (!map && count)) return false;
    frontend_q1_sky_selection **link = &owner->retained;
    for (size_t i = 0; i < count; ++i) {
        if (!selection_fields(io, owner, space, link, false)) return false;
        link = &(*link)->next;
    }
    return valid(owner);
}
bool frontend_q1_sky_checkpoint(const frontend_q1_sky *borrowed, frontend_scene_namespace *space,
    qa_buffer *out, qa_error *error)
{
    if (!borrowed || !space || !out || out->data || out->size || !borrowed->frontend->capture || !valid(borrowed))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky capture requires its actual held frontend and namespace");
    qa_source_save_io io = {0}; frontend_q1_sky copy = *borrowed;
    bool ok = qa_source_save_writer(&io, qa_application_session(copy.application), error) &&
        fields(&io, &copy, space) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Q1 sky continuation leaves its actual resource holders");
    return ok;
}
bool frontend_q1_sky_restore(qa_frontend *frontend, frontend_scene_namespace *space, qa_bytes bytes,
    frontend_q1_sky **out, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->source_restoring || frontend->capture || !space || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky restore requires its isolated actual candidate");
    frontend_q1_sky *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Restoring actual Q1 sky owner");
    owner->frontend = frontend; owner->application = frontend->application;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(owner->application), bytes, error) &&
        fields(&io, owner, space) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        frontend_q1_sky_destroy(&owner, NULL);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved Q1 sky cannot bind its actual image/provider owners");
        return false;
    }
    *out = owner; return true;
}
bool frontend_q1_sky_publish_ready(const frontend_q1_sky *owner, qa_error *error)
{
    return valid(owner) || frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky publication lost its actual cold holders");
}

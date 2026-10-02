#include "remote_q2_restore.h"
#include "remote_q2_private.h"
#include "save_private.h"
#include "qa/network_q2_wire_save.h"
#include "qa/input_command_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/archive.h"
#include "remote_q2_effects_bridge.h"
#include "remote_q2_footsteps.h"
#include "remote_q2_material_movies_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct saved_q2 {
    uint64_t domain_catalog, catalog, mounts, map_pool, map_resource;
    bool bound, selected, ready, retired, images, materials, fonts, sounds, media;
    frontend_remote_q2_domain domain;
    qa_buffer input, stage, geometry, footsteps;
} saved_q2;
typedef struct effects_refs {
    const frontend_remote_q2 *row;
    const frontend_remote_q2_restore_refs *refs;
} effects_refs;
static bool effect_image_encode(void *context, const qa_scene_image *image, uint64_t *out, qa_error *error)
{
    effects_refs *refs = context;
    return frontend_scene_image_encode(refs->refs->scene, image, out, error);
}
static bool effect_image_decode(void *context, uint64_t id, const qa_scene_image **out, qa_error *error)
{
    effects_refs *refs = context;
    return frontend_scene_image_decode(refs->refs->scene, id, out, error);
}
static bool effect_actor_encode(void *context, qa_actor_id actor, qa_saved_actor_id *out, qa_error *error)
{
    effects_refs *refs = context;
    return qa_actors_save_reference(qa_session_actor_registry(qa_application_session(refs->row->options.domain.application)),
        actor, out, error);
}
static bool effect_actor_decode(void *context, qa_saved_actor_id saved, qa_actor_id *out, qa_error *error)
{
    effects_refs *refs = context;
    return qa_actors_reference_saved(qa_session_actor_registry(qa_application_session(refs->row->options.domain.application)),
        saved, true, out, error);
}
static frontend_remote_q2_effects_refs effect_refs(effects_refs *context)
{
    return (frontend_remote_q2_effects_refs){.context = context,
        .image_encode = effect_image_encode, .image_decode = effect_image_decode,
        .actor_encode = effect_actor_encode, .actor_decode = effect_actor_decode,
        .light_encode = NULL, .light_decode = NULL};
}
static bool geometry_fields(qa_source_save_io *io, qa_collision_portal_checkpoint *state)
{
    uint32_t family = state->family, format = state->format;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_u32(io, &family) || family != QA_COLLISION_Q2 ||
        !qa_source_save_u32(io, &format) || (format != QA_BSP_IBSP38 && format != QA_BSP_QBSP && format != QA_BSP_IBSP44) ||
        !qa_source_save_u64(io, &state->map_identity) || !qa_source_save_u32(io, &state->area_count) ||
        !qa_source_save_bool(io, &state->no_areas) || !qa_source_save_count(io, &state->portal_count,
            reading ? (io->input.size - io->offset) / 9 : SIZE_MAX / sizeof(*state->portals))) return false;
    state->family = family; state->format = format;
    if (reading && state->portal_count) {
        state->portals = calloc(state->portal_count, sizeof(*state->portals));
        if (!state->portals) return false;
    }
    for (size_t i = 0; i < state->portal_count; ++i)
        if (!qa_source_save_u32(io, &state->portals[i].portal) ||
            !qa_source_save_u32(io, &state->portals[i].contributions) || !qa_source_save_bool(io, &state->portals[i].primary) ||
            (i && state->portals[i - 1].portal >= state->portals[i].portal)) return false;
    return true;
}
static bool geometry_checkpoint(const qa_collision_geometry *geometry, qa_buffer *out, qa_error *error)
{
    if (!geometry) return true;
    qa_source_save_io io = {0}; qa_collision_portal_checkpoint state = {0};
    bool ok = qa_collision_capture_portals(geometry, &state, error) && qa_source_save_writer(&io, NULL, error) &&
        geometry_fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&state); return ok;
}
static bool geometry_restore(frontend_remote_q2 *row, const qa_buffer *bytes, qa_error *error)
{
    if (!bytes->size) return true;
    if (!row->map) return false;
    qa_source_save_io io = {0}; qa_collision_portal_checkpoint state = {0}; qa_bsp_view bsp;
    bool ok = qa_source_save_reader(&io, NULL, (qa_bytes){bytes->data, bytes->size}, error) &&
        geometry_fields(&io, &state) && qa_source_save_finish(&io, NULL) &&
        qa_bsp_open(qa_resource_bytes(row->map), &bsp, error) && bsp.family == QA_BSP_Q2 &&
        qa_collision_create(&bsp, &row->geometry, error) && qa_collision_bind_resource(row->geometry, row->map, error) &&
        qa_collision_restore_portals(row->geometry, &state, error);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&state); return ok;
}
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data) return remote_q2_fail(io->error, QA_ERROR_MEMORY, "Retaining Q2 cold component bytes");
    }
    return qa_source_save_bytes(io, value->data, size);
}
static bool opening(qa_source_save_io *io, qa_vfs_acquisition *value,
    const frontend_remote_q2_restore_refs *refs, uint64_t saved_view, const qa_vfs *files)
{
    if (!qa_source_save_u64(io, &value->mount) || !qa_source_save_u64(io, &value->resource_id) ||
        !frontend_save_text(io, &value->path) || !frontend_save_text(io, &value->lookup_path) ||
        !frontend_save_text(io, &value->link_source) || !frontend_save_text(io, &value->link_target)) return false;
    if (!value->resource_id) return !value->mount && !value->opening_present;
    if (io->direction == QA_SOURCE_SAVE_READ) files = qa_application_content_view(refs->content, saved_view);
    return files && qa_vfs_acquisition_opening_codec(io, files, value) && value->opening_present;
}
static bool domain(qa_source_save_io *io, frontend_remote_q2_domain *d)
{
    qa_command_context *c = &d->command_context; uint32_t dialect = c->dialect, origin = c->origin;
    if (!qa_source_save_u64(io, &d->client.owner) || !qa_source_save_u64(io, &d->client.generation) ||
        !qa_source_save_u32(io, &d->client.slot) || !qa_source_save_u64(io, &d->seat.owner) ||
        !qa_source_save_u32(io, &d->seat.index) || !qa_source_save_u64(io, &d->epoch) ||
        !qa_source_save_u64(io, &d->configuration_generation) || !qa_source_save_u32(io, &d->physical_seat) ||
        !qa_q2_save_protocol(io, &d->protocol) || !qa_source_save_u32(io, &d->product) ||
        !qa_source_save_u64(io, &c->session) || !qa_source_save_u64(io, &c->owner) ||
        !qa_source_save_u64(io, &c->client) || !qa_source_save_u32(io, &c->seat) ||
        !qa_source_save_u64(io, &c->registry) || !qa_source_save_u64(io, &c->generation) ||
        !qa_source_save_u32(io, &dialect) || dialect > QA_CONSOLE_Q3 ||
        !qa_source_save_u32(io, &origin) || !qa_source_save_bool(io, &c->direct) ||
        !qa_source_save_bool(io, &c->console_text) || !qa_source_save_actor(io, &c->actor)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) { c->dialect = dialect; c->origin = origin; }
    return !c->script;
}
static bool model_receipt(qa_source_save_io *io, qa_resource **resource, qa_vfs_acquisition *receipt,
    const frontend_remote_q2_restore_refs *refs, uint64_t saved_view, const qa_vfs *files)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t pool = 0, id = 0;
    if (!reading && *resource && !qa_application_content_resource_id(refs->content, *resource, &pool, &id)) return false;
    if (!qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &id) || !!pool != !!id ||
        !opening(io, receipt, refs, saved_view, files)) return false;
    if (reading && id) {
        *resource = (qa_resource *)qa_application_content_resource(refs->content, pool, id);
        if (!*resource) return false;
        qa_resource_retain(*resource);
    }
    if (!id) return !*resource && !receipt->resource_id && !receipt->mount && !receipt->path &&
        !receipt->lookup_path && !receipt->link_source && !receipt->link_target && !receipt->opening_present;
    return *resource && receipt->resource_id == qa_resource_id(*resource);
}
static bool retained(frontend_remote_q2 *row, const frontend_remote_q2_restore_refs *refs, saved_q2 *saved, qa_error *error)
{
    bool map = row->map != NULL;
    if (!saved->domain_catalog || !qa_application_content_catalog(refs->content, saved->domain_catalog) ||
        (saved->selected && (!saved->catalog || !saved->mounts)) ||
        (!saved->selected && (saved->catalog || saved->mounts || map || saved->ready ||
            saved->images || saved->materials || saved->fonts || saved->sounds || row->saved_world)) ||
        (!!saved->map_pool != !!saved->map_resource) || map != !!saved->map_resource ||
        ((saved->materials || saved->fonts) && !saved->images) ||
        (row->saved_world && (!map || !saved->images || !saved->materials)) ||
        (row->models && (!saved->images || !saved->materials)) ||
        (row->saved_effects.size && (!map || !saved->images || !saved->materials || !row->saved_white)) ||
        (saved->ready && (!row->saved_world || !saved->images || !saved->materials || !saved->fonts || !saved->sounds || !row->saved_effects.size)) ||
        (saved->selected && (!qa_catalog_product_view_current(row->content.catalog, row->content.selected, row->content.mounts) ||
            !row->content.selected_write_root || !row->content.base_write_root)))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 cold resource topology leaves its actual catalog/private view");
    if (map && (qa_resource_pool_find(qa_vfs_resources(row->content.mounts), qa_resource_id(row->map)) != row->map ||
        row->map_opening.resource_id != qa_resource_id(row->map) ||
        !qa_vfs_acquisition_retained(row->content.mounts, &row->map_opening, error))) return false;
    for (remote_q2_model *m = row->models; m; m = m->next)
        if (!m->resource || !m->path || !*m->path || (row->importing && (!m->saved_model || !m->saved_scene)) ||
            qa_resource_pool_find(qa_vfs_resources(row->content.mounts), qa_resource_id(m->resource)) != m->resource ||
            m->opening.resource_id != qa_resource_id(m->resource) || !m->opening.path || strcmp(m->path, m->opening.path) ||
            !qa_vfs_acquisition_retained(row->content.mounts, &m->opening, error) ||
            !remote_q2_model_scope_current(row, m, error)) return false;
    for (remote_q2_missing_model *missing = row->missing_models; missing; missing = missing->next) {
        if (!missing->path || !*missing->path || !saved->selected) return false;
        char *normalized = qa_archive_normalize_path(missing->path, error);
        bool valid = normalized && !strcmp(normalized, missing->path); free(normalized);
        if (!valid) return false;
        for (remote_q2_model *m = row->models; m; m = m->next)
            if (!strcmp(m->path, missing->path)) return false;
        for (remote_q2_missing_model *prior = row->missing_models; prior != missing; prior = prior->next)
            if (!strcmp(prior->path, missing->path)) return false;
    }
    return true;
}
static bool fog_fields(qa_source_save_io *io, qa_scene_fog *fog)
{
    return qa_source_save_vec3(io, &fog->color) && qa_vec_finite(fog->color) &&
        qa_source_save_vec3(io, &fog->height_color) && qa_vec_finite(fog->height_color) &&
        qa_source_save_vec3(io, &fog->height_end_color) && qa_vec_finite(fog->height_end_color) &&
        qa_source_save_f32(io, &fog->density) && isfinite(fog->density) &&
        qa_source_save_f32(io, &fog->sky_factor) && isfinite(fog->sky_factor) &&
        qa_source_save_f32(io, &fog->height_density) && isfinite(fog->height_density) &&
        qa_source_save_f32(io, &fog->height_start) && isfinite(fog->height_start) &&
        qa_source_save_f32(io, &fog->height_end) && isfinite(fog->height_end) &&
        qa_source_save_f32(io, &fog->height_falloff) && isfinite(fog->height_falloff);
}
static bool fields(qa_source_save_io *io, frontend_remote_q2 *row,
    const frontend_remote_q2_restore_refs *refs, saved_q2 *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','2','R','C'}; uint32_t schema = 13;
    bool material_scripts = row->options.material_scripts;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "Q2RC", 4) ||
        !qa_source_save_u32(io, &schema) || schema != 13 || !domain(io, &saved->domain) ||
        !qa_source_save_bool(io, &material_scripts) || material_scripts != row->options.material_scripts ||
        !qa_source_save_bool(io, &saved->bound) || !qa_source_save_bool(io, &saved->selected) ||
        !qa_source_save_bool(io, &row->content_admitted) ||
        !qa_source_save_bool(io, &saved->ready) || !qa_source_save_bool(io, &saved->retired) ||
        !qa_source_save_bool(io, &row->retiring) || (row->retiring && !saved->bound) ||
        !qa_source_save_bool(io, &saved->images) || !qa_source_save_bool(io, &saved->materials) ||
        !qa_source_save_bool(io, &saved->fonts) || !qa_source_save_bool(io, &saved->sounds) ||
        !qa_source_save_bool(io, &saved->media) || (saved->media && (!saved->images || !saved->materials)) ||
        !qa_source_save_u64(io, &saved->domain_catalog) || !qa_source_save_u64(io, &saved->catalog) ||
        !qa_source_save_u64(io, &saved->mounts) || !qa_source_save_u32(io, &row->content.selected) ||
        !qa_source_save_u32(io, &row->content.base) || !qa_source_save_u64(io, &row->identity) ||
        row->identity <= QA_FRONTEND_COMMAND_OWNER || !qa_source_save_u64(io, &row->loading_generation) ||
        !qa_source_save_u64(io, &row->content_generation) || !qa_source_save_u64(io, &row->received_ns) ||
        !qa_source_save_u64(io, &row->sample_ns) || !qa_source_save_u32(io, &row->acknowledged) ||
        !qa_source_save_u32(io, &row->last_sent) || !qa_source_save_bool(io, &row->sent_set) ||
        !qa_source_save_bool(io, &row->input_set) ||
        !qa_source_save_u64(io, &row->last_command) || !qa_source_save_u64(io, &row->acknowledged_command) ||
        !qa_source_save_bool(io, &row->predicted) || !qa_source_save_vec3(io, &row->prediction_origin) ||
        !qa_source_save_vec3(io, &row->prediction_angles) || !qa_source_save_vec3(io, &row->prediction_error) ||
        !qa_source_save_vec3(io, &row->prediction_pml) || !qa_source_save_f32(io, &row->prediction_step) ||
        !isfinite(row->prediction_step) || !qa_source_save_u64(io, &row->prediction_step_ns) ||
        !qa_source_save_u64(io, &row->prediction_command) || !qa_source_save_i32(io, &row->prediction_frame) ||
        !qa_source_save_vec3(io, &row->prediction_plane.normal) || !qa_source_save_f32(io, &row->prediction_plane.distance) ||
        !qa_source_save_i32(io, &row->prediction_plane.type) || !qa_source_save_u8(io, &row->prediction_plane.signbits) ||
        !qa_source_save_f32(io, &row->fraction) || !isfinite(row->fraction) || row->fraction < 0 || row->fraction > 1 ||
        !qa_source_save_f32(io, &row->frame_ms) || !isfinite(row->frame_ms) || row->frame_ms <= 0 ||
        !qa_source_save_f64(io, &row->sample_frame_seconds) || !isfinite(row->sample_frame_seconds) || row->sample_frame_seconds < 0 ||
        !qa_source_save_f32(io, &row->height_previous) || !qa_source_save_f32(io, &row->height_current) ||
        !qa_source_save_f64(io, &row->height_changed_ms) || !qa_source_save_bool(io, &row->height_set) ||
        !qa_source_save_bool(io, &row->gun_set) || !qa_source_save_u32(io, &row->gun_frame) ||
        !qa_source_save_u32(io, &row->gun_previous_frame) || !qa_source_save_i32(io, &row->gun_server_frame) ||
        !qa_source_save_bool(io, &row->hit_marker_set) || !qa_source_save_u32(io, &row->hit_marker_count) ||
        !qa_source_save_i32(io, &row->hit_marker_frame) || !qa_source_save_u64(io, &row->hit_marker_ns) ||
        (!row->hit_marker_set && (row->hit_marker_count || row->hit_marker_ns || row->hit_marker_frame)) ||
        !qa_source_save_bool(io, &row->fog_received) || !qa_source_save_u16(io, &row->fog_duration_ms) ||
        !qa_source_save_f64(io, &row->fog_started_ms) || !isfinite(row->fog_started_ms) ||
        !fog_fields(io, &row->fog_start) || !fog_fields(io, &row->fog_end) ||
        !qa_q2_save_serverdata(io, &row->data) || (reading && !remote_q2_layout_adopt(row, &row->data, io->error)) ||
        !qa_q2_save_frame(io, &row->frame) || !qa_q2_save_frame(io, &row->previous) ||
        !qa_source_save_u64(io, &saved->map_pool) || !qa_source_save_u64(io, &saved->map_resource) ||
        !opening(io, &row->map_opening, refs, saved->mounts, row->content.mounts) || !qa_source_save_u64(io, &row->saved_world) ||
        !qa_source_save_u64(io, &row->saved_classic) || !qa_source_save_u64(io, &row->saved_white) ||
        !blob(io, &saved->geometry) || !blob(io, &saved->input) || !blob(io, &row->saved_effects) ||
        !blob(io, &saved->footsteps)) return false;
    if (row->content_admitted && !saved->selected) return false;
    uint32_t ground = row->prediction_ground.hit;
    if (!qa_source_save_u32(io, &ground) || ground > QA_TRACE_HIT_ACTOR ||
        !qa_source_save_actor(io, &row->prediction_ground.actor) ||
        !qa_source_save_u32(io, &row->prediction_ground.model)) return false;
    row->prediction_ground.hit = ground;
    for (size_t kind = 0; kind < 2; ++kind) for (size_t i = 0; i < 64; ++i) {
        remote_q2_sent_command *sent = (kind ? row->commands : row->sent) + i;
        if (!qa_source_save_bool(io, &sent->valid) || !qa_source_save_u32(io, &sent->packet_sequence) ||
            !qa_source_save_u64(io, &sent->sent_ns) || !qa_source_save_u64(io, &sent->command_number) ||
            !qa_q2_save_usercmd(io, &sent->command) || !qa_source_save_bool(io, &sent->predicted) ||
            !qa_source_save_vec3(io, &sent->origin) || (sent->valid && (!sent->command_number ||
                (kind ? (sent->command_number & 63) : (sent->packet_sequence & 63)) != i))) return false;
    }
    if (row->sent_set && (!row->sent[row->last_sent & 63].valid ||
        row->sent[row->last_sent & 63].packet_sequence != row->last_sent ||
        row->sent[row->last_sent & 63].command_number != row->last_command ||
        !row->commands[row->last_command & 63].valid ||
        row->commands[row->last_command & 63].command_number != row->last_command)) return false;
    if (!qa_vec_finite(row->prediction_origin) || !qa_vec_finite(row->prediction_angles) ||
        !qa_vec_finite(row->prediction_error) || !qa_vec_finite(row->prediction_pml) ||
        row->acknowledged_command > row->last_command) return false;
    for (size_t i = 0; i < row->layout.max_configs; ++i) if (!frontend_save_text(io, &row->configs[i])) return false;
    size_t count = row->baseline_count;
    size_t maximum = reading ? (io->input.size - io->offset) / 4 : SIZE_MAX;
    if (maximum > SIZE_MAX / sizeof(*row->baselines)) maximum = SIZE_MAX / sizeof(*row->baselines);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (reading) {
        row->baselines = count ? calloc(count, sizeof(*row->baselines)) : NULL; row->baseline_count = count;
        if (count && !row->baselines) return remote_q2_fail(io->error, QA_ERROR_MEMORY, "Retaining Q2 cold baselines");
    }
    for (size_t i = 0; i < count; ++i)
        if (!qa_q2_save_entity(io, row->baselines + i) || (i && row->baselines[i - 1].number >= row->baselines[i].number)) return false;
    for (size_t i = 0; i < 256; ++i) if (!qa_source_save_i32(io, row->inventory + i)) return false;
    if (!frontend_save_text(io, &row->overlay)) return false;
    if (!qa_source_save_count(io, &row->hud_table.row_count, 11) ||
        !qa_source_save_count(io, &row->hud_table.column_count, 5)) return false;
    for (size_t i = 0; i < 5; ++i)
        if (!qa_source_save_f32(io, row->hud_table.columns + i) || !isfinite(row->hud_table.columns[i])) return false;
    if (!qa_source_save_bytes(io, row->hud_table.cells, sizeof(row->hud_table.cells))) return false;
    for (size_t i = 0; i < 11; ++i) for (size_t j = 0; j < 6; ++j)
        if (!memchr(row->hud_table.cells[i][j], 0, sizeof(row->hud_table.cells[i][j]))) return false;
    count = frontend_remote_q2_model_count(row);
    maximum = reading ? (io->input.size - io->offset) / 49 : SIZE_MAX;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    remote_q2_model **link = &row->models;
    for (size_t i = 0; i < count; ++i) {
        if (reading) { *link = calloc(1, sizeof(**link)); if (!*link) return false; }
        remote_q2_model *m = *link; uint64_t pool = 0, resource = 0, model = m->saved_model, scene = m->saved_scene;
        if (!reading && (!qa_application_content_resource_id(refs->content, m->resource, &pool, &resource) ||
            !frontend_model_encode(refs->models, m->source, &model, io->error) ||
            !frontend_scene_root_encode(refs->roots, m->scene, &scene, io->error))) return false;
        if (!reading) { if (model == UINT64_MAX) return false; ++model; }
        if (!frontend_save_text(io, &m->path) || !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource) ||
            !opening(io, &m->opening, refs, saved->mounts, row->content.mounts) ||
            !model_receipt(io, &m->scope, &m->scope_opening, refs, saved->mounts, row->content.mounts) ||
            !model_receipt(io, &m->palette, &m->palette_opening, refs, saved->mounts, row->content.mounts) ||
            !qa_source_save_u64(io, &model) || !qa_source_save_u64(io, &scene)) return false;
        if (reading) {
            m->saved_model = model; m->saved_scene = scene;
            m->resource = (qa_resource *)qa_application_content_resource(refs->content, pool, resource);
            if (!m->resource) return false;
            qa_resource_retain(m->resource);
        }
        link = &m->next;
    }
    count = 0; for (remote_q2_missing_model *m = row->missing_models; m; m = m->next) ++count;
    maximum = reading ? (io->input.size - io->offset) / 9 : SIZE_MAX;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    remote_q2_missing_model **missing = &row->missing_models;
    for (size_t i = 0; i < count; ++i) {
        if (reading) { *missing = calloc(1, sizeof(**missing)); if (!*missing) return false; }
        if (!frontend_save_text(io, &(*missing)->path) || !(*missing)->path || !*(*missing)->path) return false;
        missing = &(*missing)->next;
    }
    count = 0; for (remote_q2_picture *p = row->pictures; p; p = p->next) ++count;
    maximum = reading ? (io->input.size - io->offset) / 9 : SIZE_MAX;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    remote_q2_picture **picture = &row->pictures;
    for (size_t i = 0; i < count; ++i) {
        if (reading) { *picture = calloc(1, sizeof(**picture)); if (!*picture) return false; }
        remote_q2_picture *p = *picture; uint64_t image = p->saved_image;
        if (!reading && !frontend_scene_image_encode(refs->scene, p->image, &image, io->error)) return false;
        if (!frontend_save_text(io, &p->name) || !qa_source_save_u64(io, &image) || !image) return false;
        if (reading) p->saved_image = image;
        picture = &p->next;
    }
    count = row->download_attempted_count;
    maximum = reading ? io->input.size - io->offset : SIZE_MAX;
    if (maximum > SIZE_MAX / sizeof(*row->download_attempted)) maximum = SIZE_MAX / sizeof(*row->download_attempted);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (reading) {
        row->download_attempted = count ? calloc(count, sizeof(*row->download_attempted)) : NULL;
        row->download_attempted_count = count; if (count && !row->download_attempted) return false;
    }
    for (size_t i = 0; i < count; ++i) if (!frontend_save_text(io, row->download_attempted + i) || !row->download_attempted[i]) return false;
    if (!frontend_save_text(io, &row->download_path) || !qa_source_save_u64(io, &row->download_logical_nonce) ||
        !qa_source_save_u64(io, &row->download_bytes) || !qa_source_save_u8(io, &row->download_percent) ||
        row->download_bytes > INT32_MAX || row->download_percent > 100 || !blob(io, &saved->stage)) return false;
    return (!row->download_path || remote_q2_download_path_valid(row->download_path)) &&
        (!!row->download_path == !!row->download_logical_nonce) &&
        (row->download_path ? saved->stage.size == row->download_bytes : !saved->stage.size && !row->download_bytes && !row->download_percent);
}
bool frontend_remote_q2_checkpoint(const frontend_remote_q2 *source,
    const frontend_remote_q2_restore_refs *refs, qa_buffer *out, qa_error *error)
{
    frontend_remote_q2 *row = (frontend_remote_q2 *)source;
    if (!row || !refs || !refs->content || !refs->models || !refs->roots || !refs->scene ||
        !out || out->data || out->size || !remote_q2_capture_owned(row)) return false;
    saved_q2 saved = {.domain = row->options.domain, .bound = row->bound, .selected = row->selected,
        .ready = row->media_ready, .retired = row->retired,
        .images = row->images != NULL, .materials = row->materials != NULL,
        .fonts = row->fonts != NULL, .sounds = row->sounds != NULL, .media = row->media != NULL,
        .domain_catalog = qa_application_content_catalog_id(refs->content, row->options.domain.catalog),
        .catalog = qa_application_content_catalog_id(refs->content, row->content.catalog),
        .mounts = qa_application_content_view_id(refs->content, row->content.mounts)};
    frontend_remote_q2 captured = *row; captured.saved_world = captured.saved_classic = captured.saved_white = 0;
    captured.saved_effects = (qa_buffer){0};
    effects_refs effect_context = {row, refs}; frontend_remote_q2_effects_refs effects = effect_refs(&effect_context);
    bool ok = (!row->map || qa_application_content_resource_id(refs->content, row->map, &saved.map_pool, &saved.map_resource)) &&
        (!row->world || frontend_world_encode(refs->roots, row->world, &captured.saved_world, error)) &&
        frontend_font_encode(row->frontend, row->classic, &captured.saved_classic, error) &&
        frontend_scene_image_encode(refs->scene, row->white, &captured.saved_white, error) &&
        qa_input_command_checkpoint(&row->input, &saved.input, error) && geometry_checkpoint(row->geometry, &saved.geometry, error) &&
        (!row->effects || frontend_remote_q2_effects_checkpoint(row->effects, &effects, &captured.saved_effects, error)) &&
        remote_q2_footsteps_checkpoint(row, refs->content, &saved.footsteps, error);
    if (ok && row->download_stage) {
        saved.stage.size = (size_t)row->download_bytes;
        saved.stage.data = saved.stage.size ? malloc(saved.stage.size) : NULL;
        if (saved.stage.size && !saved.stage.data) ok = false;
        size_t received = 0;
        if (ok) ok = qa_fs_stage_read(row->download_stage, 0, saved.stage.data, saved.stage.size, &received, error) && received == saved.stage.size;
    }
    qa_source_save_io io = {0}; ++row->busy;
    if (ok) ok = qa_source_save_writer(&io, qa_application_session(row->frontend->application), error) &&
        fields(&io, &captured, refs, &saved) && retained(&captured, refs, &saved, error) && qa_source_save_finish(&io, out);
    --row->busy; qa_source_save_dispose(&io); qa_buffer_free(&saved.input); qa_buffer_free(&saved.stage); qa_buffer_free(&saved.geometry);
    qa_buffer_free(&captured.saved_effects);
    qa_buffer_free(&saved.footsteps);
    return ok;
}
bool frontend_remote_q2_import_read(const frontend_remote_q2 *row, frontend_remote_q2_view *out, qa_error *error)
{
    if (!row || !out || !row->importing || !row->frontend->source_restoring)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 import observation requires its actual isolated cold owner");
    *out = (frontend_remote_q2_view){row, row->options.domain, row->identity, row->loading_generation,
        row->content_generation, row->received_ns, &row->data, row->content, row->map, &row->map_opening,
        &row->frame, &row->previous, row->images, row->materials, row->sounds, row->world, false, row->retired, row->geometry}; return true;
}
bool frontend_remote_q2_restore_prepare(qa_frontend *f, const frontend_remote_q2_options *options,
    const frontend_remote_q2_restore_refs *refs, qa_bytes bytes, frontend_remote_q2 **out, qa_error *error)
{
    if (!f || !f->source_restoring || !options || !refs || !refs->content || !out || *out ||
        f->application != options->domain.application || !options->current || !options->records || !options->disconnected ||
        !options->download_allowed || !options->download_nonce) return false;
    frontend_remote_q2 *row = calloc(1, sizeof(*row)); if (!row) return false;
    row->frontend = f; row->options = *options; row->layout = remote_q2_layout_read(options->domain.protocol);
    row->configs = calloc(row->layout.max_configs, sizeof(*row->configs)); row->importing = true;
    frontend_remote_q2 **tail = &f->remote_q2;
    while (*tail) tail = &(*tail)->next;
    *tail = row; *out = row; qa_catalog_retain(options->domain.catalog);
    saved_q2 saved = {0}; qa_source_save_io io = {0};
    bool ok = row->configs && qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        fields(&io, row, refs, &saved) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    saved.domain.application = options->domain.application; saved.domain.runtime = options->domain.runtime;
    saved.domain.catalog = qa_application_content_catalog(refs->content, saved.domain_catalog);
    saved.domain.console = options->domain.console; saved.domain.cvars = options->domain.cvars;
    if (ok) ok = remote_q2_domain_equal(&saved.domain, &options->domain) &&
        row->identity - QA_FRONTEND_COMMAND_OWNER <= f->next_source_id &&
        options->domain.physical_seat < f->options.seats &&
        ((saved.bound && options->domain.client.owner && options->domain.client.generation && options->domain.epoch) ||
            (!saved.bound && !options->domain.client.owner && !options->domain.epoch));
    for (frontend_remote_q2 *other = f->remote_q2; ok && other; other = other->next)
        if (other != row && other->identity == row->identity) ok = false;
    if (ok && saved.selected) {
        ok = qa_application_content_retain_catalog(refs->content, saved.catalog, &row->content.catalog, error) &&
            qa_application_content_claim_view(refs->content, saved.mounts, &row->content.mounts, error);
        if (ok) {
            row->content.selected_write_root = qa_catalog_product_write_root(row->content.catalog, row->content.selected);
            row->content.base_write_root = qa_catalog_product_write_root(row->content.catalog, row->content.base);
        }
    }
    if (ok && saved.map_resource) {
        row->map = (qa_resource *)qa_application_content_resource(refs->content, saved.map_pool, saved.map_resource);
        ok = row->map != NULL; if (ok) qa_resource_retain(row->map);
    }
    if (ok) ok = retained(row, refs, &saved, error) && qa_input_command_restore(&row->input,
        (qa_bytes){saved.input.data, saved.input.size}, error) && geometry_restore(row, &saved.geometry, error) &&
        (!saved.ready || row->geometry);
    row->bound = saved.bound; row->selected = saved.selected; row->retired = saved.retired;
    row->restore_media_ready = saved.ready;
    if (ok && saved.sounds) ok = qa_audio_bank_create(row->content.mounts, &row->sounds, error);
    if (ok) ok = (!(saved.ready && remote_q2_rerelease_presentation(row)) || saved.footsteps.size) &&
        remote_q2_footsteps_restore(row, refs->content, (qa_bytes){saved.footsteps.data, saved.footsteps.size}, error);
    if (ok && saved.images) { row->images = qa_scene_resources_create_detached(row->content.mounts, error); ok = row->images != NULL; }
    if (ok && saved.materials) { row->materials = qa_material_library_create_detached(row->images, error); ok = row->materials != NULL; }
    if (ok && saved.media) ok = remote_q2_material_movies_prepare_restored(row, error);
    if (ok && saved.fonts) { row->fonts = qa_font_library_create(row->content.mounts, row->images, error); ok = row->fonts != NULL; }
    if (ok && row->download_path) {
        row->download_root = remote_q2_download_destination(row, row->download_path);
        if (row->download_root) qa_fs_root_retain(row->download_root);
        qa_download_request request = {.path = row->download_path, .maximum_bytes = INT32_MAX, .stage_nonce = row->download_logical_nonce};
        qa_download_view view = {.path = row->download_path, .received = row->download_bytes, .limit = INT32_MAX,
            .state = QA_DOWNLOAD_RECEIVING, .stage_nonce = row->download_logical_nonce};
        qa_download_checkpoint_refs downloads = refs->downloads;
        if (!downloads.context && !downloads.resource && !downloads.stage && !downloads.artifact)
            ok = frontend_remote_q2_download_refs(row, &downloads, error);
        ok = ok && row->download_root && downloads.resource && downloads.stage &&
            downloads.resource(downloads.context, &request, &view, true, error) &&
            downloads.stage(downloads.context, &request, &view,
                (qa_bytes){saved.stage.data, saved.stage.size}, &row->download_stage, &row->download_nonce, error) &&
            row->download_stage && row->download_nonce;
    }
    qa_buffer_free(&saved.input); qa_buffer_free(&saved.stage); qa_buffer_free(&saved.geometry);
    qa_buffer_free(&saved.footsteps);
    if (!ok && (!error || error->code == QA_OK)) remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 cold receiver leaves its actual saved owner graph");
    return ok;
}
bool frontend_remote_q2_roots_attach_restored(frontend_remote_q2 *row,
    const frontend_remote_q2_restore_refs *refs, qa_error *error)
{
    if (!row || !row->importing || !row->frontend->source_restoring || row->busy ||
        !refs || !refs->roots || !refs->models) return false;
    uint64_t owner = 0;
    for (size_t i = 0; i < frontend_remote_q2_count(row->frontend); ++i)
        if (frontend_remote_q2_at(row->frontend, i) == row) { owner = i + 1; break; }
    if (!owner) return false;
    frontend_world_source world = {0}; frontend_scene_owner scope = {0};
    if (row->saved_world && (!frontend_world_inventory_world_at(refs->roots, row->saved_world - 1, &world, &scope) ||
        scope.kind != FRONTEND_SCENE_OWNER_REMOTE_Q2_MAP || scope.owner != owner || scope.row != 1 ||
        world.resource != row->map || world.files != row->content.mounts ||
        world.images != row->images || world.materials != row->materials ||
        (row->world ? world.world != row->world : !frontend_world_owner_ready(refs->roots,
            row->saved_world, FRONTEND_SCENE_OWNER_REMOTE_Q2_MAP, owner, error)))) return false;
    size_t ordinal = 0;
    for (remote_q2_model *m = row->models; m; m = m->next, ++ordinal) {
        frontend_model_source parsed = {0}; frontend_scene_root_view root = {0};
        if (!m->saved_model || !m->saved_scene ||
            !frontend_model_source_at(refs->models, m->saved_model - 1, &parsed) ||
            !frontend_world_inventory_model_at(refs->roots, m->saved_scene - 1, &root) ||
            parsed.resource != m->resource || parsed.files != row->content.mounts || parsed.parent ||
            root.source.model != parsed.model || root.source.resource != m->resource ||
            root.source.files != row->content.mounts || root.source.parent ||
            root.owner.kind != FRONTEND_SCENE_OWNER_REMOTE_Q2 || root.owner.owner != owner || root.owner.row != ordinal + 1 ||
            (m->scene ? root.scene != m->scene : !frontend_scene_root_owner_ready(refs->roots,
                m->saved_scene, FRONTEND_SCENE_OWNER_REMOTE_Q2, owner, error))) return false;
        if (m->source_lease) {
            frontend_model_source held = {0};
            if (!frontend_model_lease_source(m->source_lease, &held) || held.model != parsed.model ||
                held.resource != m->resource || held.files != row->content.mounts || m->source != parsed.model) return false;
        } else {
            if (!frontend_model_retain(refs->models, parsed.model, &m->source_lease, error)) return false;
            m->source = parsed.model;
        }
    }
    if (row->saved_world && !row->world) {
        row->world = (qa_scene_world *)world.world; frontend_world_adopt(refs->roots, row->saved_world);
    }
    for (remote_q2_model *m = row->models; m; m = m->next) if (!m->scene) {
        qa_scene_model *scene = NULL;
        if (!frontend_scene_root_decode(refs->roots, m->saved_scene, &scene, error)) return false;
        m->scene = scene; frontend_scene_root_adopt(refs->roots, m->saved_scene);
    }
    for (remote_q2_model *m = row->models; m; m = m->next)
        if (!remote_q2_model_scope_current(row, m, error)) return false;
    return true;
}
bool frontend_remote_q2_restore_finish(frontend_remote_q2 *row,
    const frontend_remote_q2_restore_refs *refs, qa_error *error)
{
    if (!row || !row->importing || !refs || !refs->content || !refs->models || !refs->roots || !refs->scene || row->busy) return false;
    if (row->saved_world && !row->world) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 saved world has not been adopted by its actual root owner");
    for (remote_q2_model *m = row->models; m; m = m->next) {
        if (!m->scene || !m->source || !m->source_lease || !remote_q2_model_scope_current(row, m, error))
            return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 saved model has not retained its actual parsed/root holder");
    }
    for (remote_q2_picture *p = row->pictures; p; p = p->next) {
        const qa_scene_image *image = NULL;
        if (!frontend_scene_image_decode(refs->scene, p->saved_image, &image, error) || !image) return false;
        if (p->image && p->image != image) return false;
        if (!p->image) { qa_scene_image_retain(image); p->image = image; }
    }
    const qa_scene_image *white = NULL;
    if (!frontend_font_decode(row->frontend, row->saved_classic, &row->classic, error) ||
        !frontend_scene_image_decode(refs->scene, row->saved_white, &white, error)) return false;
    if (row->white && row->white != white) return false;
    if (white && !row->white) { qa_scene_image_retain(white); row->white = white; }
    if (!remote_q2_footsteps_current(row, error)) return false;
    if (row->saved_effects.size && !row->effects_imported) {
        frontend_remote_q2_effects_source source;
        effects_refs context = {row, refs}; frontend_remote_q2_effects_refs effects = effect_refs(&context);
        if (!frontend_remote_q2_effects_destroy(&row->effects, error) || !remote_q2_effects_source_read(row, &source, error) ||
            !frontend_remote_q2_effects_restore(&source, &effects,
            (qa_bytes){row->saved_effects.data, row->saved_effects.size}, &row->effects, error)) return false;
        row->effects_imported = true;
    }
    if (row->restore_media_ready && !row->effects)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 ready media has no retained actual effects owner");
    if (row->retiring && !remote_q2_retirement_current(row, error)) return false;
    if (row->bound && !row->retired && !row->retiring && (!row->options.current(row->options.context, &row->options.domain, error) ||
        qa_network_epoch(row->options.domain.runtime, row->options.domain.client) != row->options.domain.epoch)) return false;
    row->media_ready = row->restore_media_ready; row->restore_media_ready = false;
    qa_buffer_free(&row->saved_effects);
    row->effects_imported = false;
    row->importing = false; return true;
}

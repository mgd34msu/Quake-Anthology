#include "internal.h"

void q3p_model_free(q3p_model *model)
{
    if (!model) return;
    for (unsigned i = 0; i < 3; ++i) {
        bool shared = false;
        for (unsigned j = 0; j < i; ++j) if (model->scene[i] == model->scene[j]) shared = true;
        if (!shared && !model->borrowed_scenes) qa_scene_model_destroy(model->scene[i]);
    }
    if (model->owns_world && !model->borrowed_world) qa_scene_world_destroy(model->world);
    for (unsigned i = 0; i < 3; ++i)
        if (model->source_leases[i].release) model->source_leases[i].release(model->source_leases[i].context);
    if (!model->borrowed_models) {
        qa_model_lods_free(&model->lods); qa_model_free(&model->model);
    } else for (unsigned i = 0; i < 3; ++i) free(model->lods.paths[i]);
    for (unsigned i = 0; i < 3; ++i) {
        qa_vfs_acquisition_dispose(&model->lod_openings[i]);
        free(model->lod_opening_orders[i].mounts); free(model->lod_opening_orders[i].prefix);
        qa_resource_release(model->lod_resources[i]);
    }
    qa_vfs_acquisition_dispose(&model->opening); free(model->first_requested_path);
    free(model->opening_order.mounts); free(model->opening_order.prefix);
    qa_resource_release(model->resource); free(model);
}

static bool model_opening(const q3p_model *m, uint32_t slot,
    qa_q3_model_opening *out, qa_error *error)
{
    *out = (qa_q3_model_opening){0};
    if (!m) return true;
    const qa_resource *resource = m->resource;
    const qa_vfs_acquisition *opening = resource ? &m->opening : NULL;
    int64_t rank = m->opening_rank;
    const q3p_opening_order *order = &m->opening_order;
    if (slot != QA_Q3_MODEL_PRIMARY_OPENING) {
        if (!m->has_lods) { if (slot || m->world) return true; }
        else {
            unsigned target = slot;
            for (unsigned depth = 0; depth < 3 && m->lods.states[target] == QA_MODEL_LOD_ALIAS; ++depth) {
                if (m->lods.aliases[target] >= 3)
                    return q3p_fail(error, QA_ERROR_FORMAT, "Q3 model opening has an invalid LOD alias");
                target = m->lods.aliases[target];
            }
            if (m->lods.states[target] != QA_MODEL_LOD_LOADED) return true;
            resource = m->lod_resources[target]; opening = &m->lod_openings[target];
            rank = m->lod_opening_ranks[target];
            order = &m->lod_opening_orders[target];
        }
    }
    *out = (qa_q3_model_opening){.present = true, .first_requested_path = m->first_requested_path,
        .provider = m->provider, .resource = resource, .receipt = opening, .rank = rank,
        .order = order->mounts, .order_count = order->count, .prefix = order->prefix,
        .user_overlay = order->user_overlay};
    return true;
}

bool qa_q3_assets_model_opening(const qa_q3_presentation_assets *a, size_t ordinal,
    uint32_t slot, qa_q3_model_opening *out, qa_error *error)
{
    if (!a || !out || (a->busy && (!a->capturing || a->codec_busy)) ||
        ordinal >= a->model_count || (slot != QA_Q3_MODEL_PRIMARY_OPENING && slot >= 3))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 model opening requires an idle or captured holder");
    return model_opening(a->models[ordinal], slot, out, error);
}

static bool opening_rank(const qa_vfs *files, const qa_vfs_acquisition *opening,
    int64_t *out, q3p_opening_order *snapshot, qa_error *error)
{
    if (!files || !opening->opening_present)
        return q3p_fail(error, QA_ERROR_FORMAT, "Q3 model acquisition lacks its actual opening snapshot");
    const qa_vfs_read_opening *admitted = &opening->opening;
    size_t count = admitted->order_count;
    if (count > SIZE_MAX / sizeof(*snapshot->mounts) || (count && !admitted->order))
        return q3p_fail(error, QA_ERROR_FORMAT, "Q3 model opening order is incomplete");
    snapshot->mounts = count ? malloc(count * sizeof(*snapshot->mounts)) : NULL;
    if (count && !snapshot->mounts) return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Q3 model opening order");
    snapshot->count = count; snapshot->user_overlay = admitted->user_overlay;
    if (count) memcpy(snapshot->mounts, admitted->order, count * sizeof(*snapshot->mounts));
    if (admitted->prefix) {
        size_t length = strlen(admitted->prefix);
        snapshot->prefix = length < SIZE_MAX ? malloc(length + 1) : NULL;
        if (!snapshot->prefix) return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Q3 model opening prefix");
        memcpy(snapshot->prefix, admitted->prefix, length + 1);
    }
    *out = admitted->rank; return true;
}

const qa_model *q3p_model_source(const q3p_model *model, uint32_t slot)
{
    if (!model || model->world) return NULL;
    if (!model->has_lods) return model->borrowed_models ? model->sources[0] : &model->model;
    if (slot >= 3) return NULL;
    return model->borrowed_models ? model->sources[slot] : qa_model_at_lod(&model->lods, slot);
}

bool q3p_model_get(const qa_q3_presentation_assets *a, int32_t handle,
                    const q3p_model **out, qa_error *error)
{
    if (!a || !out || handle < 0 || (size_t)handle > a->model_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 model handle");
    *out = handle ? a->models[handle - 1] : NULL; return true;
}

static qa_bounds bounds(const qa_model_bounds *value)
{
    return (qa_bounds){{value->min[0], value->min[1], value->min[2]},
                       {value->max[0], value->max[1], value->max[2]}};
}

typedef struct lod_reader { q3p_model *model; unsigned next_slot; } lod_reader;
static bool read_lod(void *context, const char *path, qa_buffer *out, qa_error *error)
{
    lod_reader *reader = context;
    if (reader->next_slot >= 3)
        return q3p_fail(error, QA_ERROR_FORMAT, "Q3 model loader repeated its physical LOD read");
    unsigned slot = 2 - reader->next_slot++;
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire_receipt(reader->model->provider.mounts, path, &resource,
        &reader->model->lod_openings[slot], error)) return false;
    if (!opening_rank(reader->model->provider.mounts, &reader->model->lod_openings[slot],
        &reader->model->lod_opening_ranks[slot], &reader->model->lod_opening_orders[slot], error)) {
        qa_resource_release(resource); return false;
    }
    qa_bytes source = qa_resource_bytes(resource);
    uint8_t *copy = source.size ? malloc(source.size) : NULL;
    if (source.size && !copy) {
        qa_resource_release(resource);
        return q3p_fail(error, QA_ERROR_MEMORY, "retaining decoded model LOD source");
    }
    if (source.size) memcpy(copy, source.data, source.size);
    *out = (qa_buffer){copy, source.size}; reader->model->lod_resources[slot] = resource; return true;
}

static bool extension(const char *path, const char *suffix)
{
    size_t a = strlen(path), b = strlen(suffix);
    if (a < b) return false;
    for (size_t i = 0; i < b; ++i) {
        unsigned char byte = (unsigned char)path[a - b + i];
        if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        if (byte != (unsigned char)suffix[i]) return false;
    }
    return true;
}

static bool decode(q3p_model *model, const char *path, qa_error *error)
{
    qa_bytes bytes = qa_resource_bytes(model->resource);
    qa_scene_image_options images = {.family = model->provider.family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
        .usage = QA_IMAGE_USAGE_SKIN, .transparent_index = -1};
    if (extension(path, ".bsp")) {
        qa_bsp_view bsp; qa_bsp_model first;
        qa_scene_world_options options = {.images = images, .subdivisions = 4,
            .q1_water_alpha = 1, .q2_light_modulate = 1};
        if (!qa_bsp_open(bytes, &bsp, error) || !qa_bsp_read_model(&bsp, 0, &first, error)) return false;
        options.images.usage = QA_IMAGE_USAGE_WALL;
        if (!qa_scene_world_create(&bsp, model->provider.images, model->provider.materials,
                                     &options, &model->world, error)) return false;
        model->owns_world = true;
        model->bounds = (qa_bounds){first.bounds.min, first.bounds.max}; return true;
    }
    if (bytes.size >= 4 && !memcmp(bytes.data, "IDP3", 4)) {
        lod_reader reader = {model, 0};
        if (!qa_model_md3_lods(path, read_lod, &reader, &model->lods, error)) return false;
        model->has_lods = true;
        const qa_model *base = qa_model_at_lod(&model->lods, 0);
        if (!base) return q3p_fail(error, QA_ERROR_FORMAT, "registered MD3 has no primary LOD");
        model->bounds = bounds(base->frame_count ? &base->frames[0].bounds : &base->bounds);
        for (unsigned i = 0; i < 3; ++i) {
            const qa_model *lod = qa_model_at_lod(&model->lods, i);
            for (unsigned j = 0; lod && j < i; ++j)
                if (qa_model_at_lod(&model->lods, j) == lod) { model->scene[i] = model->scene[j]; break; }
            if (lod && !model->scene[i] && !qa_scene_model_create(lod, model->provider.images, model->provider.materials,
                                               &images, &model->scene[i], error)) return false;
        }
        return true;
    }
    if (!qa_model_load(bytes, &model->model, error)) return false;
    model->bounds = bounds(&model->model.bounds);
    return qa_scene_model_create(&model->model, model->provider.images, model->provider.materials,
                                  &images, &model->scene[0], error);
}

bool qa_q3_register_model(qa_q3_presentation_assets *a, const char *path,
                            int32_t *out, qa_error *error)
{
    if (!a || a->busy || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 model request");
    if (!path || !*path) { *out = 0; return true; }
    q3p_name *prior = q3p_find_name(a, Q3P_MODEL, path);
    if (prior) { *out = prior->handle; return true; }
    char *normalized = NULL;
    if (path[0] != '*') {
        qa_error local = {0};
        normalized = qa_vfs_normalize_path(path, &local);
        if (!normalized) {
            if (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT) {
                if (!q3p_add_name(a, Q3P_MODEL, path, 0, false, error)) return false;
                *out = 0; return true;
            }
            if (error) *error = local;
            return false;
        }
    }
    ++a->busy;
    q3p_model *model = calloc(1, sizeof(*model));
    bool ok = model != NULL;
    if (!ok) q3p_fail(error, QA_ERROR_MEMORY, "allocating Q3 model resource");
    int32_t handle = 0;
    if (ok && path[0] == '*') {
        double index = 0;
        const char *number = path + 1;
        while (*number == ' ' || (*number >= '\t' && *number <= '\r')) ++number;
        if (!a->geometry || !a->world ||
            (*number && !qa_parse_number((qa_bytes){(const uint8_t *)number, strlen(number)}, &index, error)) ||
            !isfinite(index) || index < 0 || index > UINT32_MAX || trunc(index) != index)
            ok = q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 inline model name");
        if (ok) {
            model->inline_model = (uint32_t)index; model->world = a->world;
            model->provider = a->options.provider;
            ok = qa_collision_model_bounds(a->geometry, model->inline_model, &model->bounds, error);
        }
        if (ok) for (size_t i = 0; i < a->model_count; ++i) {
            const q3p_model *existing = a->models[i];
            if (!existing) continue;
            if (existing->world == model->world && !existing->owns_world &&
                existing->inline_model == model->inline_model) { handle = (int32_t)i + 1; break; }
        }
    } else if (ok) {
        qa_error local = {0};
        ok = q3p_select(a, normalized, QA_Q3_ASSET_MODEL, &model->provider, &local) &&
             qa_vfs_acquire_receipt(model->provider.mounts, normalized, &model->resource, &model->opening, &local);
        if (ok) ok = opening_rank(model->provider.mounts, &model->opening, &model->opening_rank,
            &model->opening_order, &local);
        if (!ok && local.code == QA_ERROR_NOT_FOUND) { ok = true; q3p_model_free(model); model = NULL; }
        else if (!ok && error) *error = local;
        if (ok && model) {
            for (size_t i = 0; i < a->model_count; ++i) {
                const q3p_model *existing = a->models[i];
                if (!existing) continue;
                if (existing->resource == model->resource && existing->provider.images == model->provider.images &&
                    existing->provider.materials == model->provider.materials) { handle = (int32_t)i + 1; break; }
            }
            if (!handle) ok = decode(model, normalized, error);
        }
    }
    if (ok && model && !handle) {
        size_t length = strlen(path);
        model->first_requested_path = length < SIZE_MAX ? malloc(length + 1) : NULL;
        if (!model->first_requested_path) ok = q3p_fail(error, QA_ERROR_MEMORY, "Retaining first Q3 model request");
        else memcpy(model->first_requested_path, path, length + 1);
    }
    if (ok && model && !handle && a->options.model_initialize) {
        for (uint32_t slot = 0; ok && slot < 3; ++slot) {
            if (!model->scene[slot]) continue;
            bool shared = false;
            for (uint32_t previous = 0; previous < slot; ++previous)
                if (model->scene[previous] == model->scene[slot]) shared = true;
            if (shared) continue;
            qa_q3_model_opening opening;
            ok = model_opening(model, slot, &opening, error);
            if (ok && !opening.present)
                ok = q3p_fail(error, QA_ERROR_FORMAT, "Q3 scene model lacks its actual first opening");
            if (ok) ok = a->options.model_initialize(a->options.context, &opening,
                q3p_model_source(model, slot), model->scene[slot], error);
        }
    }
    if (ok && model && !handle) {
        ok = a->model_count < INT32_MAX && q3p_reserve((void **)&a->models, &a->model_capacity,
            a->model_count + 1, sizeof(*a->models), error);
        if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_MEMORY, "Q3 model handle capacity exceeded");
        if (ok) handle = (int32_t)a->model_count + 1;
    }
    if (ok) ok = q3p_add_name(a, Q3P_MODEL, path, handle, false, error);
    if (ok) {
        if (model && (size_t)handle > a->model_count) { a->models[a->model_count++] = model; model = NULL; }
        *out = handle;
    }
    q3p_model_free(model); free(normalized); --a->busy; return ok;
}

bool qa_q3_presentation_model_bounds(const qa_q3_presentation_assets *a, int32_t handle,
                                      qa_bounds *out, qa_error *error)
{
    const q3p_model *model;
    if (!out || !q3p_model_get(a, handle, &model, error)) return false;
    *out = model ? model->bounds : (qa_bounds){0}; return true;
}

bool qa_q3_presentation_model_has_tags(const qa_q3_presentation_assets *a, int32_t handle,
                                        bool *out, qa_error *error)
{
    const q3p_model *model;
    if (!out || !q3p_model_get(a, handle, &model, error)) return false;
    const qa_model *source = model && model->has_lods ? q3p_model_source(model, 0) : NULL;
    *out = source && source->format == QA_MODEL_MD3 && source->tag_count && source->frame_count;
    return true;
}

bool qa_q3_presentation_tag(const qa_q3_presentation_assets *a, int32_t handle, const char *name,
                            int32_t first, int32_t second, float fraction, qa_model_tag *out,
                            bool *found, qa_error *error)
{
    const q3p_model *model;
    if (!out || !found || !q3p_model_get(a, handle, &model, error)) return false;
    const qa_model *source = model && model->has_lods ? q3p_model_source(model, 0) : NULL;
    bool match = source && name && first >= 0 && second >= 0 &&
        qa_model_lerp_tag(source, name, (uint32_t)first, (uint32_t)second, fraction, out);
    if (!match) *out = (qa_model_tag){.axes = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    *found = match; return true;
}

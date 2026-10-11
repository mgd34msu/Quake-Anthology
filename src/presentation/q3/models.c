#include "internal.h"
#include "qa/scene_world_save.h"
#include "qa/scene_resource_save.h"
#include "qa/binary.h"
#include "qa/q3_assets_custody.h"

void q3p_model_free(q3p_model *model)
{
    if (!model) return;
    if (!model->borrowed_scenes) qa_scene_model_destroy(model->source_md4_scene);
    if (model->source_md4_lease.release) model->source_md4_lease.release(model->source_md4_lease.context);
    if (!model->borrowed_models) qa_model_free(&model->source_md4_model);
    qa_resource_release(model->source_md4_resource);
    qa_vfs_acquisition_dispose(&model->source_md4_opening);
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
        qa_resource_release(model->lod_resources[i]);
    }
    qa_vfs_acquisition_dispose(&model->opening); free(model->first_requested_path);
    qa_resource_release(model->resource); free(model);
}

static bool model_opening(const q3p_model *m, uint32_t slot,
    qa_q3_model_opening *out, qa_error *error)
{
    *out = (qa_q3_model_opening){0};
    if (!m) return true;
    const qa_resource *resource = m->resource;
    const qa_vfs_acquisition *opening = resource ? &m->opening : NULL;
    if (slot == QA_Q3_MODEL_MD4_OPENING) {
        if (!m->source_md4_resource) return true;
        resource = m->source_md4_resource; opening = &m->source_md4_opening;
    } else if (slot != QA_Q3_MODEL_PRIMARY_OPENING) {
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
        }
    }
    const qa_vfs_read_opening *snapshot = opening ? &opening->opening : NULL;
    *out = (qa_q3_model_opening){.present = true, .first_requested_path = m->first_requested_path,
        .provider = m->provider, .resource = resource, .receipt = opening,
        .rank = snapshot ? snapshot->rank : 0, .order = snapshot ? snapshot->order : NULL,
        .order_count = snapshot ? snapshot->order_count : 0, .prefix = snapshot ? snapshot->prefix : NULL,
        .user_overlay = snapshot && snapshot->user_overlay};
    return true;
}

bool qa_q3_assets_model_opening(const qa_q3_presentation_assets *a, size_t ordinal,
    uint32_t slot, qa_q3_model_opening *out, qa_error *error)
{
    if (!a || !out || (a->busy && (!a->capturing || a->codec_busy)) ||
        ordinal >= a->model_count || (slot != QA_Q3_MODEL_PRIMARY_OPENING && slot > QA_Q3_MODEL_MD4_OPENING))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 model opening requires an idle or captured holder");
    return model_opening(a->models[ordinal], slot, out, error);
}

const qa_model *q3p_model_source(const q3p_model *model, uint32_t slot)
{
    if (!model || model->world) return NULL;
    if (!model->has_lods) return model->borrowed_models ? model->sources[0] : &model->model;
    if (slot >= 3) return NULL;
    return model->borrowed_models ? model->sources[slot] : qa_model_at_lod(&model->lods, slot);
}
const qa_model *q3p_model_md4_source(const q3p_model *model)
{
    if (!model || !model->source_registration || !model->source_md4_resource) return NULL;
    return model->borrowed_models ? model->source_md4 : &model->source_md4_model;
}

bool q3p_model_get(const qa_q3_presentation_assets *a, int32_t handle,
                    const q3p_model **out, qa_error *error)
{
    if (!a || !out) {
        q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 model lookup");
        return false;
    }
    if ((handle < 0 || (size_t)handle > a->model_count) &&
        qa_material_library_has_source_profile(a->options.provider.materials)) {
        *out = NULL; return true;
    }
    if (handle < 0 || (size_t)handle > a->model_count) {
        q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 model handle");
        return false;
    }
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

static bool decode(qa_q3_presentation_assets *assets, q3p_model *model, const char *path, qa_error *error)
{
    qa_bytes bytes = qa_resource_bytes(model->resource);
    qa_scene_image_options images = {.family = model->provider.family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
        .usage = QA_IMAGE_USAGE_SKIN, .transparent_index = -1};
    if (extension(path, ".bsp")) {
        qa_bsp_view bsp;
        qa_world *owner = model->provider.geometry_owner;
        qa_collision_geometry *geometry = assets->geometry &&
            qa_collision_resource(assets->geometry) == model->resource ? assets->geometry :
            qa_world_resource_geometry(owner, model->resource);
        bool created = geometry == NULL;
        if (!qa_bsp_open(bytes, &bsp, error)) return false;
        bool okay = !created || (qa_collision_create(&bsp, &geometry, error) &&
            qa_collision_bind_resource(geometry, model->resource, error) &&
            (!owner || qa_world_prepare_trace_geometry(owner, geometry, error)));
        qa_scene_world_options options = {.geometry = geometry, .images = images, .subdivisions = 4,
            .q1_water_alpha = 1, .q2_light_modulate = 1};
        options.images.usage = QA_IMAGE_USAGE_WALL;
        if (okay) okay = qa_scene_world_create(&bsp, model->provider.images, model->provider.materials,
            &options, &model->world, error);
        if (created) qa_collision_destroy(geometry);
        model->owns_world = model->world != NULL;
        return okay && qa_scene_world_source_resource_bind(model->world, model->resource, error) &&
            qa_collision_model_bounds(options.geometry, 0, &model->bounds, error);
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
                                               &images, assets->options.strings, &model->scene[i], error)) return false;
        }
        return true;
    }
    if (!qa_model_load(bytes, &model->model, error)) return false;
    model->bounds = bounds(&model->model.bounds);
    return qa_scene_model_create(&model->model, model->provider.images, model->provider.materials,
                                  &images, assets->options.strings, &model->scene[0], error);
}

static bool source_primary(q3p_model *model, unsigned slot, qa_error *error)
{
    qa_vfs_acquisition opening = {0};
    if (!qa_vfs_acquisition_copy(&model->lod_openings[slot], &opening, error)) return false;
    qa_resource_retain(model->lod_resources[slot]); qa_resource_release(model->resource);
    qa_vfs_acquisition_dispose(&model->opening);
    model->resource = model->lod_resources[slot]; model->opening = opening;
    return true;
}
static bool source_model_lods(qa_q3_presentation_assets *assets, q3p_model *model,
    const char *path, qa_error *error)
{
    model->source_registration = true; model->has_lods = true;
    model->source_kind = QA_MODEL_MD3; model->registration_bad = true;
    size_t length = strlen(path); const char *dot = strrchr(path, '.');
    size_t stem = dot ? (size_t)(dot - path) : length;
    if (length == SIZE_MAX || stem > SIZE_MAX - 7)
        return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source model LOD requests");
    for (unsigned slot = 0; slot < 3; ++slot) {
        model->lods.paths[slot] = malloc(slot ? stem + 7 : length + 1);
        if (!model->lods.paths[slot]) return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source model LOD requests");
        if (!slot) memcpy(model->lods.paths[slot], path, length + 1);
        else {
            memcpy(model->lods.paths[slot], path, stem);
            memcpy(model->lods.paths[slot] + stem, slot == 1 ? "_1.md3" : "_2.md3", 7);
        }
    }
    qa_scene_image_options images = {.family = model->provider.family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
        .usage = QA_IMAGE_USAGE_SKIN, .transparent_index = -1};
    int failed_slot = -1;
    for (int slot = 2; slot >= 0; --slot) {
        qa_error observed = {0};
        if (!qa_vfs_acquire_receipt(model->provider.mounts, model->lods.paths[slot],
            &model->lod_resources[slot], &model->lod_openings[slot], &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = observed;
            return false;
        }
        model->lods.load_order[model->lods.load_count++] = (uint32_t)slot;
        if (!model->resource && !source_primary(model, (unsigned)slot, error)) return false;
        qa_bytes bytes = qa_resource_bytes(model->lod_resources[slot]);
        bool md3 = bytes.size >= 4 && !memcmp(bytes.data, "IDP3", 4);
        bool md4 = bytes.size >= 4 && !memcmp(bytes.data, "IDP4", 4);
        if (!md3 && !md4) {
            if (assets->options.print) assets->options.print(assets->options.context, "RE_RegisterModel: unknown fileid\n");
            return true;
        }
        qa_model decoded = {0};
        if (!qa_model_load(bytes, &decoded, &observed)) {
            if (observed.code == QA_ERROR_MEMORY) { if (error) *error = observed; return false; }
            model->lods.states[slot] = QA_MODEL_LOD_INVALID;
            if (!slot) return true;
            failed_slot = slot; break;
        }
        qa_model *retained;
        qa_scene_model **scene;
        if (md3) {
            model->lods.models[slot] = decoded; retained = &model->lods.models[slot];
            model->lods.states[slot] = QA_MODEL_LOD_LOADED; scene = &model->scene[slot];
        } else {
            qa_scene_model_destroy(model->source_md4_scene); model->source_md4_scene = NULL;
            qa_model_free(&model->source_md4_model); model->source_md4_model = decoded;
            retained = &model->source_md4_model; scene = &model->source_md4_scene;
            qa_resource_release(model->source_md4_resource);
            model->source_md4_resource = model->lod_resources[slot]; qa_resource_retain(model->source_md4_resource);
            qa_vfs_acquisition_dispose(&model->source_md4_opening);
            if (!qa_vfs_acquisition_copy(&model->lod_openings[slot], &model->source_md4_opening, error)) return false;
            model->source_md4_slots[slot] = true;
        }
        /* Material admission is reached before the next file read, preserving
         * the Source physical shader registration order. */
        if (!qa_scene_model_create(retained, model->provider.images, model->provider.materials,
            &images, assets->options.strings, scene, error) || !source_primary(model, (unsigned)slot, error)) return false;
        model->source_kind = md3 ? QA_MODEL_MD3 : QA_MODEL_MD4;
        model->lods.lod_count = ++model->source_num_lods;
        size_t allocation = qa_load_u32le(bytes.data + (md3 ? 104 : 96));
        if (allocation > SIZE_MAX - model->lods.byte_length)
            return q3p_fail(error, QA_ERROR_MEMORY, "Source model allocation total overflows");
        model->lods.byte_length += allocation;
    }
    if (!model->source_num_lods) return true;
    if (failed_slot > 0) for (int slot = failed_slot - 1; slot >= 0; --slot) {
        model->lods.states[slot] = QA_MODEL_LOD_ALIAS; model->lods.aliases[slot] = (uint32_t)slot + 1;
        model->scene[slot] = model->scene[slot + 1]; ++model->source_num_lods;
    }
    model->lods.lod_count = model->source_num_lods;
    model->registration_bad = false;
    const qa_model *base = qa_model_at_lod(&model->lods, 0);
    model->bounds = base ? bounds(base->frame_count ? &base->frames[0].bounds : &base->bounds) : (qa_bounds){0};
    return true;
}

static bool source_bad_model(qa_q3_presentation_assets *a, const char *path,
    int32_t *out, qa_error *error)
{
    if (!q3p_reserve((void **)&a->models, &a->model_capacity,
        a->model_count + 1, sizeof(*a->models), error) ||
        !q3p_add_name(a, Q3P_MODEL, path, 0, false, error)) return false;
    a->models[a->model_count++] = NULL;
    *out = 0; return true;
}
bool qa_q3_register_model(qa_q3_presentation_assets *a, const char *path,
                            int32_t *out, qa_error *error)
{
    if (!a || a->busy || a->retired || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 model request");
    if (!path || !*path) { *out = 0; return true; }
    bool source_model = qa_material_library_has_source_profile(a->options.provider.materials);
    if (source_model && strlen(path) >= 64) {
        if (a->options.print) a->options.print(a->options.context, "Model name exceeds MAX_QPATH\n");
        *out = 0; return true;
    }
    q3p_name *prior = q3p_find_name(a, Q3P_MODEL, path);
    if (prior) { *out = prior->handle; return true; }
    if (source_model && a->model_count >= 1023) {
        if (a->options.print) a->options.print(a->options.context, "RE_RegisterModel: R_AllocModel() failed\n");
        *out = 0; return true;
    }
    char *normalized = NULL;
    if (path[0] != '*') {
        qa_error local = {0};
        normalized = qa_vfs_normalize_path(path, &local);
        if (!normalized) {
            if (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT) {
                if (source_model) return source_bad_model(a, path, out, error);
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
    int32_t handle = 0, physical_handle = 0;
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
            ok = qa_collision_model_bounds(a->geometry, model->inline_model, &model->bounds, error);
            qa_scene_world_options options;
            if (ok && !qa_scene_world_options_read(model->world, &options))
                ok = q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 inline model requires its actual observed map owner");
            if (ok) {
                model->provider.images = qa_scene_world_resource_owner(model->world);
                model->provider.materials = qa_scene_world_material_owner(model->world);
                model->provider.mounts = qa_scene_resources_files(model->provider.images);
                model->provider.family = options.images.family;
                ok = qa_q3_assets_provider_hold(a, &model->provider, error);
            }
        }
        if (ok && !source_model) for (size_t i = 0; i < a->model_count; ++i) {
            const q3p_model *existing = a->models[i];
            if (!existing) continue;
            if (existing->world == model->world && !existing->owns_world &&
                existing->inline_model == model->inline_model) { handle = (int32_t)i + 1; break; }
        }
    } else if (ok) {
        qa_error local = {0};
        bool source_lods = source_model && !extension(normalized, ".bsp") &&
            !extension(normalized, ".mdl") && !extension(normalized, ".md2") &&
            !extension(normalized, ".spr") && !extension(normalized, ".sp2") && !extension(normalized, ".md5mesh");
        ok = q3p_select(a, normalized, QA_Q3_ASSET_MODEL, &model->provider, &local);
        if (ok && source_lods) ok = source_model_lods(a, model, normalized, &local);
        else if (ok) ok = qa_vfs_acquire_receipt(model->provider.mounts, normalized,
            &model->resource, &model->opening, &local);
        if (!ok && local.code == QA_ERROR_NOT_FOUND) { ok = true; q3p_model_free(model); model = NULL; }
        else if (!ok && error) *error = local;
        if (ok && model) {
            for (size_t i = 0; !source_model && i < a->model_count; ++i) {
                const q3p_model *existing = a->models[i];
                if (!existing) continue;
                if (existing->resource == model->resource && existing->provider.images == model->provider.images &&
                    existing->provider.materials == model->provider.materials) { handle = (int32_t)i + 1; break; }
            }
            if (!handle && !source_lods) {
                qa_error decoded = {0};
                ok = decode(a, model, normalized, &decoded);
                if (!ok && source_model && (decoded.code == QA_ERROR_NOT_FOUND ||
                    decoded.code == QA_ERROR_FORMAT || decoded.code == QA_ERROR_UNSUPPORTED)) {
                    q3p_model_free(model); model = NULL; ok = true;
                } else if (!ok && error) *error = decoded;
            }
        }
    }
    if (ok && source_model && !model) {
        ok = source_bad_model(a, path, out, error);
        free(normalized); --a->busy; return ok;
    }
    if (ok && model && !handle) {
        size_t length = strlen(path);
        model->first_requested_path = length < SIZE_MAX ? malloc(length + 1) : NULL;
        if (!model->first_requested_path) ok = q3p_fail(error, QA_ERROR_MEMORY, "Retaining first Q3 model request");
        else memcpy(model->first_requested_path, path, length + 1);
    }
    if (ok && model && !handle && !model->registration_bad && a->options.model_initialize) {
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
        if (ok && model->source_md4_scene) {
            qa_q3_model_opening opening;
            ok = model_opening(model, QA_Q3_MODEL_MD4_OPENING, &opening, error);
            if (ok) ok = a->options.model_initialize(a->options.context, &opening,
                q3p_model_md4_source(model), model->source_md4_scene, error);
        }
    }
    if (ok && model && !handle) {
        ok = a->model_count < INT32_MAX && q3p_reserve((void **)&a->models, &a->model_capacity,
            a->model_count + 1, sizeof(*a->models), error);
        if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_MEMORY, "Q3 model handle capacity exceeded");
        if (ok) { physical_handle = (int32_t)a->model_count + 1; handle = model->registration_bad ? 0 : physical_handle; }
    }
    if (ok) ok = q3p_add_name(a, Q3P_MODEL, path, handle, false, error);
    if (ok) {
        if (model && physical_handle) { a->models[a->model_count++] = model; model = NULL; }
        *out = handle;
    }
    q3p_model_free(model); free(normalized); --a->busy; return ok;
}

bool qa_q3_presentation_model_bounds(const qa_q3_presentation_assets *a, int32_t handle,
                                      qa_bounds *out, qa_error *error)
{
    const q3p_model *model;
    if (!out || !q3p_model_get(a, handle, &model, error)) return false;
    const qa_model *source = q3p_model_source(model, 0);
    if (model && model->source_registration) {
        *out = source ? bounds(source->frame_count ? &source->frames[0].bounds : &source->bounds) : (qa_bounds){0};
        return true;
    }
    if (source && source->format == QA_MODEL_MD4 &&
        qa_material_library_has_source_profile(a->options.provider.materials)) {
        *out = (qa_bounds){0}; return true;
    }
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

#include "internal.h"
#include "qa/scene_model_save.h"
#include "qa/scene_world_save.h"
#include "qa/q3_assets_custody.h"

static unsigned char key_byte(q3p_resource_kind kind, unsigned char byte)
{
    return kind == Q3P_SHADER && byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
}

uint64_t q3p_name_hash(q3p_resource_kind kind, const char *name)
{
    uint64_t hash = UINT64_C(1469598103934665603) ^ (uint64_t)kind;
    for (size_t i = 0; name[i]; ++i) hash = (hash ^ key_byte(kind, (unsigned char)name[i])) * UINT64_C(1099511628211);
    return hash;
}

q3p_name *q3p_find_name(qa_q3_presentation_assets *a, q3p_resource_kind kind, const char *name)
{
    if (!a->name_capacity) return NULL;
    uint64_t hash = q3p_name_hash(kind, name);
    for (q3p_name *entry = a->names[hash & (a->name_capacity - 1)]; entry; entry = entry->next) {
        if (entry->hash != hash || entry->kind != kind) continue;
        size_t i = 0;
        while (entry->name[i] && name[i] && (unsigned char)entry->name[i] == key_byte(kind, (unsigned char)name[i])) ++i;
        if (!entry->name[i] && !name[i]) return entry;
    }
    return NULL;
}

bool q3p_add_name(qa_q3_presentation_assets *a, q3p_resource_kind kind, const char *name,
                    int32_t handle, bool option, qa_error *error)
{
    q3p_name *prior = q3p_find_name(a, kind, name);
    if (prior) { prior->handle = handle; return true; }
    size_t length = strlen(name);
    if (length > SIZE_MAX - sizeof(q3p_name) - 1)
        return q3p_fail(error, QA_ERROR_MEMORY, "Q3 resource name exceeds capacity");
    q3p_name *entry = malloc(sizeof(*entry) + length + 1);
    if (!entry) return q3p_fail(error, QA_ERROR_MEMORY, "retaining Q3 resource name");
    if (a->name_count >= a->name_capacity - a->name_capacity / 4) {
        size_t capacity = a->name_capacity ? a->name_capacity * 2 : 32;
        if (capacity < a->name_capacity || capacity > SIZE_MAX / sizeof(*a->names)) {
            free(entry); return q3p_fail(error, QA_ERROR_MEMORY, "Q3 resource name index exceeds capacity");
        }
        q3p_name **table = calloc(capacity, sizeof(*table));
        if (!table) { free(entry); return q3p_fail(error, QA_ERROR_MEMORY, "growing Q3 resource name index"); }
        for (size_t i = 0; i < a->name_capacity; ++i) {
            q3p_name *next;
            for (q3p_name *row = a->names[i]; row; row = next) {
                next = row->next; size_t bucket = row->hash & (capacity - 1);
                row->next = table[bucket]; table[bucket] = row;
            }
        }
        free(a->names); a->names = table; a->name_capacity = capacity;
    }
    *entry = (q3p_name){.kind = kind, .handle = handle, .option = option, .hash = q3p_name_hash(kind, name)};
    for (size_t i = 0; i <= length; ++i) entry->name[i] = (char)key_byte(kind, (unsigned char)name[i]);
    size_t bucket = entry->hash & (a->name_capacity - 1);
    entry->next = a->names[bucket]; a->names[bucket] = entry; ++a->name_count;
    return true;
}

bool q3p_select(qa_q3_presentation_assets *a, const char *name, qa_q3_asset_kind kind,
                  qa_q3_presentation_provider *out, qa_error *error)
{
    *out = a->options.provider;
    if (a->options.select && !a->options.select(a->options.context, name, kind, out, error)) return false;
    return out->mounts && out->images && out->materials && out->family >= QA_SCENE_Q1 && out->family <= QA_SCENE_Q3 ?
        qa_q3_assets_provider_hold(a, out, error) :
        q3p_fail(error, QA_ERROR_ARGUMENT, "selected Q3 presentation provider is incomplete");
}

bool qa_q3_presentation_assets_create(const qa_q3_presentation_asset_options *options,
                                        qa_q3_presentation_assets **out, qa_error *error)
{
    if (!options || !out || !options->provider.mounts || !options->provider.images ||
        !options->provider.materials || options->provider.family < QA_SCENE_Q1 || options->provider.family > QA_SCENE_Q3)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 presentation resource services");
    qa_q3_presentation_assets *a = calloc(1, sizeof(*a));
    if (!a) return q3p_fail(error, QA_ERROR_MEMORY, "allocating Q3 presentation resource handles");
    a->options = *options; a->users = 1;
    if (!qa_q3_assets_provider_hold(a, &options->provider, error)) { free(a); return false; }
    if (options->zero_sound && !qa_audio_asset_retain(options->zero_sound)) {
        q3p_provider_custody_release(a); free(a); return q3p_fail(error, QA_ERROR_MEMORY, "retaining Q3 fallback sound");
    }
    *out = a; return true;
}

bool q3p_assets_children_idle(const qa_q3_presentation_assets *a)
{
    if (!a || (a->world && !qa_scene_world_idle(a->world))) return false;
    for (size_t i = 0; i < a->model_count; ++i) {
        const q3p_model *m = a->models[i];
        if (!m) continue;
        if (m->world && !qa_scene_world_idle(m->world)) return false;
        if (m->source_md4_scene && !qa_scene_model_idle(m->source_md4_scene)) return false;
        for (unsigned j = 0; j < 3; ++j)
            if (m->scene[j] && !qa_scene_model_idle(m->scene[j])) return false;
    }
    return true;
}
bool qa_q3_assets_idle(const qa_q3_presentation_assets *a)
{
    return a && a->users && !a->busy && !a->capturing && !a->codec_busy &&
        q3p_assets_children_idle(a);
}
static void assets_free(qa_q3_presentation_assets *a)
{
    for (size_t i = 0; i < a->name_capacity; ++i) {
        q3p_name *next;
        for (q3p_name *row = a->names[i]; row; row = next) { next = row->next; free(row); }
    }
    for (size_t i = 0; i < a->model_count; ++i)
        if (!q3p_model_shared(a, a->models[i])) q3p_model_free(a->models[i]);
    for (size_t i = 0; i < a->skin_count; ++i) {
        if (!a->skins[i] || q3p_skin_shared(a, a->skins[i])) continue;
        free(a->skins[i]->materials);
        qa_model_skin_map_free(&a->skins[i]->map); qa_resource_release(a->skins[i]->resource); free(a->skins[i]);
    }
    for (size_t i = 0; i < a->sound_count; ++i) qa_audio_asset_release(a->sounds[i]);
    qa_audio_asset_release(a->options.zero_sound);
    q3p_provider_custody_release(a);
    qa_q3_assets_release(a->parent);
    free(a->models); free(a->skins); free(a->sounds); free(a->shaders); free(a->names); free(a);
}
void q3p_assets_dispose_borrowed(qa_q3_presentation_assets *a)
{
    /* Isolated imports own metadata and leases, never the decoded scene/world
     * roots. Their disposal must also work when those borrowed roots are held
     * by the aggregate's capture. */
    if (a) assets_free(a);
}
void qa_q3_presentation_assets_destroy(qa_q3_presentation_assets *a)
{
    if (!a || a->busy || !a->users || !q3p_assets_children_idle(a) || --a->users) return;
    assets_free(a);
}
bool qa_q3_presentation_audio_assets_read(const qa_q3_presentation_assets *a,
    qa_audio_asset ***out, size_t *count, qa_error *error)
{
    if (!a || (a->busy && (!a->capturing || a->codec_busy)) || !out || *out || !count || a->sound_count > a->sound_capacity ||
        (a->sound_capacity && !a->sounds) || a->sound_count >= SIZE_MAX / sizeof(qa_audio_asset *))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 sound holder inventory requires an idle registry and empty output");
    size_t size = a->sound_count + 1;
    qa_audio_asset **assets = malloc(size * sizeof(*assets));
    if (!assets) return q3p_fail(error, QA_ERROR_MEMORY, "Allocating borrowed Q3 sound holder inventory");
    assets[0] = a->options.zero_sound;
    if (a->sound_count) memcpy(assets + 1, a->sounds, a->sound_count * sizeof(*assets));
    *out = assets; *count = size; return true;
}
bool q3p_shader_get(const qa_q3_presentation_assets *a, int32_t handle, const qa_material **out, qa_error *error)
{
    if (!a || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 shader lookup");
    if ((handle < 0 || (size_t)handle > a->shader_count) &&
        qa_material_library_has_source_profile(a->options.provider.materials)) {
        *out = qa_material_find(a->options.provider.materials, "*default"); return *out != NULL;
    }
    if (handle < 0 || (size_t)handle > a->shader_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 shader handle");
    *out = handle ? a->shaders[handle - 1] : NULL; return true;
}

bool q3p_skin_get(const qa_q3_presentation_assets *a, int32_t handle, const qa_model_skin_map **out, qa_error *error)
{
    if (!a || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 skin lookup");
    if ((handle < 0 || (size_t)handle > a->skin_count) &&
        qa_material_library_has_source_profile(a->options.provider.materials)) {
        *out = NULL; return true;
    }
    if (handle < 0 || (size_t)handle > a->skin_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 skin handle");
    *out = handle ? &a->skins[handle - 1]->map : NULL; return true;
}

bool q3p_skin_materials(const qa_q3_presentation_assets *a, int32_t handle,
    const qa_material *const **out, size_t *count, qa_error *error)
{
    const qa_model_skin_map *map = NULL;
    if (!out || !count || !q3p_skin_get(a, handle, &map, error)) return false;
    *out = NULL; *count = 0;
    if (!map) return true;
    const q3p_skin *skin = a->skins[handle - 1];
    if (!skin->source_registration) return true;
    *out = skin->materials; *count = skin->map.count; return true;
}
static bool same_skin_name(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && b[i]) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
        if (x != y) return false;
        ++i;
    }
    return !a[i] && !b[i];
}
static bool register_source_skin(qa_q3_presentation_assets *a, const char *path,
    int32_t *out, qa_error *error)
{
    if (!*path || strlen(path) >= 64) { *out = 0; return true; }
    for (size_t i = 0; i < a->name_capacity; ++i)
        for (const q3p_name *entry = a->names[i]; entry; entry = entry->next)
            if (entry->kind == Q3P_SKIN && same_skin_name(entry->name, path)) {
                *out = entry->handle; return true;
            }
    if (a->skin_count >= 1023) {
        if (a->options.print) a->options.print(a->options.context, "WARNING: RE_RegisterSkin MAX_SKINS hit\n");
        *out = 0; return true;
    }
    ++a->busy;
    q3p_skin *skin = calloc(1, sizeof(*skin));
    bool ok = skin != NULL;
    if (!ok) q3p_fail(error, QA_ERROR_MEMORY, "Allocating Source skin registration");
    if (ok) ok = q3p_select(a, path, QA_Q3_ASSET_SKIN, &skin->provider, error);
    size_t length = strlen(path);
    bool file = length >= 5 && !strcmp(path + length - 5, ".skin");
    if (ok) {
        skin->source_registration = true;
        if (file) {
            qa_error local = {0};
            ok = qa_vfs_acquire(skin->provider.mounts, path, &skin->resource, NULL, &local);
            if (!ok && local.code == QA_ERROR_NOT_FOUND) ok = true;
            else if (!ok && error) *error = local;
            if (ok && skin->resource) ok = qa_model_skin_map_load(qa_resource_bytes(skin->resource), &skin->map, error);
        } else {
            skin->map.mappings = calloc(8, sizeof(*skin->map.mappings));
            if (!skin->map.mappings) ok = q3p_fail(error, QA_ERROR_MEMORY, "Allocating Source single-shader skin");
            if (ok) {
                skin->map.capacity = 8; skin->map.count = 1;
                skin->map.mappings[0].shader = malloc(length + 1);
                if (!skin->map.mappings[0].shader) ok = q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source skin shader");
                else memcpy(skin->map.mappings[0].shader, path, length + 1);
            }
        }
    }
    if (ok && skin->map.count) {
        skin->materials = calloc(skin->map.count, sizeof(*skin->materials));
        if (!skin->materials) ok = q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source skin shader receipts");
        qa_scene_image_options images = {.family = skin->provider.family, .wrap = QA_SCENE_REPEAT,
            .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true,
            .usage = QA_IMAGE_USAGE_SKIN, .transparent_index = -1};
        for (size_t i = 0; ok && i < skin->map.count; ++i)
            ok = qa_material_register_kind(skin->provider.materials, skin->map.mappings[i].shader,
                &images, QA_MATERIAL_DYNAMIC, &skin->materials[i], error);
    }
    int32_t handle = ok && skin->map.count ? (int32_t)a->skin_count + 1 : 0;
    if (ok) ok = q3p_reserve((void **)&a->skins, &a->skin_capacity,
        a->skin_count + 1, sizeof(*a->skins), error) && q3p_add_name(a, Q3P_SKIN, path, handle, false, error);
    if (ok) { a->skins[a->skin_count++] = skin; *out = handle; }
    else if (skin) {
        free(skin->materials); qa_model_skin_map_free(&skin->map);
        qa_resource_release(skin->resource); free(skin);
    }
    --a->busy; return ok;
}

bool qa_q3_register_skin(qa_q3_presentation_assets *a, const char *path, int32_t *out, qa_error *error)
{
    if (!a || a->busy || a->retired || !path || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 skin request");
    if (qa_material_library_has_source_profile(a->options.provider.materials))
        return register_source_skin(a, path, out, error);
    q3p_name *prior = q3p_find_name(a, Q3P_SKIN, path);
    if (prior) { *out = prior->handle; return true; }
    ++a->busy;
    qa_q3_presentation_provider provider;
    qa_resource *resource = NULL;
    qa_error local = {0};
    bool ok = q3p_select(a, path, QA_Q3_ASSET_SKIN, &provider, &local) &&
              qa_vfs_acquire(provider.mounts, path, &resource, NULL, &local);
    if (!ok && local.code == QA_ERROR_NOT_FOUND) { local = (qa_error){0}; ok = true; }
    if (!ok && error) *error = local;
    q3p_skin *skin = NULL; int32_t handle = 0;
    if (ok && resource) {
        for (size_t i = 0; i < a->skin_count; ++i)
            if (a->skins[i]->resource == resource) { handle = (int32_t)i + 1; break; }
        if (!handle) {
            skin = calloc(1, sizeof(*skin));
            ok = skin && a->skin_count < INT32_MAX && q3p_reserve((void **)&a->skins, &a->skin_capacity,
                a->skin_count + 1, sizeof(*a->skins), error);
            if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_MEMORY, "allocating Q3 skin handle");
            if (ok) ok = qa_model_skin_map_load(qa_resource_bytes(resource), &skin->map, error);
            if (ok) { skin->resource = resource; skin->provider = provider;
                resource = NULL; handle = (int32_t)a->skin_count + 1; }
        }
    }
    if (ok) ok = q3p_add_name(a, Q3P_SKIN, path, handle, false, error);
    if (ok) {
        if (skin) a->skins[a->skin_count++] = skin;
        *out = handle;
    } else if (skin) { qa_model_skin_map_free(&skin->map); qa_resource_release(skin->resource); free(skin); }
    qa_resource_release(resource); --a->busy; return ok;
}

bool qa_q3_register_shader(qa_q3_presentation_assets *a, const char *path, bool mipmap,
                             int32_t *out, qa_error *error)
{
    if (!a || a->busy || a->retired || !path || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 shader request");
    bool source_request = qa_material_library_has_source_profile(a->options.provider.materials);
    if (source_request && (!*path || strlen(path) >= 64)) {
        *out = 0; return true;
    }
    q3p_name *prior = q3p_find_name(a, Q3P_SHADER, path);
    if (prior) { *out = prior->handle; return true; }
    qa_error local = {0}; char *normalized = source_request ? NULL : qa_vfs_normalize_path(path, &local);
    if (!source_request && !normalized) {
        if (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT) { *out = 0; return true; }
        if (error) *error = local;
        return false;
    }
    ++a->busy;
    qa_q3_presentation_provider provider = {0}; const qa_material *material = NULL;
    bool ok = q3p_select(a, source_request ? path : normalized, QA_Q3_ASSET_SHADER, &provider, error);
    qa_scene_image_options images = {.family = provider.family, .wrap = QA_SCENE_REPEAT,
        .filter = mipmap ? QA_SCENE_LINEAR_MIPMAP_LINEAR : QA_SCENE_LINEAR, .mipmap = mipmap,
        .usage = QA_IMAGE_USAGE_PICTURE, .transparent_index = -1};
    bool source_shader = source_request || (ok && qa_material_library_has_source_profile(provider.materials));
    if (ok) ok = qa_material_register_kind(provider.materials, source_shader ? path : normalized,
        &images, QA_MATERIAL_PICTURE, &material, error);
    if (ok && material && material->registration == 0 && qa_material_library_has_source_profile(provider.materials)) {
        const char *default_name = "*default";
        bool named_default = strcspn(path, ".") == strlen(default_name);
        for (size_t i = 0; named_default && default_name[i]; ++i)
            named_default = key_byte(Q3P_SHADER, (unsigned char)path[i]) == (unsigned char)default_name[i];
        if (!named_default && a->options.print) a->options.print(a->options.context, "WARNING: MAX_SHADERS hit\n");
        free(normalized); --a->busy; *out = 0; return true;
    }
    int32_t handle = 0; bool append = false;
    if (ok && material && !material->default_shader) {
        for (size_t i = 0; i < a->shader_count; ++i)
            if (a->shaders[i] == material) { handle = (int32_t)i + 1; break; }
        if (!handle) {
            ok = a->shader_count < INT32_MAX && q3p_reserve((void **)&a->shaders, &a->shader_capacity,
                a->shader_count + 1, sizeof(*a->shaders), error);
            if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_MEMORY, "allocating Q3 shader handle");
            handle = (int32_t)a->shader_count + 1; append = true;
        }
        if (ok && append) a->shaders[a->shader_count++] = material;
        if (ok) ok = q3p_add_name(a, Q3P_SHADER, path, handle, mipmap, error);
        if (ok && !source_shader && strcmp(path, normalized)) ok = q3p_add_name(a, Q3P_SHADER, normalized, handle, mipmap, error);
    }
    free(normalized); --a->busy;
    if (ok) *out = handle;
    return ok;
}

bool qa_q3_register_picture_image(qa_q3_presentation_assets *a, const qa_scene_image *image,
                                    int32_t *out, qa_error *error)
{
    if (!a || a->busy || a->retired || !image || !image->name || !out)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 generated picture request");
    if (!qa_q3_assets_provider_hold(a, &a->options.provider, error)) return false;
    ++a->busy;
    const qa_material *material = NULL;
    bool ok = qa_material_register_generated_picture(a->options.provider.materials,
        image->name, image, &material, error);
    int32_t handle = 0;
    if (ok) {
        for (size_t i = 0; i < a->shader_count; ++i)
            if (a->shaders[i] == material) { handle = (int32_t)i + 1; break; }
        if (!handle) {
            if (a->shader_count >= INT32_MAX) ok = q3p_fail(error, QA_ERROR_MEMORY, "Q3 shader handle capacity exceeded");
            else ok = q3p_reserve((void **)&a->shaders, &a->shader_capacity,
                a->shader_count + 1, sizeof(*a->shaders), error);
            if (ok) handle = (int32_t)a->shader_count + 1;
        }
        if (ok) ok = q3p_add_name(a, Q3P_SHADER, image->name, handle, false, error);
        if (ok) q3p_find_name(a, Q3P_SHADER, image->name)->generated = true;
        if (ok && (size_t)handle > a->shader_count) a->shaders[a->shader_count++] = material;
    }
    --a->busy;
    if (ok) *out = handle;
    return ok;
}

static const char *registered_name(const qa_q3_presentation_assets *assets, q3p_resource_kind kind, int32_t handle)
{
    const char *name = NULL;
    for (size_t i = 0; i < assets->name_capacity; ++i)
        for (const q3p_name *entry = assets->names[i]; entry; entry = entry->next)
            if (entry->kind == kind && entry->handle == handle && (!name || strcmp(entry->name, name) < 0)) name = entry->name;
    return name;
}
bool qa_q3_registered_models(const qa_q3_presentation_assets *assets, qa_arena *scratch,
                             const qa_q3_registered_model **out, size_t *count, qa_error *error)
{
    if (!assets || !scratch || !out || !count || assets->busy || assets->model_count > SIZE_MAX / sizeof(qa_q3_registered_model))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid source model inventory observation");
    qa_q3_registered_model *rows = assets->model_count ? qa_arena_alloc(scratch, assets->model_count * sizeof(*rows), _Alignof(qa_q3_registered_model), error) : NULL;
    if (assets->model_count && !rows) return false;
    size_t n = 0;
    for (size_t i = 0; i < assets->model_count; ++i) {
        const q3p_model *model = assets->models[i]; if (!model || model->registration_bad) continue;
        const char *name = registered_name(assets, Q3P_MODEL, (int32_t)i + 1);
        if (!name) return q3p_fail(error, QA_ERROR_FORMAT, "source model handle has no retained registration name");
        const qa_model *base = q3p_model_source(model, 0);
        rows[n++] = (qa_q3_registered_model){.handle = (int32_t)i + 1, .name = name,
            .world = model->world != NULL, .inline_model = model->world && !model->owns_world,
            .format = model->source_registration ? model->source_kind : base ? base->format : QA_MODEL_MDL};
    }
    *out = rows; *count = n; return true;
}
bool qa_q3_registered_skins(const qa_q3_presentation_assets *assets, qa_arena *scratch,
                            const qa_q3_registered_skin **out, size_t *count, qa_error *error)
{
    if (!assets || !scratch || !out || !count || assets->busy || assets->skin_count > SIZE_MAX / sizeof(qa_q3_registered_skin))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid source skin inventory observation");
    qa_q3_registered_skin *rows = assets->skin_count ? qa_arena_alloc(scratch, assets->skin_count * sizeof(*rows), _Alignof(qa_q3_registered_skin), error) : NULL;
    if (assets->skin_count && !rows) return false;
    size_t n = 0;
    for (size_t i = 0; i < assets->skin_count; ++i) {
        const q3p_skin *skin = assets->skins[i]; if (!skin) continue;
        if (skin->source_registration && !skin->map.count) continue;
        const char *name = registered_name(assets, Q3P_SKIN, (int32_t)i + 1);
        if (!name) return q3p_fail(error, QA_ERROR_FORMAT, "source skin handle has no retained registration name");
        rows[n++] = (qa_q3_registered_skin){(int32_t)i + 1, name, &skin->map};
    }
    *out = rows; *count = n; return true;
}

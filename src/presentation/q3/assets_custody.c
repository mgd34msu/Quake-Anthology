#include "internal.h"
#include "qa/q3_assets_custody.h"
#include "qa/scene_world_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"

struct q3p_provider_custody {
    struct q3p_provider_custody *next;
    qa_q3_presentation_provider value;
};
struct q3p_map_custody {
    struct q3p_map_custody *next;
    qa_q3_asset_map_custody value;
};
bool qa_q3_assets_retain(qa_q3_presentation_assets *assets, qa_error *error)
{
    if (!assets || !assets->users || assets->users == UINT_MAX)
        return q3p_fail(error, QA_ERROR_MEMORY, "Retaining actual Source registry allocation");
    ++assets->users; return true;
}
void qa_q3_assets_release(qa_q3_presentation_assets *assets)
{
    if (!assets || !assets->users) return;
    if (assets->users > 1) { --assets->users; return; }
    qa_q3_presentation_assets_destroy(assets);
}
qa_q3_presentation_assets *qa_q3_assets_parent(const qa_q3_presentation_assets *assets)
{ return assets ? assets->parent : NULL; }
bool qa_q3_assets_retired(const qa_q3_presentation_assets *assets)
{ return assets && assets->retired; }
bool qa_q3_assets_services_retire(qa_q3_presentation_assets *assets, qa_error *error)
{
    if (!assets) return true;
    for (qa_q3_presentation_assets *row = assets; row; row = row->parent)
        if (!qa_q3_assets_idle(row))
            return q3p_fail(error, QA_ERROR_ARGUMENT, "Registry services remain entered or captured during parent retirement");
    for (qa_q3_presentation_assets *row = assets; row; row = row->parent) {
        row->options.provider.geometry_owner = NULL;
        for (q3p_provider_custody *provider = row->providers; provider; provider = provider->next)
            provider->value.geometry_owner = NULL;
        for (size_t i = 0; i < row->model_count; ++i)
            if (row->models[i]) row->models[i]->provider.geometry_owner = NULL;
        for (size_t i = 0; i < row->skin_count; ++i)
            if (row->skins[i]) row->skins[i]->provider.geometry_owner = NULL;
        row->options.sounds = NULL; row->options.movies = NULL; row->options.context = NULL;
        row->options.select = NULL; row->options.model_initialize = NULL; row->options.print = NULL;
        row->retired = true;
    }
    return true;
}
bool q3p_model_shared(const qa_q3_presentation_assets *assets, const q3p_model *model)
{
    if (!model || !assets || !assets->parent) return false;
    for (size_t i = 0; i < assets->parent->model_count; ++i)
        if (assets->parent->models[i] == model) return true;
    return false;
}
bool q3p_skin_shared(const qa_q3_presentation_assets *assets, const q3p_skin *skin)
{
    if (!skin || !assets || !assets->parent) return false;
    for (size_t i = 0; i < assets->parent->skin_count; ++i)
        if (assets->parent->skins[i] == skin) return true;
    return false;
}
static bool copy_slots(void **out, const void *source, size_t count, size_t capacity,
    size_t width, qa_error *error)
{
    if (count > capacity || capacity > SIZE_MAX / width || (capacity && !source))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Retained registry has invalid physical handle storage");
    if (!capacity) return true;
    *out = calloc(capacity, width);
    if (!*out) return q3p_fail(error, QA_ERROR_MEMORY, "Copying retained registry handle slots");
    memcpy(*out, source, count * width); return true;
}
bool q3p_assets_fork(qa_q3_presentation_assets *source, qa_q3_presentation_assets **out, qa_error *error)
{
    if (!out || *out || !qa_q3_assets_idle(source) || source->retired)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Registry fork requires its genuine idle current parent");
    qa_q3_presentation_assets *next = NULL;
    if (!qa_q3_presentation_assets_create(&source->options, &next, error)) return false;
    if (!qa_q3_assets_retain(source, error)) { qa_q3_presentation_assets_destroy(next); return false; }
    next->parent = source;
    bool okay = copy_slots((void **)&next->models, source->models, source->model_count,
        source->model_capacity, sizeof(*next->models), error);
    if (okay) { next->model_count = source->model_count; next->model_capacity = source->model_capacity; }
    if (okay) okay = copy_slots((void **)&next->skins, source->skins, source->skin_count,
        source->skin_capacity, sizeof(*next->skins), error);
    if (okay) { next->skin_count = source->skin_count; next->skin_capacity = source->skin_capacity; }
    if (okay) okay = copy_slots((void **)&next->shaders, source->shaders, source->shader_count,
        source->shader_capacity, sizeof(*next->shaders), error);
    if (okay) { next->shader_count = source->shader_count; next->shader_capacity = source->shader_capacity; }
    if (okay) okay = copy_slots((void **)&next->sounds, source->sounds, source->sound_count,
        source->sound_capacity, sizeof(*next->sounds), error);
    if (okay) {
        next->sound_capacity = source->sound_capacity;
        for (size_t i = 0; okay && i < source->sound_count; ++i) {
            okay = source->sounds[i] && qa_audio_asset_retain(source->sounds[i]);
            if (okay) ++next->sound_count;
            else q3p_fail(error, QA_ERROR_MEMORY, "Retaining forked numeric sound holder");
        }
    }
    if (okay && source->name_capacity) {
        next->names = calloc(source->name_capacity, sizeof(*next->names));
        okay = next->names != NULL;
        if (okay) next->name_capacity = source->name_capacity;
        else q3p_fail(error, QA_ERROR_MEMORY, "Copying retained registry name buckets");
    }
    for (size_t i = 0; okay && i < source->name_capacity; ++i) {
        q3p_name **tail = &next->names[i];
        for (const q3p_name *entry = source->names[i]; okay && entry; entry = entry->next) {
            size_t length = strlen(entry->name);
            q3p_name *copy = length <= SIZE_MAX - sizeof(*copy) - 1 ? malloc(sizeof(*copy) + length + 1) : NULL;
            if (!copy) { okay = q3p_fail(error, QA_ERROR_MEMORY, "Copying retained registry name"); break; }
            memcpy(copy, entry, sizeof(*copy) + length + 1); copy->next = NULL;
            *tail = copy; tail = &copy->next; ++next->name_count;
        }
    }
    for (q3p_provider_custody *row = source->providers; okay && row; row = row->next)
        okay = qa_q3_assets_provider_hold(next, &row->value, error);
    if (okay) { next->world = source->world; next->geometry = source->geometry; *out = next; }
    else qa_q3_presentation_assets_destroy(next);
    return okay;
}
bool qa_q3_assets_provider_hold(qa_q3_presentation_assets *assets,
    const qa_q3_presentation_provider *provider, qa_error *error)
{
    if (!assets || !provider || !provider->mounts || !provider->images || !provider->materials ||
        (unsigned)provider->family > QA_GAME_Q3 || qa_scene_resources_files(provider->images) != provider->mounts ||
        qa_material_library_resource_owner(provider->materials) != provider->images)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source registry provider lacks its actual bank authority");
    for (q3p_provider_custody *row = assets->providers; row; row = row->next)
        if (row->value.mounts == provider->mounts && row->value.images == provider->images &&
            row->value.materials == provider->materials && row->value.family == provider->family &&
            row->value.geometry_owner == provider->geometry_owner) return true;
    q3p_provider_custody *row = calloc(1, sizeof(*row));
    if (!row) return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source registry provider custody");
    bool vfs = qa_vfs_retain(provider->mounts, error);
    bool images = vfs && qa_scene_resources_retain(provider->images, error);
    bool materials = images && qa_material_library_retain(provider->materials, error);
    if (!materials) {
        if (images) qa_scene_resources_destroy(provider->images);
        if (vfs) qa_vfs_destroy(provider->mounts);
        free(row); return false;
    }
    row->value = *provider;
    q3p_provider_custody **tail = &assets->providers;
    while (*tail) tail = &(*tail)->next;
    *tail = row; return true;
}
size_t qa_q3_assets_provider_count(const qa_q3_presentation_assets *assets)
{
    size_t count = 0;
    for (const q3p_provider_custody *row = assets ? assets->providers : NULL; row; row = row->next) ++count;
    return count;
}
bool qa_q3_assets_provider_at(const qa_q3_presentation_assets *assets, size_t index,
    qa_q3_presentation_provider *out)
{
    if (!assets || !out) return false;
    for (const q3p_provider_custody *row = assets->providers; row; row = row->next)
        if (!index--) { *out = row->value; return true; }
    return false;
}
void q3p_provider_custody_release(qa_q3_presentation_assets *assets)
{
    while (assets->maps) {
        q3p_map_custody *row = assets->maps;
        assets->maps = row->next;
        qa_scene_world_release(row->value.world);
        qa_collision_destroy(row->value.geometry); free(row);
    }
    while (assets->providers) {
        q3p_provider_custody *row = assets->providers;
        assets->providers = row->next;
        qa_material_library_destroy(row->value.materials);
        qa_scene_resources_destroy(row->value.images);
        qa_vfs_destroy(row->value.mounts); free(row);
    }
}
bool qa_q3_assets_custody_restore(qa_q3_presentation_assets *assets, qa_error *error)
{
    if (!assets || !qa_q3_assets_provider_hold(assets, &assets->options.provider, error)) return false;
    if (assets->world && !qa_q3_assets_map_hold(assets, assets->world, assets->geometry, error)) return false;
    for (size_t i = 0; i < assets->model_count; ++i)
        if (assets->models[i] && !qa_q3_assets_provider_hold(assets, &assets->models[i]->provider, error)) return false;
    for (size_t i = 0; i < assets->skin_count; ++i)
        if (assets->skins[i] && !qa_q3_assets_provider_hold(assets, &assets->skins[i]->provider, error)) return false;
    for (size_t i = 0; i < assets->shader_count; ++i) {
        const qa_material *material = assets->shaders[i];
        if (!material || !material->library) return false;
        qa_scene_resources *images = qa_material_library_resource_owner(material->library);
        qa_q3_presentation_provider provider = {.mounts = qa_scene_resources_files(images),
            .images = images, .materials = material->library, .family = material->family};
        if (!qa_q3_assets_provider_hold(assets, &provider, error)) return false;
    }
    return true;
}
bool qa_q3_assets_map_hold(qa_q3_presentation_assets *assets, qa_scene_world *world,
    qa_collision_geometry *geometry, qa_error *error)
{
    if (!assets || (!world != !geometry))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source registry map custody requires its actual world and collision pair");
    if (!world) return true;
    const qa_resource *source = qa_scene_world_source_resource_read(world);
    if (qa_collision_resource(geometry) != source ||
        (!source && qa_material_library_has_source_profile(assets->options.provider.materials)))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source registry map custody lacks its shared immutable BSP source");
    for (q3p_map_custody *row = assets->maps; row; row = row->next)
        if (row->value.world == world && row->value.geometry == geometry) return true;
    q3p_map_custody *row = calloc(1, sizeof(*row));
    if (!row) return q3p_fail(error, QA_ERROR_MEMORY, "Retaining Source registry map custody");
    if (!qa_scene_world_retain(world, error)) { free(row); return false; }
    if (!qa_collision_retain(geometry, error)) { qa_scene_world_release(world); free(row); return false; }
    row->value = (qa_q3_asset_map_custody){world, geometry, source};
    q3p_map_custody **tail = &assets->maps;
    while (*tail) tail = &(*tail)->next;
    *tail = row; return true;
}
size_t qa_q3_assets_map_count(const qa_q3_presentation_assets *assets)
{
    size_t count = 0;
    for (const q3p_map_custody *row = assets ? assets->maps : NULL; row; row = row->next) ++count;
    return count;
}
bool qa_q3_assets_map_at(const qa_q3_presentation_assets *assets, size_t index, qa_q3_asset_map_custody *out)
{
    if (!assets || !out) return false;
    for (const q3p_map_custody *row = assets->maps; row; row = row->next)
        if (!index--) { *out = row->value; return true; }
    return false;
}
bool qa_q3_assets_prepare_restored_custody_maps(qa_q3_presentation_assets *assets,
    const qa_q3_asset_map_custody *maps, size_t count, qa_scene_world *world,
    qa_collision_geometry *geometry, qa_error *error)
{
    if (!assets || !assets->users || assets->busy || assets->capturing || assets->codec_busy ||
        assets->name_count || assets->name_capacity || assets->model_count || assets->model_capacity ||
        assets->skin_count || assets->skin_capacity || assets->shader_count || assets->shader_capacity ||
        assets->sound_count || assets->sound_capacity || (count && !maps) ||
        count > SIZE_MAX / sizeof(*maps) || (!world != !geometry) ||
        assets->world != world || assets->geometry != geometry)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 map custody import requires its actual empty prebound registry");
    for (const q3p_map_custody *row = assets->maps; row; row = row->next)
        if (!qa_scene_world_idle(row->value.world))
            return q3p_fail(error, QA_ERROR_ARGUMENT, "Constructor map custody remains entered during import");
    bool current = world == NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!maps[i].world || !maps[i].geometry || !qa_scene_world_observation_ready(maps[i].world) ||
            maps[i].resource != qa_scene_world_source_resource_read(maps[i].world))
            return q3p_fail(error, QA_ERROR_FORMAT, "Saved map custody lacks its actual imported world source");
        for (size_t j = 0; j < i; ++j)
            if (maps[i].world == maps[j].world && maps[i].geometry == maps[j].geometry)
                return q3p_fail(error, QA_ERROR_FORMAT, "Saved map custody repeats a physical pair");
        if (maps[i].world == world && maps[i].geometry == geometry) current = true;
    }
    if (!current)
        return q3p_fail(error, QA_ERROR_FORMAT, "Saved map custody omits its actual current pair");
    qa_q3_presentation_assets prepared = {.options = assets->options};
    for (size_t i = 0; i < count; ++i) {
        if (!qa_q3_assets_map_hold(&prepared, maps[i].world, maps[i].geometry, error)) {
            q3p_provider_custody_release(&prepared); return false;
        }
    }
    q3p_map_custody *previous = assets->maps;
    assets->maps = prepared.maps; prepared.maps = previous;
    q3p_provider_custody_release(&prepared);
    return true;
}

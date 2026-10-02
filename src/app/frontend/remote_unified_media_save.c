#include "remote_unified_media_save.h"
#include "remote_unified_media_private.h"
#include "remote_unified_private.h"
#include "remote_unified_material_movies_bridge.h"
#include "save_private.h"
#include "material_inventory.h"
#include "scene_refs.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/q3_assets_save.h"

#include <stdlib.h>
#include <string.h>

typedef struct asset_scope {
    frontend_unified_media *owner;
    const frontend_unified_media_refs *refs;
    unified_media_bank *bank;
    size_t ordinal;
} asset_scope;

static bool fail(qa_error *error, const char *message)
{ return frontend_unified_fail(error, QA_ERROR_FORMAT, message); }
static unified_media_bank *bank_at(frontend_unified_media *owner, size_t ordinal)
{
    unified_media_bank *row = owner->banks;
    while (row && ordinal--) row = row->next;
    return row;
}
static size_t bank_index(const frontend_unified_media *owner, const unified_media_bank *bank)
{
    size_t ordinal = 0;
    for (const unified_media_bank *row = owner->banks; row; row = row->next, ++ordinal)
        if (row == bank) return ordinal;
    return SIZE_MAX;
}
bool frontend_unified_media_q3_row(size_t bank, size_t model, uint64_t *out)
{
    if (!out || bank >= UINT32_MAX || model >= UINT32_MAX) return false;
    *out = ((uint64_t)(bank + 1) << 32) | (uint64_t)(model + 1); return true;
}
static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t size = value->size;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data)
            return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Retaining Unified cold media bytes");
    }
    return qa_source_save_bytes(io, value->data, size);
}
static bool request_bytes(qa_source_save_io *io, qa_bytes *span, uint8_t **owned)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = span->size;
    if (!qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (reading) {
        *owned = size ? malloc(size) : NULL;
        if (size && !*owned)
            return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Retaining Unified image request bytes");
        *span = (qa_bytes){*owned, size};
    }
    return (!size || span->data) && qa_source_save_bytes(io, (void *)span->data, size);
}
static bool options(qa_source_save_io *io, unified_media_model *row)
{
    qa_scene_image_options *o = &row->options;
    uint32_t family = row->family, wrap = o->wrap, filter = o->filter, usage = o->usage;
    int32_t transparent = o->transparent_index;
    if (!qa_source_save_u32(io, &family) || family > QA_SCENE_Q3 ||
        !qa_source_save_u32(io, &wrap) || wrap > QA_SCENE_CLAMP ||
        !qa_source_save_u32(io, &filter) || filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io, &usage) || usage > QA_IMAGE_USAGE_SKY ||
        !qa_source_save_bool(io, &o->mipmap) || !qa_source_save_bool(io, &o->transparent) ||
        !qa_source_save_bool(io, &o->fullbright_only) || !qa_source_save_i32(io, &transparent) ||
        !request_bytes(io, &o->palette_rgb, &row->palette) ||
        !request_bytes(io, &o->translation, &row->translation) || o->source_q3) return false;
    row->family = o->family = (qa_scene_family)family;
    o->wrap = (qa_scene_wrap)wrap; o->filter = (qa_scene_filter)filter;
    o->usage = (qa_scene_image_usage)usage; o->transparent_index = transparent;
    return true;
}
static bool opening(qa_source_save_io *io, qa_vfs *files, qa_vfs_acquisition *value)
{
    return qa_source_save_u64(io, &value->mount) && qa_source_save_u64(io, &value->resource_id) &&
        frontend_save_text(io, &value->path) && frontend_save_text(io, &value->lookup_path) &&
        frontend_save_text(io, &value->link_source) && frontend_save_text(io, &value->link_target) &&
        qa_vfs_acquisition_opening_codec(io, files, value) && value->opening_present &&
        qa_vfs_acquisition_retained(files, value, io->error);
}
static bool resource_encode(void *context, const qa_resource *resource, uint64_t *pool,
    uint64_t *version, qa_error *error)
{
    asset_scope *scope = context;
    return qa_application_content_resource_id(scope->refs->content, resource, pool, version) ||
        fail(error, "Unified media resource leaves its actual content graph");
}
static bool resource_decode(void *context, uint64_t pool, uint64_t version,
    const qa_resource **out, qa_error *error)
{
    asset_scope *scope = context;
    const qa_resource *resource = qa_application_content_resource(scope->refs->content, pool, version);
    if (!out || !resource) return fail(error, "Unified media resource is absent from its restored pool");
    *out = resource; return true;
}
static bool provider_encode(void *context, const qa_q3_presentation_provider *provider,
    uint64_t *out, qa_error *error)
{
    asset_scope *scope = context; size_t ordinal = 0;
    for (unified_media_bank *row = scope->owner->banks; row; row = row->next, ++ordinal)
        if (provider && provider->mounts == row->files && provider->images == row->images &&
            provider->materials == row->materials && provider->family == QA_SCENE_Q3) {
            if (!out) return false;
            *out = ordinal + 1; return true;
        }
    return fail(error, "Unified Q3 provider leaves its actual content bank");
}
static bool provider_decode(void *context, uint64_t key, qa_q3_presentation_provider *out,
    qa_error *error)
{
    asset_scope *scope = context;
    unified_media_bank *row = key && key - 1 <= SIZE_MAX ? bank_at(scope->owner, (size_t)(key - 1)) : NULL;
    if (!row || !out) return fail(error, "Unified Q3 provider bank is absent");
    *out = (qa_q3_presentation_provider){row->files, row->images, row->materials, QA_SCENE_Q3};
    return true;
}
static bool services_encode(void *context, const qa_q3_presentation_asset_options *options,
    uint64_t *out, qa_error *error)
{
    asset_scope *scope = context; uint64_t bank = 0;
    if (!options || !out || !provider_encode(scope, &options->provider, &bank, error) ||
        bank != scope->ordinal + 1 || options->sounds != scope->bank->sounds ||
        options->zero_sound || options->movies || options->context || options->select ||
        options->model_initialize || options->print)
        return fail(error, "Unified Q3 registry services differ from their real bank owner");
    *out = bank; return true;
}
static bool services_qualify(void *context, uint64_t key,
    const qa_q3_presentation_asset_options *options, qa_error *error)
{
    uint64_t actual = 0;
    return services_encode(context, options, &actual, error) && key == actual;
}
static bool model_encode(void *context, const qa_model *model, uint64_t *out, qa_error *error)
{ return frontend_model_encode(((asset_scope *)context)->refs->models, model, out, error); }
static bool model_decode(void *context, uint64_t key, qa_bytes bytes, const qa_model **out, qa_error *error)
{ return frontend_model_decode(((asset_scope *)context)->refs->models, key, bytes, out, error); }
static bool model_retain(void *context, const qa_model *model, qa_q3_asset_model_lease *out, qa_error *error)
{
    asset_scope *scope = context;
    frontend_scene_model_scope source = {{scope->refs->scene, 0}, scope->refs->models};
    return frontend_scene_q3_model_retain(&source, model, out, error);
}
static bool scene_encode(void *context, const qa_scene_model *scene, uint64_t *out, qa_error *error)
{ return frontend_scene_root_encode(((asset_scope *)context)->refs->roots, scene, out, error); }
static bool scene_decode(void *context, uint64_t key, qa_scene_model **out, qa_error *error)
{ return frontend_scene_root_decode(((asset_scope *)context)->refs->roots, key, out, error); }
static bool world_encode(void *context, const qa_scene_world *world, uint64_t *out, qa_error *error)
{ return frontend_world_encode(((asset_scope *)context)->refs->roots, world, out, error); }
static bool world_decode(void *context, uint64_t key, qa_scene_world **out, qa_error *error)
{ return frontend_world_decode(((asset_scope *)context)->refs->roots, key, out, error); }
static bool collision_encode(void *context, const qa_collision_geometry *geometry,
    uint64_t *out, qa_error *error)
{
    asset_scope *scope = context;
    if (!out || geometry != qa_executable_recipe_geometry(scope->owner->recipe))
        return fail(error, "Unified Q3 collision leaves its actual recipe geometry");
    *out = 1; return true;
}
static bool collision_decode(void *context, uint64_t key, qa_collision_geometry **out, qa_error *error)
{
    asset_scope *scope = context;
    if (!out || key != 1) return fail(error, "Unified Q3 collision key differs from its admitted geometry");
    *out = (qa_collision_geometry *)qa_executable_recipe_geometry(scope->owner->recipe);
    return *out != NULL;
}
static bool material_encode(void *context, const qa_material *material, uint64_t *out, qa_error *error)
{ return frontend_scene_material_encode(((asset_scope *)context)->refs->scene, material, out, error); }
static bool material_decode(void *context, uint64_t key, const qa_material **out, qa_error *error)
{ return frontend_scene_material_decode(((asset_scope *)context)->refs->scene, key, out, error); }
static bool audio_encode(void *context, const qa_audio_asset *asset, uint64_t *out, qa_error *error)
{
    asset_scope *scope = context;
    return qa_audio_asset_inventory_index(scope->refs->audio, asset, out) ||
        fail(error, "Unified Q3 sound leaves its actual asset graph");
}
static bool audio_decode(void *context, uint64_t key, qa_audio_asset **out, qa_error *error)
{
    asset_scope *scope = context;
    qa_audio_asset *asset = qa_audio_asset_inventory_at(scope->refs->audio, key);
    if (!out || !asset) return fail(error, "Unified Q3 sound is absent from its imported asset graph");
    *out = asset; return true;
}
static bool scene_ready(void *context, uint64_t key, size_t row, qa_error *error)
{
    asset_scope *scope = context; uint64_t ordinal = 0; frontend_scene_root_view root;
    return key && key - 1 <= SIZE_MAX && frontend_unified_media_q3_row(scope->ordinal, row, &ordinal) &&
        frontend_world_inventory_model_at(scope->refs->roots, (size_t)(key - 1), &root) &&
        root.owner.row == ordinal && frontend_scene_root_owner_ready(scope->refs->roots, key,
            FRONTEND_SCENE_OWNER_UNIFIED_Q3, scope->refs->owner, error);
}
static bool world_ready(void *context, uint64_t key, size_t row, qa_error *error)
{
    asset_scope *scope = context; uint64_t ordinal = 0; frontend_world_source world; frontend_scene_owner owner;
    return key && key - 1 <= SIZE_MAX && frontend_unified_media_q3_row(scope->ordinal, row, &ordinal) &&
        frontend_world_inventory_world_at(scope->refs->roots, (size_t)(key - 1), &world, &owner) &&
        owner.row == ordinal && frontend_world_owner_ready(scope->refs->roots, key,
            FRONTEND_SCENE_OWNER_UNIFIED_Q3, scope->refs->owner, error);
}
static void scene_adopt(void *context, uint64_t key)
{ frontend_scene_root_adopt(((asset_scope *)context)->refs->roots, key); }
static void world_adopt(void *context, uint64_t key)
{ frontend_world_adopt(((asset_scope *)context)->refs->roots, key); }
static qa_q3_asset_owner_refs asset_refs(asset_scope *scope)
{
    return (qa_q3_asset_owner_refs){.context = scope,
        .services_encode = services_encode, .services_qualify = services_qualify,
        .provider_encode = provider_encode, .provider_decode = provider_decode,
        .resource_encode = resource_encode, .resource_decode = resource_decode,
        .model_encode = model_encode, .model_decode = model_decode, .model_retain = model_retain,
        .scene_encode = scene_encode, .scene_decode = scene_decode,
        .world_encode = world_encode, .world_decode = world_decode,
        .collision_encode = collision_encode, .collision_decode = collision_decode,
        .material_encode = material_encode, .material_decode = material_decode,
        .audio_encode = audio_encode, .audio_decode = audio_decode,
        .scene_owned_ready = scene_ready, .world_owned_ready = world_ready,
        .scene_adopt = scene_adopt, .world_adopt = world_adopt};
}
static bool fields(qa_source_save_io *io, frontend_unified_media *owner,
    const frontend_unified_media_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','U','M','D'}; uint32_t version = 2;
    uint64_t physical = refs->owner, world = reading ? 0 : owner->saved_world;
    size_t banks = reading ? 0 : frontend_unified_media_bank_count(owner);
    size_t world_bank = reading ? 0 : bank_index(owner, owner->world_bank);
    if (!reading && !frontend_world_encode(refs->roots, owner->world, &world, io->error)) return false;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QUMD", sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 2 || !qa_source_save_u64(io, &physical) ||
        physical != refs->owner || !physical || !qa_source_save_count(io, &banks, UINT32_MAX - 1) ||
        !banks || (reading && banks > (io->input.size - io->offset) / 22) ||
        !qa_source_save_count(io, &world_bank, banks - 1) || !qa_source_save_u64(io, &world) || !world) return false;
    if (reading) owner->saved_world = world;
    unified_media_bank **tail = &owner->banks;
    for (size_t i = 0; i < banks; ++i) {
        unified_media_bank *row = reading ? calloc(1, sizeof(*row)) : bank_at(owner, i);
        if (!row) return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Retaining Unified content bank topology");
        if (reading) { *tail = row; tail = &row->next; }
        uint64_t view = reading ? 0 : qa_application_content_view_id(refs->content, row->files);
        uint32_t product = reading ? 0 : row->product->id;
        bool assets = row->q3_assets != NULL, map = false, movies = row->media != NULL;
        qa_buffer state = {0};
        if (!frontend_save_text(io, &row->content) || !row->content || !*row->content ||
            !qa_source_save_u64(io, &view) || !view || !qa_source_save_u32(io, &product) ||
            !qa_source_save_bool(io, &assets) || !qa_source_save_bool(io, &movies)) return false;
        qa_vfs *admitted = NULL; const qa_product *selected = NULL;
        if (!qa_executable_recipe_content_read(owner->recipe, row->content, &admitted, &selected) ||
            selected->id != product || admitted != qa_application_content_view(refs->content, view)) return false;
        if (reading) {
            row->files = admitted; row->product = selected;
            row->images = qa_scene_resources_create_detached(admitted, io->error);
            if (row->images) row->materials = qa_material_library_create_detached(row->images, io->error);
            if (row->materials) row->fonts = qa_font_library_create(admitted, row->images, io->error);
            if (!row->images || !row->materials || !row->fonts ||
                !qa_audio_bank_create(admitted, &row->sounds, io->error)) return false;
            if (movies && !frontend_unified_material_movies_prepare_restored(owner, i, io->error)) return false;
            if (assets) {
                qa_q3_presentation_asset_options policy = {.provider =
                    {admitted, row->images, row->materials, QA_SCENE_Q3}, .sounds = row->sounds};
                if (!qa_q3_presentation_assets_create(&policy, &row->q3_assets, io->error)) return false;
            }
            for (unified_media_bank *prior = owner->banks; prior != row; prior = prior->next)
                if (!strcmp(prior->content, row->content)) return false;
        } else if (admitted != row->files || selected != row->product) return false;
        if (assets && !reading) {
            qa_q3_presentation_asset_options services; qa_scene_world *bound_world = NULL;
            qa_collision_geometry *geometry = NULL; asset_scope scope = {owner, refs, row, i};
            qa_q3_asset_owner_refs registry = asset_refs(&scope);
            bool okay = qa_q3_assets_services(row->q3_assets, &services, &bound_world, &geometry, io->error) &&
                (!bound_world || (bound_world == owner->world &&
                    geometry == qa_executable_recipe_geometry(owner->recipe))) &&
                (!bound_world == !geometry) && qa_q3_assets_owner_checkpoint(row->q3_assets,
                    io->session, &registry, &state, io->error);
            if (!okay) { qa_buffer_free(&state); return false; }
            map = bound_world != NULL;
        }
        bool okay = qa_source_save_bool(io, &map) && (!map || assets) && blob(io, &state) &&
            (assets == (state.size != 0));
        if (reading) { row->saved_map = map; row->saved_assets = state; }
        else qa_buffer_free(&state);
        if (!okay) return false;
    }
    owner->world_bank = bank_at(owner, world_bank);
    const qa_recipe_choices *choices = qa_executable_recipe_choices(owner->recipe);
    if (!owner->world_bank || owner->world_bank->product->id != choices->world.geometry) return false;
    size_t models = reading ? 0 : frontend_unified_media_model_count(owner);
    if (!qa_source_save_count(io, &models, reading ? (io->input.size - io->offset) / 65 : SIZE_MAX / sizeof(unified_media_model))) return false;
    unified_media_model **model_tail = &owner->models;
    unified_media_model *live = owner->models;
    for (size_t i = 0; i < models; ++i) {
        unified_media_model *row = reading ? calloc(1, sizeof(*row)) : live;
        if (!row) return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Retaining Unified model topology");
        if (reading) { *model_tail = row; model_tail = &row->next; }
        else live = live->next;
        size_t bank = reading ? 0 : bank_index(owner, row->bank);
        uint64_t pool = 0, resource = 0, model = 0, scene = 0, brush = 0;
        if (!reading && (!qa_application_content_resource_id(refs->content, row->resource, &pool, &resource) ||
            (row->scene && (!frontend_model_encode(refs->models, row->source ? row->source : &row->decoded, &model, io->error) ||
                !frontend_scene_root_encode(refs->roots, row->scene, &scene, io->error))) ||
            (row->world && !frontend_world_encode(refs->roots, row->world, &brush, io->error)))) return false;
        if (!reading && row->scene) {
            if (model == UINT64_MAX) return false;
            ++model;
        }
        if (!qa_source_save_count(io, &bank, banks - 1) || !frontend_save_text(io, &row->path) ||
            !row->path || !*row->path || row->path[0] == '*' || !options(io, row) ||
            !qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &resource) || !resource) return false;
        row->bank = bank_at(owner, bank);
        if (!opening(io, row->bank->files, &row->opening) ||
            !qa_source_save_u64(io, &model) || !qa_source_save_u64(io, &scene) ||
            !qa_source_save_u64(io, &brush) || (!model != !scene) || (!scene == !brush)) return false;
        if (reading) {
            row->resource = (qa_resource *)qa_application_content_resource(refs->content, pool, resource);
            if (!row->resource) return false;
            qa_resource_retain(row->resource);
            row->saved_model = model; row->saved_scene = scene; row->saved_world = brush;
        }
        char *requested = qa_vfs_normalize_path(row->path, io->error);
        bool qualified = requested && row->opening.path && !strcmp(requested, row->opening.path) &&
            qa_resource_pool_find(qa_vfs_resources(row->bank->files), row->opening.resource_id) == row->resource;
        free(requested);
        if (!qualified) return false;
    }
    return true;
}
bool frontend_unified_media_checkpoint(frontend_unified_media *owner,
    const frontend_unified_media_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!owner || owner->busy || owner->importing || !frontend_unified_media_current(owner) ||
        !refs || !refs->content || !refs->scene || !refs->models || !refs->roots || !refs->audio ||
        !refs->owner || !out || out->data || out->size)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media capture needs its real idle graph and dictionaries");
    for (unified_media_bank *row = owner->banks; row; row = row->next)
        if (row->constructing || row->construction_failed || !row->content || !row->product || !row->files ||
            !row->images || !row->materials || !row->fonts || !row->sounds ||
            (row->media != NULL) != (row->shader_movies != NULL))
            return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media capture retains an unfinished real content bank");
    qa_source_save_io io = {0}; owner->busy = true;
    bool okay = qa_source_save_writer(&io, qa_application_session(owner->frontend->application), error) &&
        fields(&io, owner, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); owner->busy = false;
    if (!okay && (!error || error->code == QA_OK)) fail(error, "Invalid Unified media ownership graph");
    return okay;
}
bool frontend_unified_media_restore_prepare(qa_frontend *frontend, qa_executable_recipe *recipe,
    const frontend_unified_media_refs *refs, qa_bytes bytes, frontend_unified_media **out, qa_error *error)
{
    if (!frontend || !frontend->source_restoring || !recipe || !refs || !refs->content || !refs->owner ||
        !out || *out || !qa_executable_recipe_current(recipe, qa_executable_recipe_catalog(recipe)))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media import needs its actual restored recipe prefix");
    frontend_unified_media *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Restoring Unified media ownership");
    owner->frontend = frontend; owner->recipe = recipe; owner->importing = true;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        fields(&io, owner, refs) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) {
        (void)frontend_unified_media_destroy(owner, NULL);
        if (!error || error->code == QA_OK) fail(error, "Invalid Unified cold media topology");
        return false;
    }
    *out = owner; return true;
}
static bool world_qualify(frontend_unified_media *owner, const frontend_unified_media_refs *refs,
    uint64_t key, unified_media_bank *bank, const qa_resource *resource,
    frontend_scene_owner_kind kind, uint64_t row, qa_error *error)
{
    (void)owner;
    frontend_world_source source; frontend_scene_owner scope;
    return key && key - 1 <= SIZE_MAX && frontend_world_inventory_world_at(refs->roots,
        (size_t)(key - 1), &source, &scope) && scope.kind == kind && scope.owner == refs->owner &&
        scope.row == row && source.resource == resource && source.files == bank->files &&
        source.images == bank->images && source.materials == bank->materials &&
        frontend_world_owner_ready(refs->roots, key, kind, refs->owner, error);
}
bool frontend_unified_media_roots_attach(frontend_unified_media *owner,
    const frontend_unified_media_refs *refs, qa_error *error)
{
    if (!owner || !owner->importing || owner->busy || owner->roots_attached ||
        !owner->frontend->source_restoring || !refs || !refs->roots || !refs->models || !refs->owner)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified root import requires its isolated dictionary owner");
    bool okay = world_qualify(owner, refs, owner->saved_world, owner->world_bank,
        qa_executable_recipe_map(owner->recipe), FRONTEND_SCENE_OWNER_UNIFIED_MAP, 1, error);
    size_t ordinal = 0;
    for (unified_media_model *row = owner->models; okay && row; row = row->next, ++ordinal) {
        if (row->saved_world) {
            okay = world_qualify(owner, refs, row->saved_world, row->bank, row->resource,
                FRONTEND_SCENE_OWNER_UNIFIED_MODEL, ordinal + 1, error);
            continue;
        }
        frontend_model_source parsed; frontend_scene_root_view scene;
        okay = row->saved_model && row->saved_model - 1 <= SIZE_MAX && row->saved_scene &&
            row->saved_scene - 1 <= SIZE_MAX && frontend_model_source_at(refs->models,
                (size_t)(row->saved_model - 1), &parsed) && parsed.resource == row->resource &&
            parsed.files == row->bank->files && !parsed.parent &&
            frontend_world_inventory_model_at(refs->roots, (size_t)(row->saved_scene - 1), &scene) &&
            scene.source.model == parsed.model && scene.source.resource == row->resource &&
            scene.source.files == row->bank->files && !scene.source.parent &&
            scene.owner.row == ordinal + 1 && frontend_scene_root_owner_ready(refs->roots,
                row->saved_scene, FRONTEND_SCENE_OWNER_UNIFIED_MODEL, refs->owner, error) &&
            frontend_model_source_qualify(refs->models, parsed.model, row->bank->images,
                row->bank->materials, &row->options, error) &&
            frontend_model_retain(refs->models, parsed.model, &row->source_lease, error);
        if (okay) row->source = parsed.model;
    }
    if (!okay) return (!error || error->code == QA_OK) ? fail(error, "Unified scene roots leave their exact destructor owners") : false;
    if (!frontend_world_decode(refs->roots, owner->saved_world, &owner->world, error)) return false;
    frontend_world_adopt(refs->roots, owner->saved_world);
    for (unified_media_model *row = owner->models; row; row = row->next) {
        if (row->saved_world) {
            if (!frontend_world_decode(refs->roots, row->saved_world, &row->world, error)) return false;
            frontend_world_adopt(refs->roots, row->saved_world);
        } else {
            if (!frontend_scene_root_decode(refs->roots, row->saved_scene, &row->scene, error)) return false;
            frontend_scene_root_adopt(refs->roots, row->saved_scene);
        }
    }
    owner->roots_attached = true; return true;
}
bool frontend_unified_media_restore_finish(frontend_unified_media *owner,
    const frontend_unified_media_refs *refs, qa_error *error)
{
    if (!owner || !owner->importing || owner->busy || !owner->roots_attached ||
        !owner->frontend->source_restoring || !refs || !refs->content || !refs->scene ||
        !refs->models || !refs->roots || !refs->audio || !refs->owner)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media finish requires all actual imported dictionaries");
    if (!frontend_unified_material_movies_restore_ready(owner, error)) return false;
    owner->busy = true; bool okay = true; size_t ordinal = 0;
    for (unified_media_bank *row = owner->banks; okay && row; row = row->next, ++ordinal)
        if (row->q3_assets && !row->assets_restored) {
            asset_scope scope = {owner, refs, row, ordinal}; qa_q3_asset_owner_refs registry = asset_refs(&scope);
            okay = (!row->saved_map || qa_q3_assets_prepare_restored_map(row->q3_assets, owner->world,
                (qa_collision_geometry *)qa_executable_recipe_geometry(owner->recipe), error)) &&
                qa_q3_assets_owner_restore(row->q3_assets, qa_application_session(owner->frontend->application),
                    &registry, (qa_bytes){row->saved_assets.data, row->saved_assets.size}, error);
            if (okay) {
                row->assets_restored = true;
                qa_buffer_free(&row->saved_assets);
            }
        }
    owner->busy = false;
    if (!okay) return false;
    for (unified_media_bank *row = owner->banks; row; row = row->next) qa_buffer_free(&row->saved_assets);
    owner->importing = false; return frontend_unified_media_current(owner);
}

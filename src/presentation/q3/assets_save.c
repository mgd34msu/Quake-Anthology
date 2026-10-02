#include "internal.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_world_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/hash.h"
#include "qa/binary.h"
#include "qa/vfs_view_save.h"
#include "qa/q3_assets_custody.h"

static bool observed(const qa_q3_presentation_assets *a, qa_error *error)
{
    return a && (!a->busy || (a->capturing && !a->codec_busy)) ? true :
        q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 asset observation requires an idle or captured owner");
}
bool qa_q3_assets_capture_begin(qa_q3_presentation_assets *a, qa_error *error)
{
    if (!a || a->busy || !a->users || !q3p_assets_children_idle(a))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 asset capture requires an idle live owner");
    a->busy = 1; a->capturing = true; return true;
}
void qa_q3_assets_capture_end(qa_q3_presentation_assets *a)
{
    if (!a || !a->capturing || a->codec_busy) return;
    a->capturing = false; a->busy = 0;
}
bool qa_q3_assets_model_count(const qa_q3_presentation_assets *a, size_t *out, qa_error *error)
{
    if (!out || !observed(a, error)) return false;
    *out = a->model_count; return true;
}
bool qa_q3_assets_model_holder(const qa_q3_presentation_assets *a, size_t ordinal,
    qa_q3_asset_model_holder *out, qa_error *error)
{
    if (!out || !observed(a, error) || ordinal >= a->model_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 model holder ordinal is absent");
    const q3p_model *m = a->models[ordinal]; *out = (qa_q3_asset_model_holder){0};
    if (!m) return true;
    out->present = true; out->has_lods = m->has_lods; out->owns_world = m->owns_world;
    out->shared_parent = q3p_model_shared(a, m);
    out->source_registration = m->source_registration;
    out->registration_bad = m->registration_bad; out->source_kind = m->source_kind;
    out->source_num_lods = m->source_num_lods;
    out->source_md4 = m->borrowed_models ? m->source_md4 :
        (m->source_md4_resource ? &m->source_md4_model : NULL);
    out->source_md4_resource = m->source_md4_resource;
    out->source_md4_scene = m->source_md4_scene;
    out->provider = m->provider; out->resource = m->resource; out->world = m->world;
    out->inline_model = m->inline_model; out->lods = m->has_lods ? &m->lods : NULL;
    for (unsigned i = 0; i < 3; ++i) {
        out->lod_resources[i] = m->lod_resources[i]; out->scenes[i] = m->scene[i];
        out->sources[i] = m->has_lods || !i ? q3p_model_source(m, i) : NULL;
    }
    return true;
}
bool qa_q3_assets_skin_count(const qa_q3_presentation_assets *a, size_t *out, qa_error *error)
{
    if (!out || !observed(a, error)) return false;
    *out = a->skin_count; return true;
}
bool qa_q3_assets_skin_holder(const qa_q3_presentation_assets *a, size_t ordinal,
    qa_q3_asset_skin_holder *out, qa_error *error)
{
    if (!out || !observed(a, error) || ordinal >= a->skin_count || !a->skins[ordinal])
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 skin holder ordinal is absent");
    const q3p_skin *skin = a->skins[ordinal];
    *out = (qa_q3_asset_skin_holder){skin->provider, skin->resource, &skin->map,
        q3p_skin_shared(a, skin)}; return true;
}
bool qa_q3_assets_services(const qa_q3_presentation_assets *a,
    qa_q3_presentation_asset_options *out, qa_scene_world **world,
    qa_collision_geometry **geometry, qa_error *error)
{
    if (!out || !world || !geometry || !observed(a, error)) return false;
    *out = a->options; *world = a->world; *geometry = a->geometry; return true;
}

bool qa_q3_assets_prepare_restored_map(qa_q3_presentation_assets *a,
    qa_scene_world *world, qa_collision_geometry *geometry, qa_error *error)
{
    if (!a || !a->users || a->busy || a->codec_busy ||
        ((a->world || a->geometry) && (a->world != world || a->geometry != geometry)) ||
        a->name_count || a->name_capacity || a->model_count || a->model_capacity ||
        a->skin_count || a->skin_capacity || a->shader_count || a->shader_capacity ||
        a->sound_count || a->sound_capacity || (!world != !geometry) ||
        (world && !qa_scene_world_observation_ready(world)))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 cold map binding requires an actual empty registry");
    if (!qa_q3_assets_map_hold(a, world, geometry, error)) return false;
    a->world = world; a->geometry = geometry; return true;
}
static bool same_bytes(qa_bytes a, qa_bytes b)
{ return a.size == b.size && (!a.size || (a.data && b.data && !memcmp(a.data, b.data, a.size))); }
static bool resource_owned(const qa_q3_presentation_provider *provider, const qa_resource *resource)
{
    return !resource || qa_resource_pool_find(qa_vfs_resources(provider->mounts),
        qa_resource_id(resource)) == resource;
}
static bool reading(const qa_source_save_io *io) { return io->direction == QA_SOURCE_SAVE_READ; }
static bool signature(qa_source_save_io *io, uint32_t *schema)
{
    uint8_t magic[4] = {'Q','3','A','S'}; uint32_t version = 6;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q3AS", 4) ||
        !qa_source_save_u32(io, &version) || version < 3 || version > 6) return false;
    *schema = version; return true;
}
bool qa_q3_assets_owner_parent_key(qa_bytes bytes, uint64_t *out, qa_error *error)
{
    if (!out) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 parent key requires an output");
    qa_bytes header;
    if (!qa_bytes_slice(bytes, 0, 8, &header, error)) return false;
    uint32_t schema = qa_load_u32le(header.data + 4);
    if (memcmp(header.data, "Q3AS", 4) || schema < 3 || schema > 6)
        return q3p_fail(error, QA_ERROR_FORMAT, "Unsupported Q3 asset parent prefix");
    uint64_t key = 0;
    if (schema >= 5) {
        qa_bytes parent;
        if (!qa_bytes_slice(bytes, 8, 9, &parent, error)) return false;
        if (parent.data[8] > 1)
            return q3p_fail(error, QA_ERROR_FORMAT, "Invalid Q3 asset retired flag");
        key = qa_load_u64le(parent.data);
    }
    *out = key; return true;
}
static bool refs_ready(const qa_q3_asset_owner_refs *r)
{
    return r && r->services_encode && r->services_qualify && r->provider_encode && r->provider_decode &&
        r->resource_encode && r->resource_decode && r->model_encode && r->model_decode && r->model_retain &&
        r->scene_encode && r->scene_decode && r->world_encode && r->world_decode &&
        r->collision_encode && r->collision_decode && r->material_encode && r->material_decode &&
        r->audio_encode && r->audio_decode && r->scene_owned_ready && r->world_owned_ready &&
        r->scene_adopt && r->world_adopt;
}
static bool provider_fields(qa_source_save_io *io, qa_q3_presentation_provider *p,
    const qa_q3_asset_owner_refs *r)
{
    uint64_t id = 0; uint32_t family = p->family;
    if ((!reading(io) && !r->provider_encode(r->context, p, &id, io->error)) ||
        !qa_source_save_u64(io, &id) || !qa_source_save_u32(io, &family) || family > QA_SCENE_Q3) return false;
    if (reading(io) && !r->provider_decode(r->context, id, p, io->error)) return false;
    return p->family == (qa_scene_family)family && p->mounts && p->images && p->materials &&
        qa_scene_resources_files(p->images) == p->mounts &&
        qa_material_library_resource_owner(p->materials) == p->images;
}
static bool resource_fields(qa_source_save_io *io, qa_resource **value,
    const qa_q3_asset_owner_refs *r)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return *value == NULL;
    uint64_t pool = 0, id = 0; qa_sha256_digest digest = {{0}};
    if (!reading(io)) {
        const qa_sha256_digest *actual = qa_resource_digest(*value);
        if (!actual || !r->resource_encode(r->context, *value, &pool, &id, io->error)) return false;
        digest = *actual;
    }
    if (!qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &id) ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes))) return false;
    if (reading(io)) {
        const qa_resource *candidate = NULL;
        if (*value || !r->resource_decode(r->context, pool, id, &candidate, io->error) || !candidate ||
            !qa_resource_digest(candidate) || memcmp(digest.bytes, qa_resource_digest(candidate)->bytes, sizeof(digest.bytes))) return false;
        qa_resource_retain((qa_resource *)candidate); *value = (qa_resource *)candidate;
    }
    return true;
}
static bool source_fields(qa_source_save_io *io, const qa_model **value, const qa_resource *resource,
    const qa_q3_asset_owner_refs *r)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return *value == NULL;
    uint64_t id = 0;
    if ((!reading(io) && !r->model_encode(r->context, *value, &id, io->error)) || !qa_source_save_u64(io, &id) || !resource) return false;
    qa_bytes bytes = qa_resource_bytes(resource);
    if (reading(io) && !r->model_decode(r->context, id, bytes, value, io->error)) return false;
    return *value && same_bytes(bytes, (qa_bytes){(*value)->source.data, (*value)->source.size});
}
static bool world_fields(qa_source_save_io *io, qa_scene_world **value, uint64_t *ordinal,
    const qa_q3_asset_owner_refs *r)
{
    bool present = *value != NULL; uint64_t id = 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return *value == NULL;
    if ((!reading(io) && !r->world_encode(r->context, *value, &id, io->error)) || !qa_source_save_u64(io, &id)) return false;
    if (reading(io) && (!r->world_decode(r->context, id, value, io->error) || !*value)) return false;
    if (reading(io) && ordinal) *ordinal = id;
    return qa_scene_world_observation_ready(*value);
}
static bool scene_fields(qa_source_save_io *io, qa_scene_model **value, uint64_t *ordinal,
    const qa_q3_asset_owner_refs *r)
{
    bool present = *value != NULL; uint64_t id = 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return *value == NULL;
    if ((!reading(io) && !r->scene_encode(r->context, *value, &id, io->error)) || !qa_source_save_u64(io, &id)) return false;
    if (reading(io) && (!r->scene_decode(r->context, id, value, io->error) || !*value)) return false;
    if (reading(io)) *ordinal = id;
    return qa_scene_model_observation_ready(*value);
}
static bool audio_fields(qa_source_save_io *io, qa_audio_asset **value, const qa_q3_asset_owner_refs *r,
    bool qualify)
{
    bool present = *value != NULL; uint64_t id = 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return *value == NULL;
    if ((!reading(io) && !r->audio_encode(r->context, *value, &id, io->error)) || !qa_source_save_u64(io, &id)) return false;
    if (reading(io)) {
        qa_audio_asset *candidate = NULL;
        if (!r->audio_decode(r->context, id, &candidate, io->error) || !candidate) return false;
        if (qualify) return candidate == *value;
        if (*value || !qa_audio_asset_retain(candidate)) return false;
        *value = candidate;
    }
    return true;
}
static bool allocation_fields(qa_source_save_io *io, void **array, size_t *count,
    size_t *capacity, size_t width)
{
    size_t n = *count, cap = *capacity;
    size_t maximum = reading(io) ? io->input.size : INT32_MAX;
    if (!qa_source_save_count(io, &n, maximum) || !qa_source_save_count(io, &cap,
        reading(io) && io->input.size <= SIZE_MAX - 32 ? io->input.size + 32 : SIZE_MAX) ||
        n > cap || n > INT32_MAX || cap > SIZE_MAX / width) return false;
    if (reading(io)) {
        if (*array) return false;
        void *candidate = cap ? calloc(cap, width) : NULL;
        if (cap && !candidate) return q3p_fail(io->error, QA_ERROR_MEMORY, "Allocating restored Q3 physical handle table");
        *array = candidate; *count = n; *capacity = cap;
    } else if (cap && !*array) return false;
    return true;
}
static bool private_text(qa_source_save_io *io, char **text)
{
    bool present = !reading(io) && *text;
    size_t length = present ? strlen(*text) : 0;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return !reading(io) || *text == NULL;
    if (!qa_source_save_count(io, &length, reading(io) ? io->input.size - io->offset : SIZE_MAX - 1) ||
        length == SIZE_MAX) return false;
    if (!reading(io)) return qa_source_save_bytes(io, *text, length);
    if (*text) return false;
    char *copy = malloc(length + 1);
    if (!copy) return q3p_fail(io->error, QA_ERROR_MEMORY, "Retaining restored private Q3 text");
    if (!qa_source_save_bytes(io, copy, length) || memchr(copy, 0, length)) {
        free(copy); return false;
    }
    copy[length] = 0; *text = copy;
    return true;
}
static bool opening_empty(const qa_vfs_acquisition *opening, int64_t rank,
    const q3p_opening_order *order)
{
    return !opening->mount && !opening->resource_id && !opening->path && !opening->lookup_path &&
        !opening->link_source && !opening->link_target && !rank && !order->mounts && !order->count &&
        !order->prefix && !order->user_overlay && !opening->opening_present &&
        !opening->opening.rank && !opening->opening.order && !opening->opening.order_count &&
        !opening->opening.prefix && !opening->opening.user_overlay;
}
static bool opening_fields(qa_source_save_io *io, qa_vfs_acquisition *opening,
    int64_t *rank, q3p_opening_order *order, const qa_resource *resource,
    const qa_q3_presentation_provider *provider, const char *path, uint32_t schema)
{
    if (!resource) return opening_empty(opening, *rank, order);
    if (!qa_source_save_u64(io, &opening->mount) || !qa_source_save_u64(io, &opening->resource_id) ||
        !private_text(io, &opening->path) || !opening->path ||
        !private_text(io, &opening->lookup_path) || !opening->lookup_path ||
        !private_text(io, &opening->link_source) || !opening->link_source ||
        !private_text(io, &opening->link_target) || !opening->link_target ||
        !qa_source_save_i64(io, rank) || !qa_source_save_bool(io, &order->user_overlay) ||
        !private_text(io, &order->prefix) ||
        !qa_source_save_count(io, &order->count, reading(io) ? (io->input.size - io->offset) / 8 : SIZE_MAX / sizeof(*order->mounts))) return false;
    if (reading(io) && order->count) {
        order->mounts = calloc(order->count, sizeof(*order->mounts));
        if (!order->mounts) return q3p_fail(io->error, QA_ERROR_MEMORY, "Restoring Q3 model opening order");
    }
    if ((order->count != 0) != (order->mounts != NULL)) return false;
    for (size_t i = 0; i < order->count; ++i) {
        if (!qa_source_save_u64(io, &order->mounts[i]) ||
            !qa_vfs_mount_id_was_issued(provider->mounts, order->mounts[i])) return false;
        for (size_t j = 0; j < i; ++j) if (order->mounts[i] == order->mounts[j]) return false;
    }
    if (opening->link_source[0]) {
        if (*rank != -1 || order->count || order->prefix || order->user_overlay) return false;
    } else {
        if (*rank < 0 || (uint64_t)*rank >= order->count || order->mounts[*rank] != opening->mount ||
            (order->prefix && order->user_overlay)) return false;
        if (order->prefix) {
            size_t length = strlen(order->prefix);
            if (!length || strlen(opening->path) <= length || opening->path[length] != '/') return false;
            for (size_t i = 0; i < length; ++i) {
                unsigned char a = (unsigned char)opening->path[i], b = (unsigned char)order->prefix[i];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) return false;
            }
        }
    }
    char *normalized = qa_vfs_normalize_path(path ? path : opening->path, io->error);
    bool matches = normalized && !strcmp(normalized, opening->path); free(normalized);
    if (!matches || opening->resource_id != qa_resource_id(resource) || !resource_owned(provider, resource) ||
        !qa_vfs_acquisition_retained(provider->mounts, opening, io->error)) return false;
    if (schema < 6) return true;
    if (!qa_vfs_acquisition_opening_codec(io, provider->mounts, opening)) return false;
    if (!opening->opening_present) return true;
    const qa_vfs_read_opening *snapshot = &opening->opening;
    if (snapshot->rank != *rank || snapshot->order_count != order->count ||
        snapshot->user_overlay != order->user_overlay || (!snapshot->prefix != !order->prefix) ||
        (snapshot->prefix && strcmp(snapshot->prefix, order->prefix))) return false;
    for (size_t i = 0; i < order->count; ++i) if (snapshot->order[i] != order->mounts[i]) return false;
    return true;
}
static bool lod_fields(qa_source_save_io *io, q3p_model *m, const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    if (!qa_source_save_u32(io, &m->lods.load_count) || m->lods.load_count > 3 ||
        !qa_source_save_u32(io, &m->lods.lod_count) || m->lods.lod_count > 3 ||
        !qa_source_save_count(io, &m->lods.byte_length, SIZE_MAX)) return false;
    uint32_t loaded = 0; size_t total = 0; bool ordered[3] = {false};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t state = m->lods.states[i];
        if (!qa_source_save_u32(io, &state) || state > QA_MODEL_LOD_ALIAS ||
            !qa_source_save_u32(io, &m->lods.aliases[i]) || m->lods.aliases[i] >= 3 ||
            !qa_source_save_u32(io, &m->lods.load_order[i]) || m->lods.load_order[i] >= 3 ||
            !private_text(io, &m->lods.paths[i]) || !m->lods.paths[i] ||
            !resource_fields(io, &m->lod_resources[i], r) ||
            !resource_owned(&m->provider, m->lod_resources[i]) ||
            !opening_fields(io, &m->lod_openings[i], &m->lod_opening_ranks[i],
                &m->lod_opening_orders[i], m->lod_resources[i], &m->provider, m->lods.paths[i], schema)) return false;
        const char *base = m->opening.path, *dot = strrchr(base, '.');
        size_t stem = dot ? (size_t)(dot - base) : strlen(base);
        if (!i) { if (strcmp(m->lods.paths[i], base)) return false; }
        else {
            const char *suffix = i == 1 ? "_1.md3" : "_2.md3";
            if (stem > SIZE_MAX - 6 || strlen(m->lods.paths[i]) != stem + 6 ||
                memcmp(m->lods.paths[i], base, stem) || strcmp(m->lods.paths[i] + stem, suffix)) return false;
        }
        if (reading(io)) m->lods.states[i] = (qa_model_lod_state)state;
        if (i < m->lods.load_count) {
            uint32_t slot = m->lods.load_order[i];
            if (ordered[slot] || (i && slot >= m->lods.load_order[i - 1])) return false;
            ordered[slot] = true;
        } else if (m->lods.load_order[i]) return false;
        if (state == QA_MODEL_LOD_LOADED || state == QA_MODEL_LOD_ALIAS) ++loaded;
    }
    if (!m->lods.load_count || loaded != m->lods.lod_count || !loaded) return false;
    for (unsigned i = 0; i < 3; ++i) {
        qa_model_lod_state state = m->lods.states[i];
        if ((state == QA_MODEL_LOD_LOADED || state == QA_MODEL_LOD_INVALID) != (m->lod_resources[i] != NULL) ||
            ordered[i] != (m->lod_resources[i] != NULL)) return false;
        const qa_model *source = reading(io) ? NULL : q3p_model_source(m, i);
        const qa_resource *resource = m->lod_resources[i];
        if (state == QA_MODEL_LOD_ALIAS) {
            unsigned slot = i;
            for (unsigned depth = 0; depth < 3 && m->lods.states[slot] == QA_MODEL_LOD_ALIAS; ++depth) {
                if (m->lods.aliases[slot] != slot + 1 || m->lods.aliases[slot] >= 3) return false;
                slot = m->lods.aliases[slot];
            }
            if (m->lods.states[slot] != QA_MODEL_LOD_LOADED) return false;
            resource = m->lod_resources[slot];
        }
        if (!source_fields(io, &source, resource, r)) return false;
        if ((state == QA_MODEL_LOD_LOADED || state == QA_MODEL_LOD_ALIAS) != (source != NULL)) return false;
        if (reading(io)) m->sources[i] = source;
        if (state == QA_MODEL_LOD_LOADED) {
            if (source->format != QA_MODEL_MD3 || source->source.size < 108) return false;
            size_t length = qa_load_u32le(source->source.data + 104);
            if (length > SIZE_MAX - total) return false;
            total += length;
        }
    }
    if (total != m->lods.byte_length || !q3p_model_source(m, 0)) return false;
    for (unsigned i = 0; i < 3; ++i) if (m->lods.states[i] == QA_MODEL_LOD_ALIAS &&
        q3p_model_source(m, i) != q3p_model_source(m, m->lods.aliases[i])) return false;
    return true;
}
static bool ordinary_model_fields(qa_source_save_io *io, q3p_model *m,
    qa_q3_presentation_assets *a, const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    if (!provider_fields(io, &m->provider, r) || !resource_fields(io, &m->resource, r) ||
        !resource_owned(&m->provider, m->resource) ||
        !private_text(io, &m->first_requested_path) || !m->first_requested_path || !*m->first_requested_path ||
        !opening_fields(io, &m->opening, &m->opening_rank, &m->opening_order,
            m->resource, &m->provider, m->first_requested_path, schema) ||
        !qa_source_save_bool(io, &m->has_lods) || !qa_source_save_bool(io, &m->owns_world) ||
        !qa_source_save_u32(io, &m->inline_model) ||
        !qa_source_save_vec3(io, &m->bounds.mins) || !qa_source_save_vec3(io, &m->bounds.maxs) ||
        !world_fields(io, &m->world, &m->world_ordinal, r)) return false;
    if (!m->has_lods) for (unsigned i = 0; i < 3; ++i)
        if (m->lod_resources[i] || !opening_empty(&m->lod_openings[i],
            m->lod_opening_ranks[i], &m->lod_opening_orders[i])) return false;
    if ((m->first_requested_path[0] == '*') != (!m->resource && m->world && !m->owns_world)) return false;
    if (m->world) {
        if (m->has_lods || (m->owns_world != (m->resource != NULL)) ||
            qa_scene_world_resource_owner(m->world) != m->provider.images ||
            qa_scene_world_material_owner(m->world) != m->provider.materials ||
            m->inline_model >= qa_scene_world_model_count(m->world)) return false;
        if (!m->owns_world && (m->world != a->world || !a->geometry ||
            m->inline_model >= qa_collision_model_count(a->geometry))) return false;
    } else {
        if (!m->resource || m->owns_world || m->inline_model) return false;
        if (m->has_lods) { if (!lod_fields(io, m, r, schema)) return false; }
        else {
            const qa_model *source = reading(io) ? NULL : q3p_model_source(m, 0);
            if (!source_fields(io, &source, m->resource, r) || !source) return false;
            if (reading(io)) m->sources[0] = source;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!scene_fields(io, &m->scene[i], &m->scene_ordinals[i], r)) return false;
        const qa_model *source = m->has_lods || !i ? q3p_model_source(m, i) : NULL;
        if ((source != NULL) != (m->scene[i] != NULL)) return false;
        if (m->scene[i] && (qa_scene_model_source(m->scene[i]) != source ||
            qa_scene_model_resource_owner(m->scene[i]) != m->provider.images ||
            qa_scene_model_material_owner(m->scene[i]) != m->provider.materials)) return false;
        for (unsigned j = 0; j < i; ++j)
            if ((source && source == q3p_model_source(m, j)) != (m->scene[i] && m->scene[i] == m->scene[j])) return false;
    }
    if (reading(io) && !m->world) for (unsigned i = 0; i < (m->has_lods ? 3u : 1u); ++i) {
        const qa_model *source = q3p_model_source(m, i); bool shared = false;
        for (unsigned j = 0; j < i; ++j) if (source == q3p_model_source(m, j)) shared = true;
        if (source && !shared && (!r->model_retain(r->context, source, &m->source_leases[i], io->error) ||
            !m->source_leases[i].context || !m->source_leases[i].release)) return false;
    }
    return true;
}
static bool source_lod_path(const q3p_model *m, unsigned slot, qa_error *error)
{
    char *base = qa_vfs_normalize_path(m->first_requested_path, error);
    if (!base) return false;
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);
    const char *path = m->lods.paths[slot];
    bool same = path && (!slot ? !strcmp(path, base) :
        stem <= SIZE_MAX - 6 && strlen(path) == stem + 6 &&
        !memcmp(path, base, stem) && !strcmp(path + stem, slot == 1 ? "_1.md3" : "_2.md3"));
    free(base); return same;
}
static bool source_model_fields(qa_source_save_io *io, q3p_model *m,
    const qa_q3_asset_owner_refs *r)
{
    uint32_t kind = m->source_kind;
    if (!qa_source_save_bool(io, &m->registration_bad) ||
        !qa_source_save_u32(io, &kind) || (kind != QA_MODEL_MD3 && kind != QA_MODEL_MD4) ||
        !qa_source_save_u32(io, &m->source_num_lods) || m->source_num_lods > 3 ||
        !provider_fields(io, &m->provider, r) || !resource_fields(io, &m->resource, r) ||
        !resource_owned(&m->provider, m->resource) ||
        !private_text(io, &m->first_requested_path) || !m->first_requested_path ||
        !*m->first_requested_path || strlen(m->first_requested_path) >= 64 ||
        !opening_fields(io, &m->opening, &m->opening_rank, &m->opening_order,
            m->resource, &m->provider, m->opening.path, 6) || (m->resource && !m->opening.opening_present) ||
        !qa_source_save_vec3(io, &m->bounds.mins) || !qa_source_save_vec3(io, &m->bounds.maxs) ||
        !qa_source_save_u32(io, &m->lods.load_count) || m->lods.load_count > 3 ||
        !qa_source_save_u32(io, &m->lods.lod_count) || m->lods.lod_count > 3 ||
        !qa_source_save_count(io, &m->lods.byte_length, SIZE_MAX)) return false;
    if (reading(io)) { m->source_kind = (qa_model_format)kind; m->has_lods = true; }
    if (!m->has_lods || m->world || m->owns_world || m->inline_model) return false;
    bool ordered[3] = {false}; uint32_t successful = 0, aliases = 0;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t state = m->lods.states[i];
        if (!qa_source_save_u32(io, &state) || state > QA_MODEL_LOD_ALIAS ||
            !qa_source_save_u32(io, &m->lods.aliases[i]) || m->lods.aliases[i] >= 3 ||
            !qa_source_save_u32(io, &m->lods.load_order[i]) || m->lods.load_order[i] >= 3 ||
            !qa_source_save_bool(io, &m->source_md4_slots[i]) ||
            !private_text(io, &m->lods.paths[i]) || !source_lod_path(m, i, io->error) ||
            !resource_fields(io, &m->lod_resources[i], r) ||
            !resource_owned(&m->provider, m->lod_resources[i]) ||
            !opening_fields(io, &m->lod_openings[i], &m->lod_opening_ranks[i],
                &m->lod_opening_orders[i], m->lod_resources[i], &m->provider, m->lods.paths[i], 6) ||
            (m->lod_resources[i] && !m->lod_openings[i].opening_present)) return false;
        if (reading(io)) m->lods.states[i] = (qa_model_lod_state)state;
        if (i < m->lods.load_count) {
            uint32_t slot = m->lods.load_order[i];
            if (ordered[slot] || (i && slot >= m->lods.load_order[i - 1])) return false;
            ordered[slot] = true;
        } else if (m->lods.load_order[i]) return false;
        if (state == QA_MODEL_LOD_LOADED || m->source_md4_slots[i]) ++successful;
        if (state == QA_MODEL_LOD_ALIAS) {
            if (i == 2 || m->lods.aliases[i] != i + 1 || m->lod_resources[i]) return false;
            ++aliases;
        } else if (m->lods.aliases[i]) return false;
        if (m->source_md4_slots[i] && (state != QA_MODEL_LOD_MISSING || !m->lod_resources[i])) return false;
        if ((state == QA_MODEL_LOD_LOADED || state == QA_MODEL_LOD_INVALID) && !m->lod_resources[i]) return false;
    }
    if (successful + aliases != m->source_num_lods ||
        m->lods.lod_count != m->source_num_lods ||
        (!m->registration_bad && !successful)) return false;
    int failed_slot = -1; bool unknown = false;
    for (unsigned i = 0; i < 3; ++i) if (m->lod_resources[i]) {
        bool failure = m->lods.states[i] == QA_MODEL_LOD_INVALID ||
            (m->lods.states[i] == QA_MODEL_LOD_MISSING && !m->source_md4_slots[i]);
        if (!failure) continue;
        if (failed_slot >= 0 || !m->lods.load_count ||
            i != m->lods.load_order[m->lods.load_count - 1]) return false;
        failed_slot = (int)i;
        unknown = m->lods.states[i] == QA_MODEL_LOD_MISSING;
        qa_bytes bytes = qa_resource_bytes(m->lod_resources[i]);
        bool known = bytes.size >= 4 && (!memcmp(bytes.data, "IDP3", 4) || !memcmp(bytes.data, "IDP4", 4));
        if (unknown == known) return false;
    }
    if (m->registration_bad != (!successful || unknown || failed_slot == 0)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if ((m->lods.states[i] == QA_MODEL_LOD_ALIAS) !=
            (!m->registration_bad && failed_slot > 0 && i < (unsigned)failed_slot)) return false;
    size_t allocations = 0;
    for (unsigned i = 0; i < 3; ++i) if (m->lods.states[i] == QA_MODEL_LOD_LOADED || m->source_md4_slots[i]) {
        qa_bytes bytes = qa_resource_bytes(m->lod_resources[i]);
        size_t end = m->source_md4_slots[i] ? 96 : 104;
        if (bytes.size < end + 4 || memcmp(bytes.data, m->source_md4_slots[i] ? "IDP4" : "IDP3", 4)) return false;
        size_t length = qa_load_u32le(bytes.data + end);
        if (length > bytes.size || length > SIZE_MAX - allocations) return false;
        allocations += length;
    }
    if (allocations != m->lods.byte_length) return false;
    const qa_resource *primary = NULL; unsigned primary_slot = 3, md4_slot = 3;
    uint32_t latest_kind = QA_MODEL_MD3;
    for (unsigned n = 0; n < 3; ++n) {
        if (ordered[n] != (m->lod_resources[n] != NULL)) return false;
        if (n >= m->lods.load_count) continue;
        unsigned slot = m->lods.load_order[n];
        if (!primary) { primary = m->lod_resources[slot]; primary_slot = slot; }
        if (m->lods.states[slot] == QA_MODEL_LOD_LOADED || m->source_md4_slots[slot]) {
            primary = m->lod_resources[slot];
            primary_slot = slot;
            latest_kind = m->source_md4_slots[slot] ? QA_MODEL_MD4 : QA_MODEL_MD3;
            if (m->source_md4_slots[slot]) md4_slot = slot;
        }
    }
    if (m->resource != primary || kind != latest_kind ||
        (primary_slot < 3 && strcmp(m->opening.path, m->lod_openings[primary_slot].path))) return false;
    for (unsigned i = 0; i < 3; ++i) {
        const qa_model *source = reading(io) ? NULL : q3p_model_source(m, i);
        unsigned target = i;
        for (unsigned depth = 0; depth < 3 && m->lods.states[target] == QA_MODEL_LOD_ALIAS; ++depth)
            target = m->lods.aliases[target];
        const qa_resource *resource = m->lods.states[target] == QA_MODEL_LOD_LOADED ? m->lod_resources[target] : NULL;
        if (!source_fields(io, &source, resource, r) ||
            (resource != NULL) != (source != NULL) || (source && source->format != QA_MODEL_MD3)) return false;
        if (reading(io)) m->sources[i] = source;
        if (!scene_fields(io, &m->scene[i], &m->scene_ordinals[i], r) ||
            (source != NULL) != (m->scene[i] != NULL) || (m->scene[i] &&
            (qa_scene_model_source(m->scene[i]) != source ||
             qa_scene_model_resource_owner(m->scene[i]) != m->provider.images ||
             qa_scene_model_material_owner(m->scene[i]) != m->provider.materials))) return false;
        for (unsigned j = 0; j < i; ++j)
            if ((source && source == q3p_model_source(m, j)) !=
                (m->scene[i] && m->scene[i] == m->scene[j])) return false;
    }
    for (unsigned i = 0; i < 3; ++i) if (m->lods.states[i] == QA_MODEL_LOD_ALIAS &&
        (q3p_model_source(m, i) != q3p_model_source(m, m->lods.aliases[i]) ||
         m->scene[i] != m->scene[m->lods.aliases[i]])) return false;
    if (!resource_fields(io, &m->source_md4_resource, r) ||
        !resource_owned(&m->provider, m->source_md4_resource) ||
        m->source_md4_resource != (md4_slot < 3 ? m->lod_resources[md4_slot] : NULL) ||
        !opening_fields(io, &m->source_md4_opening, &m->source_md4_rank, &m->source_md4_order,
            m->source_md4_resource, &m->provider, md4_slot < 3 ? m->lods.paths[md4_slot] : NULL, 6) ||
        (m->source_md4_resource && !m->source_md4_opening.opening_present)) return false;
    const qa_model *md4 = reading(io) ? NULL : (m->borrowed_models ? m->source_md4 :
        (m->source_md4_resource ? &m->source_md4_model : NULL));
    if (!source_fields(io, &md4, m->source_md4_resource, r) ||
        (md4 != NULL) != (m->source_md4_resource != NULL) || (md4 && md4->format != QA_MODEL_MD4) ||
        !scene_fields(io, &m->source_md4_scene, &m->source_md4_scene_ordinal, r) ||
        (md4 != NULL) != (m->source_md4_scene != NULL) || (m->source_md4_scene &&
        (qa_scene_model_source(m->source_md4_scene) != md4 ||
         qa_scene_model_resource_owner(m->source_md4_scene) != m->provider.images ||
         qa_scene_model_material_owner(m->source_md4_scene) != m->provider.materials))) return false;
    if (reading(io)) {
        m->source_md4 = md4;
        for (unsigned i = 0; i < 3; ++i) {
            const qa_model *source = m->sources[i]; bool shared = false;
            for (unsigned j = 0; j < i; ++j) if (source == m->sources[j]) shared = true;
            if (source && !shared && (!r->model_retain(r->context, source, &m->source_leases[i], io->error) ||
                !m->source_leases[i].context || !m->source_leases[i].release)) return false;
        }
        if (md4 && (!r->model_retain(r->context, md4, &m->source_md4_lease, io->error) ||
            !m->source_md4_lease.context || !m->source_md4_lease.release)) return false;
    }
    return true;
}
static bool model_fields(qa_source_save_io *io, q3p_model *m,
    qa_q3_presentation_assets *a, const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    if (schema >= 6 && !qa_source_save_bool(io, &m->source_registration)) return false;
    if (m->source_registration) return
        qa_material_library_has_source_profile(a->options.provider.materials) && source_model_fields(io, m, r);
    return ordinary_model_fields(io, m, a, r, schema);
}
static bool skin_fields(qa_source_save_io *io, q3p_skin *skin, const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    size_t count = skin->map.count, capacity = skin->map.capacity;
    if (schema >= 4) {
        if (!qa_source_save_bool(io, &skin->source_registration)) return false;
    } else if (reading(io)) skin->source_registration = false;
    if (!provider_fields(io, &skin->provider, r) || !resource_fields(io, &skin->resource, r) ||
        (!skin->resource && !skin->source_registration) ||
        !resource_owned(&skin->provider, skin->resource) ||
        !qa_source_save_count(io, &count, reading(io) ? io->input.size / 68 : SIZE_MAX) ||
        !qa_source_save_count(io, &capacity, reading(io) ? io->input.size / 64 : SIZE_MAX) ||
        count > capacity || (!count && capacity) || (capacity && (capacity < 8 || (capacity & (capacity - 1)))) ||
        capacity > SIZE_MAX / sizeof(*skin->map.mappings)) return false;
    if (reading(io) && capacity) {
        skin->map.mappings = calloc(capacity, sizeof(*skin->map.mappings));
        if (!skin->map.mappings) return q3p_fail(io->error, QA_ERROR_MEMORY, "Allocating restored Q3 skin mappings");
    }
    if (capacity && !skin->map.mappings) return false;
    if (reading(io)) { skin->map.count = count; skin->map.capacity = capacity; }
    for (size_t i = 0; i < capacity; ++i) {
        qa_model_skin_mapping *mapping = &skin->map.mappings[i];
        if (!qa_source_save_bytes(io, mapping->surface, sizeof(mapping->surface)) ||
            !memchr(mapping->surface, 0, sizeof(mapping->surface)) ||
            !private_text(io, &mapping->shader)) return false;
        if (i < count) { if (!mapping->shader) return false; }
        else {
            const uint8_t zero[sizeof(mapping->surface)] = {0};
            if (mapping->shader || memcmp(mapping->surface, zero, sizeof(zero))) {
                if (reading(io)) { free(mapping->shader); mapping->shader = NULL; }
                return false;
            }
        }
    }
    if (!skin->resource && count && (count != 1 || skin->map.mappings[0].surface[0])) return false;
    if (skin->source_registration && count) {
        if (reading(io)) {
            skin->materials = calloc(count, sizeof(*skin->materials));
            if (!skin->materials) return q3p_fail(io->error, QA_ERROR_MEMORY, "Restoring Source skin shader receipts");
        }
        if (!skin->materials) return false;
        for (size_t i = 0; i < count; ++i) {
            uint64_t id = 0;
            if ((!reading(io) && (!skin->materials[i] ||
                !r->material_encode(r->context, skin->materials[i], &id, io->error))) ||
                !qa_source_save_u64(io, &id) ||
                (reading(io) && (!r->material_decode(r->context, id, &skin->materials[i], io->error) ||
                    !skin->materials[i])) || skin->materials[i]->library != skin->provider.materials) return false;
        }
    }
    return true;
}
static bool handles(qa_source_save_io *io, qa_q3_presentation_assets *a, const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    if (!allocation_fields(io, (void **)&a->models, &a->model_count, &a->model_capacity, sizeof(*a->models)) ||
        !allocation_fields(io, (void **)&a->skins, &a->skin_count, &a->skin_capacity, sizeof(*a->skins)) ||
        !allocation_fields(io, (void **)&a->shaders, &a->shader_count, &a->shader_capacity, sizeof(*a->shaders)) ||
        !allocation_fields(io, (void **)&a->sounds, &a->sound_count, &a->sound_capacity, sizeof(*a->sounds))) return false;
    if (qa_material_library_has_source_profile(a->options.provider.materials) &&
        (a->model_count > 1023 || a->skin_count > 1023)) return false;
    for (size_t i = 0; i < a->model_count; ++i) {
        bool present = a->models[i] != NULL;
        if (!qa_source_save_bool(io, &present)) return false;
        if (!present) continue;
        bool shared = !reading(io) && q3p_model_shared(a, a->models[i]);
        if (schema >= 5 && !qa_source_save_bool(io, &shared)) return false;
        if (shared) {
            if (!a->parent || i >= a->parent->model_count || !a->parent->models[i] ||
                (!reading(io) && a->models[i] != a->parent->models[i])) return false;
            if (reading(io)) a->models[i] = a->parent->models[i];
        } else if (reading(io)) {
            a->models[i] = calloc(1, sizeof(*a->models[i]));
            if (!a->models[i]) return q3p_fail(io->error, QA_ERROR_MEMORY, "Allocating restored Q3 model holder");
            a->models[i]->borrowed_models = a->models[i]->borrowed_scenes = a->models[i]->borrowed_world = true;
        }
        if (!shared && !model_fields(io, a->models[i], a, r, schema)) return false;
        for (size_t j = 0; j < i; ++j) if (a->models[j]) {
            q3p_model *prior = a->models[j], *current = a->models[i];
            if (current->owns_world && prior->owns_world && current->world == prior->world) return false;
            for (unsigned x = 0; x < 3; ++x) for (unsigned y = 0; y < 3; ++y)
                if (current->scene[x] && current->scene[x] == prior->scene[y]) return false;
            if (current->source_md4_scene && current->source_md4_scene == prior->source_md4_scene) return false;
            for (unsigned x = 0; x < 3; ++x)
                if ((current->scene[x] && current->scene[x] == prior->source_md4_scene) ||
                    (current->source_md4_scene && current->source_md4_scene == prior->scene[x])) return false;
        }
    }
    for (size_t i = 0; i < a->skin_count; ++i) {
        bool shared = !reading(io) && q3p_skin_shared(a, a->skins[i]);
        if (schema >= 5 && !qa_source_save_bool(io, &shared)) return false;
        if (shared) {
            if (!a->parent || i >= a->parent->skin_count || !a->parent->skins[i] ||
                (!reading(io) && a->skins[i] != a->parent->skins[i])) return false;
            if (reading(io)) a->skins[i] = a->parent->skins[i];
        } else if (reading(io)) {
            a->skins[i] = calloc(1, sizeof(*a->skins[i]));
            if (!a->skins[i]) return q3p_fail(io->error, QA_ERROR_MEMORY, "Allocating restored Q3 skin holder");
        }
        if (!a->skins[i] || (!shared && !skin_fields(io, a->skins[i], r, schema)) ||
            (a->skins[i]->source_registration && !qa_material_library_has_source_profile(a->options.provider.materials))) return false;
    }
    for (size_t i = 0; i < a->shader_count; ++i) {
        uint64_t id = 0;
        if ((!reading(io) && (!a->shaders[i] || !r->material_encode(r->context, a->shaders[i], &id, io->error))) ||
            !qa_source_save_u64(io, &id) ||
            (reading(io) && (!r->material_decode(r->context, id, &a->shaders[i], io->error) || !a->shaders[i]))) return false;
    }
    for (size_t i = 0; i < a->sound_count; ++i)
        if (!audio_fields(io, &a->sounds[i], r, false) || !a->sounds[i]) return false;
    return true;
}
static bool name_valid(const qa_q3_presentation_assets *a, const q3p_name *entry, size_t bucket)
{
    if (entry->kind > Q3P_SOUND || entry->handle < 0 ||
        (entry->generated && entry->kind != Q3P_SHADER) || entry->hash != q3p_name_hash(entry->kind, entry->name) ||
        (entry->hash & (a->name_capacity - 1)) != bucket) return false;
    size_t count = entry->kind == Q3P_MODEL ? a->model_count : entry->kind == Q3P_SKIN ? a->skin_count :
        entry->kind == Q3P_SHADER ? a->shader_count : a->sound_count;
    if ((size_t)entry->handle > count || (entry->kind == Q3P_MODEL && entry->handle &&
        (!a->models[entry->handle - 1] || a->models[entry->handle - 1]->registration_bad))) return false;
    if (entry->kind == Q3P_SHADER) for (const char *p = entry->name; *p; ++p)
        if (*p >= 'A' && *p <= 'Z') return false;
    return true;
}
static bool names(qa_source_save_io *io, qa_q3_presentation_assets *a)
{
    if (!allocation_fields(io, (void **)&a->names, &a->name_count, &a->name_capacity, sizeof(*a->names)) ||
        (a->name_capacity && (a->name_capacity < 32 || (a->name_capacity & (a->name_capacity - 1))))) return false;
    size_t seen = 0;
    for (size_t bucket = 0; bucket < a->name_capacity; ++bucket) {
        size_t count = 0;
        if (!reading(io)) for (const q3p_name *entry = a->names[bucket]; entry; entry = entry->next) {
            if (++count > a->name_count) return false;
        }
        if (!qa_source_save_count(io, &count, a->name_count - seen)) return false;
        q3p_name **tail = &a->names[bucket];
        for (size_t i = 0; i < count; ++i) {
            const q3p_name *entry = reading(io) ? NULL : *tail;
            uint32_t kind = entry ? entry->kind : 0; int32_t handle = entry ? entry->handle : 0;
            uint64_t hash = entry ? entry->hash : 0; char *name = entry ? (char *)entry->name : NULL;
            bool option = entry && entry->option, generated = entry && entry->generated;
            if (!qa_source_save_u32(io, &kind) || kind > Q3P_SOUND || !qa_source_save_u64(io, &hash) ||
                !private_text(io, &name) || !name || !qa_source_save_i32(io, &handle) ||
                !qa_source_save_bool(io, &option) || !qa_source_save_bool(io, &generated)) {
                if (reading(io)) free(name);
                return false;
            }
            if (reading(io)) {
                size_t length = strlen(name);
                if (length > SIZE_MAX - sizeof(q3p_name) - 1 || q3p_find_name(a, (q3p_resource_kind)kind, name)) {
                    free(name); return false;
                }
                q3p_name *candidate = malloc(sizeof(*candidate) + length + 1);
                if (!candidate) { free(name); return q3p_fail(io->error, QA_ERROR_MEMORY, "Allocating restored Q3 name bucket row"); }
                *candidate = (q3p_name){.hash = hash, .kind = (q3p_resource_kind)kind, .handle = handle,
                    .option = option, .generated = generated};
                memcpy(candidate->name, name, length + 1); free(name); *tail = candidate; entry = candidate;
            }
            if (!name_valid(a, entry, bucket)) return false;
            tail = &(*tail)->next;
        }
        seen += count;
    }
    if (seen != a->name_count) return false;
    if (qa_material_library_has_source_profile(a->options.provider.materials)) {
        for (size_t bucket = 0; bucket < a->name_capacity; ++bucket)
            for (const q3p_name *entry = a->names[bucket]; entry; entry = entry->next) {
                if ((entry->kind == Q3P_MODEL || entry->kind == Q3P_SKIN) &&
                    (!*entry->name || strlen(entry->name) >= 64)) return false;
                if (entry->kind != Q3P_SKIN) continue;
                for (size_t prior_bucket = 0; prior_bucket <= bucket; ++prior_bucket)
                    for (const q3p_name *prior = a->names[prior_bucket]; prior; prior = prior->next) {
                        if (prior == entry) break;
                        if (prior->kind != Q3P_SKIN) continue;
                        size_t i = 0;
                        while (entry->name[i] && prior->name[i]) {
                            unsigned char x = (unsigned char)entry->name[i], y = (unsigned char)prior->name[i];
                            if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
                            if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
                            if (x != y) break;
                            ++i;
                        }
                        if (!entry->name[i] && !prior->name[i]) return false;
                    }
            }
    }
    for (q3p_resource_kind kind = Q3P_MODEL; kind <= Q3P_SKIN; ++kind) {
        size_t count = kind == Q3P_MODEL ? a->model_count : kind == Q3P_SKIN ? a->skin_count :
            kind == Q3P_SHADER ? a->shader_count : a->sound_count;
        for (size_t i = 0; i < count; ++i) {
            if (kind == Q3P_MODEL && !a->models[i]) continue;
            if (kind == Q3P_MODEL && a->models[i]->registration_bad) {
                const q3p_name *first = q3p_find_name(a, Q3P_MODEL, a->models[i]->first_requested_path);
                if (!first || first->handle) return false;
                continue;
            }
            if (kind == Q3P_SKIN && a->skins[i]->source_registration && !a->skins[i]->map.count) continue;
            bool found = false;
            for (size_t bucket = 0; bucket < a->name_capacity && !found; ++bucket)
                for (const q3p_name *entry = a->names[bucket]; entry; entry = entry->next)
                    if (entry->kind == kind && entry->handle == (int32_t)i + 1) { found = true; break; }
            if (!found) return false;
            if (kind == Q3P_MODEL) {
                const q3p_name *first = q3p_find_name(a, Q3P_MODEL, a->models[i]->first_requested_path);
                if (!first || first->handle != (int32_t)i + 1) return false;
            }
        }
    }
    return true;
}
static bool parent_fields(qa_source_save_io *io, qa_q3_presentation_assets *a,
    const qa_q3_asset_owner_refs *r, uint32_t schema)
{
    if (schema < 5) return !a->parent && !a->retired;
    uint64_t key = 0;
    if ((!reading(io) && a->parent && (!r->registry_encode ||
        !r->registry_encode(r->context, a->parent, &key, io->error) || !key)) ||
        !qa_source_save_u64(io, &key) || !qa_source_save_bool(io, &a->retired)) return false;
    qa_q3_presentation_assets *parent = a->parent;
    if (reading(io) && key && (!r->registry_decode ||
        !r->registry_decode(r->context, key, &parent, io->error))) return false;
    if ((key != 0) != (parent != NULL)) return false;
    if (parent) {
        if (!parent->users || !parent->retired || parent->codec_busy ||
            (parent->busy && !parent->capturing) || !q3p_assets_children_idle(parent)) return false;
        for (const qa_q3_presentation_assets *row = parent; row; row = row->parent)
            if (row == a) return false;
        if (reading(io)) {
            if (a->parent || !qa_q3_assets_retain(parent, io->error)) return false;
            a->parent = parent;
        }
    }
    return true;
}
static bool asset_fields(qa_source_save_io *io, qa_q3_presentation_assets *a, const qa_q3_asset_owner_refs *r)
{
    uint64_t services = 0; qa_q3_presentation_provider provider = a->options.provider;
    uint32_t schema = 0;
    if (!signature(io, &schema) || !parent_fields(io, a, r, schema) ||
        (!reading(io) && !r->services_encode(r->context, &a->options, &services, io->error)) ||
        !qa_source_save_u64(io, &services) ||
        (reading(io) && !r->services_qualify(r->context, services, &a->options, io->error)) ||
        !provider_fields(io, &provider, r) || provider.mounts != a->options.provider.mounts ||
        provider.images != a->options.provider.images || provider.materials != a->options.provider.materials ||
        provider.family != a->options.provider.family || !audio_fields(io, &a->options.zero_sound, r, true)) return false;
    qa_scene_world *world = reading(io) ? NULL : a->world;
    if (!world_fields(io, &world, NULL, r) || world != a->world) return false;
    bool present = a->geometry != NULL; uint64_t geometry = 0;
    if (!qa_source_save_bool(io, &present) || present != (a->geometry != NULL) || present != (world != NULL)) return false;
    if (present) {
        qa_collision_geometry *candidate = NULL;
        if ((!reading(io) && !r->collision_encode(r->context, a->geometry, &geometry, io->error)) ||
            !qa_source_save_u64(io, &geometry) || (reading(io) &&
            (!r->collision_decode(r->context, geometry, &candidate, io->error) || candidate != a->geometry))) return false;
    }
    return handles(io, a, r, schema) && names(io, a);
}
static bool codec_begin(qa_q3_presentation_assets *a, bool *own_lease, qa_error *error)
{
    if (!a || a->codec_busy || (a->busy && !a->capturing))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 asset codec requires its genuine idle capture lease");
    *own_lease = !a->capturing;
    if (*own_lease && !qa_q3_assets_capture_begin(a, error)) return false;
    a->codec_busy = true; return true;
}
static void codec_end(qa_q3_presentation_assets *a, bool own_lease)
{ a->codec_busy = false; if (own_lease) qa_q3_assets_capture_end(a); }
bool qa_q3_assets_owner_checkpoint(qa_q3_presentation_assets *a, qa_session *session,
    const qa_q3_asset_owner_refs *r, qa_buffer *out, qa_error *error)
{
    bool own_lease = false;
    if (!session || !out || out->data || out->size || !refs_ready(r))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 asset checkpoint requires actual owner references and empty output");
    if (!codec_begin(a, &own_lease, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, session, error) && asset_fields(&io, a, r) && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_FORMAT, "Invalid retained Q3 asset owner graph");
    qa_source_save_dispose(&io); codec_end(a, own_lease); return ok;
}
bool qa_q3_assets_owner_restore(qa_q3_presentation_assets *a, qa_session *session,
    const qa_q3_asset_owner_refs *r, qa_bytes bytes, qa_error *error)
{
    bool own_lease = false;
    if (!a || !session || !refs_ready(r) || a->name_count || a->name_capacity || a->model_count || a->model_capacity ||
        a->skin_count || a->skin_capacity || a->shader_count || a->shader_capacity || a->sound_count || a->sound_capacity)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 asset import requires a qualified empty candidate");
    if (!codec_begin(a, &own_lease, error)) return false;
    qa_q3_presentation_assets *candidate = NULL;
    qa_source_save_io io = {0};
    bool ok = qa_q3_presentation_assets_create(&a->options, &candidate, error);
    if (ok) {
        candidate->world = a->world; candidate->geometry = a->geometry;
        for (size_t i = 0, count = qa_q3_assets_provider_count(a); ok && i < count; ++i) {
            qa_q3_presentation_provider provider;
            ok = qa_q3_assets_provider_at(a, i, &provider) &&
                qa_q3_assets_provider_hold(candidate, &provider, error);
        }
        for (size_t i = 0, count = qa_q3_assets_map_count(a); ok && i < count; ++i) {
            qa_q3_asset_map_custody map;
            ok = qa_q3_assets_map_at(a, i, &map) &&
                qa_q3_assets_map_hold(candidate, map.world, map.geometry, error);
        }
    }
    if (ok) ok = qa_source_save_reader(&io, session, bytes, error) &&
        asset_fields(&io, candidate, r) && qa_source_save_finish(&io, NULL);
    if (ok && a->parent && candidate->parent != a->parent) ok = false;
    if (ok) for (const qa_q3_presentation_assets *parent = candidate->parent; parent; parent = parent->parent)
        if (parent == a) { ok = false; break; }
    if (ok) ok = qa_q3_assets_custody_restore(candidate, error);
    if (ok) for (size_t i = 0; ok && i < candidate->model_count; ++i)
        if (candidate->models[i] && !q3p_model_shared(candidate, candidate->models[i])) {
            q3p_model *m = candidate->models[i];
            for (unsigned x = 0; ok && x < 3; ++x) {
                bool shared = false;
                for (unsigned y = 0; y < x; ++y) if (m->scene[x] == m->scene[y]) shared = true;
                if (m->scene[x] && !shared) ok = r->scene_owned_ready(r->context, m->scene_ordinals[x], i, error);
            }
            if (ok && m->source_md4_scene)
                ok = r->scene_owned_ready(r->context, m->source_md4_scene_ordinal, i, error);
            if (ok && m->owns_world) ok = r->world_owned_ready(r->context, m->world_ordinal, i, error);
        }
    if (ok) {
        qa_q3_presentation_assets previous = *a;
        *a = *candidate;
        a->users = previous.users; a->busy = previous.busy;
        a->capturing = previous.capturing; a->codec_busy = previous.codec_busy;
        *candidate = previous;
        candidate->users = 1; candidate->busy = 0;
        candidate->capturing = candidate->codec_busy = false;
    }
    if (ok) for (size_t i = 0; i < a->model_count; ++i) if (a->models[i] && !q3p_model_shared(a, a->models[i])) {
        q3p_model *m = a->models[i];
        for (unsigned x = 0; x < 3; ++x) {
            bool shared = false;
            for (unsigned y = 0; y < x; ++y) if (m->scene[x] == m->scene[y]) shared = true;
            if (m->scene[x] && !shared) r->scene_adopt(r->context, m->scene_ordinals[x]);
        }
        if (m->source_md4_scene) r->scene_adopt(r->context, m->source_md4_scene_ordinal);
        if (m->owns_world) r->world_adopt(r->context, m->world_ordinal);
        m->borrowed_scenes = m->borrowed_world = false;
    }
    if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_FORMAT, "Saved Q3 asset identity or content differs");
    qa_source_save_dispose(&io); q3p_assets_dispose_borrowed(candidate);
    codec_end(a, own_lease); return ok;
}

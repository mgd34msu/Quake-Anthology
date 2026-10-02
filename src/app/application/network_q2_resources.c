#include "network_q2_private.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include "qa/binary.h"
#include "qa/model.h"
#include "qa/scene.h"
#include "network_q2_materials.h"
#include "unified_events.h"
#include <limits.h>
#include <stdio.h>

static bool acquisition_copy(const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    *out = *source;
    out->path = out->lookup_path = out->link_source = out->link_target = NULL;
    out->opening.order = NULL; out->opening.prefix = NULL;
    const char *texts[] = {source->path, source->lookup_path, source->link_source, source->link_target, source->opening.prefix};
    char **copies[] = {&out->path, &out->lookup_path, &out->link_source, &out->link_target};
    for (size_t i = 0; i < 5; ++i) {
        if (!texts[i]) continue;
        char *copy = application_network_q2_copy(texts[i], error);
        if (!copy) { qa_vfs_acquisition_dispose(out); return false; }
        if (i < 4) *copies[i] = copy;
        else out->opening.prefix = copy;
    }
    if (source->opening.order_count) {
        if (!source->opening.order || source->opening.order_count > SIZE_MAX / sizeof(qa_mount_id)) {
            qa_vfs_acquisition_dispose(out);
            return application_fail(error, QA_ERROR_FORMAT, "Q2 resource acquisition lost its genuine opening order");
        }
        qa_mount_id *order = malloc(source->opening.order_count * sizeof(*order));
        if (!order) { qa_vfs_acquisition_dispose(out); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 resource opening order"); }
        memcpy(order, source->opening.order, source->opening.order_count * sizeof(*order));
        out->opening.order = order;
    }
    return true;
}

static void held_free(application_q2_held_resource *held)
{
    qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
    qa_vfs_destroy(held->view);
    qa_buffer_free(&held->wire_bytes); free(held->dependencies);
    qa_buffer_free(&held->catalog_bytes); free(held->script_name); free(held->sky_base);
    qa_buffer_free(&held->image_palette); qa_buffer_free(&held->image_translation);
    free(held->instance); free(held->path); free(held->wire_path);
    *held = (application_q2_held_resource){0};
}

void application_network_q2_resources_free(qa_application_network_q2 *owner)
{
    for (size_t i = 0; i < owner->held_resource_count; ++i) held_free(&owner->held_resources[i]);
    free(owner->held_resources); owner->held_resources = NULL;
    owner->held_resource_count = owner->held_resource_capacity = 0;
}

static application_provider *provider_at(qa_application_network_q2 *owner, qa_actor_owner id)
{
    application_provider *found = NULL;
    for (size_t i = 0; i < owner->app->provider_count; ++i) {
        application_provider *provider = owner->app->providers[i];
        if (provider->owner != id) continue;
        if (found) return NULL;
        found = provider;
    }
    return found;
}

static bool held_append(qa_application_network_q2 *owner, application_q2_held_resource *held, qa_error *error)
{
    if (owner->held_resource_count == owner->held_resource_capacity) {
        size_t capacity = owner->held_resource_capacity ? owner->held_resource_capacity * 2 : 16;
        if (capacity < owner->held_resource_capacity || capacity > SIZE_MAX / sizeof(*owner->held_resources))
            return application_fail(error, QA_ERROR_MEMORY, "Q2 resource holder extent overflows");
        application_q2_held_resource *rows = realloc(owner->held_resources, capacity * sizeof(*rows));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 actual content holders");
        owner->held_resources = rows; owner->held_resource_capacity = capacity;
    }
    owner->held_resources[owner->held_resource_count++] = *held;
    *held = (application_q2_held_resource){0}; return true;
}

static void hash_number(qa_sha256_context *hash, uint64_t number)
{
    uint8_t bytes[8]; qa_store_u64le(bytes, number);
    qa_sha256_update(hash, (qa_bytes){bytes, sizeof(bytes)});
}

static void hash_text(qa_sha256_context *hash, const char *text)
{
    hash_number(hash, text ? (uint64_t)strlen(text) + 1 : 0);
    if (text) qa_sha256_update(hash, (qa_bytes){(const uint8_t *)text, strlen(text)});
}

static void hash_float(qa_sha256_context *hash, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); hash_number(hash, bits);
}

static void hash_image_options(qa_sha256_context *hash, const qa_scene_image_options *options)
{
    hash_number(hash, (uint64_t)options->family); hash_number(hash, (uint64_t)options->wrap);
    hash_number(hash, (uint64_t)options->filter); hash_number(hash, (uint64_t)options->usage);
    hash_number(hash, options->mipmap); hash_number(hash, options->transparent);
    hash_number(hash, options->fullbright_only); hash_number(hash, (uint64_t)(int64_t)options->transparent_index);
    hash_number(hash, options->source_q3);
    if (options->source_q3) {
        const qa_q3_image_upload_options *upload = &options->source_upload;
        hash_number(hash, upload->color.device.hardware_gamma); hash_number(hash, upload->color.device.fullscreen);
        hash_number(hash, (uint64_t)(int64_t)upload->color.device.color_bits);
        hash_number(hash, (uint64_t)(int64_t)upload->color.requested_overbright_bits);
        hash_float(hash, upload->color.gamma); hash_float(hash, upload->color.intensity);
        hash_number(hash, (uint64_t)(int64_t)upload->picmip); hash_number(hash, upload->maximum_texture_size);
        hash_number(hash, upload->round_down); hash_number(hash, upload->simple_mips);
        hash_number(hash, upload->color_mips); hash_number(hash, upload->allow_picmip); hash_number(hash, upload->mipmap);
    }
}

static void qualified_digest(const application_q2_held_resource *held, bool derived, qa_sha256_digest *digest)
{
    qa_sha256_context hash;
    qa_sha256_init(&hash);
    qa_sha256_update(&hash, (qa_bytes){held->identity.bytes, sizeof(held->identity.bytes)});
    hash_number(&hash, (uint64_t)held->kind); hash_number(&hash, held->missing);
    if (held->kind != APPLICATION_Q2_HELD_MODEL)
        qa_sha256_update(&hash, (qa_bytes){held->authority.bytes, sizeof(held->authority.bytes)});
    hash_text(&hash, held->path);
    if (held->resource) {
        qa_sha256_update(&hash, (qa_bytes){qa_resource_digest(held->resource)->bytes, 32});
        hash_number(&hash, held->opening.mount); hash_number(&hash, held->opening.resource_id);
        hash_text(&hash, held->opening.lookup_path); hash_text(&hash, held->opening.link_source);
        hash_text(&hash, held->opening.link_target); hash_number(&hash, (uint64_t)held->opening.opening.rank);
        hash_text(&hash, held->opening.opening.prefix); hash_number(&hash, held->opening.opening.user_overlay);
        hash_number(&hash, held->opening.opening.order_count);
        for (size_t i = 0; i < held->opening.opening.order_count; ++i) hash_number(&hash, held->opening.opening.order[i]);
    }
    if (held->kind == APPLICATION_Q2_HELD_MATERIAL) {
        hash_text(&hash, held->script_name); hash_number(&hash, held->source_offset);
        hash_number(&hash, held->script_size); hash_number(&hash, held->name_offset); hash_number(&hash, held->name_size);
        if (!held->resource) qa_sha256_update(&hash, (qa_bytes){held->catalog_bytes.data, held->catalog_bytes.size});
    }
    if (held->kind == APPLICATION_Q2_HELD_EVENT) {
        hash_number(&hash, (uint64_t)held->event_kind);
        hash_text(&hash, held->event_key);
    }
    if (held->kind == APPLICATION_Q2_HELD_IMAGE || held->kind == APPLICATION_Q2_HELD_MATERIAL) {
        hash_image_options(&hash, &held->image_options);
        qa_sha256_update(&hash, (qa_bytes){held->image_palette_source.bytes, sizeof(held->image_palette_source.bytes)});
        hash_number(&hash, held->image_palette.size);
        qa_sha256_update(&hash, (qa_bytes){held->image_palette.data, held->image_palette.size});
        hash_number(&hash, held->image_translation.size);
        qa_sha256_update(&hash, (qa_bytes){held->image_translation.data, held->image_translation.size});
    }
    if (derived) {
        hash_number(&hash, held->wire_bytes.size);
        qa_sha256_update(&hash, (qa_bytes){held->wire_bytes.data, held->wire_bytes.size});
    }
    qa_sha256_final(&hash, digest);
}

static bool qualified_name(const application_q2_held_resource *held, char alias[64], qa_error *error)
{
    static const char *const faces[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    const char *extension = held->kind == APPLICATION_Q2_HELD_MATERIAL ? ".shader" :
        held->kind == APPLICATION_Q2_HELD_IMAGE ? ".png" : strrchr(held->path, '.');
    const char *separator = strrchr(held->path, '/');
    if (!extension || ((held->kind != APPLICATION_Q2_HELD_MATERIAL && held->kind != APPLICATION_Q2_HELD_IMAGE) &&
        separator && extension < separator) || strlen(extension) > 8)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 mixed resource has no genuine bounded file extension");
    qa_sha256_digest digest; char hex[65];
    if (held->sky_face) {
        if (held->sky_face > 6 || !held->sky_base)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 sky dependency lost its genuine face receipt");
        digest = held->sky_group;
    } else qualified_digest(held, true, &digest);
    qa_sha256_hex(&digest, hex);
    const char *category = held->kind == APPLICATION_Q2_HELD_EVENT && held->event_kind == QA_NATIVE_HOST_SOUND ?
        "sound" : !strncmp(held->sky_base ? held->sky_base : held->path, "players/", 8) ? "players" : "models";
    if (held->sky_face) snprintf(alias, 64, "%s/qa/%.32s_%s%s", category, hex, faces[held->sky_face - 1], extension);
    else snprintf(alias, 64, "%s/qa/%.32s%s", category, hex, extension);
    return true;
}

static bool held_store(qa_application_network_q2 *owner, application_q2_held_resource *held,
    size_t *out, qa_error *error)
{
    char alias[64];
    if (!qualified_name(held, alias, error)) return false;
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (strcmp(old->wire_path, alias)) continue;
        qa_sha256_digest actual, retained; qualified_digest(held, true, &actual); qualified_digest(old, true, &retained);
        if (!qa_sha256_equal(&actual, &retained) || held->sky_face != old->sky_face ||
            !qa_sha256_equal(&held->sky_group, &old->sky_group) || !qa_vfs_lookup_equal(old->view, held->view))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 qualified dependency collides with another genuine acquisition");
        *out = i; return true;
    }
    held->wire_path = application_network_q2_copy(alias, error);
    if (!held->wire_path) return false;
    *out = owner->held_resource_count; return held_append(owner, held, error);
}

static bool retained_opening(const qa_vfs *view, const char *path, const qa_resource *resource,
    const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    if (source) return source->resource_id == qa_resource_id(resource) &&
        qa_vfs_acquisition_retained(view, source, error) && acquisition_copy(source, out, error);
    bool found = false; qa_sha256_digest first = {0};
    for (size_t i = 0; i < qa_vfs_retained_read_count(view); ++i) {
        qa_vfs_read_reference row;
        if (!qa_vfs_retained_read_at(view, i, &row) || row.resource != resource || !row.path ||
            (path ? strcmp(row.path, path) != 0 : strncmp(row.path, "scripts/", 8) != 0)) continue;
        qa_vfs_acquisition actual = {.mount = row.mount, .resource_id = qa_resource_id(resource),
            .path = (char *)row.path, .lookup_path = (char *)row.lookup_path,
            .link_source = (char *)row.link_source, .link_target = (char *)row.link_target,
            .opening = row.opening, .opening_present = true};
        application_q2_held_resource receipt = {.path = (char *)row.path, .resource = (qa_resource *)resource, .opening = actual};
        qa_sha256_digest digest; qualified_digest(&receipt, false, &digest);
        if (found && !qa_sha256_equal(&first, &digest)) {
            qa_vfs_acquisition_dispose(out);
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency has ambiguous actual historical opening recipes");
        }
        if (!found) {
            if (!qa_vfs_acquisition_retained(view, &actual, error) || !acquisition_copy(&actual, out, error)) return false;
            first = digest; found = true;
        }
    }
    return found || application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency lost its actual retained opening receipt");
}

static bool dependency_prepare(const application_q2_held_resource *model, const char *name,
    const qa_resource *resource, const qa_vfs_acquisition *opening,
    application_q2_held_resource *held, qa_error *error)
{
    *held = (application_q2_held_resource){.provider = model->provider, .identity = model->identity,
        .kind = APPLICATION_Q2_HELD_DEPENDENCY, .missing = resource == NULL};
    qualified_digest(model, false, &held->authority);
    held->path = qa_scene_model_image_path(name, error);
    held->instance = application_network_q2_copy(model->instance, error);
    if (!held->path || !held->instance || !qa_vfs_retain(model->view, error)) return false;
    held->view = model->view;
    if (resource) {
        if (!retained_opening(held->view, held->path, resource, opening, &held->opening, error)) return false;
        held->resource = (qa_resource *)resource; qa_resource_retain(held->resource);
    }
    return true;
}

bool application_network_q2_dependency_receipt(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *name, const qa_resource *resource,
    const qa_vfs_acquisition *opening, size_t *out, qa_error *error)
{
    if (!owner || !model || !model->view || !model->resource || model->missing || !name || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency receipt lost its genuine model Source");
    application_q2_held_resource held = {0};
    bool ok = dependency_prepare(model, name, resource, opening, &held, error) && held_store(owner, &held, out, error);
    held_free(&held); return ok;
}

bool application_network_q2_dependency_of(const application_q2_held_resource *model,
    const application_q2_held_resource *dependency)
{
    if (!model || !dependency || !model->view || !dependency->view ||
        model->provider != dependency->provider || !qa_sha256_equal(&model->identity, &dependency->identity) ||
        !qa_vfs_lookup_equal(model->view, dependency->view)) return false;
    qa_sha256_digest authority; qualified_digest(model, false, &authority);
    return qa_sha256_equal(&authority, &dependency->authority);
}

static bool buffer_copy(qa_bytes bytes, qa_buffer *out, qa_error *error)
{
    if (!bytes.size) return true;
    if (!bytes.data || bytes.size > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 resource bytes lost their actual bounded Source");
    out->data = malloc(bytes.size);
    if (!out->data) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 image Source bytes");
    memcpy(out->data, bytes.data, bytes.size); out->size = bytes.size; return true;
}

bool application_network_q2_dependency_image(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *path, const qa_resource *resource,
    const qa_vfs_acquisition *opening, const qa_scene_image_options *options, qa_bytes palette,
    const char *palette_path, const qa_resource *palette_resource, const qa_vfs_acquisition *palette_opening,
    qa_bytes derived, size_t *out, qa_error *error)
{
    if (!owner || !model || !path || !resource || !options || !out || !derived.data || !derived.size ||
        (palette.size && palette.size != 768) || (options->translation.size && options->translation.size != 256) ||
        ((palette_resource != NULL) != (palette_path != NULL)) || (palette_resource && !palette.size))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 image closure requires its actual decoded Source and palette");
    application_q2_held_resource held = {0};
    bool ok = dependency_prepare(model, path, resource, opening, &held, error);
    held.kind = APPLICATION_Q2_HELD_IMAGE; held.image_options = *options;
    held.image_options.palette_rgb = held.image_options.translation = (qa_bytes){0};
    held.image_palette_dependency = SIZE_MAX;
    if (ok) ok = buffer_copy(palette, &held.image_palette, error) &&
        buffer_copy(options->translation, &held.image_translation, error) && buffer_copy(derived, &held.wire_bytes, error);
    if (ok && palette_resource) {
        ok = application_network_q2_dependency_receipt(owner, model, palette_path, palette_resource,
            palette_opening, &held.image_palette_dependency, error);
        if (ok) qualified_digest(&owner->held_resources[held.image_palette_dependency], false, &held.image_palette_source);
    }
    if (ok) {
        held.image_options.palette_rgb = (qa_bytes){held.image_palette.data, held.image_palette.size};
        held.image_options.translation = (qa_bytes){held.image_translation.data, held.image_translation.size};
        ok = application_network_q2_materials_image_validate(owner, &held, error) && held_store(owner, &held, out, error);
    }
    held_free(&held); return ok;
}

bool application_network_q2_material_resource(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const qa_material_script_view *script, qa_bytes derived,
    qa_scene_family family, qa_bytes palette, const qa_scene_palette_source *palette_source,
    const size_t *dependencies, size_t count, size_t *out, qa_error *error)
{
    if (!owner || !model || !model->view || !model->resource || !script || !script->name ||
        !script->catalog.data || !script->catalog.size || !derived.data || !derived.size ||
        derived.size > INT32_MAX || script->source_offset > script->catalog.size ||
        script->body.size > script->catalog.size - script->source_offset ||
        !script->body.data || memcmp(script->body.data, script->catalog.data + script->source_offset, script->body.size) ||
        script->name_offset > script->catalog.size || script->name_size > script->catalog.size - script->name_offset ||
        (count && !dependencies) || count > SIZE_MAX / sizeof(*dependencies) || !out ||
        (palette.size && palette.size != 768) ||
        (palette_source && (!palette_source->resource || !palette_source->opening || !palette.size)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 material requires its genuine retained Source catalog span");
    application_q2_held_resource held = {.provider = model->provider, .identity = model->identity,
        .kind = APPLICATION_Q2_HELD_MATERIAL, .source_offset = script->source_offset,
        .script_size = script->body.size, .name_offset = script->name_offset, .name_size = script->name_size,
        .image_options = {.family = family}, .image_palette_dependency = SIZE_MAX};
    qualified_digest(model, false, &held.authority);
    held.instance = application_network_q2_copy(model->instance, error);
    held.script_name = application_network_q2_copy(script->name, error);
    bool ok = held.instance && held.script_name && qa_vfs_retain(model->view, error);
    if (ok) held.view = model->view;
    if (ok && script->resource) {
        qa_bytes actual = qa_resource_bytes(script->resource);
        ok = actual.size == script->catalog.size && !memcmp(actual.data, script->catalog.data, actual.size) &&
            retained_opening(held.view, NULL, script->resource, NULL, &held.opening, error);
        if (ok) { held.resource = (qa_resource *)script->resource; qa_resource_retain(held.resource); }
    } else if (ok) {
        held.catalog_bytes.data = malloc(script->catalog.size);
        if (!held.catalog_bytes.data) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining genuine in-memory material catalog");
        else { memcpy(held.catalog_bytes.data, script->catalog.data, script->catalog.size); held.catalog_bytes.size = script->catalog.size; }
    }
    if (ok) {
        held.wire_bytes.data = malloc(derived.size);
        held.dependencies = count ? malloc(count * sizeof(*held.dependencies)) : NULL;
        if (!held.wire_bytes.data || (count && !held.dependencies))
            ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 wire material program");
        else {
            memcpy(held.wire_bytes.data, derived.data, derived.size); held.wire_bytes.size = derived.size;
            if (count) memcpy(held.dependencies, dependencies, count * sizeof(*held.dependencies));
            held.dependency_count = count;
        }
    }
    if (ok) {
        held.path = application_network_q2_copy(held.resource ? held.opening.path : script->name, error);
        ok = held.path != NULL;
    }
    if (ok) ok = buffer_copy(palette, &held.image_palette, error);
    if (ok && palette_source) {
        ok = application_network_q2_dependency_receipt(owner, model, palette_source->opening->path,
            palette_source->resource, palette_source->opening, &held.image_palette_dependency, error);
        if (ok) qualified_digest(&owner->held_resources[held.image_palette_dependency], false, &held.image_palette_source);
    }
    held.image_options.palette_rgb = (qa_bytes){held.image_palette.data, held.image_palette.size};
    if (ok) ok = held_store(owner, &held, out, error);
    held_free(&held); return ok;
}

bool application_network_q2_sky_dependencies(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *base, const char *const paths[6],
    const qa_resource *const resources[6], const qa_vfs_acquisition *const openings[6],
    size_t out[6], char alias_base[64], qa_error *error)
{
    if (!owner || !model || !model->view || !model->resource || !base || !paths || !resources || !out || !alias_base)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 sky closure requires its actual six Source file receipts");
    application_q2_held_resource faces[6] = {0};
    char *normalized = qa_scene_model_image_path(base, error);
    bool ok = normalized != NULL;
    qa_sha256_context hash; qa_sha256_digest group, authority;
    qualified_digest(model, false, &authority); qa_sha256_init(&hash);
    qa_sha256_update(&hash, (qa_bytes){authority.bytes, sizeof(authority.bytes)}); hash_text(&hash, normalized);
    for (size_t i = 0; ok && i < 6; ++i) {
        ok = paths[i] && dependency_prepare(model, paths[i], resources[i], openings ? openings[i] : NULL, &faces[i], error);
        if (ok) {
            qa_sha256_digest source; qualified_digest(&faces[i], false, &source);
            qa_sha256_update(&hash, (qa_bytes){source.bytes, sizeof(source.bytes)});
        }
    }
    qa_sha256_final(&hash, &group);
    for (size_t i = 0; ok && i < 6; ++i) {
        faces[i].sky_base = application_network_q2_copy(normalized, error);
        faces[i].sky_face = (uint8_t)(i + 1); faces[i].sky_group = group;
        ok = faces[i].sky_base && held_store(owner, &faces[i], &out[i], error);
    }
    if (ok) {
        char hex[65]; qa_sha256_hex(&group, hex);
        snprintf(alias_base, 64, "%s/qa/%.32s", !strncmp(normalized, "players/", 8) ? "players" : "models", hex);
    }
    for (size_t i = 0; i < 6; ++i) held_free(&faces[i]);
    free(normalized); return ok;
}

bool application_network_q2_sky_group_valid(const application_q2_held_resource *subject,
    const application_q2_held_resource *const faces[6])
{
    if (!subject || !faces || !faces[0] || !faces[0]->sky_base) return false;
    qa_sha256_digest authority = subject->authority, group;
    if (subject->kind == APPLICATION_Q2_HELD_MODEL) qualified_digest(subject, false, &authority);
    qa_sha256_context hash; qa_sha256_init(&hash);
    qa_sha256_update(&hash, (qa_bytes){authority.bytes, sizeof(authority.bytes)}); hash_text(&hash, faces[0]->sky_base);
    for (size_t i = 0; i < 6; ++i) {
        const application_q2_held_resource *face = faces[i];
        if (!face || face->kind != APPLICATION_Q2_HELD_DEPENDENCY || face->sky_face != i + 1 ||
            !face->sky_base || strcmp(face->sky_base, faces[0]->sky_base) ||
            face->provider != subject->provider || !qa_sha256_equal(&face->identity, &subject->identity) ||
            !qa_sha256_equal(&face->authority, &authority) || !qa_vfs_lookup_equal(face->view, subject->view)) return false;
        qa_sha256_digest source; qualified_digest(face, false, &source);
        qa_sha256_update(&hash, (qa_bytes){source.bytes, sizeof(source.bytes)});
    }
    qa_sha256_final(&hash, &group);
    for (size_t i = 0; i < 6; ++i) if (!qa_sha256_equal(&group, &faces[i]->sky_group)) return false;
    return true;
}

bool application_network_q2_dependency(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *name, size_t *out, qa_error *error)
{
    if (!owner || !model || !model->view || !model->resource || model->missing || !name || !*name || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency needs its genuine acquired model owner");
    char *path = qa_scene_model_image_path(name, error);
    if (!path) return false;
    qa_sha256_digest authority; qualified_digest(model, false, &authority);
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (old->kind == APPLICATION_Q2_HELD_DEPENDENCY && old->provider == model->provider &&
            qa_sha256_equal(&old->identity, &model->identity) && qa_sha256_equal(&old->authority, &authority) &&
            !strcmp(old->path, path) && qa_vfs_lookup_equal(old->view, model->view)) {
            free(path); *out = i; return true;
        }
    }
    application_q2_held_resource held = {.provider = model->provider, .identity = model->identity,
        .authority = authority, .kind = APPLICATION_Q2_HELD_DEPENDENCY, .path = path};
    held.instance = application_network_q2_copy(model->instance, error);
    bool ok = held.instance && qa_vfs_retain(model->view, error);
    if (ok) held.view = model->view;
    if (ok) {
        qa_error acquisition_error = {0};
        if (!qa_vfs_acquire_receipt(held.view, held.path, &held.resource, &held.opening, &acquisition_error)) {
            if (acquisition_error.code == QA_ERROR_NOT_FOUND) {
                qa_resource_release(held.resource); held.resource = NULL;
                qa_vfs_acquisition_dispose(&held.opening); held.missing = true;
            }
            else { if (error) *error = acquisition_error; ok = false; }
        }
    }
    if (ok) {
        char alias[64]; ok = qualified_name(&held, alias, error);
        if (ok) { held.wire_path = application_network_q2_copy(alias, error); ok = held.wire_path != NULL; }
    }
    if (ok) {
        for (size_t i = 0; i < owner->held_resource_count; ++i)
            if (!strcmp(owner->held_resources[i].wire_path, held.wire_path)) {
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 dependency alias collides with another actual Source receipt"); break;
            }
    }
    if (ok) { *out = owner->held_resource_count; ok = held_append(owner, &held, error); }
    held_free(&held); return ok;
}

static bool model_derivation(qa_application_network_q2 *owner, application_q2_held_resource *held, qa_error *error)
{
    qa_bytes original = qa_resource_bytes(held->resource);
    bool md2 = original.size >= 4 && !memcmp(original.data, "IDP2", 4);
    bool sprite = original.size >= 4 && !memcmp(original.data, "IDS2", 4);
    if (!md2 && !sprite) return original.size >= 4 && !memcmp(original.data, "IDP3", 4) ?
        application_network_q2_materials_derive(owner, held, error) : true;
    qa_model model = {0};
    if (!qa_model_load(original, &model, error)) return false;
    size_t count = md2 ? model.skin_count : model.sprite_count;
    bool ok = true;
    if (count) {
        if (count > SIZE_MAX / sizeof(*held->dependencies)) {
            qa_model_free(&model);
            return application_fail(error, QA_ERROR_MEMORY, "Q2 model dependency extent overflows");
        }
        held->dependencies = malloc(count * sizeof(*held->dependencies));
        held->wire_bytes.data = malloc(original.size);
        if (!held->dependencies || !held->wire_bytes.data)
            ok = application_fail(error, QA_ERROR_MEMORY, "Retaining genuine Q2 derived model closure");
        else {
            held->dependency_count = count; held->wire_bytes.size = original.size;
            memcpy(held->wire_bytes.data, original.data, original.size);
        }
    }
    size_t first = md2 ? qa_load_u32le(original.data + 44) : 28;
    size_t stride = md2 ? 64 : 80;
    for (size_t i = 0; ok && i < count; ++i) {
        const char *name = md2 ? model.skins[i].name : model.sprites[i].image;
        held->dependencies[i] = SIZE_MAX;
        if (!*name) continue;
        ok = application_network_q2_dependency(owner, held, name, &held->dependencies[i], error);
        if (ok) {
            const char *alias = owner->held_resources[held->dependencies[i]].wire_path;
            memset(held->wire_bytes.data + first + i * stride, 0, 64);
            memcpy(held->wire_bytes.data + first + i * stride, alias, strlen(alias));
        }
    }
    qa_model_free(&model); return ok;
}

bool application_network_q2_visual_resource(qa_application_network_q2 *owner,
    const qa_application_visual_view *visual, unsigned model, uint32_t *out, qa_error *error)
{
    if (!visual || model > 3 || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition requires its real BODY visual receipt");
    const char *path = visual->models[model];
    if (!path || !*path || *path == '*' || *path == '#' || visual->provider == owner->host.source.source_owner)
        return application_network_q2_resource(owner, 0, path, out, error);
    application_provider *provider = provider_at(owner, visual->provider);
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || provider->product->id != visual->content)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition lost its selected BODY content owner");
    qa_command_context request = {.owner = visual->provider, .origin = QA_COMMAND_SERVER,
        .dialect = visual->family == QA_GAME_Q2 ? QA_CONSOLE_Q2 :
            visual->family == QA_GAME_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1}, captured;
    if (!qa_application_capture_command_context(owner->app, &request, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(owner->app, &captured, NULL);
    if (!files) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 selected model has no actual provider file scope");
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (old->kind == APPLICATION_Q2_HELD_MODEL && old->provider == visual->provider && !strcmp(old->path, path) &&
            (!visual->model_resources[model] || old->resource == visual->model_resources[model]) &&
            qa_sha256_equal(&old->identity, &provider->launch->identity) && qa_vfs_lookup_equal(old->view, files)) {
            if (visual->model_openings[model]) {
                application_q2_held_resource actual = *old; actual.opening = *visual->model_openings[model];
                qa_sha256_digest expected, retained;
                qualified_digest(&actual, false, &expected); qualified_digest(old, false, &retained);
                if (!qa_sha256_equal(&expected, &retained)) continue;
            }
            return application_network_q2_resource(owner, 0, old->wire_path, out, error);
        }
    }
    application_q2_held_resource held = {.provider = provider->owner, .identity = provider->launch->identity,
        .kind = APPLICATION_Q2_HELD_MODEL};
    held.view = qa_vfs_clone(files, error);
    held.instance = application_network_q2_copy(provider->launch->selection.instance, error);
    held.path = application_network_q2_copy(path, error);
    bool ok = held.view && held.instance && held.path;
    if (ok && visual->model_resources[model]) {
        const qa_vfs_acquisition *opening = visual->model_openings[model];
        ok = opening && opening->resource_id == qa_resource_id(visual->model_resources[model]) &&
            qa_vfs_acquisition_retained(held.view, opening, error) && acquisition_copy(opening, &held.opening, error);
        if (ok) { held.resource = (qa_resource *)visual->model_resources[model]; qa_resource_retain(held.resource); }
    } else if (ok) ok = qa_vfs_acquire_receipt(held.view, path, &held.resource, &held.opening, error);
    size_t dependency_start = owner->held_resource_count;
    if (ok) ok = model_derivation(owner, &held, error);
    if (ok) {
        char alias[64];
        ok = qualified_name(&held, alias, error);
        if (ok) {
            held.wire_path = application_network_q2_copy(alias, error);
            ok = held.wire_path != NULL;
        }
        for (size_t i = 0; ok && i < owner->held_resource_count; ++i) {
            const application_q2_held_resource *old = &owner->held_resources[i];
            if (!strcmp(old->wire_path, held.wire_path)) {
                qa_sha256_digest expected, retained;
                qualified_digest(&held, true, &expected); qualified_digest(old, true, &retained);
                if (qa_sha256_equal(&expected, &retained) && qa_vfs_lookup_equal(old->view, held.view)) {
                    while (owner->held_resource_count > dependency_start)
                        held_free(&owner->held_resources[--owner->held_resource_count]);
                    held_free(&held);
                    return application_network_q2_resource(owner, 0, old->wire_path, out, error);
                }
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 qualified resource name collides with another actual acquisition");
            }
        }
    }
    if (ok && (!qa_application_command_context_active(owner->app, &captured) ||
        qa_application_context_files(owner->app, &captured, NULL) != files ||
        provider_at(owner, visual->provider) != provider))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition changed its actual BODY file owner");
    bool appended = false;
    if (ok) {
        ok = held_append(owner, &held, error); appended = ok;
        if (ok) ok = application_network_q2_resource(owner, 0,
            owner->held_resources[owner->held_resource_count - 1].wire_path, out, error);
    }
    if (!ok && !appended) while (owner->held_resource_count > dependency_start)
        held_free(&owner->held_resources[--owner->held_resource_count]);
    held_free(&held); return ok;
}

static const application_unified_event_resource *event_receipt(qa_application *app, const char *key)
{
    for (size_t i = 0; i < application_unified_event_resource_count(app); ++i) {
        const application_unified_event_resource *row = application_unified_event_resource_at(app, i);
        if (row && !strcmp(row->id, key)) return row;
    }
    return NULL;
}

bool qa_application_network_q2_event_resource(qa_application_network_q2 *owner, qa_actor_owner emitter,
    const qa_application_protocol_resource_reference *reference, uint32_t *out, qa_error *error)
{
    application_unified_event_source source;
    if (!owner || !reference || !reference->name || !out || reference->kind > QA_NATIVE_HOST_IMAGE ||
        !memchr(reference->resource_key, 0, sizeof(reference->resource_key)) ||
        !application_network_q2_current(owner, error) ||
        !application_unified_event_source_read(owner->app, emitter, &source, error) ||
        !source.descriptor || !source.content || !source.product || source.product->family != QA_GAME_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 event resource lost its captured Source owner");
    unsigned kind = (unsigned)reference->kind;
    if (!*reference->name) { *out = 0; return true; }
    if (!*reference->resource_key && ((kind == QA_NATIVE_HOST_SOUND && reference->name[0] == '*') ||
        (kind == QA_NATIVE_HOST_MODEL && (reference->name[0] == '*' || reference->name[0] == '#')))) {
        if (kind == QA_NATIVE_HOST_MODEL && reference->name[0] == '*' && emitter != owner->host.source.source_owner)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Foreign Q2 inline model belongs to another actual map");
        return application_network_q2_resource(owner, kind, reference->name, out, error);
    }
    const application_unified_event_resource *receipt = *reference->resource_key ?
        event_receipt(owner->app, reference->resource_key) : NULL;
    const qa_launch_instance *registered = receipt ? qa_launch_instance_lease_view(receipt->descriptor) : NULL;
    const char *content = receipt ? qa_strings_cstr(qa_session_strings(owner->app->session), receipt->content) : NULL;
    const char *path = receipt ? qa_strings_cstr(qa_session_strings(owner->app->session), receipt->path) : reference->name;
    if (*reference->resource_key && (!receipt || !registered || !registered->content || !receipt->resource ||
        !content || strcmp(content, source.product->identity) || !path || !*path))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 event lost its immutable captured resource key");
    if (!receipt && kind == QA_NATIVE_HOST_SOUND && *path == '#') ++path;
    char *requested = NULL;
    if (!receipt && kind == QA_NATIVE_HOST_SOUND && strncmp(path, "sound/", 6)) {
        size_t length = strlen(path);
        if (length > SIZE_MAX - 7) return application_fail(error, QA_ERROR_MEMORY, "Q2 sound request extent overflows");
        requested = malloc(length + 7);
        if (requested) { memcpy(requested, "sound/", 6); memcpy(requested + 6, path, length + 1); }
        else return application_fail(error, QA_ERROR_MEMORY, "Retaining actual missing Q2 sound request");
        path = requested;
    }
    application_q2_held_resource held = {.provider = emitter, .identity = source.descriptor->identity,
        .kind = receipt && kind == QA_NATIVE_HOST_MODEL ? APPLICATION_Q2_HELD_MODEL : APPLICATION_Q2_HELD_EVENT,
        .missing = receipt == NULL, .event_kind = reference->kind};
    if (held.kind == APPLICATION_Q2_HELD_EVENT) memcpy(held.event_key, reference->resource_key, sizeof(held.event_key));
    held.instance = application_network_q2_copy(source.descriptor->selection.instance, error);
    held.path = qa_scene_model_image_path(path, error);
    held.view = qa_vfs_clone(receipt ? registered->content : source.content, error);
    free(requested);
    bool ok = held.instance && held.path && held.view;
    if (ok && receipt) {
        ok = retained_opening(held.view, held.path, receipt->resource, NULL, &held.opening, error);
        if (ok) { held.resource = receipt->resource; qa_resource_retain(held.resource); }
    }
    size_t dependency_start = owner->held_resource_count;
    if (ok && held.kind == APPLICATION_Q2_HELD_MODEL) ok = model_derivation(owner, &held, error);
    size_t index = 0;
    if (ok) ok = held_store(owner, &held, &index, error);
    if (ok) {
        const char *wire = owner->held_resources[index].wire_path;
        char config[66];
        if (kind == QA_NATIVE_HOST_IMAGE) { snprintf(config, sizeof(config), "/%s", wire); wire = config; }
        else if (kind == QA_NATIVE_HOST_SOUND && !strncmp(wire, "sound/", 6)) wire += 6;
        ok = application_network_q2_resource(owner, kind, wire, out, error);
    } else while (owner->held_resource_count > dependency_start)
        held_free(&owner->held_resources[--owner->held_resource_count]);
    held_free(&held); return ok;
}

bool qa_application_network_q2_event_config(qa_application_network_q2 *owner, uint16_t index,
    const char **out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error) || index >= owner->config_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 event config leaves its actual HOST table");
    *out = owner->configs[index] ? owner->configs[index] : ""; return true;
}

size_t qa_application_network_q2_resource_count(const qa_application_network_q2 *owner)
{ return owner ? owner->held_resource_count : 0; }

bool qa_application_network_q2_resource_read(const qa_application_network_q2 *owner, size_t index,
    qa_application_network_q2_resource_view *out, qa_error *error)
{
    if (!owner || !out || index >= owner->held_resource_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 content inventory index leaves its actual holders");
    const application_q2_held_resource *held = &owner->held_resources[index];
    bool memory = held->kind == APPLICATION_Q2_HELD_MATERIAL && !held->resource && held->catalog_bytes.size;
    if (!held->view || !held->instance || !held->path || !held->wire_path ||
        (held->missing ? (held->kind != APPLICATION_Q2_HELD_DEPENDENCY && held->kind != APPLICATION_Q2_HELD_EVENT) ||
            held->resource || held->opening.path ||
            held->wire_bytes.data || held->wire_bytes.size :
            !memory && (!held->resource || held->opening.resource_id != qa_resource_id(held->resource) ||
            !qa_vfs_acquisition_retained(held->view, &held->opening, error)))) return false;
    *out = (qa_application_network_q2_resource_view){held->provider, held->instance, held->path,
        held->wire_path, held->view, held->resource, held->resource ? &held->opening : NULL, held->missing,
        {held->wire_bytes.data, held->wire_bytes.size}}; return true;
}

bool application_network_q2_download_resource(void *context, const char *path, const qa_vfs **view,
    qa_resource **resource, qa_vfs_acquisition *opening, qa_buffer *source_bytes, qa_buffer *wire_bytes,
    qa_q2_download_resource_status *status, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    if (!owner || !path || !view || !resource || *resource || !opening || opening->path ||
        !source_bytes || source_bytes->data || source_bytes->size || !wire_bytes || wire_bytes->data || wire_bytes->size || !status ||
        !application_network_q2_current(owner, error)) return false;
    *view = NULL; *status = QA_Q2_DOWNLOAD_UNHANDLED;
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        application_q2_held_resource *held = &owner->held_resources[i];
        if (strcmp(held->wire_path, path)) continue;
        *view = held->view;
        if (held->missing) { *status = QA_Q2_DOWNLOAD_MISSING; return true; }
        bool material = held->kind == APPLICATION_Q2_HELD_MATERIAL ||
            (held->kind == APPLICATION_Q2_HELD_MODEL && qa_resource_bytes(held->resource).size >= 4 &&
                !memcmp(qa_resource_bytes(held->resource).data, "IDP3", 4));
        if (material && (!owner->materials_bound || !owner->materials_capability)) {
            *status = QA_Q2_DOWNLOAD_MISSING; return true;
        }
        if (held->resource && (!qa_vfs_acquisition_retained(held->view, &held->opening, error) ||
            !acquisition_copy(&held->opening, opening, error))) return false;
        if (!held->resource) {
            if (held->kind != APPLICATION_Q2_HELD_MATERIAL || !held->catalog_bytes.size)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 memory artifact lost its true catalog Source");
            source_bytes->data = malloc(held->catalog_bytes.size);
            if (!source_bytes->data) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 memory catalog transfer");
            memcpy(source_bytes->data, held->catalog_bytes.data, held->catalog_bytes.size);
            source_bytes->size = held->catalog_bytes.size;
        }
        if (held->wire_bytes.size) {
            wire_bytes->data = malloc(held->wire_bytes.size);
            if (!wire_bytes->data) { qa_vfs_acquisition_dispose(opening);
                qa_buffer_free(source_bytes);
                return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 derived transfer bytes"); }
            memcpy(wire_bytes->data, held->wire_bytes.data, held->wire_bytes.size); wire_bytes->size = held->wire_bytes.size;
        }
        if (held->resource) qa_resource_retain(held->resource);
        *resource = held->resource; *status = held->resource ? QA_Q2_DOWNLOAD_HELD : QA_Q2_DOWNLOAD_MEMORY;
        return true;
    }
    return true;
}

bool application_network_q2_materials_required(const qa_application_network_q2 *owner)
{
    for (size_t i = 0; owner && i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *held = &owner->held_resources[i];
        if (held->kind == APPLICATION_Q2_HELD_MATERIAL) return true;
        qa_bytes source = held->resource ? qa_resource_bytes(held->resource) : (qa_bytes){0};
        if (held->kind == APPLICATION_Q2_HELD_MODEL && source.size >= 4 && !memcmp(source.data, "IDP3", 4)) return true;
    }
    return false;
}

static bool text_field(qa_source_save_io *io, char **field)
{
    const char *text = *field;
    if (!qa_source_save_text(io, &text)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && text) {
        *field = application_network_q2_copy(text, io->error); return *field != NULL;
    }
    return true;
}

static bool buffer_field(qa_source_save_io *io, qa_buffer *buffer, size_t maximum)
{
    if (!qa_source_save_count(io, &buffer->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && buffer->size) {
        if (io->offset > io->input.size || buffer->size > io->input.size - io->offset)
            return application_fail(io->error, QA_ERROR_FORMAT, "Q2 derived artifact exceeds its cold payload");
        buffer->data = malloc(buffer->size);
        if (!buffer->data) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring Q2 derived artifact");
    }
    return qa_source_save_bytes(io, buffer->data, buffer->size);
}

static bool image_fields(qa_source_save_io *io, application_q2_held_resource *held)
{
    qa_scene_image_options *options = &held->image_options;
    uint32_t family = (uint32_t)options->family, wrap = (uint32_t)options->wrap;
    uint32_t filter = (uint32_t)options->filter, usage = (uint32_t)options->usage;
    uint64_t palette = io->direction == QA_SOURCE_SAVE_WRITE && held->image_palette_dependency != SIZE_MAX ?
        (uint64_t)held->image_palette_dependency + 1 : 0;
    if (!qa_source_save_u32(io, &family) || family > QA_SCENE_Q3 || !qa_source_save_u32(io, &wrap) || wrap > QA_SCENE_CLAMP ||
        !qa_source_save_u32(io, &filter) || filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io, &usage) || usage > QA_IMAGE_USAGE_SKY ||
        !qa_source_save_bool(io, &options->mipmap) || !qa_source_save_bool(io, &options->transparent) ||
        !qa_source_save_bool(io, &options->fullbright_only) || !qa_source_save_i32(io, &options->transparent_index) ||
        !qa_source_save_bool(io, &options->source_q3) ||
        (options->source_q3 && !qa_q3_image_upload_options_codec(io, &options->source_upload)) ||
        !buffer_field(io, &held->image_palette, 768) || (held->image_palette.size && held->image_palette.size != 768) ||
        !buffer_field(io, &held->image_translation, 256) || (held->image_translation.size && held->image_translation.size != 256) ||
        !qa_source_save_u64(io, &palette) || palette > UINT32_MAX ||
        !qa_source_save_bytes(io, held->image_palette_source.bytes, sizeof(held->image_palette_source.bytes))) return false;
    options->family = (qa_scene_family)family; options->wrap = (qa_scene_wrap)wrap;
    options->filter = (qa_scene_filter)filter; options->usage = (qa_scene_image_usage)usage;
    options->palette_rgb = (qa_bytes){held->image_palette.data, held->image_palette.size};
    options->translation = (qa_bytes){held->image_translation.data, held->image_translation.size};
    if (io->direction == QA_SOURCE_SAVE_READ) held->image_palette_dependency = palette ? (size_t)(palette - 1) : SIZE_MAX;
    return true;
}

static bool image_source_current(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held, qa_error *error)
{
    static const qa_sha256_digest zero = {0};
    if (held->image_options.family > QA_SCENE_Q3 || held->image_options.wrap > QA_SCENE_CLAMP ||
        held->image_options.filter > QA_SCENE_LINEAR_MIPMAP_LINEAR || held->image_options.usage > QA_IMAGE_USAGE_SKY ||
        (held->image_palette.size && held->image_palette.size != 768) ||
        (held->image_translation.size && held->image_translation.size != 256) ||
        (held->image_options.source_q3 && !qa_q3_image_upload_options_valid(&held->image_options.source_upload, error)))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 image holder lost its genuine Source decode options");
    if (held->image_palette_dependency == SIZE_MAX)
        return qa_sha256_equal(&held->image_palette_source, &zero) ||
            application_fail(error, QA_ERROR_FORMAT, "Q2 image palette digest has no actual admission receipt");
    const application_q2_held_resource *palette = held->image_palette_dependency < owner->held_resource_count ?
        &owner->held_resources[held->image_palette_dependency] : NULL;
    if (!palette || palette->kind != APPLICATION_Q2_HELD_DEPENDENCY || palette->missing || !palette->resource ||
        !held->image_palette.size || palette->provider != held->provider ||
        !qa_sha256_equal(&palette->identity, &held->identity) || !qa_sha256_equal(&palette->authority, &held->authority) ||
        !qa_vfs_lookup_equal(palette->view, held->view))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 image lost its actual Source palette holder");
    qa_sha256_digest digest; qualified_digest(palette, false, &digest);
    return qa_sha256_equal(&digest, &held->image_palette_source) ||
        application_fail(error, QA_ERROR_FORMAT, "Q2 image palette differs from its retained first admission");
}

static bool derivation_current(qa_application_network_q2 *owner, const application_q2_held_resource *held,
    qa_error *error)
{
    if (held->kind != APPLICATION_Q2_HELD_MATERIAL && (held->script_name || held->source_offset ||
        held->script_size || held->name_offset || held->name_size || held->catalog_bytes.size))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 non-material holder carries a foreign catalog span");
    if ((held->sky_face ? held->kind != APPLICATION_Q2_HELD_DEPENDENCY || !held->sky_base : held->sky_base != NULL) ||
        (held->kind != APPLICATION_Q2_HELD_EVENT && (held->event_kind != QA_NATIVE_HOST_MODEL || held->event_key[0])))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 holder fields do not belong to its actual Source kind");
    if (held->kind == APPLICATION_Q2_HELD_IMAGE || held->kind == APPLICATION_Q2_HELD_MATERIAL) {
        if (!image_source_current(owner, held, error)) return false;
    } else if (held->image_palette.size || held->image_translation.size || held->image_palette_dependency)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 ordinary resource carries a foreign image palette owner");
    if (held->kind == APPLICATION_Q2_HELD_IMAGE)
        return application_network_q2_materials_image_validate(owner, held, error);
    if (held->kind == APPLICATION_Q2_HELD_MATERIAL)
        return application_network_q2_materials_validate(owner, held, error);
    if (held->kind == APPLICATION_Q2_HELD_EVENT) {
        if (held->event_kind > QA_NATIVE_HOST_IMAGE || held->wire_bytes.size || held->dependency_count ||
            !memchr(held->event_key, 0, sizeof(held->event_key)) || (held->missing ? held->event_key[0] != 0 : !held->event_key[0]))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 event holder lost its actual captured resource domain");
        if (held->missing) return true;
        const application_unified_event_resource *receipt = event_receipt(owner->app, held->event_key);
        const char *path = receipt ? qa_strings_cstr(qa_session_strings(owner->app->session), receipt->path) : NULL;
        const char *content = receipt ? qa_strings_cstr(qa_session_strings(owner->app->session), receipt->content) : NULL;
        application_provider *provider = provider_at(owner, held->provider);
        return (receipt && receipt->resource == held->resource && path && !strcmp(path, held->path) && content &&
            provider && provider->product && !strcmp(content, provider->product->identity)) ||
            application_fail(error, QA_ERROR_FORMAT, "Q2 event holder differs from its immutable captured resource key");
    }
    if (held->kind == APPLICATION_Q2_HELD_DEPENDENCY)
        return !held->wire_bytes.size && !held->dependency_count;
    qa_bytes original = qa_resource_bytes(held->resource);
    bool md2 = original.size >= 4 && !memcmp(original.data, "IDP2", 4);
    bool sprite = original.size >= 4 && !memcmp(original.data, "IDS2", 4);
    if (!md2 && !sprite)
        return original.size >= 4 && !memcmp(original.data, "IDP3", 4) ?
            application_network_q2_materials_validate(owner, held, error) : !held->wire_bytes.size && !held->dependency_count;
    qa_model model = {0};
    if (!qa_model_load(original, &model, error)) return false;
    size_t count = md2 ? model.skin_count : model.sprite_count;
    size_t first = md2 ? qa_load_u32le(original.data + 44) : 28, stride = md2 ? 64 : 80;
    bool ok = held->dependency_count == count && (count ? held->wire_bytes.size == original.size : !held->wire_bytes.size);
    if (!ok) application_fail(error, QA_ERROR_FORMAT, "Q2 derived model lost its authentic external dependency table");
    size_t position = 0; qa_sha256_digest authority; qualified_digest(held, false, &authority);
    for (size_t i = 0; ok && i < count; ++i) {
        size_t offset = first + i * stride;
        if (memcmp(original.data + position, held->wire_bytes.data + position, offset - position)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 derived model changes bytes outside its external names"); break;
        }
        const char *name = md2 ? model.skins[i].name : model.sprites[i].image;
        size_t dependency = held->dependencies[i];
        if (!*name) {
            ok = dependency == SIZE_MAX && !memcmp(original.data + offset, held->wire_bytes.data + offset, 64);
        } else {
            const application_q2_held_resource *actual = dependency < owner->held_resource_count ?
                &owner->held_resources[dependency] : NULL;
            char *path = qa_scene_model_image_path(name, error); uint8_t replacement[64] = {0};
            ok = path && actual && actual->kind == APPLICATION_Q2_HELD_DEPENDENCY &&
                actual->provider == held->provider && !strcmp(actual->path, path) &&
                qa_sha256_equal(&actual->identity, &held->identity) && qa_sha256_equal(&actual->authority, &authority) &&
                qa_vfs_lookup_equal(actual->view, held->view) && strlen(actual->wire_path) < sizeof(replacement);
            if (ok) {
                memcpy(replacement, actual->wire_path, strlen(actual->wire_path));
                ok = !memcmp(replacement, held->wire_bytes.data + offset, sizeof(replacement));
            }
            free(path);
        }
        if (!ok && (!error || error->code == QA_OK))
            application_fail(error, QA_ERROR_FORMAT, "Q2 derived model lost its actual BODY dependency acquisition");
        position = offset + 64;
    }
    if (ok && count && memcmp(original.data + position, held->wire_bytes.data + position, original.size - position))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q2 derived model changes its genuine trailing geometry bytes");
    qa_model_free(&model); return ok;
}

static bool holder_fields(qa_application_network_q2 *owner, qa_source_save_io *io,
    application_q2_held_resource *held)
{
    qa_application_content_graph *graph = qa_application_content_graph_read(owner->app);
    uint64_t view = 0, pool = 0, resource = 0;
    bool memory = held->kind == APPLICATION_Q2_HELD_MATERIAL && !held->resource;
    if (!graph) return application_fail(io->error, QA_ERROR_ARGUMENT, "Q2 holder cold codec requires its actual content graph");
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        view = qa_application_content_view_id(graph, held->view);
        if (!view || (!held->missing && !memory && !qa_application_content_resource_id(graph, held->resource, &pool, &resource)))
            return application_fail(io->error, QA_ERROR_ARGUMENT, "Q2 holder is absent from the real immutable content inventory");
    }
    uint32_t kind = (uint32_t)held->kind;
    if (!qa_source_save_u32(io, &kind) || kind > APPLICATION_Q2_HELD_IMAGE ||
        !qa_source_save_bool(io, &held->missing) ||
        (held->missing && kind != APPLICATION_Q2_HELD_DEPENDENCY && kind != APPLICATION_Q2_HELD_EVENT)) return false;
    held->kind = (application_q2_held_kind)kind;
    if (!qa_source_save_bool(io, &memory) || (memory && (held->kind != APPLICATION_Q2_HELD_MATERIAL || held->missing))) return false;
    if (!text_field(io, &held->instance) || !text_field(io, &held->path) || !text_field(io, &held->wire_path) ||
        !qa_source_save_bytes(io, held->identity.bytes, 32) || !qa_source_save_bytes(io, held->authority.bytes, 32) ||
        !qa_source_save_u64(io, &view) ||
        !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        application_provider *provider = NULL;
        for (size_t i = 0; i < owner->app->provider_count; ++i) {
            application_provider *candidate = owner->app->providers[i];
            if (candidate->launch && held->instance && !strcmp(candidate->launch->selection.instance, held->instance)) {
                if (provider) return application_fail(io->error, QA_ERROR_FORMAT, "Q2 restored holder aliases Source instances");
                provider = candidate;
            }
        }
        const qa_resource *actual = held->missing || memory ? NULL : qa_application_content_resource(graph, pool, resource);
        if (!provider || !qa_sha256_equal(&held->identity, &provider->launch->identity) ||
            (held->missing || memory ? pool || resource : !actual) ||
            !held->path || !*held->path || !held->wire_path ||
            (strncmp(held->wire_path, "models/qa/", 10) && strncmp(held->wire_path, "players/qa/", 11) &&
                strncmp(held->wire_path, "sound/qa/", 9)) ||
            !qa_application_content_retain_view(graph, view, &held->view, io->error))
            return application_fail(io->error, QA_ERROR_FORMAT, "Q2 restored holder lost its real provider or immutable bytes");
        held->provider = provider->owner; held->resource = (qa_resource *)actual;
        if (held->resource) qa_resource_retain(held->resource);
    }
    qa_vfs_acquisition *opening = &held->opening;
    if (!held->missing && !memory && (!qa_source_save_u64(io, &opening->mount) || !qa_source_save_u64(io, &opening->resource_id) ||
        !text_field(io, &opening->path) || !text_field(io, &opening->lookup_path) ||
        !text_field(io, &opening->link_source) || !text_field(io, &opening->link_target) ||
        !qa_vfs_acquisition_opening_codec(io, held->view, opening) ||
        opening->resource_id != qa_resource_id(held->resource) ||
        !qa_vfs_acquisition_retained(held->view, opening, io->error))) return false;
    if (!text_field(io, &held->script_name) || !text_field(io, &held->sky_base) ||
        !qa_source_save_bytes(io, held->sky_group.bytes, sizeof(held->sky_group.bytes)) ||
        !qa_source_save_u8(io, &held->sky_face) || held->sky_face > 6 ||
        !qa_source_save_count(io, &held->source_offset, INT32_MAX) ||
        !qa_source_save_count(io, &held->script_size, INT32_MAX) ||
        !qa_source_save_count(io, &held->name_offset, INT32_MAX) ||
        !qa_source_save_count(io, &held->name_size, INT32_MAX) ||
        !buffer_field(io, &held->catalog_bytes, memory ? INT32_MAX : 0)) return false;
    uint32_t event_kind = (uint32_t)held->event_kind;
    if (!qa_source_save_u32(io, &event_kind) || event_kind > QA_NATIVE_HOST_IMAGE ||
        !qa_source_save_bytes(io, held->event_key, sizeof(held->event_key))) return false;
    held->event_kind = (qa_native_host_resource_kind)event_kind;
    if ((held->kind == APPLICATION_Q2_HELD_IMAGE || held->kind == APPLICATION_Q2_HELD_MATERIAL) &&
        !image_fields(io, held)) return false;
    size_t maximum = held->kind == APPLICATION_Q2_HELD_MATERIAL || held->kind == APPLICATION_Q2_HELD_IMAGE ? INT32_MAX :
        held->resource ? qa_resource_bytes(held->resource).size : 0;
    size_t references = held->kind == APPLICATION_Q2_HELD_MATERIAL ? INT32_MAX :
        held->kind == APPLICATION_Q2_HELD_MODEL ? maximum / 64 : 0;
    if (!buffer_field(io, &held->wire_bytes, maximum) ||
        !qa_source_save_count(io, &held->dependency_count, references)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && held->dependency_count) {
        if (held->dependency_count > SIZE_MAX / sizeof(*held->dependencies) || io->offset > io->input.size ||
            held->dependency_count > (io->input.size - io->offset) / 8)
            return application_fail(io->error, QA_ERROR_FORMAT, "Q2 dependency references exceed their cold payload");
        held->dependencies = calloc(held->dependency_count, sizeof(*held->dependencies));
        if (!held->dependencies) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring Q2 actual dependency references");
    }
    for (size_t i = 0; i < held->dependency_count; ++i) {
        uint64_t index = io->direction == QA_SOURCE_SAVE_WRITE && held->dependencies[i] != SIZE_MAX ?
            (uint64_t)held->dependencies[i] + 1 : 0;
        if (!qa_source_save_u64(io, &index) || index > UINT32_MAX) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) held->dependencies[i] = index ? (size_t)(index - 1) : SIZE_MAX;
    }
    char actual[64];
    return qualified_name(held, actual, io->error) && (!strcmp(actual, held->wire_path) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Q2 qualified model name differs from its actual Source acquisition"));
}

bool application_network_q2_resources_capture(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, owner->app->session, error)) return false;
    uint32_t version = 5; size_t count = owner->held_resource_count;
    bool ok = qa_source_save_u32(&io, &version) && qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i) ok = derivation_current(owner, &owner->held_resources[i], error) &&
        holder_fields(owner, &io, &owner->held_resources[i]);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool application_network_q2_resources_restore(qa_application_network_q2 *owner, qa_bytes bytes, qa_error *error)
{
    if (owner->held_resource_count) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 holders restore requires an empty owner");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, owner->app->session, bytes, error)) return false;
    uint32_t version = 0; size_t count = 0;
    bool ok = qa_source_save_u32(&io, &version) && version == 5 && qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        application_q2_held_resource held = {0};
        ok = holder_fields(owner, &io, &held) && derivation_current(owner, &held, error);
        for (size_t j = 0; ok && j < owner->held_resource_count; ++j)
            if (!strcmp(held.wire_path, owner->held_resources[j].wire_path))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored holders alias a qualified wire path");
        if (ok) ok = held_append(owner, &held, error);
        held_free(&held);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); return ok;
}

#include "network_q2_private.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include "qa/binary.h"
#include "qa/model.h"
#include "qa/scene.h"
#include "qa/scene_model_save.h"
#include "network_q2_materials.h"
#include "unified_events.h"
#include <limits.h>
#include <stdio.h>

static void held_free(application_q2_held_resource *held)
{
    qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
    qa_vfs_destroy(held->view);
    qa_buffer_free(&held->wire_bytes); free(held->dependencies);
    qa_buffer_free(&held->model_scope_bytes); free(held->model_scope_path);
    qa_buffer_free(&held->catalog_bytes); free(held->script_name); free(held->sky_base);
    qa_buffer_free(&held->image_palette); qa_buffer_free(&held->image_translation);
    free(held->image_request); free(held->image_logical_path);
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

static bool text_equal(const char *a, const char *b)
{ return a == b || (a && b && !strcmp(a, b)); }

static bool bytes_equal(qa_bytes a, qa_bytes b)
{ return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size)); }

static bool acquisition_equal(const qa_vfs_acquisition *a, const qa_vfs_acquisition *b)
{
    return a->mount == b->mount && a->resource_id == b->resource_id &&
        text_equal(a->path, b->path) && text_equal(a->lookup_path, b->lookup_path) &&
        text_equal(a->link_source, b->link_source) && text_equal(a->link_target, b->link_target) &&
        a->opening_present == b->opening_present && a->opening.rank == b->opening.rank &&
        text_equal(a->opening.prefix, b->opening.prefix) && a->opening.user_overlay == b->opening.user_overlay &&
        a->opening.order_count == b->opening.order_count && (!a->opening.order_count ||
        !memcmp(a->opening.order, b->opening.order, a->opening.order_count * sizeof(*a->opening.order)));
}

static bool image_options_equal(const qa_scene_image_options *a, const qa_scene_image_options *b)
{
    return a->family == b->family && a->wrap == b->wrap && a->filter == b->filter && a->usage == b->usage &&
        a->mipmap == b->mipmap && a->transparent == b->transparent && a->fullbright_only == b->fullbright_only &&
        a->transparent_index == b->transparent_index && a->source_q3 == b->source_q3 &&
        (!a->source_q3 || qa_q3_image_upload_options_equal(&a->source_upload, &b->source_upload));
}

static bool held_same(const application_q2_held_resource *a, const application_q2_held_resource *b, bool derived)
{
    if (a->identity != b->identity || a->provider != b->provider || a->kind != b->kind ||
        a->missing != b->missing || a->authority != b->authority || !text_equal(a->path, b->path) ||
        a->resource != b->resource || !qa_vfs_lookup_equal(a->view, b->view) ||
        (a->resource && !acquisition_equal(&a->opening, &b->opening))) return false;
    if (a->kind == APPLICATION_Q2_HELD_MATERIAL && (!text_equal(a->script_name, b->script_name) ||
        a->source_offset != b->source_offset || a->script_size != b->script_size ||
        a->name_offset != b->name_offset || a->name_size != b->name_size || (!a->resource &&
        !bytes_equal((qa_bytes){a->catalog_bytes.data, a->catalog_bytes.size},
                     (qa_bytes){b->catalog_bytes.data, b->catalog_bytes.size})))) return false;
    if (a->kind == APPLICATION_Q2_HELD_EVENT && (a->event_kind != b->event_kind ||
        !text_equal(a->event_key, b->event_key) || a->event_custody != b->event_custody)) return false;
    bool images = a->kind == APPLICATION_Q2_HELD_IMAGE || a->kind == APPLICATION_Q2_HELD_MATERIAL ||
        a->kind == APPLICATION_Q2_HELD_ALIAS || (derived && a->kind == APPLICATION_Q2_HELD_MODEL && a->model_scope);
    if (images && (!image_options_equal(&a->image_options, &b->image_options) ||
        a->image_palette_dependency != b->image_palette_dependency ||
        !bytes_equal((qa_bytes){a->image_palette.data, a->image_palette.size},
                     (qa_bytes){b->image_palette.data, b->image_palette.size}) ||
        !bytes_equal((qa_bytes){a->image_translation.data, a->image_translation.size},
                     (qa_bytes){b->image_translation.data, b->image_translation.size}))) return false;
    if (a->kind == APPLICATION_Q2_HELD_ALIAS && (!text_equal(a->image_request, b->image_request) ||
        !text_equal(a->image_logical_path, b->image_logical_path) ||
        a->image_palette_attempted != b->image_palette_attempted || a->image_rejection != b->image_rejection ||
        a->image_palette_error != b->image_palette_error || a->image_logical_dependency != b->image_logical_dependency)) return false;
    if (a->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT && a->receipt_source != b->receipt_source) return false;
    return !derived || (a->model_scope == b->model_scope && bytes_equal(
        (qa_bytes){a->wire_bytes.data, a->wire_bytes.size}, (qa_bytes){b->wire_bytes.data, b->wire_bytes.size}));
}

static bool held_serial(qa_application_network_q2 *owner, application_q2_held_resource *held, qa_error *error)
{
    if (held->serial) return true;
    if (owner->held_resource_serial == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Q2 resource serial space is exhausted");
    held->serial = ++owner->held_resource_serial;
    return true;
}

static bool qualified_name(const application_q2_held_resource *held, char alias[64], qa_error *error)
{
    static const char *const faces[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    const char *extension = held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT ? ".qai" :
        held->kind == APPLICATION_Q2_HELD_MATERIAL ? ".shader" :
        held->kind == APPLICATION_Q2_HELD_IMAGE ? ".png" : strrchr(held->path, '.');
    const char *separator = strrchr(held->path, '/');
    if (!extension || ((held->kind != APPLICATION_Q2_HELD_MATERIAL && held->kind != APPLICATION_Q2_HELD_IMAGE &&
        held->kind != APPLICATION_Q2_HELD_IMAGE_RECEIPT) &&
        separator && extension < separator) || strlen(extension) > 8)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 mixed resource has no genuine bounded file extension");
    uint64_t serial = held->serial;
    if (held->sky_face) {
        if (held->sky_face > 6 || !held->sky_base)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 sky dependency lost its genuine face receipt");
        serial = held->sky_group;
    }
    if (!serial) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 alias has no actual resource serial");
    const char *category = held->kind == APPLICATION_Q2_HELD_EVENT && held->event_kind == QA_NATIVE_HOST_SOUND ?
        "sound" : !strncmp(held->sky_base ? held->sky_base : held->path, "players/", 8) ? "players" : "models";
    if (held->sky_face) snprintf(alias, 64, "%s/qa/%016llx_%s%s", category, (unsigned long long)serial, faces[held->sky_face - 1], extension);
    else snprintf(alias, 64, "%s/qa/%016llx%s", category, (unsigned long long)serial, extension);
    return true;
}

static bool model_scope_finish(qa_application_network_q2 *owner,
    application_q2_held_resource *held, qa_error *error)
{
    if (!held->model_scope) return true;
    char companion[64]; const char *extension = strrchr(held->wire_path, '.');
    size_t prefix = extension ? (size_t)(extension - held->wire_path) : strlen(held->wire_path);
    if (prefix > sizeof(companion) - 5)
        return application_fail(error, QA_ERROR_FORMAT, "Indexed model scope alias exceeds its real namespace");
    memcpy(companion, held->wire_path, prefix); memcpy(companion + prefix, ".qpm", 5);
    held->model_scope_path = application_network_q2_copy(companion, error);
    return held->model_scope_path && application_network_q2_materials_model_scope_encode(owner, held,
        &held->model_scope_bytes, error);
}

static bool held_store(qa_application_network_q2 *owner, application_q2_held_resource *held,
    size_t *out, qa_error *error)
{
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (held_same(held, old, held->kind != APPLICATION_Q2_HELD_IMAGE_RECEIPT) &&
            held->sky_face == old->sky_face && held->sky_group == old->sky_group) {
            *out = i; return true;
        }
    }
    char alias[64];
    if (!held_serial(owner, held, error) || !qualified_name(held, alias, error)) return false;
    held->wire_path = application_network_q2_copy(alias, error);
    if (!held->wire_path) return false;
    if (!model_scope_finish(owner, held, error)) return false;
    *out = owner->held_resource_count; return held_append(owner, held, error);
}

static bool retained_opening(const qa_vfs *view, const char *path, const qa_resource *resource,
    const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    if (source) return source->resource_id == qa_resource_id(resource) &&
        qa_vfs_acquisition_retained(view, source, error) && qa_vfs_acquisition_copy(source, out, error);
    bool found = false;
    for (size_t i = 0; i < qa_vfs_retained_read_count(view); ++i) {
        qa_vfs_read_reference row;
        if (!qa_vfs_retained_read_at(view, i, &row) || row.resource != resource || !row.path ||
            (path ? strcmp(row.path, path) != 0 : strncmp(row.path, "scripts/", 8) != 0)) continue;
        qa_vfs_acquisition actual = {.mount = row.mount, .resource_id = qa_resource_id(resource),
            .path = (char *)row.path, .lookup_path = (char *)row.lookup_path,
            .link_source = (char *)row.link_source, .link_target = (char *)row.link_target,
            .opening = row.opening, .opening_present = true};
        if (found && !acquisition_equal(out, &actual)) {
            qa_vfs_acquisition_dispose(out);
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency has ambiguous actual historical opening recipes");
        }
        if (!found) {
            if (!qa_vfs_acquisition_retained(view, &actual, error) || !qa_vfs_acquisition_copy(&actual, out, error)) return false;
            found = true;
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
    held->authority = model->serial;
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
        model->provider != dependency->provider || model->identity != dependency->identity ||
        !qa_vfs_lookup_equal(model->view, dependency->view)) return false;
    return model->serial && model->serial == dependency->authority;
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

    }
    if (ok) {
        held.image_options.palette_rgb = (qa_bytes){held.image_palette.data, held.image_palette.size};
        held.image_options.translation = (qa_bytes){held.image_translation.data, held.image_translation.size};
        ok = held_store(owner, &held, out, error);
    }
    held_free(&held); return ok;
}

static bool alias_prepare(qa_application_network_q2 *owner, const application_q2_held_resource *model,
    const application_q2_image_receipt *receipt, application_q2_held_resource *held, qa_error *error)
{
    if (!receipt || !receipt->request || !receipt->path || !receipt->options ||
        (receipt->palette_rgb.size && receipt->palette_rgb.size != 768) ||
        (receipt->options->translation.size && receipt->options->translation.size != 256) ||
        ((receipt->logical_source != NULL) != (receipt->logical_path != NULL)) ||
        (receipt->palette_source && (!receipt->palette_source->resource || !receipt->palette_source->opening)) ||
        (unsigned)receipt->rejection > QA_ERROR_NOT_FOUND || (unsigned)receipt->palette_error > QA_ERROR_NOT_FOUND ||
        receipt->rejection == QA_ERROR_MEMORY || receipt->palette_error == QA_ERROR_MEMORY)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 image alias requires its actual Source admission outcome");
    bool ok = dependency_prepare(model, receipt->path, receipt->source, receipt->opening, held, error);
    held->kind = APPLICATION_Q2_HELD_ALIAS; held->image_options = *receipt->options;
    held->image_options.palette_rgb = held->image_options.translation = (qa_bytes){0};
    held->image_palette_dependency = held->image_logical_dependency = SIZE_MAX;
    held->image_rejection = receipt->rejection; held->image_palette_error = receipt->palette_error;
    held->image_palette_attempted = receipt->palette_attempted;
    held->image_request = qa_scene_model_image_path(receipt->request, error);
    if (ok) ok = held->image_request && buffer_copy(receipt->palette_rgb, &held->image_palette, error) &&
        buffer_copy(receipt->options->translation, &held->image_translation, error);
    if (ok && receipt->palette_source) {
        ok = application_network_q2_dependency_receipt(owner, model, receipt->palette_source->opening->path,
            receipt->palette_source->resource, receipt->palette_source->opening, &held->image_palette_dependency, error);

    }
    if (ok && receipt->logical_source) {
        held->image_logical_path = qa_scene_model_image_path(receipt->logical_path, error);
        ok = held->image_logical_path && receipt->logical_opening && receipt->logical_opening->path &&
            application_network_q2_dependency_receipt(owner, model, receipt->logical_opening->path, receipt->logical_source,
            receipt->logical_opening, &held->image_logical_dependency, error);

    }
    held->image_options.palette_rgb = (qa_bytes){held->image_palette.data, held->image_palette.size};
    held->image_options.translation = (qa_bytes){held->image_translation.data, held->image_translation.size};
    return ok;
}

bool application_network_q2_dependency_alias(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const application_q2_image_receipt *receipt,
    size_t *out, qa_error *error)
{
    if (!owner || !model || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 image alias needs its actual model holder");
    application_q2_held_resource held = {0};
    bool ok = alias_prepare(owner, model, receipt, &held, error) && held_store(owner, &held, out, error);
    held_free(&held); return ok;
}

bool application_network_q2_dependency_image_receipt(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, size_t alias_index, size_t *out, qa_error *error)
{
    const application_q2_held_resource *alias = owner && alias_index < owner->held_resource_count ?
        &owner->held_resources[alias_index] : NULL;
    if (!model || !model->resource || !out || !alias || alias->kind != APPLICATION_Q2_HELD_ALIAS ||
        !application_network_q2_dependency_of(model, alias))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 image receipt requires its genuine model and Source image alias");
    application_q2_held_resource held = {0};
    bool ok = dependency_prepare(model, model->path, model->resource, &model->opening, &held, error);
    held.kind = APPLICATION_Q2_HELD_IMAGE_RECEIPT;
    if (ok) {
        held.receipt_source = alias->serial;
        held.dependencies = malloc(sizeof(*held.dependencies));
        if (!held.dependencies) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 actual image receipt dependency");
        else { held.dependencies[0] = alias_index; held.dependency_count = 1; }
    }
    char name[64];
    if (ok) ok = held_serial(owner, &held, error) && qualified_name(&held, name, error) &&
        application_network_q2_materials_image_receipt_encode(owner, alias, name, &held.wire_bytes, error) &&
        held_store(owner, &held, out, error);
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
    held.authority = model->serial;
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

    }
    held.image_options.palette_rgb = (qa_bytes){held.image_palette.data, held.image_palette.size};
    if (ok) ok = held_store(owner, &held, out, error);
    held_free(&held); return ok;
}

static bool sky_store(qa_application_network_q2 *owner, application_q2_held_resource faces[6],
    const char *normalized, size_t out[6], char alias_base[64], qa_error *error)
{
    uint64_t group = 0;
    for (size_t candidate = 0; candidate < owner->held_resource_count; ++candidate) {
        const application_q2_held_resource *first = &owner->held_resources[candidate];
        if (first->sky_face != 1 || !text_equal(first->sky_base, normalized) || !held_same(first, &faces[0], false)) continue;
        bool same = true;
        for (size_t face = 0; same && face < 6; ++face) {
            bool found = false;
            for (size_t i = 0; i < owner->held_resource_count; ++i) {
                const application_q2_held_resource *old = &owner->held_resources[i];
                if (old->sky_group == first->sky_group && old->sky_face == face + 1 &&
                    text_equal(old->sky_base, normalized) && held_same(old, &faces[face], false)) {
                    out[face] = i; found = true; break;
                }
            }
            same = found;
        }
        if (same) { group = first->sky_group; break; }
    }
    if (!group) {
        application_q2_held_resource receipt = {0};
        if (!held_serial(owner, &receipt, error)) return false;
        group = receipt.serial;
        for (size_t i = 0; i < 6; ++i) {
            faces[i].sky_base = application_network_q2_copy(normalized, error);
            faces[i].sky_face = (uint8_t)(i + 1); faces[i].sky_group = group;
            if (!faces[i].sky_base || !held_store(owner, &faces[i], &out[i], error)) return false;
        }
    }
    snprintf(alias_base, 64, "%s/qa/%016llx", !strncmp(normalized, "players/", 8) ? "players" : "models",
        (unsigned long long)group);
    return true;
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
    for (size_t i = 0; ok && i < 6; ++i)
        ok = paths[i] && dependency_prepare(model, paths[i], resources[i], openings ? openings[i] : NULL, &faces[i], error);
    if (ok) ok = sky_store(owner, faces, normalized, out, alias_base, error);
    for (size_t i = 0; i < 6; ++i) held_free(&faces[i]);
    free(normalized); return ok;
}

bool application_network_q2_sky_aliases(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *base, const application_q2_image_receipt receipts[6],
    size_t out[6], char alias_base[64], qa_error *error)
{
    if (!owner || !model || !base || !receipts || !out || !alias_base)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 sky aliases need their six actual Source image admissions");
    application_q2_held_resource faces[6] = {0};
    char *normalized = qa_scene_model_image_path(base, error);
    bool ok = normalized != NULL;
    for (size_t i = 0; ok && i < 6; ++i)
        ok = alias_prepare(owner, model, &receipts[i], &faces[i], error);
    if (ok) ok = sky_store(owner, faces, normalized, out, alias_base, error);
    for (size_t i = 0; i < 6; ++i) held_free(&faces[i]);
    free(normalized); return ok;
}

bool application_network_q2_dependency(qa_application_network_q2 *owner,
    const application_q2_held_resource *model, const char *name, size_t *out, qa_error *error)
{
    if (!owner || !model || !model->view || !model->resource || model->missing || !name || !*name || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 dependency needs its genuine acquired model owner");
    char *path = qa_scene_model_image_path(name, error);
    if (!path) return false;
    uint64_t authority = model->serial;
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (old->kind == APPLICATION_Q2_HELD_DEPENDENCY && old->provider == model->provider &&
            old->identity == model->identity && old->authority == authority &&
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
    if (ok) ok = held_store(owner, &held, out, error);
    held_free(&held); return ok;
}

static void model_translation(uint8_t colors, uint8_t table[256])
{
    for (unsigned i = 0; i < 256; ++i) table[i] = (uint8_t)i;
    unsigned top = colors & 240u, bottom = (colors & 15u) << 4;
    for (unsigned i = 0; i < 16; ++i) {
        table[16 + i] = (uint8_t)(top < 128 ? top + i : top + 15 - i);
        table[96 + i] = (uint8_t)(bottom < 128 ? bottom + i : bottom + 15 - i);
    }
}

static bool model_colors_current(const application_q2_held_resource *held,
    const qa_application_visual_view *visual)
{
    qa_bytes bytes = qa_resource_bytes(held->resource);
    bool translated = bytes.size >= 4 && !memcmp(bytes.data, "IDPO", 4) && visual->has_player_colors;
    if (held->image_translation.size != (translated ? 256u : 0u)) return false;
    if (!translated) return true;
    uint8_t table[256]; model_translation(visual->player_colors, table);
    return !memcmp(table, held->image_translation.data, sizeof(table));
}

static bool model_palette_admission(qa_application_network_q2 *owner,
    application_q2_held_resource *held, qa_game_family family, bool has_colors,
    uint8_t colors, qa_error *error)
{
    qa_application_model_admission_request request = {.provider = held->provider, .family = family,
        .request = held->path, .resource = held->resource, .opening = &held->opening, .view = held->view,
        .has_player_colors = has_colors, .player_colors = colors};
    qa_application_model_admission admission = {0}; bool handled = false;
    if (!qa_application_model_admit(owner->app, &request, &admission, &handled, error)) return false;
    qa_scene_resources *bank = NULL; qa_material_library *materials = NULL;
    qa_scene_model *scene = NULL; qa_model model = {0};
    bool ok = true;
    if (!handled) {
        qa_scene_image_options images = {.family = family == QA_GAME_Q2 ? QA_SCENE_Q2 :
            family == QA_GAME_Q3 ? QA_SCENE_Q3 : QA_SCENE_Q1,
            .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR,
            .mipmap = true, .usage = QA_IMAGE_USAGE_SKIN,
            .transparent_index = family == QA_GAME_Q1 ? 255 : -1};
        bank = qa_scene_resources_create(held->view, error);
        ok = bank && qa_model_load(qa_resource_bytes(held->resource), &model, error);
        uint8_t translation[256];
        if (ok && has_colors && model.format == QA_MODEL_MDL) {
            model_translation(colors, translation);
            images.translation = (qa_bytes){translation, sizeof(translation)};
        }
        if (ok && images.family == QA_SCENE_Q3) {
            materials = qa_material_library_create_detached(bank, error); ok = materials != NULL;
        }
        if (ok) ok = qa_scene_model_create(&model, bank, materials, &images, &scene, error);
        const qa_scene_image_options *actual = ok ? qa_scene_model_image_options(scene) : NULL;
        if (ok) ok = actual && qa_scene_resources_palette_read(bank, QA_SCENE_Q1, &admission.palette_rgb) &&
            qa_scene_resources_palette_source_read(bank, QA_SCENE_Q1, &admission.palette_source);
        if (ok) admission.images = *actual;
        if (ok) admission.palette_view = held->view;
    }
    if (ok && (admission.palette_rgb.size != 768 || !admission.palette_rgb.data ||
        !admission.palette_source.resource || !admission.palette_source.opening || !admission.palette_view ||
        !qa_vfs_lookup_equal(admission.palette_view, held->view)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Indexed model constructor lost its genuine BODY palette");
    if (ok) {
        held->model_scope = true; held->image_options = admission.images;
        held->image_options.palette_rgb = held->image_options.translation = (qa_bytes){0};
        held->image_palette_dependency = SIZE_MAX;
        application_q2_held_resource palette_scope = *held;
        palette_scope.view = (qa_vfs *)admission.palette_view;
        ok = buffer_copy(admission.palette_rgb, &held->image_palette, error) &&
            buffer_copy(admission.images.translation, &held->image_translation, error) &&
            application_network_q2_dependency_receipt(owner, &palette_scope,
                admission.palette_source.opening->path, admission.palette_source.resource,
                admission.palette_source.opening, &held->image_palette_dependency, error);
        if (ok) {
            held->image_options.palette_rgb = (qa_bytes){held->image_palette.data, held->image_palette.size};
            held->image_options.translation = (qa_bytes){held->image_translation.data, held->image_translation.size};
        }
    }
    qa_scene_model_destroy(scene); qa_model_free(&model);
    qa_material_library_destroy(materials); qa_scene_resources_destroy(bank);
    return ok;
}

static bool model_derivation(qa_application_network_q2 *owner, application_q2_held_resource *held,
    qa_game_family family, bool has_colors, uint8_t colors, qa_error *error)
{
    qa_bytes original = qa_resource_bytes(held->resource);
    if (original.size >= 4 && (!memcmp(original.data, "IDPO", 4) || !memcmp(original.data, "IDSP", 4)))
        return model_palette_admission(owner, held, family, has_colors, colors, error);
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
        .dialect = visual->family == QA_GAME_Q2 ? QA_RULESET_Q2_CLASSIC :
            visual->family == QA_GAME_Q3 ? QA_RULESET_Q3 : QA_RULESET_NETQUAKE}, captured;
    if (!qa_application_capture_command_context(owner->app, &request, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(owner->app, &captured, NULL);
    if (!files) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 selected model has no actual provider file scope");
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *old = &owner->held_resources[i];
        if (old->kind == APPLICATION_Q2_HELD_MODEL && old->provider == visual->provider && !strcmp(old->path, path) &&
            (!visual->model_resources[model] || old->resource == visual->model_resources[model]) &&
            old->identity == provider->launch->identity && qa_vfs_lookup_equal(old->view, files) &&
            model_colors_current(old, visual)) {
            if (visual->model_openings[model]) {
                if (!acquisition_equal(&old->opening, visual->model_openings[model])) continue;
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
            qa_vfs_acquisition_retained(held.view, opening, error) && qa_vfs_acquisition_copy(opening, &held.opening, error);
        if (ok) { held.resource = (qa_resource *)visual->model_resources[model]; qa_resource_retain(held.resource); }
    } else if (ok) ok = qa_vfs_acquire_receipt(held.view, path, &held.resource, &held.opening, error);
    size_t dependency_start = owner->held_resource_count;
    if (ok) ok = held_serial(owner, &held, error);
    if (ok) ok = model_derivation(owner, &held, visual->family,
        visual->has_player_colors, visual->player_colors, error);
    if (ok) {
        char alias[64];
        ok = qualified_name(&held, alias, error);
        if (ok) {
            held.wire_path = application_network_q2_copy(alias, error);
            ok = held.wire_path != NULL;
        }
        if (ok) ok = model_scope_finish(owner, &held, error);
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
    if (!owner || !reference || !reference->name || !out || (unsigned)reference->kind > QA_NATIVE_HOST_IMAGE ||
        !memchr(reference->resource_key, 0, sizeof(reference->resource_key)) ||
        !application_network_q2_current(owner, error) ||
        !application_unified_event_source_read(owner->app, emitter, &source, error) ||
        !source.descriptor || !source.content || !source.product || source.product->family != QA_GAME_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 event resource lost its captured Source owner");
    unsigned kind = (unsigned)reference->kind;
    if (!*reference->name) { *out = 0; return true; }
    if (!*reference->resource_key && ((kind == QA_NATIVE_HOST_SOUND && reference->name[0] == '*') ||
        (kind == QA_NATIVE_HOST_MODEL && (reference->name[0] == '*' || reference->name[0] == '#')))) {
        application_provider *actual = provider_at(owner, emitter);
        if (kind == QA_NATIVE_HOST_MODEL && reference->name[0] == '*' &&
            (!actual || actual->kind != APPLICATION_PROVIDER_NATIVE || !actual->state.native.q2_engine ||
                actual->state.native.q2_engine->world != owner->app->world))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 inline model lost its actual shared WORLD map");
        return application_network_q2_resource(owner, kind, reference->name, out, error);
    }
    const application_unified_event_resource *receipt = *reference->resource_key ?
        event_receipt(owner->app, reference->resource_key) : NULL;
    const qa_resource *captured = NULL; const qa_vfs *captured_view = NULL;
    const qa_vfs_acquisition *captured_opening = NULL;
    if (receipt && !application_unified_event_resource_receipt_read(owner->app, reference->resource_key,
        reference->resource_custody, &captured, &captured_view, &captured_opening, error)) return false;
    const char *content = receipt ? qa_strings_cstr(qa_session_strings(owner->app->session), receipt->content) : NULL;
    const char *path = receipt ? captured_opening->path : reference->name;
    if (*reference->resource_key && (!receipt || !captured_view || !captured ||
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
    if (held.kind == APPLICATION_Q2_HELD_EVENT) {
        memcpy(held.event_key, reference->resource_key, sizeof(held.event_key)); held.event_custody = reference->resource_custody;
    }
    held.instance = application_network_q2_copy(source.descriptor->selection.instance, error);
    held.path = qa_scene_model_image_path(path, error);
    held.view = qa_vfs_clone(receipt ? captured_view : source.content, error);
    free(requested);
    bool ok = held.instance && held.path && held.view;
    if (ok && receipt) {
        ok = qa_vfs_acquisition_retained(held.view, captured_opening, error) &&
            qa_vfs_acquisition_copy(captured_opening, &held.opening, error);
        if (ok) { held.resource = (qa_resource *)captured; qa_resource_retain(held.resource); }
    }
    size_t dependency_start = owner->held_resource_count;
    size_t index = 0;
    bool reused = false;
    if (ok && held.kind == APPLICATION_Q2_HELD_MODEL) {
        const qa_application_visual_view colors = {0};
        for (size_t i = 0; i < owner->held_resource_count; ++i)
            if (held_same(&held, &owner->held_resources[i], false) &&
                model_colors_current(&owner->held_resources[i], &colors)) {
                index = i; reused = true; break;
            }
    }
    if (ok && !reused) ok = held_serial(owner, &held, error);
    if (ok && !reused && held.kind == APPLICATION_Q2_HELD_MODEL)
        ok = model_derivation(owner, &held, source.product->family, false, 0, error);
    if (ok && !reused) ok = held_store(owner, &held, &index, error);
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
        (held->missing ? (held->kind != APPLICATION_Q2_HELD_DEPENDENCY && held->kind != APPLICATION_Q2_HELD_EVENT &&
            held->kind != APPLICATION_Q2_HELD_ALIAS) ||
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
        bool companion = held->model_scope && held->model_scope_path && !strcmp(held->model_scope_path, path);
        if (!companion && strcmp(held->wire_path, path)) continue;
        *view = held->view;
        if (held->missing) { *status = QA_Q2_DOWNLOAD_MISSING; return true; }
        bool material = held->model_scope || held->kind == APPLICATION_Q2_HELD_MATERIAL ||
            held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT ||
            (held->kind == APPLICATION_Q2_HELD_MODEL && qa_resource_bytes(held->resource).size >= 4 &&
                !memcmp(qa_resource_bytes(held->resource).data, "IDP3", 4));
        if (material && (!owner->materials_bound || !owner->materials_capability)) {
            *status = QA_Q2_DOWNLOAD_MISSING; return true;
        }
        if (held->resource && (!qa_vfs_acquisition_retained(held->view, &held->opening, error) ||
            !qa_vfs_acquisition_copy(&held->opening, opening, error))) return false;
        if (!held->resource) {
            if (held->kind != APPLICATION_Q2_HELD_MATERIAL || !held->catalog_bytes.size)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 memory artifact lost its true catalog Source");
            source_bytes->data = malloc(held->catalog_bytes.size);
            if (!source_bytes->data) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 memory catalog transfer");
            memcpy(source_bytes->data, held->catalog_bytes.data, held->catalog_bytes.size);
            source_bytes->size = held->catalog_bytes.size;
        }
        qa_bytes offered = companion ? (qa_bytes){held->model_scope_bytes.data, held->model_scope_bytes.size} :
            (qa_bytes){held->wire_bytes.data, held->wire_bytes.size};
        if (companion && !offered.size) {
            qa_vfs_acquisition_dispose(opening);
            return application_fail(error, QA_ERROR_FORMAT, "Indexed model scope lost its retained Source metadata");
        }
        if (offered.size) {
            wire_bytes->data = malloc(offered.size);
            if (!wire_bytes->data) { qa_vfs_acquisition_dispose(opening);
                qa_buffer_free(source_bytes);
                return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 derived transfer bytes"); }
            memcpy(wire_bytes->data, offered.data, offered.size); wire_bytes->size = offered.size;
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
        if (held->model_scope || held->kind == APPLICATION_Q2_HELD_MATERIAL ||
            held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT) return true;
        qa_bytes source = held->resource ? qa_resource_bytes(held->resource) : (qa_bytes){0};
        if (held->kind == APPLICATION_Q2_HELD_MODEL && source.size >= 4 && !memcmp(source.data, "IDP3", 4)) return true;
    }
    return false;
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
        !qa_source_save_u64(io, &palette) || palette > UINT32_MAX) return false;
    options->family = (qa_scene_family)family; options->wrap = (qa_scene_wrap)wrap;
    options->filter = (qa_scene_filter)filter; options->usage = (qa_scene_image_usage)usage;
    options->palette_rgb = (qa_bytes){held->image_palette.data, held->image_palette.size};
    options->translation = (qa_bytes){held->image_translation.data, held->image_translation.size};
    if (io->direction == QA_SOURCE_SAVE_READ) held->image_palette_dependency = palette ? (size_t)(palette - 1) : SIZE_MAX;
    return true;
}

static bool alias_fields(qa_source_save_io *io, application_q2_held_resource *held)
{
    uint64_t logical = io->direction == QA_SOURCE_SAVE_WRITE && held->image_logical_dependency != SIZE_MAX ?
        (uint64_t)held->image_logical_dependency + 1 : 0;
    uint32_t rejection = (uint32_t)held->image_rejection, palette_error = (uint32_t)held->image_palette_error;
    if (!qa_source_save_owned_text(io, &held->image_request) || !held->image_request || !*held->image_request ||
        !qa_source_save_owned_text(io, &held->image_logical_path) ||
        !qa_source_save_u64(io, &logical) || logical > UINT32_MAX ||
        !qa_source_save_bool(io, &held->image_palette_attempted) ||
        !qa_source_save_u32(io, &rejection) || rejection > QA_ERROR_NOT_FOUND || rejection == QA_ERROR_MEMORY ||
        !qa_source_save_u32(io, &palette_error) || palette_error > QA_ERROR_NOT_FOUND || palette_error == QA_ERROR_MEMORY) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        held->image_logical_dependency = logical ? (size_t)(logical - 1) : SIZE_MAX;
    held->image_rejection = (qa_status)rejection; held->image_palette_error = (qa_status)palette_error;
    return true;
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
    if (!qa_source_save_u32(io, &kind) || kind > APPLICATION_Q2_HELD_IMAGE_RECEIPT ||
        !qa_source_save_bool(io, &held->missing) ||
        (held->missing && kind != APPLICATION_Q2_HELD_DEPENDENCY && kind != APPLICATION_Q2_HELD_EVENT &&
            kind != APPLICATION_Q2_HELD_ALIAS)) return false;
    held->kind = (application_q2_held_kind)kind;
    if (!qa_source_save_bool(io, &held->model_scope) ||
        (held->model_scope && (held->kind != APPLICATION_Q2_HELD_MODEL || held->missing))) return false;
    if (!qa_source_save_bool(io, &memory) || (memory && (held->kind != APPLICATION_Q2_HELD_MATERIAL || held->missing))) return false;
    if (!qa_source_save_owned_text(io, &held->instance) || !qa_source_save_owned_text(io, &held->path) || !qa_source_save_owned_text(io, &held->wire_path) ||
        !qa_source_save_u64(io, &held->serial) || !held->serial || !qa_source_save_u64(io, &held->authority) ||
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
        if ((!owner->archival && !provider) ||
            (held->missing || memory ? pool || resource : !actual) ||
            !held->path || !*held->path || !held->wire_path ||
            (strncmp(held->wire_path, "models/qa/", 10) && strncmp(held->wire_path, "players/qa/", 11) &&
                strncmp(held->wire_path, "sound/qa/", 9)) ||
            !qa_application_content_retain_view(graph, view, &held->view, io->error))
            return application_fail(io->error, QA_ERROR_FORMAT, "Q2 restored holder lost its real provider or immutable bytes");
        held->provider = !owner->archival && provider ? provider->owner : 0;
        held->identity = !owner->archival && provider ? provider->launch->identity : 0;
        held->resource = (qa_resource *)actual;
        if (held->resource) qa_resource_retain(held->resource);
    }
    qa_vfs_acquisition *opening = &held->opening;
    if (!held->missing && !memory && (!qa_source_save_u64(io, &opening->mount) || !qa_source_save_u64(io, &opening->resource_id) ||
        !qa_source_save_owned_text(io, &opening->path) || !qa_source_save_owned_text(io, &opening->lookup_path) ||
        !qa_source_save_owned_text(io, &opening->link_source) || !qa_source_save_owned_text(io, &opening->link_target) ||
        !qa_vfs_acquisition_opening_codec(io, held->view, opening) ||
        opening->resource_id != qa_resource_id(held->resource) ||
        !qa_vfs_acquisition_retained(held->view, opening, io->error))) return false;
    if (!qa_source_save_owned_text(io, &held->script_name) || !qa_source_save_owned_text(io, &held->sky_base) ||
        !qa_source_save_u64(io, &held->sky_group) ||
        !qa_source_save_u8(io, &held->sky_face) || held->sky_face > 6 ||
        !qa_source_save_count(io, &held->source_offset, INT32_MAX) ||
        !qa_source_save_count(io, &held->script_size, INT32_MAX) ||
        !qa_source_save_count(io, &held->name_offset, INT32_MAX) ||
        !qa_source_save_count(io, &held->name_size, INT32_MAX) ||
        !buffer_field(io, &held->catalog_bytes, memory ? INT32_MAX : 0)) return false;
    uint32_t event_kind = (uint32_t)held->event_kind;
    if (!qa_source_save_u32(io, &event_kind) || event_kind > QA_NATIVE_HOST_IMAGE ||
        !qa_source_save_bytes(io, held->event_key, sizeof(held->event_key)) ||
        !memchr(held->event_key, 0, sizeof(held->event_key)) ||
        !qa_source_save_u64(io, &held->event_custody)) return false;
    held->event_kind = (qa_native_host_resource_kind)event_kind;
    if (held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT &&
        !qa_source_save_u64(io, &held->receipt_source)) return false;
    if ((held->model_scope || held->kind == APPLICATION_Q2_HELD_IMAGE || held->kind == APPLICATION_Q2_HELD_MATERIAL ||
        held->kind == APPLICATION_Q2_HELD_ALIAS) &&
        (!image_fields(io, held) || (io->direction == QA_SOURCE_SAVE_READ &&
            held->image_palette_dependency != SIZE_MAX && held->image_palette_dependency >= owner->held_resource_count))) return false;
    if (held->model_scope && (!qa_source_save_owned_text(io, &held->model_scope_path) || !held->model_scope_path ||
        !buffer_field(io, &held->model_scope_bytes, INT32_MAX) || !held->model_scope_bytes.size)) return false;
    if (held->kind == APPLICATION_Q2_HELD_ALIAS && (!alias_fields(io, held) ||
        (io->direction == QA_SOURCE_SAVE_READ && held->image_logical_dependency != SIZE_MAX &&
            held->image_logical_dependency >= owner->held_resource_count))) return false;
    size_t maximum = held->kind == APPLICATION_Q2_HELD_MATERIAL || held->kind == APPLICATION_Q2_HELD_IMAGE ||
        held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT ? INT32_MAX :
        held->resource ? qa_resource_bytes(held->resource).size : 0;
    size_t references = held->kind == APPLICATION_Q2_HELD_IMAGE_RECEIPT ? 1 :
        held->kind == APPLICATION_Q2_HELD_MATERIAL ? INT32_MAX :
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
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (index && index - 1 >= owner->held_resource_count)
                return application_fail(io->error, QA_ERROR_FORMAT, "Q2 dependency reference exceeds its restored holders");
            held->dependencies[i] = index ? (size_t)(index - 1) : SIZE_MAX;
        }
    }
    return true;
}

bool application_network_q2_resources_capture(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, owner->app->session, error)) return false;
    size_t count = owner->held_resource_count;
    bool ok = qa_source_save_u64(&io, &owner->held_resource_serial) &&
        qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i)
        ok = holder_fields(owner, &io, &owner->held_resources[i]);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool application_network_q2_resources_capture_retained(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{ return application_network_q2_resources_capture(owner, out, error); }

bool application_network_q2_resources_restore(qa_application_network_q2 *owner, qa_bytes bytes, qa_error *error)
{
    if (owner->held_resource_count) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 holders restore requires an empty owner");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, owner->app->session, bytes, error)) return false;
    size_t count = 0;
    bool ok = qa_source_save_u64(&io, &owner->held_resource_serial) &&
        qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        application_q2_held_resource held = {0};
        ok = holder_fields(owner, &io, &held);
        for (size_t j = 0; ok && j < owner->held_resource_count; ++j)
            if (held.serial == owner->held_resources[j].serial || !strcmp(held.wire_path, owner->held_resources[j].wire_path))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored holders alias a qualified wire path");
        if (ok && (held.serial > owner->held_resource_serial || held.authority > owner->held_resource_serial ||
            held.sky_group > owner->held_resource_serial || held.receipt_source > owner->held_resource_serial))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 saved holder serial exceeds its actual namespace");
        if (ok) ok = held_append(owner, &held, error);
        held_free(&held);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); return ok;
}

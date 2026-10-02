#include "recipe_private.h"
#include "qa/launch_identity.h"
#include "../application/internal.h"
#include "qa/map_sidecars.h"
#include <inttypes.h>
#include <stdio.h>

static void word_write(qa_json_writer *w, uint64_t value)
{ char text[21]; snprintf(text, sizeof(text), "%" PRIu64, value); qa_json_writer_string(w, text); }
static void bytes_write(qa_json_writer *w, qa_bytes value)
{
    static const char hex[] = "0123456789abcdef";
    if (value.size > RECIPE_MAX_BYTES / 2 || (value.size && !value.data)) { w->failed = true; recipe_fail(&w->failure, "Recipe binary exceeds transfer bound"); return; }
    char *text = malloc(value.size * 2 + 1);
    if (!text) { w->failed = true; qa_error_set(&w->failure, QA_ERROR_MEMORY, 0, "Allocating recipe binary"); return; }
    for (size_t i = 0; i < value.size; ++i) { text[2 * i] = hex[value.data[i] >> 4]; text[2 * i + 1] = hex[value.data[i] & 15]; }
    text[value.size * 2] = 0; qa_json_writer_string(w, text); free(text);
}
static bool product_write(qa_json_writer *w, const qa_catalog *catalog, qa_product_id id, qa_error *error)
{
    const qa_product *product = qa_catalog_product(catalog, id);
    if (!product || !product->identity) return recipe_fail(error, "Transfer resource has no genuine content identity");
    qa_json_writer_string(w, product->identity); return !w->failed;
}
bool recipe_view_add(qa_executable_recipe *r, const char *owner, const qa_catalog *catalog,
    qa_vfs *files, bool owns, size_t *index, qa_error *error)
{
    if (!owner || !catalog || !files || r->view_count == RECIPE_MAX_RECORDS) return recipe_fail(error, "Invalid recipe content view");
    for (size_t i = 0; i < r->view_count; ++i) if (!strcmp(owner, r->views[i].owner)) return recipe_fail(error, "Repeated recipe content owner");
    recipe_view *next = realloc(r->views, (r->view_count + 1) * sizeof(*next));
    if (!next) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining recipe content views"); return false; }
    char *copy = qa_arena_alloc(&r->arena, strlen(owner) + 1, 1, error);
    if (!copy) { r->views = next; return false; }
    strcpy(copy, owner); r->views = next;
    if (index) *index = r->view_count;
    r->views[r->view_count++] = (recipe_view){.owner = copy, .files = files, .owns_files = owns, .catalog = catalog};
    qa_catalog_retain((qa_catalog *)catalog); return true;
}
static bool physical_mount(const recipe_view *view, qa_vfs_mount_info info,
    const qa_catalog_mount **physical, const qa_product **product, size_t *ordinal, qa_error *error)
{
    const char *path = qa_vfs_mount_path(view->files, info.id);
    const qa_vfs *authority = qa_catalog_files(view->catalog);
    for (size_t i = 0; path && i < qa_catalog_mount_count(view->catalog); ++i) {
        const qa_catalog_mount *candidate = qa_catalog_mount_at(view->catalog, i);
        if (strcmp(path, candidate->path) || candidate->format != info.format ||
            info.is_archive != (candidate->format != QA_ARCHIVE_AUTO)) continue;
        if (info.is_archive ? (!info.digest || !candidate->digest || !qa_sha256_equal(info.digest, candidate->digest)) :
            !qa_fs_root_same_object(qa_vfs_mount_root(view->files, info.id), qa_vfs_mount_root(authority, candidate->id))) continue;
        for (size_t j = 0; j < qa_catalog_count(view->catalog); ++j) {
            const qa_product *owner = qa_catalog_at(view->catalog, j); const qa_mount_id *ids = NULL; size_t count = 0;
            if (!qa_catalog_product_own_mounts(view->catalog, owner->id, &ids, &count)) continue;
            for (size_t k = 0; k < count; ++k) if (ids[k] == candidate->id) {
                size_t loose = 0;
                for (size_t n = 0; n < k; ++n) for (size_t m = 0; m < qa_catalog_mount_count(view->catalog); ++m) {
                    const qa_catalog_mount *prior = qa_catalog_mount_at(view->catalog, m);
                    if (prior->id == ids[n] && prior->format == QA_ARCHIVE_AUTO) ++loose;
                }
                *physical = candidate; *product = owner; *ordinal = info.is_archive ? 0 : loose; return true;
            }
        }
    }
    return recipe_fail(error, "Actual content mount has no installed catalog owner");
}
static bool mount_index(const qa_vfs *files, qa_mount_id id, size_t *out, qa_error *error)
{
    for (size_t i = 0; i < qa_vfs_mount_count(files); ++i) { qa_vfs_mount_info info;
        if (qa_vfs_mount_at(files, i, &info) && info.id == id) { *out = i; return true; }
    }
    return recipe_fail(error, "Recipe refers to a retired content mount");
}
static bool order_write(qa_json_writer *w, const qa_vfs *files, const qa_mount_id *order, size_t count, qa_error *error)
{
    qa_json_writer_array(w);
    for (size_t i = 0; i < count; ++i) { size_t index; if (!mount_index(files, order[i], &index, error)) return false; qa_json_writer_number(w, (double)index); }
    qa_json_writer_end(w); return !w->failed;
}
bool recipe_view_write(qa_json_writer *w, const recipe_view *view, qa_error *error)
{
    qa_json_writer_object(w); qa_json_writer_key(w, "owner"); qa_json_writer_string(w, view->owner);
    qa_json_writer_key(w, "mounts"); qa_json_writer_array(w);
    for (size_t i = 0; i < qa_vfs_mount_count(view->files); ++i) {
        qa_vfs_mount_info info; const qa_catalog_mount *actual; const qa_product *product; size_t ordinal;
        if (!qa_vfs_mount_at(view->files, i, &info) || !physical_mount(view, info, &actual, &product, &ordinal, error)) return false;
        qa_json_writer_array(w); qa_json_writer_string(w, product->identity); qa_json_writer_number(w, (double)ordinal);
        qa_json_writer_bool(w, info.is_archive); qa_json_writer_number(w, info.format); qa_json_writer_number(w, info.comparison);
        qa_json_writer_bool(w, info.user_overlay); qa_json_writer_bool(w, info.q3_demo);
        if (info.is_archive) recipe_digest_write(w, info.digest); else qa_json_writer_null(w);
        qa_json_writer_end(w); (void)actual;
    }
    qa_json_writer_end(w); qa_json_writer_key(w, "prefixes"); qa_json_writer_array(w);
    for (size_t i = 0; i < qa_vfs_prefix_count(view->files); ++i) {
        const char *prefix; const qa_mount_id *order; size_t count;
        if (!qa_vfs_prefix_at(view->files, i, &prefix, &order, &count)) return recipe_fail(error, "Recipe prefix disappeared");
        qa_json_writer_array(w); qa_json_writer_string(w, prefix);
        if (!order_write(w, view->files, order, count, error)) return false;
        qa_json_writer_end(w);
    }
    qa_json_writer_end(w); qa_json_writer_key(w, "links"); qa_json_writer_array(w);
    for (size_t i = 0; i < qa_vfs_link_count(view->files); ++i) {
        const char *source, *target; qa_mount_id mount; size_t index;
        if (!qa_vfs_link_at(view->files, i, &source, &mount, &target) || !mount_index(view->files, mount, &index, error)) return false;
        qa_json_writer_array(w); qa_json_writer_string(w, source); qa_json_writer_number(w, (double)index);
        qa_json_writer_string(w, target); qa_json_writer_end(w);
    }
    qa_json_writer_end(w); const qa_sha256_digest *pure; size_t count; bool demo;
    if (!qa_vfs_restrictions_read(view->files, &pure, &count, &demo)) return recipe_fail(error, "Recipe restriction owner is absent");
    qa_json_writer_key(w, "pure"); qa_json_writer_array(w);
    for (size_t i = 0; i < count; ++i) recipe_digest_write(w, &pure[i]);
    qa_json_writer_end(w); qa_json_writer_key(w, "demo"); qa_json_writer_bool(w, demo); qa_json_writer_end(w);
    return !w->failed;
}
static bool resource_same(const qa_resource *a, const qa_resource *b)
{
    if (!a || !b || qa_resource_bytes(a).size != qa_resource_bytes(b).size || !qa_sha256_equal(qa_resource_digest(a), qa_resource_digest(b))) return false;
    qa_sha256_digest x, y; size_t i, j;
    bool archive_a = qa_resource_archive_origin(a, &x, &i), archive_b = qa_resource_archive_origin(b, &y, &j);
    return archive_a == archive_b && (!archive_a || (i == j && qa_sha256_equal(&x, &y)));
}
bool recipe_resource_add_from(qa_executable_recipe *r, qa_product_id product, const char *path,
    const qa_resource *held, size_t preferred, size_t *out, qa_error *error)
{
    if (!held || !recipe_path(path, error)) return false;
    for (size_t i = 0; i < r->resource_count; ++i) if (r->resources[i]->value.product == product &&
        (preferred == SIZE_MAX || r->resources[i]->view == preferred) &&
        !strcmp(r->resources[i]->value.path, path) && resource_same(r->resources[i]->value.resource, held)) { *out = i; return true; }
    const qa_product *content = qa_catalog_product(r->catalog, product);
    if (!content) return recipe_fail(error, "Recipe resource product is absent");
    size_t view = SIZE_MAX; qa_resource *resource = NULL; qa_vfs_acquisition receipt = {0};
    if (preferred != SIZE_MAX) {
        if (preferred >= r->view_count || r->views[preferred].product != product) return recipe_fail(error, "Resource has no genuine selected source view");
        if (!qa_vfs_acquire_receipt(r->views[preferred].files, path, &resource, &receipt, error)) return false;
        if (!resource_same(resource, held)) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Selected source bytes changed during transfer"); }
        view = preferred;
    }
    for (size_t i = 1; view == SIZE_MAX && i < r->view_count; ++i) {
        recipe_view *candidate = &r->views[i];
        if (candidate->product != product) continue;
        for (size_t j = 0; j < qa_vfs_read_count(candidate->files); ++j) { qa_vfs_read_reference read;
            if (!qa_vfs_read_at(candidate->files, j, &read) || strcmp(read.path, path) || !resource_same(read.resource, held)) continue;
            if (!qa_vfs_acquire_receipt(candidate->files, path, &resource, &receipt, error)) return false;
            if (!resource_same(resource, held)) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Held source resource changed during transfer"); }
            view = i; break;
        }
        if (view != SIZE_MAX) break;
    }
    if (view == SIZE_MAX) {
        qa_vfs *files = NULL;
        for (size_t i = 1; i < r->view_count; ++i) if (r->views[i].product == product && !strncmp(r->views[i].owner, "content:", 8)) { view = i; files = r->views[i].files; break; }
        if (files) {
            if (!qa_vfs_acquire_receipt(files, path, &resource, &receipt, error)) return false;
            if (!resource_same(resource, held)) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Current transfer content differs from held Source bytes"); }
        }
    }
    if (view == SIZE_MAX) {
        qa_vfs *files = NULL;
        if (!qa_catalog_open(r->catalog, product, &files, error)) return false;
        size_t length = strlen(content->identity) + 9; char *name = malloc(length);
        if (!name) { qa_vfs_destroy(files); qa_error_set(error, QA_ERROR_MEMORY, 0, "Naming recipe content view"); return false; }
        snprintf(name, length, "content:%s", content->identity);
        bool added = recipe_view_add(r, name, r->catalog, files, true, &view, error); free(name);
        if (!added) { qa_vfs_destroy(files); return false; }
        r->views[view].product = product;
        if (!qa_vfs_acquire_receipt(files, path, &resource, &receipt, error)) return false;
        if (!resource_same(resource, held)) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Installed transfer content differs from held Source bytes"); }
    }
    if (r->resource_count == RECIPE_MAX_RECORDS) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Too many recipe resources"); }
    recipe_resource *entry = malloc(sizeof(*entry));
    if (!entry) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining recipe resource owner"); return false; }
    recipe_resource **next = realloc(r->resources, (r->resource_count + 1) * sizeof(*next));
    if (!next) { free(entry); qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining recipe resources"); return false; }
    r->resources = next; *out = r->resource_count;
    *entry = (recipe_resource){.value = {product, receipt.path, resource}, .view = view, .acquisition = receipt, .owns_resource = true};
    r->resources[r->resource_count++] = entry;
    return true;
}
bool recipe_resource_add(qa_executable_recipe *r, qa_product_id product, const char *path,
    const qa_resource *held, size_t *out, qa_error *error)
{ return recipe_resource_add_from(r, product, path, held, SIZE_MAX, out, error); }
static bool held_sidecar_add(qa_executable_recipe *r, qa_product_id product,
    size_t view, const qa_map_sidecar *held, size_t *out, qa_error *error)
{
    const qa_vfs_acquisition *source = held->acquisition;
    if (!source || !source->opening_present || view >= r->view_count ||
        !qa_vfs_acquisition_retained(r->views[view].files, source, error) ||
        qa_resource_pool_find(qa_vfs_resources(r->views[view].files), source->resource_id) != held->resource ||
        r->resource_count == RECIPE_MAX_RECORDS) return recipe_fail(error, "Source sidecar lost its actual retained opening");
    recipe_resource *entry = calloc(1, sizeof(*entry));
    if (!entry) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining historical sidecar transfer"); return false; }
    qa_vfs_acquisition *receipt = &entry->acquisition;
    receipt->mount = source->mount; receipt->resource_id = source->resource_id;
    const char *strings[] = {source->path, source->lookup_path, source->link_source, source->link_target, source->opening.prefix};
    char **targets[] = {&receipt->path, &receipt->lookup_path, &receipt->link_source, &receipt->link_target, (char **)&receipt->opening.prefix};
    bool okay = true;
    for (size_t i = 0; okay && i < 5; ++i) if (strings[i]) {
        *targets[i] = malloc(strlen(strings[i]) + 1);
        if (!*targets[i]) okay = false;
        else strcpy(*targets[i], strings[i]);
    }
    receipt->opening.rank = source->opening.rank; receipt->opening.user_overlay = source->opening.user_overlay;
    receipt->opening.order_count = source->opening.order_count; receipt->opening_present = true;
    if (okay && receipt->opening.order_count) {
        qa_mount_id *order = malloc(receipt->opening.order_count * sizeof(*order)); receipt->opening.order = order;
        if (!order) okay = false;
        else memcpy(order, source->opening.order, receipt->opening.order_count * sizeof(*order));
    }
    recipe_resource **next = okay ? realloc(r->resources, (r->resource_count + 1) * sizeof(*next)) : NULL;
    if (!next) { qa_vfs_acquisition_dispose(receipt); free(entry); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining historical sidecar transfer"); return false; }
    r->resources = next; *out = r->resource_count;
    entry->value = (qa_launch_resource){product, receipt->path, held->resource}; entry->view = view; entry->owns_resource = true;
    qa_resource_retain((qa_resource *)held->resource); r->resources[r->resource_count++] = entry; return true;
}
bool recipe_resource_write(qa_json_writer *w, const qa_executable_recipe *r, size_t index, qa_error *error)
{
    const recipe_resource *entry = r->resources[index]; const qa_vfs *files = r->views[entry->view].files;
    const qa_vfs_acquisition *opening = &entry->acquisition;
    size_t mount;
    if (!opening->opening_present || !qa_vfs_acquisition_retained(files, opening, error) || !mount_index(files, opening->mount, &mount, error)) return false;
    qa_sha256_digest archive; size_t ordinal = 0; bool archived = qa_resource_archive_origin(entry->value.resource, &archive, &ordinal);
    qa_json_writer_array(w);
    if (!product_write(w, r->catalog, entry->value.product, error)) return false;
    qa_json_writer_string(w, entry->value.path); qa_json_writer_number(w, (double)entry->view); qa_json_writer_number(w, (double)mount);
    qa_json_writer_string(w, qa_resource_path(entry->value.resource)); qa_json_writer_bool(w, archived); word_write(w, ordinal);
    qa_json_writer_string(w, opening->lookup_path); qa_json_writer_string(w, opening->link_source); qa_json_writer_string(w, opening->link_target);
    recipe_digest_write(w, qa_resource_digest(entry->value.resource)); word_write(w, qa_resource_bytes(entry->value.resource).size);
    qa_json_writer_number(w, (double)opening->opening.rank);
    if (!order_write(w, files, opening->opening.order, opening->opening.order_count, error)) return false;
    if (opening->opening.prefix) qa_json_writer_string(w, opening->opening.prefix); else qa_json_writer_null(w);
    qa_json_writer_bool(w, opening->opening.user_overlay); qa_json_writer_end(w); return !w->failed;
}
static bool optional_resource_write(qa_json_writer *w, qa_executable_recipe *r, const qa_launch_instance *instance,
    const qa_resource *resource, size_t *index, qa_error *error)
{
    if (!resource) { qa_json_writer_null(w); return true; }
    const qa_product *actual = qa_catalog_product(qa_launch_instance_catalog(instance), instance->selection.product);
    const qa_product *current = actual ? qa_catalog_find(r->catalog, actual->identity) : NULL;
    const char *path = resource == instance->artifact && instance->artifact_acquisition ? instance->artifact_acquisition->path : qa_resource_path(resource);
    const qa_catalog_mod *mod = *instance->selection.component ? qa_catalog_mod_find(qa_launch_instance_catalog(instance), instance->selection.component) : NULL;
    if (resource == instance->declaration && mod) path = mod->declaration_path;
    size_t view = SIZE_MAX;
    for (size_t i = 1; i < r->view_count; ++i) if (!strncmp(r->views[i].owner, "provider:", 9) && !strcmp(r->views[i].owner + 9, instance->selection.instance)) { view = i; break; }
    if (!current || view == SIZE_MAX || !recipe_resource_add_from(r, current->id, path, resource, view, index, error)) return false;
    qa_json_writer_number(w, (double)*index); return true;
}
static void clock_write(qa_json_writer *w, qa_clock_config clock)
{
    qa_json_writer_array(w); qa_json_writer_number(w, clock.kind); word_write(w, clock.initial_time_ns); word_write(w, clock.interval_ns);
    word_write(w, clock.minimum_frame_ns); word_write(w, clock.maximum_frame_ns); word_write(w, clock.initial_lead_ns);
    qa_json_writer_number(w, clock.maximum_steps); qa_json_writer_end(w);
}
static application_provider *source_provider(qa_application *app, const qa_launch_instance *instance)
{
    application_provider *provider = instance ? instance->state : NULL;
    return provider && provider->application == app && provider->constructed && !provider->close_pending &&
        provider->owner && provider->launch && provider->launch->storage == instance->storage ? provider : NULL;
}
static bool execution_write(qa_json_writer *w, qa_executable_recipe *r, const qa_launch_instance *instance,
    application_provider *provider, qa_error *error)
{
    const qa_launch_provider *s = &instance->selection; size_t index;
    qa_json_writer_array(w); qa_json_writer_string(w, s->instance);
    if (!product_write(w, qa_launch_instance_catalog(instance), s->product, error)) return false;
    qa_json_writer_number(w, s->runtime); qa_json_writer_string(w, s->implementation); qa_json_writer_string(w, s->artifact);
    qa_clock_config clock; uint64_t order;
    bool registered = qa_session_component_recipe(provider->application->session, provider->owner, &clock, &order);
    if (registered != provider->component_attached || (registered && (clock.kind != s->clock.kind ||
        clock.initial_time_ns != s->clock.initial_time_ns || clock.interval_ns != s->clock.interval_ns ||
        clock.minimum_frame_ns != s->clock.minimum_frame_ns || clock.maximum_frame_ns != s->clock.maximum_frame_ns ||
        clock.initial_lead_ns != s->clock.initial_lead_ns || clock.maximum_steps != s->clock.maximum_steps)))
        return recipe_fail(error, "Source clock descriptor differs from actual registered component");
    qa_json_writer_string(w, s->component); clock_write(w, s->clock); bytes_write(w, s->options); qa_json_writer_number(w, provider->owner);
    word_write(w, instance->roles); qa_json_writer_bool(w, registered);
    if (!optional_resource_write(w, r, instance, instance->artifact, &index, error) ||
        !optional_resource_write(w, r, instance, instance->declaration, &index, error)) return false;
    qa_json_writer_array(w);
    for (size_t i = 0; i < instance->interface_count; ++i) {
        const qa_product *actual = qa_catalog_product(qa_launch_instance_catalog(instance), instance->interfaces[i].product);
        const qa_product *current = actual ? qa_catalog_find(r->catalog, actual->identity) : NULL; size_t view = SIZE_MAX;
        for (size_t j = 1; j < r->view_count; ++j) if (!strncmp(r->views[j].owner, "provider:", 9) && !strcmp(r->views[j].owner + 9, s->instance)) { view = j; break; }
        if (!current || view == SIZE_MAX || !recipe_resource_add_from(r, current->id, instance->interfaces[i].path, instance->interfaces[i].resource, view, &index, error)) return false;
        qa_json_writer_number(w, (double)index);
    }
    qa_json_writer_end(w); qa_json_writer_array(w);
    for (size_t i = 0; i < instance->behavior_count; ++i) {
        const qa_catalog_weapon_behavior *b = instance->behaviors[i]; qa_json_writer_array(w);
        if (!product_write(w, qa_launch_instance_catalog(instance), b->product, error)) return false;
        qa_json_writer_string(w, b->id); qa_json_writer_number(w, b->runtime); qa_json_writer_number(w, b->role);
        qa_json_writer_string(w, b->artifact_path); recipe_digest_write(w, &b->artifact_digest);
        qa_json_writer_string(w, b->declaration_path ? b->declaration_path : ""); recipe_digest_write(w, &b->declaration_digest);
        bytes_write(w, b->entry); qa_json_writer_end(w);
    }
    qa_json_writer_end(w); qa_json_writer_end(w); return !w->failed;
}
static bool ordering_write(qa_json_writer *w, qa_application *app, const qa_launch_snapshot *launch, qa_error *error)
{
    size_t count = qa_launch_snapshot_instance_count(launch);
    uint64_t *orders = calloc(count ? count : 1, sizeof(*orders));
    bool *registered = calloc(count ? count : 1, sizeof(*registered));
    if (!orders || !registered) { free(orders); free(registered); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual Source invocation order"); return false; }
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        application_provider *provider = source_provider(app, qa_launch_snapshot_instance(launch, i)); qa_clock_config clock;
        if (!provider) { ok = recipe_fail(error, "Selected Source provider lost its genuine owner"); break; }
        registered[i] = qa_session_component_recipe(app->session, provider->owner, &clock, &orders[i]);
    }
    qa_json_writer_object(w); qa_json_writer_key(w, "mixed"); qa_json_writer_bool(w, qa_session_mixed_order(app->session));
    qa_json_writer_key(w, "providers"); qa_json_writer_array(w);
    for (size_t n = 0; ok && n < count; ++n) {
        size_t selected = SIZE_MAX;
        for (size_t i = 0; i < count; ++i) if (registered[i] && (selected == SIZE_MAX || orders[i] < orders[selected])) selected = i;
        if (selected == SIZE_MAX) break;
        qa_json_writer_string(w, qa_launch_snapshot_instance(launch, selected)->selection.instance); registered[selected] = false;
    }
    qa_json_writer_end(w); qa_json_writer_key(w, "entityOrder"); qa_json_writer_string(w, "source-slot-order");
    qa_json_writer_key(w, "ties"); qa_json_writer_string(w, "provider-entity-invocation"); qa_json_writer_end(w);
    free(orders); free(registered); return ok && !w->failed;
}
static bool sidecars_write(qa_json_writer *w, const qa_catalog *catalog, const qa_recipe_sidecar *rows, size_t count, qa_error *error)
{
    qa_json_writer_array(w);
    for (size_t i = 0; i < count; ++i) {
        const qa_recipe_sidecar *s = &rows[i]; qa_json_writer_object(w); qa_json_writer_key(w, "content");
        if (!product_write(w, catalog, s->product, error)) return false;
        qa_json_writer_key(w, "path"); qa_json_writer_string(w, s->path); qa_json_writer_key(w, "resource");
        if (!s->resource) qa_json_writer_null(w);
        else {
            qa_json_writer_object(w); qa_json_writer_key(w, "content"); if (!product_write(w, catalog, s->product, error)) return false;
            qa_json_writer_key(w, "path"); qa_json_writer_string(w, s->path);
            char digest[72] = "sha256:"; qa_sha256_hex(qa_resource_digest(s->resource), digest + 7);
            qa_json_writer_key(w, "digest"); qa_json_writer_string(w, digest);
            qa_json_writer_key(w, "byteLength"); qa_json_writer_number(w, (double)qa_resource_bytes(s->resource).size); qa_json_writer_end(w);
        }
        qa_json_writer_end(w);
    }
    qa_json_writer_end(w); return !w->failed;
}
bool qa_application_unified_offer(qa_application *app, uint32_t epoch, const char *mode,
    uint32_t max_clients, const qa_recipe_sidecar *sidecars, size_t sidecar_count,
    qa_unified_document **out, qa_error *error)
{
    if (!app || !out || *out || !epoch || !mode || (strcmp(mode, "singleplayer") && strcmp(mode, "coop") && strcmp(mode, "deathmatch")) ||
        !max_clients || max_clients > 256 || sidecar_count > RECIPE_MAX_RECORDS || (sidecar_count && !sidecars)) return recipe_fail(error, "Invalid genuine composition offer request");
    const qa_launch_snapshot *launch = qa_application_launch(app); qa_application_map_view map;
    if (app->state != QA_APPLICATION_RUNNING || app->destroy_requested || app->operation != APPLICATION_IDLE ||
        app->frame_preparing || app->q3_round_active || app->q3_world_restart || !qa_session_safe(app->session) ||
        qa_session_faulted(app->session) || !qa_world_idle(app->world) || !launch || !qa_application_map_read(app, &map))
        return recipe_fail(error, "Composition offer requires its idle published Source world");
    const qa_map_sidecars *admission = qa_application_map_sidecars(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(launch);
    const qa_product *admitted_product = admission ? qa_catalog_product(qa_map_sidecars_catalog(admission), qa_map_sidecars_product(admission)) : NULL;
    const qa_product *current_product = admitted_product ? qa_catalog_find(qa_launch_snapshot_catalog(launch), admitted_product->identity) : NULL;
    size_t observed_count = qa_map_sidecars_count(admission);
    if (!admission || !current_product || current_product->id != choices->world.geometry ||
        (sidecar_count && sidecar_count != observed_count)) return recipe_fail(error, "Source offer lacks its genuine published map sidecars");
    qa_recipe_sidecar *observed = calloc(observed_count ? observed_count : 1, sizeof(*observed));
    if (!observed) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Publishing historical sidecar inventory"); return false; }
    for (size_t i = 0; i < observed_count; ++i) {
        const qa_map_sidecar *row = qa_map_sidecars_at(admission, i);
        observed[i] = (qa_recipe_sidecar){current_product->id, row->path, row->resource};
        if (sidecar_count && (sidecars[i].product != observed[i].product || !sidecars[i].path ||
            strcmp(sidecars[i].path, row->path) || sidecars[i].resource != row->resource)) {
            free(observed); return recipe_fail(error, "Caller sidecars differ from the actual map admission");
        }
    }
    sidecars = observed; sidecar_count = observed_count;
    qa_map_sidecars_retain((qa_map_sidecars *)admission);
    qa_launch_snapshot_retain(launch); uint64_t generation = qa_application_configuration_generation(app);
    qa_executable_recipe r = {.catalog = qa_launch_snapshot_catalog(launch), .pool = qa_application_resources(app)};
    qa_buffer identity = {0}; qa_json_document *json = NULL; qa_json_writer w = {0}, offer = {0}; qa_buffer body = {0}, encoded = {0}; qa_unified_composition canonical = {0};
    bool ok = qa_launch_identity_encode(launch, qa_session_actors(qa_application_session(app)), &identity, error) &&
        qa_json_parse((qa_bytes){identity.data, identity.size}, &json, error);
    qa_vfs *main = ok ? qa_vfs_clone(qa_launch_snapshot_mounts(launch), error) : NULL;
    if (ok) { ok = main && recipe_view_add(&r, "main", r.catalog, main, true, NULL, error); if (!ok) qa_vfs_destroy(main); }
    for (size_t i = 0; ok && i < qa_launch_snapshot_instance_count(launch); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(launch, i); qa_vfs *files = qa_vfs_clone(instance->content, error);
        size_t length = strlen(instance->selection.instance) + 10; char *owner = malloc(length);
        if (!owner) { qa_vfs_destroy(files); qa_error_set(error, QA_ERROR_MEMORY, 0, "Naming actual provider content owner"); ok = false; break; }
        snprintf(owner, length, "provider:%s", instance->selection.instance);
        size_t index; ok = files && recipe_view_add(&r, owner, qa_launch_instance_catalog(instance), files, true, &index, error); free(owner);
        if (!ok) qa_vfs_destroy(files);
        else { const qa_product *actual = qa_catalog_product(qa_launch_instance_catalog(instance), instance->selection.product);
            const qa_product *current = actual ? qa_catalog_find(r.catalog, actual->identity) : NULL;
            if (!current) ok = recipe_fail(error, "Retained provider content is absent from current composition catalog");
            else r.views[index].product = current->id; }
    }
    size_t sidecar_view = SIZE_MAX;
    if (ok) {
        qa_vfs *files = (qa_vfs *)qa_map_sidecars_view(admission);
        size_t length = strlen(admitted_product->identity) + 14;
        char *owner = malloc(length);
        if (owner) snprintf(owner, length, "map-sidecars:%s", admitted_product->identity);
        ok = owner && files && recipe_view_add(&r, owner, qa_map_sidecars_catalog(admission), files, false, &sidecar_view, error);
        free(owner);
        if (ok) r.views[sidecar_view].product = current_product->id;
    }
    if (ok) ok = recipe_resource_add(&r, choices->world.geometry, choices->world.map, map.resource, &r.map_index, error);
    qa_json_writer_object(&w); qa_json_writer_key(&w, "schemaVersion"); qa_json_writer_number(&w, 1);
    qa_json_writer_key(&w, "recipe"); qa_json_writer_object(&w);
    qa_json_writer_key(&w, "schemaVersion"); qa_json_writer_number(&w, 1); qa_json_writer_key(&w, "kind"); qa_json_writer_string(&w, "anthology:executable");
    qa_json_writer_key(&w, "choices"); qa_json_writer_object(&w);
    static const char *fields[] = {"world", "providers", "bindings", "mods", "modes", "equipment", "seats", "loadout", "monsters", "behaviors"};
    for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i) { qa_json_writer_key(&w, fields[i]); ok = recipe_copy_json(&w, json, qa_json_get(json, qa_json_root(json), fields[i]), error); }
    qa_json_writer_end(&w); qa_json_writer_key(&w, "execution"); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < qa_launch_snapshot_instance_count(launch); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(launch, i); application_provider *provider = source_provider(app, instance);
        ok = provider ? execution_write(&w, &r, instance, provider, error) : recipe_fail(error, "Selected Source provider has no genuine constructed owner");
    }
    qa_json_writer_end(&w); qa_json_writer_key(&w, "map"); qa_json_writer_array(&w); qa_json_writer_number(&w, (double)r.map_index); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < sidecar_count; ++i) {
        if (!recipe_path(sidecars[i].path, error)) { ok = false; break; }
        for (size_t j = 0; j < i; ++j) if (sidecars[j].product == sidecars[i].product && !strcmp(sidecars[j].path, sidecars[i].path)) ok = recipe_fail(error, "Duplicate actual sidecar observation");
        qa_json_writer_array(&w); if (ok) ok = product_write(&w, r.catalog, sidecars[i].product, error);
        qa_json_writer_string(&w, sidecars[i].path);
        if (sidecars[i].resource) { size_t index; if (ok) ok = held_sidecar_add(&r, sidecars[i].product, sidecar_view, qa_map_sidecars_at(admission, i), &index, error); if (ok) qa_json_writer_number(&w, (double)index); }
        else qa_json_writer_null(&w);
        qa_json_writer_end(&w);
    }
    qa_json_writer_end(&w); qa_json_writer_end(&w);
    qa_json_writer_key(&w, "ordering"); if (ok) ok = ordering_write(&w, app, launch, error);
    for (size_t i = 0; ok && i < qa_launch_snapshot_resource_count(launch); ++i) { const qa_launch_resource *resource = qa_launch_snapshot_resource(launch, i); size_t index;
        ok = recipe_resource_add(&r, resource->product, resource->path, resource->resource, &index, error); }
    qa_json_writer_key(&w, "views"); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < r.view_count; ++i) ok = recipe_view_write(&w, &r.views[i], error);
    qa_json_writer_end(&w); qa_json_writer_key(&w, "resources"); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < r.resource_count; ++i) ok = recipe_resource_write(&w, &r, i, error);
    qa_json_writer_end(&w); qa_json_writer_end(&w);
    qa_json_writer_key(&w, "snapshotSchema"); qa_json_writer_string(&w, "qts:snapshot-v10"); qa_json_writer_key(&w, "actorConfigurations"); qa_json_writer_array(&w); qa_json_writer_end(&w);
    qa_json_writer_key(&w, "sidecars"); if (ok) ok = sidecars_write(&w, r.catalog, sidecars, sidecar_count, error); qa_json_writer_end(&w);
    if (ok) ok = qa_json_writer_finish(&w, &body, error) && qa_unified_composition_create((qa_bytes){body.data, body.size}, &canonical, error);
    if (ok) {
        qa_json_document *composition = NULL; ok = qa_json_parse((qa_bytes){canonical.canonical.data, canonical.canonical.size}, &composition, error);
        qa_json_writer_object(&offer); qa_json_writer_key(&offer, "schema"); qa_json_writer_string(&offer, "qts-control1"); qa_json_writer_key(&offer, "value"); qa_json_writer_object(&offer);
        qa_json_writer_key(&offer, "kind"); qa_json_writer_string(&offer, "offer"); qa_json_writer_key(&offer, "epoch"); qa_json_writer_number(&offer, epoch);
        qa_json_writer_key(&offer, "composition"); qa_json_writer_object(&offer); qa_json_writer_key(&offer, "composition");
        if (ok) ok = recipe_copy_json(&offer, composition, qa_json_root(composition), error);
        qa_json_writer_key(&offer, "digest"); char digest[72] = "sha256:"; qa_sha256_hex(&canonical.digest, digest + 7); qa_json_writer_string(&offer, digest); qa_json_writer_end(&offer);
        qa_json_writer_key(&offer, "mode"); qa_json_writer_string(&offer, mode); qa_json_writer_key(&offer, "maxClients"); qa_json_writer_number(&offer, max_clients); qa_json_writer_end(&offer); qa_json_writer_end(&offer);
        if (ok) ok = qa_json_writer_finish(&offer, &encoded, error);
        qa_json_destroy(composition);
    }
    qa_application_map_view current;
    if (ok && (qa_application_launch(app) != launch || qa_application_configuration_generation(app) != generation ||
        !qa_application_map_read(app, &current) || current.revision != map.revision || current.resource != map.resource ||
        qa_application_map_sidecars(app) != admission)) ok = recipe_fail(error, "Source composition changed during transfer preparation");
    if (ok) ok = qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, (qa_bytes){encoded.data, encoded.size}, out, error);
    for (size_t i = 0; i < r.resource_count; ++i) { qa_resource_release((qa_resource *)r.resources[i]->value.resource); qa_vfs_acquisition_dispose(&r.resources[i]->acquisition); free(r.resources[i]); }
    for (size_t i = 0; i < r.view_count; ++i) { if (r.views[i].owns_files) qa_vfs_destroy(r.views[i].files); qa_catalog_release((qa_catalog *)r.views[i].catalog); }
    free(r.resources); free(r.views); qa_arena_destroy(&r.arena); qa_json_destroy(json); qa_buffer_free(&identity);
    qa_json_writer_destroy(&w); qa_json_writer_destroy(&offer); qa_buffer_free(&body); qa_buffer_free(&encoded); qa_unified_composition_free(&canonical); qa_launch_snapshot_release(launch);
    qa_map_sidecars_release((qa_map_sidecars *)admission); free(observed);
    return ok;
}

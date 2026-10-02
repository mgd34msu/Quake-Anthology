#include "recipe_private.h"
#include "qa/persistence_content.h"
#include <math.h>
#include <stdio.h>

static bool array(recipe_reader *r, qa_json_id id, size_t *count)
{
    *count = qa_json_size(r->json, id);
    return (qa_json_type(r->json, id) == QA_JSON_ARRAY && *count <= RECIPE_MAX_RECORDS) || recipe_fail(r->error, "Invalid executable recipe array");
}
static bool unsigned_field(const qa_json_document *json, qa_json_id id, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(json, id, &value, error) || value > UINT32_MAX) return recipe_fail(error, "Recipe field exceeds uint32");
    *out = (uint32_t)value; return true;
}
static bool view_read(qa_executable_recipe *r, const qa_json_document *json, qa_json_id root, qa_error *error)
{
    if (qa_json_type(json, root) != QA_JSON_OBJECT || qa_json_size(json, root) != 6) return recipe_fail(error, "Invalid recipe content view object");
    recipe_reader reader = {.json = json, .recipe = r, .error = error};
    qa_json_id mounts = qa_json_get(json, root, "mounts"); size_t count;
    if (!array(&reader, mounts, &count)) return false;
    qa_json_id owner_id = qa_json_get(json, root, "owner");
    qa_buffer name = {0}; if (!qa_json_string(json, owner_id, &name, error)) return false;
    if (!name.size || memchr(name.data, 0, name.size)) { qa_buffer_free(&name); return recipe_fail(error, "Invalid recipe view owner"); }
    for (size_t i = 0; i < r->view_count; ++i) if (!strcmp(r->views[i].owner, (const char *)name.data)) { qa_buffer_free(&name); return recipe_fail(error, "Duplicate recipe view owner"); }
    qa_vfs *files = qa_vfs_create(r->pool, error); size_t view;
    if (!files) { qa_buffer_free(&name); return false; }
    bool ok = recipe_view_add(r, (const char *)name.data, r->catalog, files, true, &view, error); qa_buffer_free(&name);
    if (!ok) { qa_vfs_destroy(files); return false; }
    if (!strncmp(r->views[view].owner, "content:", 8)) {
        const qa_product *product = qa_catalog_find(r->catalog, r->views[view].owner + 8);
        if (!product || strcmp(product->identity, r->views[view].owner + 8)) return recipe_fail(error, "Content view has no actual installed product owner");
        r->views[view].product = product->id;
    } else if (strcmp(r->views[view].owner, "main") && (strncmp(r->views[view].owner, "provider:", 9) || !r->views[view].owner[9]))
        return recipe_fail(error, "Unknown canonical recipe content owner");
    qa_mount_id *ids = calloc(count ? count : 1, sizeof(*ids));
    qa_mount_id *physical_used = calloc(count ? count : 1, sizeof(*physical_used));
    if (!ids || !physical_used) { free(ids); free(physical_used); qa_error_set(error, QA_ERROR_MEMORY, 0, "Resolving recipe mount order"); return false; }
    for (size_t i = 0; ok && i < count; ++i) {
        if (!recipe_record(&reader, qa_json_at(json, mounts, i), 8)) { ok = false; break; }
        qa_product_id product = recipe_product(&reader); uint32_t ordinal = recipe_unsigned(&reader);
        bool archived = recipe_boolean(&reader); qa_archive_kind format = recipe_unsigned(&reader);
        qa_archive_comparison comparison = recipe_unsigned(&reader); bool overlay = recipe_boolean(&reader), demo = recipe_boolean(&reader);
        qa_sha256_digest digest; qa_json_id digest_id = qa_json_at(json, reader.array, reader.next);
        if (archived) recipe_digest_read(&reader, &digest); else { recipe_take(&reader); if (qa_json_type(json, digest_id) != QA_JSON_NULL) reader.failed = true; }
        const qa_mount_id *owned; size_t own_count; const qa_catalog_mount *selected = NULL;
        if (reader.failed || !product || (unsigned)comparison > QA_ARCHIVE_CASE_INSENSITIVE ||
            !qa_catalog_product_own_mounts(r->catalog, product, &owned, &own_count)) { ok = recipe_fail(error, "Recipe mount lacks a valid actual product authority"); break; }
        size_t loose = 0;
        for (size_t j = 0; j < own_count; ++j) {
            const qa_catalog_mount *actual = NULL;
            for (size_t k = 0; k < qa_catalog_mount_count(r->catalog); ++k) { const qa_catalog_mount *candidate = qa_catalog_mount_at(r->catalog, k); if (candidate->id == owned[j]) { actual = candidate; break; } }
            size_t loose_ordinal = loose;
            if (actual && actual->format == QA_ARCHIVE_AUTO) ++loose;
            if (!actual || actual->format != format || archived != (actual->format != QA_ARCHIVE_AUTO) ||
                (archived ? (ordinal || !actual->digest || !qa_sha256_equal(actual->digest, &digest)) : loose_ordinal != ordinal)) continue;
            bool used = false; for (size_t k = 0; k < i; ++k) used |= physical_used[k] == actual->id;
            if (!used) { selected = actual; break; }
        }
        if (!selected) { ok = recipe_fail(error, "Offered mount is not present in the installed catalog"); break; }
        physical_used[i] = selected->id;
        ok = qa_vfs_mount_retained(files, qa_catalog_files(r->catalog), selected->id, comparison, false, &ids[i], error) &&
            qa_vfs_set_mount_q3_demo(files, ids[i], demo, error) && qa_vfs_set_user_overlay(files, ids[i], overlay, error);
    }
    qa_json_id pure = qa_json_get(json, root, "pure"); size_t pure_count = 0; bool demo = false;
    qa_sha256_digest *digests = NULL;
    if (ok) ok = array(&reader, pure, &pure_count) && qa_json_bool(json, qa_json_get(json, root, "demo"), &demo, error);
    if (ok) {
        digests = calloc(pure_count ? pure_count : 1, sizeof(*digests));
        if (!digests) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Resolving recipe archive restrictions"); ok = false; }
    }
    for (size_t i = 0; ok && i < pure_count; ++i) { reader.array = pure; reader.next = i; ok = recipe_digest_read(&reader, &digests[i]); }
    if (ok) ok = qa_vfs_set_restrictions(files, digests, pure_count, demo, error) && qa_vfs_set_order(files, ids, count, error);
    free(digests);
    qa_json_id prefixes = qa_json_get(json, root, "prefixes"); size_t prefix_count = 0;
    if (ok) ok = array(&reader, prefixes, &prefix_count);
    for (size_t i = 0; ok && i < prefix_count; ++i) {
        if (!recipe_record(&reader, qa_json_at(json, prefixes, i), 2)) { ok = false; break; }
        const char *prefix = recipe_text(&reader); qa_json_id order = recipe_take(&reader); size_t order_count;
        if (reader.failed || !recipe_path(prefix, error) || !array(&reader, order, &order_count) || order_count != count) { ok = recipe_fail(error, "Recipe prefix order is incomplete"); break; }
        for (size_t j = 0; j < qa_vfs_prefix_count(files); ++j) { const char *existing; const qa_mount_id *prior; size_t prior_count;
            if (qa_vfs_prefix_at(files, j, &existing, &prior, &prior_count) && !strcmp(existing, prefix)) ok = recipe_fail(error, "Duplicate recipe prefix order"); }
        qa_mount_id *ordered = calloc(count ? count : 1, sizeof(*ordered)); bool *used = calloc(count ? count : 1, sizeof(*used));
        if (!ordered || !used) { free(ordered); free(used); qa_error_set(error, QA_ERROR_MEMORY, 0, "Resolving recipe prefix order"); ok = false; break; }
        for (size_t j = 0; ok && j < count; ++j) { uint32_t index;
            ok = unsigned_field(json, qa_json_at(json, order, j), &index, error) && index < count && !used[index];
            if (ok) { used[index] = true; ordered[j] = ids[index]; } else recipe_fail(error, "Recipe prefix repeats or omits a mount"); }
        if (ok) ok = qa_vfs_set_prefix_order(files, prefix, ordered, count, error);
        free(ordered); free(used);
    }
    qa_json_id links = qa_json_get(json, root, "links"); size_t link_count = 0;
    if (ok) ok = array(&reader, links, &link_count);
    for (size_t i = 0; ok && i < link_count; ++i) {
        if (!recipe_record(&reader, qa_json_at(json, links, i), 3)) { ok = false; break; }
        const char *source = recipe_text(&reader); uint32_t index = recipe_unsigned(&reader); const char *target = recipe_text(&reader);
        for (size_t j = 0; j < qa_vfs_link_count(files); ++j) {
            const char *existing, *destination; qa_mount_id mount;
            if (qa_vfs_link_at(files, j, &existing, &mount, &destination) && !strcmp(existing, source)) ok = recipe_fail(error, "Duplicate recipe resource link");
        }
        if (ok) ok = !reader.failed && index < count && qa_vfs_set_link(files, source, ids[index], target, error);
    }
    free(ids); free(physical_used); return ok;
}
static bool resource_read(qa_executable_recipe *r, const qa_json_document *json, qa_json_id id, qa_error *error)
{
    recipe_reader reader = {.json = json, .recipe = r, .error = error};
    if (!recipe_record(&reader, id, 16)) return false;
    qa_product_id product = recipe_product(&reader); const char *path = recipe_text(&reader);
    uint32_t view = recipe_unsigned(&reader), mount_index = recipe_unsigned(&reader); const char *member = recipe_text(&reader);
    bool archived = recipe_boolean(&reader); uint64_t ordinal = recipe_word(&reader); const char *lookup = recipe_text(&reader);
    const char *source = recipe_text(&reader), *target = recipe_text(&reader); qa_sha256_digest digest;
    recipe_digest_read(&reader, &digest); uint64_t length = recipe_word(&reader); int32_t rank = recipe_signed(&reader);
    qa_json_id order = recipe_take(&reader); const char *prefix = recipe_optional_text(&reader); bool overlay = recipe_boolean(&reader);
    if (reader.failed || !product || view >= r->view_count || !recipe_path(path, error) || !recipe_path(member, error) || !recipe_path(lookup, error) ||
        (prefix && !recipe_path(prefix, error)) || (!archived && ordinal)) return recipe_fail(error, "Invalid complete recipe resource reference");
    qa_vfs *files = r->views[view].files; qa_vfs_mount_info mount;
    if (!qa_vfs_mount_at(files, mount_index, &mount) || mount.is_archive != archived) return recipe_fail(error, "Recipe resource mount is absent");
    qa_resource *resource = NULL; qa_vfs_acquisition receipt = {0};
    if (!qa_vfs_acquire_receipt(files, path, &resource, &receipt, error)) return false;
    qa_sha256_digest archive_digest; size_t actual_ordinal = 0; bool actual_archive = qa_resource_archive_origin(resource, &archive_digest, &actual_ordinal);
    bool ok = receipt.mount == mount.id && !strcmp(qa_resource_path(resource), member) && actual_archive == archived && actual_ordinal == ordinal &&
        !strcmp(receipt.lookup_path, lookup) && !strcmp(receipt.link_source, source) && !strcmp(receipt.link_target, target) &&
        qa_resource_bytes(resource).size == length && qa_sha256_equal(qa_resource_digest(resource), &digest);
    qa_vfs_read_reference read = {0}; bool found = false;
    for (size_t i = 0; ok && i < qa_vfs_read_count(files); ++i) if (qa_vfs_read_at(files, i, &read) && read.mount == receipt.mount &&
        qa_resource_id(read.resource) == receipt.resource_id && !strcmp(read.path, path) && !strcmp(read.lookup_path, lookup) &&
        !strcmp(read.link_source, source) && !strcmp(read.link_target, target)) { found = true; break; }
    size_t order_count;
    if (ok) ok = found && array(&reader, order, &order_count) && order_count == read.opening.order_count &&
        rank == read.opening.rank && overlay == read.opening.user_overlay &&
        (prefix ? read.opening.prefix && !strcmp(prefix, read.opening.prefix) : !read.opening.prefix);
    for (size_t i = 0; ok && i < order_count; ++i) { uint32_t index; qa_vfs_mount_info ordered;
        ok = unsigned_field(json, qa_json_at(json, order, i), &index, error) && qa_vfs_mount_at(files, index, &ordered) && ordered.id == read.opening.order[i]; }
    if (!ok) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return recipe_fail(error, "Local resource bytes or opening authority differ from offered composition"); }
    recipe_resource *entry = malloc(sizeof(*entry));
    if (!entry) { qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining resolved resource owner"); return false; }
    recipe_resource **next = realloc(r->resources, (r->resource_count + 1) * sizeof(*next));
    if (!next) { free(entry); qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining resolved recipe bytes"); return false; }
    r->resources = next; *entry = (recipe_resource){.value = {product, path, resource}, .view = view, .acquisition = receipt, .owns_resource = true};
    r->resources[r->resource_count++] = entry;
    return true;
}
static const qa_resource *resource_index(recipe_reader *r, qa_json_id id, qa_product_id product, const char *path, bool nullable)
{
    if (nullable && qa_json_type(r->json, id) == QA_JSON_NULL) return NULL;
    uint32_t index;
    if (!unsigned_field(r->json, id, &index, r->error) || index >= r->recipe->resource_count) { r->failed = true; return NULL; }
    const qa_launch_resource *resource = &r->recipe->resources[index]->value;
    if (resource->product != product || (path && strcmp(resource->path, path))) { r->failed = true; recipe_fail(r->error, "Provider resource differs from its real selected content"); return NULL; }
    return resource->resource;
}
static qa_clock_config clock_read(recipe_reader *r)
{
    recipe_reader reader = *r; qa_clock_config clock = {0};
    if (!recipe_record(&reader, recipe_take(r), 7)) { r->failed = true; return clock; }
    clock.kind = recipe_unsigned(&reader); clock.initial_time_ns = recipe_word(&reader); clock.interval_ns = recipe_word(&reader);
    clock.minimum_frame_ns = recipe_word(&reader); clock.maximum_frame_ns = recipe_word(&reader); clock.initial_lead_ns = recipe_word(&reader);
    clock.maximum_steps = recipe_unsigned(&reader); r->failed |= reader.failed; return clock;
}
static bool selection_equal(const qa_launch_provider *a, const qa_launch_provider *b)
{
    return a->product == b->product && a->runtime == b->runtime && !strcmp(a->instance, b->instance) &&
        !strcmp(a->implementation, b->implementation) && !strcmp(a->artifact, b->artifact) && !strcmp(a->component, b->component) &&
        a->options.size == b->options.size && (!a->options.size || !memcmp(a->options.data, b->options.data, a->options.size)) &&
        a->clock.kind == b->clock.kind && a->clock.initial_time_ns == b->clock.initial_time_ns && a->clock.interval_ns == b->clock.interval_ns &&
        a->clock.minimum_frame_ns == b->clock.minimum_frame_ns && a->clock.maximum_frame_ns == b->clock.maximum_frame_ns &&
        a->clock.initial_lead_ns == b->clock.initial_lead_ns && a->clock.maximum_steps == b->clock.maximum_steps;
}
static uint64_t selected_roles(const qa_recipe_choices *c, const char *name)
{
    uint64_t roles = 0;
    for (size_t i = 0; i < c->binding_count; ++i) if (!strcmp(c->bindings[i].instance, name)) roles |= QA_ROLE_BIT(c->bindings[i].role);
    for (size_t i = 0; i < c->mode_count; ++i) if (!strcmp(c->modes[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_MODE);
    for (size_t i = 0; i < c->equipment_count; ++i) {
        const qa_recipe_equipment *e = &c->equipment[i];
        if (!strcmp(e->instance, name) || (e->selection.grapple != QA_GRAPPLE_DISABLED && !strcmp(e->grapple_source, name)) ||
            (e->selection.grenades.enabled && !strcmp(e->grenade_source, name))) roles |= QA_ROLE_BIT(QA_ROLE_EQUIPMENT);
    }
    for (size_t i = 0; i < c->monster_count; ++i) if (!c->monsters[i].map_defined && !strcmp(c->monsters[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_MONSTERS);
    for (size_t i = 0; i < c->behavior_count; ++i) if (c->behaviors[i].enabled && !strcmp(c->behaviors[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_TRAJECTORY);
    return roles;
}
static bool interfaces_check(const qa_recipe_provider *instance, qa_error *error)
{
    static const char *paths[] = {"quakec-compatibility.json", "native-compatibility.json", "qvm-compatibility.json",
        "qvm-items.json", "weapon-behaviors.json", "native-weapon-behaviors.json", "qvm-weapon-behaviors.json"};
    if (instance->selection.runtime == QA_PROGRAM_BUILTIN) return !instance->interface_count || recipe_fail(error, "Builtin source has no external program interface inventory");
    size_t matched = 0;
    for (size_t i = 0; i < sizeof(paths) / sizeof(*paths); ++i) {
        const qa_resource *offered = NULL;
        for (size_t j = 0; j < instance->interface_count; ++j) if (!strcmp(paths[i], instance->interfaces[j].path)) offered = instance->interfaces[j].resource;
        qa_resource *actual = NULL; qa_error reason = {0};
        bool exists = qa_vfs_acquire(instance->content, paths[i], &actual, NULL, &reason);
        if ((!exists && reason.code != QA_ERROR_NOT_FOUND) || exists != (offered != NULL) ||
            (exists && (qa_resource_bytes(actual).size != qa_resource_bytes(offered).size || !qa_sha256_equal(qa_resource_digest(actual), qa_resource_digest(offered))))) {
            qa_resource_release(actual); if (!exists && reason.code != QA_ERROR_NOT_FOUND && error) *error = reason;
            return recipe_fail(error, "Actual provider interface inventory differs from offer");
        }
        matched += exists; qa_resource_release(actual);
    }
    return matched == instance->interface_count || recipe_fail(error, "Executable interface inventory contains an unknown document");
}
static bool execution_read(qa_executable_recipe *r, const qa_json_document *json, qa_json_id id, size_t index, qa_error *error)
{
    recipe_reader reader = {.json = json, .recipe = r, .error = error};
    if (!recipe_record(&reader, id, 15)) return false;
    qa_recipe_provider *instance = &r->providers[index]; qa_launch_provider *s = &instance->selection;
    s->instance = recipe_text(&reader); s->product = recipe_product(&reader); s->runtime = recipe_unsigned(&reader);
    s->implementation = recipe_text(&reader); s->artifact = recipe_text(&reader); s->component = recipe_text(&reader);
    s->clock = clock_read(&reader); s->options = recipe_binary(&reader); instance->source_owner = recipe_unsigned(&reader);
    instance->roles = recipe_word(&reader); instance->registered = recipe_boolean(&reader);
    qa_json_id artifact = recipe_take(&reader), declaration = recipe_take(&reader), interfaces = recipe_take(&reader), behaviors = recipe_take(&reader);
    if (reader.failed || !instance->source_owner || (instance->roles >> QA_ROLE_COUNT)) return recipe_fail(error, "Executable descriptor lacks genuine source ownership or roles");
    for (size_t i = 0; i < index; ++i) if (!strcmp(r->providers[i].selection.instance, s->instance) || r->providers[i].source_owner == instance->source_owner) return recipe_fail(error, "Duplicate executable instance or source owner");
    const qa_launch_provider *selected = NULL; qa_launch_provider mod_selection = {0};
    for (size_t i = 0; i < r->choices.provider_count; ++i) if (!strcmp(r->choices.providers[i].instance, s->instance)) selected = &r->choices.providers[i];
    if (!selected) for (size_t i = 0; i < r->choices.mod_count; ++i) {
        const qa_launch_mod_selection *choice = &r->choices.mods[i]; if (!choice->enabled || strcmp(choice->instance, s->instance)) continue;
        const qa_catalog_mod *mod = qa_catalog_mod_find(r->catalog, choice->component); const qa_product *product = mod ? qa_catalog_product(r->catalog, mod->product) : NULL;
        if (!mod || mod->unavailable || !product) return recipe_fail(error, "Offered addition is not installed");
        qa_clock_kind kind = product->family == QA_GAME_Q3 ? QA_CLOCK_Q3 : product->family == QA_GAME_Q2 ?
            (product->edition == QA_EDITION_RERELEASE ? QA_CLOCK_Q2_RERELEASE : QA_CLOCK_Q2_CLASSIC) :
            (product->edition == QA_EDITION_QUAKEWORLD ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE);
        mod_selection = (qa_launch_provider){.instance = choice->instance, .product = mod->product, .runtime = mod->runtime,
            .implementation = mod->key, .artifact = mod->program_path, .component = mod->key, .clock = qa_clock_defaults(kind)};
        selected = &mod_selection; break;
    }
    if (!selected || !selection_equal(s, selected) || instance->roles != (selected == &mod_selection ? 0 : selected_roles(&r->choices, s->instance)))
        return recipe_fail(error, "Executable descriptor differs from exact launch selection or roles");
    for (size_t i = 0; i < r->view_count; ++i) if (!strncmp(r->views[i].owner, "provider:", 9) && !strcmp(r->views[i].owner + 9, s->instance)) {
        instance->content = r->views[i].files; r->views[i].product = s->product;
    }
    if (!instance->content) return recipe_fail(error, "Executable descriptor has no actual local content view");
    instance->artifact = resource_index(&reader, artifact, s->product, s->artifact, s->runtime == QA_PROGRAM_BUILTIN);
    const qa_catalog_mod *mod = *s->component ? qa_catalog_mod_find(r->catalog, s->component) : NULL;
    instance->declaration = resource_index(&reader, declaration, s->product, mod ? mod->declaration_path : NULL, true);
    if (!mod && instance->declaration) return recipe_fail(error, "Executable has an undeclared component declaration");
    if (s->runtime == QA_PROGRAM_BUILTIN ? instance->artifact != NULL : instance->artifact == NULL) return recipe_fail(error, "Executable kind and held artifact disagree");
    if (mod && (!instance->artifact || !instance->declaration || !qa_sha256_equal(qa_resource_digest(instance->artifact), &mod->program_digest) ||
        !qa_sha256_equal(qa_resource_digest(instance->declaration), &mod->declaration_digest))) return recipe_fail(error, "Local authored component identity differs from offer");
    size_t interface_count, behavior_count;
    if (!array(&reader, interfaces, &interface_count) || !array(&reader, behaviors, &behavior_count)) return false;
    qa_launch_resource *interface_rows = interface_count ? qa_arena_alloc(&r->arena, interface_count * sizeof(*interface_rows), _Alignof(qa_launch_resource), error) : NULL;
    const qa_catalog_weapon_behavior **behavior_rows = behavior_count ? qa_arena_alloc(&r->arena, behavior_count * sizeof(*behavior_rows), _Alignof(qa_catalog_weapon_behavior *), error) : NULL;
    if ((interface_count && !interface_rows) || (behavior_count && !behavior_rows)) return false;
    instance->interfaces = interface_rows; instance->interface_count = interface_count; instance->behaviors = behavior_rows; instance->behavior_count = behavior_count;
    for (size_t i = 0; i < interface_count; ++i) {
        uint32_t resource; if (!unsigned_field(json, qa_json_at(json, interfaces, i), &resource, error) || resource >= r->resource_count || r->resources[resource]->value.product != s->product ||
            r->views[r->resources[resource]->view].files != instance->content) return recipe_fail(error, "Invalid executable interface resource");
        interface_rows[i] = r->resources[resource]->value;
        for (size_t j = 0; j < i; ++j) if (!strcmp(interface_rows[j].path, interface_rows[i].path)) return recipe_fail(error, "Duplicate executable interface");
    }
    for (size_t i = 0; i < behavior_count; ++i) {
        if (!recipe_record(&reader, qa_json_at(json, behaviors, i), 9)) return false;
        qa_product_id product = recipe_product(&reader); const char *name = recipe_text(&reader);
        qa_program_kind runtime = recipe_unsigned(&reader); qa_builtin_projectile_role role = recipe_unsigned(&reader);
        const char *path = recipe_text(&reader); qa_sha256_digest artifact_digest, declaration_digest;
        recipe_digest_read(&reader, &artifact_digest); const char *declaration_path = recipe_text(&reader); recipe_digest_read(&reader, &declaration_digest); qa_bytes entry = recipe_binary(&reader);
        const qa_catalog_weapon_behavior *actual = qa_catalog_weapon_behavior_find(r->catalog, product, name);
        if (reader.failed || !actual || actual->unavailable || product != s->product || actual->runtime != runtime || actual->role != role ||
            strcmp(actual->artifact_path, path) || strcmp(actual->declaration_path ? actual->declaration_path : "", declaration_path) ||
            !qa_sha256_equal(&actual->artifact_digest, &artifact_digest) || !qa_sha256_equal(&actual->declaration_digest, &declaration_digest) ||
            actual->entry.size != entry.size || (entry.size && memcmp(actual->entry.data, entry.data, entry.size))) return recipe_fail(error, "Installed trajectory declaration differs from offered executable");
        for (size_t j = 0; j < i; ++j) if (behavior_rows[j] == actual) return recipe_fail(error, "Repeated executable trajectory descriptor");
        if (!instance->artifact || !qa_sha256_equal(qa_resource_digest(instance->artifact), &artifact_digest)) return recipe_fail(error, "Trajectory does not belong to the held executable");
        if (*declaration_path) {
            qa_resource *declaration_resource = NULL;
            if (!qa_vfs_acquire(instance->content, declaration_path, &declaration_resource, NULL, error)) return false;
            bool same = qa_sha256_equal(qa_resource_digest(declaration_resource), &declaration_digest); qa_resource_release(declaration_resource);
            if (!same) return recipe_fail(error, "Trajectory declaration bytes differ from offered identity");
        }
        behavior_rows[i] = actual;
    }
    for (size_t i = 0; i < r->choices.behavior_count; ++i) {
        const qa_recipe_behavior *choice = &r->choices.behaviors[i];
        if (!choice->enabled || strcmp(choice->instance, s->instance)) continue;
        bool found = false; for (size_t j = 0; j < behavior_count; ++j) found |= !strcmp(choice->behavior, behavior_rows[j]->id);
        if (!found) return recipe_fail(error, "Executable omits a selected trajectory");
    }
    for (size_t j = 0; j < behavior_count; ++j) {
        bool selected_behavior = false;
        for (size_t i = 0; i < r->choices.behavior_count; ++i) selected_behavior |= r->choices.behaviors[i].enabled &&
            !strcmp(r->choices.behaviors[i].instance, s->instance) && !strcmp(r->choices.behaviors[i].behavior, behavior_rows[j]->id);
        if (!selected_behavior) return recipe_fail(error, "Executable contains an unselected trajectory");
    }
    if (instance->artifact) for (size_t i = 0; i < r->resource_count; ++i) if (r->resources[i]->value.resource == instance->artifact &&
        r->resources[i]->view < r->view_count && r->views[r->resources[i]->view].files == instance->content) { instance->artifact_acquisition = &r->resources[i]->acquisition; break; }
    if (instance->artifact && !instance->artifact_acquisition) return recipe_fail(error, "Executable artifact lost its genuine source content acquisition");
    return !reader.failed && interfaces_check(instance, error);
}

static bool ordering_read(qa_executable_recipe *r, const qa_json_document *json, qa_json_id id, qa_error *error)
{
    if (qa_json_type(json, id) != QA_JSON_OBJECT || qa_json_size(json, id) != 4 ||
        !qa_json_bool(json, qa_json_get(json, id, "mixed"), &r->mixed_order, error) ||
        !qa_json_string_equal(json, qa_json_get(json, id, "entityOrder"), "source-slot-order") ||
        !qa_json_string_equal(json, qa_json_get(json, id, "ties"), "provider-entity-invocation")) return recipe_fail(error, "Unsupported Source invocation ordering contract");
    recipe_reader reader = {.json = json, .recipe = r, .error = error}; qa_json_id providers = qa_json_get(json, id, "providers"); size_t count;
    if (!array(&reader, providers, &count)) return false;
    size_t expected = 0; for (size_t i = 0; i < r->provider_count; ++i) expected += r->providers[i].registered;
    if (count != expected) return recipe_fail(error, "Source ordering omits a registered executable");
    r->order = calloc(count ? count : 1, sizeof(*r->order));
    if (!r->order) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual Source provider order"); return false; }
    r->order_count = count;
    for (size_t i = 0; i < count; ++i) {
        reader.array = providers; reader.next = i; const char *name = recipe_text(&reader); size_t selected = SIZE_MAX;
        for (size_t j = 0; j < r->provider_count; ++j) if (r->providers[j].registered && !strcmp(r->providers[j].selection.instance, name)) selected = j;
        if (reader.failed || selected == SIZE_MAX) return recipe_fail(error, "Source ordering names an unregistered executable");
        for (size_t j = 0; j < i; ++j) if (r->order[j] == selected) return recipe_fail(error, "Source ordering repeats an executable");
        r->order[i] = selected;
    }
    return true;
}

static bool map_read(qa_executable_recipe *r, const qa_json_document *json, qa_json_id id, qa_error *error)
{
    recipe_reader reader = {.json = json, .recipe = r, .error = error};
    if (!recipe_record(&reader, id, 2)) return false;
    r->map_index = recipe_unsigned(&reader); qa_json_id sidecars = recipe_take(&reader); size_t count;
    if (reader.failed || r->map_index >= r->resource_count || !array(&reader, sidecars, &count)) return recipe_fail(error, "Recipe map resource is absent");
    const qa_launch_resource *map = &r->resources[r->map_index]->value;
    if (map->product != r->choices.world.geometry || strcmp(map->path, r->choices.world.map)) return recipe_fail(error, "Offered map differs from its authored geometry choice");
    r->sidecars = calloc(count ? count : 1, sizeof(*r->sidecars));
    if (!r->sidecars) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual map sidecar observations"); return false; }
    r->sidecar_count = count;
    for (size_t i = 0; i < count; ++i) {
        if (!recipe_record(&reader, qa_json_at(json, sidecars, i), 3)) return false;
        qa_recipe_sidecar *s = &r->sidecars[i]; s->product = recipe_product(&reader); s->path = recipe_text(&reader); qa_json_id resource = recipe_take(&reader);
        if (reader.failed || !s->product || !recipe_path(s->path, error)) return recipe_fail(error, "Invalid actual sidecar identity");
        for (size_t j = 0; j < i; ++j) if (r->sidecars[j].product == s->product && !strcmp(r->sidecars[j].path, s->path)) return recipe_fail(error, "Duplicate map sidecar");
        if (qa_json_type(json, resource) == QA_JSON_NULL) {
            qa_vfs *files = NULL; qa_resource *observed = NULL; qa_error reason = {0};
            if (!qa_catalog_open(r->catalog, s->product, &files, error)) return false;
            bool exists = qa_vfs_acquire(files, s->path, &observed, NULL, &reason); qa_resource_release(observed); qa_vfs_destroy(files);
            if (exists || reason.code != QA_ERROR_NOT_FOUND) { if (!exists && error) *error = reason; return recipe_fail(error, "Actual local sidecar observation differs from Source miss"); }
        } else { s->resource = resource_index(&reader, resource, s->product, s->path, false); if (reader.failed) return false; }
    }
    if (!qa_bsp_open(qa_resource_bytes(map->resource), &r->bsp, error) || !qa_collision_create(&r->bsp, &r->geometry, error)) return false;
    if (r->bsp.family == QA_BSP_Q2) for (size_t i = 0; i < qa_bsp_record_count(&r->bsp, QA_BSP_TEXINFO); ++i) {
        qa_bsp_texinfo texture; if (!qa_bsp_read_texinfo(&r->bsp, i, &texture, error)) return false;
        if (texture.name.size > 1024 || memchr(texture.name.data, 0, texture.name.size)) return recipe_fail(error, "Invalid Q2 sidecar texture path");
        char path[1040]; int size = snprintf(path, sizeof(path), "textures/%.*s.mat", (int)texture.name.size, (const char *)texture.name.data);
        if (size < 0 || (size_t)size >= sizeof(path)) return recipe_fail(error, "Q2 sidecar path is too long");
        for (size_t j = 0; j < r->sidecar_count; ++j) if (r->sidecars[j].product == map->product && !strcmp(r->sidecars[j].path, path) && r->sidecars[j].resource)
            if (i > UINT32_MAX || !qa_collision_set_surface_material(r->geometry, (uint32_t)i, qa_resource_bytes(r->sidecars[j].resource), error)) return false;
    }
    return true;
}
static bool local_identity(qa_executable_recipe *r, const qa_json_document *json, qa_json_id composition, qa_error *error)
{
    qa_json_writer w = {0}; qa_buffer buffer = {0}; qa_unified_composition actual = {0};
    qa_json_writer_object(&w); qa_json_writer_key(&w, "schemaVersion"); qa_json_writer_number(&w, 1);
    qa_json_writer_key(&w, "recipe"); qa_json_writer_object(&w);
    qa_json_id recipe = qa_json_get(json, composition, "recipe");
    static const char *fields[] = {"schemaVersion", "kind", "choices", "execution", "map", "ordering"}; bool ok = true;
    for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i) { qa_json_writer_key(&w, fields[i]); ok = recipe_copy_json(&w, json, qa_json_get(json, recipe, fields[i]), error); }
    qa_json_writer_key(&w, "views"); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < r->view_count; ++i) ok = recipe_view_write(&w, &r->views[i], error);
    qa_json_writer_end(&w); qa_json_writer_key(&w, "resources"); qa_json_writer_array(&w);
    for (size_t i = 0; ok && i < r->resource_count; ++i) ok = recipe_resource_write(&w, r, i, error);
    qa_json_writer_end(&w); qa_json_writer_end(&w);
    qa_json_writer_key(&w, "snapshotSchema"); qa_json_writer_string(&w, "qts:snapshot-v10"); qa_json_writer_key(&w, "actorConfigurations"); qa_json_writer_array(&w); qa_json_writer_end(&w);
    qa_json_writer_key(&w, "sidecars"); if (ok) ok = recipe_copy_json(&w, json, qa_json_get(json, composition, "sidecars"), error); qa_json_writer_end(&w);
    if (ok) ok = qa_json_writer_finish(&w, &buffer, error) && qa_unified_composition_create((qa_bytes){buffer.data, buffer.size}, &actual, error);
    if (ok && !qa_sha256_equal(&actual.digest, &r->digest)) ok = recipe_fail(error, "Rebound local composition differs from offered canonical identity");
    qa_json_writer_destroy(&w); qa_buffer_free(&buffer); qa_unified_composition_free(&actual); return ok;
}
static bool sidecars_check(const qa_executable_recipe *r, const qa_json_document *json, qa_json_id id, qa_error *error)
{
    if (qa_json_type(json, id) != QA_JSON_ARRAY || qa_json_size(json, id) != r->sidecar_count) return recipe_fail(error, "Composition sidecar observations differ from recipe");
    for (size_t i = 0; i < r->sidecar_count; ++i) {
        const qa_recipe_sidecar *s = &r->sidecars[i]; const qa_product *product = qa_catalog_product(r->catalog, s->product);
        qa_json_id row = qa_json_at(json, id, i), key = qa_json_get(json, row, "resource");
        if (!product || qa_json_type(json, row) != QA_JSON_OBJECT || qa_json_size(json, row) != 3 ||
            !qa_json_string_equal(json, qa_json_get(json, row, "content"), product->identity) ||
            !qa_json_string_equal(json, qa_json_get(json, row, "path"), s->path)) return recipe_fail(error, "Composition sidecar content or path differs from actual recipe observation");
        if (!s->resource) {
            if (qa_json_type(json, key) != QA_JSON_NULL) return recipe_fail(error, "Composition changes actual sidecar miss");
        } else {
            char digest[72] = "sha256:"; qa_sha256_hex(qa_resource_digest(s->resource), digest + 7); uint64_t size;
            if (qa_json_type(json, key) != QA_JSON_OBJECT || qa_json_size(json, key) != 4 ||
                !qa_json_string_equal(json, qa_json_get(json, key, "content"), product->identity) ||
                !qa_json_string_equal(json, qa_json_get(json, key, "path"), s->path) ||
                !qa_json_string_equal(json, qa_json_get(json, key, "digest"), digest) ||
                !qa_json_u64(json, qa_json_get(json, key, "byteLength"), &size, error) || size != qa_resource_bytes(s->resource).size)
                return recipe_fail(error, "Composition changes actual held sidecar bytes");
        }
    }
    return true;
}
bool qa_executable_recipe_prepare(const qa_unified_document *offer, qa_catalog *catalog,
    qa_resource_pool *pool, qa_executable_recipe **out, qa_error *error)
{
    if (!offer || !catalog || !pool || !out || *out || qa_catalog_resources(catalog) != pool) return recipe_fail(error, "Remote composition requires its genuine installed catalog and pool");
    const qa_json_document *json = qa_unified_document_json(offer); qa_json_id root = qa_unified_document_root(offer);
    qa_json_id value = qa_json_get(json, root, "value"), identity = qa_json_get(json, value, "composition"), composition = qa_json_get(json, identity, "composition");
    qa_json_id recipe = qa_json_get(json, composition, "recipe"); uint32_t version;
    if (qa_json_type(json, root) != QA_JSON_OBJECT || qa_json_size(json, root) != 2 || !qa_json_string_equal(json, qa_json_get(json, root, "schema"), "qts-control1") ||
        qa_json_type(json, value) != QA_JSON_OBJECT || qa_json_size(json, value) != 5 || !qa_json_string_equal(json, qa_json_get(json, value, "kind"), "offer") ||
        qa_json_type(json, identity) != QA_JSON_OBJECT || qa_json_size(json, identity) != 2 ||
        qa_json_type(json, composition) != QA_JSON_OBJECT || qa_json_size(json, composition) != 5 ||
        !unsigned_field(json, qa_json_get(json, composition, "schemaVersion"), &version, error) || version != 1 ||
        !qa_json_string_equal(json, qa_json_get(json, composition, "snapshotSchema"), "qts:snapshot-v10") ||
        qa_json_type(json, qa_json_get(json, composition, "actorConfigurations")) != QA_JSON_ARRAY || qa_json_size(json, qa_json_get(json, composition, "actorConfigurations")) != 0 ||
        qa_json_type(json, recipe) != QA_JSON_OBJECT || qa_json_size(json, recipe) != 8 ||
        !unsigned_field(json, qa_json_get(json, recipe, "schemaVersion"), &version, error) || version != 1 ||
        !qa_json_string_equal(json, qa_json_get(json, recipe, "kind"), "anthology:executable")) return recipe_fail(error, "Unsupported canonical executable composition envelope");
    qa_executable_recipe *r = calloc(1, sizeof(*r));
    if (!r) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing remote executable composition"); return false; }
    r->catalog = catalog; qa_catalog_retain(catalog); r->pool = pool; qa_resource_pool_retain(pool); r->catalog_generation = qa_catalog_generation(catalog);
    bool ok = unsigned_field(json, qa_json_get(json, value, "epoch"), &r->epoch, error) && r->epoch &&
        unsigned_field(json, qa_json_get(json, value, "maxClients"), &r->max_clients, error) && r->max_clients && r->max_clients <= 256;
    qa_buffer mode = {0}, digest = {0};
    if (ok) ok = qa_json_string(json, qa_json_get(json, value, "mode"), &mode, error) && !memchr(mode.data, 0, mode.size) &&
        (!strcmp((const char *)mode.data, "singleplayer") || !strcmp((const char *)mode.data, "coop") || !strcmp((const char *)mode.data, "deathmatch"));
    if (ok) { char *copy = qa_arena_alloc(&r->arena, mode.size + 1, 1, error); ok = copy != NULL; if (copy) { memcpy(copy, mode.data, mode.size + 1); r->mode = copy; } }
    if (ok) ok = qa_json_string(json, qa_json_get(json, identity, "digest"), &digest, error) && digest.size == 71 && !memcmp(digest.data, "sha256:", 7) && qa_sha256_parse((const char *)digest.data + 7, &r->digest, error);
    qa_buffer_free(&mode); qa_buffer_free(&digest);
    qa_unified_composition canonical = {0};
    if (ok) ok = qa_unified_composition_create(qa_json_source(json, composition), &canonical, error) && qa_sha256_equal(&canonical.digest, &r->digest);
    if (ok) { r->composition = canonical.canonical; canonical.canonical = (qa_buffer){0}; }
    qa_unified_composition_free(&canonical);
    if (ok) ok = recipe_choices_read(r, json, qa_json_get(json, recipe, "choices"), error);
    recipe_reader reader = {.json = json, .recipe = r, .error = error}; size_t count;
    qa_json_id views = qa_json_get(json, recipe, "views"); if (ok) ok = array(&reader, views, &count) && count > 0;
    for (size_t i = 0; ok && i < count; ++i) ok = view_read(r, json, qa_json_at(json, views, i), error);
    if (ok && strcmp(r->views[0].owner, "main")) ok = recipe_fail(error, "Composition has no canonical main mount plan");
    qa_json_id resources = qa_json_get(json, recipe, "resources"); if (ok) ok = array(&reader, resources, &count);
    for (size_t i = 0; ok && i < count; ++i) ok = resource_read(r, json, qa_json_at(json, resources, i), error);
    qa_json_id execution = qa_json_get(json, recipe, "execution"); if (ok) ok = array(&reader, execution, &count);
    size_t expected = r->choices.provider_count; for (size_t i = 0; i < r->choices.mod_count; ++i) expected += r->choices.mods[i].enabled;
    if (ok && count != expected) ok = recipe_fail(error, "Composition omits selected executable instances");
    if (ok) { r->provider_count = count; r->providers = calloc(count ? count : 1, sizeof(*r->providers));
        if (!r->providers) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining real executable descriptors"); ok = false; } }
    for (size_t i = 0; ok && i < count; ++i) ok = execution_read(r, json, qa_json_at(json, execution, i), i, error);
    if (ok) ok = ordering_read(r, json, qa_json_get(json, recipe, "ordering"), error) &&
        map_read(r, json, qa_json_get(json, recipe, "map"), error) &&
        sidecars_check(r, json, qa_json_get(json, composition, "sidecars"), error) && local_identity(r, json, composition, error);
    for (size_t i = 0; ok && i < r->view_count; ++i) {
        r->views[i].admitted_policy = qa_vfs_clone(r->views[i].files, error); ok = r->views[i].admitted_policy != NULL;
    }
    if (ok) { *out = r; return true; }
    if (error && error->code == QA_OK) recipe_fail(error, "Invalid complete remote executable composition");
    qa_executable_recipe_close(r, NULL); return false;
}
bool qa_executable_recipe_current(const qa_executable_recipe *r, const qa_catalog *catalog)
{
    if (!r || catalog != r->catalog || qa_catalog_generation(catalog) != r->catalog_generation || qa_catalog_resources(catalog) != r->pool) return false;
    for (size_t i = 0; i < r->view_count; ++i) if (!qa_vfs_lookup_equal(r->views[i].files, r->views[i].admitted_policy)) return false;
    for (size_t i = 0; i < r->resource_count; ++i) { const recipe_resource *entry = r->resources[i];
        if (entry->view >= r->view_count || qa_resource_pool_find(r->pool, qa_resource_id(entry->value.resource)) != entry->value.resource ||
            !qa_vfs_acquisition_retained(r->views[entry->view].files, &entry->acquisition, NULL)) return false; }
    return true;
}
bool qa_executable_recipe_close(qa_executable_recipe *r, qa_error *error)
{
    if (!r) return true;
    if (r->visiting) return recipe_fail(error, "Recipe content is borrowed by an active graph visit");
    qa_collision_destroy(r->geometry);
    for (size_t i = 0; i < r->resource_count; ++i) { if (r->resources[i]->owns_resource) qa_resource_release((qa_resource *)r->resources[i]->value.resource); qa_vfs_acquisition_dispose(&r->resources[i]->acquisition); free(r->resources[i]); }
    for (size_t i = 0; i < r->view_count; ++i) { if (r->views[i].owns_files) qa_vfs_destroy(r->views[i].files); qa_vfs_destroy(r->views[i].admitted_policy); qa_catalog_release((qa_catalog *)r->views[i].catalog); }
    free(r->resources); free(r->views); free(r->providers); free(r->order); free(r->sidecars); qa_buffer_free(&r->composition); qa_arena_destroy(&r->arena);
    qa_catalog_release(r->catalog); qa_resource_pool_destroy(r->pool); free(r); return true;
}
bool qa_executable_recipe_content_visit(const qa_executable_recipe *recipe, const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!recipe || !visitor || !visitor->pool || !visitor->catalog || !visitor->view || recipe->visiting ||
        !qa_executable_recipe_current(recipe, recipe->catalog)) return recipe_fail(error, "Recipe graph requires its current unentered actual content owner");
    qa_executable_recipe *r = (qa_executable_recipe *)recipe; r->visiting = true;
    bool ok = visitor->pool(visitor->context, r->pool, error) && visitor->catalog(visitor->context, r->catalog, error);
    for (size_t i = 0; ok && i < r->view_count; ++i) ok = visitor->view(visitor->context, r->views[i].files, error) &&
        visitor->view(visitor->context, r->views[i].admitted_policy, error);
    if (ok) ok = qa_executable_recipe_current(r, r->catalog) || recipe_fail(error, "Recipe content changed during actual graph enumeration");
    r->visiting = false; return ok;
}
const qa_sha256_digest *qa_executable_recipe_digest(const qa_executable_recipe *r) { return r ? &r->digest : NULL; }
uint32_t qa_executable_recipe_epoch(const qa_executable_recipe *r) { return r ? r->epoch : 0; }
const char *qa_executable_recipe_mode(const qa_executable_recipe *r) { return r ? r->mode : NULL; }
uint32_t qa_executable_recipe_max_clients(const qa_executable_recipe *r) { return r ? r->max_clients : 0; }
qa_catalog *qa_executable_recipe_catalog(const qa_executable_recipe *r) { return r ? r->catalog : NULL; }
qa_vfs *qa_executable_recipe_mounts(const qa_executable_recipe *r) { return r && r->view_count ? r->views[0].files : NULL; }
qa_resource *qa_executable_recipe_map(const qa_executable_recipe *r) { return r && r->map_index < r->resource_count ? (qa_resource *)r->resources[r->map_index]->value.resource : NULL; }
qa_collision_geometry *qa_executable_recipe_geometry(const qa_executable_recipe *r) { return r ? r->geometry : NULL; }
const qa_recipe_choices *qa_executable_recipe_choices(const qa_executable_recipe *r) { return r ? &r->choices : NULL; }
size_t qa_executable_recipe_provider_count(const qa_executable_recipe *r) { return r ? r->provider_count : 0; }
const qa_recipe_provider *qa_executable_recipe_provider(const qa_executable_recipe *r, size_t index) { return r && index < r->provider_count ? &r->providers[index] : NULL; }
qa_actor_owner qa_executable_recipe_source_owner(const qa_executable_recipe *r, size_t index) { return r && index < r->provider_count ? r->providers[index].source_owner : 0; }
bool qa_executable_recipe_mixed_order(const qa_executable_recipe *r) { return r && r->mixed_order; }
size_t qa_executable_recipe_order_count(const qa_executable_recipe *r) { return r ? r->order_count : 0; }
const qa_recipe_provider *qa_executable_recipe_order_at(const qa_executable_recipe *r, size_t index)
{ return r && index < r->order_count ? &r->providers[r->order[index]] : NULL; }
size_t qa_executable_recipe_resource_count(const qa_executable_recipe *r) { return r ? r->resource_count : 0; }
bool qa_executable_recipe_resource(const qa_executable_recipe *r, size_t index, qa_launch_resource *out, qa_vfs **files, const qa_vfs_acquisition **receipt)
{
    if (!r || index >= r->resource_count || !out || !files || !receipt) return false;
    const recipe_resource *entry = r->resources[index]; *out = entry->value; *files = r->views[entry->view].files; *receipt = &entry->acquisition; return true;
}
bool qa_executable_recipe_find_resource(const qa_executable_recipe *r, const char *identity, const char *path,
    const qa_sha256_digest *digest, uint64_t length, qa_launch_resource *out, qa_vfs **files, const qa_vfs_acquisition **receipt)
{
    if (!r || !identity || !path || !digest) return false;
    const qa_product *product = qa_catalog_find(r->catalog, identity); if (!product || strcmp(product->identity, identity)) return false;
    for (size_t i = 0; i < r->resource_count; ++i) { const qa_launch_resource *resource = &r->resources[i]->value;
        if (resource->product == product->id && !strcmp(resource->path, path) && qa_resource_bytes(resource->resource).size == length && qa_sha256_equal(qa_resource_digest(resource->resource), digest))
            return qa_executable_recipe_resource(r, i, out, files, receipt); }
    return false;
}
bool qa_executable_recipe_content(qa_executable_recipe *r, const char *identity, qa_vfs **files,
    const qa_product **actual_product, qa_error *error)
{
    if (!r || r->visiting || !identity || !files || !actual_product || !qa_executable_recipe_current(r, r->catalog))
        return recipe_fail(error, "Remote content requires its admitted current recipe");
    const qa_product *product = qa_catalog_find(r->catalog, identity);
    if (!product || strcmp(product->identity, identity) || product->availability != QA_CONTENT_INSTALLED) return recipe_fail(error, "Remote content is not installed");
    size_t view = SIZE_MAX;
    for (size_t i = 0; i < r->view_count; ++i) if (r->views[i].product == product->id && !strncmp(r->views[i].owner, "content:", 8)) { view = i; break; }
    if (view == SIZE_MAX) {
        qa_vfs *selected = NULL; if (!qa_catalog_open(r->catalog, product->id, &selected, error)) return false;
        qa_vfs *content = qa_vfs_create(r->pool, error); bool retained = content != NULL;
        for (size_t i = 0; retained && i < qa_vfs_mount_count(selected); ++i) {
            qa_vfs_mount_info actual; qa_mount_id mounted;
            retained = qa_vfs_mount_at(selected, i, &actual) && qa_vfs_mount_retained(content, selected, actual.id,
                actual.comparison, false, &mounted, error) && qa_vfs_set_mount_q3_demo(content, mounted, actual.q3_demo, error) &&
                qa_vfs_set_user_overlay(content, mounted, actual.user_overlay, error);
        }
        qa_vfs_destroy(selected); if (!retained) { qa_vfs_destroy(content); return false; }
        size_t size = strlen(identity) + 9; char *owner = malloc(size);
        if (!owner) { qa_vfs_destroy(content); qa_error_set(error, QA_ERROR_MEMORY, 0, "Naming genuine remote resource content"); return false; }
        snprintf(owner, size, "content:%s", identity); bool added = recipe_view_add(r, owner, r->catalog, content, true, &view, error); free(owner);
        if (!added) { qa_vfs_destroy(content); return false; }
        r->views[view].product = product->id; r->views[view].admitted_policy = qa_vfs_clone(content, error);
        if (!r->views[view].admitted_policy) {
            qa_vfs_destroy(r->views[view].files); qa_catalog_release((qa_catalog *)r->views[view].catalog); --r->view_count; return false;
        }
    }
    *files = r->views[view].files; *actual_product = product; return true;
}
bool qa_executable_recipe_acquire_resource(qa_executable_recipe *r, const char *identity, const char *path,
    const qa_sha256_digest *digest, uint64_t length, qa_launch_resource *out, qa_vfs **files,
    const qa_vfs_acquisition **receipt, qa_error *error)
{
    if (!r || r->visiting || !identity || !digest || !out || !files || !receipt || !recipe_path(path, error) ||
        !qa_executable_recipe_current(r, r->catalog)) return recipe_fail(error, "Wire resource requires its admitted current recipe");
    if (qa_executable_recipe_find_resource(r, identity, path, digest, length, out, files, receipt)) return true;
    qa_vfs *content = NULL; const qa_product *product;
    if (!qa_executable_recipe_content(r, identity, &content, &product, error)) return false;
    size_t view = SIZE_MAX;
    for (size_t i = 0; i < r->view_count; ++i) if (r->views[i].files == content) { view = i; break; }
    if (view == SIZE_MAX) return recipe_fail(error, "Resolved content lost its actual recipe holder");
    qa_resource *actual = NULL;
    if (!qa_vfs_acquire(content, path, &actual, NULL, error)) return false;
    if (qa_resource_bytes(actual).size != length || !qa_sha256_equal(qa_resource_digest(actual), digest)) {
        qa_resource_release(actual); return recipe_fail(error, "Actual remote resource bytes differ from wire key");
    }
    size_t index; bool ok = recipe_resource_add_from(r, product->id, path, actual, view, &index, error); qa_resource_release(actual);
    return ok && qa_executable_recipe_resource(r, index, out, files, receipt);
}
size_t qa_executable_recipe_sidecar_count(const qa_executable_recipe *r) { return r ? r->sidecar_count : 0; }
const qa_recipe_sidecar *qa_executable_recipe_sidecar(const qa_executable_recipe *r, size_t i) { return r && i < r->sidecar_count ? &r->sidecars[i] : NULL; }

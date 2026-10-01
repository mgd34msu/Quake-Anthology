#include "internal.h"
#include "qa/launch_save.h"

typedef struct qa_launch_instance_storage {
    size_t references, leases;
    qa_launch_instance view;
    qa_launch_draft *identity;
    qa_configuration_hooks hooks;
    bool prepared, closing;
    qa_vfs_acquisition artifact_acquisition;
} instance_owner;
struct qa_launch_instance_lease {
    instance_owner *owner;
    qa_launch_instance view;
};
typedef struct instance_binding {
    instance_owner *owner;
    qa_launch_instance view;
} instance_binding;
struct qa_launch_snapshot {
    size_t references;
    qa_launch_draft *draft;
    qa_vfs *mounts;
    instance_binding *instances;
    size_t instance_count;
    qa_launch_resource *resources;
    size_t resource_count, resource_capacity;
};
struct qa_configuration {
    qa_configuration_hooks hooks;
    qa_launch_snapshot *current;
    uint64_t generation;
    size_t transactions;
    bool busy;
};
struct qa_configuration_transaction {
    qa_configuration *manager;
    qa_launch_snapshot *candidate, *previous;
    uint64_t generation;
    void *ticket;
    bool validated;
    bool restoring;
    bool replacing;
    uint64_t restored_generation;
    const qa_launch_restore_content *content;
};

static bool error_message(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false; }

static bool retain_acquisition(const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    if (!source || !source->mount || !source->resource_id || !source->path || !source->lookup_path)
        return error_message(error, "Restored artifact lacks its true retained acquisition");
    *out = (qa_vfs_acquisition){.mount = source->mount, .resource_id = source->resource_id};
    const char *values[] = {source->path, source->lookup_path, source->link_source, source->link_target};
    char **targets[] = {&out->path, &out->lookup_path, &out->link_source, &out->link_target};
    for (size_t i = 0; i < 4; ++i) if (values[i]) {
        size_t length = strlen(values[i]);
        *targets[i] = malloc(length + 1);
        if (!*targets[i]) {
            qa_vfs_acquisition_dispose(out);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual restored artifact receipt"); return false;
        }
        memcpy(*targets[i], values[i], length + 1);
    }
    return true;
}

static void owner_dispose(instance_owner *owner)
{
    qa_resource_release((qa_resource *)owner->view.artifact);
    qa_vfs_acquisition_dispose(&owner->artifact_acquisition);
    qa_resource_release((qa_resource *)owner->view.declaration);
    for (size_t i = 0; i < owner->view.interface_count; ++i)
        qa_resource_release((qa_resource *)owner->view.interfaces[i].resource);
    free((void *)owner->view.interfaces);
    free((void *)owner->view.behaviors);
    qa_vfs_destroy(owner->view.content);
    qa_launch_draft_destroy(owner->identity);
    free(owner);
}
static void owner_release(instance_owner *owner)
{
    if (!owner || --owner->references) return;
    if (owner->prepared) {
        owner->prepared = false;
        owner->closing = true;
        owner->hooks.close_instance(owner->hooks.context, owner->view.state);
        owner->closing = false;
    }
    if (!owner->leases) owner_dispose(owner);
}
bool qa_launch_instance_retain_metadata(const qa_launch_instance *instance,
                                        qa_launch_instance_lease **out, qa_error *error)
{
    if (!instance || !instance->storage || !out)
        return error_message(error, "metadata lease needs a retained launch instance and output");
    *out = NULL;
    instance_owner *owner = instance->storage;
    if (owner->leases == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "launch metadata leases are exhausted");
        return false;
    }
    qa_launch_instance_lease *lease = malloc(sizeof(*lease));
    if (!lease) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain launch metadata");
        return false;
    }
    *lease = (qa_launch_instance_lease){.owner = owner, .view = *instance};
    ++owner->leases;
    *out = lease;
    return true;
}
const qa_launch_instance *qa_launch_instance_lease_view(const qa_launch_instance_lease *lease)
{ return lease ? &lease->view : NULL; }
qa_catalog *qa_launch_instance_catalog(const qa_launch_instance *instance)
{ return instance && instance->storage ? instance->storage->identity->catalog : NULL; }
void qa_launch_instance_lease_release(qa_launch_instance_lease *lease)
{
    if (!lease) return;
    instance_owner *owner = lease->owner;
    free(lease);
    if (!--owner->leases && !owner->references && !owner->closing)
        owner_dispose(owner);
}
void qa_launch_snapshot_retain(const qa_launch_snapshot *snapshot)
{ if (snapshot) ++((qa_launch_snapshot *)snapshot)->references; }
void qa_launch_snapshot_release(const qa_launch_snapshot *snapshot)
{
    qa_launch_snapshot *s = (qa_launch_snapshot *)snapshot;
    if (!s || --s->references) return;
    for (size_t i = s->instance_count; i-- > 0;) owner_release(s->instances[i].owner);
    for (size_t i = 0; i < s->resource_count; ++i) qa_resource_release((qa_resource *)s->resources[i].resource);
    free(s->instances); free(s->resources); qa_vfs_destroy(s->mounts);
    qa_launch_draft_destroy(s->draft); free(s);
}
const qa_launch_choices *qa_launch_snapshot_choices(const qa_launch_snapshot *s)
{ return s ? &s->draft->choices : NULL; }
bool qa_launch_snapshot_draft_copy(const qa_launch_snapshot *s,
                                    qa_launch_draft **out, qa_error *error)
{
    if (!s || !out)
        return error_message(error, "snapshot copy needs an active snapshot and output");
    return qa_launch_draft_copy(s->draft, out, error);
}
qa_catalog *qa_launch_snapshot_catalog(const qa_launch_snapshot *s)
{ return s ? s->draft->catalog : NULL; }
qa_vfs *qa_launch_snapshot_mounts(const qa_launch_snapshot *s) { return s ? s->mounts : NULL; }
size_t qa_launch_snapshot_instance_count(const qa_launch_snapshot *s) { return s ? s->instance_count : 0; }
const qa_launch_instance *qa_launch_snapshot_instance(const qa_launch_snapshot *s, size_t i)
{ return s && i < s->instance_count ? &s->instances[i].view : NULL; }
const qa_launch_instance *qa_launch_snapshot_find(const qa_launch_snapshot *s, const char *instance)
{
    if (s && instance) for (size_t i = 0; i < s->instance_count; ++i)
        if (!strcmp(s->instances[i].view.selection.instance, instance)) return &s->instances[i].view;
    return NULL;
}
size_t qa_launch_snapshot_resource_count(const qa_launch_snapshot *s) { return s ? s->resource_count : 0; }
const qa_launch_resource *qa_launch_snapshot_resource(const qa_launch_snapshot *s, size_t i)
{ return s && i < s->resource_count ? &s->resources[i] : NULL; }

static void hash_u64(qa_sha256_context *h, uint64_t value)
{
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
    qa_sha256_update(h, (qa_bytes){bytes, sizeof(bytes)});
}
static void hash_text(qa_sha256_context *h, const char *text)
{
    size_t length = strlen(text);
    hash_u64(h, length); qa_sha256_update(h, (qa_bytes){(const uint8_t *)text, length});
}
static void hash_resource(qa_sha256_context *h, const qa_resource *resource)
{
    hash_u64(h, resource != NULL);
    if (!resource) return;
    const qa_sha256_digest *digest = qa_resource_digest(resource);
    qa_sha256_update(h, (qa_bytes){digest->bytes, sizeof(digest->bytes)});
}
static bool instance_identity(instance_owner *owner, const qa_launch_choices *choices,
                               qa_error *error)
{
    qa_launch_instance *v = &owner->view;
    const qa_launch_provider *p = &v->selection;
    const qa_product *product = qa_catalog_product(owner->identity->catalog, p->product);
    qa_sha256_context h; qa_sha256_init(&h);
    hash_text(&h, "anthology-provider-v3"); hash_text(&h, p->instance);
    hash_text(&h, product->identity); hash_text(&h, p->implementation);
    hash_text(&h, p->artifact); hash_text(&h, p->component);
    hash_u64(&h, p->runtime);
    hash_u64(&h, qa_catalog_generation(owner->identity->catalog));
    const qa_clock_config *clock = &p->clock;
    hash_u64(&h, clock->kind); hash_u64(&h, clock->initial_time_ns);
    hash_u64(&h, clock->interval_ns); hash_u64(&h, clock->minimum_frame_ns);
    hash_u64(&h, clock->maximum_frame_ns); hash_u64(&h, clock->initial_lead_ns);
    hash_u64(&h, clock->maximum_steps);
    hash_u64(&h, p->options.size); qa_sha256_update(&h, p->options);
    hash_u64(&h, owner->hooks.instance_configuration != NULL);
    if (owner->hooks.instance_configuration) {
        qa_sha256_digest configuration = {0};
        if (!owner->hooks.instance_configuration(owner->hooks.context, v, choices,
                                                  &configuration, error))
            return false;
        qa_sha256_update(&h, (qa_bytes){configuration.bytes, sizeof(configuration.bytes)});
    }
    hash_resource(&h, v->artifact); hash_resource(&h, v->declaration);
    hash_u64(&h, v->interface_count);
    for (size_t i = 0; i < v->interface_count; ++i) {
        hash_text(&h, v->interfaces[i].path); hash_resource(&h, v->interfaces[i].resource);
    }
    hash_u64(&h, v->behavior_count);
    for (size_t i = 0; i < v->behavior_count; ++i) {
        const qa_catalog_weapon_behavior *b = v->behaviors[i];
        hash_text(&h, b->id);
        qa_sha256_update(&h, (qa_bytes){b->artifact_digest.bytes, sizeof(b->artifact_digest.bytes)});
        qa_sha256_update(&h, (qa_bytes){b->declaration_digest.bytes, sizeof(b->declaration_digest.bytes)});
    }
    size_t mounts = qa_vfs_mount_count(v->content);
    hash_u64(&h, mounts);
    for (size_t i = 0; i < mounts; ++i) {
        qa_vfs_mount_info mount;
        qa_vfs_mount_at(v->content, i, &mount);
        hash_u64(&h, mount.is_archive); hash_u64(&h, mount.format);
        if (mount.digest) qa_sha256_update(&h, (qa_bytes){mount.digest->bytes, sizeof(mount.digest->bytes)});
    }
    qa_sha256_final(&h, &v->identity);
    return true;
}

static uint64_t instance_roles(const qa_launch_choices *v, const char *name)
{
    uint64_t roles = 0;
    for (size_t i = 0; i < v->binding_count; ++i) if (!strcmp(v->bindings[i].instance, name)) roles |= QA_ROLE_BIT(v->bindings[i].role);
    for (size_t i = 0; i < v->mode_count; ++i) if (!strcmp(v->modes[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_MODE);
    for (size_t i = 0; i < v->equipment_count; ++i) {
        const qa_launch_equipment *e = &v->equipment[i];
        if (!strcmp(e->instance, name) || (e->selection.grapple != QA_GRAPPLE_DISABLED && !strcmp(e->grapple_source, name)) ||
            (e->selection.grenades.enabled && !strcmp(e->grenade_source, name))) roles |= QA_ROLE_BIT(QA_ROLE_EQUIPMENT);
    }
    for (size_t i = 0; i < v->monster_count; ++i)
        if (!v->monsters[i].map_defined && !strcmp(v->monsters[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_MONSTERS);
    for (size_t i = 0; i < v->behavior_count; ++i)
        if (v->behaviors[i].enabled && !strcmp(v->behaviors[i].instance, name)) roles |= QA_ROLE_BIT(QA_ROLE_TRAJECTORY);
    return roles;
}

static bool selected_behaviors(instance_owner *owner, const qa_launch_choices *choices, qa_error *error)
{
    if (!choices->behavior_count) return true;
    const qa_catalog_weapon_behavior **selected = calloc(choices->behavior_count, sizeof(*selected));
    if (!selected) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain selected trajectories"); return false; }
    owner->view.behaviors = selected;
    for (size_t i = 0; i < choices->behavior_count; ++i) {
        const qa_launch_weapon_behavior *s = &choices->behaviors[i];
        if (!s->enabled || strcmp(s->instance, owner->view.selection.instance)) continue;
        const qa_catalog_weapon_behavior *b = qa_catalog_weapon_behavior_find(owner->identity->catalog,
            owner->view.selection.product, s->behavior);
        if (!b || !owner->view.artifact ||
            !qa_sha256_equal(&b->artifact_digest, qa_resource_digest(owner->view.artifact)))
            return error_message(error, "trajectory executable changed since catalog discovery");
        if (b->declaration_path) {
            qa_resource *current;
            if (!qa_vfs_acquire(owner->view.content, b->declaration_path, &current, NULL, error)) return false;
            bool same = qa_sha256_equal(qa_resource_digest(current), &b->declaration_digest);
            qa_resource_release(current);
            if (!same) return error_message(error, "trajectory declaration changed since catalog discovery");
        }
        bool duplicate = false;
        for (size_t j = 0; j < owner->view.behavior_count; ++j) if (selected[j] == b) duplicate = true;
        if (!duplicate) selected[owner->view.behavior_count++] = b;
    }
    return true;
}

static bool read_interfaces(instance_owner *owner, qa_error *error)
{
    static const char *paths[] = {"quakec-compatibility.json", "native-compatibility.json",
        "qvm-compatibility.json", "qvm-items.json", "weapon-behaviors.json",
        "native-weapon-behaviors.json", "qvm-weapon-behaviors.json"};
    qa_launch_resource *resources = calloc(sizeof(paths) / sizeof(paths[0]), sizeof(*resources));
    if (!resources) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain provider interface identities"); return false; }
    owner->view.interfaces = resources;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        qa_resource *resource; qa_error reason = {0};
        if (!qa_vfs_acquire(owner->view.content, paths[i], &resource, NULL, &reason)) {
            if (reason.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = reason;
            return false;
        }
        resources[owner->view.interface_count++] = (qa_launch_resource){owner->view.selection.product, paths[i], resource};
    }
    return true;
}

static void bind_instance(qa_launch_snapshot *snapshot,instance_owner *owner,uint64_t roles)
{
    instance_binding *binding=&snapshot->instances[snapshot->instance_count++];
    binding->owner=owner;
    binding->view=owner->view;
    binding->view.roles=roles;
}

static bool restored_selection_matches(const qa_launch_provider *requested,
    const qa_catalog *current_catalog, const qa_launch_restored_instance *saved)
{
    const qa_launch_provider *actual = &saved->selection;
    const qa_product *current = qa_catalog_product(current_catalog, requested->product);
    const qa_product *prior = qa_catalog_product(saved->catalog, actual->product);
    if (!current || !prior || strcmp(current->identity, prior->identity) ||
        !actual->instance || !actual->implementation || !actual->artifact || !actual->component ||
        strcmp(actual->instance, requested->instance) ||
        strcmp(actual->implementation, requested->implementation) ||
        strcmp(actual->artifact, requested->artifact) ||
        strcmp(actual->component, requested->component) || actual->runtime != requested->runtime ||
        actual->options.size != requested->options.size ||
        (actual->options.size && (!actual->options.data ||
            memcmp(actual->options.data, requested->options.data, actual->options.size)))) return false;
    const qa_clock_config *a = &actual->clock, *b = &requested->clock;
    return a->kind == b->kind && a->initial_time_ns == b->initial_time_ns &&
        a->interval_ns == b->interval_ns && a->minimum_frame_ns == b->minimum_frame_ns &&
        a->maximum_frame_ns == b->maximum_frame_ns && a->initial_lead_ns == b->initial_lead_ns &&
        a->maximum_steps == b->maximum_steps;
}

static bool restored_instance_content(qa_configuration_transaction *transaction,
    instance_owner *owner, const qa_launch_provider *selection, qa_error *error)
{
    qa_launch_restored_instance saved = {0};
    bool ok = transaction->content->instance(transaction->content->context, selection, &saved, error);
    owner->view.content = saved.content;
    if (!ok) return false;
    if (!saved.catalog || !saved.content ||
        (saved.interface_count && !saved.interfaces) ||
        (saved.behavior_count && !saved.behaviors) ||
        saved.interface_count > SIZE_MAX / sizeof(qa_launch_resource) ||
        saved.behavior_count > SIZE_MAX / sizeof(*saved.behaviors) ||
        !restored_selection_matches(selection, transaction->candidate->draft->catalog, &saved))
        return error_message(error, "saved provider content disagrees with its retained selection");
    if (!launch_empty(saved.catalog, &owner->identity, error) ||
        !qa_launch_set_provider(owner->identity, &saved.selection, error)) return false;
    owner->view.selection = owner->identity->choices.providers[0];
    owner->view.artifact = saved.artifact;
    owner->view.declaration = saved.declaration;
    qa_resource_retain((qa_resource *)saved.artifact);
    qa_resource_retain((qa_resource *)saved.declaration);
    if (saved.artifact) {
        if (!retain_acquisition(saved.artifact_acquisition, &owner->artifact_acquisition, error) ||
            owner->artifact_acquisition.resource_id != qa_resource_id(saved.artifact) ||
            !qa_vfs_acquisition_valid(owner->view.content, &owner->artifact_acquisition, error)) return false;
        owner->view.artifact_acquisition = &owner->artifact_acquisition;
    }
    qa_launch_resource *interfaces = calloc(saved.interface_count ? saved.interface_count : 1,
                                             sizeof(*interfaces));
    const qa_catalog_weapon_behavior **behaviors = calloc(saved.behavior_count ? saved.behavior_count : 1,
                                                           sizeof(*behaviors));
    if (!interfaces || !behaviors) {
        free(interfaces); free(behaviors);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain saved provider content identities");
        return false;
    }
    owner->view.interfaces = interfaces;
    owner->view.behaviors = behaviors;
    for (size_t i = 0; i < saved.interface_count; ++i) {
        const qa_launch_resource *value = saved.interfaces + i;
        if (!value->path || !value->resource || value->product != saved.selection.product)
            return error_message(error, "saved provider interface has an invalid retained source");
        const char *path = launch_text(owner->identity, value->path, error);
        if (!path) return false;
        interfaces[owner->view.interface_count++] = (qa_launch_resource){value->product, path, value->resource};
        qa_resource_retain((qa_resource *)value->resource);
    }
    for (size_t i = 0; i < saved.behavior_count; ++i) {
        const qa_catalog_weapon_behavior *value = saved.behaviors[i];
        if (!value || qa_catalog_weapon_behavior_find(saved.catalog,
            saved.selection.product, value->id) != value)
            return error_message(error, "saved provider trajectory has an invalid retained catalog");
        behaviors[owner->view.behavior_count++] = value;
    }
    if (!instance_identity(owner, &transaction->candidate->draft->choices, error)) return false;
    return qa_sha256_equal(&owner->view.identity, &saved.identity) ||
        error_message(error, "saved provider implementation identity disagrees with retained content");
}

static bool prepare_instance(qa_configuration_transaction *transaction, const qa_launch_provider *selection,
                              uint64_t roles, qa_error *error)
{
    qa_launch_snapshot *candidate = transaction->candidate;
    instance_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate selected provider"); return false; }
    owner->references = 1; owner->hooks = transaction->manager->hooks;
    owner->view.storage = owner;
    owner->view.roles = roles;
    if (transaction->content) {
        if (!restored_instance_content(transaction, owner, selection, error)) goto fail;
        goto construct;
    }
    if (!launch_empty(candidate->draft->catalog, &owner->identity, error) ||
        !qa_launch_set_provider(owner->identity, selection, error)) goto fail;
    owner->view.selection = owner->identity->choices.providers[0];
    if (!qa_catalog_open(candidate->draft->catalog, selection->product, &owner->view.content, error)) goto fail;
    if (selection->runtime != QA_PROGRAM_BUILTIN) {
        qa_resource *artifact;
        if (!qa_vfs_acquire_receipt(owner->view.content, selection->artifact, &artifact,
            &owner->artifact_acquisition, error)) goto fail;
        owner->view.artifact = artifact;
        owner->view.artifact_acquisition = &owner->artifact_acquisition;
        if (!read_interfaces(owner, error)) goto fail;
    }
    if (*selection->component) {
        const qa_catalog_mod *mod = qa_catalog_mod_find(candidate->draft->catalog, selection->component);
        if (!mod || mod->unavailable || !mod->declaration.data || !owner->view.artifact ||
            !qa_sha256_equal(&mod->program_digest, qa_resource_digest(owner->view.artifact))) {
            error_message(error, "component executable changed since catalog discovery"); goto fail;
        }
        qa_resource *declaration;
        if (!qa_vfs_acquire(owner->view.content, mod->declaration_path, &declaration, NULL, error)) goto fail;
        owner->view.declaration = declaration;
        if (!qa_sha256_equal(qa_resource_digest(declaration), &mod->declaration_digest)) {
            error_message(error, "component declaration changed since catalog discovery"); goto fail;
        }
    }
    if (!selected_behaviors(owner, &candidate->draft->choices, error)) goto fail;
    if (!instance_identity(owner, &candidate->draft->choices, error)) goto fail;
    if (!transaction->replacing && transaction->previous) for (size_t i = 0; i < transaction->previous->instance_count; ++i) {
        instance_owner *previous = transaction->previous->instances[i].owner;
        if (strcmp(previous->view.selection.instance, selection->instance) ||
            !qa_sha256_equal(&previous->view.identity, &owner->view.identity)) continue;
        ++previous->references; owner_release(owner); owner = previous;
        bind_instance(candidate,owner,roles); return true;
    }
construct:
    if (!owner->hooks.prepare_instance(owner->hooks.context, &owner->view, &owner->view.state, error)) {
        owner->prepared = owner->view.state != NULL;
        goto fail;
    }
    owner->prepared = true;
    bind_instance(candidate,owner,roles); return true;
fail:
    owner_release(owner); return false;
}

bool qa_launch_instance_prepare_client_metadata(const qa_launch_instance *source,
    qa_catalog *catalog, qa_product_id selected, qa_vfs *prepared, const char *path,
    qa_launch_instance_lease **out, qa_error *error)
{
    const qa_product *product = catalog ? qa_catalog_product(catalog, selected) : NULL;
    if (!source || !source->storage || !catalog || !product || product->family != QA_GAME_Q3 ||
        !prepared || !path || !*path || !out || *out ||
        (source->selection.runtime != QA_PROGRAM_QVM && source->selection.runtime != QA_PROGRAM_NATIVE))
        return error_message(error, "Client metadata requires its actual Q3 source and prepared content");
    instance_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining private Q3 client metadata"); return false; }
    owner->references = 1; owner->view.storage = owner;
    qa_launch_provider selection = source->selection;
    selection.product = selected; selection.artifact = path; selection.component = "";
    const qa_catalog_mod *component = NULL;
    for (size_t i = 0; i < qa_catalog_mod_count(catalog); ++i) {
        const qa_catalog_mod *mod = qa_catalog_mod_at(catalog, i);
        if (mod->product != selected || mod->runtime != selection.runtime || !mod->program_path ||
            strcmp(mod->program_path, path)) continue;
        if (component) { owner_release(owner); return error_message(error, "Client artifact has ambiguous real profile owners"); }
        component = mod;
    }
    if (component) selection.component = component->key;
    owner->view.roles = QA_ROLE_BIT(QA_ROLE_HUD) | QA_ROLE_BIT(QA_ROLE_MENU);
    owner->view.state = source->state;
    bool ok = launch_empty(catalog, &owner->identity, error) &&
        qa_launch_set_provider(owner->identity, &selection, error);
    if (ok) {
        owner->view.selection = owner->identity->choices.providers[0];
        owner->view.content = qa_vfs_clone(prepared, error);
        ok = owner->view.content != NULL;
    }
    qa_resource *artifact = NULL;
    if (ok) ok = qa_vfs_acquire_receipt(owner->view.content, path, &artifact,
        &owner->artifact_acquisition, error);
    owner->view.artifact = artifact;
    owner->view.artifact_acquisition = &owner->artifact_acquisition;
    if (ok && component) {
        if (component->unavailable || !component->declaration_path ||
            !qa_sha256_equal(&component->program_digest, qa_resource_digest(artifact)))
            ok = error_message(error, "Client artifact differs from its actual selected profile");
        qa_resource *declaration = NULL;
        if (ok) ok = qa_vfs_acquire(owner->view.content, component->declaration_path,
            &declaration, NULL, error);
        owner->view.declaration = declaration;
        if (ok && !qa_sha256_equal(&component->declaration_digest, qa_resource_digest(declaration)))
            ok = error_message(error, "Client profile changed since actual content discovery");
    }
    if (ok) ok = read_interfaces(owner, error) &&
        instance_identity(owner, &owner->identity->choices, error) &&
        qa_launch_instance_retain_metadata(&owner->view, out, error);
    owner_release(owner);
    return ok;
}

bool qa_launch_instance_restore_client_metadata(const qa_launch_instance *source,
    const qa_launch_restored_instance *saved, qa_launch_instance_lease **out, qa_error *error)
{
    if (!source || !saved || !saved->content || !saved->catalog || !out || *out)
        return error_message(error, "Client metadata restoration requires its actual claimed owners");
    instance_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) {
        qa_vfs_destroy(saved->content);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring private client metadata owner"); return false;
    }
    owner->references = 1; owner->view.storage = owner; owner->view.content = saved->content;
    owner->view.state = source->state; owner->view.roles = QA_ROLE_BIT(QA_ROLE_HUD) | QA_ROLE_BIT(QA_ROLE_MENU);
    const qa_launch_provider *a = &source->selection, *b = &saved->selection;
    const qa_product *product = qa_catalog_product(saved->catalog, b->product);
    bool ok = product && product->family == QA_GAME_Q3 && b->instance && b->implementation &&
        b->artifact && *b->artifact && b->component && !strcmp(a->instance, b->instance) &&
        !strcmp(a->implementation, b->implementation) && a->runtime == b->runtime &&
        a->options.size == b->options.size && (!a->options.size ||
            (b->options.data && !memcmp(a->options.data, b->options.data, a->options.size))) &&
        a->clock.kind == b->clock.kind && a->clock.initial_time_ns == b->clock.initial_time_ns &&
        a->clock.interval_ns == b->clock.interval_ns && a->clock.minimum_frame_ns == b->clock.minimum_frame_ns &&
        a->clock.maximum_frame_ns == b->clock.maximum_frame_ns && a->clock.initial_lead_ns == b->clock.initial_lead_ns &&
        a->clock.maximum_steps == b->clock.maximum_steps && saved->artifact &&
        saved->interface_count <= SIZE_MAX / sizeof(qa_launch_resource) && !saved->behavior_count;
    if (!ok) error_message(error, "Saved private client descriptor leaves its real source selection");
    if (ok) ok = launch_empty(saved->catalog, &owner->identity, error) &&
        qa_launch_set_provider(owner->identity, b, error);
    if (ok) {
        owner->view.selection = owner->identity->choices.providers[0];
        owner->view.artifact = saved->artifact; qa_resource_retain((qa_resource *)saved->artifact);
        owner->view.declaration = saved->declaration; qa_resource_retain((qa_resource *)saved->declaration);
        ok = retain_acquisition(saved->artifact_acquisition, &owner->artifact_acquisition, error) &&
            owner->artifact_acquisition.resource_id == qa_resource_id(saved->artifact) &&
            qa_vfs_acquisition_valid(owner->view.content, &owner->artifact_acquisition, error);
        owner->view.artifact_acquisition = &owner->artifact_acquisition;
    }
    if (ok && saved->interface_count) {
        qa_launch_resource *interfaces = calloc(saved->interface_count, sizeof(*interfaces));
        owner->view.interfaces = interfaces;
        ok = interfaces != NULL && saved->interfaces != NULL;
        if (!ok) qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring private client profile inventory");
        for (size_t i = 0; ok && i < saved->interface_count; ++i) {
            const qa_launch_resource *resource = saved->interfaces + i;
            if (!resource->resource || !resource->path || resource->product != b->product) {
                ok = error_message(error, "Private client profile has an invalid true resource owner"); break;
            }
            const char *path = launch_text(owner->identity, resource->path, error);
            if (!path) { ok = false; break; }
            interfaces[owner->view.interface_count++] = (qa_launch_resource){b->product, path, resource->resource};
            qa_resource_retain((qa_resource *)resource->resource);
        }
    }
    if (ok) ok = instance_identity(owner, &owner->identity->choices, error) &&
        qa_sha256_equal(&owner->view.identity, &saved->identity);
    if (!ok && error && error->code == QA_OK)
        error_message(error, "Restored private descriptor identity differs from its actual retained content");
    if (ok) ok = qa_launch_instance_retain_metadata(&owner->view, out, error);
    owner_release(owner); return ok;
}

static bool resource_add(qa_launch_snapshot *s, qa_product_id product, const char *path, qa_error *error)
{
    for (size_t i = 0; i < s->resource_count; ++i)
        if (s->resources[i].product == product && !strcmp(s->resources[i].path, path)) return true;
    qa_vfs *view;
    if (!qa_catalog_open(s->draft->catalog, product, &view, error)) return false;
    qa_resource *resource;
    bool ok = qa_vfs_acquire(view, path, &resource, NULL, error);
    qa_vfs_destroy(view);
    if (!ok) return false;
    const char *retained_path = launch_text(s->draft, path, error);
    if (!retained_path || !launch_grow((void **)&s->resources, &s->resource_capacity,
        s->resource_count + 1, sizeof(*s->resources), error)) { qa_resource_release(resource); return false; }
    s->resources[s->resource_count++] = (qa_launch_resource){product, retained_path, resource};
    return true;
}

static bool prepare_mounts(qa_launch_snapshot *candidate,
    const qa_launch_restore_content *content, qa_error *error)
{
    const qa_launch_choices *v = &candidate->draft->choices;
    if (content) {
        if (!content->mounts(content->context, &candidate->mounts, error)) return false;
        if (!candidate->mounts || content->resource_count > SIZE_MAX / sizeof(*candidate->resources))
            return error_message(error, "saved launch content requires its real mounts and resource inventory");
        candidate->resources = calloc(content->resource_count ? content->resource_count : 1,
                                       sizeof(*candidate->resources));
        if (!candidate->resources) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain saved launch resource identities");
            return false;
        }
        candidate->resource_capacity = content->resource_count;
        bool map = false, environment = v->world.environment != QA_ENVIRONMENT_SELECTED;
        for (size_t i = 0; i < content->resource_count; ++i) {
            qa_launch_resource value = {0};
            if (!content->resource(content->context, i, &value, error)) return false;
            if (!value.path || !value.resource || !qa_catalog_product(candidate->draft->catalog, value.product))
                return error_message(error, "saved launch resource has an invalid retained product");
            for (size_t j = 0; j < i; ++j)
                if (candidate->resources[j].product == value.product &&
                    !strcmp(candidate->resources[j].path, value.path))
                    return error_message(error, "saved launch resource inventory repeats an entry");
            const char *path = launch_text(candidate->draft, value.path, error);
            if (!path) return false;
            candidate->resources[candidate->resource_count++] =
                (qa_launch_resource){value.product, path, value.resource};
            qa_resource_retain((qa_resource *)value.resource);
            if (value.product == v->world.geometry && !strcmp(value.path, v->world.map)) map = true;
            if (v->world.environment == QA_ENVIRONMENT_SELECTED &&
                value.product == v->world.environment_product &&
                !strcmp(value.path, v->world.environment_path)) environment = true;
        }
        return (map && environment) || error_message(error, "saved launch resources omit retained world content");
    }
    size_t maximum = v->provider_count + v->mod_count + 1;
    qa_product_id *products = calloc(maximum ? maximum : 1, sizeof(*products));
    if (!products) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot resolve launch content sources"); return false; }
    size_t count = 0;
    for (size_t i = 0; i < v->provider_count; ++i) products[count++] = v->providers[i].product;
    for (size_t i = 0; i < v->mod_count; ++i) if (v->mods[i].enabled)
        products[count++] = qa_catalog_mod_find(candidate->draft->catalog, v->mods[i].component)->product;
    if (v->world.environment == QA_ENVIRONMENT_SELECTED) products[count++] = v->world.environment_product;
    const qa_launch_binding *combat = qa_launch_binding_for(v, (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, QA_ROLE_COMBAT, "");
    const qa_launch_provider *rules = combat ? launch_provider(v, combat->instance) : NULL;
    qa_catalog_mount_selection mounts = {.assets = v->world.presentation, .geometry = v->world.geometry,
        .combat = rules ? rules->product : 0, .explicit_presentation = v->world.explicit_presentation,
        .additional = products, .additional_count = count};
    bool ok = qa_catalog_mount_plan(candidate->draft->catalog, &mounts, &candidate->mounts, error);
    free(products);
    if (!ok || !resource_add(candidate, v->world.geometry, v->world.map, error)) return false;
    if (v->world.environment == QA_ENVIRONMENT_SELECTED &&
        !resource_add(candidate, v->world.environment_product, v->world.environment_path, error)) return false;
    return true;
}

bool qa_configuration_create(const qa_configuration_hooks *hooks, qa_configuration **out, qa_error *error)
{
    if (!hooks || !out || !hooks->safe || !hooks->prepare_instance || !hooks->close_instance ||
        !hooks->prepare_publication || !hooks->rollback_publication || !hooks->publish)
        return error_message(error, "configuration needs complete native composition lifecycle hooks");
    qa_configuration *manager = calloc(1, sizeof(*manager));
    if (!manager) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate configuration owner"); return false; }
    manager->hooks = *hooks; *out = manager; return true;
}
bool qa_configuration_destroy(qa_configuration *manager, qa_error *error)
{
    if (!manager) return true;
    if (manager->busy || manager->transactions)
        return error_message(error, "configuration destruction requires a safe point and no open transactions");
    manager->busy = true;
    if (!manager->hooks.safe(manager->hooks.context)) {
        manager->busy = false;
        return error_message(error, "configuration destruction requires a safe point");
    }
    qa_launch_snapshot *previous = manager->current;
    if (manager->hooks.retire != NULL &&
        !manager->hooks.retire(manager->hooks.context, previous, error)) {
        manager->busy = false;
        return false;
    }
    manager->current = NULL;
    manager->hooks.publish(manager->hooks.context, previous, NULL, NULL);
    qa_launch_snapshot_release(previous); free(manager); return true;
}
uint64_t qa_configuration_generation(const qa_configuration *manager) { return manager ? manager->generation : 0; }
const qa_launch_snapshot *qa_configuration_current(const qa_configuration *manager) { return manager ? manager->current : NULL; }

static void discard_transaction(qa_configuration_transaction *t)
{
    if (t->validated) t->manager->hooks.rollback_publication(t->manager->hooks.context, t->ticket);
    qa_launch_snapshot_release(t->candidate); qa_launch_snapshot_release(t->previous);
    --t->manager->transactions; free(t);
}

static bool configuration_prepare(qa_configuration *manager, const qa_launch_draft *draft,
    const qa_launch_restore_content *content, bool replacing,
    qa_configuration_transaction **out, qa_error *error)
{
    if (!manager || !draft || !out || manager->busy) return error_message(error, "invalid or reentrant configuration preparation");
    if (!qa_launch_validate(draft, error)) return false;
    qa_configuration_transaction *t = calloc(1, sizeof(*t));
    qa_launch_snapshot *s = calloc(1, sizeof(*s));
    if (!t || !s) { free(t); free(s); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate configuration transaction"); return false; }
    s->references = 1; t->manager = manager; t->candidate = s; t->generation = manager->generation;
    t->content = content;
    t->replacing = replacing;
    t->previous = manager->current; qa_launch_snapshot_retain(t->previous); ++manager->transactions;
    manager->busy = true;
    if (!qa_launch_draft_copy(draft, &s->draft, error)) goto fail;
    const qa_launch_choices *v = &s->draft->choices;
    size_t count = v->provider_count;
    for (size_t i = 0; i < v->mod_count; ++i) count += v->mods[i].enabled;
    s->instances = calloc(count ? count : 1, sizeof(*s->instances));
    if (!s->instances) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain provider instances"); goto fail; }
    if (!prepare_mounts(s, content, error)) goto fail;
    for (size_t i = 0; i < v->provider_count; ++i)
        if (!prepare_instance(t, &v->providers[i], instance_roles(v, v->providers[i].instance), error)) goto fail;
    for (size_t i = 0; i < v->mod_count; ++i) {
        const qa_launch_mod_selection *selection = &v->mods[i];
        if (!selection->enabled) continue;
        const qa_catalog_mod *mod = qa_catalog_mod_find(s->draft->catalog, selection->component);
        const qa_product *product = qa_catalog_product(s->draft->catalog, mod->product);
        qa_clock_kind clock = product->family == QA_GAME_Q3 ? QA_CLOCK_Q3 : product->family == QA_GAME_Q2
            ? (product->edition == QA_EDITION_RERELEASE ? QA_CLOCK_Q2_RERELEASE : QA_CLOCK_Q2_CLASSIC)
            : (product->edition == QA_EDITION_QUAKEWORLD ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE);
        qa_launch_provider provider = {.instance = selection->instance, .product = mod->product,
            .runtime = mod->runtime, .implementation = mod->key, .artifact = mod->program_path,
            .component = mod->key, .clock = qa_clock_defaults(clock)};
        if (!prepare_instance(t, &provider, 0, error)) goto fail;
    }
    t->content = NULL;
    manager->busy = false; *out = t; return true;
fail:
    discard_transaction(t); manager->busy = false; return false;
}
bool qa_configuration_prepare(qa_configuration *manager, const qa_launch_draft *draft,
    qa_configuration_transaction **out, qa_error *error)
{ return configuration_prepare(manager, draft, NULL, false, out, error); }

bool qa_configuration_prepare_replacing(qa_configuration *manager, const qa_launch_draft *draft,
    qa_configuration_transaction **out, qa_error *error)
{ return configuration_prepare(manager, draft, NULL, true, out, error); }

bool qa_configuration_validate(qa_configuration_transaction *t, qa_error *error)
{
    if (!t || t->manager->busy) return error_message(error, "invalid or reentrant configuration validation");
    if (t->restoring) return error_message(error, "restored configuration cannot replay ordinary publication");
    if (t->generation != t->manager->generation) return error_message(error, "configuration changed while this candidate was being prepared");
    if (t->validated) return true;
    t->manager->busy = true;
    if (!t->manager->hooks.safe(t->manager->hooks.context)) {
        t->manager->busy = false;
        return error_message(error, "configuration publication requires a safe point");
    }
    void *ticket = NULL;
    if (!t->manager->hooks.prepare_publication(t->manager->hooks.context, t->previous, t->candidate, &ticket, error)) {
        if (ticket) t->manager->hooks.rollback_publication(t->manager->hooks.context, ticket);
        t->manager->busy = false;
        return false;
    }
    t->ticket = ticket; t->validated = true; t->manager->busy = false; return true;
}
bool qa_configuration_commit(qa_configuration_transaction *t, qa_error *error)
{
    if (!t || !t->validated) return error_message(error, "configuration must be validated before commit");
    if (t->restoring) return error_message(error, "restored configuration needs isolated publication");
    qa_configuration *manager = t->manager;
    if (manager->busy || t->generation != manager->generation || manager->generation == UINT64_MAX)
        return error_message(error, "configuration candidate is stale or publication is already active");
    manager->busy = true;
    if (!manager->hooks.safe(manager->hooks.context)) {
        manager->busy = false;
        return error_message(error, "configuration commit requires a safe point");
    }
    qa_launch_snapshot *previous = manager->current;
    manager->current = t->candidate; t->candidate = NULL; ++manager->generation;
    manager->hooks.publish(manager->hooks.context, previous, manager->current, t->ticket);
    t->ticket = NULL; t->validated = false;
    qa_launch_snapshot_release(previous);
    qa_launch_snapshot_release(t->previous);
    --manager->transactions; manager->busy = false;
    free(t); return true;
}
bool qa_configuration_abort(qa_configuration_transaction *t, qa_error *error)
{
    if (!t) return true;
    qa_configuration *manager = t->manager;
    if (manager->busy) return error_message(error, "configuration abort cannot reenter a lifecycle callback");
    manager->busy = true; discard_transaction(t); manager->busy = false; return true;
}
const qa_launch_snapshot *qa_configuration_candidate(const qa_configuration_transaction *t)
{ return t ? t->candidate : NULL; }

bool qa_configuration_checkpoint_capture(const qa_configuration *manager,
    qa_configuration_checkpoint *out, qa_error *error)
{
    if (!manager || !out || manager->busy || manager->transactions ||
        !manager->hooks.safe(manager->hooks.context))
        return error_message(error, "configuration capture requires a committed safe point");
    *out = (qa_configuration_checkpoint){manager->generation, manager->current != NULL};
    return true;
}

bool qa_configuration_prepare_restored(qa_configuration *manager, const qa_launch_draft *draft,
    const qa_configuration_checkpoint *checkpoint, const qa_launch_restore_content *content,
    qa_configuration_transaction **out, qa_error *error)
{
    if (!manager || !checkpoint || !out || manager->busy || manager->transactions ||
        manager->current || manager->generation || !checkpoint->has_current || !checkpoint->generation ||
        !content || !content->mounts || !content->instance || !content->resource ||
        !manager->hooks.safe(manager->hooks.context))
        return error_message(error, "configuration restoration requires a fresh isolated manager and saved current snapshot");
    if (!configuration_prepare(manager, draft, content, false, out, error)) return false;
    (*out)->restoring = true;
    (*out)->restored_generation = checkpoint->generation;
    return true;
}

bool qa_configuration_commit_restored(qa_configuration_transaction *t, qa_error *error)
{
    if (!t || !t->restoring || t->validated || !t->candidate || !t->restored_generation)
        return error_message(error, "isolated configuration publication requires a prepared restore transaction");
    qa_configuration *manager = t->manager;
    if (manager->busy || manager->current || manager->generation || manager->transactions != 1 ||
        !manager->hooks.safe(manager->hooks.context))
        return error_message(error, "isolated configuration candidate is stale or busy");
    manager->current = t->candidate; t->candidate = NULL;
    manager->generation = t->restored_generation;
    qa_launch_snapshot_release(t->previous);
    --manager->transactions;
    free(t);
    return true;
}

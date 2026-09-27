#include "internal.h"

typedef struct instance_owner {
    size_t references;
    qa_launch_instance view;
    qa_launch_draft *identity;
    qa_configuration_hooks hooks;
    bool prepared;
} instance_owner;
struct qa_launch_snapshot {
    size_t references;
    qa_launch_draft *draft;
    qa_vfs *mounts;
    instance_owner **instances;
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
};

static bool error_message(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false; }

static void owner_release(instance_owner *owner)
{
    if (!owner || --owner->references) return;
    if (owner->prepared) owner->hooks.close_instance(owner->hooks.context, owner->view.state);
    qa_resource_release((qa_resource *)owner->view.artifact);
    qa_resource_release((qa_resource *)owner->view.declaration);
    for (size_t i = 0; i < owner->view.interface_count; ++i)
        qa_resource_release((qa_resource *)owner->view.interfaces[i].resource);
    free((void *)owner->view.interfaces);
    free((void *)owner->view.behaviors);
    qa_vfs_destroy(owner->view.content);
    qa_launch_draft_destroy(owner->identity);
    free(owner);
}
void qa_launch_snapshot_retain(const qa_launch_snapshot *snapshot)
{ if (snapshot) ++((qa_launch_snapshot *)snapshot)->references; }
void qa_launch_snapshot_release(const qa_launch_snapshot *snapshot)
{
    qa_launch_snapshot *s = (qa_launch_snapshot *)snapshot;
    if (!s || --s->references) return;
    for (size_t i = s->instance_count; i-- > 0;) owner_release(s->instances[i]);
    for (size_t i = 0; i < s->resource_count; ++i) qa_resource_release((qa_resource *)s->resources[i].resource);
    free(s->instances); free(s->resources); qa_vfs_destroy(s->mounts);
    qa_launch_draft_destroy(s->draft); free(s);
}
const qa_launch_choices *qa_launch_snapshot_choices(const qa_launch_snapshot *s)
{ return s ? &s->draft->choices : NULL; }
qa_catalog *qa_launch_snapshot_catalog(const qa_launch_snapshot *s)
{ return s ? s->draft->catalog : NULL; }
qa_vfs *qa_launch_snapshot_mounts(const qa_launch_snapshot *s) { return s ? s->mounts : NULL; }
size_t qa_launch_snapshot_instance_count(const qa_launch_snapshot *s) { return s ? s->instance_count : 0; }
const qa_launch_instance *qa_launch_snapshot_instance(const qa_launch_snapshot *s, size_t i)
{ return s && i < s->instance_count ? &s->instances[i]->view : NULL; }
const qa_launch_instance *qa_launch_snapshot_find(const qa_launch_snapshot *s, const char *instance)
{
    if (s && instance) for (size_t i = 0; i < s->instance_count; ++i)
        if (!strcmp(s->instances[i]->view.selection.instance, instance)) return &s->instances[i]->view;
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
static void instance_identity(instance_owner *owner)
{
    qa_launch_instance *v = &owner->view;
    const qa_launch_provider *p = &v->selection;
    const qa_product *product = qa_catalog_product(owner->identity->catalog, p->product);
    qa_sha256_context h; qa_sha256_init(&h);
    hash_text(&h, "anthology-provider-v1"); hash_text(&h, p->instance);
    hash_text(&h, product->identity); hash_text(&h, p->implementation);
    hash_text(&h, p->artifact); hash_text(&h, p->component);
    hash_u64(&h, p->runtime); hash_u64(&h, v->roles);
    hash_u64(&h, qa_catalog_generation(owner->identity->catalog));
    const qa_clock_config *clock = &p->clock;
    hash_u64(&h, clock->kind); hash_u64(&h, clock->initial_time_ns);
    hash_u64(&h, clock->interval_ns); hash_u64(&h, clock->minimum_frame_ns);
    hash_u64(&h, clock->maximum_frame_ns); hash_u64(&h, clock->initial_lead_ns);
    hash_u64(&h, clock->maximum_steps);
    hash_u64(&h, p->options.size); qa_sha256_update(&h, p->options);
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

static bool prepare_instance(qa_configuration_transaction *transaction, const qa_launch_provider *selection,
                              uint64_t roles, qa_error *error)
{
    qa_launch_snapshot *candidate = transaction->candidate;
    instance_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate selected provider"); return false; }
    owner->references = 1; owner->hooks = transaction->manager->hooks;
    if (!launch_empty(candidate->draft->catalog, &owner->identity, error) ||
        !qa_launch_set_provider(owner->identity, selection, error)) goto fail;
    owner->view.selection = owner->identity->choices.providers[0];
    owner->view.roles = roles;
    if (!qa_catalog_open(candidate->draft->catalog, selection->product, &owner->view.content, error)) goto fail;
    if (selection->runtime != QA_PROGRAM_BUILTIN) {
        qa_resource *artifact;
        if (!qa_vfs_acquire(owner->view.content, selection->artifact, &artifact, NULL, error)) goto fail;
        owner->view.artifact = artifact;
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
    instance_identity(owner);
    if (transaction->previous) for (size_t i = 0; i < transaction->previous->instance_count; ++i) {
        instance_owner *previous = transaction->previous->instances[i];
        if (strcmp(previous->view.selection.instance, selection->instance) ||
            !qa_sha256_equal(&previous->view.identity, &owner->view.identity)) continue;
        ++previous->references; owner_release(owner); owner = previous;
        candidate->instances[candidate->instance_count++] = owner; return true;
    }
    if (!owner->hooks.prepare_instance(owner->hooks.context, &owner->view, &owner->view.state, error)) {
        owner->prepared = owner->view.state != NULL;
        goto fail;
    }
    owner->prepared = true;
    candidate->instances[candidate->instance_count++] = owner; return true;
fail:
    owner_release(owner); return false;
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

static bool prepare_mounts(qa_launch_snapshot *candidate, qa_error *error)
{
    const qa_launch_choices *v = &candidate->draft->choices;
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

bool qa_configuration_prepare(qa_configuration *manager, const qa_launch_draft *draft,
                               qa_configuration_transaction **out, qa_error *error)
{
    if (!manager || !draft || !out || manager->busy) return error_message(error, "invalid or reentrant configuration preparation");
    if (!qa_launch_validate(draft, error)) return false;
    qa_configuration_transaction *t = calloc(1, sizeof(*t));
    qa_launch_snapshot *s = calloc(1, sizeof(*s));
    if (!t || !s) { free(t); free(s); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate configuration transaction"); return false; }
    s->references = 1; t->manager = manager; t->candidate = s; t->generation = manager->generation;
    t->previous = manager->current; qa_launch_snapshot_retain(t->previous); ++manager->transactions;
    manager->busy = true;
    if (!qa_launch_draft_copy(draft, &s->draft, error)) goto fail;
    const qa_launch_choices *v = &s->draft->choices;
    size_t count = v->provider_count;
    for (size_t i = 0; i < v->mod_count; ++i) count += v->mods[i].enabled;
    s->instances = calloc(count ? count : 1, sizeof(*s->instances));
    if (!s->instances) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain provider instances"); goto fail; }
    if (!prepare_mounts(s, error)) goto fail;
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
    manager->busy = false; *out = t; return true;
fail:
    discard_transaction(t); manager->busy = false; return false;
}

bool qa_configuration_validate(qa_configuration_transaction *t, qa_error *error)
{
    if (!t || t->manager->busy) return error_message(error, "invalid or reentrant configuration validation");
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

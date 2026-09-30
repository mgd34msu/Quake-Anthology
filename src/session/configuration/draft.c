#include "internal.h"

bool launch_grow(void **data, size_t *capacity, size_t count, size_t size, qa_error *error)
{
    if (count <= *capacity) return true;
    size_t n = *capacity ? *capacity : 8;
    while (n < count && n <= SIZE_MAX / 2) n *= 2;
    if (n < count) n = count;
    if (n <= SIZE_MAX / size) {
        void *p = realloc(*data, n * size);
        if (p) { *data = p; *capacity = n; return true; }
    }
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow launch choices"); return false;
}
const char *launch_text(qa_launch_draft *d, const char *s, qa_error *error)
{
    qa_string_id id;
    if (!qa_strings_intern_cstr(d->strings, s ? s : "", &id, error)) return NULL;
    return qa_strings_cstr(d->strings, id);
}
bool launch_scope_equal(qa_launch_scope a, qa_launch_scope b)
{
    if (a.kind != b.kind) return false;
    if (a.kind == QA_SCOPE_SEAT) return a.seat == b.seat;
    if (a.kind == QA_SCOPE_ACTOR) return a.actor.registry == b.actor.registry &&
        a.actor.generation == b.actor.generation && a.actor.slot == b.actor.slot;
    return true;
}
bool launch_scope_valid(qa_launch_scope s)
{
    return (unsigned)s.kind <= QA_SCOPE_SEAT && (s.kind != QA_SCOPE_ACTOR || (s.actor.registry && s.actor.generation));
}
const qa_launch_provider *launch_provider(const qa_launch_choices *v, const char *name)
{
    if (name) for (size_t i = 0; i < v->provider_count; ++i)
        if (!strcmp(v->providers[i].instance, name)) return &v->providers[i];
    return NULL;
}
static bool valid_draft(qa_launch_draft *d, const void *input, qa_error *error)
{
    if (d && input) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "launch edit requires a draft and value"); return false;
}

bool launch_empty(qa_catalog *catalog, qa_launch_draft **out, qa_error *error)
{
    if (!catalog || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "launch draft requires a catalog"); return false; }
    qa_launch_draft *d = calloc(1, sizeof(*d));
    if (!d) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate launch draft"); return false; }
    if (!qa_strings_create(&d->strings, error)) { free(d); return false; }
    qa_catalog_retain(catalog); d->catalog = catalog;
    *out = d; return true;
}
bool qa_launch_draft_create(qa_catalog *catalog, qa_product_id product, const char *map,
                             qa_launch_draft **out, qa_error *error)
{
    qa_launch_draft *d;
    if (!out || !launch_empty(catalog, &d, error)) return false;
    if (!launch_defaults(d, product, map, error)) { qa_launch_draft_destroy(d); return false; }
    *out = d; return true;
}
bool qa_launch_draft_create_empty(qa_catalog *catalog, qa_launch_draft **out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing empty launch draft output"); return false; }
    return launch_empty(catalog, out, error);
}
void qa_launch_draft_destroy(qa_launch_draft *d)
{
    if (!d) return;
    for (size_t i = 0; i < d->choices.provider_count; ++i) free((void *)d->choices.providers[i].options.data);
    free((void *)d->choices.providers); free((void *)d->choices.bindings); free((void *)d->choices.mods);
    free((void *)d->choices.modes); free((void *)d->choices.equipment); free((void *)d->choices.seats);
    free((void *)d->choices.loadout); free((void *)d->choices.monsters);
    free((void *)d->choices.behaviors);
    qa_strings_destroy(d->strings); qa_catalog_release(d->catalog); free(d);
}
const qa_launch_choices *qa_launch_draft_choices(const qa_launch_draft *d) { return d ? &d->choices : NULL; }
qa_catalog *qa_launch_draft_catalog(const qa_launch_draft *d) { return d ? d->catalog : NULL; }

bool qa_launch_set_world(qa_launch_draft *d, const qa_launch_world *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_world v = *input;
    v.map = launch_text(d, input->map, error);
    v.start_command = launch_text(d, input->start_command, error);
    v.environment_path = launch_text(d, input->environment_path, error);
    if (!v.map || !v.start_command || !v.environment_path) return false;
    d->choices.world = v; return true;
}
bool qa_launch_set_provider(qa_launch_draft *d, const qa_launch_provider *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    if (!input->instance || !*input->instance || (input->options.size && !input->options.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "provider needs an instance identity and valid options"); return false;
    }
    qa_launch_provider v = *input;
    v.instance = launch_text(d, input->instance, error); v.implementation = launch_text(d, input->implementation, error);
    v.artifact = launch_text(d, input->artifact, error); v.component = launch_text(d, input->component, error);
    if (!v.instance || !v.implementation || !v.artifact || !v.component) return false;
    v.options.data = NULL;
    if (input->options.size) {
        uint8_t *bytes = malloc(input->options.size);
        if (!bytes) { qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain provider options"); return false; }
        memcpy(bytes, input->options.data, input->options.size); v.options.data = bytes;
    }
    size_t index = d->choices.provider_count;
    for (size_t i = 0; i < index; ++i) if (!strcmp(d->choices.providers[i].instance, v.instance)) { index = i; break; }
    if (!launch_grow((void **)&d->choices.providers, &d->provider_capacity, index + 1, sizeof(v), error)) { free((void *)v.options.data); return false; }
    qa_launch_provider *items = (qa_launch_provider *)d->choices.providers;
    if (index < d->choices.provider_count) free((void *)items[index].options.data);
    else ++d->choices.provider_count;
    items[index] = v; return true;
}
bool qa_launch_remove_provider(qa_launch_draft *d, const char *name, qa_error *error)
{
    if (!valid_draft(d, name, error)) return false;
    qa_launch_provider *items = (qa_launch_provider *)d->choices.providers;
    for (size_t i = 0; i < d->choices.provider_count; ++i) if (!strcmp(items[i].instance, name)) {
        free((void *)items[i].options.data);
        memmove(items + i, items + i + 1, (--d->choices.provider_count - i) * sizeof(*items)); break;
    }
    return true;
}
const qa_launch_binding *qa_launch_binding_for(const qa_launch_choices *v, qa_launch_scope scope,
                                               qa_launch_role role, const char *selector)
{
    if (!v) return NULL;
    if (!selector) selector = "";
    for (size_t i = 0; i < v->binding_count; ++i) {
        const qa_launch_binding *b = &v->bindings[i];
        if (b->role == role && launch_scope_equal(scope, b->scope) && !strcmp(selector, b->selector)) return b;
    }
    if (scope.kind == QA_SCOPE_SEAT || scope.kind == QA_SCOPE_ACTOR)
        return qa_launch_binding_for(v, (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, role, selector);
    return NULL;
}
bool qa_launch_bind(qa_launch_draft *d, const qa_launch_binding *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    if (!launch_scope_valid(input->scope) || (unsigned)input->role >= QA_ROLE_COUNT || !input->instance || !*input->instance) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid provider role binding"); return false;
    }
    qa_launch_binding v = *input;
    v.instance = launch_text(d, input->instance, error); v.selector = launch_text(d, input->selector, error);
    v.definition = launch_text(d, input->definition, error);
    if (!v.instance || !v.selector || !v.definition) return false;
    size_t index = d->choices.binding_count;
    for (size_t i = 0; i < index; ++i) {
        const qa_launch_binding *b = &d->choices.bindings[i];
        if (b->role == v.role && launch_scope_equal(b->scope, v.scope) && !strcmp(b->selector, v.selector)) { index = i; break; }
    }
    if (!launch_grow((void **)&d->choices.bindings, &d->binding_capacity, index + 1, sizeof(v), error)) return false;
    if (index == d->choices.binding_count) ++d->choices.binding_count;
    ((qa_launch_binding *)d->choices.bindings)[index] = v; return true;
}
bool qa_launch_unbind(qa_launch_draft *d, qa_launch_scope scope, qa_launch_role role,
                      const char *selector, qa_error *error)
{
    if (!d || !launch_scope_valid(scope) || (unsigned)role >= QA_ROLE_COUNT) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid binding removal"); return false;
    }
    qa_launch_binding *items = (qa_launch_binding *)d->choices.bindings;
    for (size_t i = 0; i < d->choices.binding_count; ++i)
        if (items[i].role == role && launch_scope_equal(items[i].scope, scope) && !strcmp(items[i].selector, selector ? selector : "")) {
            memmove(items + i, items + i + 1, (--d->choices.binding_count - i) * sizeof(*items)); break;
        }
    return true;
}

#define UPSERT(field,cap,type,equal) \
    size_t index = d->choices.field##_count; \
    for (size_t i = 0; i < index; ++i) { const type *old = &d->choices.field[i]; if (equal) { index = i; break; } } \
    if (!launch_grow((void **)&d->choices.field, &d->cap, index + 1, sizeof(v), error)) return false; \
    if (index == d->choices.field##_count) ++d->choices.field##_count; \
    ((type *)d->choices.field)[index] = v; return true

/* Singular count member names keep public spans conventional. */
bool qa_launch_set_mod(qa_launch_draft *d, const qa_launch_mod_selection *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_mod_selection v = *input;
    v.instance = launch_text(d, input->instance, error); v.component = launch_text(d, input->component, error);
    if (!v.instance || !*v.instance || !v.component || !qa_catalog_mod_key(v.component)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid mod selection"); return false;
    }
    size_t n = d->choices.mod_count, i = 0;
    while (i < n && strcmp(d->choices.mods[i].instance, v.instance)) ++i;
    if (!launch_grow((void **)&d->choices.mods, &d->mod_capacity, i + 1, sizeof(v), error)) return false;
    if (i == n) ++d->choices.mod_count;
    ((qa_launch_mod_selection *)d->choices.mods)[i] = v; return true;
}
bool qa_launch_set_mode(qa_launch_draft *d, const qa_launch_mode *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_mode v = *input;
    v.instance = launch_text(d, input->instance, error); v.forced_team = launch_text(d, input->forced_team, error);
    if (!v.instance || !*v.instance || !v.forced_team) return false;
    for (size_t i = 0; i < 3; ++i) { v.teams[i] = launch_text(d, input->teams[i], error); if (!v.teams[i]) return false; }
    size_t n = d->choices.mode_count, i = 0;
    while (i < n && strcmp(d->choices.modes[i].instance, v.instance)) ++i;
    if (!launch_grow((void **)&d->choices.modes, &d->mode_capacity, i + 1, sizeof(v), error)) return false;
    if (i == n) ++d->choices.mode_count;
    ((qa_launch_mode *)d->choices.modes)[i] = v; return true;
}
bool qa_launch_remove_mode(qa_launch_draft *d, const char *name, qa_error *error)
{
    if (!valid_draft(d, name, error)) return false;
    qa_launch_mode *items = (qa_launch_mode *)d->choices.modes;
    for (size_t i = 0; i < d->choices.mode_count; ++i) if (!strcmp(items[i].instance, name)) {
        memmove(items + i, items + i + 1, (--d->choices.mode_count - i) * sizeof(*items)); break;
    }
    return true;
}
bool qa_launch_set_equipment(qa_launch_draft *d, const qa_launch_equipment *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_equipment v = *input;
    v.instance = launch_text(d, input->instance, error); v.grapple_source = launch_text(d, input->grapple_source, error);
    v.grenade_source = launch_text(d, input->grenade_source, error);
    if (!v.instance || !*v.instance || !v.grapple_source || !v.grenade_source) return false;
    UPSERT(equipment, equipment_capacity, qa_launch_equipment, launch_scope_equal(old->scope, v.scope) && !strcmp(old->instance, v.instance));
}
bool qa_launch_set_seat(qa_launch_draft *d, const qa_launch_seat *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_seat v = *input;
    v.name = launch_text(d, input->name, error); v.team = launch_text(d, input->team, error);
    if (!v.name || !v.team) return false;
    size_t n = d->choices.seat_count, i = 0;
    while (i < n && d->choices.seats[i].id != v.id) ++i;
    if (!launch_grow((void **)&d->choices.seats, &d->seat_capacity, i + 1, sizeof(v), error)) return false;
    if (i == n) ++d->choices.seat_count;
    ((qa_launch_seat *)d->choices.seats)[i] = v; return true;
}
bool qa_launch_remove_seat(qa_launch_draft *d, uint32_t id, qa_error *error)
{
    if (!d) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "seat removal requires a draft"); return false; }
    qa_launch_seat *items = (qa_launch_seat *)d->choices.seats;
    for (size_t i = 0; i < d->choices.seat_count; ++i) if (items[i].id == id) {
        memmove(items + i, items + i + 1, (--d->choices.seat_count - i) * sizeof(*items)); break;
    }
    return true;
}
bool qa_launch_set_loadout(qa_launch_draft *d, const qa_launch_loadout *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_loadout v = *input; v.item = launch_text(d, input->item, error);
    if (!v.item || !*v.item) return false;
    UPSERT(loadout, loadout_capacity, qa_launch_loadout, launch_scope_equal(old->scope, v.scope) && !strcmp(old->item, v.item));
}
bool qa_launch_set_monster(qa_launch_draft *d, const qa_launch_monster *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_monster v = *input;
    v.authored_classname = launch_text(d, input->authored_classname, error);
    v.instance = launch_text(d, input->instance, error); v.classname = launch_text(d, input->classname, error);
    if (!v.authored_classname || !v.instance || !v.classname) return false;
    size_t n = d->choices.monster_count, i = 0;
    while (i < n && strcmp(d->choices.monsters[i].authored_classname, v.authored_classname)) ++i;
    if (!launch_grow((void **)&d->choices.monsters, &d->monster_capacity, i + 1, sizeof(v), error)) return false;
    if (i == n) ++d->choices.monster_count;
    ((qa_launch_monster *)d->choices.monsters)[i] = v; return true;
}
#undef UPSERT

bool qa_launch_set_weapon_behavior(qa_launch_draft *d, const qa_launch_weapon_behavior *input, qa_error *error)
{
    if (!valid_draft(d, input, error)) return false;
    qa_launch_weapon_behavior v = *input;
    v.weapon = launch_text(d, input->weapon, error); v.instance = launch_text(d, input->instance, error);
    v.behavior = launch_text(d, input->behavior, error);
    if (!v.weapon || !v.instance || !v.behavior) return false;
    size_t n = d->choices.behavior_count, i = 0;
    while (i < n) {
        const qa_launch_weapon_behavior *old = &d->choices.behaviors[i];
        if (old->role == v.role && launch_scope_equal(old->scope, v.scope) && !strcmp(old->weapon, v.weapon)) break;
        ++i;
    }
    if (!launch_grow((void **)&d->choices.behaviors, &d->behavior_capacity, i + 1, sizeof(v), error)) return false;
    if (i == n) ++d->choices.behavior_count;
    ((qa_launch_weapon_behavior *)d->choices.behaviors)[i] = v; return true;
}

static bool rebase_product(const qa_catalog *source, const qa_catalog *target,
                            qa_product_id *id, qa_error *error)
{
    if (*id == 0 || source == target) return true;
    const qa_product *old = qa_catalog_product(source, *id);
    if (!old) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "launch choice has an unknown product");
        return false;
    }
    for (size_t i = 0; i < qa_catalog_count(target); ++i) {
        const qa_product *product = qa_catalog_at(target, i);
        if (!strcmp(old->identity, product->identity)) {
            *id = product->id;
            return true;
        }
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "selected product is missing from refreshed catalog: %s",
                 old->identity);
    return false;
}

bool qa_launch_draft_rebase(const qa_launch_draft *source, qa_catalog *catalog,
                            qa_launch_draft **out, qa_error *error)
{
    if (!source || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "cannot copy a missing launch draft"); return false; }
    qa_launch_draft *d;
    if (!launch_empty(catalog, &d, error)) return false;
    const qa_launch_choices *v = &source->choices;
    qa_launch_world world = v->world;
    if (!rebase_product(source->catalog, catalog, &world.preset, error) ||
        !rebase_product(source->catalog, catalog, &world.geometry, error) ||
        !rebase_product(source->catalog, catalog, &world.presentation, error) ||
        !rebase_product(source->catalog, catalog, &world.environment_product, error) ||
        !qa_launch_set_world(d, &world, error)) goto fail;
    for (size_t i = 0; i < v->provider_count; ++i) {
        qa_launch_provider provider = v->providers[i];
        if (!rebase_product(source->catalog, catalog, &provider.product, error) ||
            !qa_launch_set_provider(d, &provider, error)) goto fail;
    }
    for (size_t i = 0; i < v->binding_count; ++i) if (!qa_launch_bind(d, &v->bindings[i], error)) goto fail;
    for (size_t i = 0; i < v->mod_count; ++i) if (!qa_launch_set_mod(d, &v->mods[i], error)) goto fail;
    for (size_t i = 0; i < v->mode_count; ++i) if (!qa_launch_set_mode(d, &v->modes[i], error)) goto fail;
    for (size_t i = 0; i < v->equipment_count; ++i) if (!qa_launch_set_equipment(d, &v->equipment[i], error)) goto fail;
    for (size_t i = 0; i < v->seat_count; ++i) if (!qa_launch_set_seat(d, &v->seats[i], error)) goto fail;
    for (size_t i = 0; i < v->loadout_count; ++i) if (!qa_launch_set_loadout(d, &v->loadout[i], error)) goto fail;
    for (size_t i = 0; i < v->monster_count; ++i) if (!qa_launch_set_monster(d, &v->monsters[i], error)) goto fail;
    for (size_t i = 0; i < v->behavior_count; ++i) if (!qa_launch_set_weapon_behavior(d, &v->behaviors[i], error)) goto fail;
    *out = d; return true;
fail:
    qa_launch_draft_destroy(d); return false;
}

bool qa_launch_draft_copy(const qa_launch_draft *source, qa_launch_draft **out, qa_error *error)
{
    return qa_launch_draft_rebase(source, source ? source->catalog : NULL, out, error);
}

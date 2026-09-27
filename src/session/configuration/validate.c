#include "internal.h"
#include <math.h>

static bool fail(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false; }

static bool selected_product(const qa_launch_draft *d, qa_product_id id, qa_error *error)
{
    const qa_product *p = qa_catalog_product(d->catalog, id);
    if (!p) return fail(error, "launch references an unknown content product");
    if (p->availability == QA_CONTENT_INSTALLED) return true;
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "%s requires %s", p->key,
                 p->requirement_count ? p->requirements[0] : "installed content"); return false;
}

static bool path_valid(const char *path, qa_error *error)
{
    char *normalized = qa_archive_normalize_path(path, error);
    if (!normalized) return false;
    bool same = !strcmp(path, normalized);
    free(normalized);
    return same || fail(error, "launch resource paths must be normalized");
}

static bool enabled_mod(const qa_launch_choices *v, const char *key)
{
    for (size_t i = 0; i < v->mod_count; ++i)
        if (v->mods[i].enabled && !strcmp(v->mods[i].component, key)) return true;
    return false;
}

static bool mod_dependencies(const qa_launch_draft *d, qa_error *error)
{
    const qa_launch_choices *v = &d->choices;
    for (size_t i = 0; i < v->mod_count; ++i) {
        const qa_launch_mod_selection *selection = &v->mods[i];
        if (!selection->enabled) continue;
        const qa_catalog_mod *mod = qa_catalog_mod_find(d->catalog, selection->component);
        if (!mod || mod->unavailable) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "%s is unavailable: %s", selection->component,
                         mod ? mod->unavailable : "component is not installed"); return false;
        }
        if (mod->purpose != QA_MOD_ADDITION) return fail(error, "game-type components require an explicit primary provider selection");
        if (launch_provider(v, selection->instance)) return fail(error, "mod and primary provider cannot share an instance identity");
        for (size_t j = 0; j < mod->requires_count; ++j) if (!enabled_mod(v, mod->requires[j])) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s requires enabled component %s", mod->key, mod->requires[j]); return false;
        }
        for (size_t j = 0; j < mod->conflicts_count; ++j) if (enabled_mod(v, mod->conflicts[j])) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s conflicts with %s", mod->key, mod->conflicts[j]); return false;
        }
    }
    return true;
}

static bool validate_equipment(const qa_launch_draft *d, const qa_launch_equipment *e, qa_error *error)
{
    if (!launch_scope_valid(e->scope) || !launch_provider(&d->choices, e->instance)) return fail(error, "equipment requires a selected instance and valid scope");
    if ((unsigned)e->selection.grapple > QA_GRAPPLE_Q3 || (unsigned)e->selection.binding > QA_EQUIPMENT_WEAPON_SLOT)
        return fail(error, "unknown grapple mechanic or binding");
    const qa_launch_provider *hook = launch_provider(&d->choices, e->grapple_source);
    if (e->selection.grapple != QA_GRAPPLE_DISABLED) {
        if (!hook) return fail(error, "grapple has no selected source provider");
        const qa_product *p = qa_catalog_product(d->catalog, hook->product);
        if (hook->runtime == QA_PROGRAM_BUILTIN) {
            bool valid = e->selection.grapple == QA_GRAPPLE_THREEWAVE ? p->family == QA_GAME_Q1 && !strcmp(p->campaign, "ctf")
                : e->selection.grapple == QA_GRAPPLE_ROGUE ? p->family == QA_GAME_Q1 && !strcmp(p->campaign, "rogue")
                : e->selection.grapple == QA_GRAPPLE_Q2_CTF ? p->family == QA_GAME_Q2 && (!strcmp(p->campaign, "ctf") || p->edition == QA_EDITION_RERELEASE)
                : e->selection.grapple == QA_GRAPPLE_LMCTF ? p->family == QA_GAME_Q2 && !strcmp(p->campaign, "lmctf")
                : p->family == QA_GAME_Q3;
            if (!valid) return fail(error, "selected native content does not provide this grapple mechanic");
        }
    }
    if (e->selection.grenades.enabled) {
        const qa_launch_provider *grenade = launch_provider(&d->choices, e->grenade_source);
        if (!grenade) return fail(error, "offhand grenades have no selected source provider");
        const qa_product *p = qa_catalog_product(d->catalog, grenade->product);
        if (grenade->runtime == QA_PROGRAM_BUILTIN && p->family != QA_GAME_Q2) return fail(error, "native offhand grenades require Q2 content");
        if (e->selection.grenades.initial_ammo < 0 || e->selection.grenades.capacity < e->selection.grenades.initial_ammo)
            return fail(error, "offhand grenade allowance exceeds its capacity");
    }
    return true;
}

bool qa_launch_validate(const qa_launch_draft *d, qa_error *error)
{
    if (!d) return fail(error, "missing launch draft");
    const qa_launch_choices *v = &d->choices;
    if (!selected_product(d, v->world.preset, error) || !selected_product(d, v->world.geometry, error) ||
        !selected_product(d, v->world.presentation, error) || !path_valid(v->world.map, error)) return false;
    if ((unsigned)v->world.environment > QA_ENVIRONMENT_SELECTED || v->world.skill < 0)
        return fail(error, "invalid launch environment or skill");
    if (v->world.environment == QA_ENVIRONMENT_SELECTED &&
        (!selected_product(d, v->world.environment_product, error) || !path_valid(v->world.environment_path, error))) return false;
    for (size_t i = 0; i < v->provider_count; ++i) {
        const qa_launch_provider *p = &v->providers[i];
        if (!selected_product(d, p->product, error)) return false;
        if ((unsigned)p->runtime > QA_PROGRAM_NATIVE || (unsigned)p->clock.kind > QA_CLOCK_Q3 || !*p->implementation)
            return fail(error, "invalid selected provider implementation or clock");
        if (p->runtime != QA_PROGRAM_BUILTIN && !path_valid(p->artifact, error)) return false;
        if (p->runtime == QA_PROGRAM_BUILTIN && *p->artifact) return fail(error, "built-in providers do not execute game modules");
        if (*p->component) {
            const qa_catalog_mod *m = qa_catalog_mod_find(d->catalog, p->component);
            if (!m || m->unavailable || m->product != p->product || m->runtime != p->runtime || strcmp(m->program_path, p->artifact))
                return fail(error, "provider differs from its selected authored component");
        }
        if (!p->clock.interval_ns && p->clock.kind != QA_CLOCK_NETQUAKE && p->clock.kind != QA_CLOCK_QUAKEWORLD)
            return fail(error, "fixed source clock needs a nonzero interval");
        if (p->clock.maximum_frame_ns && p->clock.maximum_frame_ns < p->clock.minimum_frame_ns)
            return fail(error, "source clock maximum is below its minimum");
    }
    for (size_t i = 0; i < v->binding_count; ++i) {
        const qa_launch_binding *b = &v->bindings[i];
        if (!launch_provider(v, b->instance) || !launch_scope_valid(b->scope) || (unsigned)b->role >= QA_ROLE_COUNT)
            return fail(error, "role binding references an absent provider or invalid scope");
        if (b->scope.kind == QA_SCOPE_SEAT) {
            bool found = false;
            for (size_t j = 0; j < v->seat_count; ++j) if (v->seats[j].id == b->scope.seat) found = true;
            if (!found) return fail(error, "role binding references an absent seat");
        }
    }
    qa_launch_scope world = {.kind = QA_SCOPE_WORLD}, player = {.kind = QA_SCOPE_DEFAULT_PLAYER};
    if (!qa_launch_binding_for(v, world, QA_ROLE_ENTITIES, "")) return fail(error, "map entities need one selected owner");
    if (v->world.campaign && (!qa_launch_binding_for(v, world, QA_ROLE_CAMPAIGN, "") || !qa_launch_binding_for(v, world, QA_ROLE_TRANSITION, "")))
        return fail(error, "campaign progression requires mission and transition ownership");
    static const qa_launch_role required[] = {QA_ROLE_MOVEMENT, QA_ROLE_CHARACTER, QA_ROLE_ARSENAL, QA_ROLE_COMBAT, QA_ROLE_INVENTORY, QA_ROLE_PICKUPS};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        if (!qa_launch_binding_for(v, player, required[i], "")) return fail(error, "default player is missing an independent provider choice");
    size_t primary = 0;
    for (size_t i = 0; i < v->mode_count; ++i) {
        const qa_launch_mode *m = &v->modes[i];
        if (!launch_provider(v, m->instance) || (unsigned)m->rules.source > QA_MODE_TEAM_ARENA || (unsigned)m->rules.kind > QA_MODE_HORDE)
            return fail(error, "invalid selected mode instance");
        if (m->rules.enabled && m->primary_score) ++primary;
        for (size_t j = 0; j < 3; ++j) {
            if (m->rules.teams[j]) return fail(error, "launch teams use persistent names; intern them at session publication");
            for (size_t k = 0; k < j; ++k) if (*m->teams[j] && !strcmp(m->teams[j], m->teams[k])) return fail(error, "mode has repeated team identities");
        }
        if (m->rules.forced_team) return fail(error, "forced launch team uses its persistent name");
    }
    if (primary != 1) return fail(error, "select exactly one ordinary frag scoring owner");
    for (size_t i = 0; i < v->equipment_count; ++i) if (!validate_equipment(d, &v->equipment[i], error)) return false;
    for (size_t i = 0; i < v->seat_count; ++i) {
        const qa_launch_seat *s = &v->seats[i];
        if (!isfinite(s->bot_skill) || s->bot_skill < 0) return fail(error, "invalid seat bot skill");
        if (s->bot && s->local) return fail(error, "a bot is not a local input seat");
        for (size_t j = 0; j < i; ++j) {
            const qa_launch_seat *other = &v->seats[j];
            if (s->actor.registry && launch_scope_equal((qa_launch_scope){.kind = QA_SCOPE_ACTOR, .actor = s->actor},
                (qa_launch_scope){.kind = QA_SCOPE_ACTOR, .actor = other->actor})) return fail(error, "two seats cannot own the same actor");
            if (s->local && other->local && s->input_device && s->input_device == other->input_device)
                return fail(error, "local seats cannot claim the same input device");
        }
    }
    for (size_t i = 0; i < v->loadout_count; ++i) {
        const qa_launch_loadout *l = &v->loadout[i];
        if (!launch_scope_valid(l->scope) || !strchr(l->item, ':') || l->quantity < 0 ||
            (l->override_capacity && (l->capacity < 0 || l->quantity > l->capacity))) return fail(error, "invalid canonical loadout grant or capacity");
    }
    for (size_t i = 0; i < v->monster_count; ++i) {
        const qa_launch_monster *m = &v->monsters[i];
        if (!m->map_defined && (!*m->classname || !launch_provider(v, m->instance))) return fail(error, "monster replacement needs a definition and provider");
    }
    for (size_t i = 0; i < v->behavior_count; ++i) {
        const qa_launch_weapon_behavior *s = &v->behaviors[i];
        if (!launch_scope_valid(s->scope) || (unsigned)s->role > QA_BUILTIN_GRAPPLE ||
            (*s->weapon && !strchr(s->weapon, ':'))) return fail(error, "invalid trajectory selection scope or weapon identity");
        if (!s->enabled) continue;
        const qa_launch_provider *p = launch_provider(v, s->instance);
        const qa_catalog_weapon_behavior *b = p ? qa_catalog_weapon_behavior_find(d->catalog, p->product, s->behavior) : NULL;
        if (!b || b->unavailable || b->role != s->role || b->runtime != p->runtime ||
            strcmp(b->artifact_path, p->artifact)) return fail(error, "selected trajectory does not match its source program and projectile role");
    }
    return mod_dependencies(d, error);
}

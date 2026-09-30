#include "internal.h"
#include <stdio.h>

static const qa_product *selection(qa_catalog *catalog, const char *name)
{
    const qa_product *product = qa_catalog_find(catalog, name);
    if (product) return product;
    qa_game_family family;
    if (!strcmp(name, "q1") || !strcmp(name, "qw")) family = QA_GAME_Q1;
    else if (!strcmp(name, "q2")) family = QA_GAME_Q2;
    else if (!strcmp(name, "q3")) family = QA_GAME_Q3;
    else return NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        product = qa_catalog_at(catalog, i);
        bool edition = !strcmp(name, "qw") ? product->edition == QA_EDITION_QUAKEWORLD : product->edition == QA_EDITION_CLASSIC;
        if (product->family == family && edition && product->builtin && product->availability == QA_CONTENT_INSTALLED) return product;
    }
    return NULL;
}
static bool overlay(qa_launch_draft *draft, const char *name, uint64_t roles, const char *instance, qa_error *error)
{
    qa_catalog *catalog = qa_launch_draft_catalog(draft);
    const qa_product *product = selection(catalog, name);
    if (!product) return frontend_fail(error, QA_ERROR_ARGUMENT, "selected source product is not installed");
    qa_launch_draft *source = NULL;
    if (!qa_launch_draft_create(catalog, product->id, "", &source, error)) return false;
    const qa_launch_choices *choices = qa_launch_draft_choices(source);
    bool ok = true;
    for (size_t i = 0; i < choices->binding_count && ok; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (!(roles & QA_ROLE_BIT(binding->role))) continue;
        const qa_launch_provider *provider = NULL;
        for (size_t j = 0; j < choices->provider_count; ++j)
            if (!strcmp(choices->providers[j].instance, binding->instance)) provider = &choices->providers[j];
        if (!provider) { ok = frontend_fail(error, QA_ERROR_ARGUMENT, "source preset lacks selected role provider"); break; }
        qa_launch_provider selected = *provider;
        selected.instance = instance;
        qa_launch_binding route = *binding;
        route.instance = instance;
        ok = qa_launch_set_provider(draft, &selected, error) && qa_launch_bind(draft, &route, error);
    }
    qa_launch_draft_destroy(source);
    return ok;
}
bool frontend_launch(qa_frontend *frontend, qa_error *error)
{
    if (!frontend->options.game) return true;
    qa_catalog *catalog = qa_application_catalog(frontend->application);
    const qa_product *product = selection(catalog, frontend->options.game);
    if (!product || product->availability != QA_CONTENT_INSTALLED)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "launch product is not installed");
    const char *map = frontend->options.map;
    const qa_catalog_start *starts = NULL;
    const qa_catalog_episode *episode = NULL;
    size_t count = 0;
    if (!map) {
        starts = qa_catalog_starts(catalog, product->id, &episode, &count);
        if (count) map = starts[0].path;
        else {
            const qa_catalog_map *maps = qa_catalog_maps(catalog, product->id, &count);
            if (count) map = maps[0].path;
        }
    }
    if (!map) return frontend_fail(error, QA_ERROR_ARGUMENT, "launch product has no installed map");
    qa_launch_draft *draft = NULL;
    if (!qa_launch_draft_create(catalog, product->id, map, &draft, error)) return false;
    qa_launch_world world = qa_launch_draft_choices(draft)->world;
    if (starts && count) world.start_command = episode && episode->command && *episode->command ? episode->command : starts[0].bsp;
    bool ok = true;
    if (frontend->options.map_game) {
        const qa_product *geometry = selection(catalog, frontend->options.map_game);
        if (!geometry || geometry->availability != QA_CONTENT_INSTALLED) ok = frontend_fail(error, QA_ERROR_ARGUMENT, "map source is not installed");
        else { world.geometry = geometry->id; world.start_command = NULL; }
    }
    if (ok) ok = qa_launch_set_world(draft, &world, error);
    if (ok && frontend->options.movement) ok = overlay(draft, frontend->options.movement,
        QA_ROLE_BIT(QA_ROLE_MOVEMENT), "frontend:movement", error);
    if (ok && frontend->options.character) ok = overlay(draft, frontend->options.character,
        QA_ROLE_BIT(QA_ROLE_CHARACTER) | QA_ROLE_BIT(QA_ROLE_BODY) | QA_ROLE_BIT(QA_ROLE_SKIN) | QA_ROLE_BIT(QA_ROLE_VOICE), "frontend:character", error);
    for (size_t i = 0; i < frontend->options.mod_count && ok; ++i) {
        char instance[64];
        snprintf(instance, sizeof(instance), "frontend:addon:%zu", i);
        ok = qa_launch_set_mod(draft, &(qa_launch_mod_selection){.instance = instance,
            .component = frontend->options.mods[i], .enabled = true}, error);
    }
    for (unsigned i = 0; i < frontend->options.seats && !frontend->options.dedicated && ok; ++i) {
        char name[32]; snprintf(name, sizeof(name), "Player %u", i + 1);
        ok = qa_launch_set_seat(draft, &(qa_launch_seat){.id = i, .name = name, .local = true,
            .input_device = i}, error);
    }
    if (ok) ok = qa_application_apply(frontend->application, draft, error);
    qa_launch_draft_destroy(draft);
    return ok;
}

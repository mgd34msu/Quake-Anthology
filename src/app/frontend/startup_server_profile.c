#include "startup_server_profile.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }

static bool alnum_ascii(unsigned char c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

static bool profile_path(const char *relative)
{
    if (!relative || strncmp(relative, "servers/", 8)) return false;
    const char *name = relative + 8;
    size_t length = strlen(name);
    if (length < 6 || length > 37 || strcmp(name + length - 5, ".json") ||
        !alnum_ascii((unsigned char)name[0])) return false;
    for (size_t i = 1; i < length - 5; ++i)
        if (!alnum_ascii((unsigned char)name[i]) && name[i] != '_' && name[i] != '-') return false;
    return true;
}

bool frontend_startup_server_profile_select(const char *relative, char **retained, qa_error *error)
{
    if (!retained) return fail(error, QA_ERROR_ARGUMENT, "Missing server profile selection owner");
    char *copy = NULL;
    if (relative) {
        if (!profile_path(relative)) return fail(error, QA_ERROR_ARGUMENT, "Invalid server profile settings path");
        size_t size = strlen(relative) + 1;
        copy = malloc(size);
        if (!copy) return fail(error, QA_ERROR_MEMORY, "Retaining selected server profile path");
        memcpy(copy, relative, size);
    }
    free(*retained);
    *retained = copy;
    return true;
}

static const qa_launch_provider *role(const qa_launch_choices *choices,
    qa_launch_scope_kind scope, qa_launch_role wanted)
{
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = scope}, wanted, "");
    for (size_t i = 0; binding && i < choices->provider_count; ++i)
        if (!strcmp(choices->providers[i].instance, binding->instance)) return choices->providers + i;
    return NULL;
}

static bool selection(qa_catalog *catalog, const qa_launch_choices *choices,
    qa_server_setting_selection *out, qa_error *error)
{
    const qa_launch_provider *entities = role(choices, QA_SCOPE_WORLD, QA_ROLE_ENTITIES);
    const qa_launch_provider *match = role(choices, QA_SCOPE_WORLD, QA_ROLE_MODE);
    const qa_launch_provider *combat = role(choices, QA_SCOPE_DEFAULT_PLAYER, QA_ROLE_COMBAT);
    const qa_product *source = entities ? qa_catalog_product(catalog, entities->product) : NULL;
    const qa_product *match_product = match ? qa_catalog_product(catalog, match->product) : NULL;
    const qa_product *combat_product = combat ? qa_catalog_product(catalog, combat->product) : NULL;
    if (!source) return fail(error, QA_ERROR_ARGUMENT, "Server profile requires selected WORLD ENTITIES");
    qa_server_setting_selection selected = {.source = QA_SERVER_SOURCE_NONE,
        .match = QA_MODE_Q1, .native_combat = combat_product && combat_product->family == source->family};
    const char *campaign = source->campaign ? source->campaign : "";
    switch (source->family) {
    case QA_GAME_Q1:
        selected.source = !strcmp(campaign, "rogue") ? QA_SERVER_SOURCE_Q1_ROGUE :
            !strcmp(campaign, "ctf") ? QA_SERVER_SOURCE_Q1_CTF : QA_SERVER_SOURCE_Q1;
        break;
    case QA_GAME_Q2:
        selected.source = source->edition == QA_EDITION_RERELEASE ? QA_SERVER_SOURCE_Q2_RERELEASE : QA_SERVER_SOURCE_Q2;
        break;
    case QA_GAME_Q3:
        selected.source = match_product && match_product->campaign && !strcmp(match_product->campaign, "missionpack") ?
            QA_SERVER_SOURCE_TEAM_ARENA : QA_SERVER_SOURCE_Q3;
        break;
    }
    for (size_t i = 0; match && i < choices->mode_count; ++i)
        if (!strcmp(choices->modes[i].instance, match->instance)) { selected.match = choices->modes[i].rules.source; break; }
    *out = selected;
    return true;
}

bool frontend_startup_server_profile_capture(qa_settings_store store, const char *relative,
    const qa_launch_draft *draft, qa_server_profile **out, qa_error *error)
{
    if (!draft || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "Server profile requires a copied launch draft and empty ticket");
    if (!relative) return true;
    if (!profile_path(relative)) return fail(error, QA_ERROR_ARGUMENT, "Invalid selected server profile settings path");
    qa_server_setting_selection selected;
    if (!selection(qa_launch_draft_catalog(draft), qa_launch_draft_choices(draft), &selected, error)) return false;
    bool found;
    if (!qa_settings_load_server_profile(store, relative, selected, out, &found, error)) return false;
    return found || fail(error, QA_ERROR_NOT_FOUND, "Selected server profile no longer exists");
}

bool frontend_startup_server_profile_apply(const qa_server_profile *profile,
    const qa_launch_snapshot *candidate, const qa_application_startup_source *source,
    const qa_server_profile_owner *owner, qa_error *error)
{
    if (!profile) return true;
    if (!candidate || !source || !source->descriptor || !source->cvars)
        return fail(error, QA_ERROR_ARGUMENT, "Server profile requires the actual fresh Source tuple");
    switch (source->scope.kind) {
    case QA_APPLICATION_CONSOLE_ENGINE: case QA_APPLICATION_CONSOLE_CLIENT:
    case QA_APPLICATION_CONSOLE_Q3_CGAME: case QA_APPLICATION_CONSOLE_Q3_UI:
        return true;
    case QA_APPLICATION_CONSOLE_QC: case QA_APPLICATION_CONSOLE_NATIVE_Q2:
    case QA_APPLICATION_CONSOLE_Q3_GAME: case QA_APPLICATION_CONSOLE_Q1_GAME:
    case QA_APPLICATION_CONSOLE_Q2_GAME:
        break;
    }
    const qa_launch_choices *choices = qa_launch_snapshot_choices(candidate);
    const qa_launch_provider *primary = role(choices, QA_SCOPE_WORLD, QA_ROLE_ENTITIES);
    if (!primary) return fail(error, QA_ERROR_ARGUMENT, "Server profile lost its primary Source selection");
    if (strcmp(primary->instance, source->descriptor->selection.instance)) return true;
    if (primary->product != source->descriptor->selection.product)
        return fail(error, QA_ERROR_ARGUMENT, "Server profile Source lost its selected product");
    qa_server_setting_selection selected;
    if (!selection(qa_launch_snapshot_catalog(candidate), choices, &selected, error)) return false;
    return qa_server_profile_apply_startup(profile, selected, owner, error);
}

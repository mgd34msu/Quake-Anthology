#include "client_events.h"
#include "map_players_private.h"
#include "guest_qc_profile.h"
#include "guest_q3_private.h"
#include "guest_native_q2_private.h"
#include "guest_q3_components.h"
#include "equipment_runtime.h"
#include "character_selection.h"
#include "native_q3_settings.h"
#include "qa/game_q3_client.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_type.h"
#include <stdlib.h>

typedef enum client_listener_kind {
    CLIENT_LISTENER_QC, CLIENT_LISTENER_Q3, CLIENT_LISTENER_NATIVE_Q2,
    CLIENT_LISTENER_COMPONENT, CLIENT_LISTENER_GEAR
} client_listener_kind;

typedef struct client_listener {
    client_listener_kind kind;
    application_provider *provider;
    application_q3_component *component;
    application_equipment_runtime_source gear;
    void *engine;
    qa_actor_owner owner;
    uint64_t identity;
    uint32_t slot;
    size_t component_index;
} client_listener;

static bool gear_current(qa_application *app, const client_listener *row,
    qa_actor_id actor, qa_error *error)
{
    application_equipment_runtime_source current;
    bool bound = false;
    if (app->equipment_runtime != row->engine ||
        !application_equipment_runtime_source_at(app->equipment_runtime,
            row->component_index, &current, error) ||
        current.selected_owner != row->gear.selected_owner ||
        !application_equipment_runtime_source_current(app->equipment_runtime, &row->gear) ||
        !application_q3_gear_idle(current.gear) ||
        !application_q3_gear_userinfo_bound(current.gear, actor, &bound, error) || !bound)
        return error && error->code != QA_OK ? false : application_fail(error,
            QA_ERROR_ARGUMENT, "Gear listener lost its actual admitted client or Source owner");
    return true;
}

static application_player_record *player_record(qa_application *app, qa_actor_id actor)
{
    if (!app || !app->players || !app->session ||
        !qa_actors_get(qa_session_actors(app->session), actor)) return NULL;
    application_player_record *found = NULL;
    for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record *row = app->players->records + i;
        if (!qa_actor_id_equal(row->actor, actor) || row->retiring) continue;
        if (found) return NULL;
        found = row;
    }
    return found;
}

static application_player_record *player(qa_application *app, qa_actor_id actor)
{
    application_player_record *row = player_record(app, actor);
    return row && !row->source_begin_pending ? row : NULL;
}

static bool userinfo_has(const char *text, const char *key)
{
    if (*text == '\\') ++text;
    while (*text) {
        const char *name = text;
        while (*text && *text != '\\') ++text;
        size_t length = (size_t)(text - name), i = 0;
        if (!*text) break;
        while (i < length && key[i]) {
            unsigned char byte = (unsigned char)name[i];
            if (byte >= 'A' && byte <= 'Z') byte = (unsigned char)(byte + ('a' - 'A'));
            if (byte != (unsigned char)key[i]) break;
            ++i;
        }
        if (i == length && !key[i]) return true;
        ++text;
        while (*text && *text != '\\') ++text;
        if (*text) ++text;
    }
    return false;
}

bool application_client_native_q3_userinfo(qa_application *app, application_provider *provider,
    qa_actor_id actor, const char *userinfo, int32_t game_type, qa_error *error)
{
    application_player_record *actual = player_record(app, actor);
    const qa_launch_snapshot *snapshot = qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    qa_application_character_declaration declaration;
    bool found = false;
    for (size_t i = 0; actual && choices && i < choices->seat_count; ++i) {
        if (choices->seats[i].id != actual->seat) continue;
        qa_launch_seat seat = choices->seats[i];
        seat.actor = actual->configured_actor.registry ? actual->configured_actor : actual->actor;
        if (!qa_application_character_declaration_read(qa_launch_snapshot_catalog(snapshot), choices,
            &seat, &declaration, &found, error)) return false;
        break;
    }
    if (!found || declaration.family != QA_GAME_Q3)
        return qa_q3_client_selected_presentation(provider->state.q3, actor, userinfo, game_type, error);
    const qa_native_q3_character_declaration *appearance = &declaration.appearance;
    const char *keys[2] = {qa_game_type_is_team(game_type) ? "team_model" : "model",
        qa_game_type_is_team(game_type) ? "team_headmodel" : "headmodel"};
    const char *models[2] = {appearance->model,
        *appearance->head_model ? appearance->head_model : appearance->model};
    const char *skins[2] = {appearance->skin, appearance->head_skin};
    bool missing[2] = {!userinfo_has(userinfo, keys[0]), !userinfo_has(userinfo, keys[1])};
    if (!missing[0] && !missing[1])
        return qa_q3_client_selected_presentation(provider->state.q3, actor, userinfo, game_type, error);
    size_t length = strlen(userinfo), capacity = length + 2;
    for (size_t i = 0; i < 2; ++i)
        if (missing[i]) capacity += strlen(keys[i]) + strlen(models[i]) + strlen(skins[i]) + 3;
    char *selected = malloc(capacity);
    if (!selected) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected Q3 client userinfo");
    size_t at = 0;
    for (size_t i = 0; i < 2; ++i)
        if (missing[i]) at += (size_t)snprintf(selected + at, capacity - at,
            "\\%s\\%s/%s", keys[i], models[i], skins[i]);
    if (length && userinfo[0] != '\\') selected[at++] = '\\';
    memcpy(selected + at, userinfo, length + 1);
    bool okay = qa_q3_client_selected_presentation(provider->state.q3, actor, selected, game_type, error);
    free(selected); return okay;
}

bool application_client_userinfo_publish(qa_application *app, qa_actor_id actor,
    const char *text, qa_error *error)
{
    application_player_record *row = player_record(app, actor);
    application_provider *source = app && app->players ? app->players->map_provider : NULL;
    if (!text || !row || !source || source->application != app ||
        !source->constructed || !source->attached || source->close_pending ||
        source != application_world_provider(app, QA_ROLE_ENTITIES, ""))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Userinfo publication requires its live canonical Source client");
    if (text != row->userinfo) {
        size_t size = strlen(text) + 1;
        char *copy = malloc(size);
        if (!copy) return application_fail(error, QA_ERROR_MEMORY,
            "Retaining returned Source client userinfo");
        memcpy(copy, text, size);
        free(row->userinfo); row->userinfo = copy;
    }
    return row->deferred || row->source_begin_pending ||
        application_client_userinfo_changed(app, actor, error);
}

static bool provider_current(const qa_application *app, const client_listener *row)
{
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p != row->provider) continue;
        return p->application == app && p->constructed && p->attached &&
            !p->close_pending && p->launch && p->owner == row->owner &&
            p->launch->identity == row->identity;
    }
    return false;
}

static bool qc_binding_current(const client_listener *row, qa_actor_id actor, bool spawned)
{
    application_provider *p = row->provider;
    struct application_qc_state *engine = p->state.qc.engine;
    if (engine != row->engine || !engine || engine->provider != p || !engine->initialized ||
        !row->slot || row->slot > engine->max_clients ||
        !qa_qc_idle(p->state.qc.instance)) return false;
    const application_qc_client *client = engine->clients + row->slot;
    return client->connected && client->spawned == spawned &&
        qa_actor_id_equal(client->actor, actor);
}

static bool binding_current(const client_listener *row, qa_actor_id actor)
{
    application_provider *p = row->provider;
    if (row->kind == CLIENT_LISTENER_QC) return qc_binding_current(row, actor, true);
    if (row->kind == CLIENT_LISTENER_NATIVE_Q2) {
        struct application_native_q2 *engine = p->state.native.q2_engine;
        if (engine != row->engine || !engine || engine->provider != p || !engine->initialized ||
            engine->shutting_down || engine->calls || !row->slot || row->slot >= 257)
            return false;
        const application_native_q2_client *client = engine->clients + row->slot;
        return client->connected && client->begun && !client->disconnect_started &&
            qa_actor_id_equal(client->actor, actor);
    }
    struct application_q3_guest *engine = q3g_engine(p);
    if (engine != row->engine || !engine || engine->provider != p || engine->calls || engine->restore_pending ||
        !engine->game || !engine->game->initialized || engine->game->retired ||
        row->slot >= 64) return false;
    const q3g_client *client = engine->clients + row->slot;
    return client->connected && client->begun && !client->disconnect_started &&
        !client->disconnect_pending && qa_actor_id_equal(client->actor, actor);
}

static bool provider_listener(application_provider *p, qa_actor_id actor,
    client_listener *out, bool *found, qa_error *error)
{
    *found = false;
    if (!p) return application_fail(error, QA_ERROR_ARGUMENT, "Client listener roster has no physical provider");
    client_listener row = {.provider = p};
    if (p->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = p->state.qc.engine;
        if (!engine || !engine->clients) return true;
        row.kind = CLIENT_LISTENER_QC; row.engine = engine;
        for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
            if (!engine->clients[slot].connected ||
                !qa_actor_id_equal(engine->clients[slot].actor, actor)) continue;
            if (*found) return application_fail(error, QA_ERROR_FORMAT, "QC client actor has duplicate physical slots");
            row.slot = slot; *found = true;
        }
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && p->state.native.q2_engine) {
        struct application_native_q2 *engine = p->state.native.q2_engine;
        row.kind = CLIENT_LISTENER_NATIVE_Q2; row.engine = engine;
        for (uint32_t slot = 1; slot < 257; ++slot) {
            if (!engine->clients[slot].connected ||
                !qa_actor_id_equal(engine->clients[slot].actor, actor)) continue;
            if (*found) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client actor has duplicate physical slots");
            row.slot = slot; *found = true;
        }
    } else if (p->kind == APPLICATION_PROVIDER_QVM || p->kind == APPLICATION_PROVIDER_NATIVE) {
        struct application_q3_guest *engine = q3g_engine(p);
        if (!engine || !engine->game) return true;
        row.kind = CLIENT_LISTENER_Q3; row.engine = engine;
        for (uint32_t slot = 0; slot < 64; ++slot) {
            if (!engine->clients[slot].connected ||
                !qa_actor_id_equal(engine->clients[slot].actor, actor)) continue;
            if (*found) return application_fail(error, QA_ERROR_FORMAT, "Q3 client actor has duplicate physical slots");
            row.slot = slot; *found = true;
        }
    }
    if (!*found) return true;
    if (!p->constructed || !p->attached || p->close_pending || !p->launch)
        return application_fail(error, QA_ERROR_ARGUMENT, "Client listener provider has left its physical lifetime");
    row.owner = p->owner; row.identity = p->launch->identity;
    if (!binding_current(&row, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Userinfo listener has not returned from its actual client admission");
    *out = row; return true;
}

bool application_client_userinfo_changed(qa_application *app, qa_actor_id actor, qa_error *error)
{
    application_player_record *actual = player(app, actor);
    if (!actual || !actual->userinfo || !app->players->map_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Userinfo notification requires the actual admitted Source client");
    const qa_launch_snapshot *launch = qa_application_launch(app);
    const uint64_t generation = app->publication_generation;
    application_provider *source = app->players->map_provider;
    size_t components = application_q3_components_count(app);
    size_t gears = application_equipment_runtime_source_count(app->equipment_runtime);
    if (components > SIZE_MAX - app->provider_count ||
        gears > SIZE_MAX - components - app->provider_count ||
        gears + components + app->provider_count > SIZE_MAX / sizeof(client_listener))
        return application_fail(error, QA_ERROR_MEMORY, "Client listener snapshot exceeds its retained extent");
    size_t capacity = gears + components + app->provider_count, count = 0;
    client_listener *rows = capacity ? calloc(capacity, sizeof(*rows)) : NULL;
    if (capacity && !rows)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining the physical client listener order");
    bool ok = true;
    for (size_t i = 0; ok && i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p != source && p->kind == APPLICATION_PROVIDER_Q3) {
            uint32_t slot;
            if (qa_q3_native_client_slot(p->state.q3, actor, &slot, NULL)) {
                int32_t game_type;
                ok = application_native_q3_settings_integer(p, "g_gametype", &game_type, error) &&
                    application_client_native_q3_userinfo(app, p, actor,
                        actual->userinfo, game_type, error);
            }
            continue;
        }
        if (p == source || (p && p->kind <= APPLICATION_PROVIDER_Q3)) continue;
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j) duplicate |= rows[j].provider == p;
        if (duplicate) continue;
        bool found;
        ok = provider_listener(p, actor, rows + count, &found, error);
        if (ok && found) ++count;
    }
    for (size_t i = 0; ok && i < components; ++i) {
        application_q3_component *component; uint32_t maximum;
        const char *entity, *client;
        ok = application_q3_components_at(app, i, &component, error);
        if (ok && application_q3_mod_clients(application_q3_component_profile(component),
                &maximum, &entity, &client)) {
            bool bound = false;
            ok = application_q3_component_client_bound(component, actor, &bound, error);
            if (ok && bound) rows[count++] = (client_listener){.kind = CLIENT_LISTENER_COMPONENT,
                .component = component, .component_index = i};
        }
    }
    for (size_t i = 0; ok && i < gears; ++i) {
        application_equipment_runtime_source gear;
        ok = application_equipment_runtime_source_at(app->equipment_runtime, i, &gear, error);
        if (!ok || !gear.gear) continue;
        bool bound = false;
        ok = application_equipment_runtime_owner_current(app->equipment_runtime, gear.selected_owner) &&
            application_q3_gear_idle(gear.gear) &&
            application_q3_gear_userinfo_bound(gear.gear, actor, &bound, error);
        if (!ok && (!error || error->code == QA_OK))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Gear listener has not returned from its physical admission");
        if (ok && bound) rows[count++] = (client_listener){.kind = CLIENT_LISTENER_GEAR,
            .gear = gear, .engine = app->equipment_runtime, .component_index = i};
    }
    qa_launch_snapshot_retain(launch);
    for (size_t i = 0; ok && i < count; ++i) {
        client_listener *row = rows + i;
        actual = player(app, actor);
        if (!actual || !actual->userinfo || qa_application_launch(app) != launch ||
            app->publication_generation != generation || app->players->map_provider != source) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Client notification lost its actual Source publication");
            break;
        }
        if (row->kind == CLIENT_LISTENER_GEAR) {
            ok = gear_current(app, row, actor, error) &&
                application_q3_gear_userinfo_changed(row->gear.gear, actor, error) &&
                gear_current(app, row, actor, error);
        } else if (row->kind == CLIENT_LISTENER_COMPONENT) {
            application_q3_component *current;
            ok = application_q3_components_at(app, row->component_index, &current, error) &&
                current == row->component && application_q3_component_client_current(current, actor);
            if (!ok && (!error || error->code == QA_OK))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Client component listener changed its physical owner");
            if (ok) ok = application_q3_component_userinfo(current, actor, error);
            if (ok && !application_q3_component_client_current(current, actor))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Component listener replaced its admitted client");
        } else if (!provider_current(app, row) || !binding_current(row, actor)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Client listener changed its admitted actor binding");
        } else if (row->kind == CLIENT_LISTENER_QC) {
            ok = application_qc_client_userinfo(row->provider, actor, error);
        } else if (row->kind == CLIENT_LISTENER_NATIVE_Q2) {
            ok = application_native_q2_client_userinfo(row->provider, row->slot, actual->userinfo, error);
        } else {
            ok = application_q3_guest_client_userinfo(row->provider, row->slot, actual->userinfo, error);
        }
        if (ok && row->kind != CLIENT_LISTENER_COMPONENT && row->kind != CLIENT_LISTENER_GEAR &&
            (!provider_current(app, row) || !binding_current(row, actor)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Client listener replaced its physical client binding");
        if (ok && (!player(app, actor) || qa_application_launch(app) != launch ||
            app->publication_generation != generation || app->players->map_provider != source))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Client listener replaced its canonical actor or publication");
    }
    qa_launch_snapshot_release(launch);
    free(rows); return ok;
}

bool application_client_declared_disconnect(qa_application *app, qa_actor_id actor, qa_error *error)
{
    if (!app || !app->session)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Declared disconnect requires its actual application");
    if (!qa_actors_get(qa_session_actors(app->session), actor)) return true;
    if (app->provider_count > SIZE_MAX / sizeof(client_listener))
        return application_fail(error, QA_ERROR_MEMORY,
            "Declared disconnect listener snapshot exceeds its extent");
    client_listener *rows = app->provider_count ? calloc(app->provider_count, sizeof(*rows)) : NULL;
    if (app->provider_count && !rows)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining declared disconnect listener order");
    size_t count = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        const struct application_qc_profile *profile = provider->kind == APPLICATION_PROVIDER_QC
            ? provider->state.qc.qualified : NULL;
        if (!profile || !profile->clients || !provider->constructed ||
            !provider->attached || provider->close_pending) continue;
        bool member = false;
        ok = application_qc_control_source_client(provider, actor, &member, error);
        if (!ok || !member) continue;
        bool found = false;
        ok = provider_listener(provider, actor, rows + count, &found, error);
        if (ok && found) ++count;
    }
    if (!ok || !count) { free(rows); return ok; }
    const qa_launch_snapshot *launch = qa_application_launch(app);
    const uint64_t generation = app->publication_generation;
    application_provider *source = app->players ? app->players->map_provider : NULL;
    qa_launch_snapshot_retain(launch);
    for (size_t i = 0; ok && i < count; ++i) {
        const client_listener *row = rows + i;
        if (!player(app, actor) || !source || qa_application_launch(app) != launch ||
            app->publication_generation != generation || app->players->map_provider != source ||
            !provider_current(app, row) || !binding_current(row, actor)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Declared disconnect changed its actual client or Source publication");
            break;
        }
        ok = application_qc_disconnect_player(row->provider, actor, error);
        if (ok && (!player(app, actor) || qa_application_launch(app) != launch ||
            app->publication_generation != generation || app->players->map_provider != source ||
            !provider_current(app, row) || !qc_binding_current(row, actor, false)))
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Declared disconnect replaced its full client or provider identity");
    }
    qa_launch_snapshot_release(launch);
    free(rows);
    return ok;
}

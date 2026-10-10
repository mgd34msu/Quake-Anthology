#include "map_players_private.h"
#include "guest_native_q2_private.h"
#include "qa/application_view_preferences.h"
#include "qa/application_startup_prepare.h"
#include "qa/game_q2_preferences.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>

typedef struct view_scope {
    qa_application *app;
    struct application_player_roster *roster;
    application_provider *source;
    const qa_launch_snapshot *snapshot, *candidate;
    uint64_t publication, map;
} view_scope;

static bool phase(qa_application *app, qa_error *e)
{
    if (!app || !app->session || app->destroy_requested || app->finalizing ||
        app->state == QA_APPLICATION_STOPPING || app->state == QA_APPLICATION_FAULTED)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference needs its live application owner");
    return app->operation == APPLICATION_IDLE ||
        (app->operation == APPLICATION_CONFIGURING &&
         qa_application_startup_publication_cleanup(app, qa_application_launch(app))) ||
        application_fail(e, QA_ERROR_ARGUMENT, "FOV preference requires its returned Source or entered publication finish");
}

static bool current(const view_scope *scope, qa_error *e)
{
    qa_application *app = scope->app;
    if (app->destroy_requested || app->finalizing || app->state == QA_APPLICATION_FAULTED ||
        app->players != scope->roster || app->publication_generation != scope->publication ||
        app->map_revision != scope->map || !app->players ||
        app->players->map_provider != scope->source ||
        !scope->source || !scope->source->constructed || !scope->source->attached ||
        scope->source->close_pending)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference lost its installed physical Source");
    if (scope->candidate) {
        if (!qa_application_startup_publication_cleanup(app, scope->candidate) ||
            qa_application_startup_publication_previous(app, scope->candidate) != scope->snapshot)
            return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference lost its actual previous publication roster");
    } else if (qa_application_launch(app) != scope->snapshot)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference changed its published seat declarations");
    const qa_launch_choices *choices = qa_launch_snapshot_choices(scope->snapshot);
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    if (!binding || !scope->source->launch ||
        strcmp(binding->instance, scope->source->launch->selection.instance))
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference differs from its actual declared physical Source");
    return true;
}

static application_player_record *player(const view_scope *scope, qa_actor_id actor, qa_error *e)
{
    if (!current(scope, e) || !qa_actors_get(qa_session_actors(scope->app->session), actor)) {
        if (e && e->code == QA_OK) application_fail(e, QA_ERROR_ARGUMENT, "FOV preference lost its full player generation");
        return NULL;
    }
    application_player_record *found = NULL;
    for (size_t i = 0; i < scope->roster->count; ++i) {
        application_player_record *row = scope->roster->records + i;
        if (!row->retiring && qa_actor_id_equal(row->actor, actor)) {
            if (found) { application_fail(e, QA_ERROR_FORMAT, "FOV preference aliases two physical roster rows"); return NULL; }
            found = row;
        }
    }
    if (!found) application_fail(e, QA_ERROR_ARGUMENT, "FOV preference has no actual admitted player");
    return found;
}

static view_scope scope_read(qa_application *app)
{
    const qa_launch_snapshot *snapshot = qa_application_launch(app);
    bool finishing = app->operation == APPLICATION_CONFIGURING &&
        qa_application_startup_publication_cleanup(app, snapshot);
    return (view_scope){.app = app, .roster = app->players,
        .source = app->players ? app->players->map_provider : NULL,
        .snapshot = finishing ? qa_application_startup_publication_previous(app, snapshot) : snapshot,
        .candidate = finishing ? snapshot : NULL,
        .publication = app->publication_generation, .map = app->map_revision};
}

static application_native_q2_client *native_client(const view_scope *scope,
    qa_actor_id actor, qa_error *e)
{
    application_player_record *row = player(scope, actor, e);
    if (!row) return NULL;
    struct application_native_q2 *engine = scope->source->state.native.q2_engine;
    if (!engine || !engine->initialized ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023 || row->client_slot >= 256 ||
        engine->calls || !qa_world_idle(engine->world) ||
        !qa_native_host_destroy_ready(scope->source->state.native.host)) {
        if (e && e->code == QA_OK) application_fail(e, QA_ERROR_ARGUMENT, "FOV preference lost its native Q2 client owner");
        return NULL;
    }
    application_native_q2_client *client = engine->clients + row->client_slot + 1;
    if (!client->connected || !client->userinfo_present || !qa_actor_id_equal(client->actor, actor)) {
        application_fail(e, QA_ERROR_ARGUMENT, "FOV preference differs from its native Q2 Source client");
        return NULL;
    }
    return client;
}

static bool set(const view_scope *scope, qa_actor_id actor, double value,
    qa_application_view_preference_mode mode, qa_error *e)
{
    application_player_record *row = player(scope, actor, e);
    if (!row) return false;
    application_provider *source = scope->source;
    if (source->kind == APPLICATION_PROVIDER_QVM) return true;
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        application_native_q2_client *client = native_client(scope, actor, e);
        if (!client) return false;
        if (mode == QA_APPLICATION_VIEW_RESTORE) return true;
        uint32_t slot = row->client_slot + 1;
        qa_buffer info = {0};
        if (!qa_q2_userinfo_field_of_view(client->userinfo, value, &info, e)) return false;
        bool ok = application_native_q2_client_userinfo(source, slot, (const char *)info.data, e);
        qa_buffer_free(&info);
        if (!ok || !(row = player(scope, actor, e)) || row->client_slot + 1 != slot ||
            !(client = native_client(scope, actor, e))) return false;
        size_t size = strlen(client->userinfo) + 1;
        char *copy = malloc(size);
        if (!copy) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual native Q2 FOV userinfo");
        memcpy(copy, client->userinfo, size);
        free(row->userinfo); row->userinfo = copy;
        return true;
    }
    bool is_source = source->kind == APPLICATION_PROVIDER_Q2;
    application_provider *owner = is_source ? source : row->character;
    if (!owner || owner->kind != APPLICATION_PROVIDER_Q2) return true;
    if (!owner->constructed || !owner->attached || owner->close_pending || !owner->state.q2)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference lost its actual Q2 player-state owner");
    const application_control_record *record = actor.slot < scope->app->control_capacity
        ? scope->app->controls + actor.slot : NULL;
    bool in_intermission = record && record->active && !record->retired &&
        qa_actor_id_equal(record->player.actor, actor) && record->player.player_mode_set &&
        record->player.player_mode == QA_MOVEMENT_MODE_FREEZE;
    qa_player_state control;
    bool cutscene = qa_application_control_read(scope->app, actor, &control) && control.cutscene;
    qa_q2_game *game = owner->state.q2;
    bool ok = qa_q2_player_field_of_view_set(game, actor, value, is_source,
        cutscene, in_intermission, mode == QA_APPLICATION_VIEW_RESTORE, e);
    if (!ok) return false;
    row = player(scope, actor, e);
    if (!row) return false;
    application_provider *after = is_source ? scope->source : row->character;
    return (after == owner && owner->constructed && owner->attached && !owner->close_pending &&
        owner->state.q2 == game) ||
        application_fail(e, QA_ERROR_ARGUMENT, "FOV preference callback changed its actual CHARACTER owner");
}

bool qa_application_player_field_of_view_read(qa_application *app, qa_actor_id actor,
    double *out, bool *found, qa_error *e)
{
    if (!out || !found)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV observation needs its result and presence outputs");
    if (!phase(app, e)) return false;
    view_scope scope = scope_read(app);
    application_player_record *row = player(&scope, actor, e);
    if (!row) return false;
    if (scope.source->kind == APPLICATION_PROVIDER_QVM) { *out = 0; *found = false; return true; }
    if (scope.source->kind == APPLICATION_PROVIDER_NATIVE && scope.source->state.native.q2_engine) {
        if (!native_client(&scope, actor, e)) return false;
        qa_q2_player value;
        if (!qa_native_host_q2_wire_player(scope.source->state.native.host,
                row->client_slot + 1, actor, &value, e) || !player(&scope, actor, e)) return false;
        *out = value.fov; *found = true; return true;
    }
    application_provider *owner = scope.source->kind == APPLICATION_PROVIDER_Q2 ? scope.source : row->character;
    if (!owner || owner->kind != APPLICATION_PROVIDER_Q2) { *out = 0; *found = false; return true; }
    if (!owner->constructed || !owner->attached || owner->close_pending)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV observation lost its actual Q2 state owner");
    return qa_q2_player_field_of_view_read(owner->state.q2, actor, out, found, e);
}

bool qa_application_player_field_of_view_set(qa_application *app, qa_actor_id actor,
    double value, qa_application_view_preference_mode mode, qa_error *e)
{
    if (!phase(app, e)) return false;
    if (!isfinite(value) || value < 60 || value > 160 || (unsigned)mode > QA_APPLICATION_VIEW_RESTORE)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference needs a value between 60 and 160 and its actual mode");
    view_scope scope = scope_read(app);
    application_operation previous = app->operation;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = set(&scope, actor, value, mode, e);
    app->operation = previous;
    return ok;
}

bool qa_application_player_field_of_view_apply(qa_application *app, double value,
    qa_application_view_preference_mode mode, qa_error *e)
{
    if (!phase(app, e)) return false;
    if (!isfinite(value) || value < 60 || value > 160 || (unsigned)mode > QA_APPLICATION_VIEW_RESTORE)
        return application_fail(e, QA_ERROR_ARGUMENT, "FOV preference needs a value between 60 and 160 and its actual mode");
    if (!app->players || !app->players->count) return true;
    view_scope scope = scope_read(app);
    if (!current(&scope, e)) return false;
    if (scope.source->kind == APPLICATION_PROVIDER_QVM) return true;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(scope.snapshot);
    if (!choices) return application_fail(e, QA_ERROR_ARGUMENT, "FOV fanout lost its published local seat declarations");
    qa_actor_id *actors = calloc(choices->seat_count ? choices->seat_count : 1, sizeof(*actors));
    if (!actors) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual local FOV recipients");
    size_t count = 0;
    for (size_t seat = 0; seat < choices->seat_count; ++seat) {
        if (!choices->seats[seat].local || choices->seats[seat].bot) continue;
        bool selected = false;
        for (size_t i = 0; i < scope.roster->count; ++i) {
            const application_player_record *row = scope.roster->records + i;
            if (!row->retiring && !row->remote && !application_player_identity(row)->bot && !row->deferred &&
                row->seat == choices->seats[seat].id) {
                if (selected || count == choices->seat_count) { free(actors); return application_fail(e, QA_ERROR_FORMAT, "FOV fanout aliases local seat declarations"); }
                selected = true;
                actors[count++] = row->actor;
            }
        }
    }
    application_operation previous = app->operation;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) ok = set(&scope, actors[i], value, mode, e);
    app->operation = previous;
    free(actors);
    return ok;
}

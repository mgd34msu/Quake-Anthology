#include "internal.h"
#include "guest_q3_private.h"
#include "map_players_private.h"
#include "qa/application_network.h"
#include "qa/physics.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static qa_bytes selected_arsenal(qa_application *application, qa_actor_id actor)
{
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    const char *instance = provider && provider->launch ? provider->launch->selection.instance : NULL;
    return instance ? (qa_bytes){(const uint8_t *)instance, strlen(instance)} : (qa_bytes){0};
}

bool qa_application_network_player_next(const qa_application *application, size_t *cursor,
    qa_application_network_player *out)
{
    if (!application || !application->players || !cursor || !out) return false;
    const struct application_player_roster *roster = application->players;
    while (*cursor < roster->count) {
        const application_player_record *record = &roster->records[(*cursor)++];
        if (!record->remote) continue;
        *out = (qa_application_network_player){.client = record->remote_client,
            .seat = record->remote_seat, .actor = record->actor,
            .application_seat = record->seat, .client_slot = record->client_slot,
            .source_slot = record->source_slot, .retiring = record->retiring,
            .deferred = record->deferred, .source_begin_pending = record->source_begin_pending};
        return true;
    }
    return false;
}

bool qa_application_network_controlled(qa_application *application, qa_net_client_id client,
    qa_net_seat_id seat, qa_actor_id actor, qa_movement_kind kind, qa_bytes arsenal, qa_error *error)
{
    qa_actor_id admitted;
    qa_application_control_view control;
    if (!application || !qa_application_remote_player_actor(application, client, seat, &admitted) ||
        !qa_actor_id_equal(actor, admitted) || !qa_application_control_read(application, actor, &control) ||
        control.state.kind != kind)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote command does not own the current controlled actor");
    qa_bytes selected = selected_arsenal(application, actor);
    if (arsenal.size && (arsenal.size != selected.size || !arsenal.data ||
        memcmp(arsenal.data, selected.data, selected.size)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote command selected a foreign arsenal provider");
    return true;
}

bool qa_application_network_command(qa_application *application, const qa_network_command *command,
                                     qa_error *error)
{
    if (!command || !qa_application_network_controlled(application, command->client, command->seat,
        command->actor, command->movement.kind,
        command->has_arsenal ? command->arsenal.provider : (qa_bytes){0}, error)) return false;
    /* Typed arsenal selection is an application owner contract. Until that
     * producer exists, reject intent rather than discarding it after ACK. */
    if (command->has_arsenal && (command->arsenal.weapon.size || command->arsenal.use_holdable))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Application arsenal intent producer is unavailable");
    return qa_application_control_move(application, command->actor, &command->movement, error);
}

bool qa_application_network_resolve(qa_application *application, const qa_net_client *client,
    uint32_t slot, uint32_t generation, qa_net_seat_id *seat,
    qa_unified_controlled_actor *out, qa_error *error)
{
    if (!application || !client || !seat || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing network actor resolution owner");
    for (size_t i = 0; i < client->seat_count; ++i) {
        qa_actor_id actor;
        qa_application_control_view control;
        if (!qa_application_remote_player_actor(application, client->id, client->seats[i].seat, &actor) ||
            actor.slot != slot || actor.generation != generation ||
            !qa_application_control_read(application, actor, &control)) continue;
        *seat = client->seats[i].seat;
        *out = (qa_unified_controlled_actor){.actor = actor, .movement = control.state.kind,
            .arsenal = selected_arsenal(application, actor)};
        return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Packet actor is not owned by an admitted connection seat");
}

bool qa_application_network_detach(qa_application *application, const qa_net_client *client, qa_error *error)
{
    if (!application || !client)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing disconnected roster owner");
    for (size_t i = 0; i < client->seat_count; ++i)
        if (!qa_application_remote_player_detach(application, client->id, client->seats[i].seat, error)) return false;
    return true;
}

bool qa_application_network_command_owner_bound(const qa_application *application)
{
    static const char *const names[] = {"serverlist", "serverquery", "serverfavorite", "servermaster",
        "addip", "removeip", "heartbeat", "maprotation", "nextmap", "download", "downloadstatus", "downloadcancel", "downloadsuspend"};
    qa_console *console = qa_application_console((qa_application *)application);
    if (!console) return false;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        uint64_t owner = 0;
        if (!qa_console_registration_owner(console, names[i], 0, &owner) || owner != QA_NETWORK_COMMAND_OWNER) return false;
    }
    return true;
}

static struct application_q3_guest *source(qa_application *application, qa_actor_id actor,
                                           uint32_t *slot, qa_error *error)
{
    application_provider *provider = application ?
        application_provider_for(application, actor, QA_ROLE_CHARACTER, "") : NULL;
    struct application_q3_guest *engine = provider ? q3g_engine(provider) : NULL;
    if (!engine || !engine->game || !engine->game->host ||
        !qa_q3_host_actor_slot(engine->game->host, actor, slot, error)) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 snapshot requires the selected qualified game host");
        return NULL;
    }
    /* Every selected source gameplay role must be represented by this game.
     * A native protocol cannot silently serialize only one mixed component. */
    for (unsigned role = 0; role < QA_ROLE_COUNT; ++role) {
        if (role == QA_ROLE_HUD || role == QA_ROLE_MENU || role == QA_ROLE_AUDIO || role == QA_ROLE_MUSIC) continue;
        application_provider *selected = application_provider_for(application, actor, (qa_launch_role)role, "");
        if (selected && selected != provider) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent mixed selected gameplay owners");
            return NULL;
        }
    }
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    for (size_t i = 0; choices && i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->role == QA_ROLE_HUD || binding->role == QA_ROLE_MENU ||
            binding->role == QA_ROLE_AUDIO || binding->role == QA_ROLE_MUSIC) continue;
        if (strcmp(binding->instance, provider->launch->selection.instance)) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent additional scoped gameplay providers");
            return NULL;
        }
    }
    return engine;
}

bool qa_application_network_q3_source(qa_application *application, qa_actor_id actor,
    uint32_t *slot, qa_q3_product *product, qa_error *error)
{
    if (!slot || !product) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source observation output");
    struct application_q3_guest *engine = source(application, actor, slot, error);
    if (!engine) return false;
    *product = engine->product; return true;
}
bool qa_application_network_q3_world(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t restarted_server_id, int32_t feed,
    qa_q3_server_world *out, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || server_id <= 0 || restarted_server_id <= 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 wire world requires its retained source identity");
    qa_cvars *cvars = NULL;
    (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    if (!cvars) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source cvar owner is unavailable");
    const qa_cvar_view *pure = qa_cvars_find(cvars, "sv_pure");
    const qa_cvar_view *flood = qa_cvars_find(cvars, "sv_floodProtect");
    *out = (qa_q3_server_world){.generation = qa_application_configuration_generation(application),
        .server_id = server_id, .restarted_server_id = restarted_server_id,
        .checksum_feed = feed, .time = engine->milliseconds,
        .pure = pure && pure->integer != 0, .flood_protect = !flood || flood->integer != 0};
    return true;
}
qa_cvars *qa_application_network_q3_cvars(qa_application *application, qa_actor_id actor)
{
    uint32_t slot; qa_cvars *cvars = NULL;
    struct application_q3_guest *engine = source(application, actor, &slot, NULL);
    if (engine) (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    return cvars;
}
bool qa_application_network_q3_status(qa_application *application, qa_actor_id actor,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !players || !count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 status observation output");
    *count = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = &engine->clients[i];
        if (!client->connected || !client->begun || client->pending_retirement) continue;
        qa_q3_player player;
        if (!qa_q3_host_source_player(engine->game->host, i, &player, error)) return false;
        players[(*count)++] = (qa_application_network_q3_status_player){i, player.persistant[0], player.ping,
            client->userinfo ? client->userinfo : ""};
    }
    return true;
}
bool qa_application_network_q3_slots(qa_application *application, qa_actor_id actor,
    bool occupied[64], qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !occupied) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source slot observation");
    for (size_t i = 0; i < 64; ++i)
        occupied[i] = engine->clients[i].allocated || engine->clients[i].connected || engine->clients[i].pending_retirement;
    return true;
}
bool qa_application_network_q3_signon(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t feed, qa_q3_gamestate *out, qa_q3_server_world *world, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || !world || !qa_application_network_q3_world(application, actor, server_id, server_id, feed, world, error)) return false;
    if (world->pure)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 pure signon requires complete live package-reference metadata");
    char identity[32]; snprintf(identity, sizeof(identity), "%d", server_id);
    qa_cvars *cvars = qa_application_network_q3_cvars(application, actor);
    if (!cvars || !qa_cvars_register(cvars, "sv_serverid", identity, QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY,
        engine->provider->owner, "Original Q3 server identity", error) ||
        !qa_cvars_set(cvars, "sv_serverid", identity, true, error)) return false;
    qa_buffer system = {0}, server = {0};
    bool ok = qa_cvars_info(cvars, QA_CVAR_SYSTEMINFO, QA_Q3_BIG_INFO_CHARS, &system, error) &&
        qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &server, error);
    if (ok) {
        *out = engine->gamestate; out->client_number = (int32_t)slot; out->checksum_feed = feed;
        ok = qa_q3_configstring_set(out, 0, (const char *)server.data, error) &&
            qa_q3_configstring_set(out, 1, (const char *)system.data, error);
        qa_q3_host_game_data data;
        if (ok && (!qa_q3_host_game_data_read(engine->game->host, &data) || data.entity_count > QA_Q3_ENTITY_NONE))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 source signon entity count exceeds wire capacity");
        memset(out->baseline_present, 0, sizeof(out->baseline_present));
        for (uint32_t i = 1; ok && i < data.entity_count; ++i) {
            qa_qvm_entity_shared shared;
            ok = qa_q3_host_entity(engine->game->host, i, &out->baselines[i], &shared, error);
            if (ok) out->baseline_present[i] = shared.linked;
        }
    }
    qa_buffer_free(&system); qa_buffer_free(&server); return ok;
}
bool qa_application_network_q3_userinfo(qa_application *application, qa_actor_id actor,
    const char *text, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    return engine && application_q3_guest_client_userinfo(engine->provider, slot, text, error);
}

const qa_q3_gamestate *qa_application_network_q3_gamestate(qa_application *application, qa_actor_id actor)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, NULL);
    return engine ? &engine->gamestate : NULL;
}

static q3g_role *external_cgame(qa_application *app, qa_actor_owner owner, uint32_t seat)
{
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->owner != owner || !provider->attached) continue;
        struct application_q3_guest *engine = q3g_engine(provider);
        for (q3g_role *role = engine ? engine->roles : NULL; role; role = role->next)
            if (role->kind == QA_QVM_CGAME && role->seat == seat && role->ready &&
                !role->retired && !role->local_client && role->client_services.gamestate) return role;
    }
    return NULL;
}

bool qa_application_network_q3_client_actor(qa_application *app,
    const qa_application_network_q3_projection *projection, uint32_t source_number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    if (!app || !projection || !out || !present || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing remote Q3 actor projection owner");
    *out = (qa_actor_id){0}; *present = false;
    if (source_number >= QA_Q3_ENTITY_WORLD) return true;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session),
        projection->actors[source_number]);
    if (record && record->owner == projection->owner && record->definition == projection->definition &&
        !record->has_source) {
        *out = record->id; *present = true;
    }
    return true;
}

bool qa_application_network_q3_client_unproject(qa_application *app,
    qa_application_network_q3_projection *projection, qa_error *error)
{
    if (!app || !projection || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 projection retirement requires a safe session");
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        qa_actor_id actor = projection->actors[i];
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
        if (record) {
            if (record->owner != projection->owner || record->definition != projection->definition || record->has_source)
                return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 projection contains a foreign actor");
            if (!qa_session_release(app->session, actor, error)) return false;
        }
        projection->actors[i] = (qa_actor_id){0};
    }
    projection->owner = 0; projection->definition = 0; return true;
}

static qa_trajectory projection_trajectory(const qa_q3_trajectory *source)
{
    return (qa_trajectory){(qa_trajectory_type)source->type, source->time, source->duration,
        qa_v3(source->base[0], source->base[1], source->base[2]),
        qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}
static bool projection_snapshot(const qa_q3_snapshot *snapshot,
    qa_body_state bodies[QA_Q3_ENTITY_WORLD], bool present[QA_Q3_ENTITY_WORLD],
    qa_error *error)
{
    if (!snapshot) return true;
    if (!snapshot->valid || snapshot->entity_count > QA_Q3_ENTITY_WORLD ||
        (snapshot->entity_count && !snapshot->entities) || snapshot->player.clientNum < 0 ||
        snapshot->player.clientNum >= 64 || !qa_vec_finite(qa_v3(snapshot->player.origin[0],
            snapshot->player.origin[1], snapshot->player.origin[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 projection snapshot");
    int32_t previous = -1;
    for (size_t i = 0; i < snapshot->entity_count; ++i) {
        const qa_q3_entity *entity = &snapshot->entities[i];
        if (entity->number <= previous || entity->number >= QA_Q3_ENTITY_WORLD)
            return application_fail(error, QA_ERROR_FORMAT, "Remote Q3 projection entity order or number is invalid");
        previous = entity->number;
        qa_body_state body = {0};
        qa_trajectory trajectory = projection_trajectory(&entity->pos);
        if (!qa_trajectory_position(&trajectory, snapshot->server_time, 800, &body.origin, error) ||
            !qa_trajectory_velocity(&trajectory, snapshot->server_time, 800, &body.velocity, error) ||
            !qa_vec_finite(body.origin) || !qa_vec_finite(body.velocity))
            return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 entity position trajectory");
        trajectory = projection_trajectory(&entity->apos);
        if (!qa_trajectory_position(&trajectory, snapshot->server_time, 800, &body.angles, error) ||
            !qa_vec_finite(body.angles)) return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 entity angular trajectory");
        bodies[entity->number] = body; present[entity->number] = true;
    }
    bodies[snapshot->player.clientNum] = (qa_body_state){
        .origin = qa_v3(snapshot->player.origin[0], snapshot->player.origin[1], snapshot->player.origin[2]),
        .velocity = qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2]),
        .angles = qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])};
    present[snapshot->player.clientNum] = true;
    return true;
}

bool qa_application_network_q3_client_project(qa_application *app, qa_actor_owner owner,
    qa_application_network_q3_projection *projection, const qa_q3_snapshot *current,
    const qa_q3_snapshot *next, qa_error *error)
{
    if (!app || !projection || !current || app->destroy_requested || app->operation != APPLICATION_IDLE ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !external_cgame(app, owner, 0) ||
        (projection->owner && projection->owner != owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication requires its admitted idle cgame owner");
    qa_body_state bodies[QA_Q3_ENTITY_WORLD] = {0};
    bool present[QA_Q3_ENTITY_WORLD] = {0};
    /* Current state wins where both snapshots observe the same source number. */
    if (!projection_snapshot(next, bodies, present, error) ||
        !projection_snapshot(current, bodies, present, error)) return false;
    qa_string_id definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "qa.network.q3.remote-entity", &definition, error)) return false;
    if (projection->owner && projection->definition != definition)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication has a foreign definition");
    projection->owner = owner; projection->definition = definition;
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), projection->actors[i]);
        if (record && (record->owner != owner || record->has_source || record->definition != definition))
            return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication contains a foreign actor");
        if (record && !present[i] && !qa_session_release(app->session, record->id, error)) return false;
        if (!record || !present[i]) projection->actors[i] = (qa_actor_id){0};
    }
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        if (!present[i]) continue;
        if (!projection->actors[i].registry &&
            !qa_session_allocate(app->session, owner, definition, false, 0, &projection->actors[i], error)) return false;
        if (!qa_world_body_write(app->world, projection->actors[i], &bodies[i], error)) return false;
    }
    return true;
}
bool qa_application_network_q3_client_source(qa_application *app, qa_actor_id actor,
    qa_actor_owner *owner, qa_q3_product *product, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(app, actor, &slot, error);
    application_provider *hud = app ? application_provider_for(app, actor, QA_ROLE_HUD, NULL) : NULL;
    if (!engine || !owner || !product || !hud || q3g_engine(hud) != engine ||
        !external_cgame(app, hud->owner, 0))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote client requires the selected matching external cgame owner");
    *owner = hud->owner; *product = engine->product; return true;
}
bool qa_application_network_q3_client_clear(qa_application *app, qa_actor_owner owner,
    uint32_t seat, qa_error *error)
{
    q3g_role *role = external_cgame(app, owner, seat);
    if (!role) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 remote cgame owner is retired");
    if (role->initialized) {
        q3g_role *replacement = NULL;
        return q3g_role_restart(role, &replacement, error);
    }
    qa_command_tokens_free(&role->arguments); return true;
}
bool qa_application_network_q3_client_command(qa_application *app, qa_actor_owner owner,
    uint32_t seat, const qa_q3_tokens *tokens, qa_error *error)
{
    q3g_role *role = external_cgame(app, owner, seat);
    if (!role || !tokens || tokens->truncated)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 server command lacks the current cgame argument owner");
    size_t bytes = 0;
    for (size_t i = 0; i < tokens->count; ++i) bytes += strlen(qa_q3_token(tokens, i)) + 1;
    qa_command_tokens copy = {.count = tokens->count};
    copy.values = calloc(tokens->count ? tokens->count : 1, sizeof(*copy.values));
    copy.storage = malloc(bytes ? bytes : 1); copy.args_text = malloc(bytes + 1);
    if (!copy.values || !copy.storage || !copy.args_text) {
        qa_command_tokens_free(&copy); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 cgame command arguments");
    }
    size_t cursor = 0, args = 0;
    for (size_t i = 0; i < tokens->count; ++i) {
        const char *value = qa_q3_token(tokens, i); size_t length = strlen(value);
        copy.values[i] = copy.storage + cursor;
        memcpy(copy.storage + cursor, value, length + 1); cursor += length + 1;
        if (i) {
            if (i > 1) copy.args_text[args++] = ' ';
            memcpy(copy.args_text + args, value, length); args += length;
        }
    }
    copy.args_text[args] = 0;
    qa_command_tokens_free(&role->arguments); role->arguments = copy; return true;
}

typedef struct visibility_owner { qa_collision_geometry *geometry; qa_error failure; } visibility_owner;
static bool point(void *opaque, const float origin[3], int32_t *area, int32_t *cluster, qa_error *error)
{
    visibility_owner *owner = opaque;
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(owner->geometry, (qa_vec3){origin[0], origin[1], origin[2]}, &leaf, error)) return false;
    if (leaf.area < INT32_MIN || leaf.area > INT32_MAX || leaf.cluster < INT32_MIN || leaf.cluster > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 geometry visibility index is outside source range");
    *area = (int32_t)leaf.area; *cluster = (int32_t)leaf.cluster; return true;
}
static bool area_bits(void *opaque, int32_t area, uint8_t accumulator[32], size_t *bytes, qa_error *error)
{
    uint8_t bits[32]; visibility_owner *owner = opaque;
    if (!qa_collision_area_bits(owner->geometry, area, bits, sizeof(bits), bytes, error)) return false;
    for (size_t i = 0; i < *bytes; ++i) accumulator[i] |= bits[i];
    return true;
}
static bool connected(void *opaque, int32_t first, int32_t second)
{
    visibility_owner *owner = opaque; bool value = false;
    if (!qa_collision_areas_connected(owner->geometry, first, second, &value, &owner->failure)) return false;
    return value;
}
static bool cluster_visible(void *opaque, int32_t first, int32_t second)
{
    visibility_owner *owner = opaque; bool value = false;
    if (!qa_collision_cluster_visible(owner->geometry, first, second, false, &value, &owner->failure)) return false;
    return value;
}
typedef struct source_entity {
    qa_q3_entity state;
    qa_q3_host_visibility visibility;
} source_entity;

bool qa_application_network_q3_snapshot(qa_application *application, qa_actor_id actor,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    if (!out || message < 0 || commands < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 snapshot observation");
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine) return false;
    qa_q3_host_game_data data;
    qa_collision_geometry *geometry = qa_world_geometry(application->world);
    if (!geometry || !qa_q3_host_game_data_read(engine->game->host, &data) || data.entity_count > QA_Q3_ENTITIES)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot source records or geometry are unavailable");
    source_entity *records = calloc(data.entity_count ? data.entity_count : 1, sizeof(*records));
    qa_q3_visibility_entity *entities = calloc(data.entity_count ? data.entity_count : 1, sizeof(*entities));
    qa_application_network_q3_frame *candidate = calloc(1, sizeof(*candidate));
    if (!records || !entities || !candidate) {
        free(records); free(entities); free(candidate);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating Q3 snapshot observation");
    }
    bool ok = qa_q3_host_source_player(engine->game->host, slot, &candidate->snapshot.player, error);
    for (uint32_t i = 0; ok && i < data.entity_count; ++i) {
        qa_qvm_entity_shared shared; bool present;
        ok = qa_q3_host_entity(engine->game->host, i, &records[i].state, &shared, error) &&
            qa_q3_host_visibility_read(engine->game->host, i, &records[i].visibility, &present, error);
        if (!ok) break;
        if (records[i].visibility.cluster_count > 16) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 snapshot source cluster list exceeds host capacity"); break;
        }
        entities[i] = (qa_q3_visibility_entity){.state = &records[i].state,
            .linked = shared.linked && present, .flags = (uint32_t)shared.server_flags,
            .single_client = shared.single_client, .area = records[i].visibility.area,
            .area2 = records[i].visibility.area2, .last_cluster = records[i].visibility.last_cluster,
            .clusters = records[i].visibility.clusters, .cluster_count = records[i].visibility.cluster_count};
    }
    visibility_owner owner = {.geometry = geometry};
    qa_q3_visibility_world world = {.context = &owner, .point = point, .area_bits = area_bits,
        .areas_connected = connected, .cluster_visible = cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&candidate->snapshot.player, entities, data.entity_count,
                                                &world, false, &candidate->visible, error);
    if (ok && owner.failure.code) { if (error) *error = owner.failure; ok = false; }
    if (ok) {
        candidate->snapshot.valid = true; candidate->snapshot.message_number = message;
        candidate->snapshot.server_command_number = commands; candidate->snapshot.server_time = engine->milliseconds;
        candidate->snapshot.delta_number = -1; candidate->snapshot.flags = flags;
        candidate->snapshot.area_bytes = candidate->visible.area_bytes;
        memcpy(candidate->snapshot.area_mask, candidate->visible.area_mask, sizeof(candidate->snapshot.area_mask));
        candidate->snapshot.entity_count = candidate->visible.count;
        *out = *candidate; out->snapshot.entities = out->visible.entities;
    }
    free(records); free(entities); free(candidate); return ok;
}

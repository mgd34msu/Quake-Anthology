#include "map_travel_private.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>


static bool same_vector(qa_vec3 a, qa_vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static struct application_map_state *map_state(qa_application *application,
                                               qa_error *error)
{
    if (application->map_state == NULL) {
        application->map_state = calloc(1, sizeof(*application->map_state));
        if (application->map_state == NULL) {
            application_fail(error, QA_ERROR_MEMORY,
                             "cannot retain application map continuation");
            return NULL;
        }
    }
    return application->map_state;
}

static bool map_safe(const qa_application *application)
{
    return application != NULL && application->operation == APPLICATION_IDLE &&
           !application->destroy_requested && !application->finalizing &&
           application->state != QA_APPLICATION_FAULTED &&
           application->state != QA_APPLICATION_STOPPING &&
           qa_session_destroy_ready(application->session) &&
           (application->world == NULL || qa_world_idle(application->world)) &&
           qa_combat_idle(application->combat);
}

static char *map_path(const char *input, qa_error *error)
{
    if (input == NULL || input[0] == '\0') {
        application_fail(error, QA_ERROR_ARGUMENT, "map load has no destination");
        return NULL;
    }
    char *name = qa_vfs_normalize_path(input, error);
    if (name == NULL)
        return NULL;
    size_t length = strlen(name);
    bool prefix = strncmp(name, "maps/", 5) == 0;
    bool suffix = length >= 4 && strcmp(name + length - 4, ".bsp") == 0;
    if (length > SIZE_MAX - 10) {
        free(name);
        application_fail(error, QA_ERROR_MEMORY, "map path is too long");
        return NULL;
    }
    char *path = malloc(length + (prefix ? 0u : 5u) + (suffix ? 0u : 4u) + 1u);
    if (path == NULL) {
        free(name);
        application_fail(error, QA_ERROR_MEMORY, "cannot retain map path");
        return NULL;
    }
    size_t offset = 0;
    if (!prefix) { memcpy(path, "maps/", 5); offset = 5; }
    memcpy(path + offset, name, length);
    offset += length;
    if (!suffix) { memcpy(path + offset, ".bsp", 4); offset += 4; }
    path[offset] = '\0';
    free(name);
    return path;
}

static char *start_expression(const char *path, const char *spawn, bool unit,
                               qa_error *error)
{
    if (spawn == NULL)
        spawn = "";
    for (const unsigned char *at = (const unsigned char *)spawn; *at; ++at)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '_' || *at == '-')) {
            application_fail(error, QA_ERROR_ARGUMENT, "invalid map spawn point");
            return NULL;
        }
    size_t length = strlen(path), spawn_length = strlen(spawn);
    if (length > SIZE_MAX - 4 || spawn_length > SIZE_MAX - length - 4) {
        application_fail(error, QA_ERROR_MEMORY, "map start expression is too long");
        return NULL;
    }
    char *text = malloc(length + spawn_length + 3);
    if (text == NULL) {
        application_fail(error, QA_ERROR_MEMORY, "cannot retain map start expression");
        return NULL;
    }
    size_t offset = 0;
    if (unit) text[offset++] = '*';
    memcpy(text + offset, path, length);
    offset += length;
    if (spawn_length != 0) {
        text[offset++] = '$';
        memcpy(text + offset, spawn, spawn_length);
        offset += spawn_length;
    }
    text[offset] = '\0';
    return text;
}

bool qa_application_load_map(qa_application *application,
                              const qa_application_map_request *request,
                              qa_error *error)
{
    if (request == NULL || !map_safe(application))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map load requires idle shared authorities");
    struct application_map_state *state = map_state(application, error);
    if (state == NULL)
        return false;
    if (state->loading)
        return application_fail(error, QA_ERROR_ARGUMENT, "map load cannot reenter");
    char *path = map_path(request->map, error);
    if (path == NULL)
        return false;
    char *start = start_expression(path, request->spawn_point,
                                   request->new_unit, error);
    if (start == NULL) { free(path); return false; }

    const qa_launch_snapshot *current = qa_application_launch(application);
    qa_launch_draft *draft = NULL;
    bool ok = current != NULL
                  ? qa_launch_snapshot_draft_copy(current, &draft, error)
                  : request->geometry != 0 && qa_launch_draft_create(
                        application->catalog, request->geometry, path, &draft, error);
    if (!ok && current == NULL && request->geometry == 0)
        application_fail(error, QA_ERROR_ARGUMENT,
                         "initial map load requires a content preset");
    if (ok) {
        qa_launch_world world = qa_launch_draft_choices(draft)->world;
        world.map = path;
        world.start_command = start;
        if (request->geometry != 0) world.geometry = request->geometry;
        if (request->presentation != 0) world.presentation = request->presentation;
        ok = qa_launch_set_world(draft, &world, error);
    }
    if (ok) {
        bool previous_force = application->map_force_reload;
        uint64_t previous_travel = state->revision;
        state->loading = true;
        state->load_carry = request->carry_players;
        state->load_new_unit = request->new_unit;
        application->map_force_reload = true;
        ok = qa_application_apply(application, draft, error);
        application->map_force_reload = previous_force;
        state->loading = false;
        if (ok && !state->busy && state->revision == previous_travel) {
            qa_travel_route_free(&state->route);
            state->pending = false;
            state->cursor = 0;
            state->has_landmark = false;
            state->nextserver = QA_STRING_NONE;
        }
    }
    qa_launch_draft_destroy(draft);
    free(start);
    free(path);
    return ok;
}

bool qa_application_queue_travel(qa_application *application,
                                  const qa_application_travel_request *request,
                                  qa_error *error)
{
    if (application == NULL || request == NULL || request->expression == NULL ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING ||
        application->destroy_requested || application->finalizing ||
        application->operation == APPLICATION_DESTROYING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel request requires a live application");
    struct application_map_state *state = map_state(application, error);
    if (state == NULL)
        return false;
    if (state->busy)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel cannot reenter its publication");
    if (state->revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "travel continuation identity is exhausted");
    qa_travel_route route = {0};
    if (!qa_q2_travel_parse(request->expression, &route, error))
        return false;
    route.targets[0].new_unit |= request->new_unit;
    if (state->pending) {
        const qa_travel_target *old = &state->route.targets[state->cursor];
        const qa_travel_target *next = &route.targets[0];
        bool same = state->provider == request->provider &&
                    state->geometry == request->geometry &&
                    state->carry_players == request->carry_players &&
                    state->complete_campaign == request->complete_campaign &&
                    qa_actor_id_equal(state->cause, request->cause) &&
                    state->has_landmark == (request->landmark != NULL) &&
                    state->route.count - state->cursor == route.count;
        if (same && request->landmark != NULL)
            same = state->landmark.name == request->landmark->name &&
                   same_vector(state->landmark.relative_origin, request->landmark->relative_origin) &&
                   same_vector(state->landmark.relative_velocity, request->landmark->relative_velocity) &&
                   same_vector(state->landmark.relative_view_angles, request->landmark->relative_view_angles);
        for (size_t i = 0; same && i < route.count; ++i) {
            old = &state->route.targets[state->cursor + i];
            next = &route.targets[i];
            same = old->kind == next->kind && old->new_unit == next->new_unit &&
                   strcmp(old->name, next->name) == 0 &&
                   strcmp(old->spawn_point, next->spawn_point) == 0;
        }
        qa_travel_route_free(&route);
        return same || application_fail(error, QA_ERROR_ARGUMENT,
                                       "a different world transition is already pending");
    }
    if (request->landmark != NULL &&
        (!qa_vec_finite(request->landmark->relative_origin) ||
         !qa_vec_finite(request->landmark->relative_velocity) ||
         !qa_vec_finite(request->landmark->relative_view_angles))) {
        qa_travel_route_free(&route);
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel landmark contains a nonfinite pose");
    }
    qa_travel_route_free(&state->route);
    state->route = route;
    state->cursor = 0;
    state->provider = request->provider;
    state->cause = request->cause;
    state->geometry = request->geometry;
    state->carry_players = request->carry_players;
    state->complete_campaign = request->complete_campaign;
    state->has_landmark = request->landmark != NULL;
    state->landmark = request->landmark != NULL ? *request->landmark : (qa_q2_landmark){0};
    state->pending = true;
    ++state->revision;
    return true;
}

bool qa_application_travel_read(const qa_application *application,
                                 qa_application_travel_view *out)
{
    if (application == NULL || out == NULL || application->map_state == NULL ||
        !application->map_state->pending)
        return false;
    const struct application_map_state *state = application->map_state;
    *out = (qa_application_travel_view){
        .target = state->route.targets[state->cursor],
        .provider = state->provider, .cause = state->cause,
        .geometry = state->geometry, .revision = state->revision,
        .carry_players = state->carry_players,
        .complete_campaign = state->complete_campaign,
        .has_landmark = state->has_landmark, .landmark = state->landmark};
    return true;
}

static bool nextserver_prepare(qa_application *application,
                                struct application_map_state *state,
                                qa_string_id *out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_q2_nextserver(&state->route, state->cursor, &text, error))
        return false;
    bool ok = qa_strings_intern(qa_session_strings(application->session),
                                (qa_bytes){text.data, text.size}, out, error);
    qa_buffer_free(&text);
    return ok;
}

static bool travel_current(qa_application *application, uint64_t revision,
                            struct application_map_state **out, qa_error *error)
{
    if (!map_safe(application) || application->map_state == NULL ||
        !application->map_state->pending || application->map_state->busy ||
        application->map_state->revision != revision)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel publication needs an idle current request");
    *out = application->map_state;
    return true;
}

bool qa_application_commit_travel(qa_application *application,
                                   uint64_t revision, qa_error *error)
{
    struct application_map_state *state;
    if (!travel_current(application, revision, &state, error))
        return false;
    const qa_travel_target *target = &state->route.targets[state->cursor];
    if (target->kind != QA_TRAVEL_MAP)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "media travel must complete through its presentation owner");
    qa_string_id nextserver;
    if (!nextserver_prepare(application, state, &nextserver, error))
        return false;
    state->busy = true;
    bool ok = qa_application_load_map(
        application,
        &(qa_application_map_request){.geometry = state->geometry,
                                      .map = target->name,
                                      .spawn_point = target->spawn_point,
                                      .new_unit = target->new_unit,
                                      .carry_players = state->carry_players}, error);
    state->busy = false;
    if (ok) {
        state->nextserver = nextserver;
        state->pending = false;
        state->has_landmark = false;
    }
    return ok;
}

bool qa_application_complete_travel(qa_application *application,
                                     uint64_t revision, qa_error *error)
{
    struct application_map_state *state;
    if (!travel_current(application, revision, &state, error))
        return false;
    if (state->route.targets[state->cursor].kind == QA_TRAVEL_MAP)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map travel requires world publication");
    if (state->revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "travel continuation identity is exhausted");
    qa_string_id nextserver;
    if (!nextserver_prepare(application, state, &nextserver, error))
        return false;
    state->nextserver = nextserver;
    state->pending = ++state->cursor < state->route.count;
    ++state->revision;
    return true;
}

qa_string_id qa_application_nextserver(const qa_application *application)
{
    return application == NULL || application->map_state == NULL
               ? QA_STRING_NONE : application->map_state->nextserver;
}

void application_map_travel_options(const qa_application *application,
                                     bool *carry, bool *unit,
                                     const qa_q2_landmark **landmark)
{
    const struct application_map_state *state = application->map_state;
    const qa_launch_snapshot *current = qa_application_launch(application);
    *carry = current != NULL && qa_launch_snapshot_choices(current)->world.campaign;
    *unit = false;
    *landmark = NULL;
    if (state != NULL && state->loading) {
        *carry = state->load_carry;
        *unit = state->load_new_unit;
        if (state->busy && state->has_landmark) *landmark = &state->landmark;
    }
}

void application_map_publication_dispose(application_publication *publication)
{
    if (publication == NULL)
        return;
    application_players_dispose(publication->players);
    publication->players = NULL;
}

void application_map_dispose(qa_application *application)
{
    if (application == NULL)
        return;
    application_players_close(application);
    if (application->map_state == NULL) return;
    qa_travel_route_free(&application->map_state->route);
    free(application->map_state);
    application->map_state = NULL;
}

bool application_map_server_command(application_provider *provider,
                                      qa_string_id command, qa_error *error)
{
    const char *text = qa_strings_cstr(qa_session_strings(provider->application->session), command);
    if (text == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "authored server command has no text");
    while (*text == ' ' || *text == '\t') ++text;
    const char *start = text;
    while (*text && *text != ' ' && *text != '\t' && *text != '\n' && *text != '\r') ++text;
    size_t length = (size_t)(text - start);
    bool map = length == 3 && !memcmp(start, "map", 3);
    bool gamemap = length == 7 && !memcmp(start, "gamemap", 7);
    bool changelevel = length == 11 && !memcmp(start, "changelevel", 11);
    if (!map && !gamemap && !changelevel)
        return true; /* The command consumer also receives the original event. */
    while (*text == ' ' || *text == '\t') ++text;
    bool quoted = *text == '"';
    if (quoted) ++text;
    const char *destination = text;
    while (*text && (quoted ? *text != '"' : *text != ' ' && *text != '\t' && *text != '\n' && *text != '\r')) ++text;
    size_t size = (size_t)(text - destination);
    if (!size || (quoted && *text != '"'))
        return application_fail(error, QA_ERROR_FORMAT, "authored map command has no valid destination");
    if (quoted) ++text;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    if (*text)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "authored map command contains additional commands");
    char *expression = malloc(size + 1);
    if (expression == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain authored map command");
    memcpy(expression, destination, size);
    expression[size] = '\0';
    if (size > 4 && !strcmp(expression + size - 4, ".bsp")) expression[size - 4] = '\0';
    bool ok = qa_application_queue_travel(provider->application,
        &(qa_application_travel_request){.provider = provider->owner,
            .expression = expression, .new_unit = map, .carry_players = !map,
            .complete_campaign = changelevel}, error);
    free(expression);
    return ok;
}

static bool map_checkpoint_fields(qa_source_save_io *io, struct application_map_state *state,
                                   qa_product_id *geometry, qa_catalog *catalog)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    const char *product = NULL;
    if (!reading && *geometry != 0) {
        const qa_product *selected = qa_catalog_product(catalog, *geometry);
        if (!selected || !selected->identity) {
            qa_error_set(io->error, QA_ERROR_FORMAT, 0, "map continuation product has no stable identity");
            io->failed = true; return false;
        }
        product = selected->identity;
    }
    size_t maximum = reading ? io->input.size / 4 : SIZE_MAX / sizeof(*state->route.targets);
    if (maximum > SIZE_MAX / sizeof(*state->route.targets)) maximum = SIZE_MAX / sizeof(*state->route.targets);
    if (!qa_source_save_count(io, &state->route.count, maximum) ||
        !qa_source_save_count(io, &state->cursor, state->route.count) ||
        !qa_source_save_u64(io, &state->revision) ||
        !qa_source_save_string(io, &state->provider) || !qa_source_save_actor(io, &state->cause) ||
        !qa_source_save_text(io, &product) || !qa_source_save_string(io, &state->nextserver) ||
        !qa_source_save_bool(io, &state->pending) || !qa_source_save_bool(io, &state->has_landmark) ||
        !qa_source_save_bool(io, &state->carry_players) || !qa_source_save_bool(io, &state->complete_campaign) ||
        !qa_source_save_bool(io, &state->load_carry) || !qa_source_save_bool(io, &state->load_new_unit) ||
        !qa_source_save_string(io, &state->landmark.name) ||
        !qa_source_save_vec3(io, &state->landmark.relative_origin) ||
        !qa_source_save_vec3(io, &state->landmark.relative_velocity) ||
        !qa_source_save_vec3(io, &state->landmark.relative_view_angles)) return false;
    if (reading) {
        *geometry = 0;
        if (product != NULL) {
            const qa_product *selected = qa_catalog_find(catalog, product);
            if (!selected || strcmp(selected->identity, product)) {
                qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "map continuation product is absent from candidate catalog");
                io->failed = true; return false;
            }
            *geometry = selected->id;
        }
        state->route.targets = state->route.count ? calloc(state->route.count, sizeof(*state->route.targets)) : NULL;
        if (state->route.count && !state->route.targets) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating authored map continuation targets");
            io->failed = true; return false;
        }
    }
    for (size_t i = 0; i < state->route.count; ++i) {
        qa_travel_target *target = &state->route.targets[i];
        uint32_t kind = reading ? 0 : (uint32_t)target->kind;
        if (!qa_source_save_u32(io, &kind) || !qa_source_save_bool(io, &target->new_unit) ||
            !qa_source_save_text(io, &target->name) || !qa_source_save_text(io, &target->spawn_point)) return false;
        if (kind > QA_TRAVEL_DEMO || !target->name || !*target->name || !target->spawn_point) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "invalid authored map continuation target");
            io->failed = true; return false;
        }
        if (reading) target->kind = (qa_travel_kind)kind;
    }
    if ((state->pending && (!state->revision || state->cursor >= state->route.count)) ||
        !qa_vec_finite(state->landmark.relative_origin) ||
        !qa_vec_finite(state->landmark.relative_velocity) || !qa_vec_finite(state->landmark.relative_view_angles)) {
        qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "map continuation cursor or landmark disagrees");
        io->failed = true; return false;
    }
    return true;
}

bool application_map_checkpoint_capture(qa_application *application, qa_buffer *out,
                                         qa_error *error)
{
    if (!application || !out || !application->session || !qa_session_safe(application->session) ||
        application->publication_started || (application->map_state &&
            (application->map_state->busy || application->map_state->loading)))
        return application_fail(error, QA_ERROR_ARGUMENT, "map continuation capture requires a committed safe point");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, application->session, error)) return false;
    uint8_t signature[] = {'Q','A','M','T'};
    uint32_t version = 1;
    bool present = application->map_state != NULL;
    bool ok = qa_source_save_bytes(&io, signature, sizeof(signature)) &&
        qa_source_save_u32(&io, &version) && qa_source_save_bool(&io, &present);
    if (ok && present) {
        const struct application_map_state *live = application->map_state;
        if (live->route.count && !live->route.targets)
            ok = application_fail(error, QA_ERROR_FORMAT, "map continuation has no retained target storage");
        else {
            struct application_map_state retained = *live;
            uint64_t revision = live->revision;
            ok = map_checkpoint_fields(&io, &retained, &retained.geometry, application->catalog);
            if (ok && (live != application->map_state || revision != live->revision || live->busy || live->loading))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "map continuation changed during capture");
        }
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

static bool own_map_route(qa_travel_route *route, qa_error *error)
{
    size_t size = 0;
    for (size_t i = 0; i < route->count; ++i) {
        size_t name = strlen(route->targets[i].name), spawn = strlen(route->targets[i].spawn_point);
        if (name > SIZE_MAX - 2 || spawn > SIZE_MAX - name - 2 || size > SIZE_MAX - name - spawn - 2)
            return application_fail(error, QA_ERROR_MEMORY, "authored map continuation text extent is exhausted");
        size += name + spawn + 2;
    }
    char *storage = size ? malloc(size) : NULL;
    if (size && !storage)
        return application_fail(error, QA_ERROR_MEMORY, "retaining owned authored map continuation text");
    size_t position = 0;
    for (size_t i = 0; i < route->count; ++i) {
        size_t name = strlen(route->targets[i].name) + 1, spawn = strlen(route->targets[i].spawn_point) + 1;
        memcpy(storage + position, route->targets[i].name, name);
        route->targets[i].name = storage + position; position += name;
        memcpy(storage + position, route->targets[i].spawn_point, spawn);
        route->targets[i].spawn_point = storage + position; position += spawn;
    }
    route->storage = storage;
    return true;
}

bool application_map_checkpoint_restore(qa_application *candidate, qa_bytes bytes,
                                         qa_error *error)
{
    if (!candidate || !candidate->session || !candidate->catalog || candidate->map_state != NULL ||
        !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "map continuation restore requires an isolated empty candidate");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, candidate->session, bytes, error)) return false;
    uint8_t signature[4]; uint32_t version = 0; bool present = false;
    bool ok = qa_source_save_bytes(&io, signature, sizeof(signature)) && !memcmp(signature, "QAMT", 4) &&
        qa_source_save_u32(&io, &version) && version == 1 && qa_source_save_bool(&io, &present);
    struct application_map_state *state = NULL;
    if (ok && present) {
        state = calloc(1, sizeof(*state));
        if (!state) ok = application_fail(error, QA_ERROR_MEMORY, "allocating isolated map continuation");
        else ok = map_checkpoint_fields(&io, state, &state->geometry, candidate->catalog);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok && state) ok = own_map_route(&state->route, error);
    qa_source_save_dispose(&io);
    if (!ok) {
        if (state) { qa_travel_route_free(&state->route); free(state); }
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "invalid map continuation schema or extent");
        return false;
    }
    candidate->map_state = state;
    return true;
}

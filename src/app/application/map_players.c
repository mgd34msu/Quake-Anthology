#include "map_players_private.h"
#include "guest_projection_private.h"
#include "guest_native_q2_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const qa_launch_role player_roles[] = {
    QA_ROLE_CHARACTER, QA_ROLE_MOVEMENT, QA_ROLE_ARSENAL, QA_ROLE_INVENTORY,
    QA_ROLE_COMBAT, QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT
};

static qa_mode_kind selected_mode(const qa_launch_choices *choices)
{
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (choices->modes[i].rules.enabled && choices->modes[i].primary_score)
            return choices->modes[i].rules.kind;
    return QA_MODE_SINGLE_PLAYER;
}

static bool deathmatch(const qa_launch_choices *choices)
{
    qa_mode_kind kind = selected_mode(choices);
    return kind != QA_MODE_SINGLE_PLAYER && kind != QA_MODE_COOPERATIVE;
}

static bool q1_addon(const application_provider *provider)
{
    if (provider == NULL || provider->kind != APPLICATION_PROVIDER_Q1)
        return false;
    qa_q1_program program = application_q1_program(provider->launch->selection.implementation);
    return program == QA_Q1_DOPA || program == QA_Q1_MG1 || program == QA_Q1_MG3;
}

static int32_t selected_teamplay(const qa_launch_choices *choices)
{
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (choices->modes[i].rules.enabled && choices->modes[i].primary_score)
            return choices->modes[i].rules.teamplay;
    return 0;
}

static bool keep_native_travel(const application_provider *provider,
                                const qa_launch_choices *choices,
                                const application_player_carry *carry,
                                bool requested, bool addon_reset,
                                int32_t world_type, const char *map)
{
    bool keep = requested && carry->present && !deathmatch(choices);
    if (provider->kind != APPLICATION_PROVIDER_Q1)
        return keep;
    qa_q1_program program = application_q1_program(provider->launch->selection.implementation);
    bool mission = program == QA_Q1_HIPNOTIC || program == QA_Q1_ROGUE;
    if (mission && provider->product != NULL && provider->product->edition == QA_EDITION_CLASSIC)
        keep = requested && carry->present;
    if (carry->combat.health <= 0 || program == QA_Q1_CTF ||
        (program == QA_Q1_ROGUE && selected_teamplay(choices) >= 4) ||
        (provider->q1_server_flags != 0 && map != NULL && !strcmp(map, "start")))
        return false;
    if (q1_addon(provider) &&
        (addon_reset || world_type == 3 || (map != NULL && !strcmp(map, "start")) ||
         (program != QA_Q1_MG3 && selected_mode(choices) == QA_MODE_HORDE)))
        return false;
    if (map != NULL && mission &&
        ((program == QA_Q1_HIPNOTIC &&
          (!strcmp(map, "start") || !strcmp(map, "hip1m1") ||
           !strcmp(map, "hip2m1") || !strcmp(map, "hip3m1"))) ||
         (program == QA_Q1_ROGUE && !deathmatch(choices) && !strcmp(map, "r2m1"))))
        return false;
    return keep;
}

bool qa_application_player_actor(const qa_application *application, uint32_t seat,
                                 qa_actor_id *out)
{
    if (application == NULL || out == NULL || application->players == NULL ||
        application->session == NULL)
        return false;
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = &application->players->records[i];
        if (!record->retiring && record->seat == seat && qa_actors_get(
                qa_session_actors(application->session), record->actor) != NULL) {
            *out = record->actor;
            return true;
        }
    }
    return false;
}

size_t qa_application_player_count(const qa_application *application)
{
    if (application == NULL || application->players == NULL ||
        application->session == NULL)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < application->players->count; ++i)
        count += !application->players->records[i].retiring &&
            qa_actors_get(qa_session_actors(application->session), application->players->records[i].actor) != NULL;
    return count;
}

bool application_player_source_actor(const qa_application *application,
                                      qa_actor_id actor, qa_actor_id *out)
{
    if (application == NULL || application->players == NULL || out == NULL)
        return false;
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = &application->players->records[i];
        if (qa_actor_id_equal(record->actor, actor) && record->configured_actor.registry) {
            *out = record->configured_actor;
            return true;
        }
    }
    return false;
}

static void roster_free(struct application_player_roster *roster)
{
    if (roster == NULL)
        return;
    qa_q1_spawn_selector_destroy(roster->q1_selector);
    free(roster->q1_points);
    free(roster->points);
    for (size_t i = 0; roster->records != NULL && i < roster->count; ++i) {
        free(roster->records[i].name);
        free(roster->records[i].team);
        free(roster->records[i].skin);
        free(roster->records[i].userinfo);
        free(roster->records[i].guests);
    }
    free(roster->records);
    free(roster);
}

void application_players_close(qa_application *application)
{
    roster_free(application->players);
    application->players = NULL;
}

void application_players_dispose(application_player_travel *travel)
{
    if (travel == NULL)
        return;
    for (size_t i = 0; travel->carry != NULL && i < travel->count; ++i) {
        free(travel->carry[i].inventory);
        qa_q2_player_carry_free(&travel->carry[i].q2);
    }
    free(travel->carry);
    free(travel->seats);
    roster_free(travel->roster);
    free(travel);
}

static application_provider *seat_provider(application_publication *publication,
                                            const qa_launch_seat *seat,
                                            qa_launch_role role)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(publication->candidate);
    const qa_launch_binding *binding = NULL;
    for (size_t i = 0; i < choices->binding_count; ++i) {
        const qa_launch_binding *candidate = &choices->bindings[i];
        if (candidate->role == role && candidate->selector[0] == '\0' &&
            candidate->scope.kind == QA_SCOPE_ACTOR && seat->actor.registry &&
            qa_actor_id_equal(candidate->scope.actor, seat->actor)) {
            binding = candidate;
            break;
        }
    }
    if (binding == NULL)
        binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, role, "");
    if (binding == NULL)
        return NULL;
    for (size_t i = 0; i < publication->next_count; ++i)
        if (!strcmp(publication->next[i]->launch->selection.instance, binding->instance))
            return publication->next[i];
    return NULL;
}

static bool player_adapter_available(const application_provider *provider)
{
    return provider != NULL && (provider->kind != APPLICATION_PROVIDER_NATIVE ||
                                provider->launch->selection.clock.kind == QA_CLOCK_Q3 ||
                                provider->launch->selection.clock.kind == QA_CLOCK_Q2_CLASSIC ||
                                provider->launch->selection.clock.kind == QA_CLOCK_Q2_RERELEASE);
}

static bool capture_player(qa_application *application, qa_actor_id actor,
                            application_player_carry *carry, qa_error *error)
{
    application_provider *character = application_provider_for(
        application, actor, QA_ROLE_CHARACTER, "");
    application_provider *arsenal = application_provider_for(
        application, actor, QA_ROLE_ARSENAL, "");
    if (!qa_combat_read_traits(application->combat, actor, &carry->combat, error))
        return false;
    if (character != NULL && character->kind == APPLICATION_PROVIDER_Q2) {
        if (!qa_q2_player_carry_capture(character->state.q2, actor, &carry->q2, error))
            return false;
        carry->has_q2 = true;
        carry->inventory = carry->q2.inventory;
        carry->count = carry->q2.count;
        carry->q2.inventory = NULL;
        carry->q2.count = 0;
    } else {
        if (!qa_inventory_entries(application->inventory, actor, NULL, 0, &carry->count, error))
            return false;
        if (carry->count > SIZE_MAX / sizeof(*carry->inventory))
            return application_fail(error, QA_ERROR_MEMORY, "player travel inventory is too large");
        carry->inventory = carry->count ? malloc(carry->count * sizeof(*carry->inventory)) : NULL;
        if (carry->count && carry->inventory == NULL)
            return application_fail(error, QA_ERROR_MEMORY, "cannot retain player travel inventory");
        if (!qa_inventory_entries(application->inventory, actor, carry->inventory,
                                   carry->count, &carry->count, error))
            return false;
    }
    carry->present = true;
    for (size_t i = 0; i < sizeof(player_roles) / sizeof(player_roles[0]); ++i) {
        application_provider *provider = application_provider_for(
            application, actor, player_roles[i], "");
        if (provider == NULL || provider->kind <= APPLICATION_PROVIDER_Q3)
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < carry->guest_count; ++j)
            duplicate |= carry->guests[j].owner == provider->owner;
        if (!duplicate)
            carry->guests[carry->guest_count++] = (application_guest_carry){
                .owner = provider->owner, .identity = provider->launch->identity};
    }
    carry->maximum_health = 100;
    carry->character_owner = character != NULL ? character->owner : 0;
    carry->arsenal_owner = arsenal != NULL ? arsenal->owner : 0;
    if (character != NULL && character->kind == APPLICATION_PROVIDER_Q1) {
        qa_builtin_actor_traits traits;
        if (!qa_q1_game_actor_traits(character->state.q1, actor, &traits))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 travel character has no native traits");
        carry->maximum_health = traits.max_health;
        if (qa_q1_game_map_new_game_travel(character->state.q1))
            carry->combat.health = carry->maximum_health = 50;
        else if (q1_addon(character) &&
                 application->players->world_type == 3)
            carry->addon_reset = true;
    } else if (carry->has_q2) {
        carry->maximum_health = carry->q2.maximum_health;
    } else if (character != NULL && character->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state state;
        if (qa_q3_player_read(character->state.q3, actor, &state))
            carry->maximum_health = (float)state.max_health;
    }
    if (arsenal != NULL && arsenal->kind == APPLICATION_PROVIDER_Q1) {
        carry->has_q1 = qa_q1_player_read(arsenal->state.q1, actor, &carry->q1);
        carry->has_mg3 = qa_q1_mg3_progress_read(arsenal->state.q1, actor, &carry->mg3);
        if (qa_q1_game_map_new_game_travel(arsenal->state.q1)) {
            carry->combat.health = 50;
            carry->q1.max_health = 50;
        } else if (q1_addon(arsenal) && application->players->world_type == 3)
            carry->arsenal_addon_reset = true;
    }
    if (arsenal != NULL && arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        if (!qa_q2_weapon_read(arsenal->state.q2, actor, &state, error))
            return false;
        carry->weapon2 = state.weapon;
        carry->has_weapon2 = true;
    }
    if (arsenal != NULL && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state state;
        carry->has_weapon3 = qa_q3_player_read(arsenal->state.q3, actor, &state);
        if (carry->has_weapon3)
            carry->weapon3 = state.weapon;
    }
    return true;
}

static bool record_text(application_player_record *, const char *, const char *,
                         const char *, const char *, qa_error *);

bool application_players_prepare(qa_application *application,
                                  application_publication *publication,
                                  bool carry_players, bool new_unit,
                                  const qa_q2_landmark *landmark,
                                  application_player_travel **out, qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(publication->candidate);
    if (publication->map_provider->kind == APPLICATION_PROVIDER_QC &&
        publication->map_provider->state.qc.qualified != NULL)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "qualified QC authored map ownership is not implemented");
    if (choices->seat_count > UINT32_MAX - 1 ||
        choices->seat_count > SIZE_MAX / sizeof(application_player_record) ||
        choices->seat_count > SIZE_MAX / sizeof(application_player_carry))
        return application_fail(error, QA_ERROR_MEMORY, "player source slots are exhausted");
    for (size_t i = 0; i < publication->next_count; ++i) {
        application_provider *provider = publication->next[i];
        if (provider->kind != APPLICATION_PROVIDER_QC)
            continue;
        bool selected = false;
        for (size_t seat = 0; seat < choices->seat_count && !selected; ++seat)
            for (size_t role = 0; role < sizeof(player_roles) / sizeof(player_roles[0]) && !selected; ++role)
                selected = seat_provider(publication, &choices->seats[seat], player_roles[role]) == provider;
        if (selected && !application_qc_player_roster_ready(provider, choices, error))
            return false;
    }
    application_player_travel *travel = calloc(1, sizeof(*travel));
    if (travel == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain player travel");
    travel->roster = calloc(1, sizeof(*travel->roster));
    if (travel->roster == NULL) {
        application_players_dispose(travel);
        return application_fail(error, QA_ERROR_MEMORY, "cannot reserve travel player owner");
    }
    size_t local_count = choices->seat_count, remote_count = 0;
    if (application->players != NULL)
        for (size_t i = 0; i < application->players->count; ++i) {
            const application_player_record *record = &application->players->records[i];
            if (record->remote && !record->retiring &&
                qa_actors_get(qa_session_actors(application->session), record->actor)) ++remote_count;
        }
    if (remote_count > SIZE_MAX - local_count || local_count + remote_count > UINT32_MAX - 1 ||
        local_count + remote_count > SIZE_MAX / sizeof(*travel->carry) ||
        local_count + remote_count > SIZE_MAX / sizeof(*travel->seats)) {
        application_players_dispose(travel);
        return application_fail(error, QA_ERROR_MEMORY, "travel player roster is exhausted");
    }
    travel->count = local_count + remote_count;
    travel->carry = travel->count ? calloc(travel->count, sizeof(*travel->carry)) : NULL;
    travel->seats = travel->count ? calloc(travel->count, sizeof(*travel->seats)) : NULL;
    travel->roster->count = travel->count;
    travel->roster->capacity = qa_actors_capacity(qa_session_actors(application->session));
    if (travel->roster->capacity < travel->count) travel->roster->capacity = travel->count;
    travel->roster->records = travel->roster->capacity
        ? calloc(travel->roster->capacity, sizeof(*travel->roster->records)) : NULL;
    if (travel->roster == NULL || (travel->count && (!travel->carry || !travel->seats)) ||
        (travel->roster->capacity && !travel->roster->records)) {
        application_players_dispose(travel);
        return application_fail(error, QA_ERROR_MEMORY, "cannot reserve travel player roster");
    }
    if (local_count) memcpy(travel->seats, choices->seats, local_count * sizeof(*travel->seats));
    size_t cursor = local_count;
    if (application->players != NULL)
        for (size_t i = 0; i < application->players->count; ++i) {
            const application_player_record *old = &application->players->records[i];
            if (!old->remote || old->retiring ||
                !qa_actors_get(qa_session_actors(application->session), old->actor)) continue;
            for (size_t j = 0; j < local_count; ++j)
                if (travel->seats[j].id == old->seat) {
                    application_players_dispose(travel);
                    return application_fail(error, QA_ERROR_ARGUMENT, "travel local seat collides with remote roster");
                }
            application_player_record *record = &travel->roster->records[cursor];
            *record = (application_player_record){.seat = old->seat, .client_slot = old->client_slot,
                .remote_client = old->remote_client, .remote_seat = old->remote_seat,
                .remote = true, .dynamic = true, .spectator = old->spectator, .bot = old->bot,
                .source_begin_pending = old->source_begin_pending};
            if (!record_text(record, old->name, old->team, old->skin, old->userinfo, error)) {
                application_players_dispose(travel);
                return false;
            }
            travel->seats[cursor++] = (qa_launch_seat){.id = record->seat,
                .name = record->name, .team = record->team, .spectator = record->spectator, .bot = record->bot};
        }
    qa_launch_choices complete_choices = *choices;
    complete_choices.seats = travel->seats;
    complete_choices.seat_count = travel->count;
    choices = &complete_choices;
    travel->roster->family = publication->map.family;
    travel->roster->map_provider = publication->map_provider;
    if (!application_map_spawn_point(application, choices, &travel->roster->spawn_point, error)) {
        application_players_dispose(travel);
        return false;
    }
    travel->carry_players = carry_players;
    travel->new_unit = new_unit;
    travel->has_landmark = landmark != NULL;
    if (landmark != NULL)
        travel->landmark = *landmark;
    for (size_t i = 0; i < choices->seat_count; ++i) {
        const qa_launch_seat *seat = &choices->seats[i];
        application_provider *character = seat_provider(publication, seat, QA_ROLE_CHARACTER);
        application_provider *movement = seat_provider(publication, seat, QA_ROLE_MOVEMENT);
        application_provider *arsenal = seat_provider(publication, seat, QA_ROLE_ARSENAL);
        if (!player_adapter_available(character) || !player_adapter_available(movement) ||
            !player_adapter_available(arsenal)) {
            application_players_dispose(travel);
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "selected external player admission adapter is not installed");
        }
        const qa_launch_role other_roles[] = {QA_ROLE_INVENTORY, QA_ROLE_COMBAT,
                                               QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT};
        for (size_t j = 0; j < sizeof(other_roles) / sizeof(other_roles[0]); ++j) {
            application_provider *provider = seat_provider(publication, seat, other_roles[j]);
            if (provider != NULL && !player_adapter_available(provider)) {
                application_players_dispose(travel);
                return application_fail(error, QA_ERROR_UNSUPPORTED,
                                        "selected native Q2 guest client admission is not installed");
            }
        }
        for (size_t j = 0; j < sizeof(player_roles) / sizeof(player_roles[0]); ++j) {
            application_provider *provider = seat_provider(publication, seat, player_roles[j]);
            if (provider != NULL && provider->kind == APPLICATION_PROVIDER_QC &&
                !application_qc_player_map_ready(provider, provider == character,
                                                provider == publication->map_provider, error)) {
                application_players_dispose(travel);
                return false;
            }
            if (provider != NULL && provider != publication->map_provider &&
                provider->kind > APPLICATION_PROVIDER_Q3 &&
                provider->launch->selection.clock.kind == QA_CLOCK_Q3) {
                application_players_dispose(travel);
                return application_fail(error, QA_ERROR_UNSUPPORTED,
                    "secondary guest player/map ownership is not qualified");
            }
        }
        travel->roster->records[i].seat = seat->id;
        if (i < local_count) travel->roster->records[i].client_slot = (uint32_t)i;
        travel->roster->records[i].source_slot = travel->roster->records[i].client_slot +
            (character->component.clock.kind == QA_CLOCK_Q3 ? 0u : 1u);
        for (size_t j = 0; j < i; ++j)
            if (travel->roster->records[j].client_slot == travel->roster->records[i].client_slot) {
                application_players_dispose(travel);
                return application_fail(error, QA_ERROR_ARGUMENT, "travel client slot collides with another seat");
            }
        travel->roster->records[i].bot = seat->bot;
        travel->roster->records[i].configured_actor = seat->actor;
        travel->roster->records[i].character = character;
        travel->roster->records[i].spectator = seat->spectator;
        qa_actor_id previous;
        if (carry_players && qa_application_player_actor(application, seat->id, &previous) &&
            !capture_player(application, previous, &travel->carry[i], error)) {
            application_players_dispose(travel);
            return false;
        }
    }
    for (size_t i = 0; i < publication->next_count; ++i) {
        application_provider *provider = publication->next[i];
        bool selected = false;
        for (size_t j = 0; j < choices->seat_count && !selected; ++j)
            for (size_t k = 0; k < sizeof(player_roles) / sizeof(player_roles[0]); ++k)
                selected |= seat_provider(publication, &choices->seats[j], player_roles[k]) == provider;
        if (selected && provider->kind == APPLICATION_PROVIDER_QC &&
            !application_qc_player_roster_ready(provider, choices, error)) {
            application_players_dispose(travel);
            return false;
        }
    }
    *out = travel;
    return true;
}

bool application_players_point(application_player_travel *travel, qa_mode_spawnpoint point,
                                qa_string_id target, uint32_t ordinal, qa_error *error)
{
    struct application_player_roster *roster = travel->roster;
    if (roster->point_count == travel->point_capacity) {
        size_t capacity = travel->point_capacity ? travel->point_capacity * 2 : 16;
        if (capacity < travel->point_capacity || capacity > SIZE_MAX / sizeof(*roster->points))
            return application_fail(error, QA_ERROR_MEMORY, "map spawn point storage is exhausted");
        void *points = realloc(roster->points, capacity * sizeof(*roster->points));
        if (points == NULL)
            return application_fail(error, QA_ERROR_MEMORY, "cannot retain authored spawn points");
        roster->points = points;
        travel->point_capacity = capacity;
    }
    roster->points[roster->point_count++] =
        (application_player_point){.point = point, .target = target, .ordinal = ordinal};
    return true;
}

void application_players_world_type(application_player_travel *travel, int32_t world_type)
{
    travel->roster->world_type = world_type;
}

static double spawn_random(void *context)
{
    qa_application *application = context;
    return qa_builtin_random_unit(&application->random);
}

static bool create_q1_selector(qa_application *application,
                               struct application_player_roster *roster,
                               const qa_launch_choices *choices, qa_error *error)
{
    const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(
        qa_application_launch(application)), roster->map_provider->launch->selection.product);
    roster->q1_selector = qa_q1_spawn_selector_create(&(qa_q1_spawn_options){
        .services = application_builtin_services(application, application->world, application->physics),
        .server_flags = &roster->map_provider->q1_server_flags,
        .rerelease = product != NULL && product->edition == QA_EDITION_RERELEASE,
        .coop = selected_mode(choices) == QA_MODE_COOPERATIVE,
        .deathmatch = deathmatch(choices), .context = application, .random = spawn_random}, error);
    return roster->q1_selector != NULL;
}

static bool prepare_spawnpoints(qa_application *application,
                                 struct application_player_roster *roster,
                                 const qa_launch_choices *choices, qa_error *error)
{
    qa_mode_spawnpoint *points = roster->point_count
        ? malloc(roster->point_count * sizeof(*points)) : NULL;
    if (roster->point_count && points == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot publish mode spawn points");
    qa_actor_registry *actors = qa_session_actors(application->session);
    for (size_t i = 0; i < roster->point_count; ++i) {
        const qa_actor_record *actor = qa_actors_at_source(
            actors, roster->map_provider->owner, roster->points[i].ordinal);
        if (actor != NULL)
            roster->points[i].point.actor = actor->id;
        points[i] = roster->points[i].point;
    }
    for (size_t i = 0; i < application->mode_count; ++i) {
        size_t count = 0;
        for (size_t j = 0; j < roster->point_count; ++j) {
            const qa_mode_spawnpoint *point = &roster->points[j].point;
            qa_mode_map_admission admission;
            qa_mode_map_entity entity = {.classname = qa_strings_cstr(
                qa_session_strings(application->session), point->classname),
                .actor = point->actor, .origin = point->origin, .angles = point->angles,
                .spawnflags = point->flags, .no_bots = point->no_bots, .no_humans = point->no_humans};
            if (!qa_modes_classify_entity(application->modes, application->mode_ids[i],
                                          &entity, &admission, error)) { free(points); return false; }
            if (admission.role == QA_MODE_MAP_SPAWN)
                points[count++] = admission.spawn;
        }
        if (!qa_modes_spawnpoints(application->modes, application->mode_ids[i],
                                   points, count, error)) {
            free(points);
            return false;
        }
    }
    free(points);
    if (roster->map_provider->kind != APPLICATION_PROVIDER_Q1)
        return true;
    roster->q1_points = roster->point_count
        ? calloc(roster->point_count, sizeof(*roster->q1_points)) : NULL;
    if (roster->point_count && roster->q1_points == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain Q1 spawn selection");
    qa_strings *strings = qa_session_strings(application->session);
    for (size_t i = 0; i < roster->point_count; ++i) {
        const qa_mode_spawnpoint *point = &roster->points[i].point;
        const char *name = qa_strings_cstr(strings, point->classname);
        qa_q1_spawn_kind kind;
        if (!strcmp(name, "info_player_start")) kind = QA_Q1_SPAWN_START;
        else if (!strcmp(name, "info_player_start2")) kind = QA_Q1_SPAWN_RETURN;
        else if (!strcmp(name, "info_player_coop")) kind = QA_Q1_SPAWN_COOP;
        else if (!strcmp(name, "info_player_deathmatch")) kind = QA_Q1_SPAWN_DEATHMATCH;
        else if (!strcmp(name, "testplayerstart")) kind = QA_Q1_SPAWN_TEST;
        else continue;
        if (point->actor.registry)
            roster->q1_points[roster->q1_point_count++] =
                (qa_q1_spawn_point){.actor = point->actor, .kind = kind};
    }
    return create_q1_selector(application, roster, choices, error);
}

static bool spawn_pose(qa_application *application, size_t ordinal, bool force,
                        qa_body_state *body, bool *found, qa_error *error)
{
    struct application_player_roster *roster = application->players;
    *found = false;
    if (roster->q1_selector != NULL && roster->spawn_point == QA_STRING_NONE) {
        qa_actor_id point = {0};
        if (!qa_q1_spawn_select(roster->q1_selector, roster->q1_points,
                                roster->q1_point_count, force, &point, error))
            return false;
        if (!point.registry)
            return true;
        qa_body_state spawn;
        if (!qa_world_body_read(application->world, point, &spawn, error))
            return false;
        body->origin = qa_vec_add(spawn.origin, qa_v3(0, 0, 1));
        body->angles = qa_v3(0, spawn.angles.y, 0);
        body->velocity = qa_v3(0, 0, 0);
        *found = true;
        return true;
    }
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    if (application->primary_mode_ready && roster->spawn_point == QA_STRING_NONE &&
        (deathmatch(choices) || roster->family == QA_BSP_Q3)) {
        qa_mode_spawnpoint point;
        if (!qa_modes_spawnpoint(application->modes, application->primary_mode,
                                  roster->records[ordinal].actor, false, &point, error))
            return false;
        body->origin = point.origin;
        body->angles = point.angles;
        *found = true;
        return true;
    }
    const char *wanted = selected_mode(choices) == QA_MODE_COOPERATIVE && ordinal > 0
                            ? "info_player_coop" : "info_player_start";
    size_t seen = 0;
    application_player_point *fallback = NULL, *named_fallback = NULL;
    for (size_t i = 0; i < roster->point_count; ++i) {
        application_player_point *point = &roster->points[i];
        if ((point->point.no_bots && roster->records[ordinal].bot) ||
            (point->point.no_humans && !roster->records[ordinal].bot))
            continue;
        if (roster->spawn_point != QA_STRING_NONE && point->target != roster->spawn_point)
            continue;
        if (roster->spawn_point != QA_STRING_NONE && named_fallback == NULL)
            named_fallback = point;
        const char *name = qa_strings_cstr(qa_session_strings(application->session), point->point.classname);
        if (!strcmp(name, "info_player_start") && fallback == NULL)
            fallback = point;
        if (!strcmp(name, wanted) && (ordinal == 0 || seen++ == ordinal - 1)) {
            fallback = point;
            break;
        }
    }
    if (fallback == NULL) fallback = named_fallback;
    if (fallback == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "map has no applicable player spawn");
    body->origin = fallback->point.origin;
    body->angles = fallback->point.angles;
    body->origin.z += roster->family == QA_BSP_Q3 ? 9 : 1;
    *found = true;
    return true;
}

static bool q2_movement(void *context, qa_actor_id actor,
                        qa_q2_player_movement *out, qa_error *error)
{
    application_provider *provider = context;
    application_control_record *control;
    qa_body_state body;
    if (!qa_world_body_read(provider->application->world, actor, &body, error) ||
        !application_control_ensure(provider->application, actor, body.angles, &control, error))
        return false;
    *out = (qa_q2_player_movement){.view_angles = control->view_angles,
        .command_angles = control->command_angles, .standing_bounds = control->standing_bounds,
        .buttons = control->buttons, .water_type = (uint32_t)control->water_type,
        .water_level = control->water_level, .grounded = control->ground.hit != QA_TRACE_HIT_NONE,
        .impact_delta = control->result.impact_delta,
        .noclip = control->player_mode_set && control->player_mode == QA_MOVEMENT_MODE_NOCLIP,
        .on_ladder = control->state.kind == QA_MOVEMENT_Q2_RERELEASE &&
                     (control->state.data.q2r.flags & 128u) != 0,
        .grounded_on_world = qa_actor_id_equal(body.ground, provider->application->physics->world_actor),
        .ducked = body.bounds.maxs.z < control->standing_bounds.maxs.z,
        .animate_q2 = application_provider_for(provider->application, actor,
                                                QA_ROLE_CHARACTER, "") == provider};
    const qa_launch_role grapple_roles[] = {QA_ROLE_ARSENAL, QA_ROLE_EQUIPMENT};
    for (size_t i = 0; i < sizeof(grapple_roles) / sizeof(grapple_roles[0]); ++i) {
        application_provider *owner = application_provider_for(
            provider->application, actor, grapple_roles[i], "");
        if (owner == NULL || owner->kind != APPLICATION_PROVIDER_Q2)
            continue;
        qa_clock_state source_clock, character_clock;
        if (owner != provider &&
            (!qa_session_clock(provider->application->session, owner->owner, &source_clock) ||
             !qa_session_clock(provider->application->session, provider->owner, &character_clock)))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "grapple projection needs admitted source clocks");
        for (unsigned j = QA_Q2_CTF_GRAPPLE; j <= QA_Q2_LMCTF_GRAPPLE; ++j) {
            qa_q2_grapple_state grapple;
            if (!qa_q2_grapple_read(owner->state.q2, actor, (qa_q2_grapple_kind)j, &grapple, error))
                return false;
            out->grapple_attached |= grapple.hook.registry != 0 &&
                grapple.phase != QA_Q2_GRAPPLE_FLY;
            uint64_t released_until = grapple.release_ns;
            if (owner != provider && released_until != 0 && released_until != UINT64_MAX) {
                uint64_t now = source_clock.frame.time_ns;
                if (released_until < now) released_until = 0;
                else {
                    uint64_t remaining = released_until - now;
                    now = character_clock.frame.time_ns;
                    released_until = UINT64_MAX - now < remaining ? UINT64_MAX : now + remaining;
                }
            }
            if (released_until > out->grapple_released_until_ns)
                out->grapple_released_until_ns = released_until;
        }
    }
    return true;
}

static bool q2_motion(void *context, qa_actor_id actor,
                      const qa_q2_player_motion *motion, qa_error *error)
{
    application_provider *provider = context;
    qa_application *application = provider->application;
    if (motion->kind == QA_Q2_PLAYER_NOCLIP)
        return application_control_player_mode(application, actor,
            motion->enabled ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL,
            motion->spectator, error);
    qa_body_state body;
    if (!qa_world_body_read(application->world, actor, &body, error))
        return false;
    body.origin = motion->origin;
    body.velocity = motion->velocity;
    body.angles = motion->angles;
    body.ground = (qa_actor_id){0};
    if (!qa_world_body_write(application->world, actor, &body, error))
        return false;
    qa_builtin_motion_change change = {.body = body, .view_angles = motion->angles,
        .reason = motion->kind == QA_Q2_PLAYER_TELEPORT ? QA_BUILTIN_MOTION_TELEPORT
                                                       : QA_BUILTIN_MOTION_RESET,
        .hold_ns = motion->hold_ns, .force_view_angles = true};
    if (!application_control_motion_changed(application, actor, &change, error) ||
        !application_record_motion_change(application, actor, &change, error))
        return false;
    application->controls[actor.slot].command_angles = motion->command_angles;
    return application_control_player_mode(application, actor,
        motion->kind == QA_Q2_PLAYER_FREEZE ? QA_MOVEMENT_MODE_FREEZE
            : motion->spectator ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL,
        motion->spectator, error);
}

static bool q2_controlled(void *context, qa_actor_id actor)
{
    return application_controlled(((application_provider *)context)->application, actor);
}

static bool q2_player_event(void *context, const qa_q2_player_event *event, qa_error *error)
{
    return application_emit_q2_player(context, event, error);
}

static bool q2_score_read(void *context, qa_actor_id actor, int32_t *out, qa_error *error)
{
    qa_application *application = ((application_provider *)context)->application;
    if (!application->primary_mode_ready) { *out = 0; return true; }
    return qa_modes_score(application->modes, application->primary_mode, actor, out, error);
}

static void defer_player(qa_application *application, application_player_record *record)
{
    uint64_t now = qa_session_elapsed(application->session);
    record->deferred = true;
    record->deferred_until_ns = now > UINT64_MAX - UINT64_C(5000000000)
        ? UINT64_MAX : now + UINT64_C(5000000000);
}

static bool q2_select_spawn(void *context, qa_actor_id actor, qa_vec3 *origin,
                            qa_vec3 *angles, bool *found, qa_error *error)
{
    application_provider *provider = context;
    qa_application *application = provider->application;
    *found = false;
    if (application->players == NULL ||
        application->players->map_provider == provider)
        return true;
    for (size_t i = 0; i < application->players->count; ++i)
        if (qa_actor_id_equal(application->players->records[i].actor, actor)) {
            if (application->players->records[i].deferred) {
                qa_body_state body;
                if (!qa_world_body_read(application->world, actor, &body, error)) return false;
                *origin = body.origin;
                *angles = body.angles;
                *found = true;
                return true;
            }
            qa_body_state body = {0};
            if (!spawn_pose(application, i, false, &body, found, error))
                return false;
            if (!*found) {
                defer_player(application, &application->players->records[i]);
                if (!qa_world_body_read(application->world, actor, &body, error)) return false;
                *found = true; /* The shared deferred selector owns the retry. */
            }
            *origin = body.origin;
            *angles = body.angles;
            return true;
        }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 spawn actor has no selected seat");
}

static bool configure_q2_players(qa_application *application,
                                  const qa_launch_choices *choices, qa_error *error)
{
    qa_string_id spawn_point;
    if (!application_map_spawn_point(application, choices, &spawn_point, error))
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (provider->kind != APPLICATION_PROVIDER_Q2)
            continue;
        qa_q2_player_rules rules;
        qa_q2_player_rules_default(&rules);
        const qa_cvar_view *maximum = qa_cvars_find(application->cvars, "sv_maxclients");
        if (maximum != NULL && maximum->integer < 1)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 maximum clients is outside source slot range");
        rules.max_clients = maximum != NULL ? (uint32_t)maximum->integer : (uint32_t)choices->seat_count;
        if (rules.max_clients < choices->seat_count) rules.max_clients = (uint32_t)choices->seat_count;
        rules.max_spectators = rules.max_clients;
        rules.map_name = qa_strings_cstr(qa_session_strings(application->session), application->current_map);
        rules.spawn_point = spawn_point == QA_STRING_NONE ? "" :
            qa_strings_cstr(qa_session_strings(application->session), spawn_point);
        qa_q2_player_services services = {.context = provider, .controlled = q2_controlled,
            .movement = q2_movement, .set_movement = q2_motion,
            .emit = q2_player_event, .select_spawn = q2_select_spawn,
            .shared_score_owned = true, .score_read = q2_score_read,
            .weapon_state = application_q2_character_weapon,
            .weapon_input = application_q2_weapon_input,
            .weapon_selected = application_q2_weapon_selected};
        if (!qa_q2_players_configure(provider->state.q2, &rules, &services, error))
            return false;
    }
    return true;
}

bool application_players_restore_prepare(qa_application *application,
                                           const qa_launch_choices *choices,
                                           qa_error *error)
{
    return configure_q2_players(application, choices, error);
}

static bool restore_counts(qa_application *application, qa_actor_id actor,
                            const application_player_carry *carry,
                            bool q1, bool unit, qa_error *error)
{
    qa_strings *strings = qa_session_strings(application->session);
    for (size_t i = 0; i < carry->count; ++i) {
        qa_inventory_entry entry = carry->inventory[i];
        const char *name = qa_strings_cstr(strings, entry.item);
        if (q1 && name != NULL) {
            static const char *const temporary[] = {
                "q1:key/silver", "q1:key/gold", "q1:powerup/quad",
                "q1:powerup/invulnerability", "q1:powerup/invisibility", "q1:powerup/suit"
            };
            for (size_t j = 0; j < sizeof(temporary) / sizeof(temporary[0]); ++j)
                if (!strcmp(name, temporary[j])) entry.count = 0;
        }
        if (unit && name != NULL && !strncmp(name, "q2:key_", 7))
            entry.count = 0;
        if (q1 && name != NULL && !strcmp(name, "q1:ammo/shells"))
            entry.count = fmax(25, entry.count);
        if (!qa_inventory_configure(application->inventory, actor, &entry, NULL, NULL, error))
            return false;
    }
    return true;
}

static bool loadout_scope(const qa_launch_scope *scope, const qa_launch_seat *seat)
{
    return scope->kind == QA_SCOPE_DEFAULT_PLAYER ||
           (scope->kind == QA_SCOPE_SEAT && scope->seat == seat->id) ||
           (scope->kind == QA_SCOPE_ACTOR && qa_actor_id_equal(scope->actor, seat->actor));
}

static bool fresh_q1_arsenal(qa_application *application,
                             const qa_launch_choices *choices,
                             application_provider *arsenal, qa_actor_id actor,
                             const application_player_carry *carry,
                             qa_error *error)
{
    if (arsenal->kind != APPLICATION_PROVIDER_Q1)
        return true;
    qa_q1_program program = application_q1_program(arsenal->launch->selection.implementation);
    for (size_t i = 0; i < carry->count; ++i) {
        qa_inventory_entry entry = carry->inventory[i];
        const char *name = qa_strings_cstr(qa_session_strings(application->session), entry.item);
        if (name == NULL || (strncmp(name, "q1:", 3) &&
            (program != QA_Q1_ROGUE || strncmp(name, "rogue:", 6)) &&
            (program != QA_Q1_HIPNOTIC || strncmp(name, "hipnotic:", 9)) &&
            (program != QA_Q1_MG3 || strncmp(name, "mg3:", 4))))
            continue;
        entry.count = 0;
        if (!qa_inventory_configure(application->inventory, actor, &entry, NULL, NULL, error))
            return false;
    }
    if (!qa_q1_player_inventory_reset(arsenal->state.q1, actor, error))
        return false;
    if (program != QA_Q1_ROGUE)
        return true;
    qa_string_id vengeance;
    if (!qa_strings_intern_cstr(qa_session_strings(application->session),
                                "rogue:artifact/vengeance", &vengeance, error) ||
        !qa_inventory_configure(application->inventory, actor,
            &(qa_inventory_entry){.item = vengeance, .capacity = 1,
                                  .policy = QA_COUNT_SOURCE_FLOAT}, NULL, NULL, error))
        return false;
    if (!deathmatch(choices) || selected_teamplay(choices) < 4)
        return true;
    qa_combat_state combat;
    qa_string_id green;
    if (!qa_combat_read(application->combat, actor, &combat, error) ||
        !qa_strings_intern_cstr(qa_session_strings(application->session),
                                "q1:armor/green", &green, error))
        return false;
    combat.armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q1, .points = 50,
        .item = green, .protection.q1_absorption = 0.3f};
    return qa_combat_set_armor(application->combat, actor, &combat.armor, error);
}

static bool apply_loadout(qa_application *application, const qa_launch_choices *choices,
                          const qa_launch_seat *seat, qa_actor_id actor, qa_error *error)
{
    for (size_t i = 0; i < choices->loadout_count; ++i) {
        const qa_launch_loadout *loadout = &choices->loadout[i];
        if (!loadout_scope(&loadout->scope, seat))
            continue;
        qa_string_id item;
        if (!qa_strings_intern_cstr(qa_session_strings(application->session), loadout->item, &item, error))
            return false;
        qa_inventory_entry entry;
        qa_error missing = {0};
        if (!qa_inventory_entry_read(application->inventory, actor, item, &entry, &missing)) {
            if (missing.code != QA_ERROR_NOT_FOUND) { if (error) *error = missing; return false; }
            entry = (qa_inventory_entry){.item = item, .policy = QA_COUNT_STACK,
                                         .capacity = loadout->quantity};
        }
        if (loadout->override_capacity)
            entry.capacity = loadout->capacity;
        entry.count = fmin(entry.capacity, loadout->quantity);
        if (!qa_inventory_configure(application->inventory, actor, &entry, NULL, NULL, error))
            return false;
    }
    return true;
}

static bool publish_player(qa_application *application, const qa_launch_choices *choices,
                           const qa_launch_seat *seat, application_player_record *record,
                           const application_player_carry *carry, size_t ordinal,
                           bool carry_players, bool new_unit, const qa_q2_landmark *landmark,
                           bool defer_source_begin, qa_error *error)
{
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(application->session), "player", &definition, error))
        return false;
        application_provider *character = record->character;
        if (character == NULL)
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected player character disappeared");
        bool q3_client = character->kind == APPLICATION_PROVIDER_Q3 ||
            character->kind == APPLICATION_PROVIDER_QVM ||
            (character->kind == APPLICATION_PROVIDER_NATIVE && character->component.clock.kind == QA_CLOCK_Q3);
        uint32_t source_slot = record->source_slot;
        (void)q3_client;
        if (!qa_actors_allocate_source(qa_session_actors(application->session), character->owner,
                                       source_slot, definition, &record->actor, error))
            return false;
        qa_actor_id actor = record->actor;
        application_provider *arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
        application_provider *movement = application_provider_for(application, actor, QA_ROLE_MOVEMENT, "");
        if (arsenal == NULL || movement == NULL)
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected player services disappeared");
        bool rogue_reset = arsenal->kind == APPLICATION_PROVIDER_Q1 &&
            application_q1_program(arsenal->launch->selection.implementation) == QA_Q1_ROGUE &&
            deathmatch(choices) && selected_teamplay(choices) >= 4;
        qa_movement_kind movement_kind = movement->component.clock.kind == QA_CLOCK_Q3 ? QA_MOVEMENT_Q3
            : movement->component.clock.kind == QA_CLOCK_Q2_RERELEASE ? QA_MOVEMENT_Q2_RERELEASE
            : movement->component.clock.kind == QA_CLOCK_Q2_CLASSIC ? QA_MOVEMENT_Q2_CLASSIC
            : movement->component.clock.kind == QA_CLOCK_QUAKEWORLD ? QA_MOVEMENT_QUAKEWORLD : QA_MOVEMENT_NETQUAKE;
        qa_body_state body = {.bounds = qa_movement_input_default(movement_kind, actor).standing.bounds};
        const char *current_map = qa_strings_cstr(qa_session_strings(application->session), application->current_map);
        bool keep = keep_native_travel(character, choices, carry, carry_players,
            carry->addon_reset, application->players->world_type, current_map);
        bool keep_inventory = keep_native_travel(arsenal, choices, carry, carry_players,
            carry->arsenal_addon_reset, application->players->world_type, current_map);
        qa_q1_program character_program = character->kind == APPLICATION_PROVIDER_Q1
            ? application_q1_program(character->launch->selection.implementation) : QA_Q1_ID1;
        const qa_product *character_product = qa_catalog_product(qa_launch_snapshot_catalog(
            qa_application_launch(application)), character->launch->selection.product);
        bool mission_pack = character->kind == APPLICATION_PROVIDER_Q1 &&
            (character_program == QA_Q1_HIPNOTIC || character_program == QA_Q1_ROGUE);
        bool classic_mission = mission_pack && character_product != NULL &&
            character_product->edition == QA_EDITION_CLASSIC;
        bool reduced_health = character->kind == APPLICATION_PROVIDER_Q1 && !deathmatch(choices) &&
            character_program != QA_Q1_CTF &&
            ((choices->world.skill == 3 && character_product != NULL &&
              character_product->edition == QA_EDITION_RERELEASE) ||
             application_q1_program(character->launch->selection.implementation) == QA_Q1_MG3);
        qa_combat_state combat = {.health = reduced_health ? 50 : 100, .mass = 200, .can_take_damage = !seat->spectator};
        if (keep) { combat.health = carry->combat.health; combat.armor = carry->combat.armor; }
        bool q1_carry = character->kind == APPLICATION_PROVIDER_Q1;
        float maximum_health = keep && carry->character_owner == character->owner
            ? carry->maximum_health : reduced_health ? 50 : 100;
        if (classic_mission) maximum_health = 100;
        if (keep && q1_carry)
            combat.health = fminf(maximum_health, fmaxf(maximum_health / 2, combat.health));
        if (!qa_world_body_create(application->world, actor, &body, error) ||
            !qa_combat_create_actor(application->combat, actor, &combat, error) ||
            !qa_inventory_create_actor(application->inventory, actor, NULL, 0, error))
            return false;
        if (application->modes != NULL) {
            qa_string_id name;
            if (!qa_strings_intern_cstr(qa_session_strings(application->session), seat->name, &name, error) ||
                !qa_modes_player(application->modes, &(qa_match_player){.actor = actor, .name = name,
                                .connected = true, .bot = seat->bot}, error))
                return false;
            for (size_t j = 0; j < application->mode_count; ++j) {
                qa_team_id team = 0;
                if (seat->team != NULL && seat->team[0] != '\0' &&
                    !qa_strings_intern_cstr(qa_session_strings(application->session), seat->team, &team, error))
                    return false;
                if (!qa_modes_join(application->modes, application->mode_ids[j], actor, team, seat->spectator, error))
                    return false;
            }
            if (keep && carry->has_q2 && application->primary_mode_ready &&
                !qa_modes_set_score(application->modes, application->primary_mode,
                                    actor, carry->q2.score, error)) return false;
            if (application->primary_mode_ready &&
                !qa_modes_team(application->modes, application->primary_mode, actor, &combat.team, error))
                return false;
            if (!qa_combat_set_traits(application->combat, actor, &combat, error)) return false;
        }
        bool found = true;
        if (character->kind <= APPLICATION_PROVIDER_Q3 &&
            character->kind != APPLICATION_PROVIDER_Q2 &&
            !spawn_pose(application, ordinal, false, &body, &found, error))
            return false;
        if (!qa_world_body_write(application->world, actor, &body, error))
            return false;
        record->deferred = !found;
        if (!found) defer_player(application, record);
        if (character->kind == APPLICATION_PROVIDER_Q1 &&
            (!qa_q1_character_attach(character->state.q1, actor, error) ||
             !qa_q1_character_respawn(character->state.q1, actor, &combat.health, error) ||
             !qa_q1_player_travel_reset(character->state.q1, actor, maximum_health, error)))
            return false;
        if (arsenal->kind == APPLICATION_PROVIDER_Q1 &&
            !qa_q1_player_attach(arsenal->state.q1, actor, true, error))
            return false;
        if (arsenal->kind == APPLICATION_PROVIDER_Q2 && arsenal != character &&
            (!qa_q2_items_admit_player(arsenal->state.q2, actor, true, error) ||
             !qa_q2_weapon_bind(arsenal->state.q2, actor, QA_Q2_BLASTER, error)))
            return false;
        for (size_t j = 0; j < application->provider_count; ++j) {
            application_provider *provider = application->providers[j];
            if (provider->kind != APPLICATION_PROVIDER_Q3) continue;
            uint32_t selections = (provider == character ? QA_Q3_CHARACTER : 0u) |
                (provider == arsenal ? QA_Q3_ARSENAL : 0u) |
                (provider == application_provider_for(application, actor, QA_ROLE_EFFECTS, "") ? QA_Q3_EFFECTS : 0u) |
                (provider == application_provider_for(application, actor, QA_ROLE_COMBAT, "") ? QA_Q3_COMBAT : 0u) |
                (provider == application_provider_for(application, actor, QA_ROLE_EQUIPMENT, "") ? QA_Q3_EQUIPMENT : 0u);
            if (selections && (!qa_q3_bind_player(provider->state.q3, actor, selections, 100, error) ||
                               !qa_q3_spawn_player(provider->state.q3, actor, &body, combat.team, error)))
                return false;
        }
        if (!qa_application_control_admit(application, actor, body.angles, error))
            return false;
        if (character->kind == APPLICATION_PROVIDER_Q2) {
            char userinfo[2304];
            snprintf(userinfo, sizeof(userinfo), "\\name\\%.2000s\\skin\\male/grunt\\spectator\\%d",
                     strchr(seat->name, '\\') != NULL ? "badinfo" : seat->name, seat->spectator ? 1 : 0);
            qa_q2_connection_result connection;
            if (!qa_q2_player_connect(character->state.q2, record->userinfo != NULL ? record->userinfo : userinfo, seat->bot, &connection, error))
                return false;
            if (!connection.allowed)
                return application_fail(error, QA_ERROR_ARGUMENT, connection.reason);
            qa_q2_player_carry q2 = carry->q2;
            q2.inventory = carry->inventory;
            q2.count = carry->count;
            if (!qa_q2_player_admit(character->state.q2, actor, &(qa_q2_player_admission){
                .slot = record->client_slot, .seat = seat->id, .userinfo = connection.userinfo,
                .initialize_inventory = true, .use_q2_weapons = arsenal == character,
                .use_q2_inventory = arsenal == character, .bot = seat->bot,
                .carry = keep && carry->has_q2 && carry->character_owner == character->owner ? &q2 : NULL}, error) ||
                !qa_q2_player_spawn(character->state.q2, actor, false,
                                    landmark, error))
                return false;
            found = !record->deferred;
        }
        for (size_t j = 0; j < application->provider_count; ++j) {
            application_provider *provider = application->providers[j];
            if (provider->kind <= APPLICATION_PROVIDER_Q3) continue;
            bool selected = false;
            for (size_t k = 0; k < sizeof(player_roles) / sizeof(player_roles[0]); ++k)
                selected |= application_provider_for(application, actor, player_roles[k], "") == provider;
            if (!selected) continue;
            bool continued = false;
            for (size_t k = 0; k < carry->guest_count; ++k)
                continued |= carry->guests[k].owner == provider->owner &&
                    qa_sha256_equal(&carry->guests[k].identity, &provider->launch->identity);
            if (provider->kind == APPLICATION_PROVIDER_QC) {
                if (!application_qc_bind_player(provider, record->client_slot + 1, seat->id, actor,
                                                seat->name, seat->spectator, !continued,
                                                provider == character, error))
                    return false;
            } else if (provider->component.clock.kind == QA_CLOCK_Q3) {
                char userinfo[1024];
                snprintf(userinfo, sizeof(userinfo), "\\name\\%.900s\\model\\sarge\\team\\%s",
                    strchr(seat->name, '\\') != NULL ? "badinfo" : seat->name,
                    seat->spectator ? "s" : seat->team != NULL && seat->team[0] ? seat->team : "free");
                bool accepted = false;
                if (!application_q3_guest_client_connect(provider, record->client_slot, actor,
                                                          record->userinfo != NULL ? record->userinfo : userinfo, !continued, seat->bot, &accepted, error))
                    return false;
                if (!accepted)
                    return application_fail(error, QA_ERROR_ARGUMENT, "selected Q3 guest rejected a client");
                if (defer_source_begin) record->source_begin_pending = true;
                else if (!application_q3_guest_client_begin(provider, record->client_slot, error))
                    return false;
            } else if (provider->kind == APPLICATION_PROVIDER_NATIVE &&
                       provider->state.native.q2_engine != NULL) {
                char userinfo[2304];
                snprintf(userinfo, sizeof(userinfo), "\\name\\%.2000s\\skin\\male/grunt\\spectator\\%d",
                    strchr(seat->name, '\\') != NULL ? "badinfo" : seat->name,
                    seat->spectator ? 1 : 0);
                bool accepted = false;
                if (!application_native_q2_client_admit(provider, record->client_slot + 1,
                    actor, record->userinfo != NULL ? record->userinfo : userinfo,
                    "", seat->bot, &accepted, error))
                    return false;
                if (!accepted)
                    return application_fail(error, QA_ERROR_ARGUMENT,
                                            "selected native Q2 guest rejected a client");
                if (defer_source_begin) record->source_begin_pending = true;
                else if (!application_native_q2_client_begin(provider,
                                                             record->client_slot + 1, error))
                    return false;
            } else {
                return application_fail(error, QA_ERROR_UNSUPPORTED,
                                        "selected native Q2 guest client admission is not installed");
            }
        }
        if (keep_inventory && !restore_counts(application, actor, carry,
                                     arsenal->kind == APPLICATION_PROVIDER_Q1,
                                     new_unit && selected_mode(choices) == QA_MODE_COOPERATIVE, error))
            return false;
        if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
            float arsenal_maximum = arsenal == character ? maximum_health
                : keep_inventory && carry->has_q1 && carry->arsenal_owner == arsenal->owner
                    ? carry->q1.max_health : application_q1_program(arsenal->launch->selection.implementation) == QA_Q1_MG3
                        && !deathmatch(choices) ? 50 : 100;
            if (!qa_q1_player_travel_reset(arsenal->state.q1, actor, arsenal_maximum, error))
                return false;
            if (carry_players && carry->present && carry->has_mg3 && carry->arsenal_owner == arsenal->owner &&
                !qa_q1_mg3_progress_restore(arsenal->state.q1, actor, &carry->mg3, error))
                return false;
            if (keep_inventory && !rogue_reset && carry->has_q1 && carry->arsenal_owner == arsenal->owner &&
                !qa_q1_player_select(arsenal->state.q1, actor,
                    carry->q1.weapon == QA_Q1_ROGUE_GRAPPLE && selected_teamplay(choices) < 4
                        ? QA_Q1_AXE : carry->q1.weapon, error))
                return false;
        }
        if (keep_inventory && carry->has_weapon2 && arsenal->kind == APPLICATION_PROVIDER_Q2 &&
            carry->arsenal_owner == arsenal->owner &&
            !qa_q2_weapon_bind(arsenal->state.q2, actor, carry->weapon2, error))
            return false;
        if (keep_inventory && carry->has_weapon3 && arsenal->kind == APPLICATION_PROVIDER_Q3 &&
            carry->arsenal_owner == arsenal->owner) {
            qa_q3_player_state state;
            if (!qa_q3_player_read(arsenal->state.q3, actor, &state))
                return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 carried arsenal has no admitted state");
            state.weapon = state.requested_weapon = carry->weapon3;
            if (!qa_q3_player_restore(arsenal->state.q3, actor, &state, error)) return false;
        }
        if ((!keep_inventory && !fresh_q1_arsenal(application, choices, arsenal, actor, carry, error)) ||
            (!keep_inventory && !apply_loadout(application, choices, seat, actor, error)) ||
            (keep && (!qa_combat_set_health(application->combat, actor, combat.health, error) ||
                      (!rogue_reset && !qa_combat_set_armor(application->combat, actor, &combat.armor, error)))))
            return false;
        /* The character may initialize its own source bounds; movement owns
         * the selected standing hull, including foreign character mixtures. */
        if (!qa_world_body_read(application->world, actor, &body, error)) return false;
        body.bounds = qa_movement_input_default(movement_kind, actor).standing.bounds;
        qa_collision_family family = movement_kind == QA_MOVEMENT_Q3 ? QA_COLLISION_Q3
            : movement_kind == QA_MOVEMENT_Q2_CLASSIC || movement_kind == QA_MOVEMENT_Q2_RERELEASE
                ? QA_COLLISION_Q2 : QA_COLLISION_Q1;
        qa_actor_collision collision = {.family = family, .shape = QA_SHAPE_BOX,
            .contents = family == QA_COLLISION_Q3 ? 0x2000000 : family == QA_COLLISION_Q2 ? 0x2000000 : -2,
            .role = QA_COLLISION_SOLID};
        if (!qa_world_body_write(application->world, actor, &body, error) ||
            !qa_world_set_collision(application->world, actor,
                                    seat->spectator || !found ? NULL : &collision, error))
            return false;
        qa_builtin_motion_change change = {.body = body, .view_angles = body.angles,
                                             .reason = QA_BUILTIN_MOTION_RESET};
        if (!application_control_motion_changed(application, actor, &change, error)) return false;
        qa_combat_state traits;
        if (!qa_combat_read_traits(application->combat, actor, &traits, error)) return false;
        traits.can_take_damage = !seat->spectator && found;
        if (!qa_combat_set_traits(application->combat, actor, &traits, error)) return false;
        if (!found) {
            if (!application_control_player_mode(application, actor, QA_MOVEMENT_MODE_FREEZE,
                                                   seat->spectator, error)) return false;
            if (!qa_world_unlink(application->world, actor, error)) return false;
        } else if (!application_control_player_mode(application, actor,
                seat->spectator ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL,
                seat->spectator, error) || !qa_world_link(application->world, actor, NULL, error))
            return false;
    return true;
}

bool application_players_publish(qa_application *application,
                                  const qa_launch_choices *choices,
                                  application_player_travel *travel, qa_error *error)
{
    if (travel == NULL || travel->roster == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "player publication has no retained roster");
    if (travel->roster->map_provider->kind == APPLICATION_PROVIDER_Q3) {
        static const char *const names[] = {"info_player_start", "info_player_deathmatch",
            "info_player_intermission", "team_CTF_redplayer", "team_CTF_blueplayer",
            "team_CTF_redspawn", "team_CTF_bluespawn"};
        uint32_t cursor = 0;
        qa_q3_map_spawnpoint source;
        while (qa_q3_map_spawnpoint_next(travel->roster->map_provider->state.q3, &cursor, &source)) {
            if ((unsigned)source.kind >= sizeof(names) / sizeof(names[0]))
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source spawnpoint has unknown kind");
            qa_mode_spawnpoint point = {.actor = source.actor, .origin = source.origin,
                .angles = source.angles, .flags = source.flags,
                .no_bots = source.no_bots, .no_humans = source.no_humans};
            if (!qa_strings_intern_cstr(qa_session_strings(application->session), names[source.kind],
                                         &point.classname, error))
                return false;
            bool retained = false;
            for (size_t i = 0; i < travel->roster->point_count; ++i)
                if (travel->roster->points[i].ordinal == source.ordinal) {
                    travel->roster->points[i].point = point;
                    retained = true;
                    break;
                }
            if (!retained && !application_players_point(travel, point, QA_STRING_NONE, source.ordinal, error))
                return false;
        }
        size_t count = 0;
        for (size_t i = 0; i < travel->roster->point_count; ++i)
            if (travel->roster->points[i].point.classname != QA_STRING_NONE)
                travel->roster->points[count++] = travel->roster->points[i];
        travel->roster->point_count = count;
    }
    application_players_close(application);
    application->players = travel->roster;
    travel->roster = NULL;
    if (!prepare_spawnpoints(application, application->players, choices, error) ||
        !configure_q2_players(application, choices, error))
        return false;
    for (size_t i = 0; i < travel->count; ++i)
        if (!publish_player(application, choices, &travel->seats[i],
                            &application->players->records[i], &travel->carry[i], i,
                            travel->carry_players, travel->new_unit,
                            travel->has_landmark ? &travel->landmark : NULL,
                            application->players->records[i].source_begin_pending, error))
            return false;
    for (size_t i = 0; i < application->mode_count; ++i) {
        qa_mode_view view;
        if (!qa_modes_read(application->modes, application->mode_ids[i], &view, error) ||
            (view.rules.enabled &&
             !qa_modes_start_relics(application->modes, application->mode_ids[i], error)))
            return false;
    }
    return true;
}

bool application_players_advance(qa_application *application, qa_error *error)
{
    if (application->players == NULL)
        return true;
    for (size_t i = 0; i < application->players->count; ++i) {
        application_player_record *record = &application->players->records[i];
        if (!record->deferred || !qa_actors_get(qa_session_actors(application->session), record->actor))
            continue;
        qa_body_state body;
        bool found;
        if (!qa_world_body_read(application->world, record->actor, &body, error) ||
            !spawn_pose(application, i, qa_session_elapsed(application->session) >= record->deferred_until_ns,
                         &body, &found, error))
            return false;
        if (!found) continue;
        qa_builtin_motion_change change = {.body = body, .view_angles = body.angles,
            .reason = QA_BUILTIN_MOTION_RESET, .force_view_angles = true};
        if (!qa_world_body_write(application->world, record->actor, &body, error) ||
            !application_control_motion_changed(application, record->actor, &change, error) ||
            !application_record_motion_change(application, record->actor, &change, error))
            return false;
        application_provider *movement = application_provider_for(application, record->actor, QA_ROLE_MOVEMENT, "");
        qa_collision_family family = movement->component.clock.kind == QA_CLOCK_Q3 ? QA_COLLISION_Q3
            : movement->component.clock.kind == QA_CLOCK_Q2_CLASSIC || movement->component.clock.kind == QA_CLOCK_Q2_RERELEASE
                ? QA_COLLISION_Q2 : QA_COLLISION_Q1;
        qa_actor_collision collision = {.family = family, .shape = QA_SHAPE_BOX,
            .contents = family == QA_COLLISION_Q1 ? -2 : 0x2000000, .role = QA_COLLISION_SOLID};
        qa_combat_state traits;
        if (!qa_combat_read_traits(application->combat, record->actor, &traits, error)) return false;
        traits.can_take_damage = !record->spectator;
        if (!qa_combat_set_traits(application->combat, record->actor, &traits, error) ||
            !qa_world_set_collision(application->world, record->actor,
                                     record->spectator ? NULL : &collision, error) ||
            !application_control_player_mode(application, record->actor,
                record->spectator ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL,
                record->spectator, error) ||
            !qa_world_link(application->world, record->actor, NULL, error)) return false;
        record->deferred = false;
    }
    return true;
}

static char *player_text(const char *value)
{
    if (value == NULL) value = "";
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, value, length + 1);
    return copy;
}

static void record_free(application_player_record *record)
{
    free(record->name); free(record->team); free(record->skin);
    free(record->userinfo); free(record->guests);
    *record = (application_player_record){0};
}

static bool record_text(application_player_record *record, const char *name,
                        const char *team, const char *skin, const char *userinfo,
                        qa_error *error)
{
    record->name = player_text(name);
    record->team = player_text(team);
    record->skin = player_text(skin);
    record->userinfo = userinfo != NULL ? player_text(userinfo) : NULL;
    if (record->name == NULL || record->team == NULL || record->skin == NULL ||
        (userinfo != NULL && record->userinfo == NULL)) {
        record_free(record);
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain player connection text");
    }
    return true;
}

bool qa_application_player_seat(const qa_application *application, qa_actor_id actor,
                                 uint32_t *out)
{
    if (application == NULL || application->players == NULL || out == NULL ||
        application->session == NULL ||
        !qa_actors_get(qa_session_actors(application->session), actor)) return false;
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = &application->players->records[i];
        if (!record->retiring && qa_actor_id_equal(record->actor, actor)) {
            *out = record->seat;
            return true;
        }
    }
    return false;
}

bool qa_application_remote_player_actor(const qa_application *application,
                                         qa_net_client_id client, qa_net_seat_id seat,
                                         qa_actor_id *out)
{
    if (application == NULL || application->players == NULL || out == NULL ||
        application->session == NULL) return false;
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = &application->players->records[i];
        if (record->remote && !record->retiring && qa_net_client_id_equal(record->remote_client, client) &&
            (record->remote_seat.owner == seat.owner && record->remote_seat.index == seat.index) &&
            qa_actors_get(qa_session_actors(application->session), record->actor)) {
            *out = record->actor;
            return true;
        }
    }
    return false;
}

static application_provider *current_seat_provider(qa_application *application,
                                                    const qa_launch_seat *seat,
                                                    qa_launch_role role)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, role, "");
    if (binding == NULL) return NULL;
    for (size_t i = 0; i < application->provider_count; ++i)
        if (!strcmp(application->providers[i]->launch->selection.instance, binding->instance))
            return application->providers[i];
    return NULL;
}

static bool record_append(qa_application *application, application_player_record record,
                           size_t *out, qa_error *error)
{
    struct application_player_roster *roster = application->players;
    /* Stable addresses are required while nested guest calls admit bots. The
     * roster reserves one row per canonical actor before map publication. */
    for (size_t i = 0; i < roster->count; ++i)
        if (roster->records[i].dynamic && roster->records[i].actor.registry == 0 &&
            roster->records[i].character == NULL) {
            roster->records[i] = record;
            *out = i;
            return true;
        }
    if (roster->count == roster->capacity)
        return application_fail(error, QA_ERROR_MEMORY, "canonical player roster capacity is exhausted");
    *out = roster->count;
    roster->records[roster->count++] = record;
    return true;
}

bool qa_application_remote_player_attach(qa_application *application,
    const qa_application_remote_player_request *request, qa_actor_id *out, qa_error *error)
{
    if (application == NULL || request == NULL || out == NULL || request->name == NULL ||
        application->players == NULL || application->operation != APPLICATION_IDLE ||
        application->session == NULL || !qa_session_safe(application->session) ||
        application->state == QA_APPLICATION_FAULTED || application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT, "remote admission requires an idle active player roster");
    *out = (qa_actor_id){0};
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = &application->players->records[i];
        if (record->retiring) continue;
        if (record->seat == request->application_seat || (record->remote &&
            qa_net_client_id_equal(record->remote_client, request->client) && (record->remote_seat.owner == request->seat.owner && record->remote_seat.index == request->seat.index)))
            return application_fail(error, QA_ERROR_ARGUMENT, "remote player seat is already admitted");
    }
    qa_launch_seat seat = {.id = request->application_seat, .name = request->name,
        .team = request->team, .spectator = request->spectator, .bot = request->bot};
    application_provider *character = current_seat_provider(application, &seat, QA_ROLE_CHARACTER);
    application_provider *movement = current_seat_provider(application, &seat, QA_ROLE_MOVEMENT);
    application_provider *arsenal = current_seat_provider(application, &seat, QA_ROLE_ARSENAL);
    if (!player_adapter_available(character) || !player_adapter_available(movement) ||
        !player_adapter_available(arsenal))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "remote player selected roles lack an admission adapter");
    bool zero_based = character->component.clock.kind == QA_CLOCK_Q3;
    uint32_t source_slot = request->source_slot;
    if (source_slot == UINT32_MAX) {
        source_slot = zero_based ? 0 : 1;
        for (;;) {
            bool occupied = qa_actors_at_source(qa_session_actors(application->session),
                                                character->owner, source_slot) != NULL;
            for (size_t i = 0; i < application->players->count; ++i)
                occupied |= !application->players->records[i].retiring &&
                    application->players->records[i].client_slot == source_slot - (zero_based ? 0u : 1u);
            if (!occupied) break;
            if (source_slot == UINT32_MAX - 1)
                return application_fail(error, QA_ERROR_MEMORY, "remote player source slots are exhausted");
            ++source_slot;
        }
    }
    if ((!zero_based && source_slot == 0) || source_slot == UINT32_MAX ||
        qa_actors_at_source(qa_session_actors(application->session), character->owner, source_slot))
        return application_fail(error, QA_ERROR_ARGUMENT, "remote source slot is unavailable");
    uint32_t client_slot = source_slot - (zero_based ? 0u : 1u);
    for (size_t i = 0; i < application->players->count; ++i)
        if (!application->players->records[i].retiring && application->players->records[i].client_slot == client_slot)
            return application_fail(error, QA_ERROR_ARGUMENT, "remote client slot collides with an admitted player");
    application_player_record record = {.seat = seat.id, .client_slot = client_slot,
        .source_slot = source_slot, .character = character, .remote_client = request->client,
        .remote_seat = request->seat, .remote = true, .dynamic = true,
        .spectator = request->spectator, .bot = request->bot};
    if (!record_text(&record, request->name, request->team, request->skin, request->userinfo, error)) return false;
    size_t index;
    if (!record_append(application, record, &index, error)) { record_free(&record); return false; }
    application->operation = APPLICATION_CONFIGURING;
    application_player_carry carry = {0};
    bool ok = publish_player(application, qa_launch_snapshot_choices(qa_application_launch(application)),
                              &seat, &application->players->records[index], &carry, index,
                              false, false, NULL, request->defer_source_begin, error);
    application->operation = APPLICATION_IDLE;
    if (!ok) {
        /* Source callbacks can commit before an error. The actor and source
         * context remain owned by the faulted application until destruction. */
        application->players->records[index].retiring = true;
        application_fault(application, error);
        return false;
    }
    *out = application->players->records[index].actor;
    return true;
}

bool qa_application_remote_player_detach(qa_application *application,
    qa_net_client_id client, qa_net_seat_id seat, qa_error *error)
{
    if (application == NULL || application->players == NULL || application->session == NULL ||
        application->operation != APPLICATION_IDLE || !qa_session_safe(application->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "remote detach requires an idle active roster");
    size_t index;
    for (index = 0; index < application->players->count; ++index) {
        application_player_record *record = &application->players->records[index];
        if (record->remote && qa_net_client_id_equal(record->remote_client, client) && (record->remote_seat.owner == seat.owner && record->remote_seat.index == seat.index)) break;
    }
    if (index == application->players->count) return true;
    application_player_record *record = &application->players->records[index];
    record->retiring = true;
    application->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    qa_actor_id actor = record->actor;
    application_provider *character = record->character;
    if (character->kind == APPLICATION_PROVIDER_Q2)
        ok = qa_q2_player_disconnect(character->state.q2, actor, error);
    if (ok && qa_actors_get(qa_session_actors(application->session), actor))
        ok = qa_session_release(application->session, actor, error);
    application->operation = APPLICATION_IDLE;
    if (!ok) { application_fault(application, error); return false; }
    /* Release observers may reallocate the roster, so reacquire by identity. */
    for (index = 0; index < application->players->count; ++index) {
        record = &application->players->records[index];
        if (!record->remote || !qa_net_client_id_equal(record->remote_client, client) || (record->remote_seat.owner != seat.owner || record->remote_seat.index != seat.index)) continue;
        record_free(record);
        record->dynamic = true;
        break;
    }
    return true;
}

bool application_players_guest_attach(qa_application *application,
    application_provider *provider, uint32_t slot, qa_actor_id actor,
    const qa_builtin_player_info *info, qa_error *error)
{
    if (application == NULL || provider == NULL || info == NULL || info->name == NULL ||
        application->players == NULL || provider->application != application ||
        !qa_actors_get(qa_session_actors(application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "guest roster admission requires its live canonical actor");
    struct application_player_roster *roster = application->players;
    size_t index;
    for (index = 0; index < roster->count; ++index)
        if (qa_actor_id_equal(roster->records[index].actor, actor)) break;
    for (size_t i = 0; i < roster->count; ++i)
        for (size_t j = 0; j < roster->records[i].guest_count; ++j) {
            application_player_guest_binding *binding = &roster->records[i].guests[j];
            if (binding->owner != provider->owner || binding->source_slot != slot) continue;
            if (i != index || !qa_sha256_equal(&binding->identity, &provider->launch->identity))
                return application_fail(error, QA_ERROR_ARGUMENT, "guest source slot already belongs to another generation");
            return true;
        }
    bool created = index == roster->count;
    if (created) {
        uint32_t seat = 0;
        for (;;) {
            bool occupied = false;
            for (size_t i = 0; i < roster->count; ++i) occupied |= roster->records[i].seat == seat;
            if (!occupied) break;
            if (seat == UINT32_MAX)
                return application_fail(error, QA_ERROR_MEMORY, "guest application seats are exhausted");
            ++seat;
        }
        const qa_actor_record *source = qa_actors_get(qa_session_actors(application->session), actor);
        application_player_record record = {.seat = seat, .client_slot = slot,
            .source_slot = source->source_slot, .actor = actor, .character = provider,
            .spectator = info->spectator, .bot = true, .dynamic = true};
        if (!record_text(&record, info->name, "", info->skin, NULL, error)) return false;
        if (!record_append(application, record, &index, error)) { record_free(&record); return false; }
    }
    application_player_record *record = &roster->records[index];
    if (record->guest_count >= SIZE_MAX / sizeof(*record->guests))
        return application_fail(error, QA_ERROR_MEMORY, "guest player role bindings are exhausted");
    void *bindings = realloc(record->guests, (record->guest_count + 1) * sizeof(*record->guests));
    if (bindings == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain guest player role binding");
    record->guests = bindings;
    record->guests[record->guest_count++] = (application_player_guest_binding){
        .owner = provider->owner, .identity = provider->launch->identity, .source_slot = slot};
    if (!created) return true;
    /* ClientConnect/ClientBegin and source actor admission have already run.
     * Preserve their authoritative body, inventory and combat projections. */
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !application_guest_player_state(provider, actor, &combat, error)) goto failed;
    qa_combat_state existing;
    qa_error current = {0};
    if (!qa_combat_read_traits(application->combat, actor, &existing, &current)) {
        if (current.code != QA_ERROR_NOT_FOUND) { if (error) *error = current; goto failed; }
        if (!qa_combat_create_actor(application->combat, actor, &combat, error)) goto failed;
    }
    if (!qa_inventory_has(application->inventory, actor) &&
        !qa_inventory_create_actor(application->inventory, actor, NULL, 0, error)) goto failed;
    if (!qa_application_control_admit(application, actor, body.angles, error)) goto failed;
    if (application->modes != NULL) {
        qa_string_id name;
        if (!qa_strings_intern_cstr(qa_session_strings(application->session), info->name, &name, error) ||
            !qa_modes_player(application->modes, &(qa_match_player){.actor = actor, .name = name,
                .connected = info->connected, .bot = true}, error)) goto failed;
        for (size_t i = 0; i < application->mode_count; ++i)
            if (!qa_modes_join(application->modes, application->mode_ids[i], actor,
                                 combat.team, info->spectator, error)) goto failed;
    }
    application_provider *arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    if (arsenal == NULL)
        { application_fail(error, QA_ERROR_NOT_FOUND, "guest player selected arsenal disappeared"); goto failed; }
    if (arsenal->kind == APPLICATION_PROVIDER_Q1 &&
        !qa_q1_player_attach(arsenal->state.q1, actor, true, error)) goto failed;
    if (arsenal->kind == APPLICATION_PROVIDER_Q2 &&
        (!qa_q2_items_admit_player(arsenal->state.q2, actor, true, error) ||
         !qa_q2_weapon_bind(arsenal->state.q2, actor, QA_Q2_BLASTER, error))) goto failed;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *selected = application->providers[i];
        if (selected->kind != APPLICATION_PROVIDER_Q3) continue;
        uint32_t roles = (selected == arsenal ? QA_Q3_ARSENAL : 0u) |
            (selected == application_provider_for(application, actor, QA_ROLE_CHARACTER, "") ? QA_Q3_CHARACTER : 0u) |
            (selected == application_provider_for(application, actor, QA_ROLE_EFFECTS, "") ? QA_Q3_EFFECTS : 0u) |
            (selected == application_provider_for(application, actor, QA_ROLE_COMBAT, "") ? QA_Q3_COMBAT : 0u) |
            (selected == application_provider_for(application, actor, QA_ROLE_EQUIPMENT, "") ? QA_Q3_EQUIPMENT : 0u);
        if (roles && !qa_q3_bind_player(selected->state.q3, actor, roles, combat.health, error)) goto failed;
    }
    if (!application_guest_actor_admit(provider, actor, error)) goto failed;
    return true;
failed:
    /* The source owns the actor and may already have committed its callbacks. */
    application_fault(application, error);
    return false;
}

bool application_players_guest_detach(qa_application *application,
    application_provider *provider, uint32_t slot, qa_actor_id actor, qa_error *error)
{
    if (application == NULL || provider == NULL || provider->application != application)
        return application_fail(error, QA_ERROR_ARGUMENT, "guest roster detach has the wrong provider");
    if (application->players == NULL) return true;
    struct application_player_roster *roster = application->players;
    for (size_t i = 0; i < roster->count; ++i) {
        application_player_record *record = &roster->records[i];
        if (!qa_actor_id_equal(record->actor, actor)) continue;
        for (size_t j = 0; j < record->guest_count; ++j) {
            application_player_guest_binding *binding = &record->guests[j];
            if (binding->owner != provider->owner || binding->source_slot != slot) continue;
            if (!qa_sha256_equal(&binding->identity, &provider->launch->identity))
                return application_fail(error, QA_ERROR_ARGUMENT, "guest roster detach has another provider identity");
            memmove(binding, binding + 1, (record->guest_count - j - 1) * sizeof(*binding));
            --record->guest_count;
            if (record->dynamic && !record->remote && record->character == provider && record->guest_count == 0) {
                record_free(record);
                record->dynamic = true;
            }
            return true;
        }
        return true;
    }
    return true;
}

#define PLAYER_CHECKPOINT_HEADER 52u
#define PLAYER_CHECKPOINT_RECORD 104u
#define PLAYER_CHECKPOINT_POINT 60u
#define PLAYER_CHECKPOINT_Q1_POINT 16u
#define PLAYER_CHECKPOINT_GUEST 40u

static bool roster_size(size_t *size, size_t count, size_t width, qa_error *error)
{
    if (count > (SIZE_MAX - *size) / width)
        return application_fail(error, QA_ERROR_MEMORY, "player checkpoint extent is exhausted");
    *size += count * width;
    return true;
}

static bool roster_write_actor(qa_net_writer *writer, const qa_actor_registry *actors,
                                qa_actor_id actor, qa_error *error)
{
    qa_saved_actor_id saved = {0};
    if (actor.registry && !qa_actors_save_reference(actors, actor, &saved, error)) return false;
    return qa_net_write_u64(writer, saved.generation) && qa_net_write_u32(writer, saved.slot);
}

static bool roster_resolve_actor(const qa_actor_registry *actors, qa_saved_actor_id saved,
                                 bool present, qa_actor_id *out, qa_error *error)
{
    if (!present) {
        if (saved.generation || saved.slot)
            return application_fail(error, QA_ERROR_FORMAT, "absent roster actor carries a reference");
        *out = (qa_actor_id){0};
        return true;
    }
    return qa_actors_reference_saved(actors, saved, true, out, error);
}

static qa_saved_actor_id roster_read_saved(qa_net_reader *reader)
{
    qa_saved_actor_id saved;
    saved.generation = qa_net_read_u64(reader);
    saved.slot = qa_net_read_u32(reader);
    return saved;
}

static const char *roster_name(const qa_launch_choices *choices,
                               const application_player_record *record, bool team)
{
    const char *text = team ? record->team : record->name;
    if (text != NULL) return text;
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == record->seat)
            return team ? (choices->seats[i].team ? choices->seats[i].team : "") : choices->seats[i].name;
    return "";
}

static bool roster_write_text(qa_net_writer *writer, const char *text)
{
    size_t length = text == NULL ? 0 : strlen(text);
    return qa_net_write_u32(writer, (uint32_t)length) && qa_net_write_data(writer, text, length);
}

static bool roster_read_text(qa_net_reader *reader, char **out, qa_error *error)
{
    uint32_t length = qa_net_read_u32(reader);
    if (reader->failed || length > qa_net_reader_remaining(reader) || (uint64_t)length + 1 > SIZE_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "invalid player text extent");
    char *text = malloc((size_t)length + 1);
    if (text == NULL) return application_fail(error, QA_ERROR_MEMORY, "cannot retain restored player text");
    if (!qa_net_read_data(reader, text, length) || memchr(text, '\0', length) != NULL) {
        free(text);
        return application_fail(error, QA_ERROR_FORMAT, "invalid counted player text");
    }
    text[length] = '\0';
    *out = text;
    return true;
}

static void roster_write_vec(qa_net_writer *writer, qa_vec3 value)
{
    qa_net_write_f32(writer, value.x); qa_net_write_f32(writer, value.y); qa_net_write_f32(writer, value.z);
}

static qa_vec3 roster_read_vec(qa_net_reader *reader)
{
    qa_vec3 value = {.x = qa_net_read_f32(reader), .y = qa_net_read_f32(reader), .z = qa_net_read_f32(reader)};
    return value;
}

static bool roster_vector_valid(qa_vec3 value)
{ return isfinite(value.x) && isfinite(value.y) && isfinite(value.z); }

bool application_players_checkpoint_capture(qa_application *application, qa_buffer *out,
                                             qa_error *error)
{
    if (!application || !out || !application->session || !qa_session_safe(application->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "roster checkpoint requires an idle application");
    const struct application_player_roster *roster = application->players;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    const qa_actor_registry *actors = qa_session_actors(application->session);
    size_t size = PLAYER_CHECKPOINT_HEADER;
    qa_q1_spawn_selector_checkpoint selector = {0};
    if (roster != NULL) {
        if (roster->count > UINT32_MAX || roster->point_count > UINT32_MAX ||
            roster->q1_point_count > UINT32_MAX || roster->count > roster->capacity ||
            (roster->count && !roster->records) || (roster->point_count && !roster->points) ||
            (roster->q1_point_count && !roster->q1_points) || !roster->map_provider)
            return application_fail(error, QA_ERROR_FORMAT, "invalid canonical roster storage");
        if (roster->q1_selector &&
            !qa_q1_spawn_selector_checkpoint_capture(roster->q1_selector, &selector, error)) return false;
        if (!roster_size(&size, roster->count, PLAYER_CHECKPOINT_RECORD, error) ||
            !roster_size(&size, roster->point_count, PLAYER_CHECKPOINT_POINT, error) ||
            !roster_size(&size, roster->q1_point_count, PLAYER_CHECKPOINT_Q1_POINT, error)) return false;
        for (size_t i = 0; i < roster->count; ++i) {
            const application_player_record *record = &roster->records[i];
            if (record->guest_count > UINT32_MAX || (record->guest_count && !record->guests) ||
                !roster_size(&size, record->guest_count, PLAYER_CHECKPOINT_GUEST, error)) return false;
            const char *text[] = {roster_name(choices, record, false), roster_name(choices, record, true),
                                 record->skin, record->userinfo};
            for (size_t j = 0; j < 4; ++j) {
                size_t length = text[j] ? strlen(text[j]) : 0;
                if (length > UINT32_MAX || !roster_size(&size, length, 1, error)) return false;
            }
        }
    }
    qa_buffer buffer = {.data = malloc(size), .size = size};
    if (!buffer.data) return application_fail(error, QA_ERROR_MEMORY, "cannot encode canonical roster");
    qa_net_writer writer;
    qa_net_writer_init(&writer, buffer.data, size, error);
    qa_net_write_data(&writer, "QAPR", 4); qa_net_write_u32(&writer, 1);
    uint32_t flags = (roster != NULL ? 1u : 0u) | (roster && roster->q1_selector ? 2u : 0u) |
                     (selector.last.registry ? 4u : 0u);
    qa_net_write_u32(&writer, flags);
    qa_net_write_u32(&writer, roster ? roster->map_provider->owner : 0);
    qa_net_write_u32(&writer, roster ? roster->family : 0);
    qa_net_write_u32(&writer, roster ? roster->spawn_point : 0);
    qa_net_write_i32(&writer, roster ? roster->world_type : 0);
    qa_net_write_u32(&writer, roster ? (uint32_t)roster->count : 0);
    qa_net_write_u32(&writer, roster ? (uint32_t)roster->point_count : 0);
    qa_net_write_u32(&writer, roster ? (uint32_t)roster->q1_point_count : 0);
    bool ok = roster_write_actor(&writer, actors, selector.last, error);
    for (size_t i = 0; ok && roster && i < roster->count; ++i) {
        const application_player_record *record = &roster->records[i];
        qa_net_write_u32(&writer, record->seat); qa_net_write_u32(&writer, record->client_slot);
        qa_net_write_u32(&writer, record->source_slot);
        ok = roster_write_actor(&writer, actors, record->actor, error) &&
             roster_write_actor(&writer, actors, record->configured_actor, error);
        qa_net_write_u32(&writer, record->character ? record->character->owner : 0);
        qa_net_write_u64(&writer, record->deferred_until_ns);
        qa_net_write_u64(&writer, record->remote_client.owner);
        qa_net_write_u64(&writer, record->remote_client.generation);
        qa_net_write_u32(&writer, record->remote_client.slot);
        qa_net_write_u64(&writer, record->remote_seat.owner); qa_net_write_u32(&writer, record->remote_seat.index);
        uint32_t row_flags = (record->deferred ? 1u : 0u) | (record->spectator ? 2u : 0u) |
            (record->bot ? 4u : 0u) | (record->remote ? 8u : 0u) | (record->dynamic ? 16u : 0u) |
            (record->retiring ? 32u : 0u) | (record->userinfo != NULL ? 64u : 0u) |
            (record->actor.registry ? 128u : 0u) | (record->configured_actor.registry ? 256u : 0u) |
            (record->source_begin_pending ? 512u : 0u);
        qa_net_write_u32(&writer, row_flags); qa_net_write_u32(&writer, (uint32_t)record->guest_count);
        roster_write_text(&writer, roster_name(choices, record, false));
        roster_write_text(&writer, roster_name(choices, record, true));
        roster_write_text(&writer, record->skin); roster_write_text(&writer, record->userinfo);
        for (size_t j = 0; j < record->guest_count; ++j) {
            qa_net_write_u32(&writer, record->guests[j].owner);
            qa_net_write_data(&writer, record->guests[j].identity.bytes, 32);
            qa_net_write_u32(&writer, record->guests[j].source_slot);
        }
    }
    for (size_t i = 0; ok && roster && i < roster->point_count; ++i) {
        const application_player_point *point = &roster->points[i];
        ok = roster_write_actor(&writer, actors, point->point.actor, error);
        roster_write_vec(&writer, point->point.origin); roster_write_vec(&writer, point->point.angles);
        qa_net_write_u32(&writer, point->point.team); qa_net_write_u32(&writer, point->point.classname);
        qa_net_write_u32(&writer, point->point.flags);
        qa_net_write_u32(&writer, (point->point.no_bots ? 1u : 0u) | (point->point.no_humans ? 2u : 0u) |
                                  (point->point.actor.registry ? 4u : 0u));
        qa_net_write_u32(&writer, point->target); qa_net_write_u32(&writer, point->ordinal);
    }
    for (size_t i = 0; ok && roster && i < roster->q1_point_count; ++i) {
        ok = roster_write_actor(&writer, actors, roster->q1_points[i].actor, error);
        qa_net_write_u32(&writer, roster->q1_points[i].kind);
    }
    if (!ok || writer.failed || qa_net_writer_size(&writer) != size) {
        qa_buffer_free(&buffer);
        return ok ? application_fail(error, QA_ERROR_FORMAT, "canonical roster codec size disagrees") : false;
    }
    *out = buffer;
    return true;
}

static application_provider *roster_provider(qa_application *application, qa_actor_owner owner)
{
    for (size_t i = 0; i < application->provider_count; ++i)
        if (application->providers[i]->owner == owner) return application->providers[i];
    return NULL;
}

static bool roster_string(const qa_strings *strings, qa_string_id id)
{ return id == QA_STRING_NONE || qa_strings_text(strings, id).data != NULL; }

bool application_players_checkpoint_restore(qa_application *candidate, qa_bytes bytes,
                                             qa_error *error)
{
    if (!candidate || !candidate->session || !candidate->world || candidate->players != NULL ||
        !qa_session_safe(candidate->session) || !bytes.data || bytes.size < PLAYER_CHECKPOINT_HEADER ||
        memcmp(bytes.data, "QAPR", 4))
        return application_fail(error, QA_ERROR_ARGUMENT, "roster restore requires an isolated empty prepared candidate");
    const qa_actor_registry *actors = qa_session_actors(candidate->session);
    const qa_strings *strings = qa_session_strings(candidate->session);
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    uint32_t version = qa_net_read_u32(&reader), flags = qa_net_read_u32(&reader);
    qa_actor_owner map_owner = qa_net_read_u32(&reader);
    qa_bsp_family family = (qa_bsp_family)qa_net_read_u32(&reader);
    qa_string_id spawn_point = qa_net_read_u32(&reader);
    int32_t world_type = qa_net_read_i32(&reader);
    uint32_t count = qa_net_read_u32(&reader), points = qa_net_read_u32(&reader), q1_points = qa_net_read_u32(&reader);
    qa_saved_actor_id last = roster_read_saved(&reader);
    if (reader.failed || version != 1 || (flags & ~7u) || ((flags & 4u) && !(flags & 2u)) ||
        ((flags & 2u) && !(flags & 1u)))
        return application_fail(error, QA_ERROR_FORMAT, "invalid roster schema or presence flags");
    if (!(flags & 1u)) {
        if (flags || map_owner || family || spawn_point || world_type || count || points || q1_points ||
            last.generation || last.slot || !qa_net_reader_finish(&reader))
            return application_fail(error, QA_ERROR_FORMAT, "absent roster carries mutable state");
        return true;
    }
    size_t minimum = PLAYER_CHECKPOINT_HEADER;
    if (count > qa_actors_capacity(actors) || family < QA_BSP_Q1 || family > QA_BSP_Q3 ||
        !roster_string(strings, spawn_point) || !roster_size(&minimum, count, PLAYER_CHECKPOINT_RECORD, error) ||
        !roster_size(&minimum, points, PLAYER_CHECKPOINT_POINT, error) ||
        !roster_size(&minimum, q1_points, PLAYER_CHECKPOINT_Q1_POINT, error) || minimum > bytes.size)
        return application_fail(error, QA_ERROR_FORMAT, "invalid roster counts, family or string identity");
    struct application_player_roster *roster = calloc(1, sizeof(*roster));
    if (!roster) return application_fail(error, QA_ERROR_MEMORY, "cannot restore canonical roster");
    roster->capacity = qa_actors_capacity(actors); roster->count = count;
    roster->point_count = points; roster->q1_point_count = q1_points;
    roster->family = family; roster->spawn_point = spawn_point; roster->world_type = world_type;
    roster->map_provider = roster_provider(candidate, map_owner);
    roster->records = roster->capacity ? calloc(roster->capacity, sizeof(*roster->records)) : NULL;
    roster->points = points ? calloc(points, sizeof(*roster->points)) : NULL;
    roster->q1_points = q1_points ? calloc(q1_points, sizeof(*roster->q1_points)) : NULL;
    bool ok = roster->map_provider && (!roster->capacity || roster->records) &&
              (!points || roster->points) && (!q1_points || roster->q1_points);
    if (!ok) application_fail(error, roster->map_provider ? QA_ERROR_MEMORY : QA_ERROR_FORMAT,
                              "roster map provider or allocation is missing");
    for (size_t i = 0; ok && i < count; ++i) {
        application_player_record *record = &roster->records[i];
        record->seat = qa_net_read_u32(&reader); record->client_slot = qa_net_read_u32(&reader);
        record->source_slot = qa_net_read_u32(&reader);
        qa_saved_actor_id actor = roster_read_saved(&reader), configured = roster_read_saved(&reader);
        qa_actor_owner character = qa_net_read_u32(&reader);
        record->character = roster_provider(candidate, character);
        record->deferred_until_ns = qa_net_read_u64(&reader);
        record->remote_client.owner = qa_net_read_u64(&reader);
        record->remote_client.generation = qa_net_read_u64(&reader);
        record->remote_client.slot = qa_net_read_u32(&reader);
        record->remote_seat.owner = qa_net_read_u64(&reader); record->remote_seat.index = qa_net_read_u32(&reader);
        uint32_t row_flags = qa_net_read_u32(&reader), guests = qa_net_read_u32(&reader);
        record->deferred = (row_flags & 1u) != 0; record->spectator = (row_flags & 2u) != 0;
        record->bot = (row_flags & 4u) != 0; record->remote = (row_flags & 8u) != 0;
        record->dynamic = (row_flags & 16u) != 0; record->retiring = (row_flags & 32u) != 0;
        record->source_begin_pending = (row_flags & 512u) != 0;
        ok = !reader.failed && !(row_flags & ~1023u) && !record->retiring &&
            roster_resolve_actor(actors, actor, (row_flags & 128u) != 0, &record->actor, error) &&
            roster_resolve_actor(actors, configured, (row_flags & 256u) != 0, &record->configured_actor, error) &&
            roster_read_text(&reader, &record->name, error) && roster_read_text(&reader, &record->team, error) &&
            roster_read_text(&reader, &record->skin, error) && roster_read_text(&reader, &record->userinfo, error);
        if (!ok) break;
        if (!(row_flags & 64u)) {
            if (record->userinfo[0]) { ok = false; break; }
            free(record->userinfo); record->userinfo = NULL;
        }
        if ((character && !record->character) || (!character && record->actor.registry) ||
            (record->remote && (!record->dynamic || !record->remote_client.owner ||
                               !record->remote_client.generation || !record->remote_seat.owner)) ||
            (!record->remote && (record->remote_client.owner || record->remote_client.generation ||
                record->remote_client.slot || record->remote_seat.owner || record->remote_seat.index)) ||
            guests > qa_net_reader_remaining(&reader) / PLAYER_CHECKPOINT_GUEST) { ok = false; break; }
        const qa_actor_record *live = qa_actors_get(actors, record->actor);
        if (live && (live->owner != character || !live->has_source || live->source_slot != record->source_slot))
            { ok = false; break; }
        for (size_t j = 0; j < i; ++j) {
            const application_player_record *previous = &roster->records[j];
            if (live && qa_actors_get(actors, previous->actor) &&
                (previous->seat == record->seat || qa_actor_id_equal(previous->actor, record->actor) ||
                 (previous->remote && record->remote &&
                  qa_net_client_id_equal(previous->remote_client, record->remote_client) &&
                  previous->remote_seat.owner == record->remote_seat.owner &&
                  previous->remote_seat.index == record->remote_seat.index))) ok = false;
        }
        if (!ok) break;
        record->guest_count = guests;
        record->guests = guests ? calloc(guests, sizeof(*record->guests)) : NULL;
        if (guests && !record->guests) { application_fail(error, QA_ERROR_MEMORY, "cannot retain restored guest roster bindings"); ok = false; break; }
        for (size_t j = 0; ok && j < guests; ++j) {
            application_player_guest_binding *binding = &record->guests[j];
            binding->owner = qa_net_read_u32(&reader);
            qa_net_read_data(&reader, binding->identity.bytes, 32);
            binding->source_slot = qa_net_read_u32(&reader);
            application_provider *provider = roster_provider(candidate, binding->owner);
            if (reader.failed || !provider || !qa_sha256_equal(&provider->launch->identity, &binding->identity))
                { ok = false; break; }
            for (size_t k = 0; k < i; ++k)
                for (size_t l = 0; l < roster->records[k].guest_count; ++l)
                    if (roster->records[k].guests[l].owner == binding->owner &&
                        roster->records[k].guests[l].source_slot == binding->source_slot) ok = false;
            for (size_t k = 0; k < j; ++k)
                if (record->guests[k].owner == binding->owner && record->guests[k].source_slot == binding->source_slot) ok = false;
        }
    }
    for (size_t i = 0; ok && i < points; ++i) {
        application_player_point *point = &roster->points[i];
        qa_saved_actor_id actor = roster_read_saved(&reader);
        point->point.origin = roster_read_vec(&reader); point->point.angles = roster_read_vec(&reader);
        point->point.team = qa_net_read_u32(&reader); point->point.classname = qa_net_read_u32(&reader);
        point->point.flags = qa_net_read_u32(&reader);
        uint32_t point_flags = qa_net_read_u32(&reader);
        point->point.no_bots = (point_flags & 1u) != 0; point->point.no_humans = (point_flags & 2u) != 0;
        point->target = qa_net_read_u32(&reader); point->ordinal = qa_net_read_u32(&reader);
        ok = !reader.failed && !(point_flags & ~7u) && roster_vector_valid(point->point.origin) &&
             roster_vector_valid(point->point.angles) && roster_string(strings, point->point.team) &&
             point->point.classname != QA_STRING_NONE && roster_string(strings, point->point.classname) &&
             roster_string(strings, point->target) &&
             roster_resolve_actor(actors, actor, (point_flags & 4u) != 0, &point->point.actor, error);
        for (size_t j = 0; ok && j < i; ++j)
            if (roster->points[j].ordinal == point->ordinal) ok = false;
    }
    for (size_t i = 0; ok && i < q1_points; ++i) {
        qa_saved_actor_id actor = roster_read_saved(&reader);
        roster->q1_points[i].kind = (qa_q1_spawn_kind)qa_net_read_u32(&reader);
        ok = !reader.failed && roster->q1_points[i].kind >= QA_Q1_SPAWN_START &&
             roster->q1_points[i].kind <= QA_Q1_SPAWN_TEST &&
             roster_resolve_actor(actors, actor, true, &roster->q1_points[i].actor, error);
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (ok && (flags & 2u)) {
        qa_q1_spawn_selector_checkpoint selector;
        ok = roster->map_provider->kind == APPLICATION_PROVIDER_Q1 &&
             roster_resolve_actor(actors, last, (flags & 4u) != 0, &selector.last, error) &&
             create_q1_selector(candidate, roster, qa_launch_snapshot_choices(qa_application_launch(candidate)), error) &&
             qa_q1_spawn_selector_checkpoint_restore(roster->q1_selector, &selector, error);
    } else if (ok && (last.generation || last.slot || q1_points)) ok = false;
    if (!ok) {
        roster_free(roster);
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "roster continuation disagrees with candidate owners");
        return false;
    }
    candidate->players = roster;
    return true;
}

bool qa_application_remote_player_begin(qa_application *application, qa_net_client_id client,
                                         qa_net_seat_id seat, qa_error *error)
{
    if (!application || !application->players || !application->session ||
        application->operation != APPLICATION_IDLE || !qa_session_safe(application->session) ||
        application->state == QA_APPLICATION_FAULTED || application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT, "remote player begin requires an idle healthy roster");
    application_player_record *record = NULL;
    for (size_t i = 0; i < application->players->count; ++i) {
        application_player_record *candidate = &application->players->records[i];
        if (candidate->remote && !candidate->retiring && qa_net_client_id_equal(candidate->remote_client, client) &&
            candidate->remote_seat.owner == seat.owner && candidate->remote_seat.index == seat.index) {
            record = candidate; break;
        }
    }
    if (!record || !qa_actors_get(qa_session_actors(application->session), record->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "remote player begin has no live admitted actor");
    if (!record->source_begin_pending) return true;
    qa_actor_id actor = record->actor;
    application->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    for (size_t i = 0; ok && i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (provider->kind <= APPLICATION_PROVIDER_Q3 ||
            provider->kind == APPLICATION_PROVIDER_QC) continue;
        bool selected = false;
        for (size_t j = 0; j < sizeof(player_roles) / sizeof(player_roles[0]); ++j)
            selected |= application_provider_for(application, actor, player_roles[j], "") == provider;
        if (selected) {
            if (provider->kind == APPLICATION_PROVIDER_NATIVE &&
                provider->state.native.q2_engine != NULL)
                ok = application_native_q2_client_begin(provider, record->client_slot + 1, error);
            else if (provider->component.clock.kind == QA_CLOCK_Q3)
                ok = application_q3_guest_client_begin(provider, record->client_slot, error);
        }
        if (ok && !qa_actors_get(qa_session_actors(application->session), actor))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "remote player retired during source begin");
    }
    if (ok) {
        qa_body_state body;
        ok = qa_world_body_read(application->world, actor, &body, error);
        if (ok) {
            qa_builtin_motion_change change = {.body = body, .view_angles = body.angles,
                                                .reason = QA_BUILTIN_MOTION_RESET};
            ok = application_control_motion_changed(application, actor, &change, error);
        }
    }
    application->operation = APPLICATION_IDLE;
    if (!ok) { application_fault(application, error); return false; }
    record->source_begin_pending = false;
    return true;
}

#include "map_private.h"
#include "qa/game_q1_wire.h"
#include "native_q1_wire.h"
#include "native_q1_composition.h"
#include "qa/game_q1_bots.h"
#include "bots_npc.h"
#include "qa/game_q2_wire.h"
#include "unified_q2_native_events.h"
#include "native_q2_console.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_original_save.h"
#include "map_travel_private.h"
#include "map_players_private.h"
#include "portals.h"
#include "q3_round.h"
#include "native_q3_console.h"
#include "native_q1_console.h"
#include "q3_world_restart.h"
#include "native_q3_ipfilters.h"
#include "native_q3_settings.h"
#include "native_q3_session.h"
#include "native_maps.h"
#include "qa/text.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool monster_path_read(application_provider *, qa_actor_id, qa_q1_path_state *);
static bool monster_path_change(application_provider *, qa_actor_id, const qa_q1_path_change *, qa_error *);
static bool monster_path_touch(application_provider *, qa_actor_id, qa_actor_id, bool *, qa_error *);

static const qa_entity_property *entity_properties(const qa_entities *entities,
                                                   size_t index,
                                                   size_t *count)
{
    qa_entity_record record = entities->records[index];
    *count = record.property_count;
    return entities->properties + record.first_property;
}

static char *arena_text(qa_arena *arena, qa_bytes value, qa_error *error)
{
    if (value.size == SIZE_MAX)
        return NULL;
    char *text = qa_arena_alloc(arena, value.size + 1, 1, error);
    if (text == NULL)
        return NULL;
    memcpy(text, value.data, value.size);
    text[value.size] = '\0';
    return text;
}

static bool entity_optional_text(const qa_entities *entities, size_t entity,
                                 const char *key, qa_arena *arena,
                                 const char **out, qa_error *error)
{
    qa_bytes value;
    *out = NULL;
    if (!qa_entity_value(entities, entity, key, &value))
        return true;
    *out = arena_text(arena, value, error);
    return *out != NULL;
}

static bool entity_number(const qa_entities *entities, size_t entity,
                          const char *key, double fallback, double *out,
                          qa_error *error)
{
    qa_bytes value;
    if (!qa_entity_value(entities, entity, key, &value)) {
        *out = fallback;
        return true;
    }
    if (!qa_parse_number(value, out, error) || !isfinite(*out)) {
        application_fail(error, QA_ERROR_FORMAT,
                         "map entity contains an invalid number");
        return false;
    }
    return true;
}

static bool entity_float(const qa_entities *entities, size_t entity,
                         const char *key, float fallback, float *out,
                         qa_error *error)
{
    double value;
    if (!entity_number(entities, entity, key, fallback, &value, error))
        return false;
    if (value < -FLT_MAX || value > FLT_MAX) {
        application_fail(error, QA_ERROR_FORMAT,
                         "map entity number exceeds float range");
        return false;
    }
    *out = (float)value;
    return true;
}

static bool entity_i32(const qa_entities *entities, size_t entity,
                       const char *key, int32_t fallback, int32_t *out,
                       qa_error *error)
{
    double value;
    if (!entity_number(entities, entity, key, fallback, &value, error))
        return false;
    value = trunc(value);
    if (value < INT32_MIN || value > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT,
                                "map entity integer exceeds signed range");
    *out = (int32_t)value;
    return true;
}

static bool entity_u32(const qa_entities *entities, size_t entity,
                       const char *key, uint32_t fallback, uint32_t *out,
                       qa_error *error)
{
    double value;
    if (!entity_number(entities, entity, key, fallback, &value, error))
        return false;
    value = trunc(value);
    if (value < INT32_MIN || value > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT,
                                "map entity integer exceeds flag range");
    if (value < 0)
        value += 4294967296.0;
    *out = (uint32_t)value;
    return true;
}

static bool entity_vector(const qa_entities *entities, size_t entity,
                          const char *key, qa_vec3 fallback, qa_vec3 *out,
                          bool *present, qa_error *error)
{
    qa_bytes value;
    if (!qa_entity_value(entities, entity, key, &value)) {
        *out = fallback;
        if (present != NULL)
            *present = false;
        return true;
    }
    float components[3];
    size_t offset = 0;
    for (size_t index = 0; index < 3; ++index) {
        while (offset < value.size && qa_unicode_whitespace(value.data[offset]))
            ++offset;
        size_t start = offset;
        while (offset < value.size && !qa_unicode_whitespace(value.data[offset]))
            ++offset;
        double component;
        if (start == offset ||
            !qa_parse_number((qa_bytes){value.data + start, offset - start},
                             &component, error) ||
            !isfinite(component) || component < -FLT_MAX ||
            component > FLT_MAX)
            return application_fail(error, QA_ERROR_FORMAT,
                                    "map entity contains an invalid vector");
        components[index] = (float)component;
    }
    while (offset < value.size && qa_unicode_whitespace(value.data[offset]))
        ++offset;
    if (offset != value.size)
        return application_fail(error, QA_ERROR_FORMAT,
                                "map entity vector has extra components");
    *out = qa_v3(components[0], components[1], components[2]);
    if (present != NULL)
        *present = true;
    return true;
}

bool application_map_identity(qa_application *application,
                     const application_publication *publication,
                     qa_string_id *out, qa_error *error)
{
    const char *path = qa_launch_snapshot_choices(publication->candidate)
                           ->world.map;
    const char *name = strncmp(path, "maps/", 5) == 0 ? path + 5 : path;
    size_t length = strlen(name);
    if (length > 4 && strcmp(name + length - 4, ".bsp") == 0)
        length -= 4;
    if (length == 0)
        return application_fail(error, QA_ERROR_FORMAT,
                                "selected map has no identity");
    return qa_strings_intern(
        qa_session_strings(application->session),
        (qa_bytes){(const uint8_t *)name, length}, out, error);
}

static application_provider *publication_provider(
    const application_publication *publication, const char *name)
{
    if (name == NULL)
        return NULL;
    for (size_t index = 0; index < publication->next_count; ++index) {
        application_provider *provider = publication->next[index];
        if (provider != NULL && provider->launch != NULL &&
            strcmp(provider->launch->selection.instance, name) == 0)
            return provider;
    }
    return NULL;
}

bool application_map_prepare_content(qa_application *application,
                                       application_publication *publication,
                                       qa_error *error)
{
    if (application == NULL || publication == NULL ||
        !publication->map.lumps[QA_BSP_ENTITIES].present)
        return application_fail(error, QA_ERROR_FORMAT,
                                "selected BSP has no entity lump");
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    const qa_launch_binding *binding = qa_launch_binding_for(
        choices, (qa_launch_scope){.kind = QA_SCOPE_WORLD},
        QA_ROLE_ENTITIES, "");
    if (binding == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "world has no selected entity provider");
    publication->map_provider =
        publication_provider(publication, binding->instance);
    if (publication->map_provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "selected entity provider is absent");
    qa_entity_syntax syntax = publication->map.family == QA_BSP_Q3
                                  ? QA_ENTITY_Q3 : QA_ENTITY_Q1;
    if (!qa_entities_parse(publication->map.lumps[QA_BSP_ENTITIES].bytes,
                           syntax, &publication->entities, error))
        return false;
    publication->entities_parsed = true;
    return true;
}

bool application_map_prepare(qa_application *application,
                             application_publication *publication,
                             qa_error *error)
{
    if (!application_map_prepare_content(application, publication, error))
        return false;
    bool carry, unit;
    const qa_q2_landmark *landmark;
    application_map_travel_options(application, &carry, &unit, &landmark);
    application_provider *old_source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (application->world && old_source == publication->map_provider &&
        old_source->kind == APPLICATION_PROVIDER_Q3 &&
        !application_q3_world_restart_active(application) &&
        !application_native_q3_session_capture_carry(old_source, error))
        return false;
    if (!(application->campaign_travel && application->campaign_travel->players
        ? application_players_campaign_consume(application,publication,&publication->players,error)
        : application_players_prepare(application, publication, carry, unit,
                                      landmark, &publication->players, error)))
        return false;
    if (publication->map_provider->kind == APPLICATION_PROVIDER_Q1 &&
        publication->entities.count != 0) {
        int32_t world_type;
        if (!entity_i32(&publication->entities, 0, "worldtype", 0, &world_type, error))
            return false;
        application_players_world_type(publication->players, world_type);
    }
    return true;
}

static bool player_point_class(qa_bytes name)
{
    return (name.size >= 12 && !memcmp(name.data, "info_player_", 12)) ||
        (name.size >= 8 && !memcmp(name.data, "team_CTF", 8)) ||
        (name.size >= 13 && !memcmp(name.data, "dm_dball_team", 13)) ||
        (name.size == 15 && !memcmp(name.data, "testplayerstart", 15)) ||
        (name.size == 21 && !memcmp(name.data, "info_vote_destination", 21));
}

bool application_map_prepare_points(qa_application *application,
                                     application_publication *publication,
                                     qa_error *error)
{
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    size_t source_clients = choices->seat_count;
    if (publication->map_provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_options source;
        double seconds;
        if (!qa_q1_source_respawn_options_read(publication->map_provider->state.q1,
            &source, &seconds, error)) return false;
        source_clients = source.max_clients;
    } else if (publication->map_provider->kind == APPLICATION_PROVIDER_Q2) {
        int32_t clients;
        if (!application_native_q2_source_integer(publication->map_provider,
            "maxclients", &clients, error)) return false;
        if (clients < 1 || clients > 256)
            return application_fail(error, QA_ERROR_FORMAT,
                "Q2 spawn points lost their actual client reservation");
        source_clients = (uint32_t)clients;
    }
    qa_strings *strings = qa_session_strings(application->session);
    for (size_t i = 0; i < publication->entities.count; ++i) {
        qa_bytes name;
        if (!qa_entity_value(&publication->entities, i, "classname", &name))
            continue;
        if (!player_point_class(name))
            continue;
        if (publication->map_provider->kind == APPLICATION_PROVIDER_Q3) {
            if (i > UINT32_MAX)
                return application_fail(error, QA_ERROR_MEMORY,
                                        "Q3 authored entity ordinal is exhausted");
            /* Retain names without re-decoding source numeric fields or
             * admitting points that the native decoder filters out. */
            qa_bytes target_value;
            qa_string_id target = QA_STRING_NONE;
            if (qa_entity_value(&publication->entities, i, "targetname", &target_value) &&
                !qa_strings_intern(strings, target_value, &target, error))
                return false;
            if (target != QA_STRING_NONE && !application_players_point(publication->players,
                    (qa_mode_spawnpoint){0}, target, (uint32_t)i, error))
                return false;
            continue;
        }
        if (source_clients > UINT32_MAX || i > UINT32_MAX - source_clients)
            return application_fail(error, QA_ERROR_MEMORY, "authored client source slots are exhausted");
        qa_mode_spawnpoint point = {0};
        qa_string_id target = QA_STRING_NONE;
        bool angles_present;
        if (!qa_strings_intern(strings, name, &point.classname, error) ||
            !entity_vector(&publication->entities, i, "origin", qa_v3(0, 0, 0), &point.origin, NULL, error) ||
            !entity_vector(&publication->entities, i, "angles", qa_v3(0, 0, 0), &point.angles, &angles_present, error) ||
            !entity_u32(&publication->entities, i, "spawnflags", 0, &point.flags, error))
            return false;
        float angle = 0;
        if (!angles_present && !entity_float(&publication->entities, i, "angle", 0, &angle, error))
            return false;
        if (!angles_present) point.angles.y = angle;
        qa_bytes value;
        if (qa_entity_value(&publication->entities, i, "targetname", &value) &&
            !qa_strings_intern(strings, value, &target, error))
            return false;
        int32_t no_bots, no_humans;
        if (!entity_i32(&publication->entities, i, "nobots", 0, &no_bots, error) ||
            !entity_i32(&publication->entities, i, "nohumans", 0, &no_humans, error))
            return false;
        point.no_bots = no_bots != 0;
        point.no_humans = no_humans != 0;
        uint32_t ordinal = (uint32_t)(i + source_clients);
        if (!application_players_point(publication->players, point, target, ordinal, error))
            return false;
    }
    return true;
}

bool application_map_restore_points(qa_application *application,
    application_player_travel *travel, qa_error *error)
{
    application_publication publication = {
        .candidate = qa_application_launch(application),
        .next = application->providers, .next_count = application->provider_count,
        .players = travel};
    if (!application->map_resource || !travel || !travel->roster ||
        !qa_bsp_open(qa_resource_bytes(application->map_resource), &publication.map, error) ||
        !application_map_prepare_content(application, &publication, error))
        return false;
    struct application_player_roster *roster = travel->roster;
    application_provider *source = publication.map_provider;
    roster->map_provider = source;
    roster->family = publication.map.family;
    bool ok = true;
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_options options;
        double seconds;
        ok = qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error);
        if (ok) roster->world_type = options.world_type;
    }
    if (source->kind == APPLICATION_PROVIDER_Q1 || source->kind == APPLICATION_PROVIDER_Q2) {
        const qa_actor_registry *actors = qa_session_actors(application->session);
        uint32_t cursor = 0;
        const qa_actor_record *actor;
        while (ok && qa_actors_next(actors, &cursor, &actor)) {
            if (actor->owner != source->owner || !actor->has_source) continue;
            qa_mode_spawnpoint point = {.actor = actor->id};
            qa_string_id target = QA_STRING_NONE;
            if (source->kind == APPLICATION_PROVIDER_Q1) {
                qa_q1_presentation view;
                if (!qa_q1_game_presentation(source->state.q1, actor->id, &view)) continue;
                point.classname = view.classname;
                target = view.targetname;
            } else {
                qa_q2_wire_source_entity view;
                ok = qa_q2_wire_entity_read(source->state.q2, actor->source_slot, &view, error);
                if (!ok) break;
                point.classname = view.classname;
                point.flags = view.spawn_flags;
                if (!player_point_class(qa_strings_text(qa_session_strings(application->session),
                        point.classname))) continue;
                qa_authored_target authored;
                if (qa_q2_entity_authored(source->state.q2, actor->id, &authored))
                    target = authored.targetname;
            }
            if (!player_point_class(qa_strings_text(qa_session_strings(application->session),
                    point.classname))) continue;
            qa_body_state body;
            ok = qa_world_body_read(application->world, actor->id, &body, error);
            if (ok) {
                point.origin = body.origin;
                point.angles = body.angles;
                ok = application_players_point(travel, point, target, actor->source_slot, error);
            }
        }
    } else if (ok) {
        ok = application_map_prepare_points(application, &publication, error);
    }
    qa_entities_free(&publication.entities);
    return ok;
}

static bool emit_map_event(application_provider *provider,
                           qa_builtin_event event, qa_error *error)
{
    event.provider = provider->owner;
    event.time_ns = qa_session_elapsed(provider->application->session);
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        double elapsed;
        if (!qa_q1_game_clock_read(provider->state.q1, &event.time_ns, &elapsed))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Q1 map event lost its actual source clock");
    }
    return application_emit(provider->application, &event, error);
}

static bool q1_fog_player(void *opaque, qa_actor_id player, float density,
                            qa_vec3 color, float duration, qa_error *error)
{
    application_provider *provider = opaque;
    qa_string_id resource;
    if (!qa_strings_intern_cstr(qa_session_strings(provider->application->session),
                                "q1:fog", &resource, error))
        return false;
    qa_clock_state clock;
    if (!qa_session_clock(provider->application->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "Q1 fog source clock is missing");
    return application_emit(provider->application,
        &(qa_builtin_event){.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
            .provider = provider->owner, .time_ns = clock.frame.time_ns,
            .actor = player, .other = player, .resource = resource,
            .origin = color, .end = {duration, 0, 0}, .value = density}, error);
}

static bool q1_ctf_mode(application_provider *provider, qa_mode_id *out, qa_error *error)
{
    qa_mode_view view;
    if (!application_native_q1_composition_mode(provider->application, provider, out, error) ||
        !qa_modes_read(provider->application->modes, *out, &view, error)) return false;
    return view.rules.source == QA_MODE_THREEWAVE ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 map has no genuine ThreeWave source controller");
}

static bool q1_ctf_state(void *opaque, qa_actor_id actor,
                           qa_q1_ctf_map_state *out, qa_error *error)
{
    application_provider *provider = opaque;
    qa_mode_id id;
    if (!q1_ctf_mode(provider, &id, error)) return false;
    if (!actor.registry) {
        qa_mode_view view;
        if (!qa_modes_read(provider->application->modes, id, &view, error)) return false;
        *out = (qa_q1_ctf_map_state){.start_map = view.rules.start_map,
                                     .pregame_over = view.ctf_pregame_over};
    } else {
        qa_mode_ctf_view view;
        if (!qa_modes_ctf_read(provider->application->modes, id, actor, &view, error)) return false;
        *out = (qa_q1_ctf_map_state){.start_map = view.start_map,
            .pregame_over = view.pregame_over, .observer = view.observer};
    }
    return true;
}

static bool q1_ctf_pregame_end(void *opaque, qa_error *error)
{
    application_provider *provider = opaque;
    qa_mode_id id;
    return q1_ctf_mode(provider, &id, error) &&
        qa_modes_ctf_pregame_end(provider->application->modes, id, error);
}

static bool q1_static_model(void *opaque, const qa_q1_static_model *model,
                            qa_error *error)
{
    application_provider *provider = opaque;
    return emit_map_event(provider,
                          (qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
                                             .family = QA_GAME_Q1,
                                             .resource = model->model,
                                             .origin = model->origin,
                                             .direction = model->angles,
                                             .frame = model->frame,
                                             .code = model->skin,
                                             .channel = model->color_map},
                          error);
}

static bool q1_ambient(void *opaque, qa_vec3 origin, qa_string_id sound,
                       float volume, float attenuation, qa_error *error)
{
    application_provider *provider = opaque;
    return emit_map_event(provider,
                          (qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                             .family = QA_GAME_Q1,
                                             .resource = sound,
                                             .flags = 1,
                                             .origin = origin,
                                             .volume = volume,
                                             .attenuation = attenuation},
                          error);
}

static bool q1_lightstyle(void *opaque, int32_t style, qa_string_id pattern,
                          qa_error *error)
{
    application_provider *provider = opaque;
    if (!qa_q1_wire_lightstyle(provider->state.q1, style, pattern, error))
        return false;
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_LIGHT,
                                             .family = QA_GAME_Q1,
                                             .resource = pattern,
                                             .code = style},
                          error);
}

static bool q1_integer_intent(void *opaque, int32_t value, qa_error *error)
{
    application_provider *provider = opaque;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!cvars || value < 0 || value > 3 || !qa_cvars_set_number(cvars, "skill", (float)value, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 skill intent has no qualified source value");
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .code = value},
                          error);
}

static bool q1_player_exited(void *opaque, qa_actor_id actor, qa_error *error)
{
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .actor = actor,
                                             .code = QA_Q1_CLIENT_EXIT},
                          error);
}

static bool q1_secret(void *opaque, qa_actor_id source, qa_actor_id player,
                      uint32_t total, uint32_t found, qa_error *error)
{
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
                                             .family = QA_GAME_Q1,
                                             .actor = player,
                                             .other = source,
                                             .code = (int32_t)found,
                                             .count = (int32_t)total},
                          error);
}

static application_provider *q1_alpha_provider(application_provider *origin,
                                                qa_actor_id actor,
                                                qa_error *error)
{
    application_provider *selected = application_provider_for(
        origin->application, actor, QA_ROLE_BODY, "");
    if (selected == NULL || !selected->constructed || selected->close_pending) {
        application_fail(error, QA_ERROR_NOT_FOUND,
                         "Q1 alpha target has no selected body owner");
        return NULL;
    }
    return selected;
}

static bool q1_alpha_read(void *opaque, qa_actor_id actor, float *out,
                           qa_error *error)
{
    application_provider *selected = q1_alpha_provider(opaque, actor, error);
    if (selected == NULL)
        return false;
    switch (selected->kind) {
    case APPLICATION_PROVIDER_Q1: {
        qa_q1_presentation source;
        if (!qa_q1_game_presentation(selected->state.q1, actor, &source))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Selected Q1 actor has no source alpha owner");
        *out = source.visual.alpha;
        return true;
    }
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_alpha_read(selected->state.q2, actor, out, error);
    case APPLICATION_PROVIDER_Q3:
        return qa_q3_alpha_read(selected->state.q3, actor, out, error);
    default:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Selected guest body has no admitted alpha adapter");
    }
}

static bool q1_alpha_write(void *opaque, qa_actor_id actor, float value,
                            qa_error *error)
{
    application_provider *selected = q1_alpha_provider(opaque, actor, error);
    if (selected == NULL)
        return false;
    switch (selected->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_game_alpha(selected->state.q1, actor, value, error);
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_alpha(selected->state.q2, actor, value, error);
    case APPLICATION_PROVIDER_Q3:
        return qa_q3_alpha(selected->state.q3, actor, value, error);
    default:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Selected guest body has no admitted alpha adapter");
    }
}

static application_provider *q1_actor_provider(application_provider *origin,
                                                qa_actor_id actor)
{
    qa_application *application = origin->application;
    const qa_launch_snapshot *snapshot = application->routing_snapshot;
    if (snapshot == NULL)
        snapshot = qa_configuration_current(application->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    const qa_launch_binding *monster = NULL;
    if (choices != NULL)
        for (size_t index = 0; index < choices->binding_count; ++index) {
            const qa_launch_binding *candidate = &choices->bindings[index];
            if (candidate->role == QA_ROLE_MONSTERS &&
                candidate->scope.kind == QA_SCOPE_ACTOR &&
                qa_actor_id_equal(candidate->scope.actor, actor) &&
                candidate->selector[0] == '\0') {
                monster = candidate;
                break;
            }
        }
    application_provider *provider =
        monster == NULL
            ? application_provider_for(application, actor,
                                       QA_ROLE_CHARACTER, "")
            : application_provider_for(application, actor,
                                       QA_ROLE_MONSTERS, "");
    if (provider == NULL)
        provider = application_provider_for(application, actor,
                                            QA_ROLE_ENTITIES, "");
    return provider;
}

static bool q1_path_touch(void *opaque, qa_actor_id corner,
                          qa_actor_id follower, bool *handled,
                          qa_error *error)
{
    application_provider *provider = q1_actor_provider(opaque, follower);
    if (handled == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 path touch needs an output");
    if (qa_targets_monster(((application_provider *)opaque)->application->targets, follower))
        return monster_path_touch(opaque, corner, follower, handled, error);
    *handled = false;
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1
               ? qa_q1_game_rogue_path_touch(provider->state.q1, corner,
                                             follower, handled, error)
               : application_fail(
                     error, QA_ERROR_UNSUPPORTED,
                     "selected follower has no Q1 Rogue path adapter");
}

static bool q1_path_read(void *opaque, qa_actor_id actor,
                         qa_q1_path_state *out)
{
    if (qa_targets_monster(((application_provider *)opaque)->application->targets, actor))
        return monster_path_read(opaque, actor, out);
    application_provider *provider = q1_actor_provider(opaque, actor);
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1 &&
           qa_q1_game_path_read(provider->state.q1, actor, out);
}

static bool q1_path_change(void *opaque, qa_actor_id actor,
                           const qa_q1_path_change *change, qa_error *error)
{
    if (qa_targets_monster(((application_provider *)opaque)->application->targets, actor))
        return monster_path_change(opaque, actor, change, error);
    application_provider *provider = q1_actor_provider(opaque, actor);
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1
               ? qa_q1_game_path_change(provider->state.q1, actor, change,
                                        error)
               : application_fail(error, QA_ERROR_UNSUPPORTED,
                                  "selected character has no Q1 path adapter");
}

static bool q1_control_player(void *opaque, qa_actor_id actor,
                              qa_vec3 origin, qa_vec3 angles,
                              qa_vec3 view_offset, qa_error *error)
{
    application_provider *map = opaque;
    qa_application *application = map == NULL ? NULL : map->application;
    return application != NULL
               ? application_control_cutscene(application, actor, origin,
                                              angles, view_offset, error)
               : application_fail(error, QA_ERROR_ARGUMENT,
                                  "Q1 cinematic control has no application");
}

static bool q1_finale(void *opaque, const qa_q1_map_finale_view *view,
                      qa_error *error)
{
    application_provider *provider = opaque;
    qa_q1_game_finale_reset(provider->state.q1);
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
                                             .family = QA_GAME_Q1,
                                             .resource = view->map,
                                             .text = view->text,
                                             .origin = view->origin,
                                             .direction = view->angles,
                                             .value = (float)view->exit_after,
                                             .flags = UINT32_C(0x80000000),
                                             .code = (int32_t)view->stage},
                          error);
}

static bool q1_finale_finished(void *opaque)
{
    application_provider *provider = opaque;
    return qa_q1_game_finale_finished(provider->state.q1);
}

static bool q1_finish_campaign(void *opaque, qa_error *error)
{
    application_provider *provider = opaque;
    return emit_map_event(provider,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .code = INT32_MAX},
                          error) &&
           application_record_level(provider,
                                    provider->application->current_map,
                                    error);
}

static bool q1_server_command(void *opaque, qa_string_id command,
                              qa_error *error)
{
    return application_map_server_command(opaque, command, error) && emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .text = command,
                                             .code = INT32_MAX - 1},
                          error);
}

static bool q1_level_begin(void *opaque, qa_string_id map, qa_actor_id cause,
                           double exit_after, qa_error *error)
{
    application_provider *provider = opaque;
    qa_string_id music;
    if (!qa_strings_intern_cstr(qa_session_strings(provider->application->session), "music", &music, error) ||
        !emit_map_event(opaque, (qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
            .family = QA_GAME_Q1, .resource = music, .code = 3, .count = 3}, error)) return false;
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .actor = cause,
                                             .text = map,
                                             .value = (float)exit_after,
                                             .flags = UINT32_C(0x80000000),
                                             .code = 1},
                          error);
}

static bool q1_level_travel(void *opaque, qa_string_id map, qa_actor_id cause,
                            qa_error *error)
{
    application_provider *provider = opaque;
    const char *destination = qa_strings_cstr(qa_session_strings(provider->application->session), map);
    if (destination == NULL || !application_source_queue_travel(provider->application,
        &(qa_application_travel_request){.provider = provider->owner, .cause = cause,
            .expression = destination, .carry_players = true, .complete_campaign = true}, error))
        return false;
    return emit_map_event(provider,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .actor = cause,
                                             .text = map,
                                             .code = 2},
                          error) &&
           application_record_level(provider,
                                    provider->application->current_map,
                                    error);
}

static bool q1_level_achievement(void *opaque, qa_actor_id player,
                                 const char *name, qa_error *error)
{
    application_provider *provider = opaque;
    qa_string_id text;
    if (!qa_strings_intern_cstr(
            qa_session_strings(provider->application->session), name, &text,
            error))
        return false;
    return emit_map_event(provider,
                          (qa_builtin_event){.kind = QA_BUILTIN_ACHIEVEMENT,
                                             .family = QA_GAME_Q1,
                                             .actor = player,
                                             .text = text},
                          error) &&
           application_record_achievement(provider, player, text, error);
}

static bool q1_defer_begin(void *opaque, double delay, qa_error *error)
{
    application_provider *provider = opaque;
    return qa_q1_game_map_defer_level(provider->state.q1, delay, error);
}

static qa_actor_id q1_campaign_actor(application_provider *provider,
                                     qa_actor_id actor)
{
    return actor.registry ? actor : provider->application->physics->world_actor;
}

static qa_string_id q1_campaign_read(void *opaque, qa_actor_id actor,
                                     qa_q1_campaign_text field)
{
    application_provider *provider = opaque;
    return qa_q1_game_map_text(provider->state.q1,
                               q1_campaign_actor(provider, actor), field);
}

static bool q1_campaign_write(void *opaque, qa_actor_id actor,
                              qa_q1_campaign_text field, qa_string_id value,
                              qa_error *error)
{
    application_provider *provider = opaque;
    return qa_q1_game_map_set_text(provider->state.q1,
                                   q1_campaign_actor(provider, actor), field,
                                   value, error);
}

static float q1_campaign_cvar(void *opaque, const char *name)
{
    application_provider *provider = opaque;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    return value ? value->number : 0;
}

static bool q1_campaign_command(void *opaque, const char *command,
                                qa_error *error)
{
    application_provider *provider = opaque;
    qa_string_id text;
    return qa_strings_intern_cstr(
               qa_session_strings(provider->application->session), command,
               &text, error) &&
           q1_server_command(provider, text, error);
}

static bool q1_campaign_achievement(void *opaque, const char *name,
                                    qa_error *error)
{
    return q1_level_achievement(opaque, (qa_actor_id){0}, name, error);
}

typedef enum q1_horde_action { Q1_HORDE_CONTROL, Q1_HORDE_KEYS, Q1_HORDE_FINISH } q1_horde_action;

static bool q1_horde_route(application_provider *provider, q1_horde_action action,
    qa_actor_id actor, bool check_wave, bool gold, int change, qa_error *error)
{
    qa_application *app = provider->application;
    bool found = false;
    for (size_t i = 0; app->modes && i < app->mode_count; ++i) {
        qa_mode_view view;
        if (!qa_modes_read(app->modes, app->mode_ids[i], &view, error))
            return false;
        if (view.rules.source != QA_MODE_Q1_HORDE || !view.rules.enabled)
            continue;
        qa_actor_id manager;
        if (!qa_modes_horde_manager_actor(app->modes, app->mode_ids[i], &manager, error))
            return false;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), manager);
        if (!record || record->owner != provider->owner)
            continue;
        found = true;
        if (action == Q1_HORDE_KEYS) {
            qa_horde_view horde;
            if (!qa_modes_horde_read(app->modes, app->mode_ids[i], &horde, error))
                return false;
            if (change < 0 && (gold ? horde.gold_keys : horde.silver_keys) <= 0)
                continue;
            return qa_modes_horde_keys(app->modes, app->mode_ids[i], gold, change, error);
        }
        bool ok = action == Q1_HORDE_CONTROL
            ? (check_wave ? qa_modes_horde_check(app->modes, app->mode_ids[i], error)
                          : qa_modes_horde_toggle_point(app->modes, app->mode_ids[i], actor, error))
            : qa_modes_horde_finish(app->modes, app->mode_ids[i], error);
        if (!ok)
            return false;
    }
    if (action == Q1_HORDE_KEYS)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "authored Horde door has no available managed key");
    return found || action == Q1_HORDE_FINISH ||
           application_fail(error, QA_ERROR_NOT_FOUND,
                            "authored Horde control has no configured mode owner");
}

static bool q1_horde_control(void *opaque, qa_actor_id actor, bool check_wave,
                               qa_error *error)
{
    return q1_horde_route(opaque, Q1_HORDE_CONTROL, actor, check_wave, false, 0, error);
}

static bool q1_horde_keys(void *opaque, bool gold, int change, qa_error *error)
{
    return q1_horde_route(opaque, Q1_HORDE_KEYS, (qa_actor_id){0}, false, gold, change, error);
}

static bool q1_campaign_finish_horde(void *opaque, double seconds,
                                     qa_error *error)
{
    return q1_horde_route(opaque, Q1_HORDE_FINISH, (qa_actor_id){0}, false, false, 0, error) &&
           emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .value = (float)seconds,
                                             .code = 3},
                          error);
}

static bool q1_campaign_schedule(void *opaque, qa_q1_campaign_timer timer,
                                 double delay, qa_error *error)
{
    application_provider *provider = opaque;
    return qa_q1_game_map_defer_finale(provider->state.q1, timer, delay,
                                       error);
}

static bool q1_campaign_source_options(
    application_provider *provider, const qa_product *product,
    qa_string_id current_map, qa_q1_campaign_source_options *out, qa_error *error)
{
    qa_q1_options source;
    if (provider->application->operation == APPLICATION_PERSISTING &&
        !provider->native_q1_restore_game.data) {
        if (!qa_q1_source_respawn_options_prepared(provider->state.q1, &source, error))
            return false;
    } else {
        double seconds;
        if (!qa_q1_source_respawn_options_read(provider->state.q1, &source, &seconds, error))
            return false;
    }
    bool registered;
    if (!qa_catalog_q1_registered(provider->application->catalog, product->id, &registered, error))
        return false;
    *out = (qa_q1_campaign_source_options){
        .session = provider->application->session,
        .program = application_q1_program(product->campaign),
        .current_map = current_map,
        .world = provider->application->physics->world_actor,
        .server_flags = &provider->q1_server_flags,
        .rerelease = product->edition == QA_EDITION_RERELEASE,
        .coop = source.coop,
        .deathmatch = source.deathmatch != 0,
        .registered = registered,
        .official_campaign = product->builtin,
        .skill = source.skill,
        .context = provider,
        .read_text = q1_campaign_read,
        .write_text = q1_campaign_write,
        .cvar = q1_campaign_cvar,
        .command = q1_campaign_command,
        .achievement = q1_campaign_achievement,
        .finish_horde = q1_campaign_finish_horde,
        .finale_finished = q1_finale_finished,
        .schedule = q1_campaign_schedule,
        .travel = q1_level_travel,
    };
    if (out->program < QA_Q1_ID1 || out->program > QA_Q1_CTF)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 campaign has no native program profile");
    return true;
}

static bool q1_retire_actor(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *provider = opaque;
    return qa_session_release(provider->application->session, actor, error);
}

static bool q1_schedule_remove(void *opaque, qa_actor_id actor, double delay,
                               qa_error *error)
{
    application_provider *provider = opaque;
    return qa_q1_game_map_defer_remove(provider->state.q1, actor, delay, error);
}

static bool q1_map_options(application_provider *provider,
                           const qa_product *product,
                           qa_string_id current_map,
                           qa_q1_campaign_source **source_out,
                           qa_q1_level **level_out,
                           qa_q1_map_options *map_out, qa_error *error)
{
    *source_out = NULL;
    *level_out = NULL;
    qa_q1_campaign_source_options source_options;
    if (!q1_campaign_source_options(provider, product, current_map,
                                    &source_options, error))
        return false;
    qa_q1_intermission_rule rule;
    size_t rule_count = 0;
    if (source_options.program >= QA_Q1_HIPNOTIC &&
        source_options.program <= QA_Q1_MG3) {
        *source_out = qa_q1_campaign_source_create(&source_options, error);
        if (*source_out == NULL ||
            !qa_q1_campaign_source_rule(*source_out, &rule, error))
            goto failed;
        rule_count = 1;
    }

    qa_builtin_services services = application_builtin_services(
        provider->application, provider->application->world,
        provider->application->physics);
    qa_q1_level_options level_options = {
        .services = services,
        .current_map = current_map,
        .server_flags = &provider->q1_server_flags,
        .rerelease = source_options.rerelease,
        .deathmatch = source_options.deathmatch,
        .registered = source_options.registered,
        .official_campaign = source_options.official_campaign,
        .skill = source_options.skill,
        .rules = rule_count == 0 ? NULL : &rule,
        .rule_count = rule_count,
        .context = provider,
        .begin = q1_level_begin,
        .travel = q1_level_travel,
        .achievement = q1_level_achievement,
        .defer_begin = q1_defer_begin,
    };
    *level_out = qa_q1_level_create(&level_options, error);
    if (*level_out == NULL)
        goto failed;

    *map_out = (qa_q1_map_options){
        .targets = provider->application->targets,
        .level = *level_out,
        .campaign_source = *source_out,
        .server_flags = &provider->q1_server_flags,
        .current_map = current_map,
        .registered = source_options.registered,
        .context = provider,
        .static_model = q1_static_model,
        .ambient = q1_ambient,
        .lightstyle = q1_lightstyle,
        .fog_player = q1_fog_player,
        .set_skill = q1_integer_intent,
        .player_exited = q1_player_exited,
        .secret_found = q1_secret,
        .alpha_read = q1_alpha_read,
        .alpha_write = q1_alpha_write,
        .path_touch = q1_path_touch,
        .path_read = q1_path_read,
        .path_change = q1_path_change,
        .ctf_state = q1_ctf_state,
        .ctf_pregame_end = q1_ctf_pregame_end,
        .control_player = q1_control_player,
        .finale = q1_finale,
        .finale_finished = q1_finale_finished,
        .finish_campaign = q1_finish_campaign,
        .server_command = q1_server_command,
        .horde_control = q1_horde_control,
        .horde_keys = q1_horde_keys,
        .retire_actor = q1_retire_actor,
        .schedule_remove = q1_schedule_remove,
    };
    return true;

failed:
    qa_q1_level_destroy(*level_out);
    *level_out = NULL;
    qa_q1_campaign_source_destroy(*source_out);
    *source_out = NULL;
    return false;
}

static bool q1_begin_map(application_provider *provider,
                         const qa_product *product,
                         qa_string_id current_map, bool restoring,
                         qa_error *error)
{
    if (!restoring &&
        !qa_q1_source_map_rules_refresh(provider->state.q1, error)) return false;
    qa_q1_campaign_source *source;
    qa_q1_level *level;
    qa_q1_map_options options;
    if (!q1_map_options(provider, product, current_map, &source,
                        &level, &options, error))
        return false;
    if (!application_bots_npc_idle(provider)) {
        qa_q1_level_destroy(level);
        qa_q1_campaign_source_destroy(source);
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 map reset overlaps an entered monster path");
    }
    application_bots_npc_destroy(provider);
    bool ok = provider->map_bound
                  ? qa_q1_game_begin_map(provider->state.q1, &options, error)
                  : qa_q1_game_maps_bind(provider->state.q1, &options, error);
    if (!ok) {
        qa_q1_level_destroy(level);
        qa_q1_campaign_source_destroy(source);
        return false;
    }
    qa_q1_level *old_level = provider->q1_level;
    qa_q1_campaign_source *old_source = provider->q1_campaign;
    provider->q1_level = level;
    provider->q1_campaign = source;
    provider->map_bound = true;
    qa_q1_level_destroy(old_level);
    qa_q1_campaign_source_destroy(old_source);
    return true;
}

static application_provider *active_provider_named(qa_application *application,
                                                    const char *name)
{
    if (name == NULL)
        return NULL;
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (provider != NULL && provider->attached && provider->launch != NULL &&
            strcmp(provider->launch->selection.instance, name) == 0)
            return provider;
    }
    return NULL;
}

static application_provider *monster_owner(qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider && provider->owner == owner && provider->constructed && !provider->close_pending)
            return provider;
    }
    return NULL;
}
static bool monster_activate_behavior(application_provider *map, qa_actor_id actor, qa_error *error)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(map->application->session), actor);
    application_provider *behavior = record ? monster_owner(map->application, record->owner) : NULL;
    if (!behavior) return application_fail(error, QA_ERROR_NOT_FOUND, "Activated monster lost its creature Source");
    return behavior->kind == APPLICATION_PROVIDER_Q1 ? qa_q1_monster_activate(behavior->state.q1, actor, error) :
        behavior->kind == APPLICATION_PROVIDER_Q2 ? qa_q2_monster_activate(behavior->state.q2, actor, error) : false;
}
static bool monster_row(application_provider *map, qa_actor_id actor,
    qa_authored_monster **out, qa_error *error)
{
    *out = qa_targets_monster(map->application->targets, actor);
    return (*out && (*out)->owner == map->owner) ||
        application_fail(error, QA_ERROR_NOT_FOUND, "Selected monster lost its authored map row");
}
static bool monster_spawned(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    if (row->counted_spawn) return true;
    row->counted_spawn = true;
    if (map->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_campaign_monster_count(map->state.q2, QA_Q2_MONSTER_COUNT_TOTAL, error);
    const char *classname = qa_strings_cstr(qa_session_strings(map->application->session), row->fields.classname);
    return qa_q1_game_monster_count(map->state.q1, actor, (qa_actor_id){0}, false,
        map->product->edition == QA_EDITION_CLASSIC && classname && !strcmp(classname, "monster_fish"), error);
}
static bool monster_started(void *, qa_actor_id, qa_error *);
static bool monster_active(void *opaque, qa_actor_id actor, qa_monster_activation *out, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    *out = (qa_monster_activation){0};
    if (!monster_row(map, actor, &row, error)) return false;
    qa_clock_state clock;
    if (!qa_session_clock(map->application->session, map->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Authored monster has no map clock");
    if (row->activation == QA_MONSTER_DORMANT ||
        (row->activation == QA_MONSTER_SCHEDULED && clock.frame.time_ns < row->activation_ns)) return true;
    if (row->placement == QA_MONSTER_WAITING) {
        for (size_t i = 0; i < row->barrier_count; ++i) {
            if (!qa_actors_get(qa_session_actors(map->application->session), row->barriers[i].actor)) continue;
            qa_q2_map_mover_view view; qa_body_state body; size_t count; bool found;
            if (!qa_q2_entity_mover_read(map->state.q2, row->barriers[i].actor, &view, NULL, 0, &count, &found, error) ||
                !qa_world_body_read(map->application->world, row->barriers[i].actor, &body, error)) return false;
            if (found && (view.navigation.locked || view.navigation.has_destination ||
                (body.origin.x == row->barriers[i].origin.x && body.origin.y == row->barriers[i].origin.y &&
                    body.origin.z == row->barriers[i].origin.z))) return true;
        }
        if (!monster_started(map, actor, error)) return false;
        row = qa_targets_monster(map->application->targets, actor);
        if (!row || row->placement == QA_MONSTER_WAITING) return true;
        out->activator = row->activator;
        row->activator = (qa_actor_id){0};
    }
    if (row->placement == QA_MONSTER_TELEPORT) {
        qa_body_state body;
        if (!qa_world_body_read(map->application->world, actor, &body, error)) return false;
        if (body.origin.x != row->placement_origin.x || body.origin.y != row->placement_origin.y ||
            body.origin.z != row->placement_origin.z) {
            row->authored_origin = body.origin;
            if (!monster_started(map, actor, error)) return false;
            row = qa_targets_monster(map->application->targets, actor);
            if (!row || row->placement == QA_MONSTER_WAITING) return true;
        }
    }
    if (row->activation == QA_MONSTER_SCHEDULED) {
        qa_body_state body;
        bool clear;
        if (map->kind != APPLICATION_PROVIDER_Q2 || !qa_world_body_read(map->application->world, actor, &body, error)) return false;
        body.origin.z += 1;
        if (!qa_world_body_write(map->application->world, actor, &body, error)) return false;
        bool rerelease = map->product->edition == QA_EDITION_RERELEASE;
        if (rerelease && !monster_activate_behavior(map, actor, error)) return false;
        if (!qa_q2_entities_killbox(map->state.q2, actor, actor, &clear, error)) return false;
        if (!qa_actors_get(qa_session_actors(map->application->session), actor)) return true;
        if (!rerelease && !monster_activate_behavior(map, actor, error)) return false;
        row = qa_targets_monster(map->application->targets, actor);
        if (!row) return true;
        row->activation = QA_MONSTER_ACTIVE;
        if (!(row->spawnflags & 1u)) out->activator = row->activator;
        row->activator = (qa_actor_id){0};
    }
    out->active = row->activation == QA_MONSTER_ACTIVE;
    return true;
}
static bool monster_use(void *opaque, qa_actor_id actor, qa_actor_id activator,
    bool *handled, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    *handled = row->activation != QA_MONSTER_ACTIVE || row->placement == QA_MONSTER_WAITING;
    if (!*handled) return true;
    row->activator = activator;
    if (row->activation != QA_MONSTER_DORMANT) return true;
    qa_clock_state clock;
    if (!qa_session_clock(map->application->session, map->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Triggered monster has no original map clock");
    row->activation = QA_MONSTER_SCHEDULED;
    uint64_t interval = map->product->edition == QA_EDITION_RERELEASE ?
        map->component.clock.interval_ns : UINT64_C(100000000);
    row->activation_ns = clock.frame.time_ns + interval;
    return true;
}
static bool monster_killed(void *opaque, qa_actor_id actor, qa_actor_id attacker, qa_error *error)
{
    application_provider *map = opaque;
    qa_application *app = map->application;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    if (row->counted_death) return true;
    row->counted_death = true;
    if (map->kind == APPLICATION_PROVIDER_Q2) {
        if (!qa_q2_campaign_monster_count(map->state.q2, QA_Q2_MONSTER_COUNT_KILLED, error)) return false;
        if (row->drop_item) {
            const char *drop = qa_strings_cstr(qa_session_strings(app->session), row->drop_item);
            qa_actor_id item;
            bool dropped;
            if (!qa_q2_item_drop_monster(map->state.q2, actor, drop, &item, &dropped, error)) return false;
            row = qa_targets_monster(app->targets, actor);
            if (!row) return true;
            if (map->product->edition == QA_EDITION_RERELEASE && dropped && row->item_target) {
                if (!qa_q2_entity_set_target(map->state.q2, item, row->item_target, error)) return false;
                row->item_target = 0;
            }
            row->drop_item = 0;
        }
        if (row->death_target) row->fields.target = row->death_target;
    } else if (!qa_q1_game_monster_count(map->state.q1, actor, attacker, true, false, error)) return false;
    row = qa_targets_monster(app->targets, actor);
    if (!row) return true;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, map->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Authored monster death has no map clock");
    if (!qa_targets_use_request(app->targets, &(qa_target_use){.source = actor, .activator = attacker,
        .dialect = row->source, .fields = row->fields, .time_ns = clock.frame.time_ns}, error)) return false;
    row = qa_targets_monster(app->targets, actor);
    if (!row || map->kind != APPLICATION_PROVIDER_Q2 || map->product->edition != QA_EDITION_RERELEASE || !row->health_target) return true;
    row->fields.target = row->health_target;
    return qa_targets_use_request(app->targets, &(qa_target_use){.source = actor, .activator = attacker,
        .dialect = row->source, .fields = row->fields, .time_ns = clock.frame.time_ns}, error);
}
static bool monster_class(qa_application *app, qa_actor_id actor, const char *classname)
{
    qa_authored_target target;
    if (!qa_targets_read(app->targets, actor, &target)) return false;
    const char *name = qa_strings_cstr(qa_session_strings(app->session), target.classname);
    return name && !strcmp(name, classname);
}
static bool monster_pick(application_provider *map, qa_string_id name, qa_actor_id *out)
{
    *out = (qa_actor_id){0};
    if (map->kind == APPLICATION_PROVIDER_Q1) return qa_targets_first(map->application->targets, name, out);
    return qa_q2_monster_pick_target(map->state.q2, name, out);
}
static bool monster_route_goal(void *opaque, qa_actor_id actor, qa_actor_id *out, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    if (!row->route_resolved) {
        if (map->kind == APPLICATION_PROVIDER_Q2) {
            qa_target_cursor cursor = {0}; qa_actor_id target;
            while (qa_targets_next(map->application->targets, row->route, &cursor, &target))
                if (monster_class(map->application, target, "point_combat")) {
                    row->combat_target = row->route;
                    row->fields.target = row->route = 0;
                    break;
                }
        }
        qa_actor_id target;
        if (monster_pick(map, row->route, &target) && monster_class(map->application, target, "path_corner"))
            row->route_goal = target;
        row->route_resolved = true;
        if (map->kind == APPLICATION_PROVIDER_Q2 && row->route_goal.registry) row->fields.target = 0;
    }
    *out = row->route_goal;
    return true;
}
typedef struct monster_placement_query {
    application_provider *map;
    qa_actor_id actor;
    qa_body_state body;
    qa_physics_properties movement;
    qa_bounds authored_bounds;
    uint32_t authored_flags;
    qa_vec3 authored_origin;
    qa_trace_policy policy;
    qa_actor_id *doors;
    bool *blocked;
    size_t door_count;
    const qa_actor_id *excluded;
    size_t excluded_count;
    qa_collision_bits medium;
} monster_placement_query;
static bool monster_medium(monster_placement_query *query, qa_vec3 origin, qa_collision_bits *out, qa_error *error)
{
    qa_point_contents contents;
    if (!qa_world_point_contents(query->map->application->world,
        &(qa_point_query){.point = origin, .policy = query->policy, .pass_actor = query->actor}, &contents, error)) return false;
    qa_collision_bits liquids = qa_collision_bits_union(qa_collision_bit(QA_CONTENT_WATER),
        qa_collision_bits_union(qa_collision_bit(QA_CONTENT_SLIME), qa_collision_bit(QA_CONTENT_LAVA)));
    *out = qa_collision_bits_intersection(contents.contents, liquids);
    return true;
}
static bool monster_trace(monster_placement_query *query, qa_vec3 start, qa_vec3 end,
    qa_bounds bounds, bool point, bool route, qa_trace_result *out, qa_error *error)
{
    qa_trace_policy policy = query->policy;
    if (route && policy.behavior->contents_format == QA_GAME_Q1) policy.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    if (!qa_world_trace_excluding(query->map->application->world,
        &(qa_trace_query){.start = start, .end = end,
            .shape = {.kind = point ? QA_SHAPE_POINT : QA_SHAPE_BOX, .bounds = bounds},
            .policy = policy, .pass_actor = query->actor},
        query->excluded, query->excluded_count, out, error)) return false;
    if (out->fraction < 1 && out->hit == QA_TRACE_HIT_ACTOR)
        for (size_t i = 0; i < query->door_count; ++i)
            if (qa_actor_id_equal(query->doors[i], out->actor)) query->blocked[i] = true;
    return true;
}
static bool monster_reachable(monster_placement_query *query, qa_vec3 start,
    qa_vec3 destination, bool *out, qa_error *error)
{
    qa_trace_result route, center, exit;
    *out = false;
    if (!monster_trace(query, start, destination, query->authored_bounds, false, true, &route, error)) return false;
    if (route.all_solid || route.fraction != 1) return true;
    if (!route.start_solid) { *out = true; return true; }
    if (!monster_trace(query, start, destination, query->authored_bounds, true, true, &center, error) ||
        !monster_trace(query, destination, destination, query->authored_bounds, false, true, &exit, error)) return false;
    *out = !center.start_solid && !center.all_solid && center.fraction == 1 && !exit.start_solid && !exit.all_solid;
    return true;
}
typedef struct monster_offset { int x, y, distance; } monster_offset;
static qa_actor_reference monster_ground(const monster_placement_query *query,
    const qa_trace_result *floor, bool walking)
{
    qa_actor_id actor = walking ? floor->hit == QA_TRACE_HIT_ACTOR ? floor->actor :
        query->map->application->physics->world_actor : (qa_actor_id){0};
    const qa_actor_registry *actors = qa_session_actors(query->map->application->session);
    const qa_actor_record *self = qa_actors_get(actors, query->actor);
    const qa_actor_record *ground = qa_actors_get(actors, actor);
    return self && ground && self->owner == ground->owner && ground->has_source ?
        qa_actor_reference_source(ground->owner, ground->source_slot) : qa_actor_reference_lifetime(actor);
}

static int monster_offset_compare(const void *left, const void *right)
{
    const monster_offset *a = left, *b = right;
    return a->distance != b->distance ? a->distance < b->distance ? -1 : 1 :
        a->y != b->y ? a->y < b->y ? -1 : 1 : a->x < b->x ? -1 : a->x > b->x;
}
static bool monster_candidate(monster_placement_query *query, qa_vec3 start,
    qa_vec3 end, qa_vec3 source_origin, bool walking, bool *found, qa_body_state *out, qa_error *error)
{
    qa_trace_result floor, fit;
    *found = false;
    if (!monster_trace(query, start, end, query->body.bounds, false, false, &floor, error)) return false;
    if (floor.start_solid || floor.all_solid ||
        (walking && (floor.fraction == 1 || !floor.contact || floor.contact_plane.normal.z < 0.7f))) return true;
    qa_collision_bits medium;
    if (!monster_medium(query, floor.end, &medium, error)) return false;
    if (!qa_collision_bits_equal(medium, query->medium)) return true;
    if (!monster_trace(query, floor.end, floor.end, query->body.bounds, false, false, &fit, error)) return false;
    if (fit.start_solid || fit.all_solid) return true;
    qa_vec3 destination = floor.end;
    destination.z += query->body.bounds.mins.z - query->authored_bounds.mins.z;
    bool reachable;
    if (!monster_reachable(query, source_origin, destination, &reachable, error)) return false;
    if (!reachable) return true;
    *out = query->body;
    out->origin = floor.end;
    out->ground = monster_ground(query, &floor, walking);
    *found = true;
    return true;
}
typedef struct monster_placement_node { qa_vec3 origin; int x, y; } monster_placement_node;
static bool monster_corner_placement(monster_placement_query *query, qa_vec3 source_origin,
    float radius, bool walking, qa_body_state *out, bool *found, qa_error *error)
{
    enum { limit = 4096, capacity = 4 * limit + 1 };
    static const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    float reach = fmaxf(512, radius);
    int extent = (int)ceilf(reach / 8);
    size_t width = (size_t)(extent * 2 + 1);
    bool *visited = calloc(width * width, sizeof(*visited));
    monster_placement_node *pending = malloc(capacity * sizeof(*pending));
    if (!visited || !pending) {
        free(visited); free(pending);
        return application_fail(error, QA_ERROR_MEMORY, "Finding reachable selected monster placement");
    }
    size_t count = 1;
    pending[0] = (monster_placement_node){.origin = source_origin};
    visited[(size_t)extent * width + (size_t)extent] = true;
    bool ok = true;
    for (size_t index = 0; ok && !*found && index < count && index < limit; ++index) {
        monster_placement_node current = pending[index];
        for (size_t direction = 0; ok && !*found && direction < 4; ++direction) {
            int x = current.x + directions[direction][0], y = current.y + directions[direction][1];
            if (x < -extent || x > extent || y < -extent || y > extent ||
                (float)(x * x + y * y) * 64 > reach * reach) continue;
            size_t cell = (size_t)(y + extent) * width + (size_t)(x + extent);
            if (visited[cell]) continue;
            for (size_t lift = 0; ok && !*found && lift < 2; ++lift) {
                qa_vec3 start = current.origin;
                start.z += lift ? 18 : 0;
                qa_trace_result raised, across, floor, fit;
                if (!monster_trace(query, current.origin, start, query->authored_bounds, false, true, &raised, error)) { ok = false; break; }
                if (raised.start_solid || raised.all_solid || raised.fraction != 1) continue;
                qa_vec3 end = qa_v3(source_origin.x + (float)x * 8, source_origin.y + (float)y * 8, start.z);
                if (!monster_trace(query, start, end, query->authored_bounds, false, true, &across, error)) { ok = false; break; }
                if (across.start_solid || across.all_solid || across.fraction != 1) continue;
                end.z = current.origin.z - 18;
                if (!monster_trace(query, across.end, end, query->authored_bounds, false, true, &floor, error)) { ok = false; break; }
                if (floor.start_solid || floor.all_solid || floor.fraction == 1 || !floor.contact || floor.contact_plane.normal.z < 0.7f) continue;
                visited[cell] = true;
                pending[count++] = (monster_placement_node){.origin = floor.end, .x = x, .y = y};
                qa_vec3 origin = floor.end;
                origin.z += query->authored_bounds.mins.z - query->body.bounds.mins.z;
                qa_collision_bits medium;
                if (!monster_medium(query, origin, &medium, error)) { ok = false; break; }
                if (qa_collision_bits_equal(medium, query->medium)) {
                    if (!monster_trace(query, origin, origin, query->body.bounds, false, false, &fit, error)) { ok = false; break; }
                    if (!fit.start_solid && !fit.all_solid) {
                        *out = query->body;
                        out->origin = origin;
                        out->ground = monster_ground(query, &floor, walking);
                        *found = true;
                    }
                }
                break;
            }
        }
    }
    free(visited); free(pending);
    return ok;
}
static bool monster_nearby(monster_placement_query *query, qa_body_state *out, bool *found, qa_error *error)
{
    *found = false;
    qa_vec3 start = query->authored_origin;
    start.z += 1;
    qa_trace_result floor;
    if (!monster_trace(query, start, qa_vec_add(start, qa_v3(0, 0, -256)),
        query->authored_bounds, false, true, &floor, error)) return false;
    bool supported = !floor.start_solid && !floor.all_solid && floor.fraction < 1;
    bool authored_walk = !(query->authored_flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING));
    bool walking = !(query->movement.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING));
    qa_vec3 source_origin = supported && authored_walk ? floor.end : query->authored_origin;
    qa_vec3 anchor = floor.end;
    anchor.z = source_origin.z + query->authored_bounds.mins.z - query->body.bounds.mins.z;
    float radius = 2 * fmaxf(fmaxf(query->body.bounds.maxs.x - query->body.bounds.mins.x,
        query->body.bounds.maxs.y - query->body.bounds.mins.y),
        fmaxf(query->authored_bounds.maxs.x - query->authored_bounds.mins.x,
        query->authored_bounds.maxs.y - query->authored_bounds.mins.y));
    int extent = (int)ceilf(radius / 4);
    size_t width = (size_t)(extent * 2 + 1);
    monster_offset *offsets = malloc(width * width * sizeof(*offsets));
    if (!offsets) return application_fail(error, QA_ERROR_MEMORY, "Finding selected monster placement");
    size_t count = 0;
    for (int x = -extent; x <= extent; ++x) for (int y = -extent; y <= extent; ++y)
        if ((float)((x * x + y * y) * 16) <= radius * radius)
            offsets[count++] = (monster_offset){.x = x * 4, .y = y * 4, .distance = x * x + y * y};
    qsort(offsets, count, sizeof(*offsets), monster_offset_compare);
    bool ok = true;
    for (size_t i = 0; ok && !*found && i < count; ++i) {
        if (walking) for (size_t lift = 0; ok && !*found && lift < 2; ++lift) {
            start = qa_vec_add(anchor, qa_v3((float)offsets[i].x, (float)offsets[i].y, lift ? 18 : 1));
            qa_vec3 end = start;
            end.z = anchor.z - (supported && authored_walk ? 18 : 256);
            ok = monster_candidate(query, start, end, source_origin, true, found, out, error);
        }
        else {
            float height = query->body.bounds.maxs.z - query->body.bounds.mins.z;
            for (int z = 0; ok && !*found && (float)z <= height; z += 4)
                for (int direction = -1; ok && !*found && direction <= 1; direction += 2) {
                    if (!z && direction == 1) continue;
                    start = qa_vec_add(query->body.origin, qa_v3((float)offsets[i].x,
                        (float)offsets[i].y, (float)(z * direction)));
                    ok = monster_candidate(query, start, start, source_origin, false, found, out, error);
                }
        }
    }
    free(offsets);
    if (ok && !*found && supported && authored_walk)
        ok = monster_corner_placement(query, source_origin, radius, walking, out, found, error);
    return ok;
}
static bool monster_teleport_staging(application_provider *map, const qa_body_state *body)
{
    qa_target_cursor cursor = {0}; qa_actor_id trigger;
    while (qa_targets_next_authored(map->application->targets, "trigger_teleport", &cursor, &trigger)) {
        qa_authored_target fields; qa_linked_body linked; qa_actor_collision collision; double flags = 0;
        qa_actor_id destination;
        if (!qa_targets_read(map->application->targets, trigger, &fields) || !fields.targetname ||
            !qa_world_linked(map->application->world, trigger, &linked) ||
            !qa_world_get_collision(map->application->world, trigger, &collision, NULL) ||
            collision.role != QA_COLLISION_TRIGGER ||
            !qa_targets_first(map->application->targets, fields.target, &destination)) continue;
        (void)qa_targets_number(map->application->targets, trigger, "spawnflags", &flags);
        if (!((uint32_t)flags & 1u) && qa_bounds_overlap(linked.absolute_bounds,
            qa_bounds_translate(body->bounds, body->origin))) return true;
    }
    return false;
}
static bool monster_started(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *map = opaque;
    qa_application *app = map->application;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    monster_placement_query query = {.map = map, .actor = actor, .authored_origin = row->authored_origin};
    if (!qa_world_body_read(app->world, actor, &query.body, error) ||
        !app->physics->services.read(app->physics->services.context, actor, &query.movement))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected monster has no physical Source body");
    query.policy = qa_collision_default_policy(query.movement.family);
    if (query.policy.behavior->contents_format == QA_GAME_Q2)
        query.policy.contents_mask = qa_collision_contents_mask(1, QA_GAME_Q2);
    qa_trace_result trace;
    if (!monster_trace(&query, query.body.origin, query.body.origin, query.body.bounds,
        false, false, &trace, error)) return false;
    if (!trace.start_solid && !trace.all_solid) {
        row->placement = QA_MONSTER_PLACED;
        free(row->barriers); row->barriers = NULL; row->barrier_count = 0;
        return true;
    }
    if (map->kind == APPLICATION_PROVIDER_Q1 && monster_teleport_staging(map, &query.body)) {
        row->placement = QA_MONSTER_TELEPORT; row->placement_origin = query.body.origin;
        return true;
    }
    const char *authored = qa_strings_cstr(qa_session_strings(app->session), row->fields.classname);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *behavior = record ? monster_owner(app, record->owner) : NULL;
    const char *selected = record ? qa_strings_cstr(qa_session_strings(app->session), record->definition) : NULL;
    if (behavior && behavior->product == map->product && selected && authored && !strcmp(selected, authored)) {
        row->placement = QA_MONSTER_PLACED;
        return true;
    }
    bool shape = map->kind == APPLICATION_PROVIDER_Q1 ?
        qa_q1_monster_shape(authored, &query.authored_bounds, &query.authored_flags) :
        qa_q2_monster_shape(map->state.q2, authored, &query.authored_bounds, &query.authored_flags);
    if (!shape || !monster_medium(&query, query.body.origin, &query.medium, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Authored monster has no native placement shape");
    qa_arena scratch; qa_arena_init(&scratch, 1024);
    size_t capacity = qa_actors_count(qa_session_actors(app->session));
    query.doors = qa_arena_alloc(&scratch, capacity * sizeof(*query.doors), _Alignof(qa_actor_id), error);
    query.blocked = qa_arena_alloc(&scratch, capacity * sizeof(*query.blocked), _Alignof(bool), error);
    qa_actor_id *excluded = qa_arena_alloc(&scratch, capacity * sizeof(*excluded), _Alignof(qa_actor_id), error);
    bool ok = query.doors && query.blocked && excluded;
    if (ok && map->kind == APPLICATION_PROVIDER_Q2 && row->fields.targetname) {
        qa_target_cursor cursor = {0}; qa_actor_id door;
        while (qa_targets_next(app->targets, row->fields.targetname, &cursor, &door)) {
            if (!monster_class(app, door, "func_door")) continue;
            qa_q2_map_mover_view view; size_t count; bool found;
            ok = qa_q2_entity_mover_read(map->state.q2, door, &view, NULL, 0, &count, &found, error);
            if (!ok) break;
            if (found && (view.navigation.locked || view.navigation.has_destination)) query.doors[query.door_count++] = door;
        }
    }
    if (ok) memset(query.blocked, 0, query.door_count * sizeof(*query.blocked));
    qa_body_state placement; bool found = false;
    if (ok) ok = monster_nearby(&query, &placement, &found, error);
    if (ok && found) {
        row->placement = QA_MONSTER_PLACED;
        free(row->barriers); row->barriers = NULL; row->barrier_count = 0;
        ok = qa_world_body_write(app->world, actor, &placement, error) && qa_world_link(app->world, actor, NULL, error);
    } else if (ok) {
        query.excluded = excluded;
        while (ok && !found) {
            size_t previous = query.excluded_count;
            query.excluded_count = 0;
            for (size_t i = 0; i < query.door_count; ++i) if (query.blocked[i]) excluded[query.excluded_count++] = query.doors[i];
            if (previous == query.excluded_count) break;
            ok = monster_nearby(&query, &placement, &found, error);
        }
        if (ok && found) {
            qa_monster_barrier *barriers = calloc(query.excluded_count, sizeof(*barriers));
            ok = barriers != NULL;
            if (!ok) application_fail(error, QA_ERROR_MEMORY, "Retaining authored door encounter");
            for (size_t i = 0; ok && i < query.excluded_count; ++i) {
                qa_body_state body;
                ok = qa_world_body_read(app->world, excluded[i], &body, error);
                barriers[i] = (qa_monster_barrier){.actor = excluded[i], .origin = body.origin};
            }
            if (ok) { free(row->barriers); row->barriers = barriers; row->barrier_count = query.excluded_count; row->placement = QA_MONSTER_WAITING; }
            else free(barriers);
        } else if (ok) ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Selected monster cannot fit its reachable authored encounter");
    }
    qa_arena_destroy(&scratch);
    return ok;
}

static bool monster_combat_route(void *opaque, qa_actor_id actor, qa_monster_combat_route *out, qa_error *error)
{
    qa_authored_monster *row;
    if (!monster_row(opaque, actor, &row, error)) return false;
    *out = (qa_monster_combat_route){.goal = row->combat_goal, .stand_ground = row->stand_ground};
    return true;
}
static bool monster_found_target(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    qa_actor_id target;
    if (map->kind == APPLICATION_PROVIDER_Q2 && row->combat_target && monster_pick(map, row->combat_target, &target)) {
        row->combat_target = 0;
        row->combat_goal = target;
        if (map->product->edition == QA_EDITION_CLASSIC)
            return qa_targets_set_targetname(map->application->targets, target, 0, error);
    }
    return true;
}
bool application_monster_mission(void *opaque, qa_actor_owner owner, qa_monster_mission *out, qa_error *error)
{
    application_provider *map = monster_owner(opaque, owner);
    if (!map || (map->kind != APPLICATION_PROVIDER_Q1 && map->kind != APPLICATION_PROVIDER_Q2))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Authored monster has no native map producer");
    *out = (qa_monster_mission){.owner = owner, .context = map, .spawned = monster_spawned,
        .started = monster_started, .active = monster_active, .killed = monster_killed,
        .route = monster_route_goal, .use = monster_use, .combat_route = monster_combat_route,
        .found_target = monster_found_target};
    return true;
}
bool application_monster_admit(void *opaque, qa_actor_id actor, const qa_authored_monster *row, qa_error *error)
{
    application_provider *provider = opaque;
    qa_targets_monsters_configure(provider->application->targets, provider->application, application_monster_mission);
    return qa_targets_monster_admit(provider->application->targets, actor, row, error);
}

static application_provider *monster_behavior(application_provider *map, qa_actor_id actor)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(map->application->session), actor);
    return record ? monster_owner(map->application, record->owner) : NULL;
}
static uint64_t monster_path_time(application_provider *from, application_provider *to, uint64_t deadline)
{
    if (!deadline || from == to) return deadline;
    qa_clock_state source, destination;
    if (!qa_session_clock(from->application->session, from->owner, &source) ||
        !qa_session_clock(to->application->session, to->owner, &destination)) return deadline;
    return destination.frame.time_ns + (deadline > source.frame.time_ns ? deadline - source.frame.time_ns : 0);
}
static bool monster_path_read(application_provider *map, qa_actor_id actor, qa_q1_path_state *out)
{
    qa_authored_monster *row = qa_targets_monster(map->application->targets, actor);
    application_provider *behavior = monster_behavior(map, actor);
    if (!row || !behavior) return false;
    if (behavior->kind == APPLICATION_PROVIDER_Q1) {
        if (!qa_q1_game_path_read(behavior->state.q1, actor, out)) return false;
    } else if (behavior->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_monster_route_state source;
        if (!qa_q2_monster_route_read(behavior->state.q2, actor, &source)) return false;
        *out = (qa_q1_path_state){.enemy = source.enemy, .old_enemy = source.old_enemy, .monster = true,
            .pause_until = (double)monster_path_time(behavior, map, source.pause_until_ns) / 1e9};
    } else return false;
    out->path = row->route;
    out->move_target = row->combat_goal.registry ? row->combat_goal : row->route_goal;
    out->previous_corner = row->previous_corner;
    out->follow_until = (double)row->follow_until_ns / 1e9;
    return true;
}
static bool monster_path_change(application_provider *map, qa_actor_id actor,
    const qa_q1_path_change *change, qa_error *error)
{
    qa_authored_monster *row = qa_targets_monster(map->application->targets, actor);
    application_provider *behavior = monster_behavior(map, actor);
    if (!row || !behavior) return application_fail(error, QA_ERROR_NOT_FOUND, "Selected path has no creature Source");
    if (change->kind == QA_Q1_PATH_VISIT) row->previous_corner = change->reference;
    if (change->kind == QA_Q1_PATH_FOLLOW_UNTIL) row->follow_until_ns = (uint64_t)(change->follow_until * 1e9);
    if (change->kind == QA_Q1_PATH_DESTINATION)
        qa_targets_monster_route(map->application->targets, actor, change->reference.registry ? change->target : 0, change->reference);
    if (behavior->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_path_change converted = *change;
        if (change->kind == QA_Q1_PATH_PAUSE_END || change->kind == QA_Q1_PATH_STAND)
            converted.pause_until = (double)monster_path_time(map, behavior,
                (uint64_t)(change->pause_until * 1e9)) / 1e9;
        return qa_q1_game_path_change(behavior->state.q1, actor, &converted, error);
    }
    if (behavior->kind != APPLICATION_PROVIDER_Q2)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected path creature has no native continuation");
    qa_q2_monster_route_state state;
    if (!qa_q2_monster_route_read(behavior->state.q2, actor, &state)) return false;
    switch (change->kind) {
    case QA_Q1_PATH_OWNER:
    case QA_Q1_PATH_VISIT: return true;
    case QA_Q1_PATH_DESTINATION:
        return qa_q2_monster_route_advance(behavior->state.q2, actor, change->reference, 0, false, error);
    case QA_Q1_PATH_PAUSE_END:
    case QA_Q1_PATH_STAND:
        return qa_q2_monster_route_advance(behavior->state.q2, actor, state.goal,
            monster_path_time(map, behavior, (uint64_t)(change->pause_until * 1e9)), false, error);
    case QA_Q1_PATH_CANCEL_PAUSE:
        return qa_q2_monster_route_advance(behavior->state.q2, actor, state.goal, 0, false, error);
    case QA_Q1_PATH_FOUND:
        return qa_q2_monster_action(behavior->state.q2, actor, QA_Q2_MONSTER_FOUND_TARGET, change->reference, 0, error);
    case QA_Q1_PATH_FOLLOW_BEGIN:
        return qa_q2_monster_follow_begin(behavior->state.q2, actor, change->reference, error);
    case QA_Q1_PATH_FOLLOW_UNTIL:
        return true;
    }
    return false;
}
static bool monster_path_touch(application_provider *map, qa_actor_id corner, qa_actor_id actor,
    bool *handled, qa_error *error)
{
    *handled = qa_targets_monster(map->application->targets, actor) != NULL;
    if (!*handled) return true;
    qa_q1_path_state follower;
    qa_authored_target target;
    if (!monster_path_read(map, actor, &follower) || !qa_targets_read(map->application->targets, corner, &target) ||
        follower.enemy.registry || follower.path != target.targetname) return true;
    qa_actor_id next = {0};
    (void)qa_targets_first(map->application->targets, target.target, &next);
    if (!monster_path_change(map, actor, &(qa_q1_path_change){.kind = QA_Q1_PATH_DESTINATION,
        .target = target.target, .reference = next}, error)) return false;
    if (next.registry) return true;
    qa_clock_state clock;
    if (!qa_session_clock(map->application->session, map->owner, &clock)) return false;
    return monster_path_change(map, actor, &(qa_q1_path_change){.kind = QA_Q1_PATH_PAUSE_END,
        .pause_until = (double)clock.frame.time_ns / 1e9 + 999999}, error);
}
static bool q2_monster_path_follower(void *opaque, qa_actor_id actor, qa_q2_path_follower *out)
{
    application_provider *map = opaque;
    qa_q1_path_state source;
    qa_authored_monster *row = qa_targets_monster(map->application->targets, actor);
    if (!row || !monster_path_read(map, actor, &source)) return false;
    qa_physics_properties motion;
    if (!map->application->physics->services.read(map->application->physics->services.context, actor, &motion)) return false;
    *out = (qa_q2_path_follower){.move_target = source.move_target, .enemy = source.enemy,
        .old_enemy = source.old_enemy, .activator = row->activator,
        .walking = !(motion.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))};
    return true;
}
static bool q2_monster_path_advance(void *opaque, qa_actor_id actor,
    const qa_q2_path_advance *change, qa_error *error)
{
    application_provider *map = opaque;
    qa_authored_monster *row;
    if (!monster_row(map, actor, &row, error)) return false;
    application_provider *behavior = monster_behavior(map, actor);
    bool combat = row->combat_goal.registry != 0;
    if (combat) {
        if (change->set_target) row->fields.target = change->target;
        row->combat_goal = change->finish ? (qa_actor_id){0} : change->move_target;
        row->stand_ground = change->hold;
        if (change->finish) row->fields.target = 0;
    } else qa_targets_monster_route(map->application->targets, actor, change->target, change->move_target);
    qa_actor_id goal = change->finish ? (qa_actor_id){0} : change->goal;
    if (behavior && behavior->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_monster_route_advance(behavior->state.q2, actor, goal,
            monster_path_time(map, behavior, change->pause_until_ns), change->hold, error);
    if (!behavior || behavior->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 path lost selected creature Source");
    if (!qa_q1_game_path_change(behavior->state.q1, actor, &(qa_q1_path_change){
        .kind = QA_Q1_PATH_DESTINATION, .target = change->target, .reference = goal,
        .combat_route = combat}, error)) return false;
    if (change->pause_until_ns)
        return qa_q1_game_path_change(behavior->state.q1, actor, &(qa_q1_path_change){.kind = QA_Q1_PATH_STAND,
            .pause_until = (double)monster_path_time(map, behavior, change->pause_until_ns) / 1e9}, error);
    return true;
}

static bool monster_route(application_provider *map_provider,
                             const qa_launch_choices *choices,
                             const char *authored,
                             application_provider **provider_out,
                             const char **classname_out, bool *monster_out,
                             qa_error *error)
{
    *provider_out = map_provider;
    *classname_out = authored;
    *monster_out = strncmp(authored, "monster_", 8) == 0;
    if (!*monster_out) {
        qa_bounds bounds;
        uint32_t flags;
        if (map_provider->kind == APPLICATION_PROVIDER_Q1)
            *monster_out = qa_q1_monster_shape(authored, &bounds, &flags);
        else if (map_provider->kind == APPLICATION_PROVIDER_Q2)
            *monster_out = qa_q2_monster_shape(map_provider->state.q2, authored, &bounds, &flags);
    }
    const qa_launch_monster *selection = NULL, *fallback = NULL;
    for (size_t index = 0; index < choices->monster_count; ++index) {
        const qa_launch_monster *candidate = &choices->monsters[index];
        if (!*candidate->authored_classname) fallback = candidate;
        if (!strcmp(candidate->authored_classname, authored)) { selection = candidate; break; }
    }
    if (!selection && *monster_out) selection = fallback;
    if (selection && !selection->map_defined) {
        application_provider *selected = active_provider_named(
            map_provider->application, selection->instance);
        if (selected == NULL)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "selected monster provider is absent");
        *provider_out = selected;
        *classname_out = selection->classname;
        *monster_out = true;
    }
    if (*classname_out == NULL || (*classname_out)[0] == '\0')
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "selected monster classname is empty");
    return true;
}

static bool native_entity_spawn(application_provider *map_provider,
    application_provider *actor_provider, const char *authored,
    const char *selected_classname, bool monster, const qa_entities *entities,
    size_t index, bool has_source, uint32_t source_slot, qa_arena *arena, qa_actor_id *out,
    qa_error *error);

static bool q1_spawn_entity(application_provider *provider,
    const qa_entities *entities, size_t index, qa_arena *arena,
    const char *classname, bool has_source, uint32_t source_slot,
    const qa_authored_monster *authored_monster, qa_actor_id *out, qa_error *error)
{
    if (authored_monster) {
        qa_q1_spawn spawn = {.classname = classname, .authored_monster = authored_monster,
            .has_source = has_source, .source_slot = source_slot};
        float angle;
        bool has_angles;
        if (!entity_vector(entities, index, "origin", qa_v3(0, 0, 0), &spawn.origin, NULL, error) ||
            !entity_vector(entities, index, "angles", qa_v3(0, 0, 0), &spawn.angles, &has_angles, error) ||
            !entity_float(entities, index, "angle", 0, &angle, error)) return false;
        if (!has_angles) spawn.angles = angle == -1 ? qa_v3(-90, 0, 0) :
            angle == -2 ? qa_v3(90, 0, 0) : qa_v3(0, angle, 0);
        return qa_q1_game_spawn(provider->state.q1, &spawn, out, error);
    }
    qa_q1_map_fields fields = {0};
    qa_q1_boss_fields boss = {0};
    const char *target, *targetname, *killtarget, *message;
    if (!entity_optional_text(entities, index, "model", arena,
                              &fields.model, error) ||
        !entity_optional_text(entities, index, "map", arena,
                              &fields.map, error) ||
        !entity_optional_text(entities, index, "mdl", arena,
                              &fields.mdl, error) ||
        !entity_optional_text(entities, index, "noise", arena,
                              &fields.noise, error) ||
        !entity_optional_text(entities, index, "noise1", arena,
                              &fields.noise1, error) ||
        !entity_optional_text(entities, index, "noise2", arena,
                              &fields.noise2, error) ||
        !entity_optional_text(entities, index, "noise3", arena,
                              &fields.noise3, error) ||
        !entity_optional_text(entities, index, "endtext", arena,
                              &fields.endtext, error) ||
        !entity_optional_text(entities, index, "intermissiontext", arena,
                              &fields.intermissiontext, error) ||
        !entity_optional_text(entities, index, "netname", arena,
                              &fields.netname, error) ||
        !entity_optional_text(entities, index, "kill_string", arena,
                              &fields.kill_string, error) ||
        !entity_optional_text(entities, index, "deathtype", arena,
                              &fields.death_type, error) ||
        !entity_optional_text(entities, index, "team", arena,
                              &fields.team, error) ||
        !entity_optional_text(entities, index, "event", arena,
                              &fields.event, error) ||
        !entity_optional_text(entities, index, "spawnfunction", arena,
                              &fields.spawn_function, error) ||
        !entity_optional_text(entities, index, "spawnclassname", arena,
                              &fields.spawn_classname, error) ||
        !entity_optional_text(entities, index, "group", arena, &fields.group, error) ||
        !entity_optional_text(entities, index, "path", arena, &fields.path, error) ||
        !entity_optional_text(entities, index, "category", arena, &fields.category, error) ||
        !entity_optional_text(entities, index, "fog_info_entity", arena,
                              &fields.fog_info_entity, error) ||
        !entity_optional_text(entities, index, "wave1", arena,
                              &boss.wave1, error) ||
        !entity_optional_text(entities, index, "wave2", arena,
                              &boss.wave2, error) ||
        !entity_optional_text(entities, index, "wave3", arena,
                              &boss.wave3, error) ||
        !entity_optional_text(entities, index, "tele_target", arena,
                              &boss.teleport_target, error) ||
        !entity_optional_text(entities, index, "target", arena, &target,
                              error) ||
        !entity_optional_text(entities, index, "targetname", arena,
                              &targetname, error) ||
        !entity_optional_text(entities, index, "killtarget", arena,
                              &killtarget, error) ||
        !entity_optional_text(entities, index, "message", arena, &message,
                              error))
        return false;
    qa_q1_spawn spawn = {
        .classname = classname,
        .target = target,
        .targetname = targetname,
        .killtarget = killtarget,
        .message = message,
        .source_slot = source_slot,
        .has_source = has_source,
        .map_fields = &fields,
        .boss_fields = &boss,
    };
    float angle = 0;
    bool has_angles = false;
    if (!entity_vector(entities, index, "origin", qa_v3(0, 0, 0),
                       &spawn.origin, NULL, error) ||
        !entity_vector(entities, index, "angles", qa_v3(0, 0, 0),
                       &spawn.angles, &has_angles, error) ||
        !entity_float(entities, index, "angle", 0, &angle, error) ||
        !entity_u32(entities, index, "spawnflags", 0, &spawn.spawnflags,
                    error) ||
        !entity_u32(entities, index, "upgrade", 0, &spawn.upgrade_flag,
                    error) ||
        !entity_float(entities, index, "health", 0, &spawn.health, error) ||
        !entity_float(entities, index, "speed", 0, &spawn.speed, error) ||
        !entity_float(entities, index, "wait", 0, &spawn.wait, error) ||
        !entity_float(entities, index, "delay", 0, &spawn.delay, error) ||
        !entity_float(entities, index, "dmg", 0, &spawn.damage, error) ||
        !entity_float(entities, index, "ltime", 0,
                      &fields.local_time_seconds, error) ||
        !entity_float(entities, index, "count", 0, &spawn.count, error))
        return false;
    if (!has_angles) {
        if (angle == -1)
            spawn.angles = qa_v3(-90, 0, 0);
        else if (angle == -2)
            spawn.angles = qa_v3(90, 0, 0);
        else
            spawn.angles.y = angle;
    }
    if (!entity_vector(entities, index, "mangle", qa_v3(0, 0, 0),
                       &fields.mangle, NULL, error) ||
        !entity_vector(entities, index, "rotate", qa_v3(0, 0, 0), &fields.rotate, NULL, error) ||
        !entity_vector(entities, index, "dest", qa_v3(0, 0, 0),
                       &fields.dest, NULL, error) ||
        !entity_vector(entities, index, "dest2", qa_v3(0, 0, 0),
                       &fields.dest2, &fields.has_dest2, error) ||
        !entity_vector(entities, index, "size", qa_v3(0, 0, 0),
                       &fields.particle_size, NULL, error) ||
        !entity_vector(entities, index, "pos2", qa_v3(0, 0, 0),
                       &fields.pos2, NULL, error) ||
        !entity_vector(entities, index, "avelocity", qa_v3(0, 0, 0),
                       &fields.angular_velocity, NULL, error) ||
        !entity_vector(entities, index, "movedir", qa_v3(0, 0, 0),
                       &fields.movedir, &fields.has_movedir, error) ||
        !entity_vector(entities, index, "view_ofs", qa_v3(0, 0, 0),
                       &fields.view_offset, &fields.has_view_offset, error) ||
        !entity_vector(entities, index, "fog_color", qa_v3(0, 0, 0),
                       &fields.fog_color, NULL, error) ||
        !entity_float(entities, index, "fog_density", 0, &fields.fog_density, error) ||
        !entity_float(entities, index, "weapon", 0, &fields.weapon, error) ||
        !entity_float(entities, index, "frags", 0, &fields.frags, error) ||
        !entity_float(entities, index, "height", 0, &fields.height, error) ||
        !entity_float(entities, index, "lip", 0, &fields.lip, error) ||
        !entity_float(entities, index, "width", 0, &fields.width, error) ||
        !entity_float(entities, index, "length", 0, &fields.length, error) ||
        !entity_float(entities, index, "pausetime", 0, &fields.pause_time,
                      error) ||
        !entity_float(entities, index, "volume", 0, &fields.volume, error) ||
        !entity_float(entities, index, "duration", 0, &fields.duration,
                      error) ||
        !entity_float(entities, index, "distance", 0, &fields.distance,
                      error) ||
        !entity_float(entities, index, "nextthink", 0,
                      &fields.next_think_seconds, error) ||
        !entity_float(entities, index, "spawnmulti", 0, &fields.spawn_multi,
                      error) ||
        !entity_float(entities, index, "spawnsilent", 0,
                      &fields.spawn_silent, error) ||
        !entity_float(entities, index, "gravity", 0, &fields.gravity,
                      error) ||
        !entity_float(entities, index, "currentammo", 0,
                      &fields.current_ammo, error) ||
        !entity_float(entities, index, "pain_finished", 0,
                      &fields.pain_finished, error) ||
        !entity_float(entities, index, "cnt", 0, &fields.counter_value,
                      error) ||
        !entity_float(entities, index, "goal_state", 0, &fields.goal_state, error) ||
        !entity_i32(entities, index, "sounds", 0, &fields.sounds, error) ||
        !entity_i32(entities, index, "style", 0, &fields.style, error) ||
        !entity_i32(entities, index, "state", 0, &fields.initial_state, error) ||
        !entity_i32(entities, index, "frame", 0, &fields.frame, error) ||
        !entity_i32(entities, index, "skin", 0, &fields.skin, error) ||
        !entity_i32(entities, index, "worldtype", 0, &fields.world_type,
                    error) ||
        !entity_i32(entities, index, "colormap", 0, &fields.color_map,
                    error) ||
        !entity_i32(entities, index, "impulse", 0, &fields.impulse, error) ||
        !entity_i32(entities, index, "color", 0, &fields.particle_color,
                    error))
        return false;

    qa_actor_id actor;
    if (!qa_q1_game_spawn(provider->state.q1, &spawn, &actor, error))
        return false;
    *out = actor;
    if (strcmp(classname, "worldspawn") == 0 && actor.registry)
        provider->application->physics->world_actor = actor;
    return true;
}

static bool q1_spawn_map(application_provider *provider,
    const qa_launch_choices *choices, const qa_entities *entities,
    application_player_travel *travel, qa_error *error)
{
    if (!entities->count || sizeof(qa_q1_wire_binding) > SIZE_MAX / entities->count)
        return application_fail(error, QA_ERROR_MEMORY, "Q1 authored binding extent is exhausted");
    qa_q1_wire_binding *bindings = calloc(entities->count, sizeof(*bindings));
    if (!bindings)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain actual Q1 authored spawn results");
    qa_arena arena;
    qa_arena_init(&arena, 4096);
    qa_q1_options source;
    double seconds;
    bool ok = true;
    for (size_t index = 0; ok && index < entities->count; ++index) {
        qa_arena_reset(&arena);
        qa_q1_wire_binding *binding = bindings + index;
        qa_bytes name;
        uint32_t flags;
        if (!qa_q1_wire_authored_allocate(provider->state.q1, index, &binding->source_slot, error) ||
            !qa_entity_value(entities, index, "classname", &name) ||
            !entity_u32(entities, index, "spawnflags", 0, &flags, error) ||
            !qa_q1_source_respawn_options_read(provider->state.q1, &source, &seconds, error)) {
            if (!error || !error->message[0])
                application_fail(error, QA_ERROR_FORMAT, "Q1 map entity has no classname");
            ok = false;
            break;
        }
        bool inhibited = source.quakeworld ? (flags & 2048u) != 0 :
            source.deathmatch ? (flags & 2048u) != 0 :
            (flags & (source.skill == 0 ? 256u : source.skill == 1 ? 512u : 1024u)) != 0;
        if (inhibited) {
            ok = qa_q1_wire_slot_free(provider->state.q1, binding->source_slot, error);
            continue;
        }
        const char *authored = arena_text(&arena, name, error), *classname;
        application_provider *selected;
        bool monster;
        ok = authored && monster_route(provider, choices, authored, &selected, &classname, &monster, error) &&
            native_entity_spawn(provider, selected, authored, classname, monster,
                entities, index, true, binding->source_slot, &arena, &binding->actor, error);
        if (ok && (!binding->actor.registry || selected != provider))
            ok = qa_q1_wire_slot_free(provider->state.q1, binding->source_slot, error);
    }
    qa_arena_destroy(&arena);
    ok = ok && qa_q1_game_maps_finish(provider->state.q1, error) &&
        qa_q1_source_respawn_options_read(provider->state.q1, &source, &seconds, error) &&
        application_players_q1_points(travel, source.max_clients, bindings, entities->count, error) &&
        application_native_q1_wire_resources_prepare(provider, error);
    free(bindings);
    return ok;
}

static bool q2_visual(void *opaque, qa_actor_id actor,
                      const qa_entity_visual *visual, qa_error *error)
{
    application_provider *provider = opaque;
    qa_application *application = provider->application;
    if (visual == NULL || actor.slot >= application->q2_visual_capacity ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 visual needs a live shared actor");
    application_q2_visual_record *record =
        &application->q2_visuals[actor.slot];
    uint64_t revision = record->active &&
                                qa_actor_id_equal(record->actor, actor)
                            ? record->revision
                            : 0;
    if (revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q2 visual revision is exhausted");
    *record = (application_q2_visual_record){
        .actor = actor,
        .visual = *visual,
        .revision = revision + 1,
        .active = true,
    };
    return application_unified_q2_native_visual(provider, actor, visual, error);
}

static bool q2_read_visual(void *opaque, qa_actor_id actor,
                           qa_entity_visual *out, qa_error *error)
{
    application_provider *provider = opaque;
    qa_application *application = provider->application;
    if (out == NULL || actor.slot >= application->q2_visual_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 visual read needs output storage");
    application_q2_visual_record *record =
        &application->q2_visuals[actor.slot];
    if (record->active && qa_actor_id_equal(record->actor, actor)) {
        *out = record->visual;
        return true;
    }
    application_provider *selected = application_provider_for(
        application, actor, QA_ROLE_ENTITIES, "");
    if (selected != NULL && selected->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_entity_visual(selected->state.q2, actor, out, error);
    return application_fail(error, QA_ERROR_NOT_FOUND,
                            "actor has no selected Q2 visual source");
}

static bool q2_event(void *opaque, const qa_q2_map_event *event,
                     qa_error *error)
{
    application_provider *provider = opaque;
    return (event->kind != QA_Q2_MAP_ACHIEVEMENT || event->text == 0 ||
            application_record_achievement(provider, (qa_actor_id){0},
                                           event->text, error)) &&
           application_emit_q2_map(provider,event,error);
}

static bool q2_area_portal(void *opaque, uint32_t portal, bool open,
                           qa_error *error)
{
    application_provider *provider = opaque;
    return application_portal_q2(provider, portal, open, error);
}

static bool q2_transition(void *opaque, qa_actor_id source,
                          qa_actor_id activator, qa_string_id map,
                          const qa_q2_landmark *landmark, bool end_unit,
                          qa_error *error)
{
    qa_builtin_event event = {
        .kind = QA_BUILTIN_TARGET,
        .family = QA_GAME_Q2,
        .actor = activator,
        .other = source,
        .text = map,
        .code = end_unit ? 2 : 1,
    };
    if (landmark != NULL) {
        event.resource = landmark->name;
        event.origin = landmark->relative_origin;
        event.direction = landmark->relative_velocity;
        event.end = landmark->relative_view_angles;
        event.flags = 1;
    }
    application_provider *provider = opaque;
    const char *destination = qa_strings_cstr(qa_session_strings(provider->application->session), map);
    if (destination == NULL || !application_source_queue_travel(provider->application,
        &(qa_application_travel_request){.provider = provider->owner, .cause = activator,
            .expression = destination, .landmark = landmark, .new_unit = end_unit,
            .carry_players = true, .complete_campaign = true}, error))
        return false;
    return emit_map_event(provider, event, error) &&
           application_record_level(provider,
                                    provider->application->current_map,
                                    error);
}

static bool q2_server_flags(void *opaque, bool write, bool cross_unit,
                            uint32_t *flags, qa_error *error)
{
    application_provider *provider = opaque;
    if (flags == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 campaign flags need storage");
    if (write) {
        if (cross_unit)
            provider->q2_server_flags |= *flags;
        else
            provider->q2_server_flags = *flags;
    } else {
        *flags = provider->q2_server_flags;
    }
    return true;
}

static bool q2_actor_gravity(void *opaque, qa_actor_id actor, float gravity,
                             qa_error *error)
{
    application_provider *provider = opaque;
    if (!isfinite(gravity))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 actor gravity is nonfinite");
    qa_physics_services services =
        application_physics_services(provider->application);
    qa_physics_properties properties;
    if (services.read == NULL ||
        !services.read(services.context, actor, &properties))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no selected physics state");
    properties.gravity_scale = gravity;
    return services.write(services.context, actor, &properties, error);
}

static bool q2_world_gravity(void *opaque, float gravity, qa_error *error)
{
    application_provider *provider = opaque;
    if (!isfinite(gravity))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 world gravity is nonfinite");
    provider->application->physics->gravity = gravity;
    return true;
}

static bool q2_player_push(void *opaque, qa_actor_id actor, qa_vec3 velocity,
                           qa_error *error)
{
    application_provider *provider = opaque;
    if (!qa_vec_finite(velocity))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q2 player push is nonfinite");
    qa_body_state body;
    if (!qa_world_body_read(provider->application->world, actor, &body,
                            error))
        return false;
    body.velocity = velocity;
    return qa_world_body_write(provider->application->world, actor, &body,
                               error);
}

static bool q2_target_anger(void *opaque, qa_actor_id actor,
                            qa_actor_id target, qa_error *error)
{
    application_provider *origin = opaque;
    application_provider *provider = application_provider_for(
        origin->application, actor, QA_ROLE_MONSTERS, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_monster_target_anger(provider->state.q2, actor, target,
                                          error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected monster has no Q2 anger adapter");
}

static bool q2_invoke_use(void *opaque, qa_actor_id actor,
                          qa_actor_id other, qa_actor_id activator,
                          qa_error *error)
{
    application_provider *origin = opaque;
    application_provider *provider = application_provider_for(
        origin->application, actor, QA_ROLE_ENTITIES, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_entity_use(provider->state.q2, actor, other, activator,
                                error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected entity has no Q2 use adapter");
}

static bool q2_holds_healthbar(void *opaque, qa_actor_id actor)
{
    application_provider *origin = opaque;
    application_provider *provider = application_provider_for(
        origin->application, actor, QA_ROLE_MONSTERS, "");
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2 &&
           qa_q2_monster_holds_healthbar(provider->state.q2, actor);
}

static bool q2_camera_player(void *opaque, qa_actor_id actor, qa_vec3 origin,
                             qa_vec3 angles, bool entering, qa_error *error)
{
    application_provider *source = opaque;
    application_provider *provider = application_provider_for(
        source->application, actor, QA_ROLE_CHARACTER, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_players_camera(provider->state.q2, origin, angles,
                                    entering, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected character has no Q2 camera adapter");
}

static qa_q2_entity_services q2_entity_services(application_provider *provider,
                                                 const qa_launch_choices *choices)
{
    bool ctf = false;
    for (size_t index = 0; index < choices->mode_count; ++index)
        if (choices->modes[index].rules.kind == QA_MODE_CTF) {
            ctf = true;
            break;
        }
    return (qa_q2_entity_services){
        .context = provider,
        .targets = provider->application->targets,
        .ctf_map_rules = ctf,
        .visual = q2_visual,
        .read_visual = q2_read_visual,
        .event = q2_event,
        .area_portal = q2_area_portal,
        .transition = q2_transition,
        .server_flags = q2_server_flags,
        .actor_gravity = q2_actor_gravity,
        .world_gravity = q2_world_gravity,
        .player_push = q2_player_push,
        .path_follower = q2_monster_path_follower,
        .path_advance = q2_monster_path_advance,
        .target_anger = q2_target_anger,
        .invoke_use = q2_invoke_use,
        .holds_healthbar = q2_holds_healthbar,
        .camera_player = q2_camera_player,
    };
}

static bool q2_property_id(application_provider *provider,
                           const qa_entities *entities, size_t index,
                           const char *key, qa_string_id *out,
                           qa_error *error)
{
    qa_bytes value;
    *out = QA_STRING_NONE;
    return !qa_entity_value(entities, index, key, &value) ||
           qa_strings_intern(qa_session_strings(provider->application->session),
                             value, out, error);
}

static bool monster_authored_fields(application_provider *map, const qa_entities *entities,
    size_t index, uint32_t ordinal, qa_authored_monster *row, qa_error *error)
{
    static const char *const q1_ordinary[] = {
        "monster_army_infected", "monster_knight_infected", "monster_enforcer_infected", "monster_hell_knight_infected",
        "monster_demodog", "monster_ranged_knight", "monster_ogre_rocket", "monster_army", "monster_dog", "monster_knight",
        "monster_enforcer", "monster_demon1", "monster_ogre", "monster_ogre_marksman", "monster_hell_knight", "monster_shambler",
        "monster_wizard", "monster_shalrath", "monster_tarbaby", "monster_fish", "monster_zombie", "monster_scourge",
        "monster_gremlin", "monster_eel", "monster_sword", "monster_wrath", "monster_mummy", "monster_lava_man"};
    static const char *const q2_ordinary[] = {
        "monster_soldier", "monster_soldier_light", "monster_soldier_ss", "monster_infantry", "monster_berserk", "monster_gladiator",
        "monster_gunner", "monster_parasite", "monster_flyer", "monster_floater", "monster_hover", "monster_mutant", "monster_chick",
        "monster_tank", "monster_tank_commander", "monster_flipper", "monster_brain", "monster_gekk", "monster_chick_heat",
        "monster_soldier_ripper", "monster_soldier_hypergun", "monster_soldier_lasergun", "monster_stalker", "monster_daedalus"};
    *row = (qa_authored_monster){.owner = map->owner, .source = map->component.clock.kind,
        .ordinal = ordinal};
    if (!q2_property_id(map, entities, index, "classname", &row->fields.classname, error) ||
        !q2_property_id(map, entities, index, "targetname", &row->fields.targetname, error) ||
        !q2_property_id(map, entities, index, "target", &row->fields.target, error) ||
        !q2_property_id(map, entities, index, "killtarget", &row->fields.killtarget, error) ||
        !q2_property_id(map, entities, index, "message", &row->fields.message, error) ||
        !q2_property_id(map, entities, index, "deathtarget", &row->death_target, error) ||
        !q2_property_id(map, entities, index, "item", &row->drop_item, error) ||
        !q2_property_id(map, entities, index, "itemtarget", &row->item_target, error) ||
        !q2_property_id(map, entities, index, "healthtarget", &row->health_target, error) ||
        !q2_property_id(map, entities, index, "combattarget", &row->combat_target, error) ||
        !entity_u32(entities, index, "spawnflags", 0, &row->spawnflags, error) ||
        !entity_float(entities, index, "delay", 0, &row->fields.delay_seconds, error) ||
        !entity_float(entities, index, "wait", 0, &row->fields.wait_seconds, error) ||
        !entity_vector(entities, index, "origin", qa_v3(0, 0, 0), &row->authored_origin, NULL, error)) return false;
    row->route = row->fields.target;
    row->activation = map->kind == APPLICATION_PROVIDER_Q2 && (row->spawnflags & 2u) ?
        QA_MONSTER_DORMANT : QA_MONSTER_ACTIVE;
    const char *classname = qa_strings_cstr(qa_session_strings(map->application->session), row->fields.classname);
    const char *const *ordinary = map->kind == APPLICATION_PROVIDER_Q1 ? q1_ordinary : q2_ordinary;
    size_t count = map->kind == APPLICATION_PROVIDER_Q1 ? sizeof(q1_ordinary) / sizeof(*q1_ordinary) :
        sizeof(q2_ordinary) / sizeof(*q2_ordinary);
    bool supported = false;
    for (size_t i = 0; i < count; ++i) if (classname && !strcmp(classname, ordinary[i])) { supported = true; break; }
    uint32_t inhibition = map->kind == APPLICATION_PROVIDER_Q1 ? 0xf00u :
        map->product->edition == QA_EDITION_RERELEASE ? 0xff00u : 0x1f00u;
    uint32_t flags = map->kind == APPLICATION_PROVIDER_Q1 ?
        classname && !strcmp(classname, "monster_zombie") ? 2u : 1u : 3u;
    if (!supported || (row->spawnflags & ~(inhibition | flags)) ||
        (classname && !strcmp(classname, "monster_zombie") && (row->spawnflags & 1u)))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected monster does not preserve this authored boss or special spawn obligation");
    return true;
}

static bool q2_spawn_native_fields(application_provider *map_provider,
    application_provider *actor_provider, const char *authored,
    const char *selected_classname, bool monster, const qa_entities *entities,
    size_t index, bool has_source, uint32_t source_slot, const qa_authored_monster *authored_monster,
    qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    application_provider *entity_provider = !authored_monster && map_provider->kind == APPLICATION_PROVIDER_Q2 ?
        map_provider : actor_provider;
    qa_string_id definition;
    qa_body_state body = {0};
    float angle;
    if (!qa_strings_intern_cstr(
            qa_session_strings(map_provider->application->session),
            selected_classname, &definition, error) ||
        !entity_vector(entities, index, "origin", qa_v3(0, 0, 0),
                       &body.origin, NULL, error) ||
        !entity_vector(entities, index, "angles", qa_v3(0, 0, 0),
                       &body.angles, NULL, error) ||
        !entity_vector(entities, index, "velocity", qa_v3(0, 0, 0),
                       &body.velocity, NULL, error) ||
        !entity_float(entities, index, "angle", body.angles.y, &angle,
                       error)) {
        return false;
    }
    qa_bytes authored_angles;
    if (!qa_entity_value(entities, index, "angles", &authored_angles))
        body.angles.y = angle;
    qa_builtin_services builtins = application_builtin_services(
        map_provider->application, map_provider->application->world,
        map_provider->application->physics);
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(
            &builtins,
            &(qa_builtin_spawn){.owner = actor_provider->owner,
                                .definition = definition,
                                .has_source = has_source && map_provider == actor_provider,
                                .source_slot = source_slot,
                                .body = body},
            &actor, error)) {
        return false;
    }

    size_t property_count;
    const qa_entity_property *properties =
        entity_properties(entities, index, &property_count);
    qa_entity_property selected_property = {
        .key = {(const uint8_t *)"classname", sizeof("classname") - 1},
        .value = {(const uint8_t *)selected_classname, strlen(selected_classname)}};
    if (authored_monster) { properties = &selected_property; property_count = 1; }
    bool handled = false;
    bool ok = qa_q2_entity_spawn(
        entity_provider->state.q2, actor,
        &(qa_q2_map_fields){.properties = properties,
                            .count = property_count,
                            .ordinal = has_source && entity_provider == map_provider ? source_slot : UINT32_MAX},
        &handled, error);
    if (ok && authored_monster) ok = application_monster_admit(actor_provider, actor, authored_monster, error);
    if (ok && !handled && !authored_monster &&
        qa_actors_get(qa_session_actors(map_provider->application->session),
                      actor) != NULL) {
        qa_q2_item_spawn item = {.classname = authored};
        int32_t count;
        if (!entity_u32(entities, index, "spawnflags", 0,
                        &item.spawnflags, error) ||
            !entity_i32(entities, index, "count", 0, &count, error) ||
            !entity_float(entities, index, "delay", 0, &item.delay, error) ||
            !q2_property_id(map_provider, entities, index, "target",
                            &item.target, error) ||
            !q2_property_id(map_provider, entities, index, "killtarget",
                            &item.killtarget, error) ||
            !q2_property_id(map_provider, entities, index, "message",
                            &item.message, error) ||
            !q2_property_id(map_provider, entities, index, "team", &item.team,
                            error)) {
            ok = false;
        } else {
            item.count = count;
            ok = qa_q2_item_spawn_actor(entity_provider->state.q2, actor, &item,
                                        &handled, error);
        }
    }
    if (ok && !handled && monster &&
        qa_actors_get(qa_session_actors(map_provider->application->session),
                      actor) != NULL) {
        qa_q2_monster_spawn_options options = {
            .classname = selected_classname,
        };
        if (authored_monster) {
            ok = qa_q2_monster_spawn(actor_provider->state.q2, actor, &options, error);
            handled = ok;
        } else if (!entity_u32(entities, index, "spawnflags", 0,
                        &options.spawnflags, error) ||
            !entity_float(entities, index, "scale", 0, &options.scale,
                          error) ||
            !entity_float(entities, index, "health_multiplier", 0,
                          &options.health_multiplier, error))
            ok = false;
        else {
            ok = qa_q2_monster_spawn(actor_provider->state.q2, actor,
                                     &options, error);
            handled = ok;
        }
    }
    if (ok && !handled &&
        qa_actors_get(qa_session_actors(map_provider->application->session),
                      actor) != NULL) {
        qa_error ignored = {0};
        bool released = qa_session_release(map_provider->application->session,
                                           actor, &ignored);
        if (!released && error != NULL)
            *error = ignored;
        if (!released)
            ok = false;
        else if (!has_source)
            ok = application_fail(error, QA_ERROR_NOT_FOUND,
                                  "Q2 dynamic classname has no selected spawn adapter");
        actor = (qa_actor_id){0};
    }
    if (!ok &&
        qa_actors_get(qa_session_actors(map_provider->application->session),
                      actor) != NULL) {
        qa_error ignored = {0};
        (void)qa_session_release(map_provider->application->session, actor,
                                 &ignored);
    }
    if (ok)
        *out = actor;
    return ok;
}

static bool native_entity_spawn(application_provider *map_provider,
    application_provider *actor_provider, const char *authored,
    const char *selected_classname, bool monster, const qa_entities *entities,
    size_t index, bool has_source, uint32_t source_slot, qa_arena *arena, qa_actor_id *out,
    qa_error *error)
{
    *out = (qa_actor_id){0};
    qa_authored_monster authored_row = {0};
    const qa_authored_monster *mission = NULL;
    if (monster && (actor_provider != map_provider || strcmp(authored, selected_classname))) {
        if (!monster_authored_fields(map_provider, entities, index, source_slot, &authored_row, error)) return false;
        mission = &authored_row;
    }
    if (actor_provider->kind == APPLICATION_PROVIDER_Q2)
        return q2_spawn_native_fields(map_provider, actor_provider, authored,
            selected_classname, monster, entities, index, has_source, source_slot, mission, out, error);
    if (actor_provider->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected authored monster has no native Q1 or Q2 constructor");
    bool ok = q1_spawn_entity(actor_provider, entities, index, arena,
        selected_classname, has_source && map_provider == actor_provider,
        source_slot, mission, out, error);
    if (!ok && out->registry && qa_actors_get(qa_session_actors(map_provider->application->session), *out))
        (void)qa_session_release(map_provider->application->session, *out, NULL);
    return ok;
}

static bool q2_spawn_fields(application_provider *map_provider,
    const qa_launch_choices *choices, const qa_entities *entities, size_t index,
    bool has_source, uint32_t source_slot, qa_actor_id *out, qa_error *error)
{
    qa_arena arena;
    qa_arena_init(&arena, 256);
    qa_bytes name;
    const char *authored = NULL, *classname = NULL;
    application_provider *selected = NULL;
    bool monster = false;
    bool ok = qa_entity_value(entities, index, "classname", &name);
    if (!ok) application_fail(error, QA_ERROR_FORMAT, "Q2 map entity has no classname");
    if (ok) ok = (authored = arena_text(&arena, name, error)) != NULL;
    if (ok) ok = monster_route(map_provider, choices, authored, &selected, &classname, &monster, error) &&
        native_entity_spawn(map_provider, selected, authored, classname, monster,
            entities, index, has_source, source_slot, &arena, out, error);
    qa_arena_destroy(&arena);
    return ok;
}

static bool q2_spawn(void *opaque, const char *classname, qa_vec3 origin,
                     qa_vec3 angles, qa_vec3 velocity, qa_actor_id *out,
                     qa_error *error)
{
    application_provider *provider = opaque;
    if (classname == NULL || classname[0] == '\0' || out == NULL ||
        !qa_vec_finite(origin) || !qa_vec_finite(angles) ||
        !qa_vec_finite(velocity))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid Q2 dynamic authored spawn");
    char origin_text[128], angles_text[128], velocity_text[128];
    int origin_length = snprintf(origin_text, sizeof(origin_text), "%.9g %.9g %.9g",
                                 origin.x, origin.y, origin.z);
    int angles_length = snprintf(angles_text, sizeof(angles_text), "%.9g %.9g %.9g",
                                 angles.x, angles.y, angles.z);
    int velocity_length = snprintf(velocity_text, sizeof(velocity_text), "%.9g %.9g %.9g",
                                   velocity.x, velocity.y, velocity.z);
    if (origin_length < 0 || (size_t)origin_length >= sizeof(origin_text) ||
        angles_length < 0 || (size_t)angles_length >= sizeof(angles_text) ||
        velocity_length < 0 ||
        (size_t)velocity_length >= sizeof(velocity_text))
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q2 dynamic spawn vector formatting failed");
    qa_entity_property properties[] = {
        {.key = {(const uint8_t *)"classname", 9},
         .value = {(const uint8_t *)classname, strlen(classname)}},
        {.key = {(const uint8_t *)"origin", 6},
         .value = {(const uint8_t *)origin_text, (size_t)origin_length}},
        {.key = {(const uint8_t *)"angles", 6},
         .value = {(const uint8_t *)angles_text, (size_t)angles_length}},
        {.key = {(const uint8_t *)"velocity", 8},
         .value = {(const uint8_t *)velocity_text, (size_t)velocity_length}},
    };
    qa_entities entities = {
        .records = &(qa_entity_record){.first_property = 0,
                                      .property_count = 4},
        .count = 1,
        .properties = properties,
        .property_count = 4,
    };
    const qa_launch_snapshot *snapshot =
        qa_configuration_current(provider->application->configuration);
    return q2_spawn_fields(provider, qa_launch_snapshot_choices(snapshot),
                           &entities, 0, false, UINT32_MAX, out, error);
}

static bool q2_spawn_map(application_provider *provider,
                         const qa_launch_choices *choices,
                         const qa_entities *entities,
                         application_player_travel *players, qa_error *error)
{
    int32_t clients;
    if (!application_native_q2_source_integer(provider, "maxclients", &clients, error)) return false;
    qa_q2_entity_services services = q2_entity_services(provider, choices);
    services.spawn = q2_spawn;
    if (!qa_q2_entities_configure(provider->state.q2, &services, error))
        return false;
    if (!application_q2_original_game(provider, error))
        return false;
    if (entities->count > SIZE_MAX / sizeof(qa_q2_wire_binding))
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q2 authored spawn bindings are exhausted");
    qa_q2_wire_binding *bindings = calloc(entities->count ? entities->count : 1,
                                         sizeof(*bindings));
    if (!bindings)
        return application_fail(error, QA_ERROR_MEMORY,
                                "Cannot retain Q2 authored spawn bindings");
    bool ok = true;
    for (size_t index = 0; index < entities->count; ++index) {
        uint32_t source_slot = 0;
        if (index && !qa_q2_wire_spawn_slot(provider->state.q2, &source_slot, error)) {
            ok = false;
            break;
        }
        qa_actor_id actor = {0};
        if (!q2_spawn_fields(provider, choices, entities, index, true,
                             source_slot, &actor, error)) {
            ok = false;
            break;
        }
        bindings[index] = (qa_q2_wire_binding){.actor = actor,
            .source_owner = provider->owner, .source_slot = source_slot,
            .in_use = actor.registry != 0};
        qa_bytes classname;
        if (qa_entity_value(entities, index, "classname", &classname) &&
            classname.size == 10 &&
            memcmp(classname.data, "worldspawn", 10) == 0 && actor.registry)
            provider->application->physics->world_actor = actor;
    }
    if (ok) ok = qa_q2_entities_post_spawn(provider->state.q2, error) &&
        application_q2_original_level(provider, error) &&
        application_players_q2_points(players, (uint32_t)clients,
            bindings, entities->count, error);
    free(bindings);
    return ok;
}

static bool q3_area_portal(application_provider *provider,
                           qa_actor_id actor, bool open, qa_error *error)
{
    qa_linked_body linked;
    if (!qa_world_linked(provider->application->world, actor, &linked))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "Q3 area portal actor is not linked");
    const qa_world_leaf_visibility_result *r;
    qa_world *world=provider->application->world;
    if(!qa_world_leaf_visibility(world,actor,&linked.absolute_bounds,QA_WORLD_LEAVES_BOX,
        qa_world_trace_scratch(world,qa_world_geometry(world)),&r,error)) return false;
    if(r->portal_invalid) return application_fail(error,QA_ERROR_FORMAT,"Q3 area portal area exceeds source range");
    if(r->portal_third) return application_fail(error,QA_ERROR_FORMAT,"Q3 area portal touches more than two areas");
    int32_t first=r->portal_areas.area,second=r->portal_areas.area2;
    if(first<0 || second<0) return true;
    return application_portal_q3(provider, (uint32_t)first,
                                 (uint32_t)second, open, error);
}

static bool q3_event(void *opaque, const qa_q3_map_event *event,
                     qa_error *error)
{
    application_provider *provider = opaque;
    if (event->kind == QA_Q3_MAP_CVAR) {
        qa_strings *strings = qa_session_strings(provider->application->session);
        const char *name = qa_strings_cstr(strings, event->name);
        const char *value = qa_strings_cstr(strings, event->text);
        if (!name || !value || !application_native_q3_settings_source_set(provider, name, value, error))
            return false;
    }
    return (event->kind != QA_Q3_MAP_AREA_PORTAL ||
            q3_area_portal(provider,event->actor,event->value != 0,error)) &&
           application_emit_q3_map(provider,event,error);
}

static bool q3_item_disabled(void *opaque, uint32_t item_index)
{
    application_provider *provider = opaque;
    qa_q3_product product = !strcmp(provider->product->campaign, "missionpack")
        ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    const qa_q3_item *items = qa_q3_items(product, NULL);
    char name[128];
    snprintf(name, sizeof(name), "disable_%s", items[item_index].classname);
    const qa_cvar_view *variable = qa_cvars_find(
        application_native_q3_console_registry(provider), name);
    const unsigned char *text = (const unsigned char *)(variable ? variable->value : "");
    while (*text && (*text < 128 ? (int)*text : (int)*text - 256) <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9')
        value = value * UINT32_C(10) + (uint32_t)(*text++ - '0');
    if (negative) value = UINT32_C(0) - value;
    return value != 0;
}

static bool q3_world_gravity(void *opaque, float gravity, qa_error *error)
{
    application_provider *provider = opaque;
    if (!isfinite(gravity))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 world gravity is nonfinite");
    provider->application->physics->gravity = gravity;
    return true;
}

static void q3_diagnostic(void *opaque, qa_actor_id actor,
                          const char *message)
{
    application_provider *provider = opaque;
    qa_error error = {0};
    qa_string_id text;
    if (!qa_strings_intern_cstr(
            qa_session_strings(provider->application->session), message,
            &text, &error) ||
        !application_emit_q3_map(
            provider,
            &(qa_q3_map_event){.kind = QA_Q3_MAP_PRINT,
                               .actor = actor,
                               .text = text},
            &error))
        application_fault(provider->application, &error);
}

static qa_q3_map_options q3_map_options(application_provider *provider,
                                        const qa_launch_choices *choices)
{
    bool warmup = false;
    for (size_t index = 0; index < choices->mode_count; ++index)
        if (choices->modes[index].rules.warmup_seconds > 0) {
            warmup = true;
            break;
        }
    application_q3_world_startup replacement;
    bool replacing = application_q3_world_restart_source(provider->application,
                                                          provider, &replacement);
    return (qa_q3_map_options){
        .targets = provider->application->targets,
        .context = provider,
        .random_seed = replacing ? replacement.random_seed : (uint32_t)(
            provider->owner * UINT32_C(2246822519) ^
            (uint32_t)provider->application->publication_generation),
        .start_time_ms = replacing
            ? (int32_t)(uint32_t)(replacement.initial_time_ns / UINT64_C(1000000)) : 0,
        .warmup = replacing ? replacement.warmup : warmup,
        .restarted = replacing ? replacement.restarted : 0,
        .event = q3_event,
        .item_disabled = q3_item_disabled,
        .world_gravity = q3_world_gravity,
        .diagnostic = q3_diagnostic,
    };
}

static bool q3_begin_map(application_provider *provider,
                         const qa_launch_choices *choices, qa_error *error)
{
    qa_q3_map_options options = q3_map_options(provider, choices);
    int32_t warmup;
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_DO_WARMUP, &warmup, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_RESTARTED, &options.restarted, error)) return false;
    options.warmup = warmup != 0;
    bool ok = provider->map_bound
                  ? qa_q3_maps_reset(provider->state.q3, &options, error)
                  : qa_q3_maps_bind(provider->state.q3, &options, error);
    if (ok)
        provider->map_bound = true;
    return ok;
}

bool application_map_restore_identity(qa_application *application,
                                       const qa_launch_snapshot *snapshot,
                                       qa_error *error)
{
    application_publication names = {.candidate = snapshot};
    return application_map_identity(application, &names, &application->current_map, error);
}

bool application_map_restore_bind(qa_application *application,
                                    const qa_launch_snapshot *snapshot,
                                    qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!application_map_restore_identity(application, snapshot, error))
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (!provider->attached || !provider->constructed || provider->map_bound)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "restored map binding requires fresh attached providers");
        if (provider->kind == APPLICATION_PROVIDER_Q1) {
            if (!q1_begin_map(provider, provider->product,
                               application->current_map, true, error))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
            qa_q2_entity_services services = q2_entity_services(provider, choices);
            services.spawn = q2_spawn;
            if (!qa_q2_entities_configure(provider->state.q2, &services, error))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
            qa_q3_map_options options = q3_map_options(provider, choices);
            if (!qa_q3_maps_bind_restore(provider->state.q3, &options, error))
                return false;
            provider->map_bound = true;
        }
    }
    return application_players_restore_prepare(application, choices, error);
}

static bool q3_spawn_map(application_provider *provider,
                         const qa_entities *entities, qa_error *error)
{
    const qa_launch_snapshot *snapshot = provider->application->routing_snapshot;
    if (snapshot == NULL)
        snapshot = qa_application_launch(provider->application);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (choices == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 authored spawn has no installed launch");
    for (size_t index = 0; index < entities->count; ++index) {
        if (index > UINT32_MAX)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q3 authored entity ordinal is exhausted");
        size_t count;
        const qa_entity_property *properties =
            entity_properties(entities, index, &count);
        qa_q3_map_spawn_result result;
        if (!qa_q3_map_spawn(provider->state.q3,
                             &(qa_q3_map_fields){.properties = properties,
                                                 .count = count,
                                                 .ordinal = (uint32_t)index},
                             &result, error))
            return false;
        if (result.status == QA_Q3_MAP_WORLD)
            provider->application->physics->world_actor = result.actor;
    }
    if (!qa_q3_maps_post_spawn(provider->state.q3, error))
        return false;
    if (!application_native_q3_settings_source_loaded(provider, error)) return false;
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    static const char *const compatibility[] = {"g_gametype", "sv_maxclients"};
    for (size_t i = 0; i < sizeof(compatibility) / sizeof(compatibility[0]); ++i) {
        const qa_cvar_view *variable = qa_cvars_find(cvars, compatibility[i]);
        if (variable == NULL || variable->owner != provider->owner)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Q3 authored map lost its source settings");
        qa_cvars_clear_modified(cvars, compatibility[i]);
    }
    return true;
}

bool application_q3_round_map_prepare(qa_application *application,
    application_provider *provider, qa_entities *out, qa_error *error)
{
    if (!application || !provider || !out || out->records || out->properties ||
        provider->application != application || !provider->constructed ||
        !provider->attached || !provider->map_bound ||
        provider != application_world_provider(application, QA_ROLE_ENTITIES, "") ||
        !application->map_resource || !application->geometry ||
        !application->map_view_ready)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round requires its retained authored map");
    qa_bsp_view map;
    if (!qa_bsp_open(qa_resource_bytes(application->map_resource), &map, error))
        return false;
    if (!map.lumps[QA_BSP_ENTITIES].present)
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q3 round retained map has no entity source");
    return qa_entities_parse(map.lumps[QA_BSP_ENTITIES].bytes,
        map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1, out, error);
}

bool application_q3_round_map_spawn(application_provider *provider,
    const qa_entities *entities, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !entities ||
        provider->application->operation != APPLICATION_CONFIGURING ||
        !provider->map_bound || !provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round spawn requires its reset native source");
    return q3_spawn_map(provider, entities, error);
}

bool application_map_spawn_point(qa_application *application,
                           const qa_launch_choices *choices,
                           qa_string_id *out, qa_error *error)
{
    *out = QA_STRING_NONE;
    if (choices->world.explicit_spawn_point) {
        const char *point = choices->world.spawn_point;
        if (point == NULL || point[0] == '\0')
            return true;
        return qa_strings_intern_cstr(qa_session_strings(application->session),
                                       point, out, error);
    }
    const char *command = choices->world.start_command;
    if (command == NULL)
        return true;
    const char *separator = strchr(command, '$');
    if (separator == NULL || separator[1] == '\0')
        return true;
    const char *start = separator + 1;
    size_t length = strcspn(start, "+ \t\r\n\"'");
    if (length == 0)
        return true;
    return qa_strings_intern(
        qa_session_strings(application->session),
        (qa_bytes){(const uint8_t *)start, length}, out, error);
}

static bool configure_horde(qa_application *application,
                             const qa_launch_choices *choices,
                             application_provider *map_provider, qa_error *error)
{
    for (size_t i = 0; i < application->mode_count; ++i) {
        qa_mode_view view;
        if (!qa_modes_read(application->modes, application->mode_ids[i], &view, error))
            return false;
        if (view.rules.source != QA_MODE_Q1_HORDE || !view.rules.enabled)
            continue;
        application_provider *provider = active_provider_named(application,
            choices->modes[i].instance);
        if (provider == NULL || !provider->attached || !provider->constructed ||
            provider->kind != APPLICATION_PROVIDER_Q1)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "selected Horde mode requires its native Q1 map owner");
        if (map_provider == NULL || map_provider->kind != APPLICATION_PROVIDER_Q1)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "selected Horde mode requires a native authored Q1 layout");
        size_t capacity = qa_actors_capacity(qa_session_actors(application->session));
        if (capacity > SIZE_MAX / sizeof(qa_horde_point))
            return application_fail(error, QA_ERROR_MEMORY, "Horde authored point roster is too large");
        qa_horde_point *points = calloc(capacity == 0 ? 1 : capacity, sizeof(*points));
        if (points == NULL)
            return application_fail(error, QA_ERROR_MEMORY, "Cannot observe Horde authored points");
        qa_horde_options options;
        size_t count = 0;
        bool found = false;
        bool ok = qa_q1_game_map_horde_read(map_provider->state.q1, &options, points,
                                            capacity, &count, &found, error);
        if (ok && !found)
            ok = application_fail(error, QA_ERROR_NOT_FOUND,
                                  "selected Horde source has no authored manager");
        if (ok)
            ok = qa_modes_horde_configure(application->modes, application->mode_ids[i],
                                            &options, points, count, error);
        free(points);
        if (!ok)
            return false;
    }
    return true;
}

bool application_map_publish(qa_application *application,
                             application_publication *publication,
                             qa_error *error)
{
    if (application == NULL || publication == NULL ||
        publication->candidate == NULL || publication->map_provider == NULL ||
        !publication->entities_parsed || application->world == NULL ||
        application->geometry == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map publication lacks prepared world state");
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    qa_catalog *catalog = qa_launch_snapshot_catalog(publication->candidate);
    qa_string_id current_map, spawn_point;
    if (!application_map_identity(application, publication, &current_map, error) ||
        !application_map_spawn_point(application, choices, &spawn_point, error))
        return false;

    application->current_map = current_map;
    application->map_geometry = choices->world.geometry;
    application->map_presentation = choices->world.presentation;
    if (publication->map_provider->kind != APPLICATION_PROVIDER_Q1 &&
        publication->map_provider->kind != APPLICATION_PROVIDER_QC)
        application->q1_paused = false;
    ++application->map_revision;
    application->map_view_ready = true;
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (provider != NULL && provider->attached && provider->constructed &&
            provider->kind == APPLICATION_PROVIDER_Q3 &&
            (!application_native_q3_settings_register(provider, error) ||
             (provider->map_bound &&
              !application_native_q3_settings_reset_cache(provider, __DATE__, error))))
            return false;
    }
    if (!application_bots_prepare(application, choices, &publication->map,
                                   &publication->entities, error))
        return false;
    application->physics->world_actor = (qa_actor_id){0};
    application->physics->gravity = 800.0f;
    bool carry, unit;
    const qa_q2_landmark *landmark;
    application_map_travel_options(application, &carry, &unit, &landmark);
    (void)carry; (void)landmark;
    /* Reset retired source state before Q3 allocates its level actors. */
    for (unsigned phase = 0; phase < 2; ++phase) {
        for (size_t index = 0; index < application->provider_count; ++index) {
            application_provider *provider = application->providers[index];
            if (provider == NULL || !provider->attached || !provider->constructed ||
                ((provider->kind == APPLICATION_PROVIDER_Q3) != (phase == 1)))
                continue;
            if (unit) {
                provider->q1_server_flags = 0;
                provider->q2_server_flags = 0;
            }
            const qa_product *product = qa_catalog_product(
                catalog, provider->launch->selection.product);
            if (product == NULL)
                return application_fail(error, QA_ERROR_NOT_FOUND,
                                        "provider product disappeared during map publication");
            if (provider->kind == APPLICATION_PROVIDER_Q1) {
                if (!q1_begin_map(provider, product, current_map, false, error))
                    return false;
            } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
                if (unit && !qa_q2_campaign_leave_unit(provider->state.q2, error))
                    return false;
                if (!qa_q2_begin_map(provider->state.q2, current_map, spawn_point,
                                     error))
                    return false;
                qa_q2_entity_services services =
                    q2_entity_services(provider, choices);
                services.spawn = q2_spawn;
                if (!qa_q2_entities_configure(provider->state.q2, &services,
                                              error))
                    return false;
                provider->map_bound = true;
            } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
                if (!q3_begin_map(provider, choices, error) ||
                    !application_native_q3_settings_source_init(provider, error) ||
                    !application_native_q3_ipfilters_init(provider, error) ||
                    !application_native_q3_settings_install(provider, error))
                    return false;
            }
        }
    }

    bool spawned = false;
    switch (publication->map_provider->kind) {
    case APPLICATION_PROVIDER_Q1: {
        size_t models = qa_bsp_record_count(&publication->map, QA_BSP_MODELS);
        if (!models || models - 1 > UINT32_MAX || publication->entities.count > UINT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT,
                                    "Q1 source map exceeds its native physical extent");
        if (!qa_q1_wire_begin_world(publication->map_provider->state.q1,
                choices->world.map, (uint32_t)(models - 1),
                (uint32_t)publication->entities.count, error))
            return false;
        spawned = q1_spawn_map(publication->map_provider, choices,
                            &publication->entities, publication->players, error);
        break;
    }
    case APPLICATION_PROVIDER_Q2:
        spawned = q2_spawn_map(publication->map_provider, choices,
                            &publication->entities, publication->players, error);
        break;
    case APPLICATION_PROVIDER_Q3:
        spawned = q3_spawn_map(publication->map_provider,
                            &publication->entities, error);
        break;
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        spawned = application_guest_spawn_map(publication->map_provider,
            &publication->map, &publication->entities, current_map, spawn_point, error);
        break;
    }
    if (!spawned)
        return false;
    if (!configure_horde(application, choices, publication->map_provider, error))
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (provider == publication->map_provider || !provider->attached ||
            !provider->constructed)
            continue;
        const qa_launch_instance *instance = qa_launch_snapshot_find(
            publication->candidate, provider->launch->selection.instance);
        if (instance == NULL || instance->state != provider)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "map provider has no current launch instance");
        if (instance->roles & QA_ROLE_BIT(QA_ROLE_MONSTERS)) {
            if (provider->kind == APPLICATION_PROVIDER_Q1 &&
                (!qa_q1_game_maps_finish(provider->state.q1, error) ||
                 !application_native_q1_wire_resources_prepare(provider, error)))
                return false;
            if (provider->kind == APPLICATION_PROVIDER_Q2 &&
                !qa_q2_entities_post_spawn(provider->state.q2, error))
                return false;
        }
        if (instance->roles == 0 &&
            !(provider->kind == APPLICATION_PROVIDER_QC && provider->state.qc.qualified))
            continue;
        if (provider->kind == APPLICATION_PROVIDER_QC &&
            !application_qc_initialize_map(provider, &publication->map,
                &publication->entities, current_map, spawn_point, error))
            return false;
        if(provider->kind==APPLICATION_PROVIDER_NATIVE&&provider->state.native.q2_engine&&
            provider->state.native.q2_engine->profile!=QA_NATIVE_Q2_CGAME_API2023&&
            !application_native_q2_initialize_supplemental(provider,current_map,spawn_point,error))
            return false;
    }
    if (publication->map_provider->kind == APPLICATION_PROVIDER_Q3 &&
        !application_bots_native_q3_initialize(publication->map_provider, error))
        return false;
    if (!application_players_publish(application, choices, publication->players, error))
        return false;
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (!provider->attached || !provider->constructed)
            continue;
        if (!application_guest_bots_admit(provider, error) ||
            !application_guest_clients_drain(provider, error))
            return false;
    }
    return true;
}

#include "map_private.h"
#include "qa/text.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (!qa_parse_number(value, out, error) || !isfinite(*out))
        return application_fail(error, QA_ERROR_FORMAT,
                                "map entity contains an invalid number");
    return true;
}

static bool entity_float(const qa_entities *entities, size_t entity,
                         const char *key, float fallback, float *out,
                         qa_error *error)
{
    double value;
    if (!entity_number(entities, entity, key, fallback, &value, error))
        return false;
    if (value < -FLT_MAX || value > FLT_MAX)
        return application_fail(error, QA_ERROR_FORMAT,
                                "map entity number exceeds float range");
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

static bool map_name(qa_application *application,
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
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    bool carry, unit;
    const qa_q2_landmark *landmark;
    application_map_travel_options(application, &carry, &unit, &landmark);
    if (!application_players_prepare(application, publication, carry, unit,
                                      landmark, &publication->players, error))
        return false;
    if (publication->map_provider->kind == APPLICATION_PROVIDER_Q1 &&
        publication->entities.count != 0) {
        int32_t world_type;
        if (!entity_i32(&publication->entities, 0, "worldtype", 0, &world_type, error))
            return false;
        application_players_world_type(publication->players, world_type);
    }
    qa_strings *strings = qa_session_strings(application->session);
    for (size_t i = 0; i < publication->entities.count; ++i) {
        qa_bytes name;
        if (!qa_entity_value(&publication->entities, i, "classname", &name))
            continue;
        bool player_point = (name.size >= 12 && !memcmp(name.data, "info_player_", 12)) ||
                            (name.size >= 8 && !memcmp(name.data, "team_CTF", 8)) ||
                            (name.size >= 13 && !memcmp(name.data, "dm_dball_team", 13)) ||
                            (name.size == 15 && !memcmp(name.data, "testplayerstart", 15)) ||
                            (name.size == 21 && !memcmp(name.data, "info_vote_destination", 21));
        if (!player_point)
            continue;
        if (i > UINT32_MAX - choices->seat_count)
            return application_fail(error, QA_ERROR_MEMORY, "authored client source slots are exhausted");
        if (publication->map_provider->kind == APPLICATION_PROVIDER_Q3) {
            /* Retain names without re-decoding source numeric fields or
             * admitting points that the native decoder filters out. */
            qa_bytes target_value;
            qa_string_id target = QA_STRING_NONE;
            if (qa_entity_value(&publication->entities, i, "targetname", &target_value) &&
                !qa_strings_intern(strings, target_value, &target, error))
                return false;
            if (target != QA_STRING_NONE && !application_players_point(publication->players,
                    (qa_mode_spawnpoint){0}, target, (uint32_t)(i + choices->seat_count), error))
                return false;
            continue;
        }
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
        uint32_t ordinal = (uint32_t)(i + choices->seat_count);
        if (!application_players_point(publication->players, point, target, ordinal, error))
            return false;
    }
    return true;
}

static bool emit_map_event(application_provider *provider,
                           qa_builtin_event event, qa_error *error)
{
    event.provider = provider->owner;
    event.time_ns = qa_session_elapsed(provider->application->session);
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
    qa_application *application = provider->application;
    bool found = false, ambiguous = false;
    if (application->modes)
        for (size_t i = 0; i < application->mode_count; ++i) {
            qa_mode_id id = application->mode_ids[i];
            qa_mode_view view;
            if (!qa_modes_read(application->modes, id, &view, error)) return false;
            if (!view.rules.enabled || view.rules.source != QA_MODE_THREEWAVE ||
                application_mode_provider(application, id) != provider) continue;
            if (application->primary_mode_ready &&
                id.slot == application->primary_mode.slot &&
                id.generation == application->primary_mode.generation) {
                *out = id;
                return true;
            }
            if (found) ambiguous = true;
            *out = id;
            found = true;
        }
    if (ambiguous)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 map selects ambiguous ThreeWave mode owners");
    return found || application_fail(error, QA_ERROR_NOT_FOUND,
                                      "Q1 map has no selected ThreeWave mode owner");
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
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_LIGHT,
                                             .family = QA_GAME_Q1,
                                             .resource = pattern,
                                             .code = style},
                          error);
}

static bool q1_integer_intent(void *opaque, int32_t value, qa_error *error)
{
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
    application_provider *provider = q1_actor_provider(opaque, actor);
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1 &&
           qa_q1_game_path_read(provider->state.q1, actor, out);
}

static bool q1_path_change(void *opaque, qa_actor_id actor,
                           const qa_q1_path_change *change, qa_error *error)
{
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
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
                                             .family = QA_GAME_Q1,
                                             .resource = view->map,
                                             .text = view->text,
                                             .origin = view->origin,
                                             .direction = view->angles,
                                             .value = (float)view->exit_after,
                                             .code = (int32_t)view->stage},
                          error);
}

static bool q1_finale_finished(void *opaque)
{
    (void)opaque;
    return false;
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
    return emit_map_event(opaque,
                          (qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                                             .family = QA_GAME_Q1,
                                             .actor = cause,
                                             .text = map,
                                             .value = (float)exit_after,
                                             .code = 1},
                          error);
}

static bool q1_level_travel(void *opaque, qa_string_id map, qa_actor_id cause,
                            qa_error *error)
{
    application_provider *provider = opaque;
    const char *destination = qa_strings_cstr(qa_session_strings(provider->application->session), map);
    if (destination == NULL || !qa_application_queue_travel(provider->application,
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
    (void)opaque;
    (void)name;
    return 0;
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
    const qa_launch_choices *choices, qa_string_id current_map,
    qa_q1_campaign_source_options *out, qa_error *error)
{
    qa_mode_rules mode = {0};
    bool has_mode = choices->mode_count != 0;
    if (has_mode) {
        size_t selected = 0;
        for (size_t index = 0; index < choices->mode_count; ++index)
            if (choices->modes[index].primary_score) {
                selected = index;
                break;
            }
        mode = choices->modes[selected].rules;
    }
    int32_t skill = choices->world.skill < 0
                        ? 0
                        : choices->world.skill > 3 ? 3 : choices->world.skill;
    *out = (qa_q1_campaign_source_options){
        .session = provider->application->session,
        .program = application_q1_program(product->campaign),
        .current_map = current_map,
        .world = provider->application->physics->world_actor,
        .server_flags = &provider->q1_server_flags,
        .rerelease = product->edition == QA_EDITION_RERELEASE,
        .coop = has_mode && mode.kind == QA_MODE_COOPERATIVE,
        .deathmatch = has_mode && mode.kind != QA_MODE_COOPERATIVE &&
                      mode.kind != QA_MODE_SINGLE_PLAYER,
        .registered = product->edition != QA_EDITION_DEMO,
        .official_campaign = product->builtin,
        .skill = skill,
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
                           const qa_launch_choices *choices,
                           qa_string_id current_map,
                           qa_q1_campaign_source **source_out,
                           qa_q1_level **level_out,
                           qa_q1_map_options *map_out, qa_error *error)
{
    *source_out = NULL;
    *level_out = NULL;
    qa_q1_campaign_source_options source_options;
    if (!q1_campaign_source_options(provider, product, choices, current_map,
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
                         const qa_launch_choices *choices,
                         qa_string_id current_map, qa_error *error)
{
    qa_q1_campaign_source *source;
    qa_q1_level *level;
    qa_q1_map_options options;
    if (!q1_map_options(provider, product, choices, current_map, &source,
                        &level, &options, error))
        return false;
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

static bool q1_spawn_entity(application_provider *provider,
                            const qa_entities *entities, size_t index,
                            qa_arena *arena, qa_error *error)
{
    size_t seats = qa_launch_snapshot_choices(provider->application->routing_snapshot)->seat_count;
    if (index > UINT32_MAX - seats)
        return application_fail(error, QA_ERROR_MEMORY,
                                "Q1 authored entity ordinal is exhausted");
    qa_bytes authored_classname;
    if (!qa_entity_value(entities, index, "classname", &authored_classname))
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q1 map entity has no classname");
    char *classname = arena_text(arena, authored_classname, error);
    if (classname == NULL)
        return false;
    qa_q1_map_fields fields = {0};
    qa_q1_boss_fields boss = {0};
    const char *target, *targetname, *killtarget, *message;
    if (!entity_optional_text(entities, index, "model", arena,
                              &fields.model, error) ||
        !entity_optional_text(entities, index, "map", arena,
                              &fields.map, error) ||
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
        .source_slot = index == 0 ? 0u : (uint32_t)(index +
            qa_launch_snapshot_choices(provider->application->routing_snapshot)->seat_count),
        .has_source = true,
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
    if (strcmp(classname, "worldspawn") == 0 && actor.registry)
        provider->application->physics->world_actor = actor;
    return true;
}

static bool q1_spawn_map(application_provider *provider,
                         const qa_entities *entities, qa_error *error)
{
    qa_arena arena;
    qa_arena_init(&arena, 4096);
    bool ok = true;
    for (size_t index = 0; index < entities->count; ++index) {
        qa_arena_reset(&arena);
        if (!q1_spawn_entity(provider, entities, index, &arena, error)) {
            ok = false;
            break;
        }
    }
    qa_arena_destroy(&arena);
    return ok && qa_q1_game_maps_finish(provider->state.q1, error);
}

static bool q2_visual(void *opaque, qa_actor_id actor,
                      const qa_q2_visual *visual, qa_error *error)
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
    return true;
}

static bool q2_read_visual(void *opaque, qa_actor_id actor,
                           qa_q2_visual *out, qa_error *error)
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
    return application_emit_q2_map(provider, event, error) &&
           (event->kind != QA_Q2_MAP_ACHIEVEMENT || event->text == 0 ||
            application_record_achievement(provider, (qa_actor_id){0},
                                           event->text, error));
}

static bool q2_area_portal(void *opaque, uint32_t portal, bool open,
                           qa_error *error)
{
    application_provider *provider = opaque;
    return qa_collision_adjust_portal(provider->application->geometry, portal,
                                      open ? 1 : -1, error);
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
    if (destination == NULL || !qa_application_queue_travel(provider->application,
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

static bool q2_target_name_changed(void *opaque, qa_actor_id actor,
                                   qa_string_id old_name,
                                   qa_string_id new_name, qa_error *error)
{
    (void)actor;
    (void)old_name;
    (void)new_name;
    (void)error;
    application_provider *provider = opaque;
    qa_targets_changed(provider->application->targets);
    return true;
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
        .target_name_changed = q2_target_name_changed,
        .target_anger = q2_target_anger,
        .invoke_use = q2_invoke_use,
        .holds_healthbar = q2_holds_healthbar,
        .camera_player = q2_camera_player,
    };
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

static bool q2_monster_route(application_provider *map_provider,
                             const qa_launch_choices *choices,
                             const char *authored,
                             application_provider **provider_out,
                             const char **classname_out, bool *monster_out,
                             qa_error *error)
{
    *provider_out = map_provider;
    *classname_out = authored;
    *monster_out = strncmp(authored, "monster_", 8) == 0;
    for (size_t index = 0; index < choices->monster_count; ++index) {
        const qa_launch_monster *selection = &choices->monsters[index];
        if (strcmp(selection->authored_classname, authored) != 0)
            continue;
        application_provider *selected = active_provider_named(
            map_provider->application, selection->instance);
        if (selected == NULL)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "selected monster provider is absent");
        *provider_out = selected;
        *classname_out = selection->classname;
        *monster_out = true;
        break;
    }
    if (*classname_out == NULL || (*classname_out)[0] == '\0')
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "selected monster classname is empty");
    if (*monster_out && (*provider_out)->kind != APPLICATION_PROVIDER_Q2)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "selected monster provider has no Q2 authored adapter");
    return true;
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

static bool q2_spawn_fields(application_provider *map_provider,
                            const qa_launch_choices *choices,
                            const qa_entities *entities, size_t index,
                            bool has_source, uint32_t source_slot,
                            qa_actor_id *out, qa_error *error)
{
    qa_arena arena;
    qa_arena_init(&arena, 256);
    qa_bytes authored_classname;
    if (!qa_entity_value(entities, index, "classname", &authored_classname)) {
        qa_arena_destroy(&arena);
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q2 map entity has no classname");
    }
    char *authored = arena_text(&arena, authored_classname, error);
    if (authored == NULL) {
        qa_arena_destroy(&arena);
        return false;
    }
    application_provider *actor_provider;
    const char *selected_classname;
    bool monster;
    if (!q2_monster_route(map_provider, choices, authored, &actor_provider,
                          &selected_classname, &monster, error)) {
        qa_arena_destroy(&arena);
        return false;
    }
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
        qa_arena_destroy(&arena);
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
                                .has_source = has_source,
                                .source_slot = source_slot,
                                .body = body},
            &actor, error)) {
        qa_arena_destroy(&arena);
        return false;
    }

    size_t property_count;
    const qa_entity_property *properties =
        entity_properties(entities, index, &property_count);
    bool handled = false;
    bool ok = qa_q2_entity_spawn(
        map_provider->state.q2, actor,
        &(qa_q2_map_fields){.properties = properties,
                            .count = property_count,
                            .ordinal = source_slot},
        &handled, error);
    if (ok && !handled &&
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
            ok = qa_q2_item_spawn_actor(map_provider->state.q2, actor, &item,
                                        &handled, error);
        }
    }
    if (ok && !handled && monster &&
        qa_actors_get(qa_session_actors(map_provider->application->session),
                      actor) != NULL) {
        qa_q2_monster_spawn_options options = {
            .classname = selected_classname,
        };
        if (!entity_u32(entities, index, "spawnflags", 0,
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
                         const qa_entities *entities, qa_error *error)
{
    qa_q2_entity_services services = q2_entity_services(provider, choices);
    services.spawn = q2_spawn;
    if (!qa_q2_entities_configure(provider->state.q2, &services, error))
        return false;
    for (size_t index = 0; index < entities->count; ++index) {
        if (index > UINT32_MAX - choices->seat_count)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q2 authored entity ordinal is exhausted");
        qa_actor_id actor = {0};
        if (!q2_spawn_fields(provider, choices, entities, index, true,
                             index == 0 ? 0u : (uint32_t)(index + choices->seat_count), &actor, error))
            return false;
        qa_bytes classname;
        if (qa_entity_value(entities, index, "classname", &classname) &&
            classname.size == 10 &&
            memcmp(classname.data, "worldspawn", 10) == 0 && actor.registry)
            provider->application->physics->world_actor = actor;
    }
    return qa_q2_entities_post_spawn(provider->state.q2, error);
}

static bool q3_area_portal(application_provider *provider,
                           qa_actor_id actor, bool open, qa_error *error)
{
    qa_linked_body linked;
    if (!qa_world_linked(provider->application->world, actor, &linked))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "Q3 area portal actor is not linked");
    size_t capacity = 64;
    uint32_t *leaves = NULL;
    qa_leaf_list list;
    for (;;) {
        if (capacity > SIZE_MAX / sizeof(*leaves)) {
            free(leaves);
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q3 area portal leaf set is too large");
        }
        uint32_t *next = realloc(leaves, capacity * sizeof(*next));
        if (next == NULL) {
            free(leaves);
            return application_fail(error, QA_ERROR_MEMORY,
                                    "cannot retain Q3 area portal leaves");
        }
        leaves = next;
        if (!qa_collision_box_leaves(provider->application->geometry,
                                     linked.absolute_bounds, leaves, capacity,
                                     &list, error)) {
            free(leaves);
            return false;
        }
        if (!list.overflow)
            break;
        if (capacity > SIZE_MAX / 2) {
            free(leaves);
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q3 area portal leaf set is exhausted");
        }
        capacity *= 2;
    }
    int32_t first = -1, second = -1;
    bool ok = true;
    for (size_t index = 0; index < list.count; ++index) {
        qa_collision_leaf leaf;
        if (!qa_collision_leaf_at(provider->application->geometry,
                                  leaves[index], &leaf, error)) {
            ok = false;
            break;
        }
        if (leaf.area > INT32_MAX) {
            ok = application_fail(error, QA_ERROR_FORMAT,
                                  "Q3 area portal area exceeds source range");
            break;
        }
        if (leaf.area < 0 || leaf.area == first || leaf.area == second)
            continue;
        if (first < 0)
            first = (int32_t)leaf.area;
        else if (second < 0)
            second = (int32_t)leaf.area;
        else {
            ok = application_fail(error, QA_ERROR_FORMAT,
                                  "Q3 area portal touches more than two areas");
            break;
        }
    }
    free(leaves);
    if (!ok || first < 0 || second < 0)
        return ok;
    return qa_collision_adjust_area_pair(provider->application->geometry,
                                         first, second, open, error);
}

static bool q3_event(void *opaque, const qa_q3_map_event *event,
                     qa_error *error)
{
    application_provider *provider = opaque;
    if (!application_emit_q3_map(provider, event, error))
        return false;
    return event->kind != QA_Q3_MAP_AREA_PORTAL ||
           q3_area_portal(provider, event->actor, event->value != 0, error);
}

static bool q3_item_disabled(void *opaque, uint32_t item_index)
{
    (void)opaque;
    (void)item_index;
    return false;
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
    return (qa_q3_map_options){
        .targets = provider->application->targets,
        .context = provider,
        .random_seed = (uint32_t)(
            provider->owner * UINT32_C(2246822519) ^
            (uint32_t)provider->application->publication_generation),
        .start_time_ms = 0,
        .warmup = warmup,
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
    bool ok = provider->map_bound
                  ? qa_q3_maps_reset(provider->state.q3, &options, error)
                  : qa_q3_maps_bind(provider->state.q3, &options, error);
    if (ok)
        provider->map_bound = true;
    return ok;
}

bool application_map_restore_bind(qa_application *application,
                                    const qa_launch_snapshot *snapshot,
                                    qa_error *error)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    application_publication names = {.candidate = snapshot};
    if (!map_name(application, &names, &application->current_map, error))
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (!provider->attached || !provider->constructed || provider->map_bound)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "restored map binding requires fresh attached providers");
        if (provider->kind == APPLICATION_PROVIDER_Q1) {
            if (!q1_begin_map(provider, provider->product, choices,
                               application->current_map, error))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
            qa_q2_entity_services services = q2_entity_services(provider, choices);
            services.spawn = q2_spawn;
            if (!qa_q2_entities_configure(provider->state.q2, &services, error))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
            if (!q3_begin_map(provider, choices, error))
                return false;
        }
    }
    return application_players_restore_prepare(application, choices, error);
}

static bool q3_spawn_map(application_provider *provider,
                         const qa_entities *entities, qa_error *error)
{
    size_t seats = qa_launch_snapshot_choices(provider->application->routing_snapshot)->seat_count;
    for (size_t index = 0; index < entities->count; ++index) {
        if (index > UINT32_MAX - seats)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q3 authored entity ordinal is exhausted");
        size_t count;
        const qa_entity_property *properties =
            entity_properties(entities, index, &count);
        qa_q3_map_spawn_result result;
        if (!qa_q3_map_spawn(provider->state.q3,
                             &(qa_q3_map_fields){.properties = properties,
                                                 .count = count,
                                                 .ordinal = (uint32_t)(index + seats)},
                             &result, error))
            return false;
    }
    return qa_q3_maps_post_spawn(provider->state.q3, error);
}

bool application_map_spawn_point(qa_application *application,
                           const qa_launch_choices *choices,
                           qa_string_id *out, qa_error *error)
{
    *out = QA_STRING_NONE;
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
    if (!map_name(application, publication, &current_map, error) ||
        !application_map_spawn_point(application, choices, &spawn_point, error))
        return false;

    application->current_map = current_map;
    application->map_geometry = choices->world.geometry;
    application->map_presentation = choices->world.presentation;
    ++application->map_revision;
    application->map_view_ready = true;
    if (!application_bots_prepare(application, choices, &publication->map,
                                   &publication->entities, error))
        return false;
    application->physics->world_actor = (qa_actor_id){0};
    application->physics->gravity = 800.0f;
    bool carry, unit;
    const qa_q2_landmark *landmark;
    application_map_travel_options(application, &carry, &unit, &landmark);
    (void)carry; (void)landmark;
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (provider == NULL || !provider->attached || !provider->constructed)
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
            if (!q1_begin_map(provider, product, choices, current_map, error))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
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
            if (!q3_begin_map(provider, choices, error))
                return false;
        }
    }

    bool spawned = false;
    switch (publication->map_provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        spawned = q1_spawn_map(publication->map_provider,
                            &publication->entities, error);
        break;
    case APPLICATION_PROVIDER_Q2:
        spawned = q2_spawn_map(publication->map_provider, choices,
                            &publication->entities, error);
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
        if (instance->roles == 0)
            continue;
        if (provider->kind == APPLICATION_PROVIDER_QC &&
            !application_qc_initialize_map(provider, &publication->map,
                &publication->entities, current_map, spawn_point, error))
            return false;
    }
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

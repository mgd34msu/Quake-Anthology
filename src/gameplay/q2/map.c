#include "entities/internal.h"
#include "internal.h"
#include "items/internal.h"

bool qa_q2_begin_map(qa_q2_game *game, qa_string_id map_name, qa_string_id spawn_point,
                     qa_error *error) {
    if (!game || !qa_session_safe(game->services.session) ||
        qa_actors_count(qa_session_actors(game->services.session)) ||
        game->current_actor.registry || game->hand_steps) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 map entry requires an empty safe session");
        return false;
    }
    if (game->release_failed) {
        if (error)
            *error = game->release_error;
        return false;
    }
    for (q2_trace_frame *frame = game->trace_frames; frame; frame = frame->next)
        if (frame->active) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 map entry overlaps a source query");
            return false;
        }
    for (q2_player_list *list = game->player_runtime->lists; list; list = list->next)
        if (list->active) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 map entry overlaps a player query");
            return false;
        }
    qa_strings *strings = qa_session_strings(game->services.session);
    const char *map = qa_strings_cstr(strings, map_name);
    const char *spot = spawn_point ? qa_strings_cstr(strings, spawn_point) : "";
    qa_clock_state clock;
    qa_component component = qa_q2_component(game);
    if (!map || !*map || !spot ||
        !qa_session_clock(game->services.session, game->options.owner, &clock) ||
        clock.frame.kind != component.clock.kind || clock.frame_number || clock.elapsed_ns) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 map names or unreset source clock");
        return false;
    }
    for (q2_actor *actor = game->all_actors; actor; actor = actor->all_next) {
        q2_monster_release_state(actor);
        q2_items_release_state(actor);
        q2_client_release_state(actor);
        q2_entity_release_state(actor);
        q2_actor *next = actor->all_next;
        *actor = (q2_actor){.all_next = next, .free_next = next};
    }
    if (game->capacity)
        memset(game->actors, 0, game->capacity * sizeof(*game->actors));
    game->first_actor = game->last_actor = game->retired_actors = NULL;
    game->spare_actors = game->all_actors;
    game->actor_sequence = 0;
    q2_wire_reset(game);
    game->widow_damage_multiplier = 1;
    game->widow_shot_phase = 0;
    game->now_ns = clock.frame.time_ns;
    game->frame_ns = component.clock.interval_ns;
    game->item_runtime->cubes = 0;
    q2_players *players = game->player_runtime;
    free(players->rule_strings[2]);
    free(players->rule_strings[3]);
    players->rule_strings[2] = players->rule_strings[3] = NULL;
    players->rules.spawn_point = spot;
    players->rules.map_name = map;
    qa_q2_player_rules rules = players->rules;
    qa_q2_player_services services = players->services;
    char *owned[5];
    memcpy(owned, players->rule_strings, sizeof(owned));
    q2_player_list *lists = players->lists;
    qa_string_id *rotation_maps = players->rotation_maps;
    *players = (q2_players){.rules = rules, .services = services,
                            .rotation_maps = rotation_maps, .lists = lists};
    memcpy(players->rule_strings, owned, sizeof(owned));
    for (q2_player_list *list = lists; list; list = list->next)
        list->count = 0;
    q2_entities *entities = game->entity_runtime;
    q2_entities previous_entities = *entities;
    *entities = (q2_entities){.services = entities->services,
                              .wind = entities->wind,
                              .wind_capacity = entities->wind_capacity,
                              .primary = game->options.edition == QA_Q2_RERELEASE ? entities->primary : 0,
                              .secondary = game->options.edition == QA_Q2_RERELEASE ? entities->secondary : 0,
                              .primary_changes = game->options.edition == QA_Q2_RERELEASE ? entities->primary_changes : 0,
                              .secondary_changes = game->options.edition == QA_Q2_RERELEASE ? entities->secondary_changes : 0,
                              .level_count = entities->level_count,
                              .visited_maps = entities->visited_maps,
                              .visited_count = entities->visited_count,
                              .visited_capacity = entities->visited_capacity};
    memcpy(entities->levels, previous_entities.levels, sizeof(entities->levels));
    q2_monsters_begin_map(game);
    for (q2_trace_frame *frame = game->trace_frames; frame; frame = frame->next)
        frame->snapshot.count = 0;
    return true;
}

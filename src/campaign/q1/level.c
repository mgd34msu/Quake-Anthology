#include "qa/campaign_q1.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_q1_level {
    qa_q1_level_options options;
    qa_q1_intermission_rule *rules;
    qa_q1_level_state state;
    qa_q1_level_player *players;
    size_t capacity;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static qa_strings *strings(const qa_q1_level *level) {
    return qa_session_strings(level->options.services.session);
}
static bool valid_map(const qa_q1_level *level, qa_string_id id) {
    const char *value = qa_strings_cstr(strings(level), id);
    return value && *value;
}
static bool named(const qa_q1_level *level, qa_string_id id, const char *text) {
    const char *value = qa_strings_cstr(strings(level), id);
    return value && !strcmp(value, text);
}
static bool live(const qa_q1_level *level, qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(level->options.services.session), actor) != NULL;
}
static bool reference_valid(const qa_q1_level *level, qa_actor_id actor, qa_error *error) {
    if (!actor.registry)
        return (!actor.generation && !actor.slot) || fail(error, "Malformed null campaign actor");
    qa_saved_actor_id saved;
    return qa_actors_save_reference(qa_session_actors(level->options.services.session), actor,
                                    &saved, error);
}
qa_q1_level *qa_q1_level_create(const qa_q1_level_options *options, qa_error *error) {
    if (!options || !options->services.session || !options->server_flags || !options->begin ||
        !options->travel || !options->defer_begin ||
        (options->rerelease && !options->achievement) || (options->rule_count && !options->rules) ||
        options->rule_count > SIZE_MAX / sizeof(*options->rules)) {
        fail(error, "Missing Q1 campaign services");
        return NULL;
    }
    qa_q1_level *level = calloc(1, sizeof(*level));
    if (!level) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 level rules");
        return NULL;
    }
    level->options = *options;
    if (!valid_map(level, options->current_map)) {
        qa_q1_level_destroy(level);
        fail(error, "Missing Q1 current map");
        return NULL;
    }
    level->capacity = qa_actors_capacity(qa_session_actors(options->services.session));
    level->players = calloc(level->capacity, sizeof(*level->players));
    if (options->rule_count)
        level->rules = malloc(options->rule_count * sizeof(*level->rules));
    if (!level->players || (options->rule_count && !level->rules)) {
        qa_q1_level_destroy(level);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 level ownership");
        return NULL;
    }
    for (size_t i = 0; i < options->rule_count; ++i) {
        if (!valid_map(level, options->rules[i].id)) {
            qa_q1_level_destroy(level);
            fail(error, "Invalid Q1 source rule name");
            return NULL;
        }
        for (size_t j = 0; j < i; ++j)
            if (options->rules[j].id == options->rules[i].id) {
                qa_q1_level_destroy(level);
                fail(error, "Duplicate Q1 source rule");
                return NULL;
            }
        level->rules[i] = options->rules[i];
    }
    level->options.rules = level->rules;
    return level;
}
void qa_q1_level_destroy(qa_q1_level *level) {
    if (!level)
        return;
    free(level->rules);
    free(level->players);
    free(level);
}
const qa_q1_level_state *qa_q1_level_read(const qa_q1_level *level) { return &level->state; }
static qa_q1_level_player *player(qa_q1_level *level, qa_actor_id actor, qa_error *error) {
    if (actor.slot >= level->capacity || !live(level, actor)) {
        fail(error, "Stale Q1 campaign player");
        return NULL;
    }
    qa_q1_level_player *entry = &level->players[actor.slot];
    if (!qa_actor_id_equal(entry->actor, actor))
        *entry = (qa_q1_level_player){.actor = actor};
    return entry;
}
bool qa_q1_level_reset_player(qa_q1_level *level, qa_actor_id actor, qa_error *error) {
    qa_q1_level_player *entry = player(level, actor, error);
    if (!entry)
        return false;
    *entry = (qa_q1_level_player){.actor = actor};
    return true;
}
void qa_q1_level_actor_released(qa_q1_level *level, qa_actor_id actor) {
    if (actor.slot < level->capacity && qa_actor_id_equal(level->players[actor.slot].actor, actor))
        level->players[actor.slot] = (qa_q1_level_player){0};
}
bool qa_q1_level_note_attack(qa_q1_level *level, qa_actor_id actor, bool axe_only,
                             qa_error *error) {
    if (axe_only)
        return true;
    qa_q1_level_player *entry = player(level, actor, error);
    if (!entry)
        return false;
    entry->fired_weapon = true;
    return true;
}
bool qa_q1_level_note_damage(qa_q1_level *level, qa_actor_id actor, float amount, qa_error *error) {
    if (!isfinite(amount))
        return fail(error, "Nonfinite campaign damage");
    if (!amount)
        return true;
    qa_q1_level_player *entry = player(level, actor, error);
    if (!entry)
        return false;
    entry->took_damage = true;
    return true;
}
bool qa_q1_level_touch(qa_q1_level *level, qa_actor_id trigger, qa_actor_id actor,
                       qa_error *error) {
    for (size_t i = 0; i < level->options.rule_count; ++i) {
        const qa_q1_intermission_rule *rule = &level->rules[i];
        if (rule->touch && !rule->touch(rule->context, level, trigger, actor, error))
            return false;
    }
    return true;
}
bool qa_q1_level_travel(qa_q1_level *level, qa_string_id map, qa_actor_id cause, qa_error *error) {
    if (!valid_map(level, map))
        return fail(error, "Invalid Q1 travel map");
    for (size_t i = 0; i < level->options.rule_count; ++i) {
        const qa_q1_intermission_rule *rule = &level->rules[i];
        bool handled = false;
        if (rule->travel && !rule->travel(rule->context, level, map, cause, &handled, error))
            return false;
        if (handled)
            return true;
    }
    return level->options.travel(level->options.context, map, cause, error);
}
static bool achievements(qa_q1_level *level, qa_string_id next, qa_error *error) {
    if (!level->options.rerelease)
        return true;
    qa_q1_level_options *options = &level->options;
    if (options->skill == 3 && (named(level, options->current_map, "e1m1") ||
                                named(level, options->current_map, "e4m6"))) {
        qa_builtin_actor_snapshot roster = {0};
        if (!qa_builtin_players(&options->services, &roster, error)) {
            qa_builtin_snapshot_free(&roster);
            return false;
        }
        const bool pacifist = named(level, options->current_map, "e1m1");
        bool ok = true;
        for (size_t i = 0; ok && i < roster.count; ++i) {
            qa_actor_id actor = roster.ids[i];
            if (!live(level, actor))
                continue;
            const qa_q1_level_player *entry = &level->players[actor.slot];
            bool tracked = qa_actor_id_equal(entry->actor, actor);
            bool disqualified = tracked && (pacifist ? entry->fired_weapon : entry->took_damage);
            if (!disqualified)
                ok = options->achievement(options->context, actor,
                                          pacifist ? "ACH_PACIFIST" : "ACH_PAINLESS_MAZE", error);
        }
        qa_builtin_snapshot_free(&roster);
        if (!ok)
            return false;
    }
    static const struct {
        const char *map, *id;
    } completed[] = {{"e1m7", "ACH_COMPLETE_E1M7"},
                     {"e2m6", "ACH_COMPLETE_E2M6"},
                     {"e3m6", "ACH_COMPLETE_E3M6"},
                     {"e4m7", "ACH_COMPLETE_E4M7"}};
    if (options->official_campaign)
        for (size_t i = 0; i < sizeof(completed) / sizeof(*completed); ++i)
            if (named(level, options->current_map, completed[i].map) &&
                !options->achievement(options->context, (qa_actor_id){0}, completed[i].id, error))
                return false;
    static const struct {
        const char *from, *to, *id;
    } secret[] = {{"e1m4", "e1m8", "ACH_FIND_E1M8"},
                  {"e2m3", "e2m7", "ACH_FIND_E2M7"},
                  {"e3m4", "e3m7", "ACH_FIND_E3M7"},
                  {"e4m5", "e4m8", "ACH_FIND_E4M8"}};
    for (size_t i = 0; i < sizeof(secret) / sizeof(*secret); ++i)
        if (named(level, options->current_map, secret[i].from) &&
            named(level, next, secret[i].to) &&
            !options->achievement(options->context, (qa_actor_id){0}, secret[i].id, error))
            return false;
    return true;
}
bool qa_q1_level_cutscene(qa_q1_level *level, qa_string_id map, qa_actor_id cause,
                          double exit_after, qa_error *error) {
    if (!valid_map(level, map) || !isfinite(exit_after))
        return fail(error, "Invalid Q1 cutscene state");
    level->state = (qa_q1_level_state){map, cause, 1, exit_after, true};
    return true;
}
bool qa_q1_level_begin(qa_q1_level *level, qa_string_id map, qa_actor_id cause, double seconds,
                       qa_error *error) {
    if (!isfinite(seconds))
        return fail(error, "Invalid Q1 level time");
    if (!qa_q1_level_cutscene(level, map, cause, seconds + (level->options.deathmatch ? 5 : 2),
                              error))
        return false;
    for (size_t i = 0; i < level->options.rule_count; ++i) {
        const qa_q1_intermission_rule *rule = &level->rules[i];
        if (rule->begin && !rule->begin(rule->context, level, map, cause, seconds, error))
            return false;
    }
    return level->options.begin(level->options.context, map, cause, level->state.exit_after,
                                error) &&
           achievements(level, map, error);
}
bool qa_q1_level_begin_pending(qa_q1_level *level, double seconds, qa_error *error) {
    return qa_q1_level_begin(level, level->state.next_map, (qa_actor_id){0}, seconds, error);
}
bool qa_q1_level_defer_exit(qa_q1_level *level, double until, qa_error *error) {
    if (!isfinite(until))
        return fail(error, "Invalid Q1 intermission deadline");
    level->state.exit_after = until;
    return true;
}
bool qa_q1_level_check_limits(qa_q1_level *level, double seconds, const float *scores, size_t count,
                              float minutes, float frags, qa_string_id changelevel, bool *out,
                              qa_error *error) {
    if (!out || !isfinite(seconds) ||
        (count && !scores) || (changelevel && !valid_map(level, changelevel)))
        return fail(error, "Invalid Q1 match limit input");
    bool reached = minutes != 0 && seconds >= (double)minutes * 60;
    for (size_t i = 0; i < count; ++i) {
        if (frags != 0 && scores[i] >= frags)
            reached = true;
    }
    *out = false;
    if (level->state.next_map || (minutes == 0 && frags == 0) || !reached)
        return true;
    qa_string_id next = level->options.current_map;
    uint32_t flags = *level->options.server_flags;
    const char *episode = NULL;
    if (named(level, next, "start")) {
        if (!level->options.registered)
            episode = "e1m1";
        else if (!(flags & 1u)) {
            episode = "e1m1";
            flags |= 1u;
        } else if (!(flags & 2u)) {
            episode = "e2m1";
            flags |= 2u;
        } else if (!(flags & 4u)) {
            episode = "e3m1";
            flags |= 4u;
        } else if (!(flags & 8u)) {
            episode = "e4m1";
            flags -= 7u;
        }
        if (episode && !qa_strings_intern_cstr(strings(level), episode, &next, error))
            return false;
    } else if (changelevel)
        next = changelevel;
    *level->options.server_flags = flags;
    level->state.next_map = next;
    if (!level->options.defer_begin(level->options.context, 0.1, error))
        return false;
    *out = true;
    return true;
}
static bool travel(qa_q1_level *level, bool same_level, qa_q1_intermission_result *out,
                   qa_error *error) {
    qa_string_id map = same_level ? level->options.current_map : level->state.next_map;
    qa_actor_id cause = level->state.intermission ? level->state.cause : (qa_actor_id){0};
    level->state.intermission = false;
    if (!qa_q1_level_travel(level, map, cause, error))
        return false;
    level->state.stage = 0;
    *out = (qa_q1_intermission_result){.kind = QA_Q1_INTERMISSION_TRAVEL, .map = map};
    return true;
}
static bool finale(qa_q1_level *level, const char *key, qa_q1_intermission_result *out,
                   qa_error *error) {
    qa_string_id text;
    if (!qa_strings_intern_cstr(strings(level), qa_q1_finale_text(level->options.rerelease, key),
                                &text, error))
        return false;
    *out = (qa_q1_intermission_result){.kind = QA_Q1_INTERMISSION_FINALE, .text = text, .track = 2};
    return true;
}
bool qa_q1_level_request_exit(qa_q1_level *level, double seconds, bool pressed, bool same_level,
                              qa_q1_intermission_result *out, qa_error *error) {
    if (!out || !isfinite(seconds))
        return fail(error, "Invalid Q1 intermission input");
    *out = (qa_q1_intermission_result){.kind = QA_Q1_INTERMISSION_WAITING};
    if (!level->state.stage || !pressed || seconds < level->state.exit_after)
        return true;
    if (level->options.deathmatch)
        return travel(level, same_level, out, error);
    if (level->state.stage == UINT32_MAX || !isfinite(seconds + 1))
        return fail(error, "Q1 intermission stage overflow");
    level->state.exit_after = seconds + 1;
    ++level->state.stage;
    for (size_t i = 0; i < level->options.rule_count; ++i) {
        const qa_q1_intermission_rule *rule = &level->rules[i];
        qa_q1_finale_decision decision = QA_Q1_FINALE_DELEGATE;
        qa_q1_intermission_result result = {0};
        if (rule->finale &&
            !rule->finale(rule->context, level, level->state.stage, level->state.next_map, seconds,
                          &decision, &result, error))
            return false;
        switch (decision) {
        case QA_Q1_FINALE_DELEGATE:
            break;
        case QA_Q1_FINALE_TRAVEL:
            return travel(level, same_level, out, error);
        case QA_Q1_FINALE_PRESENT:
            if (result.kind != QA_Q1_INTERMISSION_SELL &&
                (result.kind != QA_Q1_INTERMISSION_FINALE ||
                 !qa_strings_cstr(strings(level), result.text)))
                return fail(error, "Invalid source finale result");
            *out = result;
            return true;
        default:
            return fail(error, "Invalid source finale decision");
        }
    }
    if (level->state.stage == 2) {
        qa_string_id map = level->options.current_map;
        const char *key =
            named(level, map, "e1m7")
                ? (level->options.registered ? "$qc_finale_e1" : "$qc_finale_e1_shareware")
            : named(level, map, "e2m6") ? "$qc_finale_e2"
            : named(level, map, "e3m6") ? "$qc_finale_e3"
            : named(level, map, "e4m7") ? "$qc_finale_e4"
                                        : NULL;
        if (key)
            return finale(level, key, out, error);
    } else if (level->state.stage == 3) {
        if (!level->options.registered) {
            out->kind = QA_Q1_INTERMISSION_SELL;
            return true;
        }
        if ((*level->options.server_flags & 15u) == 15u)
            return finale(level, "$qc_finale_all_runes", out, error);
    }
    return travel(level, same_level, out, error);
}
bool qa_q1_level_client_connected(qa_q1_level *level, double seconds, bool same_level,
                                  qa_q1_intermission_result *out, qa_error *error) {
    if (!out || !isfinite(seconds))
        return fail(error, "Invalid Q1 intermission connection time");
    if (level->state.stage)
        level->state.exit_after = seconds;
    return qa_q1_level_request_exit(level, seconds, true, same_level, out, error);
}
bool qa_q1_level_capture(const qa_q1_level *level, qa_q1_level_checkpoint *out, qa_error *error) {
    if (!out)
        return fail(error, "Missing Q1 level checkpoint output");
    qa_q1_level_checkpoint saved = {.state = level->state,
                                    .server_flags = *level->options.server_flags};
    for (size_t i = 0; i < level->capacity; ++i)
        if (live(level, level->players[i].actor))
            ++saved.player_count;
    if (saved.player_count) {
        saved.players = malloc(saved.player_count * sizeof(*saved.players));
        if (!saved.players) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 level checkpoint");
            return false;
        }
    }
    size_t at = 0;
    for (size_t i = 0; i < level->capacity; ++i)
        if (live(level, level->players[i].actor))
            saved.players[at++] = level->players[i];
    *out = saved;
    return true;
}
void qa_q1_level_checkpoint_free(qa_q1_level_checkpoint *saved) {
    if (!saved)
        return;
    free(saved->players);
    *saved = (qa_q1_level_checkpoint){0};
}
bool qa_q1_level_restore(qa_q1_level *level, const qa_q1_level_checkpoint *saved, qa_error *error) {
    if (!saved || !isfinite(saved->state.exit_after) || saved->player_count > level->capacity ||
        (saved->player_count && !saved->players) ||
        (saved->state.next_map && !valid_map(level, saved->state.next_map)) ||
        (saved->state.stage && !saved->state.next_map) ||
        !reference_valid(level, saved->state.cause, error))
        return fail(error, "Invalid Q1 level checkpoint");
    qa_q1_level_player *players = calloc(level->capacity, sizeof(*players));
    if (!players) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating restored Q1 level players");
        return false;
    }
    for (size_t i = 0; i < saved->player_count; ++i) {
        qa_q1_level_player entry = saved->players[i];
        if (entry.actor.slot >= level->capacity || !live(level, entry.actor) ||
            players[entry.actor.slot].actor.registry) {
            free(players);
            return fail(error, "Duplicate or stale Q1 level player");
        }
        players[entry.actor.slot] = entry;
    }
    free(level->players);
    level->players = players;
    level->state = saved->state;
    *level->options.server_flags = saved->server_flags;
    return true;
}

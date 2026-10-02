#include "native_q1_composition_birth.h"
#include "native_q1_composition.h"
#include "native_q1_console.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_composition.h"
#include "qa/game_q1_source_birth.h"
#include "qa/game_q1_source_entities.h"
#include "qa/modes_q1_source.h"
#include "qa/source_number.h"
#include "qa/game_q1_source_observer.h"
#include "native_q1_composition_flags.h"
#include "native_q1_composition_rogue.h"
#include "native_q1_respawn.h"
#include "map_players_private.h"
#include "unified_player.h"
#include "qa/application_equipment.h"
#include "qa/text.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct source_birth {
    qa_application *app;
    application_provider *source;
    qa_q1_game_operation operation;
    qa_q1_options options;
    qa_actor_id actor;
    qa_mode_id mode;
    qa_mode_source kind;
} source_birth;

static bool current(source_birth *call, qa_error *error) {
    bool observer;
    qa_mode_view view;
    if (!qa_q1_game_operation_live(&call->operation) ||
        call->source->state.q1 != call->operation.game ||
        !application_native_q1_composition_player_current(call->app, call->source->owner,
            call->kind, call->actor, &observer, error) ||
        !qa_modes_read(call->app->modes, call->mode, &view, error)) return false;
    return (view.origin == QA_MODE_NATIVE_Q1_COMPOSITION &&
        view.source_owner == call->source->owner && view.rules.source == call->kind) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 birth lost its actual source controller");
}

static bool begin(application_provider *source, qa_actor_id actor,
    source_birth *call, qa_error *error) {
    if (!source || !source->application || source->kind != APPLICATION_PROVIDER_Q1 ||
        !source->state.q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 birth requires its actual native GAME");
    *call = (source_birth){.app = source->application, .source = source, .actor = actor};
    double seconds;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &call->options, &seconds, error)) return false;
    if (call->options.program != QA_Q1_CTF && call->options.program != QA_Q1_ROGUE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 birth has no actual expansion composition");
    call->kind = call->options.program == QA_Q1_CTF ? QA_MODE_THREEWAVE : QA_MODE_ROGUE;
    return qa_q1_game_operation_begin(source->state.q1, &call->operation, error) &&
        application_native_q1_composition_mode(call->app, source, &call->mode, error) && current(call, error);
}

static bool source_cvar(source_birth *call, const char *name, double *out, qa_error *error) {
    if (!current(call, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(call->source);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    if (!value || value->owner != call->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 birth lost its actual GAME policy cvar");
    *out = qa_source_fround(value->number);
    return true;
}

static bool read_number(source_birth *call, qa_actor_id actor, qa_mode_q1_number field,
    double *out, qa_error *error) {
    return qa_modes_q1_source_read(call->app->modes, call->mode, actor, field, out, error) &&
        current(call, error);
}
static bool write_number(source_birth *call, qa_mode_q1_number field,
    double value, qa_error *error) {
    return qa_modes_q1_source_write(call->app->modes, call->mode, call->actor, field, value, error) &&
        current(call, error);
}
static uint32_t source_bits(double value) {
    double integer = isfinite(value) ? fmod(trunc(value), 4294967296.0) : 0;
    if (integer < 0) integer += 4294967296.0;
    return (uint32_t)integer;
}

static bool status(source_birth *call, qa_error *error) {
    qa_builtin_ctf_status status = {0};
    if (!qa_q1_source_captures_read(call->operation.game, &status.red, &status.blue, error) ||
        !current(call, error)) return false;
    uint32_t flags = 0;
    static const char *const names[] = {"item_flag_team1", "item_flag_team2"};
    for (unsigned i = 0; i < 2; ++i) {
        qa_q1_source_entity flag;
        bool found;
        if (!qa_q1_source_entity_first(call->operation.game, names[i], &flag, &found, error) ||
            !current(call, error)) return false;
        flags |= (found ? UINT32_C(1) << (source_bits(flag.count) & 31u) : UINT32_C(1)) << (i * 3);
    }
    int32_t signed_flags;
    memcpy(&signed_flags, &flags, sizeof(signed_flags));
    status.flags = signed_flags;
    static const char *const runes[] = {"resistance", "strength", "haste", "regeneration"};
    for (unsigned i = 0; i < 4; ++i) {
        char name[40];
        size_t length = strlen(runes[i]);
        memcpy(name, "q1:ctf/rune/", 12);
        memcpy(name + 12, runes[i], length + 1);
        qa_item_id item;
        double count;
        if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), name, &item, error) ||
            !qa_inventory_count_read(call->app->inventory, call->actor, item, &count, error) ||
            !current(call, error)) return false;
        if (count > 0) { status.rune_items = UINT32_C(32) << i; break; }
    }
    uint64_t time_ns;
    double seconds;
    if (!qa_q1_game_clock_read(call->operation.game, &time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF status lost its actual source clock");
    return application_emit(call->app, &(qa_builtin_event){.kind = QA_BUILTIN_CTF_STATUS,
        .family = QA_GAME_Q1, .provider = call->source->owner, .actor = call->actor,
        .time_ns = time_ns, .ctf_status = status}, error) && current(call, error);
}

static bool observer(source_birth *call, qa_error *error) {
    if (!qa_q1_source_client_observer(call->operation.game, call->actor, true, error) ||
        !current(call, error) || !qa_combat_set_health(call->app->combat, call->actor, 999, error) ||
        !current(call, error)) return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    qa_body_state body;
    if (!qa_world_body_read(call->app->world, call->actor, &body, error)) return false;
    body.bounds = (qa_bounds){{-12, -12, -12}, {12, 12, 12}};
    return qa_world_body_write(call->app->world, call->actor, &body, error) &&
        qa_world_link(call->app->world, call->actor, NULL, error) && current(call, error);
}

static bool check_team(source_birth *call, qa_error *error) {
    qa_mode_view view;
    qa_combat_state combat;
    double last;
    if (!qa_modes_read(call->app->modes, call->mode, &view, error) ||
        !read_number(call, call->actor, QA_Q1_CTF_LAST_TEAM, &last, error) ||
        !qa_combat_read_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    bool red_team = combat.team == view.rules.teams[0], blue_team = combat.team == view.rules.teams[1];
    if (last >= 0 && (red_team || blue_team))
        return write_number(call, QA_Q1_CTF_LAST_TEAM, red_team ? 5 : 14, error);
    size_t red = 0, blue = 0;
    for (uint32_t slot = 0; slot < call->options.max_clients; ++slot) {
        qa_actor_id actor;
        if (!qa_q1_source_client_actor(call->operation.game, slot, &actor) ||
            qa_actor_id_equal(actor, call->actor)) continue;
        bool source_observer;
        if (!application_native_q1_composition_player_current(call->app, call->source->owner,
            call->kind, actor, &source_observer, error) ||
            !qa_combat_read_traits(call->app->combat, actor, &combat, error) ||
            !current(call, error)) return false;
        if (combat.team == view.rules.teams[0]) ++red;
        else if (combat.team == view.rules.teams[1]) ++blue;
    }
    bool use_blue = blue < red;
    if (blue == red) {
        double random;
        if (!qa_q1_source_random(call->operation.game, &random, error) || !current(call, error)) return false;
        use_blue = random < .5;
    }
    if (!qa_combat_read_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    combat.team = view.rules.teams[use_blue ? 1 : 0];
    return qa_combat_set_traits(call->app->combat, call->actor, &combat, error) &&
        current(call, error) && write_number(call, QA_Q1_CTF_LAST_TEAM, use_blue ? 14 : 5, error) &&
        qa_q1_source_client_colors(call->operation.game, call->actor, use_blue ? 13 : 4,
            use_blue ? 13 : 4, error) && current(call, error);
}

static bool ctf_birth(source_birth *call, bool first, qa_error *error) {
    qa_mode_view view;
    double teamplay;
    if (!qa_modes_read(call->app->modes, call->mode, &view, error) ||
        !source_cvar(call, "teamplay", &teamplay, error) ||
        !qa_q1_source_ctf_spawn_arsenal(call->operation.game, call->actor, false,
            (source_bits(teamplay) & 2048) != 0, error) || !current(call, error) ||
        !write_number(call, QA_Q1_CTF_LAST_HURT_CARRIER, -10, error) ||
        !write_number(call, QA_Q1_CTF_REGEN_TIME, 0, error) ||
        !write_number(call, QA_Q1_CTF_RUNE_NOTICE, 0, error)) return false;
    if (first) {
        if (!write_number(call, QA_Q1_CTF_KILLED, 0, error) ||
            !write_number(call, QA_Q1_CTF_MOTD, 0, error)) return false;
        if (!source_cvar(call, "teamplay", &teamplay, error)) return false;
        if ((source_bits(teamplay) & 1024) && !view.rules.start_map) {
            if (!observer(call, error)) return false;
        } else if (!check_team(call, error)) return false;
    } else {
        qa_q1_source_client_view client;
        if (!qa_q1_source_client_read(call->operation.game, call->actor, &client))
            return application_fail(error, QA_ERROR_ARGUMENT, "CTF birth lost its actual source client");
        if (client.observer && !observer(call, error)) return false;
    }
    return write_number(call, QA_Q1_CTF_STUFF_COLOR, 1, error) && status(call, error);
}

static bool rogue_legal(double mode, double team) {
    return mode < 4 ? team > 0 : mode == 4 || mode == 5 || mode == 6 ?
        team == 5 || team == 14 || (mode == 6 && team == 1) : true;
}

static bool rogue_birth(source_birth *call, qa_error *error) {
    double team, mode;
    qa_q1_source_client_view client;
    if (!qa_modes_q1_rogue_initialize(call->app->modes, call->mode, call->actor, error) ||
        !current(call, error) || !read_number(call, call->actor, QA_Q1_ROGUE_STEAM, &team, error))
        return false;
    if (!qa_q1_source_client_read(call->operation.game, call->actor, &client))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue birth lost its actual source color");
    if (!source_cvar(call, "teamplay", &mode, error)) return false;
    if ((team >= 0 || mode < 4) && rogue_legal(mode, client.team))
        return write_number(call, QA_Q1_ROGUE_STEAM, client.team, error);
    size_t red = 0, blue = 0, grey = 0;
    for (uint32_t slot = 0; slot < call->options.max_clients; ++slot) {
        qa_actor_id actor;
        if (!qa_q1_source_client_actor(call->operation.game, slot, &actor) ||
            qa_actor_id_equal(actor, call->actor)) continue;
        if (!qa_modes_q1_rogue_initialize(call->app->modes, call->mode, actor, error) ||
            !read_number(call, actor, QA_Q1_ROGUE_STEAM, &team, error)) return false;
        if (team == 5) ++red;
        else if (team == 14) ++blue;
        else if (team == 1) ++grey;
    }
    team = 5;
    size_t count = red;
    bool use_blue = blue < count;
    if (blue == count) {
        double random;
        if (!qa_q1_source_random(call->operation.game, &random, error) || !current(call, error)) return false;
        use_blue = random < .5;
    }
    if (use_blue) { team = 14; count = blue; }
    if (!source_cvar(call, "teamplay", &mode, error)) return false;
    if (mode == 6 && grey * 2 < count) team = 1;
    double old_flags;
    if (!write_number(call, QA_Q1_ROGUE_STEAM, team, error) ||
        !read_number(call, call->actor, QA_Q1_ROGUE_FLAGS, &old_flags, error)) return false;
    uint32_t bits = source_bits(old_flags) | UINT32_C(4);
    int32_t flags;
    memcpy(&flags, &bits, sizeof(flags));
    if (!write_number(call, QA_Q1_ROGUE_FLAGS, flags, error)) return false;
    const char *message = team == 5 ? "You have been assigned to the Red team.\n" :
        team == 14 ? "You have been assigned to the Blue team.\n" :
        "You have been assigned to the Grey team.\n";
    qa_string_id text;
    uint64_t time_ns;
    double seconds;
    if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), message, &text, error))
        return false;
    if (!qa_q1_game_clock_read(call->operation.game, &time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue birth lost its source clock");
    return application_emit(call->app, &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
        .family = QA_GAME_Q1, .provider = call->source->owner, .actor = call->actor,
        .time_ns = time_ns, .text = text}, error) && current(call, error) &&
        qa_q1_source_client_colors(call->operation.game, call->actor, (int32_t)team - 1,
            (int32_t)team - 1, error) && current(call, error);
}

bool application_native_q1_ctf_status(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    source_birth call = {0};
    bool okay = begin(source, actor, &call, error);
    if (okay && call.kind != QA_MODE_THREEWAVE)
        okay = application_fail(error, QA_ERROR_ARGUMENT, "CTF status requires the actual ThreeWave source");
    if (okay) okay = status(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_composition_birth(application_provider *source, qa_actor_id actor,
    bool first_admission, qa_error *error) {
    if (!source || source->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 composition birth requires its actual GAME");
    qa_q1_options options;
    double seconds;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error)) return false;
    if (options.quakeworld && options.program == QA_Q1_ID1 && options.edition == QA_Q1_CLASSIC)
        return qa_q1_source_qw_birth(source->state.q1, actor, error);
    if (options.program != QA_Q1_CTF && options.program != QA_Q1_ROGUE) return true;
    source_birth call = {0};
    bool okay = begin(source, actor, &call, error);
    if (okay && call.kind == QA_MODE_THREEWAVE) okay = ctf_birth(&call, first_admission, error);
    if (okay && call.kind == QA_MODE_ROGUE) okay = rogue_birth(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool frame_emit(source_birth *call, qa_builtin_event *event, qa_error *error) {
    double elapsed;
    if (!current(call, error) ||
        !qa_q1_game_clock_read(call->operation.game, &event->time_ns, &elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF frame lost its source clock");
    event->family = QA_GAME_Q1;
    event->provider = call->source->owner;
    return application_emit(call->app, event, error) && current(call, error);
}
static bool frame_message(source_birth *call, const char *text, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .actor = call->actor};
    return qa_strings_intern_cstr(qa_session_strings(call->app->session), text, &event.text, error) &&
        frame_emit(call, &event, error);
}
static bool prompt_supported(source_birth *call, bool *out, qa_error *error) {
    *out = false;
    return current(call, error) &&
        (!call->app->prompt_supported ||
         call->app->prompt_supported(call->app->prompt_context, call->actor, out, error)) &&
        current(call, error);
}
static bool team_prompt(source_birth *call, qa_error *error) {
    bool supported;
    if (!prompt_supported(call, &supported, error)) return false;
    if (!supported) return frame_message(call,
        "Welcome!\nRunning ThreeWave CTF 5.0\n\nCapture the Flag!\n\nPress 1 for RED team\nPress 2 for BLUE team\nOr press JUMP for automatic team\n", error);
    static const char *const labels[] = {"$qc_ctf_intro_auto", "$qc_ctf_intro_red",
        "$qc_ctf_intro_blue", "$qc_ctf_intro_observer"};
    static const int32_t impulses[] = {103, 101, 102, 104};
    qa_builtin_prompt_choice choices[4];
    qa_builtin_event event = {.kind = QA_BUILTIN_SOURCE_PROMPT, .actor = call->actor,
        .prompt_choices = choices, .prompt_choice_count = 4};
    if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), "$qc_ctf_intro",
        &event.text, error)) return false;
    for (size_t i = 0; i < 4; ++i) {
        choices[i].impulse = impulses[i];
        if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), labels[i],
            &choices[i].label, error)) return false;
    }
    return frame_emit(call, &event, error);
}
static bool frame_team(source_birth *call, double *out, qa_error *error) {
    qa_mode_view view;
    qa_combat_state combat;
    if (!qa_modes_read(call->app->modes, call->mode, &view, error) ||
        !qa_combat_read_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    *out = combat.team == view.rules.teams[0] ? 5 : combat.team == view.rules.teams[1] ? 14 : 0;
    return true;
}
static bool set_team(source_birth *call, double team, qa_error *error) {
    qa_mode_view view;
    qa_combat_state combat;
    if (!qa_modes_read(call->app->modes, call->mode, &view, error) ||
        !qa_combat_read_traits(call->app->combat, call->actor, &combat, error) ||
        !current(call, error)) return false;
    combat.team = team == 5 ? view.rules.teams[0] : team == 14 ? view.rules.teams[1] : 0;
    return qa_combat_set_traits(call->app->combat, call->actor, &combat, error) &&
        current(call, error) && write_number(call, QA_Q1_CTF_LAST_TEAM, team, error) &&
        qa_q1_source_client_colors(call->operation.game, call->actor,
            team == 5 ? 4 : team == 14 ? 13 : 0, team == 5 ? 4 : team == 14 ? 13 : 0, error) &&
        current(call, error);
}
static bool team_damage(source_birth *call, qa_error *error) {
    return qa_q1_source_damage(call->operation.game, call->actor, call->actor,
        call->actor, 1000, "ctf:teamchange", error) && current(call, error);
}
static bool frame_colors(source_birth *call, double color, qa_error *error) {
    uint32_t word = source_bits(color);
    int32_t value;
    memcpy(&value, &word, sizeof(value));
    return qa_q1_source_client_colors(call->operation.game, call->actor, value, value, error) &&
        current(call, error);
}
static bool team_lock(source_birth *call, qa_error *error) {
    double policy, last, value, team;
    qa_q1_source_client_view client;
    qa_mode_view view;
    if (!source_cvar(call, "teamplay", &policy, error)) return false;
    if (policy < 0) return true;
    if (!qa_q1_source_client_read(call->operation.game, call->actor, &client) ||
        !qa_modes_read(call->app->modes, call->mode, &view, error) ||
        !read_number(call, call->actor, QA_Q1_CTF_LAST_TEAM, &last, error)) return false;
    if (client.observer || view.rules.start_map) {
        if (last != 1 && !frame_colors(call, 0, error)) return false;
        return write_number(call, QA_Q1_CTF_LAST_TEAM, 1, error);
    }
    if (!read_number(call, call->actor, QA_Q1_CTF_STUFF_COLOR, &value, error)) return false;
    if (value != 0) return write_number(call, QA_Q1_CTF_STUFF_COLOR, 0, error) &&
        frame_colors(call, last - 1, error);
    if (!frame_team(call, &team, error)) return false;
    double previous = last == 5 || last == 14 ? last : 0;
    if (team == 0 && last == 0) {
        if (!write_number(call, QA_Q1_CTF_LAST_TEAM, -1, error)) return false;
        last = -1;
    }
    if (team == last) return true;
    if ((source_bits(policy) & 64u) && last >= 0) {
        if (previous != 0) {
            if (!read_number(call, call->actor, QA_Q1_CTF_SUICIDE_COUNT, &value, error)) return false;
            if (value > 3 && (!qa_session_release(call->app->session, call->actor, error) ||
                !current(call, error))) return false;
            if (!read_number(call, call->actor, QA_Q1_CTF_KILLED, &value, error) ||
                !write_number(call, QA_Q1_CTF_KILLED, value == 1 ? 1 : 2, error)) return false;
            qa_combat_state combat;
            if (!qa_combat_read_traits(call->app->combat, call->actor, &combat, error)) return false;
            combat.invulnerable = false;
            if (!qa_combat_set_traits(call->app->combat, call->actor, &combat, error) ||
                !current(call, error) || !team_damage(call, error) ||
                !read_number(call, call->actor, QA_Q1_CTF_SUICIDE_COUNT, &value, error) ||
                !write_number(call, QA_Q1_CTF_SUICIDE_COUNT, value + 1, error)) return false;
            return set_team(call, previous, error);
        }
        if (!write_number(call, QA_Q1_CTF_LAST_TEAM, -50, error)) return false;
    }
    if (!read_number(call, call->actor, QA_Q1_CTF_LAST_TEAM, &last, error)) return false;
    if (last > 0) {
        if (!read_number(call, call->actor, QA_Q1_CTF_KILLED, &value, error) ||
            !write_number(call, QA_Q1_CTF_KILLED, value == 1 ? 1 : 2, error) ||
            !team_damage(call, error)) return false;
    }
    if (!qa_q1_source_client_read(call->operation.game, call->actor, &client))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF team reset lost its source score");
    return qa_q1_source_client_add_score(call->operation.game, call->actor, -(double)client.frags, error) &&
        current(call, error) && check_team(call, error);
}

static bool observer_impulse(source_birth *call, const qa_q1_input *input,
    bool force_auto, bool *handled, qa_error *error) {
    qa_q1_source_client_view client;
    bool supported;
    *handled = false;
    if (!qa_q1_source_client_read(call->operation.game, call->actor, &client) ||
        !prompt_supported(call, &supported, error)) return false;
    int32_t impulse = force_auto ? 103 : input->impulse;
    if (!(impulse >= 100 && impulse <= 104) &&
        !(!supported && client.observer && ((impulse >= 1 && impulse <= 3) || input->jump))) return true;
    double policy;
    if (!source_cvar(call, "teamplay", &policy, error)) return false;
    if (impulse == 100 && (source_bits(policy) & 64u)) {
        if (!frame_message(call, "$qc_ctf_teams_locked", error) ||
            !qa_q1_source_client_consume_impulse(call->operation.game, call->actor, error) ||
            !current(call, error)) return false;
        *handled = true;
        return true;
    }
    if (!client.observer && !team_damage(call, error)) return false;
    if (!qa_q1_source_client_observer(call->operation.game, call->actor, false, error) ||
        !current(call, error) || !write_number(call, QA_Q1_CTF_KILLED, 0, error)) return false;
    if (impulse == 100 || impulse == 104) {
        if (!set_team(call, 0, error) ||
            !qa_q1_source_client_observer(call->operation.game, call->actor, true, error) ||
            !current(call, error)) return false;
    } else if (impulse == 1 || impulse == 101) {
        if (!set_team(call, 5, error)) return false;
    } else if (impulse == 2 || impulse == 102) {
        if (!set_team(call, 14, error)) return false;
    } else if (impulse == 103 &&
        (!write_number(call, QA_Q1_CTF_LAST_TEAM, -50, error) || !check_team(call, error))) return false;
    qa_builtin_event clear = {.kind = QA_BUILTIN_CLEAR_PROMPT, .actor = call->actor};
    if (!frame_emit(call, &clear, error)) return false;
    double last;
    if (!read_number(call, call->actor, QA_Q1_CTF_LAST_TEAM, &last, error)) return false;
    if ((last == 5 || last == 14) &&
        (!application_native_q1_ctf_announce(call->source, call->actor,
            last == 5 ? "$qc_ks_joined_red" : "$qc_ks_joined_blue", error) ||
         !current(call, error))) return false;
    if (!qa_q1_source_client_consume_impulse(call->operation.game, call->actor, error) ||
        !current(call, error) || !write_number(call, QA_Q1_CTF_STUFF_COLOR, 1, error) ||
        !application_native_q1_respawn_new(call->source, call->actor, error) ||
        !current(call, error) || (impulse == 100 && !team_prompt(call, error))) return false;
    *handled = true;
    return true;
}
static bool write_body(source_birth *call, const qa_body_state *body, qa_error *error) {
    return qa_world_body_write(call->app->world, call->actor, body, error) &&
        qa_world_link(call->app->world, call->actor, NULL, error) && current(call, error);
}
static bool observer_teleport(source_birth *call, qa_body_state *body, qa_vec3 angles,
    double until, qa_error *error) {
    uint64_t time_ns;
    double elapsed;
    if (!qa_q1_game_clock_read(call->operation.game, &time_ns, &elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT, "Observer teleport lost its source clock");
    double duration = fmax(0, (until - (double)time_ns / 1000000000.0) * 1000000000.0);
    if (!isfinite(duration) || duration >= 18446744073709551616.0)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Observer hold exceeds native clock representation");
    body->angles = angles;
    if (!write_body(call, body, error)) return false;
    qa_builtin_services services = application_builtin_services(call->app, call->app->world, call->app->physics);
    qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_TELEPORT, .body = *body,
        .view_angles = angles, .hold_ns = (uint64_t)duration, .force_view_angles = true,
        .preserve_command_angles = true};
    return services.motion_changed(services.context, call->actor, &change, error) && current(call, error);
}
static bool observer_frame(source_birth *call, const qa_q1_input *input, qa_error *error) {
    qa_body_state projected, passage;
    bool written, teleported;
    qa_vec3 angles;
    double until;
    if (!qa_q1_source_observer_body(call->operation.game, call->actor, input, &projected,
        &passage, &written, &teleported, &angles, &until, error) || !current(call, error) ||
        !write_body(call, &projected, error)) return false;
    if (written && !(teleported ? observer_teleport(call, &passage, angles, until, error) :
        write_body(call, &passage, error))) return false;
    double held;
    if (!read_number(call, call->actor, QA_Q1_CTF_OBSERVER_JUMP_HELD, &held, error)) return false;
    if (input->jump && held == 0) {
        qa_body_state spot, actual;
        bool found;
        if (!application_players_native_q1_spawn_pose(call->app, call->source, call->actor,
            &spot, &found, error) || !current(call, error)) return false;
        if (found) {
            if (!qa_world_body_read(call->app->world, call->actor, &actual, error)) return false;
            actual.origin = spot.origin;
            if (!observer_teleport(call, &actual, spot.angles, input->teleport_until, error)) return false;
        }
    }
    return write_number(call, QA_Q1_CTF_OBSERVER_JUMP_HELD, input->jump ? 1 : 0, error);
}

bool application_native_q1_ctf_prethink(application_provider *source, qa_actor_id actor,
    const qa_q1_input *input, qa_error *error) {
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !input)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF frame requires its actual native source input");
    qa_q1_options options;
    double elapsed;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &options, &elapsed, error)) return false;
    if (options.program != QA_Q1_CTF) return true;
    source_birth call = {0};
    bool okay = begin(source, actor, &call, error);
    qa_q1_source_client_view retained;
    qa_q1_input source_input = *input;
    if (okay) {
        okay = qa_q1_source_client_read(call.operation.game, actor, &retained);
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "CTF frame lost its retained source command");
        else source_input.impulse = retained.impulse >= 0 && retained.impulse <= UINT8_MAX ?
            (uint8_t)retained.impulse : 0;
    }
    const qa_q1_level_state *level = source->q1_level ? qa_q1_level_read(source->q1_level) : NULL;
    if (okay && !level) okay = application_fail(error, QA_ERROR_ARGUMENT, "CTF frame lost its actual source level");
    if (okay && level->intermission) { qa_q1_game_operation_end(&call.operation); return true; }
    double motd;
    qa_q1_source_client_view client;
    qa_mode_view view;
    if (okay) okay = read_number(&call, actor, QA_Q1_CTF_MOTD, &motd, error);
    if (okay && motd == 2) {
        okay = status(&call, error) && qa_q1_source_client_read(call.operation.game, actor, &client) &&
            qa_modes_read(call.app->modes, call.mode, &view, error);
        if (okay && client.observer) okay = team_prompt(&call, error);
        else if (okay) {
            double team;
            okay = frame_team(&call, &team, error) && frame_message(&call,
                view.rules.start_map ? "$qc_choose_exit" : team == 5 ? "$qc_ctf_red" : "$qc_ctf_blue", error);
        }
    }
    if (okay) okay = read_number(&call, actor, QA_Q1_CTF_MOTD, &motd, error);
    if (okay && motd <= 2) okay = write_number(&call, QA_Q1_CTF_MOTD, motd + 1, error);
    bool bot = false;
    if (okay) {
        for (size_t i = 0; i < call.app->players->count; ++i)
            if (qa_actor_id_equal(call.app->players->records[i].actor, actor)) {
                bot = call.app->players->records[i].bot; break;
            }
    }
    double team = 0;
    bool handled = false;
    if (okay) okay = frame_team(&call, &team, error) &&
        observer_impulse(&call, &source_input, bot && team == 0, &handled, error);
    if (okay && !handled) okay = team_lock(&call, error) &&
        qa_q1_source_client_read(call.operation.game, actor, &client);
    if (okay && !handled && client.observer) okay = observer_frame(&call, &source_input, error);
    else if (okay && !handled) {
        qa_combat_state combat;
        okay = qa_combat_read(call.app->combat, actor, &combat, error) && current(&call, error);
        if (okay && combat.health > 0) okay = application_native_q1_ctf_regenerate(source, actor, error) && current(&call, error);
    }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_weapon_changed(void *context, qa_actor_id actor,
    qa_item_id acquired, qa_error *error)
{
    application_provider *source = context;
    qa_application *app = source ? source->application : NULL;
    (void)acquired;
    if (!app || source->kind != APPLICATION_PROVIDER_Q1 || !source->constructed ||
        !source->attached || source->close_pending ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon change lost its physical Source");
    qa_q1_game_operation operation;
    if (!qa_q1_game_operation_begin(source->state.q1, &operation, error)) return false;
    application_unified_player_selection selected;
    bool okay = application_unified_player_selection_read(app, actor, &selected, error) &&
        application_unified_player_selection_current(app, &selected) &&
        qa_q1_game_operation_live(&operation) && source->state.q1 == operation.game &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == source;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon change lost its selected arsenal");
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool application_native_q1_ctf_impulse(application_provider *source, qa_actor_id actor,
    const qa_q1_input *input, bool *handled, qa_error *error) {
    if (!handled || !input || !source || source->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF impulse requires its genuine source command");
    *handled = false;
    qa_q1_options options;
    double seconds;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error)) return false;
    if (options.program != QA_Q1_CTF) return true;
    source_birth call = {0};
    bool okay = begin(source, actor, &call, error);
    qa_q1_source_client_view client;
    qa_q1_input command = *input;
    if (okay) {
        okay = qa_q1_source_client_read(call.operation.game, actor, &client);
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "CTF impulse lost its retained source client");
        else command.impulse = client.impulse >= 0 && client.impulse <= UINT8_MAX ?
            (uint8_t)client.impulse : 0;
    }
    bool consumed = false;
    if (okay && command.impulse) okay = observer_impulse(&call, &command, false, &consumed, error);
    if (okay && command.impulse && !consumed) {
        double policy;
        okay = source_cvar(&call, "teamplay", &policy, error);
        application_unified_player_selection selected = {0};
        if (okay && (command.impulse == 1 || command.impulse == 20 || command.impulse == 21))
            okay = application_unified_player_selection_read(call.app, actor, &selected, error) &&
                application_unified_player_selection_current(call.app, &selected) && current(&call, error);
        qa_item_id source_grapple = qa_strings_find(qa_session_strings(call.app->session),
            (qa_bytes){(const uint8_t *)"q1:ctf/weapon/grapple", 21});
        if (okay && (command.impulse == 22 || (command.impulse == 1 &&
            (!source_grapple || selected.item != source_grapple)))) {
            /* This Source's constructor receives the shared grapple owner, so
             * its source-native slot is disabled independently of selected gear. */
            okay = frame_message(&call, "$qc_no_weapon", error);
            consumed = true;
        } else if (okay && !client.observer && (source_bits(policy) & 128u) &&
                   (command.impulse == 20 || command.impulse == 21)) {
            qa_q1_drop_input drop = {.selected_weapon = selected.item,
                .selected_ammo = selected.ammo, .view_angles = command.view_angles};
            qa_actor_id dropped;
            okay = command.impulse == 20 ? qa_q1_ctf_toss_ammo(call.operation.game,
                actor, &drop, &dropped, error) : qa_q1_ctf_toss_weapon(call.operation.game,
                actor, &drop, &dropped, error);
            if (okay) okay = current(&call, error);
            consumed = true;
        } else if (okay && command.impulse == 25) {
            static const uint32_t bits[] = {1, 2, 4, 8, 16, 64, 128};
            static const char *const names[] = {"Health-Protect", "Armor-Protect", "Mirror-Damage",
                "Frag-Penalty", "Death-Penalty", "Static-Teams",
                "Drop-Items (Backpack Impulse 20, Weapon Impulse 21)"};
            char message[256] = {0};
            if (policy < 0) {
                char number[32];
                okay = qa_format_ecmascript_number(-policy, number, error);
                if (okay) snprintf(message, sizeof(message), "Frag Penalty: %s", number);
            } else {
                size_t used = 0;
                for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); ++i)
                    if (source_bits(policy) & bits[i]) {
                        size_t length = strlen(names[i]);
                        if (used) message[used++] = ' ';
                        memcpy(message + used, names[i], length);
                        used += length;
                        message[used] = 0;
                    }
            }
            if (okay) {
                qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .actor = actor, .flags = 2u};
                okay = qa_strings_intern_cstr(qa_session_strings(call.app->session), message,
                    &event.text, error) && frame_emit(&call, &event, error);
            }
            consumed = true;
        }
        if (okay && consumed) okay = qa_q1_source_client_consume_impulse(call.operation.game,
            actor, error) && current(&call, error);
    }
    if (okay) *handled = consumed;
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_source_impulse(qa_application *app, qa_actor_id actor,
    const qa_q1_input *input, bool *consumed, qa_error *error) {
    if (!app || !input || !consumed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 Source impulse requires its actual command phase");
    *consumed = false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 ||
        !qa_q1_player_source_present(source->state.q1, actor)) return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(source->state.q1, &operation, error)) return false;
    qa_q1_game_operation selected_operation = {0};
    qa_q1_options source_options;
    double source_seconds;
    qa_q1_source_client_view client;
    bool okay = qa_q1_source_client_read(operation.game, actor, &client) &&
        qa_q1_source_respawn_options_read(operation.game, &source_options, &source_seconds, error);
    if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 Source weapon phase lost its retained client");
    bool handled = false, ready = true;
    qa_q1_input command = *input;
    bool pending = okay && client.impulse != 0;
    if (okay) command.impulse = client.impulse >= 0 && client.impulse <= UINT8_MAX ?
        (uint8_t)client.impulse : 0;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (okay && pending && arsenal && arsenal->kind == APPLICATION_PROVIDER_Q1) {
        if (arsenal != source) okay = qa_q1_game_operation_begin(arsenal->state.q1,
            &selected_operation, error);
        qa_q1_player_view view;
        uint64_t time;
        double elapsed;
        okay = okay && qa_q1_player_read(arsenal->state.q1, actor, &view) &&
            qa_q1_game_clock_read(arsenal->state.q1, &time, &elapsed);
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 Source impulse lost its selected weapon clock");
        else ready = (double)time / 1000000000.0 >= view.attack_finished;
        if (okay && ready && command.impulse && arsenal != source)
            okay = qa_q1_player_selected_impulse(arsenal->state.q1, actor, command.impulse, &handled, error);
    }
    if (okay && command.impulse && ready && !handled)
        okay = application_native_q1_ctf_impulse(source, actor, &command, &handled, error);
    if (okay && command.impulse && ready && !handled && source_options.program == QA_Q1_ROGUE)
        okay = application_native_q1_rogue_impulse(source, actor, command.impulse, &handled, error);
    if (okay && command.impulse && ready && !handled && arsenal == source) {
        okay = application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == arsenal;
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 impulse changed its actual selected arsenal");
        else okay = qa_q1_player_selected_impulse(arsenal->state.q1, actor, command.impulse, &handled, error);
    }
    if (okay && command.impulse && ready && !handled)
        okay = qa_q1_player_source_impulse(operation.game, actor, command.impulse, &handled, error);
    if (okay && pending && ready) {
        okay = qa_q1_game_operation_live(&operation) && source->constructed &&
            !source->close_pending && source->state.q1 == operation.game &&
            application_world_provider(app, QA_ROLE_ENTITIES, "") == source &&
            qa_q1_source_client_consume_impulse(operation.game, actor, error);
        if (!okay && (!error || error->code == QA_OK))
            application_fail(error, QA_ERROR_ARGUMENT, "Q1 Source impulse retired its actual owner");
        if (okay) *consumed = true;
    }
    qa_q1_game_operation_end(&selected_operation);
    qa_q1_game_operation_end(&operation);
    return okay;
}

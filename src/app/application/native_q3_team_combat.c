#include "native_q3_team_combat.h"
#include "native_q3_console.h"
#include "native_q3_rank.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct team_combat_scope {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_modes *modes;
    qa_mode_id mode;
    qa_world *world;
    qa_collision_geometry *geometry;
    uint64_t publication_generation, command_generation, map_revision;
    uint32_t maximum;
    int32_t time, game_type;
    bool missionpack;
} team_combat_scope;

static bool live(const team_combat_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    qa_application *app = scope->application;
    if (provider->application != app || provider->kind != APPLICATION_PROVIDER_Q3 ||
        provider->state.q3 != scope->game || !provider->constructed ||
        !provider->attached || provider->close_pending || app->destroy_requested ||
        app->modes != scope->modes || app->world != scope->world ||
        !app->primary_mode_ready || app->primary_mode.slot != scope->mode.slot ||
        app->primary_mode.generation != scope->mode.generation ||
        app->publication_generation != scope->publication_generation ||
        app->command_generation != scope->command_generation ||
        app->map_revision != scope->map_revision ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native TEAM combat lost its actual source owner");
    int32_t time;
    return qa_q3_source_clock(scope->game, &time, error) &&
        (time == scope->time || application_fail(error, QA_ERROR_ARGUMENT,
            "native TEAM combat callback advanced its source clock"));
}

static bool begin(application_provider *provider, team_combat_scope *scope,
    qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !app->modes || !app->primary_mode_ready || !app->world ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native TEAM combat requires its admitted GAME and score owner");
    *scope = (team_combat_scope){.provider = provider, .application = app,
        .game = provider->state.q3, .modes = app->modes, .mode = app->primary_mode,
        .world = app->world, .geometry = qa_world_geometry(app->world),
        .publication_generation = app->publication_generation,
        .command_generation = app->command_generation, .map_revision = app->map_revision};
    qa_q3_product product;
    int32_t start;
    if (!scope->geometry || !qa_q3_source_clock(scope->game, &scope->time, error) ||
        !live(scope, error) ||
        !qa_q3_source_max_clients(scope->game, &scope->maximum, error) ||
        !qa_q3_source_match_context_read(scope->game, &product, &start, error) ||
        !application_native_q3_settings_integer(provider, "g_gametype", &scope->game_type, error))
        return false;
    scope->missionpack = product == QA_Q3_TEAM_ARENA;
    return application_native_q3_console_borrow(provider, error);
}

static bool finish(team_combat_scope *scope, bool okay, qa_error *error)
{
    if (okay) okay = live(scope, error);
    application_native_q3_console_release(scope->provider);
    return okay;
}

static bool client(const team_combat_scope *scope, qa_actor_id actor,
    uint32_t *slot, qa_q3_native_client *out, qa_q3_player_state *player,
    bool *present, qa_error *error)
{
    *present = false;
    if (!live(scope, error)) return false;
    if (!actor.registry) return true;
    if (!qa_q3_native_client_slot(scope->game, actor, slot, NULL)) return true;
    if (!qa_actors_get(qa_session_actors(scope->application->session), actor) ||
        !qa_q3_client_slot_read(scope->game, *slot, out, error) ||
        !qa_q3_player_read(scope->game, actor, player))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native TEAM combat lost its physical client generation");
    *present = true;
    return true;
}

static bool client_live(const team_combat_scope *scope, qa_actor_id actor,
    uint32_t expected, qa_error *error)
{
    qa_q3_native_client value;
    qa_q3_player_state player;
    uint32_t slot;
    bool present;
    return client(scope, actor, &slot, &value, &player, &present, error) &&
        ((present && slot == expected) || application_fail(error, QA_ERROR_NOT_FOUND,
            "native TEAM combat callback replaced its physical client"));
}

static int32_t signed_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool score(const team_combat_scope *scope, qa_actor_id actor,
    uint32_t slot, qa_vec3 origin, int32_t amount, bool ordinary, qa_error *error)
{
    qa_q3_source_team_state team;
    if (!client_live(scope, actor, slot, error) ||
        !qa_q3_source_team_state_read(scope->game, &team, error)) return false;
    if (team.warmup_time_ms) return true;
    if (!qa_q3_source_score_plum(scope->game, actor, origin, amount, error) ||
        !client_live(scope, actor, slot, error) ||
        !(ordinary ? qa_modes_q3_source_death_score(scope->modes, scope->mode, actor, amount, error)
                   : qa_modes_add_score(scope->modes, scope->mode, actor, amount, error)) ||
        !client_live(scope, actor, slot, error)) return false;
    if (scope->game_type == 3) {
        qa_q3_player_state player;
        qa_mode_view mode;
        if (!qa_q3_player_read(scope->game, actor, &player) ||
            !qa_q3_source_team_state_read(scope->game, &team, error) ||
            !qa_modes_read(scope->modes, scope->mode, &mode, error) ||
            player.persistent_team < 0 || player.persistent_team > 3)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                "native AddScore lost its actual persistant team");
        int32_t index = player.persistent_team;
        team.team_scores[index] = signed_bits((uint32_t)team.team_scores[index] + (uint32_t)amount);
        if (!qa_q3_source_team_state_write(scope->game, &team, error)) return false;
        qa_team_id selected_team = index == 1 ? mode.rules.teams[0] : index == 2 ? mode.rules.teams[1] : 0;
        if (selected_team && (!qa_modes_team_score(scope->modes, scope->mode, selected_team, amount, error) ||
            !client_live(scope, actor, slot, error))) return false;
    }
    return application_native_q3_rank(scope->provider, error) &&
        client_live(scope, actor, slot, error);
}

static bool points(const team_combat_scope *scope, qa_actor_id actor,
    uint32_t slot, qa_vec3 origin, int32_t amount, qa_error *error)
{
    return score(scope, actor, slot, origin, amount, false, error);
}

static bool defend(const team_combat_scope *scope, qa_actor_id actor,
    uint32_t slot, bool base, qa_error *error)
{
    qa_q3_source_player_team_state team;
    if (!client_live(scope, actor, slot, error) ||
        !qa_q3_client_team_state_read(scope->game, slot, &team, error)) return false;
    int32_t *counter = base ? &team.base_defense : &team.carrier_defense;
    *counter = signed_bits((uint32_t)*counter + 1u);
    return qa_q3_client_team_state_write(scope->game, slot, &team, error) &&
        qa_q3_client_award(scope->game, actor, QA_Q3_AWARD_DEFEND, 1, error) &&
        client_live(scope, actor, slot, error) &&
        qa_q3_source_award_visual(scope->game, actor, 0x10000u, error) &&
        client_live(scope, actor, slot, error);
}

static bool in_pvs(const team_combat_scope *scope, qa_vec3 first, qa_vec3 second,
    bool *out, qa_error *error)
{
    qa_collision_leaf a, b;
    *out = false;
    if (!live(scope, error) || !qa_collision_point_leaf(scope->geometry, first, &a, error) ||
        !qa_collision_point_leaf(scope->geometry, second, &b, error) ||
        !qa_collision_cluster_visible(scope->geometry, a.cluster, b.cluster, false, out, error))
        return false;
    if (!*out) return true;
    qa_cvars *registry = application_native_q3_console_registry(scope->provider);
    const qa_cvar_view *no_areas = qa_cvars_find(registry, "cm_noAreas");
    if (!no_areas)
        return application_fail(error, QA_ERROR_NOT_FOUND, "native TEAM PVS lost its source area policy");
    if (no_areas->number != 0) return true;
    return qa_collision_areas_connected(scope->geometry, a.area, b.area, out, error);
}

static float distance(qa_vec3 first, qa_vec3 second)
{
    volatile float x = first.x - second.x, y = first.y - second.y, z = first.z - second.z;
    volatile float xx = x * x, yy = y * y, zz = z * z;
    volatile float xy = xx + yy, sum = xy + zz;
    return (float)sqrt((double)sum);
}

static bool near_visible(const team_combat_scope *scope, float distance_to_base,
    qa_vec3 first, qa_vec3 second, bool *out, qa_error *error)
{
    *out = false;
    return !(distance_to_base < 1000) || in_pvs(scope, first, second, out, error);
}

static bool same_class(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++, b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return false;
    }
    return !*left && !*right;
}

static bool find_base(const team_combat_scope *scope, const char *classname,
    qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    uint32_t count;
    if (!qa_q3_source_entity_count(scope->game, &count, error)) return false;
    for (uint32_t slot = 0; slot < count; ++slot) {
        qa_q3_source_binding row;
        if (!qa_q3_source_binding_read(scope->game, slot, &row, error)) return false;
        if (!row.in_use || !row.classname) continue;
        const char *name = qa_strings_cstr(qa_session_strings(scope->application->session), row.classname);
        if (!name)
            return application_fail(error, QA_ERROR_NOT_FOUND, "native TEAM base lost its source classname");
        if (!same_class(name, classname)) continue;
        bool dropped;
        if (!qa_q3_source_dropped_read(scope->game, slot, &dropped, error)) return false;
        if (!dropped) { *out = row.actor; return true; }
    }
    return true;
}

bool application_native_q3_team_check_hurt_carrier(void *opaque, qa_actor_id target,
    qa_actor_id attacker, qa_error *error)
{
    application_provider *provider = opaque;
    if (provider && provider->application &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider)
        return true;
    team_combat_scope scope;
    if (!begin(opaque, &scope, error)) return false;
    qa_q3_native_client victim, killer;
    qa_q3_player_state target_ps, attacker_ps;
    uint32_t target_slot, attacker_slot;
    bool target_present, attacker_present;
    bool okay = client(&scope, target, &target_slot, &victim, &target_ps, &target_present, error) &&
        client(&scope, attacker, &attacker_slot, &killer, &attacker_ps, &attacker_present, error);
    if (okay && target_present && attacker_present && victim.session.team != killer.session.team &&
        (target_ps.powerups[victim.session.team == 1 ? 8 : 7] || target_ps.generic1)) {
        qa_q3_source_player_team_state state;
        okay = qa_q3_client_team_state_read(scope.game, attacker_slot, &state, error);
        if (okay) {
            state.last_hurt_carrier_ms = (float)scope.time;
            okay = qa_q3_client_team_state_write(scope.game, attacker_slot, &state, error);
        }
    }
    return finish(&scope, okay, error);
}

bool application_native_q3_source_death_score(void *opaque, qa_actor_id target,
    qa_actor_id attacker, qa_error *error)
{
    application_provider *provider = opaque;
    if (provider && provider->application &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider)
        return true;
    team_combat_scope scope;
    if (!begin(provider, &scope, error)) return false;
    qa_q3_native_client victim, killer;
    qa_q3_player_state victim_ps, killer_ps;
    uint32_t victim_slot, killer_slot;
    bool victim_present, killer_present;
    bool okay = client(&scope, target, &victim_slot, &victim, &victim_ps, &victim_present, error) &&
        client(&scope, attacker, &killer_slot, &killer, &killer_ps, &killer_present, error);
    if (okay && victim_present) {
        qa_actor_id recipient = killer_present ? attacker : target;
        uint32_t slot = killer_present ? killer_slot : victim_slot;
        int32_t amount = !killer_present || qa_actor_id_equal(target, attacker) ||
            (scope.game_type >= 3 && victim.session.team == killer.session.team) ? -1 : 1;
        qa_vec3 origin;
        okay = qa_q3_source_current_origin_read(scope.game, target, &origin, error) &&
            score(&scope, recipient, slot, origin, amount, true, error) &&
            client_live(&scope, target, victim_slot, error);
    }
    return finish(&scope, okay, error);
}

static bool frag_bonuses(const team_combat_scope *scope, qa_actor_id target,
    qa_actor_id attacker, qa_error *error)
{
    qa_q3_native_client victim, killer;
    qa_q3_player_state target_ps, attacker_ps;
    uint32_t target_slot, attacker_slot;
    bool target_present, attacker_present;
    if (!client(scope, target, &target_slot, &victim, &target_ps, &target_present, error) ||
        !client(scope, attacker, &attacker_slot, &killer, &attacker_ps, &attacker_present, error)) return false;
    if (!target_present || !attacker_present || qa_actor_id_equal(target, attacker) ||
        (scope->game_type >= 3 && victim.session.team == killer.session.team)) return true;
    int32_t team = victim.session.team, opposing = team == 1 ? 2 : team == 2 ? 1 : team;
    int32_t flag = team == 1 ? 7 : 8;
    int32_t enemy_flag = scope->game_type == 5 ? 9 : team == 1 ? 8 : 7;
    int32_t tokens = scope->missionpack && scope->game_type == 7 ? target_ps.generic1 : 0;
    qa_vec3 target_origin;
    if (!qa_q3_source_current_origin_read(scope->game, target, &target_origin, error)) return false;
    if (target_ps.powerups[enemy_flag] || tokens) {
        qa_q3_source_player_team_state state;
        if (!qa_q3_client_team_state_read(scope->game, attacker_slot, &state, error)) return false;
        state.last_fragged_carrier_ms = (float)scope->time;
        if (!qa_q3_client_team_state_write(scope->game, attacker_slot, &state, error)) return false;
        bool has_flag = target_ps.powerups[enemy_flag] != 0;
        uint32_t bonus = scope->missionpack ? 20u : 2u;
        if (!has_flag) bonus *= (uint32_t)tokens * (uint32_t)tokens;
        if (!points(scope, attacker, attacker_slot, target_origin, signed_bits(bonus), error) ||
            !qa_q3_client_team_state_read(scope->game, attacker_slot, &state, error)) return false;
        state.frag_carrier = signed_bits((uint32_t)state.frag_carrier + 1u);
        if (!qa_q3_client_team_state_write(scope->game, attacker_slot, &state, error) ||
            !client_live(scope, target, target_slot, error) ||
            !qa_q3_client_slot_read(scope->game, attacker_slot, &killer, error)) return false;
        char message[256];
        const char *name = team == 1 ? "RED" : team == 2 ? "BLUE" : team == 3 ? "SPECTATOR" : "FREE";
        snprintf(message, sizeof(message), "print \"%s^7 fragged %s's %s carrier!\n\"",
            killer.netname, name, has_flag ? "flag" : "skull");
        size_t length = strlen(message);
        for (size_t index = 7; index + 1 < length; ++index)
            if (message[index] == '"') message[index] = '\'';
        if (!application_native_q3_send_command(scope->provider, -1, message, error) ||
            !client_live(scope, attacker, attacker_slot, error) ||
            !client_live(scope, target, target_slot, error)) return false;
        for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
            qa_q3_source_binding row;
            qa_q3_native_client teammate;
            if (!qa_q3_source_binding_read(scope->game, slot, &row, error) ||
                !qa_q3_client_slot_read(scope->game, slot, &teammate, error)) return false;
            if (!row.in_use || teammate.session.team != opposing) continue;
            if (!qa_q3_client_team_state_read(scope->game, slot, &state, error)) return false;
            state.last_hurt_carrier_ms = 0;
            if (!qa_q3_client_team_state_write(scope->game, slot, &state, error)) return false;
        }
        return true;
    }
    qa_q3_source_player_team_state target_team;
    if (!qa_q3_client_team_state_read(scope->game, target_slot, &target_team, error)) return false;
    volatile float now = (float)scope->time, since_hurt = now - target_team.last_hurt_carrier_ms;
    if (target_team.last_hurt_carrier_ms != 0 && since_hurt < 8000) {
        if (!points(scope, attacker, attacker_slot, target_origin, scope->missionpack ? 5 : 2, error))
            return false;
        qa_q3_source_player_team_state state;
        if (!qa_q3_client_team_state_read(scope->game, attacker_slot, &state, error)) return false;
        state.carrier_defense = signed_bits((uint32_t)state.carrier_defense + 1u);
        if (!qa_q3_client_team_state_write(scope->game, attacker_slot, &state, error) ||
            !client_live(scope, target, target_slot, error) ||
            !qa_q3_client_team_state_read(scope->game, target_slot, &target_team, error)) return false;
        target_team.last_hurt_carrier_ms = 0;
        return qa_q3_client_team_state_write(scope->game, target_slot, &target_team, error) &&
            qa_q3_client_award(scope->game, attacker, QA_Q3_AWARD_DEFEND, 1, error) &&
            client_live(scope, attacker, attacker_slot, error) &&
            qa_q3_source_award_visual(scope->game, attacker, 0x10000u, error) &&
            client_live(scope, attacker, attacker_slot, error);
    }
    const char *classname;
    qa_actor_id carrier = {0};
    if (scope->missionpack && scope->game_type == 6) {
        if (killer.session.team != 1 && killer.session.team != 2) return true;
        classname = killer.session.team == 1 ? "team_redobelisk" : "team_blueobelisk";
    } else if (scope->missionpack && scope->game_type == 7) classname = "team_neutralobelisk";
    else {
        if (killer.session.team != 1 && killer.session.team != 2) return true;
        classname = killer.session.team == 1 ? "team_CTF_redflag" : "team_CTF_blueflag";
        for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
            qa_q3_source_binding row;
            if (!qa_q3_source_binding_read(scope->game, slot, &row, error)) return false;
            if (!row.in_use) continue;
            qa_q3_player_state player;
            if (!qa_q3_player_read(scope->game, row.actor, &player))
                return application_fail(error, QA_ERROR_NOT_FOUND, "native TEAM carrier lost its source PS");
            if (player.powerups[flag]) { carrier = row.actor; break; }
        }
    }
    qa_actor_id base;
    if (!find_base(scope, classname, &base, error)) return false;
    if (!base.registry) return true;
    qa_vec3 base_origin, attacker_origin;
    if (!qa_q3_source_current_origin_read(scope->game, base, &base_origin, error) ||
        !qa_q3_source_current_origin_read(scope->game, attacker, &attacker_origin, error)) return false;
    float target_distance = distance(target_origin, base_origin);
    float attacker_distance = distance(attacker_origin, base_origin);
    bool visible;
    if (!near_visible(scope, target_distance, base_origin, target_origin, &visible, error)) return false;
    if (!visible && !near_visible(scope, attacker_distance, base_origin, attacker_origin, &visible, error))
        return false;
    if (visible && killer.session.team != victim.session.team)
        return points(scope, attacker, attacker_slot, target_origin, scope->missionpack ? 10 : 1, error) &&
            defend(scope, attacker, attacker_slot, true, error);
    if (!carrier.registry || qa_actor_id_equal(carrier, attacker)) return true;
    qa_vec3 carrier_origin;
    if (!qa_q3_source_current_origin_read(scope->game, carrier, &carrier_origin, error)) return false;
    float carrier_distance = distance(attacker_origin, carrier_origin);
    if (!near_visible(scope, carrier_distance, carrier_origin, target_origin, &visible, error)) return false;
    if (!visible && !near_visible(scope, attacker_distance, carrier_origin, attacker_origin, &visible, error))
        return false;
    return !visible || killer.session.team == victim.session.team ||
        (points(scope, attacker, attacker_slot, target_origin, scope->missionpack ? 2 : 1, error) &&
         defend(scope, attacker, attacker_slot, false, error));
}

bool application_native_q3_team_frag_bonuses(void *opaque, qa_actor_id target,
    qa_actor_id attacker, qa_error *error)
{
    application_provider *provider = opaque;
    if (provider && provider->application &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider)
        return true;
    team_combat_scope scope;
    if (!begin(opaque, &scope, error)) return false;
    return finish(&scope, frag_bonuses(&scope, target, attacker, error), error);
}

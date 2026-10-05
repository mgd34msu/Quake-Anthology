#include "native_q1_composition_death.h"
#include "map_players_private.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_source_obituary.h"
#include "native_q1_console.h"
#include "native_q1_composition.h"
#include "native_q1_composition_flags.h"
#include "native_q1_composition_rogue.h"
#include "qa/modes_q1_source.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q3_source.h"
#include "qa/game_q1_source_rogue_runes.h"

#include <math.h>

typedef struct death_call {
    qa_application *app;
    application_provider *source;
    qa_q1_level *level;
    qa_q1_game_operation operation;
} death_call;

static bool current(death_call *call, qa_error *error)
{
    qa_application *app = call->app;
    application_provider *source = call->source;
    qa_q1_options options;
    qa_clock_state clock;
    double seconds;
    if (!qa_q1_game_operation_live(&call->operation) || !app ||
        app->destroy_requested || app->finalizing || !app->session || !app->world ||
        !source || source->application != app || !source->constructed ||
        !source->attached || source->close_pending || source->state.q1 != call->operation.game ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        !call->level || source->q1_level != call->level || !app->players ||
        app->players->map_provider != source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 death callback lost its physical GAME publication");
    if (!qa_q1_source_respawn_options_read(call->operation.game, &options, &seconds, error)) return false;
    return (options.provider == source->owner && qa_session_clock(app->session, source->owner, &clock) &&
        clock.frame.provider == source->owner && clock.frame.kind == source->component.clock.kind) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 death callback lost its actual source clock");
}

static bool player_current(death_call *call, qa_actor_id actor, qa_error *error)
{
    if (!current(call, error)) return false;
    uint32_t slot;
    qa_q1_source_client_view client;
    if (!qa_q1_native_client_slot(call->operation.game, actor, &slot, error) ||
        !qa_q1_source_client_read(call->operation.game, actor, &client) ||
        client.slot != slot || !qa_actor_id_equal(client.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 death callback lost its physical client");
    for (size_t i = 0; i < call->app->players->count; ++i) {
        const application_player_record *record = call->app->players->records + i;
        if (!record->retiring && record->client_slot == slot && qa_actor_id_equal(record->actor, actor))
            return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Q1 death client differs from its actual published roster");
}

bool application_native_q1_source_death_bound(void *context, qa_mode_id mode,
    qa_actor_id actor, bool *bound, bool *ctf, qa_error *error)
{
    qa_application *app = context;
    if (!app || !bound || !ctf)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source death binding has no outputs");
    *bound = false;
    *ctf = false;
    if (!app->primary_mode_ready || mode.slot != app->primary_mode.slot ||
        mode.generation != app->primary_mode.generation) return true;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->kind != APPLICATION_PROVIDER_Q1) return true;
    qa_q1_source_client_view client;
    if (!qa_q1_source_client_read(source->state.q1, actor, &client)) return true;
    death_call call = {.app = app, .source = source, .level = source->q1_level};
    qa_q1_options options;
    double seconds;
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error) &&
        player_current(&call, actor, error) &&
        qa_q1_source_respawn_options_read(call.operation.game, &options, &seconds, error);
    if (okay) { *bound = true; *ctf = options.program == QA_Q1_CTF; }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_source_fired(void *context, qa_actor_id actor,
    qa_item_id weapon, qa_error *error)
{
    application_provider *arsenal = context;
    qa_application *app = arsenal ? arsenal->application : NULL;
    if (!app || !arsenal->constructed || !arsenal->attached || arsenal->close_pending ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != arsenal)
        return application_fail(error, QA_ERROR_ARGUMENT, "Accepted shot lost its selected arsenal");
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->kind != APPLICATION_PROVIDER_Q1) return true;
    death_call call = {.app = app, .source = source, .level = source->q1_level};
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error) && current(&call, error);
    qa_q1_source_client_view client;
    if (okay && qa_q1_source_client_read(call.operation.game, actor, &client)) {
        qa_item_id axe = qa_strings_find(qa_session_strings(app->session),
            (qa_bytes){(const uint8_t *)"q1:weapon/axe", 13});
        okay = player_current(&call, actor, error) &&
            qa_q1_level_note_attack(call.level, actor, weapon && weapon == axe, error) &&
            player_current(&call, actor, error);
    }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool classname(death_call *call, qa_actor_id actor,
    const qa_q1_source_obituary_actor *raw, qa_string_id *out, qa_error *error)
{
    if (!current(call, error)) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(call->app->session), actor);
    if (!record) return application_fail(error, QA_ERROR_ARGUMENT, "Obituary source actor retired");
    *out = raw->classname;
    if (raw->entity || raw->client) return true;
    application_provider *owner = call->app->live_providers;
    while (owner && owner->owner != record->owner) owner = owner->next_live;
    /* An actor without a source execution has the donor's empty classname.
     * A canonical item/definition ID is not an execution classname. */
    if (!owner || !record->has_source) return true;
    if (!owner->constructed || !owner->attached || owner->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Obituary execution lost its actual provider");
    if (owner->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_source_obituary_actor source;
        if (!qa_q1_source_obituary_read(owner->state.q1, actor, &source, error)) return false;
        *out = source.classname;
    } else if (owner->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_bot_entity source;
        if (!qa_q2_bot_entity_read(owner->state.q2, actor, &source, error)) return false;
        *out = source.present ? source.classname : 0;
    } else if (owner->kind == APPLICATION_PROVIDER_QC) {
        qa_builtin_actor_traits source;
        if (!application_qc_actor_traits(owner, actor, &source))
            return application_fail(error, QA_ERROR_ARGUMENT, "Obituary lost its actual QuakeC execution");
        *out = source.classname;
    } else if (owner->kind == APPLICATION_PROVIDER_Q3 ||
        ((owner->kind == APPLICATION_PROVIDER_QVM || owner->kind == APPLICATION_PROVIDER_NATIVE) &&
          owner->product && owner->product->family == QA_GAME_Q3)) {
        if (owner->kind == APPLICATION_PROVIDER_Q3) {
            uint32_t slot;
            if (!qa_q3_source_actor_slot(owner->state.q3, actor, &slot, error)) return false;
        }
        if (!qa_strings_intern_cstr(qa_session_strings(call->app->session),
            "q3:projectile", out, error)) return false;
    }
    return current(call, error) &&
        (qa_actors_get(qa_session_actors(call->app->session), actor) == record ||
         application_fail(error, QA_ERROR_ARGUMENT, "Obituary execution changed during source read"));
}

static bool actor_read(death_call *call, qa_actor_id actor,
    qa_q1_obituary_actor *out, qa_q1_source_obituary_actor *raw, qa_error *error)
{
    qa_combat_state combat = {0};
    if (!current(call, error) || !qa_q1_source_obituary_read(call->operation.game, actor, raw, error))
        return false;
    if (qa_combat_storage_serial(call->app->combat, actor) &&
        !qa_combat_read(call->app->combat, actor, &combat, error)) return false;
    if (!current(call, error)) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(call->app->session), actor);
    if (!record) return application_fail(error, QA_ERROR_ARGUMENT, "Obituary actor retired during health read");
    qa_q1_obituary_actor value = {.actor = actor,
        .kill_string = raw->kill_string, .team = raw->team, .weapon = raw->weapon,
        .health = combat.health, .water_level = (uint8_t)raw->water_level,
        .quad_expires = raw->quad_remaining, .invulnerable_expires = raw->invulnerable_remaining,
        .player = raw->client, .monster = raw->monster, .brush = raw->brush};
    qa_strings *strings = qa_session_strings(call->app->session);
    if (!classname(call, actor, raw, &value.classname, error) ||
        !qa_strings_intern_cstr(strings, raw->name ? raw->name : "", &value.name, error)) return false;
    if (raw->client) {
        int32_t water, level;
        if (!player_current(call, actor, error) ||
            !qa_strings_intern_cstr(strings, "player", &value.classname, error) ||
            !application_control_water_read(call->app, actor, &water, &level, error) ||
            !player_current(call, actor, error)) return false;
        if (level < 0 || level > 3)
            return application_fail(error, QA_ERROR_ARGUMENT, "Obituary water level has no source representation");
        value.water_type = water == -3 || water == 32 ? -3 :
            water == -4 || water == 16 ? -4 : water == -5 || water == 8 ? -5 : 0;
        value.water_level = (uint8_t)level;
    }
    *out = value;
    return current(call, error);
}

static bool source_policy(death_call *call, const char *name, double *out, qa_error *error)
{
    if (!current(call, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(call->source);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    if (!value || value->owner != call->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source obituary lost its actual GAME cvar");
    *out = (float)(value->number);
    return true;
}

static bool emit(death_call *call, qa_builtin_event *event, qa_error *error)
{
    double seconds;
    if (!current(call, error) ||
        !qa_q1_game_clock_read(call->operation.game, &event->time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source death event lost its actual clock");
    event->family = QA_GAME_Q1;
    event->provider = call->source->owner;
    return application_emit(call->app, event, error) && current(call, error);
}

static bool qw_text(death_call *call, const char *text, qa_error *error)
{
    qa_string_id id;
    return current(call, error) && qa_strings_intern_cstr(
        qa_session_strings(call->app->session), text, &id, error) &&
        emit(call, &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
            .text = id, .code = 1, .flags = 2u | QA_Q1_SOURCE_MESSAGE_LITERAL}, error);
}

static bool qw_name(death_call *call, qa_actor_id actor, qa_error *error)
{
    if (!actor.registry) return qw_text(call, "", error);
    qa_q1_source_obituary_actor raw;
    return current(call, error) &&
        qa_q1_source_obituary_read(call->operation.game, actor, &raw, error) &&
        qw_text(call, raw.name ? raw.name : "", error);
}

static bool qw_score(death_call *call, qa_actor_id actor, int delta, qa_error *error)
{
    return player_current(call, actor, error) &&
        qa_q1_source_client_add_score(call->operation.game, actor, delta, error) &&
        player_current(call, actor, error);
}

static bool qw_log(death_call *call, qa_actor_id killer, qa_actor_id victim, qa_error *error)
{
    return current(call, error) &&
        qa_q1_wire_qw_logfrag(call->operation.game, killer, victim, error) && current(call, error);
}

static bool qw_is(death_call *call, qa_string_id id, const char *value)
{
    const char *text = qa_strings_cstr(qa_session_strings(call->app->session), id);
    return text && !strcmp(text, value);
}

static bool qw_team(death_call *call, qa_actor_id actor, qa_string_id *out, qa_error *error)
{
    const char *team = NULL;
    if (actor.registry) (void)qa_q1_source_client_info(call->operation.game, actor, "team", &team);
    return current(call, error) && qa_strings_intern_cstr(
        qa_session_strings(call->app->session), team ? team : "", out, error);
}

/* ClientObituary's locals retain the initial roll and team strings. Its other
 * entity reads occur at the authored stages, after reached host callbacks. */
static bool qw_obituary(death_call *call, const qa_q1_obituary_input *input, qa_error *error)
{
    float roll = qa_q1_game_random(call->operation.game);
    qa_actor_id victim = input->victim.actor;
    qa_actor_id attacker = input->attacker ? input->attacker->actor : (qa_actor_id){0};
    qa_actor_id owner = input->telefrag_owner ? input->telefrag_owner->actor : (qa_actor_id){0};
    qa_string_id attacker_team, victim_team;
    double deathmatch;
    if (!current(call, error) || !qw_team(call, attacker, &attacker_team, error) ||
        !qw_team(call, victim, &victim_team, error) ||
        !source_policy(call, "deathmatch", &deathmatch, error)) return false;
    bool same_team = attacker_team == victim_team && !qw_is(call, attacker_team, "");
    qa_string_id type = input->death_type;
    qa_string_id killer_class = input->attacker ? input->attacker->classname : 0;
    if (deathmatch > 3 && qw_is(call, type, "selfwater"))
        return qw_name(call, victim, error) && qw_text(call, " electrocutes himself.\n ", error) &&
            qw_score(call, victim, -1, error);
    if (qw_is(call, killer_class, "teledeath"))
        return qw_name(call, victim, error) && qw_text(call, " was telefragged by ", error) &&
            qw_name(call, owner, error) && qw_text(call, "\n", error) &&
            qw_log(call, owner, victim, error) && qw_score(call, owner, 1, error);
    if (qw_is(call, killer_class, "teledeath2"))
        return qw_text(call, "Satan's power deflects ", error) && qw_name(call, victim, error) &&
            qw_text(call, "'s telefrag\n", error) && qw_score(call, victim, -1, error) &&
            qw_log(call, victim, victim, error);
    if (qw_is(call, killer_class, "teledeath3"))
        return qw_name(call, victim, error) && qw_text(call, " was telefragged by ", error) &&
            qw_name(call, owner, error) && qw_text(call, "'s Satan's power\n", error) &&
            qw_score(call, victim, -1, error) && qw_log(call, victim, victim, error);
    if (qw_is(call, type, "squish")) {
        if (input->teamplay != 0 && same_team && !qa_actor_id_equal(victim, attacker))
            return qw_log(call, attacker, attacker, error) && qw_score(call, attacker, -1, error) &&
                qw_name(call, attacker, error) && qw_text(call, " squished a teammate\n", error);
        if (qw_is(call, killer_class, "player") && !qa_actor_id_equal(victim, attacker))
            return qw_name(call, attacker, error) && qw_text(call, " squishes ", error) &&
                qw_name(call, victim, error) && qw_text(call, "\n", error) &&
                qw_log(call, attacker, victim, error) && qw_score(call, attacker, 1, error);
        return qw_log(call, victim, victim, error) && qw_score(call, victim, -1, error) &&
            qw_name(call, victim, error) && qw_text(call, " was squished\n", error);
    }
    qa_q1_obituary_actor target, killer;
    qa_q1_source_obituary_actor raw;
    if (qw_is(call, killer_class, "player")) {
        if (qa_actor_id_equal(victim, attacker)) {
            if (!qw_log(call, attacker, attacker, error) || !qw_score(call, attacker, -1, error) ||
                !qw_name(call, victim, error) || !actor_read(call, victim, &target, &raw, error)) return false;
            const char *text = " becomes bored with life\n";
            if (qw_is(call, type, "grenade")) text = " tries to put the pin back in\n";
            else if (!qw_is(call, type, "rocket") && target.weapon == QA_Q1_LIGHTNING && target.water_level > 1)
                text = target.water_type == -4 ? " discharges into the slime\n" :
                    target.water_type == -5 ? " discharges into the lava\n" : " discharges into the water.\n";
            return qw_text(call, text, error);
        }
        if (input->teamplay == 2 && same_team) {
            const char *text = roll < .25f ? " mows down a teammate\n" :
                roll < .50f ? " checks his glasses\n" : roll < .75f ?
                " gets a frag for the other team\n" : " loses another friend\n";
            return qw_name(call, attacker, error) && qw_text(call, text, error) &&
                qw_score(call, attacker, -1, error) && qw_log(call, attacker, attacker, error);
        }
        if (!qw_log(call, attacker, victim, error) || !qw_score(call, attacker, 1, error) ||
            !actor_read(call, attacker, &killer, &raw, error)) return false;
        double quad_finished = raw.quad_finished;
        if (!actor_read(call, victim, &target, &raw, error)) return false;
        const char *first = "", *last = "";
        if (qw_is(call, type, "nail")) { first = " was nailed by "; last = "\n"; }
        else if (qw_is(call, type, "supernail")) { first = " was punctured by "; last = "\n"; }
        else if (qw_is(call, type, "grenade")) {
            first = target.health < -40 ? " was gibbed by " : " eats ";
            last = target.health < -40 ? "'s grenade\n" : "'s pineapple\n";
        } else if (qw_is(call, type, "rocket")) {
            if (quad_finished > 0 && target.health < -40) {
                roll = qa_q1_game_random(call->operation.game);
                if (!current(call, error)) return false;
                if (roll >= .6f)
                    return qw_name(call, attacker, error) && qw_text(call, " rips ", error) &&
                        qw_name(call, victim, error) && qw_text(call, " a new one\n", error);
                first = roll < .3f ? " was brutalized by " : " was smeared by ";
                last = "'s quad rocket\n";
            } else { first = target.health < -40 ? " was gibbed by " : " rides "; last = "'s rocket\n"; }
        } else if (killer.weapon == QA_Q1_AXE) { first = " was ax-murdered by "; last = "\n"; }
        else if (killer.weapon == QA_Q1_SHOTGUN) { first = " chewed on "; last = "'s boomstick\n"; }
        else if (killer.weapon == QA_Q1_SUPER_SHOTGUN) { first = " ate 2 loads of "; last = "'s buckshot\n"; }
        else if (killer.weapon == QA_Q1_LIGHTNING) {
            first = " accepts "; last = killer.water_level > 1 ? "'s discharge\n" : "'s shaft\n";
        }
        return qw_name(call, victim, error) && qw_text(call, first, error) &&
            qw_name(call, attacker, error) && qw_text(call, last, error);
    }
    if (!qw_log(call, victim, victim, error) || !qw_score(call, victim, -1, error) ||
        !actor_read(call, victim, &target, &raw, error)) return false;
    int32_t water_type = target.water_type;
    if (!qw_name(call, victim, error)) return false;
    const char *text;
    if (water_type == -3 || water_type == -4 || water_type == -5) {
        if (water_type == -5) {
            if (!actor_read(call, victim, &target, &raw, error)) return false;
            if (target.health < -15) return qw_text(call, " burst into flames\n", error);
        }
        roll = qa_q1_game_random(call->operation.game);
        if (!current(call, error)) return false;
        text = water_type == -3 ? (roll < .5f ? " sleeps with the fishes\n" : " sucks it down\n") :
            water_type == -4 ? (roll < .5f ? " gulped a load of slime\n" : " can't exist on slime alone\n") :
            roll < .5f ? " turned into hot slag\n" : " visits the Volcano God\n";
    } else if (qw_is(call, killer_class, "explo_box")) text = " blew up\n";
    else if (qw_is(call, type, "falling")) text = " fell to his death\n";
    else if (qw_is(call, type, "nail") || qw_is(call, type, "supernail")) text = " was spiked\n";
    else if (qw_is(call, type, "laser")) text = " was zapped\n";
    else if (qw_is(call, killer_class, "fireball")) text = " ate a lavaball\n";
    else if (qw_is(call, killer_class, "trigger_changelevel")) text = " tried to leave\n";
    else text = " died\n";
    return qw_text(call, text, error);
}

bool application_native_q1_source_before_reaction(qa_application *app,
    const qa_damage_outcome *outcome, qa_error *error)
{
    if (!app || !outcome) return application_fail(error, QA_ERROR_ARGUMENT, "Source reaction has no actual outcome");
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || outcome->stale) return true;
    death_call call = {.app = app, .source = source, .level = source->q1_level};
    qa_actor_id actor = outcome->request.target, attacker = outcome->request.attack.attacker;
    qa_q1_source_obituary_actor raw;
    qa_q1_options options;
    double seconds;
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error) &&
        current(&call, error) && qa_q1_source_obituary_read(call.operation.game, actor, &raw, error) &&
        qa_q1_source_respawn_options_read(call.operation.game, &options, &seconds, error);
    bool death = outcome->result.reaction == QA_REACTION_DEATH;
    if (okay && death && (raw.client || raw.entity)) {
        qa_combat_state combat;
        okay = qa_combat_read(app->combat, actor, &combat, error) && current(&call, error);
        if (okay && combat.health < -99)
            okay = qa_combat_set_health(app->combat, actor, -99, error) && current(&call, error);
    }
    if (!okay) goto finish;
    if (!raw.client) {
        if (!death || !raw.entity || raw.movement == QA_PHYSICS_STATIONARY || raw.movement == QA_PHYSICS_PUSH)
            goto finish;
        qa_q1_source_client_view client;
        if (options.edition == QA_Q1_RERELEASE && raw.monster && !raw.horde_source_die && attacker.registry &&
            qa_q1_source_client_read(call.operation.game, attacker, &client))
            okay = player_current(&call, attacker, error) &&
                qa_q1_source_client_add_score(call.operation.game, attacker, 1, error) &&
                player_current(&call, attacker, error);
        if (okay) { (void)qa_q1_game_random(call.operation.game); okay = current(&call, error); }
        goto finish;
    }
    okay = player_current(&call, actor, error) &&
        qa_q1_level_note_damage(call.level, actor, outcome->result.applied_damage, error) &&
        player_current(&call, actor, error);
    if (okay && outcome->result.applied_damage > 0 && options.program == QA_Q1_ROGUE)
        okay = application_native_q1_rogue_confirmed_damage(source, actor, attacker, error) &&
            player_current(&call, actor, error);
    if (!okay || !death) goto finish;
    bool first;
    okay = qa_q1_source_client_record_death(call.operation.game, actor, &first, error) &&
        player_current(&call, actor, error);
    if (!okay || !first) goto finish;
    qa_q1_obituary_input input = {0};
    qa_q1_obituary_actor killer, owner;
    qa_q1_source_obituary_actor killer_raw = {0}, owner_raw, inflictor_raw;
    okay = actor_read(&call, actor, &input.victim, &raw, error);
    if (okay && attacker.registry) {
        okay = actor_read(&call, attacker, &killer, &killer_raw, error);
        if (okay) input.attacker = &killer;
        if (okay && killer_raw.owner.registry) {
            okay = actor_read(&call, killer_raw.owner, &owner, &owner_raw, error);
            if (okay) input.telefrag_owner = &owner;
        }
    }
    if (okay && outcome->request.attack.inflictor.registry) {
        okay = qa_q1_source_obituary_read(call.operation.game, outcome->request.attack.inflictor,
            &inflictor_raw, error) && current(&call, error);
        if (okay) okay = classname(&call, outcome->request.attack.inflictor,
            &inflictor_raw, &input.inflictor_classname, error);
    }
    if (okay) okay = source_policy(&call, "teamplay", &input.teamplay, error);
    if (okay && options.program == QA_Q1_ROGUE) {
        qa_mode_id mode;
        double gamecfg;
        okay = application_native_q1_composition_mode(app, source, &mode, error) &&
            qa_modes_q1_rogue_initialize(app->modes, mode, actor, error) &&
            qa_modes_q1_source_read(app->modes, mode, actor, QA_Q1_ROGUE_STEAM,
                &input.victim_saved_team, error) && source_policy(&call, "gamecfg", &gamecfg, error);
        if (okay) {
            double integer = isfinite(gamecfg) ? fmod(trunc(gamecfg), 4294967296.0) : 0;
            if (integer < 0) integer += 4294967296.0;
            input.gamecfg = (uint32_t)integer;
            input.tag_context = source;
            input.tag_score = application_native_q1_rogue_tag_score;
        }
    }
    input.attacker_death_type = killer_raw.death_type;
    if (outcome->request.attack.cause.kind == QA_CAUSE_Q1)
        input.death_type = outcome->request.attack.cause.source.q1.death_type;
    else if (okay && outcome->request.attack.cause.kind == QA_CAUSE_ENVIRONMENT &&
        outcome->request.attack.cause.source.hazard == QA_HAZARD_FALL)
        okay = qa_strings_intern_cstr(qa_session_strings(app->session), "falling", &input.death_type, error);
    if (okay && options.quakeworld && options.program == QA_Q1_ID1 && options.edition == QA_Q1_CLASSIC) {
        okay = qw_obituary(&call, &input, error) && player_current(&call, actor, error);
        goto finish;
    }
    qa_q1_obituary_result result;
    if (okay) okay = qa_q1_obituary(call.operation.game, &input, &result, error) &&
        player_current(&call, actor, error);
    if (okay && result.text) okay = emit(&call, &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
        .text = result.text, .arguments = result.arguments, .argument_count = result.argument_count,
        .flags = 2u}, error) && player_current(&call, actor, error);
    if (okay && options.program == QA_Q1_CTF) {
        qa_mode_id mode;
        okay = application_native_q1_ctf_score_death(source, actor, attacker, error) &&
            player_current(&call, actor, error) && application_native_q1_composition_mode(app, source, &mode, error) &&
            qa_modes_q1_source_write(app->modes, mode, actor, QA_Q1_CTF_KILLED, 1, error) &&
            application_native_q1_ctf_drop_flag(source, actor, error) &&
            application_native_q1_ctf_drop_rune(source, actor, error) && player_current(&call, actor, error);
        qa_equipment_state equipment;
        if (okay && app->equipment && qa_equipment_read(app->equipment, actor, &equipment))
            okay = qa_equipment_release_grapple(app->equipment, actor, error) && player_current(&call, actor, error);
    } else if (okay && result.credited_actor.registry) {
        qa_q1_source_client_view client;
        if (qa_q1_source_client_read(call.operation.game, result.credited_actor, &client))
            okay = player_current(&call, result.credited_actor, error) &&
                qa_q1_source_client_add_score(call.operation.game, result.credited_actor, result.score_delta, error) &&
                player_current(&call, result.credited_actor, error);
    }
    if (okay && result.achievement)
        okay = application_record_achievement(source, result.achievement_actor, result.achievement, error) &&
            player_current(&call, actor, error);
    if (okay && options.program == QA_Q1_ROGUE)
        okay = application_native_q1_rogue_player_died(source, actor, attacker, error) &&
            qa_q1_source_rogue_runes_drop(call.operation.game, actor, error) &&
            player_current(&call, actor, error);
finish:
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

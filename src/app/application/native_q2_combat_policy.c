#include "native_q2_combat_policy.h"
#include "native_q2_console.h"
#include "qa/game_q2_combat.h"

static bool live_provider(const application_provider *provider) {
    return provider && provider->constructed && provider->attached && !provider->close_pending;
}

static bool source_rules(application_provider *provider, qa_q2_combat_rules *out,
                         qa_error *error) {
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->state.q2 ||
        !qa_q2_combat_rules_read(provider->state.q2, out) || out->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 combat lost its actual GAME rules");
    return true;
}

static bool character_traits(qa_application *app, qa_actor_id actor,
                              qa_builtin_actor_traits *out,
                              qa_q2_combat_actor *q2, qa_error *error) {
    *out = (qa_builtin_actor_traits){0};
    *q2 = (qa_q2_combat_actor){0};
    if (!qa_actors_get(qa_session_actors(app->session), actor)) return true;
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    bool known = false;
    if (live_provider(character)) switch (character->kind) {
    case APPLICATION_PROVIDER_Q1:
        known = qa_q1_game_actor_traits(character->state.q1, actor, out);
        break;
    case APPLICATION_PROVIDER_Q2:
        known = qa_q2_actor_traits(character->state.q2, actor, out);
        (void)qa_q2_combat_actor_read(character->state.q2, actor, q2);
        break;
    case APPLICATION_PROVIDER_Q3:
        known = qa_q3_actor_traits(character->state.q3, actor, out);
        break;
    case APPLICATION_PROVIDER_QC:
        known = application_qc_actor_traits(character, actor, out);
        break;
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        break;
    }
    if (known) return true;
    qa_physics_properties physical;
    if (!app->physics || !app->physics->services.read ||
        !app->physics->services.read(app->physics->services.context, actor, &physical))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected Q2 combat target has no actual character or movement traits");
    out->player = (physical.flags & QA_PHYSICS_PLAYER) != 0;
    out->monster = (physical.flags & QA_PHYSICS_MONSTER) != 0;
    return true;
}

static bool mode_teams(qa_application *app, bool *ctf, bool *lmctf,
                        bool *lm_railgun, qa_error *error) {
    *ctf = *lmctf = *lm_railgun = false;
    if (!app->modes || !app->primary_mode_ready) return true;
    qa_mode_view view;
    if (!qa_modes_read(app->modes, app->primary_mode, &view, error)) return false;
    if (!view.rules.enabled) return true;
    application_provider *mode = application_mode_provider(app, app->primary_mode);
    if (!live_provider(mode))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 combat mode lost its actual source owner");
    *ctf = view.rules.source == QA_MODE_Q2_CTF;
    *lmctf = view.rules.source == QA_MODE_LMCTF;
    *lm_railgun = *lmctf && (view.rules.flags & 128u) != 0;
    return true;
}

static bool same_players(const qa_builtin_actor_traits *target,
                          const qa_builtin_actor_traits *attacker,
                          const qa_combat_state *target_state,
                          const qa_combat_state *attacker_state) {
    return target->player && attacker->player && attacker_state &&
        target_state->team != 0 && target_state->team == attacker_state->team;
}

qa_actor_owner application_native_q2_attack_inventory(void *opaque, qa_actor_id actor,
                                                       qa_item_id item) {
    application_provider *source = opaque;
    if (!live_provider(source)) return 0;
    qa_application *app = source->application;
    if (!qa_actors_get(qa_session_actors(app->session), actor)) return 0;
    qa_actor_owner owner;
    if (item && qa_inventory_item_owner(app->inventory, actor, item, &owner, NULL)) return owner;
    application_provider *inventory = application_provider_for(app, actor, QA_ROLE_INVENTORY, "");
    return live_provider(inventory) ? inventory->owner : 0;
}

bool application_native_q2_damage_prepare(void *opaque, qa_damage_request *request,
                                           bool *allowed, qa_error *error) {
    application_provider *provider = opaque;
    qa_q2_combat_rules rules;
    if (!request || !allowed || !live_provider(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 damage has no admitted GAME owner");
    if (!source_rules(provider, &rules, error)) return false;
    if (!*allowed) return true;
    qa_application *app = provider->application;
    if (!qa_actors_get(qa_session_actors(app->session), request->target)) {
        *allowed = false;
        return true;
    }
    application_provider *combat = application_provider_for(app, request->target, QA_ROLE_COMBAT, "");
    application_provider *movement = application_provider_for(app, request->target, QA_ROLE_MOVEMENT, "");
    request->attack.combat_provider = combat ? combat->owner : provider->owner;
    request->attack.movement_provider = movement ? movement->owner : 0;
    if (request->attack.weapon && !request->attack.weapon_provider) {
        if (!qa_q2_combat_weapon_owned(provider->state.q2, request->attack.weapon))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q2 damage weapon has no retained source item owner");
        request->attack.weapon_provider = provider->owner;
    }
    application_provider *policy = combat ? combat : provider;
    if (!live_provider(policy) || policy->kind != APPLICATION_PROVIDER_Q2) return true;
    if (!source_rules(policy, &rules, error)) return false;
    if (request->attack.cause.kind != QA_CAUSE_Q2 ||
        qa_actor_id_equal(request->target, request->attack.attacker) ||
        (rules.edition == QA_Q2_RERELEASE && qa_attack_flags(&request->attack).no_protection) ||
        !qa_actors_get(qa_session_actors(app->session), request->attack.attacker)) return true;
    qa_builtin_actor_traits target, attacker;
    qa_q2_combat_actor ignored;
    if (!character_traits(app, request->target, &target, &ignored, error) ||
        !character_traits(app, request->attack.attacker, &attacker, &ignored, error)) return false;
    if (!target.player || !attacker.player) return true;
    bool ctf, lmctf, railgun;
    if (!mode_teams(app, &ctf, &lmctf, &railgun, error)) return false;
    int32_t teamplay = 0;
    if (rules.edition == QA_Q2_RERELEASE &&
        !application_native_q2_source_integer(policy, "teamplay", &teamplay, error)) return false;
    bool classic_team = (rules.deathmatch || rules.cooperative) &&
        (rules.deathmatch_flags & (64u | 128u)) != 0;
    bool same = rules.edition == QA_Q2_RERELEASE && rules.cooperative;
    bool team_game = rules.edition == QA_Q2_CLASSIC ? classic_team :
        ctf || (lmctf && !railgun) || teamplay != 0;
    if (!same && team_game) {
        qa_combat_state target_state, attacker_state;
        if (!qa_combat_read(app->combat, request->target, &target_state, error) ||
            !qa_combat_read(app->combat, request->attack.attacker, &attacker_state, error)) return false;
        same = same_players(&target, &attacker, &target_state, &attacker_state);
    }
    if (same) {
        bool disabled = (rules.deathmatch_flags & 256u) != 0;
        if (rules.edition == QA_Q2_RERELEASE || !disabled) {
            request->attack.cause.source.q2.friendly_fire = true;
            request->attack.cause.source.q2.means_of_death |= INT32_C(0x08000000);
            if (request->attack.cause.source.q2.native == QA_Q2_CAUSE_CLASSIC) {
                request->attack.cause.source.q2.native_value |= INT32_C(0x08000000);
            }
        }
        uint32_t means = (uint32_t)request->attack.cause.source.q2.means_of_death &
            ~UINT32_C(0x08000000);
        if (disabled && (rules.edition == QA_Q2_CLASSIC || means != 47)) request->amount = 0;
    }
    return true;
}

static bool describe(void *opaque, const qa_damage_request *request,
                      const qa_combat_state *target_state,
                      const qa_combat_state *attacker_state,
                      qa_combat_context *out, qa_error *error) {
    application_provider *provider = opaque;
    qa_q2_combat_rules rules;
    if (!source_rules(provider, &rules, error)) return false;
    qa_application *app = provider->application;
    qa_builtin_actor_traits target, attacker;
    qa_q2_combat_actor target_q2, attacker_q2;
    if (!character_traits(app, request->target, &target, &target_q2, error) ||
        !character_traits(app, request->attack.attacker, &attacker, &attacker_q2, error)) return false;
    qa_physics_properties physical;
    if (!app->physics || !app->physics->services.read ||
        !app->physics->services.read(app->physics->services.context, request->target, &physical))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Q2 combat target lost its selected physical motion");
    bool ctf, lmctf, railgun;
    if (!mode_teams(app, &ctf, &lmctf, &railgun, error)) return false;
    bool same = !qa_actor_id_equal(request->target, request->attack.attacker) &&
        same_players(&target, &attacker, target_state, attacker_state);
    qa_body_state body;
    if (!qa_world_body_read(app->world, request->target, &body, error)) return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(body.angles, &forward, NULL, NULL);
    qa_vec3 contact = qa_vec_normalize(qa_vec_sub(request->point, body.origin));
    bool defender = target_q2.defender_sphere;
    application_provider *effects = application_provider_for(app, request->target, QA_ROLE_EFFECTS, "");
    qa_q2_combat_actor effect_q2;
    if (live_provider(effects) && effects->kind == APPLICATION_PROVIDER_Q2 &&
        qa_q2_combat_actor_read(effects->state.q2, request->target, &effect_q2))
        defender |= effect_q2.defender_sphere;
    bool early_team = rules.edition == QA_Q2_CLASSIC &&
        (rules.deathmatch || rules.cooperative) && (rules.deathmatch_flags & (64u | 128u));
    *out = (qa_combat_context){
        .armor = {.q2_profile = target_state->armor.regular.kind == QA_ARMOR_Q2 ||
                target_state->armor.powered.kind != QA_POWER_NONE,
            .rerelease = target_state->armor.powered.source_edition == QA_Q2_POWER_ARMOR_RERELEASE,
            .ctf = ctf, .alive = target_state->health > 0,
            .screen_facing_dot = qa_vec_dot(forward, contact)},
        .game.q2 = {.player = target.player, .monster = target.monster,
            .attacker_player = attacker.player,
            .has_enemy = target_q2.character ? target_q2.has_enemy : physical.enemy.registry != 0,
            .easy_skill = rules.skill == 0, .deathmatch = rules.deathmatch,
            .rerelease = rules.edition == QA_Q2_RERELEASE,
            .defender_sphere = defender, .team_damage_enabled = early_team,
            .friendly_fire = (rules.deathmatch_flags & 256u) == 0,
            .nuke = rules.edition == QA_Q2_RERELEASE &&
                request->attack.cause.kind == QA_CAUSE_Q2 &&
                (((uint32_t)request->attack.cause.source.q2.means_of_death &
                    ~UINT32_C(0x08000000)) == 47),
            .no_knockback = target_state->no_knockback,
            .movable = physical.motion != QA_PHYSICS_STATIONARY &&
                physical.motion != QA_PHYSICS_BOUNCE && physical.motion != QA_PHYSICS_PUSH &&
                physical.motion != QA_PHYSICS_STOP,
            .reject_team_damage = rules.edition == QA_Q2_CLASSIC && same &&
                (ctf || (lmctf && !railgun)),
            .suppress_pain = target_q2.suppress_pain}};
    return true;
}

bool application_native_q2_combat_policy(application_provider *provider,
                                         qa_combat_policy *out, qa_error *error) {
    qa_q2_combat_rules rules;
    if (!out || !source_rules(provider, &rules, error)) return false;
    *out = (qa_combat_policy){.provider = provider->owner, .family = QA_GAME_Q2,
        .context = provider, .describe = describe};
    return true;
}

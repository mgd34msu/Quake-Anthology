#include "internal.h"

bool q2_save_reference(qa_q2_game *g, qa_actor_id id, qa_q2_saved_reference *out, qa_error *e) {
    *out = (qa_q2_saved_reference){0};
    if (id.registry == 0)
        return true;
    if (!qa_actors_save_reference(qa_session_actors(g->services.session), id, &out->actor, e))
        return false;
    out->present = true;
    return true;
}
bool q2_resolve_reference(qa_q2_game *g, qa_q2_saved_reference ref, qa_actor_id *out, qa_error *e) {
    *out = (qa_actor_id){0};
    if (!ref.present)
        return true;
    const qa_actor_record *record =
        qa_actors_resolve_saved(qa_session_actors(g->services.session), ref.actor);
    if (record == NULL)
        return qa_actors_reference_saved(qa_session_actors(g->services.session), ref.actor, true,
                                         out, e);
    *out = record->id;
    return true;
}
bool q2_checkpoint_idle(qa_q2_game *g, qa_error *e) {
    if (g->current_actor.registry != 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 checkpoint requires a completed actor turn");
        return false;
    }
    if (g->hand_steps != 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 checkpoint requires a completed hand action");
        return false;
    }
    for (qa_builtin_snapshot_frame *frame = g->trace_frames; frame != NULL; frame = frame->next)
        if (frame->active) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                         "Q2 checkpoint requires a gameplay callback boundary");
            return false;
        }
    return true;
}
bool qa_q2_runtime_capture(qa_q2_game *g, qa_q2_runtime_checkpoint *out, qa_error *e) {
    if (g == NULL || out == NULL || !q2_checkpoint_idle(g, e))
        return false;
    *out = (qa_q2_runtime_checkpoint){.edition = g->options.edition,
                                      .product = g->options.product,
                                      .definition_count = g->definition_count,
                                      .arsenal_rules = g->arsenal_rules,
                                      .native_hook = g->native_hook,
                                      .hook_edition = g->hook_edition,
                                      .equipment_hook_rules = g->equipment_hook_rules,
                                      .equipment_hook_edition = g->equipment_hook_edition,
                                      .random = g->random,
                                      .rerelease_index = g->rerelease_random.index,
                                      .rerelease_draws = g->rerelease_random.draws,
                                      .sequence = g->sequence,
                                      .actor_sequence = g->actor_sequence,
                                      .now_ns = g->now_ns,
                                      .frame_ns = g->frame_ns,
                                      .grapple_options = g->grapple_options,
                                      .lmctf_plasma_quad = g->lmctf_plasma_quad,
                                      .widow_damage_multiplier = g->widow_damage_multiplier,
                                      .widow_shot_phase = g->widow_shot_phase};
    for (size_t i = 0; i < 624; ++i)
        out->rerelease_words[i] = g->rerelease_random.words[i];
    for (uint32_t i = 0; i < g->definition_count; ++i)
        out->definition_order[i] = g->definition_order[i];
    return true;
}
bool qa_q2_runtime_restore(qa_q2_game *g, const qa_q2_runtime_checkpoint *state, qa_error *e) {
    if (g == NULL || state == NULL ||
        state->edition != g->options.edition ||
        state->product != g->options.product || state->widow_shot_phase >= 4 ||
        state->definition_count != g->definition_count ||
        state->definition_count >= QA_Q2_WEAPON_COUNT ||
        state->arsenal_rules != g->arsenal_rules || state->native_hook != g->native_hook ||
        state->hook_edition != g->hook_edition ||
        state->equipment_hook_rules != g->equipment_hook_rules ||
        state->equipment_hook_edition != g->equipment_hook_edition ||
        state->random.front >= 31 ||
        state->random.rear >= 31 || state->rerelease_index > 624 || state->frame_ns == 0 ||
        (state->widow_damage_multiplier != 1 && state->widow_damage_multiplier != 2 &&
         state->widow_damage_multiplier != 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 runtime checkpoint");
        return false;
    }
    for (uint32_t i = 0; i < state->definition_count; ++i)
        if (state->definition_order[i] != g->definition_order[i]) {
            qa_error_set(e,QA_ERROR_FORMAT,i,"Q2 continuation source arsenal order differs");
            return false;
        }
    if (!q2_checkpoint_idle(g, e) || !qa_q2_grapple_configure(g, &state->grapple_options, e))
        return false;
    g->random = state->random;
    g->rerelease_random.index = state->rerelease_index;
    g->rerelease_random.draws = state->rerelease_draws;
    for (size_t i = 0; i < 624; ++i)
        g->rerelease_random.words[i] = state->rerelease_words[i];
    g->sequence = state->sequence;
    g->actor_sequence = state->actor_sequence;
    g->now_ns = state->now_ns;
    g->frame_ns = state->frame_ns;
    g->lmctf_plasma_quad = state->lmctf_plasma_quad;
    g->widow_damage_multiplier = state->widow_damage_multiplier;
    g->widow_shot_phase = state->widow_shot_phase;
    return true;
}
bool qa_q2_actor_capture(qa_q2_game *g, qa_actor_id id, qa_q2_actor_checkpoint *out, qa_error *e) {
    if (g == NULL || out == NULL || !q2_checkpoint_idle(g, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (a == NULL)
        return false;
    const q2_projectile *p = &a->projectile;
    qa_q2_actor_checkpoint snapshot = {.source_order = a->source_order,
                                       .extra_effects = a->extra_effects,
                                       .environment_flags = a->environment_flags,
                                       .combat_surprise_ns = a->combat_surprise_ns,
                                       .character_birth_epoch = a->character_birth_epoch,
                                       .character_immortal = a->character_immortal,
                                       .character_no_damage_effects = a->character_no_damage_effects,
                                       .combat_life_owner = a->combat_life_owner,
                                       .combat_life_birth_epoch = a->combat_life_birth_epoch,
                                       .combat_death_ns = a->combat_death_ns,
                                       .combat_life_present = a->combat_life_present,
                                       .combat_no_knockback = a->combat_no_knockback,
                                       .combat_alive_knockback_only = a->combat_alive_knockback_only,
                                       .alpha = a->alpha,
                                       .weapon_bound = a->weapon_bound,
                                       .physics_bound = a->physics_bound,
                                       .weapon = a->weapon,
                                       .input = a->input,
                                       .weapon_turn = a->weapon_turn,
                                       .silencer = a->silencer,
                                       .physics = a->physics,
                                       .projectile = {.kind = (uint32_t)p->kind,
                                                      .attack = p->attack,
                                                      .owner = p->owner, .enemy = p->enemy, .child = p->child,
                                                      .movedir = p->movedir,
                                                      .damage = p->damage,
                                                      .kick = p->kick,
                                                      .radius_damage = p->radius_damage,
                                                      .radius = p->radius,
                                                      .gravity = p->gravity,
                                                      .speed = p->speed,
                                                      .delay = p->delay,
                                                      .captured_mass = p->captured_mass,
                                                      .turn_fraction = p->turn_fraction,
                                                      .born_ns = p->born_ns,
                                                      .expire_ns = p->expire_ns,
                                                      .next_ns = p->next_ns,
                                                      .effect_ns = p->effect_ns,
                                                      .effects = p->effects,
                                                      .render_flags = p->render_flags,
                                                      .gib_flags = p->gib_flags,
                                                      .classname = p->classname,
                                                      .model = p->model,
                                                      .loop_sound = p->loop_sound,
                                                      .direct_mod = p->direct_mod,
                                                      .splash_mod = p->splash_mod,
                                                      .frame = p->frame,
                                                      .phase = p->phase,
                                                      .alpha = p->alpha,
                                                      .wait = p->wait,
                                                      .skin = p->skin,
                                                      .scale = p->scale,
                                                      .hand = p->hand,
                                                      .held = p->held,
                                                      .armed = p->armed,
                                                      .visible = p->visible,
                                                      .gekk = p->gekk,
                                                      .dodgeable = p->dodgeable}};
    qa_q2_projectile_checkpoint *saved = &snapshot.projectile;
    if (!q2_save_reference(g, p->attack.attacker, &saved->attacker, e) ||
        !q2_save_reference(g, p->attack.inflictor, &saved->inflictor, e) ||
        !q2_save_reference(g, p->attack.projectile, &saved->projectile, e) ||
        !q2_save_reference(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), a->physics.enemy), &snapshot.physics_enemy, e) ||
        !q2_save_reference(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), a->physics.goal), &snapshot.physics_goal, e))
        return false;
    saved->attack.attacker = saved->attack.inflictor = saved->attack.projectile = (qa_actor_id){0};
    snapshot.physics.enemy = snapshot.physics.goal = (qa_actor_reference){0};
    for (unsigned i = 0; i < 2; ++i) {
        snapshot.grapples[i] = a->grapples[i];
        if (!q2_save_reference(g, a->grapples[i].hook, &snapshot.grapple_hooks[i], e))
            return false;
        snapshot.grapples[i].hook = (qa_actor_id){0};
    }
    snapshot.hand_grenade_bound = a->hand_grenade_bound;
    snapshot.hand_grenade = a->hand_grenade;
    snapshot.lmctf_plasma_bounce = a->lmctf_plasma_bounce;
    if (snapshot.weapon_turn.firing_weapon != snapshot.weapon.weapon) {
        snapshot.weapon_turn.firing_weapon = QA_Q2_WEAPON_NONE;
        snapshot.weapon_turn.firing_credit = 0;
    }
    if (!isfinite(snapshot.weapon_turn.firing_credit) || snapshot.weapon_turn.firing_credit < 0 ||
        snapshot.weapon_turn.firing_credit >= 1 ||
        (snapshot.weapon_turn.firing_weapon == QA_Q2_WEAPON_NONE && snapshot.weapon_turn.firing_credit != 0)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Selected Q2 turn has invalid returned firing credit");
        return false;
    }
    *out = snapshot;
    return true;
}
static bool valid_resource(qa_q2_game *g, qa_string_id id) {
    return id == 0 || qa_strings_cstr(qa_session_strings(g->services.session), id) != NULL;
}
bool qa_q2_actor_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_actor_checkpoint *s,
                         qa_error *e) {
    if (g == NULL || s == NULL ||
        s->source_order == 0 || s->silencer < 0 ||
        (!s->weapon_bound && (s->weapon_turn.attack || s->weapon_turn.latched_attack ||
                             s->weapon_turn.weapon_thunk || s->weapon_turn.firing_weapon != QA_Q2_WEAPON_NONE)) ||
        !isfinite(s->weapon_turn.firing_credit) || s->weapon_turn.firing_credit < 0 ||
        s->weapon_turn.firing_credit >= 1 ||
        (s->weapon_turn.firing_weapon == QA_Q2_WEAPON_NONE ? s->weapon_turn.firing_credit != 0 :
            (g->options.edition != QA_Q2_CLASSIC || s->weapon_turn.firing_weapon != s->weapon.weapon)) ||
        ((s->character_immortal || s->character_no_damage_effects) &&
         g->options.edition != QA_Q2_RERELEASE) ||
        (s->combat_life_present ? !s->combat_life_owner :
         (s->combat_life_owner || s->combat_life_birth_epoch || s->combat_death_ns ||
          s->combat_no_knockback || s->combat_alive_knockback_only)) ||
        (s->combat_no_knockback && g->options.edition != QA_Q2_CLASSIC) ||
        (s->combat_alive_knockback_only && g->options.edition != QA_Q2_RERELEASE) ||
        (!s->combat_alive_knockback_only && s->combat_death_ns) ||
        !isfinite(s->alpha) ||
        !qa_vec_finite(s->input.angles) || !isfinite(s->input.gravity) ||
        !isfinite(s->input.view_height) || (unsigned)s->input.hand > QA_Q2_CENTER_HAND ||
        (unsigned)s->input.source_rules > QA_Q2_WEAPON_RULES_LMCTF ||
        (unsigned)s->physics.motion > QA_PHYSICS_STEP ||
        (unsigned)s->physics.solid > QA_PHYSICS_CORPSE ||
        !qa_vec_finite(s->physics.angular_velocity) ||
        !qa_vec_finite(s->physics.gravity_direction) || !isfinite(s->physics.gravity_scale) ||
        !isfinite(s->physics.delta_yaw) || !isfinite(s->physics.ideal_yaw) ||
        !isfinite(s->physics.yaw_speed) || qa_actor_reference_present(s->physics.enemy) != 0 ||
        qa_actor_reference_present(s->physics.goal) != 0) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 actor checkpoint");
        return false;
    }
    const qa_q2_projectile_checkpoint *p = &s->projectile;
    if (p->kind > Q2_BFG_LASER ||
        (p->kind == Q2_BFG_LASER && g->options.edition != QA_Q2_RERELEASE) ||
        ((p->gib_flags & Q2_GIB_WIDOW_LEGS) != 0 &&
         (p->kind != Q2_GIB || p->frame < 0 || p->frame > 23 ||
          p->phase < 0 || p->phase > 1 || p->expire_ns != UINT64_MAX ||
          !isfinite(p->delay) || p->delay < 0 ||
          (double)p->delay * (double)Q2_NS >= (double)UINT64_MAX ||
          (p->phase == 1 && (p->frame != 23 || p->delay == 0)) ||
          p->gib_flags != Q2_GIB_WIDOW_LEGS)) ||
        ((p->gib_flags & (Q2_GIB_WIDOW | Q2_GIB_WIDOW_SIZED | Q2_GIB_WIDOW_HIT_SOUND)) != 0 &&
         (p->kind != Q2_GIB || (p->gib_flags & Q2_GIB_WIDOW) == 0 ||
          ((p->gib_flags & Q2_GIB_WIDOW_HIT_SOUND) != 0 &&
           (p->gib_flags & Q2_GIB_WIDOW_SIZED) == 0))) ||
        (p->kind == Q2_PROBOSCIS &&
         (p->phase < Q2_PROBOSCIS_FLYING || p->phase > Q2_PROBOSCIS_RETURNED)) ||
        (p->kind == Q2_PROBOSCIS_SEGMENT && p->phase != 0) ||
        ((p->kind == Q2_RERELEASE_SPAWN_GROWTH || p->kind == Q2_RERELEASE_SPAWN_BEAM) &&
         (p->phase != 0 || g->options.edition != QA_Q2_RERELEASE)) ||
        (p->kind == Q2_RERELEASE_SPAWN_GROWTH && p->delay <= 0) ||
        !qa_vec_finite(p->movedir) || !isfinite(p->damage) ||
        !isfinite(p->kick) || !isfinite(p->radius_damage) || !isfinite(p->radius) ||
        p->radius < 0 || !isfinite(p->gravity) || !isfinite(p->speed) || p->speed < 0 ||
        !isfinite(p->delay) || !isfinite(p->captured_mass) || !isfinite(p->turn_fraction) ||
        !isfinite(p->scale) || p->scale < 0 || !isfinite(p->alpha) ||
        !valid_resource(g, p->classname) ||
        !valid_resource(g, p->model) || !valid_resource(g, p->loop_sound) ||
        !valid_resource(g, s->weapon.loop_sound) || !valid_resource(g, s->weapon.view_model) ||
        p->attack.attacker.registry != 0 || p->attack.inflictor.registry != 0 ||
        p->attack.projectile.registry != 0) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 projectile checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_projectile restored = {.kind = (q2_projectile_kind)p->kind,
                              .owner = p->owner, .enemy = p->enemy, .child = p->child,
                              .attack = p->attack,
                              .movedir = p->movedir,
                              .damage = p->damage,
                              .kick = p->kick,
                              .radius_damage = p->radius_damage,
                              .radius = p->radius,
                              .gravity = p->gravity,
                              .speed = p->speed,
                              .delay = p->delay,
                              .captured_mass = p->captured_mass,
                              .turn_fraction = p->turn_fraction,
                              .born_ns = p->born_ns,
                              .expire_ns = p->expire_ns,
                              .next_ns = p->next_ns,
                              .effect_ns = p->effect_ns,
                              .effects = p->effects,
                              .render_flags = p->render_flags,
                              .gib_flags = p->gib_flags,
                              .classname = p->classname,
                              .model = p->model,
                              .loop_sound = p->loop_sound,
                              .direct_mod = p->direct_mod,
                              .splash_mod = p->splash_mod,
                              .frame = p->frame,
                              .phase = p->phase,
                              .wait = p->wait,
                              .skin = p->skin,
                              .scale = p->scale,
                              .alpha = p->alpha,
                              .hand = p->hand,
                              .held = p->held,
                              .armed = p->armed,
                              .visible = p->visible,
                              .gekk = p->gekk,
                              .dodgeable = p->dodgeable};
    qa_physics_properties physics = s->physics;
    qa_actor_id physics_enemy, physics_goal;
    if (!q2_resolve_reference(g, p->attacker, &restored.attack.attacker, e) ||
        !q2_resolve_reference(g, p->inflictor, &restored.attack.inflictor, e) ||
        !q2_resolve_reference(g, p->projectile, &restored.attack.projectile, e) ||
        !q2_resolve_reference(g, s->physics_enemy, &physics_enemy, e) ||
        !q2_resolve_reference(g, s->physics_goal, &physics_goal, e))
        return false;
    physics.enemy = qa_actor_reference_lifetime(physics_enemy);
    physics.goal = qa_actor_reference_lifetime(physics_goal);
    qa_q2_grapple_state grapples[2];
    for (unsigned i = 0; i < 2; ++i) {
        grapples[i] = s->grapples[i];
        if ((unsigned)grapples[i].phase > QA_Q2_GRAPPLE_HANG || grapples[i].hook.registry != 0 ||
            grapples[i].hook_state < 0 || grapples[i].hook_state > 2 ||
            grapples[i].hook_length < 0 ||
            (grapples[i].equipment_bound &&
             (grapples[i].equipment.weapon !=
                  (i == QA_Q2_CTF_GRAPPLE ? QA_Q2_GRAPPLE : QA_Q2_LMCTF_HOOK) ||
              !valid_resource(g, grapples[i].equipment.loop_sound) ||
              !valid_resource(g, grapples[i].equipment.view_model)))) {
            qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 grapple checkpoint");
            return false;
        }
        if ((grapples[i].equipment_bound && !q2_weapon_validate(g, &grapples[i].equipment, e)) ||
            !q2_resolve_reference(g, s->grapple_hooks[i], &grapples[i].hook, e))
            return false;
    }
    if (s->weapon_bound && !q2_weapon_validate(g, &s->weapon, e))
        return false;
    if (s->hand_grenade_bound) {
        qa_inventory_entry ammo;
        if (!q2_hand_validate(&s->hand_grenade, e) ||
            (!g->restoring_continuation &&
             !qa_inventory_entry_read(g->services.inventory, id, g->ammo[QA_Q2_GRENADES], &ammo, e)))
            return false;
    }
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->weapon_bound = s->weapon_bound;
    a->weapon = s->weapon;
    a->input = s->input;
    a->weapon_turn = s->weapon_turn;
    a->silencer = s->silencer;
    a->physics_bound = s->physics_bound;
    a->physics = physics;
    a->projectile = restored;
    for (unsigned i = 0; i < 2; ++i)
        a->grapples[i] = grapples[i];
    a->hand_grenade_bound = s->hand_grenade_bound;
    a->hand_grenade = s->hand_grenade;
    a->extra_effects = s->extra_effects;
    a->environment_flags = s->environment_flags;
    a->combat_surprise_ns = s->combat_surprise_ns;
    a->character_birth_epoch = s->character_birth_epoch;
    a->character_immortal = s->character_immortal;
    a->character_no_damage_effects = s->character_no_damage_effects;
    a->combat_life_owner = s->combat_life_owner;
    a->combat_life_birth_epoch = s->combat_life_birth_epoch;
    a->combat_death_ns = s->combat_death_ns;
    a->combat_life_present = s->combat_life_present;
    a->combat_no_knockback = s->combat_no_knockback;
    a->combat_alive_knockback_only = s->combat_alive_knockback_only;
    a->alpha = s->alpha;
    a->lmctf_plasma_bounce = s->lmctf_plasma_bounce;
    q2_actor_order(g, a, s->source_order);
    return true;
}

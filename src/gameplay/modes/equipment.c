#include "qa/equipment.h"
#include "internal.h"

typedef struct equipment_actor {
    qa_equipment *equipment;
    qa_equipment_state state;
    qa_inventory_lease q3_items;
    bool active, configuring;
} equipment_actor;
struct qa_equipment {
    qa_equipment_options options;
    equipment_actor *actors;
    uint32_t capacity;
};
static equipment_actor *equipment_get(qa_equipment *g, qa_actor_id actor) {
    if (!g || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor))
        return NULL;
    equipment_actor *p = &g->actors[actor.slot];
    return p->active && qa_actor_id_equal(p->state.actor, actor) ? p : NULL;
}
static bool selection_valid(qa_equipment *g, const qa_equipment_selection *s) {
    if (!s || s->grapple < QA_GRAPPLE_DISABLED || s->grapple > QA_GRAPPLE_Q3 ||
        s->binding < QA_EQUIPMENT_OFFHAND || s->binding > QA_EQUIPMENT_WEAPON_SLOT ||
        s->grenades.initial_ammo < 0 || s->grenades.capacity < s->grenades.initial_ammo)
        return false;
    if ((s->grapple == QA_GRAPPLE_THREEWAVE || s->grapple == QA_GRAPPLE_ROGUE) && !g->options.q1)
        return false;
    if ((s->grapple == QA_GRAPPLE_Q2_CTF || s->grapple == QA_GRAPPLE_LMCTF ||
         s->grenades.enabled) &&
        !g->options.q2)
        return false;
    if (s->grapple == QA_GRAPPLE_Q3 && !g->options.q3)
        return false;
    if (s->grapple != QA_GRAPPLE_DISABLED && s->binding == QA_EQUIPMENT_WEAPON_SLOT &&
        (!g->options.primary_holster || !g->options.primary_holstered ||
         !g->options.primary_resume))
        return false;
    return true;
}
typedef struct equipment_admission {
    qa_q2_hand_grenade_admission q2;
    qa_q3_player_binding q3;
    qa_inventory_admission *inventory;
} equipment_admission;
static bool admission_abort(qa_equipment *g, equipment_admission *a, qa_error *e) {
    qa_inventory_admission_abort(a->inventory);
    a->inventory = NULL;
    qa_q2_hand_grenade_abort(&a->q2);
    return !a->q3.token || qa_q3_bind_player_rollback(g->options.q3, &a->q3, e);
}
static bool admission_prepare(qa_equipment *g, qa_actor_id actor,
                              const qa_equipment_selection *selection,
                              equipment_admission *a, qa_error *e) {
    if (selection->grapple == QA_GRAPPLE_Q3 &&
        !qa_q3_bind_player_begin(g->options.q3, actor, QA_Q3_EQUIPMENT, 100, &a->q3, e))
        return false;
    if (g->options.q2 &&
        (!qa_q2_hand_grenade_prepare(g->options.q2, actor, &selection->grenades, &a->q2, e) ||
         !qa_inventory_prepare_entries(g->options.services.inventory, actor,
                                       &a->q2.initial_ammo, 1, &a->inventory, e)))
        return false;
    return true;
}
static bool admission_commit(qa_equipment *g, qa_actor_id actor, equipment_admission *a,
                             qa_error *e) {
    /* Inventory validation is the last operation allowed to call source code.
     * All remaining checks and commits use the retained native records only. */
    if ((a->inventory && !qa_inventory_admission_validate(a->inventory, e)) ||
        (a->q2.active && !qa_q2_hand_grenade_validate(&a->q2, e)) ||
        (a->q3.token && !qa_q3_bind_player_validate(g->options.q3, &a->q3, e)))
        return false;
    equipment_actor *p = &g->actors[actor.slot];
    if (!p->configuring || !qa_actor_id_equal(p->state.actor, actor) ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor))
        return mode_fail(e, "equipment actor changed during admission");
    if (a->inventory) {
        if (!qa_inventory_admission_commit(a->inventory, e))
            return false;
        a->inventory = NULL;
    }
    if (a->q2.active && !qa_q2_hand_grenade_commit(&a->q2, e))
        return false;
    return !a->q3.token || qa_q3_bind_player_commit(g->options.q3, &a->q3, e);
}
bool qa_equipment_create(const qa_equipment_options *options, qa_equipment **out, qa_error *e) {
    if (!options || !out || !qa_builtin_services_validate(&options->services, e))
        return mode_fail(e, "invalid equipment services");
    qa_equipment *g = calloc(1, sizeof(*g));
    if (!g) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating equipment");
        return false;
    }
    g->options = *options;
    g->capacity = qa_actors_capacity(qa_session_actors(options->services.session));
    g->actors = calloc(g->capacity, sizeof(*g->actors));
    if (!g->actors) {
        free(g);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating equipment players");
        return false;
    }
    *out = g;
    return true;
}
void qa_equipment_destroy(qa_equipment *g) {
    if (!g)
        return;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->actors[i].q3_items.serial)
            (void)qa_inventory_close_items(g->options.services.inventory, g->actors[i].q3_items,
                                           NULL);
    free(g->actors);
    free(g);
}
bool qa_equipment_release_grapple(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return true;
    p->state.grapple_pressed = false;
    p->state.grapple_released = false;
    switch (p->state.selection.grapple) {
    case QA_GRAPPLE_THREEWAVE:
    case QA_GRAPPLE_ROGUE:
        return qa_q1_grapple_release(g->options.q1, actor, e);
    case QA_GRAPPLE_Q2_CTF:
        return qa_q2_grapple_reset(g->options.q2, actor, QA_Q2_CTF_GRAPPLE, e);
    case QA_GRAPPLE_LMCTF:
        return qa_q2_grapple_reset(g->options.q2, actor, QA_Q2_LMCTF_GRAPPLE, e);
    case QA_GRAPPLE_Q3:
        return qa_q3_release_grapple(g->options.q3, actor, e);
    case QA_GRAPPLE_DISABLED:
        return true;
    }
    return mode_fail(e, "invalid grapple mechanic");
}
bool qa_equipment_admit(qa_equipment *g, qa_actor_id actor, const qa_equipment_selection *selection,
                        qa_error *e) {
    if (!g || !selection_valid(g, selection) || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor) ||
        g->actors[actor.slot].configuring)
        return mode_fail(e, "invalid equipment admission");
    if (equipment_get(g, actor))
        return qa_equipment_configure(g, actor, selection, e);
    qa_equipment_selection wanted = *selection;
    selection = &wanted;
    equipment_actor *p = &g->actors[actor.slot];
    equipment_actor before = *p;
    *p = (equipment_actor){.equipment = g, .configuring = true, .state.actor = actor};
    equipment_admission admission = {0};
    bool ok = admission_prepare(g, actor, selection, &admission, e) &&
              admission_commit(g, actor, &admission, e);
    qa_error cleanup = {0};
    if (!admission_abort(g, &admission, &cleanup)) {
        if (e) *e = cleanup;
        ok = false;
    }
    if (qa_actor_id_equal(p->state.actor, actor)) {
        if (ok)
            *p = (equipment_actor){.equipment = g, .active = true,
                .state = {.actor = actor, .selection = *selection}};
        else
            *p = before;
    }
    return ok;
}
bool qa_equipment_configure(qa_equipment *g, qa_actor_id actor,
                            const qa_equipment_selection *selection, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || p->configuring || !selection_valid(g, selection))
        return mode_fail(e, "invalid equipment configuration");
    qa_equipment_selection wanted = *selection;
    selection = &wanted;
    p->configuring = true;
    equipment_admission admission = {0};
    bool ok = false;
    if (!admission_prepare(g, actor, selection, &admission, e))
        goto finished;
    if (p->state.selection.grapple != selection->grapple ||
        p->state.selection.binding != selection->binding) {
        if (!qa_equipment_select_grapple(g, actor, false, e) ||
            !qa_equipment_release_grapple(g, actor, e))
            goto finished;
        p = equipment_get(g, actor);
        if (!p) {
            mode_fail(e, "equipment actor retired during configuration");
            goto finished;
        }
        if (p->state.slot_holstering || p->state.slot_lowering) {
            p->state.pending_selection = *selection;
            p->state.configuration_pending = true;
            ok = true;
            goto finished;
        }
    }
    if (!admission_commit(g, actor, &admission, e))
        goto finished;
    p->state.configuration_pending = false;
    p->state.selection = *selection;
    ok = true;
finished: {
        qa_error cleanup = {0};
        if (!admission_abort(g, &admission, &cleanup)) {
            if (e) *e = cleanup;
            ok = false;
        }
        p = equipment_get(g, actor);
        if (p)
            p->configuring = false;
        return ok;
    }
}
bool qa_equipment_input(qa_equipment *g, qa_actor_id actor, const qa_equipment_controls *input,
                        qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !input || !qa_vec_finite(input->view_angles) ||
        !qa_vec_finite(input->previous_velocity) || !isfinite(input->view_height) ||
        !isfinite(input->gravity) || !isfinite(input->teleport_until) ||
        input->hand < QA_Q2_RIGHT_HAND || input->hand > QA_Q2_CENTER_HAND || input->water_level > 3)
        return mode_fail(e, "invalid equipment input");
    qa_equipment_state *s = &p->state;
    s->grapple_pressed |= input->grapple_held && !s->controls.grapple_held;
    s->grapple_released |= !input->grapple_held && s->controls.grapple_held;
    s->grenade_pressed |= input->grenade_held && !s->controls.grenade_held;
    s->grenade_released |= !input->grenade_held && s->controls.grenade_held;
    if (input->teleport_known) {
        if (!input->prediction && s->teleport_seen &&
            s->teleport_sequence != input->teleport_sequence && s->selection.release_on_teleport &&
            !qa_equipment_release_grapple(g, actor, e))
            return false;
        s->teleport_seen = true;
        s->teleport_sequence = input->teleport_sequence;
    }
    if (!input->prediction && input->jump && !s->previous_jump && s->selection.release_on_jump &&
        !qa_equipment_release_grapple(g, actor, e))
        return false;
    s->previous_jump = input->jump;
    s->controls = *input;
    return true;
}
bool qa_equipment_select_grapple(qa_equipment *g, qa_actor_id actor, bool selected, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return mode_fail(e, "unknown equipment player");
    qa_equipment_state *s = &p->state;
    if (selected && (s->selection.grapple == QA_GRAPPLE_DISABLED ||
                     s->selection.binding != QA_EQUIPMENT_WEAPON_SLOT))
        return mode_fail(e, "grapple has no selected weapon slot");
    s->slot_requested = selected;
    if (selected && !s->slot_active && !s->slot_holstering && !s->slot_lowering) {
        s->slot_holstering = true;
        if (!g->options.primary_holster(g->options.context, actor, e))
            return false;
    } else if (!selected && (s->slot_active || s->slot_holstering)) {
        if (!s->selection.retain_on_weapon_change && !qa_equipment_release_grapple(g, actor, e))
            return false;
        if (s->slot_active && (s->selection.grapple == QA_GRAPPLE_Q2_CTF ||
                               s->selection.grapple == QA_GRAPPLE_LMCTF)) {
            qa_q2_grapple_kind kind =
                s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
            if (!qa_q2_grapple_equipment_holster(g->options.q2, actor, kind, e))
                return false;
            s->slot_lowering = true;
        }
        if (s->slot_active && s->selection.grapple == QA_GRAPPLE_THREEWAVE &&
            !qa_q1_grapple_weapon_holster(g->options.q1, actor, e))
            return false;
        s->slot_active = false;
        /* Primary resume is deferred until its pending holster has completed. */
        if (!s->slot_holstering && !s->slot_lowering)
            return g->options.primary_resume(g->options.context, actor, e);
    }
    return true;
}
static qa_q2_weapon_input q2_input(const qa_equipment_state *s) {
    const qa_equipment_controls *c = &s->controls;
    return (qa_q2_weapon_input){.angles = c->view_angles,
                                .hand = c->hand,
                                .view_height = c->view_height,
                                .gravity = c->gravity,
                                .attack = c->grapple_held,
                                .latched_attack = s->grapple_pressed,
                                .spectator = c->spectator,
                                .quad_until_ns = c->quad_until_ns,
                                .double_until_ns = c->double_until_ns,
                                .quad_fire_until_ns = c->quad_fire_until_ns,
                                .players_collide = c->players_collide,
                                .haste = c->haste,
                                .no_stack_double = c->no_stack_double,
                                .animate_player = s->slot_active};
}
bool qa_equipment_step(qa_equipment *g, qa_actor_id actor, uint64_t now, uint64_t elapsed,
                       qa_q2_hand_lifecycle lifecycle, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return true;
    qa_equipment_state *s = &p->state;
    qa_equipment_controls *c = &s->controls;
    if (c->prediction)
        return true;
    bool pressed = s->grapple_pressed, released = s->grapple_released;
    bool grenade_pressed = s->grenade_pressed, grenade_released = s->grenade_released;
    s->grapple_pressed = s->grapple_released = s->grenade_pressed = s->grenade_released = false;
    if (g->options.q2) {
        qa_q2_hand_grenade_input grenade = {.weapon = q2_input(s),
                                            .pressed = grenade_pressed,
                                            .held = c->grenade_held,
                                            .released = grenade_released,
                                            .lifecycle = lifecycle,
                                            .project_context = g->options.context,
                                            .project = g->options.grenade_projection};
        if (!qa_q2_hand_grenade_step(g->options.q2, actor, &grenade, now, elapsed, e))
            return false;
    }
    if (lifecycle != QA_Q2_HAND_ALIVE || c->spectator) {
        s->slot_requested = s->slot_active = s->slot_holstering = s->slot_lowering = false;
        if (s->configuration_pending) {
            qa_equipment_selection selection = s->pending_selection;
            if (!qa_equipment_configure(g, actor, &selection, e))
                return false;
        }
        return qa_equipment_release_grapple(g, actor, e);
    }
    if (s->slot_lowering) {
        qa_q2_grapple_kind kind =
            s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
        qa_q2_weapon_input input = q2_input(s);
        input.latched_attack = false;
        if (s->selection.retain_on_weapon_change)
            input.attack = true;
        qa_q2_grapple_state native;
        if (!qa_q2_grapple_equipment_tick(g->options.q2, actor, kind, &input, now, elapsed, e) ||
            !qa_q2_grapple_read(g->options.q2, actor, kind, &native, e))
            return false;
        if (native.equipment.handoff != QA_Q2_PRIMARY_HOLSTERED)
            return true;
        s->slot_lowering = false;
        if (s->configuration_pending) {
            qa_equipment_selection selection = s->pending_selection;
            if (!qa_equipment_configure(g, actor, &selection, e))
                return false;
        }
        if (!s->slot_requested)
            return g->options.primary_resume(g->options.context, actor, e);
        s->slot_holstering = true;
    }
    if (s->slot_holstering && g->options.primary_holstered(g->options.context, actor)) {
        s->slot_holstering = false;
        s->slot_active = s->slot_requested;
        if (!s->slot_requested) {
            if (s->configuration_pending) {
                qa_equipment_selection selection = s->pending_selection;
                if (!qa_equipment_configure(g, actor, &selection, e))
                    return false;
            }
            return g->options.primary_resume(g->options.context, actor, e);
        }
        if (s->selection.grapple == QA_GRAPPLE_Q2_CTF || s->selection.grapple == QA_GRAPPLE_LMCTF) {
            qa_q2_grapple_kind kind =
                s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
            if (!qa_q2_grapple_equipment_resume(g->options.q2, actor, kind, e))
                return false;
        }
        if (s->selection.grapple == QA_GRAPPLE_THREEWAVE &&
            !qa_q1_grapple_weapon_resume(g->options.q1, actor, e))
            return false;
    }
    bool slot = s->selection.binding == QA_EQUIPMENT_WEAPON_SLOT;
    bool enabled = s->selection.grapple != QA_GRAPPLE_DISABLED && (!slot || s->slot_active);
    if (!enabled)
        return s->selection.retain_on_weapon_change ? true
                                                    : qa_equipment_release_grapple(g, actor, e);
    if (released)
        return qa_equipment_release_grapple(g, actor, e);
    switch (s->selection.grapple) {
    case QA_GRAPPLE_DISABLED:
        return true;
    case QA_GRAPPLE_THREEWAVE:
    case QA_GRAPPLE_ROGUE: {
        qa_q1_input input = {.view_angles = c->view_angles,
                             .attack = c->grapple_held,
                             .jump = c->jump,
                             .water_level = c->water_level,
                             .water_type = c->water_type,
                             .teleport_until = c->teleport_until};
        if (!qa_q1_grapple_input(g->options.q1, actor, &input, !enabled, e))
            return false;
        if (slot && s->selection.grapple == QA_GRAPPLE_THREEWAVE)
            return qa_q1_grapple_weapon_tick(g->options.q1, actor, &input, true, e);
        return !pressed ||
               qa_q1_grapple_fire(g->options.q1, actor,
                                  s->selection.grapple == QA_GRAPPLE_THREEWAVE, &input, e);
    }
    case QA_GRAPPLE_Q2_CTF:
    case QA_GRAPPLE_LMCTF: {
        qa_q2_grapple_kind kind =
            s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
        if (slot) {
            qa_q2_weapon_input input = q2_input(s);
            input.latched_attack = pressed;
            return qa_q2_grapple_equipment_tick(g->options.q2, actor, kind, &input, now, elapsed,
                                                e);
        }
        return !pressed || qa_q2_grapple_offhand(g->options.q2, actor, kind, true, e);
    }
    case QA_GRAPPLE_Q3:
        if (!qa_q3_player_set_view(g->options.q3, actor, c->view_angles, c->view_height, e))
            return false;
        return !pressed || qa_q3_fire_weapon(g->options.q3, actor, QA_Q3_W_GRAPPLE, e);
    }
    return mode_fail(e, "invalid grapple mechanic");
}
bool qa_equipment_after_movement(qa_equipment *g, qa_actor_id actor, bool pulse, uint64_t now,
                                 uint64_t elapsed, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return true;
    qa_grapple_mechanic kind = p->state.selection.grapple;
    return (kind != QA_GRAPPLE_Q2_CTF && kind != QA_GRAPPLE_LMCTF) ||
           qa_q2_grapple_after_movement(g->options.q2, actor, pulse, now, elapsed, e);
}
bool qa_equipment_lmctf_command(qa_equipment *g, qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                bool native_slot, bool pressed, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    mode_instance *v = mode_get(m, id);
    if (!p || !v || v->value.rules.source != QA_MODE_LMCTF)
        return mode_fail(e, "invalid LMCTF hook command");
    if (!qa_modes_can_move(m, id, actor) || p->state.controls.spectator)
        return true;
    if (!native_slot) {
        if (p->state.selection.grapple != QA_GRAPPLE_DISABLED &&
            p->state.selection.binding == QA_EQUIPMENT_OFFHAND) {
            qa_equipment_controls input = p->state.controls;
            input.grapple_held = pressed;
            return qa_equipment_input(g, actor, &input, e);
        }
        return true;
    }
    if (!g->options.q2)
        return mode_fail(e, "LMCTF native hook has no Q2 provider");
    qa_item_id hook;
    if (!qa_builtin_resource(&g->options.services, "q2:weapon_hook", &hook, e))
        return false;
    if (!(v->value.rules.flags & 16u)) {
        if (!pressed)
            return true;
        return g->options.select_weapon
                   ? g->options.select_weapon(g->options.context, actor, hook, e)
                   : mode_fail(e, "LMCTF hook selection needs arsenal coordinator");
    }
    qa_q2_weapon_state weapon;
    qa_error local = {0};
    bool selected = p->state.selection.grapple == QA_GRAPPLE_LMCTF && p->state.slot_active;
    if (qa_q2_weapon_read(g->options.q2, actor, &weapon, &local))
        selected |= weapon.weapon == QA_Q2_LMCTF_HOOK;
    else if (local.code != QA_ERROR_NOT_FOUND) {
        if (e)
            *e = local;
        return false;
    }
    if (selected)
        return qa_q2_grapple_hold(g->options.q2, actor, pressed, e);
    if (!pressed)
        return qa_q2_grapple_reset(g->options.q2, actor, QA_Q2_LMCTF_GRAPPLE, e);
    qa_q2_grapple_state state;
    if (!qa_q2_grapple_read(g->options.q2, actor, QA_Q2_LMCTF_GRAPPLE, &state, e))
        return false;
    if (state.hook.registry)
        return true;
    double count;
    if (!mode_count(m, actor, hook, &count, e))
        return false;
    if (count <= 0)
        return mode_event(m, v, QA_MODE_MESSAGE, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, 0,
                          QA_Q2_LMCTF_HOOK, e);
    return qa_q2_grapple_offhand(g->options.q2, actor, QA_Q2_LMCTF_GRAPPLE, true, e);
}
bool qa_equipment_q3_pull(qa_equipment *g, qa_actor_id actor, qa_vec3 *velocity, bool *apply,
                          qa_error *e) {
    if (!velocity || !apply)
        return mode_fail(e, "invalid grapple movement output");
    *apply = false;
    equipment_actor *p = equipment_get(g, actor);
    if (!p || p->state.selection.grapple != QA_GRAPPLE_Q3)
        return true;
    qa_q3_grapple_state hook;
    if (!qa_q3_grapple_read(g->options.q3, actor, &hook) || !hook.active)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->options.services.world, actor, &body, e))
        return false;
    qa_vec3 angles = p->state.controls.view_angles;
    float yaw = angles.y * .01745329251994329577f, pitch = angles.x * .01745329251994329577f;
    qa_vec3 forward = {cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch)};
    qa_vec3 delta = qa_vec_sub(qa_vec_sub(hook.point, qa_vec_scale(forward, 16)), body.origin);
    float distance = qa_vec_length(delta);
    *velocity = qa_vec_scale(qa_vec_normalize(delta), distance <= 100 ? distance * 10 : 800);
    *apply = true;
    return true;
}
float qa_equipment_gravity_scale(qa_equipment *g, qa_actor_id actor) {
    equipment_actor *p = equipment_get(g, actor);
    return p && p->state.selection.grapple == QA_GRAPPLE_LMCTF
               ? qa_q2_grapple_gravity_scale(g->options.q2, actor)
               : 1;
}
static bool q3_item_action(void *context, qa_item_id item, qa_item_action action, qa_error *e) {
    equipment_actor *p = context;
    qa_equipment *g = p->equipment;
    if (!equipment_get(g, p->state.actor) || action != QA_ITEM_USE)
        return mode_fail(e, "unsupported Q3 item action");
    size_t count;
    const qa_q3_item *items = qa_q3_items(g->options.q3_product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == QA_Q3_ITEM_HOLDABLE &&
            qa_q3_item_identity(g->options.q3, (uint32_t)i) == item)
            return qa_q3_activate_holdable(g->options.q3, p->state.actor,
                                           (qa_q3_holdable)items[i].tag, false, e);
    if (item == qa_q3_weapon_item(g->options.q3, QA_Q3_W_GRAPPLE, false) &&
        p->state.selection.grapple == QA_GRAPPLE_Q3 &&
        p->state.selection.binding == QA_EQUIPMENT_WEAPON_SLOT)
        return qa_equipment_select_grapple(g, p->state.actor, true, e);
    if (!g->options.select_weapon)
        return mode_fail(e, "Q3 item selection needs the selected arsenal coordinator");
    return g->options.select_weapon(g->options.context, p->state.actor, item, e);
}
bool qa_equipment_publish_q3_items(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !g->options.q3 || !g->options.q3_owner)
        return mode_fail(e, "Q3 inventory publication needs admitted equipment and provider");
    if (p->q3_items.serial &&
        qa_inventory_lease_current(g->options.services.inventory, p->q3_items))
        return true;
    size_t count;
    const qa_q3_item *items = qa_q3_items(g->options.q3_product, &count);
    qa_item_definition definitions[64];
    size_t used = 0;
    for (size_t i = 1; i < count; ++i) {
        qa_item_id item = qa_q3_item_identity(g->options.q3, (uint32_t)i);
        if (!item)
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < used; ++j)
            if (definitions[j].item == item)
                duplicate = true;
        if (duplicate)
            continue;
        if (used == 64)
            return mode_fail(e, "Q3 item publication exceeds descriptor capacity");
        bool weapon = items[i].kind == QA_Q3_ITEM_WEAPON;
        definitions[used++] = (qa_item_definition){
            .item = item,
            .owner = g->options.q3_owner,
            .label = items[i].name,
            .weapon = weapon,
            .actions = weapon || items[i].kind == QA_Q3_ITEM_HOLDABLE ? QA_ITEM_USE : 0,
            .ammo =
                weapon ? qa_q3_weapon_item(g->options.q3, (qa_q3_weapon)items[i].tag, true) : 0};
    }
    return qa_inventory_bind_definitions(g->options.services.inventory, actor, g->options.q3_owner,
                                         definitions, used, q3_item_action, p, &p->q3_items, e);
}
bool qa_equipment_read(qa_equipment *g, qa_actor_id actor, qa_equipment_state *out) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !out)
        return false;
    *out = p->state;
    return true;
}
bool qa_equipment_restore(qa_equipment *g, const qa_equipment_state *state, qa_error *e) {
    equipment_actor *p = state ? equipment_get(g, state->actor) : NULL;
    if (!p || p->configuring || !selection_valid(g, &state->selection) ||
        (state->configuration_pending && !selection_valid(g, &state->pending_selection)) ||
        (state->slot_lowering && state->selection.grapple != QA_GRAPPLE_Q2_CTF &&
         state->selection.grapple != QA_GRAPPLE_LMCTF) ||
        !qa_vec_finite(state->controls.view_angles) ||
        !qa_vec_finite(state->controls.previous_velocity) || !isfinite(state->controls.gravity) ||
        !isfinite(state->controls.view_height) || !isfinite(state->controls.teleport_until) ||
        state->controls.hand < QA_Q2_RIGHT_HAND || state->controls.hand > QA_Q2_CENTER_HAND ||
        state->controls.water_level > 3)
        return mode_fail(e, "invalid equipment restore");
    p->state = *state;
    return true;
}
void qa_equipment_actor_released(qa_equipment *g, qa_actor_record actor) {
    if (!g || actor.id.slot >= g->capacity)
        return;
    equipment_actor *p = &g->actors[actor.id.slot];
    if ((p->active || p->configuring) && qa_actor_id_equal(p->state.actor, actor.id))
        *p = (equipment_actor){0};
}

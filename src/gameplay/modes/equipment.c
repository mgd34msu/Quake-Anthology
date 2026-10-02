#include "qa/equipment.h"
#include "qa/equipment_save.h"
#include "qa/source_save.h"
#include "internal.h"

typedef struct equipment_slot_context {
    qa_equipment *equipment;
    qa_actor_owner owner;
} equipment_slot_context;
typedef struct equipment_actor {
    qa_equipment_state state;
    qa_equipment_source grapple, grenades, items;
    bool active, configuring;
    equipment_slot_context primary_slot, grapple_slot;
} equipment_actor;
struct qa_equipment {
    qa_equipment_options options;
    equipment_actor *actors;
    uint32_t capacity;
    size_t operation_depth;
    qa_weapon_slot **slots;
};
static bool equipment_slot_ensure(qa_equipment *,qa_actor_id,qa_error *);
bool qa_equipment_idle(const qa_equipment *g) {
    if (!g || g->operation_depth)
        return false;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->actors[i].configuring)
            return false;
    for (uint32_t i=0;i<g->capacity;++i)
        if (g->slots[i]&&!qa_weapon_slot_idle(g->slots[i])) return false;
    return !g->options.source_idle || g->options.source_idle(g->options.source_context);
}
static equipment_actor *equipment_get(qa_equipment *g, qa_actor_id actor) {
    if (!g || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor))
        return NULL;
    equipment_actor *p = &g->actors[actor.slot];
    return p->active && qa_actor_id_equal(p->state.actor, actor) ? p : NULL;
}
static bool source_resolve(qa_equipment *g, qa_actor_owner owner,
    qa_equipment_source *out, qa_error *e) {
    *out = (qa_equipment_source){0};
    if (g->options.source) {
        if (!owner) return true;
        if (!g->options.source(g->options.source_context, owner, out, e)) return false;
        if (out->owner != owner) return mode_fail(e, "equipment source owner differs from its retained selection");
    } else {
        if (owner && owner != g->options.q3_owner)
            return mode_fail(e, "equipment has no resolver for its selected provider");
        *out = (qa_equipment_source){.owner = g->options.q3_owner, .q1 = g->options.q1,
            .q2 = g->options.q2, .q3 = g->options.q3, .q3_product = g->options.q3_product};
    }
    if (g->options.source && (!!out->q1 + !!out->q2 + !!out->q3 + !!out->admit > 1))
        return mode_fail(e, "equipment selected provider has conflicting runtime kinds");
    if (out->admit && (out->q1 || out->q2 || out->q3 || !out->current || !out->frame ||
        !out->fire || !out->release || !out->pull || !out->saved_actor))
        return mode_fail(e, "equipment external source lacks its complete retained runtime");
    return !out->current || out->current(out->context) ||
        mode_fail(e, "equipment selected source has retired");
}
static bool selection_valid(qa_equipment *g, const qa_equipment_selection *s,
    const qa_equipment_source *grapple, const qa_equipment_source *grenades) {
    if (!s || s->grapple < QA_GRAPPLE_DISABLED || s->grapple > QA_GRAPPLE_Q3 ||
        s->binding < QA_EQUIPMENT_OFFHAND || s->binding > QA_EQUIPMENT_WEAPON_SLOT ||
        s->grenades.initial_ammo < 0 || s->grenades.capacity < s->grenades.initial_ammo)
        return false;
    if ((s->grapple == QA_GRAPPLE_THREEWAVE || s->grapple == QA_GRAPPLE_ROGUE) && !grapple->q1)
        return false;
    if ((s->grapple == QA_GRAPPLE_Q2_CTF || s->grapple == QA_GRAPPLE_LMCTF) && !grapple->q2)
        return false;
    if (s->grenades.enabled && !grenades->q2) return false;
    if (s->grapple == QA_GRAPPLE_Q3 && !grapple->q3 && !grapple->admit)
        return false;
    if (s->grapple != QA_GRAPPLE_DISABLED && s->binding == QA_EQUIPMENT_WEAPON_SLOT &&
        (!g->options.primary_holster || (!g->options.primary_holstered&&!g->options.primary_holstered_read) ||
         !g->options.primary_resume))
        return false;
    return true;
}
typedef struct equipment_admission {
    qa_q2_hand_grenade_admission q2;
    qa_q2_hand_grenade_admission previous_q2;
    qa_q3_player_binding q3;
    qa_inventory_admission *inventory;
    qa_equipment_source grapple, grenades, items;
} equipment_admission;
static bool admission_abort(qa_equipment *g, equipment_admission *a, qa_error *e) {
    qa_inventory_admission_abort(a->inventory);
    a->inventory = NULL;
    qa_q2_hand_grenade_abort(&a->q2);
    qa_q2_hand_grenade_abort(&a->previous_q2);
    (void)g;
    return !a->q3.token || qa_q3_bind_player_rollback(a->grapple.q3, &a->q3, e);
}
static bool admission_prepare(qa_equipment *g, qa_actor_id actor,
                              const qa_equipment_selection *selection,
                              const qa_equipment_source_selection *sources,
                              equipment_admission *a, qa_error *e) {
    if (!source_resolve(g, sources->grapple, &a->grapple, e) ||
        !source_resolve(g, sources->grenades, &a->grenades, e) ||
        !source_resolve(g, sources->items, &a->items, e) ||
        !selection_valid(g, selection, &a->grapple, &a->grenades))
        return mode_fail(e, "invalid scope-selected equipment providers");
    if (selection->grapple == QA_GRAPPLE_Q3 && a->grapple.admit &&
        !a->grapple.admit(a->grapple.context, actor, e)) return false;
    if (selection->grapple != QA_GRAPPLE_DISABLED && selection->binding == QA_EQUIPMENT_WEAPON_SLOT) {
        qa_item_id weapon = a->grapple.weapon_item;
        switch (selection->grapple) {
        case QA_GRAPPLE_THREEWAVE: case QA_GRAPPLE_ROGUE:
            weapon = qa_q1_weapon_item(a->grapple.q1, selection->grapple == QA_GRAPPLE_THREEWAVE ?
                QA_Q1_CTF_GRAPPLE : QA_Q1_ROGUE_GRAPPLE);
            break;
        case QA_GRAPPLE_Q2_CTF: case QA_GRAPPLE_LMCTF: {
            const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(a->grapple.q2,
                selection->grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_GRAPPLE : QA_Q2_LMCTF_HOOK);
            if (!definition) return mode_fail(e, "gear slot has no actual native Q2 weapon declaration");
            if (!qa_builtin_resource(&g->options.services, definition->item, &weapon, e)) return false;
            break;
        }
        case QA_GRAPPLE_Q3:
            if (a->grapple.q3) weapon = qa_q3_weapon_item(a->grapple.q3, QA_Q3_W_GRAPPLE, false);
            break;
        case QA_GRAPPLE_DISABLED: break;
        }
        if (!weapon) return mode_fail(e, "gear slot has no canonical weapon identity");
        if (!qa_actors_get(qa_session_actors(g->options.services.session), actor) ||
            (a->grapple.current && !a->grapple.current(a->grapple.context)))
            return mode_fail(e, "gear slot identity retired its actor or selected source");
        qa_inventory *inventory = g->options.services.inventory;
        qa_inventory_entry entry = {.item = weapon, .count = 1,
            .capacity = 1, .policy = QA_COUNT_STACK};
        qa_inventory_admission *grant = NULL;
        bool okay = qa_inventory_has(inventory, actor) ||
            qa_inventory_create_actor(inventory, actor, NULL, 0, e);
        if (okay) okay = qa_inventory_prepare_entries(inventory, actor, &entry, 1, &grant, e) &&
            qa_inventory_admission_validate(grant, e);
        if (okay) {
            okay = qa_inventory_admission_commit(grant, e);
            if (okay) grant = NULL;
        }
        qa_inventory_admission_abort(grant);
        /* The real operation dispatcher must observe the slot allowance,
         * including an already admitted primary source item. */
        if (okay) okay = qa_inventory_configure(inventory, actor, &entry, NULL, NULL, e);
        if (!okay) return false;
        if (!qa_actors_get(qa_session_actors(g->options.services.session), actor) ||
            (a->grapple.current && !a->grapple.current(a->grapple.context)))
            return mode_fail(e, "gear slot allowance retired its actor or selected source");
    }
    if (selection->grapple == QA_GRAPPLE_Q3 && a->grapple.q3 &&
        !qa_q3_bind_player_begin(a->grapple.q3, actor, QA_Q3_EQUIPMENT, 100, &a->q3, e))
        return false;
    if (a->grenades.q2 &&
        (!qa_q2_hand_grenade_prepare(a->grenades.q2, actor, &selection->grenades, &a->q2, e) ||
         !qa_inventory_prepare_entries(g->options.services.inventory, actor,
                                       &a->q2.initial_ammo, 1, &a->inventory, e)))
        return false;
    equipment_actor *previous = equipment_get(g, actor);
    if (previous && previous->grenades.q2 && previous->grenades.q2 != a->grenades.q2 &&
        !qa_q2_hand_grenade_prepare(previous->grenades.q2, actor,
            &(qa_q2_hand_grenade_options){0}, &a->previous_q2, e)) return false;
    return true;
}
static bool admission_commit(qa_equipment *g, qa_actor_id actor, equipment_admission *a,
                             qa_error *e) {
    /* Inventory validation is the last operation allowed to call source code.
     * All remaining checks and commits use the retained native records only. */
    if ((a->inventory && !qa_inventory_admission_validate(a->inventory, e)) ||
        (a->q2.active && !qa_q2_hand_grenade_validate(&a->q2, e)) ||
        (a->previous_q2.active && !qa_q2_hand_grenade_validate(&a->previous_q2, e)) ||
        (a->q3.token && !qa_q3_bind_player_validate(a->grapple.q3, &a->q3, e)))
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
    if (a->previous_q2.active && !qa_q2_hand_grenade_commit(&a->previous_q2, e)) return false;
    return !a->q3.token || qa_q3_bind_player_commit(a->grapple.q3, &a->q3, e);
}
bool qa_equipment_create(const qa_equipment_options *options, qa_equipment **out, qa_error *e) {
    if (!options || !out || !qa_builtin_services_validate(&options->services, e))
        return mode_fail(e, "invalid equipment services");
    if (options->source && (!options->source_context || !options->source_idle ||
        !options->source_destroy || !options->source_capture || !options->source_restore))
        return mode_fail(e, "selected equipment source roster lacks its complete lifetime and codec");
    qa_equipment *g = calloc(1, sizeof(*g));
    if (!g) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating equipment");
        return false;
    }
    g->options = *options;
    g->capacity = qa_actors_capacity(qa_session_actors(options->services.session));
    g->actors = calloc(g->capacity, sizeof(*g->actors));
    g->slots = calloc(g->capacity, sizeof(*g->slots));
    if (!g->actors || !g->slots) {
        free(g->actors); free(g->slots);
        free(g);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating equipment players");
        return false;
    }
    *out = g;
    return true;
}
void qa_equipment_destroy(qa_equipment *g) {
    (void)qa_equipment_destroy_checked(g, NULL);
}
bool qa_equipment_destroy_checked(qa_equipment *g, qa_error *e) {
    if (!g)
        return true;
    if (!qa_equipment_idle(g)) return mode_fail(e, "equipment destruction retains a source operation");
    for(uint32_t i=0;i<g->capacity;++i)
        if(!qa_weapon_slot_destroy(&g->slots[i],e)) return false;
    if (g->options.source_destroy && !g->options.source_destroy(g->options.source_context, e)) return false;
    free(g->actors);
    free(g->slots);
    free(g);
    return true;
}
static bool release_grapple(qa_equipment *g, qa_actor_id actor, bool force, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return true;
    p->state.grapple_pressed = false;
    p->state.grapple_released = false;
    if (force) {
        p->state.controls.grapple_held = false;
        p->state.controls.prediction = false;
    }
    switch (p->state.selection.grapple) {
    case QA_GRAPPLE_THREEWAVE:
    case QA_GRAPPLE_ROGUE:
        return qa_q1_grapple_release(p->grapple.q1, actor, e);
    case QA_GRAPPLE_Q2_CTF:
        return qa_q2_grapple_reset(p->grapple.q2, actor, QA_Q2_CTF_GRAPPLE, e);
    case QA_GRAPPLE_LMCTF:
        return qa_q2_grapple_reset(p->grapple.q2, actor, QA_Q2_LMCTF_GRAPPLE, e);
    case QA_GRAPPLE_Q3:
        return p->grapple.release ? p->grapple.release(p->grapple.context, actor, force, e) :
            qa_q3_release_grapple(p->grapple.q3, actor, e);
    case QA_GRAPPLE_DISABLED:
        return true;
    }
    return mode_fail(e, "invalid grapple mechanic");
}
static bool equipment_release_grapple(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    return release_grapple(g, actor, true, e);
}
static bool equipment_admit(qa_equipment *g, qa_actor_id actor, const qa_equipment_selection *selection,
                        const qa_equipment_source_selection *sources,
                        qa_error *e) {
    if (!g || !selection || !sources || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor) ||
        g->actors[actor.slot].configuring)
        return mode_fail(e, "invalid equipment admission");
    if (equipment_get(g, actor))
        return qa_equipment_configure_sources(g, actor, selection, sources, e);
    qa_equipment_selection wanted = *selection;
    qa_equipment_source_selection selected_sources = *sources;
    selection = &wanted;
    sources = &selected_sources;
    equipment_actor *p = &g->actors[actor.slot];
    equipment_actor before = *p;
    *p = (equipment_actor){.configuring = true, .state.actor = actor};
    equipment_admission admission = {0};
    bool ok = admission_prepare(g, actor, selection, sources, &admission, e) &&
              admission_commit(g, actor, &admission, e);
    qa_error cleanup = {0};
    if (!admission_abort(g, &admission, &cleanup)) {
        if (e) *e = cleanup;
        ok = false;
    }
    if (qa_actor_id_equal(p->state.actor, actor)) {
        if (ok)
            *p = (equipment_actor){.active = true,
                .grapple = admission.grapple, .grenades = admission.grenades, .items = admission.items,
                .state = {.actor = actor, .selection = *selection, .sources = *sources}};
        else
            *p = before;
    }
    return ok;
}
static bool equipment_configure(qa_equipment *g, qa_actor_id actor,
                            const qa_equipment_selection *selection,
                            const qa_equipment_source_selection *sources, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !selection || !sources || p->configuring)
        return mode_fail(e, "invalid equipment configuration");
    qa_equipment_selection wanted = *selection;
    qa_equipment_source_selection selected_sources = *sources;
    selection = &wanted;
    sources = &selected_sources;
    p->configuring = true;
    equipment_admission admission = {0};
    bool ok = false;
    if (!admission_prepare(g, actor, selection, sources, &admission, e))
        goto finished;
    if (p->state.selection.grapple != selection->grapple ||
        p->state.selection.binding != selection->binding ||
        p->state.sources.grapple != sources->grapple) {
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
            p->state.pending_sources = *sources;
            p->state.configuration_pending = true;
            ok = true;
            goto finished;
        }
    }
    if (!admission_commit(g, actor, &admission, e))
        goto finished;
    p->state.configuration_pending = false;
    p->state.selection = *selection;
    p->state.sources = *sources;
    p->grapple = admission.grapple; p->grenades = admission.grenades; p->items = admission.items;
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
static bool equipment_respawn(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || p->configuring) return mode_fail(e, "equipment respawn requires its admitted actor");
    qa_equipment_selection wanted = p->state.configuration_pending ? p->state.pending_selection : p->state.selection;
    qa_equipment_source_selection sources = p->state.configuration_pending ? p->state.pending_sources : p->state.sources;
    bool resume = p->state.slot_requested || p->state.slot_active || p->state.slot_holstering || p->state.slot_lowering;
    if (resume && !g->options.primary_resume) return mode_fail(e, "equipment respawn has no primary slot owner");
    p->configuring = true;
    bool okay = release_grapple(g, actor, true, e);
    p = equipment_get(g, actor);
    if (okay && p && resume) okay = g->options.primary_resume(g->options.context, actor, e);
    p = equipment_get(g, actor);
    if (!p) return okay;
    p->configuring = false;
    if (!okay) return false;
    p->state.slot_requested = p->state.slot_active = p->state.slot_holstering = p->state.slot_lowering = false;
    p->state.primary_request_owner=0; p->state.primary_request_item=0;
    if (!equipment_configure(g, actor, &wanted, &sources, e)) return false;
    p = equipment_get(g, actor);
    if (!p) return true;
    p->configuring = true;
    if (p->state.selection.binding == QA_EQUIPMENT_WEAPON_SLOT) {
        if (p->state.selection.grapple == QA_GRAPPLE_THREEWAVE)
            okay = qa_q1_grapple_weapon_holster(p->grapple.q1, actor, e);
        else if (p->state.selection.grapple == QA_GRAPPLE_Q2_CTF || p->state.selection.grapple == QA_GRAPPLE_LMCTF)
            okay = qa_q2_grapple_equipment_reset(p->grapple.q2, actor,
                p->state.selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE, e);
    }
    p = equipment_get(g, actor);
    if (!p) return okay;
    if (okay && p->grenades.q2) {
        qa_q2_game *game = p->grenades.q2;
        qa_q2_hand_grenade_state grenade; bool bound = false;
        okay = qa_q2_hand_grenade_read(game, actor, &grenade, &bound, e);
        if (okay && !bound) okay = mode_fail(e, "equipment respawn lost its admitted grenade source");
        if (okay && grenade.options.enabled) {
            qa_q2_hand_grenade_admission admission = {0};
            okay = qa_q2_hand_grenade_prepare(game, actor, &grenade.options, &admission, e);
            qa_item_id ammo = admission.initial_ammo.item;
            qa_q2_hand_grenade_abort(&admission);
            qa_inventory_entry entry;
            if (okay) okay = qa_inventory_entry_read(g->options.services.inventory, actor, ammo, &entry, e);
            p = equipment_get(g, actor);
            if (okay && p) {
                entry.count = fmax(entry.count, grenade.options.initial_ammo);
                entry.capacity = fmax(entry.capacity, grenade.options.capacity);
                okay = qa_inventory_configure(g->options.services.inventory, actor, &entry, NULL, NULL, e);
            }
        }
        p = equipment_get(g, actor);
        if (okay && p) {
            okay = qa_q2_hand_grenade_read(game, actor, &grenade, &bound, e);
            if (okay && !bound) okay = mode_fail(e, "equipment respawn lost its reached grenade source");
            if (okay) {
                grenade.action = (qa_q2_hand_action){.kind = QA_Q2_HAND_IDLE};
                okay = qa_q2_hand_grenade_restore(game, actor, &grenade, e);
            }
        }
    }
    p = equipment_get(g, actor);
    if (p) {
        if (okay) {
            p->state.controls = (qa_equipment_controls){0};
            p->state.grapple_pressed = p->state.grapple_released = false;
            p->state.grenade_pressed = p->state.grenade_released = false;
            p->state.previous_jump = p->state.teleport_seen = false;
            p->state.teleport_sequence = 0;
        }
        p->configuring = false;
    }
    return okay;
}
static bool equipment_input(qa_equipment *g, qa_actor_id actor, const qa_equipment_controls *input,
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
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state;
        s->teleport_seen = true;
        s->teleport_sequence = input->teleport_sequence;
    }
    if (!input->prediction && input->jump && !s->previous_jump && s->selection.release_on_jump &&
        !qa_equipment_release_grapple(g, actor, e))
        return false;
    p = equipment_get(g, actor);
    if (!p) return true;
    s = &p->state;
    s->previous_jump = input->jump;
    s->controls = *input;
    return true;
}
static bool equipment_select_grapple(qa_equipment *, qa_actor_id, bool, qa_error *);
static bool primary_holstered_read(qa_equipment *g,qa_actor_id actor,bool *out,qa_error *e) {
    if(g->options.primary_holstered_read)
        return g->options.primary_holstered_read(g->options.context,actor,out,e);
    *out=g->options.primary_holstered(g->options.context,actor); return true;
}
static bool resume_primary(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    equipment_actor *p=equipment_get(g,actor);
    if(!p) return true;
    qa_actor_owner owner=p->state.primary_request_owner;
    qa_item_id item=p->state.primary_request_item;
    if(!g->options.primary_resume(g->options.context,actor,e)) return false;
    p=equipment_get(g,actor);
    if(!p||!item||p->state.primary_request_owner!=owner||p->state.primary_request_item!=item) return true;
    bool accepted=false;
    if(!g->options.primary_select||!g->options.primary_select(g->options.context,actor,owner,item,&accepted,e)) return false;
    p=equipment_get(g,actor);
    if(!p||p->state.primary_request_owner!=owner||p->state.primary_request_item!=item) return true;
    p->state.primary_request_owner=0; p->state.primary_request_item=0;
    return accepted||equipment_select_grapple(g,actor,true,e);
}
static bool equipment_select_grapple(qa_equipment *g, qa_actor_id actor, bool selected, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return mode_fail(e, "unknown equipment player");
    qa_equipment_state *s = &p->state;
    if (selected && (s->selection.grapple == QA_GRAPPLE_DISABLED ||
                     s->selection.binding != QA_EQUIPMENT_WEAPON_SLOT))
        return mode_fail(e, "grapple has no selected weapon slot");
    s->slot_requested = selected;
    if(selected) { s->primary_request_owner=0; s->primary_request_item=0; }
    if (selected && !s->slot_active && !s->slot_holstering && !s->slot_lowering) {
        s->slot_holstering = true;
        if (!g->options.primary_holster(g->options.context, actor, e))
            return false;
    } else if (!selected && (s->slot_active || s->slot_holstering)) {
        if (!s->selection.retain_on_weapon_change && !qa_equipment_release_grapple(g, actor, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state;
        if (s->slot_active && (s->selection.grapple == QA_GRAPPLE_Q2_CTF ||
                               s->selection.grapple == QA_GRAPPLE_LMCTF)) {
            qa_q2_grapple_kind kind =
                s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
            if (!qa_q2_grapple_equipment_holster(p->grapple.q2, actor, kind, e))
                return false;
            p = equipment_get(g, actor);
            if (!p) return true;
            s = &p->state;
            s->slot_lowering = true;
        }
        if (s->slot_active && s->selection.grapple == QA_GRAPPLE_THREEWAVE &&
            !qa_q1_grapple_weapon_holster(p->grapple.q1, actor, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state;
        s->slot_active = false;
        /* Primary resume is deferred until its pending holster has completed. */
        if (!s->slot_holstering && !s->slot_lowering)
            return resume_primary(g, actor, e);
    }
    return true;
}
static bool equipment_request_primary(qa_equipment *g,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e) {
    equipment_actor *p=equipment_get(g,actor);
    if(!p||!owner||!item||!accepted||!g->options.primary_accepts||!g->options.primary_select)
        return mode_fail(e,"primary weapon request has no actual admitted source");
    *accepted=false;
    if(!g->options.primary_accepts(g->options.context,actor,owner,item,accepted,e)) return false;
    if(!*accepted) return true;
    p=equipment_get(g,actor);
    if(!p) { *accepted=false; return true; }
    if(!p->state.slot_active&&!p->state.slot_holstering&&!p->state.slot_lowering)
        return g->options.primary_select(g->options.context,actor,owner,item,accepted,e);
    p->state.primary_request_owner=owner; p->state.primary_request_item=item;
    return equipment_select_grapple(g,actor,false,e);
}
static bool equipment_reconcile(qa_equipment *g,qa_actor_id actor,qa_error *e) {
    equipment_actor *p=equipment_get(g,actor);
    if(!p) return true;
    if(p->state.slot_lowering) {
        qa_q2_grapple_kind kind=p->state.selection.grapple==QA_GRAPPLE_Q2_CTF?QA_Q2_CTF_GRAPPLE:QA_Q2_LMCTF_GRAPPLE;
        qa_q2_grapple_state reached;
        if(!qa_q2_grapple_read(p->grapple.q2,actor,kind,&reached,e)) return false;
        if(reached.equipment.handoff!=QA_Q2_PRIMARY_HOLSTERED) return true;
        p=equipment_get(g,actor); if(!p) return true;
        p->state.slot_lowering=false;
        if(!p->state.slot_requested) return resume_primary(g,actor,e);
        p->state.slot_holstering=true;
    }
    bool holstered=false;
    if(!p->state.slot_holstering) return true;
    if(!primary_holstered_read(g,actor,&holstered,e)) return false;
    if(!holstered) return true;
    p=equipment_get(g,actor); if(!p) return true;
    p->state.slot_holstering=false; p->state.slot_active=p->state.slot_requested;
    if(!p->state.slot_requested) return resume_primary(g,actor,e);
    if(p->state.selection.grapple==QA_GRAPPLE_Q2_CTF||p->state.selection.grapple==QA_GRAPPLE_LMCTF)
        return qa_q2_grapple_equipment_resume(p->grapple.q2,actor,
            p->state.selection.grapple==QA_GRAPPLE_Q2_CTF?QA_Q2_CTF_GRAPPLE:QA_Q2_LMCTF_GRAPPLE,e);
    if(p->state.selection.grapple==QA_GRAPPLE_THREEWAVE)
        return qa_q1_grapple_weapon_resume(p->grapple.q1,actor,e);
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
typedef struct grenade_interval_scope {
    qa_equipment *equipment;
    qa_q2_game *game;
    qa_actor_owner owner;
} grenade_interval_scope;
static bool grenade_interval(void *context, qa_actor_id actor, uint64_t native,
    uint64_t *out, bool *handled, qa_error *e) {
    grenade_interval_scope *scope = context;
    qa_equipment *g = scope->equipment;
    equipment_actor *p = equipment_get(g, actor);
    if (!p || p->grenades.q2 != scope->game || p->state.sources.grenades != scope->owner)
        return mode_fail(e, "grenade cadence lost its actual equipment source");
    *handled = false;
    if (g->options.grenade_interval && !g->options.grenade_interval(g->options.context,
        actor, scope->owner, native, out, handled, e)) return false;
    p = equipment_get(g, actor);
    return (p && p->grenades.q2 == scope->game && p->state.sources.grenades == scope->owner) ||
        mode_fail(e, "grenade cadence replaced its equipment action owner");
}
static bool equipment_step(qa_equipment *g, qa_actor_id actor, uint64_t now, uint64_t elapsed,
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
    if (p->grapple.frame && !p->grapple.frame(p->grapple.context, now, elapsed, e)) return false;
    p = equipment_get(g, actor);
    if (!p) return true;
    s = &p->state; c = &s->controls;
    if (p->grenades.q2) {
        grenade_interval_scope cadence = {g, p->grenades.q2, s->sources.grenades};
        qa_q2_hand_grenade_input grenade = {.weapon = q2_input(s),
                                            .pressed = grenade_pressed,
                                            .held = c->grenade_held,
                                            .released = grenade_released,
                                            .lifecycle = lifecycle,
                                            .project_context = g->options.context,
                                            .project = g->options.grenade_projection,
                                            .interval_context = &cadence,
                                            .interval = grenade_interval};
        if (!qa_q2_hand_grenade_step(p->grenades.q2, actor, &grenade, now, elapsed, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state; c = &s->controls;
    }
    if (lifecycle != QA_Q2_HAND_ALIVE || c->spectator) {
        s->slot_requested = s->slot_active = s->slot_holstering = s->slot_lowering = false;
        s->primary_request_owner=0; s->primary_request_item=0;
        if (s->configuration_pending) {
            qa_equipment_selection selection = s->pending_selection;
            qa_equipment_source_selection sources = s->pending_sources;
            if (!qa_equipment_configure_sources(g, actor, &selection, &sources, e))
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
        if (!qa_q2_grapple_equipment_tick(p->grapple.q2, actor, kind, &input, now, elapsed, e) ||
            !qa_q2_grapple_read(p->grapple.q2, actor, kind, &native, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state; c = &s->controls;
        if (native.equipment.handoff != QA_Q2_PRIMARY_HOLSTERED)
            return true;
        s->slot_lowering = false;
        if (s->configuration_pending) {
            qa_equipment_selection selection = s->pending_selection;
            qa_equipment_source_selection sources = s->pending_sources;
            if (!qa_equipment_configure_sources(g, actor, &selection, &sources, e))
                return false;
            p = equipment_get(g, actor);
            if (!p) return true;
            s = &p->state; c = &s->controls;
        }
        if (!s->slot_requested)
            return resume_primary(g, actor, e);
        s->slot_holstering = true;
    }
    bool holstered = false;
    if(s->slot_holstering&&!primary_holstered_read(g,actor,&holstered,e)) return false;
    p = equipment_get(g, actor);
    if (!p) return true;
    s = &p->state; c = &s->controls;
    if (holstered) {
        s->slot_holstering = false;
        s->slot_active = s->slot_requested;
        if (!s->slot_requested) {
            if (s->configuration_pending) {
                qa_equipment_selection selection = s->pending_selection;
                qa_equipment_source_selection sources = s->pending_sources;
                if (!qa_equipment_configure_sources(g, actor, &selection, &sources, e))
                    return false;
            }
            if (!equipment_get(g, actor)) return true;
            return resume_primary(g, actor, e);
        }
        if (s->selection.grapple == QA_GRAPPLE_Q2_CTF || s->selection.grapple == QA_GRAPPLE_LMCTF) {
            qa_q2_grapple_kind kind =
                s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
            if (!qa_q2_grapple_equipment_resume(p->grapple.q2, actor, kind, e))
                return false;
            p = equipment_get(g, actor);
            if (!p) return true;
            s = &p->state; c = &s->controls;
        }
        if (s->selection.grapple == QA_GRAPPLE_THREEWAVE &&
            !qa_q1_grapple_weapon_resume(p->grapple.q1, actor, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state; c = &s->controls;
    }
    bool slot = s->selection.binding == QA_EQUIPMENT_WEAPON_SLOT;
    bool enabled = s->selection.grapple != QA_GRAPPLE_DISABLED && (!slot || s->slot_active);
    if (!enabled)
        return s->selection.retain_on_weapon_change ? true
                                                    : qa_equipment_release_grapple(g, actor, e);
    if (released)
        return s->selection.grapple == QA_GRAPPLE_Q3 && p->grapple.release
            ? release_grapple(g, actor, false, e) : qa_equipment_release_grapple(g, actor, e);
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
        if (!qa_q1_grapple_input(p->grapple.q1, actor, &input, !enabled, e))
            return false;
        p = equipment_get(g, actor);
        if (!p) return true;
        s = &p->state;
        if (slot && s->selection.grapple == QA_GRAPPLE_THREEWAVE)
            return qa_q1_grapple_weapon_tick(p->grapple.q1, actor, &input, true, e);
        return !pressed ||
               qa_q1_grapple_fire(p->grapple.q1, actor,
                                  s->selection.grapple == QA_GRAPPLE_THREEWAVE, &input, e);
    }
    case QA_GRAPPLE_Q2_CTF:
    case QA_GRAPPLE_LMCTF: {
        qa_q2_grapple_kind kind =
            s->selection.grapple == QA_GRAPPLE_Q2_CTF ? QA_Q2_CTF_GRAPPLE : QA_Q2_LMCTF_GRAPPLE;
        if (slot) {
            qa_q2_weapon_input input = q2_input(s);
            input.latched_attack = pressed;
            return qa_q2_grapple_equipment_tick(p->grapple.q2, actor, kind, &input, now, elapsed,
                                                e);
        }
        return !pressed || qa_q2_grapple_offhand(p->grapple.q2, actor, kind, true, e);
    }
    case QA_GRAPPLE_Q3:
        if (p->grapple.fire)
            return !pressed || p->grapple.fire(p->grapple.context, actor, c, e);
        if (!qa_q3_player_set_view(p->grapple.q3, actor, c->view_angles, c->view_height, e))
            return false;
        return !pressed || qa_q3_fire_weapon(p->grapple.q3, actor, QA_Q3_W_GRAPPLE, e);
    }
    return mode_fail(e, "invalid grapple mechanic");
}
static bool equipment_after_movement(qa_equipment *g, qa_actor_id actor, bool pulse, uint64_t now,
                                 uint64_t elapsed, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p)
        return true;
    qa_grapple_mechanic kind = p->state.selection.grapple;
    return (kind != QA_GRAPPLE_Q2_CTF && kind != QA_GRAPPLE_LMCTF) ||
           qa_q2_grapple_after_movement(p->grapple.q2, actor, pulse, now, elapsed, e);
}
static bool equipment_lmctf_command(qa_equipment *g, qa_modes *m, qa_mode_id id, qa_actor_id actor,
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
    if (!p->grapple.q2)
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
    if (qa_q2_weapon_read(p->grapple.q2, actor, &weapon, &local))
        selected |= weapon.weapon == QA_Q2_LMCTF_HOOK;
    else if (local.code != QA_ERROR_NOT_FOUND) {
        if (e)
            *e = local;
        return false;
    }
    if (selected)
        return qa_q2_grapple_hold(p->grapple.q2, actor, pressed, e);
    if (!pressed)
        return qa_q2_grapple_reset(p->grapple.q2, actor, QA_Q2_LMCTF_GRAPPLE, e);
    qa_q2_grapple_state state;
    if (!qa_q2_grapple_read(p->grapple.q2, actor, QA_Q2_LMCTF_GRAPPLE, &state, e))
        return false;
    if (state.hook.registry)
        return true;
    double count;
    if (!mode_count(m, actor, hook, &count, e))
        return false;
    if (count <= 0)
        return mode_event(m, v, QA_MODE_MESSAGE, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, 0,
                          QA_Q2_LMCTF_HOOK, e);
    return qa_q2_grapple_offhand(p->grapple.q2, actor, QA_Q2_LMCTF_GRAPPLE, true, e);
}
static bool equipment_q3_pull(qa_equipment *g, qa_actor_id actor, qa_vec3 *velocity, bool *apply,
                          qa_error *e) {
    if (!velocity || !apply)
        return mode_fail(e, "invalid grapple movement output");
    *apply = false;
    equipment_actor *p = equipment_get(g, actor);
    if (!p || p->state.selection.grapple != QA_GRAPPLE_Q3)
        return true;
    qa_vec3 angles = p->state.controls.view_angles;
    float yaw = angles.y * .01745329251994329577f, pitch = angles.x * .01745329251994329577f;
    qa_vec3 forward = {cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch)};
    if (p->grapple.pull) return p->grapple.pull(p->grapple.context, actor, forward, velocity, apply, e);
    qa_q3_grapple_state hook;
    if (!qa_q3_grapple_read(p->grapple.q3, actor, &hook) || !hook.active)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->options.services.world, actor, &body, e))
        return false;
    qa_vec3 delta = qa_vec_sub(qa_vec_sub(hook.point, qa_vec_scale(forward, 16)), body.origin);
    float distance = qa_vec_length(delta);
    *velocity = qa_vec_scale(qa_vec_normalize(delta), distance <= 100 ? distance * 10 : 800);
    *apply = true;
    return true;
}
float qa_equipment_gravity_scale(qa_equipment *g, qa_actor_id actor) {
    equipment_actor *p = equipment_get(g, actor);
    return p && p->state.selection.grapple == QA_GRAPPLE_LMCTF
               ? qa_q2_grapple_gravity_scale(p->grapple.q2, actor)
               : 1;
}
static bool equipment_publish_q3_items(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !p->items.q3 || !p->items.owner)
        return mode_fail(e, "Q3 inventory publication needs admitted equipment and provider");
    qa_equipment_source selected;
    if (!source_resolve(g, p->state.sources.items, &selected, e) ||
        selected.owner != p->items.owner || selected.q3 != p->items.q3)
        return mode_fail(e, "Q3 equipment publication lost its actual source tuple");
    qa_q3_game *game = p->items.q3;
    qa_actor_owner owner = p->items.owner;
    if (!qa_q3_bind_player(game, actor, QA_Q3_EQUIPMENT, 100, e) ||
        !qa_q3_inventory_admit(game, actor, e)) return false;
    p = equipment_get(g, actor);
    return (p && p->items.q3 == game && p->items.owner == owner &&
        qa_q3_inventory_equipment_current(game, actor, owner)) ||
        mode_fail(e, "Q3 equipment publication lost its native inventory owner");
}
bool qa_equipment_read(qa_equipment *g, qa_actor_id actor, qa_equipment_state *out) {
    equipment_actor *p = equipment_get(g, actor);
    if (!p || !out)
        return false;
    *out = p->state;
    return true;
}
bool qa_equipment_weapon_view_read(qa_equipment *g, qa_actor_id actor,
    qa_equipment_weapon_view *out, bool *found, qa_error *e) {
    if (!g || !out || !found || g->operation_depth ||
        !qa_actors_get(qa_session_actors(g->options.services.session), actor))
        return mode_fail(e, "equipment weapon view requires its returned full actor");
    equipment_actor *p = equipment_get(g, actor);
    if (p && p->configuring) return mode_fail(e, "equipment weapon view retains an admission");
    if (!p || p->state.selection.grapple == QA_GRAPPLE_DISABLED ||
        p->state.selection.binding != QA_EQUIPMENT_WEAPON_SLOT) {
        *found = false; return true;
    }
    qa_equipment_weapon_view value = {.actor = actor, .source = p->grapple,
        .mechanic = p->state.selection.grapple, .item = p->grapple.weapon_item,
        .label = p->state.selection.grapple == QA_GRAPPLE_LMCTF ? "Hook" : "Grapple",
        .active = p->state.slot_active};
    switch (value.mechanic) {
    case QA_GRAPPLE_THREEWAVE: case QA_GRAPPLE_ROGUE:
        value.item = qa_q1_weapon_item(p->grapple.q1, value.mechanic == QA_GRAPPLE_THREEWAVE ?
            QA_Q1_CTF_GRAPPLE : QA_Q1_ROGUE_GRAPPLE);
        break;
    case QA_GRAPPLE_Q2_CTF: case QA_GRAPPLE_LMCTF: {
        const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(p->grapple.q2,
            value.mechanic == QA_GRAPPLE_Q2_CTF ? QA_Q2_GRAPPLE : QA_Q2_LMCTF_HOOK);
        if (!definition) return mode_fail(e, "equipment weapon view lost its source definition");
        value.item = qa_strings_find(qa_session_strings(g->options.services.session),
            (qa_bytes){(const uint8_t *)definition->item, strlen(definition->item)});
        break;
    }
    case QA_GRAPPLE_Q3:
        if (p->grapple.q3) value.item = qa_q3_weapon_item(p->grapple.q3, QA_Q3_W_GRAPPLE, false);
        break;
    case QA_GRAPPLE_DISABLED: break;
    }
    if (!value.item) return mode_fail(e, "equipment weapon view lost its admitted canonical item");
    *out = value; *found = true; return true;
}
bool qa_equipment_weapon_view_current(qa_equipment *g, const qa_equipment_weapon_view *view) {
    qa_equipment_weapon_view actual; bool found;
    return view && qa_equipment_weapon_view_read(g, view->actor, &actual, &found, NULL) && found &&
        actual.item == view->item && actual.mechanic == view->mechanic && actual.active == view->active &&
        actual.label == view->label && actual.source.weapon_item == view->source.weapon_item &&
        actual.source.q3_product == view->source.q3_product &&
        actual.source.owner == view->source.owner && actual.source.q1 == view->source.q1 &&
        actual.source.q2 == view->source.q2 && actual.source.q3 == view->source.q3 &&
        actual.source.context == view->source.context && actual.source.current == view->source.current;
}
static bool state_valid(qa_equipment *g, const qa_equipment_state *state, qa_error *e) {
    qa_equipment_source grapple, grenades, items;
    if (!g || !state || !source_resolve(g, state->sources.grapple, &grapple, e) ||
        !source_resolve(g, state->sources.grenades, &grenades, e) ||
        !source_resolve(g, state->sources.items, &items, e) ||
        !selection_valid(g, &state->selection, &grapple, &grenades) ||
        (state->slot_lowering && state->selection.grapple != QA_GRAPPLE_Q2_CTF &&
         state->selection.grapple != QA_GRAPPLE_LMCTF) ||
        !qa_vec_finite(state->controls.view_angles) ||
        !qa_vec_finite(state->controls.previous_velocity) || !isfinite(state->controls.gravity) ||
        !isfinite(state->controls.view_height) || !isfinite(state->controls.teleport_until) ||
        state->controls.hand < QA_Q2_RIGHT_HAND || state->controls.hand > QA_Q2_CENTER_HAND ||
        state->controls.water_level > 3 ||
        (!!state->primary_request_owner!=!!state->primary_request_item) ||
        (state->primary_request_item&&(state->slot_requested||
            (!state->slot_holstering&&!state->slot_lowering)||!g->options.primary_select||!g->options.primary_accepts)))
        return mode_fail(e, "invalid equipment restore");
    if (state->configuration_pending &&
        (!source_resolve(g, state->pending_sources.grapple, &grapple, e) ||
         !source_resolve(g, state->pending_sources.grenades, &grenades, e) ||
         !source_resolve(g, state->pending_sources.items, &items, e) ||
         !selection_valid(g, &state->pending_selection, &grapple, &grenades)))
        return mode_fail(e, "invalid pending equipment source selection");
    return true;
}
bool qa_equipment_restore(qa_equipment *g, const qa_equipment_state *state, qa_error *e) {
    equipment_actor *p = state ? equipment_get(g, state->actor) : NULL;
    if (!p || p->configuring || !state_valid(g, state, e))
        return mode_fail(e, "invalid equipment restore");
    if (p->state.sources.grapple != state->sources.grapple ||
        p->state.sources.grenades != state->sources.grenades || p->state.sources.items != state->sources.items)
        return mode_fail(e, "equipment state restore cannot change its admitted source owners");
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

#define EQUIP_FIELD(kind, value) do { if (!qa_source_save_##kind(io, &(value))) return false; } while (0)
#define EQUIP_ENUM(value, last) do { \
    uint32_t encoded = (uint32_t)(value); \
    EQUIP_FIELD(u32, encoded); \
    if (encoded > (uint32_t)(last)) return mode_fail(io->error, "invalid equipment save enum"); \
    if (io->direction == QA_SOURCE_SAVE_READ) (value) = encoded; \
} while (0)

static bool save_selection(qa_source_save_io *io, qa_equipment_selection *p) {
    EQUIP_ENUM(p->grapple, QA_GRAPPLE_Q3); EQUIP_ENUM(p->binding, QA_EQUIPMENT_WEAPON_SLOT);
    EQUIP_FIELD(bool, p->retain_on_weapon_change); EQUIP_FIELD(bool, p->release_on_jump);
    EQUIP_FIELD(bool, p->release_on_teleport); EQUIP_FIELD(bool, p->grenades.enabled);
    EQUIP_FIELD(bool, p->grenades.infinite_ammo);
    int32_t ammo = p->grenades.initial_ammo, capacity = p->grenades.capacity;
    EQUIP_FIELD(i32, ammo); EQUIP_FIELD(i32, capacity);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        p->grenades.initial_ammo = ammo; p->grenades.capacity = capacity;
    }
    return true;
}
static bool save_state(qa_source_save_io *io, qa_equipment_state *p) {
    EQUIP_FIELD(actor, p->actor);
    EQUIP_FIELD(string, p->sources.grapple); EQUIP_FIELD(string, p->sources.grenades);
    EQUIP_FIELD(string, p->sources.items); EQUIP_FIELD(string, p->pending_sources.grapple);
    EQUIP_FIELD(string, p->pending_sources.grenades); EQUIP_FIELD(string, p->pending_sources.items);
    if (!save_selection(io, &p->selection) || !save_selection(io, &p->pending_selection)) return false;
    qa_equipment_controls *c = &p->controls;
    EQUIP_FIELD(vec3, c->view_angles); EQUIP_FIELD(vec3, c->previous_velocity);
    EQUIP_FIELD(f32, c->view_height); EQUIP_FIELD(f32, c->gravity); EQUIP_FIELD(f32, c->teleport_until);
    EQUIP_FIELD(bool, c->grapple_held); EQUIP_FIELD(bool, c->grenade_held); EQUIP_FIELD(bool, c->jump);
    EQUIP_FIELD(bool, c->primary_attack); EQUIP_FIELD(bool, c->spectator); EQUIP_FIELD(bool, c->prediction);
    EQUIP_FIELD(bool, c->haste); EQUIP_FIELD(bool, c->no_stack_double); EQUIP_FIELD(bool, c->players_collide);
    EQUIP_FIELD(bool, c->teleport_known); EQUIP_FIELD(u32, c->teleport_sequence);
    EQUIP_ENUM(c->hand, QA_Q2_CENTER_HAND); EQUIP_FIELD(u8, c->water_level); EQUIP_FIELD(i32, c->water_type);
    EQUIP_FIELD(u64, c->quad_until_ns); EQUIP_FIELD(u64, c->double_until_ns); EQUIP_FIELD(u64, c->quad_fire_until_ns);
    EQUIP_FIELD(bool, p->grapple_pressed); EQUIP_FIELD(bool, p->grapple_released);
    EQUIP_FIELD(bool, p->grenade_pressed); EQUIP_FIELD(bool, p->grenade_released);
    EQUIP_FIELD(bool, p->slot_requested); EQUIP_FIELD(bool, p->slot_active); EQUIP_FIELD(bool, p->slot_holstering);
    EQUIP_FIELD(bool, p->slot_lowering); EQUIP_FIELD(bool, p->configuration_pending);
    EQUIP_FIELD(bool, p->previous_jump); EQUIP_FIELD(bool, p->teleport_seen); EQUIP_FIELD(u32, p->teleport_sequence);
    EQUIP_FIELD(string, p->primary_request_owner); EQUIP_FIELD(string, p->primary_request_item);
    return true;
}
static bool save_header(qa_source_save_io *io) {
    uint8_t signature[8] = {'Q', 'A', 'E', 'Q', 'U', 'I', 'P', 0};
    static const uint8_t expected[8] = {'Q', 'A', 'E', 'Q', 'U', 'I', 'P', 0};
    uint32_t version = 4;
    if (!qa_source_save_bytes(io, signature, sizeof(signature)) ||
        memcmp(signature, expected, sizeof(signature))) return mode_fail(io->error, "invalid equipment save signature");
    EQUIP_FIELD(u32, version);
    return version == 4 || mode_fail(io->error, "unsupported equipment save version");
}
static bool save_boundary(qa_equipment *g, bool empty, qa_error *e) {
    if (!qa_equipment_idle(g) || !qa_session_safe(g->options.services.session) || !qa_world_idle(g->options.services.world))
        return mode_fail(e, "equipment save requires an idle service");
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->actors[i].configuring || (empty && g->actors[i].active))
            return mode_fail(e, "equipment save conflicts with admission or restore state");
    return true;
}
static bool save_native_state(qa_equipment *g, const qa_equipment_state *p, qa_error *e) {
    qa_equipment_source grapple, grenades, items;
    if (!source_resolve(g, p->sources.grapple, &grapple, e) ||
        !source_resolve(g, p->sources.grenades, &grenades, e) ||
        !source_resolve(g, p->sources.items, &items, e)) return false;
    if (items.q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(items.q3, p->actor, &player) ||
            !(player.selections & QA_Q3_EQUIPMENT))
            return mode_fail(e, "equipment save lost its actual native item source");
    }
    if (p->selection.grapple == QA_GRAPPLE_Q3) {
        qa_q3_player_state player;
        if (grapple.saved_actor) {
            if (!grapple.saved_actor(grapple.context, p->actor, e)) return false;
        } else if (!qa_q3_player_read(grapple.q3, p->actor, &player) ||
            !(player.selections & QA_Q3_EQUIPMENT))
            return mode_fail(e, "equipment save has no native Q3 equipment owner");
    }
    if (grenades.q2) {
        qa_q2_hand_grenade_state grenade;
        bool bound = false;
        if (!qa_q2_hand_grenade_read(grenades.q2, p->actor, &grenade, &bound, e))
            return false;
        const qa_q2_hand_grenade_options *native = &grenade.options,
                                        *selected = &p->selection.grenades;
        if (!bound || native->enabled != selected->enabled ||
            native->infinite_ammo != selected->infinite_ammo ||
            native->initial_ammo != selected->initial_ammo || native->capacity != selected->capacity)
            return mode_fail(e, "equipment save differs from native grenade continuation");
    }
    return true;
}
bool qa_equipment_reconnect(qa_equipment *g, qa_error *e) {
    if (!save_boundary(g, false, e)) return false;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        equipment_actor *actor = &g->actors[i];
        if (!actor->active) continue;
        if (!equipment_get(g, actor->state.actor) || !state_valid(g, &actor->state, e) ||
            !save_native_state(g, &actor->state, e))
            return mode_fail(e, "saved equipment row lost its actual selected providers");
        if (actor->items.q3 && !qa_q3_inventory_equipment_current(actor->items.q3,
                actor->state.actor, actor->items.owner))
            return mode_fail(e, "saved equipment source inventory is not connected");
    }
    return true;
}
bool qa_equipment_capture(qa_equipment *g, qa_buffer *out, qa_error *e) {
    if (!out) return mode_fail(e, "equipment capture requires output");
    if (!qa_equipment_reconnect(g, e)) return false;
    qa_buffer source = {0};
    if (g->options.source_capture && !g->options.source_capture(g->options.source_context, &source, e)) {
        qa_buffer_free(&source);
        return false;
    }
    qa_source_save_io storage = {0}, *io = &storage;
    size_t count = 0;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (equipment_get(g, g->actors[i].state.actor)) ++count;
    bool has_sources = g->options.source != NULL;
    size_t source_size = source.size;
    bool okay = qa_source_save_writer(io, g->options.services.session, e) && save_header(io) &&
        qa_source_save_bool(io, &has_sources) && qa_source_save_count(io, &source_size, SIZE_MAX) &&
        qa_source_save_bytes(io, source.data, source_size) && qa_source_save_count(io, &count, g->capacity);
    for (uint32_t i = 0; okay && i < g->capacity; ++i) {
        equipment_actor *actor = equipment_get(g, g->actors[i].state.actor);
        if (!actor) continue;
        qa_equipment_state state = actor->state;
        okay = state_valid(g, &state, e) && save_native_state(g, &state, e) && save_state(io, &state);
    }
    if (okay) okay = qa_source_save_finish(io, out);
    qa_source_save_dispose(io);
    qa_buffer_free(&source);
    return okay;
}
bool qa_equipment_restore_bytes(qa_equipment *g, qa_bytes input, qa_error *e) {
    if (!save_boundary(g, true, e)) return false;
    equipment_actor *candidate = calloc(g->capacity, sizeof(*candidate));
    if (!candidate) { qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating equipment restore"); return false; }
    qa_source_save_io storage = {0}, *io = &storage;
    size_t count = 0;
    bool has_sources = false; size_t source_size = 0; qa_bytes source = {0};
    bool okay = qa_source_save_reader(io, g->options.services.session, input, e) && save_header(io) &&
        qa_source_save_bool(io, &has_sources) && has_sources == (g->options.source != NULL) &&
        qa_source_save_count(io, &source_size, input.size);
    if (okay && (io->offset > input.size || source_size > input.size - io->offset)) okay = false;
    if (!okay && (!e || e->code == QA_OK))
        mode_fail(e, "equipment source save envelope differs from its retained roster");
    if (okay) {
        source = (qa_bytes){input.data + io->offset, source_size}; io->offset += source_size;
        okay = has_sources ? g->options.source_restore(g->options.source_context, source, e) : !source_size;
    }
    if (okay) okay = qa_source_save_count(io, &count, g->capacity);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_equipment_state state = {0};
        okay = save_state(io, &state) && state_valid(g, &state, e) && save_native_state(g, &state, e);
        if (!okay) break;
        if (state.actor.slot >= g->capacity || candidate[state.actor.slot].active ||
            !qa_actors_get(qa_session_actors(g->options.services.session), state.actor)) {
            okay = mode_fail(e, "duplicate or stale equipment save actor"); break;
        }
        candidate[state.actor.slot] = (equipment_actor){.state = state, .active = true};
        equipment_actor *row = &candidate[state.actor.slot];
        okay = source_resolve(g, state.sources.grapple, &row->grapple, e) &&
            source_resolve(g, state.sources.grenades, &row->grenades, e) &&
            source_resolve(g, state.sources.items, &row->items, e);
    }
    if (okay) okay = qa_source_save_finish(io, NULL);
    qa_source_save_dispose(io);
    if (okay) { free(g->actors); g->actors = candidate; }
    else free(candidate);
    return okay;
}

#undef EQUIP_FIELD
#undef EQUIP_ENUM

#define EQUIPMENT_CALL(call) do { \
    if (!g) return (call); \
    if (g->operation_depth == SIZE_MAX) return mode_fail(e, "equipment operation depth exhausted"); \
    ++g->operation_depth; \
    bool result = (call); \
    --g->operation_depth; \
    return result; \
} while (0)

bool qa_equipment_release_grapple(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    EQUIPMENT_CALL(equipment_release_grapple(g, actor, e));
}
bool qa_equipment_admit(qa_equipment *g, qa_actor_id actor,
                        const qa_equipment_selection *selection, qa_error *e) {
    qa_equipment_source_selection sources = {0};
    EQUIPMENT_CALL(equipment_admit(g, actor, selection, &sources, e));
}
bool qa_equipment_admit_sources(qa_equipment *g, qa_actor_id actor,
    const qa_equipment_selection *selection, const qa_equipment_source_selection *sources, qa_error *e) {
    EQUIPMENT_CALL(equipment_admit(g, actor, selection, sources, e));
}
bool qa_equipment_configure(qa_equipment *g, qa_actor_id actor,
                            const qa_equipment_selection *selection, qa_error *e) {
    equipment_actor *p = equipment_get(g, actor);
    qa_equipment_source_selection sources = p ? p->state.sources : (qa_equipment_source_selection){0};
    EQUIPMENT_CALL(equipment_configure(g, actor, selection, &sources, e));
}
bool qa_equipment_configure_sources(qa_equipment *g, qa_actor_id actor,
    const qa_equipment_selection *selection, const qa_equipment_source_selection *sources, qa_error *e) {
    EQUIPMENT_CALL(equipment_configure(g, actor, selection, sources, e));
}
bool qa_equipment_respawn(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    EQUIPMENT_CALL(equipment_respawn(g, actor, e));
}
bool qa_equipment_input(qa_equipment *g, qa_actor_id actor,
                        const qa_equipment_controls *input, qa_error *e) {
    EQUIPMENT_CALL(equipment_input(g, actor, input, e));
}
bool qa_equipment_select_grapple(qa_equipment *g, qa_actor_id actor, bool selected, qa_error *e) {
    EQUIPMENT_CALL(equipment_select_grapple(g, actor, selected, e));
}
bool qa_equipment_request_primary(qa_equipment *g,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e) {
    EQUIPMENT_CALL(equipment_request_primary(g,actor,owner,item,accepted,e));
}
bool qa_equipment_reconcile(qa_equipment *g,qa_actor_id actor,qa_error *e) {
    EQUIPMENT_CALL(equipment_reconcile(g,actor,e));
}
bool qa_equipment_step(qa_equipment *g, qa_actor_id actor, uint64_t now, uint64_t elapsed,
                       qa_q2_hand_lifecycle lifecycle, qa_error *e) {
    EQUIPMENT_CALL(equipment_step(g, actor, now, elapsed, lifecycle, e));
}
bool qa_equipment_after_movement(qa_equipment *g, qa_actor_id actor, bool pulse,
                                 uint64_t now, uint64_t elapsed, qa_error *e) {
    EQUIPMENT_CALL(equipment_after_movement(g, actor, pulse, now, elapsed, e));
}
bool qa_equipment_lmctf_command(qa_equipment *g, qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                bool native_slot, bool pressed, qa_error *e) {
    EQUIPMENT_CALL(equipment_lmctf_command(g, m, id, actor, native_slot, pressed, e));
}
bool qa_equipment_q3_pull(qa_equipment *g, qa_actor_id actor, qa_vec3 *velocity, bool *apply,
                          qa_error *e) {
    EQUIPMENT_CALL(equipment_q3_pull(g, actor, velocity, apply, e));
}
bool qa_equipment_publish_q3_items(qa_equipment *g, qa_actor_id actor, qa_error *e) {
    EQUIPMENT_CALL(equipment_publish_q3_items(g, actor, e));
}
#undef EQUIPMENT_CALL

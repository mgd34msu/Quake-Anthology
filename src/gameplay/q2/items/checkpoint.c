#include "../entities/checkpoint_internal.h"
#include "internal.h"

void qa_q2_item_checkpoint_free(qa_q2_item_checkpoint *s) {
    if (!s)
        return;
    free(s->picked_slots);
    *s = (qa_q2_item_checkpoint){0};
}
bool qa_q2_item_capture(qa_q2_game *g, qa_actor_id id, qa_q2_item_checkpoint *out, qa_error *e) {
    if (!g || !out || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item checkpoint actor");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    qa_q2_item_checkpoint s = {.version = 1};
    if (!a) {
        *out = s;
        return true;
    }
    if (a->item) {
        q2_item_state *v = a->item;
        if (v->dispatching || (v->temporary && v->think != Q2_ITEM_DROPPED)) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 item checkpoint requires a completed pickup");
            return false;
        }
        s.present = true;
        s.definition = v->definition ? v->definition->classname_id : 0;
        s.spawn = v->spawn;
        s.spawn.classname = NULL;
        s.spawn.team_master = s.spawn.team_next = (qa_actor_id){0};
        s.due_ns = v->due_ns;
        s.expires_ns = v->expires_ns;
        s.think = (uint32_t)v->think;
        s.targets_used = v->targets_used;
        s.retained = v->retained;
        s.visible = v->visible;
        s.touchable = v->touchable;
        s.temporary = v->temporary;
        s.visual = v->visual;
        s.picked_count = v->picked_count;
        void *copy;
        if (!q2_saved_array(v->picked_slots, v->picked_count, sizeof(*v->picked_slots), &copy, e))
            return false;
        s.picked_slots = copy;
        if (!q2_save_reference(g, v->owner, &s.owner, e) ||
            !q2_save_reference(g, v->spawn.team_master, &s.team_master, e) ||
            !q2_save_reference(g, v->spawn.team_next, &s.team_next, e))
            goto fail;
        if (v->companion) {
            q2_companion *c = v->companion;
            s.companion = (qa_q2_companion_checkpoint){.kind = (uint32_t)c->kind,
                                                       .expires_ns = c->expires_ns,
                                                       .attack_ns = c->attack_ns,
                                                       .next_ns = c->next_ns,
                                                       .turn_ns = c->turn_ns,
                                                       .goal = c->goal,
                                                       .frame = c->frame,
                                                       .loop_sound = c->loop_sound,
                                                       .active = c->active,
                                                       .decoy = c->decoy,
                                                       .camera = c->camera};
            if (!q2_save_reference(g, c->owner, &s.companion.owner, e) ||
                !q2_save_reference(g, c->enemy, &s.companion.enemy, e) ||
                !q2_save_reference(g, c->child, &s.companion.child, e) ||
                !q2_save_reference(g, c->credit, &s.companion.credit, e))
                goto fail;
        }
    }
    if (a->powers) {
        q2_power_state *p = a->powers;
        s.powers_present = true;
        s.powers = p->values;
        s.maximum_health = p->maximum_health;
        s.power_cubes = p->power_cubes;
        s.definitions_bound = qa_inventory_lease_current(g->services.inventory, p->definitions);
        qa_inventory *inventory;
        qa_item_id item;
        s.power_inventory_bound =
            qa_combat_power_inventory(g->services.combat, id, &inventory, &item) &&
            inventory == g->services.inventory && item == p->cells;
        if (!q2_save_reference(g, p->sphere, &s.sphere, e))
            goto fail;
    }
    *out = s;
    return true;
fail:
    qa_q2_item_checkpoint_free(&s);
    return false;
}
static bool valid_item(qa_q2_game *g, const qa_q2_item_checkpoint *s, qa_error *e) {
    if ((s->temporary && s->think != Q2_ITEM_DROPPED) || s->think > Q2_ITEM_MEGA ||
        s->spawn.classname || s->spawn.team_master.registry || s->spawn.team_next.registry ||
        !isfinite(s->spawn.delay) || !q2_saved_visual(g, &s->visual) ||
        !q2_saved_resource(g, s->definition) || !q2_saved_resource(g, s->spawn.target) ||
        !q2_saved_resource(g, s->spawn.killtarget) || !q2_saved_resource(g, s->spawn.message) ||
        !q2_saved_resource(g, s->spawn.team) || s->companion.kind > Q2_DOPPLEGANGER_BODY ||
        !qa_vec_finite(s->companion.goal) || !q2_saved_resource(g, s->companion.loop_sound) ||
        (!s->definition && s->companion.kind == Q2_COMPANION_NONE) ||
        (s->picked_count && !s->picked_slots) || s->picked_count > SIZE_MAX / sizeof(uint32_t))
        return false;
    for (size_t i = 0; i < s->picked_count; i++)
        for (size_t j = 0; j < i; j++)
            if (s->picked_slots[i] == s->picked_slots[j]) {
                qa_error_set(e, QA_ERROR_FORMAT, i, "Duplicate Q2 instanced pickup slot");
                return false;
            }
    return true;
}
bool qa_q2_item_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_item_checkpoint *s,
                        qa_error *e) {
    if (!g || !s || s->version != 1 || !q2_actor_live(g, id) ||
        (s->present && !valid_item(g, s, e)) ||
        (s->powers_present && (!isfinite(s->maximum_health) || s->maximum_health <= 0)) ||
        (!s->powers_present && (s->definitions_bound || s->power_inventory_bound))) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 item checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    bool has_state = s->present || s->powers_present;
    q2_actor *a = q2_actor_get(g, id, has_state, has_state ? e : NULL);
    if (!a)
        return !s->present && !s->powers_present;
    if (a->item || a->powers) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Q2 item restore requires an empty candidate extension");
        return false;
    }
    q2_actor candidate = {.id = id};
    if (s->present) {
        candidate.item = calloc(1, sizeof(*candidate.item));
        if (!candidate.item)
            goto memory;
        q2_item_state *v = candidate.item;
        if (s->definition) {
            v->definition = qa_q2_item_lookup(
                g, qa_strings_cstr(qa_session_strings(g->services.session), s->definition));
            if (!v->definition) {
                qa_error_set(e, QA_ERROR_FORMAT, 0, "Unknown Q2 checkpoint item definition");
                goto fail;
            }
        }
        v->spawn = s->spawn;
        v->spawn.classname = v->definition ? v->definition->classname : NULL;
        v->due_ns = s->due_ns;
        v->expires_ns = s->expires_ns;
        v->think = (q2_item_think)s->think;
        v->targets_used = s->targets_used;
        v->retained = s->retained;
        v->visible = s->visible;
        v->touchable = s->touchable;
        v->temporary = s->temporary;
        v->visual = s->visual;
        v->picked_count = v->picked_capacity = s->picked_count;
        void *copy;
        if (!q2_saved_array(s->picked_slots, s->picked_count, sizeof(*s->picked_slots), &copy, e))
            goto fail;
        v->picked_slots = copy;
        if (!q2_resolve_reference(g, s->owner, &v->owner, e) ||
            !q2_resolve_reference(g, s->team_master, &v->spawn.team_master, e) ||
            !q2_resolve_reference(g, s->team_next, &v->spawn.team_next, e))
            goto fail;
        if (s->companion.kind != Q2_COMPANION_NONE) {
            v->companion = calloc(1, sizeof(*v->companion));
            if (!v->companion)
                goto memory;
            const qa_q2_companion_checkpoint *saved = &s->companion;
            q2_companion *c = v->companion;
            *c = (q2_companion){.kind = (q2_companion_kind)saved->kind,
                                .expires_ns = saved->expires_ns,
                                .attack_ns = saved->attack_ns,
                                .next_ns = saved->next_ns,
                                .turn_ns = saved->turn_ns,
                                .goal = saved->goal,
                                .frame = saved->frame,
                                .loop_sound = saved->loop_sound,
                                .active = saved->active,
                                .decoy = saved->decoy,
                                .camera = saved->camera};
            if (!q2_resolve_reference(g, saved->owner, &c->owner, e) ||
                !q2_resolve_reference(g, saved->enemy, &c->enemy, e) ||
                !q2_resolve_reference(g, saved->child, &c->child, e) ||
                !q2_resolve_reference(g, saved->credit, &c->credit, e))
                goto fail;
        }
    }
    if (s->powers_present) {
        candidate.powers = calloc(1, sizeof(*candidate.powers));
        if (!candidate.powers)
            goto memory;
        q2_power_state *p = candidate.powers;
        const qa_q2_item_definition *cells = qa_q2_item_lookup(g, "ammo_cells");
        *p = (q2_power_state){.game = g,
                              .actor = id,
                              .values = s->powers,
                              .cells = cells ? cells->item : 0,
                              .maximum_health = s->maximum_health,
                              .power_cubes = s->power_cubes};
        if (!q2_resolve_reference(g, s->sphere, &p->sphere, e))
            goto fail;
        if (s->definitions_bound && !q2_item_bind_actions(g, id, p, e))
            goto fail;
        if (s->power_inventory_bound &&
            !qa_combat_bind_power_inventory(g->services.combat, id, g->services.inventory, p->cells,
                                            e))
            goto fail;
    }
    a->item = candidate.item;
    a->powers = candidate.powers;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring Q2 item state");
fail:
    q2_items_release_state(&candidate);
    return false;
}
bool qa_q2_items_capture(qa_q2_game *g, qa_q2_items_checkpoint *out, qa_error *e) {
    if (!g || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 items checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    *out = (qa_q2_items_checkpoint){.version = 1, .cubes = g->item_runtime->cubes};
    return true;
}
bool qa_q2_items_restore(qa_q2_game *g, const qa_q2_items_checkpoint *s, qa_error *e) {
    if (!g || !s || s->version != 1) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 items checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    g->item_runtime->cubes = s->cubes;
    return true;
}

#include "internal.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q2_combat.h"

q2_power_state *q2_powers(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        return NULL;
    if (!a->powers) {
        a->powers = calloc(1, sizeof(*a->powers));
        if (!a->powers) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 item powers");
            return NULL;
        }
        a->powers->game = g;
        a->powers->actor = id;
        a->powers->maximum_health = 100;
        const qa_q2_item_definition *cells = qa_q2_item_lookup(g, "ammo_cells");
        a->powers->cells = cells ? cells->item : 0;
    }
    return a->powers;
}
bool qa_q2_powerups_read(qa_q2_game *g, qa_actor_id id, qa_q2_powerups *out, qa_error *e) {
    if (!g || !out || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 powerup actor");
        return false;
    }
    q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    *out = a && qa_actor_id_equal(a->id, id) && a->powers ? a->powers->values : (qa_q2_powerups){0};
    return true;
}
bool qa_q2_timed_invulnerability(qa_q2_game *g, qa_actor_id id) {
    q2_actor *actor = g ? q2_actor_get(g, id, false, NULL) : NULL;
    return actor && ((actor->powers &&
                       actor->powers->values.invulnerability_until_ns > g->now_ns) ||
                      q2_monster_timed_invulnerability(actor, g->now_ns));
}
bool qa_q2_powerups_present(qa_q2_game *g, qa_actor_id id) {
    q2_actor *actor = g ? q2_actor_get(g, id, false, NULL) : NULL;
    return actor && actor->powers;
}
bool qa_q2_powerups_clear(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_power_state *p = q2_powers(g, id, e);
    if (!p)
        return false;
    p->values = (qa_q2_powerups){0};
    return true;
}
static bool item_action(void *context, qa_item_id item, qa_item_action action, qa_error *e) {
    q2_power_state *p = context;
    bool accepted;
    if (action == QA_ITEM_USE)
        return qa_q2_item_use(p->game, p->actor, item, &accepted, e);
    qa_actor_id dropped;
    return qa_q2_item_drop(p->game, p->actor, item, &(qa_q2_drop_options){0}, &dropped, &accepted,
                           e);
}
bool qa_q2_game_inventory_group(qa_q2_game *g, qa_actor_id id, uint64_t saved_serial,
                                 const qa_inventory_source_group *saved,
                                 qa_inventory_items *out, qa_error *e) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!out || !a || !a->powers || !saved || !saved_serial ||
        saved->owner != g->options.owner || !saved->definitions_only ||
        saved->count != g->item_runtime->action_count || (saved->count && !saved->items) ||
        !qa_actor_id_equal(a->powers->definitions.actor, id) ||
        a->powers->definitions.serial != saved_serial) {
        qa_error_set(e, QA_ERROR_FORMAT, id.slot, "Q2 inventory source declaration has no owner");
        return false;
    }
    for (size_t i = 0; i < saved->count; ++i) {
        const qa_item_admission *native = &g->item_runtime->admissions[i];
        const qa_item_admission *entry = &saved->items[i];
        const qa_item_definition *definition = &native->definition, *b = &entry->definition;
        if (native->replace_primary != entry->replace_primary || definition->item != b->item ||
            definition->ammo != b->ammo || definition->owner != b->owner || definition->weapon != b->weapon ||
            definition->actions != b->actions || (!!definition->label != !!b->label) ||
            (definition->label && strcmp(definition->label, b->label))) {
            qa_error_set(e, QA_ERROR_FORMAT, id.slot, "Q2 saved inventory catalog differs from source");
            return false;
        }
    }
    a->powers->definitions = (qa_inventory_lease){.actor = id, .serial = saved_serial};
    *out = (qa_inventory_items){.owner = saved->owner,
                                .items = g->item_runtime->admissions,
                                .count = g->item_runtime->action_count,
                                .action_context = a->powers,
                                .invoke = item_action};
    return true;
}
bool q2_item_ensure(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d, qa_error *e) {
    if (!qa_inventory_has(g->services.inventory, id) &&
        !qa_inventory_create_actor(g->services.inventory, id, NULL, 0, e))
        return false;
    qa_inventory_entry entry;
    qa_error missing = {0};
    if (qa_inventory_entry_read(g->services.inventory, id, d->item, &entry, &missing))
        return true;
    if (missing.code != QA_ERROR_NOT_FOUND) {
        if (e)
            *e = missing;
        return false;
    }
    return qa_inventory_configure(
        g->services.inventory, id,
        &(qa_inventory_entry){
            .item = d->item, .count = 0, .capacity = d->capacity, .policy = QA_COUNT_SOURCE_INT32},
        NULL, NULL, e);
}
bool qa_q2_items_admit_player(qa_q2_game *g, qa_actor_id id, bool give_blaster, qa_error *e) {
    q2_power_state *p = q2_powers(g, id, e);
    if (!p)
        return false;
    for (size_t i = 0; i < g->item_runtime->count; ++i) {
        const qa_q2_item_definition *d = &g->item_runtime->definitions[i];
        if (d->kind == QA_Q2_ITEM_AMMO || d->kind == QA_Q2_ITEM_WEAPON ||
            d->kind == QA_Q2_ITEM_POWER || d->kind == QA_Q2_ITEM_POWER_ARMOR ||
            d->kind == QA_Q2_ITEM_KEY || d->kind >= QA_Q2_ITEM_SPHERE) {
            if (!q2_item_ensure(g, id, d, e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
        }
    }
    if (!qa_combat_bind_power_inventory(g->services.combat, id, g->services.inventory, p->cells, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (give_blaster) {
        int count;
        if (!q2_count(g, id, g->items[QA_Q2_BLASTER], &count, e))
            return false;
        double given;
        if (!count &&
            !qa_inventory_give(g->services.inventory, id, g->items[QA_Q2_BLASTER], 1, &given, e))
            return false;
    }
    return !q2_actor_live(g, id) || q2_item_bind_actions(g, id, p, e);
}
bool q2_item_bind_actions(qa_q2_game *g, qa_actor_id id, q2_power_state *p, qa_error *e) {
    return qa_inventory_lease_current(g->services.inventory, p->definitions) ||
           qa_inventory_bind_definitions(g->services.inventory, id, g->options.owner,
                                         g->item_runtime->actions, g->item_runtime->action_count,
                                         item_action, p, &p->definitions, e);
}
static uint64_t *timer(q2_power_state *p, qa_q2_powerup kind) {
    switch (kind) {
    case QA_Q2_POWER_QUAD:
        return &p->values.quad_until_ns;
    case QA_Q2_POWER_INVULNERABILITY:
        return &p->values.invulnerability_until_ns;
    case QA_Q2_POWER_BREATHER:
        return &p->values.breather_until_ns;
    case QA_Q2_POWER_ENVIRO:
        return &p->values.enviro_until_ns;
    case QA_Q2_POWER_QUADFIRE:
        return &p->values.quad_fire_until_ns;
    case QA_Q2_POWER_DOUBLE:
        return &p->values.double_until_ns;
    case QA_Q2_POWER_IR:
        return &p->values.ir_until_ns;
    case QA_Q2_POWER_INVISIBILITY:
        return &p->values.invisibility_until_ns;
    default:
        return NULL;
    }
}
typedef struct item_use_call {
    qa_q2_game *game;
    const qa_q2_item_definition *definition;
    uint64_t duration;
    bool *used;
} item_use_call;

static bool item_use_duration(void *context, qa_actor_id id, qa_error *e) {
    item_use_call *call = context;
    qa_q2_game *g = call->game;
    const qa_q2_item_definition *d = call->definition;
    uint64_t duration = call->duration;
    bool *used = call->used;
    *used = false;
    int count;
    if (!q2_count(g, id, d->item, &count, e))
        return false;
    if (count <= 0)
        return true;
    if (d->weapon != QA_Q2_WEAPON_NONE) {
        qa_q2_selection selected;
        if (!qa_q2_weapon_select(g, id, d->weapon, false, &selected, e))
            return false;
        *used = selected == QA_Q2_SELECTED || selected == QA_Q2_CURRENT;
        return true;
    }
    q2_power_state *p = q2_powers(g, id, e);
    if (!p)
        return false;
    if (d->kind == QA_Q2_ITEM_POWER_ARMOR) {
        qa_combat_state state;
        if (!qa_combat_read(g->services.combat, id, &state, e))
            return false;
        int cells;
        if (!q2_count(g, id, p->cells, &cells, e))
            return false;
        bool active = state.armor.powered.kind != QA_POWER_NONE;
        if (!active && cells == 0)
            return true;
        qa_powered_armor armor = {.kind = active ? QA_POWER_NONE : d->powered_armor,
                                  .cells = (float)cells};
        qa_q2_combat_power_armor_source(g, &armor);
        if (!qa_combat_set_powered_armor(g->services.combat, id, &armor, e))
            return false;
        *used = true;
        return !q2_actor_live(g, id) ||
               q2_item_sound(g, id, active ? "misc/power2.wav" : "misc/power1.wav", e);
    }
    if (d->kind == QA_Q2_ITEM_SPHERE || d->kind == QA_Q2_ITEM_DECOY)
        return q2_companion_use(g, id, d, used, e);
    if (d->kind == QA_Q2_ITEM_FLASHLIGHT || d->kind == QA_Q2_ITEM_COMPASS)
        return q2_client_item_action(g, id, d->kind == QA_Q2_ITEM_FLASHLIGHT, used, e);
    if (d->kind == QA_Q2_ITEM_NUKE) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        bool consumed;
        if (!qa_inventory_consume(g->services.inventory, id, d->item, 1, &consumed, e))
            return false;
        if (!consumed || !q2_actor_live(g, id))
            return true;
        q2_actor *a = q2_actor_get(g, id, false, e);
        if (!a)
            return false;
        qa_vec3 forward;
        qa_builtin_angle_vectors(a->weapon_bound ? a->input.angles : body.angles, &forward, NULL,
                                 NULL);
        float multiplier = p->values.quad_until_ns > g->now_ns ? 4 : 1;
        if (p->values.double_until_ns > g->now_ns && !(multiplier > 1 && a->input.no_stack_double))
            multiplier *= 2;
        *used = true;
        return q2_fire_nuke(g, id, body.origin, forward, 100, multiplier, e);
    }
    if (d->kind != QA_Q2_ITEM_POWER)
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, id, d->item, 1, &consumed, e))
        return false;
    if (!consumed || !q2_actor_live(g, id))
        return true;
    *used = true;
    if (d->powerup == QA_Q2_POWER_SILENCER)
        return qa_q2_weapon_silencer(g, id, 30, e);
    uint64_t *until = timer(p, d->powerup);
    if (until)
        *until = q2_deadline(*until > g->now_ns ? *until : g->now_ns, duration);
    const char *sound = d->powerup == QA_Q2_POWER_QUAD           ? "items/damage.wav"
                        : d->powerup == QA_Q2_POWER_QUADFIRE     ? "items/quadfire1.wav"
                        : d->powerup == QA_Q2_POWER_DOUBLE       ? "misc/ddamage1.wav"
                        : d->powerup == QA_Q2_POWER_IR           ? "misc/ir_start.wav"
                        : d->powerup == QA_Q2_POWER_INVISIBILITY ? "items/protect.wav"
                                                                 : NULL;
    return !sound || q2_item_sound(g, id, sound, e);
}
bool q2_item_use_duration(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d,
                          uint64_t duration, bool *used, qa_error *e) {
    if (!g || !d || !used) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item activation");
        return false;
    }
    item_use_call call = {.game = g, .definition = d, .duration = duration, .used = used};
    return qa_q2_run_actor(g, id, item_use_duration, &call, e);
}
bool qa_q2_item_use(qa_q2_game *g, qa_actor_id id, qa_item_id item, bool *used, qa_error *e) {
    if (!g || !used || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item use");
        return false;
    }
    const qa_q2_item_definition *d = q2_item_by_id(g, item);
    if (!d) {
        *used = false;
        return true;
    }
    uint64_t duration = d->powerup == QA_Q2_POWER_IR ? 60 * Q2_NS : 30 * Q2_NS;
    return q2_item_use_duration(g, id, d, duration, used, e);
}

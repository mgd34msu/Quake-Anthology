#include "internal.h"

bool qa_q1_game_clone(qa_q1_game *g, qa_actor_id actor, qa_actor_id *out, qa_error *error) {
    q1_actor *source = g ? q1_entity(g, actor) : NULL;
    if (!source || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "native Q1 clone requires a live source");
        return false;
    }
    q1_actor *target;
    const char *classname =
        qa_strings_cstr(qa_session_strings(g->services.session), source->classname);
    if (!q1_create(g, classname, source->kind, source->owner, &target, error))
        return false;
    qa_actor_id id = target->id;
    source = q1_entity(g, actor);
    if (!source) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot,
                     "native Q1 clone source retired during allocation");
        goto fail;
    }
    q1_actor *allocation = target->allocation_next;
    *target = *source;
    target->id = id;
    target->allocation_next = allocation;
    target->pool_next = NULL;
    target->active = true;
    target->map = NULL;
    if (source->map && !q1_map_clone(g, source, target, error))
        goto fail;
    if (!q1_map_bind_target(g, target, error))
        goto fail;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(g->services.world, actor, &body, error) ||
        !qa_combat_read_traits(g->services.combat, actor, &combat, error) ||
        !qa_world_body_write(g->services.world, id, &body, error) ||
        !qa_combat_set_health(g->services.combat, id, combat.health, error) ||
        !qa_combat_set_armor(g->services.combat, id, &combat.armor, error) ||
        !qa_combat_set_traits(g->services.combat, id, &combat, error))
        goto fail;
    if (qa_inventory_has(g->services.inventory, actor)) {
        size_t count;
        if (!qa_inventory_entries(g->services.inventory, actor, NULL, 0, &count, error))
            goto fail;
        if (count > SIZE_MAX / sizeof(qa_inventory_entry)) {
            qa_error_set(error, QA_ERROR_MEMORY, count, "native Q1 clone inventory overflow");
            goto fail;
        }
        qa_inventory_entry *entries = count ? malloc(count * sizeof(*entries)) : NULL;
        if (count && !entries) {
            qa_error_set(error, QA_ERROR_MEMORY, count,
                         "native Q1 clone inventory allocation failed");
            goto fail;
        }
        bool ok =
            qa_inventory_entries(g->services.inventory, actor, entries, count, &count, error) &&
            qa_inventory_create_actor(g->services.inventory, id, entries, count, error);
        free(entries);
        if (!ok)
            goto fail;
    }
    qa_inventory *power_inventory;
    qa_item_id power_item;
    if (qa_combat_power_inventory(g->services.combat, actor, &power_inventory, &power_item) &&
        !qa_combat_bind_power_inventory(g->services.combat, id, power_inventory, power_item, error))
        goto fail;
    target = q1_entity(g, id);
    if (!target) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, id.slot,
                     "native Q1 clone retired during shared-state copying");
        goto fail;
    }
    if (target->physics.motion != QA_PHYSICS_PUSH && target->think != Q1_THINK_NONE &&
        target->next_think >= 0 &&
        !q1_schedule(g, target, target->next_think - g->time, target->think, error))
        goto fail;
    *out = id;
    return true;
fail:
    (void)qa_session_release(g->services.session, id, NULL);
    return false;
}

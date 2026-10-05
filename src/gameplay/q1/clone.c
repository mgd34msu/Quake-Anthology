#include "internal.h"
#include "qa/game_q1_source_birth.h"

const char *q1_body_queue_classname(const qa_q1_game *g) {
    return g->options.edition == QA_Q1_RERELEASE &&
        (g->options.program == QA_Q1_ID1 || g->options.program == QA_Q1_CTF)
        ? "bodyqueue" : "bodyque";
}

bool q1_body_queue_initialize(qa_q1_game *g, qa_error *error) {
    if (q1_ref_present(g->body_queue_head)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 body queue is already initialized");
        return false;
    }
    q1_actor *bodies[4] = {0};
    size_t count = 0;
    for (; count < 4; ++count) {
        if (!q1_create(g, q1_body_queue_classname(g), Q1_BODY, (qa_actor_id){0},
            &bodies[count], error)) goto fail;
        bodies[count]->physics.water_type = 0;
        bodies[count]->physics.yaw_speed = 0;
    }
    for (size_t i = 0; i < 4; ++i)
        bodies[i]->owner = q1_ref_from(g, bodies[(i + 1) % 4]->id);
    g->body_queue_head = q1_ref_from(g, bodies[0]->id);
    return true;
fail:
    for (size_t i = 0; i < count; ++i)
        (void)qa_session_release(g->services.session, bodies[i]->id, NULL);
    return false;
}

bool q1_body_queue_ring_valid(q1_ref head, const q1_ref nodes[4], const q1_ref links[4]) {
    if (!q1_ref_present(head)) return false;
    bool visited[4] = {false};
    q1_ref current = head;
    for (size_t i = 0; i < 4; ++i) {
        size_t index = 0;
        while (index < 4 && !q1_ref_equal(nodes[index], current)) ++index;
        if (index == 4 || visited[index]) return false;
        visited[index] = true;
        current = links[index];
    }
    return q1_ref_equal(current, head);
}

bool q1_body_queue_validate(const qa_q1_game *g, qa_error *error) {
    size_t count = 0;
    q1_ref nodes[4], links[4];
    for (uint32_t slot = 0; slot < g->capacity; ++slot) {
        const q1_actor *body = g->actors[slot];
        if (!body || body->kind != Q1_BODY) continue;
        if (count == 4) goto invalid;
        const char *classname = qa_strings_cstr(qa_session_strings(g->services.session),
            body->classname);
        if (q1_entity_const(g, body->id) != body || !body->native || body->map ||
            !classname || strcmp(classname, q1_body_queue_classname(g))) goto invalid;
        nodes[count] = q1_ref_from(g, body->id);
        if (g->wire && (nodes[count].kind != QA_ACTOR_REFERENCE_SOURCE ||
            nodes[count].value.source.owner != g->options.provider)) goto invalid;
        links[count] = body->owner;
        ++count;
    }
    if (!q1_ref_present(g->body_queue_head) && !count) return true;
    if (count == 4 && q1_body_queue_ring_valid(g->body_queue_head, nodes, links)) return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 body queue lacks its four actual linked edicts");
    return false;
}

bool qa_q1_source_copy_body(qa_q1_game *g, qa_actor_id actor,
    const qa_q1_presentation *visual, qa_physics_motion motion, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    q1_actor *corpse = q1_entity(g, q1_ref_actor(g, g->body_queue_head));
    bool okay = visual && player && player->source_client &&
        player->client_slot < g->options.max_clients && corpse && corpse->kind == Q1_BODY &&
        qa_actor_id_equal(visual->actor, actor);
    if (!okay) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Q1 body copy requires its actual Source client and initialized queue");
        goto finish;
    }
    qa_body_state source, body;
    if (!qa_world_body_read(g->services.world, actor, &source, error) ||
        !qa_world_body_read(g->services.world, corpse->id, &body, error)) {
        okay = false; goto finish;
    }
    corpse->model = visual->model;
    corpse->frame = visual->frame;
    corpse->state.body.color_map = (int32_t)player->client_slot + 1;
    if (g->options.program == QA_Q1_ROGUE) corpse->skin = visual->skin;
    corpse->physics.motion = motion;
    corpse->physics.flags = 0;
    corpse->source_movement_flags = 0;
    body.angles = source.angles;
    body.velocity = source.velocity;
    body.origin = source.origin;
    body.bounds = source.bounds;
    qa_actor_id id = corpse->id;
    q1_ref next = corpse->owner;
    okay = qa_world_body_write(g->services.world, id, &body, error) && q1_link(g, corpse, error);
    if (okay && (!qa_q1_game_operation_live(&operation) || !q1_entity(g, id))) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, id.slot, "Q1 body retired during Source copying");
        okay = false;
    }
    if (okay) g->body_queue_head = next;
finish:
    qa_q1_game_operation_end(&operation);
    return okay;
}

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
    if (!q1_create(g, classname, source->kind, q1_ref_actor(g, source->owner), &target, error))
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
    target->pickup_observation = (qa_pickup_lease){0};
    if (source->map && !q1_map_clone(g, source, target, error))
        goto fail;
    q1_map_addon_clone(g, source->id, id);
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
    if (target->kind == Q1_PICKUP && !q1_pickup_observe(g, target, error))
        goto fail;
    if (g->host.monster_path_clone &&
        !g->host.monster_path_clone(g->host.context, actor, id, error))
        goto fail;
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

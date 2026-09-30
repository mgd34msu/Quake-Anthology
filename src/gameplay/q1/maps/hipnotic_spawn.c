#include "internal.h"

static q1_actor *spawner(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && entity->map->kind == Q1_MAP_SPAWNER ? entity : NULL;
}
bool q1_map_spawn_template_wait(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    return q1_schedule(g, entity, 1, Q1_THINK_SPAWN_TEMPLATE, error);
}
static bool make_template(qa_q1_game *g, qa_actor_id mold, const qa_q1_spawn *source,
                          const char *classname, qa_actor_id *out, qa_error *error) {
    q1_actor *owner = spawner(g, mold);
    if (!owner)
        return q1_map_fail(error, "Q1 spawn template lost its source mold");
    qa_q1_spawn request = *source;
    request.classname = classname;
    request.has_source = false;
    request.source_slot = 0;
    const qa_strings *strings = qa_session_strings(g->services.session);
    request.target = qa_strings_cstr(strings, owner->target);
    request.targetname = qa_strings_cstr(strings, owner->targetname);
    request.killtarget = qa_strings_cstr(strings, owner->killtarget);
    request.message = qa_strings_cstr(strings, owner->message);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, mold, &body, error))
        return false;
    if (!spawner(g, mold))
        return q1_map_fail(error, "Q1 spawn mold retired during its body read");
    qa_actor_id id;
    if (!q1_spawn_template(g, &request, &body, &id, error))
        return false;
    q1_actor *entity = q1_entity(g, id);
    if (!entity)
        return q1_map_fail(error, "Q1 spawn template removed itself during initialization");
    if (!entity->map && !q1_map_allocate(g, entity, error))
        goto fail;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        goto fail;
    entity = q1_entity(g, id);
    if (!entity)
        return q1_map_fail(error, "Q1 spawn template retired during its body read");
    entity->map->spawn_template.model = entity->model;
    entity->map->spawn_template.solid = entity->physics.solid;
    entity->map->spawn_template.think = entity->think;
    entity->map->spawn_template.bounds = body.bounds;
    entity->map->spawn_template.valid = true;
    entity->model = QA_STRING_NONE;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    if (!q1_map_spawn_template_wait(g, entity, error) || !q1_link(g, entity, error))
        goto fail;
    if (!q1_entity(g, id))
        return q1_map_fail(error, "Q1 spawn template retired during linking");
    *out = id;
    return true;
fail:
    (void)qa_session_release(g->services.session, id, NULL);
    return false;
}
bool q1_map_hip_spawner_spawn(qa_q1_game *g, q1_actor *entity, const qa_q1_spawn *source,
                              qa_error *error) {
    qa_actor_id id = entity->id, master = {0};
    uint32_t total = g->total_monsters;
    if (!q1_map_text(g, entity->map->spawn_function)) {
        static const char *const classes[] = {"monster_dog", "monster_ogre", "monster_demon1",
                                              "monster_zombie", "monster_shambler"};
        float chance = q1_random(g);
        unsigned selected = chance < .5f    ? 0
                            : chance < .8f  ? 1
                            : chance < .92f ? 2
                            : chance < .97f ? 3
                                            : 4;
        for (unsigned i = 0; i < sizeof(classes) / sizeof(*classes); ++i) {
            qa_actor_id candidate;
            if (!make_template(g, id, source, classes[i], &candidate, error))
                return false;
            if (i == selected)
                master = candidate;
        }
        g->total_monsters = total + 1;
    } else {
        if (!q1_map_text(g, entity->map->spawn_classname))
            return q1_map_fail(error, "No spawnclassname defined");
        const char *classname =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->map->spawn_classname);
        if (!make_template(g, id, source, classname, &master, error))
            return false;
        entity = spawner(g, id);
        if (!entity)
            return true;
        if (entity->map->spawn_multi != 0)
            g->total_monsters = total;
    }
    entity = spawner(g, id);
    if (!entity)
        return true;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    entity->model = QA_STRING_NONE;
    entity->map->use_enabled = true;
    entity->map->pending.spawn_master = master;
    return q1_link(g, entity, error);
}
bool q1_map_hip_spawner_use(qa_q1_game *g, q1_actor *mold, qa_error *error) {
    qa_actor_id mold_id = mold->id, id = mold->map->pending.spawn_master;
    q1_actor *entity = q1_entity(g, id);
    if (!entity || !entity->map || !entity->map->spawn_template.valid)
        return q1_map_fail(error, "Q1 func_spawn lost its initialized master");
    qa_actor_id charmer = g->horn_charmer;
    if (mold->map->spawn_multi == 1 || charmer.registry) {
        if (!qa_q1_game_clone(g, id, &id, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return q1_map_fail(error, "Q1 func_spawn clone retired before activation");
    }
    entity->model = entity->map->spawn_template.model;
    entity->physics.solid = entity->map->spawn_template.solid;
    q1_think_kind think = entity->map->spawn_template.think;
    if (entity->physics.motion == QA_PHYSICS_PUSH || think == Q1_THINK_NONE) {
        entity->think = think;
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), id);
    } else if (!q1_schedule(g, entity, entity->next_think - g->time, think, error))
        return false;
    qa_bounds bounds = entity->map->spawn_template.bounds;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    body.bounds = bounds;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !q1_link(g, entity, error))
        return entity == NULL;
    entity = q1_entity(g, id);
    if (!entity || !q1_link(g, entity, error))
        return entity == NULL;
    mold = spawner(g, mold_id);
    if (!mold || !q1_entity(g, id))
        return true;
    if (mold->map->spawn_silent == 0) {
        qa_actor_id fog;
        if (!qa_world_body_read(g->services.world, id, &body, error) ||
            !qa_q1_spawn_teleport_fog(g, body.origin, &fog, error))
            return false;
    }
    if (!q1_entity(g, id))
        return true;
    if (charmer.registry && !qa_q1_monster_charm(g, id, charmer, error))
        return false;
    entity = q1_entity(g, id);
    mold = spawner(g, mold_id);
    if (!entity || !mold)
        return true;
    if (entity->physics.flags & QA_PHYSICS_MONSTER) {
        if (mold->map->spawn_multi != 0 && !charmer.registry)
            ++g->total_monsters;
        if (charmer.registry)
            entity->effects |= 8;
    }
    return mold->map->spawn_multi != 0 || charmer.registry || q1_remove(g, mold, error);
}

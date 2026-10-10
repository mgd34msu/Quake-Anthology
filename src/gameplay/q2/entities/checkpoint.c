#include "checkpoint_internal.h"
#include "internal.h"

void qa_q2_entity_checkpoint_free(qa_q2_entity_checkpoint *s) {
    if (!s)
        return;
    free(s->value.fields);
    free(s->value.mover);
    free(s->value.turret);
    free(s->value.q64);
    free(s->value.trail);
    *s = (qa_q2_entity_checkpoint){0};
}
static bool copy_arrays(qa_q2_game *g, const qa_q2_entity_state *from, qa_q2_entity_state *to,
                        bool runtime, qa_error *e) {
    to->fields = NULL;
    to->mover = NULL;
    to->turret = NULL;
    to->q64 = NULL;
    to->trail = NULL;
    void *copy;
    if (runtime) {
        size_t bytes = from->field_count * sizeof(*from->fields);
        copy = bytes ? qa_arena_alloc(&g->entity_fields, bytes, _Alignof(q2_field), e) : NULL;
        if (bytes && !copy) return false;
        if (bytes) memcpy(copy, from->fields, bytes);
    } else if (!q2_saved_array(from->fields, from->field_count, sizeof(*from->fields), &copy, e))
        return false;
    to->fields = copy;
    if (!q2_saved_array(from->mover, from->mover ? 1 : 0, sizeof(*from->mover), &copy, e))
        return false;
    to->mover = copy;
    if (!q2_saved_array(from->turret, from->turret ? 1 : 0, sizeof(*from->turret), &copy, e))
        return false;
    to->turret = copy;
    if (!q2_saved_array(from->q64, from->q64 ? 1 : 0, sizeof(*from->q64), &copy, e))
        return false;
    to->q64 = copy;
    if (runtime && from->trail) {
        *q2_entity_trail_prepare(to) = *from->trail;
    } else {
        if (!q2_saved_array(from->trail, from->trail ? 1 : 0, sizeof(*from->trail), &copy, e))
            return false;
        to->trail = copy;
    }
    return true;
}
bool qa_q2_entity_capture(qa_q2_game *g, qa_actor_id id, qa_q2_entity_checkpoint *out,
                          qa_error *e) {
    if (!g || !out || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 entity checkpoint actor");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    qa_q2_entity_checkpoint saved = {0};
    q2_actor *a = q2_ent(g, id);
    if (!a) {
        *out = saved;
        return true;
    }
    const q2_entity_state *s = a->entity;
    if (s->dispatching) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 entity checkpoint requires completed use");
        return false;
    }
    saved.present = true;
    saved.value = *s;
    if (!copy_arrays(g, s, &saved.value, false, e))
        goto fail;
    if (!q2_save_reference(g, s->activator, &saved.activator, e) ||
        !q2_save_reference(g, s->owner, &saved.owner, e) ||
        !q2_save_reference(g, s->enemy, &saved.enemy, e) ||
        !q2_save_reference(g, s->goal, &saved.goal, e))
        goto fail;
    saved.value.activator = saved.value.owner = saved.value.enemy = saved.value.goal =
        (qa_actor_id){0};
    if (s->mover) {
        if (!q2_save_reference(g, s->mover->destination, &saved.destination, e))
            goto fail;
        saved.value.mover->destination = (qa_actor_id){0};
    }
    if (s->turret) {
        if (!q2_save_reference(g, s->turret->breach, &saved.turret_breach, e))
            goto fail;
        saved.value.turret->breach = (qa_actor_id){0};
    }
    *out = saved;
    return true;
fail:
    qa_q2_entity_checkpoint_free(&saved);
    return false;
}
static bool valid_mover(const q2_mover *m) {
    if (m->destination.registry || (unsigned)m->motion.done > Q2MD_SECRET_NEXT)
        return false;
    const float values[] = {m->distance,
                            m->water_divisor,
                            m->motion.remaining,
                            m->motion.current_speed,
                            m->motion.move_speed,
                            m->motion.next_speed,
                            m->motion.decel_distance,
                            m->motion.curve_from,
                            m->motion.curve_to,
                            m->motion.curve_distance};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++)
        if (!isfinite(values[i]))
            return false;
    return qa_vec_finite(m->start) && qa_vec_finite(m->end) && qa_vec_finite(m->intermediate) &&
           qa_vec_finite(m->safe_direction) && qa_vec_finite(m->motion.direction) &&
           qa_vec_finite(m->motion.destination) && qa_vec_finite(m->motion.reference);
}
static bool valid_state(qa_q2_game *g, const q2_entity_state *s, qa_error *e) {
    if ((unsigned)s->kind > Q2E_DELAYED_USE || (unsigned)s->think > Q2ET_LIGHT_FLICKER ||
        (unsigned)s->scenery > Q2S_MAL_LASER || s->dispatching || s->activator.registry ||
        s->owner.registry || s->enemy.registry || s->goal.registry ||
        (unsigned)s->team_master.kind > QA_ACTOR_REFERENCE_SOURCE ||
        (unsigned)s->team_next.kind > QA_ACTOR_REFERENCE_SOURCE || (unsigned)s->collision.owner.kind > QA_ACTOR_REFERENCE_SOURCE ||
        (unsigned)s->collision.role > QA_COLLISION_BOTH ||
        (unsigned)s->collision.shape > QA_SHAPE_CAPSULE ||
        (s->collision.family && s->collision.family != QA_COLLISION_Q2) || !s->classname ||
        !q2_saved_visual(g, &s->visual) || !qa_vec_finite(s->direction) ||
        !qa_vec_finite(s->beam_end) || !qa_vec_finite(s->multicast_origin) ||
        (s->field_count && !s->fields) ||
        s->field_count > SIZE_MAX / sizeof(q2_field))
        return false;
    const qa_string_id resources[] = {s->classname, s->targetname, s->target, s->killtarget,
                                      s->message,   s->team,       s->map,    s->noise, s->loop_sound};
    for (size_t i = 0; i < sizeof(resources) / sizeof(*resources); i++)
        if (!q2_saved_resource(g, resources[i]))
            return false;
    const float values[] = {s->speed,  s->accel,  s->decel,  s->wait,   s->delay,
                            s->damage, s->health, s->random, s->volume, s->attenuation};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++)
        if (!isfinite(values[i]))
            return false;
    for (size_t i = 0; i < s->field_count; i++)
        if (!s->fields[i].key || !q2_saved_resource(g, s->fields[i].key) ||
            !q2_saved_resource(g, s->fields[i].value))
            return false;
    if (s->trail && ((unsigned)s->trail->owner.kind > QA_ACTOR_REFERENCE_SOURCE ||
        (unsigned)s->trail->older.kind > QA_ACTOR_REFERENCE_SOURCE ||
        (unsigned)s->trail->newer.kind > QA_ACTOR_REFERENCE_SOURCE))
        return false;
    if (s->mover && !valid_mover(s->mover))
        return false;
    if (s->turret) {
        const q2_turret *t = s->turret;
        const float numbers[] = {t->pitch_min, t->pitch_max,  t->yaw_min, t->yaw_max,
                                 t->radius,    t->yaw_offset, t->height, t->rocket_scale};
        for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); i++)
            if (!isfinite(numbers[i]))
                return false;
        if (t->breach.registry || !qa_vec_finite(t->goal) || !qa_vec_finite(t->muzzle))
            return false;
    }
    if (s->q64) {
        const q2_q64 *q = s->q64;
        if (!qa_vec_finite(q->neutral) || !qa_vec_finite(q->eye_position) ||
            !qa_vec_finite(q->angles) || !isfinite(q->vision_cone) || !isfinite(q->remaining) ||
            !isfinite(q->distance) || !isfinite(q->speed) || !isfinite(q->fade_remaining) ||
            !isfinite(q->fade_duration))
            return false;
    }
    if ((s->kind == Q2E_TURRET_BREACH || s->kind == Q2E_TURRET_DRIVER) && !s->turret)
        return false;
    if ((s->kind == Q2E_EYE || s->kind == Q2E_CAMERA || s->kind == Q2E_CAMERA_DUMMY) && !s->q64)
        return false;
    if ((s->kind == Q2E_DOOR || s->kind == Q2E_BUTTON || s->kind == Q2E_WATER ||
         s->kind == Q2E_TRAIN || s->kind == Q2E_PLAT || s->kind == Q2E_SECRET_DOOR) &&
        !s->mover)
        return false;
    if ((s->think >= Q2ET_MOVE_BEGIN && s->think <= Q2ET_MOVE_ACCEL) && !s->mover)
        return false;
    if (s->has_inline || s->collision.inline_model) {
        qa_bounds bounds;
        if (!qa_collision_model_bounds(qa_world_geometry(g->services.world), s->collision.model,
                                       &bounds, e))
            return false;
    }
    return true;
}
bool qa_q2_entity_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_entity_checkpoint *saved,
                          qa_error *e) {
    if (!g || !saved || !q2_actor_live(g, id) ||
        (saved->present && !valid_state(g, &saved->value, e))) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 entity checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, saved->present, saved->present ? e : NULL);
    if (!saved->present) {
        if (a)
            q2_entity_release_state(a);
        return true;
    }
    if (!a)
        return false;
    q2_entity_state *s = q2_entity_state_take(g, e);
    if (!s) return false;
    *s = saved->value;
    if (!copy_arrays(g, &saved->value, s, true, e))
        goto fail;
    if (!q2_resolve_reference(g, saved->activator, &s->activator, e) ||
        !q2_resolve_reference(g, saved->owner, &s->owner, e) ||
        !q2_resolve_reference(g, saved->enemy, &s->enemy, e) ||
        !q2_resolve_reference(g, saved->goal, &s->goal, e))
        goto fail;
    if (s->mover && !q2_resolve_reference(g, saved->destination, &s->mover->destination, e))
        goto fail;
    if (s->turret && !q2_resolve_reference(g, saved->turret_breach, &s->turret->breach, e))
        goto fail;
    q2_entity_state *previous = a->entity;
    qa_q2_game *previous_game = a->entity_game;
    a->entity = s;
    a->entity_game = g;
    if (!g->restoring_continuation && !q2_entity_bind(g, a, e)) {
        a->entity = previous;
        a->entity_game = previous_game;
        goto fail;
    }
    if (previous) q2_entity_state_release(previous_game, previous);
    return true;
fail:
    q2_entity_state_release(g, s);
    return false;
}
void qa_q2_entities_checkpoint_free(qa_q2_entities_checkpoint *s) {
    if (!s)
        return;
    free(s->wind);
    free(s->visited_maps);
    *s = (qa_q2_entities_checkpoint){0};
}
bool qa_q2_entities_capture(qa_q2_game *g, bool level_only, qa_q2_entities_checkpoint *out, qa_error *e) {
    if (!g || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 map runtime checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_entities *r = g->entity_runtime;
    qa_q2_entities_checkpoint s = {.poi_image = r->poi_image,
                                   .story = r->story,
                                   .poi_stage = r->poi_stage,
                                   .steam_id = r->steam_id,
                                   .total_secrets = r->total_secrets,
                                   .found_secrets = r->found_secrets,
                                   .total_goals = r->total_goals,
                                   .found_goals = r->found_goals,
                                   .last_autosave_ns = r->last_autosave_ns,
                                   .world_fog = r->world_fog,
                                   .sky = r->sky,
                                   .goals = r->goals,
                                   .primary = r->primary,
                                   .secondary = r->secondary,
                                   .sky_axis = r->sky_axis,
                                   .sky_rotation = r->sky_rotation,
                                   .primary_changes = r->primary_changes,
                                   .secondary_changes = r->secondary_changes,
                                   .goal_number = r->goal_number,
                                   .sky_auto = r->sky_auto,
                                   .has_goals = r->has_goals,
                                   .wind_count = r->wind_count,
                                   .total_monsters = r->total_monsters,
                                   .killed_monsters = r->killed_monsters,
                                   .level_count = level_only ? 0 : r->level_count,
                                   .visited_count = level_only ? 0 : r->visited_count};
    if (!level_only) memcpy(s.levels, r->levels, sizeof(s.levels));
    if (!q2_save_reference(g, r->poi, &s.poi, e) ||
        !q2_save_reference(g, r->poi_dynamic, &s.poi_dynamic, e))
        return false;
    for (size_t i = 0; i < 2; i++) {
        s.bars[i].dead_until_ns = r->bars[i].dead_until_ns;
        s.bars[i].dying = r->bars[i].dying;
        if (!q2_save_reference(g, r->bars[i].controller, &s.bars[i].controller, e) ||
            !q2_save_reference(g, r->bars[i].target, &s.bars[i].target, e))
            return false;
    }
    if (s.wind_count) {
        if (s.wind_count > SIZE_MAX / sizeof(*s.wind)) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Q2 wind checkpoint is too large");
            return false;
        }
        s.wind = calloc(s.wind_count, sizeof(*s.wind));
        if (!s.wind) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Capturing Q2 wind timers");
            return false;
        }
        for (size_t i = 0; i < s.wind_count; i++) {
            s.wind[i].until_ns = r->wind[i].until_ns;
            if (!q2_save_reference(g, r->wind[i].actor, &s.wind[i].actor, e)) {
                qa_q2_entities_checkpoint_free(&s);
                return false;
            }
        }
    }
    if (s.visited_count) {
        if (s.visited_count > SIZE_MAX / sizeof(*s.visited_maps) ||
            !(s.visited_maps = malloc(s.visited_count * sizeof(*s.visited_maps)))) {
            qa_q2_entities_checkpoint_free(&s);
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Capturing Q2 campaign visited maps");
            return false;
        }
        memcpy(s.visited_maps, r->visited_maps, s.visited_count * sizeof(*s.visited_maps));
    }
    *out = s;
    return true;
}
bool qa_q2_entities_restore(qa_q2_game *g, const qa_q2_entities_checkpoint *s, qa_error *e) {
    if (!g || !s || !q2_saved_fog(&s->world_fog) ||
        !qa_vec_finite(s->sky_axis) || !isfinite(s->sky_rotation) ||
        !q2_saved_resource(g, s->poi_image) || !q2_saved_resource(g, s->story) ||
        !q2_saved_resource(g, s->sky) || !q2_saved_resource(g, s->goals) ||
        !q2_saved_resource(g, s->primary) || !q2_saved_resource(g, s->secondary) ||
        (s->wind_count && !s->wind) || s->wind_count > SIZE_MAX / sizeof(q2_wind_time)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 map runtime checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    if (!q2_campaign_saved_valid(g, s, e)) return false;
    q2_entities next = *g->entity_runtime;
    next.wind = NULL;
    next.wind_count = next.wind_capacity = s->wind_count;
    if (!q2_resolve_reference(g, s->poi, &next.poi, e) ||
        !q2_resolve_reference(g, s->poi_dynamic, &next.poi_dynamic, e))
        return false;
    for (size_t i = 0; i < 2; i++) {
        next.bars[i].dead_until_ns = s->bars[i].dead_until_ns;
        next.bars[i].dying = s->bars[i].dying;
        if (!q2_resolve_reference(g, s->bars[i].controller, &next.bars[i].controller, e) ||
            !q2_resolve_reference(g, s->bars[i].target, &next.bars[i].target, e))
            return false;
    }
    if (s->wind_count) {
        next.wind = calloc(s->wind_count, sizeof(*next.wind));
        if (!next.wind) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring Q2 wind timers");
            return false;
        }
        for (size_t i = 0; i < s->wind_count; i++) {
            next.wind[i].until_ns = s->wind[i].until_ns;
            if (!q2_resolve_reference(g, s->wind[i].actor, &next.wind[i].actor, e)) {
                free(next.wind);
                return false;
            }
            for (size_t j = 0; j < i; j++)
                if (qa_actor_id_equal(next.wind[i].actor, next.wind[j].actor)) {
                    free(next.wind);
                    qa_error_set(e, QA_ERROR_FORMAT, i, "Duplicate Q2 wind timer");
                    return false;
                }
        }
    }
    next.poi_image = s->poi_image;
    next.story = s->story;
    next.poi_stage = s->poi_stage;
    next.steam_id = s->steam_id;
    next.total_secrets = s->total_secrets;
    next.found_secrets = s->found_secrets;
    next.total_goals = s->total_goals;
    next.found_goals = s->found_goals;
    next.last_autosave_ns = s->last_autosave_ns;
    next.world_fog = s->world_fog;
    next.sky = s->sky;
    next.goals = s->goals;
    next.primary = s->primary;
    next.secondary = s->secondary;
    next.sky_axis = s->sky_axis;
    next.sky_rotation = s->sky_rotation;
    next.primary_changes = s->primary_changes;
    next.secondary_changes = s->secondary_changes;
    next.goal_number = s->goal_number;
    next.sky_auto = s->sky_auto;
    next.has_goals = s->has_goals;
    next.total_monsters = s->total_monsters;
    next.killed_monsters = s->killed_monsters;
    next.level_count = s->level_count;
    memcpy(next.levels, s->levels, sizeof(next.levels));
    next.visited_maps = NULL;
    next.visited_count = next.visited_capacity = s->visited_count;
    if (s->visited_count) {
        next.visited_maps = malloc(s->visited_count * sizeof(*next.visited_maps));
        if (!next.visited_maps) {
            free(next.wind);
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring Q2 campaign visited maps");
            return false;
        }
        memcpy(next.visited_maps, s->visited_maps, s->visited_count * sizeof(*next.visited_maps));
    }
    free(g->entity_runtime->wind);
    free(g->entity_runtime->visited_maps);
    *g->entity_runtime = next;
    return true;
}
bool qa_q2_entities_validate_links(qa_q2_game *g, qa_error *e) {
    if (!g) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 provider");
        return false;
    }
    for (q2_actor *a = g->first_actor; a; a = a->live_next) {
        for (unsigned chain = 0; chain < 2; ++chain) {
            qa_actor_reference link = chain == 0 ?
                (a->entity ? a->entity->team_next : (qa_actor_reference){0}) :
                (a->item ? a->item->spawn.team_next : (qa_actor_reference){0});
            qa_actor_id next = qa_actor_reference_resolve(qa_session_actors(g->services.session), link);
            size_t visited = 0;
            while (q2_actor_live(g, next)) {
                q2_actor *part = q2_actor_get(g, next, false, NULL);
                if (!part || (!part->entity && !part->item) || qa_actor_id_equal(next, a->id) ||
                    ++visited > g->capacity) {
                    qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Invalid Q2 entity team chain");
                    return false;
                }
                link = chain == 0 ?
                    (part->entity ? part->entity->team_next : (qa_actor_reference){0}) :
                    (part->item ? part->item->spawn.team_next : (qa_actor_reference){0});
                next = qa_actor_reference_resolve(qa_session_actors(g->services.session), link);
            }
        }
    }
    return true;
}

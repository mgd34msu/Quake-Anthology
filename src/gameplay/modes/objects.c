#include "internal.h"

static qa_game_family family(mode_instance *v) {
    return v->value.rules.source <= QA_MODE_Q1_HORDE ? QA_GAME_Q1
           : v->value.rules.source < QA_MODE_Q3      ? QA_GAME_Q2
                                                     : QA_GAME_Q3;
}
static bool native_objective_read(void *context, qa_objective_state *out, qa_error *e) {
    mode_object *o = context;
    if (!o->active || !mode_live(o->modes, o->actor))
        return mode_fail(e, "native objective retired");
    qa_mode_object_view view = o->value;
    if (o->q3_source_owned && (!o->modes->options.hooks.q3_source_object_view ||
        !o->modes->options.hooks.q3_source_object_view(o->modes->options.hooks.context,
            o->mode, o->actor, &view, e))) return false;
    *out = (qa_objective_state){.actor = o->actor,
                                .carrier = view.carrier,
                                .phase = view.phase,
                                .complete = view.phase == QA_OBJECTIVE_COMPLETE};
    return true;
}
bool mode_object_bind_objective(qa_modes *m, mode_object *o, qa_error *e) {
    if (!o->spec.id)
        return true;
    qa_objective_binding binding = {.mode = o->mode, .owner = m->options.owner,
                                    .id = o->spec.id,
                                    .bot_goal = true,
                                    .context = &m->objects[o->actor.slot],
                                    .read = native_objective_read};
    return o->admitting ? mode_reserve_objective(m, &binding, &o->objective, e)
                        : qa_modes_bind_objective(m, &binding, &o->objective, e);
}
bool mode_object_notify(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    return mode_event(m, v, QA_MODE_OBJECTIVE_CHANGED, o->value.carrier, (qa_actor_id){0}, o->actor,
                      o->spec.team, (int32_t)o->value.phase, 0, e);
}
bool mode_object_count(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                       double count, qa_error *e) {
    if (!o->spec.item)
        return true;
    qa_item_id item;
    if (!mode_inventory_item(m, v, o->spec.item, &item, e))
        return false;
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *other = &m->objects[i];
        if (other != o && other->active && other->mode.slot == v->id.slot &&
            other->mode.generation == v->id.generation && other->spec.item == o->spec.item &&
            other->value.phase == QA_OBJECTIVE_CARRIED &&
            qa_actor_id_equal(other->value.carrier, actor))
            count += 1;
    }
    qa_inventory_entry existing;
    qa_error local = {0};
    if (!qa_inventory_entry_read(m->options.services.inventory, actor, item, &existing, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (e) *e = local;
            return false;
        }
        qa_inventory_entry initial = {.item = item, .capacity = count > 1 ? count : 1,
            .policy = QA_COUNT_SOURCE_INT32};
        qa_inventory_admission *admission = NULL;
        if (!qa_inventory_prepare_entries(m->options.services.inventory, actor, &initial, 1,
                                          &admission, e))
            return false;
        if (!qa_inventory_admission_commit(admission, e)) {
            qa_inventory_admission_abort(admission);
            return false;
        }
    }
    return mode_set_count(m, actor, item, count, e);
}
bool mode_object_sync(qa_modes *m, mode_object *o, qa_error *e) {
    if (o->q3_source_owned)
        return true;
    if (!mode_live(m, o->actor))
        return true;
    mode_instance *v = mode_get(m, o->mode);
    bool solid =
        v && v->value.rules.enabled && o->value.visible && o->value.phase != QA_OBJECTIVE_CARRIED &&
        o->value.phase != QA_OBJECTIVE_DESTROYED && o->value.phase != QA_OBJECTIVE_DISABLED;
    o->physics.solid =
        solid ? (o->spec.kind == QA_MODE_OBJECT_BALL || o->spec.kind == QA_MODE_OBJECT_OBELISK
                     ? QA_PHYSICS_BOX
                     : QA_PHYSICS_TRIGGER)
              : QA_PHYSICS_NOT_SOLID;
    if (!qa_world_set_collision(m->options.services.world, o->actor, solid ? &o->collision : NULL,
                                e))
        return false;
    return qa_world_link(m->options.services.world, o->actor, NULL, e);
}
bool mode_object_hide(qa_modes *m, mode_object *o, bool hidden, qa_error *e) {
    o->value.visible = !hidden;
    return mode_object_sync(m, o, e);
}
static const char *object_model(mode_instance *v, const qa_mode_object_spec *spec) {
    int team = mode_team_index(v, spec->team);
    if (spec->kind == QA_MODE_OBJECT_FLAG_BASE)
        return "progs/ctfbase.mdl";
    if (spec->kind == QA_MODE_OBJECT_FLAG) {
        if (v->value.rules.source == QA_MODE_ROGUE)
            return "progs/ctfmodel.mdl";
        if (v->value.rules.source <= QA_MODE_Q1_HORDE)
            return "progs/flag.mdl";
        if (v->value.rules.source < QA_MODE_Q3)
            return team == 0 ? "players/male/flag1.md2" : "players/male/flag2.md2";
        return team == 0   ? "models/flags/r_flag.md3"
               : team == 1 ? "models/flags/b_flag.md3"
                           : "models/flags/n_flag.md3";
    }
    if (spec->kind == QA_MODE_OBJECT_RELIC) {
        static const char *q1[] = {"progs/end1.mdl", "progs/end2.mdl", "progs/end3.mdl",
                                   "progs/end4.mdl"};
        static const char *q2[] = {"models/ctf/resistance/tris.md2", "models/ctf/strength/tris.md2",
                                   "models/ctf/haste/tris.md2", "models/ctf/regeneration/tris.md2",
                                   "models/ctf/vampire/tris.md2"};
        static const char *lm[] = {"models/ctf/resist/tris.md2", "models/ctf/damage/tris.md2",
                                   "models/ctf/haste/tris.md2", "models/ctf/regen/tris.md2",
                                   "models/ctf/resist/tris.md2"};
        return v->value.rules.source <= QA_MODE_Q1_HORDE ? q1[(unsigned)spec->relic % 4]
               : v->value.rules.source == QA_MODE_LMCTF  ? lm[spec->relic]
                                                         : q2[spec->relic];
    }
    if (spec->kind == QA_MODE_OBJECT_TAG)
        return v->value.rules.source == QA_MODE_ROGUE ? "progs/sphere.mdl"
                                                      : "models/items/tagtoken/tris.md2";
    if (spec->kind == QA_MODE_OBJECT_BALL)
        return "models/objects/dball/tris.md2";
    if (spec->kind == QA_MODE_OBJECT_CUBE)
        return team == 0 ? "models/powerups/orb/r_orb.md3" : "models/powerups/orb/b_orb.md3";
    if (spec->kind == QA_MODE_OBJECT_OBELISK)
        return "models/powerups/overload_base.md3";
    return NULL;
}
static const char *object_item(mode_instance *v, const qa_mode_object_spec *spec) {
    int team = mode_team_index(v, spec->team);
    if (spec->kind == QA_MODE_OBJECT_FLAG) {
        if (v->value.rules.source <= QA_MODE_Q1_HORDE)
            return team == 0   ? "q1:ctf/flag/red"
                   : team == 1 ? "q1:ctf/flag/blue"
                               : "q1:ctf/flag/neutral";
        if (v->value.rules.source == QA_MODE_LMCTF)
            return "q2:flag";
        if (v->value.rules.source < QA_MODE_Q3)
            return team == 0 ? "q2:item_flag_team1" : "q2:item_flag_team2";
        return team == 0   ? "q3:item/team_CTF_redflag"
               : team == 1 ? "q3:item/team_CTF_blueflag"
                           : "q3:item/team_CTF_neutralflag";
    }
    if (spec->kind == QA_MODE_OBJECT_TAG)
        return v->value.rules.source == QA_MODE_ROGUE ? "q1:rogue/tag" : "q2:dm_tag_token";
    if (spec->kind == QA_MODE_OBJECT_RELIC) {
        static const char *q1[] = {"q1:ctf/rune/resistance", "q1:ctf/rune/strength",
                                   "q1:ctf/rune/haste", "q1:ctf/rune/regeneration"};
        static const char *q2[] = {"q2:item_tech1", "q2:item_tech2", "q2:item_tech3",
                                   "q2:item_tech4", "q2:item_tech5"};
        static const char *lm[] = {"q2:resist_rune", "q2:damage_rune", "q2:haste_rune",
                                   "q2:regen_rune", "q2:vampire_rune"};
        return v->value.rules.source <= QA_MODE_Q1_HORDE ? q1[(unsigned)spec->relic % 4]
               : v->value.rules.source == QA_MODE_LMCTF  ? lm[spec->relic]
                                                         : q2[spec->relic];
    }
    return NULL;
}
static bool spawn_object(qa_modes *m, qa_mode_id id, const qa_mode_object_spec *spec,
                         qa_actor_id *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !spec || !out || spec->kind > QA_MODE_OBJECT_FLAG_BASE ||
        spec->kind < QA_MODE_OBJECT_FLAG || spec->relic < QA_RELIC_RESISTANCE ||
        spec->relic >= QA_RELIC_COUNT || !qa_vec_finite(spec->origin) ||
        !qa_vec_finite(spec->angles) || !qa_vec_finite(spec->direction) ||
        (spec->has_bounds &&
         (!qa_vec_finite(spec->bounds.mins) || !qa_vec_finite(spec->bounds.maxs) ||
          spec->bounds.mins.x > spec->bounds.maxs.x || spec->bounds.mins.y > spec->bounds.maxs.y ||
          spec->bounds.mins.z > spec->bounds.maxs.z)) ||
        !isfinite(spec->value))
        return mode_fail(e, "invalid native mode object");
    int team = mode_team_index(v, spec->team);
    if (!spec->command_created &&
        (spec->kind == QA_MODE_OBJECT_FLAG || spec->kind == QA_MODE_OBJECT_OBELISK) &&
        mode_live(m, v->bases[team >= 0 ? team : 2]))
        return mode_fail(e, "duplicate native objective base");
    qa_actor_id created_base = {0};
    if (!spec->command_created && v->value.rules.source == QA_MODE_ROGUE && spec->kind == QA_MODE_OBJECT_FLAG) {
        qa_mode_object_spec base = *spec;
        base.kind = QA_MODE_OBJECT_FLAG_BASE;
        base.id = 0;
        base.item = 0;
        base.has_bounds = false;
        base.retain_body = false;
        bool base_only = v->value.rules.teamplay == 5 && spec->team;
        if (!base_only)
            base.actor = (qa_actor_id){0};
        qa_actor_id base_actor;
        if (!qa_modes_spawn_object(m, id, &base, &base_actor, e))
            return false;
        if (base_only) {
            *out = base_actor;
            return true;
        }
        created_base = base_actor;
    }
    qa_body_state body = {.origin = spec->origin,
                          .angles = spec->angles,
                          .bounds = {.mins = {-15, -15, -15}, .maxs = {15, 15, 15}}};
    if (spec->kind == QA_MODE_OBJECT_BALL)
        body.bounds = (qa_bounds){{-32, -32, -32}, {32, 32, 32}};
    else if (spec->kind == QA_MODE_OBJECT_TAG && v->value.rules.source == QA_MODE_ROGUE)
        body.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
    else if (spec->kind == QA_MODE_OBJECT_FLAG_BASE)
        body.bounds = (qa_bounds){{-8, -8, 0}, {8, 8, 8}};
    else if (spec->kind == QA_MODE_OBJECT_OBELISK)
        body.bounds = (qa_bounds){{-15, -15, 0}, {15, 15, 87}};
    else if (spec->kind == QA_MODE_OBJECT_RELIC && family(v) == QA_GAME_Q1)
        body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    else if (spec->kind == QA_MODE_OBJECT_FLAG && family(v) == QA_GAME_Q1)
        body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
    else if (spec->kind == QA_MODE_OBJECT_FLAG && v->value.rules.source == QA_MODE_LMCTF)
        body.bounds.maxs.z = 33;
    if (spec->has_bounds)
        body.bounds = spec->bounds;
    qa_actor_collision collision = {.family = family(v),
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_contents_decode(1, family(v)),
                                    .role = spec->kind == QA_MODE_OBJECT_BALL ||
                                                    (spec->kind == QA_MODE_OBJECT_OBELISK &&
                                                     v->value.rules.kind == QA_MODE_OVERLOAD)
                                                ? QA_COLLISION_SOLID
                                                : QA_COLLISION_TRIGGER};
    qa_combat_state combat = {
        .health = spec->kind == QA_MODE_OBJECT_BALL ? 50000 : v->value.rules.obelisk_health,
        .mass = spec->kind == QA_MODE_OBJECT_BALL ? 50 : 200,
        .can_take_damage = true,
        .no_knockback = spec->kind == QA_MODE_OBJECT_OBELISK};
    qa_actor_id actor = spec->actor;
    qa_body_state previous_body = {0};
    qa_body_link_state previous_link = {0};
    qa_actor_collision previous_collision = {0};
    qa_combat_state previous_combat = {0};
    bool authored = actor.registry != 0, changed_body = false, changed_combat = false;
    bool reserved = false, omitted = false;
    bool had_collision = false, had_combat = false, captured_body = false;
    uint64_t combat_serial = 0, body_serial = 0;
    bool needs_combat = spec->kind == QA_MODE_OBJECT_BALL ||
        (spec->kind == QA_MODE_OBJECT_OBELISK && v->value.rules.kind == QA_MODE_OVERLOAD);
    mode_object pending = {0};
    mode_object *o = &pending;
    if (actor.registry) {
        if (!mode_live(m, actor) || mode_object_get(m, actor) || m->objects[actor.slot].admitting)
            goto invalid_actor;
        m->objects[actor.slot] = (mode_object){.actor = actor, .admitting = true};
        reserved = true;
        if (!qa_world_body_read(m->options.services.world, actor, &previous_body, e) ||
            !qa_world_link_state(m->options.services.world, actor, &previous_link))
            goto rollback;
        captured_body = true;
        body_serial = qa_world_body_storage_serial(m->options.services.world, actor);
        qa_error collision_error = {0};
        had_collision = qa_world_get_collision(m->options.services.world, actor, &previous_collision, &collision_error);
        if (collision_error.code != QA_OK) {
            if (e) *e = collision_error;
            goto rollback;
        }
        if (needs_combat) {
            qa_error local = {0};
            combat_serial = qa_combat_storage_serial(m->options.services.combat, actor);
            had_combat = qa_combat_read_traits(m->options.services.combat, actor, &previous_combat, &local);
            if (!had_combat && local.code != QA_ERROR_NOT_FOUND) {
                if (e) *e = local;
                goto rollback;
            }
        }
        if (spec->retain_body) {
            body = previous_body;
            if (had_collision) {
                qa_collision_role role = collision.role;
                collision = previous_collision;
                collision.role = role;
            }
        }
        if (qa_world_body_storage_serial(m->options.services.world, actor) != body_serial) {
            mode_fail(e, "mode body owner changed during admission read");
            goto rollback;
        }
        changed_body = true;
        if (!qa_world_body_write(m->options.services.world, actor, &body, e) ||
            !qa_world_set_collision(m->options.services.world, actor, &collision, e))
            goto rollback;
    } else {
        qa_string_id definition;
        if (!qa_builtin_resource(&m->options.services, "anthology:mode-object", &definition, e))
            goto rollback;
        qa_builtin_spawn spawn = {.owner = m->options.owner,
                                  .definition = definition,
                                  .body = body,
                                  .collision = &collision,
                                  .link = false};
        if (spec->kind == QA_MODE_OBJECT_BALL ||
            (spec->kind == QA_MODE_OBJECT_OBELISK && v->value.rules.kind == QA_MODE_OVERLOAD))
            spawn.combat = &combat;
        if (!qa_builtin_spawn_actor(&m->options.services, &spawn, &actor, e))
            goto rollback;
        m->objects[actor.slot] = (mode_object){.actor = actor, .admitting = true};
        reserved = true;
    }
    *o = (mode_object){.modes = m,
                       .actor = actor,
                       .mode = id,
                       .spec = *spec,
                       .home = body.origin,
                       .collision = collision,
                       .active = true,
                       .admitting = true,
                       .has_physics = true,
                       .born_ns = v->value.time_ns,
                       .value = {.mode = id,
                                 .kind = spec->kind,
                                 .team = spec->team,
                                 .relic = spec->relic,
                                 .visible = true,
                                 .phase = QA_OBJECTIVE_HOME}};
    o->spec.actor = actor;
    o->physics = qa_physics_properties_default(family(v));
    o->physics.q2_rerelease = v->value.rules.q2_rerelease;
    o->physics.motion =
        spec->kind == QA_MODE_OBJECT_BALL ? QA_PHYSICS_NEW_TOSS : QA_PHYSICS_STATIONARY;
    o->physics.clip_mask = family(v) == QA_GAME_Q3 ? 1 : 3;
    if (spec->kind == QA_MODE_OBJECT_BALL)
        o->physics.clip_mask =
            UINT32_C(0x2020003) | (v->value.rules.q2_rerelease ? UINT32_C(0x40000000) : 0);
    const char *model = object_model(v, spec), *item = object_item(v, spec);
    if (model && !qa_builtin_resource(&m->options.services, model, &o->value.model, e))
        goto rollback;
    if (!o->spec.item && item && !qa_builtin_resource(&m->options.services, item, &o->spec.item, e))
        goto rollback;
    qa_item_id scoped_item;
    if (o->spec.item && !(v->value.rules.source == QA_MODE_ROGUE &&
                          spec->kind == QA_MODE_OBJECT_RELIC) &&
        !mode_inventory_item(m, v, o->spec.item, &scoped_item, e))
        goto rollback;
    if (spec->kind == QA_MODE_OBJECT_FLAG) {
        o->value.skin = team > 0 ? team : 0;
        o->value.frame = family(v) == QA_GAME_Q2 ? 173 : 0;
        o->value.effects = family(v) == QA_GAME_Q1   ? (team == 0 ? 32u : 16u)
                           : family(v) == QA_GAME_Q2 ? (team == 0 ? 0x40000u : 0x80000u)
                                                          : 0;
    }
    if (spec->kind == QA_MODE_OBJECT_OBELISK) {
        o->next_ns = v->value.time_ns + v->value.rules.obelisk_regen_ns;
    }
    if (spec->kind == QA_MODE_OBJECT_TAG) {
        if (v->value.rules.source == QA_MODE_ROGUE) {
            o->value.skin = 1;
            o->value.effects = 8;
            o->tag_stage = 1;
            o->next_ns = v->value.time_ns + MODE_SECOND / 5;
        }
    }
    if (spec->kind == QA_MODE_OBJECT_RELIC)
        o->expire_ns = v->value.time_ns + (v->value.rules.source == QA_MODE_THREEWAVE ||
                                            v->value.rules.source == QA_MODE_ROGUE ? 120
                                           : v->value.rules.source == QA_MODE_LMCTF   ? 30
                                                                                      : 60) *
                                              MODE_SECOND;
    if (spec->kind == QA_MODE_OBJECT_FLAG_BASE) {
        o->value.skin = team >= 0 ? team : 2;
        if (v->value.rules.teamplay == 4)
            o->value.phase = QA_OBJECTIVE_DISABLED;
    }
    if (spec->kind == QA_MODE_OBJECT_LOCATION) {
        o->has_physics = false;
        o->value.visible = false;
        if (!o->spec.location && v->next_location == INT32_MAX) {
            mode_fail(e, "mode location identity exhausted");
            goto rollback;
        }
        if (!o->spec.location)
            o->spec.location = v->next_location + 1;
    }
    if (!mode_object_bind_objective(m, o, e))
        goto rollback;
    if (!spec->suspended && !spec->command_created &&
        (spec->kind == QA_MODE_OBJECT_FLAG || spec->kind == QA_MODE_OBJECT_OBELISK ||
         spec->kind == QA_MODE_OBJECT_FLAG_BASE)) {
        qa_trace_query trace = {.start = body.origin,
                                .end = body.origin,
                                .shape = {QA_SHAPE_BOX, body.bounds},
                                .policy = {.family = family(v), .contents_mask = qa_collision_contents_mask(3, family(v)), .q1_hull = -1},
                                .pass_actor = actor};
        trace.start.z += family(v) == QA_GAME_Q1 ? 6 : 1;
        trace.end = trace.start;
        trace.end.z -= spec->kind == QA_MODE_OBJECT_OBELISK ? 4096
                       : family(v) == QA_GAME_Q1       ? 256
                                                            : 128;
        qa_trace_result result;
        if (!qa_world_trace(m->options.services.world, &trace, &result, e))
            goto rollback;
        if (family(v) == QA_GAME_Q1 && (result.all_solid || result.fraction == 1)) {
            omitted = true;
            goto rollback;
        }
        if (!result.start_solid && result.fraction < 1) {
            if (authored && qa_world_body_storage_serial(m->options.services.world, actor) != body_serial) {
                mode_fail(e, "mode body owner changed during placement");
                goto rollback;
            }
            body.origin = result.end;
            body.ground = qa_actor_reference_lifetime(result.actor);
            o->home = body.origin;
            if (!qa_world_body_write(m->options.services.world, actor, &body, e))
                goto rollback;
        }
    }
    if (family(v) == QA_GAME_Q1 &&
        (spec->kind == QA_MODE_OBJECT_FLAG || spec->kind == QA_MODE_OBJECT_FLAG_BASE))
        o->physics.motion = QA_PHYSICS_TOSS;
    if (authored && qa_world_body_storage_serial(m->options.services.world, actor) != body_serial) {
        mode_fail(e, "mode body owner changed before link");
        goto rollback;
    }
    if (!mode_object_sync(m, o, e))
        goto rollback;
    if (!mode_live(m, actor) ||
        (authored && qa_world_body_storage_serial(m->options.services.world, actor) != body_serial) ||
        !m->objects[actor.slot].admitting ||
        !qa_actor_id_equal(m->objects[actor.slot].actor, actor)) {
        mode_fail(e, "mode object retired during admission");
        goto rollback;
    }
    if (o->objective.serial &&
        (!m->objectives[o->objective.slot].reserved ||
         m->objectives[o->objective.slot].serial != o->objective.serial)) {
        mode_fail(e, "mode objective binding changed during admission");
        goto rollback;
    }
    if (authored && needs_combat) {
        if (qa_combat_storage_serial(m->options.services.combat, actor) != combat_serial) {
            mode_fail(e, "mode combat storage changed during admission");
            goto rollback;
        }
        if (had_combat) {
            changed_combat = true;
            if (!qa_combat_set_traits(m->options.services.combat, actor, &combat, e))
                goto rollback;
            if (qa_combat_storage_serial(m->options.services.combat, actor) != combat_serial) {
                mode_fail(e, "mode combat owner changed during traits write");
                goto rollback;
            }
            if (!qa_combat_set_health(m->options.services.combat, actor, combat.health, e))
                goto rollback;
            if (qa_combat_storage_serial(m->options.services.combat, actor) != combat_serial) {
                mode_fail(e, "mode combat owner changed during health write");
                goto rollback;
            }
        } else if (!qa_combat_create_actor(m->options.services.combat, actor, &combat, e))
            goto rollback;
    }
    if (!mode_live(m, actor) ||
        (authored && qa_world_body_storage_serial(m->options.services.world, actor) != body_serial) ||
        !m->objects[actor.slot].admitting ||
        !qa_actor_id_equal(m->objects[actor.slot].actor, actor)) {
        mode_fail(e, "mode actor ownership changed during combat admission");
        goto rollback;
    }
    pending.admitting = false;
    m->objects[actor.slot] = pending;
    if (o->objective.serial)
        (void)mode_commit_objective(m, o->objective);
    if ((spec->kind == QA_MODE_OBJECT_FLAG && !spec->command_created) ||
        spec->kind == QA_MODE_OBJECT_OBELISK)
        v->bases[team >= 0 ? team : 2] = actor;
    if (spec->kind == QA_MODE_OBJECT_BALL)
        v->ball = actor;
    if (spec->kind == QA_MODE_OBJECT_TAG && !spec->command_created)
        v->tag = actor;
    if (spec->kind == QA_MODE_OBJECT_LOCATION && o->spec.location > v->next_location)
        v->next_location = o->spec.location;
    *out = actor;
    return true;
invalid_actor:
    mode_fail(e, "mode actor already claimed or stale");
rollback: {
        qa_error cleanup = {0}, first = {0};
        if (o->objective.serial)
            qa_modes_unbind_objective(m, o->objective, NULL);
        if (reserved && m->objects[actor.slot].admitting &&
            qa_actor_id_equal(m->objects[actor.slot].actor, actor))
            m->objects[actor.slot] = (mode_object){0};
        if (authored && mode_live(m, actor)) {
            if (changed_combat &&
                qa_combat_storage_serial(m->options.services.combat, actor) == combat_serial) {
                if (!qa_combat_set_traits(m->options.services.combat, actor, &previous_combat, &cleanup))
                    first = cleanup;
                if (qa_combat_storage_serial(m->options.services.combat, actor) == combat_serial &&
                    !qa_combat_set_health(m->options.services.combat, actor, previous_combat.health, &cleanup) &&
                    !first.code)
                    first = cleanup;
            }
            if (changed_body && captured_body &&
                qa_world_body_storage_serial(m->options.services.world, actor) == body_serial) {
                if (!qa_world_body_write(m->options.services.world, actor, &previous_body, &cleanup) && !first.code)
                    first = cleanup;
                if (qa_world_body_storage_serial(m->options.services.world, actor) == body_serial &&
                    !qa_world_set_collision(m->options.services.world, actor,
                        had_collision ? &previous_collision : NULL, &cleanup) && !first.code)
                    first = cleanup;
                if (qa_world_body_storage_serial(m->options.services.world, actor) == body_serial &&
                    !qa_world_restore_link_state(m->options.services.world, actor, &previous_link, &cleanup) && !first.code)
                    first = cleanup;
            }
        } else if (!authored && mode_live(m, actor) &&
                   !qa_session_release(m->options.services.session, actor, &cleanup))
            first = cleanup;
        if (mode_live(m, created_base) &&
            !qa_session_release(m->options.services.session, created_base, &cleanup) && !first.code)
            first = cleanup;
        if (first.code && e)
            *e = first;
        if (omitted && !first.code)
            *out = (qa_actor_id){0};
        return omitted && !first.code;
    }
}
bool qa_modes_spawn_object(qa_modes *m, qa_mode_id id, const qa_mode_object_spec *spec,
                           qa_actor_id *out, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode object service");
    qa_mode_object_spec requested;
    if (spec) {
        requested = *spec;
        spec = &requested;
    }
    mode_instance *v = mode_get(m, id);
    bool base = v && spec && !spec->command_created &&
        (spec->kind == QA_MODE_OBJECT_FLAG || spec->kind == QA_MODE_OBJECT_OBELISK);
    int index = base ? mode_team_index(v, spec->team) : -1;
    if (index < 0)
        index = 2;
    if (base && v->base_admitting[index])
        return mode_fail(e, "mode objective base admission already in progress");
    if (base)
        v->base_admitting[index] = true;
    bool ok = MODE_CALLBACK(m, spawn_object(m, id, spec, out, e));
    if (base)
        v->base_admitting[index] = false;
    return ok;
}
bool qa_modes_object_read(qa_modes *m, qa_actor_id actor, qa_mode_object_view *out) {
    mode_object *o = mode_object_get(m, actor);
    if (!o || !out)
        return false;
    *out = o->value;
    if (o->q3_source_owned)
        return m->options.hooks.q3_source_object_view &&
            MODE_CALLBACK(m, m->options.hooks.q3_source_object_view(
                m->options.hooks.context, o->mode, actor, out, NULL));
    mode_instance *v = mode_get(m, o->mode);
    if (!v || !v->value.rules.enabled)
        out->visible = false;
    if (o->spec.kind == QA_MODE_OBJECT_OBELISK) {
        qa_combat_state state;
        if (v && qa_combat_read(m->options.services.combat, actor, &state, NULL))
            out->health_fraction =
                (int32_t)(255 * fmaxf(0, fminf(1, state.health / v->value.rules.obelisk_health)));
    }
    return true;
}
bool qa_modes_object_home(qa_modes *m, qa_actor_id actor, qa_vec3 *origin,
                           qa_bounds *bounds, qa_error *error) {
    mode_object *object = mode_object_get(m, actor);
    if (!object || !origin || !bounds)
        return mode_fail(error, "objective home needs a live object and outputs");
    *origin = object->spec.origin;
    *bounds = object->spec.bounds;
    return true;
}
bool qa_modes_object_at(qa_modes *m, size_t index, qa_actor_id *actor, qa_mode_object_view *out,
                        qa_error *e) {
    if (!m || !actor || !out)
        return mode_fail(e, "invalid native objective enumeration");
    for (size_t i = 0; i < m->observations.count; ++i) {
        mode_object *o = mode_object_get(m, m->observations.ids[i]);
        if (o && !index--) {
            *actor = o->actor;
            return qa_modes_object_read(m, o->actor, out);
        }
    }
    qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "native objective index outside active set");
    return false;
}
bool qa_modes_physics(qa_modes *m, qa_actor_id actor, qa_physics_properties *out) {
    mode_object *o = mode_object_get(m, actor);
    if (!o || !o->has_physics || !out)
        return false;
    mode_instance *v = mode_get(m, o->mode);
    if (!v || !v->value.rules.enabled)
        return false;
    *out = o->physics;
    return true;
}
bool qa_modes_physics_write(qa_modes *m, qa_actor_id actor, const qa_physics_properties *value,
                            qa_error *e) {
    mode_object *o = mode_object_get(m, actor);
    if (!o || !value)
        return mode_fail(e, "invalid mode physics write");
    o->physics = *value;
    return true;
}
static bool touch(qa_modes *m, qa_actor_id object, qa_actor_id actor, bool *accepted, qa_error *e) {
    if (!accepted)
        return mode_fail(e, "missing mode touch result");
    *accepted = false;
    mode_object *o = mode_object_get(m, object);
    if (!o)
        return true;
    if (o->q3_source_owned)
        return true;
    mode_instance *v = mode_get(m, o->mode);
    if (!v || !v->value.rules.enabled)
        return true;
    if (o->spec.kind == QA_MODE_OBJECT_SPEED || o->spec.kind == QA_MODE_OBJECT_GOAL ||
        o->spec.kind == QA_MODE_OBJECT_BALL)
        return mode_ball_touch(m, v, o, actor, accepted, e);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || p->player.spectator || !mode_alive(m, actor))
        return true;
    if (!o->value.visible || o->value.phase == QA_OBJECTIVE_CARRIED ||
        (qa_actor_id_equal(o->value.previous_owner, actor) && v->value.time_ns < o->owner_until_ns))
        return true;
    if (o->base.registry) {
        o = mode_object_get(m, o->base);
        if (!o)
            return true;
    }
    if (o->spec.kind == QA_MODE_OBJECT_FLAG || o->spec.kind == QA_MODE_OBJECT_FLAG_BASE)
        return mode_flag_touch(m, v, o, actor, accepted, e);
    if (o->spec.kind == QA_MODE_OBJECT_RELIC)
        return mode_relic_touch(m, v, o, actor, accepted, e);
    if (o->spec.kind == QA_MODE_OBJECT_TAG)
        return mode_tag_touch(m, v, o, actor, accepted, e);
    if (o->spec.kind == QA_MODE_OBJECT_OBELISK)
        return mode_obelisk_touch(m, v, o, actor, accepted, e);
    if (o->spec.kind == QA_MODE_OBJECT_CUBE) {
        qa_team_id team;
        if (!qa_modes_team(m, v->id, actor, &team, e))
            return false;
        if (team != o->spec.team) {
            mode_member *member = mode_member_get(m, v, actor);
            member->stats.tokens = mode_add_i32(member->stats.tokens, 1);
        }
        *accepted = true;
        return qa_session_release(m->options.services.session, o->actor, e);
    }
    return true;
}
bool qa_modes_touch(qa_modes *m, qa_actor_id object, qa_actor_id actor, bool *accepted,
                    qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode touch service");
    return MODE_CALLBACK(m, touch(m, object, actor, accepted, e));
}
bool mode_object_drop(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor, bool death,
                      qa_error *e) {
    if (o->q3_source_owned)
        return true;
    qa_body_state player, body;
    if (!qa_world_body_read(m->options.services.world, actor, &player, e) ||
        !qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    qa_vec3 angles = player.angles, forward;
    if (m->options.hooks.player_view)
        m->options.hooks.player_view(m->options.hooks.context, actor, &angles);
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    qa_vec3 origin = qa_vec_add(player.origin, qa_vec_scale(forward, 24));
    origin.z -= 16;
    if (family(v) == QA_GAME_Q1) {
        origin = player.origin;
        origin.z -= 24;
    }
    qa_trace_query query = {.start = player.origin,
                            .end = origin,
                            .shape = {QA_SHAPE_BOX, body.bounds},
                            .policy = {.family = family(v), .contents_mask = qa_collision_contents_mask(3, family(v)), .q1_hull = -1},
                            .pass_actor = actor};
    qa_trace_result trace;
    if (!qa_world_trace(m->options.services.world, &query, &trace, e))
        return false;
    body.origin = trace.end;
    body.velocity = qa_vec_scale(forward, v->value.rules.source == QA_MODE_LMCTF ? 200 : 100);
    body.velocity.z = 300;
    body.ground = (qa_actor_reference){0};
    if (o->spec.kind == QA_MODE_OBJECT_FLAG && family(v) == QA_GAME_Q1)
        body.velocity = qa_v3(0, 0, 300);
    if (o->spec.kind == QA_MODE_OBJECT_RELIC && family(v) == QA_GAME_Q1) {
        body.velocity.x = (mode_random_float(m) - .5f) * 1000;
        body.velocity.y = (mode_random_float(m) - .5f) * 1000;
        body.velocity.z = 400;
    } else if (o->spec.kind == QA_MODE_OBJECT_RELIC && death &&
               v->value.rules.source == QA_MODE_Q2_CTF) {
        body.velocity.x = (float)((int)(mode_random(m) % 600) - 300);
        body.velocity.y = (float)((int)(mode_random(m) % 600) - 300);
        body.velocity.z = 300;
    }
    if (v->value.rules.source == QA_MODE_LMCTF && o->spec.kind == QA_MODE_OBJECT_RELIC) {
        mode_random(m);
        mode_random(m);
        mode_random(m);
        body.velocity = qa_vec_scale(forward, 200);
        body.velocity.z = 300;
    }
    if (!mode_object_count(m, v, o, actor, 0, e))
        return false;
    o->value.phase = QA_OBJECTIVE_DROPPED;
    o->value.carrier = (qa_actor_id){0};
    o->value.previous_owner =
        death && v->value.rules.source == QA_MODE_Q2_CTF ? (qa_actor_id){0} : actor;
    o->owner_until_ns =
        v->value.time_ns + (v->value.rules.source == QA_MODE_Q2_CTF ? 2 : 1) * MODE_SECOND;
    o->value.deadline_ns =
        v->value.time_ns + (o->spec.kind == QA_MODE_OBJECT_RELIC
                                ? (family(v) == QA_GAME_Q1              ? 120
                                   : v->value.rules.source == QA_MODE_Q2_CTF ? 60
                                                                             : 30)
                                : 30) *
                               MODE_SECOND;
    if (o->spec.kind == QA_MODE_OBJECT_FLAG && v->value.rules.source == QA_MODE_ROGUE)
        o->value.deadline_ns = v->value.time_ns + 80 * MODE_SECOND;
    if (o->spec.kind == QA_MODE_OBJECT_RELIC && v->value.rules.source == QA_MODE_Q2_CTF && !death)
        o->owner_until_ns = o->value.deadline_ns;
    o->expire_ns = o->value.deadline_ns;
    o->physics.motion = QA_PHYSICS_TOSS;
    o->dropped = true;
    if (o->spec.kind == QA_MODE_OBJECT_TAG) {
        o->expire_ns = 0;
        o->next_ns = v->value.time_ns + MODE_SECOND;
    }
    if (o->spec.kind == QA_MODE_OBJECT_FLAG &&
        (v->value.rules.source == QA_MODE_Q2_CTF || v->value.rules.source >= QA_MODE_Q3)) {
        qa_string_id definition;
        if (!qa_builtin_resource(&m->options.services, "anthology:dropped-flag", &definition, e))
            return false;
        qa_builtin_spawn spawn = {.owner = m->options.owner,
                                  .definition = definition,
                                  .body = body,
                                  .collision = &o->collision,
                                  .link = true};
        qa_actor_id dropped;
        if (!qa_builtin_spawn_actor(&m->options.services, &spawn, &dropped, e))
            return false;
        mode_object *child = &m->objects[dropped.slot];
        *child = *o;
        child->actor = dropped;
        child->spec.actor = dropped;
        child->spec.id = 0;
        child->objective = (qa_objective_lease){0};
        child->base = o->actor;
        child->dropped_actor = (qa_actor_id){0};
        child->expire_ns = 0;
        child->value.visible = true;
        o->dropped_actor = dropped;
        o->physics.motion = QA_PHYSICS_STATIONARY;
        if (!mode_object_hide(m, o, true, e) || !mode_object_sync(m, child, e))
            return false;
        return mode_object_notify(m, v, o, e);
    }
    if (!qa_world_body_write(m->options.services.world, o->actor, &body, e) ||
        !mode_object_hide(m, o, false, e))
        return false;
    return mode_object_notify(m, v, o, e);
}
static bool drop(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool death, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p)
        return true;
    if (v->value.rules.source == QA_MODE_ROGUE && p->rogue_rune &&
        !mode_rogue_relic_drop(m, v, actor, p, e)) return false;
    qa_actor_id flag = p->flag, relic = p->relic;
    p->flag = (qa_actor_id){0};
    p->relic = (qa_actor_id){0};
    mode_object *o = mode_object_get(m, flag);
    if (o && qa_actor_id_equal(o->value.carrier, actor) &&
        !mode_object_drop(m, v, o, actor, death, e))
        return false;
    o = mode_object_get(m, relic);
    if (o && qa_actor_id_equal(o->value.carrier, actor) &&
        !mode_object_drop(m, v, o, actor, death, e))
        return false;
    if (qa_actor_id_equal(v->tag_owner, actor)) {
        if (v->value.rules.source == QA_MODE_ROGUE) {
            o = mode_object_get(m, v->tag);
            return death || !o || mode_rogue_tag_drop(m, v, o, e);
        }
        o = mode_object_get(m, v->tag);
        v->tag_owner = (qa_actor_id){0};
        v->tag_count = 0;
        if (o && !mode_object_drop(m, v, o, actor, death, e))
            return false;
    }
    return true;
}
bool qa_modes_drop(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool death, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode drop service");
    return MODE_CALLBACK(m, drop(m, id, actor, death, e));
}
bool mode_object_relocate(qa_modes *m, mode_instance *v, mode_object *o, bool farthest,
                          qa_error *e) {
    if (o->spec.kind == QA_MODE_OBJECT_RELIC)
        return mode_relic_place(m, v, o, false, e);
    qa_mode_spawnpoint point;
    if (!qa_modes_spawnpoint(m, v->id, (qa_actor_id){0}, farthest, &point, e)) {
        if (!v->spawn_count) {
            o->expire_ns = v->value.time_ns + MODE_SECOND;
            return true;
        }
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    body.origin = point.origin;
    body.origin.z += 16;
    body.angles = point.angles;
    float yaw = mode_random_float(m) * 6.28318530717958647692f;
    body.velocity = qa_v3(100 * cosf(yaw), 100 * sinf(yaw), 300);
    body.ground = (qa_actor_reference){0};
    o->value.phase = QA_OBJECTIVE_HOME;
    o->value.carrier = (qa_actor_id){0};
    o->value.previous_owner = (qa_actor_id){0};
    o->value.deadline_ns = 0;
    o->physics.motion = QA_PHYSICS_TOSS;
    o->dropped = false;
    o->expire_ns = o->spec.kind == QA_MODE_OBJECT_RELIC
                       ? v->value.time_ns + (family(v) == QA_GAME_Q1              ? 120
                                             : v->value.rules.source == QA_MODE_Q2_CTF ? 60
                                                                                       : 30) *
                                                MODE_SECOND
                       : 0;
    return qa_world_body_write(m->options.services.world, o->actor, &body, e) &&
           mode_object_hide(m, o, false, e);
}
static bool follow_flag(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    qa_body_state carrier, body;
    if (!qa_world_body_read(m->options.services.world, o->value.carrier, &carrier, e) ||
        !qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    qa_vec3 forward, right;
    qa_builtin_angle_vectors(carrier.angles, &forward, &right, NULL);
    forward.z = -forward.z;
    int32_t frame = 0;
    float distance = 14;
    if (m->options.hooks.character_frame)
        m->options.hooks.character_frame(m->options.hooks.context, o->value.carrier, &frame);
    if (frame >= 29 && frame <= 40) {
        static const float offsets[] = {2, 8, 12, 11, 10, 4, 2, 10, 10, 8, 4, 2};
        distance += offsets[frame - 29];
    } else if (frame >= 103 && frame <= 106)
        distance += 6;
    else if (frame >= 107 && frame <= 118)
        distance += 7;
    body.origin = qa_vec_add(qa_vec_sub(carrier.origin, qa_vec_scale(forward, distance)),
                             qa_vec_scale(right, 22));
    body.origin.z -= 16;
    body.angles = carrier.angles;
    body.angles.z -= 45;
    o->animation_ns = v->value.time_ns + 10 * MODE_MILLISECOND;
    return qa_world_body_write(m->options.services.world, o->actor, &body, e) &&
           qa_world_link(m->options.services.world, o->actor, NULL, e);
}
bool mode_objects_frame(qa_modes *m, mode_instance *v, uint64_t elapsed, qa_error *e) {
    if (v->relic_spawn_ns && v->value.time_ns >= v->relic_spawn_ns) {
        v->relic_spawn_ns = 0;
        if (!mode_relic_spawn_all(m, v, e))
            return false;
    }
    for (size_t ordinal = 0; ordinal < m->observations.count; ++ordinal) {
        uint32_t i = m->observations.ids[ordinal].slot;
        mode_object *o = &m->objects[i];
        if (!o->active || o->mode.slot != v->id.slot || o->mode.generation != v->id.generation ||
            !mode_live(m, o->actor))
            continue;
        if (o->q3_source_owned)
            continue;
        qa_actor_id actor = o->actor;
        if (o->spec.kind == QA_MODE_OBJECT_TAG && v->value.rules.source == QA_MODE_ROGUE) {
            if (!mode_rogue_tag_frame(m, v, o, e))
                return false;
            continue;
        }
        if (o->spec.kind == QA_MODE_OBJECT_BALL && !mode_ball_frame(m, v, o, e))
            return false;
        if (o->spec.kind == QA_MODE_OBJECT_TAG && o->next_ns && v->value.time_ns >= o->next_ns) {
            qa_body_state body;
            qa_point_contents contents;
            if (!qa_world_body_read(m->options.services.world, actor, &body, e))
                return false;
            qa_point_query query = {
                .point = body.origin, .policy = {.family = QA_GAME_Q2}, .pass_actor = actor};
            if (!qa_world_point_contents(m->options.services.world, &query, &contents, e))
                return false;
            o->next_ns = 0;
            o->expire_ns = v->value.time_ns + (qa_collision_bits_overlap(contents.contents, qa_collision_bits_union(
                qa_collision_bit(QA_CONTENT_LAVA), qa_collision_bit(QA_CONTENT_SLIME))) ? 3 : 30) * MODE_SECOND;
        }
        if (o->value.phase == QA_OBJECTIVE_CARRIED) {
            if (!mode_live(m, o->value.carrier)) {
                if (o->spec.kind == QA_MODE_OBJECT_FLAG) {
                    if (!mode_flag_reset(m, v, o, true, e))
                        return false;
                } else if (!mode_object_relocate(m, v, o, false, e))
                    return false;
            } else if (family(v) == QA_GAME_Q1 && o->spec.kind == QA_MODE_OBJECT_FLAG &&
                       v->value.time_ns >= o->animation_ns && !follow_flag(m, v, o, e))
                return false;
            continue;
        }
        if (o->expire_ns && v->value.time_ns >= o->expire_ns) {
            o->expire_ns = 0;
            if (o->spec.kind == QA_MODE_OBJECT_FLAG) {
                if (!mode_flag_reset(m, v, o, true, e))
                    return false;
            } else if (o->spec.kind == QA_MODE_OBJECT_CUBE) {
                if (!qa_session_release(m->options.services.session, actor, e))
                    return false;
                continue;
            } else if (!mode_object_relocate(m, v, o, o->spec.kind == QA_MODE_OBJECT_TAG, e))
                return false;
        }
        if (o->spec.kind == QA_MODE_OBJECT_OBELISK && v->value.rules.kind == QA_MODE_OVERLOAD &&
            o->next_ns && v->value.time_ns >= o->next_ns) {
            qa_combat_state state;
            if (!qa_combat_read_traits(m->options.services.combat, actor, &state, e))
                return false;
            if (o->value.phase == QA_OBJECTIVE_DESTROYED) {
                state.can_take_damage = true;
                state.health = v->value.rules.obelisk_health;
                if (!qa_combat_set_traits(m->options.services.combat, actor, &state, e) ||
                    !qa_combat_set_health(m->options.services.combat, actor, state.health, e))
                    return false;
                o->value.phase = QA_OBJECTIVE_HOME;
                o->value.frame = 0;
                if (!mode_object_sync(m, o, e))
                    return false;
            } else if (state.health < v->value.rules.obelisk_health) {
                if (!mode_event(m, v, QA_MODE_OBELISK_REGEN, (qa_actor_id){0}, (qa_actor_id){0},
                                actor, o->spec.team, 0, 0, e) ||
                    !qa_combat_set_health(m->options.services.combat, actor,
                                          fminf(v->value.rules.obelisk_health,
                                                state.health + v->value.rules.obelisk_regen),
                                          e))
                    return false;
                o->value.frame = 0;
            }
            o->next_ns = v->value.time_ns + v->value.rules.obelisk_regen_ns;
        }
        if (o->value.visible && o->spec.kind == QA_MODE_OBJECT_FLAG &&
            family(v) == QA_GAME_Q2 && v->value.time_ns >= o->animation_ns) {
            o->value.frame = 173 + (o->value.frame - 173 + 1) % 16;
            o->animation_ns = v->value.time_ns + MODE_SECOND / 10;
        }
        if (o->value.visible && o->spec.kind == QA_MODE_OBJECT_RELIC &&
            v->value.rules.source == QA_MODE_LMCTF && v->value.time_ns >= o->animation_ns) {
            if (o->spec.relic == QA_RELIC_STRENGTH) {
                o->value.frame += v->rune_forward ? 1 : -1;
                if (o->value.frame >= 5)
                    v->rune_forward = false;
                else if (o->value.frame <= 0)
                    v->rune_forward = true;
            } else if (o->spec.relic == QA_RELIC_HASTE)
                o->value.frame =
                    o->value.frame >= 1 && o->value.frame <= 15 ? o->value.frame + 1 : 5;
            else
                o->value.frame =
                    (o->value.frame + 1) % (o->spec.relic == QA_RELIC_REGENERATION ? 14 : 15);
            o->animation_ns = v->value.time_ns + MODE_SECOND / 10;
        }
        if (o->has_physics && o->physics.motion != QA_PHYSICS_STATIONARY &&
            m->options.services.physics) {
            qa_source_frame frame = {.time_ns = v->value.time_ns, .elapsed_ns = elapsed};
            qa_physics_result result;
            if (!qa_physics_step(m->options.services.physics, actor, &frame, &result, e))
                return false;
        }
    }
    return true;
}

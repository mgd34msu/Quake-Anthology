#include "../entities/internal.h"
#include "internal.h"
#include "qa/game_q2_source.h"

bool q2_items_init(qa_q2_game *g, qa_error *e) {
    g->item_runtime = calloc(1, sizeof(*g->item_runtime));
    if (!g->item_runtime) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 items");
        return false;
    }
    g->item_runtime->options.weapon_respawn_seconds = 30;
    g->item_runtime->options.instanced_coop = g->options.edition == QA_Q2_RERELEASE;
    return qa_builtin_resource(&g->services, "foodcube", &g->item_runtime->food_classname, e) &&
           q2_item_catalog(g, e);
}
void q2_items_close(qa_q2_game *g) {
    if (g->item_runtime) {
        free(g->item_runtime->actions);
        free(g->item_runtime->admissions);
        free(g->item_runtime->definitions);
        free(g->item_runtime);
        g->item_runtime = NULL;
    }
}
void q2_items_release_state(q2_actor *a) {
    if (a->item) {
        if (a->item->observations && a->item->observation.serial)
            qa_pickups_observation_close(a->item->observations, a->item->observation, NULL);
        if (!a->entity) {
            q2_entity_unbind(a->entity_game, a);
            a->entity_game = NULL;
        }
        free(a->item->picked_slots);
        free(a->item->companion);
        free(a->item);
        a->item = NULL;
    }
    if (a->powers)
        qa_inventory_close_items(a->powers->game->services.inventory, a->powers->definitions, NULL);
    free(a->powers);
    a->powers = NULL;
}
bool qa_q2_items_configure(qa_q2_game *g, const qa_q2_item_options *options, qa_error *e) {
    if (!g || !options || !isfinite(options->weapon_respawn_seconds) ||
        options->weapon_respawn_seconds < 0 ||
        ((options->supplemental_count || options->supplemental_item ||
          options->supplemental_give) &&
         !(options->supplemental_count && options->supplemental_item &&
           options->supplemental_give))) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item options");
        return false;
    }
    g->item_runtime->options = *options;
    return true;
}
bool q2_item_sound(qa_q2_game *g, qa_actor_id id, const char *path, qa_error *e) {
    if (!q2_actor_live(g, id))
        return true;
    qa_body_state body;
    qa_string_id sound;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return !q2_actor_live(g, id);
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_builtin_resource(&g->services, path, &sound, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = id,
                                               .resource = sound,
                                               .origin = body.origin,
                                               .channel = 3,
                                               .volume = 1,
                                               .attenuation = 1,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_item_visual(qa_q2_game *g, q2_actor *a, qa_error *e) {
    a->item->visual.visible = a->item->visible;
    return q2_publish_visual(g, a->id, &a->item->visual, e);
}
bool q2_item_change_collision(qa_q2_game *g, q2_actor *a, qa_physics_solid solid, qa_error *e) {
    if (!a->entity_targets && !q2_entity_bind(g, a, e))
        return false;
    if (!a->item->observation.serial && !q2_item_observe(g, a, e))
        return false;
    a->physics_bound = true;
    a->physics.solid = solid;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = 1,
                                    .owner = a->item->owner,
                                    .role = solid == QA_PHYSICS_TRIGGER ? QA_COLLISION_TRIGGER
                                                                        : QA_COLLISION_SOLID};
    return qa_world_set_collision(g->services.world, a->id,
                                  solid == QA_PHYSICS_NOT_SOLID ? NULL : &collision, e) &&
           (!q2_actor_live(g, a->id) || qa_world_link(g->services.world, a->id, NULL, e));
}
bool q2_item_hide(qa_q2_game *g, q2_actor *a, q2_item_think think, uint64_t due, qa_error *e) {
    a->item->retained = true;
    a->item->visible = false;
    a->item->touchable = false;
    a->item->think = think;
    a->item->due_ns = due;
    return q2_item_change_collision(g, a, QA_PHYSICS_NOT_SOLID, e) &&
           (!q2_actor_live(g, a->id) || q2_item_visual(g, a, e));
}
static bool suppressed(qa_q2_game *g, const qa_q2_item_definition *d, bool *out, qa_error *e) {
    *out = true;
    uint32_t flags = g->options.deathmatch_flags;
    if (g->options.deathmatch) {
        if ((flags & 1) && (d->kind == QA_Q2_ITEM_HEALTH || d->kind == QA_Q2_ITEM_MAX_HEALTH ||
                            d->kind == QA_Q2_ITEM_FOOD))
            return true;
        if ((flags & 2) && (d->kind == QA_Q2_ITEM_POWER || d->kind == QA_Q2_ITEM_SPHERE ||
                            d->kind == QA_Q2_ITEM_DECOY))
            return true;
        if ((flags & 2048) && (d->kind == QA_Q2_ITEM_ARMOR || d->kind == QA_Q2_ITEM_SHARD ||
                               d->kind == QA_Q2_ITEM_POWER_ARMOR))
            return true;
        if ((flags & 8192) && ((d->kind == QA_Q2_ITEM_AMMO && d->weapon == QA_Q2_WEAPON_NONE) ||
                               d->weapon == QA_Q2_BFG))
            return true;
        if (g->options.edition == QA_Q2_CLASSIC && g->options.product == QA_Q2_ROGUE &&
            (((flags & 0x20000) &&
              (!strcmp(d->classname, "ammo_prox") || !strcmp(d->classname, "ammo_tesla"))) ||
             ((flags & 0x80000) && d->kind == QA_Q2_ITEM_NUKE) ||
             ((flags & 0x100000) && d->kind == QA_Q2_ITEM_SPHERE)))
            return true;
        if (g->options.edition == QA_Q2_RERELEASE) {
            const qa_q2_item_options *options = &g->item_runtime->options;
            if ((options->no_mines &&
                 (d->weapon == QA_Q2_PROXLAUNCHER || !strcmp(d->classname, "ammo_prox") ||
                  !strcmp(d->classname, "ammo_tesla") || !strcmp(d->classname, "ammo_trap"))) ||
                (options->no_nukes && d->kind == QA_Q2_ITEM_NUKE) ||
                (options->no_spheres && d->kind == QA_Q2_ITEM_SPHERE))
                return true;
        }
        if (g->options.edition == QA_Q2_CLASSIC &&
            g->arsenal_rules == QA_Q2_WEAPON_RULES_LMCTF) {
            float source_flags, disabled;
            if (!qa_q2_source_value(g, "ctfflags", 0, &source_flags, e) ||
                !qa_q2_source_value(g, "disabled_weps", 0, &disabled, e))
                return false;
            if (!isfinite(source_flags) || !isfinite(disabled) ||
                (double)source_flags < INT32_MIN || (double)source_flags > INT32_MAX ||
                (double)disabled < INT32_MIN || (double)disabled > INT32_MAX) {
                qa_error_set(e, QA_ERROR_FORMAT, 0, "LMCTF item flags exceed their source integer domain");
                return false;
            }
            if (((uint32_t)(int32_t)source_flags & 2u) == 0 &&
                d->kind == QA_Q2_ITEM_POWER && d->powerup == QA_Q2_POWER_INVULNERABILITY)
                return true;
            static const struct { const char *classname; uint32_t mask; } weapons[] = {
                {"weapon_bfg", 1}, {"weapon_hyperblaster", 2}, {"weapon_railgun", 4},
                {"weapon_rocketlauncher", 8}, {"weapon_grenadelauncher", 16},
                {"weapon_chaingun", 32}, {"weapon_machinegun", 64},
                {"weapon_supershotgun", 128}, {"weapon_shotgun", 256}, {"weapon_plasma", 512},
            };
            uint32_t mask = (uint32_t)(int32_t)disabled;
            for (size_t i = 0; i < sizeof(weapons) / sizeof(*weapons); ++i)
                if ((mask & weapons[i].mask) != 0 && !strcmp(d->classname, weapons[i].classname))
                    return true;
        }
    }
    if (g->options.edition == QA_Q2_CLASSIC && g->options.product == QA_Q2_ROGUE) {
        if (d->weapon == QA_Q2_DISINTEGRATOR || !strcmp(d->classname, "ammo_disruptor"))
            return true;
        if (!g->options.deathmatch && (d->kind == QA_Q2_ITEM_NUKE || d->kind == QA_Q2_ITEM_DECOY ||
                                       !strcmp(d->classname, "item_sphere_hunter") ||
                                       !strcmp(d->classname, "item_sphere_vengeance")))
            return true;
    }
    *out = false;
    return true;
}
bool qa_q2_item_spawn_actor(qa_q2_game *g, qa_actor_id id, const qa_q2_item_spawn *spawn,
                            bool *handled, qa_error *e) {
    if (!g || !spawn || !spawn->classname || !handled || !q2_actor_live(g, id) ||
        !isfinite(spawn->delay)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item spawn");
        return false;
    }
    const char *name = spawn->classname;
    bool rogue = g->options.product == QA_Q2_ROGUE, rr = g->options.edition == QA_Q2_RERELEASE;
    if (rogue || rr) {
        if (!strcmp(name, "weapon_nailgun"))
            name = "weapon_etf_rifle";
        else if (!strcmp(name, "ammo_nails"))
            name = "ammo_flechettes";
        else if (!strcmp(name, "weapon_heatbeam"))
            name = "weapon_plasmabeam";
    }
    if (rogue && !rr) {
        if (!strcmp(name, "ammo_magslug"))
            name = "ammo_flechettes";
        else if (!strcmp(name, "ammo_trap"))
            name = "weapon_proxlauncher";
        else if (!strcmp(name, "weapon_boomer"))
            name = "weapon_etf_rifle";
        else if (!strcmp(name, "weapon_phalanx"))
            name = "weapon_plasmabeam";
        else if (!strcmp(name, "item_quadfire")) {
            float chance = q2_random(g);
            name = chance < .2f   ? "item_sphere_hunter"
                   : chance < .6f ? "item_sphere_vengeance"
                                  : "item_sphere_defender";
        }
    }
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, name);
    *handled = d != NULL;
    if (!d)
        return true;
    bool suppress;
    if (!suppressed(g, d, &suppress, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (suppress)
        return qa_session_release(g->services.session, id, e);
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        return false;
    if (a->item) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 actor already has item state");
        return false;
    }
    a->item = calloc(1, sizeof(*a->item));
    if (!a->item) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 pickup state");
        return false;
    }
    q2_item_state *item = a->item;
    item->definition = d;
    item->spawn = *spawn;
    item->spawn.classname = d->classname;
    if (rogue && !rr && item->spawn.spawnflags > 1 && strcmp(d->classname, "key_power_cube"))
        item->spawn.spawnflags = 0;
    if (g->options.cooperative && (!strcmp(d->classname, "key_power_cube") ||
                                   (rr && !strcmp(d->classname, "key_explosive_charges")))) {
        item->spawn.spawnflags |= UINT32_C(1) << ((8 + g->item_runtime->cubes++) & 31);
    }
    item->think = Q2_ITEM_FLOOR;
    item->due_ns = q2_deadline(q2_deadline(g->now_ns, g->frame_ns), g->frame_ns);
    item->visual = (qa_q2_visual){
        .scale = 1, .alpha = 1, .old_frame = -1, .effects = d->rotate ? 1 : 0, .render_flags = 512};
    if (!strcmp(d->classname, "key_commander_head"))
        item->visual.effects |= 2;
    if (!qa_builtin_resource(&g->services, d->model, &item->visual.models[0], e))
        return false;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = rr;
    a->physics_bound = true;
    if (d->kind == QA_Q2_ITEM_FOOD)
        item->spawn.spawnflags |= 0x10000;
    return q2_item_change_collision(g, a, QA_PHYSICS_NOT_SOLID, e);
}
bool qa_q2_item_enable(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (!a || !a->item)
        return false;
    a->item->visible = true;
    a->item->touchable = (a->item->spawn.spawnflags & 2) == 0;
    return q2_item_change_collision(g, a, a->item->touchable ? QA_PHYSICS_TRIGGER : QA_PHYSICS_BOX,
                                    e) &&
           (!q2_actor_live(g, id) || q2_item_visual(g, a, e));
}
bool q2_item_finish(qa_q2_game *g, q2_actor *a, qa_actor_id player, qa_error *e) {
    q2_item_state *item = a->item;
    const qa_q2_item_definition *d = item->definition;
    bool dropped = (item->spawn.spawnflags & 0x30000) != 0;
    if (d->kind == QA_Q2_ITEM_KEY)
        return true;
    if (d->kind == QA_Q2_ITEM_WEAPON && !dropped &&
        (g->options.cooperative || (g->options.deathmatch && (g->options.deathmatch_flags & 4)))) {
        item->retained = true;
        return true;
    }
    if (d->kind == QA_Q2_ITEM_HEALTH && d->timed) {
        item->owner = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, player);
        return q2_item_hide(g, a, Q2_ITEM_MEGA, q2_deadline(g->now_ns, 5 * Q2_NS), e);
    }
    if (!dropped && g->options.deathmatch) {
        float seconds = d->kind == QA_Q2_ITEM_WEAPON
                            ? g->item_runtime->options.weapon_respawn_seconds
                            : d->respawn_seconds;
        return q2_item_hide(g, a, Q2_ITEM_RESPAWN, q2_deadline(g->now_ns, q2_item_seconds(seconds)),
                            e);
    }
    return true;
}
static bool respawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_actor *selected = a;
    q2_item_state *item = a->item;
    size_t count = 1;
    if (item->spawn.team) {
        count = 0;
        qa_actor_id cursor = qa_actor_reference_resolve(qa_session_actors(g->services.session),
            item->spawn.team_master);
        while (q2_actor_live(g, cursor)) {
            q2_actor *member = q2_actor_get(g, cursor, false, NULL);
            if (!member || (!member->item && !member->entity))
                break;
            if (++count > g->capacity) {
                qa_error_set(e, QA_ERROR_FORMAT, a->id.slot, "Cyclic Q2 item team chain");
                return false;
            }
            cursor = member->item ? qa_actor_reference_resolve(qa_session_actors(g->services.session),
                member->item->spawn.team_next) : (qa_actor_id){0};
        }
    }
    float choice = q2_random(g);
    if (count && item->spawn.team) {
        size_t index = (size_t)(choice * (float)count);
        if (index >= count)
            index = count - 1;
        qa_actor_id cursor = qa_actor_reference_resolve(qa_session_actors(g->services.session),
            item->spawn.team_master);
        for (size_t i = 0; i <= index; ++i) {
            selected = q2_actor_get(g, cursor, false, e);
            if (!selected)
                return false;
            cursor = selected->item ? qa_actor_reference_resolve(qa_session_actors(g->services.session),
                selected->item->spawn.team_next) : (qa_actor_id){0};
        }
    }
    if (selected->item && selected->item->definition && g->options.edition == QA_Q2_CLASSIC &&
        !q2_item_randomize(g, &selected, e))
        return false;
    if (!q2_actor_live(g, selected->id))
        return true;
    if (selected->item) {
        selected->item->visible = true;
        selected->item->touchable = true;
        if (!q2_item_change_collision(g, selected, QA_PHYSICS_TRIGGER, e) ||
            (q2_actor_live(g, selected->id) && !q2_item_visual(g, selected, e)))
            return false;
    } else {
        selected->entity->visual.visible = true;
        if (!q2_entity_solid(g, selected, QA_PHYSICS_TRIGGER, e) ||
            (q2_actor_live(g, selected->id) && !q2_entity_show(g, selected, e)))
            return false;
    }
    if (!q2_actor_live(g, selected->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, selected->id, &body, e))
        return false;
    if (!qa_builtin_emit(&g->services,
                         &(qa_builtin_event){.kind = QA_BUILTIN_ITEM,
                                             .family = QA_GAME_Q2,
                                             .provider = g->options.owner,
                                             .actor = selected->id,
                                             .origin = body.origin,
                                             .code = 1,
                                             .time_ns = g->now_ns},
                         e))
        return false;
    return !q2_actor_live(g, selected->id) || !selected->item || !selected->item->definition ||
           g->options.edition != QA_Q2_RERELEASE || q2_item_randomize(g, &selected, e);
}
static bool floor_item(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    body.bounds = (qa_bounds){{-15, -15, -15}, {15, 15, 15}};
    qa_trace_query query = {.start = body.origin,
                            .end = qa_vec_add(body.origin, qa_v3(0, 0, -128)),
                            .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                            .pass_actor = a->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = 3;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    if (trace.start_solid)
        return qa_session_release(g->services.session, a->id, e);
    body.origin = trace.end;
    if (!qa_world_body_write(g->services.world, a->id, &body, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    a->physics.motion = QA_PHYSICS_TOSS;
    a->item->visible = true;
    a->item->touchable = true;
    qa_physics_solid solid = QA_PHYSICS_TRIGGER;
    if (a->item->spawn.team) {
        a->physics.flags &= ~(uint32_t)QA_PHYSICS_TEAM_SLAVE;
        if (a->entity) {
            a->item->spawn.team_master = a->entity->team_master;
            a->item->spawn.team_next = a->entity->team_next;
            a->entity->team_next = (qa_actor_reference){0};
        }
        a->item->visible = false;
        solid = QA_PHYSICS_NOT_SOLID;
        if (qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(g->services.session),
                a->item->spawn.team_master), a->id)) {
            a->item->think = Q2_ITEM_RESPAWN;
            a->item->due_ns = q2_deadline(g->now_ns, g->frame_ns);
        }
    }
    if (a->item->spawn.spawnflags & 2) {
        a->item->touchable = false;
        a->item->visual.effects &= ~1u;
        a->item->visual.render_flags &= ~512u;
        solid = QA_PHYSICS_BOX;
    }
    if (a->item->spawn.spawnflags & 1) {
        a->item->visible = false;
        solid = QA_PHYSICS_NOT_SOLID;
    }
    return q2_item_change_collision(g, a, solid, e) &&
           (!q2_actor_live(g, a->id) || q2_item_visual(g, a, e));
}
bool q2_item_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_item_state *item = a->item;
    if (!item)
        return true;
    if (item->companion)
        return q2_companion_tick(g, a, e);
    if (item->think == Q2_ITEM_IDLE || g->now_ns < item->due_ns)
        return true;
    q2_item_think think = item->think;
    item->think = Q2_ITEM_IDLE;
    switch (think) {
    case Q2_ITEM_IDLE:
        return true;
    case Q2_ITEM_FLOOR:
        return floor_item(g, a, e);
    case Q2_ITEM_RESPAWN:
        return respawn(g, a, e);
    case Q2_ITEM_EXPIRE:
        return qa_session_release(g->services.session, a->id, e);
    case Q2_ITEM_DROPPED:
        item->temporary = false;
        item->touchable = true;
        if (item->expires_ns || g->options.deathmatch) {
            item->think = Q2_ITEM_EXPIRE;
            item->due_ns = item->expires_ns ? item->expires_ns : q2_deadline(g->now_ns, 29 * Q2_NS);
        }
        return true;
    case Q2_ITEM_MEGA: {
        qa_actor_id owner = qa_actor_reference_resolve(qa_session_actors(g->services.session), item->owner);
        if (q2_actor_live(g, owner)) {
            qa_combat_state state;
            if (!qa_combat_read(g->services.combat, owner, &state, e))
                return false;
            q2_power_state *power = q2_powers(g, owner, e);
            if (!power)
                return false;
            if (state.health > power->maximum_health) {
                if (!qa_combat_set_health(g->services.combat, owner, state.health - 1, e))
                    return false;
                if (q2_actor_live(g, a->id)) {
                    item->think = Q2_ITEM_MEGA;
                    item->due_ns = q2_deadline(g->now_ns, Q2_NS);
                }
                return true;
            }
        }
        return !(item->spawn.spawnflags & 0x30000) && g->options.deathmatch
                   ? q2_item_hide(g, a, Q2_ITEM_RESPAWN, q2_deadline(g->now_ns, 20 * Q2_NS), e)
                   : qa_session_release(g->services.session, a->id, e);
    }
    }
    return true;
}

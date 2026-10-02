#include "internal.h"
#include "qa/game_q2_monsters.h"

static bool beam(qa_q2_game *g, q2_actor *a, qa_vec3 start, qa_vec3 end, bool visible,
                 qa_error *e) {
    q2_entity_state *s = a->entity;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_BEAM,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .origin = start,
                                               .end = end,
                                               .value = (float)s->visual.frame,
                                               .code = s->visual.skin,
                                               .flags = visible ? 1u : 0u,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_laser_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    int sparks = (s->spawnflags & 0x80000000u) ? 8 : 4;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (q2_actor_live(g, s->enemy)) {
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, s->enemy, &target, e))
            return false;
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(
            qa_vec_add(target.origin,
                       qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), .5f)),
            body.origin));
        if (direction.x != s->direction.x || direction.y != s->direction.y ||
            direction.z != s->direction.z)
            s->spawnflags |= 0x80000000u;
        s->direction = direction;
    }
    qa_trace_query query = {
        .start = body.origin,
        .end = qa_vec_add(body.origin, qa_vec_scale(s->direction, 2048)),
        .shape = {.kind = QA_SHAPE_POINT},
        .pass_actor = a->id,
        .policy = {.family = QA_COLLISION_Q2,
                   .contents_mask = 0x6000001,
                   .q2_merged_contents = g->options.edition == QA_Q2_RERELEASE}};
    q2_trace_frame *frame = q2_scratch_acquire(g, e);
    if (!frame)
        return false;
    frame->snapshot.count = 0;
    bool okay = false;
    qa_vec3 terminal = query.end;
    for (;;) {
        qa_trace_result hit;
        if (!qa_world_trace_excluding(g->services.world, &query, frame->snapshot.ids,
                                      frame->snapshot.count, &hit, e))
            goto out;
        terminal = hit.end;
        qa_actor_id actor = hit.hit == QA_TRACE_HIT_ACTOR ? hit.actor : (qa_actor_id){0};
        qa_builtin_actor_traits traits = {0};
        if (actor.registry && g->services.actor_traits)
            g->services.actor_traits(g->services.context, actor, &traits);
        if (actor.registry && !traits.laser_immune && q2_target_damageable(g, actor)) {
            qa_attack attack = {
                .inflictor = a->id,
                .attacker = s->activator,
                .combat_provider = g->options.owner,
                .cause = qa_q2_damage_cause(g->options.edition, g->options.product, 30, 4)};
            if (!q2_damage(g, &attack, actor, s->damage, 1, s->direction, hit.end, qa_v3(0, 0, 0),
                           false, e))
                goto out;
            if (!q2_actor_live(g, a->id)) {
                okay = true;
                goto out;
            }
            traits = (qa_builtin_actor_traits){0};
            if (q2_actor_live(g, actor) && g->services.actor_traits)
                g->services.actor_traits(g->services.context, actor, &traits);
        }
        if (!actor.registry || (!traits.player && !traits.monster)) {
            if (hit.fraction < 1 && (s->spawnflags & 0x80000000u)) {
                s->spawnflags &= ~0x80000000u;
                qa_string_id effect;
                if (!qa_builtin_resource(&g->services, "q2:laser-sparks", &effect, e) ||
                    !qa_builtin_emit(&g->services,
                                     &(qa_builtin_event){.kind = QA_BUILTIN_IMPACT,
                                                         .family = QA_GAME_Q2,
                                                         .provider = g->options.owner,
                                                         .actor = a->id,
                                                         .resource = effect,
                                                         .origin = hit.end,
                                                         .direction = hit.contact
                                                                          ? hit.contact_plane.normal
                                                                          : qa_v3(0, 0, 0),
                                                         .count = sparks,
                                                         .code = s->visual.skin & 255,
                                                         .time_ns = g->now_ns},
                                     e))
                    goto out;
            }
            break;
        }
        if (!qa_builtin_snapshot_reserve(&frame->snapshot, frame->snapshot.count + 1, e))
            goto out;
        frame->snapshot.ids[frame->snapshot.count++] = actor;
        query.pass_actor = actor;
        query.start = hit.end;
    }
    if (!q2_actor_live(g, a->id)) {
        okay = true;
        goto out;
    }
    s->beam_end = terminal;
    if (!beam(g, a, body.origin, terminal, true, e))
        goto out;
    okay = !q2_actor_live(g, a->id) ||
           q2_entity_schedule(g, a, Q2ET_LASER, (float)g->frame_ns / Q2_NS);
out:
    frame->active = false;
    return okay;
}
static bool laser_switch(qa_q2_game *g, q2_actor *a, bool on, qa_error *e) {
    q2_entity_state *s = a->entity;
    s->visual.visible = on;
    if (on) {
        if (!s->activator.registry)
            s->activator = a->id;
        s->spawnflags |= 0x80000001u;
        return q2_laser_think(g, a, e);
    }
    s->spawnflags &= ~1u;
    q2_entity_schedule(g, a, Q2ET_NONE, 0);
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e) || !q2_entity_show(g, a, e))
        return false;
    return !q2_actor_live(g, a->id) || beam(g, a, b.origin, b.origin, false, e);
}
static bool laser_start(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    s->visual.frame = (s->spawnflags & 64) ? 16 : 4;
    s->visual.render_flags |= 0xa0;
    uint32_t color = (s->spawnflags & 2)    ? 0xf2f2f0f0u
                     : (s->spawnflags & 4)  ? 0xd0d1d2d3u
                     : (s->spawnflags & 8)  ? 0xf3f3f1f1u
                     : (s->spawnflags & 16) ? 0xdcdddedfu
                     : (s->spawnflags & 32) ? 0xe0e1e2e3u
                                            : 0;
    s->visual.skin = (int32_t)color;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    if (!s->enemy.registry) {
        if (s->target)
            q2_map_find(g, NULL, s->target, 0, &s->enemy);
        else {
            s->direction = q2_movedir(b.angles);
            b.angles = qa_v3(0, 0, 0);
        }
    }
    if (!s->damage)
        s->damage = 1;
    b.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
    s->usable = true;
    if (!q2_entity_body(g, a, &b, true, e))
        return false;
    return !q2_actor_live(g, a->id) || laser_switch(g, a, (s->spawnflags & 1) != 0, e);
}
static bool ramp(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_actor *target = q2_ent(g, s->enemy);
    if (!target)
        return true;
    float elapsed = (float)(g->now_ns - s->timestamp_ns) / Q2_NS;
    float level = 97 + s->direction.x + elapsed / s->speed * (s->direction.y - s->direction.x);
    uint8_t byte = isfinite(level) ? (uint8_t)(int)fmodf(truncf(level), 256) : 0;
    qa_string_id text;
    char pattern[2] = {(char)byte, 0};
    if (!qa_builtin_resource(&g->services, pattern, &text, e) ||
        !q2_map_event(g,
                      &(qa_q2_map_event){.kind = QA_Q2_MAP_LIGHTSTYLE,
                                         .actor = a->id,
                                         .style = target->entity->style,
                                         .text = text},
                      e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (elapsed < s->speed)
        return q2_entity_schedule(g, a, Q2ET_LIGHTRAMP, (float)g->frame_ns / Q2_NS);
    if (s->spawnflags & 1) {
        float from = s->direction.x;
        s->direction.x = s->direction.y;
        s->direction.y = from;
    }
    return true;
}
static bool quake(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->sound_ns < g->now_ns) {
        if (!q2_entity_sound(g, a, "world/quake.wav", 0, 1, 0, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->sound_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
    }
    q2_trace_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = false;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id id = players->snapshot.ids[i];
        if (!q2_actor_live(g, id))
            continue;
        qa_body_state body;
        qa_combat_state health;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            goto out;
        if (!body.ground.registry)
            continue;
        if (!qa_combat_read(g->services.combat, id, &health, e))
            goto out;
        body.ground = (qa_actor_id){0};
        body.velocity.x += q2_crandom(g) * 150;
        body.velocity.y += q2_crandom(g) * 150;
        body.velocity.z = s->speed * (100 / (health.mass ? health.mass : 200));
        if (!qa_world_body_write(g->services.world, id, &body, e))
            goto out;
        if (!q2_actor_live(g, a->id)) {
            okay = true;
            goto out;
        }
    }
    okay = g->now_ns >= s->expires_ns ||
           q2_entity_schedule(g, a, Q2ET_QUAKE, (float)g->frame_ns / Q2_NS);
out:
    players->active = false;
    return okay;
}
static bool steam_start(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    if (s->target)
        q2_map_find(g, NULL, s->target, 0, &s->enemy);
    else {
        s->direction = q2_movedir(b.angles);
        b.angles = qa_v3(0, 0, 0);
        if (!q2_entity_body(g, a, &b, true, e))
            return false;
    }
    s->count = (s->count ? s->count : 32) & 255;
    if (!s->speed)
        s->speed = 75;
    uint32_t color = q2_actor_field_flags(g, a->id, "sounds");
    s->style = (int)((color ? color : 8) & 255);
    s->wait *= 1000;
    s->usable = true;
    s->visual.visible = false;
    return q2_entity_show(g, a, e);
}
bool q2_target_extra_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    static const struct {
        const char *name;
        q2_entity_kind kind;
    } kinds[] = {{"target_temp_entity", Q2E_TEMP},
                 {"target_spawner", Q2E_SPAWNER},
                 {"target_blaster", Q2E_BLASTER},
                 {"target_crosslevel_trigger", Q2E_CROSS_TRIGGER},
                 {"target_crosslevel_target", Q2E_CROSS_TARGET},
                 {"target_laser", Q2E_LASER},
                 {"target_lightramp", Q2E_LIGHTRAMP},
                 {"target_earthquake", Q2E_EARTHQUAKE},
                 {"target_steam", Q2E_STEAM},
                 {"target_anger", Q2E_ANGER},
                 {"target_killplayers", Q2E_KILLPLAYERS},
                 {"target_soundfx", Q2E_SOUND_FX},
                 {"target_gravity", Q2E_GLOBAL_GRAVITY}};
    size_t i;
    for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
        if (!strcmp(name, kinds[i].name)) {
            s->kind = kinds[i].kind;
            break;
        }
    if (i == sizeof(kinds) / sizeof(*kinds)) {
        *handled = false;
        return true;
    }
    s->usable = true;
    s->visual.visible = false;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    switch (s->kind) {
    case Q2E_SPAWNER:
        if (s->speed) {
            s->direction = qa_vec_scale(q2_movedir(b.angles), s->speed);
            b.angles = qa_v3(0, 0, 0);
            return q2_entity_body(g, a, &b, false, e);
        }
        return true;
    case Q2E_BLASTER:
        if (!s->damage)
            s->damage = 15;
        if (!s->speed)
            s->speed = 1000;
        s->direction = q2_movedir(b.angles);
        b.angles = qa_v3(0, 0, 0);
        return q2_entity_body(g, a, &b, false, e);
    case Q2E_CROSS_TARGET:
        s->usable = false;
        if (!s->delay)
            s->delay = 1;
        return q2_entity_schedule(g, a, Q2ET_CROSS, s->delay);
    case Q2E_LASER:
        s->usable = false;
        return q2_entity_schedule(g, a, Q2ET_LASER_START, 1);
    case Q2E_LIGHTRAMP: {
        const char *message = qa_strings_cstr(qa_session_strings(g->services.session), s->message);
        if (!message || strlen(message) != 2 || message[0] < 'a' || message[0] > 'z' || message[1] < 'a' ||
            message[1] > 'z' || message[0] == message[1] || !s->target || g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        s->direction = qa_v3((float)(message[0] - 97), (float)(message[1] - 97), 0);
        return true;
    }
    case Q2E_EARTHQUAKE:
        if (!s->count)
            s->count = 5;
        if (!s->speed)
            s->speed = 200;
        s->wait = 0;
        return true;
    case Q2E_STEAM:
        s->usable = false;
        return s->target ? q2_entity_schedule(g, a, Q2ET_STEAM, 1) : steam_start(g, a, e);
    case Q2E_SOUND_FX: {
        const char *sound = NULL;
        switch ((int)q2_field_float(g, s, "noise", 0)) {
        case 1:
            sound = "world/x_alarm.wav";
            break;
        case 2:
            sound = "world/flyby1.wav";
            break;
        case 4:
            sound = "world/amb12.wav";
            break;
        case 5:
            sound = "world/amb17.wav";
            break;
        case 7:
            sound = "world/bigpump2.wav";
            break;
        }
        if (!s->volume)
            s->volume = 1;
        s->attenuation = s->attenuation == -1 ? 0 : s->attenuation ? s->attenuation : 1;
        if (!sound) {
            s->usable = false;
            return true;
        }
        return qa_builtin_resource(&g->services, sound, &s->noise, e);
    }
    default:
        return true;
    }
}
static bool kill_players(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_trace_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    q2_trace_frame *targets = q2_scratch_acquire(g, e);
    if (!targets) {
        players->active = false;
        return false;
    }
    bool okay = false;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id id = players->snapshot.ids[i];
        if (q2_actor_live(g, id) && !q2_entity_damage(g, a, id, a->id, 100000, 0, 21, 32, e))
            goto out;
        if (!q2_actor_live(g, a->id)) {
            okay = true;
            goto out;
        }
    }
    const qa_actor_registry *registry = qa_session_actors(g->services.session);
    if (!qa_builtin_snapshot_reserve(&targets->snapshot, qa_actors_count(registry), e))
        goto out;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    targets->snapshot.count = 0;
    while (qa_actors_next(registry, &cursor, &record))
        targets->snapshot.ids[targets->snapshot.count++] = record->id;
    for (size_t j = 0; j < targets->snapshot.count; j++) {
        qa_actor_id id = targets->snapshot.ids[j];
        if (!q2_actor_live(g, id) || !q2_target_damageable(g, id))
            continue;
        qa_combat_state health;
        qa_body_state body;
        if (!qa_combat_read(g->services.combat, id, &health, e) ||
            !qa_world_body_read(g->services.world, id, &body, e))
            goto out;
        if (health.health < 1)
            continue;
        for (size_t i = 0; i < players->snapshot.count; i++) {
            qa_actor_id player = players->snapshot.ids[i];
            if (!q2_actor_live(g, player))
                continue;
            qa_body_state from;
            if (!qa_world_body_read(g->services.world, player, &from, e))
                goto out;
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits)
                g->services.actor_traits(g->services.context, player, &traits);
            from.origin.z += traits.view_height;
            qa_trace_result hit;
            if (!q2_player_trace(g, player, from.origin, body.origin, NULL, 25, &hit, e))
                goto out;
            if (hit.fraction == 1) {
                if (!q2_entity_damage(g, a, id, a->id, health.health, 0, 21, 32, e))
                    goto out;
                break;
            }
        }
        if (!q2_actor_live(g, a->id)) {
            okay = true;
            goto out;
        }
    }
    okay = true;
out:
    players->active = false;
    targets->active = false;
    return okay;
}
bool q2_target_extra_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                         bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    switch (s->kind) {
    case Q2E_LASER:
        s->activator = activator;
        return laser_switch(g, a, !(s->spawnflags & 1), e);
    case Q2E_LIGHTRAMP: {
        if (!q2_actor_live(g, s->enemy)) {
            size_t n = 0;
            qa_actor_id target;
            while (q2_map_find(g, "light", s->target, n++, &target))
                s->enemy = target;
            if (!s->enemy.registry)
                return qa_session_release(g->services.session, a->id, e);
        }
        s->timestamp_ns = g->now_ns;
        return ramp(g, a, e);
    }
    case Q2E_TEMP: {
        qa_body_state b;
        char effect[32];
        snprintf(effect, sizeof(effect), "q2:temp-%u", (unsigned)s->style & 255);
        return qa_world_body_read(g->services.world, a->id, &b, e) &&
               q2_projectile_event(g, a->id, QA_BUILTIN_IMPACT, effect, 0, b.origin, qa_v3(0, 0, 0),
                                   e);
    }
    case Q2E_SPAWNER: {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->spawn) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 target spawner requires shared factory");
            return false;
        }
        qa_body_state b;
        qa_actor_id spawned;
        if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
            !services->spawn(services->context,
                             qa_strings_cstr(qa_session_strings(g->services.session), s->target),
                             b.origin, b.angles, qa_v3(0, 0, 0), &spawned, e))
            return false;
        if (!q2_actor_live(g, spawned))
            return true;
        bool clear;
        if (!qa_world_unlink(g->services.world, spawned, e) ||
            !q2_killbox(g, spawned, spawned, false, false, &clear, e))
            return false;
        if (!q2_actor_live(g, spawned) || !q2_actor_live(g, a->id))
            return true;
        if (!qa_world_link(g->services.world, spawned, NULL, e))
            return false;
        if (s->speed && q2_actor_live(g, spawned)) {
            if (!qa_world_body_read(g->services.world, spawned, &b, e))
                return false;
            b.velocity = s->direction;
            return qa_world_body_write(g->services.world, spawned, &b, e);
        }
        return true;
    }
    case Q2E_BLASTER: {
        qa_body_state b;
        uint64_t effects = g->options.edition == QA_Q2_CLASSIC ? 8
                           : (s->spawnflags & 2)               ? 0
                           : (s->spawnflags & 1)               ? 64
                                                               : 8;
        if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
            !q2_fire_actor_bolt(g, a->id, a->id, b.origin, s->direction, s->damage, s->speed,
                                effects, 33, false, e))
            return false;
        return !q2_actor_live(g, a->id) ||
               q2_entity_sound(g, a, "weapons/laser2.wav", 2, 1, 1, 0, e);
    }
    case Q2E_CROSS_TRIGGER: {
        uint32_t flags;
        if (!q2_entity_flags(g, false, false, &flags, e))
            return false;
        flags |= s->spawnflags;
        return q2_entity_flags(g, true, false, &flags, e) &&
               (!q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e));
    }
    case Q2E_EARTHQUAKE:
        s->expires_ns = q2_deadline(g->now_ns, q2_item_seconds((float)s->count));
        s->sound_ns = 0;
        s->activator = activator;
        return q2_entity_schedule(g, a, Q2ET_QUAKE, (float)g->frame_ns / Q2_NS);
    case Q2E_GLOBAL_GRAVITY: {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->world_gravity) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 world gravity service is not installed");
            return false;
        }
        return services->world_gravity(services->context, q2_field_float(g, s, "gravity", 0), e);
    }
    case Q2E_SOUND_FX:
        return q2_entity_schedule(g, a, Q2ET_SOUND_FX, s->delay);
    case Q2E_STEAM: {
        q2_entities *r = g->entity_runtime;
        if (r->steam_id > 20000)
            r->steam_id %= 20000;
        r->steam_id++;
        if (!s->wait)
            s->wait = other.registry ? q2_actor_field_float(g, other, "wait", 0) * 1000 : 1000;
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        if (q2_actor_live(g, s->enemy)) {
            qa_body_state target;
            if (!qa_world_body_read(g->services.world, s->enemy, &target, e))
                return false;
            s->direction = qa_vec_normalize(qa_vec_sub(
                qa_vec_add(target.origin,
                           qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), .5f)),
                b.origin));
        }
        return q2_map_event(g,
                            &(qa_q2_map_event){.kind = QA_Q2_MAP_STEAM,
                                               .actor = a->id,
                                               .origin = b.origin,
                                               .direction = s->direction,
                                               .count = s->count,
                                               .style = s->style,
                                               .slot = s->wait > 100 ? r->steam_id : -1,
                                               .value = truncf(s->speed),
                                               .duration = s->wait > 100 ? truncf(s->wait) : 0},
                            e);
    }
    case Q2E_KILLPLAYERS:
        return kill_players(g, a, e);
    case Q2E_ANGER: {
        qa_actor_id target;
        if (!s->target || !q2_map_find(g, NULL, s->killtarget, 0, &target))
            return true;
        const qa_actor_record *record =
            qa_actors_get(qa_session_actors(g->services.session), target);
        if (!record)
            return true;
        q2_actor *native = q2_actor_get(g, target, false, NULL);
        if (record->owner == g->options.owner && native)
            native->physics.flags |= QA_PHYSICS_MONSTER;
        else {
            qa_q2_entity_services *services = &g->entity_runtime->services;
            if (!services->mark_monster_target) {
                qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                             "Q2 target anger requires foreign target classification");
                return false;
            }
            if (!services->mark_monster_target(services->context, target, e))
                return false;
        }
        if (!q2_actor_live(g, a->id) || a->entity != s || !q2_actor_live(g, target))
            return true;
        qa_combat_state c;
        qa_error missing = {0};
        bool has_combat = qa_combat_read_traits(g->services.combat, target, &c, &missing);
        if (!q2_actor_live(g, a->id) || a->entity != s || !q2_actor_live(g, target))
            return true;
        if (!has_combat) {
            if (missing.code != QA_ERROR_NOT_FOUND) {
                if (e)
                    *e = missing;
                return false;
            }
            c = (qa_combat_state){.health = 300};
            if (!qa_combat_create_actor(g->services.combat, target, &c, e))
                return false;
        } else if (!qa_combat_set_health(g->services.combat, target, 300, e))
            return false;
        if (!q2_actor_live(g, a->id) || a->entity != s || !q2_actor_live(g, target))
            return true;
        qa_target_cursor cursor = {0};
        qa_actor_id monster;
        while (qa_targets_next(g->entity_runtime->services.targets, s->target, &cursor, &monster)) {
            if (qa_actor_id_equal(monster, a->id))
                continue;
            if (!qa_combat_read(g->services.combat, monster, &c, &missing)) {
                if (missing.code == QA_ERROR_NOT_FOUND)
                    c.health = 0;
                else {
                    if (e)
                        *e = missing;
                    return false;
                }
            }
            if (!q2_actor_live(g, a->id) || a->entity != s || !q2_actor_live(g, target))
                return true;
            if (!q2_actor_live(g, monster))
                continue;
            if (c.health < 0)
                return true;
            qa_q2_monster_view view;
            if (qa_q2_monster_read(g, monster, &view)) {
                if (!qa_q2_monster_target_anger(g, monster, target, e))
                    return false;
            } else {
                qa_q2_entity_services *services = &g->entity_runtime->services;
                if (!services->target_anger) {
                    qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                                 "Q2 target anger requires foreign monster control");
                    return false;
                }
                if (!services->target_anger(services->context, monster, target, e))
                    return false;
            }
            if (!q2_actor_live(g, a->id) || a->entity != s || !q2_actor_live(g, target))
                return true;
        }
        return true;
    }
    default:
        *handled = false;
        return true;
    }
}
bool q2_target_extra_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled,
                           qa_error *e) {
    *handled = true;
    switch (think) {
    case Q2ET_LASER_START:
        return laser_start(g, a, e);
    case Q2ET_LASER:
        return q2_laser_think(g, a, e);
    case Q2ET_LIGHTRAMP:
        return ramp(g, a, e);
    case Q2ET_QUAKE:
        return quake(g, a, e);
    case Q2ET_STEAM:
        return steam_start(g, a, e);
    case Q2ET_SOUND_FX:
        return q2_entity_sound(
            g, a, qa_strings_cstr(qa_session_strings(g->services.session), a->entity->noise), 2,
            a->entity->volume, a->entity->attenuation, 0, e);
    case Q2ET_CROSS: {
        uint32_t flags;
        if (!q2_entity_flags(g, false, false, &flags, e))
            return false;
        if (a->entity->spawnflags != (flags & 255u & a->entity->spawnflags))
            return true;
        if (!q2_entity_targets(g, a, a->id, false, e))
            return false;
        return !q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e);
    }
    default:
        *handled = false;
        return true;
    }
}

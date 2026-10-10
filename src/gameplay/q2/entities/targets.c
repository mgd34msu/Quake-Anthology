#include "internal.h"

static bool style(qa_q2_game *g, q2_actor *a, const char *pattern, qa_error *e) {
    qa_string_id text;
    if (!qa_builtin_resource(&g->services, pattern, &text, e))
        return false;
    return q2_map_event(
        g,
        &(qa_q2_map_event){
            .kind = QA_Q2_MAP_LIGHTSTYLE, .actor = a->id, .style = a->entity->style, .text = text},
        e);
}
static bool speaker(qa_q2_game *g, q2_actor *a, int operation, qa_error *e) {
    q2_entity_state *s = a->entity;
    float attenuation = operation == 0                              ? s->attenuation
                        : g->options.edition == QA_Q2_CLASSIC       ? 1
                        : s->attenuation == -1                      ? 0
                        : s->attenuation > 0 && s->attenuation != 3 ? s->attenuation / 5
                                                                    : 1;
    return q2_entity_sound(g, a, qa_strings_cstr(qa_session_strings(g->services.session), s->noise),
                           2 | (s->spawnflags & 4 ? 16 : 0), operation ? 1 : s->volume, attenuation,
                           operation, e);
}
static bool explode(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
        !q2_projectile_event(g, a->id, QA_BUILTIN_EXPLOSION, "q2:explosion1", 1, b.origin,
                             qa_v3(0, 0, 0), e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    q2_entity_state *s = a->entity;
    if (!q2_entity_radius(g, a, s->activator, s->damage, s->damage + 40, 25, e))
        return false;
    return !q2_actor_live(g, a->id) || q2_entity_targets(g, a, s->activator, true, e);
}
static bool timer(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!q2_entity_targets(g, a, a->entity->activator, false, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    float delay = a->entity->wait + q2_crandom(g) * a->entity->random;
    return q2_entity_schedule(g, a, Q2ET_TIMER, delay);
}
static bool key_use(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    qa_builtin_actor_traits traits = {0};
    if (!g->services.actor_traits ||
        !g->services.actor_traits(g->services.context, activator, &traits) || !traits.player)
        return true;
    q2_entity_state *s = a->entity;
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, q2_field_text(g, s, "item"));
    if (!d) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 key trigger references unknown item");
        return false;
    }
    int count;
    if (!q2_count(g, activator, d->item, &count, e))
        return false;
    if (!count) {
        if (s->timestamp_ns > g->now_ns)
            return true;
        s->timestamp_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
        char message[256];
        snprintf(message, sizeof(message), "You need the %s", d->name);
        if (!q2_entity_message(g, a, activator, message, e))
            return false;
        return !q2_actor_live(g, activator) ||
               q2_player_sound(g, activator, "misc/keytry.wav", 0, e);
    }
    if (!q2_player_sound(g, activator, "misc/keyuse.wav", 0, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, activator))
        return true;
    bool cube =
        !strcmp(d->classname, "key_power_cube") ||
        (g->options.edition == QA_Q2_RERELEASE && !strcmp(d->classname, "key_explosive_charges"));
    unsigned bit = 0;
    if (cube && g->options.cooperative) {
        q2_power_state *p = q2_powers(g, activator, e);
        if (!p)
            return false;
        while (bit < 8 && !(p->power_cubes & (1u << bit)))
            bit++;
    }
    uint32_t cursor = 0;
    const qa_actor_record *r;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &r)) {
        qa_actor_id id = r->id;
        if (g->options.cooperative) {
            traits = (qa_builtin_actor_traits){0};
            if (!g->services.actor_traits(g->services.context, id, &traits) || !traits.player)
                continue;
        } else if (!qa_actor_id_equal(id, activator))
            continue;
        if (!q2_count(g, id, d->item, &count, e))
            return false;
        if (cube && g->options.cooperative) {
            q2_power_state *p = q2_powers(g, id, e);
            if (!p)
                return false;
            if (!(p->power_cubes & (1u << bit)))
                continue;
            p->power_cubes &= ~(1u << bit);
            count = 1;
        } else if (!g->options.cooperative)
            count = 1;
        bool consumed;
        if (!qa_inventory_consume(g->services.inventory, id, d->item, count, &consumed, e))
            return false;
        if (q2_client(g, id, NULL) && !qa_q2_player_consumed_key(g, id, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (!q2_entity_targets(g, a, activator, false, e))
        return false;
    if (q2_actor_live(g, a->id))
        s->usable = false;
    return true;
}
static qa_vec3 unrotate(qa_vec3 v, qa_vec3 angles) {
    float p = -angles.x * .01745329251994329577f, r = -angles.z * .01745329251994329577f,
          y = -angles.y * .01745329251994329577f;
    qa_vec3 x = qa_v3(v.x, v.y * cosf(p) - v.z * sinf(p), v.y * sinf(p) + v.z * cosf(p));
    qa_vec3 z = qa_v3(x.x * cosf(r) + x.z * sinf(r), x.y, -x.x * sinf(r) + x.z * cosf(r));
    return qa_v3(z.x * cosf(y) - z.y * sinf(y), z.x * sinf(y) + z.y * cosf(y), z.z);
}
static bool changelevel(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                        qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->active || qa_q2_players_in_intermission(g))
        return true;
    if (!g->options.deathmatch && !g->options.cooperative) {
        for (q2_actor *p = g->first_actor; p; p = p->live_next)
            if (p->client && p->client->info.connected && !p->client->info.slot) {
                qa_combat_state c;
                if (!qa_combat_read(g->services.combat, p->id, &c, e))
                    return false;
                if (c.health <= 0)
                    return true;
                break;
            }
    }
    qa_actor_id world = g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
    bool allow_exit = g->options.edition == QA_Q2_RERELEASE
                          ? g->player_runtime->rules.deathmatch_allow_exit
                          : (g->options.deathmatch_flags & 4096) != 0;
    if (g->options.deathmatch && !allow_exit && other.registry &&
        !qa_actor_id_equal(other, world)) {
        if (!q2_target_damageable(g, other))
            return true;
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits)
            g->services.actor_traits(g->services.context, other, &traits);
        return q2_entity_damage(g, a, other, a->id,
                                10 * (traits.max_health != 0 ? traits.max_health : 100), 1000, 28, 0, e);
    }
    if (g->options.edition == QA_Q2_RERELEASE && g->options.deathmatch && g->now_ns < 10 * Q2_NS)
        return true;
    const char *map = qa_strings_cstr(qa_session_strings(g->services.session), s->map);
    bool end = strchr(map, '*') != NULL;
    if (end) {
        uint32_t flags;
        if (!q2_entity_flags(g, false, false, &flags, e))
            return false;
        flags &= g->options.edition == QA_Q2_RERELEASE ? 0xff00u : ~255u;
        if (!q2_entity_flags(g, true, false, &flags, e))
            return false;
    }
    qa_q2_landmark carry = {0};
    const qa_q2_landmark *landmark = NULL;
    qa_actor_id reference;
    q2_actor *player = q2_client(g, activator, NULL);
    if (!g->options.deathmatch && player && q2_entity_pick(g, s->target, &reference)) {
        qa_body_state b, ref;
        qa_q2_player_movement movement;
        if (!qa_world_body_read(g->services.world, activator, &b, e) ||
            !qa_world_body_read(g->services.world, reference, &ref, e) ||
            !q2_player_observe(g, player, &movement, e))
            return false;
        q2_actor *target = q2_ent(g, reference);
        carry = (qa_q2_landmark){
            .player = activator,
            .name = target ? target->entity->targetname : 0,
            .relative_origin = unrotate(qa_vec_sub(b.origin, ref.origin), ref.angles),
            .relative_velocity = unrotate(player->client->rule.old_velocity, ref.angles),
            .relative_view_angles = qa_vec_sub(movement.view_angles, ref.angles)};
        landmark = &carry;
    }
    s->active = true;
    if (g->options.edition == QA_Q2_RERELEASE)
        return qa_q2_players_intermission(g, map, landmark, s->spawnflags, e);
    return q2_map_transition(g, a->id, activator, s->map, landmark, end, e);
}
bool q2_target_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    static const struct {
        const char *name;
        q2_entity_kind kind;
    } kinds[] = {{"worldspawn", Q2E_WORLD},
                 {"trigger_relay", Q2E_RELAY},
                 {"trigger_counter", Q2E_COUNTER},
                 {"trigger_key", Q2E_KEY},
                 {"target_speaker", Q2E_SPEAKER},
                 {"func_timer", Q2E_TIMER},
                 {"light", Q2E_LIGHT},
                 {"func_areaportal", Q2E_PORTAL},
                 {"target_help", Q2E_HELP},
                 {"target_secret", Q2E_SECRET},
                 {"target_goal", Q2E_GOAL},
                 {"target_changelevel", Q2E_CHANGELEVEL},
                 {"target_explosion", Q2E_EXPLOSION},
                 {"target_splash", Q2E_SPLASH}};
    size_t i;
    for (i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
        if (!strcmp(name, kinds[i].name)) {
            s->kind = kinds[i].kind;
            break;
        }
    if (i == sizeof(kinds) / sizeof(*kinds)) {
        if (!strcmp(name, "info_null"))
            return qa_session_release(g->services.session, a->id, e);
        if (!strcmp(name, "trigger_always")) {
            if (s->delay < .2f)
                s->delay = .2f;
            return q2_entity_targets(g, a, a->id, false, e);
        }
        static const char *const points[] = {"info_player_start",
                                             "info_player_coop",
                                             "info_player_deathmatch",
                                             "info_player_intermission",
                                             "info_notnull",
                                             "info_landmark",
                                             "info_teleport_destination",
                                             "info_player_team1",
                                             "info_player_team2",
                                             "func_group"};
        for (i = 0; i < sizeof(points) / sizeof(*points); i++)
            if (!strcmp(name, points[i])) {
                if (g->entity_runtime->services.ctf_map_rules &&
                    !strcmp(name, "info_teleport_destination")) {
                    qa_body_state body;
                    if (!qa_world_body_read(g->services.world, a->id, &body, e))
                        return false;
                    body.origin.z += 16;
                    return q2_entity_body(g, a, &body, true, e);
                }
                return true;
            }
        return q2_target_extra_spawn(g, a, handled, e);
    }
    s->usable = true;
    switch (s->kind) {
    case Q2E_WORLD:
        s->usable = false;
        s->has_inline = true;
        s->collision.model = 0;
        if (!qa_builtin_resource(&g->services, "*0", &s->visual.models[0], e) ||
            !q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) || !q2_player_reserve_corpses(g, e))
            return false;
        return q2_map_event(g,
                            &(qa_q2_map_event){.kind = QA_Q2_MAP_MUSIC,
                                               .actor = a->id,
                                               .text = q2_field_id(g, s, "sounds")},
                            e);
    case Q2E_KEY:
        s->usable = s->target && qa_q2_item_lookup(g, q2_field_text(g, s, "item"));
        return true;
    case Q2E_COUNTER:
        if (!s->count)
            s->count = 2;
        s->wait = -1;
        return true;
    case Q2E_TIMER:
        if (s->wait == 0)
            s->wait = 1;
        s->random = fminf(q2_field_float(g, s, "random", 0), s->wait - (float)g->frame_ns / Q2_NS);
        if (s->spawnflags & 1) {
            s->activator = a->id;
            return q2_entity_schedule(g, a, Q2ET_TIMER,
                                      1 + q2_field_float(g, s, "pausetime", 0) + s->delay +
                                          s->wait + q2_crandom(g) * s->random);
        }
        return true;
    case Q2E_SPEAKER: {
        const char *noise = q2_field_text(g, s, "noise");
        if (!*noise) {
            s->usable = false;
            return true;
        }
        if (!strstr(noise, ".wav")) {
            size_t n = strlen(noise);
            char *path = malloc(n + 5);
            if (!path) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 speaker resource");
                return false;
            }
            memcpy(path, noise, n);
            memcpy(path + n, ".wav", 5);
            bool okay = qa_builtin_resource(&g->services, path, &s->noise, e);
            free(path);
            if (!okay)
                return false;
        } else
            s->noise = q2_field_id(g, s, "noise");
        if (s->volume == 0)
            s->volume = 1;
        bool loop = g->options.edition == QA_Q2_RERELEASE && (s->spawnflags & 3);
        if (s->attenuation == -1)
            s->attenuation = loop ? -1 : 0;
        else if (s->attenuation == 0)
            s->attenuation = loop ? 3 : 1;
        s->active = (s->spawnflags & 1) != 0;
        return !s->active || speaker(g, a, 1, e);
    }
    case Q2E_LIGHT:
        if (!s->targetname || g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        s->usable = s->style >= 32;
        return !s->usable || style(g, a, (s->spawnflags & 1) ? "a" : "m", e);
    case Q2E_HELP:
        if (g->options.deathmatch || !s->message)
            return qa_session_release(g->services.session, a->id, e);
        return true;
    case Q2E_SECRET:
    case Q2E_GOAL:
        if (g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        if (s->kind == Q2E_SECRET)
            g->entity_runtime->total_secrets++;
        else
            g->entity_runtime->total_goals++;
        return q2_map_event(
            g,
            &(qa_q2_map_event){.kind = s->kind == Q2E_SECRET ? QA_Q2_MAP_SECRET : QA_Q2_MAP_GOAL,
                               .actor = a->id,
                               .count = 1,
                               .flags = 1},
            e);
    case Q2E_CHANGELEVEL:
        if (!s->map)
            return qa_session_release(g->services.session, a->id, e);
        if (!strcmp(g->player_runtime->rules.map_name, "fact1") &&
            !strcmp(qa_strings_cstr(qa_session_strings(g->services.session), s->map), "fact3"))
            return qa_builtin_resource(&g->services, "fact3$secret1", &s->map, e);
        return true;
    case Q2E_SPLASH: {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        if (!s->count)
            s->count = 32;
        s->direction = q2_movedir(b.angles);
        return true;
    }
    default:
        return true;
    }
}
bool q2_target_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                   bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    switch (s->kind) {
    case Q2E_RELAY:
        return q2_entity_targets(g, a, activator, false, e);
    case Q2E_KEY:
        return key_use(g, a, activator, e);
    case Q2E_COUNTER:
        if (!s->count)
            return true;
        s->count--;
        if (!(s->spawnflags & 1) && activator.registry) {
            char text[64];
            if (s->count)
                snprintf(text, sizeof(text), "%d more to go...", s->count);
            else
                strcpy(text, "Sequence completed!");
            if (!q2_entity_message(g, a, activator, text, e) ||
                (q2_actor_live(g, a->id) &&
                 !q2_entity_sound(g, a, "misc/talk1.wav", 0, 1, 1, 0, e)))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        return s->count || q2_entity_multi(g, a, activator, e);
    case Q2E_SPEAKER:
        if (s->spawnflags & 3) {
            s->active = !s->active;
            return speaker(g, a, s->active ? 1 : -1, e);
        }
        return speaker(g, a, 0, e);
    case Q2E_TIMER:
        s->activator = activator;
        if (s->think != Q2ET_NONE)
            return q2_entity_schedule(g, a, Q2ET_NONE, 0);
        return s->delay != 0 ? q2_entity_schedule(g, a, Q2ET_TIMER, s->delay) : timer(g, a, e);
    case Q2E_LIGHT:
        s->spawnflags ^= 1;
        return style(g, a, (s->spawnflags & 1) ? "a" : "m", e);
    case Q2E_PORTAL: {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->area_portal) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 area portal service is not installed");
            return false;
        }
        s->count ^= 1;
        return services->area_portal(services->context, (uint32_t)s->style, s->count != 0, e);
    }
    case Q2E_HELP:
        return q2_goal_use(g, a, activator, e);
    case Q2E_SECRET:
    case Q2E_GOAL: {
        if (g->options.edition == QA_Q2_RERELEASE)
            return q2_goal_use(g, a, activator, e);
        const char *noise = q2_field_text(g, s, "noise");
        if (!q2_entity_sound(g, a, *noise ? noise : "misc/secret.wav", 2, 1, 1, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        q2_entities *r = g->entity_runtime;
        if (s->kind == Q2E_SECRET)
            r->found_secrets++;
        else
            r->found_goals++;
        if (!q2_map_event(g,
                          &(qa_q2_map_event){.kind = s->kind == Q2E_SECRET ? QA_Q2_MAP_SECRET
                                                                           : QA_Q2_MAP_GOAL,
                                             .actor = a->id,
                                             .recipient = activator,
                                             .count = 1},
                          e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->kind == Q2E_GOAL && r->found_goals == r->total_goals &&
            !q2_map_event(g, &(qa_q2_map_event){.kind = QA_Q2_MAP_MUSIC}, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_entity_targets(g, a, activator, false, e))
            return false;
        return !q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e);
    }
    case Q2E_CHANGELEVEL:
        return changelevel(g, a, other, activator, e);
    case Q2E_EXPLOSION:
        s->activator = activator;
        return s->delay != 0 ? q2_entity_schedule(g, a, Q2ET_EXPLOSION, s->delay) : explode(g, a, e);
    case Q2E_SPLASH: {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        qa_string_id effect;
        if (!qa_builtin_resource(&g->services, "q2:splash", &effect, e))
            return false;
        if (!qa_builtin_emit(&g->services,
                             &(qa_builtin_event){.kind = QA_BUILTIN_IMPACT,
                                                 .family = QA_GAME_Q2,
                                                 .provider = g->options.owner,
                                                 .actor = a->id,
                                                 .resource = effect,
                                                 .origin = b.origin,
                                                 .direction = s->direction,
                                                 .count = s->count,
                                                 .code = (int)q2_field_float(g, s, "sounds", 0),
                                                 .time_ns = g->now_ns},
                             e))
            return false;
        return !q2_actor_live(g, a->id) || s->damage == 0 ||
               q2_entity_radius(g, a, activator, s->damage, s->damage + 40, 29, e);
    }
    default:
        return q2_target_extra_use(g, a, other, activator, handled, e);
    }
}
bool q2_target_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled,
                     qa_error *e) {
    *handled = true;
    if (think == Q2ET_TIMER)
        return timer(g, a, e);
    if (think == Q2ET_EXPLOSION)
        return explode(g, a, e);
    return q2_target_extra_think(g, a, think, handled, e);
}

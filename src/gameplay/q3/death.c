#include "internal.h"
#include "qa/game_type.h"

bool q3_cancel_kamikaze_timers(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_actor *timer = &game->actors[i];
        if (timer->kind == Q3_ACTOR_KAMIKAZE_TIMER &&
            qa_actor_id_equal(timer->state.kamikaze.attacker, actor)) {
            qa_actor_id id = timer->actor;
            if (!qa_session_release(game->options.services.session, id, error))
                return false;
        }
    }
    return true;
}
static bool cancel_death_effects(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game)
        return q3_fail(error, "missing Q3 death effect provider");
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER)
        entry->state.player.flags &= ~0x200u;
    return q3_cancel_kamikaze_timers(game, actor, error);
}
bool qa_q3_cancel_death_effects(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 death-effect cancellation boundary");
    ++game->observation_depth;
    bool result = cancel_death_effects(game, actor, error);
    --game->observation_depth;
    return result;
}
bool q3_schedule_kamikaze(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_error *error) {
    uint32_t source_slot;
    bool native_source = qa_q3_source_actor_slot(game, actor, &source_slot, NULL);
    if (native_source) {
        qa_q3_entity entity;
        qa_q3_wire_visibility visibility;
        if (!qa_q3_wire_entity_read(game, source_slot, &entity, &visibility, error))
            return false;
        origin = qa_v3(entity.pos.base[0], entity.pos.base[1], entity.pos.base[2]);
    }
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .body = {.origin = native_source ? qa_v3(0, 0, 0) : origin}};
    if (!qa_builtin_resource(&game->options.services, "kamikaze timer",
                              &spawn.definition, error)) return false;
    qa_actor_id timer;
    if (!q3_spawn_actor(game, &spawn, &timer, error))
        return false;
    game->source_entities[game->source_numbers[timer.slot]].server_flags |= 1u;
    game->actors[timer.slot] =
        (q3_actor){.actor = timer,
                   .kind = Q3_ACTOR_KAMIKAZE_TIMER,
                   .alpha = 1,
                   .state.kamikaze = {.attacker = actor, .next = q3_add_time(game->now_ms, 5000)}};
    q3_wire_entity_source *source = q3_wire_entity(game, timer);
    if (!source)
        return q3_rollback_spawn(game, timer, error);
    source->position.base = origin;
    q3_postgame_native_think_assigned(game, timer);
    if (!q3_wire_entity_ready(game, timer, error))
        return q3_rollback_spawn(game, timer, error);
    return true;
}
bool q3_death_rewards(qa_q3_game *game, qa_actor_id victim, const qa_damage_request *request,
                      qa_error *error) {
    q3_actor *entry = q3_actor_get(game, victim);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, victim, &body, error))
        return false;
    qa_actor_id look = request->attack.attacker;
    if (!look.registry || qa_actor_id_equal(look, victim))
        look = request->attack.inflictor;
    qa_body_state target;
    qa_error ignored = {0};
    float yaw = body.angles.y;
    if (look.registry && !qa_actor_id_equal(look, victim) &&
        qa_world_body_read(game->options.services.world, look, &target, &ignored)) {
        qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
        yaw = atan2f(delta.y, delta.x) * 180 / Q3_PI;
        if (yaw < 0)
            yaw += 360;
    }
    entry->state.player.dead_yaw = (int32_t)fmodf(yaw, 360);
    q3_player_view_write(game, victim, qa_v3(0, body.angles.y, 0));
    int32_t method = request->attack.cause.kind == QA_CAUSE_Q3
                         ? request->attack.cause.source.q3.means_of_death
                         : 0;
    uint32_t victim_slot;
    bool native_client = qa_q3_native_client_slot(game, victim, &victim_slot, NULL);
    int32_t killer_number = q3_entity_number(game, request->attack.attacker);
    if (killer_number < 0 || killer_number >= (int32_t)QA_Q3_SOURCE_CLIENTS)
        killer_number = QA_Q3_SOURCE_WORLD;
    if (native_client && game->options.hooks.source_log) {
        static const char *const common_methods[] = {
            "MOD_UNKNOWN", "MOD_SHOTGUN", "MOD_GAUNTLET", "MOD_MACHINEGUN",
            "MOD_GRENADE", "MOD_GRENADE_SPLASH", "MOD_ROCKET", "MOD_ROCKET_SPLASH",
            "MOD_PLASMA", "MOD_PLASMA_SPLASH", "MOD_RAILGUN", "MOD_LIGHTNING",
            "MOD_BFG", "MOD_BFG_SPLASH", "MOD_WATER", "MOD_SLIME", "MOD_LAVA",
            "MOD_CRUSH", "MOD_TELEFRAG", "MOD_FALLING", "MOD_SUICIDE",
            "MOD_TARGET_LASER", "MOD_TRIGGER_HURT"};
        static const char *const expansion_methods[] = {
            "MOD_NAIL", "MOD_CHAINGUN", "MOD_PROXIMITY_MINE", "MOD_KAMIKAZE", "MOD_JUICED",
            "MOD_GRAPPLE"};
        const char *name = "<bad obituary>";
        if (method >= 0 && method < 23)
            name = common_methods[method];
        else if (game->options.product == QA_Q3_ARENA && method == 23)
            name = "MOD_GRAPPLE";
        else if (game->options.product == QA_Q3_TEAM_ARENA && method >= 23 && method <= 28)
            name = expansion_methods[method - 23];
        const char *killer_name = killer_number == (int32_t)QA_Q3_SOURCE_WORLD
            ? "<world>" : game->clients[killer_number].netname;
        char line[256];
        snprintf(line, sizeof(line), "Kill: %i %u %i: %s killed %s by %s\n",
                 killer_number, victim_slot, method, killer_name,
                 game->clients[victim_slot].netname, name);
        if (!game->options.hooks.source_log(game->options.hooks.context, line, error))
            return false;
        if (!q3_actor_get(game, victim))
            return true;
    }
    if (native_client) {
        qa_actor_id obituary;
        if (!q3_wire_temp_entity(game, body.origin, 60, &obituary, error))
            return false;
        qa_q3_entity *event = q3_wire_temporary(game, obituary);
        uint32_t obituary_slot;
        if (!event || !qa_q3_source_actor_slot(game, obituary, &obituary_slot, error))
            return q3_fail(error, "Q3 obituary lost its actual temporary source row");
        event->eventParm = method;
        event->otherEntityNum = (int32_t)victim_slot;
        event->otherEntityNum2 = killer_number;
        game->source_entities[obituary_slot].server_flags = 32u;
    }
    if (!q3_event(game, victim, request->attack.attacker, QA_BUILTIN_DEATH, 60, method, body.origin,
                  qa_v3(0, 0, 0), qa_v3(0, 0, 0), error))
        return false;
    entry = q3_actor_get(game, victim);
    q3_actor *killer = q3_actor_get(game, request->attack.attacker);
    if (!entry) return true;
    if (native_client)
        entry->state.player.deaths = q3_add_time(entry->state.player.deaths, 1);
    entry->enemy = request->attack.attacker;
    uint32_t enemy_slot;
    entry->enemy_source_present = qa_q3_source_actor_slot(
        game, request->attack.attacker, &enemy_slot, NULL);
    entry->enemy_source_slot = entry->enemy_source_present ? enemy_slot : 0;
    if (killer && killer->kind == Q3_ACTOR_PLAYER)
        killer->state.player.last_killed_client = q3_entity_number(game, victim);
    if (native_client && game->options.hooks.source_death_score &&
        !game->options.hooks.source_death_score(game->options.hooks.context,
            victim, request->attack.attacker, error)) return false;
    entry = q3_actor_get(game, victim);
    killer = q3_actor_get(game, request->attack.attacker);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || (native_client &&
        (!qa_actor_id_equal(game->source_entities[victim_slot].actor, victim) ||
         !game->source_entities[victim_slot].body_attached))) return true;
    if (!killer || killer->kind != Q3_ACTOR_PLAYER || qa_actor_id_equal(victim, killer->actor))
        return true;
    qa_actor_id attacker = request->attack.attacker;
    qa_combat_state vc, kc;
    if (!qa_combat_read(game->options.services.combat, victim, &vc, error))
        return false;
    killer = q3_actor_get(game, attacker);
    if (!killer || killer->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!qa_combat_read(game->options.services.combat, attacker, &kc, error))
        return false;
    entry = q3_actor_get(game, victim);
    killer = q3_actor_get(game, attacker);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !killer ||
        killer->kind != Q3_ACTOR_PLAYER)
        return true;
    uint32_t attacker_slot, receiver_slot;
    int32_t victim_team = qa_q3_native_client_slot(game, victim, &receiver_slot, NULL)
        ? game->clients[receiver_slot].session.team : (int32_t)vc.team;
    int32_t killer_team = qa_q3_native_client_slot(game, attacker, &attacker_slot, NULL)
        ? game->clients[attacker_slot].session.team : (int32_t)kc.team;
    if (qa_game_type_has_allies(game->options.rules.game_type) && victim_team == killer_team)
        return true;
    qa_q3_player_state *player = &killer->state.player;
    if (method == 2) {
        player->gauntlet_frag_count = q3_add_time(player->gauntlet_frag_count, 1);
        player->flags = (player->flags & ~0x38848u) | 0x40u;
        player->reward_until = q3_add_time(game->now_ms, 2000);
        entry->state.player.player_events ^= 2;
    }
    if (q3_sub_time(game->now_ms, player->last_kill_ms) < 3000) {
        if (!q3_ranking_reward(game, attacker, 8u, error))
            return false;
        killer = q3_actor_get(game, attacker);
        if (!killer || killer->kind != Q3_ACTOR_PLAYER)
            return true;
        player = &killer->state.player;
        player->excellent_count = q3_add_time(player->excellent_count, 1);
        player->flags = (player->flags & ~0x38848u) | 8u;
        player->reward_until = q3_add_time(game->now_ms, 2000);
    }
    killer = q3_actor_get(game, attacker);
    if (killer && killer->kind == Q3_ACTOR_PLAYER)
        killer->state.player.last_kill_ms = game->now_ms;
    return true;
}

static bool source_drop_item(qa_q3_game *game, qa_actor_id player, uint32_t index, float yaw,
                              int32_t count, qa_actor_id *out, qa_error *error) {
    uint32_t source_slot;
    qa_vec3 origin, angles;
    if (qa_q3_native_client_slot(game, player, &source_slot, NULL)) {
        qa_q3_entity source;
        qa_q3_wire_visibility visibility;
        if (!qa_q3_wire_entity_read(game, source_slot, &source, &visibility, error))
            return false;
        origin = qa_v3(source.pos.base[0], source.pos.base[1], source.pos.base[2]);
        angles = qa_v3(source.apos.base[0], source.apos.base[1], source.apos.base[2]);
    } else {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, player, &body, error))
            return false;
        origin = body.origin;
        angles = body.angles;
    }
    if (!q3_actor_get(game, player))
        return true;
    qa_vec3 forward;
    q3_source_angle_vectors(qa_v3(0, (angles.y + yaw), angles.z),
                             &forward, NULL, NULL);
    qa_vec3 velocity = qa_vec_scale(forward, 150);
    velocity.z = (velocity.z + (200 + (q3_crandom(game) * 50)));
    qa_q3_item_spawn spawn = {.item_index = index,
                              .dropped = true,
                              .origin = origin,
                              .velocity = velocity,
                              .count = count};
    qa_actor_id item;
    if (!qa_q3_spawn_item(game, &spawn, &item, error))
        return false;
    if (out) *out = item;
    return true;
}
bool qa_q3_source_drop_item(qa_q3_game *game, qa_actor_id player, uint32_t index,
                            float angle, qa_actor_id *out, qa_error *error) {
    size_t count;
    if (!game || !out || !isfinite(angle) || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 source Drop_Item boundary");
    qa_q3_items(game->options.product, &count);
    if (!index || index >= count)
        return q3_fail(error, "Q3 Drop_Item has no source item definition");
    uint32_t source_slot;
    if (!qa_q3_native_client_slot(game, player, &source_slot, error))
        return false;
    *out = (qa_actor_id){0};
    ++game->observation_depth;
    bool okay = source_drop_item(game, player, index, angle, 0, out, error);
    --game->observation_depth;
    return okay;
}
bool q3_drop_player_items(qa_q3_game *game, qa_actor_id actor, bool no_drop, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state player = entry->state.player;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if (!no_drop) {
        qa_q3_weapon weapon = player.weapon;
        if ((weapon == QA_Q3_W_MACHINEGUN || weapon == QA_Q3_W_GRAPPLE) &&
            player.weapon_phase == QA_Q3_DROPPING)
            weapon = player.requested_weapon;
        if ((player.selections & QA_Q3_ARSENAL) && weapon > QA_Q3_W_MACHINEGUN &&
            weapon != QA_Q3_W_GRAPPLE && q3_owns_weapon(game, actor, weapon)) {
            int32_t ammo;
            if (!q3_ammo_read(game, actor, weapon, &ammo, error))
                return false;
            if (ammo)
                for (size_t i = 1; i < count; ++i)
                    if (items[i].kind == QA_Q3_ITEM_WEAPON && items[i].tag == (int32_t)weapon) {
                        if (!source_drop_item(game, actor, (uint32_t)i, 0, 0, NULL, error))
                            return false;
                        break;
                    }
        }
        if (game->options.rules.game_type != 3) {
            float yaw = 45;
            for (unsigned powerup = 1; powerup < QA_Q3_POWERUP_COUNT; ++powerup) {
                if (player.powerups[powerup] <= game->now_ms)
                    continue;
                for (size_t i = 1; i < count; ++i)
                    if ((items[i].kind == QA_Q3_ITEM_POWERUP ||
                         items[i].kind == QA_Q3_ITEM_TEAM || items[i].kind == QA_Q3_ITEM_PERSISTENT) &&
                        items[i].tag == (int32_t)powerup) {
                        int32_t seconds =
                            q3_sub_time(player.powerups[powerup], game->now_ms) / 1000;
                        if (seconds < 1)
                            seconds = 1;
                        if (!source_drop_item(game, actor, (uint32_t)i, yaw, seconds, NULL, error))
                            return false;
                        yaw += 45;
                        break;
                    }
            }
        }
    }
    q3_actor *persistent = q3_actor_get(game, player.persistent_item);
    if (persistent && persistent->kind == Q3_ACTOR_ITEM) {
        if (!qa_q3_item_availability(game, persistent->actor, true, 0,
                                     persistent->state.item.expire_at, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        entry->state.player.persistent = QA_Q3_P_NONE;
        entry->state.player.persistent_item = (qa_actor_id){0};
    }
    return true;
}
static bool player_death_cleanup(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || entry->state.player.death_cleanup_done)
        return true;
    entry->state.player.death_cleanup_done = true;
    uint32_t source_slot;
    bool native_client = qa_q3_native_client_slot(game, actor, &source_slot, NULL);
    qa_actor_id hook = entry->state.player.hook, mine = entry->state.player.attached_mine;
    if (q3_actor_get(game, hook) &&
        !qa_session_release(game->options.services.session, hook, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!native_client && q3_actor_get(game, mine) &&
        !qa_session_release(game->options.services.session, mine, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &query, &contents, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!q3_drop_player_items(game, actor, qa_collision_bits_overlap(contents.contents, qa_collision_bit(QA_CONTENT_NODROP)), error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    if (native_client && game->options.hooks.source_client_death &&
        !game->options.hooks.source_client_death(game->options.hooks.context, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || (native_client &&
        (!qa_actor_id_equal(game->source_entities[source_slot].actor, actor) ||
         !game->source_entities[source_slot].body_attached)))
        return true;
    if (!native_client) {
        entry->state.player.weapon = QA_Q3_W_NONE;
        entry->state.player.loop_sound = 0;
    }
    if (native_client) {
        q3_wire_entity_source *source = q3_wire_entity(game, actor);
        if (!source)
            return q3_fail(error, "Q3 death cleanup lost its source player entity");
        source->weapon = 0;
        source->powerups = 0;
        source->loop_sound = 0;
    }
    memset(entry->state.player.powerups, 0, sizeof(entry->state.player.powerups));
    entry->state.player.invulnerability_until = 0;
    if (qa_q3_native_client_slot(game, actor, &source_slot, NULL) &&
        game->options.hooks.source_flags_cleared &&
        !game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (game->options.product == QA_Q3_TEAM_ARENA &&
        !(entry->state.player.selections & QA_Q3_CHARACTER) &&
        (entry->state.player.flags & 0x200u)) {
        qa_combat_state combat;
        qa_builtin_actor_traits traits = {.gib_health = -40};
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        if (game->options.services.actor_traits)
            (void)game->options.services.actor_traits(game->options.services.context, actor,
                                                      &traits);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        if (combat.health > traits.gib_health &&
            !q3_schedule_kamikaze(game, actor, body.origin, error))
            return false;
    }
    return true;
}
bool qa_q3_player_death_cleanup(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 player death-cleanup boundary");
    ++game->observation_depth;
    bool result = player_death_cleanup(game, actor, error);
    --game->observation_depth;
    return result;
}
bool q3_copy_corpse(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    uint32_t player_slot;
    bool native_client = qa_q3_native_client_slot(game, actor, &player_slot, NULL);
    if (!native_client && entry->state.player.gibbed)
        return true;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!qa_world_unlink(game->options.services.world, actor, error))
        return false;
    q3_wire_entity_source *player_source = native_client ? q3_wire_entity(game, actor) : NULL;
    if (native_client && !player_source)
        return q3_fail(error, "Q3 body copy lost its retained source origin");
    qa_point_query point = {.point = player_source ? player_source->authored_origin : body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (qa_collision_bits_overlap(contents.contents, qa_collision_bit(QA_CONTENT_NODROP)))
        return true;
    uint32_t queue = game->body_queue_index;
    qa_actor_id corpse = game->body_queue[queue];
    q3_actor *queued = q3_actor_get(game, corpse);
    uint32_t source_slot;
    if (!queued || queued->kind != Q3_ACTOR_CORPSE ||
        !qa_q3_source_actor_slot(game, corpse, &source_slot, error) ||
        !game->source_entities[source_slot].never_free)
        return q3_fail(error, "Q3 corpse copy needs its initialized persistent body queue");
    game->body_queue_index = (queue + 1u) % 8u;
    if (qa_q3_source_actor_slot(game, actor, &player_slot, NULL)) {
        game->source_entities[source_slot].server_flags = game->source_entities[player_slot].server_flags;
        game->source_entities[source_slot].owner_number = (int32_t)player_slot;
    }
    if (!qa_world_unlink(game->options.services.world, corpse, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    const qa_actor_record *owner_record = qa_actors_get(qa_session_actors(game->options.services.session), actor);
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_contents_decode(INT32_C(0x04000000), QA_COLLISION_Q3),
                                    .role = QA_COLLISION_SOLID,
                                    .owner = owner_record && owner_record->owner == game->options.owner && owner_record->has_source ?
                                        qa_actor_reference_source(owner_record->owner, owner_record->source_slot) : qa_actor_reference_lifetime(actor)};
    combat.can_take_damage = combat.health > -40;
    combat.armor = (qa_armor){0};
    if (!qa_world_body_write(game->options.services.world, corpse, &body, error) ||
        !qa_world_set_collision(game->options.services.world, corpse, &collision, error)) return false;
    if (qa_combat_storage_serial(game->options.services.combat, corpse)) {
        if (!qa_combat_set_traits(game->options.services.combat, corpse, &combat, error) ||
            !qa_combat_set_health(game->options.services.combat, corpse, combat.health, error) ||
            !qa_combat_set_armor(game->options.services.combat, corpse, &combat.armor, error)) return false;
    } else if (!qa_combat_create_actor(game->options.services.combat, corpse, &combat, error)) return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), corpse))
        return true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_trajectory source_position;
    uint32_t source_flags;
    if (native_client) {
        if (!q3_wire_copy_body(game, actor, corpse, &source_position, &source_flags, error))
            return false;
    } else {
        source_position = (qa_trajectory){.type = QA_TRAJECTORY_GRAVITY,
                                         .time_ms = game->now_ms,
                                         .base = body.origin,
                                         .delta = body.velocity};
        source_flags = entry->state.player.flags;
    }
    q3_wire_entity_source *source = q3_wire_entity(game, corpse);
    if (!source)
        return q3_fail(error, "Q3 body copy lost its genuine copied entity fields");
    if (!native_client) {
        *source = (q3_wire_entity_source){
            .type = 1,
            .client = entry->state.player.client_number,
            .weapon = (int32_t)entry->state.player.weapon,
            .legs = entry->state.player.legs_animation,
            .angular = {.type = QA_TRAJECTORY_STATIONARY, .base = body.angles}};
    }
    int32_t animation = source->legs & ~128;
    q3_postgame_native_think_assigned(game, corpse);
    animation = animation == 0 || animation == 1 ? 1
        : animation == 2 || animation == 3 ? 3 : 5;
    source->powerups = 0;
    source->loop_sound = 0;
    source->event = 0;
    source->legs = source->torso = animation;
    source->ground_entity = qa_actor_reference_present(body.ground) ? body.ground.kind == QA_ACTOR_REFERENCE_SOURCE && body.ground.value.source.owner == game->options.owner ?
        (int32_t)body.ground.value.source.slot : q3_entity_number(game, qa_actor_reference_resolve(qa_session_actors(game->options.services.session), body.ground))
                                                 : (int32_t)QA_Q3_SOURCE_NONE;
    if (!qa_actor_reference_present(body.ground)) {
        source_position.type = QA_TRAJECTORY_GRAVITY;
        source_position.time_ms = game->now_ms;
        source_position.delta = body.velocity;
    } else
        source_position.type = QA_TRAJECTORY_STATIONARY;
    body.origin = source_position.base;
    if (!qa_world_body_write(game->options.services.world, corpse, &body, error))
        return false;
    game->actors[corpse.slot] = (q3_actor){
        .actor = corpse,
        .kind = Q3_ACTOR_CORPSE,
        .alpha = 1,
        .state.corpse = {.player = actor,
                         .animation = animation,
                         .timestamp = game->now_ms,
                         .next_sink = q3_add_time(game->now_ms, 5000),
                         .flags = 1u | (source_flags & 0x200u),
                         .physics_object = true,
                         .trajectory = source_position}};
    for (uint32_t i = QA_Q3_SOURCE_CLIENTS; i < game->source_count; ++i) {
        q3_actor *timer = q3_actor_get(game, game->source_entities[i].actor);
        if (game->source_entities[i].in_use && timer && timer->kind == Q3_ACTOR_KAMIKAZE_TIMER &&
            qa_actor_id_equal(timer->state.kamikaze.attacker, actor)) {
            timer->state.kamikaze.attacker = corpse;
            break;
        }
    }
    return qa_q3_wire_link(game, corpse, NULL, error) &&
           q3_wire_entity_ready(game, corpse, error);
}
bool q3_corpse_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_CORPSE)
        return true;
    if (!entry->state.corpse.physics_object)
        return true;
    qa_trace_result trace = {0};
    bool moved = entry->state.corpse.trajectory.type != QA_TRAJECTORY_STATIONARY;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (moved) {
        qa_vec3 destination;
        if (!qa_trajectory_position(&entry->state.corpse.trajectory, game->now_ms, 800,
                                    &destination, error))
            return false;
        qa_trace_query query = {.start = body.origin,
                                .end = destination,
                                .pass_actor = actor,
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
        query.policy.contents_mask = qa_collision_contents_mask(0x10001u, QA_COLLISION_Q3);
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_CORPSE)
            return true;
        body.origin = trace.end;
        if (!qa_world_body_write(game->options.services.world, actor, &body, error) ||
            !qa_q3_wire_link(game, actor, NULL, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_CORPSE)
            return true;
    }
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_CORPSE) return true;
    if (!replaced && game->now_ms >= entry->state.corpse.next_sink) {
        if (q3_sub_time(game->now_ms, entry->state.corpse.timestamp) > 6500) {
            entry->state.corpse.physics_object = false;
            return qa_world_unlink(game->options.services.world, actor, error);
        }
        entry->state.corpse.next_sink = q3_add_time(game->now_ms, 100);
        entry->state.corpse.trajectory.base.z -= 1;
    }
    if (!moved || (trace.fraction == 1 && !trace.start_solid && !trace.all_solid))
        return true;
    qa_point_query point = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error))
        return false;
    if (qa_collision_bits_overlap(contents.contents, qa_collision_bit(QA_CONTENT_NODROP)))
        return qa_world_unlink(game->options.services.world, actor, error);
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_CORPSE)
        return true;
    qa_vec3 normal = trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0);
    body.velocity = qa_v3(0, 0, 0);
    entry->state.corpse.trajectory.delta = body.velocity;
    if (normal.z > 0) {
        body.origin = qa_physics_q3_snap(qa_vec_add(trace.end, qa_v3(0, 0, 1)));
        const qa_actor_record *ground = qa_actors_get(qa_session_actors(game->options.services.session), trace.actor);
        body.ground = trace.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(game->options.owner, QA_Q3_SOURCE_WORLD) :
            trace.hit == QA_TRACE_HIT_ACTOR ? ground && ground->owner == game->options.owner && ground->has_source ?
                qa_actor_reference_source(ground->owner, ground->source_slot) : qa_actor_reference_lifetime(trace.actor) : (qa_actor_reference){0};
        entry->state.corpse.trajectory = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY,
                                                         .base = body.origin};
        q3_wire_entity_source *source = q3_wire_entity(game, actor);
        if (!source)
            return q3_fail(error, "Q3 body bounce lost its source ground field");
        source->ground_entity = trace.hit == QA_TRACE_HIT_WORLD ? (int32_t)QA_Q3_SOURCE_WORLD
            : trace.hit == QA_TRACE_HIT_ACTOR ? q3_entity_number(game, trace.actor) : (int32_t)QA_Q3_SOURCE_NONE;
    } else {
        body.origin = qa_vec_add(body.origin, normal);
        entry->state.corpse.trajectory.base = body.origin;
        entry->state.corpse.trajectory.time_ms = game->now_ms;
    }
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}

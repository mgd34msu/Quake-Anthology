#include "boss_internal.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q1_bots.h"
#include "wire_internal.h"
#include <float.h>

static const char *const weapon_names[QA_Q1_WEAPON_COUNT] = {"q1:weapon/axe",
                                                             "q1:weapon/shotgun",
                                                             "q1:weapon/supershotgun",
                                                             "q1:weapon/nailgun",
                                                             "q1:weapon/supernailgun",
                                                             "q1:weapon/grenadelauncher",
                                                             "q1:weapon/rocketlauncher",
                                                             "q1:weapon/lightning",
                                                             "q1:weapon/hipnotic:laser",
                                                             "q1:weapon/hipnotic:mjolnir",
                                                             "q1:weapon/hipnotic:proximity",
                                                             "q1:weapon/rogue:lava-nailgun",
                                                             "q1:weapon/rogue:lava-supernailgun",
                                                             "q1:weapon/rogue:multi-grenade",
                                                             "q1:weapon/rogue:multi-rocket",
                                                             "q1:weapon/rogue:plasma",
                                                             "q1:weapon/rogue:grapple",
                                                             "q1:weapon/mg3:laser",
                                                             "q1:weapon/mg3:mjolnir",
                                                             "q1:ctf/weapon/grapple"};
static const char *const ammo_names[QA_Q1_AMMO_COUNT] = {
    "q1:ammo/shells",   "q1:ammo/nails",         "q1:ammo/rockets",
    "q1:ammo/cells",    "rogue:ammo/lava-nails", "rogue:ammo/multi-rockets",
    "rogue:ammo/plasma"};
const char *qa_q1_ammo_identity(qa_q1_ammo ammo) {
    return (unsigned)ammo < QA_Q1_AMMO_COUNT ? ammo_names[ammo] : NULL;
}
static const char *const weapon_models[QA_Q1_WEAPON_COUNT] = {
    "progs/v_axe.mdl",    "progs/v_shot.mdl",   "progs/v_shot2.mdl",  "progs/v_nail.mdl",
    "progs/v_nail2.mdl",  "progs/v_rock.mdl",   "progs/v_rock2.mdl",  "progs/v_light.mdl",
    "progs/v_laserg.mdl", "progs/v_hammer.mdl", "progs/v_prox.mdl",   "progs/v_lava.mdl",
    "progs/v_lava2.mdl",  "progs/v_multi.mdl",  "progs/v_multi2.mdl", "progs/v_plasma.mdl",
    "progs/v_grpple.mdl", "progs/v_laserg.mdl", "progs/v_hammer.mdl", "progs/v_grpple.mdl"};
const char *qa_q1_weapon_identity(qa_q1_weapon weapon) {
    return (unsigned)weapon < QA_Q1_WEAPON_COUNT ? weapon_names[weapon] : NULL;
}
bool qa_q1_weapon_source(qa_q1_program program, uint32_t value, qa_q1_weapon *out) {
    if (!out || (unsigned)program > QA_Q1_CTF)
        return false;
    if (value == (program == QA_Q1_ROGUE ? 2048u : 4096u)) {
        *out = QA_Q1_AXE;
        return true;
    }
    for (unsigned bit = 1, weapon = QA_Q1_SHOTGUN; weapon <= QA_Q1_LIGHTNING;
         bit <<= 1, ++weapon)
        if (value == bit) {
            *out = (qa_q1_weapon)weapon;
            return true;
        }
    if (program == QA_Q1_HIPNOTIC) {
        if (value == 128u) *out = QA_Q1_MJOLNIR;
        else if (value == 65536u) *out = QA_Q1_PROXIMITY;
        else if (value == 8388608u) *out = QA_Q1_LASER;
        else return false;
        return true;
    }
    if (program == QA_Q1_ROGUE) {
        for (unsigned bit = 4096u, weapon = QA_Q1_LAVA_NAILGUN; weapon <= QA_Q1_PLASMA;
             bit <<= 1, ++weapon)
            if (value == bit) {
                *out = (qa_q1_weapon)weapon;
                return true;
            }
        if (value == 8388608u) {
            *out = QA_Q1_ROGUE_GRAPPLE;
            return true;
        }
    }
    if (program == QA_Q1_CTF && value == 128u) {
        *out = QA_Q1_CTF_GRAPPLE;
        return true;
    }
    if (program == QA_Q1_MG3) {
        if (value == 128u) *out = QA_Q1_MG3_MJOLNIR;
        else if (value == 8388608u) *out = QA_Q1_MG3_LASER;
        else return false;
        return true;
    }
    return false;
}

q1_actor *q1_entity(qa_q1_game *g, qa_actor_id actor) {
    if (!g || g->destroy_pending || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return NULL;
    q1_actor *entity = g->actors[actor.slot];
    return entity && entity->active && qa_actor_id_equal(entity->id, actor) ? entity : NULL;
}
bool qa_q1_game_gravity(const qa_q1_game *g, float *out) {
    if (!g || g->destroy_pending || !out)
        return false;
    *out = g->services.physics ? g->services.physics->gravity : g->options.gravity;
    return true;
}
bool qa_q1_game_rules_read(const qa_q1_game *g, int32_t *deathmatch, uint32_t *gamecfg) {
    if (!g || g->destroy_pending || !deathmatch || !gamecfg)
        return false;
    *deathmatch = g->options.deathmatch;
    *gamecfg = g->options.gamecfg;
    return true;
}
bool qa_q1_game_rogue_runes_read(const qa_q1_game *g, qa_actor_id *world, bool *started) {
    if (!g || g->destroy_pending || !world || !started)
        return false;
    *world = g->rogue_runes_world;
    *started = g->rogue_runes_started;
    return true;
}
bool qa_q1_game_monster_counts(const qa_q1_game *g, uint32_t *total, uint32_t *killed) {
    if (!g || g->destroy_pending || !total || !killed)
        return false;
    *total = g->total_monsters;
    *killed = g->killed_monsters;
    return true;
}
bool qa_q1_game_alpha(qa_q1_game *g, qa_actor_id actor, float alpha, qa_error *error) {
    if (!isfinite(alpha)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 actor alpha");
        return false;
    }
    qa_q1_game_operation operation;
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_actor *entity = q1_entity(g, actor);
    bool ok = entity && entity->native;
    if (ok)
        entity->alpha = alpha;
    else
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 alpha requires a live native actor");
    qa_q1_game_operation_end(&operation);
    return ok;
}
const q1_actor *q1_entity_const(const qa_q1_game *g, qa_actor_id actor) {
    if (!g || g->destroy_pending || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return NULL;
    const q1_actor *entity = g->actors[actor.slot];
    return entity && entity->active && qa_actor_id_equal(entity->id, actor) ? entity : NULL;
}
bool qa_q1_player_source_present(const qa_q1_game *g, qa_actor_id actor) {
    if (!g || g->destroy_pending || g->continuation_pending || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    return player && player->active && qa_actor_id_equal(player->id, actor);
}
bool qa_q1_native_client_slot_prepared(const qa_q1_game *g, qa_actor_id actor,
                                        uint32_t *out, qa_error *error) {
    if (!g || !out || g->destroy_pending || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 source client is not admitted");
        return false;
    }
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !qa_actor_id_equal(player->id, actor) ||
        !player->source_client || player->client_slot >= g->options.max_clients) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 actor has no source client slot");
        return false;
    }
    *out = player->client_slot;
    return true;
}
bool qa_q1_native_client_slot(const qa_q1_game *g, qa_actor_id actor,
                               uint32_t *out, qa_error *error) {
    if (g && g->continuation_pending) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 source client is not admitted");
        return false;
    }
    return qa_q1_native_client_slot_prepared(g, actor, out, error);
}
bool qa_q1_source_client_actor(const qa_q1_game *g, uint32_t slot, qa_actor_id *out) {
    if (!g || !out || g->destroy_pending || g->continuation_pending ||
        slot >= g->options.max_clients)
        return false;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        const q1_player *player = g->players[i];
        if (player && player->source_client && player->client_slot == slot &&
            qa_q1_player_source_present(g, player->id)) {
            *out = player->id;
            return true;
        }
    }
    return false;
}
q1_player *q1_player_get(qa_q1_game *g, qa_actor_id actor) {
    if (!g || g->destroy_pending || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return NULL;
    q1_player *player = g->players[actor.slot];
    return player && player->active && qa_actor_id_equal(player->id, actor) ? player : NULL;
}
q1_player *q1_player_allocate(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (player)
        return player;
    if (!q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 player actor retired");
        return NULL;
    }
    player = g->spare_players;
    if (player) {
        g->spare_players = player->pool_next;
        q1_player *next = player->allocation_next;
        q1_source_client_clear(player);
        memset(player, 0, sizeof(*player));
        player->allocation_next = next;
    } else {
        player = calloc(1, sizeof(*player));
        if (!player) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q1 player extension allocation");
            return NULL;
        }
        player->allocation_next = g->allocated_players;
        g->allocated_players = player;
    }
    player->id = actor;
    player->active = true;
    player->weapon = QA_Q1_SHOTGUN;
    player->animation_at = -1;
    player->animation_base = 1;
    player->nail_side = 1;
    player->mega_rot_at = -1;
    player->air_finished = g->time + 12;
    player->drown_damage = 2;
    player->max_health =
        g->options.edition == QA_Q1_RERELEASE && g->options.skill == 3 && !g->options.deathmatch
            ? 50
            : 100;
    g->players[actor.slot] = player;
    return player;
}
bool qa_q1_source_bind_client(qa_q1_game *g, uint32_t slot, qa_actor_id actor,
                               qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = false;
    if (slot >= g->options.max_clients || actor.slot >= g->capacity || !q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot,
                     "Q1 source client admission needs its actual slot and live shared actor");
        goto finish;
    }
    q1_player *player = g->players[actor.slot];
    if (player && (!player->active || !qa_actor_id_equal(player->id, actor))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 player slot still awaits source release");
        goto finish;
    }
    if (player && player->source_client) {
        ok = player->client_slot == slot;
        if (!ok) qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q1 actor has another source client slot");
        goto finish;
    }
    for (uint32_t i = 0; i < g->capacity; ++i) {
        const q1_player *other = g->players[i];
        if (other && other->active && other->source_client && other->client_slot == slot) {
            qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q1 source client slot is already admitted");
            goto finish;
        }
    }
    player = q1_player_allocate(g, actor, error);
    if (!player)
        goto finish;
    player->client_slot = slot;
    player->source_client = true;
    player->source_team = 1;
    player->source_respawn_requested_at = -1;
    ok = true;
finish:
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_alive(qa_q1_game *g, qa_actor_id actor) {
    return g && !g->destroy_pending &&
           qa_actors_get(qa_session_actors(g->services.session), actor) != NULL;
}
float q1_random(qa_q1_game *g) { return qa_builtin_random_unit(&g->random); }
float qa_q1_game_random(qa_q1_game *g) { return g && !g->destroy_pending ? q1_random(g) : 0; }
float q1_health(qa_q1_game *g, qa_actor_id actor) {
    qa_combat_state state;
    return qa_combat_read(g->services.combat, actor, &state, NULL) ? state.health : 0;
}
bool q1_damageable(qa_q1_game *g, qa_actor_id actor) {
    qa_combat_state state;
    return qa_combat_read(g->services.combat, actor, &state, NULL) && state.can_take_damage;
}
bool q1_classnamed(qa_q1_game *g, qa_actor_id actor, const char *name) {
    qa_string_id classname = 0;
    q1_actor *entity = q1_entity(g, actor);
    if (entity)
        classname = entity->classname;
    else if (g->services.actor_traits) {
        qa_builtin_actor_traits traits;
        if (g->services.actor_traits(g->services.context, actor, &traits))
            classname = traits.classname;
    }
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), classname);
    return text.size == strlen(name) && !memcmp(text.data, name, text.size);
}
bool q1_target(qa_q1_game *g, qa_actor_id actor, qa_q1_target *target) {
    if (g->host.target && g->host.target(g->host.context, actor, target))
        return true;
    qa_builtin_actor_traits traits;
    if (g->services.actor_traits && g->services.actor_traits(g->services.context, actor, &traits)) {
        *target = (qa_q1_target){.player = traits.player,
                                 .invisible = traits.invisible,
                                 .notarget = traits.no_target || traits.spectator,
                                 .aimed_damage = traits.aimed_damage,
                                 .view_height = traits.view_height,
                                 .hostile_until = (double)traits.hostile_until_ns / 1000000000.0};
        return true;
    }
    q1_player *player = q1_player_get(g, actor);
    q1_actor *entity = q1_entity(g, actor);
    if (!entity && !player)
        return false;
    *target = (qa_q1_target){
        .player = player && (player->source_client || player->arsenal || player->character),
        .notarget = player && player->source_client && player->source_no_target,
        .aimed_damage = entity && entity->aimed_damage,
        .view_height = player ? 22
                       : entity && entity->kind == Q1_MONSTER &&
                               entity->state.monster.species->species == QA_Q1_LAVA_MAN
                           ? 48
                       : entity && entity->kind == Q1_MONSTER &&
                               entity->state.monster.addon.boss == Q1_BOSS_OLDNEW
                           ? 24
                       : entity && (entity->physics.flags & QA_PHYSICS_SWIMMING) ? 10
                                                                                 : 25,
        .invisible = player && player->power_expires[QA_Q1_INVISIBILITY] > g->time,
        .hostile_until = player ? player->hostile_until
                         : entity && entity->kind == Q1_MONSTER
                             ? entity->state.monster.hostile_until
                             : 0};
    return true;
}
bool q1_model(qa_q1_game *g, q1_actor *entity, const char *path, qa_error *error) {
    if (g->wire && !g->wire->id1 && g->wire->loading &&
        !qa_q1_wire_declare_model(g, path, error))
        return false;
    return qa_builtin_resource(&g->services, path, &entity->model, error);
}
bool q1_sound_resource(qa_q1_game *g, qa_actor_id actor, qa_string_id resource, int32_t channel,
                       float attenuation, float volume, qa_error *error) {
    if (g->destroy_pending)
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = actor,
                              .resource = resource,
                              .time_ns = g->time_ns,
                              .channel = channel,
                              .attenuation = attenuation,
                              .volume = volume};
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    event.origin =
        qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
    return qa_builtin_emit(&g->services, &event, error);
}
bool q1_sound(qa_q1_game *g, qa_actor_id actor, const char *path, int32_t channel,
              float attenuation, qa_error *error) {
    qa_string_id resource;
    return qa_builtin_resource(&g->services, path, &resource, error) &&
           q1_sound_resource(g, actor, resource, channel, attenuation, 1, error);
}
bool q1_effect(qa_q1_game *g, qa_builtin_event_kind kind, qa_actor_id actor, qa_vec3 origin,
               float value, int32_t code, qa_error *error) {
    if (g->destroy_pending)
        return true;
    qa_builtin_event event = {.kind = kind,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = actor,
                              .origin = origin,
                              .value = value,
                              .code = code,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error);
}

static bool combat_context(void *context, const qa_damage_request *request,
                           const qa_combat_state *target, const qa_combat_state *attacker,
                           qa_combat_context *out, qa_error *error) {
    (void)attacker;
    qa_q1_game *g = context;
    q1_player *player = q1_player_get(g, request->attack.attacker);
    qa_q1_target traits = {0};
    bool walk = q1_target(g, request->target, &traits) && traits.player;
    qa_vec3 origin = {0};
    qa_body_state body;
    bool has_origin =
        request->attack.projectile.registry &&
        qa_world_body_read(g->services.world, request->attack.projectile, &body, NULL);
    if (has_origin) {
        origin = qa_vec_add(body.origin,
                            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
    } else {
        qa_linked_body linked;
        qa_actor_id inflictor = request->attack.inflictor;
        bool world = g->services.physics
                         ? qa_actor_id_equal(inflictor, g->services.physics->world_actor)
                         : q1_classnamed(g, inflictor, "worldspawn");
        has_origin =
            inflictor.registry && !world && qa_world_linked(g->services.world, inflictor, &linked);
        if (has_origin)
            origin = qa_vec_scale(
                qa_vec_add(linked.absolute_bounds.mins, linked.absolute_bounds.maxs), 0.5f);
    }
    bool has_momentum =
        has_origin && qa_world_body_read(g->services.world, request->target, &body, NULL);
    *out = (qa_combat_context){
        .armor.alive = target->health > 0,
        .game.q1 = {.quad = player && player->power_expires[QA_Q1_QUAD] > g->time,
                    .walk = walk,
                    .has_momentum_direction = has_momentum,
                    .momentum_direction = has_momentum
                                              ? qa_vec_normalize(qa_vec_sub(body.origin, origin))
                                              : qa_v3(0, 0, 0),
                    .teamplay = g->options.teamplay,
                    .skip_base_team_health = g->options.program == QA_Q1_ROGUE}};
    if (g->options.quakeworld && g->options.program == QA_Q1_ID1 &&
        g->options.edition == QA_Q1_CLASSIC) {
        out->game.q1.quakeworld_rj = true;
        qa_q1_source_client_view victim, killer;
        if (qa_q1_source_client_read(g, request->target, &victim) &&
            qa_q1_source_client_read(g, request->attack.attacker, &killer)) {
            out->game.q1.rj = g->qw_rj;
            out->game.q1.same_player_netname = !strcmp(victim.name, killer.name);
        }
    }
    if (g->host.base_team_health) {
        bool enabled;
        if (!g->host.base_team_health(g->host.context, &enabled, error)) return false;
        out->game.q1.skip_base_team_health = !enabled;
    }
    return true;
}
static bool operation_finish(qa_q1_game_operation *operation, bool ok, qa_error *error) {
    if (ok && !qa_q1_game_operation_live(operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 source retired during native operation");
        ok = false;
    }
    qa_q1_game_operation_end(operation);
    return ok;
}
static bool prepare_frame(void *context, qa_session *session, const qa_source_frame *frame,
                        qa_error *error) {
    (void)session;
    qa_q1_game *g = context;
    if (g->destroy_pending || g->observation_depth || g->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,
                     "Q1 source frame cannot advance during restoration, borrowed work or teardown");
        return false;
    }
    q1_map_frame_begin(g);
    while (g->retired_actors) {
        q1_actor *entity = g->retired_actors;
        g->retired_actors = entity->pool_next;
        entity->pool_next = g->spare_actors;
        g->spare_actors = entity;
    }
    while (g->retired_players) {
        q1_player *player = g->retired_players;
        g->retired_players = player->pool_next;
        player->pool_next = g->spare_players;
        g->spare_players = player;
    }
    g->time_ns = frame->time_ns;
    g->time = (double)frame->time_ns / 1000000000.0;
    g->elapsed = (double)frame->elapsed_ns / 1000000000.0;
    return true;
}
static bool begin_frame(void *context, qa_session *session, const qa_source_frame *frame,
                        qa_error *error) {
    (void)session;
    qa_q1_game *g = context;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = q1_map_addon_frame(g, error) && q1_map_level_frame(g, frame, error) &&
        q1_map_ctf_frame(g, error);
    return operation_finish(&operation, ok, error);
}
static bool actor_frame_inner(void *context, qa_session *session, qa_actor_id actor,
                        const qa_source_frame *frame, qa_error *error) {
    (void)session;
    qa_q1_game *g = context;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || !entity->native)
        return true;
    if (entity->kind == Q1_MONSTER) {
        bool active;
        if (!q1_monster_mission_turn(g, entity, &active, error)) return false;
        if (!active || !q1_alive(g, actor)) return true;
    }
    qa_think_result thought;
    if (!g->services.physics)
        return qa_scheduler_run(qa_session_scheduler(g->services.session), actor, frame,
                                 QA_THINK_DURING_PHYSICS, &thought, error);
    qa_physics_motion motion = entity->physics.motion;
    if (motion != QA_PHYSICS_PUSH && motion != QA_PHYSICS_STEP) {
        if (!qa_scheduler_run(qa_session_scheduler(g->services.session), actor, frame,
                                QA_THINK_DURING_PHYSICS, &thought, error))
            return false;
        entity = q1_entity(g, actor);
        if (!entity)
            return true;
    }
    if (entity->kind == Q1_PROJECTILE) {
        bool changed;
        if (!qa_builtin_step_projectile(&g->services, actor, frame->time_ns, &changed, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (changed) {
            if (entity->state.projectile.kind == Q1_HIP_LASER) {
                qa_body_state body;
                if (!qa_world_body_read(g->services.world, actor, &body, error))
                    return false;
                entity = q1_entity(g, actor);
                if (!entity)
                    return true;
                entity->state.projectile.movedir = body.velocity;
                entity->speed = qa_vec_length(body.velocity);
            }
            if (!q1_link(g, entity, error))
                return false;
        }
    }
    qa_physics_result result;
    if (motion == QA_PHYSICS_PUSH)
        return qa_physics_step_q1_pusher(g->services.physics, actor, frame, false, &result, error);
    if (!qa_physics_step(g->services.physics, actor, frame, &result, error))
        return false;
    if (motion != QA_PHYSICS_STEP || !q1_entity(g, actor))
        return true;
    if (!qa_scheduler_run(qa_session_scheduler(g->services.session), actor, frame,
                            QA_THINK_DURING_PHYSICS, &thought, error))
        return false;
    return !q1_entity(g, actor) || qa_q1_game_water_transition(g, actor, error);
}
static bool actor_frame(void *context, qa_session *session, qa_actor_id actor,
                         const qa_source_frame *frame, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(context, &operation, error))
        return false;
    bool ok = actor_frame_inner(context, session, actor, frame, error);
    return operation_finish(&operation, ok, error);
}
static bool end_frame(void *context, qa_session *session, const qa_source_frame *frame,
                       qa_error *error) {
    (void)session;
    (void)frame;
    qa_q1_game *g = context;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    if (g->force_retouch)
        --g->force_retouch;
    return operation_finish(&operation, true, error);
}
static void released(void *context, qa_session *session, qa_actor_record actor) {
    (void)session;
    qa_q1_game_actor_released(context, actor);
}
static bool command_actor(void *context, qa_session *session, qa_actor_id actor) {
    const qa_q1_game *g = context;
    uint32_t slot;
    return g && session == g->services.session &&
        qa_q1_native_client_slot(g, actor, &slot, NULL);
}

bool qa_q1_game_create(const qa_builtin_services *services, const qa_q1_options *options,
                       const qa_q1_host *host, qa_q1_game **out, qa_error *error) {
    if (!out || !options || !options->provider || !options->combat_provider || options->skill > 3 ||
        options->program > QA_Q1_CTF || (unsigned)options->edition > QA_Q1_QUAKE64 ||
        !isfinite(options->gravity) || !isfinite(options->aim_threshold) ||
        !qa_builtin_services_validate(services, error))
        return false;
    qa_q1_game *g = calloc(1, sizeof(*g));
    if (!g)
        goto memory;
    g->capacity = qa_actors_capacity(qa_session_actors(services->session));
    g->actors = calloc(g->capacity, sizeof(*g->actors));
    g->players = calloc(g->capacity, sizeof(*g->players));
    if (!g->actors || !g->players) {
        free(g->actors);
        free(g->players);
        free(g);
        goto memory;
    }
    g->services = *services;
    g->options = *options;
    g->qw_rj = options->quakeworld ? 1 : 0;
    g->time_ns = Q1_SOURCE_INITIAL_TIME_NS;
    g->time = (double)g->time_ns / 1000000000.0;
    if (host)
        g->host = *host;
    qa_builtin_random_seed(&g->random, options->random_seed);
    for (size_t i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
        if (!qa_builtin_resource(services, weapon_names[i], &g->weapons[i], error) ||
            !qa_builtin_resource(services, weapon_models[i], &g->weapon_models[i], error))
            goto fail;
    for (size_t i = 0; i < QA_Q1_AMMO_COUNT; ++i)
        if (!qa_builtin_resource(services, ammo_names[i], &g->ammo[i], error))
            goto fail;
    if (!qa_builtin_resource(services, "progs/v_hammer_glow.mdl", &g->hammer_glow_model, error) ||
        !qa_builtin_resource(services, "progs/v_bloodshot.mdl", &g->blood_shotgun_model, error) ||
        !qa_builtin_resource(services, "progs/v_bloodshot2.mdl", &g->blood_super_shotgun_model, error))
        goto fail;
    if (!qa_builtin_resource(services, "progs/player.mdl", &g->player_model, error) ||
        !qa_builtin_resource(services, "progs/eyes.mdl", &g->eyes_model, error) ||
        !qa_builtin_resource(services, "progs/h_player.mdl", &g->player_head_model, error) ||
        !qa_builtin_resource(services, "rogue:artifact/vengeance", &g->vengeance_item, error))
        goto fail;
    if (!q1_pickup_supply_create(g, error))
        goto fail;
    *out = g;
    return true;
fail:
    qa_supply_destroy(g->source_supply);
    free(g->actors);
    free(g->players);
    free(g);
    return false;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 native provider state");
    return false;
}
bool qa_q1_game_operation_begin(qa_q1_game *g, qa_q1_game_operation *operation, qa_error *error) {
    if (!g || !operation || g->destroy_pending || g->continuation_pending ||
        g->observation_depth == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 native operation is unavailable");
        return false;
    }
    *operation = (qa_q1_game_operation){.game = g};
    ++g->observation_depth;
    return true;
}
bool qa_q1_game_command_begin(qa_q1_game *g, uint64_t time_ns, uint64_t source_elapsed_ns,
                              qa_q1_game_operation *operation, qa_error *error) {
    if (!qa_q1_game_operation_begin(g, operation, error))
        return false;
    g->time_ns = time_ns;
    g->time = (double)time_ns / 1000000000.0;
    g->elapsed = (double)source_elapsed_ns / 1000000000.0;
    return true;
}
bool qa_q1_game_clock_read(const qa_q1_game *g, uint64_t *time_ns, double *elapsed_seconds) {
    if (!g || !time_ns || !elapsed_seconds || g->destroy_pending || g->continuation_pending)
        return false;
    *time_ns = g->time_ns;
    *elapsed_seconds = g->elapsed;
    return true;
}
bool qa_q1_game_force_retouch(qa_q1_game *g, uint32_t source_frames, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    if (!g->services.physics) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 retouch has no source physics service");
        return operation_finish(&operation, false, error);
    }
    g->force_retouch = source_frames;
    return operation_finish(&operation, true, error);
}
bool qa_q1_game_retouch_actor(qa_q1_game *g, qa_actor_id actor,
                              const qa_source_frame *frame, qa_error *error) {
    if (!g || !frame || frame->provider != g->options.provider ||
        frame->kind != (g->options.quakeworld ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE) ||
        frame->phase != QA_ENTITY_PHYSICS) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 retouch belongs to another source turn");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = true;
    if (g->force_retouch && q1_alive(g, actor)) {
        uint32_t source_slot;
        const q1_actor *entity = q1_entity_const(g, actor);
        bool source_actor = g->wire
            ? qa_q1_wire_emission_slot(g, actor, &source_slot)
            : qa_q1_native_client_slot(g, actor, &source_slot, NULL) ||
                (entity && entity->native);
        if (!source_actor)
            return operation_finish(&operation, true, error);
        if (!g->services.physics) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 retouch lost its source physics service");
            ok = false;
        } else {
            ok = qa_world_link(g->services.world, actor, NULL, error);
            if (ok && q1_alive(g, actor))
                ok = qa_physics_touch_triggers_source(g->services.physics, actor,
                                                       QA_COLLISION_Q1, error);
        }
    }
    return operation_finish(&operation, ok, error);
}
bool qa_q1_game_operation_live(const qa_q1_game_operation *operation) {
    return operation && operation->game && !operation->game->destroy_pending;
}
bool qa_q1_game_retain(qa_q1_game *g, qa_q1_game_operation *operation, qa_error *error) {
    if (!g || !operation || g->destroy_pending || g->retention_depth == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 callback owner is unavailable");
        return false;
    }
    *operation = (qa_q1_game_operation){.game = g, .retained_owner = true};
    ++g->retention_depth;
    return true;
}
void qa_q1_game_operation_end(qa_q1_game_operation *operation) {
    if (!operation || !operation->game)
        return;
    qa_q1_game *g = operation->game;
    bool retained = operation->retained_owner;
    *operation = (qa_q1_game_operation){0};
    if (retained)
        --g->retention_depth;
    else
        --g->observation_depth;
    if (!g->observation_depth && !g->retention_depth && g->destroy_pending)
        qa_q1_game_destroy(g);
}
void qa_q1_game_destroy(qa_q1_game *g) {
    if (!g)
        return;
    if (g->observation_depth || g->retention_depth) {
        g->destroy_pending = true;
        return;
    }
    if (g->services.pickups)
        for (uint32_t i = 0; i < g->capacity; ++i)
            if (g->actors[i] && g->actors[i]->pickup_observation.serial)
                qa_pickups_observation_close(g->services.pickups,
                                              g->actors[i]->pickup_observation, NULL);
    q1_map_destroy(g);
    q1_wire_destroy(g);
    qa_supply_destroy(g->source_supply);
    q1_source_rogue_runes_free(g);
    while (g->allocated_actors) {
        q1_actor *next = g->allocated_actors->allocation_next;
        free(g->allocated_actors);
        g->allocated_actors = next;
    }
    while (g->allocated_players) {
        q1_player *next = g->allocated_players->allocation_next;
        q1_inventory_close(g, g->allocated_players);
        q1_source_client_clear(g->allocated_players);
        free(g->allocated_players);
        g->allocated_players = next;
    }
    qa_builtin_snapshot_pool_free(&g->snapshots);
    free(g->actors);
    free(g->players);
    free(g);
}
bool qa_q1_game_component(qa_q1_game *g, qa_component *out, qa_error *error) {
    if (!g || !out || g->component_admitted) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Q1 native component already exported or unavailable");
        return false;
    }
    *out = (qa_component){
        .owner = g->options.provider,
        .clock = qa_clock_defaults(g->options.quakeworld ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE),
        .state = g,
        .prepare_frame = prepare_frame,
        .begin_frame = begin_frame,
        .actor_frame = actor_frame,
        .end_frame = end_frame,
        .command_actor = command_actor,
        .actor_released = released};
    out->clock.initial_time_ns = Q1_SOURCE_INITIAL_TIME_NS;
    g->component_admitted = true;
    return true;
}
bool qa_q1_game_combat_policy(qa_q1_game *g, qa_combat_policy *out, qa_error *error) {
    if (!g || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q1 provider or combat policy output");
        return false;
    }
    *out = (qa_combat_policy){.provider = g->options.combat_provider,
                              .family = QA_GAME_Q1,
                              .context = g,
                              .describe = combat_context};
    return true;
}
void qa_q1_game_actor_released(qa_q1_game *g, qa_actor_record actor) {
    if (!g || actor.id.slot >= g->capacity)
        return;
    if (g->host.monster_path_release)
        g->host.monster_path_release(g->host.context, actor.id);
    q1_wire_actor_released(g, actor);
    q1_source_rogue_runes_release(g, actor.id);
    q1_grapple_released(g, actor.id);
    q1_map_rotation_released(g, actor.id);
    q1_map_addon_released(g, actor.id);
    q1_actor *entity = g->actors[actor.id.slot];
    if (entity && qa_actor_id_equal(entity->id, actor.id)) {
        q1_map_actor_released(g, entity);
        g->actors[actor.id.slot] = NULL;
        entity->active = false;
        entity->pool_next = g->retired_actors;
        g->retired_actors = entity;
    }
    q1_player *player = g->players[actor.id.slot];
    if (player && qa_actor_id_equal(player->id, actor.id)) {
        q1_inventory_close(g, player);
        g->players[actor.id.slot] = NULL;
        q1_source_client_clear(player);
        player->active = false;
        player->pool_next = g->retired_players;
        g->retired_players = player;
    }
}

static q1_actor *allocate_state(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_actor *entity = g->spare_actors;
    if (entity) {
        g->spare_actors = entity->pool_next;
        q1_actor *next = entity->allocation_next;
        memset(entity, 0, sizeof(*entity));
        entity->allocation_next = next;
    } else {
        entity = calloc(1, sizeof(*entity));
        if (!entity) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 actor state");
            return NULL;
        }
        entity->allocation_next = g->allocated_actors;
        g->allocated_actors = entity;
    }
    entity->id = actor;
    entity->active = true;
    entity->alpha = entity->scale = 1;
    g->actors[actor.slot] = entity;
    return entity;
}
bool q1_create(qa_q1_game *g, const char *classname, q1_entity_kind kind, qa_actor_id owner,
               q1_actor **out, qa_error *error) {
    qa_string_id name;
    if (!qa_builtin_resource(&g->services, classname, &name, error))
        return false;
    qa_combat_state combat = {.mass = 100};
    qa_builtin_spawn spawn = {.owner = g->options.provider, .definition = name, .combat = &combat};
    if (!q1_wire_allocate_slot(g, &spawn.has_source, &spawn.source_slot, error))
        return false;
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &actor, error))
        return false;
    q1_actor *entity = allocate_state(g, actor, error);
    if (!entity) {
        (void)qa_session_release(g->services.session, actor, NULL);
        return false;
    }
    entity->owner = owner;
    entity->classname = name;
    entity->kind = kind;
    entity->native = true;
    entity->physics = qa_physics_properties_default(QA_COLLISION_Q1);
    if (!q1_map_bind_target(g, entity, error)) {
        (void)q1_remove(g, entity, NULL);
        return false;
    }
    *out = entity;
    return true;
}
bool q1_remove(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    return !q1_alive(g, entity->id) || qa_session_release(g->services.session, entity->id, error);
}
bool q1_link(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->kind == Q1_PICKUP && !entity->pickup_observation.serial &&
        !q1_pickup_observe(g, entity, error))
        return false;
    qa_actor_collision collision = {.family = QA_COLLISION_Q1,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = -2,
                                    .owner = entity->owner,
                                    .monster = (entity->physics.flags & QA_PHYSICS_MONSTER) != 0,
                                    .q1_corpse = entity->physics.solid == QA_PHYSICS_CORPSE,
                                    .role = entity->physics.solid == QA_PHYSICS_TRIGGER
                                                ? QA_COLLISION_TRIGGER
                                                : QA_COLLISION_SOLID};
    if (entity->physics.solid == QA_PHYSICS_BRUSH)
        (void)q1_map_collision(entity, &collision);
    if (!qa_world_set_collision(g->services.world, entity->id,
                                entity->physics.solid == QA_PHYSICS_NOT_SOLID ? NULL : &collision,
                                error))
        return false;
    return qa_world_link(g->services.world, entity->id, NULL, error);
}
static bool think_callback(void *, qa_actor_id, const qa_think_scope *, qa_error *);
static bool schedule_source_think(qa_q1_game *g, q1_actor *entity, double due,
                                   q1_think_kind kind, uint64_t minimum_ns, qa_error *error) {
    double ns = due > 0 ? ceil(due * 1000000000.0) : 0;
    if (!isfinite(ns) || ns >= (double)UINT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 think deadline out of range");
        return false;
    }
    uint64_t projected = (uint64_t)ns;
    if (projected < minimum_ns)
        projected = minimum_ns;
    qa_think think = {.actor = entity->id,
                      .execution_provider = g->options.provider,
                      .callback_id = (uint32_t)kind,
                      .due_ns = projected,
                      .boundary = QA_THINK_DURING_PHYSICS,
                      .callback = think_callback,
                      .context = g};
    if (!qa_session_schedule(g->services.session, &think, error))
        return false;
    entity->think = kind;
    entity->next_think = due;
    return true;
}
static bool think_callback(void *context, qa_actor_id actor, const qa_think_scope *scope,
                           qa_error *error) {
    qa_q1_game *g = context;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_clock_kind clock = g->options.quakeworld ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE;
    bool admitted = scope &&
        ((scope->kind == QA_THINK_WORLD_FRAME &&
          scope->source.frame.provider == g->options.provider &&
          scope->source.frame.kind == clock) ||
         (scope->kind == QA_THINK_SOURCE_COMMAND &&
          scope->source.command.provider == g->options.provider &&
          scope->source.command.kind == clock &&
          qa_actor_id_equal(scope->source.command.actor, actor)));
    if (!admitted) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 think source admission mismatch");
        return operation_finish(&operation, false, error);
    }
    q1_actor *entity = q1_entity(g, actor);
    if (!entity)
        return operation_finish(&operation, true, error);
    double callback_time = (double)scope->time_ns / 1000000000.0;
    if (entity->physics.motion != QA_PHYSICS_PUSH) {
        uint64_t start_ns = scope->interval_start_ns;
        uint64_t elapsed_ns = scope->interval_elapsed_ns;
        double current = (double)start_ns / 1000000000.0;
        if (start_ns > UINT64_MAX - elapsed_ns) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 think source interval overflow");
            return operation_finish(&operation, false, error);
        }
        double due = entity->next_think;
        if (!(due > 0))
            return operation_finish(&operation, true, error);
        if (due > current + (double)elapsed_ns / 1000000000.0) {
            uint64_t end_ns = start_ns + elapsed_ns;
            if (end_ns == UINT64_MAX) {
                qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 think cannot defer beyond source clock");
                return operation_finish(&operation, false, error);
            }
            bool okay = schedule_source_think(g, entity, due, entity->think, end_ns + 1, error);
            return operation_finish(&operation, okay, error);
        }
        callback_time = fmax(current, due);
    }
    double previous = g->time;
    double previous_elapsed = g->elapsed;
    uint64_t previous_ns = g->time_ns;
    g->time_ns = scope->time_ns;
    g->time = callback_time;
    g->elapsed = (double)scope->interval_elapsed_ns / 1000000000.0;
    bool result = q1_think(g, entity, error);
    g->time = previous;
    g->elapsed = previous_elapsed;
    g->time_ns = previous_ns;
    return operation_finish(&operation, result, error);
}
bool qa_q1_game_think_binding(qa_q1_game *g, qa_actor_id actor, uint32_t callback_id,
                               qa_think_fn *callback, void **context, qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || !callback || !context || callback_id == Q1_THINK_NONE ||
        callback_id > Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN || entity->think != (q1_think_kind)callback_id ||
        entity->physics.motion == QA_PHYSICS_PUSH) {
        qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "Invalid restored Q1 think binding");
        return false;
    }
    *callback = think_callback;
    *context = g;
    return true;
}
bool q1_local_time(const q1_actor *entity, double *out, qa_error *error) {
    double value = entity->physics.q1_pusher.local_seconds;
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "Q1 local time exceeds finite float range");
        return false;
    }
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}
bool q1_think_deadline(double time, double delay, double *out, qa_error *error) {
    double value = time + delay;
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 think deadline exceeds finite float range");
        return false;
    }
    float rounded = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    *out = rounded;
    return true;
}
bool q1_schedule(qa_q1_game *g, q1_actor *entity, double delay, q1_think_kind kind,
                 qa_error *error) {
    double due;
    if (!q1_think_deadline(g->time, delay, &due, error))
        return false;
    if (entity->physics.motion == QA_PHYSICS_PUSH) {
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
        entity->think = kind;
        entity->next_think = due;
        return true;
    }
    return schedule_source_think(g, entity, due, kind, 0, error);
}
bool q1_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_think_kind kind = entity->think;
    entity->think = Q1_THINK_NONE;
    entity->next_think = 0;
    switch (kind) {
    case Q1_THINK_NONE:
        return true;
    case Q1_THINK_MAP:
        return q1_map_think(g, entity, error);
    case Q1_THINK_SPAWN_TEMPLATE:
        return q1_map_spawn_template_wait(g, entity, error);
    case Q1_THINK_SOURCE_CTF_FLAG_PLACE:
    case Q1_THINK_SOURCE_CTF_FLAG:
        return q1_source_flag_think(g, entity, kind, error);
    case Q1_THINK_SOURCE_CTF_RUNE_SPAWN:
    case Q1_THINK_SOURCE_CTF_RUNE_RESPAWN:
        return q1_source_rune_think(g, entity, kind, error);
    case Q1_THINK_SOURCE_ROGUE_TAG_PLACE:
    case Q1_THINK_SOURCE_ROGUE_TAG:
    case Q1_THINK_SOURCE_ROGUE_TAG_FALL:
    case Q1_THINK_SOURCE_ROGUE_TAG_RESPAWN:
        return q1_source_rogue_tag_think(g, entity, kind, error);
    case Q1_THINK_SOURCE_ROGUE_FLAG_PLACE:
    case Q1_THINK_SOURCE_ROGUE_FLAG:
        return q1_source_rogue_flag_think(g, entity, kind, error);
    case Q1_THINK_SOURCE_ROGUE_RUNE_SPAWN:
    case Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN:
        return q1_source_rogue_rune_think(g, entity, kind, error);
    case Q1_THINK_MG3_HAMMER:
        return q1_mg3_hammer_strike(g, entity, error);
    case Q1_THINK_REMOVE:
        return q1_remove(g, entity, error);
    case Q1_THINK_MONSTER_START:
        return q1_monster_start(g, entity, error);
    case Q1_THINK_MONSTER_FRAME:
        return q1_monster_frame(g, entity, error);
    case Q1_THINK_MONSTER_FOUND:
        return !entity->state.monster.enemy.registry ||
               q1_monster_found(g, entity, entity->state.monster.enemy, error);
    case Q1_THINK_GHOST_BUBBLES:
        return q1_ghost_bubbles(g, entity, error);
    case Q1_THINK_BOSS_CHILD:
        return q1_boss_child_think(g, entity, error);
    case Q1_THINK_HOMING_FLAME:
        return q1_homing_flame_think(g, entity, error);
    case Q1_THINK_DEATH_BUBBLES:
        return q1_character_bubbles(g, entity, error);
    case Q1_THINK_BUBBLE:
        return q1_bubble_think(g, entity, error);
    case Q1_THINK_SHIELD:
    case Q1_THINK_SPHERE_ORBIT:
    case Q1_THINK_SPHERE_ATTACK:
        return q1_power_think(g, entity, kind, error);
    case Q1_THINK_HOOK_LAUNCH:
        return q1_grapple_weapon_launch(g, entity, error);
    case Q1_THINK_HOOK_FLY:
    case Q1_THINK_HOOK_TRACK:
    case Q1_THINK_HOOK_RESET:
    case Q1_THINK_HOOK_LINK:
        return q1_grapple_think(g, entity, kind, error);
    case Q1_THINK_SCOURGE_TRIGGER:
        return q1_scourge_trigger(g, entity, (qa_actor_id){0}, error);
    case Q1_THINK_MULTI_EXPLOSION:
        return q1_multi_explosion_think(g, entity, error);
    case Q1_THINK_ARMAGON_BODY:
    case Q1_THINK_ARMAGON_EXPLOSION:
        return q1_armagon_think(g, entity, kind, error);
    case Q1_THINK_TELEPORT_FOG:
        return q1_teleport_fog_think(g, entity, error);
    case Q1_THINK_WRATH_HOME:
    case Q1_THINK_WRATH_EXPLODE:
        return q1_wrath_think(g, entity, kind, error);
    case Q1_THINK_AXE:
        return q1_axe_strike(g, entity, error);
    case Q1_THINK_EXPLODE:
        return q1_explode(g, entity, (qa_actor_id){0}, error);
    case Q1_THINK_DEMODOG_EXPLODE:
        return q1_demodog_explode(g, entity, (qa_actor_id){0}, error);
    case Q1_THINK_HORDE_HEAD_WAIT:
    case Q1_THINK_HORDE_HEAD_STEP:
        return q1_horde_head_think(g, entity, kind, error);
    case Q1_THINK_HEAVY_SOURCE_DIE:
        return q1_heavy_die(g, entity, error);
    case Q1_THINK_VORE:
    case Q1_THINK_SPRITE:
    case Q1_THINK_WIZARD:
        entity->think = kind;
        return q1_projectile_think(g, entity, error);
    case Q1_THINK_RESPAWN:
    case Q1_THINK_MEGA_ROT:
    case Q1_THINK_ITEM_PLACE:
    case Q1_THINK_MG3_ITEM_START:
        entity->think = kind;
        return q1_pickup_think(g, entity, error);
    case Q1_THINK_HIP_LASER:
    case Q1_THINK_PROX_WATCH:
    case Q1_THINK_PROX_EXPLODE:
    case Q1_THINK_HAMMER_STRIKE:
    case Q1_THINK_HAMMER_BOLT:
    case Q1_THINK_MULTI_SPLIT:
    case Q1_THINK_MINI_EXPLODE:
    case Q1_THINK_MULTI_EXPLODE:
    case Q1_THINK_MULTI_ACQUIRE:
    case Q1_THINK_MULTI_HOME:
    case Q1_THINK_PLASMA_LAUNCH:
        return q1_expansion_think(g, entity, kind, error);
    }
    return false;
}
bool q1_trace(qa_q1_game *g, qa_vec3 start, qa_vec3 end, qa_actor_id pass, bool monsters,
              qa_trace_result *out, qa_error *error) {
    qa_trace_query query = {.start = start,
                            .end = end,
                            .shape.kind = QA_SHAPE_POINT,
                            .pass_actor = pass,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    query.policy.q1_move = monsters ? QA_Q1_MOVE_NORMAL : QA_Q1_MOVE_NO_MONSTERS;
    return qa_world_trace(g->services.world, &query, out, error);
}
qa_attack q1_attack(qa_q1_game *g, qa_actor_id attacker, qa_actor_id inflictor,
                    qa_q1_weapon weapon) {
    q1_actor *entity = q1_entity(g, inflictor);
    if (entity && entity->kind == Q1_PROJECTILE &&
        entity->state.projectile.attack.weapon_provider) {
        qa_attack attack = entity->state.projectile.attack;
        attack.attacker = attacker;
        attack.time_ns = g->time_ns;
        return attack;
    }
    return (qa_attack){.time_ns = g->time_ns,
                       .attacker = attacker,
                       .inflictor = inflictor,
                       .weapon = weapon < QA_Q1_WEAPON_COUNT ? g->weapons[weapon] : 0,
                       .weapon_provider = g->options.provider,
                       .combat_provider = g->options.combat_provider,
                       .inventory_provider = g->options.inventory_provider,
                       .movement_provider = g->options.movement_provider,
                       .cause = {.kind = QA_CAUSE_Q1}};
}
bool q1_damage(qa_q1_game *g, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker,
               float amount, qa_q1_weapon weapon, qa_error *error) {
    qa_attack attack = q1_attack(g, attacker, inflictor, weapon);
    return q1_damage_typed(g, target, inflictor, attacker, amount, weapon, QA_Q1_ARMOR_NORMAL,
                           attack.cause.kind == QA_CAUSE_Q1 ? attack.cause.source.q1.death_type : 0,
                           error);
}
static void damage_geometry(qa_q1_game *g, qa_damage_request *request) {
    qa_body_state source, target;
    request->point = qa_world_body_read(g->services.world, request->target, &target, NULL)
                         ? target.origin
                         : qa_v3(0, 0, 0);
    request->direction =
        qa_world_body_read(g->services.world, request->attack.inflictor, &source, NULL)
            ? qa_vec_normalize(qa_vec_sub(request->point, source.origin))
            : qa_v3(0, 0, 0);
    request->knockback = request->amount;
}
bool q1_damage_typed(qa_q1_game *g, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker,
                     float amount, qa_q1_weapon weapon, qa_q1_armor_effect armor,
                     qa_string_id death_type, qa_error *error) {
    qa_damage_request request = {
        .attack = q1_attack(g, attacker, inflictor, weapon), .target = target, .amount = amount};
    request.attack.cause = (qa_damage_cause){
        .kind = QA_CAUSE_Q1, .source.q1 = {.armor = armor, .death_type = death_type}};
    if (g->host.combat_provider)
        request.attack.combat_provider = g->host.combat_provider(g->host.context, target);
    damage_geometry(g, &request);
    if (g->host.source_damage && !g->host.source_damage(g->host.context, &request, error))
        return false;
    if (!q1_damageable(g, target)) return true;
    if (!qa_attack_next(&g->attack_sequence, &request.attack, error))
        return false;
    if (!q1_alive(g, target)) return true;
    qa_damage_outcome outcome = {0};
    bool ok = qa_combat_apply(g->services.combat, &request, &outcome, error);
    qa_damage_outcome_free(&outcome);
    return ok;
}
static bool radius_adjust(void *context, qa_actor_id target, float *damage, float *knockback,
                          bool *allowed, qa_error *error) {
    (void)knockback;
    (void)allowed;
    (void)error;
    qa_q1_game *g = context;
    if (q1_classnamed(g, target, "monster_shambler"))
        *damage *= 0.5f;
    return true;
}
static bool radius_prepare(void *context, qa_damage_request *request, bool *allowed,
                           qa_error *error) {
    qa_q1_game *g = context;
    if (g->host.combat_provider)
        request->attack.combat_provider = g->host.combat_provider(g->host.context, request->target);
    damage_geometry(g, request);
    if (g->host.source_damage && !g->host.source_damage(g->host.context, request, error))
        return false;
    if (!q1_alive(g, request->target)) { *allowed = false; return true; }
    return qa_attack_next(&g->attack_sequence, &request->attack, error);
}
bool q1_radius(qa_q1_game *g, qa_actor_id inflictor, qa_actor_id attacker, float amount,
               qa_actor_id ignore, qa_q1_weapon weapon, qa_error *error) {
    return q1_radius_typed(g, inflictor, attacker, amount, ignore, weapon, NULL, error);
}
bool q1_radius_typed(qa_q1_game *g, qa_actor_id inflictor, qa_actor_id attacker, float amount,
                     qa_actor_id ignore, qa_q1_weapon weapon, const char *cause, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, inflictor, &body, error))
        return false;
    qa_builtin_radius radius = {.attack = q1_attack(g, attacker, inflictor, weapon),
                                .origin = body.origin,
                                .radius = amount + 40,
                                .damage = amount,
                                .distance_scale = 0.5f,
                                .self_scale = 0.5f,
                                .knockback_scale = 1,
                                .ignore = ignore,
                                .visibility_pass = inflictor,
                                .trace = qa_collision_default_policy(QA_COLLISION_Q1),
                                .check_visibility = true,
                                .context = g,
                                .adjust = radius_adjust,
                                .prepare = radius_prepare};
    radius.trace.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    if (cause &&
        !qa_builtin_resource(&g->services, cause, &radius.attack.cause.source.q1.death_type, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    radius.has_candidates = true;
    radius.candidates = snapshot->snapshot.ids;
    radius.candidate_count = snapshot->snapshot.count;
    bool ok = qa_builtin_radius_damage(&g->services, &radius, NULL, error);
    qa_builtin_snapshot_release(snapshot);
    return ok;
}
bool q1_can_damage(qa_q1_game *g, qa_actor_id target, qa_actor_id from, bool *out,
                   qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, from, &body, error))
        return false;
    qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q1);
    policy.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    return qa_builtin_can_damage(&g->services, body.origin, target, from, policy, false, out,
                                 error);
}

static bool spawn_actor(qa_q1_game *g, const qa_q1_spawn *spawn, const qa_body_state *initial,
                        qa_actor_id *out, qa_error *error) {
    if (!g || !spawn || !spawn->classname || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 native spawn");
        return false;
    }
    if (!strcmp(spawn->classname, "trigger_dragon")) {
        *out = (qa_actor_id){0};
        return true;
    }
    const q1_species *species = q1_species_find(spawn->classname);
    bool dormant_mine = !strcmp(spawn->classname, "monster_spikemine");
    if ((species || dormant_mine) && g->options.deathmatch && !spawn->authored_monster) {
        *out = (qa_actor_id){0};
        return true;
    }
    if (!q1_wire_spawn_declarations(g, spawn, error))
        return false;
    qa_string_id name;
    if (!qa_builtin_resource(&g->services, spawn->classname, &name, error))
        return false;
    qa_combat_state combat = {.health = spawn->health, .mass = 100};
    qa_builtin_spawn request = {.owner = g->options.provider,
                                .definition = name,
                                .has_source = spawn->has_source,
                                .source_slot = spawn->source_slot,
                                .body = {.origin = spawn->origin, .angles = spawn->angles},
                                .combat = &combat};
    if (initial)
        request.body = *initial;
    if (!request.has_source &&
        !q1_wire_allocate_slot(g, &request.has_source, &request.source_slot, error))
        return false;
    if (request.has_source && !q1_wire_spawn_slot_valid(g, request.source_slot)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 spawn has no reached allocated Source edict");
        return false;
    }
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(&g->services, &request, &actor, error))
        return false;
    if (!q1_alive(g, actor)) {
        (void)qa_session_release(g->services.session, actor, NULL);
        *out = (qa_actor_id){0};
        return true;
    }
    q1_actor *entity = allocate_state(g, actor, error);
    if (!entity) {
        (void)qa_session_release(g->services.session, actor, NULL);
        return false;
    }
    entity->classname = name;
    entity->native = true;
    entity->initial_angles = spawn->angles;
    entity->physics = qa_physics_properties_default(QA_COLLISION_Q1);
    entity->spawnflags = spawn->spawnflags;
    entity->source_movement_flags = spawn->source_movement_flags;
    entity->max_health = spawn->health;
    entity->speed = spawn->speed;
    entity->wait = spawn->wait;
    entity->delay = spawn->delay;
    entity->damage = spawn->damage;
    entity->count = spawn->count;
    if (!species && !dormant_mine)
        entity->state.pickup.upgrade_flag = spawn->upgrade_flag;
    if (!qa_builtin_resource(&g->services, spawn->target ? spawn->target : "", &entity->target,
                             error) ||
        !qa_builtin_resource(&g->services, spawn->targetname ? spawn->targetname : "",
                             &entity->targetname, error) ||
        !qa_builtin_resource(&g->services, spawn->killtarget ? spawn->killtarget : "",
                             &entity->killtarget, error) ||
        !qa_builtin_resource(&g->services, spawn->message ? spawn->message : "", &entity->message,
                             error))
        goto fail;
    if (spawn->map_fields) {
        const qa_q1_map_fields *words = spawn->map_fields;
        const char *input[] = {words->netname, words->kill_string, words->death_type, words->team};
        qa_string_id *output[] = {&entity->source_netname, &entity->source_kill_string,
            &entity->source_death_type, &entity->source_team};
        for (size_t i = 0; i < sizeof(input) / sizeof(*input); ++i)
            if (input[i] && !qa_strings_intern_cstr(qa_session_strings(g->services.session),
                input[i], output[i], error)) goto fail;
    }
    if (!q1_map_bind_target(g, entity, error))
        goto fail;
    if (spawn->authored_monster && (!species || !g->host.monster_admit ||
        !g->host.monster_admit(g->host.context, actor, spawn->authored_monster, error)))
        goto fail;
    bool flag_handled;
    if (!q1_source_rogue_flag_spawn(g, entity, &flag_handled, error)) goto fail;
    if (flag_handled) {
        *out = q1_alive(g, actor) ? actor : (qa_actor_id){0};
        return true;
    }
    if (!q1_source_rogue_tag_spawn(g, entity, &flag_handled, error)) goto fail;
    if (flag_handled) {
        *out = q1_alive(g, actor) ? actor : (qa_actor_id){0};
        return true;
    }
    if (!q1_source_flag_spawn(g, entity, &flag_handled, error)) goto fail;
    if (flag_handled) {
        *out = q1_alive(g, actor) ? actor : (qa_actor_id){0};
        return true;
    }
    bool map_handled;
    if (!q1_map_spawn(g, entity, spawn, &map_handled, error))
        goto fail;
    if (map_handled) {
        *out = q1_alive(g, actor) ? actor : (qa_actor_id){0};
        return true;
    }
    bool boss_map_handled;
    if (!q1_final_map_spawn(g, entity, &boss_map_handled, error))
        goto fail;
    if (boss_map_handled) {
        *out = actor;
        return true;
    }
    if (!strcmp(spawn->classname, "info_szombie_spawn")) {
        entity->wait = -1;
    } else if (!strcmp(spawn->classname, "dragon_corner")) {
        if (!spawn->targetname || !*spawn->targetname) {
            qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "dragon_corner: no targetname");
            goto fail;
        }
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        qa_body_state body = request.body;
        body.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
        if (!qa_world_body_write(g->services.world, actor, &body, error) ||
            !q1_link(g, entity, error))
            goto fail;
    } else if (!strcmp(spawn->classname, "info_overlord_destination")) {
        qa_body_state body = request.body;
        body.angles = qa_v3(0, 0, 0);
        body.origin.z += 27;
        if (!qa_world_body_write(g->services.world, actor, &body, error))
            goto fail;
    } else if (dormant_mine) {
        entity->physics.solid = QA_PHYSICS_BOX;
        entity->physics.motion = QA_PHYSICS_STEP;
        qa_body_state body = request.body;
        body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, 64}};
        if (!q1_model(g, entity, "progs/demon.mdl", error) ||
            !qa_world_body_write(g->services.world, actor, &body, error) ||
            !q1_link(g, entity, error))
            goto fail;
    } else if (species ? !q1_monster_spawn(g, entity, species, error)
                       : !q1_pickup_spawn(g, entity, error))
        goto fail;
    if (species && spawn->boss_fields &&
        !q1_major_boss_fields(g, entity, spawn->boss_fields, error))
        goto fail;
    *out = q1_alive(g, actor) ? actor : (qa_actor_id){0};
    return true;
fail:
    (void)qa_session_release(g->services.session, actor, NULL);
    return false;
}
bool qa_q1_game_spawn(qa_q1_game *g, const qa_q1_spawn *spawn, qa_actor_id *out, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = spawn_actor(g, spawn, NULL, out, error);
    if (out && (!ok || !qa_q1_game_operation_live(&operation)))
        *out = (qa_actor_id){0};
    return operation_finish(&operation, ok, error);
}
bool q1_spawn_template(qa_q1_game *g, const qa_q1_spawn *spawn, const qa_body_state *body,
                       qa_actor_id *out, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    int32_t previous = g->options.deathmatch;
    g->options.deathmatch = 0;
    bool ok = spawn_actor(g, spawn, body, out, error);
    g->options.deathmatch = previous;
    if (out && (!ok || !qa_q1_game_operation_live(&operation)))
        *out = (qa_actor_id){0};
    return operation_finish(&operation, ok, error);
}
static bool touch_inner(qa_q1_game *g, const qa_touch_contact *contact, qa_error *error) {
    q1_actor *entity = q1_entity(g, contact->self);
    if (!entity || entity->touch_disabled)
        return true;
    if (entity->kind == Q1_MAP)
        return q1_map_touch(g, entity, contact, error);
    if (entity->kind == Q1_SOURCE_CTF_FLAG)
        return q1_source_flag_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_SOURCE_CTF_RUNE)
        return q1_source_rune_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_SOURCE_ROGUE_TAG)
        return q1_source_rogue_tag_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_SOURCE_ROGUE_FLAG || entity->kind == Q1_SOURCE_ROGUE_FLAG_BASE)
        return q1_source_rogue_flag_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_SOURCE_ROGUE_RUNE)
        return q1_source_rogue_rune_touch(g, entity, contact->other, error);
    if (q1_classnamed(g, entity->id, "dragon_corner"))
        return q1_dragon_corner_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_PROJECTILE)
        return q1_projectile_touch(g, entity, contact->other, contact, error);
    if (entity->kind == Q1_BOSS_CHILD)
        return q1_boss_child_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_MONSTER)
        return q1_monster_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_PICKUP)
        return q1_pickup_touch(g, entity, contact->other, error);
    if (entity->kind == Q1_TIMER && q1_classnamed(g, entity->id, "scourge_trigger"))
        return q1_scourge_trigger(g, entity, contact->other, error);
    if (entity->kind == Q1_TIMER &&
        (q1_classnamed(g, entity->id, "teledeath") || q1_classnamed(g, entity->id, "teledeath2")))
        return q1_teledeath_touch(g, entity, contact->other, error);
    return true;
}
bool qa_q1_game_touch_source(qa_q1_game *g, const qa_touch_contact *contact,
                             bool *reached, qa_error *error) {
    if (!contact || !reached) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 touch requires its actual contact and callback result");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool bound = q1_entity_const(g, contact->self) != NULL;
    bool ok = touch_inner(g, contact, error);
    if (!operation_finish(&operation, ok, error)) return false;
    *reached = bound;
    return true;
}
bool qa_q1_game_touch(qa_q1_game *g, const qa_touch_contact *contact, qa_error *error) {
    bool reached;
    return qa_q1_game_touch_source(g, contact, &reached, error);
}
bool qa_q1_game_use(qa_q1_game *g, qa_actor_id actor, qa_actor_id activator, qa_error *error) {
    return qa_q1_game_use_from(g, actor, activator, activator, error);
}
static bool use_inner(qa_q1_game *g, qa_actor_id actor, qa_actor_id other, qa_actor_id activator,
                         qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    if (entity && entity->kind == Q1_MAP)
        return q1_map_use(g, entity, other, activator, error);
    if (entity)
        entity->activator = activator;
    if (entity && q1_classnamed(g, actor, "trigger_boss_teleport"))
        return q1_final_teleport(g, (entity->spawnflags & 1) != 0, error);
    if (entity && entity->kind == Q1_PICKUP)
        return q1_pickup_use(g, entity, error);
    if (entity && entity->kind == Q1_MONSTER && entity->state.monster.addon.normal_use &&
        (entity->state.monster.addon.boss == Q1_BOSS_FINAL ||
         (entity->state.monster.species->species == QA_Q1_LAVA_MAN &&
          g->options.program == QA_Q1_MG3)))
        return q1_monster_use(g, entity, activator, error);
    if (entity && entity->kind == Q1_MONSTER && entity->state.monster.addon.boss == Q1_BOSS_FINAL &&
        !entity->state.monster.source.boss.awake)
        return q1_final_awake(g, entity, activator, error);
    if (entity && entity->kind == Q1_MONSTER &&
        entity->state.monster.species->species == QA_Q1_DRAGON)
        return q1_dragon_use(g, entity, error);
    if (entity && entity->kind == Q1_MONSTER &&
        entity->state.monster.species->species == QA_Q1_LAVA_MAN)
        return g->options.program == QA_Q1_MG3 ? q1_lavaman_use(g, entity, activator, error)
                                               : q1_lavaman_awake(g, entity, activator, error);
    if (entity && entity->kind == Q1_MONSTER &&
        entity->state.monster.species->species == QA_Q1_MORPH) {
        entity->state.monster.next_frame = q1_frame_index("morph_wake");
        return q1_schedule(g, entity, entity->delay != 0 ? entity->delay : 0.1, Q1_THINK_MONSTER_FRAME,
                           error);
    }
    return !entity || entity->kind != Q1_MONSTER || q1_monster_use(g, entity, activator, error);
}
typedef struct q1_actor_callback_context {
    qa_q1_game_operation *operation;
    q1_actor *entity;
    const qa_damage_outcome *outcome;
} q1_actor_callback_context;
static bool callback_current(const q1_actor_callback_context *call, qa_actor_id actor,
                             qa_error *error) {
    qa_q1_game *g = call->operation->game;
    if (!qa_q1_game_operation_live(call->operation) || !q1_alive(g, actor) ||
        q1_entity(g, actor) != call->entity) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 callback lost its actual source actor");
        return false;
    }
    return true;
}
static bool use_body(void *opaque, const qa_builtin_actor_callback_request *request,
                     bool *result, qa_error *error) {
    q1_actor_callback_context *call = opaque;
    if (!callback_current(call, request->self, error)) return false;
    *result = use_inner(call->operation->game, request->self,
        request->source.use.other, request->source.use.activator, error);
    return *result;
}
bool qa_q1_game_use_from(qa_q1_game *g, qa_actor_id actor, qa_actor_id other,
                         qa_actor_id activator, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok;
    q1_actor *entity = q1_entity(g, actor);
    if (g->services.actor_callback && entity && q1_alive(g, actor)) {
        qa_builtin_actor_callback_request request = {.family = QA_GAME_Q1,
            .provider = g->options.provider, .self = actor,
            .source.use = {other, activator}};
        q1_actor_callback_context context = {&operation, entity, NULL};
        bool result = false;
        ok = g->services.actor_callback(g->services.context, QA_BUILTIN_ACTOR_USE,
            &request, use_body, &context, &result, error);
        if (ok && q1_alive(g, actor)) ok = callback_current(&context, actor, error);
    } else ok = use_inner(g, actor, other, activator, error);
    return operation_finish(&operation, ok, error);
}
static bool blocked_inner(qa_q1_game *g, qa_actor_id actor, qa_actor_id obstacle, qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    return !entity || entity->kind != Q1_MAP || q1_map_blocked(g, entity, obstacle, error);
}
bool qa_q1_game_blocked(qa_q1_game *g, qa_actor_id actor, qa_actor_id obstacle, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = blocked_inner(g, actor, obstacle, error);
    return operation_finish(&operation, ok, error);
}
bool qa_q1_game_pusher_think(qa_q1_game *g, qa_actor_id actor, const qa_source_frame *frame,
                             qa_error *error) {
    if (!g || !frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 pusher continuation");
        return false;
    }
    qa_think_scope scope = {.kind = QA_THINK_WORLD_FRAME,
                            .source.frame = *frame,
                            .time_ns = frame->time_ns,
                            .interval_start_ns = frame->start_ns,
                            .interval_elapsed_ns = frame->elapsed_ns};
    return think_callback(g, actor, &scope, error);
}
static bool reaction_dispatch(qa_q1_game *g, const qa_damage_outcome *outcome, qa_error *error) {
    q1_actor *entity = q1_entity(g, outcome->request.target);
    if (entity && entity->kind == Q1_BOSS_CHILD)
        return q1_boss_child_reaction(g, entity, outcome, error);
    if (entity && entity->kind == Q1_MAP)
        return q1_map_reaction(g, entity, outcome, error);
    if (entity && entity->kind == Q1_PROJECTILE && entity->state.projectile.kind == Q1_PROXIMITY &&
        outcome->result.reaction == QA_REACTION_DEATH)
        return q1_proximity_arm(g, entity, 0.1, error);
    if (!entity || entity->kind != Q1_MONSTER)
        return qa_q1_character_reaction(g, outcome, error);
    if (outcome->result.reaction == QA_REACTION_DEATH)
        return q1_monster_die(g, entity, outcome->request.attack.attacker, error);
    if (outcome->result.reaction == QA_REACTION_PAIN)
        return q1_monster_pain(g, entity, outcome->request.attack.attacker,
                               outcome->result.applied_damage, error);
    return true;
}
static bool reaction_inner(qa_q1_game *g, const qa_damage_outcome *outcome, qa_error *error) {
    if (!reaction_dispatch(g, outcome, error))
        return false;
    qa_actor_id actor = outcome->request.target;
    if (outcome->result.reaction != QA_REACTION_PAIN || g->options.quakeworld ||
        !q1_alive(g, actor))
        return true;
    bool nightmare = g->options.program == QA_Q1_MG3
                         ? g->options.skill > 2 && !q1_classnamed(g, actor, "monster_boss") &&
                               !q1_classnamed(g, actor, "monster_zombie")
                         : g->options.skill == 3;
    if (!nightmare)
        return true;
    q1_actor *entity = q1_entity(g, actor);
    if (entity && entity->kind == Q1_MONSTER)
        entity->state.monster.pain_finished = g->time + 5;
    q1_player *player = q1_player_get(g, actor);
    if (player && player->character)
        player->character_state.pain_until = g->time + 5;
    return true;
}
static bool reaction_body(void *opaque, const qa_builtin_actor_callback_request *request,
                          bool *result, qa_error *error) {
    q1_actor_callback_context *call = opaque;
    if (!callback_current(call, request->self, error)) return false;
    *result = reaction_inner(call->operation->game, call->outcome, error);
    return *result;
}
bool qa_q1_game_reaction(qa_q1_game *g, const qa_damage_outcome *outcome, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_actor_id actor = outcome->request.target;
    q1_actor *entity = q1_entity(g, actor);
    bool ok;
    if (g->services.actor_callback && entity && q1_alive(g, actor) &&
        (outcome->result.reaction == QA_REACTION_PAIN ||
         outcome->result.reaction == QA_REACTION_DEATH)) {
        bool death = outcome->result.reaction == QA_REACTION_DEATH;
        qa_builtin_actor_callback_request request = {.family = QA_GAME_Q1,
            .provider = g->options.provider, .self = actor};
        if (death) {
            request.source.die.attacker = outcome->request.attack.attacker;
            request.source.die.inflictor = outcome->request.attack.inflictor;
            request.source.die.damage = outcome->result.applied_damage;
            request.source.die.kick = outcome->result.knockback;
            request.source.die.point = outcome->request.point;
        } else {
            request.source.pain.attacker = outcome->request.attack.attacker;
            request.source.pain.damage = outcome->result.applied_damage;
            request.source.pain.kick = outcome->result.knockback;
        }
        q1_actor_callback_context context = {&operation, entity, outcome};
        bool result = false;
        ok = g->services.actor_callback(g->services.context,
            death ? QA_BUILTIN_ACTOR_DIE : QA_BUILTIN_ACTOR_PAIN,
            &request, reaction_body, &context, &result, error);
        if (ok && q1_alive(g, actor)) ok = callback_current(&context, actor, error);
    } else ok = reaction_inner(g, outcome, error);
    return operation_finish(&operation, ok, error);
}
bool qa_q1_game_presentation(const qa_q1_game *g, qa_actor_id actor, qa_q1_presentation *out) {
    const q1_actor *entity = q1_entity_const(g, actor);
    if (!entity || !out)
        return false;
    *out = (qa_q1_presentation){.actor = actor,
                                .classname = entity->classname,
                                .model = entity->model,
                                .targetname = entity->targetname,
                                .frame = entity->frame,
                                .skin = entity->skin,
                                .effects = entity->effects,
                                .alpha = entity->alpha,
                                .scale = entity->scale};
    return true;
}
static uint64_t hostile_deadline_ns(double seconds) {
    if (!(seconds > 0))
        return 0;
    double ns = seconds * 1000000000.0;
    return !isfinite(ns) || ns >= (double)UINT64_MAX ? UINT64_MAX : (uint64_t)ns;
}
bool qa_q1_game_actor_traits(const qa_q1_game *g, qa_actor_id actor, qa_builtin_actor_traits *out) {
    if (!g || !out || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_actor *entity = q1_entity_const(g, actor);
    const q1_player *player = g->players[actor.slot];
    if (player && (!player->active || !qa_actor_id_equal(player->id, actor)))
        player = NULL;
    if (!entity && !player)
        return false;
    bool is_player = player && (player->source_client || player->character || player->arsenal);
    *out = (qa_builtin_actor_traits){
        .classname = entity ? entity->classname : 0,
        .owner = entity ? entity->owner : (qa_actor_id){0},
        .player = is_player,
        .has_life = (player && player->character) || (entity && entity->kind == Q1_MONSTER),
        .birth_epoch = player && player->character ? player->character_state.birth_epoch
                        : entity && entity->kind == Q1_MONSTER ? entity->state.monster.birth_epoch : 0,
        .dead = player && player->character ? player->character_state.life != QA_Q1_ALIVE
                : entity && entity->kind == Q1_MONSTER && entity->state.monster.dead,
        .monster = entity && (entity->physics.flags & QA_PHYSICS_MONSTER),
        .no_target = player && player->source_client && player->source_no_target,
        .aimed_damage = entity && entity->aimed_damage,
        .grounded = entity && (entity->physics.flags & QA_PHYSICS_ONGROUND),
        .max_health = entity ? entity->max_health : player->max_health,
        .gib_health =
            entity && entity->kind == Q1_MONSTER ? entity->state.monster.species->gib_health : -40,
        .view_height = is_player ? 22
                       : entity && entity->kind == Q1_MONSTER &&
                               entity->state.monster.species->species == QA_Q1_LAVA_MAN
                           ? 48
                       : entity && entity->kind == Q1_MONSTER &&
                               entity->state.monster.addon.boss == Q1_BOSS_OLDNEW
                           ? 24
                       : entity && (entity->physics.flags & QA_PHYSICS_SWIMMING) ? 10
                                                                                 : 25,
        .invisible = player && player->power_expires[QA_Q1_INVISIBILITY] > g->time,
        .hostile_until_ns =
            player && player->hostile_until > 0 ? hostile_deadline_ns(player->hostile_until)
            : entity && entity->kind == Q1_MONSTER && entity->state.monster.hostile_until > 0
                ? hostile_deadline_ns(entity->state.monster.hostile_until)
                : 0};
    return true;
}
bool qa_q1_game_physics_read(const qa_q1_game *g, qa_actor_id actor, qa_physics_properties *out) {
    const q1_actor *entity = q1_entity_const(g, actor);
    if (!entity || !out)
        return false;
    double local;
    if (!q1_local_time(entity, &local, NULL))
        return false;
    *out = entity->physics;
    out->q1_pusher.local_seconds = local;
    out->q1_pusher.next_think_seconds = entity->next_think;
    return true;
}
bool qa_q1_game_physics_write(qa_q1_game *g, qa_actor_id actor, const qa_physics_properties *state,
                              qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || !state) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 physics actor retired");
        return false;
    }
    if (!isfinite(state->q1_pusher.local_seconds) ||
        !isfinite(state->q1_pusher.next_think_seconds)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid Q1 source pusher clock");
        return false;
    }
    entity->physics = *state;
    entity->next_think = state->q1_pusher.next_think_seconds;
    entity->physics.q1_pusher.next_think_seconds = 0;
    return true;
}
bool qa_q1_game_water_transition(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing Q1 water-transition owner");
        return false;
    }
    if (!q1_entity(g, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!q1_entity(g, actor))
        return true;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity)
        return true;
    /* The native Q1 host exposes its six material names, unlike QC's raw
     * contents interface. Unknown source values become empty here. */
    int32_t value = contents.contents <= -2 && contents.contents >= -6 ? contents.contents : -1;
    qa_q1_water_transition_result transition =
        qa_q1_water_transition(entity->physics.water_type, value);
    if (transition.splash && !q1_sound(g, actor, "misc/h2ohit1.wav", 0, 1, error))
        return false;
    entity = q1_entity(g, actor);
    if (entity) {
        entity->physics.water_type = transition.water_type;
        entity->physics.water_level = transition.water_level;
    }
    return true;
}
qa_item_id qa_q1_weapon_item(const qa_q1_game *g, qa_q1_weapon weapon) {
    return g && weapon < QA_Q1_WEAPON_COUNT ? g->weapons[weapon] : 0;
}
qa_item_id qa_q1_ammo_item(const qa_q1_game *g, qa_q1_ammo ammo) {
    return g && ammo < QA_Q1_AMMO_COUNT ? g->ammo[ammo] : 0;
}

bool qa_q1_game_authored_target(const qa_q1_game *g, qa_actor_id actor, qa_authored_target *out) {
    const q1_actor *entity = q1_entity_const(g, actor);
    if (!entity || !out)
        return false;
    *out = (qa_authored_target){.classname = entity->classname,
                                .targetname = entity->targetname,
                                .target = entity->target,
                                .killtarget = entity->killtarget,
                                .message = entity->message,
                                .delay_seconds = entity->delay,
                                .wait_seconds = entity->wait};
    return true;
}

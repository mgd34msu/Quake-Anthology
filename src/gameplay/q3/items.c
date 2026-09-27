#include "internal.h"

#define ITEM(c, n, m, s, i, a, k, t, q) {c, n, m, s, i, a, k, t, q}
#define WEAPON(c, n, m, i, t, q)                                                                   \
    ITEM(c, n, m, NULL, i, "sound/misc/w_pkup.wav", QA_Q3_ITEM_WEAPON, t, q)
#define AMMO(c, n, m, i, t, q)                                                                     \
    ITEM(c, n, m, NULL, i, "sound/misc/am_pkup.wav", QA_Q3_ITEM_AMMO, t, q)
#define HOLD(c, n, m, i, t)                                                                        \
    ITEM(c, n, m, NULL, i, "sound/items/holdable.wav", QA_Q3_ITEM_HOLDABLE, t, 60)
static const qa_q3_item items[] = {
    {0},
    ITEM("item_armor_shard", "Armor Shard", "models/powerups/armor/shard.md3",
         "models/powerups/armor/shard_sphere.md3", "icons/iconr_shard", "sound/misc/ar1_pkup.wav",
         QA_Q3_ITEM_ARMOR, 0, 5),
    ITEM("item_armor_combat", "Armor", "models/powerups/armor/armor_yel.md3", NULL,
         "icons/iconr_yellow", "sound/misc/ar2_pkup.wav", QA_Q3_ITEM_ARMOR, 0, 50),
    ITEM("item_armor_body", "Heavy Armor", "models/powerups/armor/armor_red.md3", NULL,
         "icons/iconr_red", "sound/misc/ar2_pkup.wav", QA_Q3_ITEM_ARMOR, 0, 100),
    ITEM("item_health_small", "5 Health", "models/powerups/health/small_cross.md3",
         "models/powerups/health/small_sphere.md3", "icons/iconh_green", "sound/items/s_health.wav",
         QA_Q3_ITEM_HEALTH, 0, 5),
    ITEM("item_health", "25 Health", "models/powerups/health/medium_cross.md3",
         "models/powerups/health/medium_sphere.md3", "icons/iconh_yellow",
         "sound/items/n_health.wav", QA_Q3_ITEM_HEALTH, 0, 25),
    ITEM("item_health_large", "50 Health", "models/powerups/health/large_cross.md3",
         "models/powerups/health/large_sphere.md3", "icons/iconh_red", "sound/items/l_health.wav",
         QA_Q3_ITEM_HEALTH, 0, 50),
    ITEM("item_health_mega", "Mega Health", "models/powerups/health/mega_cross.md3",
         "models/powerups/health/mega_sphere.md3", "icons/iconh_mega", "sound/items/m_health.wav",
         QA_Q3_ITEM_HEALTH, 0, 100),
    WEAPON("weapon_gauntlet", "Gauntlet", "models/weapons2/gauntlet/gauntlet.md3",
           "icons/iconw_gauntlet", 1, 0),
    WEAPON("weapon_shotgun", "Shotgun", "models/weapons2/shotgun/shotgun.md3",
           "icons/iconw_shotgun", 3, 10),
    WEAPON("weapon_machinegun", "Machinegun", "models/weapons2/machinegun/machinegun.md3",
           "icons/iconw_machinegun", 2, 40),
    WEAPON("weapon_grenadelauncher", "Grenade Launcher", "models/weapons2/grenadel/grenadel.md3",
           "icons/iconw_grenade", 4, 10),
    WEAPON("weapon_rocketlauncher", "Rocket Launcher", "models/weapons2/rocketl/rocketl.md3",
           "icons/iconw_rocket", 5, 10),
    WEAPON("weapon_lightning", "Lightning Gun", "models/weapons2/lightning/lightning.md3",
           "icons/iconw_lightning", 6, 100),
    WEAPON("weapon_railgun", "Railgun", "models/weapons2/railgun/railgun.md3",
           "icons/iconw_railgun", 7, 10),
    WEAPON("weapon_plasmagun", "Plasma Gun", "models/weapons2/plasma/plasma.md3",
           "icons/iconw_plasma", 8, 50),
    WEAPON("weapon_bfg", "BFG10K", "models/weapons2/bfg/bfg.md3", "icons/iconw_bfg", 9, 20),
    WEAPON("weapon_grapplinghook", "Grappling Hook", "models/weapons2/grapple/grapple.md3",
           "icons/iconw_grapple", 10, 0),
    AMMO("ammo_shells", "Shells", "models/powerups/ammo/shotgunam.md3", "icons/icona_shotgun", 3,
         10),
    AMMO("ammo_bullets", "Bullets", "models/powerups/ammo/machinegunam.md3",
         "icons/icona_machinegun", 2, 50),
    AMMO("ammo_grenades", "Grenades", "models/powerups/ammo/grenadeam.md3", "icons/icona_grenade",
         4, 5),
    AMMO("ammo_cells", "Cells", "models/powerups/ammo/plasmaam.md3", "icons/icona_plasma", 8, 30),
    AMMO("ammo_lightning", "Lightning", "models/powerups/ammo/lightningam.md3",
         "icons/icona_lightning", 6, 60),
    AMMO("ammo_rockets", "Rockets", "models/powerups/ammo/rocketam.md3", "icons/icona_rocket", 5,
         5),
    AMMO("ammo_slugs", "Slugs", "models/powerups/ammo/railgunam.md3", "icons/icona_railgun", 7, 10),
    AMMO("ammo_bfg", "Bfg Ammo", "models/powerups/ammo/bfgam.md3", "icons/icona_bfg", 9, 15),
    HOLD("holdable_teleporter", "Personal Teleporter", "models/powerups/holdable/teleporter.md3",
         "icons/teleporter", 1),
    ITEM("holdable_medkit", "Medkit", "models/powerups/holdable/medkit.md3",
         "models/powerups/holdable/medkit_sphere.md3", "icons/medkit", "sound/items/holdable.wav",
         QA_Q3_ITEM_HOLDABLE, 2, 60),
    ITEM("item_quad", "Quad Damage", "models/powerups/instant/quad.md3",
         "models/powerups/instant/quad_ring.md3", "icons/quad", "sound/items/quaddamage.wav",
         QA_Q3_ITEM_POWERUP, 1, 30),
    ITEM("item_enviro", "Battle Suit", "models/powerups/instant/enviro.md3",
         "models/powerups/instant/enviro_ring.md3", "icons/envirosuit", "sound/items/protect.wav",
         QA_Q3_ITEM_POWERUP, 2, 30),
    ITEM("item_haste", "Speed", "models/powerups/instant/haste.md3",
         "models/powerups/instant/haste_ring.md3", "icons/haste", "sound/items/haste.wav",
         QA_Q3_ITEM_POWERUP, 3, 30),
    ITEM("item_invis", "Invisibility", "models/powerups/instant/invis.md3",
         "models/powerups/instant/invis_ring.md3", "icons/invis", "sound/items/invisibility.wav",
         QA_Q3_ITEM_POWERUP, 4, 30),
    ITEM("item_regen", "Regeneration", "models/powerups/instant/regen.md3",
         "models/powerups/instant/regen_ring.md3", "icons/regen", "sound/items/regeneration.wav",
         QA_Q3_ITEM_POWERUP, 5, 30),
    ITEM("item_flight", "Flight", "models/powerups/instant/flight.md3",
         "models/powerups/instant/flight_ring.md3", "icons/flight", "sound/items/flight.wav",
         QA_Q3_ITEM_POWERUP, 6, 60),
    ITEM("team_CTF_redflag", "Red Flag", "models/flags/r_flag.md3", NULL, "icons/iconf_red1", NULL,
         QA_Q3_ITEM_TEAM, 7, 0),
    ITEM("team_CTF_blueflag", "Blue Flag", "models/flags/b_flag.md3", NULL, "icons/iconf_blu1",
         NULL, QA_Q3_ITEM_TEAM, 8, 0),
    HOLD("holdable_kamikaze", "Kamikaze", "models/powerups/kamikazi.md3", "icons/kamikaze", 3),
    HOLD("holdable_portal", "Portal", "models/powerups/holdable/porter.md3", "icons/portal", 4),
    HOLD("holdable_invulnerability", "Invulnerability",
         "models/powerups/holdable/invulnerability.md3", "icons/invulnerability", 5),
    AMMO("ammo_nails", "Nails", "models/powerups/ammo/nailgunam.md3", "icons/icona_nailgun", 11,
         20),
    AMMO("ammo_mines", "Proximity Mines", "models/powerups/ammo/proxmineam.md3",
         "icons/icona_proxlauncher", 12, 10),
    AMMO("ammo_belt", "Chaingun Belt", "models/powerups/ammo/chaingunam.md3",
         "icons/icona_chaingun", 13, 100),
    ITEM("item_scout", "Scout", "models/powerups/scout.md3", NULL, "icons/scout",
         "sound/items/scout.wav", QA_Q3_ITEM_PERSISTENT, 10, 30),
    ITEM("item_guard", "Guard", "models/powerups/guard.md3", NULL, "icons/guard",
         "sound/items/guard.wav", QA_Q3_ITEM_PERSISTENT, 11, 30),
    ITEM("item_doubler", "Doubler", "models/powerups/doubler.md3", NULL, "icons/doubler",
         "sound/items/doubler.wav", QA_Q3_ITEM_PERSISTENT, 12, 30),
    ITEM("item_ammoregen", "Ammo Regen", "models/powerups/ammo.md3", NULL, "icons/ammo_regen",
         "sound/items/ammoregen.wav", QA_Q3_ITEM_PERSISTENT, 13, 30),
    ITEM("team_CTF_neutralflag", "Neutral Flag", "models/flags/n_flag.md3", NULL,
         "icons/iconf_neutral1", NULL, QA_Q3_ITEM_TEAM, 9, 0),
    ITEM("item_redcube", "Red Cube", "models/powerups/orb/r_orb.md3", NULL, "icons/iconh_rorb",
         "sound/misc/am_pkup.wav", QA_Q3_ITEM_TEAM, 0, 0),
    ITEM("item_bluecube", "Blue Cube", "models/powerups/orb/b_orb.md3", NULL, "icons/iconh_borb",
         "sound/misc/am_pkup.wav", QA_Q3_ITEM_TEAM, 0, 0),
    WEAPON("weapon_nailgun", "Nailgun", "models/weapons/nailgun/nailgun.md3", "icons/iconw_nailgun",
           11, 10),
    WEAPON("weapon_prox_launcher", "Prox Launcher", "models/weapons/proxmine/proxmine.md3",
           "icons/iconw_proxlauncher", 12, 5),
    WEAPON("weapon_chaingun", "Chaingun", "models/weapons/vulcan/vulcan.md3",
           "icons/iconw_chaingun", 13, 80)};
#undef HOLD
#undef AMMO
#undef WEAPON
#undef ITEM

const qa_q3_item *qa_q3_items(qa_q3_product product, size_t *count) {
    if (count)
        *count = product == QA_Q3_ARENA ? 36 : sizeof(items) / sizeof(*items);
    return items;
}
const qa_q3_item *qa_q3_find_item(qa_q3_product product, const char *classname, uint32_t *index) {
    if (!classname)
        return NULL;
    size_t count;
    qa_q3_items(product, &count);
    for (size_t i = 1; i < count; ++i)
        if (!strcmp(items[i].classname, classname)) {
            if (index)
                *index = (uint32_t)i;
            return &items[i];
        }
    return NULL;
}
bool q3_item_register(qa_q3_game *game, qa_error *error) {
    size_t count;
    qa_q3_items(game->options.product, &count);
    for (size_t i = 1; i < count; ++i) {
        if (items[i].kind == QA_Q3_ITEM_WEAPON)
            game->item_ids[i] = game->weapon_items[items[i].tag];
        else if (items[i].kind == QA_Q3_ITEM_AMMO)
            game->item_ids[i] = game->ammo_items[items[i].tag];
        else {
            char name[96];
            snprintf(name, sizeof(name), "q3:item/%s", items[i].classname);
            if (!qa_builtin_resource(&game->options.services, name, &game->item_ids[i], error))
                return false;
        }
    }
    return true;
}
qa_item_id qa_q3_item_identity(const qa_q3_game *game, uint32_t index) {
    if (!game)
        return 0;
    size_t count;
    qa_q3_items(game->options.product, &count);
    return index < count ? game->item_ids[index] : 0;
}
bool qa_q3_item_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_item_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM || !out)
        return false;
    *out = entry->state.item;
    return true;
}
bool qa_q3_item_availability(qa_q3_game *game, qa_actor_id actor, bool available, int32_t respawn,
                             int32_t expire, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return q3_fail(error, "missing Q3 item lifecycle");
    entry->state.item.hidden = !available;
    entry->state.item.respawn_at = respawn;
    entry->state.item.expire_at = expire;
    if (!available)
        return qa_world_set_collision(game->options.services.world, actor, NULL, error) &&
               qa_world_unlink(game->options.services.world, actor, error);
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q3_CONTENTS_TRIGGER,
                                    .role = QA_COLLISION_TRIGGER};
    return qa_world_set_collision(game->options.services.world, actor, &collision, error) &&
           qa_world_link(game->options.services.world, actor, NULL, error);
}
static float respawn_seconds(qa_q3_game *game, const qa_q3_item *item) {
    switch (item->kind) {
    case QA_Q3_ITEM_WEAPON:
        return game->options.rules.game_type == 3 ? game->options.rules.team_weapon_respawn_seconds
                                                  : game->options.rules.weapon_respawn_seconds;
    case QA_Q3_ITEM_AMMO:
        return 40;
    case QA_Q3_ITEM_ARMOR:
        return 25;
    case QA_Q3_ITEM_HEALTH:
        return 35;
    case QA_Q3_ITEM_HOLDABLE:
        return 60;
    case QA_Q3_ITEM_POWERUP:
        return 120;
    default:
        return -1;
    }
}
static bool denied_powerup(qa_q3_game *game, qa_actor_id pickup, qa_actor_id recipient,
                           qa_error *error) {
    q3_actor *item = q3_actor_get(game, pickup);
    if (!item || item->kind != Q3_ACTOR_ITEM)
        return true;
    qa_vec3 origin = item->state.item.trajectory.base;
    qa_combat_state receiver;
    if (!qa_combat_read(game->options.services.combat, recipient, &receiver, error))
        return false;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_actor *candidate = &game->actors[i];
        if (candidate->kind != Q3_ACTOR_PLAYER || qa_actor_id_equal(candidate->actor, recipient) ||
            !q3_actor_get(game, candidate->actor))
            continue;
        qa_actor_id actor = candidate->actor;
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if (combat.health <= 0 ||
            (game->options.rules.game_type >= 3 && receiver.team == combat.team))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        qa_vec3 delta = qa_vec_sub(origin, body.origin), forward;
        if (qa_vec_length(delta) > 192)
            continue;
        qa_builtin_angle_vectors(candidate->state.player.view_angles, &forward, NULL, NULL);
        if (qa_vec_dot(qa_vec_normalize(delta), forward) < 0.4f)
            continue;
        qa_trace_result trace;
        if (!q3_trace(game, body.origin, origin, (qa_actor_id){0}, 1, &trace, error))
            return false;
        if (trace.fraction != 1)
            continue;
        candidate = q3_actor_get(game, actor);
        if (!candidate)
            continue;
        candidate->state.player.player_events ^= 1;
        candidate->state.player.denied_rewards =
            q3_add_time(candidate->state.player.denied_rewards, 1);
    }
    return true;
}
bool qa_q3_spawn_item(qa_q3_game *game, const qa_q3_item_spawn *input, qa_actor_id *out,
                      qa_error *error) {
    if (!game)
        return q3_fail(error, "missing Q3 item provider");
    size_t count;
    qa_q3_items(game->options.product, &count);
    if (!input || !out || !input->item_index || input->item_index >= count ||
        !qa_vec_finite(input->origin) || !qa_vec_finite(input->velocity) ||
        !isfinite(input->wait_seconds) || !isfinite(input->random_seconds) ||
        fabsf(input->wait_seconds) + fabsf(input->random_seconds) > 2147483)
        return q3_fail(error, "invalid Q3 item spawn");
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q3_CONTENTS_TRIGGER,
                                    .role = QA_COLLISION_TRIGGER};
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .body = {.origin = input->origin,
                                       .velocity = input->velocity,
                                       .bounds = {qa_v3(-15, -15, -15), qa_v3(15, 15, 15)}},
                              .collision = &collision};
    if (!input->dropped && !input->suspended) {
        qa_trace_query query = {.start = input->origin,
                                .end = qa_vec_add(input->origin, qa_v3(0, 0, -4096)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = spawn.body.bounds},
                                .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
        query.policy.contents_mask = 1;
        qa_trace_result trace;
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return false;
        if (trace.start_solid)
            return q3_fail(error, "Q3 item spawned inside solid geometry");
        spawn.body.origin = trace.end;
        spawn.body.ground = trace.actor;
    }
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &actor, error))
        return false;
    q3_actor *entry = &game->actors[actor.slot];
    *entry = (q3_actor){
        .actor = actor,
        .kind = Q3_ACTOR_ITEM,
        .state.item = {.spawn = *input,
                       .bounce = 0.5f,
                       .on_ground = !input->dropped,
                       .ground_entity_number = input->dropped ? -1 : 1022,
                       .trajectory = {.type = input->dropped ? QA_TRAJECTORY_GRAVITY
                                                             : QA_TRAJECTORY_STATIONARY,
                                      .base = spawn.body.origin,
                                      .delta = input->velocity,
                                      .time_ms = game->now_ms},
                       .expire_at = input->dropped ? q3_add_time(game->now_ms, 30000) : 0}};
    if (items[input->item_index].kind == QA_Q3_ITEM_POWERUP && !input->dropped) {
        entry->state.item.hidden = true;
        entry->state.item.respawn_at =
            q3_add_time(game->now_ms, (int32_t)((45 + q3_random(game) * 15) * 1000));
        if (!qa_world_set_collision(game->options.services.world, actor, NULL, error))
            return false;
    }
    if (!qa_world_link(game->options.services.world, actor, NULL, error))
        return false;
    *out = actor;
    return true;
}
typedef struct pickup_context {
    qa_q3_game *game;
    qa_actor_id item;
    float respawn;
    bool accepted;
} pickup_context;
static bool pickup_eligible(void *context, const qa_pickup_offer *offer, bool *eligible,
                            qa_error *error) {
    pickup_context *call = context;
    q3_actor *item = q3_actor_get(call->game, offer->pickup);
    qa_combat_state combat;
    *eligible = item && item->kind == Q3_ACTOR_ITEM && !item->state.item.hidden;
    if (!*eligible)
        return true;
    qa_error local = {0};
    if (!qa_combat_read(call->game->options.services.combat, offer->recipient, &combat, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) {
            *eligible = false;
            return true;
        }
        if (error)
            *error = local;
        return false;
    }
    *eligible = combat.health > 0;
    return true;
}
static bool pickup_original(void *context, const qa_pickup_offer *offer, bool *accepted,
                            qa_error *error) {
    pickup_context *call = context;
    qa_q3_game *game = call->game;
    q3_actor *entity = q3_actor_get(game, offer->pickup),
             *recipient = q3_actor_get(game, offer->recipient);
    *accepted = false;
    if (!entity || entity->kind != Q3_ACTOR_ITEM || !recipient ||
        recipient->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state *player = &recipient->state.player;
    const qa_q3_item *item = &items[entity->state.item.spawn.item_index];
    qa_q3_item_spawn spawn = entity->state.item.spawn;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, offer->recipient, &combat, error))
        return false;
    float maximum = (float)player->max_health;
    int32_t quantity = spawn.count ? spawn.count : item->quantity;
    double given;
    switch (item->kind) {
    case QA_Q3_ITEM_WEAPON: {
        if (!(player->selections & QA_Q3_ARSENAL))
            return true;
        int32_t ammo;
        if (!q3_ammo_read(game, offer->recipient, (qa_q3_weapon)item->tag, &ammo, error))
            return false;
        if (spawn.count < 0)
            quantity = 0;
        else if (!spawn.dropped && game->options.rules.game_type != 3)
            quantity = ammo < quantity ? quantity - ammo : 1;
        if (!qa_inventory_give(game->options.services.inventory, offer->recipient,
                               game->weapon_items[item->tag], 1, &given, error))
            return false;
        if (!q3_add_ammo(game, offer->recipient, (qa_q3_weapon)item->tag, quantity, error))
            return false;
        break;
    }
    case QA_Q3_ITEM_AMMO: {
        if (!(player->selections & QA_Q3_ARSENAL))
            return true;
        int32_t ammo;
        if (!q3_ammo_read(game, offer->recipient, (qa_q3_weapon)item->tag, &ammo, error))
            return false;
        if (ammo >= 200)
            return true;
        if (!q3_add_ammo(game, offer->recipient, (qa_q3_weapon)item->tag, quantity, error))
            return false;
        break;
    }
    case QA_Q3_ITEM_HEALTH:
        if (player->persistent != QA_Q3_P_GUARD && (item->quantity == 5 || item->quantity == 100))
            maximum *= 2;
        if (combat.health >= maximum)
            return true;
        if (!qa_combat_set_health(game->options.services.combat, offer->recipient,
                                  fminf(maximum, combat.health + (float)quantity), error))
            return false;
        break;
    case QA_Q3_ITEM_ARMOR: {
        if (player->persistent == QA_Q3_P_SCOUT)
            return true;
        if (player->persistent != QA_Q3_P_GUARD)
            maximum *= 2;
        if (combat.armor.regular.points >= maximum)
            return true;
        qa_regular_armor armor = {
            .kind = QA_ARMOR_Q3,
            .protection.q3_protection = 0.66f,
            .points = fminf(maximum, combat.armor.regular.points + (float)item->quantity),
            .item = game->item_ids[spawn.item_index]};
        if (!qa_combat_set_regular_armor(game->options.services.combat, offer->recipient, &armor,
                                         error))
            return false;
        break;
    }
    case QA_Q3_ITEM_HOLDABLE:
        if (player->holdable)
            return true;
        player->holdable = (qa_q3_holdable)item->tag;
        if (player->holdable == QA_Q3_H_KAMIKAZE)
            player->flags |= 0x200u;
        break;
    case QA_Q3_ITEM_POWERUP: {
        int32_t until = player->powerups[item->tag];
        if (!until)
            until = game->now_ms - game->now_ms % 1000;
        player->powerups[item->tag] = q3_add_time(until, (int32_t)((uint32_t)quantity * 1000u));
        if (!denied_powerup(game, offer->pickup, offer->recipient, error))
            return false;
        break;
    }
    case QA_Q3_ITEM_PERSISTENT: {
        int32_t team =
            game->options.hooks.source_team
                ? game->options.hooks.source_team(game->options.hooks.context, offer->recipient)
                : 0;
        if (player->persistent || game->options.product != QA_Q3_TEAM_ARENA ||
            ((spawn.team_restriction & 2) && team != 1) ||
            ((spawn.team_restriction & 4) && team != 2))
            return true;
        player->persistent = (qa_q3_powerup)item->tag;
        player->persistent_item = offer->pickup;
        player->max_health = player->handicap;
        if (player->persistent == QA_Q3_P_GUARD) {
            player->max_health *= 2;
            if (!qa_combat_set_health(game->options.services.combat, offer->recipient,
                                      (float)player->max_health, error) ||
                !qa_combat_set_regular_points(game->options.services.combat, offer->recipient,
                                              (float)player->max_health, NULL, error))
                return false;
        } else if (player->persistent == QA_Q3_P_SCOUT &&
                   !qa_combat_set_regular_points(game->options.services.combat, offer->recipient, 0,
                                                 NULL, error))
            return false;
        memset(player->ammo_time_ms, 0, sizeof(player->ammo_time_ms));
        break;
    }
    case QA_Q3_ITEM_TEAM:
        if (!game->options.hooks.objective_pickup)
            return q3_fail(error, "Q3 objective has no selected match owner");
        return game->options.hooks.objective_pickup(game->options.hooks.context, offer->pickup,
                                                    offer->recipient, spawn.item_index, accepted,
                                                    error);
    default:
        return q3_fail(error, "invalid Q3 pickup kind");
    }
    *accepted = true;
    return true;
}
static bool pickup_complete(void *context, const qa_pickup_offer *offer, bool accepted,
                            qa_error *error) {
    pickup_context *call = context;
    qa_q3_game *game = call->game;
    call->accepted = accepted;
    q3_actor *entry = q3_actor_get(game, offer->pickup);
    if (!accepted || !entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    qa_q3_item_spawn spawn = entry->state.item.spawn;
    if (!q3_player_event(game, offer->recipient, 19, (int32_t)spawn.item_index, error))
        return false;
    entry = q3_actor_get(game, offer->pickup);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    if (items[spawn.item_index].kind == QA_Q3_ITEM_POWERUP ||
        items[spawn.item_index].kind == QA_Q3_ITEM_TEAM) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, offer->pickup, &body, error))
            return false;
        if (!q3_event(game, offer->pickup, offer->recipient, QA_BUILTIN_ITEM, 20,
                      (int32_t)spawn.item_index, body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0),
                      error))
            return false;
    }
    if (!q3_actor_get(game, offer->pickup))
        return true;
    if (game->options.services.use_targets && spawn.target &&
        !game->options.services.use_targets(game->options.services.context, offer->pickup,
                                            offer->recipient, spawn.target, 0, 0, error))
        return false;
    entry = q3_actor_get(game, offer->pickup);
    if (!entry)
        return true;
    if (spawn.dropped)
        return qa_session_release(game->options.services.session, offer->pickup, error);
    float seconds = spawn.wait_seconds ? spawn.wait_seconds : call->respawn;
    if (spawn.wait_seconds != -1 && spawn.random_seconds)
        seconds = fmaxf(1, seconds + q3_crandom(game) * spawn.random_seconds);
    int32_t respawn = seconds > 0 ? q3_add_time(game->now_ms, (int32_t)(seconds * 1000)) : 0;
    return qa_q3_item_availability(game, offer->pickup, false, respawn, entry->state.item.expire_at,
                                   error);
}
bool qa_q3_touch_item(qa_q3_game *game, qa_actor_id item_actor, qa_actor_id recipient,
                      bool *accepted, qa_error *error) {
    if (accepted)
        *accepted = false;
    q3_actor *entry = q3_actor_get(game, item_actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM || entry->state.item.hidden)
        return true;
    uint32_t index = entry->state.item.spawn.item_index;
    const qa_q3_item *item = &items[index];
    qa_pickup_resource resource = {0};
    if (item->kind == QA_Q3_ITEM_WEAPON || item->kind == QA_Q3_ITEM_AMMO)
        resource = (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY, .item = game->item_ids[index]};
    else if (item->kind == QA_Q3_ITEM_ARMOR)
        resource =
            (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION, .channel = QA_PROTECTION_REGULAR};
    qa_pickup_offer offer = {.recipient = recipient,
                             .pickup = item_actor,
                             .source = game->options.owner,
                             .item = game->item_ids[index],
                             .default_resource = resource,
                             .dropped = entry->state.item.spawn.dropped,
                             .override_count = entry->state.item.spawn.count != 0,
                             .count = entry->state.item.spawn.count,
                             .time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                             .grant = item->kind == QA_Q3_ITEM_TEAM ? QA_PICKUP_MAP_COUPLED
                                      : resource.kind               ? QA_PICKUP_RESOURCE_GRANT
                                                                    : QA_PICKUP_SOURCE_EFFECT};
    pickup_context context = {game, item_actor, respawn_seconds(game, item), false};
    qa_pickup_continuation continuation = {.context = &context,
                                           .eligible = pickup_eligible,
                                           .original = pickup_original,
                                           .complete = pickup_complete};
    qa_pickup_outcome outcome;
    if (!qa_pickups_touch(game->options.services.pickups, &offer, &continuation, &outcome, error))
        return false;
    if (accepted)
        *accepted = context.accepted;
    return true;
}
bool q3_item_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    q3_item_state *item = &entry->state.item;
    if (item->expire_at && game->now_ms >= item->expire_at) {
        uint32_t index = item->spawn.item_index;
        if (items[index].kind == QA_Q3_ITEM_TEAM && game->options.hooks.objective_expired)
            return game->options.hooks.objective_expired(game->options.hooks.context, actor, index,
                                                         error);
        return qa_session_release(game->options.services.session, actor, error);
    }
    if (item->hidden) {
        if (!item->respawn_at || game->now_ms < item->respawn_at)
            return true;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        return qa_q3_item_availability(game, actor, true, 0, item->expire_at, error) &&
               q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ITEM, 40, 0, body.origin,
                        qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
    }
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (item->ground_entity_number == -1 && item->trajectory.type != QA_TRAJECTORY_GRAVITY) {
        item->trajectory.type = QA_TRAJECTORY_GRAVITY;
        item->trajectory.time_ms = game->now_ms;
    }
    if (item->trajectory.type == QA_TRAJECTORY_STATIONARY)
        return true;
    qa_vec3 destination;
    if (!qa_trajectory_position(&item->trajectory, game->now_ms, 800, &destination, error))
        return false;
    qa_trace_query query = {.start = body.origin,
                            .end = destination,
                            .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3),
                            .pass_actor = actor};
    query.policy.contents_mask = 0x10001u;
    qa_trace_result trace;
    if (!qa_world_trace(game->options.services.world, &query, &trace, error))
        return false;
    body.origin = trace.end;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error) ||
        !qa_world_link(game->options.services.world, actor, NULL, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    item = &entry->state.item;
    if (trace.fraction == 1 && !trace.start_solid)
        return true;
    qa_point_query point = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error))
        return false;
    if (contents.contents & INT32_MIN) {
        uint32_t index = item->spawn.item_index;
        if (items[index].kind == QA_Q3_ITEM_TEAM && game->options.hooks.objective_expired)
            return game->options.hooks.objective_expired(game->options.hooks.context, actor, index,
                                                         error);
        return qa_session_release(game->options.services.session, actor, error);
    }
    qa_vec3 velocity;
    int32_t hit_time = qa_physics_q3_hit_time(game->previous_ms, game->now_ms,
                                              trace.start_solid ? 0 : trace.fraction);
    if (!qa_trajectory_velocity(&item->trajectory, hit_time, 800, &velocity, error))
        return false;
    qa_vec3 normal = trace.contact_plane.normal;
    velocity = qa_vec_scale(
        qa_vec_sub(velocity, qa_vec_scale(normal, 2 * qa_vec_dot(velocity, normal))), item->bounce);
    item->trajectory.delta = velocity;
    if (normal.z > 0 && velocity.z < 40) {
        body.origin = qa_physics_q3_snap(qa_vec_add(trace.end, qa_v3(0, 0, 1)));
        body.velocity = qa_v3(0, 0, 0);
        body.ground = trace.actor;
        item->trajectory = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
        item->on_ground = true;
        item->ground_entity_number =
            trace.hit == QA_TRACE_HIT_WORLD ? 1022 : q3_entity_number(game, trace.actor);
    } else {
        body.origin = qa_vec_add(body.origin, normal);
        body.velocity = velocity;
        item->trajectory.base = body.origin;
        item->trajectory.time_ms = game->now_ms;
    }
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
bool qa_q3_touch(qa_q3_game *game, const qa_touch_contact *contact, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, contact->self);
    if (!entry)
        return true;
    if (entry->kind == Q3_ACTOR_ITEM)
        return qa_q3_touch_item(game, contact->self, contact->other, NULL, error);
    if (entry->kind == Q3_ACTOR_PROX_TRIGGER)
        return q3_missile_trigger(game, entry->state.trigger.parent, contact->other, error);
    if (entry->kind == Q3_ACTOR_PORTAL && entry->state.portal.source &&
        game->now_ms >= entry->state.portal.activate_at) {
        if (!q3_is_player(game, contact->other))
            return true;
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, contact->other, &combat, error))
            return false;
        if (combat.health <= 0)
            return true;
        if (game->options.hooks.objective_drop &&
            !game->options.hooks.objective_drop(game->options.hooks.context, contact->other, error))
            return false;
        entry = q3_actor_get(game, contact->self);
        if (!entry || entry->kind != Q3_ACTOR_PORTAL ||
            !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
            return true;
        q3_actor *destination = q3_actor_get(game, entry->state.portal.destination);
        if (!destination || destination->kind != Q3_ACTOR_PORTAL) {
            qa_vec3 fallback = entry->state.portal.fallback;
            if (qa_vec_length(fallback) > 0 &&
                !qa_q3_teleport(game, contact->other, fallback, entry->state.portal.angles, error))
                return false;
            return q3_damage(game, contact->other, contact->other, contact->other, QA_Q3_W_NONE, 18,
                             8, 100000, qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error);
        }
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, destination->actor, &body, error))
            return false;
        return qa_q3_teleport(game, contact->other, body.origin, destination->state.portal.angles,
                              error);
    }
    return true;
}

#include "internal.h"
#include "qa/game_type.h"
#include "map/internal.h"
#include "source_objectives.h"
#include "qa/game_q3_save.h"

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
bool qa_q3_model_names_bind(qa_strings *strings, qa_q3_product product,
    qa_q3_model_names *out, qa_error *error)
{
    qa_q3_model_names names = {.product = product};
    size_t count;
    const qa_q3_item *table = qa_q3_items(product, &count);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].model && !qa_strings_intern_cstr(strings, table[i].model,
            &names.items[i][0], error)) return false;
        if (table[i].secondary_model && !qa_strings_intern_cstr(strings, table[i].secondary_model,
            &names.items[i][1], error)) return false;
        if (table[i].kind == QA_Q3_ITEM_WEAPON) {
            qa_q3_weapon weapon = (qa_q3_weapon)table[i].tag;
            const char *identity = qa_q3_weapon_identity_name(weapon);
            char key[64];
            snprintf(key, sizeof(key), "q3:weapon/%s", identity);
            if (!qa_strings_intern_cstr(strings, key, &names.weapon_items[weapon], error)) return false;
            names.weapons[weapon] = names.items[i][0];
        }
    }
    if (!qa_strings_intern_cstr(strings, "models/powerups/teleporter/tele_exit.md3", &names.portals[0], error) ||
        !qa_strings_intern_cstr(strings, "models/powerups/teleporter/tele_enter.md3", &names.portals[1], error)) return false;
    static const struct { qa_q3_weapon weapon; const char *path; } missiles[] = {
        {QA_Q3_W_GRAPPLE, "models/ammo/rocket/rocket.md3"}, {QA_Q3_W_ROCKET, "models/ammo/rocket/rocket.md3"},
        {QA_Q3_W_GRENADE, "models/ammo/grenade1.md3"}, {QA_Q3_W_PROX, "models/weaphits/proxmine.md3"},
        {QA_Q3_W_NAIL, "models/weaphits/nail.md3"}, {QA_Q3_W_BFG, "models/weaphits/bfg.md3"}};
    for (size_t i = 0; i < sizeof(missiles) / sizeof(*missiles); ++i)
        if (!qa_strings_intern_cstr(strings, missiles[i].path, &names.missiles[missiles[i].weapon], error)) return false;
    *out = names;
    return true;
}
qa_string_id qa_q3_model_names_weapon(const qa_q3_model_names *names, qa_q3_weapon weapon)
{
    return names->weapons[weapon];
}
const qa_q3_model_names *qa_q3_game_model_names(const qa_q3_game *game)
{ return &game->model_names; }
bool q3_item_register(qa_q3_game *game, qa_error *error) {
    if (!qa_q3_model_names_bind(qa_session_strings(game->options.services.session),
        game->options.product, &game->model_names, error)) return false;
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
static bool item_availability(qa_q3_game *game, qa_actor_id actor, bool available, int32_t respawn,
                               int32_t expire, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return q3_fail(error, "missing Q3 item lifecycle");
    uint32_t source_slot;
    if (!qa_q3_source_actor_slot(game, actor, &source_slot, error)) return false;
    if (available) game->source_entities[source_slot].server_flags &= ~1u;
    else game->source_entities[source_slot].server_flags |= 1u;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 item visibility lacks its actual source row");
    if (available) source->flags &= ~0x80;
    else source->flags |= 0x80;
    entry->state.item.hidden = !available;
    if (!available) q3_postgame_native_think_assigned(game, actor);
    else if (source->arena_think) source->arena_nextthink = 0;
    entry->state.item.respawn_at = respawn;
    entry->state.item.expire_at = expire;
    qa_actor_collision collision = {.family = QA_GAME_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_contents_decode(Q3_CONTENTS_TRIGGER, QA_GAME_Q3),
                                    .role = QA_COLLISION_TRIGGER};
    if (!qa_world_set_collision(game->options.services.world, actor,
                                available ? &collision : NULL, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    return qa_q3_wire_link(game, actor, NULL, error);
}
bool qa_q3_item_availability(qa_q3_game *game, qa_actor_id actor, bool available, int32_t respawn,
                             int32_t expire, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 item lifecycle boundary");
    ++game->observation_depth;
    bool okay = item_availability(game, actor, available, respawn, expire, error);
    --game->observation_depth;
    return okay;
}
static qa_actor_reference item_ground_actor(const qa_q3_game *game, const qa_trace_result *trace) {
    if (trace->hit == QA_TRACE_HIT_WORLD)
        return qa_actor_reference_source(game->options.owner, QA_Q3_SOURCE_WORLD);
    if (trace->hit == QA_TRACE_HIT_ACTOR) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(game->options.services.session), trace->actor);
        if (record && record->owner == game->options.owner && record->has_source)
            return qa_actor_reference_source(record->owner, record->source_slot);
        return qa_actor_reference_lifetime(trace->actor);
    }
    return (qa_actor_reference){0};
}

static int32_t item_ground_number(const qa_q3_game *game, const qa_trace_result *trace) {
    if (trace->hit == QA_TRACE_HIT_WORLD)
        return 1022;
    if (trace->hit != QA_TRACE_HIT_ACTOR)
        return 1023;
    int32_t number = q3_entity_number(game, trace->actor);
    return number >= 0 && number < 1022 ? number : 1023;
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
    if (!q3_actor_get(game, pickup) || !q3_actor_get(game, recipient))
        return true;
    for (uint32_t i = 0; i < game->options.max_clients; ++i) {
        q3_actor *candidate = &game->client_actors[i];
        if (game->clients[i].rule.connected == QA_Q3_CLIENT_DISCONNECTED ||
            candidate->kind != Q3_ACTOR_PLAYER || qa_actor_id_equal(candidate->actor, recipient) ||
            !q3_actor_get(game, candidate->actor))
            continue;
        qa_actor_id actor = candidate->actor;
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if (!q3_actor_get(game, pickup) || !q3_actor_get(game, recipient))
            return true;
        candidate = q3_actor_get(game, actor);
        if (!candidate || candidate->kind != Q3_ACTOR_PLAYER)
            continue;
        uint32_t recipient_slot, candidate_slot;
        int32_t receiver_team = qa_q3_native_client_slot(game, recipient, &recipient_slot, NULL)
            ? game->clients[recipient_slot].rule.session.team : (int32_t)receiver.team;
        int32_t candidate_team = qa_q3_native_client_slot(game, actor, &candidate_slot, NULL)
            ? game->clients[candidate_slot].rule.session.team : (int32_t)combat.team;
        if (combat.health <= 0 ||
            (qa_game_type_has_allies(game->options.rules.game_type) && receiver_team == candidate_team))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        if (!q3_actor_get(game, pickup) || !q3_actor_get(game, recipient))
            return true;
        candidate = q3_actor_get(game, actor);
        if (!candidate || candidate->kind != Q3_ACTOR_PLAYER)
            continue;
        qa_vec3 delta = qa_vec_sub(origin, body.origin), forward;
        if (qa_vec_length(delta) > 192)
            continue;
        qa_builtin_player_control control;
        if (!q3_player_control(game, actor, &control, error)) return false;
        q3_source_angle_vectors(*control.view_angles, &forward, NULL, NULL);
        if (qa_vec_dot(qa_vec_normalize(delta), forward) < 0.4f)
            continue;
        qa_trace_result trace;
        if (!q3_trace(game, body.origin, origin, (qa_actor_id){0}, 1, &trace, error))
            return false;
        if (!q3_actor_get(game, pickup) || !q3_actor_get(game, recipient))
            return true;
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
static bool item_spawn_valid(qa_q3_game *game, const qa_q3_item_spawn *input) {
    size_t count;
    qa_q3_items(game->options.product, &count);
    return input && input->item_index && input->item_index < count &&
           qa_vec_finite(input->origin) && qa_vec_finite(input->velocity) &&
           isfinite(input->wait_seconds) && isfinite(input->random_seconds) &&
           fabsf(input->wait_seconds) + fabsf(input->random_seconds) <= 2147483;
}
bool q3_item_bind_existing(qa_q3_game *game, qa_actor_id actor,
                           const qa_q3_item_spawn *input, bool available,
                           bool initial_powerup_delay, bool *placed, qa_error *error) {
    if (placed)
        *placed = false;
    if (!game || !item_spawn_valid(game, input) || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        game->actors[actor.slot].kind)
        return q3_fail(error, "invalid existing Q3 item admission");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        game->actors[actor.slot].kind)
        return true;
    body.origin = input->origin;
    body.velocity = input->velocity;
    body.bounds = (qa_bounds){qa_v3(-15, -15, -15), qa_v3(15, 15, 15)};
    body.ground = (qa_actor_reference){0};
    qa_actor_collision collision = {.family = QA_GAME_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_contents_decode(Q3_CONTENTS_TRIGGER, QA_GAME_Q3),
                                    .role = QA_COLLISION_TRIGGER};
    int32_t ground_entity_number = 0;
    bool on_ground = false;
    if (!input->dropped && !input->suspended) {
        qa_trace_query query = {.start = input->origin,
                                .end = qa_vec_add(input->origin, qa_v3(0, 0, -4096)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = actor,
                                .policy = qa_collision_default_policy(QA_GAME_Q3)};
        query.policy.contents_mask = qa_collision_contents_mask(1, QA_GAME_Q3);
        qa_trace_result trace;
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return false;
        if (trace.start_solid && placed)
            return true;
        if (trace.start_solid)
            return q3_fail(error, "Q3 item spawned inside solid geometry");
        body.origin = trace.end;
        body.ground = item_ground_actor(game, &trace);
        ground_entity_number = item_ground_number(game, &trace);
        on_ground = trace.hit != QA_TRACE_HIT_NONE;
    }
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        game->actors[actor.slot].kind)
        return true;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        game->actors[actor.slot].kind)
        return true;
    q3_actor *entry = &game->actors[actor.slot];
    *entry = (q3_actor){
        .actor = actor,
        .kind = Q3_ACTOR_ITEM,
        .alpha = q3_initial_alpha(game, actor),
        .state.item = {.spawn = *input,
                       .bounce = 0.5f,
                       .on_ground = on_ground,
                       .ground_entity_number = ground_entity_number,
                       .trajectory = {.type = input->dropped ? QA_TRAJECTORY_GRAVITY
                                                             : QA_TRAJECTORY_STATIONARY,
                                      .base = body.origin,
                                      .delta = input->dropped ? input->velocity : qa_v3(0, 0, 0),
                                      .time_ms = input->dropped ? game->now_ms : 0},
                       .expire_at = input->dropped ? q3_add_time(game->now_ms, 30000) : 0}};
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 item constructor lacks its physical source row");
    source->type = 2;
    source->model = (int32_t)input->item_index;
    source->model2 = input->dropped ? 1 : 0;
    if (input->dropped)
        source->flags |= 0x20;
    if (input->dropped) q3_postgame_native_think_assigned(game, actor);
    if(!q3_item_observation_bind(game,actor,&game->item_observations[actor.slot],error)) return false;
    if (initial_powerup_delay && items[input->item_index].kind == QA_Q3_ITEM_POWERUP &&
        !input->dropped) {
        q3_postgame_native_think_assigned(game, actor);
        available = false;
        entry->state.item.hidden = true;
        float delay_seconds = (45.0f + (q3_crandom(game) * 15.0f));
        entry->state.item.respawn_at = q3_source_float_schedule(game->now_ms, delay_seconds);
    }
    if (!available) {
        entry->state.item.hidden = true;
        source->flags |= 0x80;
        if (!qa_world_set_collision(game->options.services.world, actor, NULL, error))
            return false;
        if (!q3_actor_get(game, actor))
            return true;
        if (!qa_world_unlink(game->options.services.world, actor, error))
            return false;
        if (q3_actor_get(game, actor) && !q3_wire_entity_ready(game, actor, error))
            return false;
        if (q3_actor_get(game, actor) && placed)
            *placed = true;
        return true;
    }
    if (input->dropped && items[input->item_index].kind == QA_Q3_ITEM_TEAM &&
        (game->options.rules.game_type == 4 ||
         (game->options.product == QA_Q3_TEAM_ARENA && game->options.rules.game_type == 5))) {
        if (!game->options.hooks.objective_dropped)
            return q3_fail(error, "Q3 dropped flag has no actual TEAM adoption owner");
        if (!game->options.hooks.objective_dropped(game->options.hooks.context, actor,
                                                  input->item_index, error))
            return false;
        if (!q3_actor_get(game, actor))
            return true;
    }
    if (!qa_world_set_collision(game->options.services.world, actor, &collision, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (!qa_q3_wire_link(game, actor, NULL, error))
        return false;
    if (q3_actor_get(game, actor) && !q3_wire_entity_ready(game, actor, error))
        return false;
    if (q3_actor_get(game, actor) && placed)
        *placed = true;
    return true;
}
bool qa_q3_source_item_adopt(qa_q3_game *game, qa_actor_id actor,
                             const qa_q3_item_spawn *input, bool available,
                             qa_error *error) {
    uint32_t source_slot;
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX ||
        !qa_q3_source_actor_slot(game, actor, &source_slot, error) ||
        source_slot < QA_Q3_SOURCE_CLIENTS || source_slot >= QA_Q3_SOURCE_WORLD)
        return q3_fail(error, "Q3 item adoption requires an actual dynamic source actor");
    ++game->observation_depth;
    bool placed = false;
    bool okay = q3_item_bind_existing(game, actor, input, available, false, &placed, error);
    if (okay && !placed && q3_actor_get(game, actor))
        okay = q3_fail(error, "Q3 adopted item starts inside solid geometry");
    --game->observation_depth;
    return okay;
}
bool qa_q3_source_item_spawn_read(const qa_q3_game *game, qa_actor_id actor,
                                  qa_q3_item_spawn *out, bool *finished,
                                  qa_error *error) {
    uint32_t source_slot;
    if (!game || !out || !finished ||
        !qa_q3_source_actor_slot(game, actor, &source_slot, error))
        return q3_fail(error, "Q3 item source read requires an actual source actor");
    const q3_actor *entry = q3_actor_const(game, actor);
    if (entry && entry->kind == Q3_ACTOR_ITEM) {
        *out = entry->state.item.spawn;
        *finished = true;
        return true;
    }
    const qa_q3_map_actor_state *map = q3_map_const(game, actor);
    if (!map || map->kind != QA_Q3_MAP_ITEM || !map->item.item_index) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Q3 source entity has no actual SpawnItem owner");
        return false;
    }
    *out = map->item;
    *finished = false;
    return true;
}
static bool spawn_item(qa_q3_game *game, const qa_q3_item_spawn *input, qa_actor_id *out,
                        qa_error *error) {
    qa_string_id classname;
    if (!qa_builtin_resource(&game->options.services, items[input->item_index].classname,
                              &classname, error)) return false;
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .definition = classname,
                              .body = {.origin = input->origin,
                                       .velocity = input->velocity,
                                       .bounds = {qa_v3(-15, -15, -15), qa_v3(15, 15, 15)}}};
    qa_actor_id actor;
    if (!q3_spawn_actor(game, &spawn, &actor, error))
        return false;
    if (!q3_item_bind_existing(game, actor, input, true, true, NULL, error))
        return q3_rollback_spawn(game, actor, error);
    if (!input->dropped && items[input->item_index].kind == QA_Q3_ITEM_TEAM &&
        q3_actor_get(game, actor) && game->options.hooks.objective_admitted &&
        !game->options.hooks.objective_admitted(game->options.hooks.context, actor,
                                                input->item_index, true, error))
        return false;
    *out = q3_actor_get(game, actor) ? actor : (qa_actor_id){0};
    return true;
}
bool qa_q3_spawn_item(qa_q3_game *game, const qa_q3_item_spawn *input, qa_actor_id *out,
                      qa_error *error) {
    if (!game || !out || game->source_restored || game->observation_depth == SIZE_MAX ||
        !item_spawn_valid(game, input))
        return q3_fail(error, "invalid Q3 item spawn boundary");
    qa_q3_item_spawn captured = *input;
    ++game->observation_depth;
    bool okay = spawn_item(game, &captured, out, error);
    --game->observation_depth;
    return okay;
}
static qa_pickup_offer item_offer(const qa_q3_game *game, const q3_actor *entry,
                                  qa_actor_id recipient) {
    uint32_t index = entry->state.item.spawn.item_index;
    const qa_q3_item *item = &items[index];
    qa_pickup_resource resource = {0};
    if (item->kind == QA_Q3_ITEM_WEAPON || item->kind == QA_Q3_ITEM_AMMO)
        resource = (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY, .item = game->item_ids[index]};
    else if (item->kind == QA_Q3_ITEM_ARMOR)
        resource = (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION,
                                        .channel = QA_PROTECTION_REGULAR};
    return (qa_pickup_offer){.pickup = entry->actor,
                             .recipient = recipient,
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
}
static bool item_observation(void *opaque,qa_actor_id pickup,qa_actor_id recipient,
                              qa_pickup_offer *offer,float *utility,bool *available,qa_error *error) {
    qa_q3_game *game=opaque;*utility=0;*available=false;
    if(game->observation_depth==SIZE_MAX) return q3_fail(error,"Q3 item observation nesting exhausted");
    ++game->observation_depth;bool ok=true;
    q3_actor *entry=q3_actor_get(game,pickup);
    if(!entry || entry->kind!=Q3_ACTOR_ITEM || entry->state.item.hidden) goto done;
    qa_q3_item_spawn spawn=entry->state.item.spawn;
    const qa_q3_item *item=&items[spawn.item_index];
    *offer = item_offer(game, entry, recipient);
    qa_combat_state combat;
    if(!qa_combat_read(game->options.services.combat,recipient,&combat,error)) {ok=false;goto done;}
    if(combat.health<=0) goto done;
    bool handled;
    if(!qa_pickups_preview(game->options.services.pickups,offer,utility,available,&handled,error)) {ok=false;goto done;}
    if(handled) goto done;
    q3_actor *player_actor=q3_actor_get(game,recipient);
    if(!q3_actor_get(game,pickup) || !player_actor || player_actor->kind!=Q3_ACTOR_PLAYER) goto done;
    qa_q3_player_state player=player_actor->state.player;
    float maximum=(float)player.max_health;
    int32_t quantity=spawn.count?spawn.count:item->quantity;
    switch(item->kind) {
    case QA_Q3_ITEM_WEAPON:
    case QA_Q3_ITEM_AMMO: {
        if(!(player.selections&QA_Q3_ARSENAL)) break;
        int32_t ammo;
        if(!q3_ammo_read(game,recipient,(qa_q3_weapon)item->tag,&ammo,error)) {ok=false;break;}
        if(item->kind==QA_Q3_ITEM_AMMO && ammo>=200) break;
        if(item->kind==QA_Q3_ITEM_WEAPON) {
            qa_inventory_entry weapon;
            if(!qa_inventory_entry_read(game->options.services.inventory,recipient,game->weapon_items[item->tag],&weapon,error)) {ok=false;break;}
            *utility=weapon.count>0?0:10;
            if(spawn.count<0) quantity=0;
            else if(!spawn.dropped && game->options.rules.game_type!=3) quantity=ammo<quantity?quantity-ammo:1;
        }
        int32_t after=q3_add_time(ammo,quantity);if(after>200) after=200;
        *utility+=fmaxf(0,(float)((int64_t)after-ammo));*available=true;break;
    }
    case QA_Q3_ITEM_HEALTH:
        if(player.persistent!=QA_Q3_P_GUARD && (item->quantity==5 || item->quantity==100)) maximum*=2;
        *utility=fmaxf(0,fminf(maximum,combat.health+(float)quantity)-combat.health);
        *available=combat.health<maximum;break;
    case QA_Q3_ITEM_ARMOR:
        if(player.persistent==QA_Q3_P_SCOUT) break;
        if(player.persistent!=QA_Q3_P_GUARD) maximum*=2;
        *utility=fmaxf(0,fminf(maximum,(float)combat.armor.regular.points+(float)item->quantity)-(float)combat.armor.regular.points);
        *available=combat.armor.regular.points<maximum;break;
    case QA_Q3_ITEM_HOLDABLE: *available=player.holdable==QA_Q3_H_NONE;*utility=*available?1:0;break;
    case QA_Q3_ITEM_POWERUP: *available=true;*utility=fmaxf(0,(float)quantity);break;
    case QA_Q3_ITEM_PERSISTENT: {
        int32_t team=game->options.hooks.source_team?
            game->options.hooks.source_team(game->options.hooks.context,recipient):0;
        *available=!player.persistent && game->options.product==QA_Q3_TEAM_ARENA &&
            (!(spawn.team_restriction&2) || team==1) && (!(spawn.team_restriction&4) || team==2);
        *utility=*available?1:0;break;
    }
    /* Explicit selected-mode objectives are considered by native team AI;
     * observing them must not call the mutating objective pickup hook. */
    case QA_Q3_ITEM_TEAM: break;
    default: break;
    }
done:
    if(!q3_actor_get(game,pickup) || !qa_actors_get(qa_session_actors(game->options.services.session),recipient)) {
        *available=false;*utility=0;
    }
    --game->observation_depth;return ok;
}
bool q3_item_observation_bind(qa_q3_game *game,qa_actor_id actor,qa_pickup_lease *out,qa_error *error) {
    qa_pickup_observer observer={.context=game,.inspect=item_observation};
    return qa_pickups_observe(game->options.services.pickups,actor,game->options.owner,&observer,out,error);
}
bool qa_q3_game_pickup_observer(qa_q3_game *game, qa_actor_id actor, uint64_t serial,
                                qa_pickup_observer *out, qa_error *error) {
    q3_actor *entry = game ? q3_actor_get(game, actor) : NULL;
    if (!entry || entry->kind != Q3_ACTOR_ITEM || !serial || !out || game->observation_depth)
        return q3_fail(error, "invalid Q3 saved pickup observer");
    qa_pickup_lease *prior = &game->item_observations[actor.slot];
    if (!qa_actor_id_equal(prior->actor, actor) || prior->serial != serial)
        return q3_fail(error, "Q3 saved pickup observer has no captured private lease");
    *out = (qa_pickup_observer){.context = game, .inspect = item_observation};
    return true;
}
void q3_item_observations_abort(qa_q3_game *game,qa_pickup_lease *candidate) {
    if(!candidate) return;
    for(uint32_t i=0;i<game->capacity;++i)
        if(candidate[i].serial && candidate[i].serial!=game->item_observations[i].serial)
            qa_pickups_observation_close(game->options.services.pickups,candidate[i],NULL);
    free(candidate);
}
bool q3_item_observations_prepare(qa_q3_game *game,const q3_actor *actors,qa_pickup_lease **out,qa_error *error) {
    qa_pickup_lease *candidate=calloc(game->capacity,sizeof(*candidate));
    if(!candidate) {qa_error_set(error,QA_ERROR_MEMORY,0,"allocating Q3 item observation restore bindings");return false;}
    for(uint32_t i=0;i<game->capacity;++i) {
        if(actors[i].kind!=Q3_ACTOR_ITEM) continue;
        if(qa_actor_id_equal(game->item_observations[i].actor,actors[i].actor) &&
           qa_pickups_observation_current(game->options.services.pickups,game->item_observations[i]))
            candidate[i]=game->item_observations[i];
        else if(!q3_item_observation_bind(game,actors[i].actor,&candidate[i],error)) {
            q3_item_observations_abort(game,candidate);return false;
        }
    }
    *out=candidate;return true;
}
void q3_item_observations_commit(qa_q3_game *game,qa_pickup_lease *candidate) {
    for(uint32_t i=0;i<game->capacity;++i)
        if(game->item_observations[i].serial && game->item_observations[i].serial!=candidate[i].serial)
            qa_pickups_observation_close(game->options.services.pickups,game->item_observations[i],NULL);
    free(game->item_observations);game->item_observations=candidate;
}
bool qa_q3_pickups_rebind(qa_q3_game *game,qa_error *error) {
    if(!game || game->source_restored || game->observation_depth) return q3_fail(error,"Q3 item bindings are borrowed");
    qa_pickup_lease *candidate;
    if(!q3_item_observations_prepare(game,game->actors,&candidate,error)) return false;
    q3_item_observations_commit(game,candidate);return true;
}

typedef struct pickup_context {
    qa_q3_game *game;
    qa_actor_id item;
    float respawn;
    bool accepted, allow_hidden, source_logged;
} pickup_context;
static bool pickup_original_live(qa_q3_game *game, const qa_pickup_offer *offer,
                                 q3_actor **pickup, q3_actor **recipient) {
    q3_actor *item = q3_actor_get(game, offer->pickup);
    q3_actor *player = q3_actor_get(game, offer->recipient);
    if (!item || item->kind != Q3_ACTOR_ITEM || !player ||
        player->kind != Q3_ACTOR_PLAYER)
        return false;
    if (pickup)
        *pickup = item;
    if (recipient)
        *recipient = player;
    return true;
}
static bool pickup_eligible(void *context, const qa_pickup_offer *offer, bool *eligible,
                            qa_error *error) {
    pickup_context *call = context;
    q3_actor *item = q3_actor_get(call->game, offer->pickup);
    qa_combat_state combat;
    *eligible = item && item->kind == Q3_ACTOR_ITEM &&
                (call->allow_hidden || !item->state.item.hidden);
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
static bool pickup_log(pickup_context *call, const qa_pickup_offer *offer,
                        const qa_q3_item *item, qa_error *error) {
    call->source_logged = true;
    if (!call->game->options.hooks.source_log)
        return true;
    uint32_t slot;
    if (!qa_q3_native_client_slot(call->game, offer->recipient, &slot, NULL))
        return true;
    char line[160];
    snprintf(line, sizeof(line), "Item: %u %s\n", slot, item->classname);
    return call->game->options.hooks.source_log(call->game->options.hooks.context, line, error);
}
static bool pickup_original_body(void *context, const qa_pickup_offer *offer, bool *accepted,
                            qa_error *error) {
    pickup_context *call = context;
    qa_q3_game *game = call->game;
    q3_actor *entity, *recipient;
    *accepted = false;
    if (!pickup_original_live(game, offer, &entity, &recipient))
        return true;
    qa_q3_player_state *player = &recipient->state.player;
    const qa_q3_item *item = &items[entity->state.item.spawn.item_index];
    qa_q3_item_spawn spawn = entity->state.item.spawn;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, offer->recipient, &combat, error))
        return false;
    if (!pickup_original_live(game, offer, &entity, &recipient))
        return true;
    player = &recipient->state.player;
    uint32_t source_client;
    bool native_client = q3_source_client_pointer(game, offer->recipient, &source_client);
    if (game->options.hooks.source_supply_take &&
        native_client) {
        q3_wire_entity_source *source = q3_wire_entity(game, offer->pickup);
        if (!source || source->model != (int32_t)spawn.item_index)
            return q3_fail(error, "Q3 source pickup lost its published item declaration");
        qa_q3_supply_descriptor descriptor = {.pickup = offer->pickup, .recipient = offer->recipient,
            .item = item, .count = spawn.count, .generic1 = source->generic1,
            .game_type = game->options.rules.game_type, .dropped = spawn.dropped,
            .weapon_respawn_seconds = game->options.rules.weapon_respawn_seconds,
            .team_weapon_respawn_seconds = game->options.rules.team_weapon_respawn_seconds};
        qa_q3_supply_kind kind = QA_Q3_SUPPLY_NATIVE;
        bool supplied = false;
        float respawn = 0;
        if (!game->options.hooks.source_supply_take(game->options.hooks.context,
                &descriptor, &kind, &supplied, &respawn, error)) return false;
        if (!pickup_original_live(game, offer, &entity, &recipient)) return true;
        if (kind == QA_Q3_SUPPLY_REJECTED) return true;
        if (kind == QA_Q3_SUPPLY_SELECTED) {
            if (!supplied) return true;
            if (!pickup_log(call, offer, item, error)) return false;
            if (!pickup_original_live(game, offer, NULL, NULL)) return true;
            call->respawn = respawn;
            *accepted = respawn != 0;
            return true;
        }
        if (kind != QA_Q3_SUPPLY_NATIVE)
            return q3_fail(error, "Q3 source pickup returned an invalid admission kind");
        player = &recipient->state.player;
    }
    float maximum = (float)player->max_health;
    int32_t quantity = spawn.count ? spawn.count : item->quantity;
    double given;
    switch (item->kind) {
    case QA_Q3_ITEM_WEAPON: {
        if (!(player->selections & QA_Q3_ARSENAL) && !native_client)
            return true;
        int32_t ammo;
        if (!q3_ammo_read(game, offer->recipient, (qa_q3_weapon)item->tag, &ammo, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        if (spawn.count < 0)
            quantity = 0;
        else if (!spawn.dropped && game->options.rules.game_type != 3)
            quantity = ammo < quantity ? quantity - ammo : 1;
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        if (!qa_inventory_give(game->options.services.inventory, offer->recipient,
                               game->weapon_items[item->tag], 1, &given, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        if (!q3_add_ammo(game, offer->recipient, (qa_q3_weapon)item->tag, quantity, error))
            return false;
        break;
    }
    case QA_Q3_ITEM_AMMO: {
        if (!(player->selections & QA_Q3_ARSENAL) && !native_client)
            return true;
        int32_t ammo;
        if (!q3_ammo_read(game, offer->recipient, (qa_q3_weapon)item->tag, &ammo, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        if (ammo >= 200)
            return true;
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
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
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
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
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        qa_regular_armor armor = {
            .kind = QA_ARMOR_Q3,
            .protection.q3_protection = 0.66f,
            .points = fminf(maximum, (float)combat.armor.regular.points + (float)item->quantity),
            .item = game->item_ids[spawn.item_index]};
        if (!qa_combat_set_regular_armor(game->options.services.combat, offer->recipient, &armor,
                                         error))
            return false;
        break;
    }
    case QA_Q3_ITEM_HOLDABLE:
        if (player->holdable)
            return true;
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, &entity, &recipient))
            return true;
        player = &recipient->state.player;
        player->holdable = (qa_q3_holdable)item->tag;
        if (player->holdable == QA_Q3_H_KAMIKAZE)
            player->flags |= 0x200u;
        if (!q3_inventory_holdable_changed(game, offer->recipient, QA_Q3_H_NONE,
                                            (qa_q3_holdable)item->tag, error))
            return false;
        break;
    case QA_Q3_ITEM_POWERUP: {
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, &entity, &recipient))
            return true;
        player = &recipient->state.player;
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
        if (!pickup_original_live(game, offer, &entity, &recipient))
            return true;
        player = &recipient->state.player;
        if (player->persistent || game->options.product != QA_Q3_TEAM_ARENA ||
            ((spawn.team_restriction & 2) && team != 1) ||
            ((spawn.team_restriction & 4) && team != 2))
            return true;
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, &entity, &recipient))
            return true;
        player = &recipient->state.player;
        player->persistent = (qa_q3_powerup)item->tag;
        player->persistent_item = offer->pickup;
        player->max_health = player->handicap;
        if (player->persistent == QA_Q3_P_GUARD) {
            player->max_health *= 2;
            if (!qa_combat_set_health(game->options.services.combat, offer->recipient,
                                      (float)player->max_health, error))
                return false;
            if (!pickup_original_live(game, offer, &entity, &recipient))
                return true;
            player = &recipient->state.player;
            if (!qa_combat_set_regular_points(game->options.services.combat,
                                              offer->recipient,
                                              (float)player->max_health, NULL,
                                              error))
                return false;
        } else if (player->persistent == QA_Q3_P_SCOUT) {
            if (!qa_combat_set_regular_points(game->options.services.combat,
                                              offer->recipient, 0, NULL,
                                              error))
                return false;
        }
        if (!pickup_original_live(game, offer, &entity, &recipient))
            return true;
        player = &recipient->state.player;
        for (int weapon = 0; weapon < QA_Q3_WEAPON_COUNT; ++weapon) {
            if (!q3_ammo_timer_store(game, offer->recipient, (qa_q3_weapon)weapon, 0, error))
                return false;
            if (!pickup_original_live(game, offer, &entity, &recipient)) return true;
        }
        break;
    }
    case QA_Q3_ITEM_TEAM:
        if (!game->options.hooks.objective_pickup)
            return q3_fail(error, "Q3 objective has no selected match owner");
        if (!pickup_log(call, offer, item, error))
            return false;
        if (!pickup_original_live(game, offer, NULL, NULL))
            return true;
        return game->options.hooks.objective_pickup(game->options.hooks.context, offer->pickup,
                                                    offer->recipient, spawn.item_index, accepted,
                                                    error);
    default:
        return q3_fail(error, "invalid Q3 pickup kind");
    }
    if (!pickup_original_live(game, offer, NULL, NULL))
        return true;
    if (!q3_ranking_pickup(game, offer->recipient, item,
                           spawn.count ? spawn.count : item->quantity, error))
        return false;
    *accepted = true;
    return true;
}
static bool pickup_original(void *context, const qa_pickup_offer *offer, bool *accepted,
                            qa_error *error) {
    pickup_context *call = context;
    qa_q3_game *game = call->game;
    q3_actor *item, *player;
    bool selected = game->options.hooks.selected_client_effects &&
        pickup_original_live(game, offer, &item, &player) &&
        items[item->state.item.spawn.item_index].kind == QA_Q3_ITEM_PERSISTENT &&
        (player->state.player.selections & (QA_Q3_ARSENAL | QA_Q3_EQUIPMENT));
    qa_q3_selected_client_effects before;
    if (selected && !qa_q3_selected_client_effects_read(game, offer->recipient, &before, error))
        return false;
    bool okay = pickup_original_body(context, offer, accepted, error);
    player = q3_actor_get(game, offer->recipient);
    if (selected && player && player->kind == Q3_ACTOR_PLAYER &&
        (player->state.player.selections & (QA_Q3_ARSENAL | QA_Q3_EQUIPMENT))) {
        qa_error publication = {0};
        bool published = qa_q3_selected_client_effects_publish(game, offer->recipient, &before, &publication);
        if (okay && !published) { if (error) *error = publication; okay = false; }
    }
    return okay;
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
    if (!call->source_logged) {
        if (!pickup_log(call, offer, &items[spawn.item_index], error))
            return false;
        if (!pickup_original_live(game, offer, &entry, NULL))
            return true;
    }
    uint32_t client_slot;
    bool source_client = qa_q3_native_client_slot(game, offer->recipient, &client_slot, NULL);
    bool predict = !source_client || (game->clients[client_slot].rule.predict_item_pickup &&
                   items[spawn.item_index].kind != QA_Q3_ITEM_POWERUP);
    if (predict) {
        if (!q3_player_event(game, offer->recipient, 19, (int32_t)spawn.item_index, error))
            return false;
    } else if (!q3_wire_add_event(game, offer->recipient, 19, (int32_t)spawn.item_index, error) ||
               !q3_event(game, offer->recipient, (qa_actor_id){0}, QA_BUILTIN_ANIMATION,
                         19, (int32_t)spawn.item_index, qa_v3(0, 0, 0), qa_v3(0, 0, 0),
                         qa_v3(0, 0, 0), error))
        return false;
    entry = q3_actor_get(game, offer->pickup);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    if (items[spawn.item_index].kind == QA_Q3_ITEM_POWERUP ||
        items[spawn.item_index].kind == QA_Q3_ITEM_TEAM) {
        qa_vec3 origin = entry->state.item.trajectory.base;
        const qa_q3_map_actor_state *authored = q3_map_const(game, offer->pickup);
        bool single_client = authored && authored->speed != 0;
        qa_actor_id temporary;
        if (source_client && !q3_wire_temp_entity(game, origin, 20, &temporary, error))
            return false;
        if (!q3_actor_get(game, offer->pickup) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), offer->recipient))
            return true;
        if (source_client) {
            qa_q3_entity *event = q3_wire_temporary(game, temporary);
            q3_wire_entity_source *source = q3_wire_entity(game, temporary);
            uint32_t temporary_slot;
            if (!event || !source || !qa_q3_source_actor_slot(game, temporary, &temporary_slot, error))
                return q3_fail(error, "Q3 global pickup event lost its source row");
            event->eventParm = (int32_t)spawn.item_index;
            if (single_client) {
                game->source_entities[temporary_slot].server_flags |= 256u;
                source->single_client = (int32_t)client_slot;
            } else
                game->source_entities[temporary_slot].server_flags |= 32u;
        }
        if (!q3_event(game, offer->pickup, offer->recipient, QA_BUILTIN_ITEM, 20,
                      (int32_t)spawn.item_index, origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0),
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
    q3_wire_entity_source *source = q3_wire_entity(game, offer->pickup);
    if (!source)
        return q3_fail(error, "Q3 item pickup lost its actual source lifecycle row");
    if (spawn.wait_seconds == -1) {
        source->unlink_after_event = true;
        entry->state.item.hidden = true;
        game->source_entities[game->source_numbers[offer->pickup.slot]].server_flags |= 1u;
        source->flags |= 0x80;
        return qa_world_set_collision(game->options.services.world, offer->pickup, NULL, error);
    }
    if (spawn.dropped)
        source->free_after_event = true;
    float seconds = spawn.wait_seconds != 0 ? spawn.wait_seconds : call->respawn;
    int32_t respawn_seconds = qa_source_float_to_i32(seconds);
    if (spawn.random_seconds != 0) {
        float adjusted = ((float)respawn_seconds + (q3_crandom(game) * spawn.random_seconds));
        respawn_seconds = qa_source_float_to_i32(adjusted);
        if (respawn_seconds < 1)
            respawn_seconds = 1;
    }
    uint32_t respawn_bits = (uint32_t)respawn_seconds * UINT32_C(1000);
    int32_t respawn_delay;
    memcpy(&respawn_delay, &respawn_bits, sizeof(respawn_delay));
    int32_t respawn = respawn_seconds > 0
                          ? q3_add_time(game->now_ms, respawn_delay)
                          : 0;
    bool map_handled = false;
    if (!q3_map_item_picked(game, offer->pickup, respawn, entry->state.item.expire_at,
                            &map_handled, error))
        return false;
    if (map_handled)
        return true;
    return qa_q3_item_availability(game, offer->pickup, false, respawn, entry->state.item.expire_at,
                                   error);
}
bool q3_item_touch(qa_q3_game *game, qa_actor_id item_actor, qa_actor_id recipient,
                   bool allow_hidden, bool *accepted, qa_error *error) {
    if (accepted)
        *accepted = false;
    q3_actor *entry = q3_actor_get(game, item_actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM ||
        (!allow_hidden && entry->state.item.hidden))
        return true;
    const qa_q3_item *item = &items[entry->state.item.spawn.item_index];
    qa_pickup_offer offer = item_offer(game, entry, recipient);
    pickup_context context = {game, item_actor, respawn_seconds(game, item), false,
                              allow_hidden, false};
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
bool qa_q3_touch_item(qa_q3_game *game, qa_actor_id item_actor, qa_actor_id recipient,
                      bool *accepted, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 item touch boundary");
    ++game->observation_depth;
    bool okay = q3_item_touch(game, item_actor, recipient, false, accepted, error);
    --game->observation_depth;
    return okay;
}
static bool item_respawn(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    bool handled;
    if (!q3_map_item_respawn(game, actor, &handled, error))
        return false;
    if (handled)
        return true;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return q3_fail(error, "Q3 RespawnItem has no actual source item owner");
    int32_t expire_at = entry->state.item.expire_at;
    if (!qa_q3_item_availability(game, actor, true, 0, expire_at, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    const qa_q3_item *definition = &items[entry->state.item.spawn.item_index];
    const char *respawn_sound = definition->kind == QA_Q3_ITEM_POWERUP
        ? "sound/items/poweruprespawn.wav"
        : definition->kind == QA_Q3_ITEM_HOLDABLE && definition->tag == QA_Q3_H_KAMIKAZE
            ? "sound/items/kamikazerespawn.wav" : NULL;
    if (respawn_sound) {
        int32_t sound_index;
        qa_actor_id temporary;
        if (!q3_wire_temp_entity(game, entry->state.item.trajectory.base, 46, &temporary, error) ||
            !qa_q3_sound_index(game, respawn_sound, &sound_index, error))
            return false;
        qa_q3_entity *event = q3_wire_temporary(game, temporary);
        uint32_t temporary_slot;
        if (!event || !qa_q3_source_actor_slot(game, temporary, &temporary_slot, error))
            return q3_fail(error, "Q3 item respawn sound lost its temporary source row");
        event->eventParm = sound_index;
        game->source_entities[temporary_slot].server_flags |= 32u;
        if (!q3_actor_get(game, actor))
            return true;
    }
    if (!q3_wire_add_event(game, actor, 40, 0, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    return !q3_actor_get(game, actor) ||
        q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ITEM, 40, 0, body.origin,
                  qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
bool qa_q3_source_item_respawn(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t source_slot;
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX ||
        !qa_q3_source_actor_slot(game, actor, &source_slot, error) ||
        source_slot < QA_Q3_SOURCE_CLIENTS || source_slot >= QA_Q3_SOURCE_WORLD)
        return q3_fail(error, "Q3 RespawnItem requires an actual dynamic source actor");
    ++game->observation_depth;
    bool okay = item_respawn(game, actor, error);
    --game->observation_depth;
    return okay;
}
static bool item_think(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    if (replaced) return true;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    q3_item_state *item = &entry->state.item;
    if (item->expire_at && game->now_ms >= item->expire_at) {
        uint32_t index = item->spawn.item_index;
        if (items[index].kind == QA_Q3_ITEM_TEAM &&
            (game->options.rules.game_type == 4 ||
             (game->options.product == QA_Q3_TEAM_ARENA && game->options.rules.game_type == 5))) {
            if (!game->options.hooks.objective_expired)
                return q3_fail(error, "Q3 dropped flag expired without its source TEAM owner");
            return game->options.hooks.objective_expired(game->options.hooks.context, actor, index,
                                                         error);
        }
        return qa_session_release(game->options.services.session, actor, error);
    }
    if (item->hidden) {
        if (!item->respawn_at || game->now_ms < item->respawn_at)
            return true;
        return item_respawn(game, actor, error);
    }
    return true;
}
bool q3_item_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    q3_item_state *item = &entry->state.item;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    item = &entry->state.item;
    if (item->ground_entity_number == -1 && item->trajectory.type != QA_TRAJECTORY_GRAVITY) {
        item->trajectory.type = QA_TRAJECTORY_GRAVITY;
        item->trajectory.time_ms = game->now_ms;
    }
    if (item->trajectory.type == QA_TRAJECTORY_STATIONARY)
        return item_think(game, actor, error);
    qa_vec3 destination;
    if (!qa_trajectory_position(&item->trajectory, game->now_ms, 800, &destination, error))
        return false;
    uint32_t source_slot;
    if (!qa_q3_source_actor_slot(game, actor, &source_slot, error))
        return false;
    int32_t owner_number = game->source_entities[source_slot].owner_number;
    qa_actor_id owner = owner_number >= 0 && owner_number < (int32_t)QA_Q3_SOURCE_WORLD &&
        game->source_entities[owner_number].in_use
            ? game->source_entities[owner_number].actor : (qa_actor_id){0};
    qa_trace_query query = {.start = body.origin,
                            .end = destination,
                            .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                            .policy = qa_collision_default_policy(QA_GAME_Q3),
                            .pass_actor = owner};
    query.policy.contents_mask = qa_collision_contents_mask(0x10001u, QA_GAME_Q3);
    qa_trace_result trace;
    if (!qa_world_trace(game->options.services.world, &query, &trace, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    body.origin = trace.end;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (!qa_q3_wire_link(game, actor, NULL, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    item = &entry->state.item;
    if (!item_think(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    item = &entry->state.item;
    if (trace.fraction == 1 && !trace.start_solid)
        return true;
    qa_point_query point = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return true;
    item = &entry->state.item;
    if (qa_collision_bits_overlap(contents.contents, qa_collision_bit(QA_CONTENT_NODROP))) {
        uint32_t index = item->spawn.item_index;
        if (items[index].kind == QA_Q3_ITEM_TEAM) {
            if (!game->options.hooks.objective_nodrop)
                return q3_fail(error, "Q3 team item entered NODROP without its source TEAM owner");
            return game->options.hooks.objective_nodrop(game->options.hooks.context, actor, index,
                                                        error);
        }
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
        body.ground = item_ground_actor(game, &trace);
        item->trajectory = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
        item->on_ground = trace.hit != QA_TRACE_HIT_NONE;
        item->ground_entity_number = item_ground_number(game, &trace);
    } else {
        body.origin = qa_vec_add(body.origin, normal);
        body.velocity = velocity;
        item->trajectory.base = body.origin;
        item->trajectory.time_ms = game->now_ms;
    }
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
static bool touch_actor(qa_q3_game *game, const qa_touch_contact *contact, qa_error *error) {
    bool handled = false;
    if (!q3_map_touch(game, contact, &handled, error))
        return false;
    if (handled)
        return true;
    q3_actor *entry = q3_actor_get(game, contact->self);
    if (!entry)
        return true;
    if (entry->kind == Q3_ACTOR_ITEM)
        return qa_q3_touch_item(game, contact->self, contact->other, NULL, error);
    if (entry->kind == Q3_ACTOR_OBELISK)
        return q3_obelisk_touch(game, contact->self, contact->other, error);
    if (entry->kind == Q3_ACTOR_PROX_TRIGGER)
        return q3_missile_trigger(game, entry->state.trigger.parent, contact->other, error);
    if (entry->kind == Q3_ACTOR_PORTAL && entry->state.portal.source &&
        entry->state.portal.enabled) {
        if (!q3_is_player(game, contact->other))
            return true;
        if (!q3_actor_get(game, contact->self) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
            return true;
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, contact->other, &combat, error))
            return false;
        if (combat.health <= 0)
            return true;
        if (!q3_actor_get(game, contact->self) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
            return true;
        if (game->options.hooks.objective_drop &&
            !game->options.hooks.objective_drop(game->options.hooks.context, contact->other, error))
            return false;
        entry = q3_actor_get(game, contact->self);
        if (!entry || entry->kind != Q3_ACTOR_PORTAL ||
            !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
            return true;
        q3_actor *destination = q3_actor_get(game, q3_portal_destination(game, entry->state.portal.sequence));
        if (!destination || destination->kind != Q3_ACTOR_PORTAL) {
            qa_vec3 fallback = entry->state.portal.fallback;
            if ((fallback.x != 0 || fallback.y != 0 || fallback.z != 0) &&
                !qa_q3_teleport(game, contact->other, fallback, entry->state.portal.angles, error))
                return false;
            if (!q3_actor_get(game, contact->self) ||
                !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
                return true;
            return q3_damage(game, contact->other, contact->other, contact->other, QA_Q3_W_NONE, 18,
                             12, 100000, qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error);
        }
        qa_actor_id destination_actor = destination->actor;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, destination_actor, &body, error))
            return false;
        destination = q3_actor_get(game, destination_actor);
        if (!destination || destination->kind != Q3_ACTOR_PORTAL ||
            !q3_actor_get(game, contact->self) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), contact->other))
            return true;
        return qa_q3_teleport(game, contact->other, body.origin, destination->state.portal.angles,
                              error);
    }
    return true;
}
bool qa_q3_touch(qa_q3_game *game, const qa_touch_contact *contact, qa_error *error) {
    if (!game || !contact || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 touch boundary");
    qa_touch_contact captured = *contact;
    ++game->observation_depth;
    bool okay = touch_actor(game, &captured, error);
    --game->observation_depth;
    return okay;
}

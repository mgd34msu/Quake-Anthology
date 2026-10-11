#include "internal.h"
#include <float.h>
#include <stdio.h>

typedef struct drop_weapon {
    qa_q1_weapon weapon, powered;
    q1_runtime_name classname;
    const char *model, *name, *localized;
} drop_weapon;
static const drop_weapon drop_weapons[] = {
    {QA_Q1_SUPER_SHOTGUN, QA_Q1_WEAPON_COUNT, Q1_NAME_CLASS_WEAPON_SUPERSHOTGUN, "progs/g_shot.mdl",
     "Double-barrelled Shotgun", "$qc_double_shotgun"},
    {QA_Q1_NAILGUN, QA_Q1_LAVA_NAILGUN, Q1_NAME_CLASS_WEAPON_NAILGUN, "progs/g_nail.mdl", "nailgun",
     "$qc_nailgun"},
    {QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_SUPER_NAILGUN, Q1_NAME_CLASS_WEAPON_SUPERNAILGUN, "progs/g_nail2.mdl",
     "Super Nailgun", "$qc_super_nailgun"},
    {QA_Q1_GRENADE, QA_Q1_MULTI_GRENADE, Q1_NAME_CLASS_WEAPON_GRENADELAUNCHER, "progs/g_rock.mdl",
     "Grenade Launcher", "$qc_grenade_launcher"},
    {QA_Q1_ROCKET, QA_Q1_MULTI_ROCKET, Q1_NAME_CLASS_WEAPON_ROCKETLAUNCHER, "progs/g_rock2.mdl",
     "Rocket Launcher", "$qc_rocket_launcher"},
    {QA_Q1_LIGHTNING, QA_Q1_PLASMA, Q1_NAME_CLASS_WEAPON_LIGHTNING, "progs/g_light.mdl", "Thunderbolt",
     "$qc_thunderbolt"}};
typedef struct drop_ammo {
    qa_q1_ammo ammo;
    qa_q1_weapon selected[2], owners[2];
    float limit;
} drop_ammo;
static const drop_ammo drop_ammunition[] = {
    {QA_Q1_SHELLS, {QA_Q1_SHOTGUN, QA_Q1_SUPER_SHOTGUN}, {QA_Q1_SHOTGUN, QA_Q1_SUPER_SHOTGUN}, 20},
    {QA_Q1_NAILS, {QA_Q1_NAILGUN, QA_Q1_SUPER_NAILGUN}, {QA_Q1_NAILGUN, QA_Q1_SUPER_NAILGUN}, 20},
    {QA_Q1_LAVA_NAILS,
     {QA_Q1_LAVA_NAILGUN, QA_Q1_LAVA_SUPER_NAILGUN},
     {QA_Q1_NAILGUN, QA_Q1_SUPER_NAILGUN},
     20},
    {QA_Q1_ROCKETS, {QA_Q1_GRENADE, QA_Q1_ROCKET}, {QA_Q1_GRENADE, QA_Q1_ROCKET}, 10},
    {QA_Q1_MULTI_ROCKETS,
     {QA_Q1_MULTI_GRENADE, QA_Q1_MULTI_ROCKET},
     {QA_Q1_GRENADE, QA_Q1_ROCKET},
     10},
    {QA_Q1_CELLS, {QA_Q1_LIGHTNING, QA_Q1_LIGHTNING}, {QA_Q1_LIGHTNING, QA_Q1_LIGHTNING}, 20},
    {QA_Q1_PLASMA_CELLS, {QA_Q1_PLASMA, QA_Q1_PLASMA}, {QA_Q1_LIGHTNING, QA_Q1_LIGHTNING}, 10}};

static bool count_item(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double *count,
                       qa_error *error) {
    qa_inventory_entry entry;
    qa_error local = {0};
    if (qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &local)) {
        *count = entry.count;
        return true;
    }
    if (local.code == QA_ERROR_NOT_FOUND) {
        *count = 0;
        return true;
    }
    if (error)
        *error = local;
    return false;
}
static bool consume(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double count,
                    qa_error *error) {
    if (count <= 0)
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, actor, item, count, &consumed, error))
        return false;
    if (!consumed)
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 drop inventory changed during admitted consumption");
    return consumed;
}
static bool selected_changed(qa_q1_game *g, qa_actor_id actor, qa_item_id acquired,
                             qa_error *error) {
    if (!q1_alive(g, actor))
        return true;
    if (g->host.weapon_changed)
        return g->host.weapon_changed(g->host.context, actor, acquired, error);
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    if (!q1_enable_combos(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    qa_q1_weapon next = q1_best_weapon(g, player);
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
        if (g->weapons[i] == acquired) {
            next = g->options.deathmatch &&
                           q1_weapon_rank(g, player->weapon) <= q1_weapon_rank(g, (qa_q1_weapon)i)
                       ? player->weapon
                       : (qa_q1_weapon)i;
            break;
        }
    return qa_q1_player_select(g, actor, next, error);
}
static bool launch_body(qa_q1_game *g, qa_actor_id actor, qa_vec3 angles, qa_body_state *out,
                        qa_error *error) {
    qa_vec3 forward, direction;
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    if (!qa_world_body_read(g->services.world, actor, out, error) ||
        !q1_aim(g, actor, forward, &direction, error))
        return false;
    out->origin.z += 16;
    out->velocity = qa_vec_scale(direction, 500);
    out->angles = qa_v3(0, 0, 0);
    out->ground = (qa_actor_reference){0};
    out->bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    return true;
}
static bool launch(qa_q1_game *g, q1_actor *item, const qa_body_state *body, bool kill_velocity,
                   qa_error *error) {
    item->physics.solid = QA_PHYSICS_TRIGGER;
    item->physics.motion = QA_PHYSICS_BOUNCE;
    item->physics.flags = kill_velocity ? QA_PHYSICS_KILL_VELOCITY : 0;
    if (kill_velocity) item->source_movement_flags |= UINT32_C(256);
    return qa_world_body_write(g->services.world, item->id, body, error) &&
           q1_link(g, item, error) && q1_schedule(g, item, 120, Q1_THINK_REMOVE, error);
}
static bool take_ammo(qa_q1_game *g, qa_actor_id actor, const drop_ammo *rule, float *amount,
                      qa_error *error) {
    double count;
    if (!count_item(g, actor, g->ammo[rule->ammo], &count, error))
        return false;
    *amount = fminf(rule->limit, (float)count);
    return consume(g, actor, g->ammo[rule->ammo], *amount, error);
}
static bool missing_owners(qa_q1_game *g, qa_actor_id actor, const drop_ammo *rule, bool *missing,
                           qa_error *error) {
    double a, b;
    if (!count_item(g, actor, g->weapons[rule->owners[0]], &a, error) ||
        !count_item(g, actor, g->weapons[rule->owners[1]], &b, error))
        return false;
    *missing = a == 0 && b == 0;
    return true;
}
static bool validate_drop(qa_q1_game *g, qa_actor_id actor, const qa_q1_drop_input *input,
                          qa_actor_id *out, qa_error *error) {
    if (!g || !input || !out || !q1_alive(g, actor) || !isfinite(input->view_angles.x) ||
        !isfinite(input->view_angles.y) || !isfinite(input->view_angles.z)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 drop requires a live actor and finite view angles");
        return false;
    }
    *out = (qa_actor_id){0};
    return true;
}
bool qa_q1_ctf_toss_ammo(qa_q1_game *g, qa_actor_id actor, const qa_q1_drop_input *input,
                         qa_actor_id *out, qa_error *error) {
    if (!validate_drop(g, actor, input, out, error))
        return false;
    double available;
    if (!input->selected_ammo)
        return true;
    if (!count_item(g, actor, input->selected_ammo, &available, error))
        return false;
    if (available <= 0)
        return true;
    q1_actor *pack;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_CTF_BACKPACK], Q1_PICKUP, actor, &pack, error))
        return false;
    pack->state.pickup.drop = Q1_DROP_CTF_AMMO;
    pack->state.pickup.weapon = QA_Q1_WEAPON_COUNT;
    if (!qa_builtin_resource(&g->services, "q1:item_backpack",
                              &pack->state.pickup.item, error) ||
        !q1_model(g, pack, "progs/backpack.mdl", error))
        goto fail;
    for (size_t i = 0; i < sizeof(drop_ammunition) / sizeof(*drop_ammunition); ++i) {
        const drop_ammo *rule = &drop_ammunition[i];
        if (rule->ammo > QA_Q1_CELLS)
            continue;
        bool selected = input->selected_weapon == g->weapons[rule->selected[0]] ||
                        input->selected_weapon == g->weapons[rule->selected[1]];
        bool missing;
        if (!missing_owners(g, actor, rule, &missing, error))
            goto fail;
        if ((selected || missing) &&
            !take_ammo(g, actor, rule, &pack->state.pickup.ammo[rule->ammo], error))
            goto fail;
        if (!q1_alive(g, actor) || !q1_alive(g, pack->id))
            goto retired;
    }
    qa_body_state body;
    if (!launch_body(g, actor, input->view_angles, &body, error) ||
        !launch(g, pack, &body, true, error) || !selected_changed(g, actor, 0, error))
        goto fail;
    if (q1_alive(g, pack->id))
        *out = pack->id;
    return true;
retired:
    if (q1_alive(g, pack->id))
        q1_remove(g, pack, NULL);
    return true;
fail:
    if (q1_alive(g, pack->id))
        q1_remove(g, pack, NULL);
    return false;
}
static bool toss_weapon(qa_q1_game *g, qa_actor_id actor, const qa_q1_drop_input *input, bool rogue,
                        qa_actor_id *out, qa_error *error) {
    if (g->options.deathmatch != 1)
        return true;
    const drop_weapon *definition = NULL;
    for (size_t i = 0; i < sizeof(drop_weapons) / sizeof(*drop_weapons); ++i) {
        const drop_weapon *candidate = &drop_weapons[i];
        if (g->weapons[candidate->weapon] == input->selected_weapon ||
            (rogue && candidate->powered < QA_Q1_WEAPON_COUNT &&
             g->weapons[candidate->powered] == input->selected_weapon)) {
            definition = candidate;
            break;
        }
    }
    if (!definition)
        return true;
    double owned;
    if (!count_item(g, actor, g->weapons[definition->weapon], &owned, error))
        return false;
    if (owned < 1)
        return true;
    q1_actor *item;
    if (!q1_create(g, g->runtime_names[definition->classname], Q1_PICKUP, actor, &item, error))
        return false;
    item->state.pickup.drop = rogue ? Q1_DROP_ROGUE_WEAPON : Q1_DROP_CTF_WEAPON;
    item->state.pickup.weapon = definition->weapon;
    item->state.pickup.item = g->weapons[definition->weapon];
    item->state.pickup.owner_delay = 1;
    qa_body_state body;
    if (!q1_model(g, item, definition->model, error) ||
        !qa_builtin_resource(&g->services, definition->name, &item->message, error) ||
        !consume(g, actor, g->weapons[definition->weapon], 1, error))
        goto fail;
    if (!q1_alive(g, actor) || !q1_alive(g, item->id))
        goto retired;
    if (rogue && definition->powered < QA_Q1_WEAPON_COUNT) {
        double powered;
        if (!count_item(g, actor, g->weapons[definition->powered], &powered, error) ||
            (powered >= 1 && !consume(g, actor, g->weapons[definition->powered], 1, error)))
            goto fail;
        if (!q1_alive(g, actor) || !q1_alive(g, item->id))
            goto retired;
    }
    if (!launch_body(g, actor, input->view_angles, &body, error) ||
        !launch(g, item, &body, !rogue, error))
        goto fail;
    if (rogue) {
        q1_player *player = q1_player_get(g, actor);
        if (player && !qa_q1_player_select(g, actor, q1_best_weapon(g, player), error))
            goto fail;
    } else if (!selected_changed(g, actor, 0, error))
        goto fail;
    if (q1_alive(g, item->id))
        *out = item->id;
    return true;
retired:
    if (q1_alive(g, item->id))
        q1_remove(g, item, NULL);
    return true;
fail:
    if (q1_alive(g, item->id))
        q1_remove(g, item, NULL);
    return false;
}
bool qa_q1_ctf_toss_weapon(qa_q1_game *g, qa_actor_id actor, const qa_q1_drop_input *input,
                           qa_actor_id *out, qa_error *error) {
    return validate_drop(g, actor, input, out, error) &&
           toss_weapon(g, actor, input, false, out, error);
}
bool q1_rogue_toss(qa_q1_game *g, q1_player *player, bool weapon, qa_error *error) {
    if (g->options.teamplay < 1)
        return true;
    int ammo = q1_weapon_ammo(player->weapon);
    qa_q1_drop_input input = {.selected_weapon = g->weapons[player->weapon],
                              .selected_ammo = ammo < 0 ? 0 : g->ammo[ammo],
                              .view_angles = player->input.view_angles};
    qa_actor_id dropped = {0};
    if (weapon)
        return toss_weapon(g, player->id, &input, true, &dropped, error);
    if (ammo < 0 || q1_ammo_count(g, player->id, (qa_q1_ammo)ammo) <= 0)
        return true;
    float cargo[QA_Q1_AMMO_COUNT] = {0}, total = 0;
    for (size_t i = 0; i < sizeof(drop_ammunition) / sizeof(*drop_ammunition); ++i) {
        const drop_ammo *rule = &drop_ammunition[i];
        if (rule->selected[0] == player->weapon || rule->selected[1] == player->weapon) {
            if (!take_ammo(g, player->id, rule, &cargo[rule->ammo], error))
                return false;
            if (!q1_alive(g, player->id))
                return true;
        }
        bool missing;
        if (!missing_owners(g, player->id, rule, &missing, error))
            return false;
        if (missing && !take_ammo(g, player->id, rule, &cargo[rule->ammo], error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
        total += cargo[rule->ammo];
    }
    if (total == 0)
        return q1_message(g, player->id,
                          g->options.edition == QA_Q1_RERELEASE ? "$qc_no_ammo_available"
                                                                : "No ammo available!\n",
                          error);
    qa_body_state body;
    q1_actor *pack;
    return launch_body(g, player->id, input.view_angles, &body, error) &&
           q1_toss_backpack(g, player->id, body.origin, body.velocity, cargo, &pack, error);
}
static bool drop_backpack(qa_q1_game *g, qa_actor_id actor, qa_item_id selected,
                           qa_actor_id *out, qa_error *error) {
    if (!g || !out || !q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 backpack drop requires a live actor");
        return false;
    }
    *out = (qa_actor_id){0};
    if (!g->options.coop && g->options.deathmatch == 0)
        return true;
    q1_player *player = q1_player_get(g, actor);
    bool rogue = g->options.program == QA_Q1_ROGUE, hip = g->options.program == QA_Q1_HIPNOTIC;
    if ((rogue || hip) && !player)
        return true;
    qa_q1_weapon weapon = player ? player->weapon : QA_Q1_WEAPON_COUNT;
    if (!rogue && !hip)
        for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
            if (selected == g->weapons[i]) {
                weapon = (qa_q1_weapon)i;
                break;
            }
    float cargo[QA_Q1_AMMO_COUNT] = {0};
    double total = 0;
    unsigned count = rogue ? QA_Q1_AMMO_COUNT : 4;
    for (unsigned i = 0; i < count; ++i) {
        double amount;
        if (!count_item(g, actor, g->ammo[i], &amount, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (!isfinite(amount) || fabs(amount) > FLT_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, actor.slot,
                         "Q1 backpack cargo exceeds finite native storage");
            return false;
        }
        cargo[i] = (float)amount;
        if (i != QA_Q1_PLASMA_CELLS)
            total += cargo[i];
    }
    if (total == 0)
        return true;
    if (rogue)
        for (size_t i = 0; i < sizeof(drop_weapons) / sizeof(*drop_weapons); ++i)
            if (drop_weapons[i].powered == weapon && weapon != QA_Q1_WEAPON_COUNT) {
                weapon = drop_weapons[i].weapon;
                break;
            }
    if (hip && g->options.edition == QA_Q1_RERELEASE &&
        (weapon == QA_Q1_LASER || weapon == QA_Q1_MJOLNIR))
        cargo[QA_Q1_CELLS] = fmaxf(15, cargo[QA_Q1_CELLS]);
    qa_body_state body;
    q1_actor *pack;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!q1_spawn_backpack(g, actor, body.origin, weapon, cargo, &pack, error))
        return false;
    if (!pack)
        return true;
    pack->state.pickup.backpack_rank = hip;
    pack->state.pickup.avoid_underwater_lightning = hip || g->options.edition == QA_Q1_RERELEASE;
    pack->state.pickup.owner_delay = rogue ? 1 : 0;
    *out = pack->id;
    return true;
}
bool qa_q1_drop_backpack(qa_q1_game *g, qa_actor_id actor, qa_item_id selected,
                         qa_actor_id *out, qa_error *error) {
    qa_q1_game_operation operation;
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool okay = drop_backpack(g, actor, selected, out, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool touch_live(qa_q1_game *g, q1_actor *item, qa_actor_id actor) {
    return q1_alive(g, item->id) && q1_alive(g, actor);
}
static bool pickup_feedback(qa_q1_game *g, q1_actor *item, qa_actor_id actor, bool ammo, bool rogue,
                            qa_error *error) {
    qa_body_state body;
    if (!q1_sound_resource(g, actor, ammo ? g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_LOCK4_WAV] : g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_PKUP_WAV], 3, 1, 1, error))
        return false;
    if (!touch_live(g, item, actor))
        return true;
    if (!qa_world_body_read(g->services.world, rogue ? item->id : actor, &body, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_ITEM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = item->id,
                              .other = actor,
                              .origin = body.origin,
                              .resource = item->state.pickup.item,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error);
}
void q1_drop_offer(const qa_q1_game *g, const q1_actor *item, qa_actor_id actor,
                     qa_pickup_offer *offer, qa_pickup_cargo cargo[4]) {
    bool ammo = item->state.pickup.drop == Q1_DROP_CTF_AMMO;
    *offer = (qa_pickup_offer){.recipient = actor, .pickup = item->id,
                               .source = g->options.provider, .item = item->state.pickup.item,
                               .dropped = true, .time_ns = g->time_ns};
    if (ammo) {
        for (unsigned i = 0; i < 4; ++i)
            cargo[i] = (qa_pickup_cargo){g->ammo[i], item->state.pickup.ammo[i], false};
        offer->cargo = cargo;
        offer->cargo_count = 4;
    } else {
        offer->default_resource = (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY,
                                                       .item = item->state.pickup.item};
        offer->override_count = true;
        offer->count = 1;
    }
}
bool q1_drop_eligible(qa_q1_game *g, q1_actor *item, qa_actor_id actor) {
    bool rogue = item->state.pickup.drop == Q1_DROP_ROGUE_WEAPON;
    bool ammo = item->state.pickup.drop == Q1_DROP_CTF_AMMO;
    q1_player *player = q1_player_get(g, actor);
    qa_q1_target target;
    if (rogue ? player == NULL : !q1_target(g, actor, &target) || !target.player)
        return false;
    qa_builtin_actor_traits traits;
    if (!rogue && g->services.actor_traits &&
        g->services.actor_traits(g->services.context, actor, &traits) && traits.spectator)
        return false;
    if (ammo && q1_health(g, actor) <= 0)
        return false;
    if (!ammo && q1_ref_equal(item->owner, q1_ref_from(g, actor)) && item->next_think - g->time > 119)
        return false;
    return touch_live(g, item, actor);
}
typedef struct drop_touch {
    qa_q1_game *game;
    q1_actor *item;
    qa_actor_id actor;
    const drop_weapon *definition;
    bool original_ran;
} drop_touch;
static bool drop_eligible(void *context, const qa_pickup_offer *offer, bool *eligible,
                           qa_error *error) {
    (void)offer;
    (void)error;
    drop_touch *touch = context;
    *eligible = q1_drop_eligible(touch->game, touch->item, touch->actor);
    return true;
}
static bool drop_definition(drop_touch *touch, qa_error *error) {
    for (size_t i = 0; i < sizeof(drop_weapons) / sizeof(*drop_weapons); ++i)
        if (drop_weapons[i].weapon == touch->item->state.pickup.weapon) {
            touch->definition = &drop_weapons[i];
            return true;
        }
    qa_error_set(error, QA_ERROR_FORMAT, touch->item->id.slot,
                 "Q1 dropped weapon has no source definition");
    return false;
}
static bool drop_original(void *context, const qa_pickup_offer *offer, bool *taken,
                          qa_error *error) {
    (void)offer;
    drop_touch *touch = context;
    qa_q1_game *g = touch->game;
    q1_actor *item = touch->item;
    qa_actor_id actor = touch->actor;
    bool rogue = item->state.pickup.drop == Q1_DROP_ROGUE_WEAPON;
    bool ammo = item->state.pickup.drop == Q1_DROP_CTF_AMMO;
    touch->original_ran = true;
    *taken = true;
    if (ammo) {
        for (unsigned i = 0; i < 4; ++i) {
            double given;
            if (item->state.pickup.ammo[i] != 0 &&
                !qa_inventory_give(g->services.inventory, actor, g->ammo[i],
                                   item->state.pickup.ammo[i], &given, error))
                return false;
            if (!touch_live(g, item, actor))
                return true;
        }
    } else {
        if (!drop_definition(touch, error))
            return false;
        if (!rogue) {
            qa_inventory_entry entry = {.item = item->state.pickup.item,
                                        .count = 1,
                                        .capacity = 1,
                                        .policy = QA_COUNT_SOURCE_FLOAT};
            if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error))
                return false;
            if (!touch_live(g, item, actor))
                return true;
        }
    }
    return true;
}
static bool drop_complete(void *context, const qa_pickup_offer *offer, bool taken,
                          qa_error *error) {
    (void)offer;
    drop_touch *touch = context;
    qa_q1_game *g = touch->game;
    q1_actor *item = touch->item;
    qa_actor_id actor = touch->actor;
    if (!taken || !touch_live(g, item, actor))
        return true;
    bool rogue = item->state.pickup.drop == Q1_DROP_ROGUE_WEAPON;
    bool ammo = item->state.pickup.drop == Q1_DROP_CTF_AMMO;
    q1_player *player = q1_player_get(g, actor);
    if (!ammo) {
        if (!touch->definition && !drop_definition(touch, error))
            return false;
        const drop_weapon *definition = touch->definition;
        if (rogue && g->options.edition != QA_Q1_RERELEASE) {
            char message[128];
            snprintf(message, sizeof(message), "You got the %s\n",
                     definition->weapon == QA_Q1_NAILGUN ? "Nailgun" : definition->name);
            if (!q1_message(g, actor, message, error))
                return false;
        } else {
            qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING,
                                               .value.text = item->message};
            if (rogue && !qa_builtin_resource(&g->services, definition->localized,
                                              &argument.value.text, error))
                return false;
            if (!q1_message_args(g, actor, "$qc_got_item", &argument, 1, error))
                return false;
        }
        if (!touch_live(g, item, actor))
            return true;
    }
    if (!pickup_feedback(g, item, actor, ammo, rogue, error))
        return false;
    if (!touch_live(g, item, actor))
        return true;
    if (rogue) {
        qa_q1_weapon weapon = item->state.pickup.weapon;
        double given;
        if (touch->original_ran &&
            !qa_inventory_give(g->services.inventory, actor, item->state.pickup.item, 1, &given,
                               error))
            return false;
        if (!touch_live(g, item, actor))
            return true;
        if (!q1_remove(g, item, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (!touch->original_ran)
            return selected_changed(g, actor, 0, error);
        if ((!g->options.deathmatch ||
             q1_weapon_rank(g, weapon) < q1_weapon_rank(g, player->weapon)) &&
            !qa_q1_player_select(g, actor, weapon, error))
            return false;
        return !q1_alive(g, actor) || q1_enable_combos(g, player, error);
    }
    if (!selected_changed(g, actor, ammo ? 0 : item->state.pickup.item, error))
        return false;
    if (!touch_live(g, item, actor))
        return true;
    if (!ammo && g->services.use_targets &&
        !g->services.use_targets(g->services.context, item->id, actor, item->target,
                                 item->killtarget, item->delay, error))
        return false;
    return !q1_alive(g, item->id) || q1_remove(g, item, error);
}
bool q1_drop_touch(qa_q1_game *g, q1_actor *item, qa_actor_id actor, qa_error *error) {
    drop_touch touch = {.game = g, .item = item, .actor = actor};
    qa_pickup_cargo cargo[4];
    qa_pickup_offer offer;
    q1_drop_offer(g, item, actor, &offer, cargo);
    qa_pickup_continuation continuation = {.context = &touch, .eligible = drop_eligible,
                                           .original = drop_original, .complete = drop_complete};
    qa_pickup_outcome outcome;
    return qa_pickups_touch(g->services.pickups, &offer, &continuation, &outcome, error);
}

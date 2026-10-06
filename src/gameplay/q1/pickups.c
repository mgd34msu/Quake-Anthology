#include "internal.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_supply.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q1_source_rogue_runes.h"
#include <float.h>
#include <stdio.h>

enum {
    Q1_ITEM_HEALTH,
    Q1_ITEM_ARMOR,
    Q1_ITEM_AMMO,
    Q1_ITEM_WEAPON,
    Q1_ITEM_KEY,
    Q1_ITEM_POWER,
    Q1_ITEM_BACKPACK,
    Q1_ITEM_HORN,
    Q1_ITEM_SPHERE,
    Q1_ITEM_MG3_SHARD,
    Q1_ITEM_MG3_UPGRADE,
    Q1_ITEM_MG3_BLOODY
};
static bool mg3_special(const q1_pickup *item) {
    return item->kind >= Q1_ITEM_MG3_SHARD || item->weapon == QA_Q1_MG3_LASER ||
           item->weapon == QA_Q1_MG3_MJOLNIR ||
           (item->kind == Q1_ITEM_POWER && item->count == (float)QA_Q1_LAVA_SUIT);
}

int q1_weapon_rank(const qa_q1_game *g, qa_q1_weapon weapon) {
    static const qa_q1_weapon rank[] = {
        QA_Q1_LIGHTNING,     QA_Q1_ROCKET,  QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE,
        QA_Q1_SUPER_SHOTGUN, QA_Q1_NAILGUN, QA_Q1_SHOTGUN,       QA_Q1_AXE};
    static const qa_q1_weapon hip[] = {QA_Q1_LIGHTNING,     QA_Q1_ROCKET,    QA_Q1_LASER,
                                       QA_Q1_SUPER_NAILGUN, QA_Q1_PROXIMITY, QA_Q1_GRENADE,
                                       QA_Q1_SUPER_SHOTGUN, QA_Q1_NAILGUN,   QA_Q1_MJOLNIR};
    static const qa_q1_weapon rogue[] = {QA_Q1_PLASMA,
                                         QA_Q1_LIGHTNING,
                                         QA_Q1_MULTI_ROCKET,
                                         QA_Q1_ROCKET,
                                         QA_Q1_LAVA_SUPER_NAILGUN,
                                         QA_Q1_SUPER_NAILGUN,
                                         QA_Q1_MULTI_GRENADE,
                                         QA_Q1_GRENADE,
                                         QA_Q1_LAVA_NAILGUN,
                                         QA_Q1_SUPER_SHOTGUN,
                                         QA_Q1_NAILGUN};
    static const qa_q1_weapon mg3[] = {QA_Q1_LIGHTNING,     QA_Q1_ROCKET,  QA_Q1_MG3_LASER,
                                       QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE, QA_Q1_SUPER_SHOTGUN,
                                       QA_Q1_NAILGUN};
    const qa_q1_weapon *order = rank;
    size_t count = sizeof(rank) / sizeof(*rank);
    if (g->options.program == QA_Q1_HIPNOTIC) {
        order = hip;
        count = sizeof(hip) / sizeof(*hip);
    }
    if (g->options.program == QA_Q1_ROGUE) {
        order = rogue;
        count = sizeof(rogue) / sizeof(*rogue);
    }
    if (g->options.program == QA_Q1_MG3) {
        order = mg3;
        count = sizeof(mg3) / sizeof(*mg3);
    }
    for (size_t i = 0; i < count; ++i)
        if (order[i] == weapon)
            return (int)i;
    return (int)count;
}
static bool supply_weapons(void *context, qa_actor_id actor, const qa_item_id *items, size_t count,
                           qa_pickup_selection_mode selection, qa_error *error) {
    qa_q1_game *g = context;
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    if (!q1_enable_combos(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    for (size_t i = 0; i < count; ++i)
        for (unsigned weapon = 0; weapon < QA_Q1_WEAPON_COUNT; ++weapon) {
            if (items[i] != g->weapons[weapon])
                continue;
            qa_q1_weapon selected = q1_combo_weapon(g, player, (qa_q1_weapon)weapon);
            if (selection == QA_PICKUP_SWITCH_ALWAYS ||
                (selection == QA_PICKUP_SWITCH_IF_BETTER &&
                 q1_weapon_rank(g, selected) < q1_weapon_rank(g, player->weapon))) {
                if (!qa_q1_player_select(g, actor, selected, error))
                    return false;
                if (!q1_alive(g, actor))
                    return true;
            }
        }
    return q1_current_ammo_select(g, player, error);
}
static bool supply_ammo(void *context, qa_actor_id actor, const qa_pickup_receipt *receipts,
                        size_t count, bool auto_switch, qa_error *error) {
    qa_q1_game *g = context;
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    qa_q1_weapon before = q1_best_weapon_before(g, player, receipts, count);
    if (!q1_enable_combos(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!auto_switch || player->weapon != before)
        return q1_current_ammo_select(g, player, error);
    return qa_q1_player_select(g, actor, q1_best_weapon(g, player), error);
}
bool qa_q1_player_auto_switch_read(const qa_q1_game *g, qa_actor_id actor,
    qa_q1_auto_switch *out, qa_error *error) {
    if (!g || !out || g->destroy_pending || g->continuation_pending ||
        actor.slot >= g->capacity || !qa_actors_get(qa_session_actors(g->services.session), actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 pickup preference has no current source actor");
        return false;
    }
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !qa_actor_id_equal(player->id, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 pickup preference has no actual player");
        return false;
    }
    *out = player->auto_switch;
    return true;
}
static bool selected_player_current(qa_q1_game_operation *operation, qa_actor_id actor,
    const q1_player *player, qa_error *error) {
    if (player && qa_q1_game_operation_live(operation) && q1_player_get(operation->game, actor) == player &&
        player->arsenal) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Selected Q1 pickup lost its admitted arsenal player");
    return false;
}
bool qa_q1_source_arsenal_spawn_read(qa_q1_game *g, qa_actor_id actor,
    qa_q1_weapon *weapon, float *max_health, qa_q1_auto_switch *preference, qa_error *error) {
    if (!weapon || !max_health || !preference) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 source spawn requires complete output fields");
        return false;
    }
    uint32_t slot;
    if (!qa_q1_native_client_slot(g, actor, &slot, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    if (!player || !qa_q1_player_source_present(g, actor) ||
        player->weapon < QA_Q1_AXE || player->weapon >= QA_Q1_WEAPON_COUNT ||
        !isfinite(player->max_health) || player->max_health <= 0 ||
        player->auto_switch < QA_Q1_SWITCH_ALWAYS || player->auto_switch > QA_Q1_SWITCH_NEVER) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 source spawn has no valid physical player declaration");
        return false;
    }
    *weapon = player->weapon;
    *max_health = player->max_health;
    *preference = player->auto_switch;
    return true;
}
bool qa_q1_selected_arsenal_spawn(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
    float max_health, const qa_q1_auto_switch *source_preference, qa_error *error) {
    if (weapon < QA_Q1_AXE || weapon >= QA_Q1_WEAPON_COUNT ||
        !isfinite(max_health) || max_health <= 0 || (source_preference &&
        (*source_preference < QA_Q1_SWITCH_ALWAYS || *source_preference > QA_Q1_SWITCH_NEVER))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid selected Q1 spawn declaration");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    bool ok = selected_player_current(&operation, actor, player, error);
    qa_body_state body;
    if (ok) ok = qa_world_body_read(g->services.world, actor, &body, error) &&
        selected_player_current(&operation, actor, player, error);
    if (ok) {
        player->attack_finished = player->next_weapon_frame = player->lightning_sound_at = 0;
        player->animation_base = player->nail_side = 1;
        player->punch = qa_v3(0, 0, 0);
        player->input = (qa_q1_input){.view_angles = body.angles};
        player->primary_holstered = false;
        player->hostile_until = player->drown_at = player->hazard_at = 0;
        player->mega_rot_at = -1;
        player->source_superhealth = false;
        player->air_finished = g->time + 12;
        player->drown_damage = 2;
        q1_powers_forget(player);
        memset(player->power_flash, 0, sizeof(player->power_flash));
        player->power_warned = 0;
        player->power_lost = 0;
        player->max_health = max_health;
        player->auto_switch = source_preference ? *source_preference : QA_Q1_SWITCH_ALWAYS;
        ok = q1_player_select_read(g, actor, player, weapon, error) &&
            selected_player_current(&operation, actor, player, error);
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_selected_pickup_ammo(qa_q1_game *g, qa_actor_id actor,
    const qa_pickup_receipt *receipts, size_t count, bool auto_switch,
    const qa_q1_auto_switch *source_preference, qa_error *error) {
    if ((count && !receipts) || (source_preference &&
        (*source_preference < QA_Q1_SWITCH_ALWAYS || *source_preference > QA_Q1_SWITCH_NEVER))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid selected Q1 ammunition receipts");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    bool ok = selected_player_current(&operation, actor, player, error);
    qa_q1_weapon before, after;
    if (ok) ok = q1_enable_combos_read(g, actor, player, error) &&
        selected_player_current(&operation, actor, player, error) &&
        q1_best_weapon_before_read(g, actor, player, receipts, count, &before, error);
    if (ok && auto_switch && (source_preference ? *source_preference : player->auto_switch) != QA_Q1_SWITCH_NEVER &&
        player->weapon == before)
        ok = q1_best_weapon_before_read(g, actor, player, NULL, 0, &after, error) &&
            q1_player_select_read(g, actor, player, after, error) &&
            selected_player_current(&operation, actor, player, error);
    if (ok) ok = q1_current_ammo_select(g, player, error) &&
        selected_player_current(&operation, actor, player, error);
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_selected_pickup_weapons(qa_q1_game *g, qa_actor_id actor,
    const qa_item_id *items, size_t count, qa_pickup_selection_mode selection,
    const qa_q1_auto_switch *source_preference, qa_error *error) {
    if ((count && !items) || selection < QA_PICKUP_SWITCH_NEVER || selection > QA_PICKUP_SWITCH_IF_BETTER ||
        (source_preference && (*source_preference < QA_Q1_SWITCH_ALWAYS || *source_preference > QA_Q1_SWITCH_NEVER))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid selected Q1 weapon receipts");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    bool ok = player && selected_player_current(&operation, actor, player, error);
    if (!player) qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Selected Q1 pickup player is absent");
    if (ok) ok = q1_enable_combos_read(g, actor, player, error) && selected_player_current(&operation, actor, player, error);
    if (ok && (source_preference ? *source_preference : player->auto_switch) == QA_Q1_SWITCH_NEVER)
        selection = QA_PICKUP_SWITCH_NEVER;
    for (size_t i = 0; ok && i < count; ++i)
        for (unsigned weapon = 0; ok && weapon < QA_Q1_WEAPON_COUNT; ++weapon) {
            if (items[i] != g->weapons[weapon]) continue;
            if (selection == QA_PICKUP_SWITCH_ALWAYS ||
                (selection == QA_PICKUP_SWITCH_IF_BETTER &&
                 q1_weapon_rank(g, (qa_q1_weapon)weapon) < q1_weapon_rank(g, player->weapon)))
                ok = q1_player_select_read(g, actor, player, (qa_q1_weapon)weapon, error) &&
                    selected_player_current(&operation, actor, player, error);
        }
    if (ok) ok = q1_current_ammo_select(g, player, error) &&
        selected_player_current(&operation, actor, player, error);
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_pickup_supply_create(qa_q1_game *g, qa_error *error) {
    qa_supply_mapping weapons[QA_Q1_WEAPON_COUNT], ammo[QA_Q1_AMMO_COUNT];
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
        weapons[i] = (qa_supply_mapping){g->weapons[i], &g->weapons[i], 1};
    for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i)
        ammo[i] = (qa_supply_mapping){g->ammo[i], &g->ammo[i], 1};
    qa_supply_profile profile = {.weapons = weapons,
                                 .weapon_count = QA_Q1_WEAPON_COUNT,
                                 .ammo = ammo,
                                 .ammo_count = QA_Q1_AMMO_COUNT};
    qa_supply_hooks hooks = {
        .context = g, .ammo_granted = supply_ammo, .weapon_granted = supply_weapons};
    return qa_supply_create(g->services.inventory, &profile, &hooks, &g->source_supply, error);
}
static bool supply(qa_q1_game *g, qa_actor_id actor, qa_supply **out, qa_error *error) {
    qa_supply *selected = NULL;
    if (g->host.supply && !g->host.supply(g->host.context, actor, &selected, error)) return false;
    *out = selected ? selected : g->source_supply;
    return true;
}
enum { Q1_COOP_WEAPONS = 0x10ff };
static bool coop_weapons_read(qa_q1_game *g, qa_supply *selected, qa_actor_id actor,
                               uint32_t *out, qa_error *error) {
    *out = 0;
    for (unsigned shift = 0; shift <= 12; ++shift) {
        uint32_t bit = UINT32_C(1) << shift;
        qa_q1_weapon weapon;
        if (!(bit & Q1_COOP_WEAPONS) || !qa_q1_weapon_source(g->options.program, bit, &weapon))
            continue;
        bool owned;
        if (!qa_supply_owns(selected, actor, g->weapons[weapon], &owned, error))
            return false;
        if (owned)
            *out |= bit;
    }
    return true;
}
bool qa_q1_game_coop_weapons_read(qa_q1_game *g, qa_actor_id actor, uint32_t *out,
                                  qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_supply *selected;
    bool okay = supply(g, actor, &selected, error) &&
                coop_weapons_read(g, selected, actor, out, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool coop_ammo_quantity(void *context, const qa_inventory_entry *entry,
                                 qa_supply_quantity *out, qa_error *error) {
    (void)entry;
    (void)error;
    *out = (qa_supply_quantity){.amount = *(const double *)context, .exact = true,
                                .accepted = true};
    return true;
}
bool qa_q1_game_coop_weapons_grant(qa_q1_game *g, qa_actor_id actor, uint32_t weapons,
                                   qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_supply *selected;
    bool okay = supply(g, actor, &selected, error);
    for (unsigned shift = 0; okay && shift <= 12; ++shift) {
        uint32_t bit = UINT32_C(1) << shift;
        qa_q1_weapon weapon;
        if (!(bit & weapons & Q1_COOP_WEAPONS) ||
            !qa_q1_weapon_source(g->options.program, bit, &weapon))
            continue;
        qa_supply_offer offer = {.kind = QA_SUPPLY_WEAPON, .item = g->weapons[weapon]};
        qa_supply_options options = {.selection = QA_PICKUP_SWITCH_NEVER};
        bool accepted;
        okay = qa_supply_apply(selected, actor, &offer, &options, &accepted, error);
    }
    uint32_t owned = 0;
    if (okay)
        okay = coop_weapons_read(g, selected, actor, &owned, error);
    static const struct {
        uint32_t weapons;
        qa_q1_ammo ammo;
        double amount;
    } starting_ammo[] = {{4u | 8u, QA_Q1_NAILS, 30},
                        {16u | 32u, QA_Q1_ROCKETS, 4},
                        {64u, QA_Q1_CELLS, 12}};
    for (size_t i = 0; okay && i < sizeof(starting_ammo) / sizeof(*starting_ammo); ++i) {
        if (!(owned & starting_ammo[i].weapons))
            continue;
        double amount = starting_ammo[i].amount;
        qa_pickup_grant grant = {.item = g->ammo[starting_ammo[i].ammo], .amount = amount};
        qa_supply_offer offer = {.kind = QA_SUPPLY_AMMO, .ammo = &grant, .ammo_count = 1};
        qa_supply_options options = {.selection = QA_PICKUP_SWITCH_NEVER,
                                      .quantity = coop_ammo_quantity,
                                      .quantity_context = &amount};
        bool accepted;
        okay = qa_supply_apply(selected, actor, &offer, &options, &accepted, error);
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool weapon_leave(const qa_q1_game *g) {
    bool mission = g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE;
    return g->options.coop || g->options.deathmatch == 2 ||
           ((!mission || g->options.edition == QA_Q1_RERELEASE) &&
            (g->options.deathmatch == 3 || g->options.deathmatch == 5));
}
static bool item_name(qa_q1_game *g, q1_actor *entity, const char *name) {
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), entity->classname);
    size_t length = strlen(name);
    return text.size == length && !memcmp(text.data, name, length);
}
static uint32_t upgrade_flag(qa_q1_game *g) {
    const char *map =
        qa_strings_cstr(qa_session_strings(g->services.session), qa_q1_game_map_name(g));
    if (!map)
        return 0;
    static const char *const maps[] = {"map1",    "map2",    "map3",    "map4",    "map5",
                                       "map6",    "map7",    "map8",    "secret1", "secret2",
                                       "secret3", "secret4", "secret5", "map2b",   "secret6"};
    for (unsigned i = 0; i < sizeof(maps) / sizeof(*maps); ++i)
        if (!strcmp(map, maps[i]))
            return 1u << i;
    return 0;
}
bool q1_pickup_define(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_pickup *item = &entity->state.pickup;
    const char *model = NULL, *sound = NULL, *id = NULL;
    char model_buffer[96], id_buffer[128];
    bool big = (entity->spawnflags & (item_name(g, entity, "item_weapon") ? 8 : 1)) != 0;
    int bounds = 0;
    item->weapon = QA_Q1_WEAPON_COUNT;
    item->duration = 30;
    if (item_name(g, entity, "item_health")) {
        item->kind = Q1_ITEM_HEALTH;
        item->mega = !big && (entity->spawnflags & 2);
        item->count = big ? 15 : item->mega ? 100 : 25;
        item->respawn = item->mega ? 120 : 20;
        model = big ? "maps/b_bh10.bsp" : item->mega ? "maps/b_bh100.bsp" : "maps/b_bh25.bsp";
        sound = big ? "items/r_item1.wav" : item->mega ? "items/r_item2.wav" : "items/health1.wav";
    } else if (item_name(g, entity, "item_armor1") || item_name(g, entity, "item_armor2") ||
               item_name(g, entity, "item_armorInv")) {
        item->kind = Q1_ITEM_ARMOR;
        bounds = 1;
        entity->skin = item_name(g, entity, "item_armor1")   ? 0
                       : item_name(g, entity, "item_armor2") ? 1
                                                             : 2;
        item->absorption = entity->skin == 0 ? 0.3f : entity->skin == 1 ? 0.6f : 0.8f;
        item->count = entity->skin == 0 ? 100 : entity->skin == 1 ? 150 : 200;
        item->respawn = 20;
        model = "progs/armor.mdl";
        sound = "items/armor1.wav";
    } else if (item_name(g, entity, "item_key1") || item_name(g, entity, "item_key2")) {
        bool silver = item_name(g, entity, "item_key1");
        item->kind = Q1_ITEM_KEY;
        bounds = 1;
        item->respawn = -1;
        item->count = 1;
        id = silver ? "q1:key/silver" : "q1:key/gold";
        snprintf(model_buffer, sizeof(model_buffer), "progs/%c_%c_key.mdl",
                 g->options.world_type == 0   ? 'w'
                 : g->options.world_type == 1 ? 'm'
                                              : 'b',
                 silver ? 's' : 'g');
        model = model_buffer;
        sound = g->options.world_type == 2   ? "misc/basekey.wav"
                : g->options.world_type == 1 ? "misc/runekey.wav"
                                             : "misc/medkey.wav";
    } else if (item_name(g, entity, "item_backpack")) {
        item->kind = Q1_ITEM_BACKPACK;
        bounds = 1;
        item->respawn = -1;
        model = "progs/backpack.mdl";
        sound = "weapons/lock4.wav";
    } else {
        static const struct {
            const char *name, *model;
            qa_q1_weapon weapon;
        } weapons[] = {{"weapon_supershotgun", "g_shot", QA_Q1_SUPER_SHOTGUN},
                       {"weapon_nailgun", "g_nail", QA_Q1_NAILGUN},
                       {"weapon_supernailgun", "g_nail2", QA_Q1_SUPER_NAILGUN},
                       {"weapon_grenadelauncher", "g_rock", QA_Q1_GRENADE},
                       {"weapon_rocketlauncher", "g_rock2", QA_Q1_ROCKET},
                       {"weapon_lightning", "g_light", QA_Q1_LIGHTNING},
                       {"weapon_laser_gun", "g_laserg", QA_Q1_LASER},
                       {"weapon_mjolnir", "g_hammer", QA_Q1_MJOLNIR},
                       {"weapon_proximity_gun", "g_prox", QA_Q1_PROXIMITY}};
        for (size_t i = 0; i < sizeof(weapons) / sizeof(*weapons); ++i)
            if (item_name(g, entity, weapons[i].name)) {
                item->kind = Q1_ITEM_WEAPON;
                item->weapon = weapons[i].weapon;
                if (g->options.program == QA_Q1_MG3 && item->weapon == QA_Q1_LASER)
                    item->weapon = QA_Q1_MG3_LASER;
                if (g->options.program == QA_Q1_MG3 && item->weapon == QA_Q1_MJOLNIR)
                    item->weapon = QA_Q1_MG3_MJOLNIR;
                item->item = g->weapons[item->weapon];
                item->count = item->weapon == QA_Q1_NAILGUN || item->weapon == QA_Q1_SUPER_NAILGUN
                                  ? 30
                              : item->weapon == QA_Q1_LIGHTNING ? 15
                                                                : 5;
                if (item->weapon == QA_Q1_LASER || item->weapon == QA_Q1_MJOLNIR ||
                    item->weapon == QA_Q1_MG3_LASER || item->weapon == QA_Q1_MG3_MJOLNIR)
                    item->count = 30;
                if (item->weapon == QA_Q1_PROXIMITY)
                    item->count = 6;
                item->mission = item->weapon >= QA_Q1_LASER;
                bounds = 1;
                item->respawn = 30;
                sound = "weapons/pkup.wav";
                snprintf(model_buffer, sizeof(model_buffer), "progs/%s.mdl", weapons[i].model);
                model = model_buffer;
                break;
            }
        if (!model) {
            int ammo = -1;
            const char *prefix = NULL;
            if (item_name(g, entity, "item_weapon")) {
                if (entity->spawnflags & 2) {
                    ammo = QA_Q1_ROCKETS;
                    prefix = "rock";
                    item->count = big ? 10 : 5;
                } else if (entity->spawnflags & 4) {
                    ammo = QA_Q1_NAILS;
                    prefix = "nail";
                    item->count = big ? 40 : 20;
                } else if (entity->spawnflags & 1) {
                    ammo = QA_Q1_SHELLS;
                    prefix = "shell";
                    item->count = big ? 40 : 20;
                }
            } else if (item_name(g, entity, "item_shells")) {
                ammo = QA_Q1_SHELLS;
                prefix = "shell";
                item->count = big ? 40 : 20;
            } else if (item_name(g, entity, "item_spikes")) {
                ammo = QA_Q1_NAILS;
                prefix = "nail";
                item->count = big ? 50 : 25;
            } else if (item_name(g, entity, "item_rockets")) {
                ammo = QA_Q1_ROCKETS;
                prefix = "rock";
                item->count = big ? 10 : 5;
            } else if (item_name(g, entity, "item_cells")) {
                ammo = QA_Q1_CELLS;
                prefix = "batt";
                item->count = big ? 12 : 6;
            } else if (item_name(g, entity, "item_lava_spikes")) {
                ammo = QA_Q1_LAVA_NAILS;
                prefix = "lnail";
                item->count = big ? 50 : 25;
                item->mission = true;
            } else if (item_name(g, entity, "item_multi_rockets")) {
                ammo = QA_Q1_MULTI_ROCKETS;
                prefix = "mrock";
                item->count = big ? 10 : 5;
                item->mission = true;
            } else if (item_name(g, entity, "item_plasma")) {
                ammo = QA_Q1_PLASMA_CELLS;
                prefix = "plas";
                item->count = big ? 12 : 6;
                item->mission = true;
            }
            if (ammo >= 0) {
                item->kind = Q1_ITEM_AMMO;
                item->item = g->ammo[ammo];
                sound = "weapons/lock4.wav";
                item->respawn = g->options.deathmatch == 3 || g->options.deathmatch == 5 ? 15 : 30;
                if (item->mission && g->options.edition == QA_Q1_CLASSIC)
                    item->respawn = 30;
                snprintf(model_buffer, sizeof(model_buffer), "maps/b_%s%d.bsp", prefix,
                         big ? 1 : 0);
                model = model_buffer;
            }
        }
        if (!model) {
            static const struct {
                const char *name, *model, *sound;
                qa_q1_power power;
                float seconds;
                int bounds;
            } powers[] = {
                {"item_artifact_wetsuit", "wetsuit", "misc/weton.wav", QA_Q1_WETSUIT, 30, 2},
                {"item_artifact_empathy_shields", "empathy", "hipitems/empathy.wav", QA_Q1_EMPATHY,
                 30, 3},
                {"item_powerup_shield", "shield", "shield/pickup.wav", QA_Q1_SHIELD, 30, 2},
                {"item_powerup_belt", "beltup", "belt/pickup.wav", QA_Q1_ANTIGRAV, 45, 2},
                {NULL, "invulner", "items/protect.wav", QA_Q1_INVULNERABILITY, 30, 2},
                {NULL, "invisibl", "items/inv1.wav", QA_Q1_INVISIBILITY, 30, 2},
                {NULL, "quaddama", "items/damage.wav", QA_Q1_QUAD, 30, 2}};
            int chosen = -1;
            item->random = item_name(g, entity, "item_random_powerup");
            if (item->random) {
                float value = q1_random(g);
                chosen = value < 0.2f   ? 2
                         : value < 0.4f ? 3
                         : value < 0.6f ? 4
                         : value < 0.8f ? 5
                                        : 6;
            } else
                for (unsigned i = 0; i < 4; ++i)
                    if (item_name(g, entity, powers[i].name)) {
                        chosen = (int)i;
                        break;
                    }
            if (chosen >= 0) {
                item->kind = Q1_ITEM_POWER;
                item->count = (float)powers[chosen].power;
                item->duration = powers[chosen].seconds;
                item->mission = item->artifact = true;
                item->respawn = item->random && chosen >= 4 ? 30 : 60;
                bounds = powers[chosen].bounds;
                sound = powers[chosen].sound;
                snprintf(model_buffer, sizeof(model_buffer), "progs/%s.mdl", powers[chosen].model);
                model = model_buffer;
            } else if (item_name(g, entity, "item_hornofconjuring")) {
                item->kind = Q1_ITEM_HORN;
                item->mission = item->artifact = true;
                item->respawn = 60;
                bounds = 3;
                model = "progs/horn.mdl";
                sound = "hipitems/horn.wav";
            } else if (item_name(g, entity, "item_sphere")) {
                item->kind = Q1_ITEM_SPHERE;
                item->mission = item->artifact = true;
                item->respawn = 180;
                bounds = 4;
                model = "progs/sphere.mdl";
                sound = "sphere/sphere.wav";
                entity->physics.angular_velocity = qa_v3(40, 40, 40);
            }
        }
        if (!model) {
            static const struct {
                const char *name, *model, *sound;
                qa_q1_power power;
            } powers[] = {
                {"item_artifact_invulnerability", "invulner", "protect", QA_Q1_INVULNERABILITY},
                {"item_artifact_invisibility", "invisibl", "inv1", QA_Q1_INVISIBILITY},
                {"item_artifact_envirosuit", "suit", "suit", QA_Q1_SUIT},
                {"item_artifact_super_damage", "quaddama", "damage", QA_Q1_QUAD}};
            for (size_t i = 0; i < sizeof(powers) / sizeof(*powers); ++i)
                if (item_name(g, entity, powers[i].name)) {
                    item->kind = Q1_ITEM_POWER;
                    item->count = (float)powers[i].power;
                    item->artifact = true;
                    bounds = 2;
                    item->respawn = powers[i].power == QA_Q1_INVULNERABILITY ||
                                            powers[i].power == QA_Q1_INVISIBILITY
                                        ? 300
                                        : 60;
                    snprintf(model_buffer, sizeof(model_buffer), "progs/%s.mdl", powers[i].model);
                    model = model_buffer;
                    snprintf(id_buffer, sizeof(id_buffer), "items/%s.wav", powers[i].sound);
                    if (!qa_builtin_resource(&g->services, id_buffer, &item->sound, error))
                        return false;
                    break;
                }
        }
    }
    if (!model && g->options.program == QA_Q1_MG3) {
        bounds = 1;
        if (item_name(g, entity, "item_armor_shard")) {
            item->kind = Q1_ITEM_MG3_SHARD;
            model = "progs/armorshard.mdl";
            sound = "items/armor1.wav";
        } else if (item_name(g, entity, "weapon_bloody_sg") ||
                   item_name(g, entity, "weapon_bloody_ssg")) {
            item->kind = Q1_ITEM_MG3_BLOODY;
            item->weapon =
                item_name(g, entity, "weapon_bloody_sg") ? QA_Q1_SHOTGUN : QA_Q1_SUPER_SHOTGUN;
            model =
                item->weapon == QA_Q1_SHOTGUN ? "progs/g_bloodshot.mdl" : "progs/g_bloodshot2.mdl";
            sound = "weapons/pkup.wav";
        } else if (item_name(g, entity, "item_artifact_lavasuit")) {
            item->kind = Q1_ITEM_POWER;
            item->count = QA_Q1_LAVA_SUIT;
            item->artifact = true;
            bounds = 2;
            model = "progs/lavasuit.mdl";
            sound = "items/suit.wav";
            qa_actor_id actor = entity->id;
            if (!qa_q1_wire_declare_model(g, model, error) ||
                !q1_alive(g, actor) || q1_entity(g, actor) != entity ||
                !qa_q1_wire_declare_sound(g, "items/suit.wav", error) ||
                !q1_alive(g, actor) || q1_entity(g, actor) != entity ||
                !qa_q1_wire_declare_sound(g, "items/suit2.wav", error) ||
                !q1_alive(g, actor) || q1_entity(g, actor) != entity) {
                if (!error || error->code == QA_OK)
                    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "MG3 lava suit retired during its source precache");
                return false;
            }
        } else {
            static const char *const names[] = {"item_upgrade_health", "item_upgrade_shells",
                                                "item_upgrade_nails", "item_upgrade_rockets",
                                                "item_upgrade_cells"};
            static const char *const models[] = {"item_h_player", "backpackshells", "backpacknails",
                                                 "backpacker", "backpackcells"};
            for (unsigned i = 0; i < 5; ++i)
                if (item_name(g, entity, names[i])) {
                    item->kind = Q1_ITEM_MG3_UPGRADE;
                    item->upgrade = (uint8_t)i;
                    if (!item->upgrade_flag)
                        item->upgrade_flag = upgrade_flag(g);
                    snprintf(model_buffer, sizeof(model_buffer), "progs/%s.mdl", models[i]);
                    model = model_buffer;
                    sound = i == 0 ? "player/tornoff2.wav" : "weapons/lock4.wav";
                    break;
                }
        }
    }
    if (!model) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, entity->id.slot,
                     "Q1 classname has no native pickup definition");
        return false;
    }
    if (!item->item) {
        if (!id) {
            qa_bytes name =
                qa_strings_text(qa_session_strings(g->services.session), entity->classname);
            if (name.size + 4 > sizeof(id_buffer)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, name.size, "Q1 item identity too long");
                return false;
            }
            memcpy(id_buffer, "q1:", 3);
            memcpy(id_buffer + 3, name.data, name.size);
            id_buffer[name.size + 3] = 0;
            id = id_buffer;
        }
        if (!qa_builtin_resource(&g->services, id, &item->item, error))
            return false;
    }
    if (!q1_model(g, entity, model, error) ||
        (sound && !qa_builtin_resource(&g->services, sound, &item->sound, error)))
        return false;
    item->original_model = entity->model;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    static const qa_bounds shapes[] = {{{0, 0, 0}, {32, 32, 56}},
                                       {{-16, -16, 0}, {16, 16, 56}},
                                       {{-16, -16, -24}, {16, 16, 32}},
                                       {{-16, -16, 0}, {16, 16, 32}},
                                       {{-8, -8, -8}, {8, 8, 8}}};
    body.bounds = shapes[bounds];
    if (item->kind == Q1_ITEM_MG3_UPGRADE && item->upgrade == 0) {
        body.origin.z += 8;
        body.bounds = (qa_bounds){{-16, -16, -8}, {16, 16, 48}};
    }
    qa_actor_id actor = entity->id;
    if (!qa_world_body_write(g->services.world, actor, &body, error))
        return false;
    return !q1_alive(g, actor) || q1_pickup_observe(g, entity, error);
}

bool q1_pickup_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    entity->kind = Q1_PICKUP;
    if (g->options.deathmatch == 0 &&
        (item_name(g, entity, "item_sphere") || item_name(g, entity, "item_random_powerup")))
        return q1_remove(g, entity, error);
    if (!q1_pickup_define(g, entity, error))
        return false;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    if (entity->state.pickup.kind != Q1_ITEM_BACKPACK) {
        bool delayed = entity->state.pickup.kind == Q1_ITEM_MG3_UPGRADE ||
                       entity->state.pickup.kind == Q1_ITEM_MG3_BLOODY;
        return q1_schedule(g, entity, delayed ? 0.5 : 0.2,
                           delayed ? Q1_THINK_MG3_ITEM_START : Q1_THINK_ITEM_PLACE, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float x = -100 + q1_random(g) * 200, y = -100 + q1_random(g) * 200;
    body.velocity = qa_v3(x, y, 300);
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    entity->physics.motion = QA_PHYSICS_TOSS;
    entity->physics.flags = 0;
    entity->source_movement_flags = UINT32_C(256);
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error);
}

typedef struct item_touch {
    qa_q1_game *game;
    q1_actor *entity;
    qa_actor_id recipient;
    qa_pickup_cargo cargo[QA_Q1_AMMO_COUNT + 1];
    size_t cargo_count;
    bool original_ran, leave, external;
} item_touch;

static size_t backpack_cargo(const qa_q1_game *g, const q1_pickup *item,
                             qa_pickup_cargo cargo[QA_Q1_AMMO_COUNT + 1]) {
    size_t count = 0;
    for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i) {
        /* Stock backpacks have four ammo fields. Keep Rogue's declared fields,
           and any actual extra cargo supplied by a foreign source. */
        if (i >= QA_Q1_LAVA_NAILS && g->options.program != QA_Q1_ROGUE && item->ammo[i] == 0)
            continue;
        cargo[count++] = (qa_pickup_cargo){g->ammo[i], item->ammo[i], false};
    }
    if (item->weapon < QA_Q1_WEAPON_COUNT)
        cargo[count++] = (qa_pickup_cargo){g->weapons[item->weapon], 1, true};
    return count;
}

static bool touch_live(const item_touch *touch) {
    return q1_alive(touch->game, touch->entity->id) && q1_alive(touch->game, touch->recipient);
}
static bool item_eligible(void *context, const qa_pickup_offer *offer, bool *eligible,
                          qa_error *error) {
    (void)offer;
    item_touch *touch = context;
    qa_q1_game *g = touch->game;
    q1_pickup *item = &touch->entity->state.pickup;
    qa_q1_target target;
    *eligible = touch_live(touch) &&
                (item->kind == Q1_ITEM_HORN || q1_health(g, touch->recipient) > 0) &&
                q1_target(g, touch->recipient, &target) && target.player;
    if (*eligible && item->kind == Q1_ITEM_WEAPON && weapon_leave(g)) {
        bool owned;
        qa_supply *selected;
        if (!supply(g, touch->recipient, &selected, error) ||
            !qa_supply_owns(selected, touch->recipient, item->item, &owned,
                            error))
            return false;
        *eligible = !owned;
    }
    return true;
}
static bool item_original(void *context, const qa_pickup_offer *offer, bool *taken,
                          qa_error *error) {
    item_touch *touch = context;
    qa_q1_game *g = touch->game;
    q1_pickup *item = &touch->entity->state.pickup;
    qa_actor_id actor = touch->recipient;
    q1_player *player = q1_player_get(g, actor);
    touch->original_ran = true;
    *taken = false;
    switch (item->kind) {
    case Q1_ITEM_HEALTH: {
        float health = q1_health(g, actor), limit = item->mega ? 250
                                                    : player   ? player->max_health
                                                               : 100;
        qa_builtin_actor_traits traits;
        if (!item->mega && g->services.actor_traits &&
            g->services.actor_traits(g->services.context, actor, &traits))
            limit = traits.max_health;
        if (health <= 0 || health >= limit)
            return true;
        *taken = true;
        return qa_combat_set_health(g->services.combat, actor, fminf(limit, health + item->count),
                                    error);
    }
    case Q1_ITEM_ARMOR: {
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, actor, &combat, error))
            return false;
        qa_regular_armor armor = combat.armor.regular;
        if (armor.kind == QA_ARMOR_SOURCE)
            return true;
        double protection = armor.kind == QA_ARMOR_Q1   ? armor.protection.q1_absorption
                           : armor.kind == QA_ARMOR_Q2 ? armor.protection.q2.normal
                           : armor.kind == QA_ARMOR_Q3 ? armor.protection.q3_protection
                                                       : 0;
        if (armor.points * protection >= (double)item->count * item->absorption)
            return true;
        armor = (qa_regular_armor){.kind = QA_ARMOR_Q1,
                                   .points = item->count,
                                   .item = item->item,
                                   .protection.q1_absorption = item->absorption};
        *taken = true;
        return qa_combat_set_regular_armor(g->services.combat, actor, &armor, error);
    }
    case Q1_ITEM_AMMO: {
        qa_supply *selected;
        if (!supply(g, actor, &selected, error)) return false;
        return qa_supply_ammo(selected, actor, (qa_pickup_grant){item->item, item->count},
                              (item->mission && g->options.edition == QA_Q1_CLASSIC) || !player ||
                                  player->auto_switch != QA_Q1_SWITCH_NEVER,
                              taken, error);
    }
    case Q1_ITEM_WEAPON: {
        int ammo = q1_weapon_ammo(item->weapon);
        if (item->weapon == QA_Q1_MJOLNIR || item->weapon == QA_Q1_MG3_MJOLNIR)
            ammo = QA_Q1_CELLS;
        qa_pickup_grant grant = {ammo >= 0 ? g->ammo[ammo] : 0, item->count};
        qa_supply_offer weapon = {.kind = QA_SUPPLY_WEAPON,
                                  .item = item->item,
                                  .ammo = &grant,
                                  .ammo_count = ammo >= 0 ? 1 : 0};
        qa_supply_options options = {.selection = g->options.deathmatch ? QA_PICKUP_SWITCH_IF_BETTER
                                                                        : QA_PICKUP_SWITCH_ALWAYS};
        bool owned;
        qa_supply *selected;
        if (!supply(g, actor, &selected, error) ||
            !qa_supply_owns(selected, actor, item->item, &owned, error))
            return false;
        bool classic_mission =
            g->options.edition == QA_Q1_CLASSIC &&
            (g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE);
        if (player && ((!classic_mission && (player->auto_switch == QA_Q1_SWITCH_NEVER ||
                                             (player->auto_switch == QA_Q1_SWITCH_NEW && owned))) ||
                       (g->options.program == QA_Q1_ROGUE &&
                        player->weapon == QA_Q1_ROGUE_GRAPPLE && player->input.attack)))
            options.selection = QA_PICKUP_SWITCH_NEVER;
        touch->leave = weapon_leave(g);
        if (!supply(g, actor, &selected, error)) return false;
        return qa_supply_apply(selected, actor, &weapon, &options, taken, error);
    }
    case Q1_ITEM_KEY: {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, actor, item->item, &entry, NULL)) {
            entry = (qa_inventory_entry){
                .item = item->item, .capacity = 1, .policy = QA_COUNT_SOURCE_FLOAT};
            if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error))
                return false;
        }
        double given;
        if (!qa_inventory_give(g->services.inventory, actor, item->item, 1, &given, error))
            return false;
        *taken = given != 0;
        touch->leave = g->options.coop;
        return true;
    }
    case Q1_ITEM_POWER:
        *taken = true;
        return q1_power_give(g, actor, (qa_q1_power)(unsigned)item->count,
            item->duration, error);
    case Q1_ITEM_HORN:
        *taken = true;
        return true;
    case Q1_ITEM_SPHERE:
        return q1_sphere_pickup(g, touch->entity, actor, taken, error);
    case Q1_ITEM_BACKPACK: {
        qa_pickup_selection_mode selection = QA_PICKUP_SWITCH_NEVER;
        qa_supply *selected;
        if (item->weapon < QA_Q1_WEAPON_COUNT) {
            bool owned;
            if (!supply(g, actor, &selected, error) ||
                !qa_supply_owns(selected, actor, g->weapons[item->weapon], &owned, error))
                return false;
            bool auto_switch = !player || g->options.edition != QA_Q1_RERELEASE ||
                               player->auto_switch == QA_Q1_SWITCH_ALWAYS ||
                               (player->auto_switch == QA_Q1_SWITCH_NEW && !owned);
            bool always = !item->backpack_rank && g->options.edition != QA_Q1_RERELEASE &&
                          g->options.deathmatch == 0;
            bool underwater = !always && player && item->avoid_underwater_lightning &&
                              player->input.water_level != 0 && item->weapon == QA_Q1_LIGHTNING;
            if (auto_switch && !underwater)
                selection = always ? QA_PICKUP_SWITCH_ALWAYS : QA_PICKUP_SWITCH_IF_BETTER;
        }
        if (!supply(g, actor, &selected, error)) return false;
        return qa_supply_cargo(selected, actor, offer->cargo, offer->cargo_count, selection,
                               false, taken, error);
    }
    case Q1_ITEM_MG3_SHARD: {
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, actor, &combat, error))
            return false;
        qa_regular_armor armor = combat.armor.regular;
        if (armor.kind == QA_ARMOR_SOURCE)
            return true;
        if (armor.kind == QA_ARMOR_Q1 && armor.protection.q1_absorption < 0.3f) {
            armor.protection.q1_absorption = 0.3f;
            if (!qa_combat_set_regular_armor(g->services.combat, actor, &armor, error))
                return false;
        }
        if (armor.points >= 200)
            return true;
        qa_string_id identity = armor.item;
        if (armor.kind != QA_ARMOR_Q1 &&
            !qa_builtin_resource(&g->services, "q1:item_armor1", &identity, error))
            return false;
        armor = (qa_regular_armor){
            .kind = QA_ARMOR_Q1,
            .item = identity,
            .points = fmin(200, armor.points + 5),
            .protection.q1_absorption =
                armor.kind == QA_ARMOR_Q1 ? fmaxf(0.3f, armor.protection.q1_absorption) : 0.3f};
        *taken = true;
        return qa_combat_set_regular_armor(g->services.combat, actor, &armor, error);
    }
    case Q1_ITEM_MG3_UPGRADE:
    case Q1_ITEM_MG3_BLOODY:
        break;
    }
    qa_error_set(error, QA_ERROR_FORMAT, item->kind, "unknown Q1 pickup kind");
    return false;
}
static bool dispatch_targets(item_touch *touch, qa_error *error) {
    qa_q1_game *g = touch->game;
    q1_actor *entity = touch->entity;
    if (!touch_live(touch))
        return true;
    if (!qa_strings_text(qa_session_strings(g->services.session), entity->target).size &&
        !qa_strings_text(qa_session_strings(g->services.session), entity->killtarget).size)
        return true;
    if (!g->services.use_targets) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "Q1 pickup target dispatcher missing");
        return false;
    }
    return g->services.use_targets(g->services.context, entity->id, touch->recipient,
                                   entity->target, entity->killtarget, entity->delay, error);
}
static bool item_complete(void *context, const qa_pickup_offer *offer, bool taken,
                          qa_error *error) {
    (void)offer;
    item_touch *touch = context;
    qa_q1_game *g = touch->game;
    q1_actor *entity = touch->entity;
    if (!taken || !touch_live(touch))
        return true;
    q1_pickup *item = &entity->state.pickup;
    if (!touch->original_ran)
        touch->leave = item->kind == Q1_ITEM_WEAPON ? weapon_leave(g)
                                                    : item->kind == Q1_ITEM_KEY && g->options.coop;
    q1_player *player = q1_player_get(g, touch->recipient);
    bool player_rot = g->options.program == QA_Q1_ID1 && g->options.edition == QA_Q1_RERELEASE;
    if (player_rot && item->mega && player) {
        player->mega_rot_at = g->time + 5;
        player->source_superhealth = true;
    }
    qa_builtin_event sound = {
        .kind = QA_BUILTIN_SOUND,
        .family = QA_GAME_Q1,
        .provider = g->options.provider,
        .actor = touch->recipient,
        .time_ns = g->time_ns,
        .resource = item->sound,
        .channel =
            item->kind == Q1_ITEM_POWER ||
                    (item->mission && item->kind != Q1_ITEM_AMMO && item->kind != Q1_ITEM_WEAPON)
                ? 2 : 3,
        .volume = 1,
        .attenuation = item->kind == Q1_ITEM_HORN ? 0 : 1};
    if (!qa_builtin_emit(&g->services, &sound, error))
        return false;
    if (!touch_live(touch))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_ITEM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = entity->id,
                              .other = touch->recipient,
                              .origin = body.origin,
                              .resource = item->item,
                              .time_ns = g->time_ns};
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    if (!touch_live(touch))
        return true;
    if (touch->external)
        return true;
    if (mg3_special(item)) {
        entity->activator = q1_ref_from(g, touch->recipient);
        if (item->kind >= Q1_ITEM_MG3_SHARD) {
            if (!dispatch_targets(touch, error))
                return false;
            return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
        }
        bool lava = item->kind == Q1_ITEM_POWER;
        if (!lava) {
            if (!dispatch_targets(touch, error))
                return false;
            if (!touch_live(touch))
                return true;
            if (touch->leave) {
                entity->target = 0;
                return true;
            }
        }
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        entity->model = 0;
        item->hidden = true;
        if (!q1_link(g, entity, error))
            return false;
        if (lava && !dispatch_targets(touch, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (lava && g->options.coop)
            entity->target = 0;
        double respawn = lava ? (g->options.coop         ? 2.5
                                 : g->options.deathmatch ? 60
                                                         : entity->wait)
                         : g->options.deathmatch && g->options.deathmatch != 2 ? 30
                                                                               : entity->wait;
        if (respawn > 0)
            return q1_schedule(g, entity, respawn, Q1_THINK_RESPAWN, error);
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
        entity->think = Q1_THINK_NONE;
        entity->next_think = 0;
        return true;
    }
    if (item->kind == Q1_ITEM_BACKPACK)
        return q1_remove(g, entity, error);
    if (touch->leave)
        return item->kind == Q1_ITEM_WEAPON || dispatch_targets(touch, error);
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->model = 0;
    item->hidden = true;
    if (!q1_link(g, entity, error))
        return false;
    if (!touch_live(touch))
        return true;
    bool respawns = g->options.deathmatch && item->respawn > 0 &&
                    (g->options.deathmatch != 2 || item->artifact);
    bool mission = g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE;
    if (mission && g->options.edition == QA_Q1_CLASSIC && g->options.deathmatch != 1 &&
        (item->kind == Q1_ITEM_WEAPON || item->kind == Q1_ITEM_AMMO || item->kind == Q1_ITEM_ARMOR))
        respawns = false;
    bool item_rot = item->mega && !player_rot;
    if (item_rot && g->options.program == QA_Q1_ROGUE) {
        uint32_t rune;
        bool found;
        if (!qa_q1_source_rogue_runes_read(g, touch->recipient, &rune, &found, error)) return false;
        item_rot = !(rune & 8u);
    }
    if (item_rot) {
        if (player && g->options.program != QA_Q1_HIPNOTIC) player->source_superhealth = true;
        item->holder = q1_ref_from(g, touch->recipient);
        if (!q1_schedule(g, entity, 5, Q1_THINK_MEGA_ROT, error))
            return false;
    } else if (respawns) {
        if (!q1_schedule(g, entity, item->respawn, Q1_THINK_RESPAWN, error))
            return false;
    } else {
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
        if (g->options.program == QA_Q1_ID1 && g->options.edition == QA_Q1_CLASSIC)
            entity->think = item->kind <= Q1_ITEM_WEAPON ? Q1_THINK_RESPAWN : entity->think;
        else entity->think = Q1_THINK_NONE;
    }
    if (item->kind == Q1_ITEM_HORN) {
        qa_actor_id previous = q1_ref_actor(g, g->horn_charmer);
        g->horn_charmer = q1_ref_from(g, touch->recipient);
        bool result = dispatch_targets(touch, error);
        g->horn_charmer = q1_ref_from(g, previous);
        return result;
    }
    return dispatch_targets(touch, error);
}

static bool pickup_grant(qa_q1_game *g, q1_actor *entity, qa_actor_id recipient, bool external,
                         bool *accepted, qa_error *error) {
    *accepted = false;
    if (entity->physics.solid != QA_PHYSICS_TRIGGER)
        return true;
    q1_pickup *item = &entity->state.pickup;
    item_touch touch = {.game = g, .entity = entity, .recipient = recipient, .external = external};
    if (item->kind == Q1_ITEM_BACKPACK)
        touch.cargo_count = backpack_cargo(g, item, touch.cargo);
    qa_pickup_offer offer = {.recipient = recipient,
                             .pickup = entity->id,
                             .source = g->options.provider,
                             .item = item->item,
                             .override_count = entity->count != 0,
                             .count = entity->count,
                             .dropped = item->kind == Q1_ITEM_BACKPACK,
                             .time_ns = g->time_ns,
                             .cargo = touch.cargo,
                             .cargo_count = touch.cargo_count};
    if (item->kind == Q1_ITEM_AMMO || item->kind == Q1_ITEM_WEAPON || item->kind == Q1_ITEM_KEY)
        offer.default_resource =
            (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY, .item = item->item};
    else if (item->kind == Q1_ITEM_ARMOR || item->kind == Q1_ITEM_MG3_SHARD)
        offer.default_resource =
            (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION, .channel = QA_PROTECTION_REGULAR};
    qa_pickup_continuation continuation = {.context = &touch,
                                           .eligible = item_eligible,
                                           .original = item_original,
                                           .complete = item_complete};
    qa_pickup_outcome outcome;
    if (!qa_pickups_touch(g->services.pickups, &offer, &continuation, &outcome, error))
        return false;
    *accepted = outcome == QA_PICKUP_ACCEPTED;
    return true;
}
static bool mg3_upgrade_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id recipient,
    q1_player *player, qa_error *error) {
    q1_pickup *item = &entity->state.pickup;
    static const char *const labels[] = {"$mg3_qc_upgrade_health", "$mg3_qc_upgrade_shell",
        "$mg3_qc_upgrade_nail", "$mg3_qc_upgrade_rocket", "$mg3_qc_upgrade_cell"};
    if (item->upgrade >= sizeof(labels) / sizeof(*labels)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot, "MG3 upgrade lacks its actual source type");
        return false;
    }
    bool collected;
    float maximum;
    if (!q1_mg3_upgrade(g, player, item->upgrade, item->upgrade_flag,
        &collected, &maximum, error)) return false;
    item_touch touch = {.game = g, .entity = entity, .recipient = recipient, .original_ran = true};
    if (!touch_live(&touch)) return true;
    qa_string_id label;
    if (!qa_builtin_resource(&g->services, labels[item->upgrade], &label, error)) return false;
    if (!touch_live(&touch)) return true;
    qa_builtin_message_arg arguments[] = {
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = label},
        {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = maximum}};
    if (!q1_message_args(g, recipient, collected ? "$mg3_qc_upgrade_fail" :
        "$mg3_qc_upgrade_success", arguments, collected ? 1 : 2, error)) return false;
    return !touch_live(&touch) || item_complete(&touch, NULL, true, error);
}

bool q1_mg3_debug_upgrade(qa_q1_game *g, qa_actor_id recipient, unsigned type,
    uint32_t flag, qa_error *error) {
    static const char *const classnames[] = {"item_upgrade_health", "item_upgrade_shells",
        "item_upgrade_nails", "item_upgrade_rockets", "item_upgrade_cells"};
    q1_player *player = q1_player_get(g, recipient);
    if (g->options.program != QA_Q1_MG3 || !player || !q1_alive(g, recipient) ||
        type >= sizeof(classnames) / sizeof(*classnames)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, recipient.slot, "MG3 debug upgrade lacks its actual player/type");
        return false;
    }
    q1_actor *entity;
    if (!q1_create(g, classnames[type], Q1_PICKUP, (qa_actor_id){0}, &entity, error)) return false;
    qa_actor_id created = entity->id;
    if (g->destroy_pending || q1_player_get(g, recipient) != player || !q1_alive(g, recipient) ||
        q1_entity(g, created) != entity || !q1_alive(g, created)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, recipient.slot, "MG3 upgrade creation retired its player");
        return false;
    }
    entity->state.pickup.kind = Q1_ITEM_MG3_UPGRADE;
    entity->state.pickup.upgrade = (uint8_t)type;
    entity->state.pickup.upgrade_flag = flag;
    qa_string_id sound;
    if (!qa_builtin_resource(&g->services, type == 0 ? "player/tornoff2.wav" :
        "weapons/lock4.wav", &sound, error)) return false;
    if (g->destroy_pending || q1_player_get(g, recipient) != player || !q1_alive(g, recipient) ||
        q1_entity(g, created) != entity || !q1_alive(g, created)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, recipient.slot, "MG3 upgrade resource admission retired its source binding");
        return false;
    }
    entity->state.pickup.sound = sound;
    return mg3_upgrade_touch(g, entity, recipient, player, error);
}

bool q1_pickup_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id recipient, qa_error *error) {
    if (entity->state.pickup.external)
        return true;
    if (entity->state.pickup.drop != Q1_DROP_NONE)
        return q1_drop_touch(g, entity, recipient, error);
    q1_pickup *item = &entity->state.pickup;
    if (q1_ref_equal(entity->owner, q1_ref_from(g, recipient)) &&
        entity->next_think - g->time > 120 - item->owner_delay)
        return true;
    if (item->kind == Q1_ITEM_MG3_UPGRADE || item->kind == Q1_ITEM_MG3_BLOODY) {
        q1_player *player = q1_player_get(g, recipient);
        if (!player || entity->physics.solid != QA_PHYSICS_TRIGGER)
            return true;
        if (item->kind == Q1_ITEM_MG3_UPGRADE) {
            return mg3_upgrade_touch(g, entity, recipient, player, error);
        } else {
            player->mg3_progress.bloody |= item->weapon == QA_Q1_SHOTGUN ? 1u : 2u;
            double given;
            qa_inventory_entry entry = {.item = g->weapons[item->weapon],
                                        .count = 1,
                                        .capacity = 1,
                                        .policy = QA_COUNT_SOURCE_FLOAT};
            if (!qa_inventory_give(g->services.inventory, recipient, g->ammo[QA_Q1_SHELLS], 30,
                                   &given, error) ||
                !qa_inventory_configure(g->services.inventory, recipient, &entry, NULL, NULL,
                                        error) ||
                !qa_q1_player_select(g, recipient, item->weapon, error) ||
                !q1_message(g, recipient, "$mg3_map2_secret_weapon", error))
                return false;
        }
        item_touch touch = {
            .game = g, .entity = entity, .recipient = recipient, .original_ran = true};
        return item_complete(&touch, NULL, true, error);
    }
    bool accepted;
    return pickup_grant(g, entity, recipient, false, &accepted, error);
}
bool qa_q1_pickup_grant_external(qa_q1_game *g, qa_actor_id actor, qa_actor_id recipient,
                                 bool *accepted, qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || entity->kind != Q1_PICKUP || !entity->state.pickup.external || !accepted) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 external pickup requires an externally managed native item");
        return false;
    }
    return pickup_grant(g, entity, recipient, true, accepted, error);
}
static bool spawn_external(qa_q1_game *g, const qa_q1_spawn *spawn, const qa_body_state *body,
                           bool bounce, qa_actor_id *out, qa_error *error) {
    if (!g || !spawn || !spawn->classname || !body || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 external pickup spawn");
        return false;
    }
    q1_actor *entity;
    if (!q1_create(g, spawn->classname, Q1_PICKUP, (qa_actor_id){0}, &entity, error))
        return false;
    entity->spawnflags = spawn->spawnflags;
    entity->source_movement_flags = spawn->source_movement_flags;
    entity->count = spawn->count;
    if (!q1_pickup_define(g, entity, error))
        goto fail;
    entity->state.pickup.external = true;
    entity->state.pickup.respawn = -1;
    entity->physics.motion = bounce ? QA_PHYSICS_BOUNCE : QA_PHYSICS_TOSS;
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    entity->physics.flags = QA_PHYSICS_KILL_VELOCITY;
    if (!qa_world_body_write(g->services.world, entity->id, body, error) ||
        !q1_link(g, entity, error))
        goto fail;
    *out = entity->id;
    return true;
fail:
    (void)q1_remove(g, entity, NULL);
    return false;
}
bool qa_q1_pickup_spawn_external(qa_q1_game *g, const qa_q1_spawn *spawn, const qa_body_state *body,
                                 bool bounce, qa_actor_id *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 external pickup output");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    qa_actor_id actor;
    bool okay = spawn_external(g, spawn, body, bounce, &actor, error);
    if (okay && (!qa_q1_game_operation_live(&operation) || !q1_alive(g, actor))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 external pickup retired during its source constructor");
        okay = false;
    }
    if (okay) *out = actor;
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool q1_pickup_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_pickup *item = &entity->state.pickup;
    q1_think_kind kind = entity->think;
    if (kind == Q1_THINK_MG3_ITEM_START) {
        if (item->kind == Q1_ITEM_MG3_BLOODY &&
            !(qa_q1_game_campaign_flags(g) & QA_Q1_BLOODY_NIGHTMARE_NEWGAME))
            return q1_remove(g, entity, error);
        if (item->kind == Q1_ITEM_MG3_UPGRADE) {
            qa_builtin_snapshot_frame *snapshot;
            if (!q1_snapshot_actors(g, &snapshot, error))
                return false;
            for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
                q1_player *player = q1_player_get(g, snapshot->snapshot.ids[i]);
                if (!player)
                    continue;
                const uint32_t flags[] = {player->mg3_progress.health, player->mg3_progress.shells,
                                          player->mg3_progress.nails, player->mg3_progress.rockets,
                                          player->mg3_progress.cells};
                if (flags[item->upgrade] & item->upgrade_flag) {
                    entity->alpha = 0.6f;
                    break;
                }
            }
            qa_builtin_snapshot_release(snapshot);
        }
        return q1_schedule(g, entity, 0.2, Q1_THINK_ITEM_PLACE, error);
    }
    if (kind == Q1_THINK_MEGA_ROT) {
        qa_actor_id holder = q1_ref_actor(g, item->holder);
        q1_player *player = q1_player_get(g, holder);
        qa_builtin_actor_traits traits;
        float max_health = player ? player->max_health : 0;
        if (!player && g->services.actor_traits &&
            g->services.actor_traits(g->services.context, holder, &traits)) max_health = traits.max_health;
        float health = q1_health(g, holder);
        if (health > max_health)
            return qa_combat_set_health(g->services.combat, holder, health - 1, error) &&
                   q1_schedule(g, entity, 1, Q1_THINK_MEGA_ROT, error);
        if (player) player->source_superhealth = false;
        bool respawn = g->options.edition == QA_Q1_CLASSIC ? g->options.deathmatch == 1 :
            g->options.deathmatch != 0 && g->options.deathmatch != 2;
        return !respawn || q1_schedule(g, entity, 20, Q1_THINK_RESPAWN, error);
    }
    if (kind == Q1_THINK_RESPAWN) {
        if (item->random && !q1_pickup_define(g, entity, error))
            return false;
        entity->model = item->original_model;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        item->hidden = false;
        return q1_sound(g, entity->id, "items/itembk2.wav", 2, 1, error) &&
               q1_link(g, entity, error);
    }
    if (kind == Q1_THINK_ITEM_PLACE) {
        item->original_model = entity->model;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        qa_trace_query query = {.start = qa_vec_add(body.origin, qa_v3(0, 0, 6)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = entity->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        query.end = qa_vec_add(query.start, qa_v3(0, 0, -256));
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.all_solid || trace.fraction == 1)
            return q1_remove(g, entity, error);
        body.origin = trace.end;
        body.velocity = qa_v3(0, 0, 0);
        body.ground = trace.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(g->options.provider, 0) : q1_ref_from(g, trace.actor);
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->physics.motion = QA_PHYSICS_TOSS;
        entity->physics.flags = QA_PHYSICS_KILL_VELOCITY | QA_PHYSICS_ONGROUND;
        entity->source_movement_flags = UINT32_C(256);
        if (mg3_special(item) && (entity->spawnflags & 4)) {
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            entity->model = 0;
            item->hidden = true;
        }
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_link(g, entity, error);
    }
    qa_error_set(error, QA_ERROR_FORMAT, kind, "invalid Q1 pickup continuation");
    return false;
}
bool q1_pickup_use(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_pickup *item = &entity->state.pickup;
    if (item->external || !mg3_special(item) || !(entity->spawnflags & 4))
        return true;
    entity->model = item->original_model;
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    item->hidden = false;
    return q1_sound(g, entity->id, "items/itembk2.wav", 2, 1, error) &&
           (!q1_alive(g, entity->id) || q1_link(g, entity, error));
}
bool q1_drop_backpack(qa_q1_game *g, q1_actor *source, qa_q1_weapon weapon,
                      const float ammo[QA_Q1_AMMO_COUNT], qa_error *error) {
    bool any = false;
    for (unsigned i = 0; i < QA_Q1_AMMO_COUNT; ++i)
        any = any || ammo[i] > 0;
    if (!any)
        return true;
    qa_body_state from;
    qa_actor_id source_id = source->id;
    if (!qa_world_body_read(g->services.world, source->id, &from, error))
        return false;
    return !q1_alive(g, source_id) ||
           q1_spawn_backpack(g, source_id, from.origin, weapon, ammo, NULL, error);
}
bool q1_backpack_definition(qa_q1_game *g, q1_actor *pack, qa_error *error) {
    q1_pickup *item = &pack->state.pickup;
    item->kind = Q1_ITEM_BACKPACK;
    item->duration = 30;
    item->respawn = -1;
    item->avoid_underwater_lightning = g->options.edition == QA_Q1_RERELEASE;
    if (!qa_builtin_resource(&g->services, "q1:item_backpack", &item->item, error) ||
        !q1_model(g, pack, "progs/backpack.mdl", error) ||
        !qa_builtin_resource(&g->services, "weapons/lock4.wav", &item->sound, error))
        return false;
    item->original_model = pack->model;
    return true;
}
static bool construct_backpack(qa_q1_game *g, qa_actor_id source, qa_actor_id owner,
                                qa_vec3 origin, const qa_vec3 *velocity, qa_q1_weapon weapon,
                                const float ammo[QA_Q1_AMMO_COUNT], q1_actor **out,
                                qa_error *error) {
    if (out)
        *out = NULL;
    double total = ammo[0];
    for (unsigned i = 1; i < 4; ++i)
        total += ammo[i];
    double extra = 0;
    for (unsigned i = 4; i < QA_Q1_AMMO_COUNT; ++i)
        extra += ammo[i];
    total += extra;
    if (total == 0 || (source.registry && !q1_alive(g, source)))
        return true;
    q1_actor *pack;
    if (!q1_create(g, "item_backpack", Q1_PICKUP, owner, &pack, error))
        return false;
    qa_actor_id child = pack->id;
    if (!q1_entity(g, child) || (source.registry && !q1_alive(g, source)))
        goto cancelled;
    pack->touch_disabled = true;
    if (!q1_backpack_definition(g, pack, error))
        goto failed;
    pack->physics.solid = QA_PHYSICS_TRIGGER;
    pack->physics.motion = velocity ? QA_PHYSICS_BOUNCE : QA_PHYSICS_TOSS;
    pack->physics.flags = 0;
    pack->state.pickup.weapon = weapon;
    pack->state.pickup.owner_delay = velocity ? 1 : 0;
    memcpy(pack->state.pickup.ammo, ammo, sizeof(pack->state.pickup.ammo));
    for (unsigned i = 0; i < 4; ++i)
        pack->state.pickup.ammo[i] = fmaxf(ammo[i], 0);
    if (g->options.edition == QA_Q1_RERELEASE && weapon < QA_Q1_WEAPON_COUNT) {
        int kind = weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_SUPER_SHOTGUN ? 0
                 : weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN ? 1
                 : weapon == QA_Q1_ROCKET || weapon == QA_Q1_GRENADE ? 2
                 : weapon == QA_Q1_LIGHTNING ? 3 : -1;
        static const float minimum[] = {5, 20, 5, 15};
        if (kind >= 0 && kind < 4)
            pack->state.pickup.ammo[kind] = fmaxf(ammo[kind], minimum[kind]);
    }
    qa_vec3 launch;
    if (velocity)
        launch = *velocity;
    else {
        origin = qa_v3(origin.x + 0.0f, origin.y + 0.0f,
                       (float)((double)origin.z - 24.0));
        float x = (float)(-100.0 + (double)q1_random(g) * 200.0);
        float y = (float)(-100.0 + (double)q1_random(g) * 200.0);
        launch = qa_v3(x, y, 300);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, child, &body, error))
        goto failed;
    pack = q1_entity(g, child);
    if (!pack || (source.registry && !q1_alive(g, source)))
        goto cancelled;
    body.origin = origin;
    body.velocity = launch;
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    if (!qa_world_body_write(g->services.world, child, &body, error))
        goto failed;
    pack = q1_entity(g, child);
    if (!pack || (source.registry && !q1_alive(g, source)))
        goto cancelled;
    pack->touch_disabled = false;
    if (!q1_schedule(g, pack, 120, Q1_THINK_REMOVE, error) || !q1_link(g, pack, error))
        goto failed;
    pack = q1_entity(g, child);
    if (!pack || (source.registry && !q1_alive(g, source)))
        goto cancelled;
    if (out)
        *out = pack;
    return true;
cancelled:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return true;
failed:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return false;
}
bool q1_spawn_backpack(qa_q1_game *g, qa_actor_id source, qa_vec3 origin, qa_q1_weapon weapon,
                       const float ammo[QA_Q1_AMMO_COUNT], q1_actor **out, qa_error *error) {
    return construct_backpack(g, source, (qa_actor_id){0}, origin, NULL, weapon, ammo, out, error);
}
bool q1_toss_backpack(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity,
                      const float ammo[QA_Q1_AMMO_COUNT], q1_actor **out, qa_error *error) {
    return construct_backpack(g, owner, owner, origin, &velocity, QA_Q1_WEAPON_COUNT,
                                ammo, out, error);
}

static double protection_value(qa_regular_armor armor) {
    double absorption = armor.kind == QA_ARMOR_Q1 ? armor.protection.q1_absorption
        : armor.kind == QA_ARMOR_Q2 ? armor.protection.q2.normal
        : armor.kind == QA_ARMOR_Q3 ? armor.protection.q3_protection : 0;
    return armor.points * absorption;
}
bool qa_q1_bot_supply_preview(qa_q1_game *g,qa_actor_id pickup,qa_actor_id recipient,
    qa_supply_preview_result *out,bool *eligible,bool *found,qa_error *error) {
    if(!g || !out || !eligible || !found) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 supply preview requires its source owner and outputs");
        return false;
    }
    *out=(qa_supply_preview_result){0};*eligible=false;*found=false;
    qa_q1_game_operation operation={0};
    if(!qa_q1_game_operation_begin(g,&operation,error)) return false;
    bool ok=true;
    q1_actor *entity=q1_entity(g,pickup);
    if(!entity || entity->kind!=Q1_PICKUP || entity->touch_disabled) goto done;
    q1_pickup item=entity->state.pickup;
    if(item.external || item.mission || item.drop!=Q1_DROP_NONE ||
       (item.kind!=Q1_ITEM_WEAPON && item.kind!=Q1_ITEM_AMMO)) goto done;
    *found=true;
    if(!item.hidden && entity->physics.solid==QA_PHYSICS_TRIGGER &&
       q1_health(g,recipient)>0 && q1_player_get(g,recipient)) {
        *eligible=true;
        if(item.kind==Q1_ITEM_WEAPON && weapon_leave(g)) {
            bool owned;
            qa_supply *selected;
            ok=supply(g,recipient,&selected,error) &&
                qa_supply_owns(selected,recipient,item.item,&owned,error);
            if(!ok) goto done;
            *eligible=!owned;
        }
    }
    if(!q1_alive(g,pickup) || !q1_alive(g,recipient) || g->destroy_pending) {
        *eligible=false;*found=false;goto done;
    }
    int ammo=item.kind==Q1_ITEM_WEAPON?q1_weapon_ammo(item.weapon):-1;
    qa_pickup_grant grant={.item=item.kind==Q1_ITEM_AMMO?item.item:ammo>=0?g->ammo[ammo]:0,
                           .amount=item.count};
    qa_supply_offer offer={.kind=item.kind==Q1_ITEM_WEAPON?QA_SUPPLY_WEAPON:QA_SUPPLY_AMMO,
        .item=item.item,.ammo=&grant,.ammo_count=item.kind==Q1_ITEM_AMMO || ammo>=0?1:0};
    qa_supply *selected;
    ok=supply(g,recipient,&selected,error) &&
        qa_supply_preview(selected,recipient,&offer,false,out,error);
    if(!ok || !q1_alive(g,pickup) || !q1_alive(g,recipient) || g->destroy_pending) {
        qa_supply_preview_free(out);*eligible=false;*found=false;
    }
done:
    qa_q1_game_operation_end(&operation);return ok;
}
static bool inventory_benefit(qa_q1_game *g, qa_actor_id actor, qa_item_id item, double amount,
                               double missing_capacity, float *utility, qa_error *error) {
    qa_inventory_entry before;
    qa_error read_error = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &before, &read_error)) {
        if (read_error.code != QA_ERROR_NOT_FOUND || missing_capacity < 0) {
            if (error)
                *error = read_error;
            return false;
        }
        before = (qa_inventory_entry){.item = item, .capacity = missing_capacity,
                                       .policy = QA_COUNT_SOURCE_FLOAT};
    }
    qa_inventory_entry after;
    double given;
    bool writes;
    if (!qa_inventory_preview_give(&before, amount, &after, &given, &writes, error))
        return false;
    *utility = (float)fmin(FLT_MAX, (double)*utility + fmax(0, given));
    return true;
}
static bool pickup_preview(qa_q1_game *g, qa_actor_id actor, const q1_pickup *item,
                            float *utility, bool *accepted, qa_error *error) {
    *utility = 0;
    *accepted = false;
    q1_player *player = q1_player_get(g, actor);
    if (item->drop != Q1_DROP_NONE) {
        if (item->drop == Q1_DROP_CTF_AMMO) {
            for (unsigned i = 0; i < 4; ++i)
                if (item->ammo[i] != 0 && !inventory_benefit(g, actor, g->ammo[i], item->ammo[i],
                                        -1, utility, error))
                    return false;
        } else if (!inventory_benefit(g, actor, item->item, 1,
                                      item->drop == Q1_DROP_ROGUE_WEAPON ? -1 : 1,
                                      utility, error))
            return false;
        *accepted = true;
        return true;
    }
    switch (item->kind) {
    case Q1_ITEM_HEALTH: {
        float health = q1_health(g, actor), limit = item->mega ? 250 : player ? player->max_health : 100;
        qa_builtin_actor_traits traits;
        if (!item->mega && g->services.actor_traits &&
            g->services.actor_traits(g->services.context, actor, &traits))
            limit = traits.max_health;
        if (health > 0 && health < limit) {
            *accepted = true;
            *utility = fmaxf(0, fminf(limit, health + item->count) - health);
        }
        return true;
    }
    case Q1_ITEM_ARMOR:
    case Q1_ITEM_MG3_SHARD: {
        qa_combat_state state;
        if (!qa_combat_read(g->services.combat, actor, &state, error))
            return false;
        qa_regular_armor armor = state.armor.regular;
        if (armor.kind == QA_ARMOR_SOURCE)
            return true;
        double before = protection_value(armor);
        if (item->kind == Q1_ITEM_ARMOR) {
            double benefit = fmax(0, (double)item->count * item->absorption - before);
            *utility = (float)benefit;
            *accepted = benefit > 0;
        } else if (armor.points < 200) {
            float absorption = armor.kind == QA_ARMOR_Q1 ? fmaxf(.3f, armor.protection.q1_absorption) : .3f;
            *utility = (float)fmax(0, fmin(200, armor.points + 5) * absorption - before);
            *accepted = true;
        }
        return true;
    }
    case Q1_ITEM_KEY:
    case Q1_ITEM_SPHERE:
        if (!inventory_benefit(g, actor, item->kind == Q1_ITEM_SPHERE ? g->vengeance_item : item->item,
                                1, 1, utility, error))
            return false;
        *accepted = *utility > 0;
        return true;
    case Q1_ITEM_POWER: {
        if (!isfinite(item->count) || item->count < 0 || item->count >= (float)QA_Q1_POWER_COUNT ||
            floorf(item->count) != item->count) {
            qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "Invalid observed Q1 power identity");
            return false;
        }
        unsigned kind = (unsigned)item->count;
        *accepted = true;
        *utility = (float)fmax(0, g->time + item->duration -
                                   fmax(g->time, qa_q1_game_power_expires(g, actor, (qa_q1_power)kind)));
        return true;
    }
    case Q1_ITEM_HORN:
    case Q1_ITEM_MG3_UPGRADE:
    case Q1_ITEM_MG3_BLOODY:
        *accepted = item->kind == Q1_ITEM_HORN || player != NULL;
        *utility = *accepted ? 1 : 0;
        return true;
    default:
        break;
    }
    qa_supply *selected;
    if (!supply(g, actor, &selected, error)) return false;
    qa_supply_preview_result preview = {0};
    bool ok;
    if (item->kind == Q1_ITEM_BACKPACK) {
        qa_pickup_cargo cargo[QA_Q1_AMMO_COUNT + 1];
        size_t count = backpack_cargo(g, item, cargo);
        ok = qa_supply_cargo_preview(selected, actor, cargo, count, false, &preview, error);
    } else if (item->kind == Q1_ITEM_AMMO || item->kind == Q1_ITEM_WEAPON) {
        int ammo = item->kind == Q1_ITEM_WEAPON ? q1_weapon_ammo(item->weapon) : -1;
        if (item->weapon == QA_Q1_MJOLNIR || item->weapon == QA_Q1_MG3_MJOLNIR)
            ammo = QA_Q1_CELLS;
        qa_pickup_grant grant = {.item = item->kind == Q1_ITEM_AMMO ? item->item
                                        : ammo >= 0 ? g->ammo[ammo] : 0, .amount = item->count};
        qa_supply_offer offer = {.kind = item->kind == Q1_ITEM_WEAPON ? QA_SUPPLY_WEAPON : QA_SUPPLY_AMMO,
                                 .item = item->item, .ammo = &grant,
                                 .ammo_count = item->kind == Q1_ITEM_AMMO || ammo >= 0 ? 1 : 0};
        ok = qa_supply_preview(selected, actor, &offer, false, &preview, error);
    } else {
        qa_error_set(error, QA_ERROR_FORMAT, item->kind, "Unknown Q1 observed pickup kind");
        return false;
    }
    if (ok) {
        *accepted = preview.accepted;
        double benefit = 0;
        for (size_t i = 0; i < preview.weapon_count; ++i)
            benefit += fmax(0, preview.weapons[i].given);
        for (size_t i = 0; i < preview.ammo_count; ++i)
            benefit += fmax(0, preview.ammo[i].given);
        *utility = (float)fmin(benefit, FLT_MAX);
    }
    qa_supply_preview_free(&preview);
    return ok;
}
static bool pickup_inspect(void *context, qa_actor_id pickup, qa_actor_id actor,
                             qa_pickup_offer *offer, float *utility, bool *available,
                             qa_error *error) {
    qa_q1_game *g = context;
    *available = false;
    *utility = 0;
    q1_actor *e = q1_entity(g, pickup);
    if (!e || e->kind != Q1_PICKUP || g->destroy_pending)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_pickup item = e->state.pickup;
    qa_actor_id owner = q1_ref_actor(g, e->owner);
    double owner_wait = e->next_think - g->time;
    *offer = (qa_pickup_offer){.pickup = pickup, .recipient = actor,
                               .source = g->options.provider, .item = item.item,
                               .override_count = e->count != 0, .count = e->count,
                               .dropped = item.kind == Q1_ITEM_BACKPACK || item.drop != Q1_DROP_NONE,
                               .time_ns = g->time_ns};
    qa_pickup_cargo cargo[QA_Q1_AMMO_COUNT + 1];
    if (item.kind == Q1_ITEM_BACKPACK)
        offer->cargo_count = backpack_cargo(g, &item, cargo);
    offer->cargo = offer->cargo_count ? cargo : NULL;
    if (item.kind == Q1_ITEM_AMMO || item.kind == Q1_ITEM_WEAPON || item.kind == Q1_ITEM_KEY)
        offer->default_resource = (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY, .item = item.item};
    else if (item.kind == Q1_ITEM_ARMOR || item.kind == Q1_ITEM_MG3_SHARD)
        offer->default_resource = (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION,
                                                       .channel = QA_PROTECTION_REGULAR};
    if (item.drop != Q1_DROP_NONE)
        q1_drop_offer(g, e, actor, offer, cargo);
    bool ok = true, eligible = false;
    qa_q1_target target;
    qa_builtin_actor_traits traits = {0};
    if (item.drop != Q1_DROP_NONE) {
        eligible = !item.hidden && e->physics.solid == QA_PHYSICS_TRIGGER &&
                   q1_drop_eligible(g, e, actor);
    } else if (!item.hidden && e->physics.solid == QA_PHYSICS_TRIGGER &&
        (item.kind == Q1_ITEM_HORN || q1_health(g, actor) > 0) &&
        q1_target(g, actor, &target) && target.player) {
        eligible = true;
        if (g->services.actor_traits)
            g->services.actor_traits(g->services.context, actor, &traits);
        if (qa_actor_id_equal(owner, actor) && owner_wait > 120 - item.owner_delay)
            eligible = false;
        if (eligible && item.kind == Q1_ITEM_WEAPON && item.drop == Q1_DROP_NONE && weapon_leave(g)) {
            bool owned;
            qa_supply *selected;
            ok = supply(g, actor, &selected, error) &&
                qa_supply_owns(selected, actor, item.item, &owned, error);
            if (ok)
                eligible = !owned;
        }
    }
    if (ok && eligible && q1_alive(g, pickup) && q1_alive(g, actor) && !g->destroy_pending) {
        bool handled;
        ok = qa_pickups_preview(g->services.pickups, offer, utility, available, &handled, error);
        if (ok && !handled && !g->destroy_pending && q1_alive(g, pickup) && q1_alive(g, actor))
            ok = pickup_preview(g, actor, &item, utility, available, error);
    }
    offer->cargo = NULL;
    offer->cargo_count = 0;
    e = q1_entity(g, pickup);
    if (!e || !q1_alive(g, actor) || e->kind != Q1_PICKUP || e->state.pickup.hidden ||
        e->state.pickup.item != item.item || e->physics.solid != QA_PHYSICS_TRIGGER || g->destroy_pending) {
        *available = false;
        *utility = 0;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_pickup_observe(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!g->services.pickups || !q1_alive(g, e->id))
        return true;
    qa_pickup_observer observer = {.context = g, .inspect = pickup_inspect};
    return qa_pickups_observe(g->services.pickups, e->id, g->options.provider,
                               &observer, &e->pickup_observation, error);
}
bool qa_q1_game_pickup_observer(qa_q1_game *g, qa_actor_id actor, qa_actor_owner owner,
                                uint64_t serial, qa_pickup_observer *out, qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!g || !out || !g->continuation_pending || owner != g->options.provider || !serial ||
        !entity || entity->kind != Q1_PICKUP || entity->pickup_observation.serial != serial ||
        !qa_actor_id_equal(entity->pickup_observation.actor, actor)) {
        qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "Invalid restored Q1 pickup observer");
        return false;
    }
    *out = (qa_pickup_observer){.context = g, .inspect = pickup_inspect};
    return true;
}
bool qa_q1_game_pickups_rebind(qa_q1_game *g, qa_error *error) {
    if (!g || g->observation_depth || g->destroy_pending || g->continuation_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q1 pickup rebinding boundary");
        return false;
    }
    for (uint32_t slot = 0; slot < g->capacity; ++slot) {
        q1_actor *e = g->actors[slot];
        if (e && e->active && e->kind == Q1_PICKUP && !q1_pickup_observe(g, e, error))
            return false;
    }
    return true;
}

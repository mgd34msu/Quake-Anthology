#include "internal.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q1_bots.h"
#include <float.h>

const qa_q1_weapon_view *q1_weapon_shape(qa_q1_weapon weapon) {
    static const qa_q1_weapon_view shapes[QA_Q1_WEAPON_COUNT] = {
        [QA_Q1_AXE] = {.attack_interval = .5f, .range = 64, .damage = 20, .shots = 1, .melee = true,
                       .launch_delay = .2f},
        [QA_Q1_SHOTGUN] = {.attack_interval = .5f, .range = 2048, .damage = 4, .shots = 6,
                           .ammo_per_shot = 1, .horizontal_spread = .04f, .vertical_spread = .04f},
        [QA_Q1_SUPER_SHOTGUN] = {.attack_interval = .7f, .range = 2048, .damage = 4, .shots = 14,
                                 .ammo_per_shot = 2, .horizontal_spread = .14f, .vertical_spread = .08f},
        [QA_Q1_NAILGUN] = {.attack_interval = .2f, .fire_interval = .1f, .speed = 1000, .range = 6000,
                           .damage = 9, .shots = 1, .ammo_per_shot = 1, .lifetime = 6},
        [QA_Q1_SUPER_NAILGUN] = {.attack_interval = .2f, .fire_interval = .1f, .speed = 1000, .range = 6000,
                                 .damage = 18, .shots = 1, .ammo_per_shot = 2, .lifetime = 6},
        [QA_Q1_GRENADE] = {.attack_interval = .6f, .speed = 600, .range = 1500, .damage = 120,
                           .blast_damage = 120, .blast_radius = 160, .shots = 1, .ammo_per_shot = 1,
                           .gravity = 1, .extra_z_velocity = 200, .lifetime = 2.5f},
        [QA_Q1_ROCKET] = {.attack_interval = .8f, .speed = 1000, .range = 5000, .damage = 110,
                          .blast_damage = 120, .blast_radius = 160, .shots = 1, .ammo_per_shot = 1, .lifetime = 5},
        [QA_Q1_LIGHTNING] = {.attack_interval = .1f, .fire_interval = .1f, .range = 600, .damage = 30,
                             .shots = 1, .ammo_per_shot = 1},
        [QA_Q1_LASER] = {.attack_interval = .1f, .speed = 1000, .range = 5000, .damage = 18,
                         .shots = 2, .ammo_per_shot = 1, .lifetime = 5},
        [QA_Q1_MJOLNIR] = {.attack_interval = .8f, .range = 64, .damage = 50, .shots = 1, .melee = true,
                           .conditional_strike = true, .launch_delay = .3f},
        [QA_Q1_PROXIMITY] = {.attack_interval = .6f, .speed = 600, .range = 9000, .damage = 95,
                             .blast_damage = 95, .blast_radius = 135, .shots = 1, .ammo_per_shot = 1,
                             .gravity = 1, .extra_z_velocity = 200, .lifetime = 15},
        [QA_Q1_LAVA_NAILGUN] = {.attack_interval = .2f, .fire_interval = .1f, .speed = 1000, .range = 6000,
                                .damage = 9, .shots = 1, .ammo_per_shot = 1, .lifetime = 6},
        [QA_Q1_LAVA_SUPER_NAILGUN] = {.attack_interval = .2f, .fire_interval = .1f, .speed = 1000,
                                      .range = 6000, .damage = 18, .shots = 1, .ammo_per_shot = 2, .lifetime = 6},
        [QA_Q1_MULTI_GRENADE] = {.attack_interval = .6f, .speed = 600, .range = 600, .damage = 120,
                                 .blast_damage = 120, .blast_radius = 160, .shots = 1, .ammo_per_shot = 1,
                                 .gravity = 1, .extra_z_velocity = 200, .lifetime = 1,
                                 .conditional_strike = true},
        [QA_Q1_MULTI_ROCKET] = {.attack_interval = .8f, .speed = 1000, .range = 4000, .damage = 67.5f,
                                .blast_damage = 75, .blast_radius = 115, .shots = 4, .ammo_per_shot = 1, .lifetime = 4},
        [QA_Q1_PLASMA] = {.attack_interval = 1, .speed = 1250, .range = 6250, .damage = 90,
                          .blast_damage = 70, .blast_radius = 110, .shots = 1, .ammo_per_shot = 1, .lifetime = 5,
                          .conditional_strike = true, .launch_delay = .1f},
        [QA_Q1_ROGUE_GRAPPLE] = {.speed = 800, .range = 1600, .shots = 1, .grapple = true, .lifetime = 2},
        [QA_Q1_MG3_LASER] = {.attack_interval = .1f, .speed = 1000, .range = 5000, .damage = 15,
                             .shots = 2, .ammo_per_shot = 1, .lifetime = 5},
        [QA_Q1_MG3_MJOLNIR] = {.attack_interval = .5f, .range = 64, .damage = 40, .shots = 1, .melee = true,
                               .conditional_strike = true, .launch_delay = .2f},
        [QA_Q1_CTF_GRAPPLE] = {.attack_interval = .1f, .speed = 800, .range = 4000, .shots = 1,
                               .grapple = true, .lifetime = 5}};
    return &shapes[weapon];
}
float q1_weapon_interval(qa_q1_weapon weapon) {
    return q1_weapon_shape(weapon)->attack_interval;
}

static bool base_parameters(qa_q1_game *g, q1_player *player, qa_q1_weapon weapon,
                             qa_q1_weapon_parameters *parameters, qa_error *error) {
    *parameters = (qa_q1_weapon_parameters){
        .interval = weapon == QA_Q1_LIGHTNING && player->weapon == weapon && player->continuous
                        ? .2f : q1_weapon_interval(weapon),
        .nail_speed = 1000};
    if (weapon == QA_Q1_SHOTGUN && (player->mg3_progress.bloody & 1))
        parameters->interval = .28f;
    return q1_weapon_parameters(g, player->id, weapon, parameters, error);
}

static void shotgun_shape(const q1_player *player, qa_q1_weapon weapon, double shells,
                            qa_q1_weapon_view *view) {
    bool super = weapon == QA_Q1_SUPER_SHOTGUN && shells > 1;
    const qa_q1_weapon_view *shape = q1_weapon_shape(super ? weapon : QA_Q1_SHOTGUN);
    view->ammo_per_shot = shape->ammo_per_shot;
    view->shots = shape->shots;
    view->horizontal_spread = shape->horizontal_spread;
    view->vertical_spread = shape->vertical_spread;
    if (super && (player->mg3_progress.bloody & 2)) {
        view->shots = 28;
        view->horizontal_spread = .3f;
    }
}

double q1_ammo_count(qa_q1_game *g, qa_actor_id actor, qa_q1_ammo ammo) {
    qa_inventory_entry entry;
    return qa_inventory_entry_read(g->services.inventory, actor, g->ammo[ammo], &entry, NULL)
               ? entry.count
               : 0;
}
bool q1_consume(qa_q1_game *g, qa_actor_id actor, qa_q1_ammo ammo, float amount, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (player && player->mg3_infinite_ammo)
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, actor, g->ammo[ammo], amount, &consumed,
                              error))
        return false;
    if (!consumed)
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 ammunition changed during admitted attack");
    return consumed;
}
static bool owns(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon) {
    qa_inventory_entry entry;
    return qa_inventory_entry_read(g->services.inventory, actor, g->weapons[weapon], &entry,
                                   NULL) &&
           entry.count > 0;
}
int q1_weapon_ammo(qa_q1_weapon weapon) {
    static const int ammunition[QA_Q1_WEAPON_COUNT] = {-1,
                                                       QA_Q1_SHELLS,
                                                       QA_Q1_SHELLS,
                                                       QA_Q1_NAILS,
                                                       QA_Q1_NAILS,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_CELLS,
                                                       QA_Q1_CELLS,
                                                       -1,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_LAVA_NAILS,
                                                       QA_Q1_LAVA_NAILS,
                                                       QA_Q1_MULTI_ROCKETS,
                                                       QA_Q1_MULTI_ROCKETS,
                                                       QA_Q1_PLASMA_CELLS,
                                                       -1,
                                                       QA_Q1_CELLS,
                                                       -1,
                                                       -1};
    return ammunition[weapon];
}
int q1_weapon_declared_ammo(qa_q1_weapon weapon) {
    return weapon == QA_Q1_MG3_MJOLNIR ? QA_Q1_CELLS : q1_weapon_ammo(weapon);
}
static double best_ammunition_needed(qa_q1_weapon weapon) {
    if (weapon == QA_Q1_MG3_MJOLNIR)
        return 0;
    return weapon == QA_Q1_SUPER_NAILGUN || weapon == QA_Q1_SUPER_SHOTGUN ||
        weapon == QA_Q1_LAVA_SUPER_NAILGUN ? 2 : 1;
}
qa_q1_weapon q1_best_weapon(qa_q1_game *g, q1_player *player) {
    return q1_best_weapon_before(g, player, NULL, 0);
}
static const qa_q1_weapon *best_weapon_order(const qa_q1_game *g, size_t *count) {
    static const qa_q1_weapon base[] = {QA_Q1_LIGHTNING, QA_Q1_SUPER_NAILGUN, QA_Q1_SUPER_SHOTGUN,
                                        QA_Q1_NAILGUN,   QA_Q1_SHOTGUN,       QA_Q1_AXE};
    static const qa_q1_weapon hipnotic[] = {QA_Q1_LIGHTNING,     QA_Q1_LASER,   QA_Q1_SUPER_NAILGUN,
                                            QA_Q1_SUPER_SHOTGUN, QA_Q1_NAILGUN, QA_Q1_SHOTGUN,
                                            QA_Q1_MJOLNIR,       QA_Q1_AXE};
    static const qa_q1_weapon rogue[] = {
        QA_Q1_LIGHTNING, QA_Q1_LAVA_SUPER_NAILGUN, QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_NAILGUN,
        QA_Q1_NAILGUN,   QA_Q1_SUPER_SHOTGUN,      QA_Q1_SHOTGUN,       QA_Q1_AXE};
    static const qa_q1_weapon mg3[] = {QA_Q1_LIGHTNING, QA_Q1_SUPER_NAILGUN, QA_Q1_SUPER_SHOTGUN,
                                       QA_Q1_NAILGUN,   QA_Q1_SHOTGUN,       QA_Q1_MG3_MJOLNIR,
                                       QA_Q1_AXE};
    const qa_q1_weapon *order = base;
    size_t length = sizeof(base) / sizeof(*base);
    if (g->options.program == QA_Q1_HIPNOTIC) {
        order = hipnotic;
        length = sizeof(hipnotic) / sizeof(*hipnotic);
    }
    if (g->options.program == QA_Q1_ROGUE) {
        order = rogue;
        length = sizeof(rogue) / sizeof(*rogue);
    }
    if (g->options.program == QA_Q1_MG3) {
        order = mg3;
        length = sizeof(mg3) / sizeof(*mg3);
    }
    *count = length;
    return order;
}
qa_q1_weapon q1_best_weapon_before(qa_q1_game *g, q1_player *player,
                                   const qa_pickup_receipt *receipts, size_t count) {
    size_t length;
    const qa_q1_weapon *order = best_weapon_order(g, &length);
    for (size_t i = 0; i < length; ++i) {
        qa_q1_weapon weapon = order[i];
        int ammo = q1_weapon_ammo(weapon);
        if (!owns(g, player->id, weapon) ||
            (weapon == QA_Q1_LIGHTNING && player->input.water_level > 1))
            continue;
        double needed = best_ammunition_needed(weapon);
        double available = ammo < 0 ? 1 : q1_ammo_count(g, player->id, (qa_q1_ammo)ammo);
        if (ammo >= 0)
            for (size_t j = 0; j < count; ++j)
                if (receipts[j].item == g->ammo[ammo]) {
                    available = receipts[j].before;
                    break;
                }
        if (available >= needed)
            return weapon;
    }
    return QA_Q1_AXE;
}
static bool best_player_current(qa_q1_game *g, qa_actor_id actor,
    const q1_player *player, qa_error *error) {
    if (g && !g->destroy_pending && !g->continuation_pending && player &&
        q1_alive(g, actor) && q1_player_get(g, actor) == player && player->arsenal) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 best-weapon read lost its selected arsenal player");
    return false;
}
bool q1_best_weapon_before_read(qa_q1_game *g, qa_actor_id actor, q1_player *player,
    const qa_pickup_receipt *receipts, size_t count, qa_q1_weapon *out, qa_error *error) {
    if (!out || (count && !receipts) || !best_player_current(g, actor, player, error)) return false;
    size_t length;
    const qa_q1_weapon *order = best_weapon_order(g, &length);
    qa_q1_weapon selected = QA_Q1_AXE;
    for (size_t i = 0; i < length; ++i) {
        qa_q1_weapon weapon = order[i];
        qa_inventory_entry owned;
        if (!qa_inventory_entry_read(g->services.inventory, actor, g->weapons[weapon], &owned, error) ||
            !best_player_current(g, actor, player, error)) return false;
        if (owned.count <= 0 || (weapon == QA_Q1_LIGHTNING && player->input.water_level > 1)) continue;
        int ammo = q1_weapon_ammo(weapon);
        double available = 1;
        if (ammo >= 0) {
            bool found = false;
            for (size_t j = 0; j < count; ++j)
                if (receipts[j].item == g->ammo[ammo]) {
                    available = receipts[j].before; found = true; break;
                }
            if (!found) {
                qa_inventory_entry entry;
                if (!qa_inventory_entry_read(g->services.inventory, actor, g->ammo[ammo], &entry, error) ||
                    !best_player_current(g, actor, player, error)) return false;
                available = entry.count;
            }
        }
        double needed = best_ammunition_needed(weapon);
        if (available >= needed) { selected = weapon; break; }
    }
    *out = selected;
    return true;
}
bool q1_weapon_ui_available_read(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
    bool *out, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!out || !best_player_current(g, actor, player, error))
        return false;
    *out = false;
    double owned;
    if (!qa_inventory_count_read(g->services.inventory, actor, g->weapons[weapon],
        &owned, error) || !best_player_current(g, actor, player, error))
        return false;
    if (owned == 0 || weapon == QA_Q1_CTF_GRAPPLE ||
        (weapon == QA_Q1_LIGHTNING && player->input.water_level > 1))
        return true;
    int ammo = q1_weapon_declared_ammo(weapon);
    double available = 1;
    if (ammo >= 0 && (!qa_inventory_count_read(g->services.inventory, actor, g->ammo[ammo],
        &available, error) || !best_player_current(g, actor, player, error)))
        return false;
    *out = ammo < 0 || available >= best_ammunition_needed(weapon);
    return true;
}
static qa_string_id weapon_model(const qa_q1_game *g, const q1_player *player) {
    if (player->weapon == QA_Q1_MG3_MJOLNIR && player->mg3_hammer_glow &&
        player->mg3_hammer_until > g->time)
        return g->hammer_glow_model;
    if (player->weapon == QA_Q1_SHOTGUN && (player->mg3_progress.bloody & 1))
        return g->blood_shotgun_model;
    if (player->weapon == QA_Q1_SUPER_SHOTGUN && (player->mg3_progress.bloody & 2))
        return g->blood_super_shotgun_model;
    return g->weapon_models[player->weapon];
}
static bool weapon_event(qa_q1_game *g, q1_player *player, float punch, int32_t attack,
    bool attacking, qa_error *error) {
    if (g->destroy_pending)
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = player->id,
                              .time_ns = g->time_ns,
                              .resource = weapon_model(g, player),
                              .frame = player->weapon_frame,
                              .value = punch,
                              .code = attack,
                              .flags = (uint32_t)player->weapon |
                                  (attacking ? UINT32_C(0x80000000) : 0)};
    return qa_builtin_emit(&g->services, &event, error);
}
bool q1_weapon_event(qa_q1_game *g, q1_player *player, float punch, int32_t attack,
    qa_error *error) {
    return weapon_event(g, player, punch, attack, false, error);
}
static bool inventory_current(qa_q1_game_operation *operation, qa_actor_id actor,
    q1_player *player, qa_error *error) {
    if (qa_q1_game_operation_live(operation) && q1_alive(operation->game, actor) &&
        q1_player_get(operation->game, actor) == player)
        return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "Q1 inventory initialization retired its source player");
    return false;
}
static bool reset_inventory(qa_q1_game_operation *operation, qa_actor_id actor,
    bool extensions, qa_error *error) {
    qa_q1_game *g = operation->game;
    q1_player *player = q1_player_get(g, actor);
    if (!inventory_current(operation, actor, player, error))
        return false;
    qa_inventory_entry entries[QA_Q1_WEAPON_COUNT + QA_Q1_AMMO_COUNT];
    size_t weapons = extensions ? QA_Q1_WEAPON_COUNT : QA_Q1_LIGHTNING + 1;
    size_t ammo = extensions ? QA_Q1_AMMO_COUNT : QA_Q1_CELLS + 1;
    for (size_t i = 0; i < weapons; ++i)
        entries[i] = (qa_inventory_entry){.item = g->weapons[i],
                                          .count = i == QA_Q1_AXE || i == QA_Q1_SHOTGUN ? 1 : 0,
                                          .capacity = 1,
                                          .policy = QA_COUNT_SOURCE_FLOAT};
    if (extensions && g->options.program == QA_Q1_ROGUE && g->options.deathmatch &&
        g->options.teamplay >= 4)
        entries[QA_Q1_ROGUE_GRAPPLE].count = 1;
    for (size_t i = 0; i < ammo; ++i)
        entries[weapons + i] = (qa_inventory_entry){
            .item = g->ammo[i],
            .count = i == QA_Q1_SHELLS ? 25 : 0,
            .capacity = i == QA_Q1_NAILS || i == QA_Q1_LAVA_NAILS ? 200 : 100,
            .policy = QA_COUNT_SOURCE_FLOAT};
    size_t count = weapons + ammo;
    if (!qa_inventory_has(g->services.inventory, actor)) {
        if (!qa_inventory_create_actor(g->services.inventory, actor, entries, count, error))
            return false;
    } else
        for (size_t i = 0; i < count; ++i) {
            if (!qa_inventory_configure(g->services.inventory, actor, &entries[i], NULL, NULL,
                                        error))
                return false;
            if (!inventory_current(operation, actor, player, error))
                return false;
        }
    return inventory_current(operation, actor, player, error);
}
bool qa_q1_player_inventory_reset(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g,&operation,error))
        return false;
    q1_player *player = q1_player_get(g, actor);
    bool result = false;
    if (!player || !player->arsenal) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 inventory reset needs an arsenal");
    } else {
        result = reset_inventory(&operation, actor, true, error) &&
                 (g->options.program != QA_Q1_MG3 || q1_mg3_capacities(g, player, error));
        if (result && !q1_alive(g,actor)) {
            qa_error_set(error,QA_ERROR_ARGUMENT,actor.slot,"Q1 inventory reset requested teardown");
            result = false;
        }
    }
    qa_q1_game_operation_end(&operation);
    return result;
}
bool qa_q1_player_inventory_initialize(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_player *player = q1_player_get(g, actor);
    bool result = false;
    if (!player || !player->arsenal) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Q1 inventory initialization needs an admitted arsenal");
    } else {
        result = reset_inventory(&operation, actor, false, error) &&
            q1_inventory_register(&operation, actor, error) &&
            inventory_current(&operation, actor, player, error);
        if (result) {
            player->max_health = 100;
            result = q1_inventory_attach(&operation, player, error) &&
                inventory_current(&operation, actor, player, error);
        }
    }
    qa_q1_game_operation_end(&operation);
    return result;
}
bool qa_q1_player_attach(qa_q1_game *g, qa_actor_id actor, bool initial_inventory,
                         qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool result = false;
    q1_player *player = NULL;
    if (!q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 arsenal needs a live shared player");
        goto finish;
    }
    q1_player *existing = q1_player_get(g, actor);
    if (existing && existing->arsenal) {
        player = existing;
        result = q1_inventory_bind(g, player, error);
        goto finish;
    }
    if (initial_inventory && !reset_inventory(&operation, actor, false, error))
        goto finish;
    if (!q1_inventory_register(&operation, actor, error))
        goto finish;
    player = q1_player_allocate(g, actor, error);
    if (!player)
        goto finish;
    player->arsenal = true;
    if (!q1_inventory_attach(&operation, player, error))
        goto finish;
    if (!q1_inventory_bind(g, player, error))
        goto finish;
    qa_body_state body;
    if (qa_world_body_read(g->services.world, actor, &body, NULL))
        player->input.view_angles = body.angles;
    result = q1_weapon_event(g, player, 0, 0, error);
finish:
    if (result && (!qa_q1_game_operation_live(&operation) ||
                   q1_player_get(g, actor) != player)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 arsenal source retired during player admission");
        result = false;
    }
    qa_q1_game_operation_end(&operation);
    return result;
}
bool qa_q1_player_source_input(qa_q1_game *g, qa_actor_id actor, const qa_q1_input *input,
                               qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!qa_q1_player_source_present(g, actor) || !player || !input ||
        !qa_vec_finite(input->view_angles) || input->water_level > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 source player input");
        return false;
    }
    player->input = *input;
    if (input->impulse) player->source_impulse = input->impulse;
    player->source_use = input->use;
    if (player->character) {
        player->character_state.input.attack = input->attack;
        player->character_state.input.jump = input->jump;
        player->character_state.input.use = input->use;
        player->character_state.input.water_level = input->water_level;
        player->character_state.input.water_type = input->water_type;
    }
    return true;
}
bool qa_q1_player_input(qa_q1_game *g, qa_actor_id actor, const qa_q1_input *input,
                        qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal || !input || !qa_vec_finite(input->view_angles) ||
        input->water_level > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 player input");
        return false;
    }
    player->input = *input;
    player->grapple_input = *input;
    player->grapple_release = !input->attack && (player->weapon == QA_Q1_ROGUE_GRAPPLE ||
                                                 player->weapon == QA_Q1_CTF_GRAPPLE);
    if (!input->attack && player->continuous) {
        player->continuous = false;
        player->animation_at = -1;
        player->weapon_frame = 0;
        return q1_weapon_event(g, player, 0, 0, error);
    }
    return true;
}
bool qa_q1_player_select(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal || weapon < QA_Q1_AXE || weapon >= QA_Q1_WEAPON_COUNT ||
        !owns(g, actor, weapon))
        return false;
    player->weapon = weapon;
    player->weapon_frame = 0;
    player->continuous = false;
    player->animation_at = -1;
    return q1_weapon_event(g, player, 0, 0, error);
}
bool q1_player_select_read(qa_q1_game *g, qa_actor_id actor, q1_player *player,
    qa_q1_weapon weapon, qa_error *error) {
    if (weapon < QA_Q1_AXE || weapon >= QA_Q1_WEAPON_COUNT) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Invalid selected Q1 weapon");
        return false;
    }
    qa_inventory_entry owned;
    if (!best_player_current(g, actor, player, error) ||
        !qa_inventory_entry_read(g->services.inventory, actor, g->weapons[weapon], &owned, error) ||
        !best_player_current(g, actor, player, error)) return false;
    if (owned.count == 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Selected Q1 weapon is not owned");
        return false;
    }
    player->weapon = weapon;
    player->weapon_frame = 0;
    player->continuous = false;
    player->animation_at = -1;
    return q1_weapon_event(g, player, 0, 0, error) &&
        best_player_current(g, actor, player, error);
}
bool qa_q1_player_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_player_view *out) {
    if (!g || !out || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !player->arsenal || !qa_actor_id_equal(player->id, actor))
        return false;
    *out = (qa_q1_player_view){.weapon = player->weapon,
                               .weapon_model = weapon_model(g, player),
                               .weapon_frame = player->weapon_frame,
                               .punch_angles = player->punch,
                               .max_health = player->max_health,
                               .holstered = player->input.holstered,
                               .attack_finished = player->attack_finished,
                               .source_weapon = player->weapon == QA_Q1_AXE ? 4096u :
                                   player->weapon > QA_Q1_AXE && player->weapon <= QA_Q1_LIGHTNING ?
                                       UINT32_C(1) << ((unsigned)player->weapon - 1) : 0};
    memcpy(out->power_expires, player->power_expires, sizeof(out->power_expires));
    return true;
}
bool qa_q1_player_power(qa_q1_game *g, qa_actor_id actor, qa_q1_power power, double expires,
                        qa_error *error) {
    return q1_power_assign(g, actor, power, expires, expires != 0, error);
}
bool qa_q1_player_travel_reset(qa_q1_game *g, qa_actor_id actor, float max_health,
                               qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !isfinite(max_health) || max_health <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 native travel state");
        return false;
    }
    player->max_health = max_health;
    if (!qa_q1_player_powers_clear(g, actor, error)) return false;
    player->mega_rot_at = -1;
    return true;
}
bool qa_q1_game_invulnerable(const qa_q1_game *g, qa_actor_id actor) {
    return g && qa_q1_game_power_expires(g, actor, QA_Q1_INVULNERABILITY) > g->time;
}
double qa_q1_game_power_expires(const qa_q1_game *g, qa_actor_id actor, qa_q1_power power) {
    if (power < QA_Q1_QUAD || power >= QA_Q1_POWER_COUNT)
        return 0;
    if (!g || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return 0;
    const q1_player *player = g->players[actor.slot];
    return player && player->active && qa_actor_id_equal(player->id, actor)
               ? player->power_expires[power]
               : 0;
}
static bool player_weapon_frame(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player)
        return true;
    if (player->weapon == QA_Q1_CTF_GRAPPLE)
        return q1_grapple_weapon_frame(g, player, error);
    if (player->weapon == QA_Q1_ROGUE_GRAPPLE) {
        q1_actor *hook = q1_entity(g, player->hook);
        if (hook && hook->kind == Q1_PROJECTILE &&
            hook->state.projectile.kind == Q1_ROGUE_HOOK && player->weapon_frame == 1 &&
            g->time >= player->animation_at + 0.1) {
            player->weapon_frame = 2;
            return q1_weapon_event(g, player, 0, 0, error);
        }
        return true;
    }
    bool hammer = player->weapon == QA_Q1_MG3_MJOLNIR;
    bool mission = player->weapon >= QA_Q1_LASER && player->weapon <= QA_Q1_PLASMA;
    if ((!hammer && ((!mission && player->weapon > QA_Q1_LIGHTNING) || player->continuous)) ||
        player->animation_at < 0)
        return true;
    double frame = floor((g->time - player->animation_at) / 0.1);
    int32_t count = player->weapon == QA_Q1_AXE || hammer ? 4 : 6;
    double value = frame >= count ? 0
                     : player->weapon == QA_Q1_MJOLNIR ? fmin(4, frame + 1)
                     : mission || hammer ? frame + 1
                                         : player->animation_base + frame;
    if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 weapon animation frame out of range");
        return false;
    }
    int32_t next = (int32_t)value;
    if (next != player->weapon_frame) {
        player->weapon_frame = next;
        if (!q1_weapon_event(g, player, 0, 0, error))
            return false;
        player = q1_player_get(g, actor);
        if (!player)
            return true;
    }
    if (frame >= count)
        player->animation_at = -1;
    return true;
}
bool qa_q1_player_weapon_frame(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = player_weapon_frame(g, actor, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 teardown requested during weapon frame");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
static bool player_prethink(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    double seconds = g->time;
    uint64_t frame_ns = g->time_ns;
    q1_player *player = q1_player_get(g, actor);
    if (!player)
        return true;
    if (!qa_q1_game_map_coordinate_dump(g, actor, player->attack_finished, error))
        return false;
    player = q1_player_get(g, actor);
    if (!player)
        return true;
    if (g->options.program == QA_Q1_MG3 && g->maps) {
        qa_q1_character_view view;
        qa_builtin_actor_traits traits = {0};
        qa_vec3 offset;
        if (qa_q1_character_read(g, actor, &view))
            offset = view.view_offset;
        else if (g->services.actor_traits &&
                 g->services.actor_traits(g->services.context, actor, &traits))
            offset = qa_v3(0, 0, traits.view_height);
        else {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "MG3 player frame requires selected view owner");
            return false;
        }
        if (!q1_alive(g, actor))
            return true;
        if (!qa_q1_game_map_addon_player_frame(g, actor, offset, error))
            return false;
        player = q1_player_get(g, actor);
        if (!player)
            return true;
    }
    if (g->options.program == QA_Q1_MG3 && !q1_mg3_weapon_frame(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!q1_power_frame(g, player, seconds, frame_ns, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!q1_grapple_frame(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!player_weapon_frame(g, actor, error))
        return false;
    player = q1_player_get(g, actor);
    if (!player)
        return true;
    if (player->mega_rot_at >= 0 && player->mega_rot_at <= g->time) {
        float health = q1_health(g, actor);
        if (!q1_alive(g, actor))
            return true;
        if (health > player->max_health) {
            if (!qa_combat_set_health(g->services.combat, actor, health - 1, error))
                return false;
            if (!q1_alive(g, actor))
                return true;
            player->mega_rot_at = g->time + 1;
        } else
            player->mega_rot_at = -1;
    }
    if (!q1_powers_expire(g, actor, seconds, error)) return false;
    float magnitude = qa_vec_length(player->punch);
    if (magnitude > 0)
        player->punch =
            qa_vec_scale(player->punch, fmaxf(0, magnitude - (float)g->elapsed * 10) / magnitude);
    return true;
}
bool qa_q1_player_prethink(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = player_prethink(g, actor, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 teardown requested during player prethink");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_environment_damage(qa_q1_game *g, qa_actor_id actor, float amount, qa_hazard hazard,
                           qa_error *error) {
    qa_actor_id world = g && g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
    qa_body_state body;
    if (!world.registry || !qa_actors_get(qa_session_actors(g->services.session), world) ||
        !qa_world_body_read(g->services.world, world, &body, error)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Q1 environmental damage requires its installed source world body");
        return false;
    }
    qa_damage_request request = {.attack =
                                     q1_attack(g, world, world, QA_Q1_WEAPON_COUNT),
                                 .target = actor,
                                 .amount = amount};
    request.attack.cause = (qa_damage_cause){.kind = QA_CAUSE_ENVIRONMENT, .source.hazard = hazard};
    if (g->host.combat_provider)
        request.attack.combat_provider = g->host.combat_provider(g->host.context, actor);
    if (!qa_attack_next(&g->attack_sequence, &request.attack, error))
        return false;
    qa_damage_outcome outcome = {0};
    bool ok = qa_combat_apply(g->services.combat, &request, &outcome, error);
    qa_damage_outcome_free(&outcome);
    return ok;
}
bool qa_q1_player_environment(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || q1_health(g, actor) < 0)
        return true;
    bool suit = player->power_expires[QA_Q1_SUIT] > g->time;
    bool lava_suit = player->power_expires[QA_Q1_LAVA_SUIT] > g->time;
    if (player->input.water_level != 3 || suit || lava_suit) {
        player->air_finished = g->time + 12;
        player->drown_damage = 2;
    } else if (player->air_finished < g->time && player->drown_at < g->time) {
        player->drown_damage += 2;
        if (player->drown_damage > 15)
            player->drown_damage = 10;
        if (!q1_environment_damage(g, actor, player->drown_damage, QA_HAZARD_DROWN, error))
            return false;
        player->drown_at = g->time + 1;
    }
    if (player->input.water_level && player->hazard_at < g->time) {
        if (player->input.water_type == -5 && !lava_suit) {
            player->hazard_at = g->time + (suit ? 1 : 0.2);
            return q1_environment_damage(g, actor, 10.0f * player->input.water_level,
                                         QA_HAZARD_LAVA, error);
        }
        if (player->input.water_type == -4 && !suit && !lava_suit) {
            player->hazard_at = g->time + 1;
            return q1_environment_damage(g, actor, 4.0f * player->input.water_level,
                                         QA_HAZARD_SLIME, error);
        }
    }
    return true;
}
bool qa_q1_player_postthink(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    if (player->input.impulse && g->time >= player->attack_finished) {
        if (!q1_weapon_impulse(g, player, player->input.impulse, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        player->input.impulse = 0;
    }
    return !player->input.attack || q1_fire(g, player, error);
}

bool q1_aim(qa_q1_game *g, qa_actor_id actor, qa_vec3 forward, qa_vec3 *out, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 20));
    qa_trace_result trace;
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(forward, 2048)), actor, true, &trace,
                  error))
        return false;
    qa_combat_state attacker;
    bool has_team =
        qa_combat_read(g->services.combat, actor, &attacker, NULL) && attacker.team != 0;
    float best = g->options.aim_threshold;
    qa_vec3 selected = {0};
    bool found = false;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        qa_actor_id target = record->id;
        if (qa_actor_id_equal(target, actor))
            continue;
        qa_q1_target observation;
        qa_combat_state combat;
        if (!q1_target(g, target, &observation) ||
            (!observation.aimed_damage && !observation.player) ||
            !qa_combat_read(g->services.combat, target, &combat, NULL) || !combat.can_take_damage ||
            (g->options.teamplay && has_team && attacker.team == combat.team))
            continue;
        if (trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, target)) {
            *out = forward;
            return true;
        }
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, error))
            return false;
        qa_vec3 end = qa_vec_add(
            other.origin, qa_vec_scale(qa_vec_add(other.bounds.mins, other.bounds.maxs), 0.5f));
        float alignment = qa_vec_dot(qa_vec_normalize(qa_vec_sub(end, start)), forward);
        if (alignment < best)
            continue;
        qa_trace_result sight;
        if (!q1_trace(g, start, end, actor, true, &sight, error))
            return false;
        if (sight.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(sight.actor, target)) {
            best = alignment;
            selected = other.origin;
            found = true;
        }
    }
    if (!found) {
        *out = forward;
        return true;
    }
    qa_vec3 delta = qa_vec_sub(selected, body.origin),
            result = qa_vec_scale(forward, qa_vec_dot(delta, forward));
    result.z = delta.z;
    *out = qa_vec_normalize(result);
    return true;
}
bool q1_bullets(qa_q1_game *g, qa_actor_id actor, qa_vec3 direction, qa_vec3 angles, unsigned count,
                float spread_x, float spread_y, qa_q1_weapon weapon, qa_error *error) {
    const qa_q1_weapon_view *shape = q1_weapon_shape(QA_Q1_SHOTGUN);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 source = qa_vec_add(body.origin, qa_vec_scale(g->forward, 10));
    source.z =
        body.origin.z + body.bounds.mins.z + (body.bounds.maxs.z - body.bounds.mins.z) * 0.7f;
    qa_actor_id pending = {0};
    float amount = 0;
    for (unsigned i = 0; i < count; ++i) {
        float x = (q1_random(g) * 2 - 1) * spread_x, y = (q1_random(g) * 2 - 1) * spread_y;
        qa_vec3 ray =
            qa_vec_add(direction, qa_vec_add(qa_vec_scale(g->right, x), qa_vec_scale(g->up, y)));
        qa_trace_result trace;
        if (!q1_trace(g, source, qa_vec_add(source, qa_vec_scale(ray, shape->range)), actor, true, &trace,
                      error))
            return false;
        if (trace.fraction == 1)
            continue;
        qa_vec3 point = qa_vec_sub(trace.end, qa_vec_scale(ray, 4));
        if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, point, shape->damage, 1, error))
                return false;
            if (!qa_actor_id_equal(pending, trace.actor)) {
                if (pending.registry && q1_alive(g, pending) &&
                    !q1_damage(g, pending, actor, actor, amount, weapon, error))
                    return false;
                pending = trace.actor;
                amount = 0;
            }
            amount += shape->damage;
        } else if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, point, 1, 2, error))
            return false;
    }
    return !pending.registry || !q1_alive(g, pending) ||
           q1_damage(g, pending, actor, actor, amount, weapon, error);
}
static bool lightning(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    float cells = (float)q1_ammo_count(g, player->id, QA_Q1_CELLS);
    if (player->input.water_level > 1)
        return q1_consume(g, player->id, QA_Q1_CELLS, cells, error) &&
               q1_radius_typed(g, player->id, player->id, 35 * cells, (qa_actor_id){0},
                               QA_Q1_LIGHTNING, "discharge", error);
    if (!q1_consume(g, player->id, QA_Q1_CELLS, 1, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result wall;
    const qa_q1_weapon_view *shape = q1_weapon_shape(QA_Q1_LIGHTNING);
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(g->forward, shape->range)), player->id, false,
                  &wall, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .actor = player->id,
                              .provider = g->options.provider,
                              .origin = start,
                              .end = wall.end,
                              .code = 2,
                              .time_ns = g->time_ns};
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    qa_vec3 end = qa_vec_add(wall.end, qa_vec_scale(g->forward, 4));
    return q1_lightning_rays(g, player->id, player->id, body.origin, end, shape->damage, 120, 1,
                             qa_v3(0, 0, 0), 0, QA_Q1_LIGHTNING, NULL, error);
}

static bool parameters_valid(qa_q1_game *g, qa_actor_id actor,
                              const qa_q1_weapon_parameters *parameters, bool zero_interval,
                              qa_error *error) {
    if (!q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 weapon policy retired the source actor");
        return false;
    }
    if (!isfinite(parameters->interval) || parameters->interval < 0 ||
        (!zero_interval && parameters->interval == 0) ||
        !isfinite(parameters->nail_speed) || parameters->nail_speed <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "invalid selected Q1 weapon timing or projectile speed");
        return false;
    }
    return true;
}

bool q1_weapon_parameters(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
                          qa_q1_weapon_parameters *parameters, qa_error *error) {
    if (g->host.weapon_parameters &&
        !g->host.weapon_parameters(g->host.context, actor, weapon, parameters, error))
        return false;
    return parameters_valid(g, actor, parameters, false, error);
}

bool q1_weapon_attack_delay(qa_q1_game *g, q1_player *player, float *delay, qa_error *error) {
    qa_actor_id actor = player->id;
    if (g->host.attack_delay &&
        !g->host.attack_delay(g->host.context, actor, player->weapon, delay, error))
        return false;
    if (q1_player_get(g, actor) != player) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 attack delay policy retired the source player");
        return false;
    }
    if (!isfinite(*delay) || *delay <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid selected Q1 attack delay");
        return false;
    }
    return true;
}

static bool before_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_actor_id actor = player->id;
    if (g->host.before_fire &&
        !g->host.before_fire(g->host.context, actor, player->weapon, error))
        return false;
    if (q1_player_get(g, actor) != player) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 firing policy retired the source player");
        return false;
    }
    return true;
}

static bool nail_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_actor_id actor = player->id;
    if (g->host.nail_fire &&
        !g->host.nail_fire(g->host.context, actor, player->weapon, error))
        return false;
    if (q1_player_get(g, actor) != player) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 nail firing policy retired the source player");
        return false;
    }
    return true;
}

static bool weapon_observe(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
                            qa_q1_weapon_view *out, bool *found, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    qa_q1_weapon_view view = *q1_weapon_shape(weapon);
    view.weapon = weapon;
    view.item = g->weapons[weapon];
    int ammo = q1_weapon_ammo(weapon);
    view.ammo = ammo < 0 ? 0 : g->ammo[ammo];
    qa_q1_weapon_parameters parameters = {.interval=view.attack_interval,.nail_speed=1000};
    if (weapon <= QA_Q1_LIGHTNING) {
        if (!base_parameters(g, player, weapon, &parameters, error))
            return false;
        if (weapon == QA_Q1_AXE) {
            bool horde;
            if (!q1_horde_axe_interval(g, player, &parameters.interval, &horde, error))
                return false;
        }
    } else if ((weapon >= QA_Q1_LASER && weapon <= QA_Q1_PROXIMITY) ||
               weapon == QA_Q1_MG3_LASER || weapon == QA_Q1_MG3_MJOLNIR) {
        if (!q1_weapon_parameters(g, actor, weapon, &parameters, error))
            return false;
    }
    if (!q1_alive(g, actor))
        return true;
    if (g->host.weapon_observation &&
        !g->host.weapon_observation(g->host.context, actor, weapon, &parameters, error))
        return false;
    if (!parameters_valid(g, actor, &parameters, weapon == QA_Q1_ROGUE_GRAPPLE, error))
        return false;
    player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    view.ammo_count = ammo < 0 ? 0 : q1_ammo_count(g, actor, (qa_q1_ammo)ammo);
    view.owned = owns(g, actor, weapon);
    view.available = view.owned && (ammo < 0 || view.ammo_count >= 1);
    view.attack_finished = player->attack_finished;
    view.ready_at = player->weapon == weapon && player->continuous
                        ? player->next_weapon_frame : player->attack_finished;
    view.attack_interval = parameters.interval;
    if (!view.fire_interval)
        view.fire_interval = parameters.interval;
    view.gravity_acceleration = view.gravity *
        (g->services.physics ? g->services.physics->gravity : g->options.gravity);
    view.launch_angles = player->input.view_angles;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(view.launch_angles, &forward, &right, &up);
    view.muzzle_count = 1;
    if (weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_SUPER_SHOTGUN) {
        shotgun_shape(player,weapon,view.ammo_count,&view);
        view.muzzle_offsets[0] = qa_vec_scale(forward, 10);
        view.muzzle_offsets[0].z = body.bounds.mins.z +
                                  (body.bounds.maxs.z-body.bounds.mins.z)*.7f;
    } else if (weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN ||
               weapon == QA_Q1_LAVA_NAILGUN || weapon == QA_Q1_LAVA_SUPER_NAILGUN) {
        bool super = (weapon == QA_Q1_SUPER_NAILGUN || weapon == QA_Q1_LAVA_SUPER_NAILGUN) &&
                     view.ammo_count >= 2;
        if (!super) {
            view.ammo_per_shot = 1;
            view.damage = q1_weapon_shape(QA_Q1_NAILGUN)->damage;
        }
        if (weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN) {
            view.speed = parameters.nail_speed;
            view.range = (float)fmin((double)view.speed*view.lifetime,FLT_MAX);
        }
        view.muzzle_offsets[0] = qa_vec_add(qa_v3(0,0,16),
            qa_vec_scale(right, super ? 0 : (float)player->nail_side*4));
    } else if (weapon == QA_Q1_LASER || weapon == QA_Q1_MG3_LASER) {
        bool paired = player->weapon != weapon || !player->continuous || player->weapon_frame == 4;
        qa_vec3 origin = qa_vec_add(qa_vec_scale(up,6),
            qa_vec_scale(qa_vec_normalize(qa_v3(forward.x,forward.y,0)),12));
        view.shots = paired ? 2 : 1;
        if (paired) {
            float offset = 6*.707f;
            view.muzzle_count = 2;
            view.muzzle_offsets[0] = qa_vec_sub(qa_vec_add(origin,qa_vec_scale(right,offset)),
                                              qa_vec_scale(up,offset));
            view.muzzle_offsets[1] = qa_vec_sub(view.muzzle_offsets[0],qa_vec_scale(right,offset*2));
        } else
            view.muzzle_offsets[0] = qa_vec_add(origin,qa_vec_scale(up,6));
    } else if (weapon == QA_Q1_ROCKET || weapon == QA_Q1_MULTI_ROCKET || weapon == QA_Q1_PLASMA ||
               view.grapple) {
        float offset = view.grapple ? 16 : weapon == QA_Q1_PLASMA ? 24 : 8;
        view.muzzle_offsets[0] = qa_vec_add(qa_vec_scale(forward,offset),qa_v3(0,0,16));
    } else if (view.melee || weapon == QA_Q1_LIGHTNING)
        view.muzzle_offsets[0] = qa_v3(0,0,16);
    if ((weapon == QA_Q1_LIGHTNING || weapon == QA_Q1_PLASMA) && player->input.water_level > 1) {
        view.discharge = true;
        view.ammo_per_shot = view.ammo_count;
        view.speed = 0;
        view.shots = 1;
        view.damage = view.blast_damage = (float)fmin(35*view.ammo_count,FLT_MAX);
        view.range = view.blast_radius = (float)fmin((double)view.blast_damage+40,FLT_MAX);
        view.muzzle_offsets[0] = qa_v3(0,0,0);
    }
    if (player->mg3_infinite_ammo)
        view.ammo_per_shot = 0;
    if (ammo >= 0)
        view.available = view.available && view.ammo_count >= view.ammo_per_shot;
    if (weapon == QA_Q1_CTF_GRAPPLE && player->grapple_weapon.selected) {
        view.attack_finished = view.ready_at = player->grapple_weapon.attack_finished;
        view.available = view.available && player->grapple_weapon.available;
    }
    if (!q1_alive(g, actor))
        return true;
    *out = view;
    *found = true;
    return true;
}
bool qa_q1_player_weapon_read(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
                              qa_q1_weapon_view *out, bool *found, qa_error *error) {
    if (!out || !found || (unsigned)weapon >= QA_Q1_WEAPON_COUNT) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 weapon observation");
        return false;
    }
    *out = (qa_q1_weapon_view){0};
    *found = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g,&operation,error))
        return false;
    bool result = weapon_observe(g,actor,weapon,out,found,error);
    if (!qa_q1_game_operation_live(&operation)) {
        *out = (qa_q1_weapon_view){0};
        *found = false;
        if (result) {
            qa_error_set(error,QA_ERROR_ARGUMENT,actor.slot,"Q1 weapon observation requested teardown");
            result = false;
        }
    }
    qa_q1_game_operation_end(&operation);
    return result;
}

static bool bot_weapon(qa_q1_weapon weapon) {
    return (unsigned)weapon <= QA_Q1_LIGHTNING && weapon != QA_Q1_GRENADE;
}
bool qa_q1_bot_weapon_items(const qa_q1_game *g,qa_q1_weapon weapon,qa_item_id *item,
                            qa_item_id *ammunition,bool *covered,qa_error *error) {
    if (!g || !item || !ammunition || !covered || (unsigned)weapon>=QA_Q1_WEAPON_COUNT ||
        g->destroy_pending || g->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot weapon items require their source registry");
        return false;
    }
    *covered=bot_weapon(weapon);*item=0;*ammunition=0;
    if (!*covered) return true;
    *item=g->weapons[weapon];
    int ammo=q1_weapon_ammo(weapon);
    *ammunition=ammo<0?0:g->ammo[ammo];return true;
}
bool qa_q1_bot_weapon_usable(qa_q1_game *g,qa_actor_id actor,qa_q1_weapon weapon,
                             bool *out,qa_error *error) {
    if (!g || !out || (unsigned)weapon>=QA_Q1_WEAPON_COUNT || g->destroy_pending ||
        g->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot weapon usability requires its source owner");
        return false;
    }
    *out=false;
    q1_player *player=q1_player_get(g,actor);
    if (!bot_weapon(weapon) || !player || !player->arsenal) return true;
    qa_inventory_entry entry;
    double owned=qa_inventory_entry_read(g->services.inventory,actor,g->weapons[weapon],&entry,NULL)?entry.count:0;
    int ammo=q1_weapon_ammo(weapon);
    unsigned needed=weapon==QA_Q1_SUPER_SHOTGUN || weapon==QA_Q1_SUPER_NAILGUN?2:1;
    *out=owned!=0 && (ammo<0 || q1_ammo_count(g,actor,(qa_q1_ammo)ammo)>=needed) &&
        (weapon!=QA_Q1_LIGHTNING || player->input.water_level<=1);
    return true;
}
bool qa_q1_bot_weapon_state_read(const qa_q1_game *g,qa_actor_id actor,qa_q1_weapon *weapon,
                                 double *attack_finished,double *time,bool *present,qa_error *error) {
    if (!g || !weapon || !attack_finished || !time || !present || g->destroy_pending ||
        g->continuation_pending) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot weapon state requires its actual source owner");
        return false;
    }
    const q1_player *player = actor.slot < g->capacity ? g->players[actor.slot] : NULL;
    *present = player && player->active && player->arsenal &&
        qa_actor_id_equal(player->id,actor) &&
        qa_actors_get(qa_session_actors(g->services.session),actor);
    *weapon = *present ? player->weapon : QA_Q1_AXE;
    *attack_finished = *present ? player->attack_finished : 0;
    *time = g->time;
    return true;
}
bool qa_q1_bot_weapon_read(qa_q1_game *g,qa_actor_id actor,qa_q1_weapon weapon,
                           qa_q1_bot_weapon_fact *out,bool *covered,qa_error *error) {
    if (!out || !covered || (unsigned)weapon >= QA_Q1_WEAPON_COUNT) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q1 bot weapon fact request");
        return false;
    }
    *out = (qa_q1_bot_weapon_fact){0};
    *covered = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g,&operation,error)) return false;
    if (!bot_weapon(weapon)) {qa_q1_game_operation_end(&operation);return true;}
    q1_player *player = q1_player_get(g,actor);
    if (player && !player->arsenal) player = NULL;
    double nails = player ? q1_ammo_count(g,actor,QA_Q1_NAILS) : 2;
    double shells = player ? q1_ammo_count(g,actor,QA_Q1_SHELLS) : 2;
    float nail_speed = 1000;
    bool okay = !player || !g->host.bot_nail_speed ||
        g->host.bot_nail_speed(g->host.context,actor,1000,&nail_speed,error);
    if (okay && (!qa_q1_game_operation_live(&operation) || !isfinite(nail_speed) || nail_speed < 0)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q1 bot nail speed lost its source operation");
        okay = false;
    }
    if (!okay) {qa_q1_game_operation_end(&operation);return false;}
    /* The callback may retire a player while retaining the GAME allocation. */
    player = q1_player_get(g,actor);
    if (player && !player->arsenal) player = NULL;
    qa_q1_bot_weapon_fact value = {.item=g->weapons[weapon],.shots=1,.offset={0,0,-6}};
    int ammo = q1_weapon_ammo(weapon);
    value.ammo = ammo < 0 ? 0 : g->ammo[ammo];
    switch (weapon) {
    case QA_Q1_AXE: value.damage=20;value.cycle=.5;value.range=64;break;
    case QA_Q1_SHOTGUN:
    case QA_Q1_SUPER_SHOTGUN: {
        bool super = weapon==QA_Q1_SUPER_SHOTGUN && shells>1;
        value.damage=4;value.shots=super?14:6;value.cycle=weapon==QA_Q1_SHOTGUN?.5:.7;
        value.ammo_per_shot=super?2:1;value.range=2048;
        value.spread_x=super?.14:.04;value.spread_y=super?.08:.04;
        qa_body_state body;
        double height=15.2;
        if (qa_world_body_read(g->services.world,actor,&body,NULL))
            height=body.bounds.mins.z+((double)body.bounds.maxs.z-body.bounds.mins.z)*.7;
        value.offset=qa_v3(10,0,(float)(height-22));break;
    }
    case QA_Q1_NAILGUN:
    case QA_Q1_SUPER_NAILGUN: {
        bool super=weapon==QA_Q1_SUPER_NAILGUN && nails>=2;
        value.damage=super?18:9;value.cycle=.1;value.ammo_per_shot=super?2:1;
        value.speed=nail_speed;value.range=(double)nail_speed*6;
        value.offset.y=super?0:(float)(player?player->nail_side:1)*4;break;
    }
    case QA_Q1_ROCKET:
        value.damage=110;value.cycle=.8;value.ammo_per_shot=1;
        value.speed=1000;value.range=5000;value.radius=160;value.offset.x=8;break;
    case QA_Q1_LIGHTNING:
        value.damage=30;value.cycle=.1;value.ammo_per_shot=1;value.range=600;break;
    default: break;
    }
    qa_inventory_entry inventory;
    double owned=qa_inventory_entry_read(g->services.inventory,actor,value.item,&inventory,NULL)?inventory.count:0;
    value.owned=owned>0;
    okay=qa_q1_bot_weapon_usable(g,actor,weapon,&value.usable,error);
    if (!okay) {qa_q1_game_operation_end(&operation);return false;}
    *out=value;*covered=true;
    qa_q1_game_operation_end(&operation);return true;
}

static bool fire_weapon(qa_q1_game *g, q1_player *player, bool *fired, qa_error *error) {
    *fired = false;
    if (player->input.holstered || q1_health(g, player->id) <= 0 ||
        g->time < (player->continuous ? player->next_weapon_frame : player->attack_finished))
        return true;
    qa_q1_weapon weapon = player->weapon;
    int ammo = q1_weapon_ammo(weapon);
    if (ammo >= 0 && q1_ammo_count(g, player->id, (qa_q1_ammo)ammo) < 1)
        return qa_q1_player_select(g, player->id, q1_best_weapon(g, player), error);
    if (weapon > QA_Q1_LIGHTNING) {
        bool held_hook = (weapon == QA_Q1_ROGUE_GRAPPLE || weapon == QA_Q1_CTF_GRAPPLE) &&
            q1_entity(g, player->hook);
        bool okay = before_fire(g, player, error) && q1_expansion_fire(g, player, error);
        if (okay && !held_hook) *fired = true;
        return okay;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    if (!before_fire(g, player, error))
        return false;
    bool repeating = player->continuous;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward, right = g->right, up = g->up;
    qa_q1_weapon_parameters parameters;
    if (!base_parameters(g, player, weapon, &parameters, error))
        return false;
    float punch = -2;
    int32_t attack = (int32_t)weapon;
    player->continuous =
        weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN || weapon == QA_Q1_LIGHTNING;
    player->next_weapon_frame = g->time + 0.1;
    if (!player->continuous) {
        player->animation_at = g->time;
        player->animation_base = 1;
    }
    player->hostile_until = g->time + 1;
    q1_actor *projectile;
    qa_vec3 direction;
    switch (weapon) {
    case QA_Q1_AXE: {
        punch = 0;
        if (!q1_sound(g, player->id, "weapons/ax1.wav", 1, 1, error))
            return false;
        float choice = q1_random(g);
        attack = choice < 0.25f ? 0 : choice < 0.5f ? 1 : choice < 0.75f ? 2 : 3;
        player->animation_base = (choice >= 0.25f && choice < 0.5f) || choice >= 0.75f ? 5 : 1;
        if (!q1_create(g, "axe_strike", Q1_TIMER, player->id, &projectile, error) ||
            !q1_schedule(g, projectile, q1_weapon_shape(weapon)->launch_delay, Q1_THINK_AXE, error))
            return false;
        break;
    }
    case QA_Q1_SHOTGUN:
    case QA_Q1_SUPER_SHOTGUN: {
        double shells = q1_ammo_count(g,player->id,QA_Q1_SHELLS);
        bool super = weapon == QA_Q1_SUPER_SHOTGUN && shells > 1;
        qa_q1_weapon_view shape = *q1_weapon_shape(weapon);
        shotgun_shape(player,weapon,shells,&shape);
        punch = super ? -4 : -2;
        if (!q1_consume(g, player->id, QA_Q1_SHELLS, (float)shape.ammo_per_shot, error) ||
            !q1_sound(g, player->id, super ? "weapons/shotgn2.wav" : "weapons/guncock.wav", 1, 1,
                      error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !q1_bullets(g, player->id, direction, player->input.view_angles,shape.shots,
                        shape.horizontal_spread,shape.vertical_spread,weapon,error))
            return false;
        break;
    }
    case QA_Q1_NAILGUN:
    case QA_Q1_SUPER_NAILGUN: {
        bool super =
            weapon == QA_Q1_SUPER_NAILGUN && q1_ammo_count(g, player->id, QA_Q1_NAILS) >= 2;
        qa_vec3 origin = qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 16)),
                                    qa_vec_scale(right, super ? 0 : (float)player->nail_side * 4));
        if (!q1_consume(g, player->id, QA_Q1_NAILS, super ? 2 : 1, error) ||
            !q1_sound(g, player->id, super ? "weapons/spike2.wav" : "weapons/rocket1i.wav", 1, 1,
                      error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !nail_fire(g, player, error) ||
            !q1_projectile_spawn(g, player->id, weapon, super ? Q1_SUPERSPIKE : Q1_SPIKE, origin,
                                 qa_vec_scale(direction, parameters.nail_speed), &projectile,
                                 error))
            return false;
        player->nail_side = -player->nail_side;
        break;
    }
    case QA_Q1_GRENADE: {
        qa_vec3 velocity;
        if (player->input.view_angles.x == 0) {
            if (!q1_aim(g, player->id, forward, &direction, error))
                return false;
            velocity = qa_vec_scale(direction, q1_weapon_shape(weapon)->speed);
            velocity.z = q1_weapon_shape(weapon)->extra_z_velocity;
        } else {
            float x = (q1_random(g) * 2 - 1) * 10, y = (q1_random(g) * 2 - 1) * 10;
            velocity = qa_vec_add(qa_vec_add(qa_vec_scale(forward, q1_weapon_shape(weapon)->speed),
                                             qa_vec_scale(up, q1_weapon_shape(weapon)->extra_z_velocity)),
                                  qa_vec_add(qa_vec_scale(right, x), qa_vec_scale(up, y)));
        }
        if (!q1_consume(g, player->id, QA_Q1_ROCKETS, 1, error) ||
            !q1_sound(g, player->id, "weapons/grenade.wav", 1, 1, error) ||
            !q1_projectile_spawn(g, player->id, weapon, Q1_GRENADE, body.origin, velocity,
                                 &projectile, error))
            return false;
        break;
    }
    case QA_Q1_ROCKET:
        if (!q1_consume(g, player->id, QA_Q1_ROCKETS, 1, error) ||
            !q1_sound(g, player->id, "weapons/sgun1.wav", 1, 1, error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !q1_projectile_spawn(
                g, player->id, weapon, Q1_ROCKET,
                qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(forward, 8)), qa_v3(0, 0, 16)),
                qa_vec_scale(direction, q1_weapon_shape(weapon)->speed), &projectile, error))
            return false;
        break;
    case QA_Q1_LIGHTNING:
        if (player->lightning_sound_at < g->time) {
            if (!q1_sound(g, player->id, "weapons/lhit.wav", 1, 1, error))
                return false;
            player->lightning_sound_at = g->time + 0.6;
        }
        if (!lightning(g, player, error) ||
            (!repeating && !q1_sound(g, player->id, "weapons/lstart.wav", 0, 1, error)))
            return false;
        break;
    default:
        return false;
    }
    if (!q1_horde_axe_delay(g, player, &parameters.interval, error) ||
        !q1_weapon_attack_delay(g, player, &parameters.interval, error))
        return false;
    player->attack_finished = g->time + parameters.interval;
    player->weapon_frame = player->continuous
                               ? player->weapon_frame % (weapon == QA_Q1_LIGHTNING ? 4 : 8) + 1
                               : player->animation_base;
    if (punch != 0)
        player->punch.x = punch;
    bool okay = weapon_event(g, player, punch, attack, true, error) &&
        q1_effect(g, QA_BUILTIN_MUZZLE, player->id, body.origin, 0, 0, error);
    if (okay) *fired = true;
    return okay;
}
bool q1_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_actor_id actor = player->id;
    qa_item_id weapon = qa_q1_weapon_item(g, player->weapon);
    bool fired;
    bool ok = fire_weapon(g, player, &fired, error);
    if (ok && fired && g->host.fired && q1_alive(g, actor))
        ok = g->host.fired(g->host.context, actor, weapon, error);
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 source retired during weapon firing");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_axe_strike(qa_q1_game *g, q1_actor *strike, qa_error *error) {
    q1_player *player = q1_player_get(g, strike->owner);
    if (!player)
        return q1_remove(g, strike, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result trace;
    const qa_q1_weapon_view *shape = q1_weapon_shape(QA_Q1_AXE);
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(g->forward, shape->range)), player->id, true,
                  &trace, error))
        return false;
    if (trace.fraction < 1) {
        if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, trace.end, shape->damage, 1, error) ||
                !q1_damage(g, trace.actor, player->id, player->id, shape->damage, QA_Q1_AXE, error))
                return false;
        } else if (!q1_sound(g, player->id, "player/axhit2.wav", 1, 1, error) ||
                   !q1_effect(g, QA_BUILTIN_IMPACT, (qa_actor_id){0}, trace.end, 3, 2, error))
            return false;
    }
    return q1_remove(g, strike, error);
}

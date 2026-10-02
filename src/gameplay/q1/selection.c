#include "internal.h"

static bool owns(qa_q1_game *g, q1_player *player, qa_q1_weapon weapon) {
    qa_inventory_entry entry;
    return qa_inventory_entry_read(g->services.inventory, player->id, g->weapons[weapon], &entry,
                                   NULL) &&
           entry.count > 0;
}

bool qa_q1_player_selected_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse,
    bool *handled, qa_error *error) {
    if (!handled) return false;
    *handled = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = q1_player_get(g, actor);
    bool okay = player && player->arsenal && q1_alive(g, actor);
    if (!okay) qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "Selected Q1 impulse requires its admitted full actor");
    if (okay && impulse && g->time >= player->attack_finished) {
        bool known = (impulse >= 1 && impulse <= 8) || impulse == 10 || impulse == 12;
        if (g->options.program == QA_Q1_HIPNOTIC)
            known = known || impulse == 225 || impulse == 226 ||
                (g->options.edition == QA_Q1_RERELEASE && (impulse == 227 || impulse == 228));
        else if (g->options.program == QA_Q1_ROGUE)
            known = known || impulse == 20 || impulse == 21 ||
                (impulse == 22 && g->options.deathmatch && g->options.teamplay >= 4) ||
                (impulse >= 60 && impulse <= 64) ||
                (g->options.edition == QA_Q1_RERELEASE && impulse >= 65 && impulse <= 68);
        else if (g->options.program == QA_Q1_MG3)
            known = known || impulse == 225 || impulse == 9 || impulse == 99 || impulse == 100 ||
                (impulse >= 111 && impulse <= 115) || impulse == 118 || impulse == 122 ||
                impulse == 227 || impulse == 228;
        okay = known ? q1_weapon_impulse(g, player, impulse, error) :
            q1_enable_combos_read(g, actor, player, error);
        if (okay && (!qa_q1_game_operation_live(&operation) ||
            q1_player_get(g, actor) != player || !q1_alive(g, actor))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                "Selected Q1 impulse retired its actual player");
            okay = false;
        }
        if (okay) {
            player->input.impulse = 0;
            *handled = known;
        }
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool qa_q1_player_source_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse,
    bool *handled, qa_error *error) {
    if (!handled) return false;
    *handled = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    bool okay = qa_q1_player_source_present(g, actor);
    if (!okay) qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "Q1 Source impulse requires its physical client");
    if (okay && g->options.program == QA_Q1_MG3) {
        q1_player *player = q1_player_get(g, actor);
        okay = q1_mg3_impulse(g, player, impulse, handled, error);
    }
    if (okay && !*handled) okay = q1_source_impulse(g, actor, impulse, handled, error);
    if (okay && (!qa_q1_game_operation_live(&operation) ||
        !qa_q1_player_source_present(g, actor))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 Source impulse lost its physical client");
        okay = false;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
static unsigned required(qa_q1_weapon weapon) {
    return weapon == QA_Q1_SUPER_SHOTGUN || weapon == QA_Q1_SUPER_NAILGUN ||
                   weapon == QA_Q1_LAVA_SUPER_NAILGUN
               ? 2
               : 1;
}
static bool ammo(qa_q1_game *g, q1_player *player, qa_q1_weapon weapon, unsigned amount) {
    int kind = q1_weapon_ammo(weapon);
    return kind < 0 || q1_ammo_count(g, player->id, (qa_q1_ammo)kind) >= amount;
}
static bool cycle(qa_q1_game *g, q1_player *player, bool reverse, qa_error *error) {
    static const qa_q1_weapon base[] = {QA_Q1_AXE,     QA_Q1_SHOTGUN,       QA_Q1_SUPER_SHOTGUN,
                                        QA_Q1_NAILGUN, QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE,
                                        QA_Q1_ROCKET,  QA_Q1_LIGHTNING};
    static const qa_q1_weapon hip[] = {QA_Q1_AXE,       QA_Q1_SHOTGUN,       QA_Q1_SUPER_SHOTGUN,
                                       QA_Q1_NAILGUN,   QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE,
                                       QA_Q1_PROXIMITY, QA_Q1_ROCKET,        QA_Q1_LIGHTNING,
                                       QA_Q1_LASER,     QA_Q1_MJOLNIR};
    static const qa_q1_weapon rogue[] = {
        QA_Q1_AXE,       QA_Q1_ROGUE_GRAPPLE, QA_Q1_SHOTGUN,       QA_Q1_SUPER_SHOTGUN,
        QA_Q1_NAILGUN,   QA_Q1_LAVA_NAILGUN,  QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_SUPER_NAILGUN,
        QA_Q1_GRENADE,   QA_Q1_MULTI_GRENADE, QA_Q1_ROCKET,        QA_Q1_MULTI_ROCKET,
        QA_Q1_LIGHTNING, QA_Q1_PLASMA};
    qa_q1_weapon mg3[] = {owns(g, player, QA_Q1_MG3_MJOLNIR) ? QA_Q1_MG3_MJOLNIR : QA_Q1_AXE,
                          QA_Q1_SHOTGUN,
                          QA_Q1_SUPER_SHOTGUN,
                          QA_Q1_NAILGUN,
                          QA_Q1_SUPER_NAILGUN,
                          QA_Q1_GRENADE,
                          QA_Q1_ROCKET,
                          QA_Q1_LIGHTNING,
                          QA_Q1_MG3_LASER};
    const qa_q1_weapon *order = base;
    int count = (int)(sizeof(base) / sizeof(*base)), current = -1;
    if (g->options.program == QA_Q1_HIPNOTIC) {
        order = hip;
        count = (int)(sizeof(hip) / sizeof(*hip));
    }
    if (g->options.program == QA_Q1_ROGUE) {
        order = rogue;
        count = (int)(sizeof(rogue) / sizeof(*rogue));
    }
    if (g->options.program == QA_Q1_MG3) {
        order = mg3;
        count = (int)(sizeof(mg3) / sizeof(*mg3));
    }
    for (int i = 0; i < count; ++i)
        if (order[i] == player->weapon) {
            current = i;
            break;
        }
    if (g->options.program == QA_Q1_MG3 &&
        (player->weapon == QA_Q1_AXE || player->weapon == QA_Q1_MG3_MJOLNIR))
        current = 0;
    if (g->options.program == QA_Q1_MG3 && current < 0)
        return true;
    for (int step = 1; step <= count; ++step) {
        qa_q1_weapon weapon = order[(current + (reverse ? -step : step) + 2 * count) % count];
        if (weapon == QA_Q1_ROGUE_GRAPPLE && !(g->options.deathmatch && g->options.teamplay >= 4))
            continue;
        unsigned needed = reverse && weapon == QA_Q1_LAVA_NAILGUN ? 2 : required(weapon);
        if (owns(g, player, weapon) && ammo(g, player, weapon, needed))
            return qa_q1_player_select(g, player->id, weapon, error);
    }
    return true;
}
static qa_q1_weapon paired(qa_q1_game *g, q1_player *player, qa_q1_weapon base,
                           qa_q1_weapon powered) {
    return owns(g, player, powered) &&
                   (player->weapon == base || !ammo(g, player, base, required(base)))
               ? powered
               : base;
}
static const char *switch_message(qa_q1_weapon previous, qa_q1_weapon selected) {
    switch (previous) {
    case QA_Q1_LAVA_NAILGUN:
    case QA_Q1_LAVA_SUPER_NAILGUN:
        return selected == QA_Q1_NAILGUN || selected == QA_Q1_SUPER_NAILGUN ? "$qc_normal_nails"
                                                                            : "";
    case QA_Q1_MULTI_GRENADE:
        return selected == QA_Q1_GRENADE ? "$qc_normal_grenades" : "";
    case QA_Q1_MULTI_ROCKET:
        return selected == QA_Q1_ROCKET ? "$qc_normal_rockets" : "";
    case QA_Q1_PLASMA:
        return selected == QA_Q1_LIGHTNING ? "$qc_lightning_gun" : "";
    default:
        break;
    }
    switch (selected) {
    case QA_Q1_LAVA_NAILGUN:
    case QA_Q1_LAVA_SUPER_NAILGUN:
        return "$qc_lava_nails";
    case QA_Q1_MULTI_GRENADE:
        return "$qc_multi_gl";
    case QA_Q1_MULTI_ROCKET:
        return "$qc_multi_rl";
    case QA_Q1_PLASMA:
        return "$qc_plasma_gun";
    default:
        return "";
    }
}
bool q1_weapon_impulse(qa_q1_game *g, q1_player *player, uint8_t impulse, qa_error *error) {
    if (!q1_enable_combos_read(g, player->id, player, error))
        return false;
    if (!q1_alive(g, player->id))
        return true;
    if (g->options.program == QA_Q1_ROGUE && (impulse == 20 || impulse == 21))
        return q1_rogue_toss(g, player, impulse == 21, error);
    if (g->options.program == QA_Q1_MG3) {
        bool handled;
        if (!q1_mg3_impulse(g, player, impulse, &handled, error))
            return false;
        if (handled || !q1_alive(g, player->id))
            return true;
    }
    bool source_handled;
    if (!q1_source_impulse(g, player->id, impulse, &source_handled, error))
        return false;
    if (source_handled || !q1_alive(g, player->id))
        return true;
    if (impulse == 10 || impulse == 12)
        return cycle(g, player, impulse == 12, error);
    qa_q1_weapon selected =
        impulse >= 1 && impulse <= 8 ? (qa_q1_weapon)(impulse - 1) : QA_Q1_WEAPON_COUNT;
    bool hip = g->options.program == QA_Q1_HIPNOTIC, rogue = g->options.program == QA_Q1_ROGUE;
    if (g->options.program == QA_Q1_MG3) {
        if (impulse == 1)
            selected = owns(g, player, QA_Q1_MG3_MJOLNIR) ? QA_Q1_MG3_MJOLNIR : QA_Q1_AXE;
        if (impulse == 225)
            selected = QA_Q1_MG3_LASER;
    }
    if (hip) {
        if (impulse == 6)
            selected = player->weapon == QA_Q1_GRENADE ? QA_Q1_PROXIMITY : QA_Q1_GRENADE;
        if (impulse == 225)
            selected = QA_Q1_LASER;
        if (impulse == 226)
            selected = QA_Q1_MJOLNIR;
        if (g->options.edition == QA_Q1_RERELEASE && impulse == 227)
            selected = QA_Q1_PROXIMITY;
        if (g->options.edition == QA_Q1_RERELEASE && impulse == 228)
            selected = QA_Q1_GRENADE;
        if (selected == QA_Q1_GRENADE && !owns(g, player, selected))
            selected = QA_Q1_PROXIMITY;
    }
    if (rogue) {
        bool ctf = g->options.deathmatch && g->options.teamplay >= 4;
        switch (impulse) {
        case 1:
            selected = ctf && player->weapon == QA_Q1_AXE ? QA_Q1_ROGUE_GRAPPLE : QA_Q1_AXE;
            break;
        case 4:
            selected = paired(g, player, QA_Q1_NAILGUN, QA_Q1_LAVA_NAILGUN);
            break;
        case 5:
            selected = paired(g, player, QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_SUPER_NAILGUN);
            break;
        case 6:
            selected = paired(g, player, QA_Q1_GRENADE, QA_Q1_MULTI_GRENADE);
            break;
        case 7:
            selected = paired(g, player, QA_Q1_ROCKET, QA_Q1_MULTI_ROCKET);
            break;
        case 8:
            selected = paired(g, player, QA_Q1_LIGHTNING, QA_Q1_PLASMA);
            break;
        case 22:
            selected = ctf ? QA_Q1_ROGUE_GRAPPLE : QA_Q1_WEAPON_COUNT;
            break;
        case 60:
            selected = QA_Q1_LAVA_NAILGUN;
            break;
        case 61:
            selected = QA_Q1_LAVA_SUPER_NAILGUN;
            break;
        case 62:
            selected = QA_Q1_MULTI_GRENADE;
            break;
        case 63:
            selected = QA_Q1_MULTI_ROCKET;
            break;
        case 64:
            selected = QA_Q1_PLASMA;
            break;
        case 65:
        case 66:
        case 67:
        case 68:
            if (g->options.edition == QA_Q1_RERELEASE)
                selected = (qa_q1_weapon)(QA_Q1_NAILGUN + impulse - 65);
            break;
        default:
            break;
        }
    }
    if (selected == QA_Q1_WEAPON_COUNT)
        return true;
    if (!owns(g, player, selected))
        return q1_message(g, player->id, "$qc_no_weapon", error);
    unsigned needed = rogue && impulse == 61 ? 1 : required(selected);
    bool enough = ammo(g, player, selected, needed);
    if (rogue && impulse >= 65 && impulse <= 68 && player->weapon >= QA_Q1_LAVA_NAILGUN &&
        player->weapon <= QA_Q1_PLASMA)
        enough = q1_ammo_count(g, player->id,
                               impulse <= 66 ? QA_Q1_LAVA_NAILS : QA_Q1_MULTI_ROCKETS) >= needed;
    if (!enough)
        return q1_message(g, player->id, "$qc_not_enough_ammo", error);
    if (rogue && selected != player->weapon &&
        !q1_message(g, player->id, switch_message(player->weapon, selected), error))
        return false;
    return !q1_alive(g, player->id) || qa_q1_player_select(g, player->id, selected, error);
}

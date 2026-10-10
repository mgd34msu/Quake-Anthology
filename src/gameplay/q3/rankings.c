#include "internal.h"
#include "qa/game_type.h"

static bool enabled(const qa_q3_game *game) {
    return game && game->options.hooks.ranking_report &&
           (!game->options.hooks.ranking_warmup ||
            !game->options.hooks.ranking_warmup(game->options.hooks.context));
}

static bool integer(qa_q3_game *game, int32_t self, int32_t other, int32_t key,
                    int32_t value, qa_error *error) {
    qa_ranking_source_report report = {
        .self = self, .other = other,
        .stat = {.kind = QA_RANKING_INTEGER, .key = key,
                 .value.integer = {.value = value, .accumulate = true}}};
    return game->options.hooks.ranking_report(game->options.hooks.context, &report, error);
}

static int32_t weapon_group(qa_q3_weapon weapon) {
    static const uint8_t groups[QA_Q3_WEAPON_COUNT] = {0, 1, 2, 3, 4, 5, 8, 7, 6, 9, 10};
    return (unsigned)weapon < QA_Q3_WEAPON_COUNT ? groups[weapon] : 0;
}

static int32_t damage_group(int32_t method) {
    switch (method) {
    case 2: return 1;
    case 3: return 2;
    case 1: return 3;
    case 4: case 5: return 4;
    case 6: case 7: return 5;
    case 8: case 9: return 6;
    case 10: return 7;
    case 11: return 8;
    case 12: case 13: return 9;
    case 23: return 10;
    default: return 11;
    }
}

static int32_t source_method(const qa_q3_game *game, const qa_damage_request *request) {
    int32_t method = request->attack.cause.kind == QA_CAUSE_Q3
                         ? request->attack.cause.source.q3.means_of_death : 0;
    return game->options.product == QA_Q3_TEAM_ARENA && method >= 23
               ? (method == 28 ? 23 : 0) : method;
}

static bool pair(qa_q3_game *game, int32_t self, int32_t base, int32_t detail,
                 int32_t value, qa_error *error) {
    return integer(game, self, -1, base, value, error) &&
           (!detail || integer(game, self, -1, detail, value, error));
}

bool q3_ranking_fire(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
                      qa_error *error) {
    if (!enabled(game) || weapon == QA_Q3_W_GAUNTLET)
        return true;
    int32_t group = weapon_group(weapon);
    return pair(game, q3_entity_number(game, actor), 1111020002,
                group ? 1111020002 + 100 * group : 0, 1, error);
}

bool q3_ranking_pickup(qa_q3_game *game, qa_actor_id actor, const qa_q3_item *item,
                        int32_t quantity, qa_error *error) {
    if (!enabled(game))
        return true;
    int32_t self = q3_entity_number(game, actor), group;
    switch (item->kind) {
    case QA_Q3_ITEM_WEAPON:
        group = weapon_group((qa_q3_weapon)item->tag);
        return pair(game, self, 1111020009, group ? 1111020009 + 100 * group : 0, 1, error);
    case QA_Q3_ITEM_AMMO: {
        static const uint8_t groups[QA_Q3_WEAPON_COUNT] = {0, 0, 1, 2, 3, 4, 7, 6, 5, 8};
        group = (unsigned)item->tag < QA_Q3_WEAPON_COUNT ? groups[item->tag] : 0;
        return integer(game, self, -1, 1111030000, 1, error) &&
               integer(game, self, -1, 1111030001, quantity, error) &&
               (!group || (integer(game, self, -1, 1111030000 + 100 * group, 1, error) &&
                           integer(game, self, -1, 1111030001 + 100 * group, quantity, error)));
    }
    case QA_Q3_ITEM_HEALTH:
    case QA_Q3_ITEM_ARMOR: {
        bool health = item->kind == QA_Q3_ITEM_HEALTH;
        int32_t amount = health ? quantity : item->quantity;
        group = amount == 5 ? 1 : health && amount == 25 ? 2
                : amount == 50 ? (health ? 3 : 2) : amount == 100 ? (health ? 4 : 3) : 0;
        int32_t base = health ? 1111040000 : 1111050000;
        return integer(game, self, -1, base, 1, error) &&
               integer(game, self, -1, base + 1, amount, error) &&
               (!group || integer(game, self, -1, base + 100 * group, 1, error));
    }
    case QA_Q3_ITEM_POWERUP:
    case QA_Q3_ITEM_TEAM:
        if (item->tag == QA_Q3_P_REDFLAG || item->tag == QA_Q3_P_BLUEFLAG)
            return integer(game, self, -1, 1111110000, 1, error);
        if (item->kind == QA_Q3_ITEM_TEAM)
            return true;
        group = item->tag >= QA_Q3_P_QUAD && item->tag <= QA_Q3_P_FLIGHT ? item->tag : 0;
        return pair(game, self, 1111060000, group ? 1111060000 + 100 * group : 0, 1, error);
    case QA_Q3_ITEM_HOLDABLE:
        return item->tag == QA_Q3_H_MEDKIT || item->tag == QA_Q3_H_TELEPORTER
                   ? integer(game, self, -1, item->tag == QA_Q3_H_MEDKIT
                                               ? 1111070000 : 1111070100, 1, error) : true;
    default:
        return true;
    }
}

bool q3_ranking_holdable(qa_q3_game *game, qa_actor_id actor, qa_q3_holdable holdable,
                          qa_error *error) {
    return !enabled(game) || (holdable != QA_Q3_H_MEDKIT && holdable != QA_Q3_H_TELEPORTER) ||
           integer(game, q3_entity_number(game, actor), -1,
                    holdable == QA_Q3_H_MEDKIT ? 1111070001 : 1111070101, 1, error);
}

bool q3_ranking_reward(qa_q3_game *game, qa_actor_id actor, uint32_t award,
                        qa_error *error) {
    return !enabled(game) || (award != 0x8000u && award != 8u) ||
           integer(game, q3_entity_number(game, actor), -1,
                    award == 0x8000u ? 1111090000 : 1111090100, 1, error);
}

bool qa_q3_ranking_capture(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game)
        return q3_fail(error, "missing Q3 ranking capture provider");
    return !enabled(game) || integer(game, q3_entity_number(game, actor), -1, 1111110001, 1, error);
}

bool qa_q3_ranking_flag_pickup(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game)
        return q3_fail(error, "missing Q3 ranking flag provider");
    return !enabled(game) || integer(game, q3_entity_number(game, actor), -1, 1111110000, 1, error);
}

bool qa_q3_ranking_team_name(qa_q3_game *game, qa_actor_id actor, const char *name,
                             qa_error *error) {
    if (!game || !name)
        return q3_fail(error, "missing Q3 ranking team name");
    if (!enabled(game))
        return true;
    qa_ranking_source_report report = {
        .self = q3_entity_number(game, actor), .other = -1,
        .stat = {.kind = QA_RANKING_STRING, .key = 1100100007, .value.string = name}};
    return game->options.hooks.ranking_report(game->options.hooks.context, &report, error);
}

bool qa_q3_ranking_weapon_time(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
                               int32_t time, qa_error *error) {
    if (!game || (unsigned)weapon >= QA_Q3_WEAPON_COUNT)
        return q3_fail(error, "invalid Q3 ranking weapon time");
    if (time <= 0 || !enabled(game))
        return true;
    int32_t group = weapon_group(weapon);
    return pair(game, q3_entity_number(game, actor), 1111020010,
                group ? 1111020010 + 100 * group : 0, time, error);
}

bool qa_q3_ranking_damage(qa_q3_game *game, const qa_damage_outcome *outcome,
                           qa_error *error) {
    if (!game || !outcome)
        return q3_fail(error, "invalid Q3 ranking damage outcome");
    if (!enabled(game) || outcome->stale || !q3_is_player(game, outcome->request.target))
        return true;
    bool armor_written;
    float armor = q3_damage_regular_delta(outcome, &armor_written);
    if (outcome->result.applied_damage == 0 && !armor_written)
        return true;
    float amount = outcome->result.applied_damage + armor;
    const qa_damage_request *request = &outcome->request;
    int32_t self = q3_entity_number(game, request->target);
    int32_t attacker = q3_entity_number(game, request->attack.attacker);
    int32_t method = source_method(game, request);
    bool new_hit = !game->ranking_hit.valid || game->ranking_hit.frame != game->now_ms ||
                   game->ranking_hit.self != self || game->ranking_hit.attacker != attacker ||
                   game->ranking_hit.method != method;
    game->ranking_hit = (qa_q3_ranking_hit){game->now_ms, self, attacker, method, true};
    bool attacker_player = q3_is_player(game, request->attack.attacker);
    if (attacker != 1022 && attacker != self && method == 2 && attacker_player &&
        !integer(game, attacker, -1, 1111020102, 1, error))
        return false;
    if ((method >= 14 && method <= 20) || method == 22)
        return true;
    int32_t group = damage_group(method);
    int32_t damage = qa_source_float_to_i32(amount);
    bool splash = method == 5 || method == 7 || method == 9 || method == 13;
    if ((new_hit && !pair(game, self, 1111020004, 1111020004 + 100 * group, 1, error)) ||
        !pair(game, self, 1111020006, 1111020006 + 100 * group, damage, error) ||
        (splash && damage &&
         !pair(game, self, 1111020008, 1111020008 + 100 * group, damage, error)))
        return false;
    if (attacker != 1022 && attacker != self && attacker_player) {
        if ((new_hit && !pair(game, attacker, 1111020003, 1111020003 + 100 * group, 1, error)) ||
            !pair(game, attacker, 1111020005, 1111020005 + 100 * group, damage, error) ||
            (splash && damage &&
             !pair(game, attacker, 1111020007, 1111020007 + 100 * group, damage, error)))
            return false;
    }
    if (attacker != self && attacker_player && qa_game_type_has_allies(game->options.rules.game_type) &&
        game->options.hooks.source_team &&
        game->options.hooks.source_team(game->options.hooks.context, request->target) ==
            game->options.hooks.source_team(game->options.hooks.context, request->attack.attacker)) {
        if ((new_hit && (!integer(game, self, -1, 1111100002, 1, error) ||
                         !integer(game, attacker, -1, 1111100001, 1, error))) ||
            !integer(game, self, -1, 1111100004, damage, error) ||
            !integer(game, attacker, -1, 1111100003, damage, error) ||
            (splash && damage && (!integer(game, self, -1, 1111100006, damage, error) ||
                                 !integer(game, attacker, -1, 1111100005, damage, error))))
            return false;
    }
    return true;
}

bool qa_q3_ranking_death(qa_q3_game *game, const qa_damage_outcome *outcome,
                          qa_error *error) {
    if (!game || !outcome)
        return q3_fail(error, "invalid Q3 ranking death outcome");
    if (!enabled(game) || outcome->stale || outcome->result.reaction != QA_REACTION_DEATH ||
        game->options.rules.intermission || !q3_is_player(game, outcome->request.target))
        return true;
    const qa_damage_request *request = &outcome->request;
    qa_actor_id actor = request->target;
    int32_t self = q3_entity_number(game, actor), method = source_method(game, request);
    int32_t attacker = q3_is_player(game, request->attack.attacker)
                           ? q3_entity_number(game, request->attack.attacker) : 1022;
    if (attacker == 1022) {
        int32_t group = method >= 14 && method <= 20 ? method - 13 : method == 22 ? 8 : 9;
        return pair(game, self, 1111080000, 1111080000 + 100 * group, 1, error);
    }
    int32_t group = damage_group(method);
    if (attacker == self)
        return pair(game, self, 1111020001, 1111020001 + 100 * group, 1, error);
    return integer(game, attacker, self, 1211020000, 1, error) &&
           integer(game, attacker, self, 1211020000 + 100 * group, 1, error);
}

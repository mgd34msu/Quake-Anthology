#include "qa/horde.h"
#include "internal.h"

typedef qa_horde_monster_state horde_monster;
typedef qa_horde_loot_kind horde_loot_kind;
typedef qa_horde_loot_state horde_loot;
typedef struct horde_state {
    qa_horde_options options;
    qa_horde_view value;
    qa_horde_point *points;
    size_t point_count;
    horde_monster *monsters;
    horde_loot *loot;
    qa_builtin_actor_snapshot finish_order, count_order;
    bool prepared, checking, finishing, wave_check_active;
} horde_state;
typedef struct squad_member {
    const char *name;
    qa_vec3 offset;
} squad_member;
typedef struct squad {
    squad_member members[3];
    size_t count;
    qa_horde_point_kind kind;
    bool double_demon;
} squad;
static float random_value(qa_modes *m, mode_instance *v) {
    return m->options.hooks.source_random
               ? m->options.hooks.source_random(m->options.hooks.context, v->id)
               : mode_random_float(m);
}
static bool read_manager(qa_modes *m, mode_instance *v, horde_state *h,
                         qa_string_id *target, qa_actor_id *activator, bool *present,
                         qa_error *e) {
    *target = h->options.target;
    *activator = h->options.manager;
    *present = mode_live(m, h->options.manager);
    if (!*present || !m->options.hooks.horde_manager)
        return true;
    qa_error observed = {0};
    if (!m->options.hooks.horde_manager(m->options.hooks.context, v->id,
                                         h->options.manager, target, activator, &observed)) {
        *present = false;
        if (observed.code == QA_OK)
            return true;
        if (e)
            *e = observed;
        return false;
    }
    return true;
}
static bool read_point(qa_modes *m, mode_instance *v, qa_horde_point *point,
                       bool *present, qa_error *e) {
    *present = true;
    if (!m->options.hooks.horde_point)
        return true;
    qa_error observed = {0};
    qa_vec3 origin, angles;
    qa_string_id target;
    uint32_t flags;
    if (!m->options.hooks.horde_point(m->options.hooks.context, v->id, point->actor,
                                      &origin, &angles, &target, &flags, &observed)) {
        *present = false;
        if (observed.code == QA_OK)
            return true;
        if (e)
            *e = observed;
        return false;
    }
    point->origin = origin;
    point->angles = angles;
    point->target = target;
    point->flags = flags;
    return true;
}

bool mode_horde_retire(qa_modes *m, mode_instance *v, qa_error *e) {
    horde_state *h = v->horde;
    if (!h)
        return true;
    h->finishing = true;
    h->value.spawning = false;
    h->value.next_ns = 0;
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        qa_actor_id monster = h->monsters[i].actor, loot = h->loot[i].actor;
        if (mode_live(m, monster) && !qa_session_release(m->options.services.session, monster, e))
            return false;
        h->monsters[i] = (horde_monster){0};
        if (mode_live(m, loot) && !qa_session_release(m->options.services.session, loot, e))
            return false;
        h->loot[i] = (horde_loot){0};
    }
    return true;
}

void mode_horde_free(mode_instance *v) {
    horde_state *h = v->horde;
    if (!h)
        return;
    free(h->points);
    free(h->monsters);
    free(h->loot);
    qa_builtin_snapshot_free(&h->finish_order);
    qa_builtin_snapshot_free(&h->count_order);
    free(h);
    v->horde = NULL;
}
bool qa_modes_horde_configure(qa_modes *m, qa_mode_id id, const qa_horde_options *options,
                              const qa_horde_point *points, size_t count, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || m->callback_depth || v->value.rules.kind != QA_MODE_HORDE || !options ||
        (count && !points) || options->skill < 0 || options->skill > 3 ||
        count > SIZE_MAX / sizeof(*points) || !mode_live(m, options->manager) ||
        !m->options.hooks.spawn_monster || !m->options.hooks.spawn_loot ||
        !m->options.hooks.grant_loot || !m->options.hooks.horde_head)
        return mode_fail(e, "invalid Horde admission");
    for (size_t i = 0; i < count; ++i)
        if (points[i].kind < QA_HORDE_NORMAL || points[i].kind > QA_HORDE_KEY ||
            !qa_vec_finite(points[i].origin) || !qa_vec_finite(points[i].angles))
            return mode_fail(e, "invalid Horde spawn point");
    horde_state *h = calloc(1, sizeof(*h));
    if (!h) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating Horde");
        return false;
    }
    h->points = count ? malloc(count * sizeof(*points)) : NULL;
    h->monsters = calloc(m->actor_capacity, sizeof(*h->monsters));
    h->loot = calloc(m->actor_capacity, sizeof(*h->loot));
    if ((count && !h->points) || !h->monsters || !h->loot) {
        free(h->points);
        free(h->monsters);
        free(h->loot);
        free(h);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating Horde actors");
        return false;
    }
    if (!qa_builtin_snapshot_reserve(&h->finish_order, m->actor_capacity, e) ||
        !qa_builtin_snapshot_reserve(&h->count_order, m->actor_capacity, e)) {
        free(h->points);
        free(h->monsters);
        free(h->loot);
        qa_builtin_snapshot_free(&h->finish_order);
        qa_builtin_snapshot_free(&h->count_order);
        free(h);
        return false;
    }
    if (count)
        memcpy(h->points, points, count * sizeof(*points));
    h->point_count = count;
    h->options = *options;
    h->value.next_ns = v->value.time_ns + 11 * MODE_SECOND;
    h->value.powerup_chance = .025f;
    for (size_t i = 0; i < count; ++i)
        if (h->points[i].kind == QA_HORDE_AMMO)
            h->points[i].next_ns = v->value.time_ns + 10 * MODE_SECOND +
                                   (uint64_t)(random_value(m, v) * 3 * (float)MODE_SECOND);
    mode_horde_free(v);
    v->horde = h;
    return true;
}
bool qa_modes_horde_read(qa_modes *m, qa_mode_id id, qa_horde_view *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h || !out)
        return mode_fail(e, "unknown Horde mode");
    *out = h->value;
    return true;
}
bool qa_modes_horde_manager_actor(qa_modes *m, qa_mode_id id, qa_actor_id *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h || !out || !mode_live(m, h->options.manager))
        return mode_fail(e, "Horde manager is absent or retired");
    *out = h->options.manager;
    return true;
}
bool qa_modes_horde_toggle_point(qa_modes *m, qa_mode_id id, qa_actor_id point, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h)
        return mode_fail(e, "unknown Horde mode");
    for (size_t i = 0; i < h->point_count; ++i)
        if (qa_actor_id_equal(h->points[i].actor, point)) {
            h->points[i].flags ^= 1u;
            return true;
        }
    return mode_fail(e, "unknown Horde point");
}
static size_t living(qa_modes *m, mode_instance *v) {
    size_t count = 0;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        if (player && !p->player.spectator && mode_alive(m, p->actor))
            ++count;
    }
    return count;
}
static qa_actor_id choose_target(qa_modes *m, mode_instance *v) {
    size_t count = living(m, v), seen = 0;
    if (!count)
        return (qa_actor_id){0};
    float roll = random_value(m, v) * (float)count;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        if (!p->joined || !mode_player_get(m, p->actor))
            continue;
        if (mode_alive(m, p->actor))
            ++seen;
        qa_builtin_actor_traits traits = {0};
        if (m->options.services.actor_traits)
            m->options.services.actor_traits(m->options.services.context, p->actor, &traits);
        if (roll <= (float)seen && !traits.no_target)
            return p->actor;
    }
    return (qa_actor_id){0};
}
static bool blocked(qa_modes *m, mode_instance *v, const qa_horde_point *point) {
    if (point->flags & 2u)
        return false;
    float width = point->kind == QA_HORDE_RANGED || point->kind == QA_HORDE_BOSS ? 44 : 80;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        if (!p->joined || !mode_alive(m, p->actor))
            continue;
        qa_builtin_player_info info = {0};
        if (m->options.services.player_info &&
            m->options.services.player_info(m->options.services.context, p->actor, &info) &&
            info.dead)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, p->actor, &body, NULL))
            continue;
        qa_bounds b = qa_bounds_translate(body.bounds, body.origin);
        if (b.maxs.x > point->origin.x - width && b.mins.x < point->origin.x + width &&
            b.maxs.y > point->origin.y - width && b.mins.y < point->origin.y + width)
            return true;
    }
    return false;
}
static bool choose_point(qa_modes *m, mode_instance *v, horde_state *h,
                          qa_horde_point_kind kind, qa_horde_point **out, qa_error *e) {
    *out = NULL;
    for (int pass = 0; pass < 2; ++pass) {
        size_t total = 0;
        qa_horde_point *first = NULL;
        for (size_t i = 0; i < h->point_count; ++i) {
            qa_horde_point *p = &h->points[i];
            if (p->kind != kind)
                continue;
            bool present;
            if (!read_point(m, v, p, &present, e))
                return false;
            if (!present)
                continue;
            ++total;
            if (p->kind == kind && !(p->flags & 1u) && v->value.time_ns > p->next_ns &&
                !blocked(m, v, p) && !first)
                first = p;
        }
        if (!first) {
            if (kind == QA_HORDE_NORMAL)
                return true;
            kind = QA_HORDE_NORMAL;
            continue;
        }
        float roll = random_value(m, v) * (float)total;
        size_t index = 0;
        for (size_t i = 0; i < h->point_count; ++i) {
            qa_horde_point *p = &h->points[i];
            if (p->kind != kind)
                continue;
            bool present;
            if (!read_point(m, v, p, &present, e))
                return false;
            if (!present)
                continue;
            ++index;
            if ((p->flags & 1u) || v->value.time_ns <= p->next_ns || blocked(m, v, p))
                continue;
            if ((float)index >= roll) {
                *out = p;
                return true;
            }
        }
        *out = first;
        return true;
    }
    return true;
}
static squad choose_squad(qa_modes *m, mode_instance *v, horde_state *h, int category) {
    squad result = {.kind = QA_HORDE_NORMAL};
    int pick = -1;
    float roll;
    if (h->value.army) {
        if (category == 2)
            return result;
        if (category == 1) {
            pick = random_value(m, v) * 2 < 1.5f ? 4 : 5;
            result.kind = QA_HORDE_RANGED;
        } else {
            roll = random_value(m, v) * 4;
            pick = roll < 1 ? 0 : roll < 2 ? 1 : roll < 3.5f ? 2 : 3;
            if (pick == 3)
                result.kind = QA_HORDE_RANGED;
        }
    } else if (category == 0) {
        roll = random_value(m, v) * 4;
        pick = roll < 2 ? 6 : roll < 3 ? 7 : 8;
        if (pick == 8)
            result.kind = QA_HORDE_FLYING;
    } else if (category == 1) {
        roll = random_value(m, v) * 4;
        pick = roll < 1 ? 9 : roll < 2 ? 10 : roll < 3 ? 5 : 11;
        if (pick == 5)
            result.kind = QA_HORDE_RANGED;
        else if (pick == 11)
            result.kind = QA_HORDE_FLYING;
    } else {
        roll = random_value(m, v) * 3;
        pick = roll < 1 ? 12 : roll < 2.5f ? 13 : 14;
        if (pick != 13)
            result.kind = QA_HORDE_BOSS;
    }
    int skill = h->options.skill;
    switch (pick) {
    case 0:
        result.count = skill > 0 ? 3 : 2;
        result.members[0] = (squad_member){"army", {skill > 0 ? 0 : -40, skill > 0 ? -40 : 0, 0}};
        result.members[1] = (squad_member){"army", {40, skill > 0 ? 40 : 0, 0}};
        result.members[2] = (squad_member){"army", {-40, 40, 0}};
        break;
    case 1:
        result.count = 3;
        result.members[0] = (squad_member){"dog", {44, 0, 0}};
        result.members[1] = (squad_member){"army", {-40, -40, 0}};
        result.members[2] = (squad_member){"army", {-40, 40, 0}};
        break;
    case 2:
        result.count = skill > 0 ? 2 : 1;
        result.members[0] = (squad_member){"dog", {0, -44, 0}};
        result.members[1] = (squad_member){"dog", {0, 44, 0}};
        break;
    case 3:
        result.count = 1;
        result.members[0] = (squad_member){"enforcer", {0, 0, 0}};
        break;
    case 4:
        result.count = skill > 0 ? 2 : 1;
        result.members[0] = (squad_member){"enforcer", {skill > 0 ? 40 : 0, 0, 0}};
        result.members[1] = (squad_member){"enforcer", {-40, 0, 0}};
        break;
    case 5:
        result.count = 1;
        result.members[0] = (squad_member){"ogre", {0, 0, 0}};
        break;
    case 6:
        result.count = skill > 0 ? 2 : 1;
        result.members[0] = (squad_member){"knight", {40, 0, 0}};
        result.members[1] = (squad_member){"knight", {-40, 0, 0}};
        break;
    case 7:
        result.count = 2;
        result.members[0] = (squad_member){"zombie", {40, 0, 0}};
        result.members[1] = (squad_member){"zombie", {-40, 0, 0}};
        break;
    case 8:
        result.count = 1;
        result.members[0] = (squad_member){"wizard", {0, 0, 0}};
        break;
    case 9:
        result.count = skill > 0 ? 2 : 1;
        result.members[0] = (squad_member){"hell_knight", {0, 40, 0}};
        result.members[1] = (squad_member){"hell_knight", {0, -40, 0}};
        break;
    case 10:
        result.count = 3;
        result.members[0] = (squad_member){"hell_knight", {40, 0, 0}};
        result.members[1] = (squad_member){"knight", {-40, 40, 0}};
        result.members[2] = (squad_member){"knight", {-40, -40, 0}};
        break;
    case 11:
        result.count = skill > 0 ? 3 : 2;
        result.members[0] = (squad_member){"wizard", {40, 40, 40}};
        result.members[1] = (squad_member){"wizard", {-40, 40, 40}};
        result.members[2] = (squad_member){"wizard", {-40, -40, 40}};
        break;
    case 12:
        result.count = 1;
        result.members[0] = (squad_member){"shambler", {0, 0, 0}};
        break;
    case 13: {
        result.double_demon = true;
        result.count = skill >= 1 ? 2 : 1;
        result.members[0] =
            (squad_member){"demon1",
                           {result.count == 2 ? 40 : 0, result.count == 2 ? 40 : 0, 0}};
        result.members[1] = (squad_member){"demon1", {-40, -40, 0}};
        break;
    }
    case 14:
        result.count = 1;
        result.members[0] = (squad_member){"shalrath", {0, 0, 0}};
        break;
    default:
        break;
    }
    return result;
}
static bool spawn_loot(qa_modes *m, mode_instance *v, horde_state *h, size_t point_index,
                       horde_loot_kind kind, qa_vec3 origin, qa_vec3 velocity, qa_error *e) {
    const char *classname = "item_health", *identity = NULL;
    float amount = 25, capacity = 100;
    bool big = false;
    qa_bounds bounds = {{0, 0, 0}, {32, 32, 56}};
    if (kind == HORDE_AMMO) {
        big = random_value(m, v) * 4 <= 1;
        float roll = random_value(m, v) * 20;
        if (roll <= 7) {
            classname = "item_shells";
            identity = "q1:ammo/shells";
            amount = big ? 40 : 20;
        } else if (roll <= 14) {
            classname = "item_spikes";
            identity = "q1:ammo/nails";
            amount = big ? 50 : 25;
            capacity = 200;
        } else if (roll <= 17) {
            classname = "item_rockets";
            identity = "q1:ammo/rockets";
            amount = big ? 10 : 5;
        } else {
            classname = "item_cells";
            identity = "q1:ammo/cells";
            amount = big ? 12 : 6;
        }
        float offset = big ? 16 : 12;
        origin.x -= offset;
        origin.y -= offset;
        origin.z += 1;
    } else if (kind == HORDE_HEALTH) {
        origin.x -= 16;
        origin.y -= 16;
        origin.z += 1;
    } else if (kind == HORDE_ARMOR) {
        classname = "item_armor1";
        identity = "q1:item_armor1";
        amount = 100;
        origin.z += 1;
        bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    } else if (kind == HORDE_SILVER || kind == HORDE_GOLD) {
        classname = kind == HORDE_GOLD ? "item_key2" : "item_key1";
        identity = kind == HORDE_GOLD ? "q1:key/gold" : "q1:key/silver";
        origin.z += 32;
        velocity.z = 255;
        amount = 1;
        capacity = 1;
        bounds = (qa_bounds){{-16, -16, -25}, {16, 16, 32}};
    } else if (kind == HORDE_POWER) {
        classname = random_value(m, v) < .25f ? "item_artifact_invulnerability"
                                                : "item_artifact_super_damage";
        bounds = (qa_bounds){{-12, -12, -12}, {12, 12, 12}};
    }
    qa_string_id name, item = 0;
    if (!qa_builtin_resource(&m->options.services, classname, &name, e) ||
        (identity && !qa_builtin_resource(&m->options.services, identity, &item, e)))
        return false;
    qa_actor_id actor;
    qa_mode_loot_spawn spawn = {.classname = name,
                                .body = {.origin = origin, .velocity = velocity, .bounds = bounds},
                                .spawnflags = big ? 1u : 0,
                                .bounce = kind == HORDE_POWER};
    if (!m->options.hooks.spawn_loot(m->options.hooks.context, v->id, &spawn, &actor, e))
        return false;
    if (!mode_live(m, actor))
        return mode_fail(e, "Horde loot provider did not return a live actor");
    h->loot[actor.slot] =
        (horde_loot){.actor = actor,
                     .kind = kind,
                     .point = point_index,
                     .item = item,
                     .amount = amount,
                     .capacity = capacity,
                     .alpha = 1,
                     .fade_ns = kind == HORDE_POWER ? v->value.time_ns + 10 * MODE_SECOND : 0};
    if (point_index < h->point_count) {
        h->points[point_index].occupied = true;
        h->points[point_index].next_ns = 0;
    }
    return true;
}
static bool prepare(qa_modes *m, mode_instance *v, horde_state *h, qa_error *e) {
    size_t players = living(m, v);
    if (!players) {
        h->value.next_ns = 0;
        return true;
    }
    h->value.key_spawned = false;
    ++h->value.wave;
    int level = h->value.wave + (h->options.skill >= 3 ? 6 : h->options.skill >= 2 ? 3 : 0);
    h->value.army = (level + 2) % 3 == 0;
    float scale = players >= 4 ? 2 : players >= 3 ? 1.5f : players >= 2 ? 1.25f : 1;
    if (level % 3 == 0)
        h->value.bosses = (level + 1) / 4;
    else if (h->options.skill > 1 && !h->value.army && level > 9)
        h->value.bosses = (level + 1) / 8;
    h->value.elites =
        (int)floorf((ceilf((float)(level - 1) / 3) - floorf((float)h->value.bosses / 2)) * scale);
    h->value.fodder =
        (int)floorf((float)(level + 2 - (h->value.bosses * 2 + h->value.elites)) * scale);
    h->prepared = true;
    h->checking = false;
    h->value.spawning = true;
    h->value.countdown = 3;
    for (size_t i = 0; i < h->point_count; ++i)
        if (h->points[i].kind == QA_HORDE_ITEM && !h->points[i].occupied)
            h->points[i].next_ns =
                v->value.time_ns + (uint64_t)(random_value(m, v) * 2 * (float)MODE_SECOND) + 1;
    qa_string_id target;
    qa_actor_id activator;
    bool manager_present;
    if (!read_manager(m, v, h, &target, &activator, &manager_present, e))
        return false;
    if (!manager_present)
        return true;
    if (target && m->options.services.use_targets &&
        !m->options.services.use_targets(m->options.services.context, h->options.manager,
                                         activator, target, 0, 0, e))
        return false;
    return mode_event(m, v, QA_MODE_HORDE_WAVE, (qa_actor_id){0}, (qa_actor_id){0},
                      h->options.manager, 0, h->value.wave, level, e);
}
static bool spawn_squad(qa_modes *m, mode_instance *v, horde_state *h, qa_error *e) {
    int category = h->value.fodder > 0 ? 0 : h->value.elites > 0 ? 1 : 2;
    squad s = choose_squad(m, v, h, category);
    if (!s.count) {
        h->value.bosses = 0;
        h->value.next_ns = v->value.time_ns + MODE_SECOND;
        return true;
    }
    qa_horde_point *point;
    if (!choose_point(m, v, h, s.kind, &point, e))
        return false;
    if (!point) {
        h->value.next_ns = v->value.time_ns + MODE_SECOND;
        return true;
    }
    point->next_ns = v->value.time_ns + 5 * MODE_SECOND;
    qa_string_id manager_target;
    qa_actor_id activator;
    bool manager_present;
    if (!read_manager(m, v, h, &manager_target, &activator, &manager_present, e))
        return false;
    if (!manager_present) {
        h->value.next_ns = v->value.time_ns + MODE_SECOND;
        return true;
    }
    if (point->target && m->options.services.use_targets &&
        !m->options.services.use_targets(m->options.services.context, point->actor,
                                         activator, point->target, 0, 0, e))
        return false;
    bool present;
    if (!read_point(m, v, point, &present, e))
        return false;
    if (!present) {
        h->value.next_ns = v->value.time_ns + MODE_SECOND;
        return true;
    }
    if (s.double_demon && h->options.skill >= 3 && random_value(m, v) > .8f)
        for (size_t i = 0; i < s.count; ++i)
            s.members[i].name = "shambler";
    for (size_t i = 0; i < s.count; ++i) {
        char name[48];
        size_t n = strlen(s.members[i].name);
        memcpy(name, "monster_", 8);
        memcpy(name + 8, s.members[i].name, n + 1);
        qa_string_id classname;
        qa_actor_id actor;
        if (!qa_builtin_resource(&m->options.services, name, &classname, e) ||
            !m->options.hooks.spawn_monster(m->options.hooks.context, v->id, classname,
                                            qa_vec_add(point->origin, s.members[i].offset),
                                            point->angles, choose_target(m, v), &actor, e))
            return false;
        if (!mode_live(m, actor))
            return mode_fail(e, "Horde monster provider returned no live actor");
        h->monsters[actor.slot] =
            (horde_monster){.actor = actor,
                .zombie = strcmp(s.members[i].name, "zombie") == 0};
    }
    if (category == 0)
        --h->value.fodder;
    else if (category == 1)
        --h->value.elites;
    else
        --h->value.bosses;
    if (h->value.fodder + h->value.elites + h->value.bosses <= 0) {
        h->value.spawning = false;
        h->checking = true;
        h->value.next_ns = v->value.time_ns + 30 * MODE_SECOND;
    } else
        h->value.next_ns = v->value.time_ns + 2 * MODE_SECOND +
                           (uint64_t)(random_value(m, v) * (float)MODE_SECOND);
    return true;
}
static bool check_wave(qa_modes *m, mode_instance *v, horde_state *h, qa_error *e) {
    h->value.next_ns = v->value.time_ns + 10 * MODE_SECOND;
    if (!qa_builtin_observations(&m->options.services, &h->count_order, e))
        return false;
    size_t monsters = 0;
    for (size_t i = 0; i < h->count_order.count; ++i) {
        qa_actor_id actor = h->count_order.ids[i];
        qa_builtin_actor_traits traits = {0};
        if (!mode_alive(m, actor))
            continue;
        if (m->options.services.actor_traits)
            m->options.services.actor_traits(m->options.services.context, actor, &traits);
        const char *name =
            qa_strings_cstr(qa_session_strings(m->options.services.session), traits.classname);
        if (traits.monster && (!name || strcmp(name, "monster_zombie")))
            ++monsters;
    }
    if (monsters > (h->value.wave % 3 == 0 || h->value.wave < 3 ? 0u : 5u))
        return true;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        if (p->joined && mode_player_get(m, p->actor) && !mode_alive(m, p->actor) &&
            m->options.hooks.respawn &&
            !m->options.hooks.respawn(m->options.hooks.context, v->id, p->actor, false, e))
            return false;
    }
    h->prepared = false;
    h->checking = false;
    h->value.spawning = true;
    h->value.next_ns = v->value.time_ns + (h->value.wave % 3 == 0 ? 20 : 0) * MODE_SECOND;
    if (h->value.wave % 3 == 0 && !h->value.key_spawned) {
        unsigned flag = h->value.wave <= 3   ? 1
                        : h->value.wave <= 6 ? 2
                        : h->value.wave <= 9 ? 4
                                             : 8;
        size_t first = SIZE_MAX, chosen = SIZE_MAX;
        for (size_t i = 0; i < h->point_count; ++i) {
            if (h->points[i].kind == QA_HORDE_KEY) {
                bool present;
                if (!read_point(m, v, &h->points[i], &present, e))
                    return false;
                if (!present)
                    continue;
                if (first == SIZE_MAX)
                    first = i;
                if (chosen == SIZE_MAX && (h->points[i].flags & flag))
                    chosen = i;
            }
        }
        bool gold = chosen == SIZE_MAX ? h->value.wave == 9 : flag == 4;
        if (chosen == SIZE_MAX)
            chosen = first;
        if (chosen == SIZE_MAX)
            h->value.next_ns = v->value.time_ns + 4 * MODE_SECOND;
        else {
            if (!spawn_loot(m, v, h, chosen, gold ? HORDE_GOLD : HORDE_SILVER,
                            h->points[chosen].origin, (qa_vec3){0}, e))
                return false;
            h->value.key_spawned = true;
        }
    }
    return true;
}
bool qa_modes_horde_check(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h || !v->value.rules.enabled || h->value.spawning || h->finishing || h->wave_check_active)
        return true;
    h->wave_check_active = true;
    bool ok = MODE_CALLBACK(m, check_wave(m, v, h, e));
    h->wave_check_active = false;
    return ok;
}
static bool key_owner(mode_instance *v, qa_actor_id manager) {
    horde_state *h = v->horde;
    return v->active && v->value.rules.enabled && v->value.rules.source == QA_MODE_Q1_HORDE &&
        h && qa_actor_id_equal(h->options.manager, manager);
}
static bool key_present(qa_modes *m, qa_actor_id manager, bool gold) {
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        if (!key_owner(v, manager)) continue;
        horde_state *h = v->horde;
        if ((gold ? h->value.gold_keys : h->value.silver_keys) > 0) return true;
    }
    return false;
}
static bool key_player(qa_modes *m, qa_actor_id manager, qa_actor_id actor) {
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        if (!key_owner(v, manager)) continue;
        mode_member *p = mode_member_get(m, v, actor);
        if (p) return true;
    }
    return false;
}
bool mode_horde_reconcile_keys(qa_modes *m, mode_instance *changed, qa_error *e) {
    horde_state *h = changed->horde;
    if (!h) return true;
    qa_actor_id manager = h->options.manager;
    for (unsigned gold = 0; gold < 2; ++gold) {
        bool present = key_present(m, manager, gold != 0);
        qa_item_id item;
        if (!qa_builtin_resource(&m->options.services, gold ? "q1:key/gold" : "q1:key/silver", &item, e))
            return false;
        for (size_t i = 0; i < m->players_order.count; ++i) {
            qa_actor_id actor = m->players_order.ids[i];
            if (!mode_player_get(m, actor)) continue;
            bool participating = key_player(m, manager, actor);
            if (!participating && !mode_member_get(m, changed, actor)) continue;
            if (!mode_set_count(m, actor, item, participating && present ? 1 : 0, e)) return false;
        }
    }
    return true;
}
static bool change_keys(qa_modes *m, qa_mode_id id, bool gold, int change, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h || (change != 1 && change != -1))
        return mode_fail(e, "invalid Horde key change");
    int32_t *count = gold ? &h->value.gold_keys : &h->value.silver_keys;
    if ((change < 0 && *count <= 0) || (change > 0 && *count == INT32_MAX))
        return mode_fail(e, "Horde key count out of range");
    qa_actor_id manager = h->options.manager;
    bool before = key_present(m, manager, gold);
    *count += change;
    if ((change == 1 && *count != 1) || (change == -1 && *count != 0))
        return true;
    bool present = key_present(m, manager, gold);
    if (before == present) return true;
    qa_item_id item;
    if (!qa_builtin_resource(&m->options.services, gold ? "q1:key/gold" : "q1:key/silver", &item,
                             e))
        return false;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.ids[ordinal];
        if (mode_player_get(m, actor) && key_player(m, manager, actor) &&
            !mode_set_count(m, actor, item, present ? 1 : 0, e))
            return false;
    }
    return true;
}
bool qa_modes_horde_keys(qa_modes *m, qa_mode_id id, bool gold, int change, qa_error *e) {
    if (!m) return mode_fail(e, "invalid Horde key service");
    return MODE_CALLBACK(m, change_keys(m, id, gold, change, e));
}
bool mode_horde_respawn(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    horde_state *h = v->horde;
    if (!h)
        return true;
    for (int i = 0; i < 2; ++i) {
        qa_item_id item;
        if (!qa_builtin_resource(&m->options.services, i ? "q1:key/gold" : "q1:key/silver", &item,
                                 e))
            return false;
        if (!mode_set_count(m, actor, item, key_present(m, h->options.manager, i != 0) ? 1 : 0, e))
            return false;
    }
    return true;
}
bool qa_modes_horde_request_respawn(qa_modes *m, qa_mode_id id, bool *handled, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!handled)
        return mode_fail(e, "missing Horde respawn result");
    *handled = h != NULL;
    if (!h || (h->options.cooperative && living(m, v)))
        return true;
    if (!m->options.hooks.campaign_restart)
        return mode_fail(e, "Horde defeat needs campaign restart coordinator");
    return m->options.hooks.campaign_restart(m->options.hooks.context, id,
                                             h->options.starting_campaign_flags, e);
}
bool mode_horde_death(qa_modes *m, mode_instance *v, const qa_damage_outcome *outcome,
                      qa_error *e) {
    horde_state *h = v->horde;
    if (!h || h->finishing)
        return true;
    qa_actor_id victim = outcome->request.target, attacker = outcome->request.attack.attacker;
    mode_member *killer = mode_member_get(m, v, attacker), *player = mode_member_get(m, v, victim);
    if (player) {
        if (killer && !qa_actor_id_equal(attacker, victim)) {
            killer->stats.last_kill_ns = 0;
            return qa_modes_add_score(m, v->id, attacker, -2, e);
        }
        return true;
    }
    return true;
}
static bool before_death(qa_modes *m, mode_instance *v, const qa_damage_outcome *outcome,
                         qa_error *e) {
    horde_state *h = v->horde;
    qa_actor_id victim = outcome->request.target, attacker = outcome->request.attack.attacker;
    if (!h || victim.slot >= m->actor_capacity || mode_member_get(m, v, victim))
        return true;
    mode_member *killer = mode_member_get(m, v, attacker);
    horde_monster *monster = &h->monsters[victim.slot];
    if (!qa_actor_id_equal(monster->actor, victim) || monster->counted)
        return true;
    monster->counted = true;
    monster->death_pending = true;
    if (killer) {
        killer->stats.streak = mode_add_i32(killer->stats.streak, 1);
        killer->stats.last_kill_ns = v->value.time_ns + 2 * MODE_SECOND;
        if (killer->stats.streak > 1 &&
            !mode_event(m, v, QA_MODE_HORDE_KILL_STREAK, attacker, (qa_actor_id){0}, victim, 0,
                        killer->stats.streak, 0, e))
            return false;
    }
    if (random_value(m, v) >= h->value.powerup_chance)
        h->value.powerup_chance += .025f;
    else {
        h->value.powerup_chance = .025f;
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, victim, &body, e) ||
            !spawn_loot(m, v, h, SIZE_MAX, HORDE_POWER, body.origin, qa_v3(0, 0, 300), e))
            return false;
    }
    return !killer || qa_modes_add_score(m, v->id, attacker, 1, e);
}
bool qa_modes_horde_before_death(qa_modes *m, qa_mode_id id, const qa_damage_outcome *outcome,
                                 qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !outcome)
        return mode_fail(e, "invalid Horde death");
    return !v->value.rules.enabled || MODE_CALLBACK(m, before_death(m, v, outcome, e));
}
static bool after_death(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    horde_state *h = v->horde;
    if (!h || actor.slot >= m->actor_capacity)
        return true;
    horde_monster *monster = &h->monsters[actor.slot];
    if (!qa_actor_id_equal(monster->actor, actor) || !monster->death_pending)
        return true;
    monster->death_pending = false;
    bool zombie = monster->zombie;
    if (mode_live(m, actor) && m->options.hooks.horde_head &&
        !m->options.hooks.horde_head(m->options.hooks.context, actor, v->value.rules.enabled, e))
        return false;
    return zombie || qa_modes_horde_check(m, v->id, e);
}
bool qa_modes_horde_after_death(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "unknown Horde mode");
    return MODE_CALLBACK(m, after_death(m, v, actor, e));
}
static bool loot_touch(qa_modes *m, qa_actor_id item_actor, qa_actor_id actor, bool *handled,
                       bool *accepted, qa_error *e) {
    if (!m || !handled || !accepted)
        return mode_fail(e, "invalid Horde loot touch");
    *handled = false;
    *accepted = false;
    for (uint32_t index = 0; index < m->mode_capacity; ++index) {
        mode_instance *v = &m->instances[index];
        horde_state *h = v->active ? v->horde : NULL;
        if (!h || !v->value.rules.enabled || item_actor.slot >= m->actor_capacity)
            continue;
        horde_loot *loot = &h->loot[item_actor.slot];
        if (!qa_actor_id_equal(loot->actor, item_actor) || !mode_live(m, item_actor))
            continue;
        *handled = true;
        mode_player *player = mode_player_get(m, actor);
        if (!player || !mode_alive(m, actor) || !mode_member_get(m, v, actor))
            return true;
        if (loot->kind == HORDE_AMMO) {
            double current;
            if (!mode_count(m, actor, loot->item, &current, e))
                return false;
            if (current >= loot->capacity)
                return true;
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                uint32_t i = m->players_order.ids[ordinal].slot;
                mode_member *p = &v->members[i];
                if (!p->joined || !mode_alive(m, p->actor))
                    continue;
                qa_inventory_entry entry;
                qa_error local = {0};
                if (!qa_inventory_entry_read(m->options.services.inventory, p->actor, loot->item,
                                             &entry, &local)) {
                    if (local.code != QA_ERROR_NOT_FOUND) {
                        if (e)
                            *e = local;
                        return false;
                    }
                    entry = (qa_inventory_entry){.item = loot->item,
                                                 .capacity = loot->capacity,
                                                 .policy = QA_COUNT_SOURCE_FLOAT};
                    if (!qa_inventory_configure(m->options.services.inventory, p->actor, &entry,
                                                NULL, NULL, e))
                        return false;
                }
                double given;
                if (!qa_inventory_give(m->options.services.inventory, p->actor, loot->item,
                                       loot->amount, &given, e))
                    return false;
            }
        } else if (loot->kind == HORDE_HEALTH) {
            qa_combat_state state;
            qa_builtin_actor_traits traits = {.max_health = 100};
            if (!qa_combat_read(m->options.services.combat, actor, &state, e))
                return false;
            if (m->options.services.actor_traits)
                m->options.services.actor_traits(m->options.services.context, actor, &traits);
            float maximum = traits.max_health > 0 ? traits.max_health : 100;
            if (state.health >= maximum)
                return true;
            if (!qa_combat_set_health(m->options.services.combat, actor,
                                      fminf(maximum, state.health + loot->amount), e))
                return false;
        } else if (loot->kind == HORDE_ARMOR || loot->kind == HORDE_POWER) {
            bool granted = false;
            if (!m->options.hooks.grant_loot(m->options.hooks.context, item_actor, actor, &granted,
                                             e))
                return false;
            if (!granted)
                return true;
        } else {
            if (player->value.bot)
                return true;
            if (!qa_modes_horde_keys(m, v->id, loot->kind == HORDE_GOLD, 1, e))
                return false;
            if (h->value.key_spawned) {
                h->prepared = false;
                h->checking = false;
                h->value.spawning = true;
                h->value.next_ns = v->value.time_ns;
            }
        }
        if (loot->point < h->point_count) {
            qa_horde_point *point = &h->points[loot->point];
            point->occupied = false;
            if (loot->kind == HORDE_AMMO)
                point->next_ns = v->value.time_ns + 20 * MODE_SECOND;
        }
        *accepted = true;
        loot->actor = (qa_actor_id){0};
        if (!mode_event(m, v, QA_MODE_LOOT, actor, (qa_actor_id){0}, item_actor, 0,
                        (int32_t)loot->amount, loot->kind, e))
            return false;
        return !mode_live(m, item_actor) ||
               qa_session_release(m->options.services.session, item_actor, e);
    }
    return true;
}
bool qa_modes_horde_loot_touch(qa_modes *m, qa_actor_id item, qa_actor_id player, bool *handled,
                               bool *accepted, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid Horde loot service");
    return MODE_CALLBACK(m, loot_touch(m, item, player, handled, accepted, e));
}
bool mode_horde_frame(qa_modes *m, mode_instance *v, uint64_t elapsed, qa_error *e) {
    horde_state *h = v->horde;
    if (!h || v->value.rules.paused)
        return true;
    if (h->finishing) {
        for (uint32_t i = 0; i < m->actor_capacity; ++i) {
            horde_monster *monster = &h->monsters[i];
            if (!monster->kill_ns || monster->kill_ns > v->value.time_ns)
                continue;
            monster->kill_ns = 0;
            if (!mode_live(m, monster->actor))
                continue;
            qa_damage_request request = {.target = monster->actor,
                                         .amount = 10000,
                                         .attack = {.attacker = h->options.manager,
                                                    .inflictor = h->options.manager,
                                                    .weapon_provider = m->options.owner,
                                                    .time_ns = v->value.time_ns,
                                                    .cause = {.kind = QA_CAUSE_Q1}}};
            if (!mode_damage(m, QA_GAME_Q1, &request, e))
                return false;
        }
        return true;
    }
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        if (p->joined && p->stats.streak > 0 && v->value.time_ns > p->stats.last_kill_ns) {
            int streak = p->stats.streak;
            p->stats.streak = 0;
            if (streak > 1) {
                int64_t score = ((int64_t)streak * streak + 1) / 2;
                if (!qa_modes_add_score(m, v->id, p->actor,
                                        score > INT32_MAX ? INT32_MAX : (int32_t)score, e) ||
                    !mode_event(m, v, QA_MODE_HORDE_SPREE, p->actor, (qa_actor_id){0},
                                h->options.manager, 0, streak, (int32_t)score, e))
                    return false;
            }
        }
    }
    for (size_t ordinal = 0; ordinal < m->observations.count; ++ordinal) {
        uint32_t i = m->observations.ids[ordinal].slot;
        horde_loot *loot = &h->loot[i];
        if (loot->kind != HORDE_POWER || !loot->fade_ns || !mode_live(m, loot->actor) ||
            v->value.time_ns < loot->fade_ns)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, loot->actor, &body, e))
            return false;
        if (loot->alpha == 1 && body.velocity.z < 0) {
            if (!qa_session_release(m->options.services.session, loot->actor, e))
                return false;
            loot->actor = (qa_actor_id){0};
            continue;
        }
        loot->alpha -= .25f * (float)elapsed / (float)MODE_SECOND;
        if (loot->alpha <= 0) {
            if (!qa_session_release(m->options.services.session, loot->actor, e))
                return false;
            loot->actor = (qa_actor_id){0};
        } else if (m->options.hooks.loot_alpha &&
                   !m->options.hooks.loot_alpha(m->options.hooks.context, loot->actor, loot->alpha,
                                                e))
            return false;
    }
    for (size_t i = 0; i < h->point_count; ++i) {
        qa_horde_point *point = &h->points[i];
        if (point->occupied || !point->next_ns || point->next_ns > v->value.time_ns)
            continue;
        if (point->kind == QA_HORDE_AMMO || point->kind == QA_HORDE_ITEM) {
            bool present;
            if (!read_point(m, v, point, &present, e))
                return false;
            if (!present) {
                point->next_ns = 0;
                continue;
            }
            horde_loot_kind kind = point->kind == QA_HORDE_AMMO   ? HORDE_AMMO
                                   : random_value(m, v) * 6 < 5 ? HORDE_HEALTH
                                                                  : HORDE_ARMOR;
            if (!spawn_loot(m, v, h, i, kind, point->origin, (qa_vec3){0}, e))
                return false;
        }
    }
    if (!h->value.next_ns || v->value.time_ns < h->value.next_ns)
        return true;
    if (h->checking)
        return qa_modes_horde_check(m, v->id, e);
    if (!h->prepared && !prepare(m, v, h, e))
        return false;
    if (!h->prepared)
        return true;
    if (h->value.countdown > 0) {
        int count = h->value.countdown--;
        h->value.next_ns = v->value.time_ns + MODE_SECOND;
        return mode_event(m, v, QA_MODE_HORDE_COUNTDOWN, (qa_actor_id){0}, (qa_actor_id){0},
                          h->options.manager, 0, count, 0, e);
    }
    return spawn_squad(m, v, h, e);
}
bool qa_modes_horde_finish(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    horde_state *h = v ? v->horde : NULL;
    if (!h || h->finishing)
        return true;
    h->finishing = true;
    h->value.next_ns = 0;
    h->value.spawning = false;
    if (!qa_builtin_observations(&m->options.services, &h->finish_order, e))
        return false;
    for (size_t i = 0; i < h->finish_order.count; ++i) {
        qa_actor_id actor = h->finish_order.ids[i];
        qa_builtin_actor_traits traits = {0};
        if (!m->options.services.actor_traits ||
            !m->options.services.actor_traits(m->options.services.context, actor, &traits) ||
            !traits.monster)
            continue;
        horde_monster *monster = &h->monsters[actor.slot];
        if (!qa_actor_id_equal(monster->actor, actor))
            *monster = (horde_monster){.actor = actor};
        monster->kill_ns = v->value.time_ns + MODE_SECOND / 5 +
                           (uint64_t)(random_value(m, v) * 1.8f * (float)MODE_SECOND);
    }
    return true;
}
bool qa_modes_horde_count_monster(qa_modes *m, qa_mode_id id, qa_actor_id actor) {
    mode_instance *v = mode_get(m, id);
    if (!v || !v->horde)
        return true;
    qa_builtin_actor_traits traits = {0};
    if (!m->options.services.actor_traits ||
        !m->options.services.actor_traits(m->options.services.context, actor, &traits))
        return true;
    const char *name =
        qa_strings_cstr(qa_session_strings(m->options.services.session), traits.classname);
    return !name || strcmp(name, "monster_zombie") != 0;
}
void mode_horde_checkpoint_free(qa_horde_checkpoint *checkpoint) {
    if (!checkpoint)
        return;
    free(checkpoint->points);
    free(checkpoint->monsters);
    free(checkpoint->loot);
    free(checkpoint);
}
bool mode_horde_capture(qa_modes *m, mode_instance *v, qa_horde_checkpoint **out, qa_error *e) {
    *out = NULL;
    horde_state *h = v->horde;
    if (!h)
        return true;
    qa_horde_checkpoint *saved = calloc(1, sizeof(*saved));
    if (!saved) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating Horde checkpoint");
        return false;
    }
    saved->options = h->options;
    saved->value = h->value;
    saved->point_count = h->point_count;
    saved->prepared = h->prepared;
    saved->checking = h->checking;
    saved->finishing = h->finishing;
    saved->points = h->point_count ? malloc(h->point_count * sizeof(*saved->points)) : NULL;
    saved->monsters = calloc(m->actor_capacity, sizeof(*saved->monsters));
    saved->loot = calloc(m->actor_capacity, sizeof(*saved->loot));
    if ((h->point_count && !saved->points) || !saved->monsters || !saved->loot) {
        mode_horde_checkpoint_free(saved);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating Horde snapshot records");
        return false;
    }
    if (h->point_count)
        memcpy(saved->points, h->points, h->point_count * sizeof(*saved->points));
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        if (h->monsters[i].death_pending) {
            mode_horde_checkpoint_free(saved);
            return mode_fail(e, "Horde death has not completed");
        }
        if (mode_live(m, h->monsters[i].actor))
            saved->monsters[saved->monster_count++] = h->monsters[i];
        if (mode_live(m, h->loot[i].actor))
            saved->loot[saved->loot_count++] = h->loot[i];
    }
    *out = saved;
    return true;
}
bool mode_horde_restore(qa_modes *m, mode_instance *v, const qa_horde_checkpoint *saved,
                        qa_error *e) {
    if (!saved)
        return true;
    if (saved->monster_count > m->actor_capacity || saved->loot_count > m->actor_capacity ||
        (saved->monster_count && !saved->monsters) || (saved->loot_count && !saved->loot) ||
        !isfinite(saved->value.powerup_chance) || saved->value.powerup_chance < 0 ||
        saved->value.silver_keys < 0 || saved->value.gold_keys < 0 || saved->value.countdown < 0 ||
        saved->value.countdown > 3)
        return mode_fail(e, "invalid Horde checkpoint");
    uint64_t random = m->random;
    if (!qa_modes_horde_configure(m, v->id, &saved->options, saved->points, saved->point_count, e))
        return false;
    m->random = random;
    horde_state *h = v->horde;
    h->value = saved->value;
    h->prepared = saved->prepared;
    h->checking = saved->checking;
    h->finishing = saved->finishing;
    if (saved->point_count)
        memcpy(h->points, saved->points, saved->point_count * sizeof(*h->points));
    for (size_t i = 0; i < saved->monster_count; ++i) {
        const qa_horde_monster_state *entry = &saved->monsters[i];
        if (!mode_live(m, entry->actor) || entry->death_pending ||
            h->monsters[entry->actor.slot].actor.registry)
            return mode_fail(e, "invalid Horde monster reference");
        h->monsters[entry->actor.slot] = *entry;
    }
    for (size_t i = 0; i < saved->loot_count; ++i) {
        const qa_horde_loot_state *entry = &saved->loot[i];
        if (!mode_live(m, entry->actor) || h->loot[entry->actor.slot].actor.registry ||
            entry->kind < HORDE_AMMO || entry->kind > HORDE_POWER || !isfinite(entry->amount) ||
            !isfinite(entry->capacity) || !isfinite(entry->alpha) ||
            (entry->point != SIZE_MAX && entry->point >= h->point_count))
            return mode_fail(e, "invalid Horde loot reference");
        h->loot[entry->actor.slot] = *entry;
    }
    return true;
}

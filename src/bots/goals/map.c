#include "internal.h"
#include <stdio.h>

bool bot_goal_allowed(const qa_bot_goals *g, uint32_t flags) {
    return !(flags & (g->options.game_type == 2 ? 4u : g->options.game_type >= 3 ? 2u : 1u));
}
bool bot_goal_equal_name(const char *a, const char *b) {
    for (size_t i = 0; i < 99999; ++i) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}
void bot_goal_map_clear(qa_bot_goals *g) {
    for (size_t i = 0; i < g->source_count; ++i) free(g->source[i].name);
    free(g->source); free(g->level); free(g->locations); free(g->camps);
    g->source = NULL; g->level = NULL; g->locations = g->camps = NULL;
    g->source_count = g->source_capacity = g->location_count = g->camp_count = 0;
    g->level_head = g->free_head = 0;
    g->initial_count = 0;
    memset(g->source_buckets, 0, sizeof(g->source_buckets));
    g->entities = NULL;
}
bot_level_item *bot_goal_find(const qa_bot_goals *g, int32_t number) {
    for (uint32_t i = g->level_head; i; i = g->level[i].next)
        if (g->level[i].number == number) return &g->level[i];
    return NULL;
}
qa_bot_goal bot_goal_item(const qa_bot_goals *g, const bot_level_item *item) {
    const qa_bot_item_info *info = &qa_bot_items_read(g->items)->items[item->info];
    return (qa_bot_goal){.origin = item->goal_origin, .area = (int32_t)item->goal_area,
        .mins = info->mins, .maxs = info->maxs, .entity = item->entity, .number = item->number,
        .flags = QA_BOT_GOAL_ITEM | (item->timeout != 0 ? QA_BOT_GOAL_DROPPED : 0) |
                 (item->flags & 16 ? QA_BOT_GOAL_ROAM : 0), .item_info = info->number};
}
static uint32_t allocate(qa_bot_goals *g) {
    uint32_t id = g->free_head;
    if (id) {
        g->free_head = g->level[id].next;
        g->level[id] = (bot_level_item){0};
    } else bot_goal_report(g, QA_SCRIPT_FATAL, "out of level items\n");
    return id;
}
static void add(qa_bot_goals *g, uint32_t id) {
    if (g->level_head) g->level[g->level_head].previous = id;
    g->level[id].next = g->level_head;
    g->level[id].previous = 0;
    g->level_head = id;
}
static void release(qa_bot_goals *g, uint32_t id, bool linked) {
    bot_level_item *item = &g->level[id];
    if (linked) {
        if (item->previous) g->level[item->previous].next = item->next;
        else g->level_head = item->next;
        if (item->next) g->level[item->next].previous = item->previous;
    }
    item->next = g->free_head;
    g->free_head = id;
}
static void text(const qa_entities *entities, int32_t id, const char *key, char *out, size_t size) {
    qa_bytes bytes;
    size_t count = qa_bot_bsp_value(entities, id, key, &bytes) ? bytes.size : 0;
    if (count >= size) count = size - 1;
    if (count) memcpy(out, bytes.data, count);
    out[count] = 0;
}
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool move(qa_bot_goals *g, bot_level_item *item, qa_vec3 origin,
                  qa_bot_navigation *n, qa_error *e) {
    if (same_vector(item->origin, origin)) return true;
    const qa_bot_item_info *info = &qa_bot_items_read(g->items)->items[item->info];
    qa_vec3 target; uint32_t area;
    if (!qa_bot_navigation_best(n, origin, (qa_bounds){info->mins, info->maxs}, &target, &area, e))
        return false;
    item->origin = origin;
    item->goal_origin = target;
    item->goal_area = area;
    return true;
}
static bool load_info(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    size_t count = g->entities->count;
    if (count > SIZE_MAX / sizeof(bot_map_goal)) return bot_goal_fail(e, "map goal allocation overflow");
    if (count && (!(g->locations = calloc(count, sizeof(*g->locations))) ||
                  !(g->camps = calloc(count, sizeof(*g->camps))))) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating map locations and camps");
        return false;
    }
    for (int32_t id = qa_bot_bsp_next(g->entities, 0); id; id = qa_bot_bsp_next(g->entities, id)) {
        char classname[128];
        text(g->entities, id, "classname", classname, sizeof(classname));
        bool camp = strcmp(classname, "info_camp") == 0;
        if (!camp && strcmp(classname, "target_location") != 0) continue;
        bot_map_goal info = {0}; bool found;
        if (!qa_bot_bsp_vector(g->entities, id, "origin", &info.origin, &found, e)) return false;
        text(g->entities, id, "message", info.name, sizeof(info.name));
        if (camp && (!qa_bot_bsp_float(g->entities, id, "range", &info.range, &found, e) ||
                     !qa_bot_bsp_float(g->entities, id, "weight", &info.weight, &found, e) ||
                     !qa_bot_bsp_float(g->entities, id, "wait", &info.wait, &found, e) ||
                     !qa_bot_bsp_float(g->entities, id, "random", &info.random, &found, e))) return false;
        if (!qa_bot_navigation_point(n, info.origin, &info.area, e)) return false;
        if (camp) {
            if (info.area) g->camps[g->camp_count++] = info;
            else if (!bot_goal_position_report(g, "camp spot at ", info.origin, " in solid\n", e)) return false;
        } else g->locations[g->location_count++] = info;
    }
    /* Source lists prepend. Reverse once at admission instead of traversing or
     * allocating linked nodes on every name lookup. */
    for (size_t i = 0; i < g->location_count / 2; ++i) {
        bot_map_goal swap = g->locations[i];
        g->locations[i] = g->locations[g->location_count - 1 - i];
        g->locations[g->location_count - 1 - i] = swap;
    }
    for (size_t i = 0; i < g->camp_count / 2; ++i) {
        bot_map_goal swap = g->camps[i];
        g->camps[i] = g->camps[g->camp_count - 1 - i];
        g->camps[g->camp_count - 1 - i] = swap;
    }
    if (g->services.developer && g->services.developer(g->services.context)) {
        char line[64];
        (void)snprintf(line, sizeof(line), "%zu map locations\n", g->location_count);
        bot_goal_report(g, QA_SCRIPT_INFO, line);
        (void)snprintf(line, sizeof(line), "%zu camp spots\n", g->camp_count);
        bot_goal_report(g, QA_SCRIPT_INFO, line);
    }
    return true;
}
static bool unknown_item(qa_bot_goals *g, qa_bytes name, qa_error *e) {
    static const char prefix[] = "entity ", suffix[] = " unknown item\r\n";
    (void)e;
    char line[sizeof(prefix) + 127 + sizeof(suffix)];
    memcpy(line, prefix, sizeof(prefix) - 1);
    memcpy(line + sizeof(prefix) - 1, name.data, name.size);
    memcpy(line + sizeof(prefix) - 1 + name.size, suffix, sizeof(suffix));
    bot_goal_log(g, line);
    return true;
}
static bool load_items(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    size_t count = (size_t)g->options.maximum_level_items + 1;
    if (count > SIZE_MAX / sizeof(*g->level)) return bot_goal_fail(e, "level item allocation overflow");
    g->level = calloc(count, sizeof(*g->level));
    if (!g->level) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating retained level items");
        return false;
    }
    for (uint32_t i = 1; i < g->options.maximum_level_items; ++i) g->level[i].next = i + 1;
    g->free_head = 1;
    if (!g->configured) return true;
    const qa_bot_items_view *items = qa_bot_items_read(g->items);
    char line[256];
    for (size_t i = 0; i < items->count; ++i)
        if (!items->items[i].model_index) {
            (void)snprintf(line, sizeof(line), "item %s has modelindex 0", items->items[i].classname);
            bot_goal_log(g, line);
        }
    for (int32_t entity = qa_bot_bsp_next(g->entities, 0); entity; entity = qa_bot_bsp_next(g->entities, entity)) {
        qa_bytes classname;
        if (!qa_bot_bsp_value(g->entities, entity, "classname", &classname)) continue;
        if (classname.size > 127) classname.size = 127;
        const uint8_t *end = classname.size ? memchr(classname.data, 0, classname.size) : NULL;
        if (end) classname.size = (size_t)(end - classname.data);
        int32_t spawnflags; bool found;
        if (!qa_bot_bsp_integer(g->entities, entity, "spawnflags", &spawnflags, &found, e)) return false;
        size_t index = 0;
        while (index < items->count && (strlen(items->items[index].classname) != classname.size ||
            memcmp(classname.data, items->items[index].classname, classname.size))) ++index;
        if (index == items->count) { if (!unknown_item(g, classname, e)) return false; continue; }
        const qa_bot_item_info *info = &items->items[index];
        qa_vec3 origin;
        if (!qa_bot_bsp_vector(g->entities, entity, "origin", &origin, &found, e)) return false;
        if (!found) {
            (void)snprintf(line, sizeof(line), "item %s without origin\n", info->classname);
            bot_goal_report(g, QA_SCRIPT_ERROR, line);
            continue;
        }
        qa_bounds bounds = {info->mins, info->maxs};
        uint32_t area = 0;
        if (spawnflags & 1) {
            int32_t contents;
            if (!qa_bot_navigation_contents(n, origin, &contents, e)) return false;
            if (!(contents & 32)) {
                qa_vec3 end = origin; end.z -= 32;
                qa_trace_result trace;
                if (!qa_bot_navigation_trace(n, origin, end, &bounds, (qa_actor_id){0}, 0x10001, &trace, e)) return false;
                if (trace.fraction >= 1) {
                    if (!qa_bot_navigation_jump_pad(n, origin, bounds, &area, e)) return false;
                    (void)snprintf(line, sizeof(line), "item %s reachable from jumppad area %u\r\n", info->classname, area);
                    bot_goal_log(g, line);
                    if (!area) continue;
                }
            }
        }
        uint32_t id = allocate(g);
        if (!id) return true;
        bot_level_item *item = &g->level[id];
        item->number = ++g->initial_count;
        item->info = (uint32_t)index;
        static const char *const names[] = {"notfree", "notteam", "notsingle", "notbot"};
        for (size_t i = 0; i < 4; ++i) {
            int32_t flag;
            if (!qa_bot_bsp_integer(g->entities, entity, names[i], &flag, &found, e)) return false;
            if (flag) item->flags |= 1u << i;
        }
        if (strcmp(info->classname, "item_botroam") == 0) {
            item->flags |= 16;
            if (!qa_bot_bsp_float(g->entities, entity, "weight", &item->weight, &found, e)) return false;
        }
        if (!(spawnflags & 1)) {
            if (!qa_bot_navigation_drop(n, origin, bounds, &origin, &found, e)) return false;
            if (!found) {
                (void)snprintf(line, sizeof(line), "%s in solid at (", info->classname);
                if (!bot_goal_position_report(g, line, origin, ")\n", e)) return false;
            }
        }
        item->origin = origin;
        item->goal_origin = origin;
        item->goal_area = area;
        if (!area && !qa_bot_navigation_best(n, origin, bounds, &item->goal_origin, &item->goal_area, e)) return false;
        if (!item->goal_area) {
            (void)snprintf(line, sizeof(line), "%s not reachable for bots at (", info->classname);
            if (!bot_goal_position_report(g, line, origin, ")\n", e)) return false;
        }
        add(g, id);
    }
    (void)snprintf(line, sizeof(line), "found %d level items\n", g->initial_count);
    bot_goal_report(g, QA_SCRIPT_INFO, line);
    return true;
}
bool qa_bot_goals_load_map(qa_bot_goals *g, const qa_entities *entities,
                          qa_bot_navigation *navigation, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!entities || !navigation || entities->count > INT32_MAX)
        return bot_goal_fail(e, "invalid goal map/navigation");
    qa_bot_goals staged = {.items = g->items, .services = g->services, .options = g->options,
                           .entities = entities, .configured = g->configured};
    g->busy = true;
    bool ok = load_info(&staged, navigation, e) && load_items(&staged, navigation, e);
    if (ok) {
        bot_goal_map_clear(g);
        g->entities = entities;
        g->level = staged.level;
        g->level_head = staged.level_head;
        g->free_head = staged.free_head;
        g->initial_count = staged.initial_count;
        g->locations = staged.locations;
        g->location_count = staged.location_count;
        g->camps = staged.camps;
        g->camp_count = staged.camp_count;
    } else bot_goal_map_clear(&staged);
    g->busy = false;
    return ok;
}
bool bot_goal_entities(qa_bot_goals *g, const qa_bot_goal_entity **out, size_t *count,
                       void **lease, qa_error *e) {
    *out = NULL; *count = 0; *lease = NULL;
    if (!g->services.entities) return true;
    if (!g->services.entities(g->services.context, out, count, lease, e)) return false;
    int32_t previous = 0;
    for (size_t i = 0; i < *count; ++i) {
        if (!*out || (*out)[i].number <= previous || (*out)[i].number > 1000000) {
            g->services.entities_end(g->services.context, *lease);
            *lease = NULL;
            return bot_goal_fail(e, "bot entity snapshot must have increasing source numbers");
        }
        previous = (*out)[i].number;
    }
    return true;
}
void bot_goal_entities_end(qa_bot_goals *g, void *lease) {
    if (g->services.entities_end) g->services.entities_end(g->services.context, lease);
}
static bool update(qa_bot_goals *g, qa_bot_navigation *n, const qa_bot_goal_entity *entities,
                    size_t count, qa_error *e) {
    const qa_bot_items_view *items = qa_bot_items_read(g->items);
    for (size_t i = 0; i < count; ++i) {
        const qa_bot_goal_entity *entity = &entities[i];
        if (entity->type != 2 || !entity->model_index || !same_vector(entity->origin, entity->last_visible_origin)) continue;
        uint32_t linked = 0;
        for (uint32_t id = g->level_head; id; id = g->level[id].next)
            if (g->level[id].entity && g->level[id].entity == entity->number) { linked = id; break; }
        if (linked) {
            if (items->items[g->level[linked].info].model_index == entity->model_index) {
                if (!move(g, &g->level[linked], entity->origin, n, e)) return false;
                continue;
            }
            release(g, linked, true);
        }
        linked = 0;
        for (uint32_t id = g->level_head; id; id = g->level[id].next) {
            bot_level_item *item = &g->level[id];
            if (!item->entity && bot_goal_allowed(g, item->flags) &&
                items->items[item->info].model_index == entity->model_index &&
                qa_vec_length(qa_vec_sub(item->origin, entity->origin)) < 30) { linked = id; break; }
        }
        if (linked) {
            g->level[linked].entity = entity->number;
            if (!move(g, &g->level[linked], entity->origin, n, e)) return false;
            if (g->services.debug && g->services.debug(g->services.context)) {
                char line[160];
                (void)snprintf(line, sizeof(line), "linked item %s to an entity",
                               items->items[g->level[linked].info].classname);
                bot_goal_log(g, line);
            }
            continue;
        }
        size_t index = 0;
        while (index < items->count && items->items[index].model_index != entity->model_index) ++index;
        if (index == items->count) continue;
        uint32_t id = allocate(g);
        if (!id) continue;
        bot_level_item *item = &g->level[id];
        item->entity = entity->number;
        item->number = g->initial_count + entity->number;
        item->info = (uint32_t)index;
        item->origin = entity->origin;
        qa_bounds bounds = {items->items[index].mins, items->items[index].maxs};
        if (!qa_bot_navigation_best(n, item->origin, bounds, &item->goal_origin, &item->goal_area, e)) {
            release(g, id, false);
            return false;
        }
        if (qa_bot_navigation_area(n, item->goal_area).contents & 128) {
            release(g, id, false);
            continue;
        }
        item->timeout = g->time + 30;
        add(g, id);
    }
    return true;
}
bool qa_bot_goals_update_items(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!g->level) return true;
    g->busy = true;
    for (uint32_t id = g->level_head, next; id; id = next) {
        next = g->level[id].next;
        if (g->level[id].timeout != 0 && g->level[id].timeout < g->time) release(g, id, true);
    }
    if (!g->configured || !n) { g->busy = false; return true; }
    const qa_bot_goal_entity *entities; size_t count; void *lease;
    bool ok = bot_goal_entities(g, &entities, &count, &lease, e);
    if (ok) {
        ok = update(g, n, entities, count, e);
        bot_goal_entities_end(g, lease);
    }
    g->busy = false;
    return ok;
}
bool qa_bot_goals_find_entity(qa_bot_goals *g, int32_t number, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!g->configured) return true;
    bot_level_item *item = bot_goal_find(g, number);
    if (!item) return true;
    g->busy = true;
    const qa_bot_goal_entity *entities; size_t count; void *lease;
    bool ok = bot_goal_entities(g, &entities, &count, &lease, e);
    if (ok) {
        int32_t model = qa_bot_items_read(g->items)->items[item->info].model_index;
        for (size_t i = 0; i < count; ++i)
            if (entities[i].model_index && entities[i].model_index == model &&
                same_vector(entities[i].origin, entities[i].last_visible_origin) &&
                qa_vec_length(qa_vec_sub(item->origin, entities[i].origin)) < 30) item->entity = entities[i].number;
        bot_goal_entities_end(g, lease);
    }
    g->busy = false;
    return ok;
}
const char *qa_bot_goals_name(const qa_bot_goals *g, int32_t number) {
    if (!g) return "";
    if (number >= QA_BOT_SOURCE_GOAL_MIN)
        for (size_t i = 0; i < g->source_count; ++i)
            if (g->source[i].goal.number == number) return g->source[i].name;
    if (!g->configured) return "";
    bot_level_item *item = bot_goal_find(g, number);
    return item ? qa_bot_items_read(g->items)->items[item->info].name : "";
}
bool qa_bot_goals_level_item(const qa_bot_goals *g, int32_t after, const char *name,
                              qa_bot_goal *out, bool *found, qa_error *e) {
    if (!g || !name || !out || !found) return bot_goal_fail(e, "invalid level item query");
    *found = false;
    if (!g->configured) return true;
    uint32_t id = g->level_head;
    if (after >= 0 && after < QA_BOT_SOURCE_GOAL_MIN) {
        bot_level_item *item = bot_goal_find(g, after);
        if (!item) return true;
        id = item->next;
    } else if (after >= QA_BOT_SOURCE_GOAL_MIN) id = 0;
    for (; id; id = g->level[id].next) {
        const bot_level_item *item = &g->level[id];
        if (bot_goal_allowed(g, item->flags) && !(item->flags & 8) &&
            bot_goal_equal_name(name, qa_bot_items_read(g->items)->items[item->info].name)) {
            int32_t info = out->item_info;
            *out = bot_goal_item(g, item);
            out->item_info = info;
            out->flags &= ~QA_BOT_GOAL_ROAM;
            *found = true;
            return true;
        }
    }
    bool next = after < QA_BOT_SOURCE_GOAL_MIN;
    for (size_t i = 0; i < g->source_count; ++i) {
        const bot_source_goal *source = &g->source[i];
        if (!next) { next = source->goal.number == after; continue; }
        if (!bot_goal_equal_name(name, source->name)) continue;
        int32_t info = out->item_info;
        *out = source->goal;
        out->item_info = info;
        *found = true;
        break;
    }
    return true;
}
static void info_goal(const bot_map_goal *info, qa_bot_goal *out) {
    out->origin = info->origin;
    out->area = (int32_t)info->area;
    out->entity = 0;
    out->mins = qa_v3(-8, -8, -8);
    out->maxs = qa_v3(8, 8, 8);
}
bool qa_bot_goals_location(const qa_bot_goals *g, const char *name, qa_bot_goal *out,
                            bool *found, qa_error *e) {
    if (!g || !name || !out || !found) return bot_goal_fail(e, "invalid map location query");
    *found = false;
    for (size_t i = 0; i < g->location_count; ++i)
        if (bot_goal_equal_name(name, g->locations[i].name)) {
            info_goal(&g->locations[i], out);
            *found = true;
            break;
        }
    return true;
}
bool qa_bot_goals_camp(const qa_bot_goals *g, int32_t index, qa_bot_goal *out,
                       int32_t *next, qa_error *e) {
    if (!g || !out || !next) return bot_goal_fail(e, "invalid camp goal query");
    if (index < 0) index = 0;
    *next = 0;
    if ((size_t)index < g->camp_count) {
        info_goal(&g->camps[index], out);
        *next = index + 1;
    }
    return true;
}

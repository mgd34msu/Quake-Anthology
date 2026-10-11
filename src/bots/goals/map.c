#include "internal.h"
#include "qa/game_type.h"
#include <stdio.h>

bool bot_goal_allowed(const qa_bot_goals *g, uint32_t flags) {
    return !(flags & (g->options.game_type == 2 ? 4u : qa_game_type_is_team(g->options.game_type) ? 2u : 1u));
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
    free(g->source); free(g->info);
    g->source = NULL; g->info = NULL;
    g->level_allocation = (qa_bot_memory_allocation){0};
    g->level_capacity = 0;
    g->source_count = g->source_capacity = g->info_count = g->info_capacity = 0;
    g->level_head = g->free_head = 0;
    g->location_head = g->camp_head = 0;
    g->initial_count = 0;
    memset(g->source_buckets, 0, sizeof(g->source_buckets));
    g->entities = NULL;
}
static uint32_t map_word(const uint8_t *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}
static void map_word_set(uint8_t *at, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) at[i] = (uint8_t)(word >> (i * 8));
}
static float map_float(const uint8_t *at) {
    uint32_t word = map_word(at); float value; memcpy(&value, &word, sizeof(value)); return value;
}
static void map_float_set(uint8_t *at, float value) {
    uint32_t word; memcpy(&word, &value, sizeof(word)); map_word_set(at, word);
}
static uint32_t map_float_word(float value) {
    uint32_t word; memcpy(&word, &value, sizeof(word)); return word;
}
static qa_vec3 map_vector(const uint8_t *at) {
    return qa_v3(map_float(at), map_float(at + 4), map_float(at + 8));
}
static void map_vector_set(uint8_t *at, qa_vec3 value) {
    map_float_set(at, value.x); map_float_set(at + 4, value.y); map_float_set(at + 8, value.z);
}
bool bot_goal_info_span(const qa_bot_goals *g, uint32_t pointer, qa_bot_memory_span *out,
                        bool *camp, qa_error *error) {
    if (!g || !pointer || !out || !camp) return bot_goal_fail(error, "Map info read requires its actual pointer/output");
    const bot_map_info *row = NULL;
    for (size_t i = 0; i < g->info_count; ++i) if (g->info[i].pointer == pointer) { row = &g->info[i]; break; }
    if (!row) return bot_goal_fail(error, "Invalid goal map info pointer");
    if (!qa_bot_memory_bytes(g->memory, row->allocation, out, error)) return false;
    if (out->size != (row->camp ? 164u : 148u)) return bot_goal_fail(error, "Goal map info allocation size mismatch");
    *camp = row->camp; return true;
}
bool bot_goal_info_free(qa_bot_goals *g, qa_error *error) {
    uint32_t *heads[2] = {&g->location_head, &g->camp_head};
    for (unsigned list = 0; list < 2; ++list) {
        size_t steps = 0, maximum = g->info_count;
        for (uint32_t pointer = *heads[list]; pointer;) {
            qa_bot_memory_span bytes; bool camp;
            if (++steps > maximum)
                return bot_goal_fail(error, "Goal map info free has an invalid retained list");
            if (!bot_goal_info_span(g, pointer, &bytes, &camp, error)) return false;
            uint32_t next = map_word(bytes.data + (camp ? 160u : 144u));
            size_t i = 0;
            while (g->info[i].pointer != pointer) ++i;
            if (!qa_bot_memory_free(g->memory, g->info[i].allocation, error)) return false;
            memmove(g->info + i, g->info + i + 1, (g->info_count - i - 1) * sizeof(*g->info));
            --g->info_count; pointer = next;
        }
        *heads[list] = 0;
    }
    return true;
}
bool bot_goal_info_topology(const qa_bot_goals *g, qa_error *error) {
    if (!g->next_info || g->next_info > UINT64_C(0x100000000) || g->info_count > g->info_capacity ||
        (g->info_count && !g->info)) return bot_goal_fail(error, "Invalid retained goal map info table");
    for (size_t i = 0; i < g->info_count; ++i) {
        qa_bot_memory_span bytes; bool camp;
        if (!g->info[i].pointer || g->info[i].pointer >= g->next_info)
            return bot_goal_fail(error, "Invalid saved goal info pointer identity");
        if (!bot_goal_info_span(g, g->info[i].pointer, &bytes, &camp, error)) return false;
        for (size_t j = 0; j < i; ++j)
            if (g->info[j].pointer == g->info[i].pointer) return bot_goal_fail(error, "Duplicate goal map info pointer");
    }
    uint32_t heads[2] = {g->location_head, g->camp_head};
    for (unsigned list = 0; list < 2; ++list) {
        size_t steps = 0;
        for (uint32_t pointer = heads[list]; pointer;) {
            qa_bot_memory_span bytes; bool camp;
            if (++steps > g->info_count) return bot_goal_fail(error, "Saved goal map info list has a cycle");
            if (!bot_goal_info_span(g, pointer, &bytes, &camp, error)) return false;
            pointer = map_word(bytes.data + (camp ? 160u : 144u));
        }
    }
    return true;
}
bool qa_bot_goals_rebind_world(qa_bot_goals *g, const qa_entities *entities, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (g->entities != entities)
        return bot_goal_fail(e, "bot round must retain the actual map goal metadata");
    for (uint32_t i = 0; i < g->options.maximum_states; ++i)
        if (g->states[i].used) return bot_goal_fail(e, "retire every bot goal state before rebinding its world");
    for (size_t i = 0; i < g->source_count; ++i) free(g->source[i].name);
    free(g->source);
    g->source = NULL;
    g->source_count = g->source_capacity = 0;
    memset(g->source_buckets, 0, sizeof(g->source_buckets));
    return true;
}
static bool level_span(const qa_bot_goals *g, uint32_t id, qa_bot_memory_span *out, qa_error *error) {
    qa_bot_memory_span bytes;
    if (!id || id > g->level_capacity)
        return bot_goal_fail(error, "Invalid retained level-item cell");
    if (!qa_bot_memory_bytes(g->memory, g->level_allocation, &bytes, error)) return false;
    if (bytes.size / 60 != g->level_capacity || bytes.size % 60)
        return bot_goal_fail(error, "Level-item heap extent differs from its source capacity");
    *out = (qa_bot_memory_span){bytes.data + (id - 1) * 60, 60}; return true;
}
static int32_t map_integer(uint32_t word) {
    return word <= INT32_MAX ? (int32_t)word : -1 - (int32_t)(UINT32_MAX - word);
}
bool bot_goal_level_read(const qa_bot_goals *g, uint32_t id, bot_level_item *out, qa_error *error) {
    qa_bot_memory_span bytes;
    if (!out || !level_span(g, id, &bytes, error)) return false;
    const uint8_t *at = bytes.data;
    *out = (bot_level_item){.number = map_integer(map_word(at)), .info = map_word(at + 4),
        .flags = map_word(at + 8), .weight = map_float(at + 12), .origin = map_vector(at + 16),
        .goal_area = map_word(at + 28), .goal_origin = map_vector(at + 32),
        .entity = map_integer(map_word(at + 44)), .timeout = map_float(at + 48),
        .previous = map_word(at + 52), .next = map_word(at + 56)};
    return true;
}
bool bot_goal_level_word(const qa_bot_goals *g, uint32_t id, uint32_t offset, uint32_t word, qa_error *error) {
    qa_bot_memory_span bytes;
    if (offset > 56 || offset % 4) return bot_goal_fail(error, "Invalid level-item word offset");
    if (!level_span(g, id, &bytes, error)) return false;
    map_word_set(bytes.data + offset, word); return true;
}
bool bot_goal_level_vector(const qa_bot_goals *g, uint32_t id, uint32_t offset, qa_vec3 value, qa_error *error) {
    qa_bot_memory_span bytes;
    if (offset != 16 && offset != 32) return bot_goal_fail(error, "Invalid level-item vector offset");
    if (!level_span(g, id, &bytes, error)) return false;
    map_vector_set(bytes.data + offset, value); return true;
}
bool bot_goal_level_topology(const qa_bot_goals *g, qa_error *error) {
    if (!g->level_allocation.owner)
        return !g->level_capacity && !g->level_head && !g->free_head && !g->initial_count ? true :
            bot_goal_fail(error, "Goal level map words lack their actual allocation");
    qa_bot_memory_span bytes;
    if (!qa_bot_memory_bytes(g->memory, g->level_allocation, &bytes, error)) return false;
    if (!g->level_capacity || bytes.size % 60 || bytes.size / 60 != g->level_capacity)
        return bot_goal_fail(error, "Goal level heap extent differs from its retained capacity");
    uint32_t heads[2] = {g->level_head, g->free_head};
    for (unsigned list = 0; list < 2; ++list) {
        size_t steps = 0;
        for (uint32_t pointer = heads[list]; pointer;) {
            bot_level_item item;
            if (++steps > g->level_capacity) return bot_goal_fail(error, "Saved level-item list has a cycle");
            if (!bot_goal_level_read(g, pointer, &item, error)) return false;
            pointer = item.next;
        }
    }
    return true;
}
bool bot_goal_find(const qa_bot_goals *g, int32_t number, uint32_t *id, bot_level_item *out,
                   bool *found, qa_error *error) {
    *found = false; size_t steps = 0;
    for (uint32_t i = g->level_head; i;) {
        if (++steps > g->level_capacity) return bot_goal_fail(error, "Level-item list has a cycle");
        if (!bot_goal_level_read(g, i, out, error)) return false;
        if (out->number == number) { *found = true; *id = i; return true; }
        i = out->next;
    }
    return true;
}
bool bot_goal_item(const qa_bot_goals *g, const bot_level_item *item, qa_bot_goal *out, qa_error *e) {
    const qa_bot_items_view *items;
    if (!qa_bot_items_view_read(g->items, &items, e)) return false;
    if (item->info >= items->count) return bot_goal_fail(e, "Item goal info exceeds its source configuration");
    const qa_bot_item_info *info = &items->items[item->info];
    *out = (qa_bot_goal){.origin = item->goal_origin, .area = map_integer(item->goal_area),
        .mins = info->mins, .maxs = info->maxs, .entity = item->entity, .number = item->number,
        .flags = QA_BOT_GOAL_ITEM | (item->timeout != 0 ? QA_BOT_GOAL_DROPPED : 0) |
                 (item->flags & 16 ? QA_BOT_GOAL_ROAM : 0), .item_info = info->number};
    return true;
}
static bool allocate(qa_bot_goals *g,uint32_t *id,qa_error *error) {
    *id = g->free_head;
    if (*id) {
        bot_level_item item; qa_bot_memory_span bytes;
        if (!bot_goal_level_read(g, *id, &item, error) || !level_span(g, *id, &bytes, error)) return false;
        g->free_head = item.next;
        memset(bytes.data, 0, bytes.size);
    } else return bot_goal_report(g, QA_SCRIPT_FATAL, "out of level items\n",error);
    return true;
}
static bool add(qa_bot_goals *g, uint32_t id, qa_error *error) {
    if (g->level_head && !bot_goal_level_word(g, g->level_head, 52, id, error)) return false;
    if (!bot_goal_level_word(g, id, 56, g->level_head, error) || !bot_goal_level_word(g, id, 52, 0, error)) return false;
    g->level_head = id;
    return true;
}
static bool release(qa_bot_goals *g, uint32_t id, bool linked, qa_error *error) {
    bot_level_item value;
    if (!bot_goal_level_read(g, id, &value, error)) return false;
    const bot_level_item *item = &value;
    if (linked) {
        if (item->previous) { if (!bot_goal_level_word(g, item->previous, 56, item->next, error)) return false; }
        else g->level_head = item->next;
        if (item->next && !bot_goal_level_word(g, item->next, 52, item->previous, error)) return false;
    }
    if (!bot_goal_level_word(g, id, 56, g->free_head, error)) return false;
    g->free_head = id;
    return true;
}
static void text(const qa_entities *entities, int32_t id, const char *key, char *out, size_t size) {
    qa_bytes bytes;
    size_t count = qa_bot_bsp_value(entities, id, key, &bytes) ? bytes.size : 0;
    if (count >= size) count = size - 1;
    if (count) memcpy(out, bytes.data, count);
    out[count] = 0;
}
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool move(qa_bot_goals *g, uint32_t id, qa_vec3 origin,
                  qa_bot_navigation *n, qa_error *e) {
    bot_level_item value;
    if (!bot_goal_level_read(g, id, &value, e)) return false;
    const bot_level_item *item = &value;
    if (same_vector(item->origin, origin)) return true;
    const qa_bot_items_view *items;
    if (!qa_bot_items_view_read(g->items, &items, e)) return false;
    if (item->info >= items->count) return bot_goal_fail(e, "Level item info index exceeds its source configuration");
    const qa_bot_item_info *info = &items->items[item->info];
    qa_vec3 target; uint32_t area;
    if (!qa_bot_navigation_best(n, origin, (qa_bounds){info->mins, info->maxs}, &target, &area, e))
        return false;
    return bot_goal_level_vector(g, id, 16, origin, e) && bot_goal_level_vector(g, id, 32, target, e) &&
        bot_goal_level_word(g, id, 28, area, e);
}
bool bot_goal_map_info_load(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    if (!bot_goal_info_free(g, e)) return false;
    size_t location_count = 0, camp_count = 0;
    for (int32_t id = qa_bot_bsp_next(g->entities, 0); id; id = qa_bot_bsp_next(g->entities, id)) {
        qa_bytes name;
        if (!qa_bot_bsp_value(g->entities, id, "classname", &name)) continue;
        if (name.size > 127) name.size = 127;
        const uint8_t *zero = name.size ? memchr(name.data, 0, name.size) : NULL;
        if (zero) name.size = (size_t)(zero - name.data);
        qa_string_id classname = qa_strings_find(g->services.strings, name);
        bool camp = classname == g->info_camp;
        if (!camp && classname != g->target_location) continue;
        if (!g->next_info || g->next_info > UINT32_MAX) return bot_goal_fail(e, "Goal info pointer IDs exhausted");
        if (g->info_count == g->info_capacity) {
            size_t capacity = g->info_capacity ? g->info_capacity * 2 : 16;
            if (capacity < g->info_capacity || capacity > SIZE_MAX / sizeof(*g->info))
                return bot_goal_fail(e, "Goal map info table capacity overflow");
            bot_map_info *rows = realloc(g->info, capacity * sizeof(*rows));
            if (!rows) { qa_error_set(e, QA_ERROR_MEMORY, capacity, "Retaining goal map info allocations"); return false; }
            g->info = rows; g->info_capacity = capacity;
        }
        qa_bot_memory_allocation allocation;
        if (!qa_bot_memory_allocate(g->memory, camp ? 164u : 148u, QA_BOT_MEMORY_HEAP, true, NULL, &allocation, e)) return false;
        uint32_t pointer = (uint32_t)g->next_info++;
        g->info[g->info_count++] = (bot_map_info){allocation, pointer, camp};
        qa_bot_memory_span bytes; bool kind, found; qa_vec3 origin;
        if (!bot_goal_info_span(g, pointer, &bytes, &kind, e) ||
            !qa_bot_bsp_vector(g->entities, id, "origin", &origin, &found, e)) return false;
        map_vector_set(bytes.data, origin);
        text(g->entities, id, "message", (char *)bytes.data + 16, 128);
        if (camp) {
            static const char *const fields[4] = {"range", "weight", "wait", "random"};
            for (unsigned field = 0; field < 4; ++field) {
                float value;
                if (!qa_bot_bsp_float(g->entities, id, fields[field], &value, &found, e)) return false;
                map_float_set(bytes.data + 144 + field * 4, value);
            }
        }
        uint32_t area;
        if (!qa_bot_navigation_point(n, map_vector(bytes.data), &area, e) ||
            !bot_goal_info_span(g, pointer, &bytes, &kind, e)) return false;
        map_word_set(bytes.data + 12, area);
        if (camp) {
            if (!area) {
                if (!bot_goal_position_report(g, "camp spot at ", map_vector(bytes.data), " in solid\n", e) ||
                    !qa_bot_memory_free(g->memory, allocation, e)) return false;
                --g->info_count;
                continue;
            }
            map_word_set(bytes.data + 160, g->camp_head); g->camp_head = pointer; ++camp_count;
        } else {
            map_word_set(bytes.data + 144, g->location_head); g->location_head = pointer; ++location_count;
        }
    }
    if (g->services.developer && g->services.developer(g->services.context)) {
        char line[64];
        (void)snprintf(line, sizeof(line), "%zu map locations\n", location_count);
        if(!bot_goal_report(g, QA_SCRIPT_INFO, line,e)) return false;
        (void)snprintf(line, sizeof(line), "%zu camp spots\n", camp_count);
        if(!bot_goal_report(g, QA_SCRIPT_INFO, line,e)) return false;
    }
    return true;
}
static bool unknown_item(qa_bot_goals *g, qa_bytes name, qa_error *e) {
    static const char prefix[] = "entity ", suffix[] = " unknown item\r\n";
    char line[sizeof(prefix) + 127 + sizeof(suffix)];
    memcpy(line, prefix, sizeof(prefix) - 1);
    memcpy(line + sizeof(prefix) - 1, name.data, name.size);
    memcpy(line + sizeof(prefix) - 1 + name.size, suffix, sizeof(suffix));
    return bot_goal_log(g, line, e);
}
static bool load_items(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    uint32_t count = g->options.maximum_level_items;
    if (g->services.maximum_level_items) {
        int32_t maximum;
        if (!g->services.maximum_level_items(g->services.context, &maximum, e)) return false;
        if (maximum < 1) return bot_goal_fail(e, "maximum level items must be a positive source integer");
        count = (uint32_t)maximum;
    }
    if (count > (INT32_MAX - 4u) / 60u) return bot_goal_fail(e, "level item allocation overflow");
    qa_bot_memory_allocation allocation;
    if (!qa_bot_memory_allocate(g->memory, count * 60u, QA_BOT_MEMORY_HEAP, true, NULL, &allocation, e)) return false;
    g->level_allocation = allocation;
    g->level_capacity = count;
    for (uint32_t i = 1; i < count; ++i)
        if (!bot_goal_level_word(g, i, 56, i + 1, e)) return false;
    g->free_head = 1;
    g->level_head = 0; g->initial_count = 0;
    if (!g->configured) return true;
    const qa_bot_items_view *items;
    if (!qa_bot_items_view_read(g->items, &items, e)) return false;
    char line[256];
    for (size_t i = 0; i < items->count; ++i) {
        if (!qa_bot_items_view_read(g->items, &items, e)) return false;
        if (i >= items->count) break;
        if (!items->items[i].model_index) {
            (void)snprintf(line, sizeof(line), "item %s has modelindex 0", items->items[i].classname);
            if (!bot_goal_log(g, line, e)) return false;
        }
    }
    for (int32_t entity = qa_bot_bsp_next(g->entities, 0); entity; entity = qa_bot_bsp_next(g->entities, entity)) {
        qa_bytes classname;
        if (!qa_bot_bsp_value(g->entities, entity, "classname", &classname)) continue;
        if (classname.size > 127) classname.size = 127;
        const uint8_t *text_end = classname.size ? memchr(classname.data, 0, classname.size) : NULL;
        if (text_end) classname.size = (size_t)(text_end - classname.data);
        int32_t spawnflags; bool found;
        if (!qa_bot_bsp_integer(g->entities, entity, "spawnflags", &spawnflags, &found, e)) return false;
        if (!qa_bot_items_view_read(g->items, &items, e)) return false;
        size_t index = 0;
        while (index < items->count && (strlen(items->items[index].classname) != classname.size ||
            memcmp(classname.data, items->items[index].classname, classname.size))) ++index;
        if (index == items->count) { if (!unknown_item(g, classname, e)) return false; continue; }
        const qa_bot_item_info *info = &items->items[index];
        qa_vec3 origin;
        if (!qa_bot_bsp_vector(g->entities, entity, "origin", &origin, &found, e)) return false;
        if (!found) {
            (void)snprintf(line, sizeof(line), "item %s without origin\n", info->classname);
            if(!bot_goal_report(g, QA_SCRIPT_ERROR, line,e)) return false;
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
                    if (!qa_bot_items_view_read(g->items, &items, e)) return false;
                    info = &items->items[index];
                    (void)snprintf(line, sizeof(line), "item %s reachable from jumppad area %u\r\n", info->classname, area);
                    if (!bot_goal_log(g, line, e)) return false;
                    if (!area) continue;
                }
            }
        }
        uint32_t id;
        if(!allocate(g,&id,e)) return false;
        if (!id) return true;
        if (!bot_goal_level_word(g, id, 0, (uint32_t)++g->initial_count, e)) return false;
        uint32_t flags = 0;
        static const char *const names[] = {"notfree", "notteam", "notsingle", "notbot"};
        for (size_t i = 0; i < 4; ++i) {
            int32_t flag;
            if (!qa_bot_bsp_integer(g->entities, entity, names[i], &flag, &found, e)) return false;
            if (flag) {
                flags |= 1u << i;
                if (!bot_goal_level_word(g, id, 8, flags, e)) return false;
            }
        }
        if (classname.size == sizeof("item_botroam") - 1 &&
            !memcmp(classname.data, "item_botroam", sizeof("item_botroam") - 1)) {
            flags |= 16;
            float weight;
            if (!bot_goal_level_word(g, id, 8, flags, e) ||
                !qa_bot_bsp_float(g->entities, entity, "weight", &weight, &found, e) ||
                !bot_goal_level_word(g, id, 12, map_float_word(weight), e)) return false;
        }
        if (!(spawnflags & 1)) {
            if (!qa_bot_navigation_drop(n, origin, bounds, &origin, &found, e)) return false;
            if (!found) {
                (void)snprintf(line, sizeof(line), "%.*s in solid at (", (int)classname.size,
                               classname.data ? (const char *)classname.data : "");
                if (!bot_goal_position_report(g, line, origin, ")\n", e)) return false;
            }
        }
        if (!bot_goal_level_word(g, id, 4, (uint32_t)index, e) ||
            !bot_goal_level_vector(g, id, 16, origin, e)) return false;
        qa_vec3 target = origin;
        if (!area && !qa_bot_navigation_best(n, origin, bounds, &target, &area, e)) return false;
        if (!bot_goal_level_word(g, id, 28, area, e) || !bot_goal_level_vector(g, id, 32, target, e)) return false;
        if (!area) {
            (void)snprintf(line, sizeof(line), "%.*s not reachable for bots at (", (int)classname.size,
                           classname.data ? (const char *)classname.data : "");
            if (!bot_goal_position_report(g, line, origin, ")\n", e)) return false;
        }
        if (!add(g, id, e)) return false;
    }
    (void)snprintf(line, sizeof(line), "found %d level items\n", g->initial_count);
    return bot_goal_report(g, QA_SCRIPT_INFO, line,e);
}
bool qa_bot_goals_load_map(qa_bot_goals *g, const qa_entities *entities,
                          qa_bot_navigation *navigation, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!entities || !navigation || entities->count > INT32_MAX)
        return bot_goal_fail(e, "invalid goal map/navigation");
    g->busy = true;
    g->entities = entities;
    for (size_t i = 0; i < g->source_count; ++i) free(g->source[i].name);
    free(g->source);
    g->source = NULL;
    g->source_count = g->source_capacity = 0;
    memset(g->source_buckets, 0, sizeof(g->source_buckets));
    bool ok = bot_goal_map_info_load(g, navigation, e);
    if (ok && g->level_allocation.owner) ok = qa_bot_memory_free(g->memory, g->level_allocation, e);
    if (ok) ok = load_items(g, navigation, e);
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
    const qa_bot_items_view *items;
    for (size_t i = 0; i < count; ++i) {
        const qa_bot_goal_entity *entity = &entities[i];
        if (entity->type != 2 || !entity->model_index || !same_vector(entity->origin, entity->last_visible_origin)) continue;
        if (!qa_bot_items_view_read(g->items, &items, e)) return false;
        uint32_t linked = 0;
        size_t steps = 0;
        bot_level_item value;
        for (uint32_t id = g->level_head; id;) {
            if (++steps > g->level_capacity) return bot_goal_fail(e, "Level-item update list has a cycle");
            if (!bot_goal_level_read(g, id, &value, e)) return false;
            if (value.entity && value.entity == entity->number) { linked = id; break; }
            id = value.next;
        }
        if (linked) {
            if (value.info >= items->count) return bot_goal_fail(e, "Level item info index exceeds its source configuration");
            if (items->items[value.info].model_index == entity->model_index) {
                if (!move(g, linked, entity->origin, n, e)) return false;
                continue;
            }
            if (!release(g, linked, true, e)) return false;
        }
        linked = 0;
        steps = 0;
        for (uint32_t id = g->level_head; id;) {
            if (++steps > g->level_capacity) return bot_goal_fail(e, "Level-item update list has a cycle");
            if (!bot_goal_level_read(g, id, &value, e)) return false;
            const bot_level_item *item = &value;
            if (item->info >= items->count) return bot_goal_fail(e, "Level item info index exceeds its source configuration");
            if (!item->entity && bot_goal_allowed(g, item->flags) &&
                items->items[item->info].model_index == entity->model_index &&
                qa_vec_length(qa_vec_sub(item->origin, entity->origin)) < 30) { linked = id; break; }
            id = item->next;
        }
        if (linked) {
            if (!bot_goal_level_word(g, linked, 44, (uint32_t)entity->number, e) ||
                !move(g, linked, entity->origin, n, e)) return false;
            if (g->services.debug && g->services.debug(g->services.context)) {
                char line[160];
                if (!qa_bot_items_view_read(g->items, &items, e)) return false;
                if (!bot_goal_level_read(g, linked, &value, e)) return false;
                if (value.info >= items->count) return bot_goal_fail(e, "Level item info index exceeds its source configuration");
                (void)snprintf(line, sizeof(line), "linked item %s to an entity",
                               items->items[value.info].classname);
                if (!bot_goal_log(g, line, e)) return false;
            }
            continue;
        }
        size_t index = 0;
        while (index < items->count && items->items[index].model_index != entity->model_index) ++index;
        if (index == items->count) continue;
        uint32_t id;
        if(!allocate(g,&id,e)) return false;
        if (!id) continue;
        if (!bot_goal_level_word(g, id, 44, (uint32_t)entity->number, e) ||
            !bot_goal_level_word(g, id, 0, (uint32_t)g->initial_count + (uint32_t)entity->number, e) ||
            !bot_goal_level_word(g, id, 4, (uint32_t)index, e) ||
            !bot_goal_level_vector(g, id, 16, entity->origin, e)) return false;
        qa_bounds bounds = {items->items[index].mins, items->items[index].maxs};
        qa_vec3 target; uint32_t area;
        if (!qa_bot_navigation_best(n, entity->origin, bounds, &target, &area, e) ||
            !bot_goal_level_word(g, id, 28, area, e) || !bot_goal_level_vector(g, id, 32, target, e)) return false;
        if (qa_bot_navigation_area(n, area).contents & 128) {
            if (!release(g, id, false, e)) return false;
            continue;
        }
        if (!bot_goal_level_word(g, id, 48, map_float_word(g->time + 30), e) || !add(g, id, e)) return false;
    }
    return true;
}
bool qa_bot_goals_update_items(qa_bot_goals *g, qa_bot_navigation *n, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!g->level_allocation.owner) return true;
    g->busy = true;
    size_t steps = 0;
    for (uint32_t id = g->level_head, next; id; id = next) {
        bot_level_item item;
        if (++steps > g->level_capacity) { g->busy = false; return bot_goal_fail(e, "Invalid level-item expiration list"); }
        if (!bot_goal_level_read(g, id, &item, e)) { g->busy = false; return false; }
        next = item.next;
        if (item.timeout != 0 && item.timeout < g->time && !release(g, id, true, e)) { g->busy = false; return false; }
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
    bot_level_item value; bool found; uint32_t id;
    if (!bot_goal_find(g, number, &id, &value, &found, e)) return false;
    if (!found) return true;
    g->busy = true;
    const qa_bot_goal_entity *entities; size_t count; void *lease;
    bool ok = bot_goal_entities(g, &entities, &count, &lease, e);
    if (ok) {
        const qa_bot_items_view *items;
        for (size_t i = 0; ok && i < count; ++i) {
            ok = bot_goal_level_read(g, id, &value, e) && qa_bot_items_view_read(g->items, &items, e);
            if (!ok) break;
            if (value.info >= items->count) {ok=bot_goal_fail(e, "Invalid level-item source info index");break;}
            if (entities[i].model_index && entities[i].model_index == items->items[value.info].model_index &&
                same_vector(entities[i].origin, entities[i].last_visible_origin) &&
                qa_vec_length(qa_vec_sub(value.origin, entities[i].origin)) < 30)
                ok = bot_goal_level_word(g, id, 44, (uint32_t)entities[i].number, e);
        }
        bot_goal_entities_end(g, lease);
    }
    g->busy = false;
    return ok;
}
bool qa_bot_goals_name_read(const qa_bot_goals *g, int32_t number, const char **out, qa_error *e) {
    if (!g || !out) return bot_goal_fail(e, "Missing source goal-name owner/output");
    *out = "";
    if (number >= QA_BOT_SOURCE_GOAL_MIN)
        for (size_t i = 0; i < g->source_count; ++i)
            if (g->source[i].goal.number == number) {*out = g->source[i].name;return true;}
    if (!g->configured) return true;
    bot_level_item item; bool found; uint32_t id;
    if (!bot_goal_find(g, number, &id, &item, &found, e)) return false;
    if (!found) return true;
    const qa_bot_items_view *items;
    if (!qa_bot_items_view_read(g->items, &items, e)) return false;
    if (item.info >= items->count) return bot_goal_fail(e, "Goal-name info exceeds its source configuration");
    *out = items->items[item.info].name;return true;
}
bool qa_bot_goals_level_item(const qa_bot_goals *g, int32_t after, const char *name,
                              qa_bot_goal *out, bool *found, qa_error *e) {
    if (!g || !name || !out || !found) return bot_goal_fail(e, "invalid level item query");
    *found = false;
    if (!g->configured) return true;
    uint32_t id = g->level_head;
    if (after >= 0 && after < QA_BOT_SOURCE_GOAL_MIN) {
        bot_level_item item; bool present; uint32_t previous;
        if (!bot_goal_find(g, after, &previous, &item, &present, e)) return false;
        if (!present) return true;
        id = item.next;
    } else if (after >= QA_BOT_SOURCE_GOAL_MIN) id = 0;
    size_t steps = 0;
    for (; id;) {
        bot_level_item value;
        if (++steps > g->level_capacity) return bot_goal_fail(e, "Level-item query list has a cycle");
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        const bot_level_item *item = &value;
        const qa_bot_items_view *items;
        if (!qa_bot_items_view_read(g->items, &items, e)) return false;
        if (item->info >= items->count) return bot_goal_fail(e, "Level item source info index exceeds its configuration");
        if (bot_goal_allowed(g, item->flags) && !(item->flags & 8) &&
            bot_goal_equal_name(name, items->items[item->info].name)) {
            int32_t info = out->item_info;
            if (!bot_goal_item(g, item, out, e)) return false;
            out->item_info = info;
            out->flags &= ~QA_BOT_GOAL_ROAM;
            *found = true;
            return true;
        }
        id = item->next;
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
static void info_goal(const qa_bot_memory_span *info, qa_bot_goal *out) {
    out->origin = map_vector(info->data);
    uint32_t area = map_word(info->data + 12);
    memcpy(&out->area, &area, sizeof(area));
    out->entity = 0;
    out->mins = qa_v3(-8, -8, -8);
    out->maxs = qa_v3(8, 8, 8);
}
bool qa_bot_goals_location(const qa_bot_goals *g, const char *name, qa_bot_goal *out,
                            bool *found, qa_error *e) {
    if (!g || !name || !out || !found) return bot_goal_fail(e, "invalid map location query");
    *found = false;
    size_t steps = 0;
    for (uint32_t pointer = g->location_head; pointer;) {
        qa_bot_memory_span bytes; bool camp; char candidate[129];
        if (++steps > g->info_count) return bot_goal_fail(e, "Goal map location list has a cycle");
        if (!bot_goal_info_span(g, pointer, &bytes, &camp, e)) return false;
        memcpy(candidate, bytes.data + 16, 128); candidate[128] = 0;
        if (bot_goal_equal_name(name, candidate)) {
            info_goal(&bytes, out);
            *found = true;
            break;
        }
        pointer = map_word(bytes.data + (camp ? 160u : 144u));
    }
    return true;
}
bool qa_bot_goals_camp(const qa_bot_goals *g, int32_t index, qa_bot_goal *out,
                       int32_t *next, qa_error *e) {
    if (!g || !out || !next) return bot_goal_fail(e, "invalid camp goal query");
    if (index < 0) index = 0;
    *next = 0;
    size_t steps = 0; uint32_t pointer = g->camp_head;
    while (pointer) {
        qa_bot_memory_span bytes; bool camp;
        if (++steps > g->info_count) return bot_goal_fail(e, "Goal map camp list has a cycle");
        if (!bot_goal_info_span(g, pointer, &bytes, &camp, e)) return false;
        if ((size_t)index == steps - 1) { info_goal(&bytes, out); *next = index + 1; break; }
        pointer = map_word(bytes.data + (camp ? 160u : 144u));
    }
    return true;
}

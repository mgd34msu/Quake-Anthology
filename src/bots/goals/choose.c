#include "internal.h"

typedef struct goal_choice {
    qa_bot_goals *goals;
    bot_goal_slot *state;
    qa_bot_navigation *navigation;
    const qa_bot_goal_choice *query;
    uint32_t handle, area, long_term_time, best_item;
    float best_weight, avoid_duration;
    qa_bot_goal best;
    bool found, native;
} goal_choice;
static bool travel(goal_choice *c, uint32_t from, qa_vec3 origin, uint32_t to,
                    uint32_t *out, qa_error *e) {
    qa_bot_nav_route_query query = {.area = from, .goal_area = to, .origin = origin,
                                    .has_origin = true, .travel_flags = c->query->travel_flags};
    qa_bot_nav_route result;
    if (!qa_bot_navigation_route(c->navigation, &query, &result, e)) return false;
    *out = result.found ? result.travel_time : 0;
    return true;
}
static bool consider(goal_choice *c, const qa_bot_goal *goal, float weight, bool native,
                      uint32_t item_pointer, float avoid, bool return_leg, qa_error *e) {
    if (!(weight > 0)) return true;
    uint32_t time;
    if (!travel(c, c->area, c->query->origin, (uint32_t)goal->area, &time, e)) return false;
    if (!time || (c->query->nearby && !((float)time < c->query->maximum_time))) return true;
    float remaining;
    if (!qa_bot_goals_avoid_time(c->goals, c->handle, goal->number, &remaining, e)) return false;
    if (remaining - (float)time * .009f > 0) return true;
    weight /= (float)time * .01f;
    if (!(weight > c->best_weight)) return true;
    qa_vec3 goal_origin = goal->origin;
    if (c->query->nearby && native) {
        bot_level_item item;
        if (!bot_goal_level_read(c->goals, item_pointer, &item, e)) return false;
        goal_origin = item.goal_origin; return_leg = item.timeout == 0;
    }
    if (c->query->nearby && c->query->long_term && return_leg) {
        uint32_t back;
        if (!travel(c, (uint32_t)goal->area, goal_origin,
                    (uint32_t)c->query->long_term->area, &back, e)) return false;
        if (back > c->long_term_time) return true;
    }
    c->best = *goal;
    c->best_weight = weight;
    c->avoid_duration = avoid;
    c->native = native;
    c->best_item = item_pointer;
    c->found = true;
    return true;
}
uint32_t bot_goal_source_bucket(qa_actor_id actor) {
    uint64_t h = actor.registry ^ actor.generation ^ ((uint64_t)actor.slot * UINT64_C(0x9e3779b97f4a7c15));
    h ^= h >> 33;
    return (uint32_t)h & 255;
}
static bool source_binding(qa_bot_goals *g, const qa_bot_pickup_goal *current,
                            qa_vec3 origin, uint32_t area, qa_bounds world,
                            qa_bot_goal *out, qa_error *e) {
    uint32_t bucket = bot_goal_source_bucket(current->actor), index = g->source_buckets[bucket];
    while (index && !qa_actor_id_equal(g->source[index - 1].actor, current->actor))
        index = g->source[index - 1].hash_next;
    if (!current->name) return bot_goal_fail(e, "source pickup lacks a name");
    size_t length = strlen(current->name);
    if (length == SIZE_MAX) return bot_goal_fail(e, "source pickup name is too large");
    char *name = NULL;
    if (!index || g->source[index - 1].name_capacity <= length) {
        name = malloc(length + 1);
        if (!name) { qa_error_set(e, QA_ERROR_MEMORY, length, "retaining source pickup name"); return false; }
        memcpy(name, current->name, length + 1);
    }
    if (!index) {
        if (g->next_source < QA_BOT_SOURCE_GOAL_MIN || g->source_count >= UINT32_MAX) {
            free(name);
            return bot_goal_fail(e, "source pickup goal range exhausted");
        }
        if (g->source_count == g->source_capacity) {
            size_t capacity = g->source_capacity ? g->source_capacity * 2 : 64;
            if (capacity <= g->source_capacity || capacity > SIZE_MAX / sizeof(*g->source)) {
                free(name);
                return bot_goal_fail(e, "source pickup goal storage overflow");
            }
            bot_source_goal *source = realloc(g->source, capacity * sizeof(*source));
            if (!source) {
                free(name);
                qa_error_set(e, QA_ERROR_MEMORY, capacity, "growing retained source pickup goals");
                return false;
            }
            g->source = source;
            g->source_capacity = capacity;
        }
        index = (uint32_t)++g->source_count;
        g->source[index - 1] = (bot_source_goal){.actor = current->actor,
            .goal = {.number = g->next_source--, .flags = QA_BOT_GOAL_ITEM, .item_info = -1},
            .hash_next = g->source_buckets[bucket]};
        g->source_buckets[bucket] = index;
    }
    bot_source_goal *binding = &g->source[index - 1];
    if (name) {
        free(binding->name);
        binding->name = name;
        binding->name_capacity = length + 1;
    } else memcpy(binding->name, current->name, length + 1);
    binding->goal.origin = origin;
    binding->goal.area = (int32_t)area;
    binding->goal.mins = qa_vec_sub(world.mins, origin);
    binding->goal.maxs = qa_vec_sub(world.maxs, origin);
    binding->goal.entity = current->entity;
    *out = binding->goal;
    return true;
}
static bool choose_sources(goal_choice *c, const qa_actor_id *actors, size_t count, qa_error *e) {
    qa_bot_goals *g = c->goals;
    for (size_t i = 0; i < count; ++i) {
        qa_bot_pickup_goal current; bool found;
        int32_t client;
        if (!bot_goal_record_integer_read(&c->state->record,BOT_GOAL_CLIENT,&client,e) ||
            !g->services.pickup(g->services.context, client, actors[i], &current, &found, e)) return false;
        if (!found || !qa_actor_id_equal(current.actor, actors[i]) || !(current.utility > 0)) continue;
        if (!qa_vec_finite(current.origin) || !qa_vec_finite(current.bounds.mins) ||
            !qa_vec_finite(current.bounds.maxs) || !isfinite(current.utility))
            return bot_goal_fail(e, "invalid canonical pickup goal observation");
        qa_bounds world = qa_bounds_translate(current.bounds, current.origin);
        qa_vec3 center = qa_vec_scale(qa_vec_add(world.mins, world.maxs), .5f), origin;
        qa_bounds local = {qa_vec_sub(world.mins, center), qa_vec_sub(world.maxs, center)};
        uint32_t area;
        if (!qa_bot_navigation_best(c->navigation, center, local, &origin, &area, e)) return false;
        if (!area) continue;
        qa_bot_goal goal;
        if (!source_binding(g, &current, origin, area, world, &goal, e) ||
            !consider(c, &goal, current.utility, false, 0, 0, true, e)) return false;
    }
    return true;
}
static bool choose(goal_choice *c, qa_error *e) {
    qa_bot_goals *g = c->goals;
    const qa_bot_items_view *items;
    bot_goal_weights *weights;int32_t client;
    if (!bot_goal_config_get(g,c->state,&weights,e)) return false;
    if (!weights) return true;
    if (!bot_goal_record_integer_read(&c->state->record,BOT_GOAL_CLIENT,&client,e)) return false;
    c->navigation = g->services.navigation(g->services.context, client);
    if (!c->navigation) return true;
    if (!qa_bot_navigation_reachable(c->navigation, c->query->origin, &c->area, e)) return false;
    if ((!c->area || !qa_bot_navigation_area(c->navigation, c->area).reach_count) &&
        !bot_goal_record_word_read(&c->state->record,BOT_GOAL_LAST_AREA,&c->area,e)) return false;
    if (!bot_goal_record_word_write(&c->state->record,BOT_GOAL_LAST_AREA,c->area,e)) return false;
    if (!c->area) return true;
    c->long_term_time = 99999;
    if (c->query->nearby && c->query->long_term &&
        !travel(c, c->area, c->query->origin, (uint32_t)c->query->long_term->area,
                 &c->long_term_time, e)) return false;
    if (!g->configured) return true;
    size_t steps = 0;
    for (uint32_t id = g->level_head; id;) {
        bot_level_item value;
        if (++steps > g->level_capacity) return bot_goal_fail(e, "Goal choice level-item list has a cycle");
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        const bot_level_item *item = &value;
        if (!bot_goal_allowed(g, item->flags) || (item->flags & 8) || !item->goal_area ||
            (!item->entity && !(item->flags & 16))) goto advance;
        if (g->services.owns_item) {
            bool owns;
            if (!bot_goal_record_integer_read(&c->state->record,BOT_GOAL_CLIENT,&client,e) ||
                !g->services.owns_item(g->services.context, client, item->entity, &owns, e)) return false;
            if (owns) goto advance;
        }
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        if (!qa_bot_items_view_read(g->items, &items, e)) return false;
        if (item->info >= items->count) return bot_goal_fail(e, "Goal choice info index exceeds its source configuration");
        const qa_bot_item_info *info = &items->items[item->info];
        if (info->number < 0)
            return bot_goal_fail(e, "item weight index is outside its configuration");
        int32_t index;
        if(!bot_goal_indexes_read(g,c->state,info->number,&index,e)) return false;
        if (index < 0) goto advance;
        float weight;
        qa_bot_inventory_view native = {.data = c->query->inventory, .count = c->query->inventory_count};
        const qa_bot_inventory_view *inventory = c->query->inventory_source ? c->query->inventory_source : &native;
        if (!bot_goal_config_get(g,c->state,&weights,e)) return false;
        if (!weights) return bot_goal_fail(e,"Item weight configuration is absent during evaluation");
        if (!qa_bot_weights_evaluate_view(weights->weights, (uint32_t)index, inventory,
                                           &g->options.random, g->workspace, &weight, e)) return false;
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        if (item->timeout != 0) weight += g->services.dropped_weight ?
            g->services.dropped_weight(g->services.context) : g->options.dropped_weight;
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        if (item->flags & 16) weight *= item->weight;
        qa_bot_goal goal = {.area = 0, .number = item->number};
        memcpy(&goal.area, &item->goal_area, sizeof(goal.area));
        if (!consider(c, &goal, weight, true, id, 0, false, e)) return false;
advance:
        if (!bot_goal_level_read(g, id, &value, e)) return false;
        id = value.next;
    }
    if (g->services.pickups) {
        const qa_actor_id *actors; size_t count; void *lease;
        if (!bot_goal_record_integer_read(&c->state->record,BOT_GOAL_CLIENT,&client,e) ||
            !g->services.pickups(g->services.context, client, &actors, &count, &lease, e)) return false;
        bool ok = count && !actors ? bot_goal_fail(e, "invalid pickup candidate snapshot") :
                  choose_sources(c, actors, count, e);
        g->services.pickups_end(g->services.context, lease);
        if (!ok) return false;
    }
    if (c->found) {
        if (c->native) {
            bot_level_item item;
            if (!bot_goal_level_read(g, c->best_item, &item, e)) return false;
            if (!qa_bot_items_view_read(g->items, &items, e)) return false;
            if (item.info >= items->count) return bot_goal_fail(e, "Chosen item info index exceeds its source configuration");
            float avoid = item.timeout != 0 ? 10 : bot_goal_default_avoid(&items->items[item.info]);
            if (!bot_goal_avoid(g, c->state, item.number, avoid, e) ||
                !bot_goal_level_read(g, c->best_item, &item, e)) return false;
            if (!bot_goal_item(g, &item, &c->best, e)) return false;
        }
        bool pushed;
        if (!bot_goal_push(g, c->state, &c->best, &pushed, e)) return false;
    }
    return true;
}
bool qa_bot_goals_choose(qa_bot_goals *g, uint32_t handle, const qa_bot_goal_choice *query,
                         bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *state = bot_goal_slot_get(g, handle, e);
    if (!state) return false;
    if (!query || !out || !qa_vec_finite(query->origin) ||
        (!query->inventory_source && query->inventory_count && !query->inventory) ||
        (query->nearby && !isfinite(query->maximum_time)))
        return bot_goal_fail(e, "invalid bot item choice query");
    goal_choice c = {.goals = g, .state = state, .query = query, .handle = handle};
    g->busy = true;
    bool ok = choose(&c, e);
    g->busy = false;
    if (ok) *out = c.found;
    return ok;
}
bool bot_goal_source_status(qa_bot_goals *g, int32_t client, const qa_bot_goal *goal,
                            qa_bot_source_goal_status *out, qa_error *e) {
    *out = goal->number < QA_BOT_SOURCE_GOAL_MIN ? QA_BOT_GOAL_NATIVE : QA_BOT_GOAL_UNAVAILABLE;
    if (*out == QA_BOT_GOAL_NATIVE || !g->services.pickup) return true;
    for (size_t i = 0; i < g->source_count; ++i) {
        bot_source_goal *binding = &g->source[i];
        if (binding->goal.number != goal->number) continue;
        qa_bot_pickup_goal current; bool found;
        if (!g->services.pickup(g->services.context, client, binding->actor, &current, &found, e)) return false;
        if (found && qa_actor_id_equal(current.actor, binding->actor) &&
            current.entity == goal->entity && current.utility > 0) *out = QA_BOT_GOAL_AVAILABLE;
        break;
    }
    return true;
}
bool qa_bot_goals_source_status(qa_bot_goals *g, int32_t client, const qa_bot_goal *goal,
                                qa_bot_source_goal_status *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!goal || !out) return bot_goal_fail(e, "invalid source goal query");
    g->busy = true;
    bool ok = bot_goal_source_status(g, client, goal, out, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_missing_visible(qa_bot_goals *g, int32_t client, qa_vec3 eye,
                                  const qa_bot_goal *goal, bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!goal || !out) return bot_goal_fail(e, "invalid goal visibility query");
    g->busy = true;
    qa_bot_source_goal_status status;
    bool ok = bot_goal_source_status(g, client, goal, &status, e);
    *out = false;
    if (ok && status != QA_BOT_GOAL_NATIVE) *out = status == QA_BOT_GOAL_UNAVAILABLE;
    else if (ok && (goal->flags & QA_BOT_GOAL_ITEM)) {
        qa_bot_navigation *n = g->services.navigation(g->services.context, client);
        if (n) {
            qa_trace_result trace;
            ok = qa_bot_navigation_trace(n, eye, qa_vec_add(goal->origin, goal->mins), NULL,
                                         qa_bot_navigation_actor(n), 1, &trace, e);
            if (ok && trace.fraction >= 1 && goal->entity > 0) {
                float updated = 0;
                if (g->services.entity) {
                    qa_bot_goal_entity entity; bool found;
                    ok = g->services.entity(g->services.context, goal->entity, &entity, &found, e);
                    if (ok && found) updated = entity.last_update_time;
                } else {
                    const qa_bot_goal_entity *entities; size_t count; void *lease;
                    ok = bot_goal_entities(g, &entities, &count, &lease, e);
                    if (ok) {
                        for (size_t i = 0; i < count; ++i)
                            if (entities[i].number == goal->entity) { updated = entities[i].last_update_time; break; }
                        bot_goal_entities_end(g, lease);
                    }
                }
                if (ok) *out = updated < g->time - .5f;
            }
        }
    }
    g->busy = false;
    return ok;
}
bool qa_bot_goal_touching(qa_vec3 origin, const qa_bot_goal *goal) {
    if (!goal) return false;
    qa_vec3 min = qa_vec_add(qa_vec_sub(goal->mins, qa_v3(15, 15, 32)), goal->origin);
    qa_vec3 max = qa_vec_add(qa_vec_sub(goal->maxs, qa_v3(-15, -15, -24)), goal->origin);
    return origin.x >= min.x && origin.x <= max.x && origin.y >= min.y && origin.y <= max.y &&
           origin.z >= min.z && origin.z <= max.z;
}

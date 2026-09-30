#include "internal.h"
#include "../checkpoint_internal.h"
#include <stdio.h>

bool bot_goal_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_goal_mutable(qa_bot_goals *g, qa_error *e) {
    return g && !g->busy ? true : bot_goal_fail(e, "goal owner is absent or executing a query");
}
bool qa_bot_goals_active(const qa_bot_goals *g) { return g && g->busy; }
bool qa_bot_goals_has_handle(const qa_bot_goals *g, uint32_t id) {
    return g && id && id <= g->options.maximum_states && g->states[id - 1].used;
}
bot_goal_slot *bot_goal_slot_get(const qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!g || !id || id > g->options.maximum_states || !g->states[id - 1].used) {
        bot_goal_fail(e, "invalid bot goal state handle");
        return NULL;
    }
    return &g->states[id - 1];
}
static void release_weights(qa_bot_goals *g, bot_goal_slot *s) {
    bot_goal_weights *w = s->weights;
    s->weights = NULL;
    if (!w || --w->users) return;
    bot_goal_weights **link = &g->weights;
    while (*link != w) link = &(*link)->next;
    *link = w->next;
    qa_bot_weights_release(w->weights);
    free(w->indices);
    free(w);
}
static bool bind_weights(qa_bot_goals *g, bot_goal_slot *s, qa_bot_weights *weights, qa_error *e) {
    if (s->weights && s->weights->weights == weights) return true;
    bot_goal_weights *w = g->weights;
    if (weights) {
        while (w && w->weights != weights) w = w->next;
        if (!w) {
            size_t count = qa_bot_items_read(g->items)->count;
            w = calloc(1, sizeof(*w));
            if (!w || (count && !(w->indices = malloc(count * sizeof(*w->indices))))) {
                free(w);
                qa_error_set(e, QA_ERROR_MEMORY, count, "allocating shared item weight mapping");
                return false;
            }
            const qa_bot_items_view *items = qa_bot_items_read(g->items);
            for (size_t i = 0; i < count; ++i)
                w->indices[i] = qa_bot_weights_find(weights, items->items[i].classname);
            w->weights = weights;
            qa_bot_weights_retain(weights);
            w->next = g->weights;
            g->weights = w;
        }
        ++w->users;
    }
    release_weights(g, s);
    s->weights = weights ? w : NULL;
    return true;
}
bool qa_bot_goals_create(qa_bot_items *items, const qa_bot_goal_options *options,
                         const qa_bot_goal_services *services, qa_bot_goals **out, qa_error *e) {
    if (!items || !options || !services || !out || !options->maximum_states ||
        !options->maximum_level_items || options->maximum_level_items >= QA_BOT_SOURCE_GOAL_MIN - 1000000 ||
        !isfinite(options->dropped_weight) || !options->random.next || !services->navigation ||
        (!!services->entities != !!services->entities_end) ||
        (!!services->pickups != !!services->pickups_end) ||
        (services->pickups && !services->pickup) ||
        options->maximum_states > SIZE_MAX / sizeof(bot_goal_slot))
        return bot_goal_fail(e, "invalid bot goal services or capacities");
    qa_bot_goals *g = calloc(1, sizeof(*g));
    if (!g || !(g->states = calloc(options->maximum_states, sizeof(*g->states)))) {
        free(g);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot goal states");
        return false;
    }
    g->options = *options;
    g->services = *services;
    g->items = items;
    g->configured = true;
    g->next_source = INT32_MAX;
    qa_bot_items_retain(items);
    if (!qa_bot_weight_workspace_create(&g->workspace, e)) {
        qa_bot_goals_destroy(g);
        return false;
    }
    *out = g;
    return true;
}
void qa_bot_goals_destroy(qa_bot_goals *g) {
    if (!g || g->busy) return;
    for (uint32_t i = 0; i < g->options.maximum_states; ++i) release_weights(g, &g->states[i]);
    bot_goal_map_clear(g);
    qa_bot_weight_workspace_destroy(g->workspace);
    qa_bot_items_release(g->items);
    free(g->states);
    free(g);
}
bool qa_bot_goals_reconfigure(qa_bot_goals *g, qa_bot_items *items, int32_t game_type,
                               uint32_t next_map_capacity, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!items) { g->configured = false; return true; }
    if (!next_map_capacity || next_map_capacity >= QA_BOT_SOURCE_GOAL_MIN - 1000000)
        return bot_goal_fail(e, "invalid goal reconfiguration");
    const qa_bot_items_view *view = qa_bot_items_read(items);
    for (uint32_t i = g->level_head; i; i = g->level[i].next)
        if (g->level[i].info >= view->count)
            return bot_goal_fail(e, "replacement item config omits retained level item");
    size_t count = 0;
    for (bot_goal_weights *w = g->weights; w; w = w->next) ++count;
    int32_t **indices = count ? calloc(count, sizeof(*indices)) : NULL;
    if (count && !indices) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "preparing item weight mappings");
        return false;
    }
    size_t i = 0;
    for (bot_goal_weights *w = g->weights; w; w = w->next, ++i) {
        indices[i] = view->count ? malloc(view->count * sizeof(**indices)) : NULL;
        if (view->count && !indices[i]) {
            for (size_t j = 0; j < i; ++j) free(indices[j]);
            free(indices);
            qa_error_set(e, QA_ERROR_MEMORY, view->count, "rebuilding item weight mapping");
            return false;
        }
        for (size_t j = 0; j < view->count; ++j)
            indices[i][j] = qa_bot_weights_find(w->weights, view->items[j].classname);
    }
    i = 0;
    for (bot_goal_weights *w = g->weights; w; w = w->next, ++i) {
        free(w->indices); w->indices = indices[i];
    }
    free(indices);
    qa_bot_items_retain(items);
    qa_bot_items_release(g->items);
    g->items = items;
    g->configured = true;
    g->options.game_type = game_type;
    g->options.maximum_level_items = next_map_capacity;
    return true;
}
bool qa_bot_goals_time(qa_bot_goals *g, float time, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!isfinite(time)) return bot_goal_fail(e, "bot goal time must be finite");
    g->time = time;
    return true;
}
bool qa_bot_goals_allocate(qa_bot_goals *g, int32_t client, uint32_t *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!out) return bot_goal_fail(e, "missing goal handle output");
    *out = 0;
    for (uint32_t i = 0; i < g->options.maximum_states; ++i)
        if (!g->states[i].used) {
            g->states[i] = (bot_goal_slot){.used = true, .state = {.client = client}};
            *out = i + 1;
            break;
        }
    return true;
}
bool qa_bot_goals_free(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    release_weights(g, s);
    *s = (bot_goal_slot){0};
    return true;
}
bool qa_bot_goals_reset(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    memset(s->state.stack, 0, sizeof(s->state.stack));
    memset(s->state.avoid, 0, sizeof(s->state.avoid));
    s->state.stack_top = 0;
    return true;
}
bool qa_bot_goals_weights(qa_bot_goals *g, uint32_t id, qa_bot_weights *w, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    return s && bind_weights(g, s, w, e);
}
static void breed_report(void *context, const char *message) {
    char line[128];
    (void)snprintf(line, sizeof(line), "%s\n", message);
    bot_goal_report(context, QA_SCRIPT_ERROR, line);
}
bool qa_bot_goals_interbreed(qa_bot_goals *g, uint32_t first, uint32_t second, uint32_t child,
                            bool *matched, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *a = bot_goal_slot_get(g, first, e), *b = bot_goal_slot_get(g, second, e),
                  *c = bot_goal_slot_get(g, child, e);
    if (!a || !b || !c) return false;
    if (!matched) return bot_goal_fail(e, "goal breeding requires matched output");
    *matched = false;
    g->busy = true;
    bool ok = true;
    if (!a->weights || !b->weights || !c->weights)
        bot_goal_report(g, QA_SCRIPT_FATAL, "goal fuzzy interbreeding requires loaded item weights");
    else ok = qa_bot_weights_interbreed_report(c->weights->weights, a->weights->weights,
                                                   b->weights->weights, g, breed_report, matched, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_mutate(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    g->busy = true;
    bool ok = true;
    if (!s->weights) bot_goal_report(g, QA_SCRIPT_FATAL, "goal fuzzy mutation requires loaded item weights");
    else ok = qa_bot_weights_evolve(s->weights->weights, &g->options.random, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_save_weights(qa_bot_goals *g, uint32_t id, qa_error *e) {
    return bot_goal_slot_get(g, id, e) != NULL;
}
static bool push(qa_bot_goals *g, bot_goal_slot *s, const qa_bot_goal *goal,
                 void *context, bool (*read)(void *, qa_bot_goal *, qa_error *),
                 bool *out, qa_error *e) {
    *out = s->state.stack_top < QA_BOT_GOAL_STACK - 1;
    if (*out) {
        ++s->state.stack_top;
        qa_bot_goal value;
        if (read) {
            if (!read(context, &value, e)) return false;
            goal = &value;
        }
        s->state.stack[s->state.stack_top] = *goal;
    } else {
        bot_goal_report(g, QA_SCRIPT_ERROR, "goal heap overflow\n");
        return bot_goal_dump_stack(g, s, e);
    }
    return true;
}
bool bot_goal_push(qa_bot_goals *g, bot_goal_slot *s, const qa_bot_goal *goal, bool *out) {
    return push(g, s, goal, NULL, NULL, out, NULL);
}
bool qa_bot_goals_push(qa_bot_goals *g, uint32_t id, const qa_bot_goal *goal, bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!goal || !out) return bot_goal_fail(e, "missing goal or push output");
    g->busy = true;
    bool ok = push(g, s, goal, NULL, NULL, out, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_push_from(qa_bot_goals *g, uint32_t id, void *context,
                              bool (*read)(void *, qa_bot_goal *, qa_error *), bool *out, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!read || !out) return bot_goal_fail(e, "missing source goal reader/output");
    g->busy = true;
    bool ok = push(g, s, NULL, context, read, out, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_pop(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (s->state.stack_top) --s->state.stack_top;
    return true;
}
bool qa_bot_goals_empty(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    s->state.stack_top = 0;
    return true;
}
bool qa_bot_goals_top(const qa_bot_goals *g, uint32_t id, bool second, qa_bot_goal *out,
                      bool *found, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out || !found) return bot_goal_fail(e, "missing goal stack output");
    *found = s->state.stack_top > (uint32_t)second;
    if (*found) *out = s->state.stack[s->state.stack_top - (uint32_t)second];
    return true;
}
bool qa_bot_goals_avoid_clear(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    memset(s->state.avoid, 0, sizeof(s->state.avoid));
    return true;
}
float bot_goal_default_avoid(const qa_bot_item_info *item) {
    return fmaxf(10, item->respawn_seconds == 0 ? 30 : item->respawn_seconds);
}
void bot_goal_avoid(qa_bot_goals *g, bot_goal_slot *s, int32_t number, float duration) {
    qa_bot_avoid_goal *slot = NULL;
    for (size_t i = 0; i < QA_BOT_AVOID_GOALS; ++i)
        if (s->state.avoid[i].number == number) { slot = &s->state.avoid[i]; break; }
    if (!slot)
        for (size_t i = 0; i < QA_BOT_AVOID_GOALS; ++i)
            if (s->state.avoid[i].expires < g->time) { slot = &s->state.avoid[i]; break; }
    if (slot) *slot = (qa_bot_avoid_goal){number, g->time + duration};
}
bool qa_bot_goals_avoid_set(qa_bot_goals *g, uint32_t id, int32_t number, float duration, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!isfinite(duration)) return bot_goal_fail(e, "invalid goal avoidance duration");
    if (duration < 0) {
        if (!g->configured) return true;
        bot_level_item *item = bot_goal_find(g, number);
        if (!item) return true;
        duration = bot_goal_default_avoid(&qa_bot_items_read(g->items)->items[item->info]);
    }
    if (!isfinite(g->time + duration)) return bot_goal_fail(e, "goal avoidance time overflow");
    bot_goal_avoid(g, s, number, duration);
    return true;
}
bool qa_bot_goals_avoid_remove(qa_bot_goals *g, uint32_t id, int32_t number, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    for (size_t i = 0; i < QA_BOT_AVOID_GOALS; ++i)
        if (s->state.avoid[i].number == number && s->state.avoid[i].expires >= g->time) {
            s->state.avoid[i].expires = 0;
            break;
        }
    return true;
}
bool qa_bot_goals_avoid_time(const qa_bot_goals *g, uint32_t id, int32_t number, float *out, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out) return bot_goal_fail(e, "missing avoidance time output");
    *out = 0;
    for (size_t i = 0; i < QA_BOT_AVOID_GOALS; ++i)
        if (s->state.avoid[i].number == number && s->state.avoid[i].expires >= g->time) {
            *out = s->state.avoid[i].expires - g->time;
            break;
        }
    return true;
}
bool qa_bot_goals_capture(const qa_bot_goals *g, uint32_t id, qa_bot_goal_state *out,
                          qa_bot_weights **weights, qa_error *e) {
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!out || !weights) return bot_goal_fail(e, "missing goal checkpoint output");
    *out = s->state;
    *weights = s->weights ? s->weights->weights : NULL;
    return true;
}
bool qa_bot_goals_restore(qa_bot_goals *g, uint32_t id, const qa_bot_goal_state *state,
                          qa_bot_weights *weights, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    if (!id || id > g->options.maximum_states || !state || state->stack_top >= QA_BOT_GOAL_STACK)
        return bot_goal_fail(e, "invalid goal state checkpoint");
    for (size_t i = 0; i < QA_BOT_AVOID_GOALS; ++i)
        if (!isfinite(state->avoid[i].expires)) return bot_goal_fail(e, "invalid saved avoid time");
    bot_goal_slot *s = &g->states[id - 1];
    if (!bind_weights(g, s, weights, e)) return false;
    s->state = *state;
    s->used = true;
    return true;
}

struct bot_goal_restore {
    qa_bot_goals *owner;
    uint32_t id;
    bot_goal_slot slot;
};
void bot_goal_restore_lock(qa_bot_goals *g, bool locked) { g->busy = locked; }
bool bot_goal_restore_prepare(qa_bot_goals *g, uint32_t id, const qa_bot_goal_state *state,
                              qa_bot_weights *weights, bot_goal_restore **out, qa_error *e) {
    if (!qa_bot_goals_has_handle(g,id) || !state || state->stack_top >= QA_BOT_GOAL_STACK)
        return bot_goal_fail(e,"invalid prepared goal checkpoint");
    for (size_t i=0;i<QA_BOT_AVOID_GOALS;++i)
        if (!isfinite(state->avoid[i].expires)) return bot_goal_fail(e,"invalid saved avoid time");
    bot_goal_restore *prepared=calloc(1,sizeof(*prepared));
    if (!prepared) { qa_error_set(e,QA_ERROR_MEMORY,0,"preparing goal checkpoint");return false; }
    prepared->owner=g;prepared->id=id;
    if (!bind_weights(g,&prepared->slot,weights,e)) { free(prepared);return false; }
    prepared->slot.state=*state;prepared->slot.used=true;
    *out=prepared;return true;
}
void bot_goal_restore_finish(bot_goal_restore *prepared, bool commit) {
    if (!prepared) return;
    if (commit) {
        bot_goal_slot *slot=&prepared->owner->states[prepared->id-1];
        release_weights(prepared->owner,slot);
        *slot=prepared->slot;
    } else release_weights(prepared->owner,&prepared->slot);
    free(prepared);
}

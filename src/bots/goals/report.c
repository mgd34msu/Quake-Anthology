#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

bool bot_goal_log(qa_bot_goals *g, const char *text, qa_error *error) {
    return !g->services.log || g->services.log(g->services.context, text, error);
}
void bot_goal_report(qa_bot_goals *g, qa_script_severity severity, const char *text) {
    if (g->services.report) g->services.report(g->services.context, severity, text);
    else if (g->services.diagnostic) g->services.diagnostic(g->services.context, text);
}
bool bot_goal_position_report(qa_bot_goals *g, const char *prefix, qa_vec3 point,
                                const char *suffix, qa_error *e) {
    char x[64], y[64], z[64], line[512];
    if (!qa_format_fixed(point.x, 1, x, sizeof(x), e) ||
        !qa_format_fixed(point.y, 1, y, sizeof(y), e) ||
        !qa_format_fixed(point.z, 1, z, sizeof(z), e)) return false;
    (void)snprintf(line, sizeof(line), "%s%s %s %s%s", prefix, x, y, z, suffix);
    bot_goal_report(g, QA_SCRIPT_INFO, line);
    return true;
}
bool bot_goal_dump_stack(qa_bot_goals *g, const bot_goal_slot *s, qa_error *e) {
    for (uint32_t i = 1; i <= s->state.stack_top; ++i) {
        char line[64];
        const char *name = qa_bot_goals_name(g, s->state.stack[i].number);
        (void)snprintf(line, sizeof(line), "%u: %.31s", i, name ? name : "");
        if (!bot_goal_log(g, line, e)) return false;
    }
    return true;
}
bool qa_bot_goals_dump_stack(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    g->busy = true;
    bool ok = bot_goal_dump_stack(g, s, e);
    g->busy = false;
    return ok;
}
bool qa_bot_goals_dump_avoid(qa_bot_goals *g, uint32_t id, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    g->busy = true;
    bool ok = true;
    for (size_t i = 0; ok && i < QA_BOT_AVOID_GOALS; ++i) {
        const qa_bot_avoid_goal *goal = &s->state.avoid[i];
        if (!(goal->expires >= g->time)) continue;
        const char *name = qa_bot_goals_name(g, goal->number);
        char remaining[64], line[160];
        ok = qa_format_fixed((float)(goal->expires - g->time), 6, remaining, sizeof(remaining), e);
        if (!ok) break;
        (void)snprintf(line, sizeof(line), "avoid goal %.31s, number %d for %s seconds",
                       name ? name : "", goal->number, remaining);
        ok = bot_goal_log(g, line, e);
    }
    g->busy = false;
    return ok;
}
bool qa_bot_goals_load_weights(qa_bot_goals *g, uint32_t id, qa_bot_library *library,
                                const char *path, int32_t *result, qa_error *e) {
    if (!bot_goal_mutable(g, e)) return false;
    bot_goal_slot *s = bot_goal_slot_get(g, id, e);
    if (!s) return false;
    if (!library || !path || !result) return bot_goal_fail(e, "missing goal weight source/result");
    *result = 9;
    g->busy = true;
    qa_bot_weights *weights = NULL;
    qa_error local = {0};
    bool loaded = qa_bot_weights_load(library, path, &weights, &local);
    g->busy = false;
    if (!loaded) {
        if (local.code != QA_ERROR_FORMAT && local.code != QA_ERROR_NOT_FOUND) {
            if (e) *e = local;
            return false;
        }
        if (!qa_bot_goals_weights(g, id, NULL, e)) return false;
        g->busy = true;
        bot_goal_report(g, QA_SCRIPT_FATAL, "couldn't load weights\n");
        g->busy = false;
        return true;
    }
    bool ok = qa_bot_goals_weights(g, id, weights, e);
    qa_bot_weights_release(weights);
    if (!ok || !g->configured) return ok;
    g->busy = true;
    const qa_bot_items_view *items = qa_bot_items_read(g->items);
    for (size_t i = 0; i < items->count; ++i)
        if (s->weights->indices[i] < 0) {
            char line[256];
            (void)snprintf(line, sizeof(line), "item info %zu \"%s\" has no fuzzy weight\r\n",
                           i, items->items[i].classname);
            if (!bot_goal_log(g, line, e)) { g->busy = false; return false; }
        }
    *result = 0;
    g->busy = false;
    return true;
}

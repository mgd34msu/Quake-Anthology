#include "tools_internal.h"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

typedef struct timer_scope { size_t index; double start, children; } timer_scope;
struct qa_profiler {
    double (*clock)(void *);
    void *context;
    qa_timer_report *totals;
    timer_scope *stack;
    qa_timer_stamp *stamps;
    size_t total_count, total_capacity, stack_count, stack_capacity;
    size_t stamp_count, stamp_capacity, stamp_first;
    double epoch;
    bool enabled, busy;
};
static bool clock_read(qa_profiler *p, double *out, qa_error *error) {
    p->busy = true; double value = p->clock(p->context); p->busy = false;
    if (!isfinite(value)) return tools_fail(error, "profiler clock must be finite");
    *out = value; return true;
}
static void *grow(void *array, size_t *capacity, size_t item, qa_error *error) {
    size_t count = *capacity ? *capacity * 2 : 16;
    if (count < *capacity || count > SIZE_MAX / item) { tools_fail(error, "profiler collection overflow"); return NULL; }
    void *copy = realloc(array, count * item);
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "growing profiler collection"); return NULL; }
    *capacity = count; return copy;
}
static void clear(qa_profiler *p) {
    for (size_t i = 0; i < p->total_count; ++i) free((char *)p->totals[i].name);
    for (size_t i = 0; i < p->stamp_count; ++i) free((char *)p->stamps[(p->stamp_first + i) % p->stamp_capacity].name);
    p->total_count = p->stamp_count = p->stamp_first = 0;
}
bool qa_profiler_create(double (*clock)(void *), void *context, size_t capacity, qa_profiler **out, qa_error *error) {
    if (!clock || !out || !capacity || capacity > SIZE_MAX / sizeof(qa_timer_stamp)) return tools_fail(error, "invalid profiler options");
    qa_profiler *p = calloc(1, sizeof *p);
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating profiler"); return false; }
    p->clock = clock; p->context = context; p->stamp_capacity = capacity;
    p->stamps = calloc(capacity, sizeof(*p->stamps));
    if (!p->stamps) { free(p); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating profiler stamp ring"); return false; }
    if (!clock_read(p, &p->epoch, error)) { free(p->stamps); free(p); return false; }
    *out = p; return true;
}
bool qa_profiler_destroy(qa_profiler *p, qa_error *error) {
    if (!p) return true;
    if (p->busy || p->stack_count) return tools_fail(error, "profiler teardown requires scopes and callbacks to return");
    clear(p); free(p->totals); free(p->stack); free(p->stamps); free(p); return true;
}
bool qa_profiler_enable(qa_profiler *p, bool enabled, qa_error *error) {
    if (!p || p->busy || p->stack_count) return tools_fail(error, "cannot change profiler mode inside an active scope");
    p->enabled = enabled; return true;
}
bool qa_profiler_reset(qa_profiler *p, qa_error *error) {
    if (!p || p->busy || p->stack_count) return tools_fail(error, "cannot reset profiler inside an active scope");
    double epoch; if (!clock_read(p, &epoch, error)) return false;
    clear(p); p->epoch = epoch; return true;
}
bool qa_profiler_push(qa_profiler *p, const char *name, qa_error *error) {
    if (!p || !name || p->busy) return tools_fail(error, "invalid profiler scope admission");
    if (!p->enabled) return true;
    size_t index = 0;
    while (index < p->total_count && strcmp(p->totals[index].name, name)) ++index;
    if (p->stack_count == p->stack_capacity) {
        void *copy = grow(p->stack, &p->stack_capacity, sizeof(*p->stack), error); if (!copy) return false; p->stack = copy;
    }
    char *copy = NULL;
    if (index == p->total_count) {
        if (p->total_count == p->total_capacity) {
            void *grown = grow(p->totals, &p->total_capacity, sizeof(*p->totals), error); if (!grown) return false; p->totals = grown;
        }
        copy = tools_copy(name, error); if (!copy) return false;
    }
    double now; if (!clock_read(p, &now, error)) { free(copy); return false; }
    if (copy) { p->totals[index] = (qa_timer_report){.name = copy}; ++p->total_count; }
    p->stack[p->stack_count++] = (timer_scope){.index = index, .start = now}; return true;
}
bool qa_profiler_pop(qa_profiler *p, qa_error *error) {
    if (!p || p->busy) return tools_fail(error, "invalid profiler scope retirement");
    if (!p->enabled) return true;
    if (!p->stack_count) return tools_fail(error, "unbalanced profiler pop");
    double now; if (!clock_read(p, &now, error)) return false;
    timer_scope scope = p->stack[p->stack_count - 1]; qa_timer_report *total = &p->totals[scope.index];
    if (total->calls == UINT64_MAX) return tools_fail(error, "profiler call count exceeds native range");
    double elapsed = fmax(0, now - scope.start), self = fmax(0, elapsed - scope.children);
    if (!isfinite(elapsed) || !isfinite(total->total_ms + elapsed) || !isfinite(total->self_ms + self) ||
        (p->stack_count > 1 && !isfinite(p->stack[p->stack_count - 2].children + elapsed))) return tools_fail(error, "profiler timing exceeds native range");
    --p->stack_count;
    if (p->stack_count) p->stack[p->stack_count - 1].children += elapsed;
    ++total->calls; total->total_ms += elapsed; total->self_ms += self; total->maximum_ms = fmax(total->maximum_ms, elapsed); return true;
}
bool qa_profiler_stamp(qa_profiler *p, const char *name, qa_error *error) {
    if (!p || !name || p->busy) return tools_fail(error, "invalid profiler stamp");
    if (!p->enabled) return true;
    char *copy = tools_copy(name, error); if (!copy) return false;
    double now;
    if (!clock_read(p, &now, error) || !isfinite(now - p->epoch)) { free(copy); return tools_fail(error, "profiler stamp exceeds native clock range"); }
    size_t index = (p->stamp_first + p->stamp_count) % p->stamp_capacity;
    if (p->stamp_count == p->stamp_capacity) {
        free((char *)p->stamps[index].name); p->stamp_first = (p->stamp_first + 1) % p->stamp_capacity;
    } else ++p->stamp_count;
    p->stamps[index] = (qa_timer_stamp){copy, now - p->epoch}; return true;
}
static char *copy_label(qa_arena *arena, const char *label, qa_error *error) {
    size_t n = strlen(label);
    char *text = qa_arena_alloc(arena, n + 1, 1, error); if (text) memcpy(text, label, n + 1); return text;
}
bool qa_profiler_report(const qa_profiler *p, qa_arena *scratch, const qa_timer_report **out, size_t *count, qa_error *error) {
    if (!p || !scratch || !out || !count || p->busy) return tools_fail(error, "invalid profiler report");
    qa_timer_report *rows = p->total_count ? qa_arena_alloc(scratch, p->total_count * sizeof(*rows), _Alignof(qa_timer_report), error) : NULL;
    if (p->total_count && !rows) return false;
    for (size_t i = 0; i < p->total_count; ++i) {
        rows[i] = p->totals[i]; rows[i].name = copy_label(scratch, rows[i].name, error); if (!rows[i].name) return false;
    }
    *out = rows; *count = p->total_count; return true;
}
bool qa_profiler_stamps(const qa_profiler *p, qa_arena *scratch, const qa_timer_stamp **out, size_t *count, qa_error *error) {
    if (!p || !scratch || !out || !count || p->busy) return tools_fail(error, "invalid profiler stamp report");
    qa_timer_stamp *rows = p->stamp_count ? qa_arena_alloc(scratch, p->stamp_count * sizeof(*rows), _Alignof(qa_timer_stamp), error) : NULL;
    if (p->stamp_count && !rows) return false;
    for (size_t i = 0; i < p->stamp_count; ++i) {
        rows[i] = p->stamps[(p->stamp_first + i) % p->stamp_capacity]; rows[i].name = copy_label(scratch, rows[i].name, error); if (!rows[i].name) return false;
    }
    *out = rows; *count = p->stamp_count; return true;
}
bool qa_profiler_enabled(const qa_profiler *p) { return p && p->enabled; }
bool qa_profiler_idle(const qa_profiler *p) { return p && !p->busy && !p->stack_count; }

bool tools_timer_command(qa_tools *tools, const qa_command_invocation *call, qa_error *error) {
    if (call->argv[0][0] == 't' || call->argv[0][0] == 'T') {
        size_t n = strlen(call->argv[0]);
        if (n == strlen("timerstamp")) {
            size_t length = 0;
            for (size_t i = 1; i < call->argc; ++i) {
                size_t added = strlen(call->argv[i]);
                if (added > SIZE_MAX - length - 1) return tools_fail(error, "timer stamp label overflow");
                length += added + (i > 1 ? 1 : 0);
            }
            if (!length) return tools_fail(error, "usage: timerstamp <label>");
            char *label = malloc(length + 1);
            if (!label) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating timer stamp label"); return false; }
            size_t at = 0;
            for (size_t i = 1; i < call->argc; ++i) {
                if (i > 1) label[at++] = ' ';
                size_t added = strlen(call->argv[i]); memcpy(label + at, call->argv[i], added); at += added;
            }
            label[at] = 0; bool success = qa_profiler_stamp(tools->profiler, label, error); free(label); return success;
        }
    }
    const char *action = call->argc > 1 ? call->argv[1] : "report";
    if (!strcmp(action, "on") || !strcmp(action, "off")) return qa_profiler_enable(tools->profiler, !strcmp(action, "on"), error);
    if (!strcmp(action, "reset")) return qa_profiler_reset(tools->profiler, error);
    qa_arena scratch = {0}; bool success = false;
    if (!strcmp(action, "report")) {
        const qa_timer_report *rows = NULL; size_t count = 0;
        if (!qa_profiler_report(tools->profiler, &scratch, &rows, &count, error)) goto done;
        tools_print(tools, &call->context, "name\tcalls\ttotal_ms\tself_ms\tmax_ms\n");
        for (size_t i = 0; i < count; ++i) {
            char total[384], self[384], maximum[384], line[128];
            if (!qa_format_fixed(rows[i].total_ms, 3, total, sizeof total, error) ||
                !qa_format_fixed(rows[i].self_ms, 3, self, sizeof self, error) ||
                !qa_format_fixed(rows[i].maximum_ms, 3, maximum, sizeof maximum, error)) goto done;
            tools_print(tools, &call->context, rows[i].name);
            (void)snprintf(line, sizeof line, "\t%" PRIu64 "\t", rows[i].calls); tools_print(tools, &call->context, line);
            tools_print(tools, &call->context, total); tools_print(tools, &call->context, "\t");
            tools_print(tools, &call->context, self); tools_print(tools, &call->context, "\t");
            tools_print(tools, &call->context, maximum); tools_print(tools, &call->context, "\n");
        }
        success = true;
    } else if (!strcmp(action, "stamps")) {
        const qa_timer_stamp *rows = NULL; size_t count = 0;
        if (!qa_profiler_stamps(tools->profiler, &scratch, &rows, &count, error)) goto done;
        tools_print(tools, &call->context, "milliseconds\tname\n");
        for (size_t i = 0; i < count; ++i) {
            char value[384]; if (!qa_format_fixed(rows[i].milliseconds, 3, value, sizeof value, error)) goto done;
            tools_print(tools, &call->context, value); tools_print(tools, &call->context, "\t");
            tools_print(tools, &call->context, rows[i].name); tools_print(tools, &call->context, "\n");
        }
        success = true;
    } else tools_fail(error, "usage: timers [on|off|reset|report|stamps]");
done:
    qa_arena_destroy(&scratch); return success;
}

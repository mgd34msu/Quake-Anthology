#include "tools_internal.h"
#include "save_internal.h"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include "qa/text.h"
#include "qa/id_index.h"
#include "qa/allocation_gate.h"
#include <stdlib.h>
#include <string.h>

typedef struct timer_scope { qa_string_id name; double start, children; } timer_scope;
typedef struct timer_total {
    qa_string_id name;
    uint64_t calls;
    double total_ms, self_ms, maximum_ms;
} timer_total;
typedef struct timer_stamp {
    const char *name;
    double milliseconds;
    qa_arena storage;
} timer_stamp;
struct qa_profiler {
    double (*clock)(void *);
    void *context;
    qa_strings *names;
    qa_id_index total_index;
    qa_arena storage, stamp_input;
    timer_total *totals;
    timer_scope *stack;
    timer_stamp *stamps;
    size_t total_count, total_capacity, stack_count, stack_capacity;
    size_t stamp_count, stamp_capacity, stamp_first;
    size_t disabled_depth;
    double epoch;
    bool enabled, busy, pending_restore, mode_pending, next_enabled;
};
static void mode_commit(qa_profiler *p) {
    if (!p->stack_count && !p->disabled_depth && p->mode_pending) {
        p->enabled = p->next_enabled;
        p->mode_pending = false;
    }
}
static bool clock_read(qa_profiler *p, double *out, qa_error *error) {
    p->busy = true; double value = p->clock(p->context); p->busy = false;
    if (!isfinite(value)) {
        tools_fail(error, "profiler clock must be finite");
        return false;
    }
    *out = value; return true;
}
static void *array_alloc(qa_arena *storage, size_t count, size_t item,
    size_t alignment, qa_error *error) {
    if (!count) return NULL;
    if (count > SIZE_MAX / item) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "profiler collection exceeds storage"); return NULL;
    }
    void *array = qa_arena_alloc(storage, count * item, alignment, error);
    if (array) memset(array, 0, count * item);
    return array;
}
static bool label_admit(qa_profiler *p, const char *name, qa_string_id *out, qa_error *error) {
    return (p->names || qa_strings_create(&p->names, error)) &&
        qa_strings_intern_cstr(p->names, name, out, error);
}
static void clear(qa_profiler *p) {
    for (size_t i = 0; i < p->total_index.count; ++i)
        p->total_index.entries[i].value = SIZE_MAX;
    if (p->stamps) for (size_t i = 0; i < p->stamp_count; ++i) {
        timer_stamp *stamp = p->stamps + (p->stamp_first + i) % p->stamp_capacity;
        qa_arena_reset(&stamp->storage); stamp->name = NULL;
    }
    qa_arena_reset(&p->stamp_input);
    p->total_count = p->stamp_count = p->stamp_first = 0;
}
bool qa_profiler_create(double (*clock)(void *), void *context, size_t capacity, qa_profiler **out, qa_error *error) {
    if (!clock || !out || !capacity || capacity > SIZE_MAX / sizeof(qa_timer_stamp)) return tools_fail(error, "invalid profiler options");
    qa_profiler *p = calloc(1, sizeof *p);
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating profiler"); return false; }
    p->clock = clock; p->context = context; p->stamp_capacity = capacity;
    p->total_index.storage = &p->storage;
    p->stamps = array_alloc(&p->storage, capacity, sizeof(*p->stamps), _Alignof(timer_stamp), error);
    if (!p->stamps || !clock_read(p, &p->epoch, error)) { (void)qa_profiler_destroy(p, NULL); return false; }
    *out = p; return true;
}
bool qa_profiler_prepare(qa_profiler *p, const char *const *names, size_t count,
    size_t depth, qa_string_id *labels, qa_error *error)
{
    if (!p || p->pending_restore || p->busy || p->stack_count ||
        (count && (!names || !labels)))
        return tools_fail(error, "profiler preparation requires returned scopes and labels");
    for (size_t i = 0; i < count; ++i) {
        if (!names[i]) return tools_fail(error, "profiler label is missing");
        if (!label_admit(p, names[i], &labels[i], error)) return false;
    }
    size_t additional = 0;
    for (size_t i = 0; i < count; ++i) {
        if (qa_id_index_find(&p->total_index, labels[i], 0)) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) duplicate |= labels[j] == labels[i];
        if (!duplicate) ++additional;
    }
    if (additional > SIZE_MAX - p->total_index.count)
        return tools_fail(error, "profiler labels exceed native storage");
    size_t totals = p->total_index.count + additional;
    if (totals > SIZE_MAX / sizeof(*p->totals) || depth > SIZE_MAX / sizeof(*p->stack))
        return tools_fail(error, "profiler preparation exceeds native storage");
    bool sealed = p->storage.sealed;
    p->storage.sealed = false;
    if (totals > p->total_capacity) {
        timer_total *rows = qa_arena_grow(&p->storage, p->totals,
            p->total_count * sizeof(*rows), totals * sizeof(*rows), _Alignof(timer_total), error);
        if (!rows) goto failed;
        p->totals = rows; p->total_capacity = totals;
    }
    if (depth > p->stack_capacity) {
        timer_scope *stack = array_alloc(&p->storage, depth, sizeof(*stack),
            _Alignof(timer_scope), error);
        if (!stack) goto failed;
        p->stack = stack; p->stack_capacity = depth;
    }
    if (!qa_id_index_reserve(&p->total_index, additional, error)) goto failed;
    for (size_t i = 0; i < count; ++i)
        if (!qa_id_index_find(&p->total_index, labels[i], 0))
            qa_id_index_insert(&p->total_index, labels[i], 0, SIZE_MAX);
    qa_arena_seal(&p->storage);
    return true;
failed:
    p->storage.sealed = sealed;
    return false;
}
bool qa_profiler_destroy(qa_profiler *p, qa_error *error) {
    if (!p) return true;
    if (p->busy || p->stack_count) return tools_fail(error, "profiler teardown requires scopes and callbacks to return");
    clear(p);
    qa_strings_destroy(p->names);
    if (p->stamps) for (size_t i = 0; i < p->stamp_capacity; ++i) qa_arena_destroy(&p->stamps[i].storage);
    qa_id_index_destroy(&p->total_index);
    qa_arena_destroy(&p->stamp_input); qa_arena_destroy(&p->storage); free(p); return true;
}
bool qa_profiler_enable(qa_profiler *p, bool enabled, qa_error *error) {
    if (!p || p->pending_restore || p->busy) return tools_fail(error, "cannot change pending profiler mode or an active callback");
    p->next_enabled = enabled; p->mode_pending = true;
    mode_commit(p); return true;
}
bool qa_profiler_reset(qa_profiler *p, qa_error *error) {
    if (!p || p->pending_restore || p->busy || p->stack_count) return tools_fail(error, "cannot reset pending profiler or an active scope");
    double epoch; if (!clock_read(p, &epoch, error)) return false;
    clear(p); p->epoch = epoch; return true;
}
bool qa_profiler_push(qa_profiler *p, qa_string_id label, qa_error *error) {
    if (!p || !label || p->pending_restore || p->busy) return tools_fail(error, "invalid or pending profiler scope admission");
    if (!p->enabled) {
        if (p->disabled_depth == SIZE_MAX) return tools_fail(error, "profiler scope depth exceeds native range");
        ++p->disabled_depth;
        return true;
    }
    if (p->stack_count == p->stack_capacity) {
        qa_allocation_gate_capacity_exhausted();
        return tools_fail(error, "profiler admitted scope depth exhausted");
    }
    double now; if (!clock_read(p, &now, error)) return false;
    p->stack[p->stack_count++] = (timer_scope){.name = label, .start = now}; return true;
}
bool qa_profiler_pop(qa_profiler *p, qa_error *error) {
    if (!p || p->pending_restore || p->busy) return tools_fail(error, "invalid or pending profiler scope retirement");
    if (!p->enabled) {
        if (!p->disabled_depth) return tools_fail(error, "unbalanced profiler pop");
        --p->disabled_depth;
        mode_commit(p);
        return true;
    }
    if (!p->stack_count) return tools_fail(error, "unbalanced profiler pop");
    double now; if (!clock_read(p, &now, error)) return false;
    timer_scope scope = p->stack[p->stack_count - 1];
    qa_id_index_entry *entry = qa_id_index_find(&p->total_index, scope.name, 0);
    if (!entry) return tools_fail(error, "profiler scope label was not admitted");
    size_t index = entry->value == SIZE_MAX ? p->total_count : entry->value;
    timer_total empty = {0}, *total = index < p->total_count ? &p->totals[index] : &empty;
    if (total->calls == UINT64_MAX) return tools_fail(error, "profiler call count exceeds native range");
    double elapsed = fmax(0, now - scope.start), self = fmax(0, elapsed - scope.children);
    if (!isfinite(elapsed) || !isfinite(total->total_ms + elapsed) || !isfinite(total->self_ms + self) ||
        (p->stack_count > 1 && !isfinite(p->stack[p->stack_count - 2].children + elapsed))) return tools_fail(error, "profiler timing exceeds native range");
    if (index == p->total_count) {
        if (p->total_count == p->total_capacity) {
            qa_allocation_gate_capacity_exhausted();
            return tools_fail(error, "profiler admitted totals exhausted");
        }
        p->totals[index] = (timer_total){.name = scope.name}; ++p->total_count;
        entry->value = index;
    }
    total = &p->totals[index];
    --p->stack_count;
    if (p->stack_count) p->stack[p->stack_count - 1].children += elapsed;
    ++total->calls; total->total_ms += elapsed; total->self_ms += self; total->maximum_ms = fmax(total->maximum_ms, elapsed);
    mode_commit(p); return true;
}
bool qa_profiler_stamp(qa_profiler *p, const char *name, qa_error *error) {
    if (!p || !name || p->pending_restore || p->busy) return tools_fail(error, "invalid or pending profiler stamp");
    if (!p->enabled) return true;
    qa_arena_reset(&p->stamp_input);
    char *copy = qa_arena_text(&p->stamp_input, name, error); if (!copy) return false;
    size_t index = (p->stamp_first + p->stamp_count) % p->stamp_capacity;
    timer_stamp *stamp = p->stamps + index;
    if (!qa_arena_reserve(&stamp->storage, strlen(copy) + 1, error)) return false;
    double now;
    if (!clock_read(p, &now, error) || !isfinite(now - p->epoch)) return tools_fail(error, "profiler stamp exceeds native clock range");
    qa_arena_reset(&stamp->storage);
    stamp->name = qa_arena_text(&stamp->storage, copy, error);
    if (p->stamp_count == p->stamp_capacity) {
        p->stamp_first = (p->stamp_first + 1) % p->stamp_capacity;
    } else ++p->stamp_count;
    stamp->milliseconds = now - p->epoch; return true;
}
bool qa_profiler_report(const qa_profiler *p, qa_arena *scratch, const qa_timer_report **out, size_t *count, qa_error *error) {
    if (!p || !scratch || !out || !count || p->pending_restore || p->busy) return tools_fail(error, "invalid or pending profiler report");
    qa_timer_report *rows = p->total_count ? qa_arena_alloc(scratch, p->total_count * sizeof(*rows), _Alignof(qa_timer_report), error) : NULL;
    if (p->total_count && !rows) return false;
    for (size_t i = 0; i < p->total_count; ++i) {
        const timer_total *total = p->totals + i;
        rows[i] = (qa_timer_report){.name = qa_arena_text(scratch, qa_strings_cstr(p->names, total->name), error),
            .calls = total->calls, .total_ms = total->total_ms, .self_ms = total->self_ms, .maximum_ms = total->maximum_ms};
        if (!rows[i].name) return false;
    }
    *out = rows; *count = p->total_count; return true;
}
bool qa_profiler_stamps(const qa_profiler *p, qa_arena *scratch, const qa_timer_stamp **out, size_t *count, qa_error *error) {
    if (!p || !scratch || !out || !count || p->pending_restore || p->busy) return tools_fail(error, "invalid or pending profiler stamp report");
    qa_timer_stamp *rows = p->stamp_count ? qa_arena_alloc(scratch, p->stamp_count * sizeof(*rows), _Alignof(qa_timer_stamp), error) : NULL;
    if (p->stamp_count && !rows) return false;
    for (size_t i = 0; i < p->stamp_count; ++i) {
        const timer_stamp *stamp = p->stamps + (p->stamp_first + i) % p->stamp_capacity;
        rows[i] = (qa_timer_stamp){qa_arena_text(scratch, stamp->name, error), stamp->milliseconds};
        if (!rows[i].name) return false;
    }
    *out = rows; *count = p->stamp_count; return true;
}
bool qa_profiler_enabled(const qa_profiler *p) { return p && !p->pending_restore && p->enabled; }
bool qa_profiler_idle(const qa_profiler *p) { return p && !p->busy && !p->stack_count; }
bool tools_profiler_empty(const qa_tools_options *options, qa_profiler **out, qa_error *error) {
    qa_profiler *p = calloc(1, sizeof *p);
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating empty profiler"); return false; }
    p->clock = options->profiler_milliseconds ? options->profiler_milliseconds : options->milliseconds;
    p->context = options->context; p->stamp_capacity = 4096; p->pending_restore = true;
    p->total_index.storage = &p->storage;
    p->stamps = array_alloc(&p->storage, p->stamp_capacity, sizeof *p->stamps, _Alignof(timer_stamp), error);
    if (!p->stamps) { (void)qa_profiler_destroy(p, NULL); return false; }
    *out = p; return true;
}
bool tools_profiler_rebind_ready(const qa_profiler *p, const void *context, qa_error *error) {
    return (qa_profiler_idle(p) && !p->pending_restore && p->context == context) || tools_fail(error, "profiler context rebind requires its restored idle owner");
}
void tools_profiler_rebind(qa_profiler *p, void *context) { p->context = context; }
void tools_profiler_exchange(qa_profiler *stable, qa_profiler *candidate) {
    size_t disabled_depth = stable->disabled_depth;
    qa_profiler old = *stable; *stable = *candidate; *candidate = old;
    stable->total_index.storage = &stable->storage; candidate->total_index.storage = &candidate->storage;
    candidate->disabled_depth = 0; candidate->mode_pending = false;
    if (disabled_depth) {
        stable->disabled_depth = disabled_depth;
        stable->next_enabled = stable->enabled; stable->mode_pending = true;
        stable->enabled = false;
    }
}
static bool label_fields(qa_source_save_io *io, qa_profiler *p, qa_string_id *label) {
    char *name = io->direction == QA_SOURCE_SAVE_WRITE ? (char *)qa_strings_cstr(p->names, *label) : NULL;
    bool ok = tool_save_text(io, &name);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (ok && name) ok = label_admit(p, name, label, io->error);
        bool present = name != NULL; free(name);
        if (!ok) io->failed = true;
        return ok && present;
    }
    return ok && name;
}
bool tools_profiler_fields(qa_source_save_io *io, qa_profiler **holder, const qa_tools_options *options) {
    qa_profiler *p = *holder;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        p = calloc(1, sizeof *p);
        if (!p) { io->failed = true; qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating restored profiler"); return false; }
        *holder = p; p->clock = options->profiler_milliseconds ? options->profiler_milliseconds : options->milliseconds; p->context = options->context;
        p->total_index.storage = &p->storage;
    } else if (!qa_profiler_idle(p) || p->pending_restore) return tool_save_fail(io, "profiler continuation requires restored state and returned scopes");
    if (!qa_source_save_count(io, &p->total_count, SIZE_MAX / sizeof(qa_timer_report)) ||
        !qa_source_save_count(io, &p->total_capacity, SIZE_MAX / sizeof(qa_timer_report)) || p->total_count > p->total_capacity ||
        !qa_source_save_count(io, &p->stack_capacity, SIZE_MAX / sizeof *p->stack) ||
        !qa_source_save_count(io, &p->stamp_capacity, SIZE_MAX / sizeof(qa_timer_stamp)) || !p->stamp_capacity ||
        !qa_source_save_count(io, &p->stamp_count, p->stamp_capacity) ||
        !qa_source_save_count(io, &p->stamp_first, p->stamp_capacity - 1) ||
        !qa_source_save_f64(io, &p->epoch) || !isfinite(p->epoch) || !qa_source_save_bool(io, &p->enabled))
        return tool_save_fail(io, "invalid profiler continuation metadata");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        p->totals = array_alloc(&p->storage, p->total_capacity, sizeof *p->totals, _Alignof(timer_total), io->error);
        p->stack = array_alloc(&p->storage, p->stack_capacity, sizeof *p->stack, _Alignof(timer_scope), io->error);
        p->stamps = array_alloc(&p->storage, p->stamp_capacity, sizeof *p->stamps, _Alignof(timer_stamp), io->error);
        if ((p->total_capacity && !p->totals) || (p->stack_capacity && !p->stack) || !p->stamps)
            return tool_save_fail(io, "allocating profiler continuation collections");
        for (size_t i = 0; i < p->stamp_capacity; ++i) qa_arena_init(&p->stamps[i].storage, sizeof(char));
        if (!qa_id_index_reserve(&p->total_index, p->total_count, io->error)) { io->failed = true; return false; }
    }
    for (size_t i = 0; i < p->total_count; ++i) {
        timer_total *v = &p->totals[i];
        if (!label_fields(io, p, &v->name) || !qa_source_save_u64(io, &v->calls) || !qa_source_save_f64(io, &v->total_ms) ||
            !qa_source_save_f64(io, &v->self_ms) || !qa_source_save_f64(io, &v->maximum_ms) ||
            !isfinite(v->total_ms) || !isfinite(v->self_ms) || !isfinite(v->maximum_ms) ||
            v->total_ms < 0 || v->self_ms < 0 || v->maximum_ms < 0 || v->self_ms > v->total_ms || v->maximum_ms > v->total_ms)
            return tool_save_fail(io, "invalid profiler continuation aggregate");
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (qa_id_index_find(&p->total_index, v->name, 0)) return tool_save_fail(io, "duplicate profiler continuation label");
            qa_id_index_insert(&p->total_index, v->name, 0, i);
        }
    }
    for (size_t i = 0; i < p->stamp_count; ++i) {
        timer_stamp *v = &p->stamps[(p->stamp_first + i) % p->stamp_capacity];
        if (!tool_save_arena_text(io, &v->storage, &v->name) || !v->name ||
            !qa_source_save_f64(io, &v->milliseconds) || !isfinite(v->milliseconds))
            return tool_save_fail(io, "invalid profiler continuation stamp");
    }
    return true;
}

static bool timer_diagnostic(qa_tools *tools, const qa_command_invocation *call, qa_error *error) {
    if (!tools->options.diagnostic) return true;
    qa_buffer text = {0};
    bool ok = tools->options.diagnostic(tools->options.context, call, &text, error);
    if (ok && text.size && (!text.data || memchr(text.data, 0, text.size)))
        ok = tools_fail(error, "timer diagnostic returned invalid console text");
    if (ok && text.size) tools_print(tools, &call->context, (const char *)text.data);
    qa_buffer_free(&text); return ok;
}

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
            qa_arena storage = {0};
            char *label = qa_arena_alloc(&storage, length + 1, 1, error);
            bool success = label && qa_text_join_words(call->argv + 1, call->argc - 1, label, length + 1, error) &&
                qa_profiler_stamp(tools->profiler, label, error);
            qa_arena_destroy(&storage); return success;
        }
    }
    const char *action = call->argc > 1 ? call->argv[1] : "report";
    if (!strcmp(action, "on") || !strcmp(action, "off"))
        return qa_profiler_enable(tools->profiler, !strcmp(action, "on"), error) && timer_diagnostic(tools, call, error);
    if (!strcmp(action, "reset"))
        return qa_profiler_reset(tools->profiler, error) && timer_diagnostic(tools, call, error);
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
        success = timer_diagnostic(tools, call, error);
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

#include "qa/material.h"

#include <stdlib.h>
#include <string.h>

static void exchange(qa_scene_group **entries, ptrdiff_t a, ptrdiff_t b)
{
    qa_scene_group *value = entries[a]; entries[a] = entries[b]; entries[b] = value;
}

static bool source_sort(qa_scene_group **entries, size_t count, qa_error *error)
{
    if (count < 2) return true;
    /* The byte-sized partition comparison below is intentionally the source
     * comparison, including its minus-one-byte rather than minus-one-item. */
    if (count > (size_t)PTRDIFF_MAX / 8) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "too many source draw groups");
        return false;
    }
    struct { ptrdiff_t lo, hi; } stack[30];
    size_t depth = 0;
    ptrdiff_t lo = 0, hi = (ptrdiff_t)count-1;
    for (;;) {
        ptrdiff_t size = hi-lo+1;
        if (size <= 8) {
            while (hi > lo) {
                ptrdiff_t maximum = lo;
                for (ptrdiff_t p = lo+1; p <= hi; ++p)
                    if (entries[p]->source_sort > entries[maximum]->source_sort) maximum = p;
                exchange(entries, maximum, hi--);
            }
        } else {
            exchange(entries, lo + size/2, lo);
            ptrdiff_t lower = lo, upper = hi+1;
            for (;;) {
                do { ++lower; } while (lower <= hi && entries[lower]->source_sort <= entries[lo]->source_sort);
                do { --upper; } while (upper > lo && entries[upper]->source_sort >= entries[lo]->source_sort);
                if (upper < lower) break;
                exchange(entries, lower, upper);
            }
            exchange(entries, lo, upper);
            ptrdiff_t save_lo = 0, save_hi = -1, next_lo = 0, next_hi = -1;
            if ((upper-lo)*8-1 >= (hi-lower)*8) {
                if (lo+1 < upper) { save_lo = lo; save_hi = upper-1; }
                if (lower < hi) { next_lo = lower; next_hi = hi; }
            } else {
                if (lower < hi) { save_lo = lower; save_hi = hi; }
                if (lo+1 < upper) { next_lo = lo; next_hi = upper-1; }
            }
            if (save_hi >= save_lo) {
                if (depth == 30) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source draw sort exceeds its work stack");
                    return false;
                }
                stack[depth].lo = save_lo; stack[depth].hi = save_hi; ++depth;
            }
            if (next_hi >= next_lo) { lo = next_lo; hi = next_hi; continue; }
        }
        if (depth == 0) return true;
        --depth; lo = stack[depth].lo; hi = stack[depth].hi;
    }
}

static int generic_compare(const void *left, const void *right)
{
    const qa_scene_group *a = *(qa_scene_group *const *)left;
    const qa_scene_group *b = *(qa_scene_group *const *)right;
    if (a->priority < b->priority) return -1;
    if (a->priority > b->priority) return 1;
    return a->ordinal < b->ordinal ? -1 : a->ordinal > b->ordinal ? 1 : 0;
}

static bool precedes(const qa_scene_group *a, const qa_scene_group *b)
{
    return b == NULL || (a != NULL && (a->priority < b->priority ||
        (a->priority == b->priority && a->ordinal < b->ordinal)));
}

static bool finish_range(qa_scene_frame *frame, size_t start, size_t end,
                          qa_scene_group **scratch, qa_scene_command *commands, qa_error *error)
{
    size_t count = end-start, native_count = 0, generic_count = 0;
    qa_scene_group **native = scratch, **generic = scratch+count, **merged = scratch+count*2;
    for (size_t i = start; i < end; ++i) {
        qa_scene_group *group = &frame->groups[i];
        if (group->kind != QA_SCENE_GROUP_SEQUENCE && group->material != NULL) {
            const qa_material *ordering = group->material;
            if (group->kind == QA_SCENE_GROUP_COMPILED) {
                for (size_t hop = 0; ordering->remapped != NULL; ++hop) {
                    if (hop >= 16384) {
                        qa_error_set(error, QA_ERROR_FORMAT, 0, "scene group material remap is cyclic");
                        return false;
                    }
                    ordering = ordering->remapped;
                }
            }
            group->priority = ordering->sort;
        }
        if (group->kind == QA_SCENE_GROUP_SOURCE) {
            uint32_t rank;
            if (!qa_material_order_rank(frame->material_order, group->material, &rank, error)) return false;
            if (rank >= 16384) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source shader rank exceeds draw sort field");
                return false;
            }
            group->source_sort = (rank << 17) | (group->entity << 7) | (group->fog << 2) | group->dlight;
            native[native_count++] = group;
        } else if (group->kind == QA_SCENE_GROUP_COMPILED) generic[generic_count++] = group;
    }
    if (!source_sort(native, native_count, error)) return false;
    if (generic_count > 1) qsort(generic, generic_count, sizeof(*generic), generic_compare);
    size_t n = 0, g = 0, merged_count = 0;
    while (n < native_count || g < generic_count) {
        qa_scene_group *a = n < native_count ? native[n] : NULL;
        qa_scene_group *b = g < generic_count ? generic[g] : NULL;
        if (a != NULL && precedes(a, b)) { merged[merged_count++] = a; ++n; }
        else { merged[merged_count++] = b; ++g; }
    }
    size_t m = 0, s = start, offset = frame->groups[start].first;
    while (s < end && frame->groups[s].kind != QA_SCENE_GROUP_SEQUENCE) ++s;
    while (m < merged_count || s < end) {
        qa_scene_group *a = m < merged_count ? merged[m] : NULL;
        qa_scene_group *b = s < end ? &frame->groups[s] : NULL;
        const qa_scene_group *next;
        if (a != NULL && precedes(a, b)) { next = a; ++m; }
        else {
            next = b;
            do { ++s; } while (s < end && frame->groups[s].kind != QA_SCENE_GROUP_SEQUENCE);
        }
        if (next->count != 0)
            memcpy(commands+offset, frame->commands+next->first, next->count*sizeof(*commands));
        offset += next->count;
    }
    return true;
}

bool qa_scene_frame_finish(qa_scene_frame *frame, const qa_scene_view *view,
                           const qa_scene_fog *fog, qa_error *error)
{
    if (frame == NULL || (fog != NULL && fog->kind == QA_FOG_Q2 && view == NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene finish request");
        return false;
    }
    if (frame->group_count != 0) {
        bool source = false;
        for (size_t i = 0; i < frame->group_count; ++i)
            if (frame->groups[i].kind == QA_SCENE_GROUP_SOURCE) { source = true; break; }
        if (source && !qa_material_order_prepare(frame->material_order, error)) return false;
        if (frame->group_count > (size_t)PTRDIFF_MAX / 3 / sizeof(qa_scene_group *) ||
            frame->command_count > (size_t)PTRDIFF_MAX / sizeof(qa_scene_command)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "scene sorting storage overflow"); return false;
        }
        size_t group_capacity = frame->group_count*3;
        size_t command_capacity = frame->command_count != 0 ? frame->command_count : 1;
        if (group_capacity > frame->sort_group_capacity) {
            qa_scene_group **grown = realloc(frame->sort_groups, group_capacity*sizeof(*grown));
            if (grown == NULL) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene sort groups"); return false;
            }
            frame->sort_groups = grown; frame->sort_group_capacity = group_capacity;
        }
        if (command_capacity > frame->sort_command_capacity) {
            qa_scene_command *grown = realloc(frame->sort_commands, command_capacity*sizeof(*grown));
            if (grown == NULL) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate scene sort commands"); return false;
            }
            frame->sort_commands = grown; frame->sort_command_capacity = command_capacity;
        }
        qa_scene_group **scratch = frame->sort_groups;
        qa_scene_command *commands = frame->sort_commands;
        if (frame->command_count != 0)
            memcpy(commands, frame->commands, frame->command_count*sizeof(*commands));
        bool ok = true;
        for (size_t first = 0; first < frame->group_count;) {
            size_t end = first+1;
            while (end < frame->group_count && frame->groups[end].first ==
                frame->groups[end-1].first+frame->groups[end-1].count) ++end;
            if (!finish_range(frame, first, end, scratch, commands, error)) { ok = false; break; }
            first = end;
        }
        if (ok) {
            if (frame->command_count != 0)
                memcpy(frame->commands, commands, frame->command_count*sizeof(*commands));
            frame->group_count = 0;
        }
        if (!ok) return false;
    }
    if (fog != NULL && fog->kind == QA_FOG_Q2) {
        qa_scene_command command = {.kind = QA_SCENE_COMMAND_FOG, .data.fog = {*fog, *view}};
        return qa_scene_frame_emit(frame, &command, error);
    }
    return true;
}

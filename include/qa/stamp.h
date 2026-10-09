#ifndef QA_STAMP_H
#define QA_STAMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct qa_stamp_set {
    uint32_t *marks;
    size_t count;
    uint32_t epoch;
} qa_stamp_set;

/* Storage belongs to the caller and is sized before queries begin. */
static inline void qa_stamp_set_init(qa_stamp_set *set, uint32_t *marks, size_t count)
{
    *set = (qa_stamp_set){marks, count, 1};
    if (count) memset(marks, 0, count * sizeof(*marks));
}

static inline void qa_stamp_set_begin(qa_stamp_set *set)
{
    if (++set->epoch == 0) {
        if (set->count) memset(set->marks, 0, set->count * sizeof(*set->marks));
        set->epoch = 1;
    }
}

static inline bool qa_stamp_set_test(const qa_stamp_set *set, size_t index)
{
    return set->marks[index] == set->epoch;
}

static inline bool qa_stamp_set_mark(qa_stamp_set *set, size_t index)
{
    if (qa_stamp_set_test(set, index)) return false;
    set->marks[index] = set->epoch;
    return true;
}

static inline uint32_t qa_stamp_set_value(const qa_stamp_set *set, size_t index)
{
    return set->marks[index];
}

static inline void qa_stamp_set_restore_mark(qa_stamp_set *set, size_t index, uint32_t value)
{
    set->marks[index] = value;
}

static inline uint32_t qa_stamp_set_snapshot(const qa_stamp_set *set, uint32_t *marks)
{
    if (set->count) memcpy(marks, set->marks, set->count * sizeof(*marks));
    return set->epoch;
}

/* NULL marks restore only the epoch after sparse rollback. */
static inline void qa_stamp_set_restore(qa_stamp_set *set, const uint32_t *marks, uint32_t epoch)
{
    if (marks && set->count) memcpy(set->marks, marks, set->count * sizeof(*marks));
    set->epoch = epoch;
}

#endif

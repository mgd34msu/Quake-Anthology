#ifndef QA_Q2_CONFIG_CHANGES_H
#define QA_Q2_CONFIG_CHANGES_H
#include "qa/strings.h"

typedef struct qa_q2_config_change {
    uint64_t revision;
    uint32_t older, newer;
} qa_q2_config_change;
typedef struct qa_q2_config_changes {
    qa_q2_config_change *slots;
    uint32_t latest;
    uint64_t revision;
} qa_q2_config_changes;

/* Links are slot + 1; zero is the end. Each slot appears once, in edit order. */
static inline void qa_q2_config_changed(qa_q2_config_changes *changes, uint32_t index)
{
    qa_q2_config_change *slot = changes->slots + index;
    if (slot->revision) {
        if (slot->older) changes->slots[slot->older - 1].newer = slot->newer;
        if (slot->newer) changes->slots[slot->newer - 1].older = slot->older;
        else changes->latest = slot->older;
    }
    slot->revision = ++changes->revision;
    slot->newer = 0;
    slot->older = changes->latest;
    if (changes->latest) changes->slots[changes->latest - 1].newer = index + 1;
    changes->latest = index + 1;
}
static inline uint32_t qa_q2_config_changes_begin(const qa_q2_config_changes *changes, uint64_t since)
{
    uint32_t first = 0;
    for (uint32_t link = changes->latest; link && changes->slots[link - 1].revision > since;
        link = changes->slots[link - 1].older) first = link;
    return first;
}
#endif

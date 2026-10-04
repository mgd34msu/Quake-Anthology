#ifndef QA_HAPTICS_PRIVATE_H
#define QA_HAPTICS_PRIVATE_H
#include "qa/input.h"
#include "qa/vfs.h"
struct qa_haptic_pattern {
    size_t references, count;
    uint16_t rate;
    bool loop;
    uint32_t start, end, interval;
    uint8_t samples[];
};
struct haptic_entry {
    qa_haptic_pattern *pattern;
    qa_resource *source;
    struct haptic_entry *next;
};
struct qa_haptic_cache {
    struct haptic_entry *entries;
};
#endif

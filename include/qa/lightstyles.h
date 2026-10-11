#ifndef QA_LIGHTSTYLES_H
#define QA_LIGHTSTYLES_H

#include "qa/strings.h"

typedef struct qa_lightstyles {
    qa_string_id patterns[256];
    uint32_t count;
    uint64_t revision;
} qa_lightstyles;

/* The caller validates the style at the game or protocol boundary. */
static inline void qa_lightstyles_set(qa_lightstyles *styles, uint32_t style, qa_string_id pattern)
{
    if (styles->patterns[style] == pattern) return;
    styles->patterns[style] = pattern;
    ++styles->revision;
}

#endif

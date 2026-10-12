#ifndef QA_LIGHTSTYLES_H
#define QA_LIGHTSTYLES_H

#include "qa/strings.h"

typedef struct qa_lightstyles {
    qa_string_id patterns[256];
    uint32_t count;
    uint64_t revision;
    uint64_t sequences[256];
    uint64_t ordered[4];
} qa_lightstyles;

/* The caller validates the style at the game or protocol boundary. */
static inline void qa_lightstyles_set(qa_lightstyles *styles, uint32_t style, qa_string_id pattern)
{
    if (styles->patterns[style] == pattern) return;
    styles->patterns[style] = pattern;
    ++styles->revision;
}

/* An admitted empty pattern is distinct from a style with no event. */
static inline bool qa_lightstyles_ordered_present(const qa_lightstyles *styles, uint32_t style)
{
    return (styles->ordered[style / 64] & (UINT64_C(1) << (style % 64))) != 0;
}

static inline void qa_lightstyles_set_ordered(qa_lightstyles *styles, uint32_t style,
    qa_string_id pattern, uint64_t sequence)
{
    bool changed = !qa_lightstyles_ordered_present(styles, style) || styles->sequences[style] != sequence;
    uint64_t revision = styles->revision;
    qa_lightstyles_set(styles, style, pattern);
    styles->sequences[style] = sequence;
    styles->ordered[style / 64] |= UINT64_C(1) << (style % 64);
    if (changed && styles->revision == revision) ++styles->revision;
}

/* Providers are visited in publication order; the later provider wins a tie. */
static inline void qa_lightstyles_merge_ordered(qa_lightstyles *styles, const qa_lightstyles *source)
{
    for (uint32_t style = 0; style < source->count; ++style)
        if (qa_lightstyles_ordered_present(source, style) &&
            (!qa_lightstyles_ordered_present(styles, style) || source->sequences[style] >= styles->sequences[style]))
            qa_lightstyles_set_ordered(styles, style, source->patterns[style], source->sequences[style]);
}

#endif

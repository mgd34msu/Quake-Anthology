#ifndef QA_Q1_TEXT_H
#define QA_Q1_TEXT_H
#include "qa/common.h"
struct qa_arena;
/* Classic source fallback after the actual source catalog misses its key.
 * Arguments contain the genuine event's string and number values. The
 * complete first pass uses caller scratch; the output does not retain it. */
bool qa_q1_classic_text(const char *, const char *const *, size_t, struct qa_arena *,
                        char *, size_t, qa_error *);
#endif

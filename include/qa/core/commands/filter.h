/*
 * Port of src/core/commands/filter.ts (sourceFilter): id Software's
 * common.c Com_Filter. Source matching accepts a prefix and searches
 * star runs without backtracking. C accommodation: the TS calls
 * sourceCommandText (from commands/text.ts, not yet ported) to cut at
 * an embedded NUL and reject charCodes above 255; both are inherent
 * to NUL-terminated C byte strings. Reads the TS guards with its
 * RangeError byte() helper abort identically here.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMANDS_FILTER_H
#define QA_CORE_COMMANDS_FILTER_H

#include <stdbool.h>

bool qa_source_filter(const char *filter_text, const char *name_text,
                      bool case_sensitive);

#endif

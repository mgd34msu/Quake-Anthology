/* Port of src/content/q1/base/messages.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MESSAGES_H
#define QA_CONTENT_Q1_BASE_MESSAGES_H

#include <stddef.h>

/* TS classicMonsterObituaries (ReadonlyMap). NULL when absent. */
const char *qa_q1_classic_monster_obituary(const char *classname);
/* TS classicObituaryText; writes into out (truncates like snprintf). */
char *qa_q1_classic_obituary_text(const char *key, const char *const *args, size_t arg_count,
                                  char *out, size_t out_size);

#endif

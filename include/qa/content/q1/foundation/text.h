/* Port of src/content/q1/foundation/text.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_TEXT_H
#define QA_CONTENT_Q1_FOUNDATION_TEXT_H

#include <stddef.h>

#include "qa/content/q1/foundation/types.h"

typedef struct qa_q1_message_entry { const char *key, *value; } qa_q1_message_entry;
extern const qa_q1_message_entry QA_Q1_CLASSIC_MESSAGES[];
extern const size_t QA_Q1_CLASSIC_MESSAGE_COUNT;
const char *qa_q1_classic_message(const char *key); /* NULL absent */
/* TS classicQ1Text/q1EntityString; write into out (snprintf-style). */
char *qa_q1_classic_text(const char *text, const qa_q1_msg_arg *args, size_t arg_count, char *out, size_t out_size);
char *qa_q1_entity_string(const char *value, char *out, size_t out_size);

#endif

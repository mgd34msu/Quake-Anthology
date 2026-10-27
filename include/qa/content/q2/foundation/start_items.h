/* Port of src/content/q2/foundation/start-items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_START_ITEMS_H
#define QA_CONTENT_Q2_FOUNDATION_START_ITEMS_H
#include <stddef.h>
typedef struct qa_q2_start_item { char *classname; double count; } qa_q2_start_item;
/* Malloc'd; free with qa_q2_start_items_free. Aborts on TS Error. */
qa_q2_start_item *qa_q2_parse_start_items(const char *expression, size_t *out_count);
void qa_q2_start_items_free(qa_q2_start_item *items, size_t count);
#endif

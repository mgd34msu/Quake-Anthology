/*
 * Ownership of a decoded checkpoint (C accommodation; TS has GC). A
 * decoded content checkpoint borrows its strings from the parsed value
 * tree and its arrays from blocks the reader allocated. Both live in this
 * owner until qa_persist_decoded_free. Restore functions copy what they
 * keep, so the owner can be freed once restore returns.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_DECODED_H
#define QA_PERSISTENCE_DECODED_H

#include <stddef.h>

#include "qa/persistence/value.h"

typedef struct qa_persist_decoded {
  qa_save_value *root; /* NULL when the reader ran over a caller's tree */
  void **blocks;
  size_t block_count, block_capacity;
} qa_persist_decoded;

/* Zeroed array of count elements (at least one byte), tracked by owner.
 * Aborts on allocation failure, as the other save readers do. */
void *qa_persist_decoded_array(qa_persist_decoded *owner, size_t count, size_t size);
/* Releases the tree and every block; the owner is left empty and reusable. */
void qa_persist_decoded_free(qa_persist_decoded *owner);

#endif

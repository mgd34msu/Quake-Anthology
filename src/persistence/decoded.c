/* Decoded checkpoint ownership (see include/qa/persistence/decoded.h). GPL-2.0-or-later. */
#include "qa/persistence/decoded.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

void *qa_persist_decoded_array(qa_persist_decoded *owner, size_t count, size_t size) {
  if (owner->block_count == owner->block_capacity) {
    size_t capacity = owner->block_capacity != 0 ? owner->block_capacity * 2 : 16;
    void **blocks = realloc(owner->blocks, capacity * sizeof *blocks);
    if (blocks == NULL) qa_range_error("out of memory");
    owner->blocks = blocks;
    owner->block_capacity = capacity;
  }
  void *block = calloc(count != 0 ? count : 1, size != 0 ? size : 1);
  if (block == NULL) qa_range_error("out of memory");
  owner->blocks[owner->block_count++] = block;
  return block;
}

void qa_persist_decoded_free(qa_persist_decoded *owner) {
  for (size_t i = 0; i < owner->block_count; i++) free(owner->blocks[i]);
  free(owner->blocks);
  qa_save_value_free(owner->root);
  owner->root = NULL;
  owner->blocks = NULL;
  owner->block_count = owner->block_capacity = 0;
}

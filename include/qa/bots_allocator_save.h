#ifndef QA_BOTS_ALLOCATOR_SAVE_H
#define QA_BOTS_ALLOCATOR_SAVE_H
#include "qa/bots_allocator.h"

/* Ordered live allocations, including unpublished/orphan blocks. Import into
 * the same fresh owner address uses isolated decoding and no source logging. */
bool qa_bot_memory_capture(const qa_bot_memory *,qa_buffer *,qa_error *);
bool qa_bot_memory_restore(qa_bot_memory *,qa_bytes,qa_error *);
/* Reference zero is the first allocation. References belong to the same idle
 * capture/import cut and do not retain their allocation beyond that cut. */
bool qa_bot_memory_reference(const qa_bot_memory *,qa_bot_memory_allocation,size_t *,qa_error *);
bool qa_bot_memory_resolve(const qa_bot_memory *,size_t,qa_bot_memory_allocation *,qa_error *);
#endif

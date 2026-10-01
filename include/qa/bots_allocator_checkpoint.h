#ifndef QA_BOTS_ALLOCATOR_CHECKPOINT_H
#define QA_BOTS_ALLOCATOR_CHECKPOINT_H
#include "qa/bots_allocator.h"

typedef struct qa_bot_memory_checkpoint qa_bot_memory_checkpoint;
typedef struct qa_bot_memory_prepared qa_bot_memory_prepared;
/* Same-owner checkpoints retain every ordered live allocation, including
 * blocks without published aliases. Surviving blocks keep their backing and
 * handles; recreated blocks receive fresh generations. Callers remap their
 * captured allocation aliases through the prepared plan before committing.
 * Prepare holds the memory mutation guard until finish; finish allocates
 * nothing and emits no source print/log calls. */
bool qa_bot_memory_checkpoint_capture(qa_bot_memory *,qa_bot_memory_checkpoint **,qa_error *);
void qa_bot_memory_checkpoint_destroy(qa_bot_memory_checkpoint *);
bool qa_bot_memory_checkpoint_prepare(qa_bot_memory *,const qa_bot_memory_checkpoint *,
    qa_bot_memory_prepared **,qa_error *);
bool qa_bot_memory_checkpoint_resolve(const qa_bot_memory_prepared *,qa_bot_memory_allocation,
    qa_bot_memory_allocation *,qa_error *);
void qa_bot_memory_checkpoint_finish(qa_bot_memory_prepared *,bool commit);
#endif

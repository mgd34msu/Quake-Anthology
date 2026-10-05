#ifndef QA_SCRIPT_DEFINES_SAVE_H
#define QA_SCRIPT_DEFINES_SAVE_H
#include "qa/script.h"

/* Pure continuation of the actual retained global macro owner. Restore makes
 * a fresh owner without lexing, defining, opening resources or callbacks. */
bool qa_script_defines_save_capture(const qa_script_defines *,qa_buffer *,qa_error *);
bool qa_script_defines_save_restore(qa_bytes,qa_script_defines **,qa_error *);
/* Import raw header/token aliases from already restored MEMORY, preserving
 * this retained owner's identity. Source definitions own independent copies. */
bool qa_script_defines_save_restore_into(qa_script_defines *,qa_bytes,qa_error *);
typedef struct qa_script_defines_prepared qa_script_defines_prepared;
/* The resolver qualifies captured bytes and returns their future backing and
 * handle from an actual prepared MEMORY plan. Finish emits no source calls and
 * allocates nothing; commit follows the corresponding MEMORY publication. */
typedef bool (*qa_script_defines_alias)(void *,size_t,qa_bytes,
    qa_script_memory_allocation *,qa_script_memory_span *,qa_error *);
bool qa_script_defines_save_prepare(qa_script_defines *,qa_bytes,void *,
    qa_script_defines_alias,qa_script_defines_prepared **,qa_error *);
void qa_script_defines_save_finish(qa_script_defines_prepared *,bool);
/* Persistent mutable definitions rebuild real allocations from typed tokens. */
bool qa_script_defines_state_capture(const qa_script_defines *,qa_buffer *,qa_error *);
bool qa_script_defines_state_restore_into(qa_script_defines *,qa_bytes,qa_error *);
#endif

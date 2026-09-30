#ifndef QA_LLM_SAVE_H
#define QA_LLM_SAVE_H
#include "qa/llm.h"
#include "qa/source_save.h"

typedef struct qa_llm_checkpoint_refs {
    void *context;
    bool (*services_encode)(void *, const qa_llm_options *, uint64_t *, qa_error *);
    bool (*services_decode)(void *, uint64_t, qa_llm_options *, qa_error *);
    bool (*console_encode)(void *, const qa_console *, uint64_t *, qa_error *);
    bool (*console_decode)(void *, uint64_t, qa_console **, qa_error *);
    bool (*command_context)(void *, const qa_command_context *, qa_command_context *, qa_error *);
    /* Only non-console observers use these. A descriptor qualifies the real
     * callbacks and their lifetime owner, never a saved function address. */
    bool (*observer_encode)(void *, const qa_llm_observer *, uint64_t *, qa_error *);
    bool (*observer_decode)(void *, uint64_t, qa_llm_observer *, qa_error *);
} qa_llm_checkpoint_refs;
/* No settings reads, browser launch, entropy, HTTP request or clock read.
 * Ordinary use rejects until full private continuation restore succeeds. */
bool qa_llm_create_empty(const qa_llm_options *, qa_llm **, qa_error *);
/* Safe points reject live OAuth socket/refresh owners and active HTTP jobs.
 * Unsent jobs and terminal results continue without replaying a request. */
bool qa_llm_checkpoint_ready(const qa_llm *, qa_error *);
bool qa_llm_checkpoint(const qa_llm *, qa_session *, const qa_llm_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_llm_restore(qa_llm *, qa_session *, const qa_llm_checkpoint_refs *, qa_bytes, qa_error *);
bool qa_llm_rebind_ready(const qa_llm *, const void *old_context, const void *new_context, qa_error *);
void qa_llm_rebind_context(qa_llm *, void *new_context);
#endif

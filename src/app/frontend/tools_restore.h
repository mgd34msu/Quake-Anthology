#ifndef QA_FRONTEND_TOOLS_RESTORE_H
#define QA_FRONTEND_TOOLS_RESTORE_H
#include "internal.h"
#include "qa/tools_save.h"
#include "qa/llm_save.h"
/* Complete wrapper payload includes the actual graph identities and nested
 * HTTP/tools/LLM continuations. Prepare installs genuine empty heaps; attach
 * source consoles before COMMANDS import, then import private fields later. */
bool frontend_tools_checkpoint(qa_frontend *, const qa_tools_checkpoint_refs *,
    const qa_llm_checkpoint_refs *, qa_buffer *, qa_error *);
bool frontend_tools_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_tools_attach_restored(qa_frontend *, qa_error *);
bool frontend_tools_restore(qa_frontend *, const qa_tools_checkpoint_refs *,
    const qa_llm_checkpoint_refs *, qa_bytes, qa_error *);
bool frontend_tools_service_options(const qa_frontend *, qa_tools_options *, qa_llm_options *);
/* Pure complete bindings for the genuine single tools/LLM wrapper, actual
 * physical application console roster and admitted deferred command contexts.
 * This frontend installs no non-console LLM observers. */
bool frontend_tools_checkpoint_resolvers(qa_frontend *, qa_tools_checkpoint_refs *,
    qa_llm_checkpoint_refs *, qa_error *);
bool frontend_tools_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_tools_rebind(qa_frontend *, qa_frontend *);
#endif

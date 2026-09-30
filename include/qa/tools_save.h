#ifndef QA_TOOLS_SAVE_H
#define QA_TOOLS_SAVE_H
#include "qa/tools.h"
#include "qa/source_save.h"

typedef struct qa_tools_checkpoint_refs {
    void *context;
    /* Qualify the actual file, clock, output and source-service owners. Decode
     * returns installed candidate callbacks and borrowed graph-backed views. */
    bool (*services_encode)(void *, const qa_tools_options *, uint64_t *, qa_error *);
    bool (*services_decode)(void *, uint64_t, qa_tools_options *, qa_error *);
    bool (*console_encode)(void *, const qa_console *, uint64_t *, qa_error *);
    bool (*console_decode)(void *, uint64_t, qa_console **, qa_error *);
    bool (*command_context)(void *, const qa_command_context *, qa_command_context *, qa_error *);
} qa_tools_checkpoint_refs;
/* Empty restoration owners install callbacks without observing either clock.
 * Attach the actual candidate consoles before private continuation restore.
 * Commands and nested profiler/debug use reject until full restore succeeds. */
bool qa_tools_create_empty(const qa_tools_options *, qa_tools **, qa_error *);
bool qa_tools_checkpoint_ready(const qa_tools *, qa_error *);
bool qa_tools_checkpoint(const qa_tools *, qa_session *, const qa_tools_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_tools_restore(qa_tools *, qa_session *, const qa_tools_checkpoint_refs *, qa_bytes, qa_error *);
bool qa_tools_rebind_ready(const qa_tools *, const void *old_context, const void *new_context, qa_error *);
void qa_tools_rebind_context(qa_tools *, void *new_context);
#endif

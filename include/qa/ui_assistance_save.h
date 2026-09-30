#ifndef QA_UI_ASSISTANCE_SAVE_H
#define QA_UI_ASSISTANCE_SAVE_H
#include "qa/ui.h"
typedef struct qa_ui_assistance_checkpoint_refs {
    void *context;
    bool (*service_encode)(void *, const qa_llm *, uint64_t *, qa_error *);
    bool (*service_decode)(void *, uint64_t, qa_llm **borrowed, qa_error *);
} qa_ui_assistance_checkpoint_refs;
const qa_llm *qa_ui_llm_service(const qa_ui_llm *);
/* Restore preserves the registered menu and installed service addresses.
 * The enclosing owner imports the actual service separately. These private
 * drafts never execute sign-in, discovery, key writes or menu lifecycle hooks. */
bool qa_ui_llm_checkpoint(const qa_ui_llm *, const qa_ui_assistance_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_ui_llm_restore(qa_ui_llm *, const qa_ui_assistance_checkpoint_refs *, qa_bytes, qa_error *);
#endif

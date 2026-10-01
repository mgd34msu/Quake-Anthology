#ifndef QA_UI_SAVE_H
#define QA_UI_SAVE_H
#include "qa/ui.h"
#include "qa/source_save.h"
typedef struct qa_ui_checkpoint_refs {
    void *context;
    /* The input owner qualifies this actual controller's installed handler. */
    bool (*input_encode)(void *, const qa_ui *, qa_input_ui_token, uint64_t *, qa_error *);
    /* Read-only: returns an already restored handler token and verifies its
     * actual seat, handler function and context against input_binding_read and
     * qa_input_seat_ui_binding_read. No UI push/remove callbacks run. */
    bool (*input_decode)(void *, qa_ui *, uint64_t, qa_input_ui_token *, qa_error *);
    /* Pure factory qualification of the optional physical clock and its
     * actual borrowed context, including a controller with no open menu. */
    bool (*input_clock_ready)(void *,const qa_ui *,double (*)(void *),void *,qa_error *);
} qa_ui_checkpoint_refs;
typedef struct qa_ui_input_binding {
    qa_input_seat *seat;
    qa_input_ui_handler handler;
    void *context;
    qa_input_ui_token token;
} qa_ui_input_binding;
bool qa_ui_input_binding_read(const qa_ui *, qa_ui_input_binding *);
/* Preserves controller state with the actual existing menu registrations and
 * borrowed font/image/service owners. Menu adapter drafts and input routing
 * have separate producers. No factory/lifecycle/action/sound callback runs. */
bool qa_ui_checkpoint(const qa_ui *, const qa_ui_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_ui_restore(qa_ui *, const qa_ui_checkpoint_refs *, qa_bytes, qa_error *);
#endif

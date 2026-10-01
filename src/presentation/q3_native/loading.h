#ifndef QA_Q3_NATIVE_LOADING_H
#define QA_Q3_NATIVE_LOADING_H

#include "frame.h"
#include "qa/application_native_q3_client.h"

typedef struct q3n_loading q3n_loading;
typedef struct q3n_loading_options {
    qa_application *application;
    qa_native_q3_client_service *client;
    qa_native_q3_wire_reader *reader;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    q3n_media *media;
    qa_ui *ui;
    uint32_t seat, presentation_seat;
    void *context;
    /* The real frontend begins/binds its loading frame, calls DrawInformation
     * below, and executes/presents that frame before returning. No CGAME frame
     * reentry or fabricated local playerstate is involved. */
    bool (*update_screen)(void *, q3n_loading *, const q3n_frame *, qa_error *);
} q3n_loading_options;

bool q3n_loading_create(const q3n_loading_options *, q3n_loading **, qa_error *);
/* Pure installed-basis constructor; does not register assets or paint. */
bool q3n_loading_create_restored(const q3n_loading_options *, q3n_loading **, qa_error *);
bool q3n_loading_idle(const q3n_loading *);
void q3n_loading_destroy(q3n_loading *);
bool q3n_loading_string(q3n_loading *, const q3n_frame *, const char *, qa_error *);
bool q3n_loading_item(q3n_loading *, const q3n_frame *, uint32_t, qa_error *);
bool q3n_loading_client(q3n_loading *, const q3n_frame *, uint32_t, qa_error *);
/* Actual constructor/awaiting-snapshot cut; has_local_player may be false.
 * Also admitted from the owner's real update_screen callback. */
bool q3n_loading_draw_information(q3n_loading *, const q3n_frame *, qa_error *);
bool q3n_loading_checkpoint(const q3n_loading *, qa_buffer *, qa_error *);
bool q3n_loading_restore(q3n_loading *, qa_bytes, qa_error *);

#endif

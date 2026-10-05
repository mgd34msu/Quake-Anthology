#ifndef QA_FRONTEND_PERSISTENCE_H
#define QA_FRONTEND_PERSISTENCE_H
#include "qa/frontend.h"
#include "qa/persistence_application.h"
#include "qa/input_platform_save.h"
#include "qa/audio_device_save.h"
#include "qa/display_save.h"
#include "qa/render_gl_save.h"

typedef struct frontend_persistence_native {
    qa_input_platform_restore_guard *input;
    qa_audio_device_restore_guard *device;
    qa_display_restore_guard *display;
    qa_gl_restore_guard *gl;
} frontend_persistence_native;

/* Backend/file resolvers remain borrowed for the operation. Saves contain
 * application state; the frontend rebuilds through its normal constructors.
 * A failed candidate whose children reject retirement transfers to retained;
 * its frontend context must remain alive until ordinary destroy succeeds. */
bool frontend_persistence_capture(qa_frontend *, const qa_application_persistence_ops *,
    qa_save_purpose, qa_save_image **, qa_error *);
/* Actual fresh logical owners can borrow a qualified native cut until their
 * image is published. Guards remain borrowed and are never consumed here. */
bool frontend_persistence_capture_detached(qa_frontend *, const qa_application_persistence_ops *,
    const frontend_persistence_native *,qa_save_purpose,qa_save_image **,qa_error *);
bool frontend_persistence_restore(qa_frontend **, const qa_application_persistence_ops *,
    const qa_save_image *, qa_frontend **displaced, qa_frontend **retained, qa_error *);
/* source is the actual isolated original-Q1 frontend that produced image.
 * Its constructor capabilities remain borrowed through this operation. */
bool frontend_persistence_restore_original(qa_frontend **,const qa_application_persistence_ops *,
    qa_frontend *source,const qa_save_image *,qa_frontend **displaced,qa_frontend **retained,qa_error *);
#endif

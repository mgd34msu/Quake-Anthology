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
typedef struct frontend_persistence frontend_persistence_restore_operation;
/* The image and external owner contexts stay borrowed until end. The operation
 * owns all frontend/application slots and callback tables across advances. */
bool frontend_persistence_restore_begin(qa_frontend **,
    const qa_application_persistence_ops *,const qa_save_image *,
    frontend_persistence_restore_operation **,qa_error *);
bool frontend_persistence_restore_advance(frontend_persistence_restore_operation *,
    uint64_t quantum,bool *complete,qa_error *);
/* A refused inverse keeps the operation and its active lease for retry. */
bool frontend_persistence_restore_end(frontend_persistence_restore_operation **,
    qa_frontend **displaced,qa_frontend **retained,qa_error *);
bool frontend_persistence_restore(qa_frontend **, const qa_application_persistence_ops *,
    const qa_save_image *, qa_frontend **displaced, qa_frontend **retained, qa_error *);
typedef bool (*frontend_persistence_replay_fn)(void *,qa_frontend *,uint64_t,bool *,qa_error *);
bool frontend_persistence_restore_replay_begin(qa_frontend **,const qa_application_persistence_ops *,
    const qa_save_image *,void *,frontend_persistence_replay_fn,
    frontend_persistence_restore_operation **,qa_error *);
#endif

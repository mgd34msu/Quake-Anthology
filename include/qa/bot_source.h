#ifndef QA_BOT_SOURCE_H
#define QA_BOT_SOURCE_H
#include "qa/math.h"
#include "qa/common.h"

/* Borrowed for one synchronous operation. A value uses direct native access;
 * otherwise the checked callback runs only when its component is consumed. */
typedef struct qa_bot_vector_source {
    const qa_vec3 *value;
    void *context;
    bool (*read)(void *, unsigned component, float *, qa_error *);
} qa_bot_vector_source;
typedef struct qa_bot_vector_target {
    qa_vec3 *value;
    void *context;
    bool (*admit)(void *, unsigned component, qa_error *);
    bool (*write)(void *, unsigned component, float, qa_error *);
} qa_bot_vector_target;
bool qa_bot_vector_component(const qa_bot_vector_source *, unsigned, float *, qa_error *);
bool qa_bot_vector_read(const qa_bot_vector_source *, qa_vec3 *, qa_error *);
/* Optional destination admission precedes each source read, then its write. */
bool qa_bot_vector_write(const qa_bot_vector_target *, const qa_bot_vector_source *, qa_error *);
#endif

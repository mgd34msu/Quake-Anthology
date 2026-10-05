#ifndef QA_Q3_NATIVE_SELECTED_AUTHORED_MEDIA_H
#define QA_Q3_NATIVE_SELECTED_AUTHORED_MEDIA_H

#include "selected_media.h"

typedef struct q3n_selected_authored_media q3n_selected_authored_media;
typedef struct q3n_selected_authored_attachment {
    const char *path, *tag;
} q3n_selected_authored_attachment;
typedef struct q3n_selected_authored_options {
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    const char *gun, *anchor, *anchor_tag;
    const q3n_selected_authored_attachment *attachments;
    size_t attachment_count;
} q3n_selected_authored_options;
typedef struct q3n_selected_authored_attachment_view {
    const char *tag;
    int32_t model;
} q3n_selected_authored_attachment_view;

/* Copies immutable authored declarations; the caller retains the genuine
 * selected content and registry. Create performs no media admission. */
bool q3n_selected_authored_create(const q3n_selected_authored_options *,
    q3n_selected_authored_media **, qa_error *);
bool q3n_selected_authored_idle(const q3n_selected_authored_media *);
void q3n_selected_authored_destroy(q3n_selected_authored_media *);
bool q3n_selected_authored_options_read(const q3n_selected_authored_media *,
    q3n_selected_authored_options *, qa_error *);
/* Authored view has no native animation/barrel/flash media. Its anchor is
 * optional, with the genuine shotgun hands fallback; attachments are required.
 * Held admission independently loads the source-derived barrel/flash/shaders. */
bool q3n_selected_authored_prepare(q3n_selected_authored_media *, bool view,
    void *context, bool (*current)(void *), q3n_selected_weapon_media *, qa_error *);
bool q3n_selected_authored_read(const q3n_selected_authored_media *, bool view,
    q3n_selected_weapon_media *, qa_error *);
size_t q3n_selected_authored_attachment_count(const q3n_selected_authored_media *);
bool q3n_selected_authored_attachment_read(const q3n_selected_authored_media *, size_t,
    q3n_selected_authored_attachment_view *, qa_error *);

#endif

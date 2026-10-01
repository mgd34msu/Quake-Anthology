#ifndef QA_FRONTEND_REMAP_SAVE_H
#define QA_FRONTEND_REMAP_SAVE_H
#include "internal.h"
/* Actual retained frontend remap rows in physical order. Material/world codecs
 * import their private remaps separately; this codec never replays application
 * or source remap callbacks while reconstructing the candidate's row owner. */
bool frontend_shader_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
bool frontend_shader_restore(qa_frontend *, qa_bytes, qa_error *);
#endif

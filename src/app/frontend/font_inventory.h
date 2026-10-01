#ifndef QA_FRONTEND_FONT_INVENTORY_H
#define QA_FRONTEND_FONT_INVENTORY_H
#include "internal.h"
#include "scene_identity.h"

/* Library order is the actual frontend library, physical source groups, then
 * physical native Q2 lease owners with installed libraries. References qualify
 * real registration rows without loading or rasterizing content. The aggregate
 * holds all libraries through these calls. */
bool frontend_fonts_checkpoint(qa_frontend *, frontend_scene_namespace *, qa_buffer *, qa_error *);
bool frontend_fonts_restore(qa_frontend *, frontend_scene_namespace *, qa_bytes, qa_error *);
bool frontend_font_encode(void *, const qa_font *, uint64_t *, qa_error *);
bool frontend_font_decode(void *, uint64_t, const qa_font **, qa_error *);
#endif

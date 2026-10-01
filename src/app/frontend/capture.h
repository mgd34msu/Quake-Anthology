#ifndef QA_FRONTEND_CAPTURE_H
#define QA_FRONTEND_CAPTURE_H
#include "internal.h"

/* Pure admission for the source, scene and immutable-content child owners.
 * The persistent local UI can remain in its map-selection action during travel. */
bool frontend_owners_idle(const qa_frontend *);
/* The whole frontend cannot be captured, stepped or destroyed until its real
 * seat callbacks have returned, including HUD codecs and wheel callbacks. */
bool frontend_seat_callbacks_idle(const qa_frontend *);
bool frontend_sources_idle(const qa_frontend *);
/* The application opens its own persistence/content lease while this actual
 * frontend lease is held. Collection opens unique Q3 registries before their
 * child root tokens, and closes every token in reverse order on all paths. */
bool frontend_capture_begin(qa_frontend *, frontend_capture **, qa_error *);
void frontend_capture_end(frontend_capture *);
const qa_scene_resources *frontend_capture_images_at(const frontend_capture *, size_t);
const qa_material_library *frontend_capture_library_at(const frontend_capture *, size_t);
const qa_font_library *frontend_capture_fonts_at(const frontend_capture *, size_t);
qa_q3_presentation_assets *frontend_capture_assets_at(const frontend_capture *, size_t);
const qa_scene_world *frontend_capture_world_at(const frontend_capture *, size_t);
const qa_scene_model *frontend_capture_model_at(const frontend_capture *, size_t);
#endif

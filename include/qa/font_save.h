#ifndef QA_FONT_SAVE_H
#define QA_FONT_SAVE_H
#include "qa/font.h"
typedef struct qa_font_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, const qa_resource **, qa_error *);
} qa_font_checkpoint_refs;
/* Call at the owning frontend's idle boundary. Resolvers return borrowed
 * versions already admitted to the library's actual image/content owners.
 * Restore retains atlas/source versions, preserves existing heap fonts in
 * their registration order, and appends later registrations without loading
 * files, creating images, rasterizing glyphs or calling FreeType. */
bool qa_font_library_checkpoint(const qa_font_library *, const qa_font_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_font_library_restore(qa_font_library *, qa_bytes, const qa_font_checkpoint_refs *, qa_error *);
size_t qa_font_library_record_count(const qa_font_library *);
const qa_font *qa_font_library_record_at(const qa_font_library *, size_t);
qa_scene_resources *qa_font_library_resource_owner(const qa_font_library *);
#endif

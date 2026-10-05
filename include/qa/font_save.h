#ifndef QA_FONT_SAVE_H
#define QA_FONT_SAVE_H
#include "qa/font.h"
typedef struct qa_font_library_capture qa_font_library_capture;
bool qa_font_library_capture_begin(const qa_font_library *, qa_font_library_capture **, qa_error *);
void qa_font_library_capture_end(qa_font_library_capture *);
bool qa_font_library_idle(const qa_font_library *);
size_t qa_font_library_record_count(const qa_font_library *);
const qa_font *qa_font_library_record_at(const qa_font_library *, size_t);
qa_scene_resources *qa_font_library_resource_owner(const qa_font_library *);
#endif

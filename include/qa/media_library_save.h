#ifndef QA_MEDIA_LIBRARY_SAVE_H
#define QA_MEDIA_LIBRARY_SAVE_H
#include "qa/cinematic.h"

size_t qa_media_library_record_count(const qa_media_library *);
const qa_cinematic_asset *qa_media_library_record_at(const qa_media_library *, size_t);
qa_scene_resources *qa_media_library_resource_owner(const qa_media_library *);
#endif

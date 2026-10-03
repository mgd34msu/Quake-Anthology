#ifndef QA_MATERIAL_LIBRARY_SAVE_PRIVATE_H
#define QA_MATERIAL_LIBRARY_SAVE_PRIVATE_H
#include "library_internal.h"
#include "qa/material_library_save.h"
#include "qa/source_save.h"
bool qa_material_saved_image(qa_source_save_io *, const qa_material_library_checkpoint_refs *, const qa_scene_image **);
bool qa_material_saved_identity(qa_source_save_io *, const qa_material_library_checkpoint_refs *, uint64_t *, bool world);
bool qa_material_saved_text(qa_source_save_io *, char **);
bool qa_material_saved_record(qa_source_save_io *, const qa_material_library_checkpoint_refs *, qa_material_record *);
#endif

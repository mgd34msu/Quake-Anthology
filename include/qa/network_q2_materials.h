#ifndef QA_NETWORK_Q2_MATERIALS_H
#define QA_NETWORK_Q2_MATERIALS_H
#include "qa/material.h"

typedef enum qa_q2_material_dependency_kind {
    QA_Q2_MATERIAL_IMAGE, QA_Q2_MATERIAL_MOVIE, QA_Q2_MATERIAL_SKY
} qa_q2_material_dependency_kind;
typedef struct qa_q2_material_dependency {
    qa_q2_material_dependency_kind kind;
    const char *path;
    size_t begin, end;
} qa_q2_material_dependency;
typedef bool (*qa_q2_material_dependency_fn)(void *, const qa_q2_material_dependency *, qa_error *);
typedef struct qa_q2_material_scope {
    qa_scene_family family;
    bool palette_present;
    uint8_t palette[768];
} qa_q2_material_scope;
/* The authenticated Source supplied this declaration in the actual artifact.
 * It is independent of the receiving Q2 transport and its default palette. */
bool qa_q2_material_script_scope(qa_bytes, qa_q2_material_scope *, qa_error *);
/* One complete downloaded shader program. Callbacks borrow token text only
 * during the call; byte spans include quotes and address the original source. */
bool qa_q2_material_script_dependencies(qa_bytes, qa_q2_material_dependency_fn, void *, qa_error *);
/* Admit the downloaded program under its actual qualified MD3 shader name.
 * The caller has already admitted its dependencies in the same file scope. */
bool qa_q2_material_script_import(qa_material_library *, const char *, qa_bytes,
    const qa_scene_image_options *, qa_error *);
#endif

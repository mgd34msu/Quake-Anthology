#ifndef QA_NETWORK_Q2_MATERIALS_H
#define QA_NETWORK_Q2_MATERIALS_H
#include "qa/material.h"
#include "qa/hash.h"

typedef enum qa_q2_material_dependency_kind {
    QA_Q2_MATERIAL_IMAGE, QA_Q2_MATERIAL_MOVIE, QA_Q2_MATERIAL_SKY
} qa_q2_material_dependency_kind;
typedef struct qa_q2_material_dependency {
    qa_q2_material_dependency_kind kind;
    const char *path;
    size_t begin, end;
    bool builtin_images;
} qa_q2_material_dependency;
typedef bool (*qa_q2_material_dependency_fn)(void *, const qa_q2_material_dependency *, qa_error *);
bool qa_q2_material_dependency_builtin(const qa_q2_material_dependency *);
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
 * The bank and view are the real receiving model's content owners. Retained
 * image aliases install before the program and preserve Source decode rules. */
bool qa_q2_material_script_import(qa_material_library *, qa_scene_resources *, qa_vfs *, const char *, qa_bytes,
    const qa_scene_image_options *, qa_error *);
/* A .qai stores one genuine image admission, including missing/rejected
 * winners, logical sizing and palette outcomes; it contains no shader. */
bool qa_q2_material_image_dependencies(qa_bytes, qa_q2_material_dependency_fn, void *, qa_error *);
bool qa_q2_material_image_import(qa_scene_resources *, qa_vfs *, const char *, qa_bytes, qa_error *);
/* An indexed model's .qpm retains its actual Source constructor options.
 * Palette and translation spans borrow this record until scene creation copies them. */
typedef struct qa_q2_material_model_scope {
    char model_alias[1024], companion_path[1024], palette_alias[1024], palette_path[1024];
    qa_sha256_digest model_digest;
    qa_scene_image_options options;
    uint8_t palette[768], translation[256];
} qa_q2_material_model_scope;
bool qa_q2_material_model_scope_path(const char *model_alias, char out[1024], qa_error *);
bool qa_q2_material_model_scope_read(qa_bytes artifact, const char *model_alias, qa_bytes model,
    qa_q2_material_model_scope *, qa_error *);
bool qa_q2_material_model_scope_dependencies(qa_bytes, qa_q2_material_dependency_fn, void *, qa_error *);
bool qa_q2_material_model_scope_apply(const qa_q2_material_model_scope *, qa_bytes palette,
    qa_scene_image_options *, qa_error *);
#endif

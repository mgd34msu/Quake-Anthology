#ifndef QA_FRONTEND_EQUIPMENT_HELD_H
#define QA_FRONTEND_EQUIPMENT_HELD_H

#include "qa/model.h"
#include "qa/vfs.h"

typedef struct frontend_held_declaration {
    qa_resource *source;
    char *path, *fallback;
    uint32_t reference_frame, *vertices;
    size_t vertex_count;
    uint64_t byte_length;
    qa_model_transform grip;
    bool none, has_byte_length;
} frontend_held_declaration;

/* Retains the exact admitted declaration. Output is unchanged on failure. */
bool frontend_held_declaration_read(qa_resource *, frontend_held_declaration *, qa_error *);
bool frontend_held_declaration_value(qa_bytes, frontend_held_declaration *, qa_error *);
void frontend_held_declaration_free(frontend_held_declaration *);

typedef struct frontend_held_model {
    const qa_model *model;
    qa_model *subset;
    qa_model_mesh *mesh;
    qa_model_triangle *triangles;
    qa_model_transform alignment;
    uint32_t reference_frame;
} frontend_held_model;

/* Borrows the real parsed holder. The enclosing media owner keeps it alive.
 * Subsets own only their triangle list, never the borrowed model arrays. */
bool frontend_held_model_prepare(const frontend_held_declaration *, const char *source_path, const qa_resource *,
    const qa_model *, frontend_held_model *, qa_error *);
void frontend_held_model_free(frontend_held_model *);

#endif

#include "equipment_held.h"
#include "qa/json.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

void frontend_held_declaration_free(frontend_held_declaration *declaration)
{
    if (!declaration) return;
    qa_resource_release(declaration->source);
    free(declaration->path); free(declaration->fallback);
    free(declaration->vertices);
    *declaration = (frontend_held_declaration){0};
}

static bool string(const qa_json_document *doc, qa_json_id id, qa_buffer *out, qa_error *error)
{
    if (!qa_json_string(doc, id, out, error)) return false;
    if (memchr(out->data, 0, out->size)) {
        qa_buffer_free(out);
        return fail(error, QA_ERROR_FORMAT, "Held declaration string contains NUL");
    }
    return true;
}

static bool path(const qa_json_document *doc, qa_json_id id, char **out, qa_error *error)
{
    qa_buffer value = {0};
    if (!string(doc, id, &value, error)) return false;
    char *normalized = qa_vfs_normalize_path((const char *)value.data, error);
    qa_buffer_free(&value);
    if (!normalized) return false;
    *out = normalized;
    return true;
}

static bool index(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, id, &value, error)) return false;
    if (value > UINT32_MAX) return fail(error, QA_ERROR_FORMAT, "Held model index exceeds native extent");
    *out = (uint32_t)value;
    return true;
}

static bool vector(const qa_json_document *doc, qa_json_id id, float out[3], qa_error *error)
{
    static const char *const axes[] = {"x", "y", "z"};
    for (size_t i = 0; i < 3; ++i) {
        double value;
        if (!qa_json_number(doc, qa_json_get(doc, id, axes[i]), &value, error)) return false;
        if (!isfinite(value) || value < -FLT_MAX || value > FLT_MAX)
            return fail(error, QA_ERROR_FORMAT, "Held model grip exceeds finite native coordinates");
        out[i] = (float)value;
    }
    return true;
}

static double dot(const float a[3], const float b[3])
{
    return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
}

static bool grip(const qa_json_document *doc, qa_json_id id, qa_model_transform *out,
    qa_error *error)
{
    qa_model_transform_identity(out);
    qa_json_id axes = qa_json_get(doc, id, "axis"), scale = qa_json_get(doc, id, "scale");
    if (qa_json_type(doc, axes) != QA_JSON_ARRAY || qa_json_size(doc, axes) != 3)
        return fail(error, QA_ERROR_FORMAT, "Held grip requires three source axes");
    if (!vector(doc, qa_json_get(doc, id, "origin"), out->origin, error)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!vector(doc, qa_json_at(doc, axes, i), out->axes[i], error)) return false;
    if (scale != QA_JSON_NONE && !vector(doc, scale, out->scale, error)) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (fabs(dot(out->axes[i], out->axes[i]) - 1) > .001 || out->scale[i] == 0)
            return fail(error, QA_ERROR_FORMAT, "Held grip requires rotation axes and invertible scale");
        for (size_t j = 0; j < i; ++j)
            if (fabs(dot(out->axes[i], out->axes[j])) > .001)
                return fail(error, QA_ERROR_FORMAT, "Held grip axes are not orthogonal");
    }
    double cross[3] = {
        (double)out->axes[0][1] * out->axes[1][2] - (double)out->axes[0][2] * out->axes[1][1],
        (double)out->axes[0][2] * out->axes[1][0] - (double)out->axes[0][0] * out->axes[1][2],
        (double)out->axes[0][0] * out->axes[1][1] - (double)out->axes[0][1] * out->axes[1][0]
    };
    double determinant = 0;
    for (size_t i = 0; i < 3; ++i) determinant += cross[i] * out->axes[2][i];
    return determinant >= .999 || fail(error, QA_ERROR_FORMAT, "Held grip axes reverse orientation");
}

static bool part(const qa_json_document *doc, qa_json_id id, frontend_held_declaration *out,
    qa_error *error)
{
    qa_json_id vertices = qa_json_get(doc, id, "vertices");
    if (qa_json_type(doc, vertices) != QA_JSON_ARRAY ||
        !(out->vertex_count = qa_json_size(doc, vertices)))
        return fail(error, QA_ERROR_FORMAT, "Held subset requires source vertices");
    if (out->vertex_count > SIZE_MAX / sizeof(*out->vertices))
        return fail(error, QA_ERROR_MEMORY, "Held subset exceeds address space");
    out->vertices = calloc(out->vertex_count, sizeof(*out->vertices));
    if (!out->vertices)
        return fail(error, QA_ERROR_MEMORY, "Retaining held subset qualification");
    for (size_t i = 0; i < out->vertex_count; ++i) {
        if (!index(doc, qa_json_at(doc, vertices, i), out->vertices + i, error)) return false;
        for (size_t j = 0; j < i; ++j)
            if (out->vertices[i] == out->vertices[j])
                return fail(error, QA_ERROR_FORMAT, "Held subset repeats a source vertex");
    }
    return true;
}

static bool declaration_read(qa_bytes bytes,bool file, frontend_held_declaration *out,qa_error *error)
{
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Held declaration requires output");
    qa_json_document *doc = NULL;
    if (!qa_json_parse(bytes, &doc, error)) return false;
    frontend_held_declaration result = {0};
    qa_json_id root = qa_json_root(doc), kind = qa_json_get(doc, root, "kind");
    uint32_t version;
    bool ok = !file || index(doc, qa_json_get(doc, root, "version"), &version, error);
    if (ok && file && version != 1) ok = fail(error, QA_ERROR_FORMAT, "Unsupported held declaration version");
    if (ok && qa_json_string_equal(doc, kind, "none")) result.none = true;
    else if (ok && qa_json_string_equal(doc, kind, "model")) {
        qa_json_id model = qa_json_get(doc, root, "model"), fallback = qa_json_get(doc, model, "fallback"),
            length = qa_json_get(doc, model, "byteLength"), subset = qa_json_get(doc, model, "part");
        ok = path(doc, qa_json_get(doc, model, "path"), &result.path, error) &&
            index(doc, qa_json_get(doc, model, "referenceFrame"), &result.reference_frame, error) &&
            grip(doc, qa_json_get(doc, model, "grip"), &result.grip, error);
        if (ok && fallback != QA_JSON_NONE) ok = path(doc, fallback, &result.fallback, error);
        if (ok && length != QA_JSON_NONE) {
            result.has_byte_length = true; ok = qa_json_u64(doc, length, &result.byte_length, error);
        }
        if (ok && subset != QA_JSON_NONE) ok = part(doc, subset, &result, error);
    } else if (ok) ok = fail(error, QA_ERROR_FORMAT, "Held declaration requires none or model");
    qa_json_destroy(doc);
    if (!ok) { frontend_held_declaration_free(&result); return false; }
    *out = result;
    return true;
}

bool frontend_held_declaration_value(qa_bytes bytes,frontend_held_declaration *out,qa_error *error)
{ return declaration_read(bytes,false,out,error); }

bool frontend_held_declaration_read(qa_resource *source,frontend_held_declaration *out,qa_error *error)
{
    if(!source)return fail(error,QA_ERROR_ARGUMENT,"Held declaration requires retained source");
    if(!declaration_read(qa_resource_bytes(source),true,out,error))return false;
    out->source=source;qa_resource_retain(source);return true;
}

void frontend_held_model_free(frontend_held_model *model)
{
    if (!model) return;
    free(model->triangles);
    free(model->mesh); free(model->subset);
    *model = (frontend_held_model){0};
}

static bool contains(const frontend_held_declaration *declaration, uint32_t vertex)
{
    for (size_t i = 0; i < declaration->vertex_count; ++i)
        if (declaration->vertices[i] == vertex) return true;
    return false;
}

bool frontend_held_model_prepare(const frontend_held_declaration *declaration,
    const char *source_path, const qa_resource *source, const qa_model *model,
    frontend_held_model *out, qa_error *error)
{
    if (!declaration || declaration->none || !source || !model || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Held model requires its actual admitted source holder");
    if (!declaration->path || !source_path ||
        (strcmp(source_path, declaration->path) &&
         (!declaration->fallback || strcmp(source_path, declaration->fallback))))
        return fail(error, QA_ERROR_FORMAT, "Held model differs from declared source path");
    if (declaration->has_byte_length && declaration->byte_length != qa_resource_bytes(source).size)
        return fail(error, QA_ERROR_FORMAT, "Held model differs from declared byte length");
    if (declaration->reference_frame >= model->frame_count)
        return fail(error, QA_ERROR_FORMAT, "Held reference frame exceeds source animation");
    frontend_held_model result = {0};
    qa_model_transform socket;
    qa_model_transform_identity(&socket);
    socket.origin[0] = -2.9841071642362156f;
    socket.origin[1] = -.7671715473899474f;
    socket.origin[2] = -2.208833547738882f;
    if (!qa_model_attachment_align(&declaration->grip, &socket, &result.alignment))
        return fail(error, QA_ERROR_FORMAT, "Held grip cannot align to actual Q3 hand socket");
    for (size_t i = 0; i < 3; ++i) {
        if (!isfinite(result.alignment.origin[i]))
            return fail(error, QA_ERROR_FORMAT, "Held alignment exceeds finite native coordinates");
        for (size_t j = 0; j < 3; ++j)
            if (!isfinite(result.alignment.axes[i][j]))
                return fail(error, QA_ERROR_FORMAT, "Held alignment exceeds finite native axes");
    }
    result.reference_frame = declaration->reference_frame;
    if (!declaration->vertex_count) { result.model = model; *out = result; return true; }
    if (model->format != QA_MODEL_MDL || model->mesh_count != 1)
        return fail(error, QA_ERROR_FORMAT, "Held subset is not qualified for actual Q1 MDL holder");
    const qa_model_mesh *mesh = model->meshes;
    for (size_t i = 0; i < declaration->vertex_count; ++i)
        if (declaration->vertices[i] >= mesh->vertex_count)
            return fail(error, QA_ERROR_FORMAT, "Held subset vertex exceeds actual source topology");
    uint32_t count = 0;
    for (uint32_t i = 0; i < mesh->triangle_count; ++i) {
        const qa_model_triangle *triangle = mesh->triangles + i;
        if (contains(declaration, triangle->vertex[0]) && contains(declaration, triangle->vertex[1]) &&
            contains(declaration, triangle->vertex[2])) ++count;
    }
    if (!count) return fail(error, QA_ERROR_FORMAT, "Held subset has no source triangles");
    if ((uint64_t)count * sizeof(*result.triangles) > SIZE_MAX)
        return fail(error, QA_ERROR_MEMORY, "Held triangle subset exceeds address space");
    result.triangles = calloc(count, sizeof(*result.triangles));
    result.subset = malloc(sizeof(*result.subset));
    result.mesh = malloc(sizeof(*result.mesh));
    if (!result.triangles || !result.subset || !result.mesh) {
        frontend_held_model_free(&result);
        return fail(error, QA_ERROR_MEMORY, "Retaining held source triangle subset");
    }
    uint32_t at = 0;
    for (uint32_t i = 0; i < mesh->triangle_count; ++i) {
        const qa_model_triangle *triangle = mesh->triangles + i;
        if (contains(declaration, triangle->vertex[0]) && contains(declaration, triangle->vertex[1]) &&
            contains(declaration, triangle->vertex[2])) result.triangles[at++] = *triangle;
    }
    *result.subset = *model; *result.mesh = *mesh;
    result.mesh->triangles = result.triangles; result.mesh->triangle_count = count;
    result.subset->meshes = result.mesh; result.model = result.subset;
    *out = result;
    return true;
}

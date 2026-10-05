#include "internal.h"
#include "native_q3_client.h"

struct frontend_remap {
    frontend_remap *next;
    char *original, *replacement;
    float offset;
};
bool frontend_material_remaps(qa_frontend *frontend, qa_material_library *library, qa_error *error)
{
    for (frontend_remap *remap = frontend->remaps; remap; remap = remap->next)
        if (!qa_material_remap(library, remap->original, remap->replacement, remap->offset, error)) return false;
    return true;
}
static bool apply(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    return (!frontend->scene_world || qa_scene_world_remap(frontend->scene_world, original, replacement, offset, error)) &&
        (!frontend->materials || qa_material_remap(frontend->materials, original, replacement, offset, error)) &&
        frontend_source_remap(frontend, original, replacement, offset, error) &&
        frontend_native_q3_remap(frontend,original,replacement,offset,error) &&
        frontend_visuals_remap(frontend, original, replacement, offset, error);
}
bool frontend_shader_remap(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    if (!original || !*original || !replacement || !*replacement || !isfinite(offset))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "shader remap requires two names and a finite clock offset");
    frontend_remap *entry;
    for (entry = frontend->remaps; entry; entry = entry->next)
        if (!strcmp(entry->original, original)) break;
    if (entry && !strcmp(entry->replacement, replacement) && entry->offset == offset) return true;
    char *copy = malloc(strlen(replacement) + 1);
    frontend_remap *created = entry ? NULL : calloc(1, sizeof(*created));
    if (!copy || (!entry && !created)) { free(copy); free(created); return frontend_fail(error, QA_ERROR_MEMORY, "retaining shader remap"); }
    memcpy(copy, replacement, strlen(replacement) + 1);
    if (created) {
        created->original = malloc(strlen(original) + 1);
        if (!created->original) { free(copy); free(created); return frontend_fail(error, QA_ERROR_MEMORY, "retaining shader remap name"); }
        memcpy(created->original, original, strlen(original) + 1);
        entry = created;
    }
    if (!apply(frontend, original, replacement, offset, error)) {
        free(copy); if (created) { free(created->original); free(created); } return false;
    }
    free(entry->replacement); entry->replacement = copy; entry->offset = offset;
    if (created) { entry->next = frontend->remaps; frontend->remaps = entry; }
    return true;
}
bool frontend_shader_sync(qa_frontend *frontend, qa_error *error)
{
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (size_t i = 0; i < qa_application_shader_remap_count(frontend->application); ++i) {
        qa_application_shader_remap_view view;
        if (!qa_application_shader_remap_at(frontend->application, i, &view))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "shader remap observations changed during presentation");
        if (!frontend_shader_remap(frontend, qa_strings_cstr(strings, view.original),
                qa_strings_cstr(strings, view.replacement), (float)((double)view.time_ns / 1e9), error)) return false;
    }
    return true;
}
bool frontend_shader_retire(qa_frontend *frontend, qa_error *error)
{
    for (frontend_remap *remap = frontend->remaps; remap; remap = remap->next)
        if (!apply(frontend, remap->original, remap->original, 0, error)) return false;
    frontend_shader_destroy(frontend); return true;
}
void frontend_shader_destroy(qa_frontend *frontend)
{
    while (frontend->remaps) {
        frontend_remap *remap = frontend->remaps; frontend->remaps = remap->next;
        free(remap->original); free(remap->replacement); free(remap);
    }
}

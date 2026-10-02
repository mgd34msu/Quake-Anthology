#include "internal.h"
#include "remap_save.h"
#include "save_private.h"
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
static bool remap_fields(qa_source_save_io *io,frontend_remap **head)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','F','R','M'}; uint32_t version=1; size_t count=0;
    if (!reading) for (const frontend_remap *row=*head;row;row=row->next) {
        if (count==SIZE_MAX) return false;
        ++count;
    }
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QFRM",4) ||
        !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_count(io,&count,reading?io->input.size/14:SIZE_MAX)) return false;
    frontend_remap *row=*head,**tail=head;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            row=calloc(1,sizeof(*row));
            if (!row) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining actual frontend shader remap continuation");
            *tail=row; tail=&row->next;
        }
        if (!row || !frontend_save_text(io,&row->original) || !row->original || !*row->original ||
            !frontend_save_text(io,&row->replacement) || !row->replacement || !*row->replacement ||
            !qa_source_save_f32(io,&row->offset) || !isfinite(row->offset)) return false;
        for (const frontend_remap *prior=*head;prior!=row;prior=prior->next)
            if (!strcmp(prior->original,row->original)) return false;
        if (!reading) row=row->next;
    }
    return reading || !row;
}
bool frontend_shader_checkpoint(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    if (!f || !f->application || f->stepping || !f->capture || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Shader rows require a held genuine frontend capture");
    qa_source_save_io io={0}; frontend_remap *head=f->remaps;
    bool ok=qa_source_save_writer(&io,qa_application_session(f->application),error) &&
        remap_fields(&io,&head) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid retained frontend shader remap rows");
    return ok;
}
bool frontend_shader_restore(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->stepping || !f->source_restoring || f->remaps)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Shader rows require the actual empty isolated candidate owner");
    qa_source_save_io io={0}; qa_frontend state={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) &&
        remap_fields(&io,&state.remaps) && qa_source_save_finish(&io,NULL);
    if (ok) { f->remaps=state.remaps; state.remaps=NULL; }
    frontend_shader_destroy(&state); qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved frontend shader remap rows");
    return ok;
}

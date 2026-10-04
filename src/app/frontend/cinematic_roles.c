#include "cinematic_roles.h"
#include "material_movies_private.h"

typedef struct cinematic_role {
    struct cinematic_role *next;
    frontend_cinematic_role_view view;
} cinematic_role;
struct frontend_cinematic_roles {
    cinematic_role *first, *last;
    size_t count;
};
static bool source_equal(const frontend_material_movie_source *a,
    const frontend_material_movie_source *b)
{
    return a->frontend == b->frontend && a->files == b->files && a->images == b->images &&
        a->materials == b->materials && a->media == b->media &&
        a->context == b->context && a->current == b->current;
}
static bool source_structure(const qa_frontend *f, const frontend_material_movie_source *source)
{
    return f && source && source->frontend == f && source->files && source->images &&
        source->materials && source->media && source->current &&
        qa_scene_resources_files(source->images) == source->files &&
        qa_material_library_resource_owner(source->materials) == source->images &&
        qa_media_library_resource_owner(source->media) == source->images;
}
static cinematic_role *at(const qa_frontend *f, size_t index)
{
    cinematic_role *row = f && f->cinematic_roles ? f->cinematic_roles->first : NULL;
    while (row && index--) row = row->next;
    return row;
}
static bool reserve(qa_frontend *f, cinematic_role **out, qa_error *error)
{
    if (!f->cinematic_roles) {
        f->cinematic_roles = calloc(1, sizeof(*f->cinematic_roles));
        if (!f->cinematic_roles)
            return frontend_fail(error, QA_ERROR_MEMORY, "Retaining detached cinematic role owner");
    }
    if (f->cinematic_roles->count == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_MEMORY, "Detached cinematic role count overflow");
    *out = calloc(1, sizeof(**out));
    return *out != NULL || frontend_fail(error, QA_ERROR_MEMORY, "Retaining detached cinematic role source");
}
static void append(qa_frontend *f, cinematic_role *row)
{
    frontend_cinematic_roles *owner = f->cinematic_roles;
    if (owner->last) owner->last->next = row;
    else owner->first = row;
    owner->last = row; ++owner->count;
}
size_t frontend_cinematic_roles_count(const qa_frontend *f)
{ return f && f->cinematic_roles ? f->cinematic_roles->count : 0; }
bool frontend_cinematic_roles_read(const qa_frontend *f, size_t index,
    frontend_cinematic_role_view *out, qa_error *error)
{
    const cinematic_role *row = at(f, index);
    if (!row || !out || !source_structure(f, &row->view.source) ||
        row->view.seat >= f->options.seats || !row->view.bus || row->view.bus == QA_AUDIO_NO_OWNER)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Detached cinematic role lost its real provider namespace");
    if (row->view.cinematics) {
        const qa_q3_cinematic_source *parent = NULL;
        uint32_t seat = 0; uint64_t bus = 0; bool numeric = false;
        void *diagnostic = NULL; bool bound = false;
        if (!row->view.parent || !source_equal(&row->view.parent->source, &row->view.source) ||
            !frontend_material_movies_cinematic_namespace_read(row->view.parent, &seat, &bus, &numeric, error) || !numeric ||
            !qa_q3_cinematic_source_role_read(row->view.cinematics, &parent, &seat, &bus) ||
            parent != row->view.parent->cinematic_source || seat != row->view.seat || bus != row->view.bus ||
            !qa_q3_cinematic_source_role_diagnostic_read(row->view.cinematics, &diagnostic, &bound) || bound || diagnostic)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Detached cinematic role leaves its stable parent custody");
    } else if (!f->source_restoring || row->view.parent)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Detached cinematic role has no reconstructed numeric source");
    *out = row->view; return true;
}
bool frontend_cinematic_roles_adopt(qa_frontend *f, qa_q3_cinematic_source **slot,
    void *diagnostic_context, qa_error *error)
{
    if (!f || !slot)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Cinematic role transfer requires its actual owned slot");
    qa_q3_cinematic_source *source = *slot;
    if (!source || !qa_q3_cinematic_source_retained(source)) return true;
    const qa_q3_cinematic_source *parent = NULL;
    uint32_t seat = 0; uint64_t bus = 0;
    if (f->source_restoring || f->capture || f->resource_inventory ||
        !qa_q3_cinematic_source_role_read(source, &parent, &seat, &bus) ||
        qa_q3_cinematic_source_handles(source) != f->source_cinematics || seat >= f->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Cinematic role transfer requires its returned original pool source");
    frontend_material_movies *provider = f->material_movie_owners;
    while (provider && provider->cinematic_source != parent) provider = provider->next;
    uint32_t provider_seat = 0; uint64_t provider_bus = 0; bool numeric = false;
    if (!provider || !frontend_material_movies_cinematic_namespace_read(provider,
        &provider_seat, &provider_bus, &numeric, error) || !numeric || provider_seat != seat)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Cinematic role transfer has no retained material provider");
    for (cinematic_role *row = f->cinematic_roles ? f->cinematic_roles->first : NULL; row; row = row->next)
        if (row->view.cinematics == source || row->view.bus == bus)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Cinematic role already has detached namespace custody");
    cinematic_role *row = NULL;
    if (!reserve(f, &row, error)) return false;
    row->view = (frontend_cinematic_role_view){.parent = provider, .source = provider->source,
        .cinematics = source, .seat = seat, .bus = bus};
    if (!qa_q3_cinematic_source_role_detach(source, diagnostic_context, error)) { free(row); return false; }
    append(f, row); *slot = NULL; return true;
}
static bool remove_rows(qa_frontend *f, frontend_material_movies *parent, bool unused_only, qa_error *error)
{
    frontend_cinematic_roles *owner = f ? f->cinematic_roles : NULL;
    if (!owner) return true;
    cinematic_role **link = &owner->first;
    while (*link) {
        cinematic_role *row = *link;
        if ((parent && row->view.parent != parent) ||
            (unused_only && qa_q3_cinematic_source_retained(row->view.cinematics))) {
            link = &row->next; continue;
        }
        if (!qa_q3_cinematic_source_destroy(&row->view.cinematics, error)) return false;
        *link = row->next; free(row); --owner->count;
    }
    owner->last = owner->first;
    while (owner->last && owner->last->next) owner->last = owner->last->next;
    if (!owner->count) { free(owner); f->cinematic_roles = NULL; }
    return true;
}
bool frontend_cinematic_roles_parent_destroy(qa_frontend *f, frontend_material_movies *parent, qa_error *error)
{ return parent && remove_rows(f, parent, false, error); }
void frontend_cinematic_roles_parent_rebind(qa_frontend *f, frontend_material_movies *parent)
{
    for (cinematic_role *row = f && f->cinematic_roles ? f->cinematic_roles->first : NULL; row; row = row->next)
        if (row->view.parent == parent) row->view.source = parent->source;
}
bool frontend_cinematic_roles_destroy(qa_frontend *f, qa_error *error)
{ return f && remove_rows(f, NULL, false, error); }
bool frontend_cinematic_roles_prune(qa_frontend *f, qa_error *error)
{
    if (!f || f->source_restoring || f->capture || f->resource_inventory ||
        (f->source_cinematics && !qa_q3_cinematic_handles_idle(f->source_cinematics)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Detached cinematic pruning requires its returned live pool");
    return remove_rows(f, NULL, true, error);
}
bool frontend_cinematic_roles_restore_add(qa_frontend *f,
    const frontend_material_movie_source *source, uint32_t seat, size_t index, qa_error *error)
{
    if (!f || !f->source_restoring || !source_structure(f, source) || seat >= f->options.seats)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Detached role namespace import requires its actual candidate provider");
    cinematic_role *existing = at(f, index);
    if (existing)
        return (source_equal(&existing->view.source, source) && existing->view.seat == seat) ||
            frontend_fail(error, QA_ERROR_FORMAT, "Detached role retry changed its physical provider namespace");
    if (index != frontend_cinematic_roles_count(f))
        return frontend_fail(error, QA_ERROR_FORMAT, "Detached role namespace import changed its physical order");
    cinematic_role *row = NULL; uint64_t bus = 0;
    if (!reserve(f, &row, error)) return false;
    if (!frontend_source_identity_allocate(f, &bus, error)) { free(row); return false; }
    row->view = (frontend_cinematic_role_view){.source = *source, .seat = seat, .bus = bus};
    append(f, row); return true;
}
bool frontend_cinematic_roles_restore_bind(qa_frontend *f, size_t index,
    frontend_material_movies *parent, qa_error *error)
{
    cinematic_role *row = at(f, index);
    uint32_t seat = 0; uint64_t bus = 0; bool numeric = false;
    if (!f || !f->source_restoring || !row || !parent || !source_equal(&row->view.source, &parent->source) ||
        !frontend_material_movies_cinematic_namespace_read(parent, &seat, &bus, &numeric, error) ||
        !numeric || seat != row->view.seat)
        return frontend_fail(error, QA_ERROR_FORMAT, "Detached role import has no exact reconstructed provider source");
    if (row->view.cinematics) return row->view.parent == parent;
    if (!qa_q3_cinematic_source_create_role(parent->cinematic_source, row->view.seat,
        row->view.bus, &row->view.cinematics, error)) return false;
    row->view.parent = parent; return true;
}

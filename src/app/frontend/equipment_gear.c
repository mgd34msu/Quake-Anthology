#include "qa/q3_assets_save.h"
#include "equipment_gear_private.h"

bool frontend_equipment_gear_source(qa_frontend *frontend, qa_actor_owner namespace,
    qa_application_equipment_content *out, const application_q3_grapple_definition **definition,
    qa_error *error)
{
    if (!frontend || !frontend->application || !out || !definition ||
        !qa_application_equipment_content_read(frontend->application, namespace, out, error)) return false;
    application_equipment_runtime *runtime = frontend->application->equipment_runtime;
    application_equipment_runtime_source source;
    if (!application_equipment_runtime_source_read(runtime, out->selected_owner, &source, error)) return false;
    if (source.gear_owner != namespace || !source.gear || !source.definition ||
        source.service_owner != out->service_owner || source.content != out->files ||
        source.artifact != out->artifact || source.acquisition != out->acquisition)
        return frontend_fail(error, QA_ERROR_FORMAT, "Gear media lost its actual private source tuple");
    *definition = source.definition; return true;
}

bool frontend_equipment_gear_quiet(const equipment_gear_content *owner)
{
    if (!owner || owner->admitting || owner->world_users || !q3n_selected_authored_idle(owner->view.media) ||
        !q3n_weapons_idle(owner->weapons)) return false;
    for (const frontend_equipment_gear_presenter *row = owner->presenters; row; row = row->next)
        if (row->users) return false;
    return true;
}
bool frontend_equipment_gear_idle(const qa_frontend *frontend)
{
    const frontend_equipment_gear *equipment = frontend ? frontend->equipment_gear : NULL;
    if (!equipment) return true;
    if (equipment->admitting) return false;
    for (const equipment_gear_content *owner = equipment->contents; owner; owner = owner->next)
        if (!frontend_equipment_gear_quiet(owner) || !qa_q3_assets_idle(owner->view.assets)) return false;
    return true;
}
void frontend_equipment_gear_content_dispose(equipment_gear_content *owner)
{
    if (!owner) return;
    while (owner->presenters) {
        frontend_equipment_gear_presenter *row = owner->presenters;
        owner->presenters = row->next; free(row);
    }
    q3n_weapons_destroy(owner->weapons);
    q3n_selected_authored_destroy(owner->view.media);
    qa_q3_presentation_assets_destroy(owner->view.assets);
    free(owner);
}
bool frontend_equipment_gear_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_equipment_gear_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear media retains a true draw, registration or capture");
    if (!frontend || !frontend->equipment_gear) return true;
    while (frontend->equipment_gear->contents) {
        equipment_gear_content *row = frontend->equipment_gear->contents;
        frontend->equipment_gear->contents = row->next; frontend_equipment_gear_content_dispose(row);
    }
    frontend->equipment_gear->tail = NULL; return true;
}
void frontend_equipment_gear_destroy(qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment_gear || !frontend_equipment_gear_retire(frontend, NULL)) return;
    free(frontend->equipment_gear); frontend->equipment_gear = NULL;
}

bool frontend_equipment_gear_content_create(qa_frontend *frontend,
    const qa_application_equipment_content *source, const application_q3_grapple_definition *definition,
    const frontend_visual_owner_view *content, qa_q3_product product,
    equipment_gear_content **out, qa_error *error)
{
    if (!source || !definition || !content || !out || *out ||
        source->selected_owner != content->owner || content->family != QA_GAME_Q3 ||
        !content->mounts || !content->images || !content->materials ||
        !qa_vfs_lookup_equal(content->mounts, source->files) ||
        definition->presentation.attachment_count > SIZE_MAX / sizeof(q3n_selected_authored_attachment))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear registry requires its actual declaration and selected media heaps");
    equipment_gear_content *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining private gear presentation owner");
    owner->frontend = frontend;
    owner->view = (frontend_equipment_gear_owner_view){.source = *source,
        .definition = definition, .product = product, .content = *content};
    qa_q3_presentation_asset_options assets = {.provider = {.mounts = content->mounts,
        .images = content->images, .materials = content->materials, .family = QA_GAME_Q3, .geometry_owner = qa_application_world(frontend->application)}};
    bool okay = qa_q3_presentation_assets_create(&assets, &owner->view.assets, error);
    size_t count = definition->presentation.attachment_count;
    q3n_selected_authored_attachment *attachments = count ? calloc(count, sizeof(*attachments)) : NULL;
    if (okay && count && !attachments)
        okay = frontend_fail(error, QA_ERROR_MEMORY, "Reading genuine gear attachment declarations");
    for (size_t i = 0; okay && i < count; ++i)
        attachments[i] = (q3n_selected_authored_attachment){definition->presentation.attachments[i].path,
            definition->presentation.attachments[i].tag};
    q3n_selected_authored_options media = {.content = content->mounts, .assets = owner->view.assets,
        .gun = definition->presentation.view_model, .anchor = definition->presentation.anchor_path,
        .anchor_tag = definition->presentation.anchor_tag, .attachments = attachments, .attachment_count = count};
    if (okay) okay = q3n_selected_authored_create(&media, &owner->view.media, error);
    free(attachments);
    q3n_weapon_options weapons = {.product = product, .assets = owner->view.assets};
    if (okay) okay = q3n_weapons_create(&weapons, &owner->weapons, error);
    if (!okay) { frontend_equipment_gear_content_dispose(owner); return false; }
    *out = owner; return true;
}

typedef struct gear_prepare_call {
    qa_frontend *frontend;
    const application_equipment_gear_presentation *source;
    void *context;
    bool (*current)(void *);
} gear_prepare_call;
static bool prepare_current(void *context)
{
    gear_prepare_call *call = context;
    return call->current(call->context) &&
        application_equipment_gear_presentation_current(call->frontend->application, call->source);
}
bool frontend_equipment_gear_prepare(qa_frontend *frontend,
    const application_equipment_gear_presentation *source, bool view,
    void *context, bool (*current)(void *), frontend_equipment_gear_presenter **out, qa_error *error)
{
    if (!frontend || !source || !out || !current || !current(context) ||
        !application_equipment_gear_presentation_current(frontend->application, source) ||
        (frontend->equipment_gear && frontend->equipment_gear->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear admission requires its actual full actor and recipient lease");
    if (!frontend->equipment_gear) {
        frontend->equipment_gear = calloc(1, sizeof(*frontend->equipment_gear));
        if (!frontend->equipment_gear) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining frontend gear media roster");
    }
    frontend_equipment_gear *equipment = frontend->equipment_gear;
    equipment_gear_content *owner = equipment->contents;
    while (owner && owner->view.source.owner != source->source.gear_owner) owner = owner->next;
    bool fresh = !owner, okay = true; equipment->admitting = true;
    qa_application_equipment_content actual = {0}; const application_q3_grapple_definition *definition = NULL;
    okay = frontend_equipment_gear_source(frontend, source->source.gear_owner, &actual, &definition, error);
    if (okay && fresh) {
        frontend_visual_owner_view content; const qa_q3_model_names *names;
        okay = frontend_visual_media_acquire(frontend, actual.selected_owner, QA_GAME_Q3, &content, error) &&
            qa_application_equipment_q3_metadata_read(frontend->application, actual.selected_owner, &names, error) &&
            frontend_equipment_gear_content_create(frontend, &actual, definition, &content, names->product, &owner, error);
    }
    if (okay && (definition != owner->view.definition || !qa_application_equipment_content_current(
            frontend->application, &owner->view.source)))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Gear registry was superseded by a different source");
    frontend_equipment_gear_presenter *presenter = okay ? owner->presenters : NULL, *candidate = NULL;
    while (presenter && !qa_actor_id_equal(presenter->actor, source->actor)) presenter = presenter->next;
    if (okay && !presenter) {
        candidate = calloc(1, sizeof(*candidate));
        if (!candidate) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear actor presenter");
        else { candidate->owner = owner; candidate->actor = source->actor; presenter = candidate; }
    }
    if (okay) {
        gear_prepare_call call = {frontend, source, context, current}; q3n_selected_weapon_media media;
        owner->admitting = true;
        okay = q3n_selected_authored_prepare(owner->view.media, view, &call, prepare_current, &media, error);
        owner->admitting = false;
    }
    equipment->admitting = false;
    if (!okay) { free(candidate); if (fresh) frontend_equipment_gear_content_dispose(owner); return false; }
    if (fresh) {
        if (equipment->tail) equipment->tail->next = owner; else equipment->contents = owner;
        equipment->tail = owner;
    }
    if (candidate) {
        if (owner->tail) owner->tail->next = candidate; else owner->presenters = candidate;
        owner->tail = candidate;
    }
    *out = presenter; return true;
}
bool frontend_equipment_gear_retain(frontend_equipment_gear_presenter *presenter, qa_error *error)
{
    if (!presenter || presenter->users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear presenter retain exceeds its actual lifetime");
    ++presenter->users; return true;
}
void frontend_equipment_gear_release(frontend_equipment_gear_presenter *presenter)
{ if (presenter) --presenter->users; }
size_t frontend_equipment_gear_count(const qa_frontend *frontend)
{
    size_t count = 0;
    for (const equipment_gear_content *row = frontend && frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
        row; row = row->next) ++count;
    return count;
}
bool frontend_equipment_gear_at(const qa_frontend *frontend, size_t ordinal,
    frontend_equipment_gear_owner_view *out, qa_error *error)
{
    const equipment_gear_content *row = frontend && frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
    while (row && ordinal) { row = row->next; --ordinal; }
    if (!row || !out || row->admitting)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear inventory lacks its physical retained owner");
    *out = row->view; return true;
}
bool frontend_equipment_gear_content_visit(const qa_frontend *frontend,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->view || (frontend->equipment_gear && frontend->equipment_gear->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear content visitor requires its actual retained roster");
    for (const equipment_gear_content *row = frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
        row; row = row->next)
        if (!frontend_equipment_gear_quiet(row) ||
            !qa_application_equipment_content_current(frontend->application, &row->view.source) ||
            !visitor->view(visitor->context, row->view.content.mounts, error) ||
            !visitor->view(visitor->context, row->view.source.files, error)) return false;
    return true;
}

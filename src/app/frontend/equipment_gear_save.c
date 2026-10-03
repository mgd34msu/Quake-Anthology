#include "equipment_gear_private.h"
#include "equipment_gear_save.h"
#include "save_private.h"

static equipment_gear_content *content_at(const qa_frontend *frontend, size_t ordinal)
{
    equipment_gear_content *owner = frontend && frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
    while (owner && ordinal) { owner = owner->next; --ordinal; }
    return owner;
}
static bool namespace_fields(qa_source_save_io *io, qa_actor_owner *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_strings *strings = qa_session_strings(io->session);
    char *name = !reading && *owner <= UINT32_MAX ? (char *)qa_strings_cstr(strings, (qa_string_id)*owner) : NULL;
    if (!reading && (!name || !*name)) return false;
    bool okay = frontend_save_text(io, &name);
    if (reading) {
        *owner = name ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
        free(name);
    }
    return okay && *owner;
}
static bool header(qa_source_save_io *io, const char expected[4])
{
    uint8_t magic[4]; memcpy(magic, expected, 4); return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, expected, 4) &&
        true;
}
static bool exact_profile(qa_source_save_io *io, const char *expected)
{
    char *profile = io->direction == QA_SOURCE_SAVE_WRITE ? (char *)expected : NULL;
    bool okay = frontend_save_text(io, &profile) && profile && !strcmp(profile, expected);
    if (io->direction == QA_SOURCE_SAVE_READ) free(profile);
    return okay;
}
static bool identity(qa_source_save_io *io, const frontend_equipment_gear_owner_view *view)
{
    qa_actor_owner owner = view->source.owner, selected = view->source.selected_owner,
        service = view->source.service_owner;
    uint32_t product = view->product;
    return namespace_fields(io, &owner) && owner == view->source.owner &&
        namespace_fields(io, &selected) && selected == view->source.selected_owner &&
        namespace_fields(io, &service) && service == view->source.service_owner &&
        qa_source_save_u32(io, &product) && product == (uint32_t)view->product &&
        exact_profile(io, view->definition->id);
}
static bool current_content(const equipment_gear_content *owner)
{
    qa_application_equipment_content source; const application_q3_grapple_definition *definition = NULL;
    return owner && frontend_equipment_gear_source(owner->frontend, owner->view.source.owner,
        &source, &definition, NULL) && definition == owner->view.definition &&
        qa_application_equipment_content_current(owner->frontend->application, &owner->view.source);
}
bool frontend_equipment_gear_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size ||
        (frontend->equipment_gear && frontend->equipment_gear->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear topology requires its actual idle frontend capture");
    qa_source_save_io io = {0}; size_t count = frontend_equipment_gear_count(frontend);
    bool okay = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        header(&io, "QFGT") && qa_source_save_count(&io, &count, SIZE_MAX);
    for (const equipment_gear_content *owner = frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
        okay && owner; owner = owner->next) {
        size_t visual = 0; frontend_visual_owner_view actual;
        for (; visual < frontend_visual_owner_count(frontend); ++visual) {
            if (!frontend_visual_owner_read(frontend, visual, &actual)) { okay = false; break; }
            if (actual.owner == owner->view.content.owner && actual.family == QA_SCENE_Q3 &&
                actual.mounts == owner->view.content.mounts && actual.images == owner->view.content.images &&
                actual.materials == owner->view.content.materials) break;
        }
        okay = okay && frontend_equipment_gear_quiet(owner) && !owner->restoring && current_content(owner) &&
            visual < frontend_visual_owner_count(frontend) && identity(&io, &owner->view) &&
            qa_source_save_count(&io, &visual, SIZE_MAX);
    }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Gear topology leaves its genuine runtime or visual heaps");
    return okay;
}
bool frontend_equipment_gear_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->equipment_gear)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear topology import requires an empty isolated owner");
    frontend_equipment_gear *equipment = calloc(1, sizeof(*equipment));
    if (!equipment) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing genuine gear topology");
    qa_source_save_io io = {0}; size_t count = 0;
    bool okay = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        header(&io, "QFGT") && qa_source_save_count(&io, &count, bytes.size / 29);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_actor_owner namespace = 0, selected = 0, service = 0; uint32_t product = 0;
        char *profile = NULL; size_t visual = 0;
        qa_application_equipment_content source = {0};
        const application_q3_grapple_definition *definition = NULL; frontend_visual_owner_view content;
        okay = namespace_fields(&io, &namespace) && namespace_fields(&io, &selected) &&
            namespace_fields(&io, &service) && qa_source_save_u32(&io, &product) && product <= QA_Q3_TEAM_ARENA &&
            frontend_save_text(&io, &profile) && profile &&
            qa_source_save_count(&io, &visual, SIZE_MAX) &&
            frontend_equipment_gear_source(frontend, namespace, &source, &definition, error) &&
            source.selected_owner == selected && source.service_owner == service && !strcmp(profile, definition->id) &&
            frontend_visual_owner_read(frontend, visual, &content) && content.owner == selected && content.family == QA_SCENE_Q3;
        free(profile);
        for (const equipment_gear_content *prior = equipment->contents; okay && prior; prior = prior->next)
            if (prior->view.source.owner == namespace) okay = false;
        equipment_gear_content *owner = NULL;
        if (okay) okay = frontend_equipment_gear_content_create(frontend, &source, definition, &content,
            (qa_q3_product)product, &owner, error);
        if (okay) {
            owner->restoring = true;
            if (equipment->tail) equipment->tail->next = owner; else equipment->contents = owner;
            equipment->tail = owner;
        }
    }
    okay = okay && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (!okay) {
        while (equipment->contents) {
            equipment_gear_content *row = equipment->contents; equipment->contents = row->next;
            frontend_equipment_gear_content_dispose(row);
        }
        free(equipment);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid genuine gear topology");
        return false;
    }
    frontend->equipment_gear = equipment; return true;
}
static bool blob(qa_source_save_io *io, qa_buffer *owned, qa_bytes *bytes)
{
    size_t count = io->direction == QA_SOURCE_SAVE_WRITE ? owned->size : 0;
    if (!qa_source_save_count(io, &count, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, owned->data, count);
    *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count; return true;
}
bool frontend_equipment_gear_checkpoint(const qa_frontend *frontend, size_t ordinal,
    const q3n_selected_media_refs *refs, qa_buffer *out, qa_error *error)
{
    equipment_gear_content *owner = content_at(frontend, ordinal);
    if (!frontend_equipment_gear_quiet(owner) || owner->restoring || !current_content(owner) ||
        !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear continuation requires its actual capture and source");
    qa_buffer media = {0}, weapons = {0}; qa_source_save_io io = {0}; size_t count = 0;
    for (const frontend_equipment_gear_presenter *row = owner->presenters; row; row = row->next) ++count;
    bool okay = q3n_selected_authored_checkpoint(owner->view.media, refs, &media, error) &&
        q3n_weapons_checkpoint(owner->weapons, &weapons, error) &&
        qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        header(&io, "QFGS") && identity(&io, &owner->view) && blob(&io, &media, NULL) &&
        blob(&io, &weapons, NULL) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (const frontend_equipment_gear_presenter *row = owner->presenters; okay && row; row = row->next) {
        qa_actor_id actor = row->actor; q3n_selected_weapon_state state = row->state;
        okay = qa_source_save_actor(&io, &actor) && actor.registry && q3n_selected_weapon_state_fields(&io, &state);
    }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    qa_buffer_free(&media); qa_buffer_free(&weapons); return okay;
}
bool frontend_equipment_gear_restore(qa_frontend *frontend, size_t ordinal,
    const q3n_selected_media_refs *refs, qa_bytes bytes, qa_error *error)
{
    equipment_gear_content *owner = content_at(frontend, ordinal);
    if (!frontend_equipment_gear_quiet(owner) || !owner->restoring || owner->presenters || !current_content(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear continuation import requires its actual empty retained row");
    qa_source_save_io io = {0}; size_t count = 0; qa_bytes media = {0}, weapons = {0};
    bool okay = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        header(&io, "QFGS") && identity(&io, &owner->view) && blob(&io, NULL, &media) &&
        blob(&io, NULL, &weapons) && qa_source_save_count(&io, &count, bytes.size / 64);
    frontend_equipment_gear_presenter *head = NULL, *tail = NULL;
    for (size_t i = 0; okay && i < count; ++i) {
        frontend_equipment_gear_presenter *row = calloc(1, sizeof(*row));
        if (!row) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Restoring actual gear presenter"); break; }
        if (tail) tail->next = row; else head = row; tail = row; row->owner = owner;
        okay = qa_source_save_actor(&io, &row->actor) && row->actor.registry &&
            q3n_selected_weapon_state_fields(&io, &row->state);
        for (const frontend_equipment_gear_presenter *prior = head; okay && prior != row; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, row->actor)) okay = false;
    }
    okay = okay && qa_source_save_finish(&io, NULL);
    if (okay) okay = q3n_selected_authored_restore(owner->view.media, refs, media, error) &&
        q3n_weapons_restore(owner->weapons, weapons, error);
    qa_source_save_dispose(&io);
    if (!okay) {
        while (head) { frontend_equipment_gear_presenter *next = head->next; free(head); head = next; }
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid gear presenter continuation");
        return false;
    }
    owner->presenters = head; owner->tail = tail; owner->restoring = false; return true;
}
bool frontend_equipment_gear_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !frontend_equipment_gear_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear qualification requires its idle imported owners");
    for (const equipment_gear_content *row = frontend->equipment_gear ? frontend->equipment_gear->contents : NULL;
        row; row = row->next) {
        qa_q3_product product;
        if (row->restoring || !current_content(row) ||
            !qa_application_equipment_q3_product_read(frontend->application, row->view.source.selected_owner, &product, error) ||
            product != row->view.product)
            return frontend_fail(error, QA_ERROR_FORMAT, "Gear topology omitted its actual source or private continuation");
    }
    return true;
}
bool frontend_equipment_gear_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    return owned && destination && owned != destination && !destination->equipment_gear &&
        frontend_equipment_gear_idle(owned) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Gear rebind requires an idle isolated owner and empty destination");
}
void frontend_equipment_gear_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (!owned || !destination || owned == destination) return;
    destination->equipment_gear = owned->equipment_gear; owned->equipment_gear = NULL;
    for (equipment_gear_content *row = destination->equipment_gear ? destination->equipment_gear->contents : NULL;
        row; row = row->next) row->frontend = destination;
}

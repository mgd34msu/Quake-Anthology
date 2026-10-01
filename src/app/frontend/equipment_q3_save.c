#include "equipment_q3_private.h"
#include "equipment_q3_save.h"
#include "save_private.h"
#include "qa/persistence_content.h"
#include "qa/q3_assets_save.h"

static equipment_q3_content *content_at(const qa_frontend *frontend, size_t ordinal)
{
    equipment_q3_content *owner = frontend && frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
    while (owner && ordinal) { owner = owner->next; --ordinal; }
    return owner;
}
static bool quiet(const equipment_q3_content *owner)
{
    if (!owner || owner->admitting || !q3n_selected_media_idle(owner->view.media) ||
        !q3n_weapons_idle(owner->weapons)) return false;
    for (const frontend_equipment_q3_presenter *row = owner->presenters; row; row = row->next)
        if (row->users) return false;
    return true;
}
static bool provider_fields(qa_source_save_io *io, qa_actor_owner *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_strings *strings = qa_session_strings(io->session);
    char *name = !reading && *owner <= UINT32_MAX ? (char *)qa_strings_cstr(strings, (qa_string_id)*owner) : NULL;
    if (!reading && (!*owner || !name || !*name)) return false;
    bool ok = frontend_save_text(io, &name);
    if (reading) {
        *owner = name ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
        free(name);
    }
    return ok && *owner;
}
static bool topology_fields(qa_source_save_io *io, qa_actor_owner *provider,
    qa_q3_product *product, size_t *visual)
{
    uint32_t value = *product;
    if (!provider_fields(io, provider) || !qa_source_save_u32(io, &value) || value > QA_Q3_TEAM_ARENA ||
        !qa_source_save_count(io, visual, SIZE_MAX)) return false;
    *product = (qa_q3_product)value; return true;
}
bool frontend_equipment_q3_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size ||
        (frontend->equipment_q3 && frontend->equipment_q3->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 topology requires its idle actual frontend capture");
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','Q','T'}; uint32_t schema = 1;
    size_t count = frontend_equipment_q3_count(frontend);
    bool ok = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_u32(&io, &schema) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (const equipment_q3_content *owner = frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
        ok && owner; owner = owner->next) {
        size_t visual = 0; frontend_visual_owner_view actual;
        for (; visual < frontend_visual_owner_count(frontend); ++visual) {
            if (!frontend_visual_owner_read(frontend, visual, &actual)) { ok = false; break; }
            if (actual.owner == owner->view.provider && actual.family == QA_SCENE_Q3 &&
                actual.mounts == owner->view.content.mounts && actual.images == owner->view.content.images &&
                actual.materials == owner->view.content.materials) break;
        }
        qa_actor_owner provider = owner->view.provider; qa_q3_product product = owner->view.product;
        ok = ok && quiet(owner) && !owner->restoring && visual < frontend_visual_owner_count(frontend) &&
            topology_fields(&io, &provider, &product, &visual);
    }
    ok = ok && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 topology leaves its actual content heaps");
    return ok;
}
bool frontend_equipment_q3_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->equipment_q3)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 topology import requires an empty candidate owner");
    frontend_equipment_q3 *equipment = calloc(1, sizeof(*equipment));
    if (!equipment) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing selected Q3 topology owner");
    qa_source_save_io io = {0}; uint8_t magic[4]; uint32_t schema = 0; size_t count = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFQT", 4) &&
        qa_source_save_u32(&io, &schema) && schema == 1 && qa_source_save_count(&io, &count, bytes.size / 14);
    for (size_t i = 0; ok && i < count; ++i) {
        equipment_q3_content *owner = calloc(1, sizeof(*owner));
        if (!owner) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Preparing actual selected Q3 registry row"); break; }
        if (equipment->tail) equipment->tail->next = owner; else equipment->contents = owner;
        equipment->tail = owner; owner->frontend = frontend; owner->restoring = true;
        size_t visual = 0;
        ok = topology_fields(&io, &owner->view.provider, &owner->view.product, &visual) &&
            frontend_visual_owner_read(frontend, visual, &owner->view.content) &&
            owner->view.content.owner == owner->view.provider && owner->view.content.family == QA_SCENE_Q3;
        for (const equipment_q3_content *prior = equipment->contents; ok && prior != owner; prior = prior->next)
            if (prior->view.provider == owner->view.provider) ok = false;
        qa_q3_presentation_asset_options assets = {.provider = {.mounts = owner->view.content.mounts,
            .images = owner->view.content.images, .materials = owner->view.content.materials, .family = QA_SCENE_Q3}};
        if (ok) ok = qa_q3_presentation_assets_create(&assets, &owner->view.assets, error);
        q3n_selected_media_options media = {.content = owner->view.content.mounts,
            .assets = owner->view.assets, .product = owner->view.product};
        if (ok) ok = q3n_selected_media_create(&media, &owner->view.media, error);
        q3n_weapon_options weapons = {.product = owner->view.product, .assets = owner->view.assets};
        if (ok) ok = q3n_weapons_create(&weapons, &owner->weapons, error);
    }
    ok = ok && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (!ok) {
        while (equipment->contents) {
            equipment_q3_content *row = equipment->contents; equipment->contents = row->next;
            frontend_equipment_q3_content_dispose(row);
        }
        free(equipment);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected Q3 content topology");
        return false;
    }
    frontend->equipment_q3 = equipment; return true;
}
static bool view_encode(void *context, const qa_vfs *view, uint64_t *out, qa_error *error)
{
    uint64_t id = qa_application_content_view_id(context, view);
    if (!id) return frontend_fail(error, QA_ERROR_FORMAT, "Selected animation view is outside its actual content graph");
    *out = id; return true;
}
static bool view_decode(void *context, uint64_t id, qa_vfs **out, qa_error *error)
{
    qa_vfs *view = qa_application_content_view(context, id);
    if (!view) return frontend_fail(error, QA_ERROR_FORMAT, "Selected animation view is absent from the decoded content graph");
    *out = view; return true;
}
static bool resource_encode(void *context, const qa_resource *resource, uint64_t *pool,
    uint64_t *version, qa_error *error)
{
    return qa_application_content_resource_id(context, resource, pool, version) ||
        frontend_fail(error, QA_ERROR_FORMAT, "Selected animation resource leaves its actual retained content pool");
}
static bool resource_decode(void *context, uint64_t pool, uint64_t version,
    const qa_resource **out, qa_error *error)
{
    const qa_resource *resource = qa_application_content_resource(context, pool, version);
    if (!resource) return frontend_fail(error, QA_ERROR_FORMAT, "Selected animation resource is absent from the decoded pool");
    *out = resource; return true;
}
static q3n_selected_media_refs media_refs(qa_application_content_graph *graph)
{
    return (q3n_selected_media_refs){.context = graph, .view_encode = view_encode, .view_decode = view_decode,
        .resource_encode = resource_encode, .resource_decode = resource_decode};
}
static bool blob(qa_source_save_io *io, qa_buffer *owned, qa_bytes *bytes)
{
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? owned->size : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, owned->data, size);
    *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
bool frontend_equipment_q3_checkpoint(const qa_frontend *frontend, size_t ordinal,
    qa_buffer *out, qa_error *error)
{
    equipment_q3_content *owner = content_at(frontend, ordinal);
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!quiet(owner) || owner->restoring || !graph || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 continuation requires its real capture and content graph");
    q3n_selected_media_refs refs = media_refs(graph); qa_buffer media = {0}, weapons = {0};
    bool ok = q3n_selected_media_checkpoint(owner->view.media, &refs, &media, error) &&
        q3n_weapons_checkpoint(owner->weapons, &weapons, error);
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','Q','S'}; uint32_t schema = 1;
    qa_actor_owner provider = owner->view.provider; uint32_t product = owner->view.product; size_t count = 0;
    for (const frontend_equipment_q3_presenter *row = owner->presenters; row; row = row->next) ++count;
    ok = ok && qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_u32(&io, &schema) && provider_fields(&io, &provider) &&
        qa_source_save_u32(&io, &product) && blob(&io, &media, NULL) && blob(&io, &weapons, NULL) &&
        qa_source_save_count(&io, &count, SIZE_MAX);
    for (const frontend_equipment_q3_presenter *row = owner->presenters; ok && row; row = row->next) {
        qa_actor_id actor = row->actor; q3n_selected_weapon_state state = row->state;
        ok = qa_source_save_actor(&io, &actor) && actor.registry && q3n_selected_weapon_state_fields(&io, &state);
    }
    ok = ok && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    qa_buffer_free(&media); qa_buffer_free(&weapons); return ok;
}
bool frontend_equipment_q3_restore(qa_frontend *frontend, size_t ordinal, qa_bytes bytes, qa_error *error)
{
    equipment_q3_content *owner = content_at(frontend, ordinal);
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!quiet(owner) || !owner->restoring || owner->presenters || !graph)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 continuation import requires its empty actual row");
    qa_source_save_io io = {0}; uint8_t magic[4]; uint32_t schema = 0, product = 0;
    qa_actor_owner provider = 0; qa_bytes media = {0}, weapons = {0}; size_t count = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFQS", 4) && qa_source_save_u32(&io, &schema) && schema == 1 &&
        provider_fields(&io, &provider) && provider == owner->view.provider &&
        qa_source_save_u32(&io, &product) && product == (uint32_t)owner->view.product &&
        blob(&io, NULL, &media) && blob(&io, NULL, &weapons) && qa_source_save_count(&io, &count, bytes.size / 64);
    frontend_equipment_q3_presenter *head = NULL, *tail = NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        frontend_equipment_q3_presenter *row = calloc(1, sizeof(*row));
        if (!row) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Restoring genuine full actor selected presenter"); break; }
        if (tail) tail->next = row; else head = row; tail = row; row->owner = owner;
        ok = qa_source_save_actor(&io, &row->actor) && row->actor.registry && q3n_selected_weapon_state_fields(&io, &row->state);
        for (const frontend_equipment_q3_presenter *prior = head; ok && prior != row; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, row->actor)) ok = false;
    }
    ok = ok && qa_source_save_finish(&io, NULL);
    q3n_selected_media_refs refs = media_refs(graph);
    if (ok) ok = q3n_selected_media_restore(owner->view.media, &refs, media, error) &&
        q3n_weapons_restore(owner->weapons, weapons, error);
    qa_source_save_dispose(&io);
    if (!ok) {
        while (head) { frontend_equipment_q3_presenter *next = head->next; free(head); head = next; }
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected Q3 presenter continuation");
        return false;
    }
    owner->presenters = head; owner->tail = tail; owner->restoring = false; return true;
}
bool frontend_equipment_q3_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !frontend_equipment_q3_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 topology requires its actual idle imported owners");
    for (const equipment_q3_content *owner = frontend->equipment_q3 ? frontend->equipment_q3->contents : NULL;
        owner; owner = owner->next) {
        qa_q3_product product;
        if (owner->restoring || !qa_application_equipment_q3_product_read(frontend->application,
                owner->view.provider, &product, error) || product != owner->view.product)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected Q3 topology omits a real provider or private continuation");
    }
    return true;
}
bool frontend_equipment_q3_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    return owned && destination && owned != destination && !destination->equipment_q3 &&
        frontend_equipment_q3_idle(owned) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 owner rebind requires an idle isolated source and empty destination");
}
void frontend_equipment_q3_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (!owned || !destination || owned == destination) return;
    destination->equipment_q3 = owned->equipment_q3; owned->equipment_q3 = NULL;
    for (equipment_q3_content *owner = destination->equipment_q3 ? destination->equipment_q3->contents : NULL;
        owner; owner = owner->next) owner->frontend = destination;
}

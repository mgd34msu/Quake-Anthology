#include "image_inventory.h"
#include "save_private.h"
#include "qa/scene_resource_save.h"
#include "qa/persistence_content.h"

typedef struct image_owner {
    qa_scene_resources *images;
    uint32_t kind;
    uint64_t ordinal, identity, view;
} image_owner;
typedef struct image_inventory { image_owner *entries; qa_scene_resources **owners; size_t count; } image_inventory;
static void dispose(image_inventory *inventory)
{ free(inventory->entries); free(inventory->owners); *inventory = (image_inventory){0}; }
static bool add(image_inventory *inventory, qa_application_content_graph *graph,
    const qa_scene_resources *images, uint32_t kind, uint64_t ordinal, uint64_t identity, qa_error *error)
{
    if (!images) return true;
    uint64_t view = qa_application_content_view_id(graph, qa_scene_resources_files(images));
    if (!view) return frontend_fail(error, QA_ERROR_FORMAT, "image owner view is outside the actual content graph");
    for (size_t i = 0; i < inventory->count; ++i)
        if (inventory->entries[i].images == images)
            return frontend_fail(error, QA_ERROR_FORMAT, "frontend image owner has duplicate destructor authority");
    if (inventory->count == SIZE_MAX / sizeof(*inventory->entries) || inventory->count == SIZE_MAX / sizeof(*inventory->owners))
        return frontend_fail(error, QA_ERROR_MEMORY, "image owner inventory overflows storage");
    image_owner *entries = realloc(inventory->entries, (inventory->count + 1) * sizeof(*entries));
    if (!entries) return frontend_fail(error, QA_ERROR_MEMORY, "allocating image owner inventory");
    inventory->entries = entries;
    qa_scene_resources **owners = realloc(inventory->owners, (inventory->count + 1) * sizeof(*owners));
    if (!owners) return frontend_fail(error, QA_ERROR_MEMORY, "allocating image codec owner array");
    inventory->owners = owners;
    entries[inventory->count] = (image_owner){(qa_scene_resources *)images, kind, ordinal, identity, view};
    owners[inventory->count++] = (qa_scene_resources *)images; return true;
}
static bool collect(qa_frontend *f, image_inventory *inventory, qa_error *error)
{
    if (!f || !f->application || f->stepping || !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "image graph requires actual idle frontend owners");
    qa_application_content_graph *graph = qa_application_content_graph_read(f->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "image graph requires the actual content graph lease");
    bool ok = add(inventory, graph, f->ui_images, 0, 0, 0, error) && add(inventory, graph, f->images, 1, 0, 0, error);
    for (size_t i = 0; ok && i < frontend_source_group_count(f); ++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f, i, &group)) return frontend_fail(error, QA_ERROR_ARGUMENT, "source image heap is not fully constructed");
        ok = group.images && add(inventory, graph, group.images, 2, i, group.identity, error);
    }
    for (uint32_t kind = 3; ok && kind <= 5; ++kind) {
        for (size_t i = 0; ok; ++i) {
            const qa_scene_resources *images = kind == 3 ? frontend_event_images_at(f, i) :
                kind == 4 ? frontend_visual_images_at(f, i) : frontend_native_q2_images_at(f, i);
            if (!images) break;
            ok = add(inventory, graph, images, kind, i, 0, error);
        }
    }
    return ok;
}
static bool header(qa_source_save_io *io, const image_inventory *inventory)
{
    uint8_t magic[4] = {'Q','F','I','M'}; uint32_t version = 1; size_t count = inventory->count;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFIM", 4) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(image_owner)) || count != inventory->count) return false;
    for (size_t i = 0; i < count; ++i) {
        image_owner saved = inventory->entries[i];
        if (!qa_source_save_u32(io, &saved.kind) || !qa_source_save_u64(io, &saved.ordinal) ||
            !qa_source_save_u64(io, &saved.identity) || !qa_source_save_u64(io, &saved.view) ||
            saved.kind != inventory->entries[i].kind || saved.ordinal != inventory->entries[i].ordinal ||
            saved.identity != inventory->entries[i].identity || saved.view != inventory->entries[i].view) return false;
    }
    return true;
}
bool frontend_images_checkpoint(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size) return frontend_fail(error, QA_ERROR_ARGUMENT, "image capture requires empty output");
    image_inventory inventory = {0}; qa_buffer images = {0}; qa_source_save_io io = {0};
    bool ok = collect(f, &inventory, error) &&
        (!inventory.count || qa_scene_images_checkpoint((const qa_scene_resources *const *)inventory.owners, inventory.count, &images, error)) &&
        qa_source_save_writer(&io, qa_application_session(f->application), error) && header(&io, &inventory);
    size_t count = images.size;
    ok = ok && qa_source_save_count(&io, &count, SIZE_MAX) && qa_source_save_bytes(&io, images.data, count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&images); dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "image owner topology is not completely qualified");
    return ok;
}
bool frontend_images_restore(qa_frontend *f, qa_bytes bytes, qa_scene_image_set **out, qa_error *error)
{
    if (!out || *out) return frontend_fail(error, QA_ERROR_ARGUMENT, "image restore requires empty construction-reference output");
    image_inventory inventory = {0}; qa_source_save_io io = {0}; size_t size = 0;
    bool ok = collect(f, &inventory, error) && qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        header(&io, &inventory) && qa_source_save_count(&io, &size, bytes.size);
    qa_bytes images = {0};
    if (ok) {
        if (io.offset > bytes.size || size > bytes.size - io.offset) ok = false;
        else { images = (qa_bytes){bytes.data + io.offset, size}; io.offset += size; }
    }
    ok = ok && qa_source_save_finish(&io, NULL) && (inventory.count ?
        qa_scene_images_restore(inventory.owners, inventory.count, images, out, error) : images.size == 0);
    qa_source_save_dispose(&io); dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "saved image topology differs from prepared actual owners");
    return ok;
}
bool frontend_image_index(qa_frontend *f, const qa_scene_image *image, uint64_t *out, qa_error *error)
{
    if (!image || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "image reference requires an actual version and output");
    image_inventory inventory = {0}; size_t index = 0;
    bool ok = collect(f, &inventory, error) && qa_scene_image_owner_index((const qa_scene_resources *const *)inventory.owners,
        inventory.count, image, &index);
    if (ok) *out = index;
    dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "retained image is outside the actual frontend owner graph");
    return ok;
}

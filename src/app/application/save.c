#include "save_private.h"
#include "guest_qc_internal.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/launch_identity.h"

#include <string.h>
#include <stdlib.h>

void application_save_foundation_free(application_save_foundation *value)
{
    if (value == NULL) return;
    qa_strings_destroy(value->strings);
    qa_actor_checkpoint_free(&value->actors);
    qa_session_checkpoint_free(&value->session);
    qa_world_checkpoint_free(&value->world);
    *value = (application_save_foundation){0};
}

bool application_save_foundation_capture(qa_application *application, qa_buffer out[4],
                                         qa_error *error)
{
    if (application == NULL || out == NULL || application->session == NULL ||
        application->world == NULL || !qa_session_safe(application->session) ||
        !qa_world_idle(application->world) || application->publication_started ||
        application->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT, "foundation capture requires an idle healthy committed application");
    uint64_t generation = application->publication_generation;
    uint64_t revision = qa_actors_revision(qa_session_actors(application->session));
    application_save_foundation value = {0};
    qa_buffer buffers[4] = {{0}};
    bool ok = qa_actors_checkpoint(qa_session_actors(application->session), &value.actors, error) &&
        qa_session_checkpoint_capture(application->session, &value.session, error) &&
        qa_world_checkpoint_capture(application->world, &value.world, error) &&
        qa_save_strings_encode(qa_session_strings(application->session), &buffers[0], error) &&
        qa_save_actors_encode(&value.actors, &buffers[1], error) &&
        qa_save_session_encode(&value.session, &buffers[2], error) &&
        qa_save_world_encode(&value.world, &buffers[3], error);
    if (ok && (generation != application->publication_generation ||
               revision != qa_actors_revision(qa_session_actors(application->session)) ||
               !qa_session_safe(application->session) || !qa_world_idle(application->world)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "foundation owners changed during capture");
    application_save_foundation_free(&value);
    if (!ok) {
        for (size_t i = 0; i < 4; ++i) qa_buffer_free(&buffers[i]);
        return false;
    }
    memcpy(out, buffers, sizeof(buffers));
    return true;
}

static const qa_save_record *foundation_record(const qa_save_image *image,
    qa_save_owner_kind kind, const char *schema, qa_error *error)
{
    const qa_save_record *record = qa_save_image_find(image, kind, "");
    if (record == NULL || strcmp(record->owner.schema, schema) ||
        record->owner.schema_version != 1 || record->owner.backend[0] != '\0') {
        application_fail(error, QA_ERROR_FORMAT, "foundation record schema or backend disagrees");
        return NULL;
    }
    return record;
}

bool application_save_foundation_decode(const qa_save_image *image,
    application_save_foundation *out, qa_error *error)
{
    if (image == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "foundation decode requires a complete save image");
    const qa_save_record *strings = foundation_record(image, QA_SAVE_STRINGS, "qa.strings", error);
    const qa_save_record *actors = foundation_record(image, QA_SAVE_ACTORS, "qa.actors", error);
    const qa_save_record *session = foundation_record(image, QA_SAVE_SESSION, "qa.session", error);
    const qa_save_record *world = foundation_record(image, QA_SAVE_WORLD, "qa.world", error);
    if (!strings || !actors || !session || !world) return false;
    application_save_foundation value = {0};
    bool ok = qa_save_strings_decode(strings->payload, &value.strings, error) &&
        qa_save_actors_decode(actors->payload, &value.actors, error) &&
        qa_save_session_decode(session->payload, &value.session, error) &&
        qa_save_world_decode(world->payload, &value.world, error);
    if (ok && (value.actors.capacity != value.session.actor_capacity ||
               value.session.elapsed_ns != qa_save_image_metadata(image)->elapsed_ns))
        ok = application_fail(error, QA_ERROR_FORMAT, "foundation actor capacity or session time disagrees with save image");
    if (!ok) { application_save_foundation_free(&value); return false; }
    *out = value;
    return true;
}

bool application_save_session_create(const qa_session_options *callbacks,
    const qa_save_image *image, qa_session **out, qa_error *error)
{
    if (callbacks == NULL || image == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "restore requires session callbacks and complete image");
    application_save_foundation value = {0};
    if (!application_save_foundation_decode(image, &value, error)) return false;
    qa_session_options options = *callbacks;
    options.actor_capacity = value.session.actor_capacity;
    options.component_capacity = value.session.component_capacity;
    options.mixed_order = value.session.mixed_order;
    bool ok = qa_session_create_restored(&options, &value.actors, value.strings, out, error);
    if (ok) value.strings = NULL;
    application_save_foundation_free(&value);
    return ok;
}

static bool restore_think(void *context, qa_actor_owner owner, qa_actor_id actor,
    uint32_t callback_id, qa_think_fn *callback, void **binding, qa_error *error)
{
    qa_application *candidate = context;
    for (size_t i = 0; i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (provider->owner != owner) continue;
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_think_binding(provider->state.q1, actor, callback_id, callback, binding, error);
        if (provider->kind == APPLICATION_PROVIDER_QC)
            return application_qc_think_binding(provider, actor, callback_id, callback, binding, error);
        return application_fail(error, QA_ERROR_FORMAT, "saved scheduler callback has no producer in the selected provider");
    }
    return application_fail(error, QA_ERROR_FORMAT, "saved scheduler callback names an absent selected provider");
}

bool application_save_foundation_finish(qa_application *candidate,
    const application_save_foundation *value, qa_error *error)
{
    if (candidate == NULL || value == NULL || candidate->world == NULL ||
        candidate->session == NULL || !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "foundation restore requires an isolated prepared candidate");
    if (!qa_world_checkpoint_restore(candidate->world, &value->world, error) ||
        !qa_session_checkpoint_restore(candidate->session, &value->session, restore_think, candidate, error))
        return false;
    return true;
}

#define APPLICATION_CHECKPOINT_HEADER 212u

static const char *saved_product(qa_application *application, qa_product_id id, qa_error *error)
{
    if (!id) return "";
    const qa_product *product = qa_catalog_product(application->catalog, id);
    if (!product || !product->identity) {
        application_fail(error, QA_ERROR_FORMAT, "application map product has no stable catalog identity");
        return NULL;
    }
    return product->identity;
}

bool application_save_metadata_capture(qa_application *application, qa_buffer *out,
                                        qa_error *error)
{
    if (!application || !out || !application->session || !qa_session_safe(application->session) ||
        application->publication_started || application->destroy_requested || application->finalizing ||
        application->state == QA_APPLICATION_FAULTED || application->state == QA_APPLICATION_STOPPING ||
        application->routing_snapshot || application->routing_providers || application->routing_provider_count ||
        application->pending_close || application->mode_count > UINT32_MAX ||
        (application->mode_count && !application->mode_ids) || application->random.front >= 31 ||
        application->random.rear >= 31)
        return application_fail(error, QA_ERROR_ARGUMENT, "application metadata capture requires a committed safe point");
    const char *geometry = saved_product(application, application->map_geometry, error);
    const char *presentation = saved_product(application, application->map_presentation, error);
    if (!geometry || !presentation) return false;
    size_t geometry_length = strlen(geometry), presentation_length = strlen(presentation);
    size_t size = APPLICATION_CHECKPOINT_HEADER;
    if (application->mode_count > (SIZE_MAX - size) / 12 || geometry_length > UINT32_MAX ||
        presentation_length > UINT32_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "application checkpoint extent is exhausted");
    size += application->mode_count * 12;
    if (geometry_length > SIZE_MAX - size || presentation_length > SIZE_MAX - size - geometry_length)
        return application_fail(error, QA_ERROR_MEMORY, "application product identities exceed checkpoint extent");
    size += geometry_length + presentation_length;
    qa_buffer buffer = {.data = malloc(size), .size = size};
    if (!buffer.data) return application_fail(error, QA_ERROR_MEMORY, "cannot encode application metadata");
    qa_net_writer writer;
    qa_net_writer_init(&writer, buffer.data, size, error);
    qa_net_write_data(&writer, "QAAP", 4); qa_net_write_u32(&writer, 1);
    qa_net_write_u64(&writer, application->catalog_generation);
    qa_net_write_u64(&writer, application->publication_generation);
    qa_net_write_u64(&writer, application->command_generation); qa_net_write_u64(&writer, application->map_revision);
    qa_net_write_u32(&writer, application->current_map); qa_net_write_u32(&writer, application->state);
    uint32_t flags = (application->discover_mods ? 1u : 0u) | (application->physics_ready ? 2u : 0u) |
        (application->primary_mode_ready ? 4u : 0u) | (application->map_view_ready ? 8u : 0u) |
        (application->map_force_reload ? 16u : 0u);
    qa_net_write_u32(&writer, flags); qa_net_write_u32(&writer, application->primary_mode.slot);
    qa_net_write_u64(&writer, application->primary_mode.generation);
    qa_net_write_u32(&writer, (uint32_t)application->mode_count);
    for (size_t i = 0; i < 31; ++i) qa_net_write_u32(&writer, application->random.words[i]);
    qa_net_write_u8(&writer, application->random.front); qa_net_write_u8(&writer, application->random.rear);
    qa_net_write_u16(&writer, 0); qa_net_write_u64(&writer, application->random.draws);
    qa_net_write_u32(&writer, (uint32_t)geometry_length); qa_net_write_u32(&writer, (uint32_t)presentation_length);
    qa_net_write_data(&writer, geometry, geometry_length); qa_net_write_data(&writer, presentation, presentation_length);
    for (size_t i = 0; i < application->mode_count; ++i) {
        qa_net_write_u32(&writer, application->mode_ids[i].slot);
        qa_net_write_u64(&writer, application->mode_ids[i].generation);
    }
    if (writer.failed || qa_net_writer_size(&writer) != size) {
        qa_buffer_free(&buffer);
        return application_fail(error, QA_ERROR_FORMAT, "application metadata codec size disagrees");
    }
    *out = buffer;
    return true;
}

static bool restore_product(qa_application *candidate, qa_net_reader *reader,
    uint32_t length, qa_product_id *out, qa_error *error)
{
    if (length > qa_net_reader_remaining(reader) || (uint64_t)length + 1 > SIZE_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "invalid saved application product extent");
    char *identity = malloc((size_t)length + 1);
    if (!identity) return application_fail(error, QA_ERROR_MEMORY, "cannot restore application product identity");
    bool ok = qa_net_read_data(reader, identity, length) && memchr(identity, '\0', length) == NULL;
    identity[length] = '\0';
    qa_product_id id = 0;
    if (ok && length) {
        const qa_product *product = qa_catalog_find(candidate->catalog, identity);
        ok = product && !strcmp(product->identity, identity);
        if (ok) id = product->id;
    }
    free(identity);
    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "saved application product is absent from candidate catalog");
    *out = id;
    return true;
}

bool application_save_metadata_restore(qa_application *candidate, qa_bytes bytes,
                                        qa_error *error)
{
    if (!candidate || !candidate->session || !candidate->catalog || !qa_session_safe(candidate->session) ||
        !bytes.data || bytes.size < APPLICATION_CHECKPOINT_HEADER || memcmp(bytes.data, "QAAP", 4))
        return application_fail(error, QA_ERROR_ARGUMENT, "metadata restore requires an isolated prepared candidate");
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    uint32_t version = qa_net_read_u32(&reader);
    uint64_t catalog = qa_net_read_u64(&reader), publication = qa_net_read_u64(&reader);
    uint64_t commands = qa_net_read_u64(&reader), revision = qa_net_read_u64(&reader);
    qa_string_id map = qa_net_read_u32(&reader);
    qa_application_state state = (qa_application_state)qa_net_read_u32(&reader);
    uint32_t flags = qa_net_read_u32(&reader);
    qa_mode_id primary = {.slot = qa_net_read_u32(&reader)};
    primary.generation = qa_net_read_u64(&reader);
    uint32_t count = qa_net_read_u32(&reader);
    qa_builtin_random random;
    for (size_t i = 0; i < 31; ++i) random.words[i] = qa_net_read_u32(&reader);
    random.front = qa_net_read_u8(&reader); random.rear = qa_net_read_u8(&reader);
    uint16_t reserved = qa_net_read_u16(&reader); random.draws = qa_net_read_u64(&reader);
    uint32_t geometry_length = qa_net_read_u32(&reader), presentation_length = qa_net_read_u32(&reader);
    size_t remaining = qa_net_reader_remaining(&reader);
    if (reader.failed || version != 1 || reserved || (flags & ~31u) || !commands ||
        (state != QA_APPLICATION_READY && state != QA_APPLICATION_RUNNING) ||
        random.front >= 31 || random.rear >= 31 ||
        (map && !qa_strings_text(qa_session_strings(candidate->session), map).data) ||
        geometry_length > remaining || presentation_length > remaining - geometry_length ||
        (uint64_t)count * 12 != remaining - geometry_length - presentation_length)
        return application_fail(error, QA_ERROR_FORMAT, "invalid saved application metadata fields");
    qa_product_id geometry, presentation;
    if (!restore_product(candidate, &reader, geometry_length, &geometry, error) ||
        !restore_product(candidate, &reader, presentation_length, &presentation, error)) return false;
    qa_mode_id *ids = count ? calloc(count, sizeof(*ids)) : NULL;
    if (count && !ids) return application_fail(error, QA_ERROR_MEMORY, "cannot restore ordered application mode identities");
    bool has_primary = false;
    for (size_t i = 0; i < count; ++i) {
        ids[i].slot = qa_net_read_u32(&reader); ids[i].generation = qa_net_read_u64(&reader);
        if (ids[i].slot == primary.slot && ids[i].generation == primary.generation) has_primary = true;
        for (size_t j = 0; j < i; ++j)
            if (ids[j].slot == ids[i].slot && ids[j].generation == ids[i].generation) reader.failed = true;
    }
    if (!qa_net_reader_finish(&reader) || ((flags & 4u) && !has_primary)) {
        free(ids);
        return application_fail(error, QA_ERROR_FORMAT, "application primary or ordered mode identities disagree");
    }
    /* Candidate-only publication: all allocations and product resolution have
     * succeeded. Mode codec validation still checks these actual identities. */
    free(candidate->mode_ids); candidate->mode_ids = ids; candidate->mode_count = count;
    candidate->catalog_generation = catalog; candidate->publication_generation = publication;
    candidate->command_generation = commands; candidate->map_revision = revision;
    candidate->current_map = map; candidate->state = state; candidate->primary_mode = primary;
    candidate->random = random; candidate->map_geometry = geometry; candidate->map_presentation = presentation;
    candidate->discover_mods = (flags & 1u) != 0; candidate->physics_ready = (flags & 2u) != 0;
    candidate->primary_mode_ready = (flags & 4u) != 0; candidate->map_view_ready = (flags & 8u) != 0;
    candidate->map_force_reload = (flags & 16u) != 0;
    return true;
}

bool application_save_configuration_capture(qa_application *application, qa_buffer *out,
                                             qa_error *error)
{
    if (!application || !application->session || !qa_session_safe(application->session) ||
        application->publication_started)
        return application_fail(error, QA_ERROR_ARGUMENT, "configuration capture requires a committed safe point");
    return qa_launch_identity_encode(qa_application_launch(application),
                                      qa_session_actors(application->session), out, error);
}

bool application_save_configuration_decode(qa_application *candidate, qa_bytes bytes,
                                             qa_launch_draft **out, qa_error *error)
{
    if (!candidate || !candidate->session || !candidate->catalog ||
        !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "configuration decode requires a restored actor namespace");
    return qa_launch_identity_decode(candidate->catalog, qa_session_actors(candidate->session), bytes, out, error);
}

bool application_save_configuration_validate(qa_application *candidate, qa_bytes bytes,
                                               qa_error *error)
{
    if (!candidate || !candidate->session || !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "configuration validation requires an isolated prepared candidate");
    return qa_launch_identity_match(qa_application_launch(candidate), qa_session_actors(candidate->session), bytes, error);
}

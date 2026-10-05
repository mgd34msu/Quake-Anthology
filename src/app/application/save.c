#include "save_private.h"
#include "startup_flow.h"
#include "startup_program.h"
#include "control_frame.h"
#include "guest_qc_internal.h"
#include "guest_qc_objectives.h"
#include "guest_native_q2_private.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q3_save.h"
#include "qa/modes_save.h"
#include "qa/equipment_save.h"
#include "qa/launch_identity.h"
#include "qa/launch_save.h"
#include "qa/cvars_save.h"
#include "qa/persistence_fields.h"
#include "qa/persistence_gameplay.h"
#include "qa/binary.h"
#include "qa/native_process.h"
#include "guest_checkpoint.h"
#include "map_players_private.h"
#include "map_travel_private.h"
#include "supplies.h"
#include "bots_save_private.h"
#include "match_intents.h"
#include "network_q1_signon.h"
#include "save_native_q2_record.h"
#include "portals.h"
#include "guest_q3_save.h"
#include "save_content.h"
#include "save_progression.h"
#include "rankings.h"
#include "events_save.h"
#include "native_q3_console.h"
#include "native_q1_console.h"
#include "native_q1_wire.h"
#include "native_q2_checkpoint.h"
#include "native_q2_callbacks.h"
#include "native_q3_ipfilters.h"
#include "native_q3_settings.h"
#include "native_q3_team_status.h"
#include "native_q3_votes.h"
#include "native_q3_wire_state.h"
#include "native_q3_checkpoint.h"
#include "q3_product.h"
#include "qa/map_sidecars.h"
#include "guest_q3_components.h"
#include "bots_npc.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static bool controls_fields(qa_source_save_io *io, qa_application *app,
                             application_control_record *record)
{
    uint32_t mode = record->player_mode;
    uint32_t rounding = record->numeric.rounding;
    uint32_t prediction_rounding = record->prediction_numeric.rounding;
    qa_actor_owner cutscene = record->cutscene_character ? record->cutscene_character->owner : 0;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_actor(io, &record->actor) || !qa_actors_get(qa_session_actors(app->session), record->actor) ||
        !qa_source_save_string(io, &cutscene) || !qa_persistence_movement(io, &record->state) ||
        !qa_persistence_movement_profile(io, &record->profile) || record->state.kind != record->profile.kind ||
        !qa_source_save_string(io, &record->numeric.id) || !qa_source_save_bool(io, &record->numeric.native_c) ||
        !qa_source_save_u32(io, &record->numeric.radix) || !qa_source_save_u32(io, &record->numeric.scalar_mantissa_bits) ||
        !qa_source_save_u32(io, &record->numeric.double_mantissa_bits) ||
        !qa_source_save_i32(io, &record->numeric.evaluation_method) || !qa_source_save_u32(io, &rounding) ||
        rounding > QA_APPLICATION_ROUND_ZERO || !qa_source_save_bool(io, &record->numeric.qw_origin_binary64) ||
        !qa_source_save_string(io, &record->prediction_numeric.id) || !qa_source_save_bool(io, &record->prediction_numeric.native_c) ||
        !qa_source_save_u32(io, &record->prediction_numeric.radix) || !qa_source_save_u32(io, &record->prediction_numeric.scalar_mantissa_bits) ||
        !qa_source_save_u32(io, &record->prediction_numeric.double_mantissa_bits) ||
        !qa_source_save_i32(io, &record->prediction_numeric.evaluation_method) || !qa_source_save_u32(io, &prediction_rounding) ||
        prediction_rounding > QA_APPLICATION_ROUND_ZERO || !qa_source_save_bool(io, &record->prediction_numeric.qw_origin_binary64) ||
        !qa_persistence_movement_result(io, &record->result) ||
        !qa_persistence_bounds(io, &record->standing_bounds) || !qa_persistence_bounds(io, &record->bounds) ||
        !qa_persistence_ground(io, &record->ground) ||
        !qa_source_save_vec3(io, &record->view_angles) || !qa_source_save_vec3(io, &record->command_angles) ||
        !qa_source_save_vec3(io, &record->view_offset) || !qa_source_save_vec3(io, &record->saved_view_offset) ||
        !qa_source_save_vec3(io, &record->q2r_pml_origin) ||
        !qa_source_save_u64(io, &record->command_sequence) || !qa_source_save_u32(io, &record->buttons) ||
        !qa_source_save_u32(io, &record->previous_buttons) || !qa_source_save_i32(io, &record->water_level) ||
        !qa_source_save_i32(io, &record->water_type) || !qa_source_save_f32(io, &record->view_height) ||
        !qa_source_save_f32(io, &record->gravity_multiplier) || !qa_source_save_u32(io, &mode) || mode > QA_MOVEMENT_MODE_FREEZE ||
        !qa_source_save_i32(io, &record->saved_mode) || !qa_source_save_bool(io, &record->flight) ||
        !qa_source_save_bool(io, &record->cutscene) || !qa_source_save_bool(io, &record->saved_damageable) ||
        !qa_source_save_bool(io, &record->saved_mode_valid) || !qa_source_save_bool(io, &record->command_seen) ||
        !qa_source_save_bool(io, &record->guest_mode_valid) || !qa_source_save_bool(io, &record->player_mode_set) ||
        !qa_source_save_i32(io, &record->guest_mode)) return false;
    record->numeric.rounding = (qa_application_numeric_rounding)rounding;
    record->prediction_numeric.rounding = (qa_application_numeric_rounding)prediction_rounding;
    if (!application_control_numeric_current(app, record->actor, &record->numeric, io->error) ||
        !application_control_prediction_numeric_current(app, record->actor, &record->prediction_numeric, io->error)) return false;
    if (!qa_vec_finite(record->view_angles) || !qa_vec_finite(record->command_angles) ||
        !qa_vec_finite(record->view_offset) || !qa_vec_finite(record->saved_view_offset) ||
        !qa_vec_finite(record->q2r_pml_origin) || !isfinite(record->view_height) ||
        !isfinite(record->gravity_multiplier) || record->gravity_multiplier < 0 ||
        (record->result.actor.registry && !qa_actor_id_equal(record->result.actor, record->actor)))
        return application_fail(io->error, QA_ERROR_FORMAT, "saved control continuation contains invalid actor or view state");
    if (reading) {
        record->application = app; record->player_mode = (qa_movement_mode)mode; record->active = true;
        if (cutscene) {
            for (size_t i = 0; i < app->provider_count; ++i)
                if (app->providers[i]->owner == cutscene) record->cutscene_character = app->providers[i];
            if (!record->cutscene_character)
                return application_fail(io->error, QA_ERROR_FORMAT, "saved cinematic character provider is unavailable");
        }
    }
    return true;
}

static bool motion_fields(qa_source_save_io *io, qa_application *app, application_motion_record *record)
{
    uint32_t reason = record->reason;
    if (!qa_source_save_actor(io, &record->actor) || !qa_actors_get(qa_session_actors(app->session), record->actor) ||
        !qa_persistence_body(io, &record->body) || !qa_source_save_vec3(io, &record->view_angles) ||
        !qa_source_save_vec3(io, &record->angular_kick) || !qa_source_save_u64(io, &record->hold_until_ns) ||
        !qa_source_save_u64(io, &record->revision) || !record->revision ||
        !qa_source_save_u32(io, &reason) || reason > QA_BUILTIN_MOTION_RESET ||
        !qa_source_save_bool(io, &record->force_view_angles) || !qa_source_save_bool(io, &record->apply_angular_kick)) return false;
    if (!qa_vec_finite(record->view_angles) || !qa_vec_finite(record->angular_kick))
        return application_fail(io->error, QA_ERROR_FORMAT, "saved motion continuation has a nonfinite view");
    record->reason = (qa_builtin_motion_reason)reason; record->active = true; return true;
}

static bool controls_signature(qa_source_save_io *io)
{
    unsigned char actual[8] = {'Q','A','C','T','R','L','S',0};
    static const unsigned char expected[8] = {'Q','A','C','T','R','L','S',0};
    return qa_source_save_bytes(io, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual));
}

static bool application_controls_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, app->session, error)) return false;
    size_t count = 0;
    for (uint32_t i = 0; i < app->control_capacity; ++i) if (app->controls[i].active) ++count;
    bool ok = controls_signature(&io) && qa_source_save_count(&io, &count, app->control_capacity);
    for (uint32_t i = 0; ok && i < app->control_capacity; ++i) {
        application_control_record copy = app->controls[i];
        if (copy.moving || copy.retired) ok = application_fail(error, QA_ERROR_ARGUMENT, "control capture cannot interrupt movement retirement");
        else if (copy.active) ok = controls_fields(&io, app, &copy);
    }
    count = 0;
    for (uint32_t i = 0; i < app->motion_capacity; ++i) if (app->motion[i].active) ++count;
    if (ok) ok = qa_source_save_count(&io, &count, app->motion_capacity);
    for (uint32_t i = 0; ok && i < app->motion_capacity; ++i) if (app->motion[i].active) {
        application_motion_record copy = app->motion[i]; ok = motion_fields(&io, app, &copy);
    }
    if (ok) ok = application_control_frames_fields(&io, app, app->controls, NULL, error);
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "invalid control continuation");
    qa_source_save_dispose(&io); return ok;
}

static bool application_controls_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    struct application_control_frames *frames = NULL;
    application_control_record *controls = calloc(app->control_capacity, sizeof(*controls));
    application_motion_record *motion = calloc(app->motion_capacity, sizeof(*motion));
    if (!controls || !motion) { free(controls); free(motion); return application_fail(error, QA_ERROR_MEMORY, "allocating saved control continuations"); }
    qa_source_save_io io = {0}; size_t count = 0;
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && controls_signature(&io) &&
        qa_source_save_count(&io, &count, app->control_capacity);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        application_control_record record = {0};
        ok = controls_fields(&io, app, &record) && record.actor.slot < app->control_capacity && (!i || record.actor.slot > previous);
        if (ok) { previous = record.actor.slot; controls[record.actor.slot] = record; }
        else qa_movement_result_free(&record.result);
    }
    if (ok) ok = qa_source_save_count(&io, &count, app->motion_capacity);
    previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        application_motion_record record = {0};
        ok = motion_fields(&io, app, &record) && record.actor.slot < app->motion_capacity && (!i || record.actor.slot > previous);
        if (ok) { previous = record.actor.slot; motion[record.actor.slot] = record; }
    }
    if (ok) ok = application_control_frames_fields(&io, app, controls, &frames, error);
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok) {
        application_control_record *old_controls = app->controls; application_motion_record *old_motion = app->motion;
        app->controls = controls; app->motion = motion; controls = old_controls; motion = old_motion;
        struct application_control_frames *old_frames = app->control_frames;
        app->control_frames = frames; frames = old_frames;
    }
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "invalid saved control continuation");
    for (uint32_t i = 0; i < app->control_capacity; ++i) qa_movement_result_free(&controls[i].result);
    application_control_frames_free(frames);
    free(controls); free(motion); qa_source_save_dispose(&io); return ok;
}

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
        record->owner.backend[0] != '\0') {
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
    qa_session *session = NULL;
    bool ok = qa_session_create_restored(&options, &value.actors, value.strings, &session, error);
    if (ok) {
        value.strings = NULL;
        ok = qa_session_restore_elapsed(session, value.session.elapsed_ns, error);
        if (ok) *out = session;
        else (void)qa_session_destroy(session, NULL);
    }
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

static bool restore_qc_bindings(void *context, qa_error *error)
{
    qa_application *candidate = context;
    for (size_t i = 0; i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_QC &&
            !application_qc_bind_entities(provider, error)) return false;
    }
    return true;
}

bool application_save_foundation_finish(qa_application *candidate,
    const application_save_foundation *value, qa_error *error)
{
    if (candidate == NULL || value == NULL || candidate->world == NULL ||
        candidate->session == NULL || !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "foundation restore requires an isolated prepared candidate");
    if (!qa_world_checkpoint_restore(candidate->world, &value->world,
            restore_qc_bindings, candidate, error) ||
        !qa_session_checkpoint_restore(candidate->session, &value->session, restore_think, candidate, error))
        return false;
    return true;
}

#define APPLICATION_CHECKPOINT_HEADER 216u

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
    qa_net_write_data(&writer, "QAAP", 4);
    qa_net_write_u64(&writer, application->catalog_generation);
    qa_net_write_u64(&writer, application->publication_generation);
    qa_net_write_u64(&writer, application->command_generation); qa_net_write_u64(&writer, application->map_revision);
    qa_net_write_u64(&writer, application->frame_revision);
    qa_net_write_u32(&writer, application->current_map); qa_net_write_u32(&writer, application->state);
    uint32_t flags = (application->discover_mods ? 1u : 0u) | (application->physics_ready ? 2u : 0u) |
        (application->primary_mode_ready ? 4u : 0u) | (application->map_view_ready ? 8u : 0u) |
        (application->map_force_reload ? 16u : 0u) | (application->q1_paused ? 32u : 0u);
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
    uint64_t catalog = qa_net_read_u64(&reader), publication = qa_net_read_u64(&reader);
    uint64_t commands = qa_net_read_u64(&reader), revision = qa_net_read_u64(&reader);
    uint64_t frame_revision = qa_net_read_u64(&reader);
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
    if (reader.failed || reserved || (flags & ~63u) || !commands ||
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
    if (!candidate->native_restore_current) {
        candidate->catalog_generation = catalog; candidate->publication_generation = publication;
        candidate->command_generation = commands; candidate->frame_revision = frame_revision;
        candidate->random = random; candidate->discover_mods = (flags & 1u) != 0;
    }
    candidate->map_revision = revision;
    candidate->current_map = map; candidate->state = state; candidate->primary_mode = primary;
    candidate->map_geometry = geometry; candidate->map_presentation = presentation;
    candidate->physics_ready = (flags & 2u) != 0;
    candidate->primary_mode_ready = (flags & 4u) != 0; candidate->map_view_ready = (flags & 8u) != 0;
    candidate->map_force_reload = (flags & 16u) != 0;
    candidate->q1_paused = (flags & 32u) != 0;
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
    qa_catalog *catalog = qa_application_content_catalog(candidate->content_graph,
        application_save_content_launch_catalog(candidate->content_graph));
    if (!catalog)
        return application_fail(error, QA_ERROR_FORMAT, "configuration requires its retained launch catalog");
    return qa_launch_identity_decode(catalog, qa_session_actors(candidate->session), bytes, out, error);
}

bool application_save_configuration_validate(qa_application *candidate, qa_bytes bytes,
                                               qa_error *error)
{
    if (!candidate || !candidate->session || !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "configuration validation requires an isolated prepared candidate");
    return qa_launch_identity_match(qa_application_launch(candidate), qa_session_actors(candidate->session), bytes, error);
}

typedef struct application_persistence {
    qa_application *active;
    qa_world *world;
    qa_application **slot;
    qa_application *displaced;
    qa_application *retained;
    const qa_application_options *options;
    const qa_application_persistence_ops *ops;
    const qa_save_image *image;
    qa_persistence_gameplay_resolvers resolvers;
    qa_save_owner *owners;
    size_t owner_count;
    qa_buffer foundation[4];
    qa_buffer configuration;
    qa_buffer progression;
    qa_application_content_graph *content_graph;
    application_startup_program_roster *programs;
    uint64_t configuration_generation, publication_generation, actor_revision;
    qa_save_purpose purpose;
    bool leased;
} application_persistence;

static bool persistence_restore_provider_clocks(application_persistence *,
    qa_application *, qa_error *);

static bool persistence_idle(qa_application *app, application_operation expected)
{
    return app && app->operation == expected && !app->client_preparation &&
        !app->q3_round_active && !app->frame_preparing && app->session &&
        app->configuration && qa_session_safe(app->session) &&
        (!app->world || qa_world_idle(app->world)) && qa_combat_idle(app->combat) &&
        application_guests_idle(app) && application_bots_can_destroy(app) &&
        application_rankings_idle(app) &&
        !app->publication_started && !app->destroy_requested && !app->finalizing &&
        !app->pending_close && !app->routing_snapshot && !app->routing_providers &&
        !app->routing_provider_count &&
        (app->state == QA_APPLICATION_READY || app->state == QA_APPLICATION_RUNNING);
}

static bool persistence_safe(qa_application *app)
{
    return persistence_idle(app, APPLICATION_IDLE) && app->world && qa_application_launch(app);
}

static bool persistence_lease(application_persistence *operation, qa_error *error)
{
    bool restoring = operation->image != NULL;
    if (!(restoring ? persistence_idle(operation->active, APPLICATION_IDLE) : persistence_safe(operation->active)))
        return application_fail(error, QA_ERROR_ARGUMENT, restoring
            ? "application restore requires an idle engine session"
            : "application persistence requires a committed idle session");
    operation->configuration_generation = qa_configuration_generation(operation->active->configuration);
    operation->publication_generation = operation->active->publication_generation;
    operation->actor_revision = qa_actors_revision(qa_session_actors(operation->active->session));
    operation->world = operation->active->world;
    operation->active->operation = APPLICATION_PERSISTING;
    operation->leased = true;
    return true;
}

static bool persistence_unchanged(application_persistence *operation, qa_error *error)
{
    qa_application *app = operation->active;
    if (!operation->leased || !persistence_idle(app, APPLICATION_PERSISTING) || app->world != operation->world ||
        (!operation->image && (!app->world || !qa_application_launch(app))) ||
        qa_configuration_generation(app->configuration) != operation->configuration_generation ||
        app->publication_generation != operation->publication_generation ||
        qa_actors_revision(qa_session_actors(app->session)) != operation->actor_revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "application changed during persistence operation");
    return true;
}

static void persistence_end(void *opaque)
{
    application_persistence *operation = opaque;
    for (size_t i = 0; i < 4; ++i) qa_buffer_free(operation->foundation + i);
    qa_buffer_free(&operation->configuration);
    qa_buffer_free(&operation->progression);
    if (operation->content_graph) {
        if (operation->active->capture_content_graph == operation->content_graph)
            operation->active->capture_content_graph = NULL;
        application_save_content_destroy(operation->content_graph);
        operation->content_graph = NULL;
    }
    free(operation->owners); operation->owners = NULL;
    if (operation->leased) {
        operation->active->operation = APPLICATION_IDLE;
        operation->leased = false;
    }
}

static const char *shared_schema(qa_save_owner_kind kind)
{
    switch (kind) {
    case QA_SAVE_STRINGS: return "qa.strings";
    case QA_SAVE_RESOURCES: return "qa.content-manifest";
    case QA_SAVE_ACTORS: return "qa.actors";
    case QA_SAVE_SESSION: return "qa.session";
    case QA_SAVE_WORLD: return "qa.world";
    case QA_SAVE_CONFIGURATION: return "qa.configuration";
    case QA_SAVE_ROSTER: return "qa.roster";
    case QA_SAVE_CVARS: return "qa.cvars";
    case QA_SAVE_APPLICATION: return "qa.application";
    case QA_SAVE_COMBAT: return "qa.combat";
    case QA_SAVE_INVENTORY: return "qa.inventory";
    case QA_SAVE_PICKUPS: return "qa.pickups";
    case QA_SAVE_TARGETS: return "qa.targets";
    case QA_SAVE_CONTROLS: return "qa.controls";
    case QA_SAVE_MODES: return "qa.modes";
    case QA_SAVE_EQUIPMENT: return "qa.equipment";
    case QA_SAVE_EVENTS: return "qa.events.application";
    case QA_SAVE_NAVIGATION: return "qa.navigation.application";
    case QA_SAVE_BOTS: return "qa.bots.application";
    case QA_SAVE_PROGRESSION: return "qa.progression.application";
    default: return NULL;
    }
}

static const char *provider_schema(const application_provider *provider)
{
    if (provider->kind == APPLICATION_PROVIDER_Q1) return "qa.q1.native";
    if (provider->kind == APPLICATION_PROVIDER_Q2) return "qa.native-q2-continuation";
    if (provider->kind == APPLICATION_PROVIDER_Q3) return "qa.q3.native";
    if (provider->kind == APPLICATION_PROVIDER_QC) return "qa.qc.guest";
    if (provider->kind == APPLICATION_PROVIDER_QVM) return "qa.q3.qvm";
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine)
        return "qa.q3.external-native";
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine &&
        provider->state.native.q2_engine->profile != QA_NATIVE_Q2_CGAME_API2023)
        return "qa.q2.external-native";
    return NULL;
}

static application_provider *saved_provider(qa_application *app, const char *instance)
{
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->launch &&
            !strcmp(app->providers[i]->launch->selection.instance, instance)) return app->providers[i];
    return NULL;
}

static bool same_owner(const qa_save_owner *a, const qa_save_owner *b)
{
    return a->kind == b->kind &&
        a->instance && b->instance && !strcmp(a->instance, b->instance) &&
        a->schema && b->schema && !strcmp(a->schema, b->schema) &&
        a->backend && b->backend && !strcmp(a->backend, b->backend);
}

static const qa_application_persistence_owner *external_owner(
    const qa_application_persistence_ops *ops, const qa_save_owner *owner)
{
    for (size_t i = 0; i < ops->owner_count; ++i)
        if (same_owner(&ops->owners[i].identity, owner)) return ops->owners + i;
    return NULL;
}

static bool configuration_fields(qa_bytes bytes, qa_configuration_checkpoint *state,
    qa_bytes *identity, qa_error *error)
{
    if (!bytes.data || bytes.size < 20 || memcmp(bytes.data, "QACF", 4) ||
        !qa_load_u64le(bytes.data + 4) ||
        qa_load_u64le(bytes.data + 12) != bytes.size - 20)
        return application_fail(error, QA_ERROR_FORMAT, "invalid application configuration continuation");
    *state = (qa_configuration_checkpoint){.generation = qa_load_u64le(bytes.data + 4), .has_current = true};
    *identity = (qa_bytes){bytes.data + 20, bytes.size - 20};
    return true;
}

static bool configuration_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    qa_configuration_checkpoint checkpoint;
    qa_buffer identity = {0};
    if (!qa_configuration_checkpoint_capture(app->configuration, &checkpoint, error) ||
        !checkpoint.has_current || !checkpoint.generation)
        return application_fail(error, QA_ERROR_ARGUMENT, "application save requires a committed configuration");
    if (!application_save_configuration_capture(app, &identity, error)) return false;
    if (identity.size > SIZE_MAX - 20) {
        qa_buffer_free(&identity);
        return application_fail(error, QA_ERROR_MEMORY, "configuration continuation extent overflow");
    }
    qa_buffer bytes = {.data = malloc(identity.size + 20), .size = identity.size + 20};
    if (!bytes.data) {
        qa_buffer_free(&identity);
        return application_fail(error, QA_ERROR_MEMORY, "allocating configuration continuation");
    }
    memcpy(bytes.data, "QACF", 4); qa_store_u64le(bytes.data + 4, checkpoint.generation);
    qa_store_u64le(bytes.data + 12, identity.size);
    memcpy(bytes.data + 20, identity.data, identity.size); qa_buffer_free(&identity);
    *out = bytes;
    return true;
}

bool application_campaign_fields(qa_source_save_io *io, qa_application *app, bool departed)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_campaign_unit_checkpoint checkpoint={0};
    bool ok=reading || departed || qa_campaign_unit_capture(app->campaign_unit,&checkpoint,io->error);
    size_t maximum=SIZE_MAX/sizeof(*checkpoint.worlds);
    if (reading && io->offset<=io->input.size && (io->input.size-io->offset)/26<maximum)
        maximum=(io->input.size-io->offset)/26;
    if (ok) ok=qa_source_save_bool(io,&checkpoint.has_current) &&
        qa_source_save_string(io,&checkpoint.current.content) &&
        qa_source_save_string(io,&checkpoint.current.map) &&
        qa_source_save_count(io,&checkpoint.count,maximum);
    if (ok && reading && checkpoint.count) {
        checkpoint.worlds=calloc(checkpoint.count,sizeof(*checkpoint.worlds));
        if (!checkpoint.worlds) ok=application_fail(io->error,QA_ERROR_MEMORY,"allocating departed campaign handles");
    }
    for (size_t i=0;ok && i<checkpoint.count;++i) {
        qa_campaign_location location=reading?(qa_campaign_location){0}:
            qa_campaign_world_location(checkpoint.worlds[i]);
        qa_bytes bytes=reading?(qa_bytes){0}:qa_campaign_world_bytes(checkpoint.worlds[i]);
        size_t size=bytes.size;
        ok=qa_source_save_string(io,&location.content) && qa_source_save_string(io,&location.map) &&
            qa_source_save_count(io,&size,SIZE_MAX);
        if (ok && reading) {
            ok=qa_source_save_span(io,size,&bytes);
            if (ok) ok=qa_campaign_world_create(location,bytes,&checkpoint.worlds[i],io->error);
        } else if (ok) ok=qa_source_save_bytes(io,(void *)bytes.data,size);
    }
    if (ok && reading) ok=qa_campaign_unit_restore(app->campaign_unit,&checkpoint,io->error);
    qa_campaign_unit_checkpoint_free(&checkpoint);
    if (!ok) io->failed=true;
    return ok;
}

static bool application_capture(qa_application *app, qa_save_purpose purpose, qa_buffer *out, qa_error *error)
{
    enum { PART_COUNT = 10, HEADER_SIZE = 84 };
    qa_buffer parts[PART_COUNT] = {0};
    application_match_intents *empty = NULL;
    application_match_intents *intents = app->match_intents;
    if (intents == NULL)
        intents = empty = application_match_intents_create(error);
    bool ok = intents != NULL &&
        application_save_metadata_capture(app, parts, error) &&
        application_map_checkpoint_capture(app, purpose==QA_SAVE_TRANSITION, parts + 1, error) &&
        application_physics_capture(app, parts + 2, error) &&
        application_match_intents_capture(intents, app, parts + 3, error) &&
        application_q1_signon_capture(app, parts + 4, error) &&
        application_portals_capture(app, parts + 5, error);
    qa_source_save_io policy_io;
    if (ok && purpose!=QA_SAVE_TRANSITION) {
        ok = qa_source_save_writer(&policy_io, NULL, error) &&
            application_q3_product_fields(&policy_io, &app->q3_product) &&
            qa_source_save_finish(&policy_io, parts + 6);
        qa_source_save_dispose(&policy_io);
    }
    if (ok && purpose!=QA_SAVE_TRANSITION) {
        ok = qa_source_save_writer(&policy_io, NULL, error) &&
            application_startup_fields(&policy_io, app) &&
            qa_source_save_finish(&policy_io, parts + 7);
        qa_source_save_dispose(&policy_io);
    }
    if (ok && purpose!=QA_SAVE_TRANSITION) ok = qa_map_sidecars_checkpoint(app->map_sidecars,
        qa_application_content_graph_read(app), parts + 8, error);
    if (ok) ok = application_q3_components_checkpoint(app, parts + 9, error);
    size_t size = HEADER_SIZE;
    for (size_t i = 0; ok && i < PART_COUNT; ++i) {
        if ((!parts[i].size && (i<6 || i>8)) || parts[i].size > SIZE_MAX - size)
            ok = application_fail(error, QA_ERROR_MEMORY, "application continuation extent overflow");
        else
            size += parts[i].size;
    }
    qa_buffer bytes = {0};
    if (ok) {
        bytes.size = size; bytes.data = malloc(bytes.size);
        if (!bytes.data) ok = application_fail(error, QA_ERROR_MEMORY, "allocating application continuation");
    }
    if (ok) {
        memcpy(bytes.data, "QAAO", 4); size_t offset = HEADER_SIZE;
        for (size_t i = 0; i < PART_COUNT; ++i) {
            qa_store_u64le(bytes.data + 4 + i * 8, parts[i].size);
            if (parts[i].size) memcpy(bytes.data + offset, parts[i].data, parts[i].size);
            offset += parts[i].size;
        }
        *out = bytes;
    }
    for (size_t i = 0; i < PART_COUNT; ++i)
        qa_buffer_free(parts + i);
    application_match_intents_destroy(empty);
    return ok;
}

static bool application_parts(qa_bytes bytes, qa_bytes parts[10], qa_error *error)
{
    enum { PART_COUNT = 10, HEADER_SIZE = 84 };
    if (!bytes.data || bytes.size < HEADER_SIZE || memcmp(bytes.data, "QAAO", 4))
        return application_fail(error, QA_ERROR_FORMAT, "invalid application continuation header");
    size_t offset = HEADER_SIZE;
    for (size_t i = 0; i < PART_COUNT; ++i) {
        uint64_t length = qa_load_u64le(bytes.data + 4 + i * 8);
        if ((!length && (i<6 || i>8)) || length > bytes.size - offset)
            return application_fail(error, QA_ERROR_FORMAT, "invalid application continuation extents");
        parts[i] = (qa_bytes){bytes.data + offset, (size_t)length};
        offset += (size_t)length;
    }
    if (offset != bytes.size)
        return application_fail(error, QA_ERROR_FORMAT, "invalid application continuation extents");
    return true;
}

static bool q3_product_decode(qa_bytes bytes, qa_q3_product_policy *policy, qa_error *error)
{
    qa_source_save_io io;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) &&
        application_q3_product_fields(&io, policy) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    return okay;
}

bool application_save_q3_product_decode(const qa_save_image *image,
    qa_q3_product_policy *policy, qa_error *error)
{
    const qa_save_record *record = qa_save_image_find(image, QA_SAVE_APPLICATION, "");
    qa_bytes parts[10];
    if (!record || strcmp(record->owner.schema, "qa.application") ||
        record->owner.backend[0] ||
        !application_parts(record->payload, parts, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved Q3 product policy has no qualified application owner");
    return q3_product_decode(parts[6], policy, error);
}

bool application_save_startup_decode(const qa_save_image *image,
    qa_application *app, qa_error *error)
{
    const qa_save_record *record = qa_save_image_find(image, QA_SAVE_APPLICATION, "");
    qa_bytes parts[10];
    if (!record ||
        !application_parts(record->payload, parts, error)) return false;
    qa_source_save_io io;
    bool okay = qa_source_save_reader(&io, NULL, parts[7], error) &&
        application_startup_fields(&io, app) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    return okay;
}

bool application_save_sidecars_decode(const qa_save_image *image,
    qa_application *app, qa_error *error)
{
    if (qa_save_image_metadata(image)->purpose==QA_SAVE_TRANSITION) return true;
    const qa_save_record *record = qa_save_image_find(image, QA_SAVE_APPLICATION, "");
    qa_bytes parts[10];
    if (!app || app->map_sidecars || !record || strcmp(record->owner.schema, "qa.application") ||
        record->owner.backend[0] ||
        !application_parts(record->payload, parts, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved map sidecars have no genuine application owner");
    return qa_map_sidecars_create_restored(qa_application_content_graph_read(app), parts[8], &app->map_sidecars, error);
}

bool application_save_components_decode(const qa_save_image *image,
    qa_bytes *out, qa_error *error)
{
    const qa_save_record *record = qa_save_image_find(image, QA_SAVE_APPLICATION, "");
    qa_bytes parts[10];
    if (!out || !record || strcmp(record->owner.schema, "qa.application") ||
        record->owner.backend[0] ||
        !application_parts(record->payload, parts, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved components have no genuine application owner");
    *out = parts[9];
    return true;
}

static bool application_metadata_prepare(qa_application *app,
                                           const qa_save_image *image,
                                           qa_error *error)
{
    const qa_save_record *record = qa_save_image_find(image, QA_SAVE_APPLICATION, "");
    qa_bytes parts[10];
    if (!record || strcmp(record->owner.schema, "qa.application") ||
        record->owner.backend[0] ||
        !application_parts(record->payload, parts, error))
        return application_fail(error, QA_ERROR_FORMAT, "Saved application metadata has no qualified owner");
    return application_save_metadata_restore(app, parts[0], error);
}

static bool application_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    qa_bytes parts[10];
    if (app->match_intents != NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Application continuation already installed");
    if (!application_parts(bytes, parts, error))
        return false;
    if (!app->native_restore_current) {
        qa_q3_product_policy policy = {0};
        if (!q3_product_decode(parts[6], &policy, error)) return false;
        if (policy.prerelease_demo != app->q3_product.prerelease_demo ||
            policy.prerelease_team_arena_demo != app->q3_product.prerelease_team_arena_demo ||
            policy.fs_restrict != app->q3_product.fs_restrict ||
            policy.restriction_resolved != app->q3_product.restriction_resolved ||
            policy.filesystem_restricted != app->q3_product.filesystem_restricted)
            return application_fail(error, QA_ERROR_FORMAT, "Saved application changed its imported initial Q3 policy");
    }
    app->match_intents = application_match_intents_create(error);
    return app->match_intents != NULL &&
        application_save_metadata_restore(app, parts[0], error) &&
        application_map_checkpoint_restore(app, parts[1], error) &&
        application_physics_restore(app, parts[2], error) &&
        application_match_intents_restore(app->match_intents, app, parts[3], error) &&
        application_q1_signon_restore(app, parts[4], error) &&
        application_portals_restore(app, parts[5], error);
}

typedef struct native_q3_record {
    qa_bytes game, wire, settings, ipfilters, votes, registry;
    bool game_present;
    bool console_present, wire_present, settings_bound;
    bool settings_present, settings_initialized;
    bool ipfilters_present, ipfilters_initialized;
    bool votes_present, team_present, team_bound;
} native_q3_record;

static bool native_q3_record_read(qa_bytes bytes, native_q3_record *out, qa_error *error)
{
    native_q3_record value={0};
    qa_bytes *parts[]={&value.game,&value.wire,&value.settings,&value.ipfilters,&value.votes,
        &value.registry};
    const size_t count=sizeof(parts)/sizeof(parts[0]);
    const size_t header=12+count*8;
    if (!bytes.data || bytes.size<header || memcmp(bytes.data,"QAN3",4) ||
        qa_load_u32le(bytes.data+4)>2047 ||
        qa_load_u32le(bytes.data+8))
        return application_fail(error,QA_ERROR_FORMAT,"Invalid native Q3 owner bundle");
    uint32_t flags=qa_load_u32le(bytes.data+4);
    size_t offset=header;
    for (size_t i=0;i<count;++i) {
        uint64_t length=qa_load_u64le(bytes.data+12+i*8);
        if (length>bytes.size-offset)
            return application_fail(error,QA_ERROR_FORMAT,"Native Q3 owner extent exceeds its actual record");
        *parts[i]=(qa_bytes){length ? bytes.data+offset : NULL,(size_t)length}; offset+=(size_t)length;
    }
    if (offset!=bytes.size ||
        ((flags&1u)!=0)!=(value.registry.size!=0) ||
        ((flags&1024u)!=0)!=(value.game.size!=0) || (!(flags&1024u) && (flags&1023u)) ||
        ((flags&2u)!=0)!=(value.wire.size!=0) || ((flags&16u)!=0)!=(value.settings.size!=0) ||
        ((flags&32u)!=0)!=(value.ipfilters.size!=0) || ((flags&32u)!=0 && !(flags&1u)) ||
        ((flags&64u)!=0 && (flags&48u)!=48u) ||
        ((flags&128u)!=0)!=(value.votes.size!=0) || ((flags&128u)!=0 && !(flags&1u)) ||
        ((flags&256u)!=0 && !(flags&1u)) || ((flags&512u)!=0 && (flags&260u)!=260u) ||
        ((flags&8u)!=0 && !(flags&1u)) ||
        ((flags&16u)!=0 && (flags&9u)!=9u) || ((flags&4u)!=0 && !(flags&16u)))
        return application_fail(error,QA_ERROR_FORMAT,"Native Q3 source owner extent or presence differs");
    value.game_present=(flags&1024u)!=0;
    value.console_present=(flags&1u)!=0; value.wire_present=(flags&2u)!=0;
    value.settings_bound=(flags&4u)!=0; value.settings_present=(flags&8u)!=0;
    value.settings_initialized=(flags&16u)!=0; value.ipfilters_present=(flags&32u)!=0;
    value.ipfilters_initialized=(flags&64u)!=0; value.votes_present=(flags&128u)!=0;
    value.team_present=(flags&256u)!=0; value.team_bound=(flags&512u)!=0;
    *out=value;
    return true;
}

static bool native_q3_saved_record(application_provider *provider,
    const qa_save_image *image, native_q3_record *out, qa_error *error)
{
    const qa_save_record *saved=qa_save_image_find(image,QA_SAVE_PROVIDER,
        provider->launch->selection.instance);
    qa_bytes bytes=saved ? saved->payload : (qa_bytes){0};
    if (!saved || strcmp(saved->owner.schema,"qa.q3.native") ||
        saved->owner.backend[0] ||
        !bytes.data || bytes.size<28 || memcmp(bytes.data,"QAPV",4) ||
        qa_load_u32le(bytes.data+4)!=APPLICATION_PROVIDER_Q3 ||
        qa_load_u64le(bytes.data+20)!=bytes.size-28)
        return application_fail(error,QA_ERROR_FORMAT,"Missing actual native Q3 provider record");
    return native_q3_record_read((qa_bytes){bytes.data+28,bytes.size-28},out,error);
}

bool application_native_q3_checkpoint_prepare(application_provider *provider,
    const qa_save_record *saved, qa_error *error)
{
    qa_bytes bytes=saved ? saved->payload : (qa_bytes){0};
    if (!provider || provider->kind!=APPLICATION_PROVIDER_Q3 || !provider->launch ||
        !provider->constructed || provider->attached || !provider->application ||
        provider->application->operation!=APPLICATION_PERSISTING || !saved ||
        saved->owner.kind!=QA_SAVE_PROVIDER || !saved->owner.instance ||
        strcmp(saved->owner.instance,provider->launch->selection.instance) ||
        strcmp(saved->owner.schema,"qa.q3.native") ||
        saved->owner.backend[0] ||
        !bytes.data || bytes.size<28 || memcmp(bytes.data,"QAPV",4) ||
        qa_load_u32le(bytes.data+4)!=APPLICATION_PROVIDER_Q3 ||
        qa_load_u32le(bytes.data+8)>1 || qa_load_u64le(bytes.data+20)!=bytes.size-28)
        return application_fail(error,QA_ERROR_FORMAT,"Missing actual native Q3 GAME constructor record");
    native_q3_record record={0};
    if (!native_q3_record_read((qa_bytes){bytes.data+28,bytes.size-28},&record,error)) return false;
    if (record.game_present!=(provider->state.q3!=NULL))
        return application_fail(error,QA_ERROR_FORMAT,"Saved Q3 GAME presence differs from its selected compiled roles");
    if (record.console_present) {
        qa_console *console = NULL;
        qa_cvars *cvars = NULL;
        qa_command_context command;
        if (!application_native_q3_console_restore(provider,record.registry,error)) return false;
        if (!application_native_q3_console_at(provider, &console, &cvars, &command))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Restored native Q3 console lost its physical Source owner");
        if (!application_startup_source_restore(provider, console, cvars, &command, error)) return false;
    }
    return true;
}

static bool native_q3_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    qa_buffer game={0}, wire={0}, settings={0}, ipfilters={0}, votes={0}, registry={0};
    qa_buffer *parts[]={&game,&wire,&settings,&ipfilters,&votes,&registry};
    const size_t count=sizeof(parts)/sizeof(parts[0]);
    const size_t header=12+count*8;
    bool game_present=provider->state.q3!=NULL;
    bool present=provider->native_q3_wire!=NULL;
    bool console=provider->native_q3_console!=NULL;
    bool bound=application_native_q3_console_settings_bound(provider);
    bool cached=provider->native_q3_settings!=NULL;
    bool initialized=application_native_q3_settings_initialized(provider);
    bool filters=provider->native_q3_ipfilters!=NULL;
    bool filters_initialized=application_native_q3_ipfilters_initialized(provider);
    bool voting=provider->native_q3_votes!=NULL;
    bool team=provider->native_q3_team_status!=NULL;
    bool team_bound=application_native_q3_team_status_bound(provider);
    bool ok=application_native_q3_console_idle(provider) &&
        application_native_q3_settings_idle(provider) &&
        application_native_q3_ipfilters_idle(provider) &&
        application_native_q3_votes_idle(provider) &&
        application_native_q3_team_status_idle(provider);
    if (ok && game_present) ok=qa_q3_game_capture(provider->state.q3,&game,error);
    if (ok && present) ok=application_native_q3_wire_capture(provider,&wire,error) && wire.size;
    if (ok && console) ok=application_native_q3_console_capture(provider,&registry,error) && registry.size;
    if (ok && initialized) ok=application_native_q3_settings_capture(provider,&settings,error) && settings.size;
    if (ok && filters) ok=application_native_q3_ipfilters_capture(provider,&ipfilters,error) && ipfilters.size;
    if (ok && voting) ok=application_native_q3_votes_capture(provider,&votes,error) && votes.size;
    if (ok && ((game_present && !game.size) ||
        (!game_present && (console || present || bound || cached || initialized || filters || voting || team)) ||
        (cached && !console) || (filters && !console) ||
        (voting && !console) || (team && !console) || (team_bound && (!team || !bound)) ||
        (bound && !initialized) || (initialized && !cached) ||
        (filters_initialized && (!filters || !initialized))))
        ok=application_fail(error,QA_ERROR_FORMAT,"Unqualified native Q3 source, settings and wire owners");
    size_t extent=header;
    for (size_t i=0;ok && i<count;++i) {
        if (parts[i]->size>SIZE_MAX-extent)
            ok=application_fail(error,QA_ERROR_FORMAT,"Native Q3 owner extent exceeds its retained storage");
        else extent+=parts[i]->size;
    }
    if (ok) {
        out->size=extent;
        out->data=calloc(1,out->size);
        if (!out->data) { out->size=0; ok=application_fail(error,QA_ERROR_MEMORY,"Retaining native Q3 owner bundle"); }
    }
    if (ok) {
        memcpy(out->data,"QAN3",4); qa_store_u32le(out->data+4,(console ? 1u : 0u)|(present ? 2u : 0u)|(bound ? 4u : 0u)|
            (cached ? 8u : 0u)|(initialized ? 16u : 0u)|(filters ? 32u : 0u)|
            (filters_initialized ? 64u : 0u)|(voting ? 128u : 0u)|
            (team ? 256u : 0u)|(team_bound ? 512u : 0u)|(game_present ? 1024u : 0u));
        size_t offset=header;
        for (size_t i=0;i<count;++i) {
            qa_store_u64le(out->data+12+i*8,parts[i]->size);
            if (parts[i]->size) memcpy(out->data+offset,parts[i]->data,parts[i]->size);
            offset+=parts[i]->size;
        }
    }
    qa_buffer_free(&game); qa_buffer_free(&wire); qa_buffer_free(&settings); qa_buffer_free(&ipfilters); qa_buffer_free(&votes);
    qa_buffer_free(&registry);
    if (!ok && error && error->code==QA_OK)
        application_fail(error,QA_ERROR_ARGUMENT,"Native Q3 owner bundle is borrowed or incomplete");
    return ok;
}

static bool native_q3_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    native_q3_record record={0};
    if (!native_q3_record_read(bytes,&record,error)) return false;
    if (record.game_present!=(provider->state.q3!=NULL) ||
        record.console_present!=(provider->native_q3_console!=NULL) ||
        record.wire_present!=(provider->native_q3_wire!=NULL) ||
        record.settings_present!=(provider->native_q3_settings!=NULL) ||
        record.ipfilters_present!=(provider->native_q3_ipfilters!=NULL) ||
        record.votes_present!=(provider->native_q3_votes!=NULL) ||
        record.team_present!=(provider->native_q3_team_status!=NULL) ||
        application_native_q3_console_settings_bound(provider) ||
        application_native_q3_settings_initialized(provider) ||
        application_native_q3_ipfilters_initialized(provider) ||
        application_native_q3_team_status_bound(provider))
        return application_fail(error,QA_ERROR_FORMAT,"Native Q3 bundle differs from its actual empty candidate services");
    bool ok=(!record.game_present || qa_q3_game_restore(provider->state.q3,record.game,error)) &&
        (!record.settings_initialized || application_native_q3_settings_restore(provider,record.settings,error)) &&
        (!record.ipfilters_present || application_native_q3_ipfilters_restore(provider,record.ipfilters,error)) &&
        (!record.votes_present || application_native_q3_votes_restore(provider,record.votes,error)) &&
        (!record.wire_present || application_native_q3_wire_restore(provider,record.wire,error));
    if (ok && record.ipfilters_initialized!=application_native_q3_ipfilters_initialized(provider))
        return application_fail(error,QA_ERROR_FORMAT,"Native Q3 filters differ from their imported source state");
    return ok;
}

static bool native_q1_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    qa_buffer game = {0}, cvars = {0}, npc = {0};
    bool ok = qa_q1_game_capture(provider->state.q1, &game, error) &&
        application_native_q1_console_capture(provider, &cvars, error) &&
        application_bots_npc_capture(provider, &npc, error);
    if (ok && (!game.size || !cvars.size || !npc.size || game.size > SIZE_MAX - 28 ||
        cvars.size > SIZE_MAX - 28 - game.size || npc.size > SIZE_MAX - 28 - game.size - cvars.size))
        ok = application_fail(error, QA_ERROR_FORMAT, "Invalid native Q1 source owner extent");
    if (ok) {
        out->size = 28 + game.size + cvars.size + npc.size;
        out->data = malloc(out->size);
        if (!out->data) {
            out->size = 0;
            ok = application_fail(error, QA_ERROR_MEMORY, "Retaining native Q1 source owners");
        }
    }
    if (ok) {
        memcpy(out->data, "QAN1", 4); qa_store_u64le(out->data + 4, game.size); qa_store_u64le(out->data + 12, cvars.size);
        qa_store_u64le(out->data + 20, npc.size);
        memcpy(out->data + 28, game.data, game.size);
        memcpy(out->data + 28 + game.size, cvars.data, cvars.size);
        memcpy(out->data + 28 + game.size + cvars.size, npc.data, npc.size);
    }
    qa_buffer_free(&game); qa_buffer_free(&cvars); qa_buffer_free(&npc);
    return ok;
}

static bool native_q1_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    if (!bytes.data || bytes.size < 28 || memcmp(bytes.data, "QAN1", 4))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q1 source owner bundle");
    uint64_t game_size = qa_load_u64le(bytes.data + 4), cvars_size = qa_load_u64le(bytes.data + 12);
    uint64_t npc_size = qa_load_u64le(bytes.data + 20);
    if (!game_size || game_size > bytes.size - 28 || !cvars_size ||
        cvars_size > bytes.size - 28 - (size_t)game_size || !npc_size ||
        npc_size != bytes.size - 28 - (size_t)game_size - (size_t)cvars_size)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q1 source owner lengths");
    qa_q1_restore *ticket = NULL;
    bool ok = application_native_q1_console_restore(provider,
        (qa_bytes){bytes.data + 28 + (size_t)game_size, (size_t)cvars_size}, error) &&
        qa_q1_game_restore_prepare_source(provider->state.q1,
            (qa_bytes){bytes.data + 28, (size_t)game_size}, &ticket, error) &&
        qa_q1_game_restore_commit(ticket, error);
    if (!ok) qa_q1_game_restore_abort(ticket);
    if (ok) ok = application_bots_npc_restore(provider,
        (qa_bytes){bytes.data + 28 + (size_t)game_size + (size_t)cvars_size, (size_t)npc_size}, error);
    return ok;
}

static bool provider_capture(application_provider *provider, qa_save_purpose purpose,
                              const qa_application_native_resource_refs *resources,
                              qa_buffer *out, qa_error *error)
{
    qa_buffer state = {0};
    bool ok;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        ok = native_q1_capture(provider, &state, error);
    else if (provider->kind == APPLICATION_PROVIDER_Q2)
        ok = application_native_q2_checkpoint_capture(provider, purpose, &state, error);
    else if (provider->kind == APPLICATION_PROVIDER_Q3)
        ok = native_q3_capture(provider, &state, error);
    else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
        ok = application_native_q2_save_capture(provider, purpose, resources, &state, error);
    else ok = application_guest_checkpoint_capture(provider, resources, &state, error);
    if (!ok) return false;
    if (state.size > SIZE_MAX - 28) {
        qa_buffer_free(&state);
        return application_fail(error, QA_ERROR_MEMORY, "provider continuation extent overflow");
    }
    qa_buffer bytes = {.size = state.size + 28}; bytes.data = calloc(1, bytes.size);
    if (!bytes.data) {
        qa_buffer_free(&state);
        return application_fail(error, QA_ERROR_MEMORY, "allocating provider continuation");
    }
    memcpy(bytes.data, "QAPV", 4); qa_store_u32le(bytes.data + 4, provider->kind);
    qa_store_u32le(bytes.data + 8, provider->map_bound ? 1u : 0u);
    qa_store_u32le(bytes.data + 12, provider->q1_server_flags);
    qa_store_u32le(bytes.data + 16, provider->q2_server_flags);
    qa_store_u64le(bytes.data + 20, state.size);
    memcpy(bytes.data + 28, state.data, state.size); qa_buffer_free(&state);
    *out = bytes;
    return true;
}

static bool provider_restore(application_persistence *operation,
                              application_provider *provider, qa_bytes bytes, qa_error *error)
{
    if (!bytes.data || bytes.size < 28 || memcmp(bytes.data, "QAPV", 4) ||
        qa_load_u32le(bytes.data + 4) != (uint32_t)provider->kind ||
        qa_load_u32le(bytes.data + 8) > 1 || qa_load_u64le(bytes.data + 20) != bytes.size - 28)
        return application_fail(error, QA_ERROR_FORMAT, "invalid provider continuation header or extent");
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine &&
        qa_load_u32le(bytes.data + 8) != 1)
        return application_fail(error, QA_ERROR_FORMAT, "native Q2 provider wrapper requires its actual ready map owner");
    qa_bytes state = {bytes.data + 28, bytes.size - 28};
    bool ok;
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        ok = native_q1_restore(provider, state, error);
    } else if (provider->kind == APPLICATION_PROVIDER_Q2)
        ok = application_native_q2_checkpoint_restore(provider, state, error);
    else if (provider->kind == APPLICATION_PROVIDER_Q3)
        ok = native_q3_restore(provider, state, error);
    else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
        ok = application_native_q2_save_restore(provider,
            operation->purpose == QA_SAVE_TRANSITION
                ? saved_provider(operation->active, provider->launch->selection.instance) : NULL,
            state, operation->options, operation->ops, error);
    else ok = application_guest_q3_save_declarations(provider,
        operation->options, operation->ops, error) &&
        application_guest_checkpoint_restore(provider, state, error);
    if (!ok) return false;
    provider->map_bound = qa_load_u32le(bytes.data + 8) != 0;
    provider->q1_server_flags = qa_load_u32le(bytes.data + 12);
    provider->q2_server_flags = qa_load_u32le(bytes.data + 16);
    return true;
}

static bool persistence_inventory(application_persistence *operation, qa_application *app,
    const qa_save_image *image, qa_error *error)
{
    const qa_launch_snapshot *launch = qa_application_launch(app);
    size_t provider_count = qa_launch_snapshot_instance_count(launch);
    size_t shared_count=0;
    for (qa_save_owner_kind kind=QA_SAVE_STRINGS;kind<QA_SAVE_PROVIDER;++kind)
        if (qa_save_shared_state_kind(kind, operation->purpose)) ++shared_count;
    if (!launch || provider_count != app->provider_count ||
        provider_count > QA_SAVE_OWNER_LIMIT - shared_count)
        return application_fail(error, QA_ERROR_FORMAT, "application provider inventory differs from configuration");
    size_t count = shared_count + provider_count;
    qa_save_owner *owners = calloc(count, sizeof(*owners));
    if (!owners) return application_fail(error, QA_ERROR_MEMORY, "allocating application save owner inventory");
    bool ok = true;
    size_t used_external = 0;
    qa_save_owner_kind shared_kind=QA_SAVE_STRINGS;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_save_owner *owner = owners + i;
        const char *schema = NULL;
        application_provider *provider = NULL;
        if (i < shared_count) {
            while (!qa_save_shared_state_kind(shared_kind, operation->purpose)) ++shared_kind;
            owner->kind = shared_kind++; owner->instance = "";
            schema = shared_schema(owner->kind);
        } else {
            const qa_launch_instance *instance = qa_launch_snapshot_instance(launch, i - shared_count);
            provider = saved_provider(app, instance->selection.instance);
            if (!provider || provider != instance->state || !provider->constructed || !provider->attached) {
                ok = application_fail(error, QA_ERROR_FORMAT, "selected provider is not an attached restored owner"); break;
            }
            owner->kind = QA_SAVE_PROVIDER; owner->instance = instance->selection.instance;
            schema = provider_schema(provider);
            owner->backend = provider->kind == APPLICATION_PROVIDER_QC ? "quakec" :
                provider->kind == APPLICATION_PROVIDER_QVM ? "qvm" :
                (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine)
                    ? "native-owned" : "";
        }
        if (schema) {
            owner->schema = schema;
            if (!owner->backend) owner->backend = "";
        } else {
            const qa_application_persistence_owner *binding = NULL;
            for (size_t j = 0; j < operation->ops->owner_count; ++j) {
                const qa_application_persistence_owner *entry = operation->ops->owners + j;
                if (entry->identity.kind == owner->kind && entry->identity.instance &&
                    !strcmp(entry->identity.instance, owner->instance)) {
                    if (binding) { ok = application_fail(error, QA_ERROR_FORMAT, "duplicate application owner producer"); break; }
                    binding = entry;
                }
            }
            if (!ok) break;
            if (!binding || !binding->capture || !binding->restore) {
                ok = application_fail(error, QA_ERROR_UNSUPPORTED, "required application persistence producer is unavailable"); break;
            }
            *owner = binding->identity; ++used_external;
        }
        if (image) {
            const qa_save_record *record = qa_save_image_find(image, owner->kind, owner->instance);
            if (!record || !same_owner(owner, &record->owner))
                ok = application_fail(error, QA_ERROR_FORMAT, "save owner content/schema/backend differs from candidate");
        }
    }
    size_t image_count=0;
    for (size_t i=0;image && i<qa_save_image_record_count(image);++i) {
        const qa_save_record *record=qa_save_image_record_at(image,i);
        if (record->owner.kind==QA_SAVE_PROVIDER ||
            qa_save_shared_state_kind(record->owner.kind,operation->purpose)) ++image_count;
    }
    if (ok && (used_external != operation->ops->owner_count ||
        (image && image_count != count)))
        ok = application_fail(error, QA_ERROR_FORMAT, "application persistence owner inventory contains unselected owners");
    if (!ok) { free(owners); return false; }
    free(operation->owners); operation->owners = owners; operation->owner_count = count;
    return true;
}

static bool persistence_begin(void *opaque, qa_save_purpose purpose, qa_save_metadata *metadata,
    const qa_save_owner **owners, size_t *count, qa_error *error)
{
    application_persistence *operation = opaque;
    if (!persistence_lease(operation, error)) return false;
    operation->purpose = purpose;
    qa_application *app = operation->active;
    bool ok = persistence_inventory(operation, app, NULL, error) &&
        application_save_foundation_capture(app, operation->foundation, error) &&
        (purpose==QA_SAVE_TRANSITION || configuration_capture(app, &operation->configuration, error)) &&
        application_save_content_retain_current(app, operation->ops->visit_content,
            operation->ops->context, &operation->content_graph, error);
    if (ok) app->capture_content_graph = operation->content_graph;
    qa_configuration_checkpoint checkpoint; qa_bytes identity;
    if (ok) ok = purpose==QA_SAVE_TRANSITION
        ? qa_configuration_checkpoint_capture(app->configuration,&checkpoint,error)
        : configuration_fields((qa_bytes){operation->configuration.data, operation->configuration.size},
            &checkpoint, &identity, error);
    if (!ok) { persistence_end(operation); return false; }
    *metadata = (qa_save_metadata){.purpose = purpose, .elapsed_ns = qa_session_elapsed(app->session),
        .configuration_generation = checkpoint.generation, .world_generation = app->map_revision};
    qa_application_map_view map;
    if (qa_application_map_read(app, &map)) {
        const qa_launch_snapshot *launch = qa_application_launch(app);
        const qa_launch_binding *entities = qa_launch_binding_for(qa_launch_snapshot_choices(launch),
            (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
        const qa_launch_instance *source = entities ? qa_launch_snapshot_find(launch, entities->instance) : NULL;
        const qa_product *product = source ? qa_catalog_product(qa_application_catalog(app), source->selection.product) : NULL;
        const char *title = product ? product->title && *product->title ? product->title : product->key : "";
        static const char *const editions[] = {"classic", "rerelease", "quakeworld", "demo"};
        const char *edition = product && (unsigned)product->edition < sizeof(editions)/sizeof(*editions) ? editions[product->edition] : "";
        int map_length = snprintf(metadata->map, sizeof(metadata->map), "%s", map.name ? map.name : "");
        int game_length = snprintf(metadata->game, sizeof(metadata->game), "%s%s%s%s", title,
            *edition ? " (" : "", edition, *edition ? ")" : "");
        if (map_length < 0 || (size_t)map_length >= sizeof(metadata->map) ||
            game_length < 0 || (size_t)game_length >= sizeof(metadata->game)) {
            persistence_end(operation);
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Archived map or game title exceeds save summary extent");
        }
    }
    *owners = operation->owners; *count = operation->owner_count;
    return true;
}

static bool buffer_move(qa_buffer *buffer, qa_buffer *out)
{
    *out = *buffer;
    *buffer = (qa_buffer){0};
    return true;
}

static bool persistence_capture_owner(void *opaque, const qa_save_owner *owner,
    qa_buffer *out, qa_error *error)
{
    application_persistence *operation = opaque;
    qa_application *app = operation->active;
    size_t index;
    switch (owner->kind) {
    case QA_SAVE_STRINGS: index = 0; break;
    case QA_SAVE_ACTORS: index = 1; break;
    case QA_SAVE_SESSION: index = 2; break;
    case QA_SAVE_WORLD: index = 3; break;
    case QA_SAVE_RESOURCES: return application_save_content_encode(operation->content_graph, out, error);
    case QA_SAVE_CONFIGURATION: return buffer_move(&operation->configuration, out);
    case QA_SAVE_ROSTER: return application_players_checkpoint_capture(app, out, error);
    case QA_SAVE_CVARS: return qa_cvars_save_capture(app->cvars, out, error);
    case QA_SAVE_APPLICATION: return application_capture(app, operation->purpose, out, error);
    case QA_SAVE_COMBAT: return qa_persistence_combat_capture(app->session, app->combat, app->inventory, out, error);
    case QA_SAVE_INVENTORY: return qa_persistence_inventory_capture(app->session, app->inventory, out, error);
    case QA_SAVE_PICKUPS: return qa_persistence_pickups_capture(app->session, app->pickups, out, error);
    case QA_SAVE_TARGETS: return qa_persistence_targets_capture(app->targets, out, error);
    case QA_SAVE_CONTROLS: return application_controls_capture(app, out, error);
    case QA_SAVE_MODES: return qa_modes_capture(app->modes, out, error);
    case QA_SAVE_EQUIPMENT: return qa_equipment_capture(app->equipment, out, error);
    case QA_SAVE_EVENTS: return application_events_save_capture(app, out, error);
    case QA_SAVE_NAVIGATION: return application_navigation_save_capture(app, out, error);
    case QA_SAVE_BOTS: return application_bots_save_capture(app, out, error);
    case QA_SAVE_PROGRESSION: return application_save_progression_capture(app, operation->ops, out, error);
    case QA_SAVE_PROVIDER: {
        application_provider *provider = saved_provider(app, owner->instance);
        if (provider && provider_schema(provider))
            return provider_capture(provider, operation->purpose, operation->ops->native_resources, out, error);
        const qa_application_persistence_owner *binding = external_owner(operation->ops, owner);
        return binding && binding->capture(binding->context, app, out, error);
    }
    default: {
        const qa_application_persistence_owner *binding = external_owner(operation->ops, owner);
        return binding && binding->capture(binding->context, app, out, error);
    }
    }
    qa_buffer *buffer = operation->foundation + index;
    return buffer_move(buffer, out);
}

static bool persistence_capture_validate(void *opaque, const qa_save_image *image, qa_error *error)
{
    application_persistence *operation = opaque;
    return persistence_unchanged(operation, error) &&
        operation->ops->validate(operation->ops->context, operation->active, image, error) &&
        persistence_unchanged(operation, error);
}

static bool persistence_capture_attach(void *opaque, qa_save_image *image, qa_error *error)
{
    application_persistence *operation = opaque;
    const qa_application_native_resource_refs *resources = operation->ops->native_resources;
    if (!resources) return true;
    if (!resources->attach)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native capture requires its image-owned capability attachment");
    return resources->attach(resources->context, image, error);
}

bool qa_application_persistence_capture(qa_application *app,
    const qa_application_persistence_ops *ops, qa_save_purpose purpose,
    qa_save_image **out, qa_error *error)
{
    if (!ops || !ops->validate || (ops->owner_count && !ops->owners) ||
        ops->owner_count > QA_SAVE_OWNER_LIMIT || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "application capture requires actual owner producers");
    application_persistence operation = {.active = app, .ops = ops};
    const qa_save_capture_ops capture = {.begin = persistence_begin,
        .capture = persistence_capture_owner, .validate = persistence_capture_validate,
        .end = persistence_end, .attach = persistence_capture_attach};
    return qa_save_capture(&operation, &capture, purpose, out, error);
}

static bool content_mounts(void *opaque, qa_vfs **out, qa_error *error)
{
    qa_application_content_graph *graph = opaque;
    return qa_application_content_claim_view(graph, application_save_content_launch_view(graph), out, error);
}
static bool content_instance(void *opaque, const qa_launch_provider *selection,
    qa_launch_restored_instance *out, qa_error *error)
{
    qa_application_content_graph *graph = opaque;
    application_saved_instance_content saved = {0};
    if (!application_save_content_instance(graph, selection->instance, &saved, error)) return false;
    *out = saved.source;
    out->content = NULL;
    return qa_application_content_claim_view(graph, saved.view, &out->content, error);
}
static bool content_resource(void *opaque, size_t index, qa_launch_resource *out, qa_error *error)
{ return application_save_content_launch_resource_at(opaque, index, out, error); }
static bool content_resource_origin(void *opaque, size_t index, qa_launch_resource_origin *out, qa_error *error)
{
    qa_application_content_graph *graph = opaque;
    if (!application_save_content_launch_resource_origin(graph, index, out, error)) return false;
    if (out->kind == QA_LAUNCH_ORIGIN_SOURCE_QW)
        return application_save_content_launch_source_claim(graph, index, out, error);
    uint64_t view = qa_application_content_view_id(graph, out->content);
    out->content = NULL;
    return qa_application_content_retain_view(graph, view, &out->content, error);
}

static qa_bytes persistence_configuration(application_persistence *operation,
    const qa_save_image *image, qa_error *error)
{
    if (operation->purpose==QA_SAVE_TRANSITION)
        return (qa_bytes){operation->configuration.data,operation->configuration.size};
    const qa_save_record *record=foundation_record(image,QA_SAVE_CONFIGURATION,"qa.configuration",error);
    return record?record->payload:(qa_bytes){0};
}

static qa_bytes persistence_progression(application_persistence *operation,
    const qa_save_image *image, qa_error *error)
{
    if (operation->progression.size)
        return (qa_bytes){operation->progression.data,operation->progression.size};
    const qa_save_record *record=foundation_record(image,QA_SAVE_PROGRESSION,"qa.progression.application",error);
    return record?record->payload:(qa_bytes){0};
}

static bool persistence_create(void *opaque, const qa_save_image *image, void **out, qa_error *error)
{
    application_persistence *operation = opaque;
    bool transition=operation->purpose==QA_SAVE_TRANSITION;
    qa_configuration_checkpoint checkpoint; qa_bytes identity;
    if (!configuration_fields(persistence_configuration(operation,image,error),
        &checkpoint,&identity,error)) return false;
    const qa_save_metadata *metadata = qa_save_image_metadata(image);
    if (!transition && checkpoint.generation != metadata->configuration_generation)
        return application_fail(error, QA_ERROR_FORMAT, "configuration continuation disagrees with save envelope");
    qa_bytes progression_bytes=persistence_progression(operation,image,error);
    if (!progression_bytes.size) return false;
    qa_application *candidate = NULL;
    qa_application_content_graph *graph = NULL;
    qa_application_options options = *operation->options, construction;
    bool created;
    if (transition) created=application_save_content_retain_current(operation->active,
        operation->ops->visit_content,operation->ops->context,&graph,error);
    else {
        const qa_save_record *resources=foundation_record(image,QA_SAVE_RESOURCES,"qa.content-manifest",error);
        created=resources && application_save_content_prepare(resources->payload,&options,&graph,error);
    }
    if (created) {
        qa_catalog *catalog=qa_application_content_catalog(graph,application_save_content_launch_catalog(graph));
        qa_product_id preset=0;
        created=qa_launch_identity_preset(catalog,identity,&preset,error);
        if (created) options.player_profile_root=qa_catalog_product_write_root(catalog,preset);
    }
    if (created) created = application_save_progression_prepare(&options,
        operation->ops, progression_bytes, &construction, error) &&
        application_create_restored(&construction, image, transition ? operation->active : NULL,
            &graph, &candidate, error);
    application_save_content_destroy(graph);
    *out = candidate;
    if (!created) return false;
    candidate->operation = APPLICATION_PERSISTING;
    candidate->native_restore_image = image;
    candidate->native_restore_current = transition ? operation->active : NULL;
    candidate->native_restore_resources = operation->ops->native_resources;
    if (operation->ops->prepare_services &&
        !operation->ops->prepare_services(operation->ops->context, candidate, image, error)) {
        candidate->native_restore_image = NULL;
        candidate->native_restore_current = NULL;
        candidate->native_restore_resources = NULL;
        return false;
    }
    qa_launch_draft *draft = NULL;
    qa_configuration_transaction *transaction = NULL;
    const qa_launch_restore_content content = {.context = candidate->content_graph,
        .mounts = content_mounts, .instance = content_instance,
        .resource = transition ? NULL : content_resource,
        .resource_origin = transition ? NULL : content_resource_origin,
        .resource_count = transition ? 0 : application_save_content_launch_resource_count(candidate->content_graph)};
    bool ok = application_save_configuration_decode(candidate, identity, &draft, error) &&
        (!transition || application_campaign_restore_draft(operation->active,draft,error)) &&
        application_q3_product_validate_draft(&candidate->q3_product, draft, error) &&
        qa_configuration_prepare_restored(candidate->configuration, draft, &checkpoint, &content, &transaction, error);
    qa_launch_draft_destroy(draft);
    if (ok) {
        const qa_launch_snapshot *snapshot = qa_configuration_candidate(transaction);
        ok = (transition || qa_launch_identity_match(snapshot, qa_session_actors(candidate->session), identity, error)) &&
            application_q3_product_validate_snapshot(&candidate->q3_product, snapshot, error) &&
            application_save_prepare_content(candidate, snapshot, image, error) &&
            application_metadata_prepare(candidate, image, error);
        for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
            application_provider *provider = candidate->providers[i];
            if (provider && provider->constructed && provider->attached &&
                provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
                ok = application_native_q2_prepare_restore(provider, error);
        }
        if (ok && operation->ops->prepare_content) {
            const qa_launch_snapshot *routing_snapshot = candidate->routing_snapshot;
            application_provider **routing_providers = candidate->routing_providers;
            size_t routing_count = candidate->routing_provider_count;
            candidate->routing_snapshot = snapshot;
            candidate->routing_providers = candidate->providers;
            candidate->routing_provider_count = candidate->provider_count;
            ok = operation->ops->prepare_content(operation->ops->context, candidate, snapshot, image, error);
            candidate->routing_snapshot = routing_snapshot;
            candidate->routing_providers = routing_providers;
            candidate->routing_provider_count = routing_count;
        }
    }
    if (ok) ok = qa_configuration_commit_restored(transaction, error);
    if (!ok && transaction) (void)qa_configuration_abort(transaction, NULL);
    if (ok) ok = persistence_restore_provider_clocks(operation, candidate, error);
    if (ok) ok = persistence_inventory(operation, candidate, image, error);
    if (ok) ok = application_save_resolvers(candidate, &operation->resolvers, error);
    if (!ok) {
        candidate->native_restore_image = NULL;
        candidate->native_restore_current = NULL;
        candidate->native_restore_resources = NULL;
    }
    return ok;
}

static bool persistence_restore_provider_clocks(application_persistence *operation,
    qa_application *candidate, qa_error *error)
{
    const qa_save_record *record = foundation_record(operation->image, QA_SAVE_SESSION, "qa.session", error);
    qa_session_checkpoint saved = {0};
    bool ok = record && candidate->operation == APPLICATION_PERSISTING &&
        candidate->native_restore_image == operation->image &&
        qa_save_session_decode(record->payload, &saved, error);
    for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (!provider->component_attached) continue;
        const qa_session_component_checkpoint *clock = NULL;
        for (size_t j = 0; j < saved.component_count; ++j) {
            if (saved.components[j].owner != provider->owner) continue;
            if (clock) { ok = false; break; }
            clock = saved.components + j;
        }
        if (!ok || !provider->constructed || !provider->attached || provider->application != candidate ||
            !clock) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved Source clock has no unique constructed provider owner");
            break;
        }
        ok = qa_session_restore_clock(candidate->session, provider->owner, &clock->state, error);
    }
    qa_session_checkpoint_free(&saved);
    if (!ok && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Source clock import requires its actual candidate save image");
    return ok;
}

static bool persistence_restore_owner(void *opaque, void *value,
    const qa_save_record *record, qa_error *error)
{
    application_persistence *operation = opaque;
    qa_application *candidate = value;
    switch (record->owner.kind) {
    /* Foundation is decoded during isolated construction and applied after
     * source bindings. Configuration was prepared without ordinary callbacks. */
    case QA_SAVE_STRINGS: case QA_SAVE_ACTORS: case QA_SAVE_SESSION:
    case QA_SAVE_WORLD: case QA_SAVE_CONFIGURATION: case QA_SAVE_RESOURCES: return true;
    case QA_SAVE_ROSTER: return application_players_checkpoint_restore(candidate, record->payload, error);
    case QA_SAVE_APPLICATION: return application_restore(candidate, record->payload, error);
    case QA_SAVE_COMBAT: return qa_persistence_combat_restore(candidate->session, candidate->combat, candidate->inventory,
        &operation->resolvers, record->payload, error);
    case QA_SAVE_INVENTORY: return qa_persistence_inventory_restore(candidate->session, candidate->inventory,
        &operation->resolvers, record->payload, error);
    case QA_SAVE_PICKUPS: return qa_persistence_pickups_restore(candidate->session, candidate->pickups,
        &operation->resolvers, record->payload, error);
    case QA_SAVE_TARGETS: return qa_persistence_targets_restore(candidate->targets, &operation->resolvers, record->payload, error);
    case QA_SAVE_CONTROLS: return application_controls_restore(candidate, record->payload, error);
    case QA_SAVE_MODES: {
        bool ok = qa_modes_restore_bytes(candidate->modes, record->payload, error);
        for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
            application_provider *provider = candidate->providers[i];
            if (provider->kind == APPLICATION_PROVIDER_Q3) {
                native_q3_record saved={0};
                ok = native_q3_saved_record(provider, operation->image, &saved, error);
                if (ok && saved.settings_bound)
                    ok = application_native_q3_settings_register(provider, error);
            }
        }
        return ok;
    }
    case QA_SAVE_EQUIPMENT: return application_native_q2_callbacks_equipment_restore_prepare(candidate,error)&&
        qa_equipment_restore_bytes(candidate->equipment, record->payload, error);
    case QA_SAVE_EVENTS: return application_events_save_restore(candidate, record->payload, error);
    case QA_SAVE_NAVIGATION: return application_navigation_save_restore(candidate, record->payload, error);
    case QA_SAVE_BOTS: return application_bots_save_restore(candidate, record->payload, error);
    case QA_SAVE_PROGRESSION: return application_save_progression_restore(candidate,
        operation->ops, operation->progression.size?
            (qa_bytes){operation->progression.data,operation->progression.size}:record->payload, error);
    case QA_SAVE_CVARS: {
        qa_cvars_restore *ticket = NULL;
        bool ok = qa_cvars_save_prepare(candidate->cvars, record->payload, &ticket, error) &&
            qa_cvars_save_commit(ticket, error);
        if (!ok) qa_cvars_save_abort(ticket);
        return ok;
    }
    case QA_SAVE_PROVIDER: {
        application_provider *provider = saved_provider(candidate, record->owner.instance);
        if (!provider) return application_fail(error, QA_ERROR_FORMAT, "restore selected provider is absent");
        if (provider_schema(provider))
            return provider_restore(operation, provider, record->payload, error);
        const qa_application_persistence_owner *binding = external_owner(operation->ops, &record->owner);
        return binding && binding->restore(binding->context, candidate, record->payload, error);
    }
    default: {
        const qa_application_persistence_owner *binding = external_owner(operation->ops, &record->owner);
        return binding && binding->restore(binding->context, candidate, record->payload, error);
    }
    }
}

static bool persistence_finish(void *opaque, void *value, const qa_save_image *image, qa_error *error)
{
    application_persistence *operation = opaque;
    qa_application *candidate = value;
    application_save_foundation foundation = {0};
    bool ok=true;
    if (operation->purpose==QA_SAVE_TRANSITION) {
        qa_buffer values={0}; qa_cvars_restore *ticket=NULL;
        ok=qa_cvars_save_capture(operation->active->cvars,&values,error) &&
            qa_cvars_save_prepare(candidate->cvars,(qa_bytes){values.data,values.size},&ticket,error) &&
            qa_cvars_save_commit(ticket,error);
        if (!ok) qa_cvars_save_abort(ticket);
        qa_buffer_free(&values);
        if (ok) ok=application_save_progression_restore(candidate,operation->ops,
            persistence_progression(operation,image,error),error);
    }
    if (ok && operation->ops->reconnect)
        ok=operation->ops->reconnect(operation->ops->context, candidate, image, error);
    for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            ok = qa_q2_game_restore_finish(provider->state.q2, error);
        else if (provider->kind == APPLICATION_PROVIDER_Q1)
            ok = qa_q1_game_restore_finish(provider->state.q1, error);
        else if (provider->kind == APPLICATION_PROVIDER_QC)
            ok = application_qc_npc_restore_finish(provider, error) &&
                application_qc_objectives_restored(provider->state.qc.engine, error);
        if (ok)
            ok = application_guest_inventory_restore_finish(provider, error);
    }
    if (ok) ok = qa_modes_reconnect(candidate->modes, error) &&
        qa_equipment_reconnect(candidate->equipment, error) &&
        qa_persistence_combat_validate(candidate->combat, candidate->inventory, error);
    if (ok) ok = application_save_foundation_decode(image, &foundation, error) &&
        application_save_foundation_finish(candidate, &foundation, error);
    application_save_foundation_free(&foundation);
    if (ok) ok = application_supplies_reconnect(candidate->supplies, error);
    for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_Q3 && provider->state.q3)
            ok = qa_q3_game_reconnect(provider->state.q3, error);
    }
    if (ok) ok = application_native_q1_wire_reconnect(candidate, error);
    if (ok) ok = application_portals_reconnect(candidate, error);
    if (ok) ok = application_bots_save_finish(candidate, error);
    if (ok && candidate->match_intents)
        ok = application_match_intents_reconnect(candidate->match_intents, candidate, error);
    for (size_t i = 0; ok && i < candidate->provider_count; ++i) {
        application_provider *provider = candidate->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_Q3) {
            native_q3_record saved={0};
            ok = native_q3_saved_record(provider, image, &saved, error);
            if (ok && saved.settings_bound!=application_native_q3_console_settings_bound(provider))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 settings differ from their restored source owner");
            if (ok && (saved.settings_present!=(provider->native_q3_settings!=NULL) ||
                saved.settings_initialized!=application_native_q3_settings_initialized(provider)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 cache differs from its imported settings owner");
            if (ok && (saved.ipfilters_present!=(provider->native_q3_ipfilters!=NULL) ||
                saved.ipfilters_initialized!=application_native_q3_ipfilters_initialized(provider)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 filters differ from their imported source owner");
            if (ok && saved.votes_present!=(provider->native_q3_votes!=NULL))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 votes differ from their imported source owner");
            if (ok && saved.team_present!=(provider->native_q3_team_status!=NULL))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 team capability differs from its source holder");
            if (ok && saved.settings_bound)
                ok = application_native_q3_settings_reconnect(provider, error);
            if (ok && saved.team_bound)
                ok = application_native_q3_team_status_reconnect(provider, error);
            if (ok && saved.team_bound!=application_native_q3_team_status_bound(provider))
                ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 team capability differs from its restored source binding");
            if (ok && saved.wire_present)
                ok = application_native_q3_wire_finish(provider, error);
        }
        else if (provider->kind == APPLICATION_PROVIDER_QVM ||
                 (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine))
            ok = application_guest_q3_save_finish(provider, error);
    }
    if (ok) ok = application_q3_components_finish_restore(candidate->components, error);
    if (ok) ok = qa_equipment_weapons_reconnect(candidate->equipment, error);
    if (ok && candidate->q1_paused) {
        application_provider *source = application_world_provider(candidate, QA_ROLE_ENTITIES, "");
        if (!source || (source->kind != APPLICATION_PROVIDER_Q1 &&
                       source->kind != APPLICATION_PROVIDER_QC))
            ok = application_fail(error, QA_ERROR_FORMAT, "Saved Quake pause has no active Quake source server");
    }
    qa_configuration_checkpoint checkpoint; qa_bytes identity;
    if (ok) ok = configuration_fields(persistence_configuration(operation,image,error),
        &checkpoint,&identity,error) &&
        (operation->purpose==QA_SAVE_TRANSITION || application_save_configuration_validate(candidate, identity, error));
    if (ok) candidate->operation = APPLICATION_IDLE;
    if (ok && (candidate->map_revision != qa_save_image_metadata(image)->world_generation ||
        !persistence_safe(candidate)))
        ok = application_fail(error, QA_ERROR_FORMAT, "candidate application is not a complete idle saved world");
    if (ok) ok = application_unified_events_restore_finish(candidate, error);
    if (ok && operation->ops->complete_state)
        ok=operation->ops->complete_state(operation->ops->context,candidate,image,error);
    if (ok)
        ok=application_startup_program_restore_prepare(candidate,operation->active,&operation->programs,error) &&
            application_startup_program_publication_seal(operation->programs,error) &&
            application_startup_program_publication_adopt(&operation->programs,error);
    if (ok) ok = operation->ops->validate(operation->ops->context, candidate, image, error);
    if (ok && operation->ops->replay) {
        ok = operation->ops->replay(operation->ops->context, candidate, image, error);
        if (ok && !persistence_safe(candidate))
            ok = application_fail(error, QA_ERROR_FORMAT, "Recovery replay did not return an idle world");
        qa_buffer progression = {0};
        if (ok) ok = application_save_progression_capture(candidate, operation->ops, &progression, error);
        if (ok) {
            qa_buffer_free(&operation->progression);
            operation->progression = progression;
            progression = (qa_buffer){0};
            ok = operation->ops->validate(operation->ops->context, candidate, image, error);
        }
        qa_buffer_free(&progression);
    }
    if (ok && !persistence_safe(candidate))
        ok = application_fail(error, QA_ERROR_FORMAT, "restored candidate changed during final validation");
    if (ok) ok = persistence_unchanged(operation, error);
    if (ok) {
        candidate->native_restore_image = NULL;
        candidate->native_restore_current = NULL;
        candidate->native_restore_resources = NULL;
    }
    return ok;
}

static bool persistence_publish(void *opaque, void *value, qa_error *error)
{
    application_persistence *operation = opaque;
    qa_application *candidate = value;
    if (*operation->slot != operation->active || !persistence_unchanged(operation, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "active application changed before restored publication");
    if (!application_save_content_ready(candidate->content_graph, error)) return false;
    qa_bytes progression=persistence_progression(operation,operation->image,error);
    if (!progression.size || !application_save_progression_matches(candidate,
        operation->ops, progression, error) ||
        !persistence_unchanged(operation, error))
        return false;
    if (!persistence_safe(candidate))
        return application_fail(error, QA_ERROR_ARGUMENT, "progression validation changed the restored candidate");
    if (operation->ops->publish_ready &&
        !operation->ops->publish_ready(operation->ops->context,
            operation->active, candidate, error)) return false;
    bool relinquish_active = false;
    candidate->operation = APPLICATION_PERSISTING;
    if (!application_save_progression_handoff(operation->active, candidate,
        operation->ops, &relinquish_active, error)) return false;
    application_save_progression_publish(operation->active, candidate, relinquish_active);
    candidate->operation = APPLICATION_IDLE;
    candidate->source_shutdown_admitted = true;
    operation->active->source_shutdown_admitted = false;
    operation->displaced = operation->active;
    if (operation->ops->publish)
        operation->ops->publish(operation->ops->context, operation->active, candidate);
    *operation->slot = value;
    return true;
}

static void persistence_discard(void *opaque, void *value)
{
    application_persistence *operation = opaque;
    qa_application *candidate = value;
    if (!application_startup_program_publication_abort(&operation->programs,NULL)) {
        operation->retained = value;
        return;
    }
    candidate->native_restore_image = NULL;
    candidate->native_restore_current = NULL;
    candidate->native_restore_resources = NULL;
    candidate->operation = APPLICATION_IDLE;
    if (operation->ops->discard_services &&
        !operation->ops->discard_services(operation->ops->context, value, NULL)) {
        operation->retained = value;
        return;
    }
    if (!qa_application_destroy(value, NULL)) operation->retained = value;
}

bool qa_application_persistence_restore(qa_application **active,
    const qa_application_options *options, const qa_application_persistence_ops *ops,
    const qa_save_image *image, qa_application **displaced,
    qa_application **retained_on_failure, qa_error *error)
{
    if (!active || !options || !ops || !ops->validate ||
        (ops->owner_count && !ops->owners) || ops->owner_count > QA_SAVE_OWNER_LIMIT ||
        ((ops->publish_ready != NULL) != (ops->publish != NULL)) ||
        !image || !displaced || !retained_on_failure || *retained_on_failure ||
        active == displaced || active == retained_on_failure || displaced == retained_on_failure)
        return application_fail(error, QA_ERROR_ARGUMENT, "application restore requires isolated content and owner producers");
    application_persistence operation = {.active = *active, .slot = active,
        .options = options, .ops = ops, .image = image,
        .purpose = qa_save_image_metadata(image)->purpose};
    if (!persistence_lease(&operation, error)) return false;
    const qa_save_restore_ops restore = {.create = persistence_create, .restore = persistence_restore_owner,
        .finish = persistence_finish, .publish = persistence_publish, .discard = persistence_discard};
    bool ok=operation.purpose!=QA_SAVE_TRANSITION ||
        (application_save_progression_capture(operation.active,ops,&operation.progression,error) &&
            configuration_capture(operation.active,&operation.configuration,error));
    if (ok) ok = !qa_application_launch(operation.active) ||
        application_save_content_retain_current(operation.active, ops->visit_content,
            ops->context, &operation.content_graph, error);
    if (ok) {
        operation.active->capture_content_graph = operation.content_graph;
        ok = qa_save_restore(&operation, &restore, image, error);
    }
    persistence_end(&operation);
    if (ok) *displaced = operation.displaced;
    else if (operation.retained) *retained_on_failure = operation.retained;
    return ok;
}

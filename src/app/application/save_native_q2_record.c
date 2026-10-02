#include "save_native_q2_record.h"
#include "save_native_q2.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_continuation.h"
#include "startup_flow.h"
#include "qa/binary.h"

#include <stdlib.h>
#include <string.h>

enum { NATIVE_RECORD_HEADER = 88, NATIVE_RECORD_PARTS = 6 };

static bool complete(const qa_native_checkpoint *state, qa_error *error)
{
    return ((state->kind == QA_NATIVE_CHECKPOINT_Q2_CLASSIC ||
             state->kind == QA_NATIVE_CHECKPOINT_Q2_RERELEASE) &&
            state->has_game && state->has_level && state->has_host &&
            state->game.size && state->level.size && state->host.size) ||
        application_fail(error, QA_ERROR_FORMAT, "native Q2 record requires actual GAME LEVEL and HOST continuation");
}

static const char *source_text(application_provider *provider, qa_string_id id)
{
    return id ? qa_strings_cstr(qa_session_strings(provider->application->session), id) : "";
}

bool application_native_q2_save_resource_recipe(const qa_save_record *record,
    qa_bytes *out, qa_error *error)
{
    if (!record || !out || record->owner.kind != QA_SAVE_PROVIDER ||
        !record->payload.data || record->payload.size < 32 ||
        memcmp(record->payload.data, "QAPV", 4) ||
        qa_load_u32le(record->payload.data + 4) != 1 ||
        qa_load_u32le(record->payload.data + 8) != APPLICATION_PROVIDER_NATIVE ||
        qa_load_u64le(record->payload.data + 24) != record->payload.size - 32)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 resource recipe lacks its actual provider envelope");
    qa_bytes bytes = {record->payload.data + 32, record->payload.size - 32};
    if (bytes.size < NATIVE_RECORD_HEADER || memcmp(bytes.data, "QAN2", 4) ||
        qa_load_u32le(bytes.data + 4) != 2)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 resource recipe lacks its actual source envelope");
    size_t offset = NATIVE_RECORD_HEADER;
    qa_bytes recipe = {0};
    for (size_t i = 0; i < NATIVE_RECORD_PARTS; ++i) {
        uint64_t bytes_count = qa_load_u64le(bytes.data + 40 + i * 8);
        if ((!bytes_count && i != 5) || bytes_count > bytes.size - offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 resource recipe part is truncated");
        if (i == 5) recipe = (qa_bytes){bytes.data + offset, (size_t)bytes_count};
        offset += (size_t)bytes_count;
    }
    if (offset != bytes.size || !recipe.size)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 resource recipe is absent or has trailing bytes");
    *out = recipe;
    return true;
}

bool application_native_q2_save_capture(application_provider *provider, qa_save_purpose purpose,
    const qa_application_native_resource_refs *resources, qa_buffer *out, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!engine || !out || !engine->initialized || !engine->map_ready || !provider->map_bound ||
        !provider->state.native.host || !provider->application->map_resource ||
        !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q2 capture requires its complete initialized map owner");
    if (!application_native_q2_continuation_portable(provider, error)) return false;
    const char *texts[] = {source_text(provider, engine->map_name), engine->entity_text,
                          source_text(provider, engine->spawn_point)};
    for (size_t i = 0; i < 3; ++i)
        if (!texts[i]) return application_fail(error, QA_ERROR_FORMAT, "native Q2 source map text has no actual owner");
    qa_native_checkpoint snapshot = {0};
    qa_buffer parts[NATIVE_RECORD_PARTS] = {0};
    qa_native_checkpoint_request request = {.game = true, .level = true,
        .autosave = purpose == QA_SAVE_LEVEL_ENTRY};
    bool ok = qa_native_checkpoint_capture(qa_native_host_instance(provider->state.native.host),
        request, &snapshot, error) && complete(&snapshot, error) &&
        application_native_q2_continuation_capture(provider, &snapshot, parts + 1, error) &&
        qa_native_checkpoint_encode(&snapshot, parts, error);
    if (ok && engine->process.resources) {
        if (!resources || !resources->capture || !resources->resolve || !resources->attach)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 capture requires its historical external capability graph");
        else
            ok = resources->capture(resources->context, provider->launch->selection.instance,
                engine->process.process.source_id, engine->process.resources, parts + 5, error);
        if (ok && !parts[5].size)
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 external capability capture has no actual recipe");
    }
    qa_native_checkpoint_free(&snapshot);
    size_t size = NATIVE_RECORD_HEADER;
    for (size_t i = 0; ok && i < NATIVE_RECORD_PARTS; ++i) {
        if (i >= 2 && i <= 4) {
            parts[i].data = (uint8_t *)texts[i - 2];
            parts[i].size = strlen(texts[i - 2]) + 1;
        }
        if ((!parts[i].size && i != 5) || parts[i].size > SIZE_MAX - size)
            ok = application_fail(error, QA_ERROR_MEMORY, "native Q2 owned record extent is exhausted");
        else size += parts[i].size;
    }
    qa_buffer bytes = {0};
    if (ok) {
        bytes = (qa_buffer){.data = calloc(1, size), .size = size};
        if (!bytes.data) ok = application_fail(error, QA_ERROR_MEMORY, "retaining full native Q2 continuation");
    }
    if (ok) {
        memcpy(bytes.data, "QAN2", 4);
        qa_store_u32le(bytes.data + 4, 2);
        memcpy(bytes.data + 8, qa_resource_digest(provider->application->map_resource)->bytes, 32);
        size_t offset = NATIVE_RECORD_HEADER;
        for (size_t i = 0; i < NATIVE_RECORD_PARTS; ++i) {
            qa_store_u64le(bytes.data + 40 + i * 8, parts[i].size);
            if (parts[i].size) memcpy(bytes.data + offset, parts[i].data, parts[i].size);
            offset += parts[i].size;
        }
        *out = bytes;
    }
    qa_buffer_free(parts);
    qa_buffer_free(parts + 1);
    qa_buffer_free(parts + 5);
    return ok;
}

static bool record_parts(application_provider *provider, qa_bytes bytes,
    qa_bytes out[NATIVE_RECORD_PARTS], qa_error *error)
{
    if (!bytes.data || bytes.size < NATIVE_RECORD_HEADER || memcmp(bytes.data, "QAN2", 4) ||
        qa_load_u32le(bytes.data + 4) != 2 || !provider->application->map_resource ||
        memcmp(bytes.data + 8, qa_resource_digest(provider->application->map_resource)->bytes, 32))
        return application_fail(error, QA_ERROR_FORMAT, "native Q2 continuation map identity differs");
    size_t offset = NATIVE_RECORD_HEADER;
    for (size_t i = 0; i < NATIVE_RECORD_PARTS; ++i) {
        uint64_t length = qa_load_u64le(bytes.data + 40 + i * 8);
        if ((!length && i != 5) || length > bytes.size - offset)
            return application_fail(error, QA_ERROR_FORMAT, "native Q2 continuation part extent is invalid");
        out[i] = (qa_bytes){bytes.data + offset, (size_t)length};
        offset += (size_t)length;
        if (i >= 2 && i <= 4 && (out[i].data[length - 1] || memchr(out[i].data, 0, (size_t)length - 1)))
            return application_fail(error, QA_ERROR_FORMAT, "native Q2 source map text has an invalid terminator");
    }
    if (offset != bytes.size)
        return application_fail(error, QA_ERROR_FORMAT, "native Q2 continuation has trailing bytes");
    qa_bsp_view map;
    if (!qa_bsp_open(qa_resource_bytes(provider->application->map_resource), &map, error)) return false;
    qa_bytes entities = map.lumps[QA_BSP_ENTITIES].bytes;
    const uint8_t *end = memchr(entities.data, 0, entities.size);
    size_t length = end ? (size_t)(end - entities.data) : entities.size;
    if (out[3].size - 1 != length || memcmp(out[3].data, entities.data, length))
        return application_fail(error, QA_ERROR_FORMAT, "native Q2 baseline entity text differs from its actual map");
    const char *map_name = source_text(provider, provider->application->current_map);
    return (map_name && strcmp(map_name, (const char *)out[2].data) == 0) ||
        application_fail(error, QA_ERROR_FORMAT, "native Q2 baseline map name differs from its selected source");
}

bool application_native_q2_save_matches(application_provider *provider, qa_bytes bytes,
    qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || !engine->map_ready || !provider->map_bound ||
        !provider->state.native.host || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 equivalence requires its complete idle source owner");
    qa_bytes parts[NATIVE_RECORD_PARTS];
    qa_native_checkpoint saved = {0}, actual = {0};
    qa_buffer private = {0};
    bool ok = application_native_q2_continuation_portable(provider, error) &&
        record_parts(provider, bytes, parts, error) &&
        qa_native_checkpoint_decode(parts[0], &saved, error) && complete(&saved, error);
    const char *texts[] = {source_text(provider, engine->map_name), engine->entity_text,
                          source_text(provider, engine->spawn_point)};
    for (size_t i = 0; ok && i < 3; ++i)
        if (!texts[i] || strcmp(texts[i], (const char *)parts[i + 2].data))
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 actual source map text changed after restoration");
    /* The qualified full graph covers actual post-export source continuation.
     * GAME/LEVEL exports can mutate that state and contain historical pointers. */
    if (ok)
        ok = application_native_q2_continuation_capture(provider, &saved, &private, error);
    if (ok && (private.size != parts[1].size || memcmp(private.data, parts[1].data, private.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 complete portable private state changed after restoration");
    qa_native_checkpoint_request request = {.autosave = saved.autosave,
                                            .transition = saved.transition};
    if (ok)
        ok = qa_native_checkpoint_capture(qa_native_host_instance(provider->state.native.host),
                                           request, &actual, error);
    if (ok && (!actual.has_host || actual.has_game || actual.has_level ||
        actual.kind != saved.kind || actual.profile != saved.profile || actual.q3_role != saved.q3_role ||
        !actual.has_declaration || !qa_sha256_equal(&actual.declaration, &saved.declaration) ||
        !qa_sha256_equal(&actual.image.digest, &saved.image.digest) ||
        actual.host.size != saved.host.size || memcmp(actual.host.data, saved.host.data, actual.host.size)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 actual HOST continuation changed after restoration");
    qa_buffer_free(&private);
    qa_native_checkpoint_free(&actual);
    qa_native_checkpoint_free(&saved);
    return ok;
}

bool application_native_q2_save_restore(application_provider *provider, qa_bytes bytes,
    const qa_application_options *options, const qa_application_persistence_ops *ops, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    qa_application *app = provider ? provider->application : NULL;
    if (!engine || !app || !options || !ops || app->operation != APPLICATION_PERSISTING ||
        engine->initialized || engine->map_ready || !provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q2 restore requires its detached fresh source owner");
    if (!application_native_q2_continuation_portable(provider, error)) return false;
    qa_bytes parts[NATIVE_RECORD_PARTS];
    qa_native_checkpoint snapshot = {0};
    struct application_native_q2_continuation *private = NULL;
    bool ok = record_parts(provider, bytes, parts, error) &&
        qa_native_checkpoint_decode(parts[0], &snapshot, error) && complete(&snapshot, error);
    const qa_actor_record *world = qa_actors_at_source(qa_session_actors(app->session), provider->owner, 0);
    if (ok && !world) ok = application_fail(error, QA_ERROR_FORMAT, "native Q2 saved world actor is missing");
    if (ok) {
        engine->world_actor = world->id;
        ok = application_native_q2_activate(engine, error) &&
            application_native_q2_prepare_restore(provider, error);
    }
    if (ok)
        ok = qa_native_host_restore_cvars(provider->state.native.host,
                (qa_bytes){snapshot.host.data, snapshot.host.size}, error) &&
            application_startup_source_restore(provider, engine->console, engine->cvars,
                &engine->command_context, error);
    if (ok) {
        ++engine->calls;
        ok = qa_native_host_initialize(provider->state.native.host, 0, 0, false, error);
        --engine->calls;
        if (ok) engine->initialized = true;
    }
    if (ok) ok = application_native_q2_continuation_prepare(provider, &snapshot, parts[1], &private, error);
    qa_application_native_baseline_services *services = NULL;
    struct application_native_q2_scratch *scratch = NULL;
    const qa_launch_snapshot *launch = qa_application_launch(app);
    if (ok) ok = application_native_q2_baseline_services_prepare(app, options, &services, error);
    if (ok && ops->prepare_native_baseline)
        ok = ops->prepare_native_baseline(ops->context, app, provider->owner, services, error);
    if (ok) ok = application_native_q2_scratch_prepare(provider, launch, services, &scratch, error);
    qa_native_instance *instance = provider->state.native.host
        ? qa_native_host_instance(provider->state.native.host) : NULL;
    if (ok) ok = qa_native_checkpoint_restore(instance, &snapshot, QA_NATIVE_RESTORE_GAME, error) &&
        application_native_q2_scratch_begin(scratch, error) &&
        application_native_q2_scratch_spawn(scratch, (const char *)parts[2].data,
            (const char *)parts[3].data, (const char *)parts[4].data, error) &&
        qa_native_checkpoint_restore(instance, &snapshot, QA_NATIVE_RESTORE_LEVEL, error) &&
        application_native_q2_scratch_end(scratch, error) &&
        qa_native_checkpoint_restore(instance, &snapshot, QA_NATIVE_RESTORE_HOST, error) &&
        application_native_q2_continuation_apply(provider, private, error);
    if (ok) {
        const char *name = source_text(provider, engine->map_name);
        const char *spawn = source_text(provider, engine->spawn_point);
        ok = engine->initialized && engine->map_ready && name && spawn && engine->entity_text &&
            strcmp(name, (const char *)parts[2].data) == 0 &&
            strcmp(engine->entity_text, (const char *)parts[3].data) == 0 &&
            strcmp(spawn, (const char *)parts[4].data) == 0;
        if (!ok) application_fail(error, QA_ERROR_FORMAT, "native Q2 HOST continuation differs from its actual baseline source input");
    }
    application_native_q2_continuation_abort(private);
    qa_native_checkpoint_free(&snapshot);
    return ok && application_native_q2_baselines_destroy(app, error);
}

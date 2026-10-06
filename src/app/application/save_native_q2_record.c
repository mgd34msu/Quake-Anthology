#include "save_native_q2_record.h"
#include "save_native_q2.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_baseline.h"
#include "startup_flow.h"
#include "qa/binary.h"
#include "qa/map_sidecars.h"

#include <stdlib.h>
#include <string.h>

enum { NATIVE_RECORD_PARTS = 4, NATIVE_RECORD_HEADER = 4 + NATIVE_RECORD_PARTS * 8 };

static bool complete(const qa_native_checkpoint *state, qa_error *error)
{
    return ((state->kind == QA_NATIVE_CHECKPOINT_Q2_CLASSIC ||
             state->kind == QA_NATIVE_CHECKPOINT_Q2_RERELEASE) &&
            state->has_host && state->host.size && !state->has_process &&
            state->has_level && state->level.size &&
            (state->transition ? !state->has_game : state->has_game && state->game.size)) ||
        application_fail(error, QA_ERROR_FORMAT,
            "Native Q2 record requires its original GAME/LEVEL files and engine state");
}

static const char *source_text(application_provider *provider, qa_string_id id)
{
    return id ? qa_strings_cstr(qa_session_strings(provider->application->session), id) : "";
}

static bool record_read(qa_bytes bytes, qa_bytes out[NATIVE_RECORD_PARTS], qa_error *error)
{
    if (!bytes.data || bytes.size < NATIVE_RECORD_HEADER || memcmp(bytes.data, "QAN2", 4))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 save source envelope is invalid");
    size_t offset = NATIVE_RECORD_HEADER;
    for (size_t i = 0; i < NATIVE_RECORD_PARTS; ++i) {
        uint64_t length = qa_load_u64le(bytes.data + 4 + i * 8);
        if ((!length && i != 3) || length > bytes.size - offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 save part extent is invalid");
        out[i] = (qa_bytes){bytes.data + offset, (size_t)length};
        offset += (size_t)length;
        if (i >= 1 && i <= 2 && (out[i].data[length - 1] || memchr(out[i].data, 0, (size_t)length - 1)))
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 save map text has an invalid terminator");
    }
    return offset == bytes.size || application_fail(error, QA_ERROR_FORMAT, "Native Q2 save has trailing bytes");
}

bool application_native_q2_save_resource_recipe(const qa_save_record *record,
    qa_bytes *out, qa_error *error)
{
    if (!record || !out || record->owner.kind != QA_SAVE_PROVIDER ||
        !record->payload.data || record->payload.size < 28 ||
        memcmp(record->payload.data, "QAPV", 4) ||
        qa_load_u32le(record->payload.data + 4) != APPLICATION_PROVIDER_NATIVE ||
        qa_load_u64le(record->payload.data + 20) != record->payload.size - 28)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 resource recipe lacks its actual provider envelope");
    qa_bytes parts[NATIVE_RECORD_PARTS];
    if (!record_read((qa_bytes){record->payload.data + 28, record->payload.size - 28}, parts, error)) return false;
    if (!parts[3].size)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 external capability recipe is absent");
    *out = parts[3];
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
    const char *texts[] = {source_text(provider, engine->map_name),
                          source_text(provider, engine->spawn_point)};
    for (size_t i = 0; i < 2; ++i)
        if (!texts[i]) return application_fail(error, QA_ERROR_FORMAT, "native Q2 source map text has no actual owner");
    qa_native_checkpoint snapshot = {0};
    qa_buffer parts[NATIVE_RECORD_PARTS] = {0};
    bool transition = purpose == QA_SAVE_TRANSITION;
    qa_native_checkpoint_request request = {.game = !transition, .level = true, .host = true,
        .autosave = purpose == QA_SAVE_LEVEL_ENTRY, .transition = transition};
    bool ok = qa_native_checkpoint_capture(qa_native_host_instance(provider->state.native.host),
        request, &snapshot, error) && complete(&snapshot, error) &&
        qa_native_checkpoint_encode(&snapshot, parts, error);
    if (ok && engine->process.resources) {
        if (!resources || !resources->capture || !resources->resolve || !resources->attach)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 capture requires its external file capabilities");
        else
            ok = resources->capture(resources->context, provider->launch->selection.instance,
                engine->process.process.source_id, engine->process.resources, parts + 3, error);
        if (ok && !parts[3].size)
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 external capability capture has no actual recipe");
    }
    qa_native_checkpoint_free(&snapshot);
    size_t size = NATIVE_RECORD_HEADER;
    for (size_t i = 0; ok && i < NATIVE_RECORD_PARTS; ++i) {
        if (i >= 1 && i <= 2) {
            parts[i].data = (uint8_t *)texts[i - 1];
            parts[i].size = strlen(texts[i - 1]) + 1;
        }
        if ((!parts[i].size && i != 3) || parts[i].size > SIZE_MAX - size)
            ok = application_fail(error, QA_ERROR_MEMORY, "native Q2 owned record extent is exhausted");
        else size += parts[i].size;
    }
    qa_buffer bytes = {0};
    if (ok) {
        bytes = (qa_buffer){.data = calloc(1, size), .size = size};
        if (!bytes.data) ok = application_fail(error, QA_ERROR_MEMORY, "retaining native Q2 save files");
    }
    if (ok) {
        memcpy(bytes.data, "QAN2", 4);
        size_t offset = NATIVE_RECORD_HEADER;
        for (size_t i = 0; i < NATIVE_RECORD_PARTS; ++i) {
            qa_store_u64le(bytes.data + 4 + i * 8, parts[i].size);
            if (parts[i].size) memcpy(bytes.data + offset, parts[i].data, parts[i].size);
            offset += parts[i].size;
        }
        *out = bytes;
    }
    qa_buffer_free(parts);
    qa_buffer_free(parts + 3);
    return ok;
}

static bool record_parts(application_provider *provider, qa_bytes bytes,
    qa_bytes out[NATIVE_RECORD_PARTS], qa_error *error)
{
    if (!provider->application->map_resource || !record_read(bytes, out, error)) return false;
    const char *map_name = source_text(provider, provider->application->current_map);
    return (map_name && strcmp(map_name, (const char *)out[1].data) == 0) ||
        application_fail(error, QA_ERROR_FORMAT, "native Q2 baseline map name differs from its selected source");
}

bool application_native_q2_save_restore(application_provider *provider, application_provider *current, qa_bytes bytes,
    const qa_application_options *options, const qa_application_persistence_ops *ops, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    qa_application *app = provider ? provider->application : NULL;
    if (!engine || !app || !options || !ops || app->operation != APPLICATION_PERSISTING ||
        !provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q2 restore requires its detached fresh source owner");
    if(engine->restore_record.data)
        return (bytes.data==engine->restore_record.data&&bytes.size==engine->restore_record.size&&
            engine->initialized&&engine->map_ready&&provider->state.native.host&&
            application_native_q2_idle(provider))||
            application_fail(error,QA_ERROR_ARGUMENT,"Native source restore differs from its already imported immutable image record");
    if(engine->initialized||engine->map_ready)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native source restore requires its fresh uninitialized owner");
    qa_bytes parts[NATIVE_RECORD_PARTS];
    qa_native_checkpoint snapshot = {0};
    bool ok = record_parts(provider, bytes, parts, error) &&
        qa_native_checkpoint_decode(parts[0], &snapshot, error) && complete(&snapshot, error);
    if (ok && snapshot.transition != (current != NULL))
        ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 record differs from its actual GAME or LEVEL restore purpose");
    qa_native_checkpoint game = {0};
    if (ok && snapshot.transition) {
        struct application_native_q2 *live = current->kind == APPLICATION_PROVIDER_NATIVE
            ? current->state.native.q2_engine : NULL;
        ok = live && current != provider && current->application != app &&
            current->constructed && current->attached && !current->close_pending &&
            current->owner == provider->owner && current->launch &&
            application_native_q2_launch_matches(current->launch, provider->launch) &&
            live->initialized && live->map_ready && live->profile == engine->profile &&
            current->state.native.host && application_native_q2_idle(current);
        if (!ok)
            application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 revisit requires its current matching GAME owner");
        if (ok) {
            qa_native_checkpoint_request request = {.game = true, .host = true, .autosave = true, .transition = true};
            ok = qa_native_checkpoint_capture(qa_native_host_instance(current->state.native.host),
                request, &game, error);
        }
        if (ok && (!game.has_game || !game.game.size || !game.has_host || !game.host.size || game.has_level || game.has_process))
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 revisit requires its current original GAME file");
    }
    const qa_actor_record *world = qa_actors_at_source(qa_session_actors(app->session), provider->owner, 0);
    if (ok && !world) ok = application_fail(error, QA_ERROR_FORMAT, "native Q2 saved world actor is missing");
    if (ok) {
        engine->world_actor = world->id;
        ok = application_native_q2_activate(engine, error) &&
            application_native_q2_prepare_restore(provider, error);
    }
    if (ok) {
        qa_bsp_view map;
        char *entities = NULL;
        ok = qa_bsp_open(qa_resource_bytes(app->map_resource), &map, error) &&
            qa_map_sidecars_apply_entities(app->map_sidecars, &map, error) &&
            application_native_q2_entity_text(&map, &entities, error);
        if (ok) { free(engine->entity_text); engine->entity_text = entities; }
    }
    if (ok)
        ok = qa_native_host_restore_cvars(provider->state.native.host,
                snapshot.transition ? (qa_bytes){game.host.data, game.host.size} :
                    (qa_bytes){snapshot.host.data, snapshot.host.size}, error) &&
            application_startup_source_restore(provider, engine->console, engine->cvars,
                &engine->command_context, error);
    if (ok) {
        ++engine->calls;
        /* SDK Init creates provisional source storage. Saved HOST slot bindings
         * are imported below; admitting these fresh slots into the restored
         * shared actor registry would precede its suspended source producers. */
        ok = qa_native_host_initialize(provider->state.native.host, 0, 0, false, error);
        --engine->calls;
        if (ok) engine->initialized = true;
    }
    qa_application_native_baseline_services *services = NULL;
    struct application_native_q2_scratch *scratch = NULL;
    const qa_launch_snapshot *launch = qa_application_launch(app);
    if (ok) ok = application_native_q2_baseline_services_prepare(app, options, &services, error);
    if (ok && ops->prepare_native_baseline)
        ok = ops->prepare_native_baseline(ops->context, app, provider->owner, services, error);
    if (ok) ok = application_native_q2_scratch_prepare(provider, launch, services, &scratch, error);
    qa_native_instance *instance = provider->state.native.host
        ? qa_native_host_instance(provider->state.native.host) : NULL;
    if (ok) ok = qa_native_checkpoint_restore(instance,
        snapshot.transition ? &game : &snapshot, QA_NATIVE_RESTORE_GAME, error) &&
        application_native_q2_scratch_begin(scratch, error) &&
        application_native_q2_scratch_spawn(scratch, (const char *)parts[1].data,
            engine->entity_text, (const char *)parts[2].data, error) &&
        qa_native_checkpoint_restore(instance, &snapshot, QA_NATIVE_RESTORE_LEVEL, error) &&
        application_native_q2_scratch_end(scratch, error) &&
        qa_native_host_restore(provider->state.native.host,
            (qa_bytes){snapshot.host.data, snapshot.host.size}, !snapshot.transition, error);
    if (ok) {
        const char *name = source_text(provider, engine->map_name);
        const char *spawn = source_text(provider, engine->spawn_point);
        ok = engine->initialized && engine->map_ready && name && spawn && engine->entity_text &&
            strcmp(name, (const char *)parts[1].data) == 0 &&
            strcmp(spawn, (const char *)parts[2].data) == 0;
        if (!ok) application_fail(error, QA_ERROR_FORMAT, "native Q2 HOST continuation differs from its actual baseline source input");
    }
    qa_native_checkpoint_free(&game);
    qa_native_checkpoint_free(&snapshot);
    if (ok) ok = application_native_q2_baselines_destroy(app, error);
    if (ok) engine->restore_record = bytes;
    return ok;
}

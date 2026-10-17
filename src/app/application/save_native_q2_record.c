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

bool application_native_q2_capture_checkpoint(struct application_native_q2 *engine,
    qa_native_checkpoint_request request, qa_native_checkpoint *out, qa_error *error)
{
    application_provider *provider = engine->provider;
    qa_native_host *host = provider->state.native.host;
    qa_native_instance *instance = qa_native_host_instance(host);
    const qa_native_host_entity_layout *layout = qa_native_host_source_layout(host);
    uint8_t client_inuse[256][4];
    uint32_t cleared = 0;
    bool ok = true;
    if (request.transition && request.level && engine->profile == QA_NATIVE_Q2_GAME_API3) {
        const qa_cvar_view *maximum = qa_cvars_read(engine->cvars, provider->q2_maxclients);
        if (!maximum || maximum->integer < 1 || maximum->integer > 256)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 travel lost its source maximum clients");
        /* SV_GameMap omits client body shells from the old level file. */
        const uint8_t unused[4] = {0};
        for (uint32_t slot = 1; ok && slot <= (uint32_t)maximum->integer; ++slot) {
            qa_native_address address;
            ok = qa_native_entity_address(instance, slot, &address, error) &&
                qa_native_read(instance, address + layout->inuse, client_inuse[cleared], 4, error);
            if (ok) {
                ++cleared;
                ok = qa_native_write(instance, address + layout->inuse, (qa_bytes){unused, 4}, error);
            }
        }
    }
    bool captured = ok && qa_native_checkpoint_capture(instance, request, out, error);
    ok = ok && captured;
    if (cleared) {
        qa_error returned = {0};
        qa_native_entity_table table;
        bool restored = qa_native_entity_table_refresh(instance, &table, &returned);
        if (restored) {
            /* API3 fixes edict stride at Init; resolve the current exported base. */
            for (uint32_t slot = 1; slot <= cleared; ++slot) {
                qa_native_address address;
                qa_error local = {0};
                bool written = qa_native_entity_address(instance, slot, &address, &local) &&
                    qa_native_write(instance, address + layout->inuse,
                        (qa_bytes){client_inuse[slot - 1], 4}, &local);
                if (!written && restored) returned = local;
                restored = written && restored;
            }
        }
        if (ok && !restored && error) *error = returned;
        ok = ok && restored;
    }
    if (captured && !ok) qa_native_checkpoint_free(out);
    return ok;
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
    bool ok = application_native_q2_capture_checkpoint(engine, request, &snapshot, error);
    if (ok) ok = complete(&snapshot, error) && qa_native_checkpoint_encode(&snapshot, parts, error);
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

typedef enum native_record_restore_phase {
    RECORD_PREPARE, RECORD_INITIALIZE_BEGIN, RECORD_INITIALIZE, RECORD_SCRATCH,
    RECORD_GAME, RECORD_BASELINE, RECORD_SPAWN_BEGIN, RECORD_SPAWN, RECORD_LEVEL,
    RECORD_HOST, RECORD_FINISH, RECORD_COMPLETE, RECORD_ABANDON
} native_record_restore_phase;

struct application_native_q2_record_restore {
    application_provider *provider, *current;
    struct application_native_q2 *engine;
    qa_application_options options;
    const qa_application_persistence_ops *ops;
    qa_bytes record, parts[NATIVE_RECORD_PARTS];
    qa_native_checkpoint snapshot, game;
    struct application_native_q2_scratch *scratch;
    qa_native_loading_call *call;
    native_record_restore_phase phase;
};

bool application_native_q2_record_restore_begin(application_provider *provider,
    application_provider *current, qa_bytes bytes, const qa_application_options *options,
    const qa_application_persistence_ops *ops, struct application_native_q2_record_restore **out,
    qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    qa_application *app = provider ? provider->application : NULL;
    if (!engine || !app || !options || !ops || !out || *out || engine->record_restore ||
        app->operation != APPLICATION_PERSISTING || !provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q2 restore requires its detached fresh source owner");
    if (engine->restore_record.data) {
        if (bytes.data != engine->restore_record.data || bytes.size != engine->restore_record.size ||
            !engine->initialized || !engine->map_ready || !provider->state.native.host ||
            !application_native_q2_idle(provider))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Native source restore differs from its already imported immutable image record");
    } else if (engine->initialized || engine->map_ready)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native source restore requires its fresh uninitialized owner");
    struct application_native_q2_record_restore *operation = calloc(1, sizeof(*operation));
    if (!operation) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 record restore");
    *operation = (struct application_native_q2_record_restore){.provider = provider, .current = current,
        .engine = engine, .options = *options, .ops = ops, .record = bytes,
        .phase = engine->restore_record.data ? RECORD_COMPLETE : RECORD_PREPARE};
    engine->record_restore = operation;
    *out = operation;
    return true;
}

static qa_bytes record_game_host(const struct application_native_q2_record_restore *operation)
{
    const qa_buffer *host = operation->snapshot.transition ? &operation->game.host : &operation->snapshot.host;
    return (qa_bytes){host->data, host->size};
}

static bool record_prepare(struct application_native_q2_record_restore *operation, qa_error *error)
{
    application_provider *provider = operation->provider, *current = operation->current;
    struct application_native_q2 *engine = operation->engine;
    qa_application *app = provider->application;
    qa_native_checkpoint *snapshot = &operation->snapshot, *game = &operation->game;
    if (!record_parts(provider, operation->record, operation->parts, error) ||
        !qa_native_checkpoint_decode(operation->parts[0], snapshot, error) || !complete(snapshot, error)) return false;
    if (snapshot->transition != (current != NULL))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 record differs from its actual GAME or LEVEL restore purpose");
    if (snapshot->transition) {
        struct application_native_q2 *live = current->kind == APPLICATION_PROVIDER_NATIVE
            ? current->state.native.q2_engine : NULL;
        if (!live || current == provider || current->application == app ||
            !current->constructed || !current->attached || current->close_pending ||
            current->owner != provider->owner || !current->launch ||
            !application_native_q2_launch_matches(current->launch, provider->launch) ||
            !live->initialized || !live->map_ready || live->profile != engine->profile ||
            !current->state.native.host || !application_native_q2_idle(current))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 revisit requires its current matching GAME owner");
        qa_native_checkpoint_request request = {.game = true, .host = true, .autosave = true, .transition = true};
        if (!application_native_q2_capture_checkpoint(live, request, game, error)) return false;
        if (!game->has_game || !game->game.size || !game->has_host || !game->host.size || game->has_level || game->has_process)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 revisit requires its current original GAME file");
    }
    const qa_actor_record *world = qa_actors_at_source(qa_session_actors(app->session), provider->owner, 0);
    if (!world) return application_fail(error, QA_ERROR_FORMAT, "native Q2 saved world actor is missing");
    engine->world_actor = world->id;
    if (!application_native_q2_activate(engine, error) || !application_native_q2_prepare_restore(provider, error) ||
        !qa_native_host_restore(provider->state.native.host, record_game_host(operation),
            QA_NATIVE_HOST_RESTORE_CVARS, true, error) ||
        !application_startup_source_restore(provider, engine->console, engine->cvars,
            &engine->command_context, error)) return false;
    qa_bsp_view map;
    char *entities = NULL;
    if (!qa_bsp_open(qa_resource_bytes(app->map_resource), &map, error) ||
        !qa_map_sidecars_apply_entities(app->map_sidecars, &map, error) ||
        !application_native_q2_entity_text(provider, &map, &entities, error)) return false;
    free(engine->entity_text); engine->entity_text = entities;
    return true;
}

static bool record_file_advance(struct application_native_q2_record_restore *operation,
    qa_native_restore_part part, size_t quantum, bool *returned, qa_error *error)
{
    qa_native_instance *instance = qa_native_host_instance(operation->provider->state.native.host);
    const qa_native_checkpoint *checkpoint = part == QA_NATIVE_RESTORE_GAME && operation->snapshot.transition
        ? &operation->game : &operation->snapshot;
    if (!operation->call && !qa_native_checkpoint_restore_begin(instance, checkpoint, part,
        &operation->call, error)) return false;
    if (!qa_native_loading_call_step(operation->call, quantum, returned, error)) return false;
    return !*returned || qa_native_loading_call_finish(&operation->call, error);
}

static bool record_spawn(struct application_native_q2_record_restore *operation,
    bool begin, size_t quantum, bool *returned, qa_error *error)
{
    struct application_native_q2 *source = operation->engine;
    application_provider *baseline = application_source_baseline_provider(operation->scratch);
    struct application_native_q2 *temporary = baseline->state.native.q2_engine;
    ++source->calls; ++temporary->calls;
    bool ok = begin ? qa_native_host_spawn_entities_begin(operation->provider->state.native.host,
        (const char *)operation->parts[1].data, source->entity_text,
        (const char *)operation->parts[2].data, 0, error) :
        qa_native_host_spawn_entities_advance(operation->provider->state.native.host, quantum, returned, error);
    --temporary->calls; --source->calls;
    return ok;
}

bool application_native_q2_record_restore_advance(struct application_native_q2_record_restore *operation,
    size_t quantum, bool *finished, qa_error *error)
{
    if (!operation || !finished || operation->phase == RECORD_ABANDON)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 restore needs its retained uncanceled operation");
    *finished = false;
    struct application_native_q2 *engine = operation->engine;
    application_provider *provider = operation->provider;
    qa_application *app = provider->application;
    while (operation->phase != RECORD_COMPLETE) {
        bool ok = true, returned = true;
        switch (operation->phase) {
        case RECORD_PREPARE:
            ok = record_prepare(operation, error);
            break;
        case RECORD_INITIALIZE_BEGIN:
            ++engine->calls;
            /* Init's provisional slots remain outside the saved actor graph. */
            ok = qa_native_host_initialize_begin(provider->state.native.host, 0, 0, false, error);
            --engine->calls;
            break;
        case RECORD_INITIALIZE:
            ++engine->calls;
            ok = qa_native_host_initialize_advance(provider->state.native.host, quantum, &returned, error);
            --engine->calls;
            if (ok && returned) engine->initialized = true;
            break;
        case RECORD_SCRATCH: {
            qa_application_native_baseline_services *services = NULL;
            ok = application_native_q2_baseline_services_prepare(app, &operation->options, &services, error);
            if (ok && operation->ops->prepare_native_baseline)
                ok = operation->ops->prepare_native_baseline(operation->ops->context, app, provider->owner, services, error);
            if (ok) ok = application_native_q2_scratch_prepare(provider, qa_application_launch(app), services,
                record_game_host(operation), &operation->scratch, error);
            break;
        }
        case RECORD_GAME:
            ok = record_file_advance(operation, QA_NATIVE_RESTORE_GAME, quantum, &returned, error);
            break;
        case RECORD_BASELINE:
            ok = application_native_q2_scratch_begin(operation->scratch, error) &&
                qa_native_host_restore(provider->state.native.host,
                    (qa_bytes){operation->snapshot.host.data, operation->snapshot.host.size},
                    QA_NATIVE_HOST_RESTORE_PRECACHES, false, error);
            break;
        case RECORD_SPAWN_BEGIN:
            ok = record_spawn(operation, true, quantum, &returned, error);
            break;
        case RECORD_SPAWN:
            ok = record_spawn(operation, false, quantum, &returned, error);
            break;
        case RECORD_LEVEL:
            ok = record_file_advance(operation, QA_NATIVE_RESTORE_LEVEL, quantum, &returned, error);
            break;
        case RECORD_HOST:
            ok = application_native_q2_scratch_end(operation->scratch, error) &&
                qa_native_host_restore(provider->state.native.host,
                    (qa_bytes){operation->snapshot.host.data, operation->snapshot.host.size},
                    QA_NATIVE_HOST_RESTORE_COMPLETE, !operation->snapshot.transition, error);
            if (ok) {
                const char *name = source_text(provider, engine->map_name);
                const char *spawn = source_text(provider, engine->spawn_point);
                ok = engine->initialized && engine->map_ready && name && spawn && engine->entity_text &&
                    strcmp(name, (const char *)operation->parts[1].data) == 0 &&
                    strcmp(spawn, (const char *)operation->parts[2].data) == 0;
                if (!ok) application_fail(error, QA_ERROR_FORMAT,
                    "native Q2 HOST continuation differs from its actual baseline source input");
            }
            break;
        case RECORD_FINISH:
            ok = application_native_q2_baselines_destroy(app, error);
            if (ok) engine->restore_record = operation->record;
            break;
        case RECORD_COMPLETE: case RECORD_ABANDON: break;
        }
        if (!ok) {
            operation->phase = RECORD_ABANDON;
            operation->ops = NULL;
            return false;
        }
        if (!returned) return true;
        ++operation->phase;
        if (quantum && operation->phase != RECORD_COMPLETE) return true;
    }
    *finished = true;
    return true;
}

bool application_native_q2_record_restore_dispose(struct application_native_q2_record_restore **owner,
    qa_error *error)
{
    if (!owner) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 restore owner slot is absent");
    struct application_native_q2_record_restore *operation = *owner;
    if (!operation) return true;
    if (operation->phase != RECORD_COMPLETE) {
        operation->phase = RECORD_ABANDON;
        operation->ops = NULL; operation->current = NULL;
        if (!qa_native_loading_call_abandon(&operation->call, error)) return false;
        qa_native_host *host = operation->provider->state.native.host;
        if ((host && !qa_native_host_startup_cancel(host, error)) ||
            !application_native_q2_baselines_destroy(operation->provider->application, error)) return false;
    }
    qa_native_checkpoint_free(&operation->game);
    qa_native_checkpoint_free(&operation->snapshot);
    operation->engine->record_restore = NULL;
    free(operation); *owner = NULL;
    return true;
}

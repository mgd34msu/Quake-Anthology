#include "internal.h"
#include "native_q2_checkpoint.h"
#include "native_q2_console.h"
#include "startup_flow.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/binary.h"

#include <stdlib.h>
#include <string.h>

typedef struct q2_source_record {
    qa_bytes game, cvars;
} q2_source_record;

static bool record_read(application_provider *provider, qa_bytes bytes,
    q2_source_record *out, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->state.q2 ||
        !provider->constructed || !provider->application ||
        provider->application->operation != APPLICATION_PERSISTING ||
        provider->close_pending || !bytes.data || bytes.size < 20 ||
        memcmp(bytes.data, "QAN2", 4))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid Q2 physical source continuation");
    uint64_t game_size = qa_load_u64le(bytes.data + 4);
    uint64_t cvars_size = qa_load_u64le(bytes.data + 12);
    if (!game_size || game_size > bytes.size - 20 ||
        cvars_size != bytes.size - 20 - (size_t)game_size)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid Q2 source continuation lengths");
    *out = (q2_source_record){
        .game = {bytes.data + 20, (size_t)game_size},
        .cvars = {bytes.data + 20 + (size_t)game_size, (size_t)cvars_size},
    };
    return true;
}

bool application_native_q2_checkpoint_capture(application_provider *provider,
    qa_save_purpose purpose, qa_buffer *out, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->state.q2 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !out || out->data || out->size)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 capture requires its live physical source owner");
    qa_buffer game = {0}, cvars = {0}, bundle = {0};
    bool okay = (purpose == QA_SAVE_TRANSITION ||
        application_native_q2_console_capture(provider, &cvars, error)) &&
        qa_q2_game_capture(provider->state.q2, purpose, &game, error);
    if (okay && (game.size > SIZE_MAX - 20 || cvars.size > SIZE_MAX - 20 - game.size))
        okay = application_fail(error, QA_ERROR_MEMORY, "Q2 source continuation extent overflow");
    if (okay) {
        bundle.size = 20 + game.size + cvars.size;
        bundle.data = malloc(bundle.size);
        if (!bundle.data) okay = application_fail(error, QA_ERROR_MEMORY, "Retaining the Q2 source continuation");
    }
    if (okay) {
        memcpy(bundle.data, "QAN2", 4);
        qa_store_u64le(bundle.data + 4, game.size);
        qa_store_u64le(bundle.data + 12, cvars.size);
        memcpy(bundle.data + 20, game.data, game.size);
        if (cvars.size) memcpy(bundle.data + 20 + game.size, cvars.data, cvars.size);
        *out = bundle;
    } else qa_buffer_free(&bundle);
    qa_buffer_free(&game);
    qa_buffer_free(&cvars);
    return okay;
}

bool application_native_q2_checkpoint_prepare(application_provider *provider,
    const application_provider *current, const qa_save_record *saved, qa_error *error)
{
    qa_bytes bytes = saved ? saved->payload : (qa_bytes){0};
    if (!provider || !provider->launch || provider->attached || !saved ||
        saved->owner.kind != QA_SAVE_PROVIDER || !saved->owner.instance ||
        strcmp(saved->owner.instance, provider->launch->selection.instance) ||
        !bytes.data || bytes.size < 28 || memcmp(bytes.data, "QAPV", 4) ||
        qa_load_u32le(bytes.data + 4) != APPLICATION_PROVIDER_Q2 ||
        qa_load_u32le(bytes.data + 8) > 1 ||
        qa_load_u64le(bytes.data + 20) != bytes.size - 28)
        return application_fail(error, QA_ERROR_FORMAT, "Missing actual Q2 provider source record");
    q2_source_record record;
    if (!record_read(provider, (qa_bytes){bytes.data + 28, bytes.size - 28}, &record, error))
        return false;
    qa_console *console = NULL;
    qa_cvars *cvars = NULL;
    qa_command_context command;
    if (!application_native_q2_console_at(provider, &console, &cvars, &command) ||
        qa_cvars_count(cvars) != 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source prefix requires its empty restored registry");
    qa_buffer settings = {0};
    bool okay;
    if (current) {
        okay = current->application == provider->application->native_restore_current &&
            current->kind == APPLICATION_PROVIDER_Q2 && current->constructed &&
            current->attached && !current->close_pending && current->launch &&
            !strcmp(current->launch->selection.instance, provider->launch->selection.instance) &&
            application_native_q2_console_capture((application_provider *)current, &settings, error);
        if (okay) record.cvars = (qa_bytes){settings.data, settings.size};
    } else okay = record.cvars.size != 0;
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Q2 level restore lacks its current unit Source settings");
    okay = okay && application_native_q2_console_restore(provider, record.cvars, error) &&
        application_startup_source_restore(provider, console, cvars, &command, error) &&
        application_native_q2_console_refresh(provider, error);
    qa_buffer_free(&settings);
    return okay;
}

bool application_native_q2_checkpoint_restore(application_provider *provider,
    qa_bytes bytes, qa_error *error)
{
    q2_source_record record;
    if (!record_read(provider, bytes, &record, error)) return false;
    qa_application *unit = provider->application->native_restore_current;
    const qa_launch_instance *selected = unit ? qa_launch_snapshot_find(
        qa_application_launch(unit), provider->launch->selection.instance) : NULL;
    const application_provider *current = selected ? selected->state : NULL;
    return application_native_q2_console_refresh(provider, error) &&
        qa_q2_game_restore(provider->state.q2, unit ? QA_SAVE_TRANSITION : QA_SAVE_MANUAL,
            current ? current->state.q2 : NULL, record.game, error);
}

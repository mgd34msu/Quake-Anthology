#include "internal.h"
#include "../ai/internal.h"
#include "../library/internal.h"
#include "../save_fields.h"
#include "qa/bots_save.h"
#include "qa/script_defines_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'R', 'E', 'Q', 'S', 0};

static bool signature(qa_source_save_io *io)
{
    uint8_t bytes[8]; memcpy(bytes, magic, sizeof(bytes)); uint32_t version = 2;
    return qa_source_save_bytes(io, bytes, sizeof(bytes)) && !memcmp(bytes, magic, sizeof(bytes)) &&
        qa_source_save_u32(io, &version) && version == 2 ? true :
        bot_save_fail(io, QA_ERROR_FORMAT, "Unsupported bot constructor configuration schema");
}

static bool globals_fields(qa_source_save_io *io, const qa_script_defines **globals)
{
    qa_buffer bytes = {0};
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = reading || qa_script_defines_save_capture(*globals, &bytes, io->error);
    size_t count = bytes.size;
    if (ok) ok = qa_source_save_count(io, &count, SIZE_MAX);
    if (ok && reading) {
        if (count > io->input.size - io->offset)
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot global macro constructor owner");
        else {
            qa_script_defines *restored = NULL;
            ok = qa_script_defines_save_restore((qa_bytes){io->input.data + io->offset, count}, &restored, io->error);
            if (ok) { *globals = restored; io->offset += count; }
        }
    } else if (ok) ok = qa_source_save_bytes(io, bytes.data, bytes.size);
    qa_buffer_free(&bytes);
    if (!ok) io->failed = true;
    return ok;
}

void qa_bots_save_requirements_free(qa_bots_save_requirements *requirements)
{
    if (!requirements)
        return;
    free((void *)requirements->runtime.library.preprocessor.include_path);
    free((void *)requirements->runtime.library.scripts.date);
    free((void *)requirements->runtime.library.scripts.time);
    qa_script_defines_release((qa_script_defines *)requirements->runtime.library.preprocessor.globals);
    *requirements = (qa_bots_save_requirements){0};
}

static bool requirements_fields(qa_source_save_io *io, qa_bots_save_requirements *requirements)
{
    qa_bot_runtime_options *runtime = &requirements->runtime;
    qa_script_options *preprocessor = &runtime->library.preprocessor;
    uint32_t profile = (uint32_t)runtime->observations;
    bool ok = qa_source_save_u32(io, &runtime->maximum_states) && runtime->maximum_states &&
        runtime->maximum_states <= INT32_MAX && runtime->maximum_states <= SIZE_MAX / sizeof(bot_weapon_state) &&
        qa_source_save_u32(io, &runtime->minimum_clients) && qa_source_save_u32(io, &profile) &&
        profile <= QA_BOT_OBSERVATION_MODULE && qa_source_save_bool(io, &runtime->debug) &&
        qa_source_save_bool(io, &runtime->library.reload_characters) &&
        qa_source_save_bool(io, &requirements->library_reload_characters) &&
        qa_source_save_u32(io, &preprocessor->lexer_flags) &&
        qa_source_save_count(io, &preprocessor->token_limit, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_include_depth, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_expansions, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_queued_tokens, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_output_tokens, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_defines, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_expression_tokens, SIZE_MAX) &&
        qa_source_save_count(io, &preprocessor->maximum_source_tokens, SIZE_MAX) &&
        qa_source_save_bool(io, &preprocessor->builtins) && bot_save_text(io, &preprocessor->include_path) &&
        bot_save_text(io, &runtime->library.scripts.date) && bot_save_text(io, &runtime->library.scripts.time) &&
        qa_source_save_u32(io, &requirements->action_capacity) &&
        requirements->action_capacity <= INT32_MAX / 40 &&
        requirements->action_capacity <= SIZE_MAX / sizeof(qa_bot_input) &&
        qa_source_save_bool(io, &requirements->population) &&
        qa_source_save_u32(io, &requirements->population_client_capacity) &&
        requirements->population_client_capacity <= INT32_MAX &&
        requirements->population_client_capacity <= SIZE_MAX / sizeof(bot_ai_state *) &&
        qa_source_save_u32(io, &requirements->population_actor_capacity) &&
        requirements->population_actor_capacity <= SIZE_MAX / sizeof(uint32_t) &&
        (requirements->population ||
        (!requirements->population_client_capacity && !requirements->population_actor_capacity));
    if (ok) ok = globals_fields(io, &preprocessor->globals);
    runtime->observations = (qa_bot_observation_profile)profile;
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot constructor configuration");
    return ok;
}

bool qa_bots_save_requirements_capture(const qa_bot_runtime *runtime, const qa_bots *population,
                                      qa_buffer *out, qa_error *error)
{
    if (!runtime || (!runtime->library && !runtime->closed) || !runtime->actions || !out ||
        !qa_bot_runtime_can_destroy(runtime) || runtime->restore_pending ||
        (population && (population->runtime != runtime || !qa_bots_can_destroy(population) ||
            population->restore_pending))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot constructor owners are absent, restoring or borrowed");
        return false;
    }
    const qa_bot_library_options *library = runtime->library ? &runtime->library->options : &runtime->options.library;
    if (!runtime->globals || library->preprocessor.globals != runtime->globals)
        return bot_runtime_fail(error, "Bot constructor global macro alias differs from its actual owner");
    qa_bots_save_requirements requirements = {
        .runtime = runtime->options,
        .library_reload_characters = runtime->library && runtime->library->options.reload_characters,
        .action_capacity = qa_bot_actions_capacity(runtime->actions),
        .population = population != NULL,
        .population_client_capacity = population ? population->client_capacity : 0,
        .population_actor_capacity = population ? population->actor_capacity : 0
    };
    requirements.runtime.library = *library;
    requirements.runtime.library.reload_characters = runtime->options.library.reload_characters;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && signature(&io) &&
        requirements_fields(&io, &requirements) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bots_save_requirements_read(qa_bytes bytes, qa_bots_save_requirements *out, qa_error *error)
{
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot constructor configuration output");
        return false;
    }
    qa_bots_save_requirements requirements = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io) &&
        requirements_fields(&io, &requirements) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        qa_bots_save_requirements_free(&requirements);
        return false;
    }
    *out = requirements;
    return true;
}

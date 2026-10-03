#include "internal.h"
#include <float.h>

bool qc_game_fail(qa_error *error, qa_status status, const char *message) {
    qa_error_set(error, status, 0, "%s", message); return false;
}
static bool active(void *context) {
    qa_qc_game *game = context;
    qa_qc_host *host = &game->options.vm.host;
    return host->server_active ? host->server_active(host->context) : !game->loading;
}
static uint32_t random_word(void *context) {
    qa_qc_game *game = context;
    return game->options.vm.host.random_u32(game->options.vm.host.context);
}
static double source_time_seconds(void *context) {
    qa_qc_game *game = context;
    return game->options.vm.host.source_time_seconds(game->options.vm.host.context);
}
static bool may_move(void *context, qa_actor_id actor) {
    qa_qc_game *game = context;
    return game->options.vm.host.may_move(game->options.vm.host.context, actor);
}
static bool prepare(void *context, qa_qc_instance *vm, const qa_qc_entity_access *access,
                    qa_error *error) {
    qa_qc_game *game = context;
    return game->options.vm.host.prepare_entity(game->options.vm.host.context, vm, access, error);
}
static bool unknown(void *context, qa_qc_instance *vm, int32_t number, const char *name,
                    qa_error *error) {
    qa_qc_game *game = context;
    return game->options.vm.host.unknown_builtin(game->options.vm.host.context, vm, number, name, error);
}
static bool capture_host(void *context, qa_buffer *out, qa_error *error) {
    qa_qc_game *game = context;
    qa_buffer engine = {0};
    if (!game->options.checkpoint(game->options.context, &engine, error)) {
        qa_buffer_free(&engine); return false;
    }
    if ((engine.size && !engine.data) || engine.size > SIZE_MAX - 8) {
        qa_buffer_free(&engine); return qc_game_fail(error, QA_ERROR_FORMAT, "Invalid QC engine checkpoint");
    }
    uint8_t *bytes = malloc(engine.size + 8);
    if (!bytes) { qa_buffer_free(&engine); return qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC host checkpoint"); }
    qa_store_u32le(bytes, game->options.max_clients);
    qa_store_u32le(bytes + 4, game->loading ? 1u : 0u);
    if (engine.size) memcpy(bytes + 8, engine.data, engine.size);
    *out = (qa_buffer){bytes, engine.size + 8}; qa_buffer_free(&engine); return true;
}
static bool restore_host(void *context, qa_bytes bytes, qa_error *error) {
    qa_qc_game *game = context;
    if (!bytes.data || bytes.size < 8 || qa_load_u32le(bytes.data) != game->options.max_clients ||
        qa_load_u32le(bytes.data + 4) > 1)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC host checkpoint identity differs");
    game->restoring = true;
    bool ok = game->options.restore(game->options.context,
        (qa_bytes){bytes.data + 8, bytes.size - 8}, error);
    game->restoring = false;
    if (!ok) return false;
    game->loading = qa_load_u32le(bytes.data + 4) != 0; return true;
}
bool qa_qc_game_create(const qa_qc_program *program, const qa_qc_game_options *options,
                        qa_qc_game **out, qa_error *error) {
    if (!program || !options || !out || !options->services.session || !options->services.world ||
        !options->cvars || !options->console || !options->resource || !options->services.emit ||
        !options->checkpoint || !options->restore || !options->vm.host.owner ||
        !qa_strings_text(qa_session_strings(options->services.session), options->vm.host.default_definition).data ||
        options->max_clients == UINT32_MAX || options->max_clients + 1 >= options->vm.entity_capacity ||
        (options->vm.host.builtin_count && !options->vm.host.builtins) ||
        (options->vm.host.session && options->vm.host.session != options->services.session) ||
        (options->vm.host.world && options->vm.host.world != options->services.world))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid shared QuakeC game services");
    static const qa_qc_builtin defaults[] = {
        QA_QC_BUILTIN_SETMODEL, QA_QC_BUILTIN_SOUND, QA_QC_BUILTIN_PRECACHE_SOUND,
        QA_QC_BUILTIN_PRECACHE_MODEL, QA_QC_BUILTIN_PRECACHE_FILE, QA_QC_BUILTIN_CVAR,
        QA_QC_BUILTIN_CVAR_SET, QA_QC_BUILTIN_LOCALCMD, QA_QC_BUILTIN_BPRINT,
        QA_QC_BUILTIN_SPRINT, QA_QC_BUILTIN_DPRINT, QA_QC_BUILTIN_CENTERPRINT,
        QA_QC_BUILTIN_PARTICLE, QA_QC_BUILTIN_AMBIENTSOUND, QA_QC_BUILTIN_OBJERROR,
        QA_QC_BUILTIN_BREAK, QA_QC_BUILTIN_CHANGEYAW, QA_QC_BUILTIN_WALKMOVE,
        QA_QC_BUILTIN_CHECKBOTTOM, QA_QC_BUILTIN_MOVETOGOAL
    };
    size_t n = sizeof(defaults) / sizeof(defaults[0]);
    if (options->vm.host.builtin_count > SIZE_MAX / sizeof(qa_qc_builtin_binding) - n)
        return qc_game_fail(error, QA_ERROR_MEMORY, "QC engine import count overflow");
    qa_qc_game *game = calloc(1, sizeof(*game));
    if (!game) return qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC game host");
    game->options = *options; game->program = program; game->loading = true;
    game->bindings = malloc((options->vm.host.builtin_count + n) * sizeof(*game->bindings));
    if (!game->bindings) { free(game); return qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC engine imports"); }
    size_t count = options->vm.host.builtin_count;
    if (count) memcpy(game->bindings, options->vm.host.builtins, count * sizeof(*game->bindings));
    for (size_t i = 0; i < n; ++i) {
        if (!options->services.physics && (defaults[i] == QA_QC_BUILTIN_CHANGEYAW ||
            defaults[i] == QA_QC_BUILTIN_WALKMOVE || defaults[i] == QA_QC_BUILTIN_CHECKBOTTOM ||
            defaults[i] == QA_QC_BUILTIN_MOVETOGOAL)) continue;
        bool bound = false;
        for (size_t j = 0; j < count; ++j) if (game->bindings[j].builtin == defaults[i]) { bound = true; break; }
        if (!bound) game->bindings[count++] = (qa_qc_builtin_binding){defaults[i], NULL, game, qc_game_builtin};
    }
    qa_qc_options vm = options->vm;
    vm.first_dynamic_slot = options->max_clients + 1;
    vm.require_complete_host_profile = true;
    vm.host.session = options->services.session; vm.host.world = options->services.world;
    vm.host.combat = options->services.combat; vm.host.inventory = options->services.inventory;
    vm.host.pickups = options->services.pickups;
    vm.host.builtins = game->bindings; vm.host.builtin_count = count; vm.host.context = game;
    vm.host.server_active = active;
    vm.host.random_u32 = options->vm.host.random_u32 ? random_word : NULL;
    vm.host.source_time_seconds = options->vm.host.source_time_seconds ? source_time_seconds : NULL;
    vm.host.may_move = options->vm.host.may_move ? may_move : NULL;
    vm.host.prepare_entity = options->vm.host.prepare_entity ? prepare : NULL;
    vm.host.unknown_builtin = options->vm.host.unknown_builtin ? unknown : NULL;
    vm.host.checkpoint = capture_host; vm.host.restore = restore_host;
    game->binding_count = count;
    if (!qa_qc_instance_create(program, &vm, &game->vm, error)) {
        free(game->bindings); free(game); return false;
    }
    *out = game; return true;
}
bool qa_qc_game_destroy(qa_qc_game *game, qa_error *error) {
    if (!game) return true;
    if (game->calls) return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC game host is active");
    if (!qa_qc_instance_destroy(game->vm, error)) return false;
    free(game->bindings); free(game); return true;
}
bool qa_qc_game_idle(const qa_qc_game *game) { return game && !game->calls && qa_qc_idle(game->vm); }
qa_qc_instance *qa_qc_game_instance(qa_qc_game *game) { return game ? game->vm : NULL; }
bool qa_qc_game_rebind_console(qa_qc_game *game, qa_cvars *cvars, qa_console *console, qa_error *error) {
    if (!game || !cvars || !console || (!game->restoring && (game->calls || !qa_qc_idle(game->vm))))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Cannot replace active QC console services");
    game->options.cvars = cvars; game->options.console = console; return true;
}
bool qa_qc_game_bind_client(qa_qc_game *game, uint32_t client, qa_actor_id actor, qa_error *error) {
    if (!game || !client || client > game->options.max_clients)
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC client slot is outside reservation");
    return qa_qc_bind_actor(game->vm, client, actor, QA_QC_SLOT_BORROWED, error);
}
void qa_qc_game_actor_released(qa_qc_game *game, qa_actor_record actor) {
    if (game) qa_qc_actor_released(game->vm, actor);
}
bool qa_qc_game_loading(qa_qc_game *game, bool loading, qa_error *error) {
    if (!game || game->calls || !qa_qc_idle(game->vm))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Cannot change loading during QC execution");
    game->loading = loading; return true;
}
bool qa_qc_game_reset_level(qa_qc_game *game, qa_error *error) {
    if (!game || game->calls || !qa_qc_idle(game->vm))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Cannot reset an active QC level");
    for (uint32_t slot = 1; slot < qa_qc_entity_count(game->vm); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(game->vm, slot, &binding) || binding.kind != QA_QC_SLOT_FREE)
            return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC level still has actor bindings");
    }
    qa_qc_options vm = game->options.vm;
    vm.first_dynamic_slot = game->options.max_clients + 1;
    vm.require_complete_host_profile = true;
    vm.host.session = game->options.services.session; vm.host.world = game->options.services.world;
    vm.host.combat = game->options.services.combat; vm.host.inventory = game->options.services.inventory;
    vm.host.pickups = game->options.services.pickups;
    vm.host.builtins = game->bindings;
    vm.host.builtin_count = game->binding_count;
    vm.host.context = game; vm.host.server_active = active;
    vm.host.random_u32 = game->options.vm.host.random_u32 ? random_word : NULL;
    vm.host.source_time_seconds = game->options.vm.host.source_time_seconds ? source_time_seconds : NULL;
    vm.host.may_move = game->options.vm.host.may_move ? may_move : NULL;
    vm.host.prepare_entity = game->options.vm.host.prepare_entity ? prepare : NULL;
    vm.host.unknown_builtin = game->options.vm.host.unknown_builtin ? unknown : NULL;
    vm.host.checkpoint = capture_host; vm.host.restore = restore_host;
    qa_qc_instance *replacement;
    if (!qa_qc_instance_create(game->program, &vm, &replacement, error)) return false;
    if (!qa_qc_instance_destroy(game->vm, error)) {
        qa_error ignored = {0}; qa_qc_instance_destroy(replacement, &ignored); return false;
    }
    game->vm = replacement; game->loading = true; return true;
}
bool qa_qc_game_set_time(qa_qc_game *game, double seconds, double frame, qa_error *error) {
    if (!game || !isfinite(seconds) || seconds < 0 || !isfinite(frame) || frame < 0 ||
        seconds > FLT_MAX || frame > FLT_MAX)
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC source clock");
    const qa_qc_definition *time = qa_qc_program_find_global(game->program, "time");
    const qa_qc_definition *delta = qa_qc_program_find_global(game->program, "frametime");
    float values[2] = {(float)seconds, (float)frame}; uint32_t words[2];
    memcpy(words, values, sizeof(words));
    if (time && time->type == QA_QC_FLOAT && !qa_qc_stage_globals(game->vm, time->offset, words, 1, error)) return false;
    return !delta || delta->type != QA_QC_FLOAT || qa_qc_stage_globals(game->vm, delta->offset, words + 1, 1, error);
}
bool qa_qc_game_capture(qa_qc_game *game, qa_qc_checkpoint **out, qa_error *error) {
    if (!game || game->calls) return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC game capture is not idle");
    ++game->calls; bool ok = qa_qc_checkpoint_capture(game->vm, out, error); --game->calls; return ok;
}
bool qa_qc_game_restore(qa_qc_game *game, const qa_qc_checkpoint *saved, qa_error *error) {
    if (!game || game->calls) return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC game restore is not idle");
    ++game->calls; bool ok = qa_qc_checkpoint_restore(game->vm, saved, error); --game->calls; return ok;
}

#include "internal.h"
#include "browser.h"
#include "files.h"
#include "qa/input.h"
#include "qa/bot_runtime.h"
#include "qa/scene_world_save.h"
#include "qa/q3_presentation.h"

static bool vm_read(void *context, uint64_t address, void *out, size_t size, qa_error *error)
{
    qa_bytes bytes;
    if (!q3_vm_span(context, address, size, &bytes, error))
        return false;
    if (size) memcpy(out, bytes.data, size);
    return true;
}

static bool vm_write(void *context, uint64_t address, qa_bytes bytes, qa_error *error)
{
    qa_bytes admitted;
    if (!q3_vm_span(context, address, bytes.size, &admitted, error)) return false;
    return qa_qvm_write(context, (uint32_t)(address - qa_qvm_memory_size(context)), bytes, error);
}

static bool vm_string(void *context, uint64_t address, size_t maximum,
                        qa_buffer *out, qa_error *error)
{
    qa_bytes bytes;
    if (!q3_vm_string_span(context, address, maximum, &bytes, error)) return false;
    qa_buffer copy = {.data = malloc(bytes.size + 1), .size = bytes.size + 1};
    if (!copy.data) return q3_fail(error, QA_ERROR_MEMORY, 0, "copying Q3 source string");
    memcpy(copy.data, bytes.data, bytes.size); copy.data[bytes.size] = 0; *out = copy;
    return true;
}

static bool dispatch(q3_call *call, int32_t *result, qa_error *error)
{
    static const q3_handler handlers[] = {q3_browser, q3_common, q3_cvars, q3_files,
                                         q3_information, q3_client_state, q3_scripts,
                                         q3_bot_actions, q3_bot_library, q3_bot_chat, q3_bot_goals,
                                         q3_bot_genetic, q3_bot_weapons, q3_bot_navigation, q3_bot_movement,
                                         q3_client_collision,
                                         q3_client_input, q3_client_keys, q3_game_records, q3_game_spatial,
                                         q3_entity_tokens, q3_presentation};
    *result = 0;
    qa_bot_runtime *runtime = call->host->options.bots;
    bool leased = runtime && call->host->options.role == QA_QVM_GAME &&
        call->service >= 202 && call->service != 206 && call->service != 541;
    if (leased && !qa_bot_runtime_lease_begin(runtime, error)) return false;
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        q3_service_result handled = handlers[i](call, result, error);
        if (handled != Q3_UNHANDLED) {
            if (leased) qa_bot_runtime_lease_end(runtime);
            return handled == Q3_COMPLETED;
        }
    }
    if (leased) qa_bot_runtime_lease_end(runtime);
    return q3_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)call->source_service,
                    "Q3 service owner is unbound");
}

static bool qvm_call(void *context, const qa_qvm_call *source, int32_t trap,
                       int32_t *result, qa_error *error)
{
    qa_q3_host *host = context;
    if (!host || host->retired || host->restore_pending || host->native || (host->vm && host->vm != source->vm) ||
        qa_qvm_get_role(source->vm) != host->options.role || qa_qvm_get_abi(source->vm) != host->options.abi)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 call belongs to another module host");
    const q3_signature *signature;
    q3_call call = {.host = host, .source_service = trap, .vm = source->vm, .source_call = source,
        .memory = {source->vm, 4, vm_read, vm_write, vm_string}};
    if (!q3_signature_find(host->options.role, host->options.abi, trap, &signature, &call.service, error)) return false;
    call.count = signature->count;
    for (size_t i = 0; i < call.count; ++i) {
        int32_t word;
        if (!qa_qvm_call_argument(source, i, &word, error)) return false;
        call.arguments[i] = (uint32_t)word;
        if (signature->arguments[i] == QA_NATIVE_ADDRESS && word) {
            uint32_t offset = qa_qvm_mask_address(source->vm, word);
            call.arguments[i] = (uint64_t)qa_qvm_memory_size(source->vm) + offset;
        }
    }
    host->vm = source->vm; host->memory = call.memory;
    ++host->calls;
    bool ok = dispatch(&call, result, error);
    if (!--host->calls) qa_arena_reset(&host->scratch);
    return ok;
}

static bool native_describe(void *context, qa_qvm_role role, qa_qvm_abi abi, int32_t trap,
                              const qa_native_value_type **types, size_t *count, qa_error *error)
{
    qa_q3_host *host = context;
    if (!host || host->retired || role != host->options.role || abi != host->options.abi || !types || !count)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 syscall signature request");
    const q3_signature *signature; int32_t canonical;
    if (!q3_signature_find(role, abi, trap, &signature, &canonical, error)) return false;
    *types = signature->arguments; *count = signature->count; return true;
}

static bool intrinsic_span(void *context, uint64_t raw, size_t size, uint64_t *address, qa_error *error)
{
    q3_call *call = context;
    if (!raw || raw > UINT64_MAX - size)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "invalid native Q3 intrinsic span");
    if (size) {
        void *probe = qa_arena_alloc(&call->host->scratch, size, 1, error);
        if (!probe || !q3_read(call, raw, probe, size, error)) return false;
    }
    *address = raw; return true;
}

static bool intrinsic_read(void *context, uint64_t address, void *out, size_t size, qa_error *error)
{
    return q3_read(context, address, out, size, error);
}

static bool intrinsic_write(void *context, uint64_t address, qa_bytes bytes, qa_error *error)
{
    return q3_write(context, address, bytes, error);
}

static bool intrinsic_copy(void *context, uint64_t destination, uint64_t source, size_t size, qa_error *error)
{
    q3_call *call = context;
    if (!size) return true;
    uint8_t *bytes = qa_arena_alloc(&call->host->scratch, size, 1, error);
    return bytes && q3_read(call, source, bytes, size, error) &&
           q3_write(call, destination, (qa_bytes){bytes, size}, error);
}

static bool intrinsic_fill(void *context, uint64_t address, size_t size, uint8_t value, qa_error *error)
{
    q3_call *call = context;
    if (!size) return true;
    uint8_t *bytes = qa_arena_alloc(&call->host->scratch, size, 1, error);
    if (!bytes) return false;
    memset(bytes, value, size);
    return q3_write(call, address, (qa_bytes){bytes, size}, error);
}

static bool intrinsic_length(void *context, uint64_t address, size_t limit,
                               size_t *length, bool *terminated, qa_error *error)
{
    *length = 0; *terminated = false;
    for (; *length < limit; ++*length) {
        uint8_t byte;
        if (address > UINT64_MAX - *length || !q3_read(context, address + *length, &byte, 1, error))
            return false;
        if (!byte) { *terminated = true; break; }
    }
    return true;
}

static bool native_call(void *context, const qa_native_host_q3_call *source,
                          qa_native_value *result, qa_error *error)
{
    qa_q3_host *host = context;
    if (!host || !source || host->retired || host->restore_pending || host->vm ||
        (host->native && host->native != source->instance) || source->role != host->options.role ||
        source->abi != host->options.abi)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "native Q3 call belongs to another module host");
    bool fixed = source->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10;
    const q3_signature *signature;
    q3_call call = {.host = host, .source_service = source->source_service,
                     .memory = source->memory, .native = source->instance,
                     .native_host = source->host,
                     .native_profile = source->profile};
    host->native_host = source->host; host->memory = source->memory; host->native_profile = source->profile;
    if (fixed) {
        if (!source->fixed_signature || source->role != QA_QVM_GAME ||
            source->fixed_signature->parameter_count > 16 ||
            !q3_ql_service(source->source_service, &call.service, error)) return false;
        call.count = source->fixed_signature->parameter_count;
    } else {
        if (source->profile != QA_NATIVE_Q3_VMMAIN ||
            !q3_signature_find(source->role, source->abi, source->source_service, &signature, &call.service, error)) return false;
        call.count = signature->count;
    }
    if (source->argument_count != call.count || (call.count && !source->arguments))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "native Q3 argument count disagrees with source signature");
    for (size_t i = 0; i < call.count; ++i) {
        const qa_native_value *argument = &source->arguments[i];
        qa_native_value_type expected = fixed ? source->fixed_signature->parameters[i].kind : signature->arguments[i];
        if (argument->type != expected)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "native Q3 argument type disagrees with source signature");
        if (argument->type == QA_NATIVE_ADDRESS) call.arguments[i] = argument->as.address;
        else if (argument->type == QA_NATIVE_I32) call.arguments[i] = (uint32_t)argument->as.i32;
        else if (argument->type == QA_NATIVE_F32) {
            uint32_t bits; memcpy(&bits, &argument->as.f32, sizeof(bits)); call.arguments[i] = bits;
        } else return q3_fail(error, QA_ERROR_UNSUPPORTED, i, "unsupported Q3 fixed import scalar type");
    }
    if (fixed && source->source_service == 0) {
        call.arguments[1] = call.arguments[0]; call.arguments[0] = 2; call.count = 2;
    }
    host->native = source->instance;
    ++host->calls;
    size_t count; uint32_t pointers; bool address_result;
    qa_native_value value = {.type = QA_NATIVE_I32};
    bool ok;
    if (!fixed && qa_q3_abi_intrinsic_signature(source->role, source->abi, source->source_service,
                                      &count, &pointers, &address_result)) {
        qa_q3_abi_memory memory = {&call, intrinsic_span, intrinsic_read, intrinsic_write,
                                     intrinsic_copy, intrinsic_fill, intrinsic_length};
        uint64_t word;
        ok = qa_q3_abi_intrinsic(source->role, source->abi, source->source_service,
                                  call.arguments, call.count, &memory, &word, error);
        if (ok && address_result) { value.type = QA_NATIVE_ADDRESS; value.as.address = word; }
        else if (ok) { uint32_t bits = (uint32_t)word; memcpy(&value.as.i32, &bits, sizeof(bits)); }
    } else {
        ok = dispatch(&call, &value.as.i32, error);
        if (ok && fixed) {
            qa_native_value_type type = source->fixed_signature->result.kind;
            if (type == QA_NATIVE_F32) {
                int32_t bits = value.as.i32; memcpy(&value.as.f32, &bits, sizeof(bits));
            } else if (type != QA_NATIVE_I32 && type != QA_NATIVE_VOID) {
                ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 fixed import needs its typed result owner");
            }
            value.type = type;
        }
    }
    if (!--host->calls) qa_arena_reset(&host->scratch);
    if (ok && result) *result = value;
    return ok;
}

static bool checkpoint(void *context, qa_buffer *state, qa_error *error)
{
    return qa_q3_host_checkpoint(context, state, error);
}
static bool restore(void *context, qa_bytes state, qa_error *error)
{
    return qa_q3_host_restore(context, state, error);
}

qa_console *qa_q3_host_console(const qa_q3_host *host, qa_cvars **cvars, qa_command_context *context)
{
    if (!host || host->retired || !host->options.console) return NULL;
    if (cvars) *cvars = host->options.cvars;
    if (context) *context = host->options.command_context;
    return host->options.console;
}

bool qa_q3_host_client_context_read(const qa_q3_host *host, qa_q3_host_client_context *out)
{
    if (!host || !out || host->retired || host->options.role == QA_QVM_GAME ||
        !host->options.owner || !host->options.service_owner || !host->options.cvars ||
        !host->options.console) return false;
    *out = (qa_q3_host_client_context){.session = host->options.session,
        .role = host->options.role, .owner = host->options.owner,
        .service_owner = host->options.service_owner, .console = host->options.console,
        .cvars = host->options.cvars, .command_context = host->options.command_context,
        .client_time_cvars = host->options.client_time_cvars,
        .client_time_owner = host->options.client_time_owner,
        .frontend_lifetime = host->options.frontend_lifetime};
    return true;
}
bool qa_q3_host_end_registration(qa_q3_host *host,qa_error *error)
{
    if(!host||host->options.role!=QA_QVM_CGAME||host->retired)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Renderer registration requires its actual CG host");
    return !host->options.presentation.end_registration||
        host->options.presentation.end_registration(host->options.presentation.context,error);
}
qa_q3_presentation_assets *qa_q3_host_presentation_resources(const qa_q3_host *host)
{
    return host && !host->retired && host->options.presentation.seat ?
        qa_q3_presentation_resources(host->options.presentation.seat) : NULL;
}

bool qa_q3_host_create(const qa_q3_host_options *options, qa_q3_host **out, qa_error *error)
{
    if (!options || !out || (unsigned)options->role > QA_QVM_UI ||
        (unsigned)options->abi > QA_QVM_Q3_116N ||
        (options->role == QA_QVM_GAME && !options->owner) ||
        options->client_time_from_game ||
        (!!options->client_time_cvars != !!options->client_time_owner) ||
        (options->role == QA_QVM_GAME && options->client_time_cvars) ||
        (options->script_globals_owner && (options->role == QA_QVM_GAME || !options->script_globals)) ||
        (options->role != QA_QVM_GAME && options->script_globals && !options->script_globals_owner) ||
        (options->engine_cvars && (options->role != QA_QVM_GAME || !options->cvars ||
            qa_cvars_dialect(options->engine_cvars) != QA_RULESET_Q3 ||
            qa_cvars_dialect(options->cvars) != QA_RULESET_Q3)) ||
        (!!options->frontend_lifetime != !!options->release_frontend) ||
        (!!options->source_entity != !!options->source_entity_context) ||
        (!!options->source_poly != !!options->source_poly_context) ||
        (!!options->source_light != !!options->source_light_context) ||
        (!!options->cvar_namespaces.reference != !!options->cvar_namespaces.resolve) ||
        (!!options->cvar_entry.context != !!options->cvar_entry.entered) ||
        (options->cvar_entry.entered && !options->console) ||
        (!!options->cvar_status.context != !!options->cvar_status.visible) ||
        (options->cvar_status.visible && options->role!=QA_QVM_CGAME) ||
        (!!options->input.context != !!options->input.bindings) ||
        (options->input.bindings && (!options->seat || options->role == QA_QVM_GAME)) ||
        (options->source_entity && options->role != QA_QVM_CGAME) ||
        ((options->source_poly || options->source_light) && options->role != QA_QVM_CGAME) ||
        (!!options->client.ui_state_context != !!options->client.ui_state) ||
        (options->client.ui_state && options->role != QA_QVM_UI) ||
        (!!options->presentation.system_movie_context != !!options->presentation.system_movie) ||
        (options->presentation.system_movie && options->role == QA_QVM_GAME) ||
        options->server.maximum_clients > 64)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 module host options");
    if (!qa_q3_host_browser_services_validate(&options->browser,error) ||
        (options->browser.context && options->role!=QA_QVM_UI))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 browser services require their actual UI host");
    qa_fs_root *write_root=NULL;
    if (!q3_write_view_root(options,&write_root,error)) return false;
    qa_q3_host *host = calloc(1, sizeof(*host));
    if (!host) return q3_fail(error, QA_ERROR_MEMORY, 0, "allocating Q3 module host");
    host->options = *options;
    host->no_curves = qa_cvars_resolve(options->cvars, "cm_noCurves");
    host->player_curve_clip = qa_cvars_resolve(options->cvars, "cm_playerCurveClip");
    if (!host->options.service_owner)
        host->options.service_owner = options->owner ? options->owner : (uint64_t)(uintptr_t)host;
    if (options->role == QA_QVM_GAME) {
        if (!options->session || !options->world) {
            free(host); return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 game host requires the shared session and world");
        }
        if (!host->options.server.maximum_clients) host->options.server.maximum_clients = 64;
        host->game = calloc(1, sizeof(*host->game));
        if (!host->game) { free(host); return q3_fail(error, QA_ERROR_MEMORY, 0, "allocating Q3 game record bindings"); }
        if (!qa_strings_intern_cstr(qa_session_strings(options->session), "q3:guest-slot", &host->slot_definition, error)) {
            free(host->game); free(host); return false;
        }
        for (uint32_t i = 0; i < 1024; ++i) host->game->slots[i] = (q3_entity_slot){.host = host, .number = i};
    }
    if (!host->options.maximum_string_bytes) host->options.maximum_string_bytes = 1024u * 1024u;
    if (!qa_common_cursor_init(&host->entity_cursor, options->entity_text, QA_COMMON_TERMINATED, error)) {
        free(host->game); free(host); return false;
    }
    qa_arena_init(&host->scratch, 16384);
    if (host->options.maximum_string_bytes > SIZE_MAX / 16 ||
        !qa_arena_reserve(&host->scratch, host->options.maximum_string_bytes * 16, error)) {
        qa_arena_destroy(&host->scratch); free(host->game); free(host); return false;
    }
    qa_arena_seal(&host->scratch);
    if (!q3_script_namespace_bind(host, options->script_globals, error)) {
        qa_arena_destroy(&host->scratch); free(host->game); free(host); return false;
    }
    qa_fs_root_retain(write_root); host->options.write_view.root=write_root;
    *out = host; return true;
}

bool qa_q3_host_borrows_bots(const qa_q3_host *host, const qa_bot_runtime *runtime)
{
    return host && runtime && host->options.bots == runtime;
}

static bool destroy_ready(const qa_q3_host *host, qa_error *error)
{
    if (!host) return true;
    if (host->calls || host->collision_holds || (host->options.world && !qa_world_idle(host->options.world)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "cannot destroy a Q3 module host during active service callbacks");
    if (host->script_namespace) {
        if (host->script_namespace->reporting)
            return q3_fail(error,QA_ERROR_ARGUMENT,0,"Cannot destroy a Q3 host during shared PC shutdown reporting");
        for (size_t i=1;i<64;++i)
            if (host->script_namespace->pending[i] ||
                (host->script_namespace->scripts[i] && host->script_namespace->scripts[i]->operations))
                return q3_fail(error,QA_ERROR_ARGUMENT,i,"Cannot destroy a Q3 host during shared PC source operations");
    }
    if (host->game) for (size_t i = 0; i < 1022; ++i) {
        const q3_entity_slot *slot = &host->game->slots[i];
        if (slot->input_motion)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "cannot destroy a Q3 host with an admitted input scope");
        if (!slot->borrowed && slot->actor.registry &&
            qa_actors_get(qa_session_actors(host->options.session), slot->actor))
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "retire owned Q3 actors before destroying their bound source host");
    }
    return true;
}

bool qa_q3_host_destroy_ready(const qa_q3_host *host)
{
    return destroy_ready(host, NULL);
}

bool qa_q3_host_close_map(qa_q3_host *host, qa_error *error)
{
    if (!host) return true;
    if (host->calls || host->retired || (host->options.world && !qa_world_idle(host->options.world)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 map portal cleanup requires an idle live host");
    return q3_game_close_portals(host, error);
}

bool qa_q3_host_round_ready(const qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || !host->game ||
        host->options.role != QA_QVM_GAME || host->calls || host->script_namespace->reporting ||
        (host->vm && !qa_qvm_can_destroy(host->vm)) ||
        (host->native && !qa_native_can_destroy(host->native)) ||
        !qa_session_safe(host->options.session) || !qa_world_idle(host->options.world) ||
        (host->options.bots && !qa_bot_runtime_can_destroy(host->options.bots)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 round restart requires idle GAME services and input");
    for (size_t i = 0; i < 1024; ++i)
        if (host->game->slots[i].input_motion)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 round restart has an admitted input motion");
    for (size_t i = 0; i < 64; ++i)
        if (host->script_namespace->pending[i] || (host->script_namespace->scripts[i] && host->script_namespace->scripts[i]->operations))
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 round restart has an admitted script operation");
    for (const q3_crossings *lease = host->crossings; lease; lease = lease->next)
        if (lease->busy)
            return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 round restart has a borrowed navigation crossing");
    return true;
}

bool qa_q3_host_source_entity(qa_q3_host *host, uint32_t number, qa_q3_entity *entity,
    qa_qvm_entity_shared *shared, qa_error *error)
{
    q3_call call;
    if ((!entity && !shared) || !q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_entity_record(&call, number, &record, error) &&
        (!entity || qa_q3_abi_read_entity(&record.abi, 0, true, entity, error)) &&
        (!shared || qa_q3_abi_read_shared_entity(&record.abi, 0, shared, error));
    return q3_game_end(&call, ok);
}

bool qa_q3_host_round_reset(qa_q3_host *host, qa_bytes entity_text, qa_error *error)
{
    if (!qa_q3_host_round_ready(host, error)) return false;
    for (size_t i = 0; i < 1024; ++i)
        if (host->game->slots[i].actor.registry)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Retire Q3 source bindings before round reset");
    qa_common_cursor cursor;
    if (!qa_common_cursor_init(&cursor, entity_text, QA_COMMON_TERMINATED, error)) return false;
    if (!q3_game_close_portals(host, error)) return false;
    q3_game_fields_clear(host);
    host->game->entities = host->game->clients = 0;
    host->game->entity_count = host->game->entity_stride = host->game->client_stride = 0;
    for (uint32_t i = 0; i < 1024; ++i)
        host->game->slots[i] = (q3_entity_slot){.host = host, .number = i};
    host->options.entity_text = entity_text;
    host->entity_cursor = cursor;
    qa_common_parser_reset(&host->entity_parser);
    host->bots_shutdown = false;
    return true;
}

bool qa_q3_host_frontend_rebind_ready(const qa_q3_host *host, const qa_scene_frame *current,
    const void *current_context, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || host->calls ||
        (host->vm && qa_qvm_active(host->vm)) || (host->native && qa_native_active(host->native)) ||
        (host->options.world && !qa_world_idle(host->options.world)) ||
        (host->options.scene_frame && host->options.scene_frame != current) ||
        (host->options.frontend_lifetime && !current_context))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 frontend frame exchange requires an idle matching host");
    if (host->game) for (size_t i = 0; i < 1022; ++i)
        if (host->game->slots[i].input_motion)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 frontend exchange has an admitted input motion");
    return true;
}

void qa_q3_host_frontend_rebind(qa_q3_host *host, qa_scene_frame *destination,
    const void *current_context, void *destination_context)
{
    if (host->options.scene_frame) host->options.scene_frame = destination;
    if (host->options.client.context && host->options.client.context == current_context)
        host->options.client.context = destination_context;
}

const qa_scene_world *qa_q3_host_scene_world(const qa_q3_host *host)
{
    return host ? host->options.scene_world : NULL;
}
bool qa_q3_host_scene_world_rebind_ready(const qa_q3_host *host, const qa_scene_world *current,
    const qa_scene_world *destination, qa_error *error)
{
    if (!host || host->retired || host->calls ||
        (host->vm && qa_qvm_active(host->vm)) || (host->native && qa_native_active(host->native)) ||
        (host->options.world && !qa_world_idle(host->options.world)) || host->options.scene_world != current ||
        (current && !qa_scene_world_idle(current)) || (destination && !qa_scene_world_idle(destination)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 renderer world exchange requires idle qualified borrowed owners");
    if (host->game) for (size_t i = 0; i < 1022; ++i)
        if (host->game->slots[i].input_motion)
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 renderer world exchange has an admitted input motion");
    return true;
}
void qa_q3_host_scene_world_rebind(qa_q3_host *host, qa_scene_world *destination)
{
    host->options.scene_world = destination;
}

bool qa_q3_host_destroy(qa_q3_host *host, qa_error *error)
{
    if (!host) return true;
    if (!destroy_ready(host, error)) return false;
    if (!q3_game_close_portals(host, error)) return false;
    if (host->options.console && !qa_console_remove_owner(host->options.console, host->options.service_owner, error)) return false;
    host->retired = true;
    q3_collision_scene_close(host);
    if (host->options.seat && !host->options.input_owner)
        qa_input_seat_retire_catcher(host->options.seat, host->options.service_owner);
    if (host->options.cvars) qa_cvars_remove_owner(host->options.cvars, host->options.service_owner);
    qa_error close_fault={0}; bool files_closed=true;
    for (size_t i = 1; i < 64; ++i) {
        qa_error fault={0};
        if (!q3_file_close_checked(&host->files[i],&fault)) {
            files_closed=false;
            if (close_fault.code==QA_OK) close_fault=fault;
        }
    }
    if (!files_closed) {
        if (error && error->code==QA_OK) *error=close_fault;
        return false;
    }
    qa_fs_root_close(host->options.write_view.root); host->options.write_view.root=NULL;
    q3_script_namespace_release(host);
    while (host->crossings) {
        q3_crossings *lease = host->crossings;
        host->crossings = lease->next;
        qa_nav_crossings_free(&lease->storage); free(lease);
    }
    qa_arena_destroy(&host->scratch);
    if (host->game) free(host->game->portals);
    free(host->game);
    q3_cvars_bindings_free(host->cvar_bindings,host->cvar_binding_count);
    free(host->cvar_caches);
    free(host->cvar_status.previous_value);
    void *frontend_lifetime=host->options.frontend_lifetime;
    void (*release_frontend)(void *)=host->options.release_frontend;
    host->options.frontend_lifetime=NULL;host->options.release_frontend=NULL;
    if(release_frontend) release_frontend(frontend_lifetime);
    free(host); return true;
}

bool qa_q3_host_attach_qvm(qa_q3_host *host, qa_qvm *vm, qa_error *error)
{
    if (!host || host->retired || !vm || host->native || (host->vm && host->vm != vm) ||
        host->options.role != qa_qvm_get_role(vm) || host->options.abi != qa_qvm_get_abi(vm))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 host executor identity mismatch");
    host->vm = vm; host->memory = (qa_native_host_guest_memory){vm, 4, vm_read, vm_write, vm_string};
    return true;
}

bool qa_q3_host_attach_native(qa_q3_host *host, qa_native_host *native, qa_error *error)
{
    qa_native_instance *instance = native ? qa_native_host_instance(native) : NULL;
    if (!host || host->retired || !instance || host->vm || (host->native && host->native != instance))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 host executor identity mismatch");
    qa_native_host_guest_memory memory;
    if (!qa_native_host_q3_memory(native, host->options.role, host->options.abi, &memory, error)) return false;
    host->native = instance; host->native_host = native; host->native_profile = qa_native_host_profile(native);
    host->memory = memory; return true;
}

void qa_q3_host_native_consumed(qa_q3_host *host)
{
    if (!host) return;
    q3_game_fields_clear(host);
    free(host->cvar_caches); host->cvar_caches=NULL; host->cvar_cache_count=0;
    free(host->cvar_status.previous_value); host->cvar_status=(q3_cvar_status){0};
    host->native = NULL;
    host->native_host = NULL;
    host->memory = (qa_native_host_guest_memory){0};
    if (host->game) {
        host->game->entities = host->game->clients = 0;
        host->game->entity_count = host->game->entity_stride = host->game->client_stride = 0;
    }
}

void qa_q3_host_qvm_consumed(qa_q3_host *host)
{
    if (!host) return;
    q3_game_fields_clear(host);
    free(host->cvar_caches); host->cvar_caches=NULL; host->cvar_cache_count=0;
    free(host->cvar_status.previous_value); host->cvar_status=(q3_cvar_status){0};
    host->vm = NULL;
    host->memory = (qa_native_host_guest_memory){0};
    if (host->game) {
        host->game->entities = host->game->clients = 0;
        host->game->entity_count = host->game->entity_stride = host->game->client_stride = 0;
    }
}

qa_qvm_options qa_q3_host_qvm_options(qa_q3_host *host, qa_qvm_semantics semantics)
{
    return (qa_qvm_options){.role = host->options.role, .abi = host->options.abi,
        .semantics = semantics, .syscall = qvm_call, .context = host,
        .checkpoint = checkpoint, .restore = restore};
}

qa_native_host_q3_bridge qa_q3_host_native_bridge(qa_q3_host *host)
{
    return (qa_native_host_q3_bridge){.context = host, .describe_native = native_describe,
        .dispatch = native_call, .checkpoint = checkpoint, .restore = restore,
        .entity_changed = q3_game_fields_changed};
}

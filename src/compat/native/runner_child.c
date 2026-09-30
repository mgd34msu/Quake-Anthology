#include "protocol.h"
#include "region_service.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct native_child_state {
    native_runner_connection connection;
    qa_native_module *module;
    qa_native_instance *instance;
    qa_native_value_type syscall_types[NATIVE_MAX_ARGUMENTS];
    size_t syscall_type_count;
    bool stop;
} native_child_state;

static native_child_state *region_child;

static bool child_handle_request(native_child_state *state, const native_wire_frame *frame,
                                 qa_error *transport_error);

static bool child_send_reply(native_child_state *state, const native_wire_frame *request,
                             const native_wire_buffer *body, const qa_error *failure,
                             qa_error *error) {
    native_wire_buffer payload = {0};
    bool ok = native_wire_put_error(&payload, failure, error);
    if (ok && !failure && body)
        ok = native_wire_put_raw(&payload, body->data, body->size, error);
    if (ok)
        ok = native_wire_send(&state->connection, (uint16_t)(request->opcode | NATIVE_WIRE_REPLY),
                              request->sequence, (qa_bytes){payload.data, payload.size}, NULL,
                              error);
    native_wire_buffer_free(&payload);
    return ok;
}

static bool child_request(native_child_state *state, uint16_t opcode, qa_bytes payload,
                          qa_buffer *response, qa_error *error) {
    native_runner_connection *connection = &state->connection;
    if (connection->depth == UINT32_MAX)
        return native_fail(error, QA_ERROR_ARGUMENT, connection->depth,
                           "native runner callback depth is exhausted");
    ++connection->depth;
    native_hooks_depth(connection->depth);
    uint64_t sequence;
    if (!native_wire_send(connection, opcode, 0, payload, &sequence, error)) {
        --connection->depth;
        native_hooks_depth(connection->depth);
        return false;
    }
    for (;;) {
        native_wire_frame frame = {0};
        if (!native_wire_receive(connection, &frame, error)) {
            --connection->depth;
            native_hooks_depth(connection->depth);
            return false;
        }
        bool reply =
            frame.opcode == (uint16_t)(opcode | NATIVE_WIRE_REPLY) && frame.reply_to == sequence;
        bool request = !(frame.opcode & NATIVE_WIRE_REPLY) && !frame.reply_to;
        if ((reply && frame.depth != connection->depth) ||
            (request &&
             (connection->depth == UINT32_MAX || frame.depth != connection->depth + 1u))) {
            native_fail(error, QA_ERROR_FORMAT, frame.depth,
                        "native runner request depth is invalid");
            native_wire_frame_free(&frame);
            native_wire_poison(connection, error);
            --connection->depth;
            native_hooks_depth(connection->depth);
            return false;
        }
        if (reply) {
            native_wire_reader reader = {.bytes = {frame.payload.data, frame.payload.size}};
            bool remote_ok = false;
            if (!native_wire_get_error(&reader, &remote_ok, error)) {
                native_wire_frame_free(&frame);
                native_wire_poison(connection, error);
                --connection->depth;
                native_hooks_depth(connection->depth);
                return false;
            }
            if (!remote_ok) {
                native_wire_poison(connection, error);
                native_wire_frame_free(&frame);
                --connection->depth;
                native_hooks_depth(connection->depth);
                return false;
            }
            size_t remaining = reader.bytes.size - reader.offset;
            if (remaining)
                memmove(frame.payload.data, frame.payload.data + reader.offset, remaining);
            frame.payload.size = remaining;
            *response = frame.payload;
            frame.payload = (qa_buffer){0};
            native_wire_frame_free(&frame);
            --connection->depth;
            native_hooks_depth(connection->depth);
            return true;
        }
        if (request) {
            bool handled = child_handle_request(state, &frame, error);
            native_wire_frame_free(&frame);
            if (!handled) {
                native_wire_poison(connection, error);
                --connection->depth;
                native_hooks_depth(connection->depth);
                return false;
            }
            continue;
        }
        native_wire_frame_free(&frame);
        native_fail(error, QA_ERROR_FORMAT, 0, "native runner request ordering is invalid");
        native_wire_poison(connection, error);
        --connection->depth;
        native_hooks_depth(connection->depth);
        return false;
    }
}

static bool child_encode_values(native_wire_buffer *buffer, const qa_native_value *arguments,
                                size_t count, qa_error *error) {
    if (count > NATIVE_MAX_ARGUMENTS || (count && !arguments) ||
        !native_wire_put_u64(buffer, count, error))
        return native_fail(error, QA_ERROR_ARGUMENT, count,
                           "native runner callback arguments are invalid");
    for (size_t index = 0; index < count; ++index)
        if (!native_wire_put_value(buffer, &arguments[index], error))
            return false;
    return true;
}

static bool child_finish_response(native_child_state *state, bool received, bool decoded,
                                  qa_error *error) {
    if (received && !decoded) {
        qa_error fallback = {0};
        const qa_error *failure = error;
        if (!error || error->code == QA_OK) {
            native_fail(&fallback, QA_ERROR_FORMAT, 0,
                        "native runner callback response is invalid");
            if (error)
                *error = fallback;
            failure = &fallback;
        }
        native_wire_poison(&state->connection, failure);
    }
    return received && decoded;
}

#if defined(_WIN32)
__declspec(dllexport) __declspec(noinline)
#else
__attribute__((visibility("default"), noinline))
#endif
void qa_native_runner_region_resume(native_region_service *packet) {
    /* The DR resume clean call replaces this body. Uninstrumented execution
     * must never return into the source instruction through an ordinary ret. */
    volatile uint64_t magic = packet->magic;
    (void)magic;
    _Exit(EXIT_FAILURE);
}

#if defined(_WIN32)
__declspec(dllexport) __declspec(noinline)
#else
__attribute__((visibility("default"), noinline))
#endif
void qa_native_runner_region_service(native_region_service *packet) {
    _Static_assert(sizeof(native_region_state) == sizeof(qa_native_processor_state),
                   "region processor packet must match its explicit state fields");
    native_child_state *state = region_child;
    if (!state || !state->instance || native_active_instance != state->instance ||
        state->connection.poisoned || packet->magic != NATIVE_REGION_SERVICE_MAGIC ||
        !packet->cookie || packet->depth != state->connection.depth || packet->phase > 1u ||
        packet->completed)
        _Exit(EXIT_FAILURE);
    native_hook_control control = {.operation = NATIVE_HOOK_REGION_SERVICE,
        .address = (uint64_t)(uintptr_t)packet, .id = packet->cookie, .size = packet->depth};
    if (!native_hooks_control(&control)) _Exit(EXIT_FAILURE);
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    qa_error error = {0};
    qa_native_processor_state before;
    memcpy(&before, &packet->before, sizeof(before));
    bool encoded = native_wire_put_u32(&request, packet->id, &error) &&
        native_wire_put_u32(&request, packet->phase, &error) &&
        native_wire_put_state(&request, &before, &error);
    bool received = encoded && child_request(state, NATIVE_WIRE_REGION_APP,
        (qa_bytes){request.data, request.size}, &response, &error);
    native_wire_reader reader = {.bytes = {response.data, response.size}};
    uint32_t action = 0; uint8_t replace = 0; qa_native_processor_state after = {0};
    bool decoded = received && native_wire_get_u32(&reader, &action, &error) &&
        native_wire_get_u8(&reader, &replace, &error) && replace <= 1u &&
        native_wire_get_state(&reader, &after, &error) && native_wire_end(&reader, &error) &&
        action <= QA_NATIVE_REGION_FAIL_INSTANCE;
    bool ok = child_finish_response(state, received, decoded, &error);
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    if (!ok || action == QA_NATIVE_REGION_FAIL_INSTANCE ||
        state->connection.depth != packet->depth || native_active_instance != state->instance)
        _Exit(EXIT_FAILURE);
    packet->action = action;
    packet->replace_state = replace;
    memcpy(&packet->after, &after, sizeof(after));
    packet->completed = 1;
    qa_native_runner_region_resume(packet);
    _Exit(EXIT_FAILURE);
}

static size_t child_import_result_bytes(native_child_state *state, uint32_t slot) {
    if (!state->instance)
        return 0;
    for (size_t index = 0; index < state->instance->import_count; ++index) {
        native_import_binding *binding = &state->instance->imports[index];
        if (binding->spec.slot == slot)
            return binding->ffi.result ? binding->ffi.result->size : 0;
    }
    return 0;
}

static bool child_attach_callback(native_child_state *state, qa_native_instance *instance,
                                  qa_native_instance **previous, qa_error *error) {
    if (!instance || (state->instance && state->instance != instance) ||
        (!state->instance && (native_active_instance != instance ||
                              instance->module != state->module)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner callback belongs to another instance");
    *previous = state->instance;
    state->instance = instance;
    return true;
}

static bool child_import(void *context, qa_native_instance *instance,
                         const qa_native_import_call *call, qa_native_value *result,
                         qa_error *error) {
    native_child_state *state = context;
    qa_native_instance *previous;
    if (!child_attach_callback(state, instance, &previous, error))
        _Exit(EXIT_FAILURE);
    native_wire_buffer request = {0};
    qa_buffer response = {0}, storage = {0};
    size_t result_bytes = child_import_result_bytes(state, call->slot);
    bool received = false;
    bool ok = native_wire_put_u32(&request, (uint32_t)call->profile, error) &&
              native_wire_put_u32(&request, call->slot, error) &&
              native_wire_put_u64(&request, result_bytes, error) &&
              child_encode_values(&request, call->arguments, call->argument_count, error);
    if (ok)
        received = child_request(state, NATIVE_WIRE_IMPORT, (qa_bytes){request.data, request.size},
                                 &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        qa_native_value decoded = {0};
        ok = native_wire_get_value(&reader, &decoded, &storage, error) &&
             native_wire_end(&reader, error) && decoded.type == call->signature->result.kind;
        if (ok && decoded.type == QA_NATIVE_BYTES) {
            if (result->type != QA_NATIVE_BYTES || !result->as.bytes.data ||
                result->as.bytes.size != result_bytes || storage.size != result_bytes)
                ok = native_fail(error, QA_ERROR_FORMAT, storage.size,
                                 "native runner import aggregate result differs from its actual ABI extent");
            else {
                memcpy(result->as.bytes.data, storage.data, storage.size);
                result->as.bytes.size = storage.size;
            }
        } else if (ok) {
            *result = decoded;
        }
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    qa_buffer_free(&storage);
    bool completed = child_finish_response(state, received, ok, error);
    state->instance = previous;
    if (!completed) _Exit(EXIT_FAILURE);
    return true;
}

static bool child_describe_syscall(void *context, int32_t service,
                                   const qa_native_value_type **types, size_t *count,
                                   qa_error *error) {
    native_child_state *state = context;
    qa_native_instance *previous;
    if (!child_attach_callback(state, native_active_instance, &previous, error))
        _Exit(EXIT_FAILURE);
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u32(&request, (uint32_t)service, error);
    if (ok)
        received = child_request(state, NATIVE_WIRE_DESCRIBE_SYSCALL,
                                 (qa_bytes){request.data, request.size}, &response, error);
    uint64_t encoded_count = 0;
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_get_u64(&reader, &encoded_count, error) &&
             encoded_count <= NATIVE_MAX_ARGUMENTS;
        for (size_t index = 0; ok && index < (size_t)encoded_count; ++index) {
            uint32_t type;
            ok = native_wire_get_u32(&reader, &type, error) && type > QA_NATIVE_VOID &&
                 type <= QA_NATIVE_ADDRESS;
            if (ok)
                state->syscall_types[index] = (qa_native_value_type)type;
        }
        if (ok)
            ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    bool completed = child_finish_response(state, received, ok, error);
    state->instance = previous;
    if (!completed)
        _Exit(EXIT_FAILURE);
    state->syscall_type_count = (size_t)encoded_count;
    *types = state->syscall_types;
    *count = state->syscall_type_count;
    return true;
}

static bool child_syscall(void *context, qa_native_instance *instance, int32_t service,
                          const qa_native_value *arguments, size_t argument_count, intptr_t *result,
                          qa_error *error) {
    native_child_state *state = context;
    qa_native_instance *previous;
    if (!child_attach_callback(state, instance, &previous, error))
        _Exit(EXIT_FAILURE);
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u32(&request, (uint32_t)service, error) &&
              child_encode_values(&request, arguments, argument_count, error);
    if (ok)
        received = child_request(state, NATIVE_WIRE_SYSCALL, (qa_bytes){request.data, request.size},
                                 &response, error);
    uint64_t encoded = 0;
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_get_u64(&reader, &encoded, error) && native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    bool completed = child_finish_response(state, received, ok, error);
    state->instance = previous;
    if (completed) {
        *result = (intptr_t)encoded;
        return true;
    }
    _Exit(EXIT_FAILURE);
}

void native_runner_child_failure(qa_native_instance *instance, const qa_error *error) {
    native_child_state *state = region_child;
    if (!state || !instance || instance->options.context != state ||
        instance->options.import != child_import || instance->module != state->module)
        return;
    /* No transport or owner-thread failure latch is accessed by a foreign
     * source thread. Its callback cannot return a default result into C. */
    if (native_active_instance != instance ||
        (state->instance && state->instance != instance)) _Exit(EXIT_FAILURE);
    qa_error failure = error ? *error : (qa_error){0};
    if (failure.code == QA_OK) failure.code = QA_ERROR_ARGUMENT;
    if (!failure.message[0])
        snprintf(failure.message, sizeof(failure.message), "native source callback failed");
    qa_native_instance *previous = state->instance;
    state->instance = instance;
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    qa_error transport = {0};
    if (native_wire_put_error(&request, &failure, &transport))
        (void)child_request(state, NATIVE_WIRE_SOURCE_FAILURE,
            (qa_bytes){request.data, request.size}, &response, &transport);
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    state->instance = previous;
    _Exit(EXIT_FAILURE);
}

static bool child_checkpoint(void *context, qa_buffer *out, qa_error *error) {
    native_child_state *state = context;
    qa_buffer response = {0};
    if (!child_request(state, NATIVE_WIRE_HOST_CHECKPOINT, (qa_bytes){0}, &response, error))
        return false;
    native_wire_reader reader = {.bytes = {response.data, response.size}};
    qa_bytes borrowed;
    bool ok = native_wire_get_bytes(&reader, &borrowed, error) && native_wire_end(&reader, error) &&
              native_copy_bytes(borrowed, out, error);
    qa_buffer_free(&response);
    return child_finish_response(state, true, ok, error);
}

static bool child_restore(void *context, qa_bytes bytes, qa_error *error) {
    native_child_state *state = context;
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_bytes(&request, bytes, error);
    if (ok)
        received = child_request(state, NATIVE_WIRE_HOST_RESTORE,
                                 (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return child_finish_response(state, received, ok, error);
}

static bool child_targets_equal(qa_native_target left, qa_native_target right) {
    return left.os == right.os && left.arch == right.arch && left.abi == right.abi &&
           left.pointer_bytes == right.pointer_bytes;
}

static void child_dependencies_free(qa_native_dependency *dependencies, size_t count) {
    if (!dependencies)
        return;
    for (size_t index = 0; index < count; ++index) {
        free((void *)dependencies[index].path);
        free((void *)dependencies[index].bytes.data);
    }
    free(dependencies);
}

static bool child_load(native_child_state *state, native_wire_reader *reader,
                       native_wire_buffer *body, qa_error *error) {
    if (state->instance || state->module)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native runner already loaded a module");
    uint32_t profile, os, arch, abi, pointer_bytes, tick_rate, frame_bits, frame_milliseconds,
        q3_role;
    uint8_t has_declaration;
    const uint8_t *digest_bytes;
    qa_buffer source = {0};
    qa_bytes image;
    uint64_t dependency_count;
    if (!native_wire_get_u32(reader, &profile, error) || !native_wire_get_u32(reader, &os, error) ||
        !native_wire_get_u32(reader, &arch, error) || !native_wire_get_u32(reader, &abi, error) ||
        !native_wire_get_u32(reader, &pointer_bytes, error) ||
        !native_wire_get_u32(reader, &tick_rate, error) ||
        !native_wire_get_u32(reader, &frame_bits, error) ||
        !native_wire_get_u32(reader, &frame_milliseconds, error) ||
        !native_wire_get_u32(reader, &q3_role, error) ||
        !native_wire_get_u8(reader, &has_declaration, error) ||
        !native_wire_get_raw(reader, 64u, &digest_bytes, error) ||
        !native_wire_get_string(reader, &source, error) ||
        !native_wire_get_bytes(reader, &image, error) ||
        !native_wire_get_u64(reader, &dependency_count, error) ||
        profile > QA_NATIVE_QUAKE_LIVE_GAME_API10 || os > QA_NATIVE_OS_MACOS ||
        arch > QA_NATIVE_ARCH_AARCH64 || abi > QA_NATIVE_ABI_AAPCS64 ||
        (pointer_bytes != 4u && pointer_bytes != 8u) || has_declaration > 1u ||
        dependency_count > 4096u || q3_role > QA_QVM_UI) {
        qa_buffer_free(&source);
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner load request is invalid");
    }
    qa_native_dependency *dependencies =
        calloc((size_t)dependency_count ? (size_t)dependency_count : 1u, sizeof(*dependencies));
    if (!dependencies) {
        qa_buffer_free(&source);
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner dependencies");
    }
    bool ok = true;
    for (size_t index = 0; ok && index < (size_t)dependency_count; ++index) {
        qa_buffer path = {0};
        qa_bytes bytes;
        ok = native_wire_get_string(reader, &path, error) &&
             native_wire_get_bytes(reader, &bytes, error);
        if (ok) {
            uint8_t *copy = malloc(bytes.size ? bytes.size : 1u);
            if (!copy) {
                qa_buffer_free(&path);
                native_fail(error, QA_ERROR_MEMORY, index,
                            "allocating native runner dependency bytes");
                ok = false;
            } else {
                if (bytes.size)
                    memcpy(copy, bytes.data, bytes.size);
                dependencies[index] =
                    (qa_native_dependency){.path = (char *)path.data, .bytes = {copy, bytes.size}};
            }
        }
    }
    if (ok)
        ok = native_wire_end(reader, error);
    qa_native_target requested = {(qa_native_os)os, (qa_native_arch)arch, (qa_native_abi)abi,
                                  (uint8_t)pointer_bytes};
    if (ok && !child_targets_equal(requested, qa_native_host_target()))
        ok = native_fail(error, QA_ERROR_UNSUPPORTED, arch,
                         "native runner executable has the wrong target ABI");
    qa_sha256_digest declaration = {0}, artifact = {0};
    memcpy(declaration.bytes, digest_bytes, 32u);
    memcpy(artifact.bytes, digest_bytes + 32u, 32u);
    if (ok)
        ok = qa_native_module_load(image, (const char *)source.data, (qa_native_profile)profile,
                                   &artifact, &state->module, error);
    qa_native_options options = {.context = state,
                                 .q3_role = (qa_qvm_role)q3_role,
                                 .import = child_import,
                                 .describe_syscall = child_describe_syscall,
                                 .syscall = child_syscall,
                                 .checkpoint = child_checkpoint,
                                 .restore = child_restore,
                                 .declaration_digest = has_declaration ? &declaration : NULL,
                                 .dependencies = dependencies,
                                 .dependency_count = (size_t)dependency_count,
                                 .tick_rate = tick_rate,
                                 .frame_milliseconds = frame_milliseconds};
    memcpy(&options.frame_seconds, &frame_bits, sizeof(frame_bits));
    if (ok)
        ok = qa_native_create_direct(state->module, &options, &state->instance, error);
    child_dependencies_free(dependencies, (size_t)dependency_count);
    qa_buffer_free(&source);
    if (!ok) {
        qa_native_module_release(state->module);
        state->module = NULL;
        return false;
    }
    qa_native_entity_table table = {0};
    if (!qa_native_entity_table_get(state->instance, &table, error))
        return false;
    return native_wire_put_u64(body, state->instance->image_base, error) &&
           native_wire_put_u64(body, state->instance->image_bytes, error) &&
           native_wire_put_u64(body, table.base, error) &&
           native_wire_put_u64(body, table.stride, error) &&
           native_wire_put_u32(body, table.count, error) &&
           native_wire_put_u32(body, table.capacity, error) &&
           native_wire_put_u32(body, (uint32_t)qa_native_get_lifecycle(state->instance), error);
}

static bool child_decode_values(native_wire_reader *reader, qa_native_value **values,
                                qa_buffer **storage, size_t *count, qa_error *error) {
    uint64_t encoded_count;
    if (!native_wire_get_u64(reader, &encoded_count, error) || encoded_count > NATIVE_MAX_ARGUMENTS)
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner argument count is invalid");
    qa_native_value *decoded =
        calloc((size_t)encoded_count ? (size_t)encoded_count : 1u, sizeof(*decoded));
    qa_buffer *owned = calloc((size_t)encoded_count ? (size_t)encoded_count : 1u, sizeof(*owned));
    if (!decoded || !owned) {
        free(decoded);
        free(owned);
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner arguments");
    }
    for (size_t index = 0; index < (size_t)encoded_count; ++index) {
        if (!native_wire_get_value(reader, &decoded[index], &owned[index], error)) {
            for (size_t prior = 0; prior < index; ++prior)
                qa_buffer_free(&owned[prior]);
            free(decoded);
            free(owned);
            return false;
        }
    }
    *values = decoded;
    *storage = owned;
    *count = (size_t)encoded_count;
    return true;
}

static void child_values_free(qa_native_value *values, qa_buffer *storage, size_t count) {
    for (size_t index = 0; index < count; ++index)
        qa_buffer_free(&storage[index]);
    free(values);
    free(storage);
}

static bool child_result_storage(const qa_native_signature *signature, qa_native_value *result,
                                 qa_buffer *storage, qa_error *error) {
    result->type = signature->result.kind;
    if (result->type != QA_NATIVE_BYTES)
        return true;
    native_ffi_signature prepared = {0};
    if (!native_ffi_prepare(&prepared, signature, error))
        return false;
    size_t size = prepared.result->size;
    native_ffi_destroy(&prepared);
    storage->data = calloc(size ? size : 1u, 1u);
    storage->size = size;
    if (!storage->data)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner aggregate result");
    result->as.bytes = (qa_native_memory){storage->data, storage->size};
    return true;
}

static bool child_call(native_child_state *state, native_wire_reader *reader,
                       native_wire_buffer *body, qa_error *error) {
    qa_buffer name = {0};
    qa_native_value *arguments = NULL;
    qa_buffer *storage = NULL;
    size_t count = 0;
    bool ok = native_wire_get_string(reader, &name, error) &&
              child_decode_values(reader, &arguments, &storage, &count, error) &&
              native_wire_end(reader, error);
    const qa_native_signature *signature =
        ok ? qa_native_entry_signature(state->instance, (const char *)name.data) : NULL;
    if (ok && !signature)
        ok = native_fail(error, QA_ERROR_NOT_FOUND, 0,
                         "native runner entry is not in the selected profile");
    qa_native_value result = {0};
    qa_buffer result_storage = {0};
    if (ok)
        ok = child_result_storage(signature, &result, &result_storage, error) &&
             qa_native_call(state->instance, (const char *)name.data, arguments, count, &result,
                            error) &&
             native_wire_put_value(body, &result, error);
    qa_buffer_free(&result_storage);
    child_values_free(arguments, storage, count);
    qa_buffer_free(&name);
    return ok;
}

static bool child_invoke(native_child_state *state, native_wire_reader *reader,
                         native_wire_buffer *body, qa_error *error) {
    uint64_t address;
    qa_native_signature signature = {0};
    qa_native_value *arguments = NULL;
    qa_buffer *storage = NULL;
    size_t count = 0;
    bool ok = native_wire_get_u64(reader, &address, error) &&
              native_wire_get_signature(reader, &signature, error) &&
              child_decode_values(reader, &arguments, &storage, &count, error) &&
              native_wire_end(reader, error);
    qa_native_value result = {0};
    qa_buffer result_storage = {0};
    if (ok)
        ok = child_result_storage(&signature, &result, &result_storage, error) &&
             qa_native_invoke(state->instance, address, &signature, arguments, count, &result,
                              error) &&
             native_wire_put_value(body, &result, error);
    qa_buffer_free(&result_storage);
    child_values_free(arguments, storage, count);
    native_wire_signature_free(&signature);
    return ok;
}

static bool child_export(native_child_state *state, native_wire_reader *reader,
                         native_wire_buffer *body, bool source_entry, qa_error *error) {
    qa_buffer name = {0};
    qa_native_address address;
    bool ok = native_wire_get_string(reader, &name, error) && native_wire_end(reader, error) &&
              (source_entry ? qa_native_entry_address(state->instance, (const char *)name.data, &address, error) :
               qa_native_export(state->instance, (const char *)name.data, &address, error)) &&
              native_wire_put_u64(body, address, error);
    qa_buffer_free(&name);
    return ok;
}

static bool child_observer_entry(void *context, qa_native_instance *instance,
                                 qa_native_entry_observer *binding,
                                 const qa_native_value *arguments, size_t count,
                                 qa_native_value *result, qa_error *error) {
    native_child_state *state = context;
    /* This bridge runs on the runner application stack. Returning a default
     * libffi result after rejection would resume the original native caller. */
    if (state->instance != instance)
        _Exit(EXIT_FAILURE);
    native_wire_buffer request = {0};
    qa_buffer response = {0}, storage = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, binding->id, error) &&
              native_wire_put_u64(&request, binding->ffi.result->size, error) &&
              child_encode_values(&request, arguments, count, error);
    if (ok)
        received = child_request(state, NATIVE_WIRE_OBSERVER_ENTRY,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        qa_native_value decoded = {0};
        ok = native_wire_get_value(&reader, &decoded, &storage, error) &&
             native_wire_end(&reader, error) && decoded.type == binding->signature.result.kind;
        if (ok && decoded.type == QA_NATIVE_BYTES) {
            if (result->type != QA_NATIVE_BYTES || storage.size != result->as.bytes.size)
                ok = native_fail(error, QA_ERROR_FORMAT, storage.size,
                                 "native entry aggregate result extent changed");
            else
                memcpy(result->as.bytes.data, storage.data, storage.size);
        } else if (ok) {
            *result = decoded;
        }
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    qa_buffer_free(&storage);
    if (!child_finish_response(state, received, ok, error))
        _Exit(EXIT_FAILURE);
    return true;
}

static qa_native_entry_observer *child_observer_find(native_child_state *state, uint64_t id) {
    qa_native_entry_observer *binding = state->instance->entry_observers;
    while (binding && binding->id != id)
        binding = binding->next;
    return binding;
}

static bool child_observer_add(native_child_state *state, native_wire_reader *reader,
                               qa_error *error) {
    qa_native_entry_observer *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating runner entry observer");
    binding->instance = state->instance;
    binding->callback = child_observer_entry;
    binding->context = state;
    bool ok = native_wire_get_u64(reader, &binding->id, error) &&
              native_wire_get_u64(reader, &binding->address, error) &&
              native_wire_get_signature(reader, &binding->signature, error) &&
              native_wire_end(reader, error);
    if (ok && (!binding->id || child_observer_find(state, binding->id) ||
               binding->address < state->instance->image_base ||
               binding->address - state->instance->image_base >= state->instance->image_bytes))
        ok = native_fail(error, QA_ERROR_FORMAT, 0, "runner entry observer address is invalid");
    if (ok)
        ok = native_ffi_prepare(&binding->ffi, &binding->signature, error);
    if (ok) {
        binding->closure = ffi_closure_alloc(sizeof(*binding->closure), &binding->code);
        if (!binding->closure)
            ok = native_fail(error, QA_ERROR_MEMORY, 0, "allocating runner entry closure");
    }
    if (ok && ffi_prep_closure_loc(binding->closure, &binding->ffi.cif, native_observer_dispatch,
                                   binding, binding->code) != FFI_OK)
        ok = native_fail(error, QA_ERROR_UNSUPPORTED, 0, "preparing runner entry closure");
    if (ok) {
        native_hook_control control = {.operation = NATIVE_HOOK_ENTRY_ADD, .id = binding->id,
                                       .address = binding->address,
                                       .replacement = (uint64_t)(uintptr_t)binding->code};
        if (!native_hooks_control(&control))
            ok = native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                             "instrumented runner rejected entry registration");
    }
    if (!ok) {
        if (binding->closure)
            ffi_closure_free(binding->closure);
        native_ffi_destroy(&binding->ffi);
        native_wire_signature_free(&binding->signature);
        free(binding);
        return false;
    }
    binding->next = state->instance->entry_observers;
    state->instance->entry_observers = binding;
    return true;
}

static bool child_observer_remove(native_child_state *state, native_wire_reader *reader,
                                  qa_error *error) {
    uint64_t id;
    if (!native_wire_get_u64(reader, &id, error) || !native_wire_end(reader, error))
        return false;
    qa_native_entry_observer *binding = child_observer_find(state, id);
    if (!binding || binding->active_calls)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "runner entry observer is active or absent");
    native_hook_control control = {.operation = NATIVE_HOOK_ENTRY_REMOVE, .id = id};
    if (!native_hooks_control(&control))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0, "removing runner entry interception");
    qa_native_entry_observer **cursor = &state->instance->entry_observers;
    while (*cursor != binding)
        cursor = &(*cursor)->next;
    *cursor = binding->next;
    ffi_closure_free(binding->closure);
    native_ffi_destroy(&binding->ffi);
    native_wire_signature_free(&binding->signature);
    free(binding);
    return true;
}

static bool child_observer_original(native_child_state *state, native_wire_reader *reader,
                                    native_wire_buffer *body, qa_error *error) {
    uint64_t id = 0;
    qa_native_value *arguments = NULL;
    qa_buffer *storage = NULL, result_storage = {0};
    size_t count = 0;
    bool ok = native_wire_get_u64(reader, &id, error) &&
              child_decode_values(reader, &arguments, &storage, &count, error) &&
              native_wire_end(reader, error);
    qa_native_entry_observer *binding = ok ? child_observer_find(state, id) : NULL;
    if (ok && !binding)
        ok = native_fail(error, QA_ERROR_ARGUMENT, 0, "original runner entry is absent");
    qa_native_value result = {0};
    if (ok)
        ok = child_result_storage(&binding->signature, &result, &result_storage, error);
    native_hook_control control = {.operation = NATIVE_HOOK_BYPASS_ARM, .id = id};
    bool armed = ok && native_hooks_control(&control);
    if (ok && !armed)
        ok = native_fail(error, QA_ERROR_UNSUPPORTED, 0, "arming original runner entry bypass");
    if (armed) {
        ok = qa_native_invoke(state->instance, binding->address, &binding->signature, arguments,
                              count, &result, error);
        control.operation = NATIVE_HOOK_BYPASS_CLEAR;
        bool consumed = native_hooks_control(&control);
        if (ok && !consumed)
            ok = native_fail(error, QA_ERROR_FORMAT, 0, "original runner entry bypass was not consumed");
    }
    if (ok)
        ok = native_wire_put_value(body, &result, error);
    child_values_free(arguments, storage, count);
    qa_buffer_free(&result_storage);
    return ok;
}

static bool child_observer_control(native_wire_reader *reader, qa_error *error) {
    native_hook_control control = {0};
    bool ok = native_wire_get_u64(reader, &control.magic, error) &&
              native_wire_get_u64(reader, &control.operation, error) &&
              native_wire_get_u64(reader, &control.id, error) &&
              native_wire_get_u64(reader, &control.address, error) &&
              native_wire_get_u64(reader, &control.size, error) &&
              native_wire_get_u64(reader, &control.replacement, error) &&
              native_wire_end(reader, error);
    if (!ok)
        return false;
    if (control.magic != NATIVE_HOOK_CONTROL_MAGIC ||
        (control.operation != NATIVE_HOOK_WATCH_ADD && control.operation != NATIVE_HOOK_WATCH_REMOVE))
        return native_fail(error, QA_ERROR_FORMAT, 0, "runner watch control is invalid");
    return native_hooks_control(&control) ||
           native_fail(error, QA_ERROR_UNSUPPORTED, 0, "instrumented runner rejected write watch");
}

static bool child_read(native_child_state *state, native_wire_reader *reader,
                       native_wire_buffer *body, qa_error *error) {
    uint64_t address, count;
    size_t maximum = state->connection.maximum_frame > NATIVE_WIRE_REPLY_BYTES_OVERHEAD
                         ? state->connection.maximum_frame - NATIVE_WIRE_REPLY_BYTES_OVERHEAD
                         : 0;
    if (!native_wire_get_u64(reader, &address, error) ||
        !native_wire_get_u64(reader, &count, error) || !native_wire_end(reader, error) ||
        !native_u64_fits_size(count) || count > maximum)
        return native_fail(error, QA_ERROR_ARGUMENT, reader->offset,
                           "native runner read range is invalid");
    uint8_t *bytes = malloc((size_t)count ? (size_t)count : 1u);
    if (!bytes)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner read response");
    bool ok = qa_native_read(state->instance, address, bytes, (size_t)count, error) &&
              native_wire_put_bytes(body, (qa_bytes){bytes, (size_t)count}, error);
    free(bytes);
    return ok;
}

static bool child_write(native_child_state *state, native_wire_reader *reader, qa_error *error) {
    uint64_t address;
    qa_bytes bytes;
    return native_wire_get_u64(reader, &address, error) &&
           native_wire_get_bytes(reader, &bytes, error) && native_wire_end(reader, error) &&
           qa_native_write(state->instance, address, bytes, error);
}

static bool child_allocate(native_child_state *state, native_wire_reader *reader,
                           native_wire_buffer *body, qa_error *error) {
    uint64_t count;
    uint32_t tag;
    qa_native_address address;
    return native_wire_get_u64(reader, &count, error) && native_u64_fits_size(count) &&
           native_wire_get_u32(reader, &tag, error) && native_wire_end(reader, error) &&
           qa_native_allocate(state->instance, (size_t)count, (int32_t)tag, &address, error) &&
           native_wire_put_u64(body, address, error);
}

static bool child_allocation_query(native_child_state *state, native_wire_reader *reader,
                                   native_wire_buffer *body, qa_error *error) {
    uint64_t address;
    qa_native_allocation_info info;
    return native_wire_get_u64(reader, &address, error) && native_wire_end(reader, error) &&
        qa_native_allocation_query(state->instance, address, &info, error) &&
        native_wire_put_u64(body, info.base, error) && native_wire_put_u64(body, info.bytes, error) &&
        native_wire_put_u32(body, (uint32_t)info.tag, error);
}

static bool child_free(native_child_state *state, native_wire_reader *reader, qa_error *error) {
    uint64_t address;
    return native_wire_get_u64(reader, &address, error) && native_wire_end(reader, error) &&
           qa_native_free(state->instance, address, error);
}

static bool child_free_tag(native_child_state *state, native_wire_reader *reader, qa_error *error) {
    uint32_t tag;
    if (!native_wire_get_u32(reader, &tag, error) || !native_wire_end(reader, error))
        return false;
    qa_native_free_tag(state->instance, (int32_t)tag);
    return true;
}

static bool child_entity_get(native_child_state *state, native_wire_reader *reader,
                             native_wire_buffer *body, qa_error *error) {
    qa_native_entity_table table;
    return native_wire_end(reader, error) &&
           qa_native_entity_table_get(state->instance, &table, error) &&
           native_wire_put_u64(body, table.base, error) &&
           native_wire_put_u64(body, table.stride, error) &&
           native_wire_put_u32(body, table.count, error) &&
           native_wire_put_u32(body, table.capacity, error);
}

static bool child_entity_set(native_child_state *state, native_wire_reader *reader,
                             qa_error *error) {
    qa_native_entity_table table = {0};
    uint64_t stride;
    if (!native_wire_get_u64(reader, &table.base, error) ||
        !native_wire_get_u64(reader, &stride, error) || !native_u64_fits_size(stride) ||
        !native_wire_get_u32(reader, &table.count, error) ||
        !native_wire_get_u32(reader, &table.capacity, error) || !native_wire_end(reader, error))
        return false;
    table.stride = (size_t)stride;
    return qa_native_set_entity_table(state->instance, table, error);
}

static bool child_checkpoint_capture(native_child_state *state, native_wire_reader *reader,
                                     native_wire_buffer *body, qa_error *error) {
    uint32_t flags;
    if (!native_wire_get_u32(reader, &flags, error) || (flags & ~15u) ||
        !native_wire_end(reader, error))
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner checkpoint flags are invalid");
    qa_native_checkpoint checkpoint = {0};
    qa_native_checkpoint_request request = {.game = (flags & 1u) != 0,
                                            .level = (flags & 2u) != 0,
                                            .autosave = (flags & 4u) != 0,
                                            .transition = (flags & 8u) != 0};
    qa_buffer encoded = {0};
    bool ok = qa_native_checkpoint_capture(state->instance, request, &checkpoint, error) &&
              qa_native_checkpoint_encode(&checkpoint, &encoded, error) &&
              native_wire_put_bytes(body, (qa_bytes){encoded.data, encoded.size}, error);
    qa_native_checkpoint_free(&checkpoint);
    qa_buffer_free(&encoded);
    return ok;
}

static bool child_checkpoint_restore(native_child_state *state, native_wire_reader *reader,
                                     qa_error *error) {
    uint32_t part;
    qa_bytes encoded;
    if (!native_wire_get_u32(reader, &part, error) || part > QA_NATIVE_RESTORE_HOST ||
        !native_wire_get_bytes(reader, &encoded, error) || !native_wire_end(reader, error))
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner checkpoint restore request is invalid");
    qa_native_checkpoint checkpoint = {0};
    bool ok = qa_native_checkpoint_decode(encoded, &checkpoint, error) &&
              qa_native_checkpoint_restore(state->instance, &checkpoint,
                                           (qa_native_restore_part)part, error);
    qa_native_checkpoint_free(&checkpoint);
    return ok;
}

static bool child_destroy(native_child_state *state, native_wire_reader *reader, qa_error *error) {
    if (!native_wire_end(reader, error))
        return false;
    if (state->instance && !qa_native_can_destroy(state->instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native child destruction requires idle ownership");
    bool ok = !state->instance || qa_native_destroy(state->instance, error);
    state->instance = NULL;
    qa_native_module_release(state->module);
    state->module = NULL;
    state->stop = true;
    return ok;
}

static bool child_handle_request(native_child_state *state, const native_wire_frame *frame,
                                 qa_error *transport_error) {
    native_wire_reader reader = {.bytes = {frame->payload.data, frame->payload.size}};
    native_wire_buffer body = {0};
    qa_error operation_error = {0};
    bool ok;
    switch (frame->opcode) {
    case NATIVE_WIRE_LOAD:
        ok = child_load(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_CALL:
        ok = state->instance && child_call(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_INVOKE:
        ok = state->instance && child_invoke(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_OBSERVER_ENTRY_ADD:
        ok = state->instance && child_observer_add(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_OBSERVER_ENTRY_REMOVE:
        ok = state->instance && child_observer_remove(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_OBSERVER_ORIGINAL:
        ok = state->instance && child_observer_original(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_OBSERVER_CONTROL:
        ok = state->instance && child_observer_control(&reader, &operation_error);
        break;
    case NATIVE_WIRE_EXPORT:
        ok = state->instance && child_export(state, &reader, &body, false, &operation_error);
        break;
    case NATIVE_WIRE_ENTRY_ADDRESS:
        ok = state->instance && child_export(state, &reader, &body, true, &operation_error);
        break;
    case NATIVE_WIRE_READ:
        ok = state->instance && child_read(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_WRITE:
        ok = state->instance && child_write(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_ALLOCATE:
        ok = state->instance && child_allocate(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_ALLOCATION_QUERY:
        ok = state->instance && child_allocation_query(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_FREE:
        ok = state->instance && child_free(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_FREE_TAG:
        ok = state->instance && child_free_tag(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_ENTITY_GET:
        ok = state->instance && child_entity_get(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_ENTITY_SET:
        ok = state->instance && child_entity_set(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_CHECKPOINT_CAPTURE:
        ok = state->instance && child_checkpoint_capture(state, &reader, &body, &operation_error);
        break;
    case NATIVE_WIRE_CHECKPOINT_RESTORE:
        ok = state->instance && child_checkpoint_restore(state, &reader, &operation_error);
        break;
    case NATIVE_WIRE_DESTROY:
        ok = child_destroy(state, &reader, &operation_error);
        break;
    default:
        ok = native_fail(&operation_error, QA_ERROR_FORMAT, frame->opcode,
                         "native runner received an unknown operation");
        break;
    }
    if (!ok && !operation_error.message[0])
        native_fail(&operation_error, QA_ERROR_ARGUMENT, frame->opcode,
                    "native runner operation requires a loaded module");
    bool sent =
        child_send_reply(state, frame, &body, ok ? NULL : &operation_error, transport_error);
    native_wire_buffer_free(&body);
    return sent;
}

int qa_native_runner_main(void) {
    native_child_state state = {0};
    region_child = &state;
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    state.connection.input = (intptr_t)GetStdHandle(STD_INPUT_HANDLE);
    state.connection.output = (intptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
#else
    state.connection.input = STDIN_FILENO;
    state.connection.output = STDOUT_FILENO;
#endif
    state.connection.child = true;
    state.connection.sequence = UINT64_C(0x8000000000000001);
    state.connection.maximum_frame = NATIVE_DEFAULT_MAX_FRAME;
    while (!state.stop && !state.connection.poisoned) {
        qa_error error = {0};
        native_wire_frame frame = {0};
        if (!native_wire_receive(&state.connection, &frame, &error))
            break;
        if ((frame.opcode & NATIVE_WIRE_REPLY) || frame.reply_to || frame.depth != 1u ||
            !child_handle_request(&state, &frame, &error)) {
            native_wire_frame_free(&frame);
            native_wire_poison(&state.connection, &error);
            break;
        }
        native_wire_frame_free(&frame);
    }
    if (state.instance) {
        qa_error ignored = {0};
        qa_native_destroy(state.instance, &ignored);
    }
    qa_native_module_release(state.module);
    region_child = NULL;
    return state.stop && !state.connection.poisoned ? 0 : 1;
}

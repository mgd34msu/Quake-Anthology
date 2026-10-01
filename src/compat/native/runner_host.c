#include "protocol.h"

#include <math.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

typedef struct native_runner_command {
    const char *arguments[12];
    char *owned[8];
    size_t owned_count;
} native_runner_command;

static bool targets_equal(qa_native_target left, qa_native_target right) {
    return left.os == right.os && left.arch == right.arch && left.abi == right.abi &&
           left.pointer_bytes == right.pointer_bytes;
}

bool qa_native_terminal(const qa_native_instance *instance) {
    return instance && instance->backend == QA_NATIVE_BACKEND_RUNNER &&
           instance->runner && instance->runner->poisoned;
}
bool qa_native_terminal_retired(const qa_native_instance *instance, const qa_actor_registry *actors) {
    if (!qa_native_terminal(instance) || !qa_native_can_destroy(instance))
        return false;
    for (uint32_t i = 0; i < instance->slot_capacity; ++i) {
        const native_slot *slot = instance->slots + i;
        if (slot->kind != QA_NATIVE_SLOT_FREE && slot->actor.registry &&
            (!actors || slot->actor.registry != qa_actors_identity(actors) || qa_actors_get(actors, slot->actor)))
            return false;
    }
    return true;
}

static void command_destroy(native_runner_command *command) {
    for (size_t index = 0; index < command->owned_count; ++index)
        free(command->owned[index]);
    memset(command, 0, sizeof(*command));
}

static char *command_copy(native_runner_command *command, const char *text, qa_error *error) {
    if (command->owned_count >= sizeof(command->owned) / sizeof(command->owned[0])) {
        native_fail(error, QA_ERROR_MEMORY, 0, "native runner command owns too many paths");
        return NULL;
    }
    char *copy = native_strdup(text, error);
    if (copy)
        command->owned[command->owned_count++] = copy;
    return copy;
}

static char *wine_path(native_runner_command *command, const char *path, const char *drive,
                       qa_error *error) {
    if (!path)
        return NULL;
    if (strchr(path, ':') || path[0] != '/')
        return command_copy(command, path, error);
    if (!drive || !drive[0])
        drive = "Z:";
    size_t size;
    if (!native_size_add(strlen(drive), strlen(path) + 1u, &size)) {
        native_fail(error, QA_ERROR_MEMORY, 0, "Wine native runner path length overflows");
        return NULL;
    }
    char *converted = malloc(size);
    if (!converted) {
        native_fail(error, QA_ERROR_MEMORY, 0, "allocating Wine native runner path");
        return NULL;
    }
    size_t used = 0;
    memcpy(converted, drive, strlen(drive));
    used = strlen(drive);
    for (const char *cursor = path; *cursor; ++cursor)
        converted[used++] = *cursor == '/' ? '\\' : *cursor;
    converted[used] = 0;
    command->owned[command->owned_count++] = converted;
    return converted;
}

static const char *runner_path(const qa_native_runner_config *config, qa_native_target target) {
    if (target.os == QA_NATIVE_OS_WINDOWS)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->windows_i386_runner
                                                  : config->windows_x86_64_runner;
    if (target.os == QA_NATIVE_OS_LINUX)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->linux_i386_runner
                                                  : config->linux_x86_64_runner;
    return NULL;
}

static const char *drrun_path(const qa_native_runner_config *config, qa_native_target target) {
    if (target.os == QA_NATIVE_OS_WINDOWS)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->windows_i386_drrun
                                                  : config->windows_x86_64_drrun;
    if (target.os == QA_NATIVE_OS_LINUX)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->linux_i386_drrun
                                                  : config->linux_x86_64_drrun;
    return NULL;
}

static const char *client_path(const qa_native_runner_config *config, qa_native_target target) {
    if (target.os == QA_NATIVE_OS_WINDOWS)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->windows_i386_client
                                                  : config->windows_x86_64_client;
    if (target.os == QA_NATIVE_OS_LINUX)
        return target.arch == QA_NATIVE_ARCH_I386 ? config->linux_i386_client
                                                  : config->linux_x86_64_client;
    return NULL;
}

static bool prepare_command(qa_native_instance *instance, const qa_native_runner_config *config,
                            const char *descriptor, native_runner_command *out, qa_error *error) {
    qa_native_target target = instance->module->info.image.target;
    qa_native_target host = qa_native_host_target();
    const char *runner = runner_path(config, target);
    bool instrumented = instance->options.observe || instance->region_count != 0;
    if (config->validate && !config->validate(config->validation_context, target,
        instrumented, error)) return false;
    const char *drrun = instrumented ? drrun_path(config, target) : NULL;
    const char *client = instrumented ? client_path(config, target) : NULL;
    if (!runner)
        return native_fail(error, QA_ERROR_UNSUPPORTED, target.arch,
                           "native runner executable is not packaged for the target");
    if (instrumented && (!drrun || !client || !descriptor))
        return native_fail(
            error, QA_ERROR_UNSUPPORTED, target.arch,
            "declared native regions require matching DynamoRIO launcher and client");
    native_runner_command command = {0};
    size_t at = 0;
    if (target.os == QA_NATIVE_OS_WINDOWS && host.os != QA_NATIVE_OS_WINDOWS) {
        if (!config->wine)
            return native_fail(error, QA_ERROR_UNSUPPORTED, target.arch,
                               "Windows native modules require a packaged Wine/WoW64 runtime");
        command.arguments[at++] = config->wine;
        if (instrumented) {
            command.arguments[at++] = drrun;
            command.arguments[at++] = "-c";
            char *guest_client = wine_path(&command, client, config->wine_drive, error);
            char *guest_descriptor = wine_path(&command, descriptor, config->wine_drive, error);
            char *guest_runner = wine_path(&command, runner, config->wine_drive, error);
            if (!guest_client || !guest_descriptor || !guest_runner) {
                command_destroy(&command);
                return false;
            }
            command.arguments[at++] = guest_client;
            command.arguments[at++] = guest_descriptor;
            command.arguments[at++] = "--";
            command.arguments[at++] = guest_runner;
        } else {
            command.arguments[at++] = runner;
        }
    } else {
        if (target.os != host.os) {
            command_destroy(&command);
            return native_fail(error, QA_ERROR_UNSUPPORTED, target.os,
                               "native runner cannot bridge this operating system");
        }
        if (instrumented) {
            command.arguments[at++] = drrun;
            command.arguments[at++] = "-c";
            command.arguments[at++] = client;
            command.arguments[at++] = descriptor;
            command.arguments[at++] = "--";
        }
        command.arguments[at++] = runner;
    }
    command.arguments[at] = NULL;
    *out = command;
    return true;
}

#if defined(_WIN32)
static bool runner_os_error(qa_error *error, const char *operation) {
    qa_error_set(error, QA_ERROR_IO, (size_t)GetLastError(), "%s failed", operation);
    return false;
}

static bool append_quoted(native_wire_buffer *line, const char *argument, qa_error *error) {
    bool quote = !argument[0] || strpbrk(argument, " \t\"") != NULL;
    if (!quote)
        return native_wire_put_raw(line, argument, strlen(argument), error);
    if (!native_wire_put_u8(line, '"', error))
        return false;
    size_t slashes = 0;
    for (const char *cursor = argument;; ++cursor) {
        if (*cursor == '\\') {
            ++slashes;
            continue;
        }
        size_t repeats = slashes;
        if (*cursor == '"' || !*cursor) {
            size_t quote_escape = *cursor == '"' ? 1u : 0u;
            if (slashes > (SIZE_MAX - quote_escape) / 2u)
                return native_fail(error, QA_ERROR_MEMORY, slashes,
                                   "native runner command quoting overflows");
            repeats = slashes * 2u + quote_escape;
        }
        for (size_t index = 0; index < repeats; ++index)
            if (!native_wire_put_u8(line, '\\', error))
                return false;
        slashes = 0;
        if (!*cursor)
            break;
        if (!native_wire_put_u8(line, (uint8_t)*cursor, error))
            return false;
    }
    return native_wire_put_u8(line, '"', error);
}

static wchar_t *utf8_to_wide(const char *text, qa_error *error) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) {
        runner_os_error(error, "converting native runner command");
        return NULL;
    }
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (!wide)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner command"), NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        runner_os_error(error, "converting native runner command");
        return NULL;
    }
    return wide;
}

static bool spawn_runner(native_runner_connection *connection, const native_runner_command *command,
                         qa_error *error) {
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE child_input_read = NULL, parent_input_write = NULL;
    HANDLE parent_output_read = NULL, child_output_write = NULL;
    HANDLE child_error = NULL;
    if (!CreatePipe(&child_input_read, &parent_input_write, &security, 0) ||
        !CreatePipe(&parent_output_read, &child_output_write, &security, 0)) {
        if (child_input_read)
            CloseHandle(child_input_read);
        if (parent_input_write)
            CloseHandle(parent_input_write);
        if (parent_output_read)
            CloseHandle(parent_output_read);
        if (child_output_write)
            CloseHandle(child_output_write);
        return runner_os_error(error, "creating native runner pipes");
    }
    child_error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (child_error == INVALID_HANDLE_VALUE) {
        CloseHandle(child_input_read);
        CloseHandle(parent_input_write);
        CloseHandle(parent_output_read);
        CloseHandle(child_output_write);
        return runner_os_error(error, "opening native runner error sink");
    }
    if (!SetHandleInformation(parent_input_write, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parent_output_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(child_input_read);
        CloseHandle(parent_input_write);
        CloseHandle(parent_output_read);
        CloseHandle(child_output_write);
        CloseHandle(child_error);
        return runner_os_error(error, "protecting native runner pipe handles");
    }
    native_wire_buffer line = {0};
    bool ok = true;
    for (size_t index = 0; command->arguments[index] && ok; ++index) {
        if (index)
            ok = native_wire_put_u8(&line, ' ', error);
        if (ok)
            ok = append_quoted(&line, command->arguments[index], error);
    }
    if (ok)
        ok = native_wire_put_u8(&line, 0, error);
    wchar_t *wide = ok ? utf8_to_wide((const char *)line.data, error) : NULL;
    native_wire_buffer_free(&line);
    if (!wide)
        ok = false;
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = malloc(attribute_bytes);
    bool attributes_initialized =
        attributes && InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes);
    HANDLE inherited[3] = {child_input_read, child_output_write, child_error};
    if (ok && !attributes) {
        ok = native_fail(error, QA_ERROR_MEMORY, attribute_bytes,
                         "allocating native runner process attributes");
    } else if (ok && (!attributes_initialized ||
                      !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                 inherited, sizeof(inherited), NULL, NULL))) {
        ok = runner_os_error(error, "preparing native runner handle inheritance");
    }
    STARTUPINFOEXW startup = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input_read;
    startup.StartupInfo.hStdOutput = child_output_write;
    startup.StartupInfo.hStdError = child_error;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process = {0};
    if (ok && !CreateProcessW(NULL, wide, NULL, NULL, TRUE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, NULL, NULL,
                              &startup.StartupInfo, &process)) {
        runner_os_error(error, "launching native runner");
        ok = false;
    }
    if (attributes_initialized)
        DeleteProcThreadAttributeList(attributes);
    if (attributes)
        free(attributes);
    free(wide);
    CloseHandle(child_input_read);
    CloseHandle(child_output_write);
    CloseHandle(child_error);
    if (!ok) {
        CloseHandle(parent_input_write);
        CloseHandle(parent_output_read);
        return false;
    }
    CloseHandle(process.hThread);
    connection->input = (intptr_t)parent_output_read;
    connection->output = (intptr_t)parent_input_write;
    connection->process = (intptr_t)process.hProcess;
    return true;
}

static bool close_runner_process(native_runner_connection *connection, qa_error *error) {
    if (connection->input) {
        if (!CloseHandle((HANDLE)connection->input)) return runner_os_error(error, "closing native runner input");
        connection->input = 0;
    }
    if (connection->output) {
        if (!CloseHandle((HANDLE)connection->output)) return runner_os_error(error, "closing native runner output");
        connection->output = 0;
    }
    if (connection->process) {
        DWORD wait = WaitForSingleObject((HANDLE)connection->process, 5000);
        if (wait == WAIT_TIMEOUT) {
            if (!TerminateProcess((HANDLE)connection->process, 125)) {
                if (WaitForSingleObject((HANDLE)connection->process, 0) != WAIT_OBJECT_0)
                    return runner_os_error(error, "terminating native runner");
            }
            wait = WaitForSingleObject((HANDLE)connection->process, 5000);
        }
        if (wait == WAIT_TIMEOUT) return native_fail(error, QA_ERROR_IO, 0,
            "native runner process has not retired after termination");
        if (wait != WAIT_OBJECT_0) return runner_os_error(error, "waiting for native runner retirement");
        if (!CloseHandle((HANDLE)connection->process)) return runner_os_error(error, "closing retired native runner process");
        connection->process = 0;
    }
    return true;
}
#else
static bool spawn_runner(native_runner_connection *connection, const native_runner_command *command,
                         qa_error *error) {
    int to_child[2] = {-1, -1}, from_child[2] = {-1, -1};
    if (pipe(to_child) || pipe(from_child)) {
        int saved = errno;
        if (to_child[0] >= 0)
            close(to_child[0]);
        if (to_child[1] >= 0)
            close(to_child[1]);
        if (from_child[0] >= 0)
            close(from_child[0]);
        if (from_child[1] >= 0)
            close(from_child[1]);
        errno = saved;
        qa_error_set(error, QA_ERROR_IO, (size_t)errno, "creating native runner pipes: %s",
                     strerror(errno));
        return false;
    }
    pid_t process = fork();
    if (process < 0) {
        int saved = errno;
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        errno = saved;
        qa_error_set(error, QA_ERROR_IO, (size_t)errno, "forking native runner: %s",
                     strerror(errno));
        return false;
    }
    if (!process) {
        if (dup2(to_child[0], STDIN_FILENO) < 0 || dup2(from_child[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execvp(command->arguments[0], (char *const *)command->arguments);
        _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    connection->input = from_child[0];
    connection->output = to_child[1];
    connection->process = process;
    return true;
}

static bool close_runner_process(native_runner_connection *connection, qa_error *error) {
    if (connection->input >= 0) {
        int descriptor = (int)connection->input;
        connection->input = -1;
        if (close(descriptor) && errno != EBADF) {
            qa_error_set(error, QA_ERROR_IO, (size_t)errno, "closing native runner input: %s", strerror(errno));
            return false;
        }
    }
    if (connection->output >= 0) {
        int descriptor = (int)connection->output;
        connection->output = -1;
        if (close(descriptor) && errno != EBADF) {
            qa_error_set(error, QA_ERROR_IO, (size_t)errno, "closing native runner output: %s", strerror(errno));
            return false;
        }
    }
    if (connection->process > 0) {
        int status;
        pid_t result;
        do {
            result = waitpid((pid_t)connection->process, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == (pid_t)connection->process) { connection->process = 0; return true; }
        if (result < 0) {
            if (errno == ECHILD && kill((pid_t)connection->process, 0) && errno == ESRCH) {
                connection->process = 0; return true;
            }
            qa_error_set(error, QA_ERROR_IO, (size_t)errno, "qualifying native runner child: %s", strerror(errno));
            return false;
        }
        if (!result) {
            if (kill((pid_t)connection->process, SIGKILL) && errno != ESRCH) {
                qa_error_set(error, QA_ERROR_IO, (size_t)errno, "terminating native runner: %s", strerror(errno));
                return false;
            }
            struct timespec start;
            if (clock_gettime(CLOCK_MONOTONIC, &start)) {
                qa_error_set(error, QA_ERROR_IO, (size_t)errno, "timing native runner retirement: %s", strerror(errno));
                return false;
            }
            for (;;) {
                do { result = waitpid((pid_t)connection->process, &status, WNOHANG); }
                while (result < 0 && errno == EINTR);
                if (result == (pid_t)connection->process) { connection->process = 0; return true; }
                if (result < 0) {
                    if (errno == ECHILD && kill((pid_t)connection->process, 0) && errno == ESRCH) {
                        connection->process = 0; return true;
                    }
                    qa_error_set(error, QA_ERROR_IO, (size_t)errno, "waiting for native runner retirement: %s", strerror(errno));
                    return false;
                }
                struct timespec now;
                if (clock_gettime(CLOCK_MONOTONIC, &now)) {
                    qa_error_set(error, QA_ERROR_IO, (size_t)errno, "timing native runner retirement: %s", strerror(errno));
                    return false;
                }
                if (now.tv_sec - start.tv_sec > 5 ||
                    (now.tv_sec - start.tv_sec == 5 && now.tv_nsec >= start.tv_nsec))
                    return native_fail(error, QA_ERROR_IO, 0,
                        "native runner process has not retired after termination");
                struct timespec interval = {.tv_nsec = 10000000};
                nanosleep(&interval, NULL);
            }
        }
    }
    return true;
}
#endif

static const native_signature_spec *import_spec(qa_native_instance *instance, uint32_t slot) {
    const native_profile_spec *profile = native_profile(instance->module->info.profile);
    if (!profile)
        return NULL;
    for (size_t index = 0; index < profile->import_count; ++index)
        if (profile->imports[index].slot == slot)
            return &profile->imports[index];
    return NULL;
}

static bool send_reply(native_runner_connection *connection, const native_wire_frame *request,
                       const native_wire_buffer *body, const qa_error *failure, qa_error *error) {
    native_wire_buffer payload = {0};
    bool ok = native_wire_put_error(&payload, failure, error);
    if (ok && !failure && body)
        ok = native_wire_put_raw(&payload, body->data, body->size, error);
    if (ok)
        ok = native_wire_send(connection, (uint16_t)(request->opcode | NATIVE_WIRE_REPLY),
                              request->sequence, (qa_bytes){payload.data, payload.size}, NULL,
                              error);
    native_wire_buffer_free(&payload);
    return ok;
}

static bool decode_values(native_wire_reader *reader, qa_native_value **values, qa_buffer **storage,
                          size_t *count, qa_error *error) {
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

static void free_values(qa_native_value *values, qa_buffer *storage, size_t count) {
    for (size_t index = 0; index < count; ++index)
        qa_buffer_free(&storage[index]);
    free(values);
    free(storage);
}

static bool values_match(const native_signature_spec *spec, const qa_native_value *values,
                         size_t count) {
    if (!spec || count != spec->signature.parameter_count)
        return false;
    for (size_t index = 0; index < count; ++index)
        if (values[index].type != spec->signature.parameters[index].kind)
            return false;
    return true;
}

static bool runner_arguments_ready(const qa_native_instance *instance,
                                   const qa_native_signature *signature,
                                   const qa_native_value *arguments, size_t count,
                                   qa_error *error) {
    if (!signature || signature->abi != instance->module->info.image.target.abi ||
        signature->variadic || count != signature->parameter_count ||
        (count && (!arguments || !signature->parameters)))
        return native_fail(error, QA_ERROR_ARGUMENT, count,
                           "native runner signature and exact arguments are required");
    for (size_t index = 0; index < count; ++index) {
        if (arguments[index].type != signature->parameters[index].kind) {
            qa_error_set(error, QA_ERROR_ARGUMENT, index,
                         "native runner argument %zu has type %u; expected %u", index,
                         arguments[index].type, signature->parameters[index].kind);
            return false;
        }
        if (arguments[index].type == QA_NATIVE_BYTES && !arguments[index].as.bytes.data &&
            arguments[index].as.bytes.size)
            return native_fail(error, QA_ERROR_ARGUMENT, index,
                               "native runner aggregate argument has no storage");
    }
    return true;
}

static bool runner_finish_response(qa_native_instance *instance, bool received, bool decoded,
                                   qa_error *error) {
    if (received && !decoded) {
        qa_error fallback = {0};
        const qa_error *failure = error;
        if (!error || error->code == QA_OK) {
            native_fail(&fallback, QA_ERROR_FORMAT, 0, "native runner response is invalid");
            if (error)
                *error = fallback;
            failure = &fallback;
        }
        native_wire_poison(instance->runner, failure);
    }
    return received && decoded;
}

static bool handle_import(qa_native_instance *instance, const native_wire_frame *frame,
                          native_wire_reader *reader, qa_error *error) {
    uint32_t profile, slot;
    uint64_t result_bytes;
    qa_native_value *arguments = NULL;
    qa_buffer *argument_storage = NULL;
    size_t count = 0;
    qa_error callback_error = {0};
    native_wire_buffer body = {0};
    bool ok = native_wire_get_u32(reader, &profile, &callback_error) &&
              native_wire_get_u32(reader, &slot, &callback_error) &&
              native_wire_get_u64(reader, &result_bytes, &callback_error) &&
              decode_values(reader, &arguments, &argument_storage, &count, &callback_error) &&
              native_wire_end(reader, &callback_error);
    const native_signature_spec *spec = ok ? import_spec(instance, slot) : NULL;
    if (ok && (profile != (uint32_t)instance->module->info.profile || !spec ||
               !values_match(spec, arguments, count)
#if SIZE_MAX < UINT64_MAX
               || result_bytes > (uint64_t)SIZE_MAX
#endif
               )) {
        qa_error_set(&callback_error, QA_ERROR_FORMAT, slot,
                     "native runner import identity is invalid");
        ok = false;
    }
    qa_native_value result = {.type = spec ? spec->signature.result.kind : QA_NATIVE_VOID};
    qa_buffer result_storage = {0};
    if (ok && result.type == QA_NATIVE_BYTES) {
        result_storage.data = calloc((size_t)result_bytes ? (size_t)result_bytes : 1u, 1u);
        result_storage.size = (size_t)result_bytes;
        if (!result_storage.data) {
            native_fail(&callback_error, QA_ERROR_MEMORY, 0,
                        "allocating native runner import result");
            ok = false;
        } else {
            result.as.bytes = (qa_native_memory){result_storage.data, result_storage.size};
        }
    }
    if (ok) {
        if (instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && slot == 22) {
            if (count != 5 || arguments[0].type != QA_NATIVE_ADDRESS ||
                arguments[1].type != QA_NATIVE_I32 || arguments[2].type != QA_NATIVE_I32 ||
                arguments[3].type != QA_NATIVE_ADDRESS || arguments[4].type != QA_NATIVE_I32 ||
                arguments[1].as.i32 < 0 || arguments[2].as.i32 <= 0 || arguments[4].as.i32 <= 0) {
                native_fail(&callback_error, QA_ERROR_FORMAT, slot,
                            "Quake Live LocateGameData callback is invalid");
                ok = false;
            } else {
                qa_native_entity_table table = {.base = arguments[0].as.address,
                                                .stride = (size_t)arguments[2].as.i32,
                                                .count = (uint32_t)arguments[1].as.i32,
                                                .capacity = (uint32_t)arguments[1].as.i32};
                ok = native_entity_table_store(instance, table, &callback_error);
            }
        }
    }
    if (ok) {
        if (!instance->options.import) {
            qa_error_set(&callback_error, QA_ERROR_UNSUPPORTED, slot,
                         "native import %s is not bound", spec->name);
            ok = false;
        } else {
            qa_native_signature signature = spec->signature;
            signature.abi = instance->module->info.image.target.abi;
            qa_native_import_call call = {.profile = instance->module->info.profile,
                                          .slot = slot,
                                          .name = spec->name,
                                          .signature = &signature,
                                          .arguments = arguments,
                                          .argument_count = count};
            ++instance->callback_depth;
            ok = instance->options.import(instance->options.context, instance, &call, &result,
                                          &callback_error);
            --instance->callback_depth;
        }
    }
    if (ok && (result.type != spec->signature.result.kind ||
               (result.type == QA_NATIVE_BYTES && result.as.bytes.size != result_bytes)))
        ok = native_fail(&callback_error, QA_ERROR_FORMAT, slot,
                         "native import result differs from its actual source ABI extent or type");
    if (ok)
        ok = native_wire_put_value(&body, &result, &callback_error);
    if (!ok && callback_error.code == QA_OK)
        native_fail(&callback_error, QA_ERROR_ARGUMENT, 0, "native import callback failed");
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &callback_error, error);
    if (sent && !ok) native_wire_poison(instance->runner, &callback_error);
    native_wire_buffer_free(&body);
    qa_buffer_free(&result_storage);
    free_values(arguments, argument_storage, count);
    return sent;
}

static bool handle_describe(qa_native_instance *instance, const native_wire_frame *frame,
                            native_wire_reader *reader, qa_error *error) {
    uint32_t raw_service;
    qa_error callback_error = {0};
    native_wire_buffer body = {0};
    bool ok = native_wire_get_u32(reader, &raw_service, &callback_error) &&
              native_wire_end(reader, &callback_error);
    const qa_native_value_type *types = NULL;
    size_t count = 0;
    if (ok && !instance->options.describe_syscall) {
        native_fail(&callback_error, QA_ERROR_UNSUPPORTED, raw_service,
                    "native syscall description is unbound");
        ok = false;
    }
    if (ok) {
        ++instance->callback_depth;
        ok = instance->options.describe_syscall(instance->options.context, (int32_t)raw_service,
                                                &types, &count, &callback_error);
        --instance->callback_depth;
    }
    if (ok && (count > NATIVE_MAX_ARGUMENTS || (count && !types))) {
        native_fail(&callback_error, QA_ERROR_ARGUMENT, count,
                    "native syscall description is invalid");
        ok = false;
    }
    if (ok)
        ok = native_wire_put_u64(&body, count, &callback_error);
    for (size_t index = 0; ok && index < count; ++index) {
        if (types[index] == QA_NATIVE_VOID || types[index] > QA_NATIVE_ADDRESS) {
            native_fail(&callback_error, QA_ERROR_ARGUMENT, index,
                        "native syscall parameter type is invalid");
            ok = false;
        } else {
            ok = native_wire_put_u32(&body, (uint32_t)types[index], &callback_error);
        }
    }
    if (!ok && callback_error.code == QA_OK)
        native_fail(&callback_error, QA_ERROR_ARGUMENT, 0, "native syscall description failed");
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &callback_error, error);
    if (sent && !ok) native_wire_poison(instance->runner, &callback_error);
    native_wire_buffer_free(&body);
    return sent;
}

static bool handle_syscall(qa_native_instance *instance, const native_wire_frame *frame,
                           native_wire_reader *reader, qa_error *error) {
    uint32_t raw_service;
    qa_native_value *arguments = NULL;
    qa_buffer *storage = NULL;
    size_t count = 0;
    qa_error callback_error = {0};
    native_wire_buffer body = {0};
    bool ok = native_wire_get_u32(reader, &raw_service, &callback_error) &&
              decode_values(reader, &arguments, &storage, &count, &callback_error) &&
              native_wire_end(reader, &callback_error);
    intptr_t result = 0;
    if (ok && !instance->options.syscall) {
        native_fail(&callback_error, QA_ERROR_UNSUPPORTED, raw_service,
                    "native syscall dispatch is unbound");
        ok = false;
    }
    if (ok) {
        ++instance->callback_depth;
        ok = instance->options.syscall(instance->options.context, instance, (int32_t)raw_service,
                                       arguments, count, &result, &callback_error);
        --instance->callback_depth;
    }
    if (ok)
        ok = native_wire_put_u64(&body, (uint64_t)(intptr_t)result, &callback_error);
    if (!ok && callback_error.code == QA_OK)
        native_fail(&callback_error, QA_ERROR_ARGUMENT, 0, "native syscall callback failed");
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &callback_error, error);
    if (sent && !ok) native_wire_poison(instance->runner, &callback_error);
    native_wire_buffer_free(&body);
    free_values(arguments, storage, count);
    return sent;
}

static bool handle_host_checkpoint(qa_native_instance *instance, const native_wire_frame *frame,
                                   native_wire_reader *reader, qa_error *error) {
    qa_error callback_error = {0};
    qa_buffer state = {0};
    bool ok = native_wire_end(reader, &callback_error);
    if (ok && !instance->options.checkpoint) {
        native_fail(&callback_error, QA_ERROR_UNSUPPORTED, 0, "native host checkpoint is unbound");
        ok = false;
    }
    if (ok) {
        ++instance->callback_depth;
        ok = instance->options.checkpoint(instance->options.context, &state, &callback_error);
        --instance->callback_depth;
    }
    native_wire_buffer body = {0};
    if (ok)
        ok = native_wire_put_bytes(&body, (qa_bytes){state.data, state.size}, &callback_error);
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &callback_error, error);
    native_wire_buffer_free(&body);
    qa_buffer_free(&state);
    return sent;
}

static bool handle_host_restore(qa_native_instance *instance, const native_wire_frame *frame,
                                native_wire_reader *reader, qa_error *error) {
    qa_error callback_error = {0};
    qa_bytes state;
    bool ok = native_wire_get_bytes(reader, &state, &callback_error) &&
              native_wire_end(reader, &callback_error);
    if (ok && !instance->options.restore) {
        native_fail(&callback_error, QA_ERROR_UNSUPPORTED, 0, "native host restore is unbound");
        ok = false;
    }
    if (ok) {
        ++instance->callback_depth;
        ok = instance->options.restore(instance->options.context, state, &callback_error);
        --instance->callback_depth;
    }
    return send_reply(instance->runner, frame, NULL, ok ? NULL : &callback_error, error);
}

static bool handle_region(qa_native_instance *instance, const native_wire_frame *frame,
                          native_wire_reader *reader, qa_error *error) {
    uint32_t id = 0, phase = 0;
    qa_error callback_error = {0};
    qa_native_region_event event = {0};
    bool ok = native_wire_get_u32(reader, &id, &callback_error) &&
              native_wire_get_u32(reader, &phase, &callback_error) &&
              native_wire_get_state(reader, &event.state, &callback_error) &&
              native_wire_end(reader, &callback_error) && id < instance->region_count &&
              phase <= QA_NATIVE_REGION_JOIN;
    if (!ok && !callback_error.message[0])
        native_fail(&callback_error, QA_ERROR_FORMAT, id, "native runner region event is invalid");
    qa_native_region_decision decision = {0};
    if (ok) {
        event.region = instance->regions[id].definition;
        event.phase = (qa_native_region_phase)phase;
        ++instance->region_depth;
        bool application_service = frame->opcode == NATIVE_WIRE_REGION_APP;
        if (application_service) ++instance->region_service_depth;
        ok = native_runner_region_event(instance, &event, &decision, &callback_error);
        if (application_service) --instance->region_service_depth;
        --instance->region_depth;
    }
    native_wire_buffer body = {0};
    if (ok)
        ok = native_wire_put_u32(&body, (uint32_t)decision.action, &callback_error) &&
             native_wire_put_u8(&body, decision.replace_state ? 1u : 0u, &callback_error) &&
             native_wire_put_state(&body, &decision.state, &callback_error);
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &callback_error, error);
    native_wire_buffer_free(&body);
    if (sent && (!ok || decision.action == QA_NATIVE_REGION_FAIL_INSTANCE)) {
        if (callback_error.code == QA_OK)
            native_fail(&callback_error, QA_ERROR_ARGUMENT, id, "native region terminated its source instance");
        native_wire_poison(instance->runner, &callback_error);
    }
    return sent;
}

static bool handle_observer_entry(qa_native_instance *instance, const native_wire_frame *frame,
                                  native_wire_reader *reader, qa_error *error) {
    uint64_t id = 0, result_bytes = 0;
    qa_error failure = {0};
    qa_native_value *arguments = NULL;
    qa_buffer *storage = NULL, result_storage = {0};
    size_t count = 0;
    bool ok = native_wire_get_u64(reader, &id, &failure) &&
              native_wire_get_u64(reader, &result_bytes, &failure) &&
              decode_values(reader, &arguments, &storage, &count, &failure) &&
              native_wire_end(reader, &failure);
    qa_native_entry_observer *binding = instance->entry_observers;
    while (binding && binding->id != id)
        binding = binding->next;
    if (ok && (!binding || result_bytes > instance->runner->maximum_frame ||
               !native_u64_fits_size(result_bytes) ||
               !runner_arguments_ready(instance, &binding->signature, arguments, count,
                                       &failure)))
        ok = native_fail(&failure, QA_ERROR_FORMAT, 0, "native entry observer identity is invalid");
    qa_native_value result = {.type = binding ? binding->signature.result.kind : QA_NATIVE_VOID};
    if (ok && result.type == QA_NATIVE_BYTES) {
        result_storage.data = calloc((size_t)result_bytes ? (size_t)result_bytes : 1u, 1u);
        result_storage.size = (size_t)result_bytes;
        if (!result_storage.data)
            ok = native_fail(&failure, QA_ERROR_MEMORY, 0, "allocating native entry result");
        else
            result.as.bytes = (qa_native_memory){result_storage.data, result_storage.size};
    }
    if (ok) {
        ++instance->callback_depth;
        ++binding->active_calls;
        ok = binding->callback(binding->context, instance, binding, arguments, count, &result,
                                &failure);
        --binding->active_calls;
        --instance->callback_depth;
        if (!ok && failure.code == QA_OK)
            native_fail(&failure, QA_ERROR_ARGUMENT, 0, "native entry observer callback failed");
        if (ok && result.type != binding->signature.result.kind)
            ok = native_fail(&failure, QA_ERROR_ARGUMENT, 0, "native entry result type is invalid");
    }
    native_wire_buffer body = {0};
    if (ok)
        ok = native_wire_put_value(&body, &result, &failure);
    bool sent = send_reply(instance->runner, frame, &body, ok ? NULL : &failure, error);
    native_wire_buffer_free(&body);
    qa_buffer_free(&result_storage);
    free_values(arguments, storage, count);
    if (sent && !ok)
        native_wire_poison(instance->runner, &failure);
    return sent;
}

static bool handle_observer_write(qa_native_instance *instance, const native_wire_frame *frame,
                                  native_wire_reader *reader, qa_error *error) {
    uint64_t id = 0, offset = 0, size = 0;
    qa_native_write_event event = {0};
    qa_error failure = {0};
    bool ok = native_wire_get_u64(reader, &id, &failure) &&
              native_wire_get_u64(reader, &event.instruction, &failure) &&
              native_wire_get_u64(reader, &offset, &failure) &&
              native_wire_get_u64(reader, &size, &failure) &&
              native_wire_get_bytes(reader, &event.before, &failure) &&
              native_wire_get_bytes(reader, &event.after, &failure) &&
              native_wire_end(reader, &failure);
    qa_native_write_observer *binding = instance->write_observers;
    while (binding && binding->id != id)
        binding = binding->next;
    if (ok && (!binding || offset > binding->size || size > binding->size - offset || !size ||
               event.before.size != binding->size || event.after.size != binding->size))
        ok = native_fail(&failure, QA_ERROR_FORMAT, 0, "native write observer extent is invalid");
    if (ok) {
        event.address = binding->address;
        event.offset = (size_t)offset;
        event.size = (size_t)size;
        ++instance->callback_depth;
        ++instance->write_depth;
        ++binding->active_calls;
        ok = binding->callback(binding->context, instance, &event, &failure);
        --binding->active_calls;
        --instance->write_depth;
        --instance->callback_depth;
        if (!ok && failure.code == QA_OK)
            native_fail(&failure, QA_ERROR_ARGUMENT, 0, "native write observer callback failed");
    }
    return send_reply(instance->runner, frame, NULL, ok ? NULL : &failure, error);
}

static bool handle_callback(qa_native_instance *instance, const native_wire_frame *frame,
                            qa_error *error) {
    native_wire_reader reader = {.bytes = {frame->payload.data, frame->payload.size}};
    switch (frame->opcode) {
    case NATIVE_WIRE_IMPORT:
        return handle_import(instance, frame, &reader, error);
    case NATIVE_WIRE_DESCRIBE_SYSCALL:
        return handle_describe(instance, frame, &reader, error);
    case NATIVE_WIRE_SYSCALL:
        return handle_syscall(instance, frame, &reader, error);
    case NATIVE_WIRE_SOURCE_FAILURE: {
        qa_error failure = {0};
        bool success = false;
        bool decoded = native_wire_get_error(&reader, &success, &failure);
        if ((!decoded && failure.code == QA_OK) ||
            (decoded && (success || !native_wire_end(&reader, NULL))))
            native_fail(&failure, QA_ERROR_FORMAT, 0, "native source failure packet is invalid");
        bool sent = send_reply(instance->runner, frame, NULL, &failure, error);
        if (sent) native_wire_poison(instance->runner, &failure);
        return sent;
    }
    case NATIVE_WIRE_HOST_CHECKPOINT:
        return handle_host_checkpoint(instance, frame, &reader, error);
    case NATIVE_WIRE_HOST_RESTORE:
        return handle_host_restore(instance, frame, &reader, error);
    case NATIVE_WIRE_REGION:
    case NATIVE_WIRE_REGION_APP:
        return handle_region(instance, frame, &reader, error);
    case NATIVE_WIRE_OBSERVER_ENTRY:
        return handle_observer_entry(instance, frame, &reader, error);
    case NATIVE_WIRE_OBSERVER_WRITE:
        return handle_observer_write(instance, frame, &reader, error);
    default:
        return native_fail(error, QA_ERROR_FORMAT, frame->opcode,
                           "native runner sent an unexpected callback");
    }
}

static bool runner_request(qa_native_instance *instance, uint16_t opcode, qa_bytes payload,
                           qa_buffer *response, qa_error *error) {
    native_runner_connection *connection = instance->runner;
    if (!connection || connection->poisoned) {
        if (connection && error)
            *error = connection->failure;
        return false;
    }
    if (connection->depth == UINT32_MAX)
        return native_fail(error, QA_ERROR_ARGUMENT, connection->depth,
                           "native runner callback depth is exhausted");
    ++connection->depth;
    uint64_t sequence;
    if (!native_wire_send(connection, opcode, 0, payload, &sequence, error)) {
        --connection->depth;
        return false;
    }
    bool completed = false;
    while (!completed) {
        native_wire_frame frame = {0};
        if (!native_wire_receive(connection, &frame, error)) {
            --connection->depth;
            return false;
        }
        bool reply =
            frame.opcode == (uint16_t)(opcode | NATIVE_WIRE_REPLY) && frame.reply_to == sequence;
        bool callback = !(frame.opcode & NATIVE_WIRE_REPLY) && !frame.reply_to;
        if ((reply && frame.depth != connection->depth - 1u) ||
            (callback && frame.depth != connection->depth)) {
            native_fail(error, QA_ERROR_FORMAT, frame.depth,
                        "native runner response depth is invalid");
            native_wire_frame_free(&frame);
            native_wire_poison(connection, error);
            --connection->depth;
            return false;
        }
        if (reply) {
            native_wire_reader reader = {.bytes = {frame.payload.data, frame.payload.size}};
            bool remote_ok = false;
            if (!native_wire_get_error(&reader, &remote_ok, error)) {
                native_wire_frame_free(&frame);
                native_wire_poison(connection, error);
                --connection->depth;
                return false;
            }
            if (!remote_ok) {
                native_wire_poison(connection, error);
                native_wire_frame_free(&frame);
                --connection->depth;
                return false;
            }
            size_t remaining = reader.bytes.size - reader.offset;
            if (remaining)
                memmove(frame.payload.data, frame.payload.data + reader.offset, remaining);
            frame.payload.size = remaining;
            *response = frame.payload;
            frame.payload = (qa_buffer){0};
            native_wire_frame_free(&frame);
            completed = true;
        } else if (callback) {
            bool handled = handle_callback(instance, &frame, error);
            native_wire_frame_free(&frame);
            if (!handled) {
                native_wire_poison(connection, error);
                --connection->depth;
                return false;
            }
        } else {
            native_wire_frame_free(&frame);
            native_fail(error, QA_ERROR_FORMAT, 0, "native runner response ordering is invalid");
            native_wire_poison(connection, error);
            --connection->depth;
            return false;
        }
    }
    --connection->depth;
    return true;
}

static bool encode_values(native_wire_buffer *buffer, const qa_native_value *arguments,
                          size_t count, qa_error *error) {
    if (count > NATIVE_MAX_ARGUMENTS || (count && !arguments) ||
        !native_wire_put_u64(buffer, count, error))
        return native_fail(error, QA_ERROR_ARGUMENT, count, "native runner arguments are invalid");
    for (size_t index = 0; index < count; ++index)
        if (!native_wire_put_value(buffer, &arguments[index], error))
            return false;
    return true;
}

bool native_runner_observer_control(qa_native_instance *instance, native_hook_control control,
                                    qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool ok = native_wire_put_u64(&request, NATIVE_HOOK_CONTROL_MAGIC, error) &&
              native_wire_put_u64(&request, control.operation, error) &&
              native_wire_put_u64(&request, control.id, error) &&
              native_wire_put_u64(&request, control.address, error) &&
              native_wire_put_u64(&request, control.size, error) &&
              native_wire_put_u64(&request, control.replacement, error);
    if (ok)
        ok = runner_request(instance, NATIVE_WIRE_OBSERVER_CONTROL,
                            (qa_bytes){request.data, request.size}, &response, error);
    if (ok && response.size)
        ok = native_fail(error, QA_ERROR_FORMAT, 0, "native observer control reply is not empty");
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return ok;
}

bool native_runner_observer_entry_add(qa_native_entry_observer *binding, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool ok = native_wire_put_u64(&request, binding->id, error) &&
              native_wire_put_u64(&request, binding->address, error) &&
              native_wire_put_signature(&request, &binding->signature, error);
    if (ok)
        ok = runner_request(binding->instance, NATIVE_WIRE_OBSERVER_ENTRY_ADD,
                            (qa_bytes){request.data, request.size}, &response, error);
    if (ok && response.size)
        ok = native_fail(error, QA_ERROR_FORMAT, 0, "native observer add reply is not empty");
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return ok;
}

bool native_runner_observer_entry_remove(qa_native_entry_observer *binding, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool ok = native_wire_put_u64(&request, binding->id, error);
    if (ok)
        ok = runner_request(binding->instance, NATIVE_WIRE_OBSERVER_ENTRY_REMOVE,
                            (qa_bytes){request.data, request.size}, &response, error);
    if (ok && response.size)
        ok = native_fail(error, QA_ERROR_FORMAT, 0, "native observer remove reply is not empty");
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return ok;
}

static bool decode_call_result(qa_buffer response, const qa_native_type *expected,
                               qa_native_value *result, qa_error *error) {
    native_wire_reader reader = {.bytes = {response.data, response.size}};
    qa_native_value decoded = {0};
    qa_buffer storage = {0};
    bool ok = native_wire_get_value(&reader, &decoded, &storage, error) &&
              native_wire_end(&reader, error) && decoded.type == expected->kind;
    if (!ok && error && !error->message[0])
        native_fail(error, QA_ERROR_FORMAT, decoded.type,
                    "native runner returned the wrong result type");
    if (ok && expected->kind != QA_NATIVE_VOID) {
        if (!result) {
            ok = native_fail(error, QA_ERROR_ARGUMENT, 0,
                             "nonvoid native runner call requires a result");
        } else if (expected->kind == QA_NATIVE_BYTES) {
            if (result->type != QA_NATIVE_BYTES || !result->as.bytes.data ||
                result->as.bytes.size < storage.size) {
                ok = native_fail(error, QA_ERROR_ARGUMENT, storage.size,
                                 "native runner aggregate result storage is too small");
            } else {
                memcpy(result->as.bytes.data, storage.data, storage.size);
                result->as.bytes.size = storage.size;
            }
        } else {
            *result = decoded;
        }
    }
    qa_buffer_free(&storage);
    return ok;
}

static bool runner_result_ready(const qa_native_type *expected, qa_native_value *result,
                                qa_error *error) {
    if (expected->kind == QA_NATIVE_VOID)
        return true;
    if (!result)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "nonvoid native runner call requires a result");
    if (expected->kind == QA_NATIVE_BYTES &&
        (result->type != QA_NATIVE_BYTES || !result->as.bytes.data))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner aggregate result storage is required");
    return true;
}

static bool write_region_descriptor(qa_native_instance *instance, char **path, qa_error *error) {
    if (!instance->options.observe && !instance->region_count)
        return true;
    qa_buffer descriptor = {0};
    if (!native_regions_descriptor(instance, &descriptor, error))
        return false;
    if (!native_temp_directory(&instance->runner->temporary_directory, error)) {
        qa_buffer_free(&descriptor);
        return false;
    }
    bool ok = native_temp_file(instance->runner->temporary_directory, "regions.qanh",
                               (qa_bytes){descriptor.data, descriptor.size}, path, error);
    qa_buffer_free(&descriptor);
    return ok;
}

static bool encode_load(qa_native_instance *instance, native_wire_buffer *payload,
                        qa_error *error) {
    const qa_native_image_info *image = &instance->module->info.image;
    uint32_t frame_bits;
    memcpy(&frame_bits, &instance->options.frame_seconds, sizeof(frame_bits));
    if (!native_wire_put_u32(payload, (uint32_t)instance->module->info.profile, error) ||
        !native_wire_put_u32(payload, (uint32_t)image->target.os, error) ||
        !native_wire_put_u32(payload, (uint32_t)image->target.arch, error) ||
        !native_wire_put_u32(payload, (uint32_t)image->target.abi, error) ||
        !native_wire_put_u32(payload, image->target.pointer_bytes, error) ||
        !native_wire_put_u32(payload, instance->options.tick_rate, error) ||
        !native_wire_put_u32(payload, frame_bits, error) ||
        !native_wire_put_u32(payload, instance->options.frame_milliseconds, error) ||
        !native_wire_put_u32(payload, (uint32_t)instance->options.q3_role, error) ||
        !native_wire_put_u8(payload, instance->has_declaration ? 1u : 0u, error) ||
        !native_wire_put_u8(payload, instance->options.observe || instance->region_count ? 1u : 0u, error) ||
        !native_wire_put_raw(payload, instance->declaration.bytes,
                             sizeof(instance->declaration.bytes), error) ||
        !native_wire_put_raw(payload, image->digest.bytes, sizeof(image->digest.bytes), error) ||
        !native_wire_put_string(payload, instance->module->source, error) ||
        !native_wire_put_bytes(payload, (qa_bytes){instance->module->bytes, instance->module->size},
                               error) ||
        !native_wire_put_u64(payload, instance->original_dependency_count, error))
        return false;
    for (size_t index = 0; index < instance->original_dependency_count; ++index) {
        const qa_native_dependency *dependency = &instance->original_dependencies[index];
        if (!native_wire_put_string(payload, dependency->path, error) ||
            !native_wire_put_bytes(payload, dependency->bytes, error))
            return false;
    }
    return true;
}

static bool decode_load_response(qa_native_instance *instance, qa_buffer response,
                                 qa_error *error) {
    native_wire_reader reader = {.bytes = {response.data, response.size}};
    qa_native_entity_table table = {0};
    qa_native_address image_base;
    uint64_t image_bytes, stride;
    uint32_t lifecycle;
    if (!native_wire_get_u64(&reader, &image_base, error) ||
        !native_wire_get_u64(&reader, &image_bytes, error) ||
        !native_wire_get_u64(&reader, &table.base, error) ||
        !native_wire_get_u64(&reader, &stride, error) ||
        !native_wire_get_u32(&reader, &table.count, error) ||
        !native_wire_get_u32(&reader, &table.capacity, error) ||
        !native_wire_get_u32(&reader, &lifecycle, error) || !native_wire_end(&reader, error) ||
        !native_u64_fits_size(stride) || !image_base ||
        image_bytes != instance->module->info.image.image_bytes || lifecycle != QA_NATIVE_LOADED)
        return native_fail(error, QA_ERROR_FORMAT, reader.offset,
                           "native runner load response is invalid");
    table.stride = (size_t)stride;
    if (!native_entity_table_store(instance, table, error))
        return false;
    instance->image_base = image_base;
    instance->image_bytes = image_bytes;
    instance->lifecycle = (qa_native_lifecycle)lifecycle;
    return true;
}

bool native_runner_open(qa_native_instance *instance, const qa_native_runner_config *config,
                        qa_error *error) {
    if (!instance || !config)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner instance and configuration are required");
    native_runner_connection *connection = calloc(1, sizeof(*connection));
    if (!connection)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner connection");
#if !defined(_WIN32)
    connection->input = -1;
    connection->output = -1;
    connection->process = 0;
#endif
    connection->sequence = 1u;
    connection->maximum_frame =
        config->maximum_frame_bytes ? config->maximum_frame_bytes : NATIVE_DEFAULT_MAX_FRAME;
    if (connection->maximum_frame > NATIVE_DEFAULT_MAX_FRAME) {
        size_t requested = connection->maximum_frame;
        free(connection);
        instance->runner = NULL;
        return native_fail(error, QA_ERROR_ARGUMENT, requested,
                           "native runner frame limit exceeds the packaged protocol");
    }
    connection->instance = instance;
    connection->validate = config->validate;
    connection->validation_context = config->validation_context;
    instance->runner = connection;
    char *descriptor = NULL;
    native_runner_command command = {0};
    if (!write_region_descriptor(instance, &descriptor, error) ||
        !prepare_command(instance, config, descriptor, &command, error) ||
        !spawn_runner(connection, &command, error)) {
        free(descriptor);
        command_destroy(&command);
        native_runner_close(instance, NULL);
        return false;
    }
    free(descriptor);
    command_destroy(&command);
    native_wire_buffer load = {0};
    qa_buffer response = {0};
    bool ok = encode_load(instance, &load, error) && load.size <= connection->maximum_frame &&
              runner_request(instance, NATIVE_WIRE_LOAD, (qa_bytes){load.data, load.size},
                             &response, error) &&
              decode_load_response(instance, response, error);
    if (!ok && error && !error->message[0])
        native_fail(error, QA_ERROR_MEMORY, load.size,
                    "native runner load frame exceeds its configured limit");
    native_wire_buffer_free(&load);
    qa_buffer_free(&response);
    if (!ok) {
        native_runner_close(instance, NULL);
        return false;
    }
    return true;
}

bool qa_native_create_runner(qa_native_module *module, const qa_native_options *options,
                             const qa_native_runner_config *runner, qa_native_instance **out,
                             qa_error *error) {
    if (!module || !options || !runner || !out || *out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native module, options, runner and output are required");
    if (module->info.profile == QA_NATIVE_Q3_VMMAIN &&
        (!options->describe_syscall || !options->syscall))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native Q3 requires syscall description and dispatch");
    if (module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && !options->import)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Quake Live API 10 requires engine import dispatch");
    if ((module->info.profile == QA_NATIVE_Q2_GAME_API2023 ||
         module->info.profile == QA_NATIVE_Q2_CGAME_API2023) &&
        (!options->tick_rate || !isfinite(options->frame_seconds) ||
         options->frame_seconds <= 0.0f || !options->frame_milliseconds))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Q2 API 2023 requires a positive tick interval");
    qa_native_instance *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner instance");
    instance->module = module;
    instance->options = *options;
    instance->backend = QA_NATIVE_BACKEND_RUNNER;
    instance->lifecycle = QA_NATIVE_LOADED;
    if (!native_instance_setup_identity(instance, options, error)) {
        free(instance);
        return false;
    }
    instance->options.declaration = NULL;
    instance->options.declaration_digest = NULL;
    qa_native_module_retain(module);
    if (!native_profile_prepare_remote(instance, error) ||
        !native_runner_open(instance, runner, error)) {
        if (instance->runner) {
            instance->lifecycle = QA_NATIVE_SHUT_DOWN;
            *out = instance;
            return false;
        }
        native_profile_unbind(instance);
        qa_native_module_release(module);
        native_regions_destroy(instance);
        native_original_dependencies_destroy(instance);
        free(instance->slots);
        free(instance);
        return false;
    }
    instance->options.dependencies = NULL;
    instance->options.dependency_count = 0;
    *out = instance;
    return true;
}

bool native_runner_close(qa_native_instance *instance, qa_error *error) {
    if (!instance || !instance->runner)
        return true;
    native_runner_connection *connection = instance->runner;
    bool protocol_ok = true;
    if (!connection->poisoned && connection->process) {
        qa_buffer response = {0};
        protocol_ok =
            runner_request(instance, NATIVE_WIRE_DESTROY, (qa_bytes){0}, &response, error);
        qa_buffer_free(&response);
    }
    connection->poisoned = true;
    if (!close_runner_process(connection, error)) return false;
    native_remove_tree(connection->temporary_directory);
    free(connection->temporary_directory);
    memset(connection, 0, sizeof(*connection));
    free(connection);
    instance->runner = NULL;
    return protocol_ok;
}

bool native_runner_restart_original(qa_native_instance *instance, qa_error *error) {
    if (!instance || !instance->runner)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native reload requires its actual runner connection");
    if (instance->runner->validate && !instance->runner->validate(
        instance->runner->validation_context, instance->module->info.image.target,
        instance->options.observe || instance->region_count != 0, error)) return false;
    qa_buffer response = {0};
    bool ok = runner_request(instance, NATIVE_WIRE_RESTART_ORIGINAL, (qa_bytes){0}, &response, error);
    if (ok) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        uint64_t image_base = 0, image_bytes = 0;
        uint32_t lifecycle = 0;
        ok = native_wire_get_u64(&reader, &image_base, error) &&
            native_wire_get_u64(&reader, &image_bytes, error) &&
            native_wire_get_u32(&reader, &lifecycle, error) && native_wire_end(&reader, error) &&
            image_base && image_bytes == instance->module->info.image.image_bytes &&
            lifecycle == QA_NATIVE_RESTART_READY;
        if (!ok) {
            if (!error || error->code == QA_OK)
                native_fail(error, QA_ERROR_FORMAT, reader.offset, "native runner original reload response is invalid");
            native_wire_poison(instance->runner, error);
        } else {
            native_profile_unbind(instance);
            instance->entities = (qa_native_entity_table){0};
            if (instance->slot_capacity)
                memset(instance->slots, 0, (size_t)instance->slot_capacity * sizeof(*instance->slots));
            instance->image_base = image_base;
            instance->image_bytes = image_bytes;
            ok = native_profile_prepare_remote(instance, error);
            if (!ok) native_wire_poison(instance->runner, error);
        }
    }
    qa_buffer_free(&response);
    return ok;
}

static bool runner_sync_entities(qa_native_instance *instance, qa_error *error) {
    qa_native_profile profile = instance->module->info.profile;
    if (profile != QA_NATIVE_Q2_GAME_API3 && profile != QA_NATIVE_Q2_GAME_API2023 &&
        profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return true;
    qa_native_entity_table table;
    if (!native_runner_entity_get(instance, &table, error))
        return false;
    return native_entity_table_store(instance, table, error);
}

bool native_runner_call(qa_native_instance *instance, const char *entry,
                        const qa_native_value *arguments, size_t count, qa_native_value *result,
                        qa_error *error) {
    const native_entry_binding *binding = native_entry(instance, entry);
    if (!binding)
        return native_fail(error, QA_ERROR_NOT_FOUND, 0,
                           "native runner entry is not in the selected profile");
    const native_signature_spec *spec = &binding->spec;
    if (!runner_arguments_ready(instance, &spec->signature, arguments, count, error) ||
        !runner_result_ready(&spec->signature.result, result, error))
        return false;
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_string(&request, entry, error) &&
              encode_values(&request, arguments, count, error);
    if (ok) {
        native_call_started(instance);
        received = runner_request(instance, NATIVE_WIRE_CALL,
                                  (qa_bytes){request.data, request.size}, &response, error);
    }
    if (received)
        ok = decode_call_result(response, &spec->signature.result, result, error);
    else
        ok = false;
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    if (received && !ok) {
        native_wire_poison(instance->runner, error);
        return false;
    }
    if (ok && !runner_sync_entities(instance, error)) {
        native_wire_poison(instance->runner, error);
        return false;
    }
    return ok;
}

bool native_runner_invoke(qa_native_instance *instance, qa_native_address address,
                          const qa_native_signature *signature, const qa_native_value *arguments,
                          size_t count, qa_native_value *result, qa_error *error) {
    if (!runner_arguments_ready(instance, signature, arguments, count, error) ||
        !runner_result_ready(&signature->result, result, error))
        return false;
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, address, error) &&
              native_wire_put_signature(&request, signature, error) &&
              encode_values(&request, arguments, count, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_INVOKE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received)
        ok = decode_call_result(response, &signature->result, result, error);
    else
        ok = false;
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    if (received && !ok) {
        native_wire_poison(instance->runner, error);
        return false;
    }
    if (ok && !runner_sync_entities(instance, error)) {
        native_wire_poison(instance->runner, error);
        return false;
    }
    return ok;
}

bool native_runner_observer_original(qa_native_entry_observer *binding,
                                     const qa_native_value *arguments, size_t count,
                                     qa_native_value *result, qa_error *error) {
    qa_native_instance *instance = binding->instance;
    if (!runner_arguments_ready(instance, &binding->signature, arguments, count, error) ||
        !runner_result_ready(&binding->signature.result, result, error))
        return false;
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, binding->id, error) &&
              encode_values(&request, arguments, count, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_OBSERVER_ORIGINAL,
                                  (qa_bytes){request.data, request.size}, &response, error);
    ok = received && decode_call_result(response, &binding->signature.result, result, error);
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    if (ok)
        ok = runner_sync_entities(instance, error);
    return runner_finish_response(instance, received, ok, error);
}

static bool runner_address(qa_native_instance *instance, const char *name, qa_native_address *out,
                           native_wire_opcode operation, qa_error *error) {
    if (!name || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner export name and output are required");
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_string(&request, name, error);
    if (ok)
        received = runner_request(instance, operation,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_get_u64(&reader, out, error) && native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_export(qa_native_instance *instance, const char *name, qa_native_address *out,
                          qa_error *error) {
    return runner_address(instance, name, out, NATIVE_WIRE_EXPORT, error);
}

bool native_runner_entry_address(qa_native_instance *instance, const char *name,
                                 qa_native_address *out, qa_error *error) {
    return runner_address(instance, name, out, NATIVE_WIRE_ENTRY_ADDRESS, error);
}

bool native_runner_read(qa_native_instance *instance, qa_native_address source, void *out,
                        size_t bytes, qa_error *error) {
    size_t maximum = instance->runner->maximum_frame > NATIVE_WIRE_REPLY_BYTES_OVERHEAD
                         ? instance->runner->maximum_frame - NATIVE_WIRE_REPLY_BYTES_OVERHEAD
                         : 0;
    if ((!out && bytes) || bytes > maximum)
        return native_fail(error, QA_ERROR_ARGUMENT, bytes, "native runner read range is invalid");
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok =
        native_wire_put_u64(&request, source, error) && native_wire_put_u64(&request, bytes, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_READ,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        qa_bytes received;
        ok = native_wire_get_bytes(&reader, &received, error) && native_wire_end(&reader, error) &&
             received.size == bytes;
        if (ok && bytes)
            memcpy(out, received.data, bytes);
        if (!ok && error && !error->message[0])
            native_fail(error, QA_ERROR_FORMAT, response.size,
                        "native runner read returned the wrong byte count");
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_write(qa_native_instance *instance, qa_native_address destination,
                         qa_bytes bytes, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, destination, error) &&
              native_wire_put_bytes(&request, bytes, error) &&
              request.size <= instance->runner->maximum_frame;
    if (!ok && error && !error->message[0])
        native_fail(error, QA_ERROR_ARGUMENT, request.size,
                    "native runner write exceeds its configured frame limit");
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_WRITE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_allocation_query(qa_native_instance *instance, qa_native_address address,
                                    qa_native_allocation_info *out, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, address, error);
    if (ok) received = runner_request(instance, NATIVE_WIRE_ALLOCATION_QUERY,
        (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        uint32_t tag;
        ok = native_wire_get_u64(&reader, &out->base, error) &&
            native_wire_get_u64(&reader, &out->bytes, error) && native_wire_get_u32(&reader, &tag, error) &&
            native_wire_end(&reader, error) && out->bytes && address >= out->base && address - out->base < out->bytes;
        if (ok) out->tag = (int32_t)tag;
        else if (error && error->code == QA_OK)
            native_fail(error, QA_ERROR_FORMAT, 0, "native allocation query returned an invalid extent");
    } else ok = false;
    native_wire_buffer_free(&request); qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_allocate(qa_native_instance *instance, size_t bytes, int32_t tag,
                            qa_native_address *out, qa_error *error) {
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner allocation output is required");
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, bytes, error) &&
              native_wire_put_u32(&request, (uint32_t)tag, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_ALLOCATE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_get_u64(&reader, out, error) && native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_free(qa_native_instance *instance, qa_native_address address, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, address, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_FREE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_free_tag(qa_native_instance *instance, int32_t tag, qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u32(&request, (uint32_t)tag, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_FREE_TAG,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_entity_get(qa_native_instance *instance, qa_native_entity_table *out,
                              qa_error *error) {
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner entity table output is required");
    qa_buffer response = {0};
    bool received =
        runner_request(instance, NATIVE_WIRE_ENTITY_GET, (qa_bytes){0}, &response, error);
    bool ok = received;
    uint64_t stride = 0;
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_get_u64(&reader, &out->base, error) &&
             native_wire_get_u64(&reader, &stride, error) &&
             native_wire_get_u32(&reader, &out->count, error) &&
             native_wire_get_u32(&reader, &out->capacity, error) &&
             native_wire_end(&reader, error) && native_u64_fits_size(stride);
        if (!ok && error && !error->message[0])
            native_fail(error, QA_ERROR_FORMAT, reader.offset,
                        "native runner entity table is invalid");
        if (ok)
            out->stride = (size_t)stride;
    }
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_entity_set(qa_native_instance *instance, qa_native_entity_table table,
                              qa_error *error) {
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    bool received = false;
    bool ok = native_wire_put_u64(&request, table.base, error) &&
              native_wire_put_u64(&request, table.stride, error) &&
              native_wire_put_u32(&request, table.count, error) &&
              native_wire_put_u32(&request, table.capacity, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_ENTITY_SET,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_checkpoint_capture(qa_native_instance *instance,
                                      qa_native_checkpoint_request request_value,
                                      qa_native_checkpoint *out, qa_error *error) {
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native runner checkpoint output is required");
    native_wire_buffer request = {0};
    qa_buffer response = {0};
    uint32_t flags = (request_value.game ? 1u : 0u) | (request_value.level ? 2u : 0u) |
                     (request_value.autosave ? 4u : 0u) | (request_value.transition ? 8u : 0u);
    bool received = false;
    bool ok = native_wire_put_u32(&request, flags, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_CHECKPOINT_CAPTURE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        qa_bytes encoded;
        ok = native_wire_get_bytes(&reader, &encoded, error) && native_wire_end(&reader, error) &&
             qa_native_checkpoint_decode(encoded, out, error);
    } else {
        ok = false;
    }
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

bool native_runner_checkpoint_restore(qa_native_instance *instance,
                                      const qa_native_checkpoint *checkpoint,
                                      qa_native_restore_part part, qa_error *error) {
    qa_buffer encoded = {0}, response = {0};
    native_wire_buffer request = {0};
    bool received = false;
    bool ok = qa_native_checkpoint_encode(checkpoint, &encoded, error) &&
              native_wire_put_u32(&request, (uint32_t)part, error) &&
              native_wire_put_bytes(&request, (qa_bytes){encoded.data, encoded.size}, error);
    if (ok)
        received = runner_request(instance, NATIVE_WIRE_CHECKPOINT_RESTORE,
                                  (qa_bytes){request.data, request.size}, &response, error);
    if (received) {
        native_wire_reader reader = {.bytes = {response.data, response.size}};
        ok = native_wire_end(&reader, error);
    } else {
        ok = false;
    }
    qa_buffer_free(&encoded);
    native_wire_buffer_free(&request);
    qa_buffer_free(&response);
    return runner_finish_response(instance, received, ok, error);
}

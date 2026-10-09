#include "internal.h"

static qa_command_result declared_command(void *context,
    const qa_command_invocation *command, qa_error *error)
{
    qa_q3_host *host=context;
    if (!host || host->retired || !command || command->console!=host->options.console ||
        !qa_console_invocation_current(command->console,command) ||
        command->receiver!=host->options.owner || command->registration_owner!=host->options.service_owner ||
        !(host->options.command_context.cvar_view ?
            qa_console_invocation_delivered_view(command,host->options.command_context.cvar_view,
                host->options.owner,host->options.service_owner) :
            qa_console_invocation_delivered(command,host->options.owner,host->options.service_owner)) ||
        command->context.seat!=host->options.command_context.seat || !host->options.console_command) {
        q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 declaration lost its retained module command owner");
        return QA_COMMAND_FAILED;
    }
    return host->options.console_command(host->options.console_command_context,command,error);
}
#include <stdio.h>

q3_service_result q3_common(q3_call *call, int32_t *result, qa_error *error)
{
    qa_q3_host *host = call->host;
    const qa_q3_host_common_services *services = &host->options.common;
    const qa_qvm_role role = host->options.role;
    const bool ui = role == QA_QVM_UI, game = role == QA_QVM_GAME;
    int32_t trap = call->service;
    if (trap == (ui ? 1 : 0) || trap == (ui ? 0 : 1)) {
        qa_buffer text = {0};
        if (!q3_string(call, call->arguments[0], &text, error)) return Q3_FAILED;
        bool ok = true;
        if (trap == (ui ? 0 : 1))
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, (const char *)text.data);
        else if (services->print)
            services->print(services->context, (const char *)text.data);
        else
            ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 console output is unbound");
        qa_buffer_free(&text);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    if (trap == 2) {
        if (!services->milliseconds) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 clock is unbound");
            return Q3_FAILED;
        }
        uint32_t word = services->milliseconds(services->context);
        memcpy(result, &word, sizeof(word));
        return Q3_COMPLETED;
    }
    if (!game && trap == (ui ? Q3_UI_MEMORY_REMAINING : Q3_CGAME_MEMORY_REMAINING)) {
        *result = qa_memory_available();
        return Q3_COMPLETED;
    }
    if (trap == (ui ? 10 : game ? 8 : 7) || trap == (ui ? 11 : game ? 9 : 8) ||
        (!ui && !game && trap == 9)) {
        qa_native_host_command_view args = {0};
        if (!services->arguments || !services->arguments(services->context, &args, error)) {
            if (!services->arguments)
                q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 command arguments are unbound");
            return Q3_FAILED;
        }
        if (args.count > INT32_MAX || (args.count && !args.arguments)) {
            q3_fail(error, QA_ERROR_FORMAT, 0, "invalid Q3 command argument view");
            return Q3_FAILED;
        }
        char mapped_index[16]; const char *index_text = NULL;
        if (!ui && !game && args.canonical_configstrings && host->options.abi != QA_QVM_Q3_MODERN &&
            args.count && !strcmp(args.arguments[0], "cs")) {
            int32_t index = 0; bool mapped = false;
            if (!qa_qvm_client_configstring_argument(host->options.abi,
                args.count > 1 ? args.arguments[1] : NULL, &index, &mapped, error)) return Q3_FAILED;
            if (mapped) { snprintf(mapped_index, sizeof(mapped_index), "%d", index); index_text = mapped_index; }
        }
        if (trap == (ui ? 10 : game ? 8 : 7)) {
            *result = (int32_t)args.count;
            return Q3_COMPLETED;
        }
        if (!ui && !game && trap == 9) {
            char text[1024]; size_t used = 0;
            for (size_t i = 1; i < args.count; ++i) {
                const char *value = i == 1 && index_text ? index_text : args.arguments[i];
                size_t size = strlen(value);
                if (size >= sizeof(text) - used - (i > 1 ? 1u : 0u)) {
                    q3_fail(error, QA_ERROR_FORMAT, used, "Cmd_Args exceeds its source buffer");
                    return Q3_FAILED;
                }
                if (i > 1) text[used++] = ' ';
                memcpy(text + used, value, size); used += size;
            }
            text[used] = 0;
            return q3_write_string(call, call->arguments[0], text, q3_integer(call, 1), error)
                       ? Q3_COMPLETED : Q3_FAILED;
        }
        int32_t index = q3_integer(call, 0);
        const char *text = index == 1 && index_text ? index_text :
            index >= 0 && (size_t)index < args.count ? args.arguments[index] : "";
        return q3_write_string(call, call->arguments[1], text, q3_integer(call, 2), error)
                   ? Q3_COMPLETED : Q3_FAILED;
    }
    if (trap == (ui ? 64 : game ? 41 : 70)) {
        if (!services->calendar) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 calendar is unbound");
            return Q3_FAILED;
        }
        qa_q3_host_calendar calendar;
        *result = services->calendar(services->context, call->arguments[0] ? &calendar : NULL);
        if (call->arguments[0]) {
            const int32_t values[] = {calendar.second, calendar.minute, calendar.hour,
                calendar.day, calendar.month, calendar.year, calendar.weekday,
                calendar.year_day, calendar.is_dst};
            for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
                if (!q3_write_word(call, call->arguments[0] + i * 4u, (uint32_t)values[i], error))
                    return Q3_FAILED;
        }
        return Q3_COMPLETED;
    }
    if (!ui && !game && (trap == 15 || trap == 16 || trap == 72)) {
        qa_buffer text = {0};
        if (!q3_string(call, call->arguments[0], &text, error)) return Q3_FAILED;
        bool ok;
        if (trap == 16) {
            ok = services->client_command &&
                 services->client_command(services->context, (const char *)text.data, error);
            if (!services->client_command)
                q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 reliable client command owner is unbound");
        } else if (!host->options.console) {
            ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 command registry is unbound");
        } else if (trap == 15) {
            if (!host->options.console_command) {
                q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 AddCommand requires its actual module export");
                ok=false;
            } else {
                qa_console_contribution owner={.receiver=host->options.owner,
                    .lifetime_owner=host->options.service_owner,.seat=host->options.command_context.seat,
                    .cvar_view=host->options.command_context.cvar_view,
                    .callback=declared_command,.user=host};
                ok=qa_console_contribute(host->options.console,(const char *)text.data,&owner,error);
            }
        } else {
            qa_console_uncontribute(host->options.console, (const char *)text.data,
                                      host->options.owner, host->options.service_owner);
            ok = true;
        }
        qa_buffer_free(&text);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    if (trap != (ui ? 12 : 14)) return Q3_UNHANDLED;
    if (!host->options.console) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 command buffer is unbound");
        return Q3_FAILED;
    }
    int32_t when = !ui && !game ? 2 : q3_integer(call, 0);
    uint64_t pointer = call->arguments[!ui && !game ? 0 : 1];
    if (when < 0 || when > 2) {
        q3_fail(error, QA_ERROR_ARGUMENT, 0, "Cbuf_ExecuteText: bad exec_when");
        return Q3_FAILED;
    }
    qa_buffer text = {0};
    if ((when != 0 || pointer) && !q3_string(call, pointer, &text, error)) return Q3_FAILED;
    bool ok;
    if (when == 0 && !pointer) {
        size_t executed;
        ok = qa_console_drain(host->options.console, 0, &executed, error);
    } else if (when == 0)
        ok = qa_console_execute_now(host->options.console, &host->options.command_context,
                                     (const char *)text.data, error);
    else if (when == 1)
        ok = qa_console_insert(host->options.console, &host->options.command_context,
                                (const char *)text.data, error);
    else
        ok = qa_console_append(host->options.console, &host->options.command_context,
                                (const char *)text.data, error);
    qa_buffer_free(&text);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}

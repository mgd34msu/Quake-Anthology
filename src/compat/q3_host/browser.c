/* UI LAN syscall behavior follows cl_ui.c/cl_main.c. GPL-2.0-or-later. */
#include "browser.h"

uint32_t qa_q3_host_browser_services_mask(const qa_q3_host_browser_services *services)
{
    if (!services) return 0;
    const bool callbacks[] = {
        services->current != NULL, services->server_count != NULL,
        services->server_address != NULL, services->server_info != NULL,
        services->ping_count != NULL, services->clear_ping != NULL,
        services->get_ping != NULL, services->ping_info != NULL,
        services->mark_visible != NULL, services->update_pings != NULL,
        services->reset_pings != NULL, services->load_cache != NULL,
        services->save_cache != NULL, services->add_server != NULL,
        services->remove_server != NULL, services->server_status != NULL,
        services->server_ping != NULL, services->server_visible != NULL,
        services->compare_servers != NULL
    };
    uint32_t mask = 0;
    for (size_t i = 0; i < sizeof(callbacks) / sizeof(*callbacks); ++i)
        if (callbacks[i]) mask |= UINT32_C(1) << i;
    return mask;
}

bool qa_q3_host_browser_services_validate(const qa_q3_host_browser_services *services,
                                          qa_error *error)
{
    uint32_t mask = qa_q3_host_browser_services_mask(services);
    if (services && ((!mask && !services->context) ||
        (mask == UINT32_C(0x7ffff) && services->context))) return true;
    return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Incomplete Q3 UI browser services");
}

typedef struct browser_memory {
    const q3_call *call;
    const qa_q3_host_browser_services *services;
    uint64_t address;
    int32_t capacity;
} browser_memory;

static bool memory_current(const browser_memory *memory, qa_error *error)
{
    return memory->services->current(memory->services->context, error);
}

static bool write_text(void *context, const char *text, qa_error *error)
{
    const browser_memory *memory = context;
    if (!memory_current(memory, error)) return false;
    const uint8_t zero = 0;
    bool ok = text ? q3_write_string(memory->call, memory->address, text, memory->capacity, error) :
        q3_write(memory->call, memory->address, (qa_bytes){&zero, 1}, error);
    return ok && memory_current(memory, error);
}

static bool read_address(void *context, qa_buffer *out, qa_error *error)
{
    const browser_memory *memory = context;
    qa_buffer text = {0};
    if (!memory_current(memory, error) ||
        !q3_string(memory->call, memory->address, &text, error)) {
        qa_buffer_free(&text);
        return false;
    }
    if (!memory_current(memory, error)) { qa_buffer_free(&text); return false; }
    *out = text;
    return true;
}

static bool read_name(void *context, qa_buffer *out, qa_error *error)
{
    const browser_memory *memory = context;
    if (!memory_current(memory, error)) return false;
    if (!memory->address)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q_strncpyz: NULL src");
    uint8_t bytes[32];
    size_t length = 0;
    while (length < 31) {
        if (memory->address > UINT64_MAX - length)
            return q3_fail(error, QA_ERROR_ARGUMENT, length, "Q3 browser name address overflow");
        if (!q3_read(memory->call, memory->address + length, bytes + length, 1, error) ||
            !memory_current(memory, error)) return false;
        if (!bytes[length]) break;
        ++length;
    }
    bytes[length] = 0;
    qa_buffer text = {.data = malloc(length + 1), .size = length + 1};
    if (!text.data)
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Copying Q3 browser name");
    memcpy(text.data, bytes, text.size);
    *out = text;
    return true;
}

static bool has_record(int32_t source, int32_t index)
{
    return index >= 0 && (source == 2 ? index < 4096 :
        (source == 0 || source == 1 || source == 3) && index < 128);
}

static bool browser_trap(int32_t trap)
{
    return (trap >= 46 && trap <= 49) || (trap >= 65 && trap <= 74) ||
        (trap >= 82 && trap <= 85);
}

q3_service_result q3_browser(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_UI) return Q3_UNHANDLED;
    bool legacy = call->host->options.abi == QA_QVM_Q3_116N &&
        call->source_service >= 46 && call->source_service <= 49;
    int32_t trap = call->service;
    if (!legacy && !browser_trap(trap)) return Q3_UNHANDLED;
    qa_q3_host_browser_services services = call->host->options.browser;
    if (!qa_q3_host_browser_services_validate(&services, error)) return Q3_FAILED;
    if (!services.current) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 UI browser owner is unbound");
        return Q3_FAILED;
    }
    if (!services.current(services.context, error)) return Q3_FAILED;
    int32_t value = 0;
    bool ok = false;
    if (legacy) {
        int32_t source = call->source_service <= 47 ? 0 : 2;
        if (call->source_service == 46 || call->source_service == 48)
            ok = services.server_count(services.context, source, &value, error);
        else {
            browser_memory memory = {call, &services, call->arguments[1], q3_integer(call, 2)};
            qa_q3_host_browser_writer writer = {&memory, write_text};
            ok = !memory.capacity || write_text(&memory, NULL, error);
            if (ok) ok = services.server_address(services.context, source, q3_integer(call, 0),
                                                 memory.capacity, &writer, error);
        }
    } else switch (trap) {
    case 46:
        ok = services.ping_count(services.context, &value, error);
        break;
    case 47:
        ok = services.clear_ping(services.context, q3_integer(call, 0), error);
        break;
    case 48: {
        browser_memory memory = {call, &services, call->arguments[1], q3_integer(call, 2)};
        qa_q3_host_browser_writer writer = {&memory, write_text};
        int32_t time = 0;
        ok = services.get_ping(services.context, q3_integer(call, 0), memory.capacity, &writer, &time, error);
        if (ok) ok = memory_current(&memory, error) &&
            q3_write_word(call, call->arguments[3], (uint32_t)time, error);
        break;
    }
    case 49: {
        browser_memory memory = {call, &services, call->arguments[1], q3_integer(call, 2)};
        qa_q3_host_browser_writer writer = {&memory, write_text};
        bool present = false;
        ok = services.ping_info(services.context, q3_integer(call, 0), memory.capacity, &writer, &present, error);
        if (ok && !present && memory.capacity) ok = write_text(&memory, NULL, error);
        break;
    }
    case 65:
        ok = services.server_count(services.context, q3_integer(call, 0), &value, error);
        break;
    case 66:
    case 67: {
        int32_t source = q3_integer(call, 0), index = q3_integer(call, 1);
        browser_memory memory = {call, &services, call->arguments[2], q3_integer(call, 3)};
        qa_q3_host_browser_writer writer = {&memory, write_text};
        if (trap == 67 && !memory.address) { ok = true; break; }
        bool valid = has_record(source, index);
        ok = (trap != 67 && valid) || write_text(&memory, NULL, error);
        if (ok && valid) ok = trap == 66 ?
            services.server_address(services.context, source, index, memory.capacity, &writer, error) :
            services.server_info(services.context, source, index, memory.capacity, &writer, error);
        break;
    }
    case 68:
        ok = services.mark_visible(services.context, q3_integer(call, 0), q3_integer(call, 1),
                                   q3_integer(call, 2), error);
        break;
    case 69: {
        bool active = false;
        ok = services.update_pings(services.context, q3_integer(call, 0), &active, error);
        value = active;
        break;
    }
    case 70:
        ok = services.reset_pings(services.context, q3_integer(call, 0), error);
        break;
    case 71:
        ok = services.load_cache(services.context, error);
        break;
    case 72:
        ok = services.save_cache(services.context, error);
        break;
    case 73: {
        browser_memory name = {call, &services, call->arguments[1], 0};
        browser_memory address = {call, &services, call->arguments[2], 0};
        qa_q3_host_browser_reader name_reader = {&name, read_name}, address_reader = {&address, read_address};
        ok = services.add_server(services.context, q3_integer(call, 0), &name_reader, &address_reader, &value, error);
        break;
    }
    case 74: {
        browser_memory address = {call, &services, call->arguments[1], 0};
        qa_q3_host_browser_reader reader = {&address, read_address};
        ok = services.remove_server(services.context, q3_integer(call, 0), &reader, error);
        break;
    }
    case 82: {
        browser_memory address = {call, &services, call->arguments[0], 0};
        browser_memory memory = {call, &services, call->arguments[1], q3_integer(call, 2)};
        qa_q3_host_browser_writer writer = {&memory, write_text};
        qa_buffer text = {0};
        bool present = false;
        ok = !address.address || read_address(&address, &text, error);
        if (ok) ok = services.server_status(services.context, (const char *)text.data, memory.capacity,
                                             memory.address ? &writer : NULL, &present, error);
        qa_buffer_free(&text);
        value = present;
        break;
    }
    case 83:
        ok = services.server_ping(services.context, q3_integer(call, 0), q3_integer(call, 1), &value, error);
        break;
    case 84:
        ok = services.server_visible(services.context, q3_integer(call, 0), q3_integer(call, 1), &value, error);
        break;
    case 85:
        ok = services.compare_servers(services.context, q3_integer(call, 0), q3_integer(call, 1),
                                      q3_integer(call, 2), q3_integer(call, 3), q3_integer(call, 4), &value, error);
        break;
    }
    if (!ok || !services.current(services.context, error)) return Q3_FAILED;
    *result = value;
    return Q3_COMPLETED;
}

#include "native_q3_ipfilters.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "unified_q3_events.h"
#include "qa/source_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IP_FILTER_CAPACITY = 1024, IP_BAN_CAPACITY = 256, IP_ARGUMENT_CAPACITY = 1024 };

typedef struct ip_filter {
    uint32_t mask, compare;
} ip_filter;

typedef struct ip_filter_state {
    ip_filter filters[IP_FILTER_CAPACITY];
    size_t count;
    uint64_t ban_modification_count;
    char ban_vm_string[IP_BAN_CAPACITY];
    bool initialized, ban_vm_present;
} ip_filter_state;

typedef enum ip_operation {
    IP_IDLE, IP_INITIALIZING, IP_CONSOLE, IP_CAPTURING, IP_IMPORTING
} ip_operation;

struct application_native_q3_ipfilters {
    application_provider *provider;
    ip_filter_state state;
    ip_operation operation;
};

static struct application_native_q3_ipfilters *owner_at(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 ? provider->native_q3_ipfilters : NULL;
}

static bool source_live(const struct application_native_q3_ipfilters *owner)
{
    const application_provider *provider = owner->provider;
    return provider->native_q3_ipfilters == owner && provider->constructed && provider->attached &&
        provider->state.q3 && !provider->close_pending && !provider->application->destroy_requested;
}

static bool admitted(const struct application_native_q3_ipfilters *owner,
    const qa_command_context *context, qa_error *error)
{
    if (!source_live(owner) || (context && !application_command_active(owner->provider->application, context)))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 IP filter source has retired");
    return true;
}

static bool emit(struct application_native_q3_ipfilters *owner, const qa_command_context *context,
    const char *text, qa_error *error)
{
    qa_console *console;
    if (!application_native_q3_console_at(owner->provider, &console, NULL, NULL))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 IP filter console is absent");
    qa_console_emit(console, context, text);
    return admitted(owner, context, error);
}

static bool digit(unsigned char byte)
{
    return byte >= '0' && byte <= '9';
}

/* The source advances over one arbitrary delimiter and stores octets modulo
 * 256. A short address leaves the remaining filter bytes as wildcards. */
static bool string_to_filter(struct application_native_q3_ipfilters *owner,
    const qa_command_context *context, const char *text, ip_filter *out, bool *parsed,
    qa_error *error)
{
    ip_filter result = {0};
    const unsigned char *cursor = (const unsigned char *)text;
    *parsed = false;
    for (unsigned octet = 0; octet < 4; ++octet) {
        if (!digit(*cursor)) {
            if (*cursor == '*') {
                ++cursor;
                if (!*cursor) break;
                ++cursor;
                continue;
            }
            char message[IP_ARGUMENT_CAPACITY + 32];
            snprintf(message, sizeof(message), "Bad filter address: %s\n", (const char *)cursor);
            return emit(owner, context, message, error);
        }
        uint32_t value = 0;
        size_t digits = 0;
        while (digit(*cursor)) {
            if (++digits >= 128)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "IP filter component exceeds the source127-digit scratch buffer");
            value = value * 10u + (uint32_t)(*cursor++ - '0');
        }
        result.compare |= (value & 255u) << (octet * 8);
        result.mask |= 255u << (octet * 8);
        if (!*cursor) break;
        ++cursor;
    }
    *out = result;
    *parsed = true;
    return true;
}

static bool update_bans(struct application_native_q3_ipfilters *owner,
    const qa_command_context *context, qa_error *error)
{
    char list[IP_BAN_CAPACITY] = {0};
    size_t size = 0;
    for (size_t row = 0; row < owner->state.count; ++row) {
        const ip_filter filter = owner->state.filters[row];
        if (filter.compare == UINT32_MAX) continue;
        char address[64];
        size_t length = 0;
        for (unsigned octet = 0; octet < 4; ++octet) {
            if (((filter.mask >> (octet * 8)) & 255u) == 255u)
                length += (size_t)snprintf(address + length, sizeof(address) - length, "%u",
                    (filter.compare >> (octet * 8)) & 255u);
            else address[length++] = '*';
            address[length++] = octet < 3 ? '.' : ' ';
        }
        address[length] = 0;
        if (size + length >= sizeof(list)) {
            if (!emit(owner, context, "g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n", error)) return false;
            break;
        }
        memcpy(list + size, address, length + 1);
        size += length;
    }
    return application_native_q3_settings_force_set(owner->provider, APPLICATION_Q3_SETTING_G_BAN_IPS, list, error) &&
        admitted(owner, context, error);
}

static bool add_ip(struct application_native_q3_ipfilters *owner,
    const qa_command_context *context, const char *text, qa_error *error)
{
    size_t index = 0;
    while (index < owner->state.count && owner->state.filters[index].compare != UINT32_MAX) ++index;
    if (index == IP_FILTER_CAPACITY) return emit(owner, context, "IP filter list is full\n", error);
    ip_filter parsed = {0};
    bool valid;
    if (!string_to_filter(owner, context, text, &parsed, &valid, error)) return false;
    if (index == owner->state.count) {
        owner->state.filters[index] = (ip_filter){.compare = UINT32_MAX};
        ++owner->state.count;
    }
    if (valid) owner->state.filters[index] = parsed;
    else owner->state.filters[index].compare = UINT32_MAX;
    return update_bans(owner, context, error);
}

bool application_native_q3_ipfilters_create(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->application ||
        !provider->owner || !provider->product || provider->product->family != QA_GAME_Q3 ||
        !application_native_q3_console_registry(provider) || provider->native_q3_ipfilters ||
        provider->close_pending || provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP filters require their actual source owner");
    struct application_native_q3_ipfilters *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 IP filters");
    owner->provider = provider;
    provider->native_q3_ipfilters = owner;
    return true;
}

bool application_native_q3_ipfilters_idle(const application_provider *provider)
{
    const struct application_native_q3_ipfilters *owner = owner_at(provider);
    return !owner || owner->operation == IP_IDLE;
}

bool application_native_q3_ipfilters_initialized(const application_provider *provider)
{
    const struct application_native_q3_ipfilters *owner = owner_at(provider);
    return owner && owner->state.initialized;
}

bool application_native_q3_ipfilters_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q3_ipfilters_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP filters are borrowed");
    free(provider->native_q3_ipfilters);
    provider->native_q3_ipfilters = NULL;
    return true;
}

bool application_native_q3_ipfilters_init(application_provider *provider, qa_error *error)
{
    struct application_native_q3_ipfilters *owner = owner_at(provider);
    const application_native_q3_cvar_snapshot *snapshot;
    if (!owner || owner->operation != IP_IDLE || !admitted(owner, NULL, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP filters have no idle source Init owner");
    if (!application_native_q3_settings_snapshot_at(provider, APPLICATION_Q3_SETTING_G_BAN_IPS, &snapshot, error)) return false;
    size_t size = strlen(snapshot->value);
    if (size >= IP_BAN_CAPACITY)
        return application_fail(error, QA_ERROR_ARGUMENT, "g_banIPs exceeds the source256-byte vmCvar buffer");
    char input[IP_BAN_CAPACITY];
    memcpy(input, snapshot->value, size + 1);
    uint64_t modification_count = snapshot->modification_count;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = IP_INITIALIZING;
    owner->state = (ip_filter_state){.initialized = true, .ban_vm_present = true,
        .ban_modification_count = modification_count};
    memcpy(owner->state.ban_vm_string, input, size + 1);
    char *first_space = strchr(owner->state.ban_vm_string, ' ');
    if (first_space) *first_space = 0;
    bool okay = true;
    char *start = input;
    while (okay && *start) {
        char *space = strchr(start, ' ');
        if (!space) break;
        *space = 0;
        if (*start) okay = add_ip(owner, NULL, start, error);
        start = space + 1;
        while (*start == ' ') ++start;
    }
    owner->operation = IP_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_ipfilters_filter(application_provider *provider, const char *address,
    bool *reject, qa_error *error)
{
    struct application_native_q3_ipfilters *owner = owner_at(provider);
    if (!owner || !owner->state.initialized || !address || !reject || !admitted(owner, NULL, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 admission requires its initialized IP filters");
    size_t size = strlen(address);
    if (size >= IP_ARGUMENT_CAPACITY)
        return application_fail(error, QA_ERROR_ARGUMENT, "IP address exceeds the source userinfo bound");
    const unsigned char *cursor = (const unsigned char *)address;
    uint32_t incoming = 0;
    unsigned initialized = 0;
    while (*cursor && initialized < 4) {
        uint32_t octet = 0;
        while (digit(*cursor)) octet = (octet * 10u + (uint32_t)(*cursor++ - '0')) & 255u;
        incoming |= octet << (initialized++ * 8);
        if (!*cursor || *cursor == ':') break;
        ++cursor;
    }
    int32_t filter_ban;
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_FILTER_BAN, &filter_ban, error)) return false;
    uint32_t initialized_mask = initialized == 4 ? UINT32_MAX : (1u << (initialized * 8)) - 1u;
    bool deny_matches = filter_ban != 0;
    for (size_t row = 0; row < owner->state.count; ++row) {
        const ip_filter filter = owner->state.filters[row];
        if ((incoming & filter.mask & initialized_mask) != (filter.compare & initialized_mask)) continue;
        if (filter.mask & ~initialized_mask)
            return application_fail(error, QA_ERROR_ARGUMENT, "IP filter reads uninitialized source octets");
        if ((incoming & filter.mask) == filter.compare) {
            *reject = deny_matches;
            return true;
        }
    }
    *reject = !deny_matches;
    return true;
}

static bool named(const char *text, const char *name)
{
    while (*text && *name) {
        unsigned char byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        if (byte != (unsigned char)*name++) return false;
    }
    return !*text && !*name;
}

static bool remove_ip(struct application_native_q3_ipfilters *owner,
    const qa_command_context *context, const char *text, qa_error *error)
{
    ip_filter parsed;
    bool valid;
    if (!string_to_filter(owner, context, text, &parsed, &valid, error)) return false;
    if (!valid) return true;
    for (size_t row = 0; row < owner->state.count; ++row) {
        ip_filter *filter = &owner->state.filters[row];
        if (filter->mask != parsed.mask || filter->compare != parsed.compare) continue;
        filter->compare = UINT32_MAX;
        return emit(owner, context, "Removed.\n", error) && update_bans(owner, context, error);
    }
    char message[IP_ARGUMENT_CAPACITY + 32];
    snprintf(message, sizeof(message), "Didn't find %s.\n", text);
    return emit(owner, context, message, error);
}

bool application_native_q3_ipfilters_console(application_provider *provider,
    const qa_command_invocation *invocation, bool *handled, qa_error *error)
{
    if (!invocation || !handled || (invocation->argc && (!invocation->argv || !invocation->argv[0])))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP console requires source arguments");
    *handled = false;
    if (!invocation->argc) return true;
    bool add = named(invocation->argv[0], "addip"), remove = named(invocation->argv[0], "removeip");
    bool list = named(invocation->argv[0], "listip");
    if (!add && !remove && !list) return true;
    struct application_native_q3_ipfilters *owner = owner_at(provider);
    if (!owner || !owner->state.initialized || owner->operation != IP_IDLE ||
        invocation->context.dialect != QA_CONSOLE_Q3 ||
        (invocation->context.owner && invocation->context.owner != provider->owner) ||
        invocation->context.actor.registry || invocation->context.actor.generation ||
        invocation->context.actor.slot ||
        !admitted(owner, &invocation->context, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP command has no admitted source console");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = IP_CONSOLE;
    *handled = true;
    bool okay;
    if (list) {
        qa_console *console;
        if (!application_native_q3_console_at(provider, &console, NULL, NULL))
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 IP filter console is absent");
        else okay = application_unified_q3_console(provider, true, "g_banIPs\n", error) &&
            qa_console_execute_now(console, &invocation->context, "g_banIPs\n", error) &&
            admitted(owner, &invocation->context, error);
    } else if (invocation->argc < 2) {
        okay = emit(owner, &invocation->context,
            add ? "Usage:  addip <ip-mask>\n" : "Usage:  sv removeip <ip-mask>\n", error);
    } else if (!invocation->argv[1]) {
        okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP command argument is absent");
    } else {
        char text[IP_ARGUMENT_CAPACITY];
        size_t size = strlen(invocation->argv[1]);
        if (size >= sizeof(text)) size = sizeof(text) - 1;
        memcpy(text, invocation->argv[1], size);
        text[size] = 0;
        okay = add ? add_ip(owner, &invocation->context, text, error) :
            remove_ip(owner, &invocation->context, text, error);
    }
    owner->operation = IP_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

static bool state_fields(qa_source_save_io *io, ip_filter_state *state)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bool(io, &state->initialized) ||
        !qa_source_save_count(io, &state->count, IP_FILTER_CAPACITY)) return false;
    for (size_t row = 0; row < state->count; ++row)
        if (!qa_source_save_u32(io, &state->filters[row].mask) ||
            !qa_source_save_u32(io, &state->filters[row].compare)) return false;
    if (!qa_source_save_bool(io, &state->ban_vm_present)) return false;
    if (state->ban_vm_present) {
        size_t length = reading ? 0 : strlen(state->ban_vm_string);
        if (!qa_source_save_u64(io, &state->ban_modification_count) ||
            !qa_source_save_count(io, &length, IP_BAN_CAPACITY - 1) ||
            !qa_source_save_bytes(io, state->ban_vm_string, length) ||
            memchr(state->ban_vm_string, 0, length)) return false;
        if (reading) state->ban_vm_string[length] = 0;
    }
    if ((!state->initialized && (state->count || state->ban_vm_present)) ||
        (state->initialized && !state->ban_vm_present))
        return application_fail(io->error, QA_ERROR_FORMAT, "native Q3 IP filters have inconsistent source Init state");
    return true;
}

static bool header(qa_source_save_io *io, const application_provider *provider)
{
    uint8_t magic[4] = {'Q', 'A', 'G', 'I'};
    uint32_t product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    uint32_t expected_product = product;
    uint64_t owner = provider->owner;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAGI", 4) &&
        qa_source_save_u32(io, &product) && product == expected_product &&
        qa_source_save_u64(io, &owner) && owner == provider->owner;
}

bool application_native_q3_ipfilters_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    struct application_native_q3_ipfilters *owner = owner_at(provider);
    if (!owner || !out || owner->operation != IP_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP filters are not at a continuation boundary");
    owner->operation = IP_CAPTURING;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && header(&io, provider) &&
        state_fields(&io, &owner->state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    owner->operation = IP_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 IP filter continuation");
    return okay;
}

bool application_native_q3_ipfilters_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    struct application_native_q3_ipfilters *owner = owner_at(provider);
    if (!owner || owner->operation != IP_IDLE || owner->state.initialized ||
        owner->state.count || owner->state.ban_vm_present)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 IP filters require an empty candidate owner");
    owner->operation = IP_IMPORTING;
    qa_source_save_io io = {0};
    ip_filter_state candidate = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && header(&io, provider) &&
        state_fields(&io, &candidate) && qa_source_save_finish(&io, NULL);
    if (okay) owner->state = candidate;
    qa_source_save_dispose(&io);
    owner->operation = IP_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 IP filter continuation");
    return okay;
}

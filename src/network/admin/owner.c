#include "qa/server_admin.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q2.h"
#include "qa/tokenizer.h"
#include <stdlib.h>
#include <string.h>

typedef struct ip_filter { uint8_t mask[4], compare[4]; } ip_filter;
typedef struct rate_entry { qa_net_address address; double tokens; uint64_t time; bool active; } rate_entry;
struct qa_server_admin {
    qa_admin_options options;
    ip_filter *filters;
    size_t filter_count;
    rate_entry *rates;
    char **prefixes, **rotation;
    size_t prefix_count, rotation_count, rotation_index;
    qa_net_address *masters;
    size_t master_count;
    uint64_t heartbeat_time, rcon_time;
    uint32_t heartbeat_sequence;
    bool heartbeat_sent, rcon_sent, shuffle, callback;
};
static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false;
}
static void strings_free(char **strings, size_t count) {
    for (size_t i = 0; i < count; ++i) free(strings[i]);
    free(strings);
}
static char *copy(const char *text) {
    size_t n = strlen(text); char *out = malloc(n + 1); if (out) memcpy(out, text, n + 1); return out;
}
bool qa_server_admin_create(const qa_admin_options *options, qa_server_admin **out, qa_error *error) {
    if (!options || !out || !options->filters || options->filters > 4096 || !options->rate_entries ||
        options->rate_entries > 4096 || !options->burst || !options->rate_interval_ns ||
        !options->heartbeat_interval_ns || !options->hooks.password || !options->hooks.execute ||
        !options->hooks.send || !options->hooks.travel || !options->hooks.players || !options->hooks.random)
        return fail(error, "Invalid server administration options");
    qa_server_admin *admin = calloc(1, sizeof(*admin));
    if (!admin) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server administration"); return false; }
    admin->filters = calloc(options->filters, sizeof(*admin->filters));
    admin->rates = calloc(options->rate_entries, sizeof(*admin->rates));
    if (!admin->filters || !admin->rates) { qa_server_admin_destroy(admin); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server filters/limiter"); return false; }
    admin->options = *options; *out = admin; return true;
}
void qa_server_admin_destroy(qa_server_admin *admin) {
    if (!admin || admin->callback) return;
    free(admin->filters); free(admin->rates); free(admin->masters);
    strings_free(admin->prefixes, admin->prefix_count); strings_free(admin->rotation, admin->rotation_count); free(admin);
}
static bool filter_parse(const char *text, ip_filter *out) {
    *out = (ip_filter){0}; if (!text || !*text) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (*text < '0' || *text > '9') return false;
        uint8_t value = 0;
        while (*text >= '0' && *text <= '9') value = (uint8_t)(value * 10u + (unsigned)(*text++ - '0'));
        out->compare[i] = value; out->mask[i] = value ? 255 : 0;
        if (!*text) return true;
        if (*text++ != '.' || i == 3 || !*text) return false;
    }
    return !*text;
}
bool qa_server_admin_filter(qa_server_admin *admin, const char *text, bool remove, qa_error *error) {
    if (!admin || admin->callback) return fail(error, "Invalid server filter operation");
    ip_filter value; if (!filter_parse(text, &value)) return fail(error, "Invalid source IPv4 filter");
    for (size_t i = 0; i < admin->filter_count; ++i) if (!memcmp(&admin->filters[i], &value, sizeof(value))) {
        if (remove) {
            memmove(admin->filters + i, admin->filters + i + 1, (admin->filter_count - i - 1) * sizeof(value)); --admin->filter_count;
        }
        return true;
    }
    if (remove) return true;
    if (admin->filter_count == admin->options.filters) return fail(error, "Server filter capacity exhausted");
    admin->filters[admin->filter_count++] = value; return true;
}
bool qa_server_admin_rejects(const qa_server_admin *admin, const qa_net_address *address) {
    if (!admin || !address || address->kind != QA_NET_IPV4 || admin->options.dialect == QA_CONSOLE_Q3) return false;
    bool match = false;
    for (size_t i = 0; i < admin->filter_count; ++i) {
        bool equal = true;
        for (size_t byte = 0; byte < 4; ++byte)
            if ((address->host.ipv4[byte] & admin->filters[i].mask[byte]) != admin->filters[i].compare[byte]) { equal = false; break; }
        if (equal) { match = true; break; }
    }
    return admin->options.deny_matches ? match : !match;
}
static bool strings_copy(const char *const *strings, size_t count, size_t maximum,
                          char ***out, qa_error *error) {
    if ((count && !strings) || count > 256) return fail(error, "Invalid server string list");
    char **owned = count ? calloc(count, sizeof(*owned)) : NULL;
    if (count && !owned) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server list"); return false; }
    for (size_t i = 0; i < count; ++i) {
        if (!strings[i] || !*strings[i] || strlen(strings[i]) > maximum || !(owned[i] = copy(strings[i]))) {
            strings_free(owned, count); return fail(error, "Invalid or unretainable server list entry");
        }
    }
    *out = owned; return true;
}
bool qa_server_admin_limited_prefixes(qa_server_admin *admin, const char *const *prefixes, size_t count, qa_error *error) {
    if (!admin || admin->callback) return fail(error, "Invalid limited command update");
    char **owned; if (!strings_copy(prefixes, count, 1023, &owned, error)) return false;
    strings_free(admin->prefixes, admin->prefix_count); admin->prefixes = owned; admin->prefix_count = count; return true;
}
static bool limited(qa_server_admin *admin, const char *command) {
    /* Limited commands cannot smuggle a second command through separators. */
    for (const char *p = command; *p; ++p) if (*p == ';' || *p == '\n' || *p == '\r') return false;
    for (size_t i = 0; i < admin->prefix_count; ++i)
        if (!strncmp(command, admin->prefixes[i], strlen(admin->prefixes[i]))) return true;
    return false;
}
static bool equal_secret(const char *left, const char *right) {
    size_t a = strlen(left), b = strlen(right), n = a > b ? a : b; size_t different = a ^ b;
    for (size_t i = 0; i < n; ++i) different |= (size_t)((i < a ? (unsigned char)left[i] : 0) ^ (i < b ? (unsigned char)right[i] : 0));
    return different == 0;
}
static bool allow(qa_server_admin *admin, const qa_net_address *address, uint64_t now) {
    if (admin->options.dialect == QA_CONSOLE_Q3) {
        uint32_t milliseconds = (uint32_t)(now / 1000000), previous = (uint32_t)(admin->rcon_time / 1000000);
        if (milliseconds < previous + 500u) return false;
        admin->rcon_time = now; admin->rcon_sent = true; return true;
    }
    rate_entry *entry = NULL, *oldest = &admin->rates[0];
    for (uint32_t i = 0; i < admin->options.rate_entries; ++i) {
        rate_entry *candidate = &admin->rates[i];
        if (candidate->active && qa_net_address_equal(&candidate->address, address, false)) { entry = candidate; break; }
        if (!candidate->active && !entry) entry = candidate;
        if (candidate->time < oldest->time) oldest = candidate;
    }
    if (!entry) entry = oldest;
    if (!entry->active || !qa_net_address_equal(&entry->address, address, false))
        *entry = (rate_entry){.address = *address, .time = now, .tokens = admin->options.burst, .active = true};
    if (now >= entry->time) {
        entry->tokens += (double)(now - entry->time) / (double)admin->options.rate_interval_ns;
        if (entry->tokens > admin->options.burst) entry->tokens = admin->options.burst;
        entry->time = now;
    }
    if (entry->tokens < 1) return false;
    entry->tokens -= 1; return true;
}
typedef struct rcon_output { qa_server_admin *admin; qa_net_address address; char text[1009]; size_t size; } rcon_output;
static bool flush(rcon_output *output, qa_error *error) {
    if (!output->size) return true;
    uint8_t bytes[1030]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    bool ok;
    if (output->admin->options.dialect == QA_CONSOLE_QW) {
        qa_net_write_u32(&writer, UINT32_MAX); qa_net_write_u8(&writer, 'n');
        ok = qa_net_write_string(&writer, output->text);
    } else {
        char message[1020]; memcpy(message, "print\n", 6); memcpy(message + 6, output->text, output->size + 1);
        ok = qa_q2_oob_write(&writer, message);
    }
    if (ok) ok = output->admin->options.hooks.send(output->admin->options.hooks.context,
        &output->address, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    if (ok) output->size = 0; return ok;
}
static bool write_output(void *context, const char *text, qa_error *error) {
    rcon_output *output = context; if (!text) return fail(error, "Missing administration output");
    for (; *text; ++text) {
        if (output->size == sizeof(output->text) - 1 && !flush(output, error)) return false;
        output->text[output->size++] = *text; output->text[output->size] = 0;
    }
    return true;
}
bool qa_server_admin_receive(qa_server_admin *admin, const qa_net_datagram *packet, qa_admin_result *out, qa_error *error) {
    if (!admin || admin->callback || !packet || !out) return fail(error, "Invalid remote administration delivery");
    *out = QA_ADMIN_IGNORED;
    if (packet->kind != QA_NET_POLL_PACKET || packet->payload.size < 5 ||
        qa_load_u32le(packet->payload.data) != UINT32_MAX || packet->payload.size > 16384) return true;
    qa_bytes text = {packet->payload.data + 4, packet->payload.size - 4};
    if (text.size && !text.data[text.size - 1]) --text.size;
    if (memchr(text.data, 0, text.size)) return true;
    qa_tokenizer lexer;
    qa_tokenizer_options options = {.maximum_units = 8192, .reject_quoted_newlines = true};
    if (!qa_tokenizer_init_options(&lexer, text, &options, error)) return false;
    qa_token token; bool found;
    if (!qa_tokenizer_next(&lexer, &token, &found, error)) return false;
    if (!found || token.text.size != 4 || memcmp(token.text.data, "rcon", 4)) return true;
    if (!allow(admin, &packet->from, packet->received_ns)) { *out = QA_ADMIN_THROTTLED; return true; }
    char supplied[8193] = {0};
    if (!qa_tokenizer_next(&lexer, &token, &found, error)) return false;
    if (found && !qa_token_copy(&token, supplied, sizeof(supplied), error)) return false;
    size_t offset = lexer.offset;
    while (offset < text.size && (text.data[offset] == ' ' || text.data[offset] == '\t')) ++offset;
    size_t length = text.size - offset;
    if (admin->options.dialect == QA_CONSOLE_Q3 && length > 1023) length = 1023;
    char command[16385]; memcpy(command, text.data + offset, length); command[length] = 0;
    admin->callback = true;
    const char *full = admin->options.hooks.password(admin->options.hooks.context, false);
    bool is_full = full && *full && equal_secret(full, supplied);
    bool disabled = !full || !*full;
    const char *small = admin->options.hooks.password(admin->options.hooks.context, true);
    bool is_limited = !is_full && small && *small && equal_secret(small, supplied) && limited(admin, command);
    memset(supplied, 0, sizeof(supplied));
    rcon_output output = {.admin = admin, .address = packet->from};
    bool ok;
    if (!is_full && !is_limited) {
        *out = disabled ? QA_ADMIN_DISABLED : QA_ADMIN_DENIED;
        ok = write_output(&output, disabled ? "No rconpassword set on the server.\n" : "Bad rconpassword or command not permitted.\n", error);
    } else {
        *out = QA_ADMIN_EXECUTED;
        ok = admin->options.hooks.execute(admin->options.hooks.context, &packet->from, command,
            is_limited, write_output, &output, error);
    }
    if (ok) ok = flush(&output, error);
    if (admin->options.hooks.record) admin->options.hooks.record(admin->options.hooks.context, &packet->from, *out);
    admin->callback = false; return ok;
}
bool qa_server_admin_masters(qa_server_admin *admin, const qa_net_address *addresses, size_t count, qa_error *error) {
    if (!admin || admin->callback || count > 32 || (count && !addresses)) return fail(error, "Invalid server masters");
    qa_net_address *owned = count ? malloc(count * sizeof(*owned)) : NULL;
    if (count && !owned) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining server masters"); return false; }
    if (count) memcpy(owned, addresses, count * sizeof(*owned));
    free(admin->masters); admin->masters = owned; admin->master_count = count; admin->heartbeat_sent = false; return true;
}
static bool heartbeat(qa_server_admin *admin, bool shutdown, qa_error *error) {
    uint8_t bytes[128]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    bool ok;
    if (admin->options.dialect == QA_CONSOLE_QW)
        ok = shutdown ? qa_qw_shutdown(&writer) : qa_qw_heartbeat(++admin->heartbeat_sequence,
            admin->options.hooks.players(admin->options.hooks.context), &writer);
    else if (admin->options.dialect == QA_CONSOLE_Q3)
        ok = qa_q2_oob_write(&writer, shutdown ? "heartbeat flatline\n" : "heartbeat QuakeArena-1\n");
    else ok = qa_q2_oob_write(&writer, shutdown ? "shutdown\n" : "heartbeat\n");
    if (!ok) return false;
    for (size_t i = 0; i < admin->master_count; ++i)
        if (!admin->options.hooks.send(admin->options.hooks.context, &admin->masters[i],
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    return true;
}
bool qa_server_admin_tick(qa_server_admin *admin, uint64_t now, bool force, qa_error *error) {
    if (!admin || admin->callback) return fail(error, "Invalid administration heartbeat");
    if (!admin->options.public_server || !admin->master_count) return true;
    if (!force && admin->heartbeat_sent && (now < admin->heartbeat_time || now - admin->heartbeat_time < admin->options.heartbeat_interval_ns)) return true;
    admin->callback = true; bool ok = heartbeat(admin, false, error); admin->callback = false;
    if (ok) { admin->heartbeat_sent = true; admin->heartbeat_time = now; } return ok;
}
bool qa_server_admin_shutdown(qa_server_admin *admin, qa_error *error) {
    if (!admin || admin->callback) return fail(error, "Invalid server shutdown");
    if (!admin->options.public_server) return true;
    admin->callback = true; bool ok = heartbeat(admin, true, error); admin->callback = false; return ok;
}
static bool map_name(const char *text) {
    if (!text || !*text || strlen(text) > 127) return false;
    bool segment = false;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '/') { if (!segment) return false; segment = false; }
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') segment = true;
        else return false;
    }
    return segment;
}
bool qa_server_admin_rotation(qa_server_admin *admin, const char *const *maps, size_t count, bool shuffle, qa_error *error) {
    if (!admin || admin->callback || (count && !maps)) return fail(error, "Invalid map rotation");
    for (size_t i = 0; i < count; ++i) if (!map_name(maps[i])) return fail(error, "Invalid rotation map name");
    char **owned; if (!strings_copy(maps, count, 127, &owned, error)) return false;
    strings_free(admin->rotation, admin->rotation_count); admin->rotation = owned;
    admin->rotation_count = count; admin->rotation_index = 0; admin->shuffle = shuffle; return true;
}
bool qa_server_admin_next_map(qa_server_admin *admin, const char *current, bool *rotated, qa_error *error) {
    if (!admin || admin->callback || !rotated) return fail(error, "Invalid rotation transition");
    *rotated = false; if (!admin->rotation_count) return true;
    size_t next = admin->rotation_index;
    if (current) for (size_t i = 0; i < admin->rotation_count; ++i)
        if (!strcmp(admin->rotation[i], current)) { next = (i + 1) % admin->rotation_count; break; }
    admin->callback = true;
    bool ok = admin->options.hooks.travel(admin->options.hooks.context, admin->rotation[next], error);
    if (ok) {
        admin->rotation_index = (next + 1) % admin->rotation_count; *rotated = true;
        if (!admin->rotation_index && admin->shuffle) {
            for (size_t i = admin->rotation_count - 1; i > 0; --i) {
                size_t other = admin->options.hooks.random(admin->options.hooks.context) % (i + 1);
                char *temporary = admin->rotation[i]; admin->rotation[i] = admin->rotation[other]; admin->rotation[other] = temporary;
            }
        }
    }
    admin->callback = false; return ok;
}
bool qa_server_admin_save_filters(const qa_server_admin *admin, qa_buffer *out, qa_error *error) {
    if (!admin || !out) return fail(error, "Missing server filter output");
    size_t size = 13 + admin->filter_count * 8;
    uint8_t *data = malloc(size); if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding server filters"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer, data, size, error);
    qa_net_write_data(&writer, "QAIP", 4); qa_net_write_u32(&writer, 1);
    qa_net_write_u8(&writer, admin->options.deny_matches ? 1 : 0); qa_net_write_u32(&writer, (uint32_t)admin->filter_count);
    for (size_t i = 0; i < admin->filter_count; ++i) {
        qa_net_write_data(&writer, admin->filters[i].mask, 4); qa_net_write_data(&writer, admin->filters[i].compare, 4);
    }
    if (writer.failed) { free(data); return false; } *out = (qa_buffer){data, size}; return true;
}
bool qa_server_admin_restore_filters(qa_server_admin *admin, qa_bytes bytes, qa_error *error) {
    if (!admin || admin->callback || bytes.size < 13 || !bytes.data || memcmp(bytes.data, "QAIP", 4)) return fail(error, "Invalid saved server filters");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    if (qa_net_read_u32(&reader) != 1) return fail(error, "Unsupported server filter version");
    uint8_t deny = qa_net_read_u8(&reader); uint32_t count = qa_net_read_u32(&reader);
    if (deny > 1 || count > admin->options.filters || bytes.size != 13 + (size_t)count * 8) return fail(error, "Invalid server filter extent");
    ip_filter *candidate = calloc(admin->options.filters, sizeof(*candidate));
    if (!candidate) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring server filters"); return false; }
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        ok = qa_net_read_data(&reader, candidate[i].mask, 4) && qa_net_read_data(&reader, candidate[i].compare, 4);
        for (size_t n = 0; ok && n < 4; ++n) if ((candidate[i].mask[n] != 0 && candidate[i].mask[n] != 255) ||
            (candidate[i].compare[n] & candidate[i].mask[n]) != candidate[i].compare[n]) ok = false;
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (!ok) { free(candidate); return fail(error, "Invalid stored source filter"); }
    free(admin->filters); admin->filters = candidate; admin->filter_count = count; admin->options.deny_matches = deny != 0; return true;
}

#include "qa/network_q1_channel.h"
#include "qa/network_q1_connection_save.h"
#include "../q3/save_fields.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message); return false;
}
static char *copy_text(const char *text, qa_error *error) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) { fail(error, QA_ERROR_MEMORY, "Cannot allocate Quake connection text"); return NULL; }
    memcpy(copy, text, size); return copy;
}
static uint32_t read_be32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}
static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24); bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8); bytes[3] = (uint8_t)value;
}

bool qa_nq_control_decode(qa_bytes bytes, qa_nq_control *out, qa_error *error) {
    if (!out || !bytes.data || bytes.size < 5 || bytes.size > UINT16_MAX)
        return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake control size");
    uint32_t header = read_be32(bytes.data);
    if ((header & UINT32_C(65535)) != bytes.size || header >> 16 != 0x8000)
        return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake control header");
    qa_net_reader reader; qa_net_reader_init(&reader, (qa_bytes){bytes.data + 4, bytes.size - 4}, error);
    qa_nq_control message = {0};
    message.kind = (qa_nq_control_kind)qa_net_read_u8(&reader);
    switch (message.kind) {
    case QA_NQ_CONNECT_REQUEST: case QA_NQ_SERVER_INFO_REQUEST:
        qa_q1_read_cstring(&reader, &message.data.request.game);
        message.data.request.version = qa_net_read_u8(&reader); break;
    case QA_NQ_PLAYER_INFO_REQUEST: message.data.player = qa_net_read_u8(&reader); break;
    case QA_NQ_RULE_INFO_REQUEST: qa_q1_read_cstring(&reader, &message.data.previous_rule); break;
    case QA_NQ_ACCEPT: message.data.port = qa_net_read_i32(&reader); break;
    case QA_NQ_REJECT: qa_q1_read_cstring(&reader, &message.data.reason); break;
    case QA_NQ_SERVER_INFO:
        qa_q1_read_cstring(&reader, &message.data.server.address);
        qa_q1_read_cstring(&reader, &message.data.server.name);
        qa_q1_read_cstring(&reader, &message.data.server.map);
        message.data.server.players = qa_net_read_u8(&reader);
        message.data.server.max_players = qa_net_read_u8(&reader);
        message.data.server.version = qa_net_read_u8(&reader); break;
    case QA_NQ_PLAYER_INFO:
        message.data.player_info.player = qa_net_read_u8(&reader);
        qa_q1_read_cstring(&reader, &message.data.player_info.name);
        message.data.player_info.colors = qa_net_read_i32(&reader);
        message.data.player_info.frags = qa_net_read_i32(&reader);
        message.data.player_info.seconds = qa_net_read_i32(&reader);
        qa_q1_read_cstring(&reader, &message.data.player_info.address); break;
    case QA_NQ_RULE_INFO:
        message.data.rule_info.present = qa_net_reader_remaining(&reader) != 0;
        if (message.data.rule_info.present) {
            qa_q1_read_cstring(&reader, &message.data.rule_info.rule.name);
            qa_q1_read_cstring(&reader, &message.data.rule_info.rule.value);
        }
        break;
    default: return fail(error, QA_ERROR_FORMAT, "Unknown NetQuake control command");
    }
    if (!qa_net_reader_finish(&reader)) return false;
    *out = message; return true;
}

bool qa_nq_control_encode(const qa_nq_control *message, qa_net_writer *writer) {
    if (!message || !writer || writer->bit % 8) return qa_net_writer_fail(writer, "Invalid NetQuake control output");
    size_t start = qa_net_writer_size(writer);
    qa_net_write_u32(writer, 0); qa_net_write_u8(writer, (uint8_t)message->kind);
    switch (message->kind) {
    case QA_NQ_CONNECT_REQUEST: case QA_NQ_SERVER_INFO_REQUEST:
        qa_net_write_string(writer, message->data.request.game);
        qa_net_write_u8(writer, message->data.request.version); break;
    case QA_NQ_PLAYER_INFO_REQUEST: qa_net_write_u8(writer, message->data.player); break;
    case QA_NQ_RULE_INFO_REQUEST: qa_net_write_string(writer, message->data.previous_rule); break;
    case QA_NQ_ACCEPT: qa_net_write_i32(writer, message->data.port); break;
    case QA_NQ_REJECT: qa_net_write_string(writer, message->data.reason); break;
    case QA_NQ_SERVER_INFO:
        qa_net_write_string(writer, message->data.server.address);
        qa_net_write_string(writer, message->data.server.name);
        qa_net_write_string(writer, message->data.server.map);
        qa_net_write_u8(writer, message->data.server.players);
        qa_net_write_u8(writer, message->data.server.max_players);
        qa_net_write_u8(writer, message->data.server.version); break;
    case QA_NQ_PLAYER_INFO:
        qa_net_write_u8(writer, message->data.player_info.player);
        qa_net_write_string(writer, message->data.player_info.name);
        qa_net_write_i32(writer, message->data.player_info.colors);
        qa_net_write_i32(writer, message->data.player_info.frags);
        qa_net_write_i32(writer, message->data.player_info.seconds);
        qa_net_write_string(writer, message->data.player_info.address); break;
    case QA_NQ_RULE_INFO:
        if (message->data.rule_info.present) {
            qa_net_write_string(writer, message->data.rule_info.rule.name);
            qa_net_write_string(writer, message->data.rule_info.rule.value);
        }
        break;
    default: return qa_net_writer_fail(writer, "Unknown NetQuake control command");
    }
    if (writer->failed) return false;
    size_t size = qa_net_writer_size(writer) - start;
    if (size > UINT16_MAX) return qa_net_writer_fail(writer, "NetQuake control exceeds 16-bit length");
    write_be32(writer->data + start, QA_NQ_FLAG_CONTROL | (uint32_t)size);
    return true;
}

bool qa_nq_control_answer(qa_bytes bytes, const qa_net_address *from, uint64_t now_ns,
                           const qa_nq_connection_host *host, bool *present,
                           qa_net_writer *writer, qa_error *error) {
    if (!from || !host || !present || !writer) return fail(error, QA_ERROR_ARGUMENT, "Invalid NetQuake connection host");
    *present = false;
    qa_nq_control request, response = {0};
    if (!qa_nq_control_decode(bytes, &request, error)) return false;
    switch (request.kind) {
    case QA_NQ_SERVER_INFO_REQUEST:
        if (strcmp(request.data.request.game, "QUAKE")) return true;
        if (!host->server_info) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake server info provider");
        if (!host->server_info(host->context, &response, error)) return false;
        if (response.kind != QA_NQ_SERVER_INFO) return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake server info result");
        break;
    case QA_NQ_PLAYER_INFO_REQUEST: {
        bool found;
        if (!host->player_info) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake player info provider");
        if (!host->player_info(host->context, request.data.player, &found, &response, error)) return false;
        if (!found) return true;
        if (response.kind != QA_NQ_PLAYER_INFO) return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake player info result");
        break;
    }
    case QA_NQ_RULE_INFO_REQUEST:
        if (!host->next_rule) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake rule provider");
        response.kind = QA_NQ_RULE_INFO;
        if (!host->next_rule(host->context, request.data.previous_rule, &response.data.rule_info.present,
                             &response.data.rule_info.rule, error)) return false;
        break;
    case QA_NQ_CONNECT_REQUEST: {
        if (strcmp(request.data.request.game, "QUAKE")) return true;
        if (request.data.request.version != 3) {
            response.kind = QA_NQ_REJECT; response.data.reason = "Incompatible version.\n"; break;
        }
        if (!host->connect) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake connect provider");
        qa_q1_connect_result result = {0};
        if (!host->connect(host->context, from, now_ns, &result, error)) return false;
        if (result.decision == QA_Q1_CONNECT_IGNORE) return true;
        if (result.decision == QA_Q1_CONNECT_ACCEPT) {
            if (!result.port) return fail(error, QA_ERROR_ARGUMENT, "NetQuake accepted port is zero");
            response.kind = QA_NQ_ACCEPT; response.data.port = result.port;
        } else if (result.decision == QA_Q1_CONNECT_REJECT) {
            response.kind = QA_NQ_REJECT; response.data.reason = result.reason;
        } else return fail(error, QA_ERROR_ARGUMENT, "Invalid NetQuake connection decision");
        break;
    }
    default: return true;
    }
    if (!qa_nq_control_encode(&response, writer)) return false;
    *present = true; return true;
}

bool qa_qw_oob_encode(const char *text, bool nul, qa_net_writer *writer) {
    if (!text || !writer || writer->bit % 8) return qa_net_writer_fail(writer, "Invalid QuakeWorld connectionless text");
    size_t size = strlen(text);
    if (size > 65531U - (nul ? 1U : 0U)) return qa_net_writer_fail(writer, "QuakeWorld connectionless text exceeds capacity");
    return qa_net_write_u32(writer, UINT32_MAX) && qa_net_write_data(writer, text, size) &&
           (!nul || qa_net_write_u8(writer, 0));
}
bool qa_qw_oob_decode(qa_bytes bytes, qa_bytes *text, qa_error *error) {
    if (!text || !bytes.data || bytes.size < 4 || bytes.size > UINT16_MAX || read_be32(bytes.data) != UINT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Not a QuakeWorld connectionless packet");
    const uint8_t *end = memchr(bytes.data + 4, 0, bytes.size - 4);
    *text = (qa_bytes){bytes.data + 4, end ? (size_t)(end - bytes.data - 4) : bytes.size - 4};
    return true;
}

void qa_qw_info_free(qa_qw_info *info) {
    if (!info) return;
    free(info->rules); free(info->storage); *info = (qa_qw_info){0};
}
const char *qa_qw_info_get(const qa_qw_info *info, const char *key) {
    if (!info || !key) return NULL;
    for (size_t i = 0; i < info->count; ++i) if (!strcmp(info->rules[i].name, key)) return info->rules[i].value;
    return NULL;
}
bool qa_qw_info_parse(const char *text, qa_qw_info *out, qa_error *error) {
    if (!text || !out) return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld info string");
    size_t length = strlen(text);
    if (length > UINT16_MAX) return fail(error, QA_ERROR_FORMAT, "QuakeWorld info string exceeds capacity");
    qa_qw_info info = {0};
    info.storage = copy_text(text, error);
    if (!info.storage) return false;
    info.rules = calloc(length / 2 + 2, sizeof(*info.rules));
    if (!info.rules) { qa_qw_info_free(&info); return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld info fields"); }
    char *key = info.storage;
    if (*key == '\\') ++key;
    while (*key) {
        char *value = strchr(key, '\\');
        if (!value) break;
        *value++ = 0;
        char *next = strchr(value, '\\');
        if (next) *next++ = 0;
        size_t index = 0;
        while (index < info.count && strcmp(info.rules[index].name, key)) ++index;
        if (index == info.count) ++info.count;
        info.rules[index] = (qa_qw_rule){key, value};
        if (!next) break;
        key = next;
    }
    *out = info; return true;
}

typedef struct qw_challenge_record { qa_net_address address; uint32_t challenge; uint64_t issued_seconds; } qw_challenge_record;
struct qa_qw_challenges {
    qw_challenge_record *records;
    size_t count, capacity;
    qa_qw_random_fn random;
    void *context;
};
bool qa_qw_challenges_create(size_t capacity, qa_qw_random_fn random, void *context,
                              qa_qw_challenges **out, qa_error *error) {
    if (!capacity || !random || !out || capacity > SIZE_MAX / sizeof(qw_challenge_record))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld challenge table options");
    qa_qw_challenges *challenges = calloc(1, sizeof(*challenges));
    if (!challenges) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld challenges");
    challenges->records = calloc(capacity, sizeof(*challenges->records));
    if (!challenges->records) { free(challenges); return fail(error, QA_ERROR_MEMORY, "Cannot allocate challenge records"); }
    challenges->capacity = capacity; challenges->random = random; challenges->context = context;
    *out = challenges; return true;
}
void qa_qw_challenges_destroy(qa_qw_challenges *challenges) {
    if (challenges) { free(challenges->records); free(challenges); }
}
bool qa_qw_challenge_issue(qa_qw_challenges *challenges, const qa_net_address *address,
                            uint64_t now_ns, uint32_t *out, qa_error *error) {
    if (!challenges || !address || !out || !qa_net_address_equal(address, address, false))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld challenge address");
    for (size_t i = 0; i < challenges->count; ++i)
        if (qa_net_address_equal(address, &challenges->records[i].address, false)) {
            *out = challenges->records[i].challenge; return true;
        }
    if (challenges->count == challenges->capacity) {
        size_t oldest = 0;
        for (size_t i = 1; i < challenges->count; ++i)
            if (challenges->records[i].issued_seconds < challenges->records[oldest].issued_seconds) oldest = i;
        memmove(challenges->records + oldest, challenges->records + oldest + 1,
                (challenges->count - oldest - 1) * sizeof(*challenges->records));
        --challenges->count;
    }
    uint32_t challenge = (challenges->random(challenges->context) & 32767U) << 16;
    challenge ^= challenges->random(challenges->context) & 32767U;
    challenges->records[challenges->count++] = (qw_challenge_record){*address, challenge, now_ns / UINT64_C(1000000000)};
    *out = challenge; return true;
}
bool qa_qw_challenge_validate(const qa_qw_challenges *challenges, const qa_net_address *address, uint32_t challenge) {
    if (!challenges || !address) return false;
    for (size_t i = 0; i < challenges->count; ++i)
        if (qa_net_address_equal(address, &challenges->records[i].address, false)) return challenges->records[i].challenge == challenge;
    return false;
}

typedef struct qw_arguments { char **values; char *storage; size_t count; } qw_arguments;
static void arguments_free(qw_arguments *arguments) {
    free(arguments->values); free(arguments->storage); *arguments = (qw_arguments){0};
}
static bool arguments_parse(const char *text, qw_arguments *out, qa_error *error) {
    size_t length = strlen(text), capacity = length / 2 + 1;
    qw_arguments arguments = {0};
    arguments.storage = malloc(length + 2);
    arguments.values = calloc(capacity, sizeof(*arguments.values));
    if (!arguments.storage || !arguments.values) {
        arguments_free(&arguments); return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld command arguments");
    }
    const char *cursor = text;
    size_t used = 0;
    for (;;) {
        bool present;
        if (!qa_q1_token(&cursor, true, arguments.storage + used, length + 2 - used, &present, error)) {
            arguments_free(&arguments); return false;
        }
        if (!present) break;
        if (arguments.count == capacity) {
            arguments_free(&arguments); return fail(error, QA_ERROR_FORMAT, "Too many QuakeWorld arguments");
        }
        arguments.values[arguments.count++] = arguments.storage + used;
        used += strlen(arguments.storage + used) + 1;
    }
    *out = arguments; return true;
}
static const char *argument(const qw_arguments *arguments, size_t index) {
    return index < arguments->count ? arguments->values[index] : "";
}
/* Match parseInt's decimal prefix, with a bounded C integer result. */
static bool decimal_prefix(const char *text, int32_t *out) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    if (*text < '0' || *text > '9') return false;
    uint32_t magnitude = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (magnitude > limit / 10 || (magnitude == limit / 10 && digit > limit % 10)) return false;
        magnitude = magnitude * 10 + digit;
    }
    *out = negative ? (magnitude == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)magnitude) : (int32_t)magnitude;
    return true;
}
static bool reply_text(qa_qw_reply_fn reply, void *context, const char *prefix,
                        const char *text, qa_error *error) {
    if (!text) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld reply text");
    size_t prefix_size = strlen(prefix), size = strlen(text);
    if (prefix_size > 65531 || size > 65531 - prefix_size)
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld reply exceeds packet capacity");
    size_t bytes = 4 + prefix_size + size;
    uint8_t *buffer = malloc(bytes);
    if (!buffer) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld reply");
    memset(buffer, 255, 4);
    memcpy(buffer + 4, prefix, prefix_size); memcpy(buffer + 4 + prefix_size, text, size);
    bool result = reply(context, (qa_bytes){buffer, bytes}, error);
    free(buffer); return result;
}
static void info_delete(qa_qw_info *info, const char *name) {
    for (size_t i = 0; i < info->count; ++i) {
        if (strcmp(info->rules[i].name, name)) continue;
        memmove(info->rules + i, info->rules + i + 1, (info->count - i - 1) * sizeof(*info->rules));
        --info->count; return;
    }
}
static void info_spectator(qa_qw_info *info) {
    for (size_t i = 0; i < info->count; ++i) {
        if (strcmp(info->rules[i].name, "*spectator")) continue;
        info->rules[i].value = "1"; return;
    }
    /* parse allocates space for this one additional field. */
    info->rules[info->count++] = (qa_qw_rule){"*spectator", "1"};
}
static bool no_password(const char *password) {
    if (!*password) return true;
    return (password[0] == 'n' || password[0] == 'N') &&
           (password[1] == 'o' || password[1] == 'O') &&
           (password[2] == 'n' || password[2] == 'N') &&
           (password[3] == 'e' || password[3] == 'E') && password[4] == 0;
}
static void userinfo_filtered(const qa_qw_info *info, bool high, char out[196]) {
    size_t used = 0;
    for (size_t i = 0; i < info->count && used < 195; ++i) {
        const char *pieces[] = {"\\", info->rules[i].name, "\\", info->rules[i].value};
        for (size_t j = 0; j < 4; ++j) {
            const unsigned char *p = (const unsigned char *)pieces[j];
            while (*p && used < 195) {
                unsigned char byte = *p++;
                if (high || (byte > 31 && byte <= 127)) out[used++] = (char)byte;
            }
        }
    }
    out[used] = 0;
}
typedef struct qw_admin_output {
    qa_qw_reply_fn reply;
    void *context;
    uint8_t buffer[8000];
    size_t used;
    bool failed;
} qw_admin_output;
static bool admin_flush(qw_admin_output *output, qa_error *error) {
    if (!output->used) return !output->failed;
    if (!output->reply(output->context, (qa_bytes){output->buffer, output->used + 5}, error)) {
        output->failed = true; return false;
    }
    output->used = 0; return true;
}
static bool admin_write(void *opaque, const char *text, qa_error *error) {
    qw_admin_output *output = opaque;
    if (output->failed) return false;
    if (!text) { output->failed = true; return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld admin output"); }
    while (*text) {
        output->buffer[5 + output->used++] = (uint8_t)*text++;
        if (output->used == 7995 && !admin_flush(output, error)) return false;
    }
    return true;
}

static bool qw_command(const qa_qw_connection_host *host, qa_qw_challenges *challenges,
                         const qw_arguments *args, bool ping_byte, const qa_net_address *from,
                         uint64_t now_ns, qa_qw_reply_fn reply, void *reply_context, qa_error *error) {
    const char *command = argument(args, 0);
    if (!strcmp(command, "ping") || ping_byte) return reply_text(reply, reply_context, "", "l", error);
    if (!strcmp(command, "getchallenge")) {
        uint32_t challenge; char text[32];
        if (!qa_qw_challenge_issue(challenges, from, now_ns, &challenge, error)) return false;
        (void)snprintf(text, sizeof(text), "c%" PRIu32, challenge);
        return reply_text(reply, reply_context, "", text, error);
    }
    if (!strcmp(command, "status")) {
        const char *text = NULL;
        if (!host->status) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld status provider");
        return host->status(host->context, &text, error) && reply_text(reply, reply_context, "n", text, error);
    }
    if (!strcmp(command, "log")) {
        int32_t sequence = -1; const char *text = NULL;
        if (args->count > 1 && !decimal_prefix(argument(args, 1), &sequence))
            return fail(error, QA_ERROR_FORMAT, "Invalid QuakeWorld log sequence");
        if (!host->log) return reply_text(reply, reply_context, "", "m", error);
        return host->log(host->context, sequence, &text, error) &&
               reply_text(reply, reply_context, "", text ? text : "m", error);
    }
    if (!strcmp(command, "rcon")) {
        if (!*host->rcon_password || strcmp(argument(args, 1), host->rcon_password))
            return reply_text(reply, reply_context, "", "nBad rcon_password.\n", error);
        if (!host->execute_admin) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld admin provider");
        size_t length = 0;
        for (size_t i = 2; i < args->count; ++i) length += strlen(args->values[i]) + 1;
        char *admin_command = malloc(length + 1);
        if (!admin_command) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld admin command");
        size_t offset = 0;
        for (size_t i = 2; i < args->count; ++i) {
            size_t part = strlen(args->values[i]);
            memcpy(admin_command + offset, args->values[i], part); offset += part;
            admin_command[offset++] = ' ';
        }
        admin_command[offset] = 0;
        qw_admin_output output = {.reply = reply, .context = reply_context};
        memset(output.buffer, 255, 4); output.buffer[4] = 'n';
        bool result = host->execute_admin(host->context, admin_command, admin_write, &output, error);
        free(admin_command);
        return result && !output.failed && admin_flush(&output, error);
    }
    if (strcmp(command, "connect")) return true;
    if (strcmp(argument(args, 1), "28"))
        return reply_text(reply, reply_context, "", "n\nServer uses QuakeWorld protocol 28.\n", error);
    int32_t qport, challenge;
    if (!decimal_prefix(argument(args, 2), &qport) || qport < 0 || qport > UINT16_MAX ||
        !decimal_prefix(argument(args, 3), &challenge) || challenge < 0 ||
        !qa_qw_challenge_validate(challenges, from, (uint32_t)challenge))
        return reply_text(reply, reply_context, "", "n\nBad challenge.\n", error);
    char info_text[1023];
    size_t info_size = strlen(argument(args, 4)); if (info_size > 1022) info_size = 1022;
    memcpy(info_text, argument(args, 4), info_size); info_text[info_size] = 0;
    qa_qw_info info;
    if (!qa_qw_info_parse(info_text, &info, error)) return false;
    const char *spectator_key = qa_qw_info_get(&info, "spectator");
    bool spectator = spectator_key && *spectator_key && strcmp(spectator_key, "0");
    const char *password = spectator ? host->spectator_password : host->password;
    const char *supplied = spectator ? spectator_key : qa_qw_info_get(&info, "password");
    if (!supplied) supplied = "";
    if (!no_password(password) && strcmp(password, supplied)) {
        qa_qw_info_free(&info);
        return reply_text(reply, reply_context, "", spectator ? "n\nrequires a spectator password\n\n" :
                            "n\nserver requires a password\n\n", error);
    }
    info_delete(&info, spectator ? "spectator" : "password");
    if (spectator) info_spectator(&info);
    const char *wide = qa_qw_info_get(&info, "*wide");
    bool donor_wide = wide && !strcmp(wide, "1");
    char userinfo[196]; userinfo_filtered(&info, host->high_characters, userinfo);
    qa_qw_info_free(&info);
    if (!host->connect) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld connect provider");
    qa_qw_connect_request request = {*from, (uint16_t)qport, (uint32_t)challenge, userinfo, spectator, donor_wide};
    qa_q1_connect_result result = {0};
    if (!host->connect(host->context, &request, now_ns, &result, error)) return false;
    if (result.decision == QA_Q1_CONNECT_IGNORE) return true;
    if (result.decision == QA_Q1_CONNECT_ACCEPT) return reply_text(reply, reply_context, "", "j", error);
    if (result.decision == QA_Q1_CONNECT_REJECT) return reply_text(reply, reply_context, "n", result.reason, error);
    return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld connection decision");
}

bool qa_qw_connectionless_receive(const qa_qw_connection_host *host, qa_qw_challenges *challenges,
                                   qa_bytes bytes, const qa_net_address *from, uint64_t now_ns,
                                   qa_qw_reply_fn reply, void *reply_context, qa_error *error) {
    if (!host || !challenges || !from || !reply || !host->password || !host->spectator_password || !host->rcon_password)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld connectionless host");
    if (host->blocked && host->blocked(host->context, from))
        return reply_text(reply, reply_context, "", "n\nbanned.\n", error);
    qa_bytes text;
    if (!qa_qw_oob_decode(bytes, &text, error)) return false;
    char *line = malloc(text.size + 1);
    if (!line) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld connectionless command");
    memcpy(line, text.data, text.size); line[text.size] = 0;
    char *newline = strchr(line, '\n'); if (newline) *newline = 0;
    qw_arguments args;
    if (!arguments_parse(line, &args, error)) { free(line); return false; }
    bool result = qw_command(host, challenges, &args, text.size == 1 && text.data[0] == 'k',
                              from, now_ns, reply, reply_context, error);
    arguments_free(&args); free(line); return result;
}

struct qa_nq_connect_client {
    qa_q1_connect_state state;
    uint64_t sent_ns;
    bool sent;
    char *owned_reason;
};
struct qa_qw_connect_client {
    qa_q1_connect_state state;
    uint64_t sent_ns;
    bool sent;
    uint16_t qport;
    char *userinfo, *owned_reason;
};

bool qa_nq_connect_create(qa_nq_connect_client **out, qa_error *error) {
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake connect client output");
    qa_nq_connect_client *client = calloc(1, sizeof(*client));
    if (!client) return fail(error, QA_ERROR_MEMORY, "Cannot allocate NetQuake connect client");
    client->state.phase = QA_Q1_CONNECT_WAITING;
    *out = client; return true;
}
void qa_nq_connect_destroy(qa_nq_connect_client *client) {
    if (client) { free(client->owned_reason); free(client); }
}
qa_q1_connect_state qa_nq_connect_state(const qa_nq_connect_client *client) {
    return client ? client->state : (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REJECTED, .reason = "Missing connection"};
}
bool qa_nq_connect_next(qa_nq_connect_client *client, uint64_t now_ns, bool *present,
                         qa_net_writer *writer, qa_error *error) {
    if (!client || !present || !writer) return fail(error, QA_ERROR_ARGUMENT, "Invalid NetQuake connect output");
    *present = false;
    if (client->state.phase != QA_Q1_CONNECT_WAITING ||
        (client->sent && (now_ns < client->sent_ns || now_ns - client->sent_ns < UINT64_C(2500000000)))) return true;
    if (client->state.attempts == 3) {
        client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REJECTED, .reason = "No response"};
        return true;
    }
    qa_nq_control request = {.kind = QA_NQ_CONNECT_REQUEST, .data.request = {"QUAKE", 3}};
    if (!qa_nq_control_encode(&request, writer)) return false;
    ++client->state.attempts; client->sent_ns = now_ns; client->sent = true;
    *present = true; return true;
}
bool qa_nq_connect_receive(qa_nq_connect_client *client, qa_bytes bytes, qa_error *error) {
    if (!client) return fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake connect client");
    qa_nq_control message;
    if (!qa_nq_control_decode(bytes, &message, error)) return false;
    if (message.kind == QA_NQ_REJECT) {
        char *reason = copy_text(message.data.reason, error);
        if (!reason) return false;
        free(client->owned_reason); client->owned_reason = reason;
        client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REJECTED, .reason = reason};
    } else if (message.kind == QA_NQ_ACCEPT) {
        if (message.data.port < 1 || message.data.port > UINT16_MAX)
            return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake accepted port");
        free(client->owned_reason); client->owned_reason = NULL;
        client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_CONNECTED, .port = (uint16_t)message.data.port};
    }
    return true;
}

bool qa_qw_connect_create(uint16_t qport, const char *userinfo, qa_qw_connect_client **out, qa_error *error) {
    if (!userinfo || !out || strlen(userinfo) > 65500 || strpbrk(userinfo, "\"\r\n"))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld connect userinfo");
    qa_qw_connect_client *client = calloc(1, sizeof(*client));
    if (!client) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld connect client");
    client->userinfo = copy_text(userinfo, error);
    if (!client->userinfo) { free(client); return false; }
    client->qport = qport; client->state.phase = QA_Q1_CONNECT_CHALLENGE;
    *out = client; return true;
}
void qa_qw_connect_destroy(qa_qw_connect_client *client) {
    if (client) { free(client->userinfo); free(client->owned_reason); free(client); }
}
qa_q1_connect_state qa_qw_connect_state(const qa_qw_connect_client *client) {
    return client ? client->state : (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REJECTED, .reason = "Missing connection"};
}
bool qa_qw_connect_next(qa_qw_connect_client *client, uint64_t now_ns, bool *present,
                         qa_net_writer *writer, qa_error *error) {
    if (!client || !present || !writer) return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld connect output");
    *present = false;
    if (client->state.phase == QA_Q1_CONNECT_CONNECTED || client->state.phase == QA_Q1_CONNECT_REJECTED ||
        (client->sent && (now_ns < client->sent_ns || now_ns - client->sent_ns < UINT64_C(5000000000)))) return true;
    bool written;
    if (client->state.phase == QA_Q1_CONNECT_CHALLENGE) written = qa_qw_oob_encode("getchallenge\n", false, writer);
    else {
        size_t capacity = strlen(client->userinfo) + 80;
        char *request = malloc(capacity);
        if (!request) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld connect request");
        int count = snprintf(request, capacity, "connect 28 %u %" PRId32 " \"%s\"\n",
                              (unsigned)client->qport, client->state.challenge, client->userinfo);
        written = count >= 0 && (size_t)count < capacity && qa_qw_oob_encode(request, false, writer);
        free(request);
        if (count < 0 || (size_t)count >= capacity) return fail(error, QA_ERROR_FORMAT, "QuakeWorld connect request formatting failed");
    }
    if (!written) return false;
    client->sent = true; client->sent_ns = now_ns; *present = true;
    return true;
}
bool qa_qw_connect_receive(qa_qw_connect_client *client, qa_bytes bytes, qa_error *error) {
    if (!client) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld connect client");
    qa_bytes text;
    if (!qa_qw_oob_decode(bytes, &text, error)) return false;
    if (!text.size) return true;
    if (text.data[0] == 'c') {
        char *number = malloc(text.size);
        if (!number) return fail(error, QA_ERROR_MEMORY, "Cannot allocate challenge reply");
        memcpy(number, text.data + 1, text.size - 1); number[text.size - 1] = 0;
        int32_t challenge; bool valid = decimal_prefix(number, &challenge);
        free(number);
        if (valid) {
            free(client->owned_reason); client->owned_reason = NULL;
            client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REQUESTING, .challenge = challenge};
            client->sent = false;
        }
    } else if (text.size == 1 && text.data[0] == 'j') {
        free(client->owned_reason); client->owned_reason = NULL;
        client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_CONNECTED};
    } else if (text.data[0] == 'n') {
        char *reason = malloc(text.size);
        if (!reason) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld rejection reason");
        memcpy(reason, text.data + 1, text.size - 1); reason[text.size - 1] = 0;
        free(client->owned_reason); client->owned_reason = reason;
        client->state = (qa_q1_connect_state){.phase = QA_Q1_CONNECT_REJECTED, .reason = reason};
    }
    return true;
}

bool qa_qw_heartbeat(uint32_t sequence, uint32_t active_clients, qa_net_writer *writer) {
    char text[64];
    (void)snprintf(text, sizeof(text), "a\n%" PRIu32 "\n%" PRIu32 "\n", sequence, active_clients);
    return qa_qw_oob_encode(text, false, writer);
}
bool qa_qw_shutdown(qa_net_writer *writer) { return qa_qw_oob_encode("C\n", false, writer); }
bool qa_qw_master_next(qa_qw_master_heartbeat *heartbeat, uint64_t now_ns, uint32_t active_clients,
                        bool force, bool *present, qa_net_writer *writer, qa_error *error) {
    if (!heartbeat || !present || !writer) return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld heartbeat output");
    *present = false;
    if (!force && heartbeat->sent &&
        (now_ns < heartbeat->previous_ns || now_ns - heartbeat->previous_ns < UINT64_C(300000000000))) return true;
    uint32_t next_sequence = heartbeat->sequence + 1;
    if (!qa_qw_heartbeat(next_sequence, active_clients, writer)) return false;
    heartbeat->sequence = next_sequence; heartbeat->previous_ns = now_ns; heartbeat->sent = true;
    *present = true; return true;
}

static uint8_t saved_reason_kind(const qa_q1_connect_state *state, const char *owned)
{
    if (owned) return state->reason == owned ? 1 : 3;
    return !state->reason ? 0 : !strcmp(state->reason, "No response") ? 2 : 3;
}
static bool nq_connect_saved_valid(const qa_nq_connect_client *client)
{
    if (!client || client->state.challenge || (!client->sent && client->sent_ns)) return false;
    uint8_t reason = saved_reason_kind(&client->state, client->owned_reason);
    switch (client->state.phase) {
    case QA_Q1_CONNECT_WAITING:
        return client->state.attempts <= 3 && client->sent == (client->state.attempts != 0) &&
            !client->state.port && !reason;
    case QA_Q1_CONNECT_CONNECTED:
        return !client->state.attempts && client->state.port && !reason;
    case QA_Q1_CONNECT_REJECTED:
        return !client->state.attempts && !client->state.port &&
            (reason == 1 || (reason == 2 && client->sent));
    default: return false;
    }
}
static bool qw_connect_saved_valid(const qa_qw_connect_client *client)
{
    if (!client || !client->userinfo || strlen(client->userinfo) > 65500 ||
        strpbrk(client->userinfo, "\"\r\n") || client->state.attempts || client->state.port) return false;
    uint8_t reason = saved_reason_kind(&client->state, client->owned_reason);
    switch (client->state.phase) {
    case QA_Q1_CONNECT_CHALLENGE: return !client->state.challenge && !reason;
    case QA_Q1_CONNECT_REQUESTING: return !reason;
    case QA_Q1_CONNECT_CONNECTED: return !client->state.challenge && !reason;
    case QA_Q1_CONNECT_REJECTED: return !client->state.challenge && reason == 1;
    default: return false;
    }
}
static bool connect_state_write(qa_net_writer *writer, const qa_q1_connect_state *state,
    uint64_t sent_ns, bool sent, const char *owned_reason)
{
    uint8_t reason = saved_reason_kind(state, owned_reason);
    return qa_net_write_u32(writer, state->phase) && qa_net_write_u32(writer, state->attempts) &&
        qa_net_write_u16(writer, state->port) && qa_net_write_i32(writer, state->challenge) &&
        qa_net_write_u64(writer, sent_ns) && qa_net_write_u8(writer, sent) && qa_net_write_u8(writer, reason) &&
        (reason != 1 || qa_net_write_string(writer, owned_reason));
}
static bool connect_state_read(qa_net_reader *reader, qa_q1_connect_state *state,
    uint64_t *sent_ns, bool *sent, char **owned_reason)
{
    state->phase = (qa_q1_connect_phase)qa_net_read_u32(reader);
    state->attempts = qa_net_read_u32(reader); state->port = qa_net_read_u16(reader);
    state->challenge = qa_net_read_i32(reader); *sent_ns = qa_net_read_u64(reader);
    *sent = q3_save_bool(reader); uint8_t reason = qa_net_read_u8(reader);
    if (reader->failed || reason > 2) return qa_net_reader_fail(reader, "Invalid Q1 connection rejection ownership");
    if (reason == 1) {
        const char *text;
        if (!qa_q1_read_cstring(reader, &text)) return false;
        *owned_reason = copy_text(text, reader->error);
        if (!*owned_reason) { reader->failed = true; return false; }
        state->reason = *owned_reason;
    } else state->reason = reason == 2 ? "No response" : NULL;
    return true;
}
bool qa_nq_connect_checkpoint(const qa_nq_connect_client *client, qa_buffer *out, qa_error *error)
{
    if (!out || !nq_connect_saved_valid(client)) return fail(error, QA_ERROR_ARGUMENT, "Invalid actual NetQuake connect owner");
    size_t size = 32;
    if (client->owned_reason) {
        size_t length = strlen(client->owned_reason);
        if (length > SIZE_MAX - size - 1) return fail(error, QA_ERROR_MEMORY, "NetQuake rejection extent exceeds memory");
        size += length + 1;
    }
    uint8_t *data = malloc(size);
    if (!data) return fail(error, QA_ERROR_MEMORY, "Encoding NetQuake connection continuation");
    qa_net_writer writer; qa_net_writer_init(&writer, data, size, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x4e434151)) && qa_net_write_u32(&writer, 1) &&
        connect_state_write(&writer, &client->state, client->sent_ns, client->sent, client->owned_reason);
    if (!ok || writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
bool qa_nq_connect_restore_checkpoint(qa_bytes bytes, qa_nq_connect_client **out, qa_error *error)
{
    if (!out || *out || (bytes.size && !bytes.data)) return fail(error, QA_ERROR_ARGUMENT, "NetQuake connect restore requires empty output");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x4e434151) || qa_net_read_u32(&reader) != 1)
        return fail(error, QA_ERROR_FORMAT, "Invalid NetQuake connect continuation schema");
    qa_nq_connect_client *client = NULL;
    if (!qa_nq_connect_create(&client, error)) return false;
    bool ok = connect_state_read(&reader, &client->state, &client->sent_ns, &client->sent, &client->owned_reason) &&
        qa_net_reader_finish(&reader) && nq_connect_saved_valid(client);
    if (!ok) {
        qa_nq_connect_destroy(client);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid saved NetQuake connection state");
        return false;
    }
    *out = client; return true;
}
bool qa_qw_connect_checkpoint(const qa_qw_connect_client *client, qa_buffer *out, qa_error *error)
{
    if (!out || !qw_connect_saved_valid(client)) return fail(error, QA_ERROR_ARGUMENT, "Invalid actual QuakeWorld connect owner");
    size_t size = 35 + strlen(client->userinfo);
    if (client->owned_reason) {
        size_t length = strlen(client->owned_reason);
        if (length > SIZE_MAX - size - 1) return fail(error, QA_ERROR_MEMORY, "QuakeWorld rejection extent exceeds memory");
        size += length + 1;
    }
    uint8_t *data = malloc(size);
    if (!data) return fail(error, QA_ERROR_MEMORY, "Encoding QuakeWorld connection continuation");
    qa_net_writer writer; qa_net_writer_init(&writer, data, size, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x57434151)) && qa_net_write_u32(&writer, 1) &&
        qa_net_write_u16(&writer, client->qport) && qa_net_write_string(&writer, client->userinfo) &&
        connect_state_write(&writer, &client->state, client->sent_ns, client->sent, client->owned_reason);
    if (!ok || writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
bool qa_qw_connect_restore_checkpoint(qa_bytes bytes, uint16_t qport, const char *userinfo,
    qa_qw_connect_client **out, qa_error *error)
{
    if (!out || *out || !userinfo || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld connect restore requires qualified identity and empty output");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    const char *saved_info;
    if (qa_net_read_u32(&reader) != UINT32_C(0x57434151) || qa_net_read_u32(&reader) != 1 ||
        qa_net_read_u16(&reader) != qport || !qa_q1_read_cstring(&reader, &saved_info) || strcmp(saved_info, userinfo))
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld connect continuation identity differs");
    qa_qw_connect_client *client = NULL;
    if (!qa_qw_connect_create(qport, userinfo, &client, error)) return false;
    bool ok = connect_state_read(&reader, &client->state, &client->sent_ns, &client->sent, &client->owned_reason) &&
        qa_net_reader_finish(&reader) && qw_connect_saved_valid(client);
    if (!ok) {
        qa_qw_connect_destroy(client);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid saved QuakeWorld connection state");
        return false;
    }
    *out = client; return true;
}

static bool challenges_saved_valid(const qa_qw_challenges *challenges)
{
    if (!challenges || !challenges->random || !challenges->records || !challenges->capacity ||
        challenges->count > challenges->capacity) return false;
    for (size_t i = 0; i < challenges->capacity; ++i) {
        const qw_challenge_record *record = &challenges->records[i];
        if (!qa_net_address_equal(&record->address, &record->address, true) ||
            (record->challenge & UINT32_C(0x80008000)) ||
            record->issued_seconds > UINT64_MAX / UINT64_C(1000000000)) return false;
        if (i < challenges->count)
            for (size_t j = 0; j < i; ++j)
                if (qa_net_address_equal(&record->address, &challenges->records[j].address, false)) return false;
    }
    return true;
}
bool qa_qw_challenges_checkpoint(const qa_qw_challenges *challenges, qa_buffer *out, qa_error *error)
{
    if (!out || !challenges_saved_valid(challenges) || challenges->capacity > (SIZE_MAX - 24) / 146)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid actual QuakeWorld challenge table");
    size_t capacity = 24 + challenges->capacity * 146;
    uint8_t *data = malloc(capacity);
    if (!data) return fail(error, QA_ERROR_MEMORY, "Encoding QuakeWorld challenges");
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x48434151)) && qa_net_write_u32(&writer, 1) &&
        qa_net_write_u64(&writer, challenges->capacity) && qa_net_write_u64(&writer, challenges->count);
    for (size_t i = 0; ok && i < challenges->capacity; ++i) {
        const qw_challenge_record *record = &challenges->records[i];
        ok = q3_save_address(&writer, &record->address) && qa_net_write_u32(&writer, record->challenge) &&
            qa_net_write_u64(&writer, record->issued_seconds);
    }
    if (!ok || writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
bool qa_qw_challenges_restore_checkpoint(qa_bytes bytes, size_t capacity, qa_qw_random_fn random,
    void *context, qa_qw_challenges **out, qa_error *error)
{
    if (!out || *out || !random || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld challenges restore requires candidate binding and empty output");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x48434151) || qa_net_read_u32(&reader) != 1 ||
        qa_net_read_u64(&reader) != capacity)
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld challenge continuation capacity differs");
    uint64_t count = qa_net_read_u64(&reader);
    if (reader.failed || count > capacity || capacity > qa_net_reader_remaining(&reader) / 20)
        return fail(error, QA_ERROR_FORMAT, "Truncated QuakeWorld challenge table");
    qa_qw_challenges *challenges = NULL;
    if (!qa_qw_challenges_create(capacity, random, context, &challenges, error)) return false;
    challenges->count = (size_t)count;
    bool ok = true;
    for (size_t i = 0; ok && i < capacity; ++i) {
        qw_challenge_record *record = &challenges->records[i];
        ok = q3_restore_address(&reader, &record->address);
        record->challenge = qa_net_read_u32(&reader); record->issued_seconds = qa_net_read_u64(&reader);
        ok = ok && !reader.failed;
    }
    if (ok) ok = qa_net_reader_finish(&reader) && challenges_saved_valid(challenges);
    if (!ok) {
        qa_qw_challenges_destroy(challenges);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid saved QuakeWorld challenge records");
        return false;
    }
    *out = challenges; return true;
}

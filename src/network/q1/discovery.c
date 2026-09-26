#include "qa/network_q1_channel.h"

#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message); return false;
}
bool qa_nq_discovery_query(qa_net_writer *writer) {
    qa_nq_control request = {.kind = QA_NQ_SERVER_INFO_REQUEST, .data.request = {"QUAKE", 3}};
    return qa_nq_control_encode(&request, writer);
}
bool qa_qw_discovery_query(qa_net_writer *writer) { return qa_qw_oob_encode("status\n", false, writer); }
bool qa_q1_discovery_master_query(bool quakeworld, qa_net_writer *writer) {
    return qa_net_writer_fail(writer, quakeworld ? "QuakeWorld master lists use the HTTP discovery service" :
                               "NetQuake has no configured master discovery protocol");
}
void qa_q1_discovery_free(qa_q1_discovery *status) {
    if (!status) return;
    qa_qw_info_free(&status->rules); free(status->player_details); free(status->storage);
    *status = (qa_q1_discovery){0};
}
static bool nq_discovery(qa_bytes bytes, qa_q1_discovery *out, qa_error *error) {
    qa_nq_control response;
    if (!qa_nq_control_decode(bytes, &response, error)) return false;
    if (response.kind != QA_NQ_SERVER_INFO || response.data.server.version != 3)
        return fail(error, QA_ERROR_FORMAT, "Not a compatible NetQuake discovery response");
    size_t name_size = strlen(response.data.server.name) + 1;
    size_t map_size = strlen(response.data.server.map) + 1;
    size_t address_size = strlen(response.data.server.address) + 1;
    qa_q1_discovery status = {.protocol = {.kind = QA_NET_NQ15},
        .players = response.data.server.players, .max_players = response.data.server.max_players};
    status.storage = malloc(name_size + map_size + address_size);
    if (!status.storage) return fail(error, QA_ERROR_MEMORY, "Cannot allocate NetQuake discovery text");
    memcpy(status.storage, response.data.server.name, name_size);
    memcpy(status.storage + name_size, response.data.server.map, map_size);
    memcpy(status.storage + name_size + map_size, response.data.server.address, address_size);
    status.name = status.storage; status.map = status.storage + name_size;
    status.source_address = status.storage + name_size + map_size;
    *out = status; return true;
}

static bool unsigned_decimal(const char **cursor, uint32_t limit, uint32_t *out) {
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return false;
    uint32_t number = 0;
    do {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (number > limit / 10 || (number == limit / 10 && digit > limit % 10)) return false;
        number = number * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p; *out = number; return true;
}
static bool signed_decimal(const char **cursor, int32_t *out) {
    const char *p = *cursor; bool negative = *p == '-';
    if (negative) ++p;
    uint32_t magnitude;
    if (!unsigned_decimal(&p, negative ? UINT32_C(2147483648) : INT32_MAX, &magnitude)) return false;
    *out = negative ? (magnitude == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)magnitude) : (int32_t)magnitude;
    *cursor = p; return true;
}
static bool one_space(const char **cursor) {
    if (**cursor != ' ') return false;
    ++*cursor; return true;
}
static bool player_line(char *line, qa_q1_discovery_player *out) {
    const char *p = line;
    uint32_t ignored;
    int32_t score, ping;
    if (!unsigned_decimal(&p, UINT32_MAX, &ignored) || !one_space(&p) ||
        !signed_decimal(&p, &score) || !one_space(&p) ||
        !unsigned_decimal(&p, UINT32_MAX, &ignored) || !one_space(&p) ||
        !signed_decimal(&p, &ping) || !one_space(&p) || *p != '"') return false;
    const char *name = ++p;
    const char *name_end = strchr(name, '"');
    if (!name_end) return false;
    p = name_end + 1;
    if (!one_space(&p) || *p != '"') return false;
    const char *skin_end = strchr(p + 1, '"');
    if (!skin_end) return false;
    p = skin_end + 1;
    if (!one_space(&p) || !unsigned_decimal(&p, UINT32_MAX, &ignored) ||
        !one_space(&p) || !unsigned_decimal(&p, UINT32_MAX, &ignored)) return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\v' || *p == '\f') ++p;
    if (*p) return false;
    line[(size_t)(name_end - line)] = 0;
    *out = (qa_q1_discovery_player){score, ping, name}; return true;
}
static bool qw_discovery(qa_bytes bytes, qa_q1_discovery *out, qa_error *error) {
    qa_bytes text;
    if (!qa_qw_oob_decode(bytes, &text, error)) return false;
    if (text.size < 2 || text.data[0] != 'n' || text.data[1] != '\\')
        return fail(error, QA_ERROR_FORMAT, "Not a QuakeWorld status reply");
    qa_q1_discovery status = {.protocol = {.kind = QA_NET_QW28}, .max_players = 32, .source_address = ""};
    status.storage = malloc(text.size);
    if (!status.storage) return fail(error, QA_ERROR_MEMORY, "Cannot allocate QuakeWorld discovery text");
    memcpy(status.storage, text.data + 1, text.size - 1); status.storage[text.size - 1] = 0;
    size_t lines = 0;
    for (size_t i = 1; i < text.size; ++i) if (text.data[i] == '\n') ++lines;
    if (lines) {
        status.player_details = calloc(lines, sizeof(*status.player_details));
        if (!status.player_details) { qa_q1_discovery_free(&status); return fail(error, QA_ERROR_MEMORY, "Cannot allocate discovery player records"); }
    }
    char *line = strchr(status.storage, '\n');
    if (line) *line++ = 0;
    if (!qa_qw_info_parse(status.storage, &status.rules, error)) { qa_q1_discovery_free(&status); return false; }
    status.name = qa_qw_info_get(&status.rules, "hostname"); if (!status.name) status.name = "";
    status.map = qa_qw_info_get(&status.rules, "map"); if (!status.map) status.map = "";
    const char *maximum = qa_qw_info_get(&status.rules, "maxclients");
    if (maximum) {
        const char *end = maximum;
        if (!unsigned_decimal(&end, UINT32_MAX, &status.max_players) || *end) {
            qa_q1_discovery_free(&status); return fail(error, QA_ERROR_FORMAT, "Invalid QuakeWorld maxclients field");
        }
    }
    while (line) {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        qa_q1_discovery_player player;
        if (player_line(line, &player)) status.player_details[status.player_count++] = player;
        line = next;
    }
    status.players = (uint32_t)status.player_count;
    *out = status; return true;
}
bool qa_q1_discovery_read(qa_bytes bytes, bool quakeworld, qa_q1_discovery *out, qa_error *error) {
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Missing Quake discovery output");
    return quakeworld ? qw_discovery(bytes, out, error) : nq_discovery(bytes, out, error);
}

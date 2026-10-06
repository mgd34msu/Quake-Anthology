#include "qa/network_q3.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static int32_t source_integer(const char *text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    uint32_t magnitude = 0;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (unsigned)(*text++ - '0');
        if (magnitude > (limit - digit) / 10) magnitude = limit;
        else magnitude = magnitude * 10 + digit;
    }
    if (!negative) return (int32_t)magnitude;
    return magnitude == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)magnitude;
}

static int checksum_order(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

bool qa_q3_verify_pure(const qa_q3_pure_server *server, const qa_q3_tokens *tokens,
                        qa_q3_pure_result *out, qa_error *error)
{
    if (server == NULL || tokens == NULL || out == NULL || tokens->count > 1024 ||
        (server->loaded_count != 0 && server->loaded == NULL))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 pure verification state");
    if (!server->enabled) {
        *out = QA_Q3_PURE_DISABLED;
        return true;
    }
    if (source_integer(qa_q3_token(tokens, 1)) < server->checksum_feed_server_id) {
        *out = QA_Q3_PURE_OUTDATED;
        return true;
    }
    *out = QA_Q3_PURE_REJECTED;
    if (!server->has_cgame || !server->has_ui || tokens->count < 6) return true;
    const char *cgame = qa_q3_token(tokens, 2), *ui = qa_q3_token(tokens, 3);
    if (*cgame == '@' || source_integer(cgame) != server->cgame_checksum ||
        *ui == '@' || source_integer(ui) != server->ui_checksum ||
        *qa_q3_token(tokens, 4) != '@') return true;
    size_t reference_count = tokens->count - 6;
    uint32_t references[1024], loaded[1024];
    uint32_t checksum = (uint32_t)server->checksum_feed ^ (uint32_t)reference_count;
    for (size_t index = 0; index < reference_count; ++index) {
        references[index] = (uint32_t)source_integer(qa_q3_token(tokens, index + 5));
        checksum ^= references[index];
    }
    if (checksum != (uint32_t)source_integer(qa_q3_token(tokens, tokens->count - 1))) return true;
    size_t loaded_count = server->loaded_count < 1024 ? server->loaded_count : 1024;
    if (loaded_count != 0) memcpy(loaded, server->loaded, loaded_count * sizeof(*loaded));
    if (loaded_count > 1) qsort(loaded, loaded_count, sizeof(*loaded), checksum_order);
    if (reference_count > 1) qsort(references, reference_count, sizeof(*references), checksum_order);
    size_t position = 0;
    for (size_t index = 0; index < reference_count; ++index) {
        if (index != 0 && references[index - 1] == references[index]) return true;
        while (position < loaded_count && loaded[position] < references[index]) ++position;
        if (position == loaded_count || loaded[position] != references[index]) return true;
    }
    *out = QA_Q3_PURE_AUTHENTIC;
    return true;
}

static unsigned char ascii_lower(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + 'a' - 'A') : value;
}

static bool pk3_suffix(const char *text)
{
    return text[0] == '.' && ascii_lower((unsigned char)text[1]) == 'p' &&
           ascii_lower((unsigned char)text[2]) == 'k' && text[3] == '3';
}

bool qa_q3_download_name(const char *name, qa_error *error)
{
    if (name == NULL) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 package download name");
    size_t length = 0;
    while (length < 4096 && name[length] != '\0') ++length;
    if (length < 4 || length >= 4096 || !pk3_suffix(name + length - 4))
        return fail(error, QA_ERROR_FORMAT, "Unsafe Q3 package download name");
    size_t component = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)name[index];
        bool allowed = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                       (byte >= '0' && byte <= '9') || byte == '_' || byte == '+' ||
                       byte == '.' || byte == '/' || byte == '-';
        if (!allowed || (byte == '.' && index + 1 < length && name[index + 1] == '.'))
            return fail(error, QA_ERROR_FORMAT, "Unsafe Q3 package download name");
        if (byte == '/') {
            if (index == component || (index - component == 1 && name[component] == '.'))
                return fail(error, QA_ERROR_FORMAT, "Unsafe Q3 package download component");
            component = index + 1;
        }
    }
    return true;
}

static bool stock_prefix(const char *name, const char *prefix)
{
    while (*prefix != '\0') {
        unsigned char byte = (unsigned char)*name++;
        if (byte == '\\' || byte == ':') byte = '/';
        if (ascii_lower(byte) != (unsigned char)*prefix++) return false;
    }
    return true;
}

unsigned qa_q3_stock_package(const char *name)
{
    if (name == NULL) return 0;
    unsigned product;
    size_t prefix;
    if (stock_prefix(name, "baseq3/pak")) {
        product = 1;
        prefix = 10;
    } else if (stock_prefix(name, "missionpack/pak")) {
        product = 2;
        prefix = 15;
    } else return 0;
    if (name[prefix] < '0' || name[prefix] > '8') return 0;
    const char *suffix = name + prefix + 1;
    if (*suffix == '\0') return product;
    if (strlen(suffix) == 4 && pk3_suffix(suffix)) return product;
    return 0;
}

static void append(char *out, size_t capacity, size_t *used, const char *text)
{
    while (*text != '\0' && *used < capacity - 1) out[(*used)++] = *text++;
    out[*used] = '\0';
}

static bool contains_checksum(const uint32_t *sorted, size_t count, uint32_t checksum)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (sorted[middle] < checksum) low = middle + 1;
        else high = middle;
    }
    return low < count && sorted[low] == checksum;
}

bool qa_q3_compare_packages(const qa_q3_package *referenced, size_t reference_count,
                             const uint32_t *loaded, size_t loaded_count,
                             bool (*exists)(void *, const char *), void *context, bool download,
                             char *out, size_t capacity, qa_error *error)
{
    if (out == NULL || capacity == 0 || exists == NULL ||
        (reference_count != 0 && referenced == NULL) || (loaded_count != 0 && loaded == NULL) ||
        loaded_count > SIZE_MAX / sizeof(uint32_t))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 package comparison input");
    uint32_t *sorted = loaded_count == 0 ? NULL : malloc(loaded_count * sizeof(*sorted));
    if (loaded_count != 0 && sorted == NULL)
        return fail(error, QA_ERROR_MEMORY, "Allocating Q3 package checksum index");
    if (loaded_count != 0) memcpy(sorted, loaded, loaded_count * sizeof(*sorted));
    if (loaded_count > 1) qsort(sorted, loaded_count, sizeof(*sorted), checksum_order);
    size_t used = 0;
    out[0] = '\0';
    bool success = true;
    for (size_t index = 0; index < reference_count; ++index) {
        const qa_q3_package *pack = &referenced[index];
        if (pack->name == NULL || *pack->name == '\0' ||
            contains_checksum(sorted, loaded_count, pack->checksum) || qa_q3_stock_package(pack->name) != 0)
            continue;
        size_t length = 0;
        while (length < 4092 && pack->name[length] != '\0') ++length;
        if (length >= 4092) {
            success = fail(error, QA_ERROR_FORMAT, "Q3 package download name exceeds maximum length");
            break;
        }
        char remote[4096];
        memcpy(remote, pack->name, length);
        memcpy(remote + length, ".pk3", 5);
        if (!qa_q3_download_name(remote, error)) { success = false; break; }
        bool present = exists(context, remote);
        if (download) {
            append(out, capacity, &used, "@");
            append(out, capacity, &used, remote);
            append(out, capacity, &used, "@");
            if (present) {
                char hexadecimal[9];
                static const char digits[] = "0123456789abcdef";
                for (unsigned digit = 0; digit < 8; ++digit)
                    hexadecimal[digit] = digits[(pack->checksum >> (28 - digit * 4)) & 15u];
                hexadecimal[8] = '\0';
                append(out, capacity, &used, pack->name);
                append(out, capacity, &used, ".");
                append(out, capacity, &used, hexadecimal);
                append(out, capacity, &used, ".pk3");
            } else append(out, capacity, &used, remote);
        } else {
            append(out, capacity, &used, remote);
            if (present) append(out, capacity, &used, " (local file exists with wrong checksum)");
            append(out, capacity, &used, "\n");
        }
    }
    free(sorted);
    return success;
}

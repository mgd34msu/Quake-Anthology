#include "qa/network_q3.h"
#include "qa/network_q3_pak_role.h"
#include "qa/network_q3_pak_save.h"
#include "qa/source_save.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IDENTITY_BUCKETS = 8192, ALL_FLAGS = 15 };
struct qa_q3_pak_references {
    qa_q3_pak_reference entries[QA_Q3_SEARCH_PATHS];
    uint16_t order[QA_Q3_SEARCH_PATHS], identities[IDENTITY_BUCKETS];
    size_t count;
    uint32_t checksum_feed;
    unsigned fake_checksum;
    qa_q3_pak_random_fn random;
    void *random_context;
};
struct qa_q3_server_pak_set {
    size_t count;
    uint32_t checksums[QA_Q3_SEARCH_PATHS];
    char *names[QA_Q3_SEARCH_PATHS];
};
static bool fail(qa_error *error, qa_status status, const char *message) {
    qa_error_set(error, status, 0, "%s", message); return false;
}
static unsigned char lower(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + 'a' - 'A') : value;
}
static bool suffix(const char *path, const char *ending) {
    size_t size = strlen(path), length = strlen(ending);
    if (size < length) return false;
    path += size - length;
    for (size_t i = 0; i < length; ++i)
        if (lower((unsigned char)path[i]) != (unsigned char)ending[i]) return false;
    return true;
}
bool qa_q3_pure_loose_path(const char *path) {
    return path && (suffix(path, ".cfg") || suffix(path, ".menu") || suffix(path, ".game")
        || suffix(path, ".dm_68") || suffix(path, ".dat"));
}
static bool excludes_general(const char *path) {
    return suffix(path, ".shader") || suffix(path, ".txt") || suffix(path, ".cfg")
        || suffix(path, ".config") || strstr(path, "levelshots") || suffix(path, ".bot")
        || suffix(path, ".arena") || suffix(path, ".menu");
}
static size_t identity_bucket(const qa_q3_pak_entry *pack) {
    uintptr_t bits = (uintptr_t)pack;
    bits ^= bits >> 7; bits ^= bits >> 17;
    return (size_t)bits & (IDENTITY_BUCKETS - 1);
}
static size_t find_identity(const qa_q3_pak_references *refs, const qa_q3_pak_entry *pack) {
    size_t bucket = identity_bucket(pack);
    for (;;) {
        uint16_t value = refs->identities[bucket];
        if (!value || refs->entries[value - 1].pack == pack) return bucket;
        bucket = (bucket + 1) & (IDENTITY_BUCKETS - 1);
    }
}
static bool add(qa_q3_pak_references *refs, const qa_q3_pak_entry *pack, qa_error *error) {
    if (!pack || !pack->game || !pack->basename || !pack->archive_path)
        return fail(error, QA_ERROR_ARGUMENT, "Incomplete Q3 package catalog entry");
    if (refs->count == QA_Q3_SEARCH_PATHS)
        return fail(error, QA_ERROR_FORMAT, "Q3 package catalog exceeds 4096 entries");
    size_t bucket = find_identity(refs, pack);
    if (refs->identities[bucket]) return fail(error, QA_ERROR_ARGUMENT, "Duplicate Q3 package identity");
    refs->entries[refs->count] = (qa_q3_pak_reference){pack, 0};
    refs->order[refs->count] = (uint16_t)refs->count;
    refs->identities[bucket] = (uint16_t)(refs->count + 1);
    ++refs->count;
    return true;
}
bool qa_q3_pak_references_create(const qa_q3_pak_entry *const *packs, size_t count, uint32_t feed,
                                  qa_q3_pak_random_fn random, void *context,
                                  qa_q3_pak_references **out, qa_error *error) {
    if (!out || !random || count > QA_Q3_SEARCH_PATHS || (count && !packs))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 package reference catalog");
    qa_q3_pak_references *refs = calloc(1, sizeof(*refs));
    if (!refs) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 package references");
    refs->checksum_feed = feed; refs->random = random; refs->random_context = context;
    for (size_t i = 0; i < count; ++i) {
        if (!add(refs, packs[i], error)) { free(refs); return false; }
    }
    *out = refs; return true;
}
void qa_q3_pak_references_destroy(qa_q3_pak_references *refs) { free(refs); }
bool qa_q3_pak_prepend(qa_q3_pak_references *refs, const qa_q3_pak_entry *pack, qa_error *error) {
    if (!refs) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 package reference catalog");
    size_t index = refs->count;
    if (!add(refs, pack, error)) return false;
    memmove(refs->order + 1, refs->order, index * sizeof(*refs->order));
    refs->order[0] = (uint16_t)index; return true;
}
bool qa_q3_pak_reorder(qa_q3_pak_references *refs, const qa_q3_pak_entry *const *packs,
                        size_t count, qa_error *error) {
    if (!refs || count != refs->count || (count && !packs))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 package reorder changes catalog membership");
    uint16_t order[QA_Q3_SEARCH_PATHS];
    uint8_t seen[QA_Q3_SEARCH_PATHS / 8] = {0};
    for (size_t i = 0; i < count; ++i) {
        uint16_t value = refs->identities[find_identity(refs, packs[i])];
        if (!value) return fail(error, QA_ERROR_ARGUMENT, "Q3 package reorder contains an unmounted identity");
        unsigned index = (unsigned)value - 1U, bit = 1u << (index & 7);
        if (seen[index >> 3] & bit) return fail(error, QA_ERROR_ARGUMENT, "Q3 package reorder repeats an identity");
        seen[index >> 3] |= (uint8_t)bit; order[i] = (uint16_t)index;
    }
    if (count) memcpy(refs->order, order, count * sizeof(*order));
    return true;
}
void qa_q3_pak_retain_loose(qa_q3_pak_references *refs, const qa_q3_pak_references *previous) {
    refs->fake_checksum = previous->fake_checksum;
}
bool qa_q3_pak_record_packed(qa_q3_pak_references *refs, const qa_q3_pak_entry *pack,
                              const char *path, qa_error *error) {
    if (!refs || !pack || !path) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 packed-file reference");
    uint16_t value = refs->identities[find_identity(refs, pack)];
    if (!value) return fail(error, QA_ERROR_ARGUMENT, "Packed Q3 file does not belong to this catalog");
    unsigned *flags = &refs->entries[value - 1].flags;
    if (!(*flags & QA_Q3_PAK_GENERAL) && !excludes_general(path)) *flags |= QA_Q3_PAK_GENERAL;
    if (strstr(path, "qagame.qvm")) *flags |= QA_Q3_PAK_QAGAME;
    if (strstr(path, "cgame.qvm")) *flags |= QA_Q3_PAK_CGAME;
    if (strstr(path, "ui.qvm")) *flags |= QA_Q3_PAK_UI;
    return true;
}
bool qa_q3_pak_record_loose(qa_q3_pak_references *refs, const char *path, qa_error *error) {
    if (!refs || !path) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 loose-file reference");
    if (qa_q3_pure_loose_path(path)) return true;
    double random = refs->random(refs->random_context);
    if (!isfinite(random) || random < 0 || random > 1)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 filesystem random value is outside zero through one");
    refs->fake_checksum = (unsigned)random; return true;
}
bool qa_q3_pak_record_client_role(qa_q3_pak_references *refs, const qa_q3_pak_entry *pack,
    qa_qvm_role role, qa_error *error) {
    if (!refs || !pack || (role != QA_QVM_CGAME && role != QA_QVM_UI))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 client package role requires its actual CGAME or UI artifact");
    uint16_t value = refs->identities[find_identity(refs, pack)];
    if (!value) return fail(error, QA_ERROR_ARGUMENT, "Q3 client module package does not belong to this catalog");
    refs->entries[value - 1].flags |= role == QA_QVM_CGAME ? QA_Q3_PAK_CGAME : QA_Q3_PAK_UI;
    return true;
}
bool qa_q3_pak_clear(qa_q3_pak_references *refs, unsigned flags, qa_error *error) {
    if (!refs || flags > ALL_FLAGS) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 package reference flags");
    unsigned mask = flags ? flags : ALL_FLAGS;
    for (size_t i = 0; i < refs->count; ++i) refs->entries[i].flags &= ~mask;
    return true;
}
size_t qa_q3_pak_reference_count(const qa_q3_pak_references *refs) { return refs->count; }
bool qa_q3_pak_reference_at(const qa_q3_pak_references *refs, size_t index, qa_q3_pak_reference *out) {
    if (!refs || !out || index >= refs->count) return false;
    *out = refs->entries[refs->order[index]]; return true;
}
uint32_t qa_q3_pak_checksum_feed(const qa_q3_pak_references *refs) { return refs->checksum_feed; }

static bool reference_order_valid(const uint16_t *order, size_t count, qa_error *error) {
    uint8_t seen[QA_Q3_SEARCH_PATHS / 8] = {0};
    for (size_t i = 0; i < count; ++i) {
        unsigned index = order[i];
        if (index >= count || (seen[index >> 3] & (1u << (index & 7))))
            return fail(error, QA_ERROR_FORMAT, "Q3 saved package search order is not its complete slot permutation");
        seen[index >> 3] |= (uint8_t)(1u << (index & 7));
    }
    return true;
}
static bool reference_valid(const qa_q3_pak_references *refs, qa_error *error) {
    if (!refs || !refs->random || refs->count > QA_Q3_SEARCH_PATHS || refs->fake_checksum > 1 ||
        !reference_order_valid(refs->order, refs->count, error))
        return fail(error, QA_ERROR_FORMAT, "Q3 reference continuation lost its actual catalog or loose checksum cell");
    for (size_t i = 0; i < refs->count; ++i) {
        const qa_q3_pak_entry *pack = refs->entries[i].pack;
        if (!pack || !pack->game || !pack->basename || !pack->archive_path ||
            refs->entries[i].flags > ALL_FLAGS || refs->identities[find_identity(refs, pack)] != i + 1)
            return fail(error, QA_ERROR_FORMAT, "Q3 reference continuation has another actual package identity or flag mask");
    }
    return true;
}
static bool reference_text(qa_source_save_io *io, const char *text) {
    size_t length = strlen(text), saved = length;
    if (!qa_source_save_count(io, &saved, length) || saved != length)
        return fail(io->error, QA_ERROR_FORMAT, "Saved Q3 package spelling differs from its actual inventory slot");
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)text, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset ||
        (length && memcmp(io->input.data + io->offset, text, length)))
        return fail(io->error, QA_ERROR_FORMAT, "Saved Q3 package spelling differs from its actual inventory slot");
    io->offset += length; return true;
}
static bool reference_header(qa_source_save_io *io, size_t *count, uint32_t *feed, uint32_t *loose) {
    uint32_t magic = UINT32_C(0x46525051), version = 1;
    return qa_source_save_u32(io, &magic) && magic == UINT32_C(0x46525051) &&
        qa_source_save_u32(io, &version) && version == 1 &&
        qa_source_save_count(io, count, QA_Q3_SEARCH_PATHS) &&
        qa_source_save_u32(io, feed) && qa_source_save_u32(io, loose) && *loose <= 1;
}
static bool reference_pack(qa_source_save_io *io, const qa_q3_pak_entry *pack, uint32_t *flags) {
    uint32_t checksum = pack->checksum, pure = pack->pure_checksum;
    return reference_text(io, pack->game) && reference_text(io, pack->basename) &&
        reference_text(io, pack->archive_path) && qa_source_save_u32(io, &checksum) && checksum == pack->checksum &&
        qa_source_save_u32(io, &pure) && pure == pack->pure_checksum &&
        qa_source_save_u32(io, flags) && *flags <= ALL_FLAGS;
}
bool qa_q3_pak_references_checkpoint(const qa_q3_pak_references *refs, qa_buffer *out, qa_error *error) {
    if (!out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 reference capture requires its actual owner and empty output");
    if (!reference_valid(refs, error)) return false;
    qa_source_save_io io = {0}; size_t count = refs->count;
    uint32_t feed = refs->checksum_feed, loose = refs->fake_checksum;
    bool ok = qa_source_save_writer(&io, NULL, error) && reference_header(&io, &count, &feed, &loose);
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t flags = refs->entries[i].flags;
        ok = reference_pack(&io, refs->entries[i].pack, &flags);
    }
    for (size_t i = 0; ok && i < count; ++i) {
        uint16_t ordinal = refs->order[i]; ok = qa_source_save_u16(&io, &ordinal);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_pak_references_restore(qa_bytes bytes, const qa_q3_pak_entry *const *packs, size_t count,
    uint32_t feed, qa_q3_pak_random_fn random, void *context, qa_q3_pak_references **out, qa_error *error) {
    if (!out || *out || !random || count > QA_Q3_SEARCH_PATHS || (count && !packs))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 reference restore requires its actual inventory and empty output");
    for (size_t i = 0; i < count; ++i) if (!packs[i] || !packs[i]->game || !packs[i]->basename || !packs[i]->archive_path)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 reference restore lacks an actual inventory slot");
    uint32_t flags[QA_Q3_SEARCH_PATHS] = {0}, saved_feed = feed, loose = 0;
    uint16_t order[QA_Q3_SEARCH_PATHS] = {0}; size_t saved_count = count;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) &&
        reference_header(&io, &saved_count, &saved_feed, &loose) && saved_count == count && saved_feed == feed;
    for (size_t i = 0; ok && i < count; ++i) ok = reference_pack(&io, packs[i], flags + i);
    for (size_t i = 0; ok && i < count; ++i) ok = qa_source_save_u16(&io, order + i);
    if (ok) ok = reference_order_valid(order, count, error) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Q3 reference record differs from its admitted inventory and feed");
        return false;
    }
    qa_q3_pak_references *refs = NULL;
    if (!qa_q3_pak_references_create(packs, count, feed, random, context, &refs, error)) return false;
    for (size_t i = 0; i < count; ++i) refs->entries[i].flags = flags[i];
    memcpy(refs->order, order, count * sizeof(*order)); refs->fake_checksum = loose;
    *out = refs; return true;
}

typedef struct info_output { char *data; size_t size, maximum; } info_output;
static void append(info_output *info, const char *text) {
    while (*text && info->size < info->maximum) info->data[info->size++] = *text++;
    info->data[info->size] = 0;
}
static void append_checksum(info_output *info, uint32_t checksum, bool space) {
    int64_t signed_value = checksum <= INT32_MAX ? (int64_t)checksum : (int64_t)checksum - INT64_C(4294967296);
    char text[16];
    snprintf(text, sizeof(text), "%lld%s", (long long)signed_value, space ? " " : "");
    append(info, text);
}
static bool included(const qa_q3_pak_reference *reference) {
    if (reference->flags) return true;
    static const char base[] = "baseq3";
    for (size_t i = 0; i < sizeof(base) - 1; ++i)
        if (lower((unsigned char)reference->pack->game[i]) != (unsigned char)base[i]) return true;
    return false;
}
bool qa_q3_pak_report(const qa_q3_pak_references *refs, qa_q3_pak_report_kind kind,
                       char *out, size_t capacity, qa_error *error) {
    if (!refs || !out || !capacity || kind < QA_Q3_PAK_LOADED_CHECKSUMS || kind > QA_Q3_PAK_GAME_CHECKSUM)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 package reference report");
    info_output info = {out, 0, capacity > QA_Q3_BIG_INFO_CHARS ? QA_Q3_BIG_INFO_CHARS - 1 : capacity - 1};
    out[0] = 0;
    if (kind == QA_Q3_PAK_REFERENCED_PURE_CHECKSUMS) {
        const unsigned flags[] = {QA_Q3_PAK_CGAME, QA_Q3_PAK_UI, QA_Q3_PAK_GENERAL};
        uint32_t checksum = refs->checksum_feed, count = 0;
        for (size_t f = 0; f < 3; ++f) {
            if (flags[f] == QA_Q3_PAK_GENERAL) append(&info, "@ ");
            for (size_t i = 0; i < refs->count; ++i) {
                const qa_q3_pak_reference *state = &refs->entries[refs->order[i]];
                if (!(state->flags & flags[f])) continue;
                append_checksum(&info, state->pack->pure_checksum, true);
                if (flags[f] != QA_Q3_PAK_GENERAL) break;
                checksum ^= state->pack->pure_checksum; ++count;
            }
            if (refs->fake_checksum) append_checksum(&info, refs->fake_checksum, true);
        }
        append_checksum(&info, checksum ^ count, false); return true;
    }
    for (size_t i = 0; i < refs->count; ++i) {
        const qa_q3_pak_reference *state = &refs->entries[refs->order[i]];
        switch (kind) {
        case QA_Q3_PAK_LOADED_CHECKSUMS: append_checksum(&info, state->pack->checksum, true); break;
        case QA_Q3_PAK_LOADED_NAMES:
            if (info.size) append(&info, " ");
            append(&info, state->pack->basename); break;
        case QA_Q3_PAK_LOADED_PURE_CHECKSUMS: append_checksum(&info, state->pack->pure_checksum, true); break;
        case QA_Q3_PAK_REFERENCED_CHECKSUMS:
            if (included(state)) append_checksum(&info, state->pack->checksum, true);
            break;
        case QA_Q3_PAK_REFERENCED_NAMES:
            if (info.size) append(&info, " ");
            if (included(state)) { append(&info, state->pack->game); append(&info, "/"); append(&info, state->pack->basename); }
            break;
        case QA_Q3_PAK_GAME_CHECKSUM:
            if (state->flags & QA_Q3_PAK_QAGAME) { info.size = 0; out[0] = 0; append_checksum(&info, state->pack->checksum, false); }
            break;
        case QA_Q3_PAK_REFERENCED_PURE_CHECKSUMS: break;
        }
    }
    return true;
}
bool qa_q3_pak_is_pure(uint32_t checksum, const uint32_t *server, size_t count) {
    if (!count) return true;
    if (!server) return false;
    for (size_t i = 0; i < count; ++i) if (checksum == server[i]) return true;
    return false;
}
bool qa_q3_reorder_pure_paths(qa_q3_pure_path *paths, size_t count, const uint32_t *server,
                               size_t server_count, qa_error *error) {
    if (count > QA_Q3_SEARCH_PATHS || server_count > QA_Q3_SEARCH_PATHS || (count && !paths) || (server_count && !server))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 pure search paths");
    for (size_t i = 0; i < count; ++i)
        if (paths[i].kind != QA_Q3_PURE_DIRECTORY && paths[i].kind != QA_Q3_PURE_PACKAGE)
            return fail(error, QA_ERROR_ARGUMENT, "Unknown Q3 pure search-path kind");
    size_t insertion = 0;
    for (size_t p = 0; p < server_count; ++p) {
        for (size_t i = insertion; i < count; ++i) {
            if (paths[i].kind != QA_Q3_PURE_PACKAGE || paths[i].checksum != server[p]) continue;
            qa_q3_pure_path selected = paths[i];
            memmove(paths + insertion + 1, paths + insertion, (i - insertion) * sizeof(*paths));
            paths[insertion++] = selected; break;
        }
    }
    return true;
}

static uint32_t source_checksum(const char *text) {
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (unsigned)(*text++ - '0');
        value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    return negative ? UINT32_C(0) - value : value;
}
bool qa_q3_server_pak_set_create(qa_q3_server_pak_set **out, qa_error *error) {
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 server package set output");
    qa_q3_server_pak_set *set = calloc(1, sizeof(*set));
    if (!set) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 server package set");
    *out = set; return true;
}
void qa_q3_server_pak_set_destroy(qa_q3_server_pak_set *set) {
    if (!set) return;
    for (size_t i = 0; i < QA_Q3_SEARCH_PATHS; ++i) free(set->names[i]);
    free(set);
}
bool qa_q3_server_pak_set_checksums(qa_q3_server_pak_set *set, const char *text, qa_error *error) {
    if (!set || !text) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 server package checksums");
    qa_q3_tokens tokens;
    if (!qa_q3_tokenize(text, &tokens, error)) return false;
    for (size_t i = 0; i < tokens.count; ++i) set->checksums[i] = source_checksum(qa_q3_token(&tokens, i));
    set->count = tokens.count; return true;
}
bool qa_q3_server_pak_set_names(qa_q3_server_pak_set *set, const char *text,
                                 size_t clear_count, qa_error *error) {
    if (!set || !text) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 server package names");
    if (clear_count == SIZE_MAX) clear_count = set->count;
    if (clear_count > QA_Q3_SEARCH_PATHS) return fail(error, QA_ERROR_ARGUMENT, "Q3 server package clear exceeds 4096 cells");
    qa_q3_tokens tokens;
    if (!qa_q3_tokenize(text, &tokens, error)) return false;
    char *names[1024] = {0};
    for (size_t i = 0; i < tokens.count; ++i) {
        const char *token = qa_q3_token(&tokens, i);
        size_t size = strlen(token) + 1;
        names[i] = malloc(size);
        if (!names[i]) {
            for (size_t j = 0; j < i; ++j) free(names[j]);
            return fail(error, QA_ERROR_MEMORY, "Allocating Q3 server package name");
        }
        memcpy(names[i], token, size);
    }
    for (size_t i = 0; i < clear_count; ++i) { free(set->names[i]); set->names[i] = NULL; }
    for (size_t i = 0; i < tokens.count; ++i) { free(set->names[i]); set->names[i] = names[i]; }
    return true;
}
const uint32_t *qa_q3_server_pak_set_sums(const qa_q3_server_pak_set *set, size_t *count) {
    if (count) *count = set->count;
    return set->checksums;
}
bool qa_q3_server_pak_set_at(const qa_q3_server_pak_set *set, size_t index, qa_q3_package *out) {
    if (!set || !out || index >= set->count) return false;
    *out = (qa_q3_package){set->names[index], set->checksums[index]}; return true;
}

#include "network_browser.h"
#include "qa/console.h"
#include "qa/network_q3.h"
#include "../../network/service_save_fields.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct browser_row { qa_net_address address; char name[32]; double ping; int32_t visible; bool addressed; } browser_row;
typedef struct browser_list {
    browser_row *rows;
    uint32_t count, capacity;
    uint64_t generation, reset_generation;
    bool synchronized, pending;
} browser_list;
typedef struct browser_ping {
    qa_net_address address;
    qa_browser_q3_request_id request;
    uint64_t start;
    char info[1024];
    bool addressed, published;
} browser_ping;
typedef struct browser_status {
    qa_net_address address;
    qa_browser_q3_request_id request;
    uint64_t start;
    bool addressed, retrieved, print;
} browser_status;
struct frontend_q3_browser {
    frontend_q3_browser_options options;
    browser_list lists[4];
    qa_net_address seen[8192], overflow[4096];
    uint32_t seen_count, overflow_count;
    browser_ping pings[32];
    browser_status statuses[16];
    bool busy;
};
static bool fail(qa_error *e, const char *text)
{ qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool current(frontend_q3_browser *b, qa_error *e)
{ return b && b->options.current(b->options.context, e); }
static bool access_current(void *context, qa_error *e)
{
    frontend_q3_browser_access *a = context;
    return a && a->browser && a->cvars && a->current && a->current(a->context, e) && current(a->browser, e);
}
static bool same(const qa_net_address *a, const qa_net_address *b)
{ return qa_net_address_equal(a, b, true); }
static bool contains(const qa_net_address *addresses, uint32_t count, const qa_net_address *a)
{ for (uint32_t i = 0; i < count; ++i) if (same(a, &addresses[i])) return true; return false; }
static bool row_contains(const browser_row *rows, uint32_t count, const qa_net_address *a)
{ for (uint32_t i = 0; i < count; ++i) if (rows[i].addressed && same(a, &rows[i].address)) return true; return false; }
static bool list_read(frontend_q3_browser *b, int32_t source, browser_list **out, qa_error *e)
{
    *out = NULL;
    if (!current(b, e)) return false;
    if (source < 0 || source > 3) return true;
    browser_list *list = &b->lists[source]; qa_browser_q3_list canonical;
    if (!qa_server_browser_q3_list_read(b->options.browser, source, &canonical, e)) return false;
    list->pending = canonical.pending;
    if (list->synchronized && list->generation == canonical.generation) { *out = list; return true; }
    browser_row *previous = calloc(list->capacity, sizeof(*previous));
    if (!previous) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Synchronizing actual Q3 UI list"); return false; }
    bool reset = !list->synchronized || list->reset_generation != canonical.reset_generation;
    uint32_t old_count = reset ? 0 : list->count;
    memcpy(previous, list->rows, list->capacity * sizeof(*previous));
    if (reset) {
        for (uint32_t i = 0; i < list->capacity; ++i) {
            int32_t visible = list->rows[i].visible;
            list->rows[i] = (browser_row){.visible = visible};
        }
        if (source == 2) b->seen_count = b->overflow_count = 0;
    }
    if (source == 2) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < old_count; ++i)
            if (previous[i].addressed && contains(canonical.addresses, canonical.count, &previous[i].address))
                list->rows[count++] = previous[i];
        list->count = count;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < b->seen_count; ++i)
            if (contains(canonical.addresses, canonical.count, &b->seen[i])) b->seen[kept++] = b->seen[i];
        b->seen_count = kept; kept = 0;
        for (uint32_t i = 0; i < b->overflow_count; ++i)
            if (contains(canonical.addresses, canonical.count, &b->overflow[i]) &&
                !row_contains(list->rows, count, &b->overflow[i])) b->overflow[kept++] = b->overflow[i];
        b->overflow_count = kept;
        for (uint32_t i = 0; i < canonical.count; ++i) {
            const qa_net_address *a = &canonical.addresses[i];
            if (contains(b->seen, b->seen_count, a)) continue;
            b->seen[b->seen_count++] = *a;
            if (list->count < list->capacity) {
                uint32_t slot = list->count++;
                list->rows[slot] = (browser_row){.address = *a, .addressed = true,
                    .visible = previous[slot].visible, .ping = -1};
            } else if (b->overflow_count < 4096) b->overflow[b->overflow_count++] = *a;
        }
    } else {
        list->count = canonical.count < list->capacity ? canonical.count : list->capacity;
        for (uint32_t i = 0; i < list->count; ++i) {
            bool found = false;
            for (uint32_t j = 0; j < old_count; ++j) if (previous[j].addressed && same(&previous[j].address, &canonical.addresses[i])) {
                list->rows[i] = previous[j]; found = true; break;
            }
            if (!found) list->rows[i] = (browser_row){.address = canonical.addresses[i], .addressed = true,
                .visible = previous[i].visible, .ping = -1};
        }
    }
    list->generation = canonical.generation; list->reset_generation = canonical.reset_generation; list->synchronized = true;
    free(previous); *out = list; return true;
}
static bool row_read(frontend_q3_browser *b, int32_t source, int32_t index, browser_row **out, qa_error *e)
{
    browser_list *list; *out = NULL;
    if (!list_read(b, source, &list, e)) return false;
    if (list && index >= 0 && (uint32_t)index < list->capacity) *out = &list->rows[index];
    return true;
}
static int32_t integer(const char *text)
{
    char *end; errno = 0; long long n = strtoll(text, &end, 10);
    if (end == text) return 0;
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : (int32_t)n;
}
static int32_t word(double value)
{
    double n = fmod(trunc(value), 4294967296.0); if (n < 0) n += 4294967296.0;
    uint32_t bits = (uint32_t)n; int32_t result; memcpy(&result, &bits, sizeof(result)); return result;
}
static double elapsed(uint64_t now, uint64_t then)
{ return trunc((double)now / 1000000.0 - (double)then / 1000000.0); }
static void rule(const char *info, const char *key, char *out, size_t capacity)
{
    out[0] = 0; const char *p = info; if (*p == '\\') ++p;
    while (*p) {
        const char *name = p; while (*p && *p != '\\') ++p;
        size_t n = (size_t)(p - name); if (!*p) break;
        const char *value = ++p; while (*p && *p != '\\') ++p;
        if (strlen(key) == n && !memcmp(name, key, n)) {
            size_t size = (size_t)(p - value); if (size >= capacity) size = capacity - 1;
            memcpy(out, value, size); out[size] = 0;
        }
        if (*p) ++p;
    }
}
static bool info_set(frontend_q3_browser *b, char info[1024], const char *key, const char *value, qa_error *e)
{
    if (strlen(info) >= 1024) return fail(e, "Info_SetValueForKey: oversize infostring");
    const char *warning = strpbrk(key, "\\") || strpbrk(value, "\\") ? "Can't use keys or values with a \\\n" :
        strpbrk(key, ";") || strpbrk(value, ";") ? "Can't use keys or values with a semicolon\n" :
        strpbrk(key, "\"") || strpbrk(value, "\"") ? "Can't use keys or values with a \"\n" : NULL;
    if (warning) return b->options.print(b->options.context, warning, e);
    /* Remove exact key before the source length check. */
    char retained[1024]; strcpy(retained, info);
    char *p = retained;
    while (*p) {
        char *begin = p; if (*p == '\\') ++p;
        char *name = p; while (*p && *p != '\\') ++p;
        size_t size = (size_t)(p - name); if (!*p) break;
        ++p; while (*p && *p != '\\') ++p;
        if (strlen(key) == size && !memcmp(name, key, size)) { memmove(begin, p, strlen(p) + 1); break; }
    }
    strcpy(info, retained); if (!*value) return true;
    char pair[1024]; size_t used = 0;
    const char *parts[] = {"\\", key, "\\", value};
    for (size_t i = 0; i < 4; ++i) for (const char *s = parts[i]; *s && used < 1023; ++s) pair[used++] = *s;
    pair[used] = 0;
    size_t length = strlen(retained);
    if (used + length > 1024) return b->options.print(b->options.context, "Info string length exceeded\n", e);
    if (used + length == 1024) return fail(e, "Info string overflows source terminator");
    memcpy(info, pair, used); memcpy(info + used, retained, length + 1); return true;
}
static bool numeric(frontend_q3_browser *b, char info[1024], const char *key, double value, qa_error *e)
{ char text[64]; (void)snprintf(text, sizeof(text), "%.0f", value); return info_set(b, info, key, text, e); }
static bool row_info(frontend_q3_browser *b, const browser_row *row, char info[1024], qa_error *e)
{
    qa_server_entry entry = {0}; bool status = row->addressed && qa_server_browser_q3_entry(b->options.browser, &row->address, &entry) && entry.available;
    char address[256] = "bot", name[32], map[32], game[32], value[8193];
    if (row->addressed && !qa_net_address_format(&row->address, address, sizeof(address), e)) return false;
    const char *full_name = status && *entry.name ? entry.name : *row->name ? row->name : row->addressed ? address : "";
    (void)snprintf(name, sizeof(name), "%.*s", 31, full_name);
    (void)snprintf(map, sizeof(map), "%.*s", 31, status ? entry.map : "");
    rule(status ? entry.rules : "", "game", value, sizeof(value)); (void)snprintf(game, sizeof(game), "%.*s", 31, value);
    info[0] = 0;
    if (!info_set(b, info, "hostname", name, e) || !info_set(b, info, "mapname", map, e) ||
        !numeric(b, info, "clients", status ? entry.players : 0, e) ||
        !numeric(b, info, "sv_maxclients", status ? entry.maximum_players : 0, e) || !numeric(b, info, "ping", row->ping, e)) return false;
    const char *keys[] = {"minping", "maxping"};
    for (size_t i = 0; i < 2; ++i) { rule(status ? entry.rules : "", keys[i], value, sizeof(value)); if (!numeric(b, info, keys[i], integer(value), e)) return false; }
    if (!info_set(b, info, "game", game, e)) return false;
    rule(status ? entry.rules : "", "gametype", value, sizeof(value));
    if (!numeric(b, info, "gametype", integer(value), e) || !numeric(b, info, "nettype", row->addressed ? 1 : 0, e) ||
        !info_set(b, info, "addr", address, e)) return false;
    rule(status ? entry.rules : "", "punkbuster", value, sizeof(value)); return numeric(b, info, "punkbuster", integer(value), e);
}
static bool server_count(void *ctx, int32_t source, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!out || !access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    *out = !list ? 0 : list->pending ? -1 : (int32_t)list->count; return true;
}
static bool server_address(void *ctx, int32_t source, int32_t index, int32_t capacity,
    const qa_q3_host_browser_writer *writer, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_row *row;
    if (!access_current(ctx, e) || !row_read(a->browser, source, index, &row, e)) return false;
    if (!row) return true;
    if (capacity < 1) return fail(e, "Q_strncpyz: destsize < 1");
    char address[256] = "bot";
    if (row->addressed && !qa_net_address_format(&row->address, address, sizeof(address), e)) return false;
    return writer && writer->write && writer->write(writer->context, address, e) && access_current(ctx, e);
}
static bool server_info(void *ctx, int32_t source, int32_t index, int32_t capacity,
    const qa_q3_host_browser_writer *writer, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_row *row;
    if (!access_current(ctx, e) || !row_read(a->browser, source, index, &row, e)) return false;
    if (!row || !writer) return true;
    char info[1024];
    if (!row_info(a->browser, row, info, e) || !access_current(ctx, e)) return false;
    if (capacity < 1) return fail(e, "Q_strncpyz: destsize < 1");
    return writer->write && writer->write(writer->context, info, e) && access_current(ctx, e);
}
static bool server_ping(void *ctx, int32_t source, int32_t index, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_row *row;
    if (!out || !access_current(ctx, e) || !row_read(a->browser, source, index, &row, e)) return false;
    *out = row ? word(row->ping) : -1; return true;
}
static bool server_visible(void *ctx, int32_t source, int32_t index, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_row *row;
    if (!out || !access_current(ctx, e) || !row_read(a->browser, source, index, &row, e)) return false;
    *out = row ? row->visible : 0; return true;
}
static bool mark_visible(void *ctx, int32_t source, int32_t index, int32_t value, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    if (list) {
        if (index == -1) for (uint32_t i = 0; i < list->capacity; ++i) list->rows[i].visible = value;
        else if (index >= 0 && (uint32_t)index < list->capacity) list->rows[index].visible = value;
    }
    return true;
}
static bool reset_pings(void *ctx, int32_t source, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    if (list) for (uint32_t i = 0; i < list->capacity; ++i) list->rows[i].ping = -1;
    return true;
}
static bool resolve(frontend_q3_browser *b, const char *text, uint16_t port, qa_net_address *out, bool *present, qa_error *e)
{
    if (!current(b, e) || !b->options.resolve(b->options.context, text, port, out, present, e)) return false;
    if (!current(b, e)) return false;
    if (*present && (out->kind != QA_NET_IPV4 || !out->port ||
        (out->host.ipv4[0] == 255 && out->host.ipv4[1] == 255 && out->host.ipv4[2] == 255 && out->host.ipv4[3] == 255))) *present = false;
    return true;
}
static bool read_text(frontend_q3_browser_access *a, const qa_q3_host_browser_reader *reader, qa_buffer *out, qa_error *e)
{
    if (!reader || !reader->read || !reader->read(reader->context, out, e)) return false;
    if (!access_current(a, e) || !out->data || !out->size || !memchr(out->data, 0, out->size)) {
        qa_buffer_free(out); return fail(e, "Q3 browser reader returned stale or unterminated text");
    }
    return true;
}
static uint32_t source_bit(int32_t source)
{
    static const uint32_t bits[] = {QA_SERVER_LAN, QA_SERVER_SECONDARY_MASTER, QA_SERVER_MASTER, QA_SERVER_FAVORITE};
    return bits[source];
}
static bool add_server(void *ctx, int32_t source, const qa_q3_host_browser_reader *name_reader,
    const qa_q3_host_browser_reader *address_reader, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!out || !access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    *out = -1; if (!list || list->count == list->capacity) return true;
    qa_buffer address = {0};
    if (!read_text(a, address_reader, &address, e)) return false;
    qa_net_address resolved; bool present;
    bool ok = resolve(a->browser, (char *)address.data, 27960, &resolved, &present, e);
    qa_buffer_free(&address);
    if (!ok || !access_current(ctx, e)) return false;
    if (!present) return fail(e, "Invalid Q3 server address");
    if (row_contains(list->rows, list->count, &resolved)) { *out = 0; return true; }
    browser_row *target = &list->rows[list->count];
    /* This address store genuinely precedes the source's lazy name read. */
    target->address = resolved; target->addressed = true;
    qa_buffer name = {0};
    if (!read_text(a, name_reader, &name, e)) return false;
    (void)snprintf(target->name, sizeof(target->name), "%.*s", 31, (char *)name.data);
    qa_buffer_free(&name); target->visible = 1;
    if (!qa_server_browser_add(a->browser->options.browser, &resolved, (qa_net_protocol_id){QA_NET_Q3_68,0,0}, source_bit(source), e)) return false;
    ++list->count;
    if (source == 2 && !contains(a->browser->seen, a->browser->seen_count, &resolved))
        a->browser->seen[a->browser->seen_count++] = resolved;
    qa_browser_q3_list canonical;
    if (!qa_server_browser_q3_list_read(a->browser->options.browser, source, &canonical, e)) return false;
    list->generation = canonical.generation; list->reset_generation = canonical.reset_generation; *out = 1; return true;
}
static bool remove_server(void *ctx, int32_t source, const qa_q3_host_browser_reader *reader, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    if (!list) return true;
    qa_buffer text = {0}; if (!read_text(a, reader, &text, e)) return false;
    qa_net_address address; bool present;
    bool ok = resolve(a->browser, (char *)text.data, 27960, &address, &present, e); qa_buffer_free(&text);
    if (!ok || !access_current(ctx, e)) return false;
    if (!present) return true;
    for (uint32_t i = 0; i < list->count; ++i) if (list->rows[i].addressed && same(&list->rows[i].address, &address)) {
        if (!qa_server_browser_remove_source(a->browser->options.browser, &address,
            (qa_net_protocol_id){QA_NET_Q3_68,0,0}, source_bit(source), e)) return false;
        memmove(&list->rows[i], &list->rows[i + 1], (list->count - i - 1) * sizeof(*list->rows)); --list->count;
        if (source == 2) for (uint32_t j = 0; j < a->browser->seen_count; ++j) if (same(&a->browser->seen[j], &address)) {
            memmove(&a->browser->seen[j], &a->browser->seen[j + 1], (a->browser->seen_count - j - 1) * sizeof(*a->browser->seen)); --a->browser->seen_count; break;
        }
        qa_browser_q3_list canonical;
        if (!qa_server_browser_q3_list_read(a->browser->options.browser, source, &canonical, e)) return false;
        list->generation = canonical.generation; break;
    }
    return true;
}
static bool ping_slot(frontend_q3_browser_access *a, int32_t index, browser_ping **out, qa_error *e)
{
    if (!access_current(a, e)) return false;
    if (index < 0 || index >= 32) return fail(e, "Q3 ping index is outside its 32 source slots");
    *out = &a->browser->pings[index]; return true;
}
static bool ping_count(void *ctx, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx;
    if (!out || !access_current(ctx, e)) return false;
    *out = 0; for (size_t i = 0; i < 32; ++i) if (a->browser->pings[i].addressed) ++*out;
    return true;
}
static bool clear_ping(void *ctx, int32_t index, qa_error *e)
{
    frontend_q3_browser_access *a = ctx;
    if (!access_current(ctx, e)) return false;
    if (index < 0 || index >= 32) return true;
    browser_ping *slot = &a->browser->pings[index];
    if (!qa_server_browser_q3_release(a->browser->options.browser, slot->request, e)) return false;
    slot->addressed = false; slot->request = 0; return true;
}
static bool ping_time(frontend_q3_browser *b, browser_ping *slot, double *out, qa_error *e)
{
    *out = 0; if (!slot->request) return true;
    const qa_browser_q3_result *result;
    if (!qa_server_browser_q3_result(b->options.browser, slot->request, &result, e)) return false;
    if (!result->completed) return true;
    char info[1024];
    if (!memchr(result->entry.rules, 0, sizeof(result->entry.rules)))
        return fail(e, "Q3 ping result lost its retained ordered rule extent");
    if (strlen(result->entry.rules) >= sizeof(info))
        return fail(e, "Info_SetValueForKey: oversize infostring");
    strcpy(info, result->entry.rules);
    if (!numeric(b, info, "nettype", 1, e)) return false;
    strcpy(slot->info, info); *out = elapsed(result->completed_ns, result->sent_ns) + 1; return true;
}
static bool publish_ping(frontend_q3_browser *b, const qa_net_address *address, double ping, qa_error *e)
{
    for (int32_t source = 0; source < 4; ++source) {
        browser_list *list; if (!list_read(b, source, &list, e)) return false;
        for (uint32_t i = 0; i < list->capacity; ++i)
            if (list->rows[i].addressed && same(&list->rows[i].address, address)) list->rows[i].ping = ping;
    }
    return true;
}
static bool get_ping(void *ctx, int32_t index, int32_t capacity,
    const qa_q3_host_browser_writer *writer, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_ping *slot;
    if (!out || !writer || !writer->write || !ping_slot(a, index, &slot, e)) return false;
    if (!slot->addressed) { if (!writer->write(writer->context, NULL, e)) return false; *out = 0; return true; }
    if (capacity < 1) return fail(e, "Q_strncpyz: destsize < 1");
    char address[256];
    if (!qa_net_address_format(&slot->address, address, sizeof(address), e) ||
        !writer->write(writer->context, address, e) || !access_current(ctx, e)) return false;
    double measured; if (!ping_time(a->browser, slot, &measured, e) || !access_current(ctx, e)) return false;
    double duration = elapsed(a->browser->options.now_ns(a->browser->options.context), slot->start);
    const qa_cvar_view *maximum = qa_cvars_find(a->cvars, "cl_maxPing");
    double threshold = maximum && maximum->number > 100 ? maximum->number : 100;
    *out = word(measured != 0 ? measured : duration < threshold ? 0 : duration);
    return publish_ping(a->browser, &slot->address, measured, e);
}
static bool ping_info(void *ctx, int32_t index, int32_t capacity,
    const qa_q3_host_browser_writer *writer, bool *present, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_ping *slot;
    if (!present || !writer || !writer->write || !ping_slot(a, index, &slot, e)) return false;
    *present = slot->addressed;
    if (!slot->addressed) return !capacity || writer->write(writer->context, NULL, e);
    double ignored; if (!ping_time(a->browser, slot, &ignored, e) || !access_current(ctx, e)) return false;
    if (capacity < 1) return fail(e, "Q_strncpyz: destsize < 1");
    return writer->write(writer->context, slot->info, e) && access_current(ctx, e);
}
static bool start_ping(frontend_q3_browser *b, browser_ping *slot, const qa_net_address *address, qa_error *e)
{
    if (!qa_server_browser_q3_release(b->options.browser, slot->request, e)) return false;
    slot->address = *address; slot->addressed = true; slot->start = b->options.now_ns(b->options.context); slot->published = false; slot->request = 0;
    if (!qa_server_browser_q3_request(b->options.browser, address, QA_BROWSER_Q3_INFO, slot->start, &slot->request, e)) {
        slot->addressed = false; return false;
    }
    return true;
}
static bool local_address_text(void *ctx, const char *text, qa_error *e)
{
    (void)e; char *address = ctx;
    if (!text) address[0] = 0;
    else (void)snprintf(address, 256, "%s", text);
    return true;
}
static bool update_pings(void *ctx, int32_t source, bool *active, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_list *list;
    if (!active || !access_current(ctx, e) || !list_read(a->browser, source, &list, e)) return false;
    *active = false; if (!list) return true;
    int32_t count; if (!ping_count(ctx, &count, e)) return false;
    for (uint32_t i = 0; i < list->count && count < 32; ++i) {
        browser_row *row = &list->rows[i]; if (!row->visible) continue;
        if (source == 2 && row->ping == 0 && a->browser->overflow_count) {
            row->address = a->browser->overflow[--a->browser->overflow_count]; row->addressed = true; row->name[0] = 0; row->ping = -1; continue;
        }
        if (!row->addressed || row->ping != -1) continue;
        bool queued = false;
        for (size_t j = 0; j < 32; ++j) if (a->browser->pings[j].addressed && same(&a->browser->pings[j].address, &row->address)) queued = true;
        if (queued) continue;
        for (size_t j = 0; j < 32; ++j) if (!a->browser->pings[j].addressed) {
            if (!start_ping(a->browser, &a->browser->pings[j], &row->address, e)) return false;
            ++count; *active = true; break;
        }
    }
    if (count) *active = true;
    char address[256]; qa_q3_host_browser_writer writer = {address, local_address_text};
    for (int32_t i = 0; i < 32; ++i) if (a->browser->pings[i].addressed) {
        int32_t time; if (!get_ping(ctx, i, 256, &writer, &time, e)) return false;
        if (time && !clear_ping(ctx, i, e)) return false;
    }
    return true;
}
static bool manual_ping(frontend_q3_browser_access *a, const char *remote, qa_error *e)
{
    if (!a || !access_current(a, e)) return false;
    qa_net_address address; bool present;
    if (!resolve(a->browser, remote, 27960, &address, &present, e) || !access_current(a, e)) return false;
    if (!present) return true;
    uint64_t now = a->browser->options.now_ns(a->browser->options.context);
    browser_ping *chosen = NULL;
    for (size_t i = 0; i < 32; ++i) {
        browser_ping *slot = &a->browser->pings[i];
        if (!slot->addressed) { chosen = slot; break; }
        double measured;
        if (!ping_time(a->browser, slot, &measured, e) || !access_current(a, e)) return false;
        if (measured == 0) {
            if (elapsed(now, slot->start) >= 500) { chosen = slot; break; }
        } else {
            if (!ping_time(a->browser, slot, &measured, e) || !access_current(a, e)) return false;
            if (measured >= 500) { chosen = slot; break; }
        }
    }
    if (!chosen) {
        chosen = &a->browser->pings[0];
        for (size_t i = 1; i < 32; ++i) if (a->browser->pings[i].start < chosen->start) chosen = &a->browser->pings[i];
    }
    return start_ping(a->browser, chosen, &address, e) && publish_ping(a->browser, &address, 0, e);
}
static browser_status *status_slot(frontend_q3_browser *b, const qa_net_address *address)
{
    for (size_t i = 0; i < 16; ++i) if (b->statuses[i].addressed && same(&b->statuses[i].address, address)) return &b->statuses[i];
    for (size_t i = 0; i < 16; ++i) if (b->statuses[i].retrieved) return &b->statuses[i];
    browser_status *oldest = &b->statuses[0];
    for (size_t i = 1; i < 16; ++i) if (b->statuses[i].start < oldest->start) oldest = &b->statuses[i];
    return oldest;
}
static bool start_status(frontend_q3_browser *b, browser_status *slot, const qa_net_address *address, qa_error *e)
{
    if (!qa_server_browser_q3_release(b->options.browser, slot->request, e)) return false;
    slot->address = *address; slot->addressed = true; slot->retrieved = false; slot->start = b->options.now_ns(b->options.context); slot->request = 0;
    if (!qa_server_browser_q3_request(b->options.browser, address, QA_BROWSER_Q3_STATUS, slot->start, &slot->request, e)) {
        slot->retrieved = true; return false;
    }
    return true;
}
static bool server_status(void *ctx, const char *remote, int32_t capacity,
    const qa_q3_host_browser_writer *writer, bool *present, qa_error *e)
{
    frontend_q3_browser_access *a = ctx;
    if (!present || !access_current(ctx, e)) return false;
    *present = false;
    if (!remote) {
        for (size_t i = 0; i < 16; ++i) {
            browser_status *slot = &a->browser->statuses[i];
            if (!qa_server_browser_q3_release(a->browser->options.browser, slot->request, e)) return false;
            slot->request = 0; slot->addressed = false; slot->retrieved = true; slot->print = false;
        }
        return true;
    }
    qa_net_address address; bool resolved;
    if (!resolve(a->browser, remote, 27960, &address, &resolved, e) || !access_current(ctx, e)) return false;
    if (!resolved) return true;
    if (!writer) {
        for (size_t i = 0; i < 16; ++i) {
            browser_status *slot = &a->browser->statuses[i];
            if (slot->addressed && same(&slot->address, &address)) {
                if (!qa_server_browser_q3_release(a->browser->options.browser, slot->request, e)) return false;
                slot->request = 0; slot->addressed = false; slot->retrieved = true; slot->print = false;
            }
        }
        return true;
    }
    browser_status *slot = status_slot(a->browser, &address);
    bool matching = slot->addressed && same(&slot->address, &address);
    if (!matching && !slot->retrieved) return true;
    const qa_browser_q3_result *result = NULL;
    if (matching && slot->request && !qa_server_browser_q3_result(a->browser->options.browser, slot->request, &result, e)) return false;
    if (result && result->completed) {
        if (capacity < 1) return fail(e, "Q_strncpyz: destsize < 1");
        if (!writer->write || !writer->write(writer->context, result->status, e) || !access_current(ctx, e)) return false;
        slot->retrieved = true; slot->start = 0; *present = true; return true;
    }
    const qa_cvar_view *resend = qa_cvars_find(a->cvars, "cl_serverStatusResendTime");
    double interval = resend ? resend->number : 0;
    double duration = (double)a->browser->options.now_ns(a->browser->options.context) / 1000000.0 - (double)slot->start / 1000000.0;
    if (!matching || !slot->request || duration > interval) {
        slot->print = false; if (!start_status(a->browser, slot, &address, e)) return false;
    }
    return true;
}
static bool browser_status_command(frontend_q3_browser_access *a, const char *remote, qa_error *e)
{
    if (!access_current(a, e)) return false;
    qa_net_address address; bool present;
    if (!resolve(a->browser, remote, 27960, &address, &present, e) || !access_current(a, e)) return false;
    if (!present) return true;
    browser_status *slot = status_slot(a->browser, &address);
    if (!start_status(a->browser, slot, &address, e)) return false;
    slot->print = true; return true;
}
static int compare_text(const char *a, const char *b)
{
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        int8_t sx, sy; memcpy(&sx, &x, 1); memcpy(&sy, &y, 1);
        if (sx != sy) return sx < sy ? -1 : 1;
        if (!x) return 0;
    }
}
static bool compare_servers(void *ctx, int32_t source, int32_t key, int32_t direction,
    int32_t first, int32_t second, int32_t *out, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; browser_row *x, *y;
    if (!out || !access_current(ctx, e) || !row_read(a->browser, source, first, &x, e) || !row_read(a->browser, source, second, &y, e)) return false;
    *out = 0; if (!x || !y) return true;
    qa_server_entry xe = {0}, ye = {0};
    if (x->addressed) (void)qa_server_browser_q3_entry(a->browser->options.browser, &x->address, &xe);
    if (y->addressed) (void)qa_server_browser_q3_entry(a->browser->options.browser, &y->address, &ye);
    if (key == 0) {
        char xa[256] = "", ya[256] = "", xn[32], yn[32];
        if (x->addressed && !qa_net_address_format(&x->address, xa, sizeof(xa), e)) return false;
        if (y->addressed && !qa_net_address_format(&y->address, ya, sizeof(ya), e)) return false;
        (void)snprintf(xn, sizeof(xn), "%.*s", 31, xe.available && *xe.name ? xe.name : *x->name ? x->name : xa);
        (void)snprintf(yn, sizeof(yn), "%.*s", 31, ye.available && *ye.name ? ye.name : *y->name ? y->name : ya);
        *out = compare_text(xn, yn);
    } else if (key == 1) {
        char xm[32], ym[32]; (void)snprintf(xm, sizeof(xm), "%.*s", 31, xe.available ? xe.map : "");
        (void)snprintf(ym, sizeof(ym), "%.*s", 31, ye.available ? ye.map : ""); *out = compare_text(xm, ym);
    } else if (key >= 2 && key <= 4) {
        double xv = 0, yv = 0;
        if (key == 2) { xv = xe.available ? xe.players : 0; yv = ye.available ? ye.players : 0; }
        else if (key == 3) { char text[8193]; rule(xe.available ? xe.rules : "", "gametype", text, sizeof(text)); xv = integer(text); rule(ye.available ? ye.rules : "", "gametype", text, sizeof(text)); yv = integer(text); }
        else { xv = x->ping; yv = y->ping; }
        *out = xv < yv ? -1 : xv > yv ? 1 : 0;
    }
    if (direction) *out = -*out;
    return true;
}
bool frontend_q3_browser_create(const frontend_q3_browser_options *options, frontend_q3_browser **out, qa_error *e)
{
    if (!options || !options->browser || !options->current || !options->now_ns || !options->resolve ||
        !options->cache_read || !options->cache_write || !options->print || !out || *out || !options->current(options->context, e))
        return fail(e, "Incomplete physical Q3 browser options");
    frontend_q3_browser *b = calloc(1, sizeof(*b));
    if (!b) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Constructing Q3 UI browser view"); return false; }
    b->options = *options;
    for (size_t i = 0; i < 4; ++i) {
        b->lists[i].capacity = i == 2 ? 4096 : 128;
        b->lists[i].rows = calloc(b->lists[i].capacity, sizeof(*b->lists[i].rows));
        if (!b->lists[i].rows) { frontend_q3_browser_destroy(b); qa_error_set(e, QA_ERROR_MEMORY, 0, "Constructing source LAN rows"); return false; }
    }
    for (size_t i = 0; i < 16; ++i) b->statuses[i].retrieved = true;
    *out = b; return true;
}
void frontend_q3_browser_destroy(frontend_q3_browser *b)
{
    if (!b || b->busy) return;
    for (size_t i = 0; i < 32; ++i) (void)qa_server_browser_q3_release(b->options.browser, b->pings[i].request, NULL);
    for (size_t i = 0; i < 16; ++i) (void)qa_server_browser_q3_release(b->options.browser, b->statuses[i].request, NULL);
    for (size_t i = 0; i < 4; ++i) free(b->lists[i].rows);
    free(b);
}
static bool browser_scan(frontend_q3_browser *b, qa_error *e)
{
    if (!current(b, e) || !qa_server_browser_q3_scan(b->options.browser, b->options.now_ns(b->options.context), e)) return false;
    browser_list *list; return list_read(b, 0, &list, e);
}
static bool browser_master(frontend_q3_browser *b, int32_t source, const char *remote,
    int32_t protocol, const char *const *keywords, size_t count, qa_error *e)
{
    if (!current(b, e)) return false;
    qa_net_address address; bool present;
    if (!resolve(b, remote, 27950, &address, &present, e)) return false;
    if (!present) return fail(e, "Invalid Q3 master address");
    if (!qa_server_browser_q3_master(b->options.browser, source, &address, protocol, keywords, count,
        b->options.now_ns(b->options.context), e)) return false;
    browser_list *list; return list_read(b, source, &list, e);
}
static bool browser_poll(frontend_q3_browser *b, qa_error *e)
{
    if (!current(b, e)) return false;
    for (size_t i = 0; i < 32; ++i) {
        browser_ping *slot = &b->pings[i]; if (!slot->addressed || !slot->request || slot->published) continue;
        const qa_browser_q3_result *result;
        if (!qa_server_browser_q3_result(b->options.browser, slot->request, &result, e)) return false;
        if (result->completed) {
            double time; if (!ping_time(b, slot, &time, e) || !publish_ping(b, &slot->address, time, e)) return false;
            slot->published = true;
        }
    }
    for (size_t i = 0; i < 16; ++i) {
        browser_status *slot = &b->statuses[i]; if (!slot->addressed || !slot->request || !slot->print) continue;
        const qa_browser_q3_result *result;
        if (!qa_server_browser_q3_result(b->options.browser, slot->request, &result, e)) return false;
        if (!result->completed) continue;
        if (!b->options.print(b->options.context, "Server settings:\n", e)) return false;
        const char *p = result->entry.rules; if (*p == '\\') ++p;
        while (*p) {
            const char *key = p; while (*p && *p != '\\') ++p; size_t n = (size_t)(p - key); if (!*p) break;
            const char *value = ++p; while (*p && *p != '\\') ++p;
            char line[16416]; (void)snprintf(line, sizeof(line), "%-24.*s%.*s\n", (int)n, key, (int)(p - value), value);
            if (!b->options.print(b->options.context, line, e)) return false;
            if (*p) ++p;
        }
        if (!b->options.print(b->options.context, "\nPlayers:\nnum: score: ping: name:\n", e)) return false;
        for (uint32_t j = 0; j < result->player_count; ++j) {
            const qa_browser_q3_player *player = &result->players[j];
            char line[16416]; (void)snprintf(line, sizeof(line), "%-2u   %-3" PRId32 "    %-3" PRId32 "   \"%s\"\n",
                j, player->score, player->ping, result->names + player->name);
            if (!b->options.print(b->options.context, line, e)) return false;
        }
        slot->print = false; slot->retrieved = true;
    }
    return current(b, e);
}
static bool address_valid(const qa_net_address *address)
{ return (address->kind == QA_NET_IPV4 || address->kind == QA_NET_IPV6) && address->port && service_address_valid(address); }
static bool request_valid(frontend_q3_browser *b, uint64_t id, const qa_net_address *address,
    qa_browser_q3_request_kind kind, uint64_t start, qa_error *e)
{
    if (!id) return true;
    const qa_browser_q3_result *result;
    return qa_server_browser_q3_result(b->options.browser, id, &result, e) &&
        same(address, &result->address) && result->kind == kind && result->sent_ns == start;
}
static bool view_valid(frontend_q3_browser *b, qa_error *e)
{
    if (!b || b->busy || !current(b, e) || b->seen_count > 8192 || b->overflow_count > 4096) return false;
    for (int32_t s = 0; s < 4; ++s) {
        browser_list *list = &b->lists[s]; qa_browser_q3_list canonical;
        if (!list->rows || list->capacity != (s == 2 ? 4096u : 128u) || list->count > list->capacity ||
            !qa_server_browser_q3_list_read(b->options.browser, s, &canonical, e) ||
            list->generation > canonical.generation || list->reset_generation > canonical.reset_generation ||
            list->reset_generation > list->generation || (!list->synchronized && list->count)) return false;
        for (uint32_t i = 0; i < list->capacity; ++i) {
            const browser_row *row = &list->rows[i];
            if (!memchr(row->name, 0, sizeof(row->name)) || !isfinite(row->ping) || trunc(row->ping) != row->ping ||
                (row->addressed && !address_valid(&row->address)) ||
                (i < list->count && !row->addressed)) return false;
            if (i < list->count) for (uint32_t j = 0; j < i; ++j)
                if (same(&row->address, &list->rows[j].address)) return false;
        }
    }
    for (uint32_t i = 0; i < b->seen_count; ++i) {
        if (!address_valid(&b->seen[i]) || contains(b->seen, i, &b->seen[i])) return false;
    }
    for (uint32_t i = 0; i < b->overflow_count; ++i) {
        if (!address_valid(&b->overflow[i]) || contains(b->overflow, i, &b->overflow[i]) ||
            !contains(b->seen, b->seen_count, &b->overflow[i])) return false;
    }
    uint64_t ids[48]; size_t count = 0;
    for (size_t i = 0; i < 32; ++i) {
        browser_ping *p = &b->pings[i];
        if (!memchr(p->info, 0, sizeof(p->info)) || (p->addressed && !address_valid(&p->address)) ||
            (p->request && !p->addressed) || !request_valid(b, p->request, &p->address, QA_BROWSER_Q3_INFO, p->start, e)) return false;
        if (p->request) ids[count++] = p->request;
    }
    for (size_t i = 0; i < 16; ++i) {
        browser_status *p = &b->statuses[i];
        if ((p->addressed && !address_valid(&p->address)) || (p->request && !p->addressed)) return false;
        if (p->request) {
            const qa_browser_q3_result *result;
            if (!qa_server_browser_q3_result(b->options.browser, p->request, &result, e) ||
                !same(&p->address, &result->address) || result->kind != QA_BROWSER_Q3_STATUS ||
                (p->start != result->sent_ns && !(p->retrieved && !p->start && result->completed))) return false;
            ids[count++] = p->request;
        }
    }
    for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < i; ++j) if (ids[i] == ids[j]) return false;
    return true;
}
static bool row_write(qa_net_writer *w, const browser_row *row)
{
    return qa_net_write_u8(w, row->addressed) && q3_save_address(w, &row->address) &&
        qa_net_write_string(w, row->name) && qa_net_write_f64(w, row->ping) && qa_net_write_i32(w, row->visible);
}
static bool row_read_saved(qa_net_reader *r, browser_row *row)
{
    row->addressed = q3_save_bool(r);
    if (!q3_restore_address(r, &row->address) || !qa_net_read_string(r, row->name, sizeof(row->name))) return false;
    row->ping = qa_net_read_f64(r); row->visible = qa_net_read_i32(r); return !r->failed;
}
bool frontend_q3_browser_checkpoint(const frontend_q3_browser *owner, qa_buffer *out, qa_error *e)
{
    frontend_q3_browser *b = (frontend_q3_browser *)owner;
    if (!out || out->data || out->size || !view_valid(b, e)) return fail(e, "Invalid or busy Q3 browser continuation");
    size_t capacity = 1048576; uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding Q3 browser view"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, e);
    bool ok = qa_net_write_data(&w, "Q3BV", 4);
    for (size_t s = 0; ok && s < 4; ++s) {
        const browser_list *list = &b->lists[s];
        ok = qa_net_write_u32(&w, list->count) && qa_net_write_u64(&w, list->generation) &&
            qa_net_write_u64(&w, list->reset_generation) && qa_net_write_u8(&w, list->synchronized) && qa_net_write_u8(&w, list->pending);
        for (uint32_t i = 0; ok && i < list->capacity; ++i) ok = row_write(&w, &list->rows[i]);
    }
    ok = ok && qa_net_write_u32(&w, b->seen_count);
    for (uint32_t i = 0; ok && i < b->seen_count; ++i) ok = q3_save_address(&w, &b->seen[i]);
    ok = ok && qa_net_write_u32(&w, b->overflow_count);
    for (uint32_t i = 0; ok && i < b->overflow_count; ++i) ok = q3_save_address(&w, &b->overflow[i]);
    for (size_t i = 0; ok && i < 32; ++i) {
        const browser_ping *p = &b->pings[i];
        ok = q3_save_address(&w, &p->address) && qa_net_write_u64(&w, p->request) && qa_net_write_u64(&w, p->start) &&
            qa_net_write_string(&w, p->info) && qa_net_write_u8(&w, p->addressed) && qa_net_write_u8(&w, p->published);
    }
    for (size_t i = 0; ok && i < 16; ++i) {
        const browser_status *p = &b->statuses[i];
        ok = q3_save_address(&w, &p->address) && qa_net_write_u64(&w, p->request) && qa_net_write_u64(&w, p->start) &&
            qa_net_write_u8(&w, p->addressed) && qa_net_write_u8(&w, p->retrieved) && qa_net_write_u8(&w, p->print);
    }
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
bool frontend_q3_browser_restore(const frontend_q3_browser_options *options, qa_bytes bytes,
    frontend_q3_browser **out, qa_error *e)
{
    if (!out || *out || !bytes.data || bytes.size > 1048576) return fail(e, "Invalid Q3 browser view import");
    qa_net_reader r; qa_net_reader_init(&r, bytes, e);
    if (qa_net_read_u32(&r) != UINT32_C(0x56423351)) return fail(e, "Q3 browser view schema differs");
    frontend_q3_browser *b = NULL;
    if (!frontend_q3_browser_create(options, &b, e)) return false;
    bool ok = true;
    for (size_t s = 0; ok && s < 4; ++s) {
        browser_list *list = &b->lists[s];
        list->count = qa_net_read_u32(&r); list->generation = qa_net_read_u64(&r); list->reset_generation = qa_net_read_u64(&r);
        list->synchronized = q3_save_bool(&r); list->pending = q3_save_bool(&r);
        for (uint32_t i = 0; ok && i < list->capacity; ++i) ok = row_read_saved(&r, &list->rows[i]);
    }
    b->seen_count = qa_net_read_u32(&r);
    if (b->seen_count > 8192) ok = qa_net_reader_fail(&r, "Q3 seen membership exceeds source capacity");
    for (uint32_t i = 0; ok && i < b->seen_count; ++i) ok = q3_restore_address(&r, &b->seen[i]);
    b->overflow_count = qa_net_read_u32(&r);
    if (b->overflow_count > 4096) ok = qa_net_reader_fail(&r, "Q3 overflow exceeds source capacity");
    for (uint32_t i = 0; ok && i < b->overflow_count; ++i) ok = q3_restore_address(&r, &b->overflow[i]);
    for (size_t i = 0; ok && i < 32; ++i) {
        browser_ping *p = &b->pings[i];
        ok = q3_restore_address(&r, &p->address); p->request = qa_net_read_u64(&r); p->start = qa_net_read_u64(&r);
        ok = ok && qa_net_read_string(&r, p->info, sizeof(p->info));
        p->addressed = q3_save_bool(&r); p->published = q3_save_bool(&r);
    }
    for (size_t i = 0; ok && i < 16; ++i) {
        browser_status *p = &b->statuses[i];
        ok = q3_restore_address(&r, &p->address); p->request = qa_net_read_u64(&r); p->start = qa_net_read_u64(&r);
        p->addressed = q3_save_bool(&r); p->retrieved = q3_save_bool(&r); p->print = q3_save_bool(&r);
    }
    if (!ok || !qa_net_reader_finish(&r) || !view_valid(b, e)) {
        /* An invalid candidate has never acquired the imported request leases. */
        for (size_t i = 0; i < 32; ++i) b->pings[i].request = 0;
        for (size_t i = 0; i < 16; ++i) b->statuses[i].request = 0;
        frontend_q3_browser_destroy(b); return fail(e, "Invalid Q3 browser view continuation");
    }
    *out = b; return true;
}
typedef struct cached_browser {
    qa_browser_q3_cached_entry *entries;
    uint32_t count;
    browser_list lists[3];
} cached_browser;
static void cache_free(cached_browser *cache)
{
    for (uint32_t i = 0; i < cache->count; ++i) free((void *)cache->entries[i].response.data);
    free(cache->entries);
    for (size_t i = 0; i < 3; ++i) free(cache->lists[i].rows);
    memset(cache, 0, sizeof(*cache));
}
static bool entry_write(qa_net_writer *w, const qa_browser_q3_cached_entry *saved)
{
    const qa_server_entry *entry = &saved->entry;
    return q3_save_address(w, &entry->address) && qa_net_write_u32(w, entry->sources) &&
        qa_net_write_u8(w, entry->available) && qa_net_write_u32(w, entry->players) && qa_net_write_u32(w, entry->maximum_players) &&
        qa_net_write_u64(w, entry->updated_ns) && qa_net_write_u64(w, entry->ping_ns) &&
        qa_net_write_u8(w, entry->has_ping) &&
        qa_net_write_string(w, entry->name) && qa_net_write_string(w, entry->map) && qa_net_write_string(w, entry->rules) &&
        qa_net_write_u32(w, (uint32_t)saved->response.size) && qa_net_write_data(w, saved->response.data, saved->response.size);
}
static bool entry_read(qa_net_reader *r, qa_browser_q3_cached_entry *saved)
{
    qa_server_entry *entry = &saved->entry; entry->protocol = (qa_net_protocol_id){QA_NET_Q3_68,0,0};
    if (!q3_restore_address(r, &entry->address)) return false;
    entry->sources = qa_net_read_u32(r); entry->available = q3_save_bool(r);
    entry->players = qa_net_read_u32(r); entry->maximum_players = qa_net_read_u32(r);
    entry->updated_ns = qa_net_read_u64(r); entry->ping_ns = qa_net_read_u64(r);
    entry->has_ping = q3_save_bool(r);
    if (!qa_net_read_string(r, entry->name, sizeof(entry->name)) || !qa_net_read_string(r, entry->map, sizeof(entry->map)) ||
        !qa_net_read_string(r, entry->rules, sizeof(entry->rules))) return false;
    uint32_t size = qa_net_read_u32(r);
    if (r->failed || size > QA_Q3_MESSAGE_BYTES || size > qa_net_reader_remaining(r))
        return qa_net_reader_fail(r, "Cached Q3 response exceeds source datagram");
    if (size) {
        uint8_t *bytes = malloc(size);
        if (!bytes) { qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Importing cached complete Q3 response"); r->failed = true; return false; }
        saved->response = (qa_bytes){bytes, size};
        if (!qa_net_read_data(r, bytes, size)) return false;
    }
    return qa_server_browser_q3_cached_entry_validate(saved, r->error);
}
static bool cache_decode(qa_bytes bytes, cached_browser *cache, qa_error *e)
{
    if (!bytes.data || bytes.size > 8388608) return fail(e, "Q3 browser cache exceeds source extent");
    qa_net_reader r; qa_net_reader_init(&r, bytes, e);
    if (qa_net_read_u32(&r) != UINT32_C(0x43563351)) return fail(e, "Q3 browser cache schema differs");
    cache->count = qa_net_read_u32(&r);
    if (r.failed || cache->count > 16384 || cache->count > qa_net_reader_remaining(&r) / 40) {
        cache->count = 0; return qa_net_reader_fail(&r, "Cached Q3 entries exceed encoded extent");
    }
    cache->entries = calloc(cache->count ? cache->count : 1, sizeof(*cache->entries));
    if (!cache->entries) { cache->count = 0; qa_error_set(e, QA_ERROR_MEMORY, 0, "Decoding Q3 cache records"); return false; }
    uint32_t master = 0, secondary = 0;
    for (uint32_t i = 0; i < cache->count; ++i) {
        if (!entry_read(&r, &cache->entries[i])) return false;
        const qa_server_entry *entry = &cache->entries[i].entry;
        if (entry->sources & QA_SERVER_MASTER) ++master;
        if (entry->sources & QA_SERVER_SECONDARY_MASTER) ++secondary;
        for (uint32_t j = 0; j < i; ++j) if (same(&entry->address, &cache->entries[j].entry.address))
            return qa_net_reader_fail(&r, "Duplicate Q3 cache endpoint");
    }
    if (master > 8192 || secondary > 128) return qa_net_reader_fail(&r, "Cached master membership exceeds source capacity");
    for (int32_t s = 1; s < 4; ++s) {
        browser_list *list = &cache->lists[s - 1]; list->capacity = s == 2 ? 4096 : 128;
        list->count = qa_net_read_u32(&r);
        if (r.failed || list->count > list->capacity) return qa_net_reader_fail(&r, "Cached UI rows exceed source capacity");
        list->rows = calloc(list->capacity, sizeof(*list->rows));
        if (!list->rows) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Decoding cached Q3 UI rows"); return false; }
        for (uint32_t i = 0; i < list->count; ++i) {
            browser_row *row = &list->rows[i]; bool member = false;
            if (!row_read_saved(&r, row) || !row->addressed || row->address.kind != QA_NET_IPV4 || !row->address.port ||
                !isfinite(row->ping) || trunc(row->ping) != row->ping || row->ping < INT32_MIN || row->ping > INT32_MAX)
                return qa_net_reader_fail(&r, "Invalid cached Q3 UI row");
            for (uint32_t j = 0; j < i; ++j) if (same(&row->address, &list->rows[j].address))
                return qa_net_reader_fail(&r, "Duplicate cached Q3 UI row");
            for (uint32_t j = 0; j < cache->count; ++j) if (same(&row->address, &cache->entries[j].entry.address) &&
                (cache->entries[j].entry.sources & source_bit(s))) { member = true; break; }
            if (!member) return qa_net_reader_fail(&r, "Cached Q3 UI row has no canonical membership");
        }
    }
    return qa_net_reader_finish(&r);
}
static bool save_cache(void *ctx, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; if (!access_current(a, e)) return false;
    frontend_q3_browser *b = a->browser;
    for (int32_t s = 1; s < 4; ++s) { browser_list *list; if (!list_read(b, s, &list, e)) return false; }
    qa_net_address *addresses = calloc(16384, sizeof(*addresses)); uint64_t *orders = calloc(16384, sizeof(*orders));
    uint8_t *data = malloc(8388608);
    if (!addresses || !orders || !data) { free(addresses); free(orders); free(data); qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding Q3 server cache"); return false; }
    uint32_t count = 0; bool ok = true;
    for (int32_t s = 1; ok && s < 4; ++s) {
        qa_browser_q3_list list; ok = qa_server_browser_q3_list_read(b->options.browser, s, &list, e);
        for (uint32_t i = 0; ok && i < list.count; ++i) if (!contains(addresses, count, &list.addresses[i])) {
            if (count == 16384) { ok = fail(e, "Q3 cached endpoints exceed source capacity"); break; }
            qa_browser_q3_cached_entry saved;
            if (!qa_server_browser_q3_cached_entry_read(b->options.browser, &list.addresses[i], &saved, e)) { ok = false; break; }
            uint32_t position = count;
            while (position && orders[position - 1] > saved.insertion_order) {
                addresses[position] = addresses[position - 1]; orders[position] = orders[position - 1]; --position;
            }
            addresses[position] = list.addresses[i]; orders[position] = saved.insertion_order; ++count;
        }
    }
    qa_net_writer w; qa_net_writer_init(&w, data, 8388608, e);
    ok = ok && qa_net_write_data(&w, "Q3VC", 4) && qa_net_write_u32(&w, count);
    for (uint32_t i = 0; ok && i < count; ++i) {
        qa_browser_q3_cached_entry saved;
        ok = qa_server_browser_q3_cached_entry_read(b->options.browser, &addresses[i], &saved, e);
        if (ok) {
            saved.entry.sources &= QA_SERVER_MASTER | QA_SERVER_SECONDARY_MASTER | QA_SERVER_FAVORITE;
            saved.entry.pending = saved.entry.timed_out = false;
            ok = qa_server_browser_q3_cached_entry_validate(&saved, e) && entry_write(&w, &saved);
        }
    }
    for (int32_t s = 1; ok && s < 4; ++s) {
        browser_list *list = &b->lists[s]; ok = qa_net_write_u32(&w, list->count);
        for (uint32_t i = 0; ok && i < list->count; ++i) {
            browser_row *row = &list->rows[i];
            if (row->address.kind != QA_NET_IPV4 || row->ping < INT32_MIN || row->ping > INT32_MAX)
                ok = fail(e, "Q3 UI row cannot be represented by the source cache");
            else ok = row_write(&w, row);
        }
    }
    if (ok && !w.failed && access_current(a, e)) ok = b->options.cache_write(b->options.context, (qa_bytes){data, qa_net_writer_size(&w)}, e);
    else ok = false;
    free(addresses); free(orders); free(data); return ok && access_current(a, e);
}
static bool load_cache_owner(frontend_q3_browser *b, qa_error *e)
{
    if (!current(b, e)) return false;
    qa_buffer bytes = {0}; bool present = false;
    if (!b->options.cache_read(b->options.context, &bytes, &present, e)) { qa_buffer_free(&bytes); return false; }
    if (!current(b, e)) { qa_buffer_free(&bytes); return false; }
    cached_browser cache = {0}; bool ok = !present || cache_decode((qa_bytes){bytes.data, bytes.size}, &cache, e);
    qa_buffer_free(&bytes); if (!ok) { cache_free(&cache); return false; }
    if (!present) return true;
    /* Preserve genuine already-received status before source memberships clear. */
    for (uint32_t i = 0; ok && i < cache.count; ++i) {
        qa_browser_q3_cached_entry live; qa_error ignored = {0};
        if (!qa_server_browser_q3_cached_entry_read(b->options.browser, &cache.entries[i].entry.address, &live, &ignored) || !live.entry.available) continue;
        uint8_t *response = malloc(live.response.size);
        if (!response) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining live Q3 status across cache import"); ok = false; break; }
        memcpy(response, live.response.data, live.response.size);
        uint32_t sources = cache.entries[i].entry.sources;
        free((void *)cache.entries[i].response.data);
        cache.entries[i] = (qa_browser_q3_cached_entry){.entry=live.entry,
            .response={response,live.response.size},.insertion_order=live.insertion_order};
        cache.entries[i].entry.sources = sources; cache.entries[i].entry.pending = cache.entries[i].entry.timed_out = false;
    }
    if (ok) { qa_server_browser_cancel_master(b->options.browser); for (int32_t s = 1; ok && s < 4; ++s) ok = qa_server_browser_q3_clear(b->options.browser, s, e); }
    for (uint32_t i = 0; ok && i < cache.count; ++i) ok = qa_server_browser_q3_restore_entry(b->options.browser, &cache.entries[i], e);
    for (int32_t s = 1; ok && s < 4; ++s) {
        browser_list *list; ok = list_read(b, s, &list, e); if (!ok) break;
        browser_list *saved = &cache.lists[s - 1];
        if (s == 2) {
            list->count = saved->count;
            memcpy(list->rows, saved->rows, list->capacity * sizeof(*list->rows));
            qa_browser_q3_list canonical; ok = qa_server_browser_q3_list_read(b->options.browser, s, &canonical, e);
            if (ok) { b->seen_count = canonical.count; memcpy(b->seen, canonical.addresses, canonical.count * sizeof(*b->seen)); b->overflow_count = 0; }
        } else for (uint32_t i = 0; i < list->capacity; ++i) {
            if (i >= list->count) { list->rows[i] = (browser_row){0}; continue; }
            for (uint32_t j = 0; j < saved->count; ++j) if (same(&list->rows[i].address, &saved->rows[j].address)) { list->rows[i] = saved->rows[j]; break; }
        }
    }
    cache_free(&cache); return ok && current(b, e);
}
static bool load_cache(void *ctx, qa_error *e)
{
    frontend_q3_browser_access *a = ctx;
    return access_current(a, e) && load_cache_owner(a->browser, e) && access_current(a, e);
}
static bool enter_access(void *ctx, qa_error *e)
{
    frontend_q3_browser_access *a = ctx;
    if (!access_current(ctx, e)) return false;
    if (a->browser->busy) return fail(e, "Recursive Q3 UI browser operation");
    a->browser->busy = true; return true;
}
static bool leave_access(void *ctx, bool ok, qa_error *e)
{
    frontend_q3_browser_access *a = ctx; a->browser->busy = false;
    return ok && access_current(ctx, e);
}
#define BROWSER_CALL(name, signature, arguments) \
    static bool service_##name signature { \
        if (!enter_access(ctx, e)) return false; \
        bool ok = name arguments; return leave_access(ctx, ok, e); \
    }
BROWSER_CALL(server_count, (void *ctx, int32_t source, int32_t *out, qa_error *e), (ctx, source, out, e))
BROWSER_CALL(server_address, (void *ctx, int32_t source, int32_t index, int32_t capacity, const qa_q3_host_browser_writer *writer, qa_error *e), (ctx, source, index, capacity, writer, e))
BROWSER_CALL(server_info, (void *ctx, int32_t source, int32_t index, int32_t capacity, const qa_q3_host_browser_writer *writer, qa_error *e), (ctx, source, index, capacity, writer, e))
BROWSER_CALL(ping_count, (void *ctx, int32_t *out, qa_error *e), (ctx, out, e))
BROWSER_CALL(clear_ping, (void *ctx, int32_t index, qa_error *e), (ctx, index, e))
BROWSER_CALL(get_ping, (void *ctx, int32_t index, int32_t capacity, const qa_q3_host_browser_writer *writer, int32_t *time, qa_error *e), (ctx, index, capacity, writer, time, e))
BROWSER_CALL(ping_info, (void *ctx, int32_t index, int32_t capacity, const qa_q3_host_browser_writer *writer, bool *present, qa_error *e), (ctx, index, capacity, writer, present, e))
BROWSER_CALL(mark_visible, (void *ctx, int32_t source, int32_t index, int32_t value, qa_error *e), (ctx, source, index, value, e))
BROWSER_CALL(update_pings, (void *ctx, int32_t source, bool *active, qa_error *e), (ctx, source, active, e))
BROWSER_CALL(reset_pings, (void *ctx, int32_t source, qa_error *e), (ctx, source, e))
BROWSER_CALL(load_cache, (void *ctx, qa_error *e), (ctx, e))
BROWSER_CALL(save_cache, (void *ctx, qa_error *e), (ctx, e))
BROWSER_CALL(add_server, (void *ctx, int32_t source, const qa_q3_host_browser_reader *name, const qa_q3_host_browser_reader *address, int32_t *out, qa_error *e), (ctx, source, name, address, out, e))
BROWSER_CALL(remove_server, (void *ctx, int32_t source, const qa_q3_host_browser_reader *address, qa_error *e), (ctx, source, address, e))
BROWSER_CALL(server_status, (void *ctx, const char *address, int32_t capacity, const qa_q3_host_browser_writer *writer, bool *present, qa_error *e), (ctx, address, capacity, writer, present, e))
BROWSER_CALL(server_ping, (void *ctx, int32_t source, int32_t index, int32_t *out, qa_error *e), (ctx, source, index, out, e))
BROWSER_CALL(server_visible, (void *ctx, int32_t source, int32_t index, int32_t *out, qa_error *e), (ctx, source, index, out, e))
BROWSER_CALL(compare_servers, (void *ctx, int32_t source, int32_t key, int32_t direction, int32_t first, int32_t second, int32_t *out, qa_error *e), (ctx, source, key, direction, first, second, out, e))
#undef BROWSER_CALL
bool frontend_q3_browser_services(frontend_q3_browser_access *a, qa_q3_host_browser_services *out, qa_error *e)
{
    if (!out || !access_current(a, e)) return false;
    *out = (qa_q3_host_browser_services){.context = a, .current = access_current,
        .server_count = service_server_count, .server_address = service_server_address, .server_info = service_server_info,
        .ping_count = service_ping_count, .clear_ping = service_clear_ping, .get_ping = service_get_ping, .ping_info = service_ping_info,
        .mark_visible = service_mark_visible, .update_pings = service_update_pings, .reset_pings = service_reset_pings,
        .load_cache = service_load_cache, .save_cache = service_save_cache, .add_server = service_add_server, .remove_server = service_remove_server,
        .server_status = service_server_status, .server_ping = service_server_ping, .server_visible = service_server_visible,
        .compare_servers = service_compare_servers};
    return qa_q3_host_browser_services_validate(out, e);
}
static bool enter_owner(frontend_q3_browser *b, qa_error *e)
{
    if (!current(b, e)) return false;
    if (b->busy) return fail(e, "Recursive physical Q3 browser operation");
    b->busy = true; return true;
}
static bool leave_owner(frontend_q3_browser *b, bool ok, qa_error *e)
{ b->busy = false; return ok && current(b, e); }
bool frontend_q3_browser_poll(frontend_q3_browser *b, qa_error *e)
{
    if (!enter_owner(b, e)) return false;
    bool ok = browser_poll(b, e); return leave_owner(b, ok, e);
}
bool frontend_q3_browser_scan(frontend_q3_browser *b, qa_error *e)
{
    if (!enter_owner(b, e)) return false;
    bool ok = browser_scan(b, e); return leave_owner(b, ok, e);
}
bool frontend_q3_browser_load_cache(frontend_q3_browser *b, qa_error *e)
{
    if (!enter_owner(b, e)) return false;
    bool ok = load_cache_owner(b, e); return leave_owner(b, ok, e);
}
bool frontend_q3_browser_master(frontend_q3_browser *b, int32_t source, const char *remote,
    int32_t protocol, const char *const *keywords, size_t count, qa_error *e)
{
    if (!enter_owner(b, e)) return false;
    bool ok = browser_master(b, source, remote, protocol, keywords, count, e); return leave_owner(b, ok, e);
}
bool frontend_q3_browser_ping(frontend_q3_browser_access *a, const char *remote, qa_error *e)
{
    if (!enter_access(a, e)) return false;
    bool ok = manual_ping(a, remote, e); return leave_access(a, ok, e);
}
bool frontend_q3_browser_status_command(frontend_q3_browser_access *a, const char *remote, qa_error *e)
{
    if (!enter_access(a, e)) return false;
    bool ok = browser_status_command(a, remote, e); return leave_access(a, ok, e);
}

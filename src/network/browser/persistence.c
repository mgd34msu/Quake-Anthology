#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool address_write(qa_net_writer *writer, const qa_net_address *address) {
    if (!qa_net_write_u8(writer, (uint8_t)address->kind) || !qa_net_write_u16(writer, address->port)) return false;
    switch (address->kind) {
    case QA_NET_IPV4: return qa_net_write_data(writer, address->host.ipv4, 4);
    case QA_NET_IPV6: return qa_net_write_data(writer, address->host.ipv6.bytes, 16) && qa_net_write_u32(writer, address->host.ipv6.scope);
    case QA_NET_IPX: return qa_net_write_u32(writer, address->host.ipx.network) && qa_net_write_data(writer, address->host.ipx.node, 6);
    case QA_NET_LOOPBACK: return qa_net_write_string(writer, address->host.loopback);
    }
    return qa_net_writer_fail(writer, "Unsupported stored server address");
}
static bool address_read(qa_net_reader *reader, qa_net_address *address) {
    memset(address, 0, sizeof(*address)); address->kind = (qa_net_address_kind)qa_net_read_u8(reader);
    address->port = qa_net_read_u16(reader);
    switch (address->kind) {
    case QA_NET_IPV4: return qa_net_read_data(reader, address->host.ipv4, 4);
    case QA_NET_IPV6:
        if (!qa_net_read_data(reader, address->host.ipv6.bytes, 16)) return false;
        address->host.ipv6.scope = qa_net_read_u32(reader); return !reader->failed;
    case QA_NET_IPX:
        address->host.ipx.network = qa_net_read_u32(reader);
        return qa_net_read_data(reader, address->host.ipx.node, 6);
    case QA_NET_LOOPBACK: return qa_net_read_string(reader, address->host.loopback, sizeof(address->host.loopback));
    }
    return qa_net_reader_fail(reader, "Unsupported stored server address");
}
bool qa_server_browser_save(const qa_server_browser *browser, qa_buffer *out, qa_error *error) {
    if (!browser || !out) return qa_browser_fail(error, "Missing browser persistence output");
    size_t capacity = 12 + (size_t)browser->capacity * 160;
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server list encoding"); return false; }
    uint32_t count = 0;
    for (uint32_t i = 0; i < browser->capacity; ++i)
        if (browser->records[i].occupied && (browser->records[i].entry.sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT))) ++count;
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    qa_net_write_data(&writer, "QASB", 4); qa_net_write_u32(&writer, 1); qa_net_write_u32(&writer, count);
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        const qa_server_entry *entry = &browser->records[i].entry;
        if (!browser->records[i].occupied || !(entry->sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT))) continue;
        qa_net_write_u32(&writer, entry->sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT));
        qa_net_write_u32(&writer, (uint32_t)entry->protocol.kind);
        qa_net_write_u32(&writer, entry->protocol.revision); qa_net_write_u32(&writer, entry->protocol.flags);
        address_write(&writer, &entry->address);
    }
    if (writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
bool qa_server_browser_restore(qa_server_browser *browser, qa_bytes bytes, qa_error *error) {
    if (!browser || browser->callback || bytes.size < 12 || !bytes.data || memcmp(bytes.data, "QASB", 4))
        return qa_browser_fail(error, "Invalid stored server list");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    if (qa_net_read_u32(&reader) != 1) return qa_browser_fail(error, "Unsupported server list version");
    uint32_t count = qa_net_read_u32(&reader);
    if (count > browser->capacity) return qa_browser_fail(error, "Stored server list exceeds capacity");
    browser_record *records = calloc(browser->capacity, sizeof(*records));
    if (!records) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating stored server candidate"); return false; }
    memcpy(records, browser->records, browser->capacity * sizeof(*records));
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        records[i].entry.sources &= ~(QA_SERVER_FAVORITE | QA_SERVER_DIRECT);
        if (!records[i].entry.sources) memset(&records[i], 0, sizeof(records[i]));
    }
    qa_server_browser candidate = *browser; candidate.records = records; candidate.hooks.changed = NULL;
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t sources = qa_net_read_u32(&reader);
        qa_net_protocol_id protocol;
        protocol.kind = (qa_net_protocol)qa_net_read_u32(&reader);
        protocol.revision = qa_net_read_u32(&reader); protocol.flags = qa_net_read_u32(&reader);
        qa_net_address address;
        ok = !reader.failed && address_read(&reader, &address) && sources && !(sources & ~(QA_SERVER_FAVORITE | QA_SERVER_DIRECT)) &&
            qa_server_browser_add(&candidate, &address, protocol, sources, error);
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (!ok) { free(records); return false; }
    free(browser->records); browser->records = records; return true;
}

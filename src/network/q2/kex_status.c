#include "qa/network_kex_status.h"
#include "qa/text.h"

#include <string.h>

static bool string_read(qa_net_reader *r, qa_bytes *out)
{
    uint64_t extent = qa_kex_read_varint(r);
    if (r->failed || extent > qa_net_reader_remaining(r))
        return qa_net_reader_fail(r, "Truncated KEX status string");
    qa_bytes text;
    if (!qa_net_read_bytes(r, (size_t)extent, &text)) return false;
    if (!qa_kex_text_valid(text)) return qa_net_reader_fail(r, "Invalid KEX status UTF-8");
    /* Each source TextDecoder.decode consumes its own leading UTF-8 BOM. */
    if (text.size >= 3 && !memcmp(text.data, "\xef\xbb\xbf", 3)) {
        text.data += 3;
        text.size -= 3;
    }
    *out = text;
    return true;
}

static bool equal(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

bool qa_kex_status_read(qa_bytes bytes, qa_kex_status_view *out, qa_error *e)
{
    if (!out || !bytes.data || !bytes.size || bytes.size > 65535) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX retail status extent");
        return false;
    }
    qa_kex_status_view view = {0};
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    if (!string_read(&r, &view.name)) return false;
    size_t cursor = 0, units = 0;
    uint32_t scalar;
    while (qa_utf8_next(view.name, &cursor, &scalar)) {
        units += scalar > 0xffff ? 2 : 1;
        if (units > 1024) return qa_net_reader_fail(&r, "KEX lobby name exceeds source text limit");
    }
    uint64_t players = qa_kex_read_varint(&r), maximum = qa_kex_read_varint(&r);
    if (r.failed || players > 255 || maximum > 255 || players > maximum)
        return qa_net_reader_fail(&r, "Invalid KEX lobby capacity");
    view.players = (uint8_t)players;
    view.max_players = (uint8_t)maximum;
    while (qa_net_reader_remaining(&r)) {
        qa_kex_status_attribute attribute;
        if (!string_read(&r, &attribute.key) || !string_read(&r, &attribute.value)) return false;
        if (view.attribute_count == 256)
            return qa_net_reader_fail(&r, "KEX lobby has too many source attributes");
        size_t at = 0;
        while (at < view.attribute_count && !equal(view.attributes[at].key, attribute.key)) ++at;
        if (at == view.attribute_count) ++view.attribute_count;
        view.attributes[at] = attribute;
    }
    if (!qa_net_reader_finish(&r)) return false;
    *out = view;
    return true;
}

qa_bytes qa_kex_status_value(const qa_kex_status_view *view, const char *key)
{
    if (!view || !key) return (qa_bytes){0};
    qa_bytes sought = {(const uint8_t *)key, strlen(key)};
    for (size_t i = 0; i < view->attribute_count; ++i)
        if (equal(view->attributes[i].key, sought)) return view->attributes[i].value;
    return (qa_bytes){0};
}

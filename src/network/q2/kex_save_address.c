#include "kex_save_internal.h"

#include <string.h>

bool qa_kex_save_address_valid(const qa_net_address *a, bool peer)
{
    if (!a) return false;
    if (a->kind == QA_NET_LOOPBACK)
        return a->port == 0 && a->host.loopback[0] && memchr(a->host.loopback, 0, sizeof(a->host.loopback));
    return (a->kind == QA_NET_IPV4 || a->kind == QA_NET_IPV6) && (!peer || a->port != 0);
}

bool qa_kex_save_address_write(qa_net_writer *w, const qa_net_address *a)
{
    if (!qa_kex_save_address_valid(a, false)) return qa_net_writer_fail(w, "Invalid KEX retained endpoint");
    bool ok = qa_net_write_u8(w, (uint8_t)a->kind) && qa_net_write_u16(w, a->port);
    if (a->kind == QA_NET_IPV4) return ok && qa_net_write_data(w, a->host.ipv4, 4);
    if (a->kind == QA_NET_IPV6)
        return ok && qa_net_write_data(w, a->host.ipv6.bytes, 16) && qa_net_write_u32(w, a->host.ipv6.scope);
    return ok && qa_net_write_string(w, a->host.loopback);
}

bool qa_kex_save_address_read(qa_net_reader *r, qa_net_address *a)
{
    a->kind = (qa_net_address_kind)qa_net_read_u8(r);
    a->port = qa_net_read_u16(r);
    bool ok;
    if (a->kind == QA_NET_IPV4) ok = qa_net_read_data(r, a->host.ipv4, 4);
    else if (a->kind == QA_NET_IPV6) {
        ok = qa_net_read_data(r, a->host.ipv6.bytes, 16);
        a->host.ipv6.scope = qa_net_read_u32(r);
    } else if (a->kind == QA_NET_LOOPBACK) ok = qa_net_read_string(r, a->host.loopback, sizeof(a->host.loopback));
    else return qa_net_reader_fail(r, "Invalid KEX retained endpoint family");
    return ok && !r->failed && qa_kex_save_address_valid(a, false);
}

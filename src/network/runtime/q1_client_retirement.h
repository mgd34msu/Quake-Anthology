#ifndef QA_NETWORK_Q1_CLIENT_RETIREMENT_H
#define QA_NETWORK_Q1_CLIENT_RETIREMENT_H

#include "qa/network.h"
#include <stdlib.h>
#include <string.h>

typedef struct q1_client_retirement {
    char *reason;
    qa_buffer packet;
    size_t capacity;
    uint8_t transmissions;
    bool notify, marked;
} q1_client_retirement;

static inline void q1_client_retirement_clear(q1_client_retirement *r)
{
    free(r->reason); qa_buffer_free(&r->packet); memset(r, 0, sizeof(*r));
}
static inline bool q1_client_retirement_start(q1_client_retirement *r,
    const char *reason, bool notify, size_t capacity, qa_error *e)
{
    size_t length = strlen(reason);
    if (length == SIZE_MAX) return qa_network_fail(e, "Q1 CLIENT retirement reason extent overflows");
    char *copy = malloc(length + 1);
    uint8_t *packet = notify ? malloc(capacity) : NULL;
    if (!copy || (notify && !packet)) {
        free(copy); free(packet);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q1 CLIENT retirement"); return false;
    }
    memcpy(copy, reason, length + 1);
    *r = (q1_client_retirement){.reason = copy, .packet = {packet, 0},
        .capacity = notify ? capacity : 0, .notify = notify};
    return true;
}
static inline bool q1_client_retirement_valid(const q1_client_retirement *r,
    bool retiring, bool qw, size_t capacity, qa_error *e)
{
    unsigned required = qw ? 3u : 1u;
    if (retiring != (r->reason != NULL) ||
        (!retiring && (r->packet.data || r->packet.size || r->capacity || r->transmissions || r->notify || r->marked)) ||
        (retiring && ((!r->notify && (r->packet.data || r->packet.size || r->capacity || r->transmissions)) ||
            (r->notify && (!r->packet.data || r->capacity != capacity || r->packet.size > capacity ||
                r->transmissions > required || (r->transmissions == required && r->packet.size))) ||
            (r->marked && r->notify && r->transmissions != required))))
        return qa_network_fail(e, "Invalid Q1 CLIENT retirement continuation");
    return true;
}
static inline bool q1_client_retirement_extent(const q1_client_retirement *r, size_t *extent, qa_error *e)
{
    size_t length = r->reason ? strlen(r->reason) : 0;
    if (length > SIZE_MAX - 19 || r->packet.size > SIZE_MAX - 19 - length ||
        *extent > SIZE_MAX - 19 - length - r->packet.size)
        return qa_network_fail(e, "Q1 CLIENT retirement extent overflows");
    *extent += 19 + length + r->packet.size; return true;
}
static inline bool q1_client_retirement_write(const q1_client_retirement *r, qa_net_writer *w)
{
    size_t length = r->reason ? strlen(r->reason) : 0;
    return qa_net_write_u8(w, r->notify) && qa_net_write_u8(w, r->marked) &&
        qa_net_write_u8(w, r->transmissions) && qa_net_write_u64(w, length) &&
        qa_net_write_data(w, (const uint8_t *)r->reason, length) &&
        qa_net_write_u64(w, r->packet.size) && qa_net_write_data(w, r->packet.data, r->packet.size);
}
static inline bool q1_client_retirement_read(q1_client_retirement *r, qa_net_reader *reader,
    bool retiring, bool qw, size_t capacity, qa_error *e)
{
    uint8_t notify = qa_net_read_u8(reader), marked = qa_net_read_u8(reader),
        transmissions = qa_net_read_u8(reader);
    uint64_t length = qa_net_read_u64(reader); qa_bytes reason = {0}, packet = {0};
    if (reader->failed || notify > 1 || marked > 1 || length >= SIZE_MAX ||
        !qa_net_read_bytes(reader, (size_t)length, &reason) ||
        (length && memchr(reason.data, 0, reason.size)))
        return qa_net_reader_fail(reader, "Invalid Q1 CLIENT retirement reason");
    uint64_t size = qa_net_read_u64(reader);
    if (reader->failed || size > capacity || !qa_net_read_bytes(reader, (size_t)size, &packet))
        return qa_net_reader_fail(reader, "Invalid Q1 CLIENT retirement packet");
    if (retiring) {
        r->reason = malloc((size_t)length + 1);
        r->packet.data = notify ? malloc(capacity) : NULL;
        if (!r->reason || (notify && !r->packet.data)) {
            q1_client_retirement_clear(r);
            qa_error_set(e, QA_ERROR_MEMORY, reader->bit / 8, "Restoring Q1 CLIENT retirement"); return false;
        }
        if (length) memcpy(r->reason, reason.data, (size_t)length);
        r->reason[length] = 0;
        if (size && notify) memcpy(r->packet.data, packet.data, (size_t)size);
        r->packet.size = (size_t)size; r->capacity = notify ? capacity : 0;
    } else if (length || size) return qa_net_reader_fail(reader, "Live Q1 CLIENT has retirement storage");
    r->notify = notify != 0; r->marked = marked != 0; r->transmissions = transmissions;
    return q1_client_retirement_valid(r, retiring, qw, capacity, e);
}
#endif

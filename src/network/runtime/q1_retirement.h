#ifndef QA_NETWORK_Q1_RETIREMENT_H
#define QA_NETWORK_Q1_RETIREMENT_H

#include "qa/network.h"
#include <stdlib.h>
#include <string.h>

typedef struct q1_retirement {
    char *reason;
    qa_buffer packet;
    size_t capacity;
    bool notify, packet_disconnect, sent, marked, busy;
} q1_retirement;

static void q1_retirement_clear(q1_retirement *r)
{
    free(r->reason); qa_buffer_free(&r->packet); memset(r, 0, sizeof(*r));
}
static bool q1_retirement_start(q1_retirement *r, const char *reason, bool notify,
    size_t capacity, qa_error *error)
{
    size_t length = strlen(reason);
    if (length == SIZE_MAX) return qa_network_fail(error, "Source retirement reason extent overflow");
    char *copy = malloc(length + 1);
    uint8_t *packet = notify ? malloc(capacity) : NULL;
    if (!copy || (notify && !packet)) {
        free(copy); free(packet);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining source retirement operation"); return false;
    }
    memcpy(copy, reason, length + 1);
    *r = (q1_retirement){.reason = copy, .packet = {packet, 0}, .capacity = notify ? capacity : 0,
        .notify = notify, .sent = !notify};
    return true;
}
static bool q1_retirement_valid(const q1_retirement *r, bool retiring, size_t capacity,
    qa_error *error)
{
    if (r->busy || (retiring != (r->reason != NULL)) ||
        (!retiring && (r->packet.data || r->packet.size || r->capacity || r->notify ||
            r->packet_disconnect || r->sent || r->marked)) ||
        (retiring && ((!r->notify && (r->packet.data || r->packet.size || r->capacity ||
                r->packet_disconnect || !r->sent)) ||
            (r->notify && (!r->packet.data || r->capacity != capacity || r->packet.size > capacity)) ||
            (r->packet_disconnect && !r->packet.size) ||
            (r->sent && r->notify && (!r->packet.size || !r->packet_disconnect)) ||
            (r->marked && !r->sent))))
        return qa_network_fail(error, "Invalid retained source retirement progress");
    return true;
}
static bool q1_retirement_extent(const q1_retirement *r, size_t *extent, qa_error *error)
{
    size_t reason = r->reason ? strlen(r->reason) : 0;
    if (reason > SIZE_MAX - 20 || r->packet.size > SIZE_MAX - 20 - reason ||
        *extent > SIZE_MAX - 20 - reason - r->packet.size)
        return qa_network_fail(error, "Source retirement checkpoint extent overflow");
    *extent += 20 + reason + r->packet.size; return true;
}
static bool q1_retirement_write(const q1_retirement *r, qa_net_writer *w)
{
    size_t length = r->reason ? strlen(r->reason) : 0;
    return qa_net_write_u8(w, r->notify) && qa_net_write_u8(w, r->packet_disconnect) &&
        qa_net_write_u8(w, r->sent) && qa_net_write_u8(w, r->marked) &&
        qa_net_write_u64(w, length) && qa_net_write_data(w, (const uint8_t *)r->reason, length) &&
        qa_net_write_u64(w, r->packet.size) && qa_net_write_data(w, r->packet.data, r->packet.size);
}
static bool q1_retirement_read(q1_retirement *r, qa_net_reader *reader, bool retiring,
    size_t capacity, qa_error *error)
{
    uint8_t notify = qa_net_read_u8(reader), packet_disconnect = qa_net_read_u8(reader),
        sent = qa_net_read_u8(reader), marked = qa_net_read_u8(reader);
    uint64_t length = qa_net_read_u64(reader); qa_bytes reason = {0}, packet = {0};
    if (reader->failed || notify > 1 || packet_disconnect > 1 || sent > 1 || marked > 1 ||
        length >= SIZE_MAX || !qa_net_read_bytes(reader, (size_t)length, &reason) ||
        (length && memchr(reason.data, 0, reason.size)))
        return qa_net_reader_fail(reader, "Invalid source retirement reason");
    uint64_t size = qa_net_read_u64(reader);
    if (reader->failed || size > capacity || !qa_net_read_bytes(reader, (size_t)size, &packet))
        return qa_net_reader_fail(reader, "Invalid source retirement packet extent");
    if (retiring) {
        r->reason = malloc((size_t)length + 1);
        if (notify) r->packet.data = malloc(capacity);
        if (!r->reason || (notify && !r->packet.data)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring source retirement operation"); return false;
        }
        if (length) memcpy(r->reason, reason.data, (size_t)length);
        r->reason[(size_t)length] = 0;
        if (size && r->packet.data) memcpy(r->packet.data, packet.data, (size_t)size);
    } else if (length || size) return qa_net_reader_fail(reader, "Inactive source retirement has retained bytes");
    r->packet.size = (size_t)size; r->capacity = notify ? capacity : 0;
    r->notify = notify != 0; r->packet_disconnect = packet_disconnect != 0;
    r->sent = sent != 0; r->marked = marked != 0;
    return q1_retirement_valid(r, retiring, capacity, error);
}

#endif

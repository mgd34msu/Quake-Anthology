#include "qa/network.h"
#include "qa/network_save.h"
#include "q3/save_fields.h"

#include <stdlib.h>
#include <string.h>

typedef struct client_slot {
    qa_net_client client;
    qa_net_seat_binding *seats;
    uint64_t generation;
    bool occupied, retired;
} client_slot;

struct qa_net_connections {
    uint64_t owner;
    uint32_t capacity;
    client_slot *slots;
    qa_net_admit_fn admit;
    void *context;
    bool admitting;
};

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

bool qa_net_client_id_equal(qa_net_client_id a, qa_net_client_id b)
{
    return a.owner == b.owner && a.generation == b.generation && a.slot == b.slot;
}

bool qa_net_client_owns_seat(const qa_net_client *client, qa_net_seat_id seat)
{
    if (client == NULL) return false;
    for (size_t index = 0; index < client->seat_count; ++index)
        if (client->seats[index].seat.owner == seat.owner && client->seats[index].seat.index == seat.index) return true;
    return false;
}

bool qa_net_connections_create(uint64_t owner, uint32_t capacity,
                                qa_net_admit_fn admit, void *context,
                                qa_net_connections **out, qa_error *error)
{
    if (owner == 0 || capacity == 0 || out == NULL || admit == NULL ||
        (uint64_t)capacity > SIZE_MAX / sizeof(client_slot))
        return fail(error, "Invalid connection owner configuration");
    qa_net_connections *table = calloc(1, sizeof(*table));
    if (table == NULL) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate connection owner"); return false; }
    table->slots = calloc(capacity, sizeof(*table->slots));
    if (table->slots == NULL) {
        free(table);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate connection slots");
        return false;
    }
    table->owner = owner;
    table->capacity = capacity;
    table->admit = admit;
    table->context = context;
    for (uint32_t index = 0; index < capacity; ++index) table->slots[index].generation = 1;
    *out = table;
    return true;
}

void qa_net_connections_destroy(qa_net_connections *table)
{
    if (table == NULL) return;
    /* Admission callbacks borrow the table and cannot destroy it. */
    if (table->admitting) return;
    for (uint32_t index = 0; index < table->capacity; ++index)
        free(table->slots[index].seats);
    free(table->slots);
    free(table);
}

static client_slot *lookup(qa_net_connections *table, qa_net_client_id id)
{
    if (table == NULL || id.owner != table->owner || id.slot >= table->capacity) return NULL;
    client_slot *slot = &table->slots[id.slot];
    return slot->occupied && slot->generation == id.generation ? slot : NULL;
}

const qa_net_client *qa_net_connections_get(const qa_net_connections *table, qa_net_client_id id)
{
    if (table == NULL || id.owner != table->owner || id.slot >= table->capacity) return NULL;
    const client_slot *slot = &table->slots[id.slot];
    return slot->occupied && slot->generation == id.generation ? &slot->client : NULL;
}

bool qa_net_connections_next(const qa_net_connections *table, uint32_t *cursor, const qa_net_client **out)
{
    if (table == NULL || cursor == NULL || out == NULL) return false;
    while (*cursor < table->capacity) {
        const client_slot *slot = &table->slots[(*cursor)++];
        if (slot->occupied) { *out = &slot->client; return true; }
    }
    return false;
}

bool qa_net_connections_add(qa_net_connections *table, const qa_net_connect *request,
                             uint64_t now_ns, qa_net_client_id *out, qa_error *error)
{
    if (table == NULL || table->admitting || request == NULL || out == NULL ||
        request->seat_count > SIZE_MAX / sizeof(qa_net_seat_binding) ||
        (request->seat_count != 0 && request->seats == NULL))
        return fail(error, "Invalid connection request");
    char endpoint[256];
    if (!qa_net_protocol_valid(request->protocol, error) ||
        !qa_net_address_format(&request->endpoint, endpoint, sizeof(endpoint), error)) return false;
    if (request->endpoint.kind != QA_NET_LOOPBACK && request->endpoint.port == 0)
        return fail(error, "Connection endpoint has a zero port");
    switch (request->attachment) {
        case QA_NET_LOCAL_SEAT:
            if (request->seat_count != 1)
                return fail(error, "A local connection requires one seat");
            break;
        case QA_NET_REMOTE: break;
        case QA_NET_HEADLESS:
            if (request->seat_count != 0) return fail(error, "A headless connection cannot own seats");
            break;
        default: return fail(error, "Unknown connection attachment kind");
    }
    for (size_t index = 0; index < request->seat_count; ++index) {
        const qa_net_seat_binding *seat = &request->seats[index];
        if (seat->seat.owner != table->owner) return fail(error, "Seat belongs to another connection owner");
        for (size_t earlier = 0; earlier < index; ++earlier)
            if (request->seats[earlier].seat.index == seat->seat.index ||
                request->seats[earlier].remote_index == seat->remote_index)
                return fail(error, "Connection seat bindings are not unique");
        for (uint32_t slot_index = 0; slot_index < table->capacity; ++slot_index)
            if (table->slots[slot_index].occupied && qa_net_client_owns_seat(&table->slots[slot_index].client, seat->seat))
                return fail(error, "Seat already belongs to a connected client");
    }
    uint32_t free_index = 0;
    while (free_index < table->capacity && (table->slots[free_index].occupied || table->slots[free_index].retired)) ++free_index;
    if (free_index == table->capacity) return fail(error, "Connection table is full");
    qa_net_seat_binding *seats = NULL;
    if (request->seat_count != 0) {
        seats = malloc(request->seat_count * sizeof(*seats));
        if (seats == NULL) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate connection seat bindings"); return false; }
        memcpy(seats, request->seats, request->seat_count * sizeof(*seats));
    }
    qa_net_connect owned = *request;
    owned.seats = seats;
    table->admitting = true;
    bool admitted = table->admit(table->context, &owned, error);
    table->admitting = false;
    if (!admitted) { free(seats); return false; }
    client_slot *slot = &table->slots[free_index];
    slot->seats = seats;
    qa_net_client_id id = { .owner = table->owner, .generation = slot->generation, .slot = free_index };
    slot->client = (qa_net_client){ .id = id, .attachment = owned.attachment,
        .endpoint = owned.endpoint, .protocol = owned.protocol, .phase = QA_NET_CONNECTED,
        .seats = seats, .seat_count = owned.seat_count, .connected_ns = now_ns,
        .received_ns = now_ns, .composition = owned.composition };
    slot->occupied = true;
    *out = id;
    return true;
}

bool qa_net_connections_phase(qa_net_connections *table, qa_net_client_id id, qa_net_phase phase, qa_error *error)
{
    client_slot *slot = lookup(table, id);
    if (slot == NULL || table->admitting) return fail(error, "Connection is stale or its owner is in admission");
    if ((slot->client.phase == QA_NET_CONNECTED && phase == QA_NET_PRIMED) ||
        (slot->client.phase == QA_NET_PRIMED && phase == QA_NET_ACTIVE)) {
        slot->client.phase = phase;
        return true;
    }
    return fail(error, "Invalid connection phase transition");
}

bool qa_net_connections_received(qa_net_connections *table, qa_net_client_id id, uint64_t now_ns, qa_error *error)
{
    client_slot *slot = lookup(table, id);
    if (slot == NULL || table->admitting) return fail(error, "Connection is stale or its owner is in admission");
    if (now_ns < slot->client.received_ns) return fail(error, "Connection receive time moved backwards");
    slot->client.received_ns = now_ns;
    return true;
}

bool qa_net_connections_restart(qa_net_connections *table, qa_net_client_id id,
                                 const qa_sha256_digest *composition, qa_error *error)
{
    client_slot *slot = lookup(table, id);
    if (slot == NULL || table->admitting || composition == NULL)
        return fail(error, "Invalid connection restart");
    qa_net_connect request = { .attachment = slot->client.attachment,
        .endpoint = slot->client.endpoint, .protocol = slot->client.protocol,
        .seats = slot->seats, .seat_count = slot->client.seat_count,
        .composition = *composition };
    table->admitting = true;
    bool admitted = table->admit(table->context, &request, error);
    table->admitting = false;
    if (!admitted) return false;
    slot->client.composition = *composition;
    slot->client.phase = QA_NET_CONNECTED;
    return true;
}

bool qa_net_connections_rebind(qa_net_connections *table, qa_net_client_id id,
                                const qa_net_address *endpoint, qa_error *error)
{
    client_slot *slot = lookup(table, id);
    if (slot == NULL || table->admitting) return fail(error, "Connection is stale or its owner is in admission");
    if (!qa_net_address_equal(&slot->client.endpoint, endpoint, false) ||
        (endpoint->kind != QA_NET_LOOPBACK && endpoint->port == 0))
        return fail(error, "Port rebinding cannot change the base address");
    slot->client.endpoint = *endpoint;
    return true;
}

bool qa_net_connections_remove(qa_net_connections *table, qa_net_client_id id, qa_error *error)
{
    client_slot *slot = lookup(table, id);
    if (slot == NULL || table->admitting) return fail(error, "Connection is stale or its owner is in admission");
    free(slot->seats);
    slot->seats = NULL;
    slot->client = (qa_net_client){0};
    slot->occupied = false;
    if (slot->generation == UINT64_MAX) slot->retired = true;
    else ++slot->generation;
    return true;
}

bool qa_net_client_expired(const qa_net_client *client, uint64_t now_ns, uint64_t timeout_ns)
{
    return client != NULL && client->attachment != QA_NET_LOCAL_SEAT && now_ns >= client->received_ns &&
        now_ns - client->received_ns > timeout_ns;
}

bool qa_net_connections_checkpoint(const qa_net_connections *table, qa_net_writer *w)
{
    if (!table || table->admitting) return qa_net_writer_fail(w, "Connection checkpoint requires idle admission");
    if (!qa_net_write_u32(w, 1) || !qa_net_write_u32(w, table->capacity)) return false;
    for (uint32_t i = 0; i < table->capacity; ++i) {
        const client_slot *slot = &table->slots[i]; const qa_net_client *c = &slot->client;
        if (!qa_net_write_u64(w, slot->generation) || !qa_net_write_u8(w, slot->occupied) ||
            !qa_net_write_u8(w, slot->retired)) return false;
        if (!slot->occupied) continue;
        if (c->seat_count > QA_NETWORK_MAX_SEATS)
            return qa_net_writer_fail(w, "Connection checkpoint seat extent exceeds its runtime owner");
        if (!qa_net_write_u32(w, c->attachment) || !q3_save_address(w, &c->endpoint) ||
            !qa_net_write_u32(w, c->protocol.kind) || !qa_net_write_u32(w, c->protocol.revision) ||
            !qa_net_write_u32(w, c->protocol.flags) ||
            !qa_net_write_u32(w, c->phase) || !qa_net_write_u64(w, c->connected_ns) ||
            !qa_net_write_u64(w, c->received_ns) || !qa_net_write_data(w, c->composition.bytes, 32) ||
            !qa_net_write_u32(w, (uint32_t)c->seat_count)) return false;
        for (size_t j = 0; j < c->seat_count; ++j)
            if (!qa_net_write_u32(w, c->seats[j].seat.index) || !qa_net_write_u32(w, c->seats[j].remote_index)) return false;
    }
    return true;
}

bool qa_net_connections_restore(qa_net_reader *r, uint64_t owner, uint32_t capacity,
    qa_net_admit_fn admit, void *context, qa_net_connections **out)
{
    if (!r || !out) return r && qa_net_reader_fail(r, "Missing candidate connection table");
    uint32_t version = qa_net_read_u32(r), saved_capacity = qa_net_read_u32(r);
    if (version != 1 || saved_capacity != capacity)
        return qa_net_reader_fail(r, "Candidate connection table capacity differs");
    qa_net_connections *table = NULL;
    if (r->failed || !qa_net_connections_create(owner, capacity, admit, context, &table, r->error)) return false;
    for (uint32_t i = 0; i < capacity; ++i) {
        client_slot *slot = &table->slots[i];
        slot->generation = qa_net_read_u64(r);
        bool occupied = q3_save_bool(r); slot->retired = q3_save_bool(r);
        if (r->failed || !slot->generation || (slot->retired && (occupied || slot->generation != UINT64_MAX))) {
            qa_net_reader_fail(r, "Invalid saved connection generation lifecycle"); goto failure;
        }
        if (!occupied) continue;
        qa_net_client *c = &slot->client;
        c->id = (qa_net_client_id){owner, slot->generation, i};
        c->attachment = (qa_net_attachment)qa_net_read_u32(r);
        if (!q3_restore_address(r, &c->endpoint)) goto failure;
        c->protocol.kind = (qa_net_protocol)qa_net_read_u32(r); c->protocol.revision = qa_net_read_u32(r);
        c->protocol.flags = qa_net_read_u32(r);
        c->phase = (qa_net_phase)qa_net_read_u32(r); c->connected_ns = qa_net_read_u64(r);
        c->received_ns = qa_net_read_u64(r);
        if (!qa_net_read_data(r, c->composition.bytes, 32)) goto failure;
        c->seat_count = qa_net_read_u32(r);
        char endpoint[256];
        if (r->failed || c->seat_count > QA_NETWORK_MAX_SEATS || (unsigned)c->phase > QA_NET_ACTIVE ||
            c->received_ns < c->connected_ns ||
            (c->attachment != QA_NET_LOCAL_SEAT && c->attachment != QA_NET_REMOTE && c->attachment != QA_NET_HEADLESS) ||
            (c->attachment == QA_NET_LOCAL_SEAT && c->seat_count != 1) ||
            (c->attachment == QA_NET_HEADLESS && c->seat_count) ||
            (c->endpoint.kind != QA_NET_LOOPBACK && !c->endpoint.port) ||
            !qa_net_protocol_valid(c->protocol, r->error) ||
            !qa_net_address_format(&c->endpoint, endpoint, sizeof(endpoint), r->error)) {
            qa_net_reader_fail(r, "Invalid saved connection admission fields"); goto failure;
        }
        slot->seats = c->seat_count ? calloc(c->seat_count, sizeof(*slot->seats)) : NULL;
        if (c->seat_count && !slot->seats) {
            qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Restoring connection seats"); r->failed = true; goto failure;
        }
        c->seats = slot->seats;
        for (size_t j = 0; j < c->seat_count; ++j) {
            slot->seats[j].seat = (qa_net_seat_id){owner, qa_net_read_u32(r)};
            slot->seats[j].remote_index = qa_net_read_u32(r);
            for (size_t k = 0; k < j; ++k)
                if (slot->seats[k].seat.index == slot->seats[j].seat.index ||
                    slot->seats[k].remote_index == slot->seats[j].remote_index) {
                    qa_net_reader_fail(r, "Duplicate restored connection seat"); goto failure;
                }
            for (uint32_t k = 0; k < i; ++k)
                if (table->slots[k].occupied && qa_net_client_owns_seat(&table->slots[k].client, slot->seats[j].seat)) {
                    qa_net_reader_fail(r, "Restored seat belongs to multiple clients"); goto failure;
                }
        }
        for (uint32_t k = 0; k < i; ++k)
            if (table->slots[k].occupied && qa_net_address_equal(&table->slots[k].client.endpoint, &c->endpoint, true)) {
                qa_net_reader_fail(r, "Restored endpoint belongs to multiple clients"); goto failure;
            }
        qa_net_connect request = {.attachment = c->attachment, .endpoint = c->endpoint, .protocol = c->protocol,
            .seats = c->seats, .seat_count = c->seat_count, .composition = c->composition};
        table->admitting = true;
        bool accepted = !r->failed && table->admit(table->context, &request, r->error);
        table->admitting = false;
        if (!accepted) { r->failed = true; goto failure; }
        slot->occupied = true;
    }
    *out = table; return true;
failure:
    qa_net_connections_destroy(table); return false;
}

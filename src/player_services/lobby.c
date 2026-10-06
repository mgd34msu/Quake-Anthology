#include "qa/local_lobby.h"
#include "qa/strings.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

typedef struct lobby_strings {
    size_t references;
    qa_strings *values;
} lobby_strings;
typedef struct lobby_definition {
    size_t references;
    lobby_strings *strings;
    qa_unified_composition composition;
} lobby_definition;
struct qa_lobby {
    size_t references, member_capacity;
    lobby_definition *definition;
    qa_lobby_view view;
    qa_lobby_member *members;
};
struct qa_lobbies {
    uint64_t owner, serial;
    lobby_strings *strings;
    qa_lobby **rooms;
    size_t count, capacity;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool text_valid(const char *text, bool nonblank) {
    if (!text)
        return false;
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    if (!qa_utf8_valid(bytes))
        return false;
    if (!nonblank)
        return true;
    size_t cursor = 0;
    uint32_t point;
    while (qa_utf8_next(bytes, &cursor, &point))
        if (!qa_unicode_whitespace(point))
            return true;
    return false;
}
static bool account_valid(qa_local_account account) {
    return account.id && *account.id && text_valid(account.id, false) &&
           text_valid(account.name, false);
}
static bool intern(qa_lobbies *service, const char *text, const char **out, qa_error *error) {
    qa_string_id id;
    if (!qa_strings_intern_cstr(service->strings->values, text, &id, error))
        return false;
    *out = qa_strings_cstr(service->strings->values, id);
    return true;
}
static void strings_release(lobby_strings *strings) {
    if (!--strings->references) {
        qa_strings_destroy(strings->values);
        free(strings);
    }
}
static void definition_release(lobby_definition *definition) {
    if (!definition || --definition->references)
        return;
    strings_release(definition->strings);
    qa_unified_composition_free(&definition->composition);
    free(definition);
}
const qa_lobby_view *qa_lobby_read(const qa_lobby *room) { return room ? &room->view : NULL; }
void qa_lobby_retain(const qa_lobby *room) {
    if (room)
        ++((qa_lobby *)room)->references;
}
void qa_lobby_release(const qa_lobby *room) {
    qa_lobby *owned = (qa_lobby *)room;
    if (!owned || --owned->references)
        return;
    definition_release(owned->definition);
    free(owned->members);
    free(owned);
}
static size_t find(const qa_lobbies *service, qa_lobby_id id) {
    if (!service || id.owner != service->owner || !id.serial)
        return SIZE_MAX;
    size_t first = 0, end = service->count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (service->rooms[middle]->view.id.serial < id.serial)
            first = middle + 1;
        else
            end = middle;
    }
    return first < service->count && service->rooms[first]->view.id.serial == id.serial ? first
                                                                                        : SIZE_MAX;
}
static bool require(qa_lobbies *service, qa_lobby_id id, size_t *index, qa_error *error) {
    *index = find(service, id);
    if (*index != SIZE_MAX)
        return true;
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Local lobby no longer exists");
    return false;
}
const qa_lobby *qa_lobbies_find(const qa_lobbies *service, qa_lobby_id id) {
    size_t index = find(service, id);
    return index == SIZE_MAX ? NULL : service->rooms[index];
}
size_t qa_lobbies_count(const qa_lobbies *service) { return service ? service->count : 0; }
const qa_lobby *qa_lobbies_at(const qa_lobbies *service, size_t index) {
    return service && index < service->count ? service->rooms[index] : NULL;
}
bool qa_lobbies_create(uint64_t owner, qa_lobbies **out, qa_error *error) {
    if (!owner || !out)
        return fail(error, "Missing local lobby identity or output");
    qa_lobbies *service = calloc(1, sizeof(*service));
    lobby_strings *strings = calloc(1, sizeof(*strings));
    if (!service || !strings) {
        free(service);
        free(strings);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating local lobby service");
        return false;
    }
    if (!qa_strings_create(&strings->values, error)) {
        free(service);
        free(strings);
        return false;
    }
    strings->references = 1;
    service->strings = strings;
    service->owner = owner;
    *out = service;
    return true;
}
void qa_lobbies_destroy(qa_lobbies *service) {
    if (!service)
        return;
    for (size_t i = 0; i < service->count; ++i)
        qa_lobby_release(service->rooms[i]);
    free(service->rooms);
    strings_release(service->strings);
    free(service);
}
static size_t member(const qa_lobby *room, const char *account) {
    if (account)
        for (size_t i = 0; i < room->view.member_count; ++i)
            if (!strcmp(room->members[i].account.id, account))
                return i;
    return SIZE_MAX;
}
/* Copy only when a transition/UI reader retains the previous room version. */
static qa_lobby *edit(qa_lobbies *service, size_t index, size_t count, qa_error *error) {
    qa_lobby *old = service->rooms[index], *room = old;
    if (count > SIZE_MAX / sizeof(*room->members)) {
        fail(error, "Too many lobby members");
        return NULL;
    }
    if (old->references > 1) {
        room = malloc(sizeof(*room));
        if (!room)
            goto memory;
        *room = *old;
        room->references = 1;
        room->member_capacity = count;
        room->members = count ? malloc(count * sizeof(*room->members)) : NULL;
        if (count && !room->members) {
            free(room);
            goto memory;
        }
        if (old->view.member_count)
            memcpy(room->members, old->members, old->view.member_count * sizeof(*room->members));
        ++room->definition->references;
        room->view.members = room->members;
        service->rooms[index] = room;
        qa_lobby_release(old);
    } else if (count > room->member_capacity) {
        qa_lobby_member *members = realloc(room->members, count * sizeof(*members));
        if (!members)
            goto memory;
        room->members = members;
        room->view.members = members;
        room->member_capacity = count;
    }
    return room;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, count, "Preparing local lobby membership");
    return NULL;
}
bool qa_lobbies_host(qa_lobbies *service, qa_local_account account, const char *name,
                     uint32_t capacity, const qa_lobby_selection *selection, uint32_t seats,
                     const qa_lobby **out, qa_error *error) {
    if (!service || !account_valid(account) || !text_valid(name, true) || !capacity || !seats ||
        seats > capacity || !selection || !selection->composition ||
        !selection->composition->canonical.data || !selection->composition->canonical.size ||
        !text_valid(selection->snapshot_schema, true) || service->serial == UINT64_MAX)
        return fail(error, "Invalid local lobby settings");
    qa_bytes bytes = {selection->composition->canonical.data,
                      selection->composition->canonical.size};
    const char *owner, *owner_name, *room_name, *schema;
    if (!intern(service, account.id, &owner, error) ||
        !intern(service, account.name, &owner_name, error) ||
        !intern(service, name, &room_name, error) ||
        !intern(service, selection->snapshot_schema, &schema, error))
        return false;
    if (service->count == service->capacity) {
        size_t next = service->capacity ? service->capacity * 2 : 8;
        if (next < service->capacity || next > SIZE_MAX / sizeof(*service->rooms))
            return fail(error, "Too many local lobbies");
        qa_lobby **rooms = realloc(service->rooms, next * sizeof(*rooms));
        if (!rooms) {
            qa_error_set(error, QA_ERROR_MEMORY, next, "Growing local lobby registry");
            return false;
        }
        service->rooms = rooms;
        service->capacity = next;
    }
    qa_lobby *room = calloc(1, sizeof(*room));
    lobby_definition *definition = calloc(1, sizeof(*definition));
    qa_lobby_member *members = malloc(sizeof(*members));
    uint8_t *composition = malloc(bytes.size);
    if (!room || !definition || !members || !composition) {
        free(room);
        free(definition);
        free(members);
        free(composition);
        qa_error_set(error, QA_ERROR_MEMORY, bytes.size, "Retaining prepared local lobby");
        return false;
    }
    memcpy(composition, bytes.data, bytes.size);
    definition->references = 1;
    definition->strings = service->strings;
    ++service->strings->references;
    definition->composition = (qa_unified_composition){{composition, bytes.size}};
    room->references = room->member_capacity = 1;
    room->definition = definition;
    room->members = members;
    *members = (qa_lobby_member){{owner, owner_name}, seats, false};
    room->view = (qa_lobby_view){.id = {service->owner, ++service->serial},
                                 .owner = owner,
                                 .name = room_name,
                                 .capacity = capacity,
                                 .selection = {&definition->composition, schema},
                                 .members = members,
                                 .member_count = 1,
                                 .phase = QA_LOBBY_OPEN};
    service->rooms[service->count++] = room;
    if (out)
        *out = room;
    return true;
}
bool qa_lobbies_join(qa_lobbies *service, qa_lobby_id id, qa_local_account account, uint32_t seats,
                     const qa_lobby **out, qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    qa_lobby *room = service->rooms[index];
    if (!account_valid(account) || !seats || room->view.phase != QA_LOBBY_OPEN ||
        member(room, account.id) != SIZE_MAX)
        return fail(error, "Cannot join this local lobby");
    uint64_t used = seats;
    for (size_t i = 0; i < room->view.member_count; ++i)
        used += room->members[i].seats;
    if (used > room->view.capacity)
        return fail(error, "Local lobby has insufficient seat capacity");
    qa_local_account owned;
    if (!intern(service, account.id, &owned.id, error) ||
        !intern(service, account.name, &owned.name, error))
        return false;
    room = edit(service, index, room->view.member_count + 1, error);
    if (!room)
        return false;
    room->members[room->view.member_count++] = (qa_lobby_member){owned, seats, false};
    if (out)
        *out = room;
    return true;
}
bool qa_lobbies_ready(qa_lobbies *service, qa_lobby_id id, const char *account, bool ready,
                      qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    qa_lobby *room = service->rooms[index];
    size_t seat = member(room, account);
    if (room->view.phase != QA_LOBBY_OPEN || seat == SIZE_MAX)
        return fail(error, "Account cannot change lobby readiness");
    room = edit(service, index, room->view.member_count, error);
    if (!room)
        return false;
    room->members[seat].ready = ready;
    return true;
}
static bool owned(const qa_lobby *room, const char *owner, qa_error *error) {
    return (owner && !strcmp(room->view.owner, owner)) ||
           fail(error, "Operation requires the local lobby owner");
}
bool qa_lobbies_start(qa_lobbies *service, qa_lobby_id id, const char *owner, const qa_lobby **out,
                      qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    qa_lobby *room = service->rooms[index];
    if (!owned(room, owner, error))
        return false;
    if (room->view.phase != QA_LOBBY_OPEN || room->view.match_generation == UINT64_MAX)
        return fail(error, "Local lobby cannot start");
    for (size_t i = 0; i < room->view.member_count; ++i)
        if (!room->members[i].ready)
            return fail(error, "Local lobby is not ready");
    room = edit(service, index, room->view.member_count, error);
    if (!room)
        return false;
    room->view.phase = QA_LOBBY_STARTING;
    ++room->view.match_generation;
    if (out)
        *out = room;
    return true;
}
bool qa_lobbies_publish(qa_lobbies *service, qa_lobby_id id, const char *owner, uint64_t generation,
                        const qa_net_address *endpoint, const qa_lobby_wire *wire,
                        const qa_lobby **out, qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    qa_lobby *room = service->rooms[index];
    if (!owned(room, owner, error))
        return false;
    if (room->view.phase != QA_LOBBY_STARTING || room->view.match_generation != generation)
        return fail(error, "Local lobby launch is no longer current");
    if (!endpoint || !wire || !qa_net_protocol_valid(wire->protocol, error))
        return fail(error, "Missing or invalid lobby transport");
    char address[256];
    if (!qa_net_address_format(endpoint, address, sizeof(address), error))
        return false;
    bool unified = wire->protocol.kind == QA_NET_UNIFIED_1;
    const qa_buffer *prepared = &room->definition->composition.canonical;
    if (unified && (!wire->composition || !wire->composition_bytes.data ||
                    wire->composition_bytes.size != prepared->size ||
                    memcmp(wire->composition_bytes.data, prepared->data, prepared->size) ||
                    !wire->snapshot_schema ||
                    strcmp(wire->snapshot_schema, room->view.selection.snapshot_schema)))
        return fail(error, "Bound lobby wire differs from its prepared composition");
    qa_net_address bound = *endpoint;
    qa_lobby_wire selected = *wire;
    selected.composition_bytes = unified ? (qa_bytes){prepared->data, prepared->size} : (qa_bytes){0};
    selected.snapshot_schema = unified ? room->view.selection.snapshot_schema : NULL;
    room = edit(service, index, room->view.member_count, error);
    if (!room)
        return false;
    room->view.endpoint = bound;
    room->view.wire = selected;
    room->view.phase = QA_LOBBY_PLAYING;
    if (out)
        *out = room;
    return true;
}
bool qa_lobbies_complete(qa_lobbies *service, qa_lobby_id id, const char *owner,
                         uint64_t generation, const qa_lobby **out, qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    qa_lobby *room = service->rooms[index];
    if (!owned(room, owner, error))
        return false;
    if (room->view.phase != QA_LOBBY_OPEN && room->view.match_generation == generation) {
        room = edit(service, index, room->view.member_count, error);
        if (!room)
            return false;
        room->view.phase = QA_LOBBY_OPEN;
        room->view.endpoint = (qa_net_address){0};
        room->view.wire = (qa_lobby_wire){0};
        for (size_t i = 0; i < room->view.member_count; ++i)
            room->members[i].ready = false;
    }
    if (out)
        *out = room;
    return true;
}
bool qa_lobbies_leave(qa_lobbies *service, qa_lobby_id id, const char *account, qa_error *error) {
    size_t index;
    if (!require(service, id, &index, error))
        return false;
    if (!account)
        return fail(error, "Missing local lobby account");
    qa_lobby *room = service->rooms[index];
    if (!strcmp(room->view.owner, account)) {
        memmove(service->rooms + index, service->rooms + index + 1,
                (service->count - index - 1) * sizeof(*service->rooms));
        --service->count;
        qa_lobby_release(room);
        return true;
    }
    size_t seat = member(room, account);
    if (seat == SIZE_MAX)
        return true;
    room = edit(service, index, room->view.member_count, error);
    if (!room)
        return false;
    memmove(room->members + seat, room->members + seat + 1,
            (room->view.member_count - seat - 1) * sizeof(*room->members));
    --room->view.member_count;
    return true;
}

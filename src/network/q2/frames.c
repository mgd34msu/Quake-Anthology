#include "qa/network_q2_messages.h"
#include <stdlib.h>
#include <string.h>

struct qa_q2_frame_history {
    qa_q2_wire_frame *frames;
    bool *present;
    size_t capacity, next;
};

static bool ordered(qa_q2_entity_span entities) {
    if (entities.count > UINT16_MAX || (entities.count && !entities.data)) return false;
    uint32_t last = 0;
    for (size_t i = 0; i < entities.count; ++i) {
        if (entities.data[i].number <= last || entities.data[i].number > UINT16_MAX) return false;
        last = entities.data[i].number;
    }
    return true;
}

static const qa_q2_entity *baseline_find(qa_q2_entity_span entries, uint32_t number) {
    size_t lo = 0, hi = entries.count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entries.data[mid].number < number) lo = mid + 1;
        else hi = mid;
    }
    return lo < entries.count && entries.data[lo].number == number ? &entries.data[lo] : NULL;
}

void qa_q2_frame_free(qa_q2_wire_frame *frame) {
    if (!frame) return;
    for (size_t i = 0; i < frame->player_count && i < QA_Q2_MAX_SEATS; ++i)
        free((void *)frame->players[i].area_bits.data);
    free(frame->entities);
    memset(frame, 0, sizeof(*frame));
}

bool qa_q2_frame_clone(const qa_q2_wire_frame *from, qa_q2_wire_frame *out, qa_error *error) {
    if (!from || !out || from == out || !from->player_count || from->player_count > QA_Q2_MAX_SEATS ||
        !ordered((qa_q2_entity_span){from->entities, from->entity_count})) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 frame clone");
        return false;
    }
    qa_q2_wire_frame copy = *from;
    copy.entities = NULL;
    for (size_t i = 0; i < copy.player_count; ++i) copy.players[i].area_bits = (qa_bytes){0};
    for (size_t i = 0; i < copy.player_count; ++i) {
        qa_bytes area = from->players[i].area_bits;
        if (area.size > QA_Q2_MAX_AREABITS || (area.size && !area.data)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Invalid Q2 frame area bits");
            qa_q2_frame_free(&copy);
            return false;
        }
        if (area.size) {
            uint8_t *bytes = malloc(area.size);
            if (!bytes) goto memory;
            memcpy(bytes, area.data, area.size);
            copy.players[i].area_bits = (qa_bytes){bytes, area.size};
        }
    }
    if (copy.entity_count) {
        copy.entities = malloc(copy.entity_count * sizeof(*copy.entities));
        if (!copy.entities) goto memory;
        memcpy(copy.entities, from->entities, copy.entity_count * sizeof(*copy.entities));
    }
    *out = copy;
    return true;
memory:
    qa_q2_frame_free(&copy);
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot retain Q2 frame");
    return false;
}

bool qa_q2_frame_history_create(size_t capacity, qa_q2_frame_history **out, qa_error *error) {
    if (!capacity || !out || capacity > SIZE_MAX / sizeof(qa_q2_wire_frame)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 history capacity");
        return false;
    }
    qa_q2_frame_history *h = calloc(1, sizeof(*h));
    if (h) {
        h->frames = calloc(capacity, sizeof(*h->frames));
        h->present = calloc(capacity, sizeof(*h->present));
    }
    if (!h || !h->frames || !h->present) {
        qa_q2_frame_history_destroy(h);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate Q2 frame history");
        return false;
    }
    h->capacity = capacity;
    *out = h;
    return true;
}

void qa_q2_frame_history_clear(qa_q2_frame_history *h) {
    if (!h) return;
    for (size_t i = 0; i < h->capacity; ++i) {
        if (h->present[i]) qa_q2_frame_free(&h->frames[i]);
        h->present[i] = false;
    }
    h->next = 0;
}

void qa_q2_frame_history_destroy(qa_q2_frame_history *h) {
    if (!h) return;
    qa_q2_frame_history_clear(h);
    free(h->frames);
    free(h->present);
    free(h);
}

const qa_q2_wire_frame *qa_q2_frame_history_get(const qa_q2_frame_history *h, int32_t number) {
    if (h) for (size_t i = 0; i < h->capacity; ++i)
        if (h->present[i] && h->frames[i].server_frame == number) return &h->frames[i];
    return NULL;
}

const qa_q2_wire_frame *qa_q2_frame_history_latest(const qa_q2_frame_history *h) {
    const qa_q2_wire_frame *last = NULL;
    if (h) for (size_t i = 0; i < h->capacity; ++i)
        if (h->present[i] && h->frames[i].valid && (!last || h->frames[i].server_frame > last->server_frame))
            last = &h->frames[i];
    return last;
}

static const qa_q2_wire_frame *history_store(qa_q2_frame_history *h, qa_q2_wire_frame *frame) {
    size_t slot = h->capacity;
    for (size_t i = 0; i < h->capacity; ++i)
        if (h->present[i] && h->frames[i].server_frame == frame->server_frame) { slot = i; break; }
    if (slot == h->capacity) { slot = h->next; h->next = (slot + 1) % h->capacity; }
    if (h->present[slot]) qa_q2_frame_free(&h->frames[slot]);
    h->frames[slot] = *frame;
    h->present[slot] = true;
    memset(frame, 0, sizeof(*frame));
    return &h->frames[slot];
}

bool qa_q2_frame_history_accept(qa_q2_frame_history *h, const qa_q2_wire_frame *frame, qa_error *error) {
    if (!h) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 history"); return false; }
    qa_q2_wire_frame copy;
    if (!qa_q2_frame_clone(frame, &copy, error)) return false;
    history_store(h, &copy);
    return true;
}

static bool append_entity(qa_q2_wire_frame *frame, size_t *capacity, qa_q2_codec *c,
                           qa_net_reader *r, const qa_q2_entity *from, uint32_t number, uint64_t bits) {
    if (frame->entity_count == UINT16_MAX) return qa_net_reader_fail(r, "Too many Q2 frame entities");
    if (frame->entity_count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 64;
        if (next > UINT16_MAX) next = UINT16_MAX;
        qa_q2_entity *entities = realloc(frame->entities, next * sizeof(*entities));
        if (!entities) return qa_net_reader_fail(r, "Cannot retain Q2 frame entities");
        frame->entities = entities;
        *capacity = next;
    }
    if (!qa_q2_read_entity(c, r, from, number, bits, &frame->entities[frame->entity_count])) return false;
    ++frame->entity_count;
    return true;
}

bool qa_q2_frame_history_read(qa_q2_frame_history *history, qa_q2_codec *c, qa_net_reader *r,
                              qa_q2_entity_span baselines, const qa_q2_wire_frame **out) {
    if (!history || !c || !out || baselines.count > UINT16_MAX || (baselines.count && !baselines.data))
        return qa_net_reader_fail(r, "Invalid Q2 frame history read");
    qa_q2_frame_header header;
    if (!qa_q2_read_frame_header(c, r, &header)) return false;
    const qa_q2_wire_frame *old = header.deltaframe > 0 ? qa_q2_frame_history_get(history, header.deltaframe) : NULL;
    qa_q2_wire_frame frame = {0};
    frame.valid = header.deltaframe <= 0 || (old && old->valid);
    frame.server_frame = header.serverframe;
    frame.delta_frame = header.deltaframe;
    frame.suppressed_count = header.suppress_count;
    bool kex = c->protocol.kind == QA_NET_Q2KEX_2023 || c->protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    frame.player_count = kex ? c->split_players : 1;
    if (!frame.player_count || frame.player_count > QA_Q2_MAX_SEATS)
        return qa_net_reader_fail(r, "Invalid Q2 split-player count");
    static const qa_q2_player zero_player;
    for (size_t i = 0; i < frame.player_count; ++i) {
        size_t length = i == 0 ? header.areabytes : qa_net_read_u8(r);
        uint8_t *area = length ? malloc(length) : NULL;
        if (length && !area) { qa_net_reader_fail(r, "Cannot retain Q2 frame area bits"); goto failed; }
        frame.players[i].area_bits = (qa_bytes){area, length};
        if (i == 0 && length) memcpy(area, header.areabits, length);
        if (i && !qa_net_read_data(r, area, length)) goto failed;
        const qa_q2_player *previous = old && i < old->player_count ? &old->players[i].player : &zero_player;
        c->frame_player_pending = true;
        if (!qa_q2_read_player(c, r, previous, &frame.players[i].player)) goto failed;
    }
    if (!qa_q2_read_entities_begin(c, r)) goto failed;
    size_t cursor = 0, count = old ? old->entity_count : 0, capacity = 0;
    uint32_t last_number = 0;
    static const qa_q2_entity zero_entity;
    for (;;) {
        uint32_t number;
        uint64_t bits;
        if (!qa_q2_read_entity_header(c, r, &number, &bits)) goto failed;
        if (!number) break;
        if (number <= last_number || number > UINT16_MAX) {
            qa_net_reader_fail(r, "Unordered Q2 entity delta"); goto failed;
        }
        last_number = number;
        while (cursor < count && old->entities[cursor].number < number) {
            const qa_q2_entity *entity = &old->entities[cursor++];
            if (!append_entity(&frame, &capacity, c, r, entity, entity->number, 0)) goto failed;
        }
        bool matched = cursor < count && old->entities[cursor].number == number;
        if (bits & 64u) {
            if (matched) ++cursor;
            else if (frame.valid) { qa_net_reader_fail(r, "Q2 removed entity has no delta source"); goto failed; }
            continue;
        }
        const qa_q2_entity *base = matched ? &old->entities[cursor] : baseline_find(baselines, number);
        if (!append_entity(&frame, &capacity, c, r, base ? base : &zero_entity, number, bits)) goto failed;
        if (matched) ++cursor;
    }
    while (cursor < count) {
        const qa_q2_entity *entity = &old->entities[cursor++];
        if (!append_entity(&frame, &capacity, c, r, entity, entity->number, 0)) goto failed;
    }
    *out = history_store(history, &frame);
    return true;
failed:
    qa_q2_frame_free(&frame);
    c->frame_player_pending = false;
    return false;
}

bool qa_q2_packet_entities_write(qa_q2_codec *c, qa_net_writer *w, qa_q2_entity_span old,
                                  qa_q2_entity_span current, qa_q2_entity_span baselines,
                                  uint32_t max_clients) {
    if (!c || !ordered(old) || !ordered(current) || baselines.count > UINT16_MAX || (baselines.count && !baselines.data))
        return qa_net_writer_fail(w, "Q2 frame entities must be unique and sorted");
    if (!qa_q2_write_entities_begin(c, w)) return false;
    size_t before = 0, after = 0;
    static const qa_q2_entity zero;
    while (before < old.count || after < current.count) {
        const qa_q2_entity *f = before < old.count ? &old.data[before] : NULL;
        const qa_q2_entity *t = after < current.count ? &current.data[after] : NULL;
        if (f && t && f->number == t->number) {
            if (!qa_q2_write_entity(c, w, f, t, false, t->number <= max_clients)) return false;
            ++before; ++after;
        } else if (t && (!f || t->number < f->number)) {
            const qa_q2_entity *base = baseline_find(baselines, t->number);
            if (!qa_q2_write_entity(c, w, base ? base : &zero, t, true, true)) return false;
            ++after;
        } else {
            if (!qa_q2_write_entity_remove(c, w, f->number)) return false;
            ++before;
        }
    }
    return qa_q2_write_entity_end(c, w);
}

typedef struct frame_entities {
    qa_q2_codec *codec;
    qa_q2_entity_span old, current, baselines;
    uint32_t max_clients;
} frame_entities;
static bool write_entities(void *user, qa_net_writer *w, qa_error *error) {
    (void)error;
    frame_entities *e = user;
    return qa_q2_packet_entities_write(e->codec, w, e->old, e->current, e->baselines, e->max_clients);
}

bool qa_q2_frame_write(qa_q2_codec *c, qa_net_writer *w, const qa_q2_wire_frame *frame,
                        const qa_q2_wire_frame *old, qa_q2_entity_span baselines, uint32_t max_clients) {
    if (!c) return qa_net_writer_fail(w, "Missing Q2 frame codec");
    bool kex = c->protocol.kind == QA_NET_Q2KEX_2023 || c->protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    if (!frame || !frame->player_count || frame->player_count > QA_Q2_MAX_SEATS ||
        (!kex && frame->player_count != 1) || (kex && frame->player_count != c->split_players))
        return qa_net_writer_fail(w, "Invalid Q2 frame player count");
    for (size_t i = 0; i < frame->player_count; ++i)
        if (frame->players[i].area_bits.size > UINT8_MAX ||
            (frame->players[i].area_bits.size && !frame->players[i].area_bits.data))
            return qa_net_writer_fail(w, "Q2 frame area bits exceed wire range");
    frame_entities entities = {c, {old ? old->entities : NULL, old ? old->entity_count : 0},
                                {frame->entities, frame->entity_count}, baselines, max_clients};
    static const qa_q2_player zero;
    if (kex && frame->player_count > 1) {
        qa_net_write_u8(w, 20);
        qa_net_write_i32(w, frame->server_frame);
        qa_net_write_i32(w, old ? old->server_frame : -1);
        qa_net_write_u8(w, frame->suppressed_count);
        for (size_t i = 0; i < frame->player_count; ++i) {
            const qa_q2_frame_player *p = &frame->players[i];
            const qa_q2_player *previous = old && i < old->player_count ? &old->players[i].player : &zero;
            if (!qa_net_write_u8(w, (uint8_t)p->area_bits.size) ||
                !qa_net_write_data(w, p->area_bits.data, p->area_bits.size) || !qa_net_write_u8(w, 17) ||
                !qa_q2_write_player(c, w, previous, &p->player)) return false;
        }
        return write_entities(&entities, w, w->error);
    }
    qa_q2_frame_header header = {0};
    header.serverframe = frame->server_frame;
    header.deltaframe = old ? old->server_frame : -1;
    header.suppress_count = frame->suppressed_count;
    header.areabytes = frame->players[0].area_bits.size;
    if (header.areabytes) memcpy(header.areabits, frame->players[0].area_bits.data, header.areabytes);
    return qa_q2_write_frame(c, w, &header, old ? &old->players[0].player : &zero,
                             &frame->players[0].player, write_entities, &entities);
}

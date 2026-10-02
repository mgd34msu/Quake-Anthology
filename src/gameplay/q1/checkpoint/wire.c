#include "internal.h"
#include "../wire_internal.h"
#include <stdio.h>
#include <math.h>

static bool table(q1_save_io *io, q1_wire_table *rows) {
    if (!io->reading && rows->count > UINT32_MAX)
        return q1_save_fail(io, "Q1 ordered precache extent exceeds its codec");
    uint32_t count = io->reading ? 0 : (uint32_t)rows->count;
    Q1_SAVE(io, u32, count);
    if (!count || (io->reading && count > (io->input.size - io->offset) / 4))
        return q1_save_fail(io, "Invalid Q1 ordered precache extent");
    if (io->reading) {
        if (sizeof(*rows->rows) > SIZE_MAX / count)
            return q1_save_fail(io, "Q1 ordered precache allocation overflow");
        rows->rows = calloc(count, sizeof(*rows->rows));
        if (!rows->rows) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "restoring ordered Q1 precaches");
            return false;
        }
        rows->count = rows->capacity = count;
    }
    for (uint32_t i = 0; i < count; ++i) {
        Q1_SAVE(io, string, rows->rows[i]);
        if (!i) {
            if (rows->rows[i]) return q1_save_fail(io, "Q1 ordered precache zero is not empty");
            continue;
        }
        const char *path = qa_strings_cstr(qa_session_strings(io->game->services.session), rows->rows[i]);
        if (!path || (unsigned char)*path <= 32)
            return q1_save_fail(io, "Invalid Q1 ordered precache resource");
        for (uint32_t j = 1; j < i; ++j)
            if (rows->rows[j] == rows->rows[i])
                return q1_save_fail(io, "Duplicate Q1 ordered precache declaration");
    }
    return true;
}
bool q1_save_wire_validate(q1_save_io *io, qa_q1_game *g) {
    if (!g->wire) return true;
    const qa_actor_registry *registry = qa_session_actors(g->services.session);
    for (uint32_t slot = 0; slot < g->capacity; ++slot) {
        const q1_actor *actor = g->actors[slot];
        if (!actor || !actor->native) continue;
        const qa_actor_record *shared = qa_actors_get(registry, actor->id);
        uint32_t client;
        if (qa_q1_native_client_slot_prepared(g, actor->id, &client, NULL)) continue;
        if (!shared || !shared->has_source || shared->source_slot >= g->wire->next_dynamic ||
            (shared->source_slot && shared->source_slot <= g->options.max_clients))
            return q1_save_fail(io, "Q1 wire continuation leaves its actual source entity namespace");
    }
    for (size_t i = 0; i < g->wire->damage_count; ++i) {
        uint32_t client;
        if (!qa_q1_native_client_slot_prepared(g, g->wire->damage[i].recipient, &client, NULL))
            return q1_save_fail(io, "Q1 wire feedback has no actual source client continuation");
    }
    return true;
}
bool q1_save_wire(q1_save_io *io, qa_q1_game *g) {
    bool present = g->wire != NULL;
    Q1_SAVE(io, bool, present);
    if (!present) return true;
    if (io->reading) {
        g->wire = calloc(1, sizeof(*g->wire));
        if (!g->wire) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "restoring Q1 source wire state");
            return false;
        }
    }
    q1_wire_state *wire = g->wire;
    Q1_SAVE(io, u64, wire->generation);
    Q1_SAVE(io, string, wire->map_path);
    Q1_SAVE(io, u32, wire->next_dynamic);
    Q1_SAVE(io, u32, wire->authored_entities);
    Q1_SAVE(io, u32, wire->inline_models);
    Q1_SAVE(io, bool, wire->loading);
    Q1_SAVE(io, bool, wire->id1);
    for (size_t i = 0; i < 64; ++i)
        Q1_SAVE(io, string, wire->lightstyles[i]);
    bool id1 = g->options.program == QA_Q1_ID1 && g->options.edition == QA_Q1_CLASSIC &&
               !g->options.quakeworld;
    if (!wire->generation || !wire->map_path || !wire->authored_entities || wire->id1 != id1 ||
        wire->authored_entities > UINT32_MAX - g->options.max_clients ||
        wire->next_dynamic < wire->authored_entities + g->options.max_clients)
        return q1_save_fail(io, "Invalid Q1 source wire continuation");
    if (!table(io, &wire->models) || !table(io, &wire->sounds)) return false;
    if (wire->models.count < 2 || wire->models.rows[1] != wire->map_path ||
        wire->inline_models > wire->models.count - 2)
        return q1_save_fail(io, "Q1 ordered world model prefix is missing");
    for (uint32_t i = 1; i <= wire->inline_models; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "*%u", i);
        const char *actual = qa_strings_cstr(qa_session_strings(g->services.session), wire->models.rows[i + 1]);
        if (!actual || strcmp(path, actual))
            return q1_save_fail(io, "Q1 ordered inline model prefix differs");
    }
    if (!io->reading && wire->damage_count > UINT32_MAX)
        return q1_save_fail(io, "Q1 feedback extent exceeds its codec");
    uint32_t count = io->reading ? 0 : (uint32_t)wire->damage_count;
    Q1_SAVE(io, u32, count);
    if (count > g->options.max_clients ||
        (io->reading && count > (io->input.size - io->offset) / 35))
        return q1_save_fail(io, "Invalid Q1 source feedback extent");
    if (io->reading && count) {
        wire->damage = calloc(count, sizeof(*wire->damage));
        if (!wire->damage) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "restoring Q1 source feedback");
            return false;
        }
        wire->damage_count = wire->damage_capacity = count;
    }
    for (uint32_t i = 0; i < count; ++i) {
        q1_wire_damage *row = &wire->damage[i];
        Q1_SAVE(io, actor, row->recipient);
        Q1_SAVE(io, float, row->value.armor);
        Q1_SAVE(io, float, row->value.blood);
        Q1_SAVE(io, actor, row->value.inflictor);
        for (size_t axis = 0; axis < 3; ++axis)
            Q1_SAVE(io, double, row->value.origin[axis]);
        if (!row->recipient.registry)
            return q1_save_fail(io, "Q1 source feedback has no recipient");
        for (uint32_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(wire->damage[j].recipient, row->recipient))
                return q1_save_fail(io, "Duplicate Q1 source feedback recipient");
    }
    if (io->reading && g->options.max_clients) {
        if (sizeof(*wire->board) > SIZE_MAX / g->options.max_clients)
            return q1_save_fail(io, "Q1 source client observation allocation overflow");
        wire->board = calloc(g->options.max_clients, sizeof(*wire->board));
        if (!wire->board) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "restoring Q1 source client observations");
            return false;
        }
    }
    for (uint32_t i = 0; i < g->options.max_clients; ++i) {
        q1_wire_client *row = &wire->board[i];
        Q1_SAVE(io, bool, row->present);
        Q1_SAVE(io, actor, row->actor);
        Q1_SAVE(io, string, row->name);
        Q1_SAVE(io, float, row->frags);
        Q1_SAVE(io, u8, row->colors);
        if (row->present != (row->actor.registry != 0) ||
            (!row->present && (row->name || row->frags || row->colors)) ||
            (row->present && (!row->name || !qa_strings_cstr(qa_session_strings(g->services.session), row->name))))
            return q1_save_fail(io, "Invalid retained Q1 source client observation");
    }
    if (g->options.quakeworld) {
        if (g->options.max_clients!=32)
            return q1_save_fail(io,"QW retained stats leave their fixed physical client table");
        for (size_t slot=0;slot<32;++slot)
            for (size_t field=0;field<16;++field) {
                double *value=&wire->qw_client_stats[slot][field];
                Q1_SAVE(io,double,*value);
                if (!isfinite(*value) || trunc(*value)!=*value)
                    return q1_save_fail(io,"Invalid QW retained physical source stat");
            }
        for (size_t slot=0;slot<32;++slot)
            if (wire->qw_client_stats[slot][15]<INT32_MIN || wire->qw_client_stats[slot][15]>INT32_MAX ||
                wire->qw_client_stats[slot][2]<0 || wire->qw_client_stats[slot][2]>=(double)wire->models.count)
                return q1_save_fail(io,"QW retained stat items or model leave their physical source fields");
    }
    return true;
}

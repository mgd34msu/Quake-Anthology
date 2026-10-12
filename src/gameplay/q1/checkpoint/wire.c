#include "internal.h"
#include "../wire_internal.h"
#include <stdio.h>
#include <math.h>

static bool table(q1_save_io *io, q1_wire_table *rows) {
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && rows->count > UINT32_MAX)
        return q1_save_fail(io, "Q1 ordered precache extent exceeds its codec");
    uint32_t count = (io->values.direction == QA_SOURCE_SAVE_READ) ? 0 : (uint32_t)rows->count;
    Q1_SAVE(io, u32, count);
    if (!count || ((io->values.direction == QA_SOURCE_SAVE_READ) && count > (io->values.input.size - io->values.offset) / 4))
        return q1_save_fail(io, "Invalid Q1 ordered precache extent");
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
        if (sizeof(*rows->rows) > SIZE_MAX / count)
            return q1_save_fail(io, "Q1 ordered precache allocation overflow");
        rows->rows = calloc(count, sizeof(*rows->rows));
        if (!rows->rows) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "restoring ordered Q1 precaches");
            return false;
        }
        rows->count = rows->capacity = count;
        rows->ordinals.storage = &rows->ordinal_storage;
        if (!qa_id_index_reserve(&rows->ordinals, count, io->values.error)) return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        Q1_SAVE(io, string, rows->rows[i]);
        if (!i) {
            if (rows->rows[i]) return q1_save_fail(io, "Q1 ordered precache zero is not empty");
            if (io->values.direction == QA_SOURCE_SAVE_READ)
                qa_id_index_insert(&rows->ordinals, 0, 0, 0);
            continue;
        }
        const char *path = qa_strings_cstr(qa_session_strings(io->game->services.session), rows->rows[i]);
        if (!path || (unsigned char)*path <= 32)
            return q1_save_fail(io, "Invalid Q1 ordered precache resource");
        const qa_id_index_entry *entry = qa_id_index_find(&rows->ordinals, rows->rows[i], 0);
        if (entry && (io->values.direction == QA_SOURCE_SAVE_READ || entry->value != i))
            return q1_save_fail(io, "Duplicate Q1 ordered precache declaration");
        if (io->values.direction == QA_SOURCE_SAVE_READ)
            qa_id_index_insert(&rows->ordinals, rows->rows[i], 0, i);
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
            g->wire->edicts[shared->source_slot].free ||
            (shared->source_slot && shared->source_slot <= g->options.max_clients))
            return q1_save_fail(io, "Q1 wire continuation leaves its actual source entity namespace");
    }
    return true;
}
bool q1_save_wire(q1_save_io *io, qa_q1_game *g) {
    bool present = g->wire != NULL;
    Q1_SAVE(io, bool, present);
    if (!present) return true;
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
        g->wire = calloc(1, sizeof(*g->wire));
        if (!g->wire) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "restoring Q1 source wire state");
            return false;
        }
    }
    q1_wire_state *wire = g->wire;
    if (g->options.rules == QA_RULESET_QUAKEWORLD) {
        q1_qw_fraglog *log = &wire->qw_fraglog;
        Q1_SAVE(io, u32, log->sequence);
        Q1_SAVE(io, double, log->time);
        if (!log->sequence || log->sequence > INT32_MAX || !isfinite(log->time) || log->time < 0)
            return q1_save_fail(io, "Invalid QuakeWorld server fraglog clock");
        for (size_t i = 0; i < 2; ++i) {
            Q1_SAVE(io, u32, log->sizes[i]);
            Q1_SAVE(io, bool, log->overflowed[i]);
            if (log->sizes[i] > sizeof(log->buffers[i]) ||
                !q1_save_bytes(io, log->buffers[i], log->sizes[i]))
                return q1_save_fail(io, "Invalid QuakeWorld server fraglog extent");
            if (log->sizes[i] && log->buffers[i][log->sizes[i] - 1])
                return q1_save_fail(io, "QuakeWorld server fraglog lost its trailing NUL");
        }
    }
    Q1_SAVE(io, u64, wire->generation);
    Q1_SAVE(io, string, wire->map_path);
    Q1_SAVE(io, u32, wire->next_dynamic);
    Q1_SAVE(io, u32, wire->authored_entities);
    Q1_SAVE(io, u32, wire->authored_cursor);
    Q1_SAVE(io, u32, wire->edict_limit);
    Q1_SAVE(io, u32, wire->inline_models);
    Q1_SAVE(io, bool, wire->loading);
    Q1_SAVE(io, bool, wire->id1);
    wire->lightstyles.count=64;
    for (size_t i = 0; i < 64; ++i)
        Q1_SAVE(io, string, wire->lightstyles.patterns[i]);
    bool id1 = g->options.program == QA_Q1_ID1 && g->options.edition == QA_Q1_CLASSIC &&
               g->options.rules != QA_RULESET_QUAKEWORLD;
    if (!wire->generation || !wire->map_path || !wire->authored_entities || wire->id1 != id1 ||
        wire->authored_cursor > wire->authored_entities ||
        wire->edict_limit != (g->options.rules == QA_RULESET_QUAKEWORLD ? 768u :
            g->options.edition == QA_Q1_CLASSIC ? 600u : 0u) ||
        wire->next_dynamic <= g->options.max_clients ||
        (wire->edict_limit && wire->next_dynamic > wire->edict_limit))
        return q1_save_fail(io, "Invalid Q1 source wire continuation");
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
        if (wire->next_dynamic > (io->values.input.size - io->values.offset) / 5)
            return q1_save_fail(io, "Invalid Q1 physical Source edict continuation extent");
        if (!q1_wire_edict_extent(wire, wire->next_dynamic, io->values.error)) return false;
    }
    for (uint32_t slot = 0; slot < wire->next_dynamic; ++slot) {
        Q1_SAVE(io, bool, wire->edicts[slot].free);
        Q1_SAVE(io, float, wire->edicts[slot].freetime);
        if (!isfinite(wire->edicts[slot].freetime) || wire->edicts[slot].freetime < 0 ||
            (slot && slot <= g->options.max_clients &&
                (wire->edicts[slot].free || wire->edicts[slot].freetime != 0)))
            return q1_save_fail(io, "Invalid Q1 physical Source edict free continuation");
    }
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
    uint32_t count=0;
    Q1_SAVE(io,u32,count);
    for(uint32_t i=0;i<count;++i) {
        qa_actor_id recipient={0},inflictor={0}; float armor=0,blood=0; double origin[3]={0};
        Q1_SAVE(io,actor,recipient); Q1_SAVE(io,float,armor); Q1_SAVE(io,float,blood);
        Q1_SAVE(io,actor,inflictor);
        for(size_t axis=0;axis<3;++axis) Q1_SAVE(io,double,origin[axis]);
    }
    if ((io->values.direction == QA_SOURCE_SAVE_READ) && g->options.max_clients) {
        if (sizeof(*wire->board) > SIZE_MAX / g->options.max_clients)
            return q1_save_fail(io, "Q1 source client observation allocation overflow");
        wire->board = calloc(g->options.max_clients, sizeof(*wire->board));
        if (!wire->board) {
            qa_error_set(io->values.error, QA_ERROR_MEMORY, io->values.offset, "restoring Q1 source client observations");
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
        Q1_SAVE(io, vector, row->eye);
        if (row->present != (row->actor.registry != 0) ||
            (!row->present && (row->name || row->frags != 0 || row->colors)) ||
            (row->present && (!row->name || !qa_strings_cstr(qa_session_strings(g->services.session), row->name))))
            return q1_save_fail(io, "Invalid retained Q1 source client observation");
    }
    if (g->options.rules == QA_RULESET_QUAKEWORLD) {
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

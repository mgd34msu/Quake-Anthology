#include "internal.h"
#include "qa/network_save.h"
#include "../q3/save_fields.h"

static bool movement_valid(const qa_movement_command *m)
{
    return (unsigned)m->kind <= QA_RULESET_Q3 && isfinite(m->acknowledged_server_seconds) &&
        isfinite(m->angles.x) && isfinite(m->angles.y) && isfinite(m->angles.z) &&
        isfinite(m->forward_move) && isfinite(m->side_move) && isfinite(m->up_move);
}
static bool put_movement(qa_net_writer *w, const qa_movement_command *m)
{
    if (!movement_valid(m)) return qa_net_writer_fail(w, "Invalid retained network movement command");
    return qa_net_write_u32(w, m->kind) && qa_net_write_u64(w, m->sequence) &&
        qa_net_write_u32(w, m->milliseconds) && qa_net_write_i32(w, m->server_time_ms) &&
        qa_net_write_i32(w, m->server_frame) && qa_net_write_f64(w, m->acknowledged_server_seconds) &&
        qa_net_write_f32(w, m->angles.x) && qa_net_write_f32(w, m->angles.y) && qa_net_write_f32(w, m->angles.z) &&
        q3_save_ints(w, m->angle_words, 3) && qa_net_write_f32(w, m->forward_move) &&
        qa_net_write_f32(w, m->side_move) && qa_net_write_f32(w, m->up_move) && qa_net_write_u32(w, m->buttons) &&
        qa_net_write_u8(w, m->impulse) && qa_net_write_u8(w, m->light_level) && qa_net_write_u8(w, m->weapon);
}
static bool get_movement(qa_net_reader *r, qa_movement_command *m)
{
    m->kind = (qa_ruleset_id)qa_net_read_u32(r); m->sequence = qa_net_read_u64(r);
    m->milliseconds = qa_net_read_u32(r); m->server_time_ms = qa_net_read_i32(r); m->server_frame = qa_net_read_i32(r);
    m->acknowledged_server_seconds = qa_net_read_f64(r);
    m->angles.x = qa_net_read_f32(r); m->angles.y = qa_net_read_f32(r); m->angles.z = qa_net_read_f32(r);
    if (!q3_restore_ints(r, m->angle_words, 3)) return false;
    m->forward_move = qa_net_read_f32(r); m->side_move = qa_net_read_f32(r); m->up_move = qa_net_read_f32(r);
    m->buttons = qa_net_read_u32(r); m->impulse = qa_net_read_u8(r);
    m->light_level = qa_net_read_u8(r); m->weapon = qa_net_read_u8(r);
    return (!r->failed && movement_valid(m)) || qa_net_reader_fail(r, "Invalid saved network movement command");
}
static bool history_valid(const qa_network_peer *peer, const qa_network_seat *s, qa_error *error)
{
    if (s->applying || (!s->has_submitted && s->submitted) || (s->has_submitted && !s->submitted) ||
        (!s->has_accepted && s->accepted) || (!s->has_snapshot && (s->acknowledged || s->snapshot)) ||
        s->acknowledged > s->submitted || s->submitted - s->acknowledged > QA_NETWORK_COMMAND_BACKUP) goto invalid;
    for (size_t i = 0; i < QA_NETWORK_COMMAND_BACKUP; ++i) {
        const qa_network_history_entry *entry = &s->history[i];
        if (!entry->valid) continue;
        const qa_network_command *c = &entry->command;
        if (!s->has_submitted || !qa_net_client_id_equal(c->client, peer->id) ||
            c->seat.owner != s->id.owner || c->seat.index != s->id.index || c->epoch != peer->epoch ||
            !c->actor.registry || !c->actor.generation || !movement_valid(&c->movement) ||
            !c->movement.sequence || c->movement.sequence <= s->acknowledged || c->movement.sequence > s->submitted ||
            c->movement.sequence % QA_NETWORK_COMMAND_BACKUP != i) goto invalid;
        if (c->has_arsenal && (!c->arsenal.provider.size || c->arsenal.weapon.size > SIZE_MAX - c->arsenal.provider.size ||
            entry->arsenal.size != c->arsenal.provider.size + c->arsenal.weapon.size ||
            !entry->arsenal.data || c->arsenal.provider.data != entry->arsenal.data ||
            c->arsenal.weapon.data != entry->arsenal.data + c->arsenal.provider.size)) goto invalid;
        if (!c->has_arsenal && entry->arsenal.size) goto invalid;
    }
    for (uint64_t i = 1; i <= s->submitted - s->acknowledged; ++i) {
        uint64_t sequence = s->acknowledged + i;
        const qa_network_history_entry *entry = &s->history[sequence % QA_NETWORK_COMMAND_BACKUP];
        if (!entry->valid || entry->command.movement.sequence != sequence) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid network prediction sequence or command ownership"); return false;
}

bool qa_network_prediction_checkpoint(const qa_network_runtime *runtime, const qa_network_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!runtime || !refs || !refs->save_actor || !out || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Prediction capture requires idle qualified actor references");
    size_t size = 12; uint32_t count = 0;
    for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i]; if (!peer->occupied) continue;
        if (size > SIZE_MAX - 24 || peer->seat_count > (SIZE_MAX - size - 24) / (512 * QA_NETWORK_COMMAND_BACKUP))
            return qa_network_fail(error, "Prediction owner inventory extent overflow");
        size += 24 + peer->seat_count * 512 * QA_NETWORK_COMMAND_BACKUP; ++count;
        for (size_t j = 0; j < peer->seat_count; ++j) {
            const qa_network_seat *s = &peer->seats[j];
            if (!history_valid(peer, s, error)) return false;
            for (size_t k = 0; k < QA_NETWORK_COMMAND_BACKUP; ++k) {
                const qa_network_history_entry *entry = &s->history[k]; if (!entry->valid) continue;
                if (entry->arsenal.size > SIZE_MAX - size) return qa_network_fail(error, "Prediction arsenal extent overflow");
                size += entry->arsenal.size;
            }
        }
    }
    qa_buffer bytes = {malloc(size), 0};
    if (!bytes.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating prediction continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, bytes.data, size, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x504e4151)) && qa_net_write_u32(&w, count);
    for (uint32_t i = 0; ok && i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i]; if (!peer->occupied) continue;
        ok = qa_net_write_u32(&w, i) && qa_net_write_u64(&w, peer->id.generation) &&
            qa_net_write_u64(&w, peer->epoch) && qa_net_write_u32(&w, (uint32_t)peer->seat_count);
        for (size_t j = 0; ok && j < peer->seat_count; ++j) {
            const qa_network_seat *s = &peer->seats[j];
            ok = qa_net_write_u32(&w, s->id.index) && qa_net_write_u64(&w, s->submitted) &&
                qa_net_write_u64(&w, s->accepted) && qa_net_write_u64(&w, s->acknowledged) && qa_net_write_u64(&w, s->snapshot) &&
                qa_net_write_u8(&w, s->has_submitted) && qa_net_write_u8(&w, s->has_accepted) &&
                qa_net_write_u8(&w, s->has_snapshot) && qa_net_write_u8(&w, s->prediction_fault);
            for (size_t k = 0; ok && k < QA_NETWORK_COMMAND_BACKUP; ++k) {
                const qa_network_history_entry *entry = &s->history[k];
                ok = qa_net_write_u8(&w, entry->valid); if (!ok || !entry->valid) continue;
                const qa_network_command *c = &entry->command; qa_saved_actor_id actor;
                ok = refs->save_actor(refs->context, c->actor, &actor, error) &&
                    qa_net_write_u64(&w, actor.generation) && qa_net_write_u32(&w, actor.slot) &&
                    put_movement(&w, &c->movement) && qa_net_write_u8(&w, c->has_arsenal);
                if (ok && c->has_arsenal)
                    ok = qa_net_write_u64(&w, c->arsenal.provider.size) && qa_net_write_u64(&w, c->arsenal.weapon.size) &&
                        qa_net_write_u8(&w, c->arsenal.use_holdable) &&
                        qa_net_write_u8(&w, c->arsenal.has_impulse) && qa_net_write_u8(&w,c->arsenal.impulse) &&
                        qa_net_write_data(&w, entry->arsenal.data, entry->arsenal.size);
            }
        }
    }
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}

bool qa_network_prediction_restore(qa_network_runtime *runtime, const qa_network_checkpoint_refs *refs,
    qa_bytes bytes, qa_error *error)
{
    if (!runtime || !refs || !refs->restore_actor || !bytes.data || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Prediction restore requires an isolated idle connection owner");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    uint32_t tag = qa_net_read_u32(&r), count = qa_net_read_u32(&r);
    if (tag != UINT32_C(0x504e4151) || count > runtime->options.clients)
        return qa_net_reader_fail(&r, "Invalid prediction continuation header");
    qa_network_seat **scratch = calloc(runtime->options.clients, sizeof(*scratch));
    if (!scratch) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating prediction owner inventory"); return false; }
    bool ok = true; uint32_t previous = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t slot = qa_net_read_u32(&r); uint64_t generation = qa_net_read_u64(&r), epoch = qa_net_read_u64(&r);
        uint32_t seats = qa_net_read_u32(&r);
        if (r.failed || slot >= runtime->options.clients || (i && slot <= previous)) {
            ok = qa_net_reader_fail(&r, "Invalid prediction owner ordering"); break;
        }
        previous = slot; qa_network_peer *peer = &runtime->peers[slot];
        if (!peer->occupied || peer->id.generation != generation || peer->epoch != epoch || seats != peer->seat_count) {
            ok = qa_net_reader_fail(&r, "Prediction owner differs from restored source connection"); break;
        }
        scratch[slot] = calloc(seats, sizeof(**scratch));
        if (!scratch[slot]) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating prediction seat history"); ok = false; break; }
        for (size_t j = 0; ok && j < seats; ++j) {
            qa_network_seat *s = &scratch[slot][j]; s->id = (qa_net_seat_id){peer->id.owner, qa_net_read_u32(&r)};
            s->submitted = qa_net_read_u64(&r); s->accepted = qa_net_read_u64(&r);
            s->acknowledged = qa_net_read_u64(&r); s->snapshot = qa_net_read_u64(&r);
            s->has_submitted = q3_save_bool(&r); s->has_accepted = q3_save_bool(&r);
            s->has_snapshot = q3_save_bool(&r); s->prediction_fault = q3_save_bool(&r);
            if (s->id.index != peer->seats[j].id.index) { ok = qa_net_reader_fail(&r, "Prediction seat binding differs"); break; }
            for (size_t k = 0; ok && k < QA_NETWORK_COMMAND_BACKUP; ++k) {
                qa_network_history_entry *entry = &s->history[k]; entry->valid = q3_save_bool(&r);
                if (r.failed) { ok = false; break; } if (!entry->valid) continue;
                qa_network_command *c = &entry->command;
                qa_saved_actor_id actor;
                actor.generation = qa_net_read_u64(&r); actor.slot = qa_net_read_u32(&r);
                c->client = peer->id; c->seat = s->id; c->epoch = peer->epoch;
                ok = !r.failed && refs->restore_actor(refs->context, actor, &c->actor, error) && get_movement(&r, &c->movement);
                if (!ok) break;
                c->has_arsenal = q3_save_bool(&r);
                if (c->has_arsenal) {
                    uint64_t provider = qa_net_read_u64(&r), weapon = qa_net_read_u64(&r);
                    c->arsenal.use_holdable = q3_save_bool(&r);
                    c->arsenal.has_impulse = q3_save_bool(&r);
                    c->arsenal.impulse = qa_net_read_u8(&r);
                    if (r.failed || !provider || provider > SIZE_MAX || weapon > SIZE_MAX - (size_t)provider ||
                        provider + weapon > qa_net_reader_remaining(&r)) {
                        ok = qa_net_reader_fail(&r, "Invalid prediction arsenal storage extent"); break;
                    }
                    entry->arsenal.size = (size_t)(provider + weapon); entry->arsenal.data = malloc(entry->arsenal.size);
                    if (!entry->arsenal.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring prediction arsenal"); ok = false; break; }
                    c->arsenal.provider = (qa_bytes){entry->arsenal.data, (size_t)provider};
                    c->arsenal.weapon = (qa_bytes){entry->arsenal.data + provider, (size_t)weapon};
                    ok = qa_net_read_data(&r, entry->arsenal.data, entry->arsenal.size);
                }
                if (ok && !r.failed) {
                    bool previous_callback = runtime->callback;
                    runtime->callback = true; peer->seats[j].applying = true;
                    ok = runtime->options.hooks.controlled(runtime->options.hooks.context, c->client,
                        c->seat, c->actor, c->movement.kind,
                        c->has_arsenal ? c->arsenal.provider : (qa_bytes){0}, error);
                    peer->seats[j].applying = false; runtime->callback = previous_callback;
                }
            }
            if (ok) ok = history_valid(peer, s, error);
        }
    }
    for (uint32_t i = 0; ok && i < runtime->options.clients; ++i)
        if (runtime->peers[i].occupied && !scratch[i]) ok = qa_net_reader_fail(&r, "Prediction inventory omits an installed connection");
    if (ok) ok = qa_net_reader_finish(&r);
    if (ok) for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        qa_network_peer *peer = &runtime->peers[i]; if (!peer->occupied) continue;
        qa_network_seat *old = peer->seats; peer->seats = scratch[i]; scratch[i] = old;
    }
    for (uint32_t i = 0; i < runtime->options.clients; ++i) if (scratch[i]) {
        for (size_t j = 0; j < runtime->peers[i].seat_count; ++j)
            for (size_t k = 0; k < QA_NETWORK_COMMAND_BACKUP; ++k) qa_buffer_free(&scratch[i][j].history[k].arsenal);
        free(scratch[i]);
    }
    free(scratch); return ok;
}

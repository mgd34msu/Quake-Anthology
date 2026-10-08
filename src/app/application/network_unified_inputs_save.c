#include "network_unified_inputs_save.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool vector(qa_source_save_io *io, qa_unified_vec3 *v)
{ return qa_source_save_f64(io, &v->x) && qa_source_save_f64(io, &v->y) && qa_source_save_f64(io, &v->z); }
static bool words(qa_source_save_io *io, double v[3])
{ return qa_source_save_f64(io, v) && qa_source_save_f64(io, v + 1) && qa_source_save_f64(io, v + 2); }
static bool movement(qa_source_save_io *io, qa_unified_movement *m)
{
    uint32_t kind = (uint32_t)m->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_MOVEMENT_Q3) return false;
    m->kind = (qa_movement_kind)kind;
    switch (m->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return qa_source_save_f64(io, &m->data.nq.acknowledged_seconds) && vector(io, &m->data.nq.angles) &&
            qa_source_save_f64(io, &m->data.nq.forward) && qa_source_save_f64(io, &m->data.nq.side) &&
            qa_source_save_f64(io, &m->data.nq.up) && qa_source_save_f64(io, &m->data.nq.buttons) &&
            qa_source_save_f64(io, &m->data.nq.impulse);
    case QA_MOVEMENT_QUAKEWORLD:
        return qa_source_save_f64(io, &m->data.qw.milliseconds) && vector(io, &m->data.qw.angles) &&
            qa_source_save_f64(io, &m->data.qw.forward) && qa_source_save_f64(io, &m->data.qw.side) &&
            qa_source_save_f64(io, &m->data.qw.up) && qa_source_save_f64(io, &m->data.qw.buttons) &&
            qa_source_save_f64(io, &m->data.qw.impulse);
    case QA_MOVEMENT_Q2_CLASSIC:
        return qa_source_save_f64(io, &m->data.q2.milliseconds) && words(io, m->data.q2.angle_shorts) &&
            qa_source_save_f64(io, &m->data.q2.forward) && qa_source_save_f64(io, &m->data.q2.side) &&
            qa_source_save_f64(io, &m->data.q2.up) && qa_source_save_f64(io, &m->data.q2.buttons) &&
            qa_source_save_f64(io, &m->data.q2.impulse) && qa_source_save_f64(io, &m->data.q2.light_level);
    case QA_MOVEMENT_Q2_RERELEASE:
        return qa_source_save_f64(io, &m->data.q2r.milliseconds) && vector(io, &m->data.q2r.angles) &&
            qa_source_save_f64(io, &m->data.q2r.forward) && qa_source_save_f64(io, &m->data.q2r.side) &&
            qa_source_save_f64(io, &m->data.q2r.buttons) && qa_source_save_f64(io, &m->data.q2r.server_frame);
    case QA_MOVEMENT_Q3:
        return qa_source_save_f64(io, &m->data.q3.server_time_ms) && words(io, m->data.q3.angle_words) &&
            qa_source_save_f64(io, &m->data.q3.buttons) && qa_source_save_f64(io, &m->data.q3.weapon) &&
            qa_source_save_f64(io, &m->data.q3.forward) && qa_source_save_f64(io, &m->data.q3.right) &&
            qa_source_save_f64(io, &m->data.q3.up);
    }
    return false;
}
static bool input(qa_source_save_io *io, retained_input *row, uint32_t epoch,
    const qa_unified_session_player *player)
{
    qa_unified_input *value = &row->value;
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    if (!qa_source_save_u64(io, &value->sequence) || value->sequence > QA_UNIFIED_SAFE_INTEGER ||
        !movement(io, &value->command) || value->command.kind != player->movement ||
        !qa_source_save_bool(io, &value->has_arsenal)) return false;
    if (value->has_arsenal) {
        if (writing && (value->arsenal.provider.data != row->provider.data ||
            value->arsenal.provider.size != row->provider.size || value->arsenal.weapon.data != row->weapon.data ||
            value->arsenal.weapon.size != row->weapon.size)) return false;
        if (!application_unified_save_blob(io, &row->provider) ||
            !application_unified_save_blob(io, &row->weapon) ||
            !qa_source_save_bool(io, &value->arsenal.use_holdable) ||
            !qa_source_save_bool(io, &value->arsenal.has_impulse) ||
            !qa_source_save_u8(io, &value->arsenal.impulse) ||
            row->provider.size != player->arsenal.size ||
            (row->provider.size && memcmp(row->provider.data, player->arsenal.data, row->provider.size))) return false;
        if (!writing) {
            row->provider_capacity = row->provider.size;
            row->weapon_capacity = row->weapon.size;
        }
        value->arsenal.provider = (qa_bytes){row->provider.data, row->provider.size};
        value->arsenal.weapon = (qa_bytes){row->weapon.data, row->weapon.size};
    }
    /* Reuse the actual public wire schema for value qualification while the
     * saved representation above retains each original binary64 bit. */
    qa_unified_document *document = NULL;
    bool okay = qa_unified_inputs_document(epoch, value, 1, &document, io->error);
    qa_unified_document_destroy(document);
    return okay;
}
bool application_unified_inputs_save(qa_source_save_io *io, application_unified_inputs **slot,
    qa_application *app, qa_network_runtime *runtime, qa_net_client_id client, qa_net_seat_id seat,
    uint32_t epoch, const application_unified_source *source, const qa_unified_session_player *player)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    bool present = *slot != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    if (!player) return false;
    if (!writing) {
        *slot = calloc(1, sizeof(**slot));
        if (!*slot) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring retained Source input programme");
        **slot = (application_unified_inputs){.application = app, .runtime = runtime, .client = client,
            .seat = seat, .actor = player->actor, .source = source->owner, .source_slot = player->source_slot,
            .publication = source->publication, .map_revision = source->map_revision, .epoch = epoch};
    }
    application_unified_inputs *owner = *slot;
    uint32_t wire_epoch = owner->epoch;
    uint64_t publication = owner->publication, map = owner->map_revision;
    if (owner->advancing || owner->application != app || owner->runtime != runtime ||
        !qa_net_client_id_equal(owner->client, client) || owner->seat.owner != seat.owner || owner->seat.index != seat.index ||
        !qa_actor_id_equal(owner->actor, player->actor) || owner->source != source->owner ||
        owner->source_slot != player->source_slot || !application_unified_save_player(io, player) ||
        !qa_source_save_u32(io, &wire_epoch) || wire_epoch != epoch ||
        !qa_source_save_u64(io, &publication) || publication != source->publication ||
        !qa_source_save_u64(io, &map) || map != source->map_revision ||
        !qa_source_save_u64(io, &owner->runtime_epoch) || !owner->runtime_epoch ||
        !qa_source_save_i64(io, &owner->queued) || !qa_source_save_i64(io, &owner->submitted) ||
        owner->queued < -1 || owner->submitted < -1 || owner->submitted > owner->queued ||
        owner->queued > (int64_t)QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_count(io, &owner->count, SIZE_MAX / sizeof(*owner->commands)) ||
        !qa_source_save_count(io, &owner->cursor, owner->count)) return false;
    if (!writing) {
        if (owner->count > (io->input.size - io->offset) / 13) return false;
        size_t capacity = owner->count > 64 ? owner->count : 64;
        owner->commands = calloc(capacity, sizeof(*owner->commands));
        if (!owner->commands)
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring ordered binary64 Source commands");
        owner->capacity = capacity;
    }
    if (owner->count && !owner->commands) return false;
    int64_t previous = -1;
    for (size_t i = 0; i < owner->count; ++i) {
        if (!input(io, owner->commands + i, epoch, player)) return false;
        int64_t sequence = (int64_t)owner->commands[i].value.sequence;
        if (sequence <= previous || sequence > owner->queued ||
            (i < owner->cursor ? sequence > owner->submitted : sequence <= owner->submitted)) return false;
        previous = sequence;
    }
    if (owner->count && previous != owner->queued) return false;
    if (!owner->count && (owner->cursor || owner->queued != owner->submitted)) return false;
    return !writing || qa_network_epoch(runtime, client) == owner->runtime_epoch;
}

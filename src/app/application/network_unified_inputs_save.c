#include "network_unified_inputs_save.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool selection(qa_source_save_io *io, qa_strings *names, qa_string_id *name,
    qa_bytes *view)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    qa_bytes text = writing ? qa_strings_text(names, *name) : (qa_bytes){0};
    size_t size = text.size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (writing) {
        if (!qa_source_save_bytes(io, (void *)text.data, size)) return false;
    } else {
        if (!qa_source_save_span(io, size, &text)) return false;
        *name = QA_STRING_NONE;
        if (size && !qa_strings_intern(names, text, name, io->error)) return false;
    }
    *view = qa_strings_text(names, *name);
    return true;
}
static bool input(qa_source_save_io *io, qa_usercmd *value, qa_strings *names, uint32_t epoch,
    const qa_unified_session_player *player)
{
    if (!qa_source_save_usercmd(io, value) || value->sequence > QA_UNIFIED_SAFE_INTEGER ||
        value->kind != player->movement ||
        !qa_source_save_bool(io, &value->has_arsenal)) return false;
    if (value->has_arsenal) {
        value->arsenal.names = names;
        if (!selection(io, names, &value->arsenal.provider_id, &value->arsenal.provider) ||
            !selection(io, names, &value->arsenal.weapon_id, &value->arsenal.weapon) ||
            !qa_source_save_bool(io, &value->arsenal.use_holdable) ||
            !qa_source_save_bool(io, &value->arsenal.has_impulse) ||
            !qa_source_save_u8(io, &value->arsenal.impulse) ||
            value->arsenal.provider.size != player->arsenal.size ||
            (value->arsenal.provider.size && memcmp(value->arsenal.provider.data,
                player->arsenal.data, value->arsenal.provider.size))) return false;
    }
    /* Apply the same command schema at the save boundary. */
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
        (*slot)->names = qa_session_strings(app->session);
        qa_strings_retain((*slot)->names);
        (*slot)->arsenal = qa_strings_find((*slot)->names, player->arsenal);
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
        !qa_source_save_count(io, &owner->count, SIZE_MAX / sizeof(qa_usercmd)) ||
        !qa_source_save_count(io, &owner->cursor, owner->count)) return false;
    if (!writing) {
        if (owner->count > (io->input.size - io->offset) / 13) return false;
        if (owner->count && !application_unified_inputs_group_take(owner, owner->count, io->error)) return false;
    }
    if (owner->count && !owner->first) return false;
    int64_t previous = -1;
    size_t ordinal = 0;
    for (application_unified_input_group *group = owner->first; group; group = group->next)
    for (size_t i = 0; i < group->count; ++i, ++ordinal) {
        qa_usercmd *command = group->commands + i;
        if (!input(io, command, owner->names, epoch, player)) return false;
        int64_t sequence = (int64_t)command->sequence;
        if (sequence <= previous || sequence > owner->queued ||
            (ordinal < owner->cursor ? sequence > owner->submitted : sequence <= owner->submitted)) return false;
        previous = sequence;
    }
    if (owner->count && previous != owner->queued) return false;
    if (!owner->count && (owner->cursor || owner->queued != owner->submitted)) return false;
    return !writing || qa_network_epoch(runtime, client) == owner->runtime_epoch;
}

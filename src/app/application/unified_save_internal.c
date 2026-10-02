#include "unified_save_internal.h"
#include "map_players_private.h"

#include <stdlib.h>
#include <string.h>

bool application_unified_save_source_read(qa_application *app, application_unified_source *out, qa_error *e)
{
    return app && (app->operation == APPLICATION_PERSISTING ?
        application_unified_source_checkpoint_read(app, out, e) :
        application_unified_source_read(app, out, e));
}
bool application_unified_save_player_read(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, qa_unified_session_player *out, qa_error *e)
{
    return app && (app->operation == APPLICATION_PERSISTING ?
        application_unified_player_checkpoint_read(app, client, seat, out, e) :
        application_unified_player_read(app, client, seat, out, e));
}
bool application_unified_save_magic(qa_source_save_io *io, const char expected[4])
{
    char magic[4]; memcpy(magic, expected, sizeof(magic));
    uint32_t version = 1;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, expected, sizeof(magic)) &&
        qa_source_save_u32(io, &version) && version == 1;
}
bool application_unified_save_blob(qa_source_save_io *io, qa_buffer *buffer)
{
    size_t size = buffer->size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset) return false;
        buffer->data = size ? malloc(size) : NULL;
        if (size && !buffer->data)
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring retained Unified child bytes");
        buffer->size = size;
    }
    return (!size || buffer->data) && qa_source_save_bytes(io, buffer->data, size);
}
bool application_unified_save_document(qa_source_save_io *io, qa_unified_document **doc,
    qa_unified_document_kind kind)
{
    bool present = *doc != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    qa_buffer bytes = {0};
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    bool okay = !writing || (qa_unified_document_type(*doc) == kind &&
        qa_unified_document_encode(*doc, &bytes, io->error));
    if (okay) okay = application_unified_save_blob(io, &bytes) && bytes.size;
    if (okay && !writing) okay = qa_unified_document_decode(kind,
        (qa_bytes){bytes.data, bytes.size}, doc, io->error);
    qa_buffer_free(&bytes);
    return okay;
}
bool application_unified_save_output(qa_source_save_io *io, application_unified_output *output)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    size_t count = output->control_count;
    if (!application_unified_save_document(io, &output->frame, QA_UNIFIED_FRAME_DOCUMENT) ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(*output->controls))) return false;
    if (!writing) {
        if (count > io->input.size - io->offset) return false;
        output->controls = count ? calloc(count, sizeof(*output->controls)) : NULL;
        if (count && !output->controls)
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring actual Unified prerequisite order");
        output->control_count = count;
    }
    if (count && !output->controls) return false;
    for (size_t i = 0; i < count; ++i)
        if (!application_unified_save_document(io, output->controls + i, QA_UNIFIED_CONTROL_DOCUMENT) ||
            !output->controls[i]) return false;
    return output->frame || !count;
}
bool application_unified_save_client(qa_source_save_io *io, qa_net_client_id actual)
{
    qa_net_client_id saved = actual;
    return qa_source_save_u64(io, &saved.owner) && qa_source_save_u64(io, &saved.generation) &&
        qa_source_save_u32(io, &saved.slot) && qa_net_client_id_equal(saved, actual);
}
static bool exact_text(qa_source_save_io *io, const char *actual)
{
    const char *saved = actual;
    return actual && qa_source_save_text(io, &saved) && saved && !strcmp(saved, actual);
}
static bool frame_fields(qa_source_save_io *io, qa_source_frame *frame)
{
    uint32_t kind = (uint32_t)frame->kind, phase = (uint32_t)frame->phase;
    if (!qa_source_save_string(io, &frame->provider) || !qa_source_save_u32(io, &kind) ||
        !qa_source_save_u32(io, &phase) || !qa_source_save_u64(io, &frame->number) ||
        !qa_source_save_u64(io, &frame->start_ns) || !qa_source_save_u64(io, &frame->elapsed_ns) ||
        !qa_source_save_u64(io, &frame->time_ns)) return false;
    frame->kind = (qa_clock_kind)kind; frame->phase = (qa_frame_phase)phase;
    return true;
}
static bool frame_equal(const qa_source_frame *a, const qa_source_frame *b)
{
    return a->provider == b->provider && a->kind == b->kind && a->phase == b->phase &&
        a->number == b->number && a->start_ns == b->start_ns &&
        a->elapsed_ns == b->elapsed_ns && a->time_ns == b->time_ns;
}
bool application_unified_save_source(qa_source_save_io *io, qa_application *app,
    const application_unified_source *actual, application_unified_source *saved, bool historical)
{
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_sha256_digest map, retained;
    if (!primary || !primary->launch || !app->map_resource) return false;
    qa_sha256(qa_resource_bytes(app->map_resource), &map); retained = map;
    const char *map_name = qa_strings_cstr(qa_session_strings(actual->session), app->current_map);
    uint32_t family = (uint32_t)saved->family;
    if (!qa_source_save_bytes(io, retained.bytes, sizeof(retained.bytes)) || !qa_sha256_equal(&map, &retained) ||
        !exact_text(io, map_name) || !exact_text(io, primary->launch->selection.instance) ||
        !qa_source_save_string(io, &saved->owner) || !qa_source_save_u32(io, &family) ||
        !qa_source_save_u64(io, &saved->publication) || !qa_source_save_u64(io, &saved->map_revision) ||
        !qa_source_save_u64(io, &saved->frame_revision) || !qa_source_save_u32(io, &saved->max_clients) ||
        !frame_fields(io, &saved->frame)) return false;
    saved->family = (qa_game_family)family;
    if (saved->owner != actual->owner || saved->family != actual->family ||
        saved->publication != actual->publication || saved->map_revision != actual->map_revision ||
        saved->max_clients != actual->max_clients || saved->frame.provider != actual->frame.provider ||
        saved->frame.kind != actual->frame.kind) return false;
    if (historical) {
        if (saved->frame_revision > actual->frame_revision || saved->frame.number > actual->frame.number ||
            saved->frame.time_ns > actual->frame.time_ns || saved->frame.start_ns > actual->frame.start_ns ||
            (unsigned)saved->frame.phase > QA_FRAME_EXIT) return false;
    } else if (saved->frame_revision != actual->frame_revision || !frame_equal(&saved->frame, &actual->frame)) return false;
    saved->launch = actual->launch; saved->session = actual->session; saved->world = actual->world;
    return true;
}
bool application_unified_save_source_obsolete(const application_unified_source *actual,
    const application_unified_source *saved)
{
    return actual && saved && saved->publication <= actual->publication && saved->map_revision <= actual->map_revision &&
        (saved->publication < actual->publication || saved->map_revision < actual->map_revision);
}
bool application_unified_save_source_stamp_equal(const application_unified_source *a,
    const application_unified_source *b)
{
    return a && b && a->owner == b->owner && a->family == b->family && a->publication == b->publication &&
        a->map_revision == b->map_revision && a->frame_revision == b->frame_revision &&
        a->max_clients == b->max_clients && frame_equal(&a->frame, &b->frame);
}
bool application_unified_save_retained_source(qa_source_save_io *io, qa_application *app,
    const application_unified_source *actual, application_unified_source *saved)
{
    bool obsolete = application_unified_save_source_obsolete(actual, saved);
    if (!qa_source_save_bool(io, &obsolete)) return false;
    if (!obsolete) return application_unified_save_source(io, app, actual, saved, true);
    uint32_t family = (uint32_t)saved->family;
    if (!qa_source_save_string(io, &saved->owner) || !qa_source_save_u32(io, &family) ||
        !qa_source_save_u64(io, &saved->publication) || !qa_source_save_u64(io, &saved->map_revision) ||
        !qa_source_save_u64(io, &saved->frame_revision) || !qa_source_save_u32(io, &saved->max_clients) ||
        !frame_fields(io, &saved->frame)) return false;
    saved->family = (qa_game_family)family;
    if (!saved->owner || family > QA_GAME_Q3 || !saved->publication || !saved->max_clients ||
        saved->max_clients > 256 || !saved->frame.provider ||
        (unsigned)saved->frame.kind > QA_CLOCK_Q3 || (unsigned)saved->frame.phase > QA_FRAME_EXIT ||
        !application_unified_save_source_obsolete(actual, saved)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        saved->launch = NULL; saved->session = NULL; saved->world = NULL;
    }
    return true;
}
bool application_unified_save_player_equal(const qa_unified_session_player *a,
    const qa_unified_session_player *b)
{
    return a && b && qa_actor_id_equal(a->actor, b->actor) && a->seat.owner == b->seat.owner &&
        a->seat.index == b->seat.index && a->movement == b->movement && a->source_owner == b->source_owner &&
        a->source_slot == b->source_slot && a->arsenal.size == b->arsenal.size &&
        (!a->arsenal.size || (a->arsenal.data && b->arsenal.data &&
            !memcmp(a->arsenal.data, b->arsenal.data, a->arsenal.size)));
}
bool application_unified_save_player_owned(qa_source_save_io *io, qa_unified_session_player *player,
    qa_buffer *arsenal)
{
    uint32_t movement = (uint32_t)player->movement;
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        (player->arsenal.data != arsenal->data || player->arsenal.size != arsenal->size)) return false;
    if (!qa_source_save_actor(io, &player->actor) || !player->actor.registry ||
        !qa_source_save_u64(io, &player->seat.owner) || !player->seat.owner ||
        !qa_source_save_u32(io, &player->seat.index) || !qa_source_save_u32(io, &movement) ||
        movement > QA_MOVEMENT_Q3 || !qa_source_save_string(io, &player->source_owner) || !player->source_owner ||
        !qa_source_save_u32(io, &player->source_slot) || !application_unified_save_blob(io, arsenal)) return false;
    player->movement = (qa_movement_kind)movement;
    player->arsenal = (qa_bytes){arsenal->data, arsenal->size};
    return true;
}
bool application_unified_save_player(qa_source_save_io *io, const qa_unified_session_player *actual)
{
    qa_unified_session_player saved = *actual;
    uint32_t movement = (uint32_t)saved.movement;
    qa_buffer arsenal = {0};
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    if (writing) arsenal = (qa_buffer){(uint8_t *)actual->arsenal.data, actual->arsenal.size};
    bool okay = qa_source_save_actor(io, &saved.actor) && qa_actor_id_equal(saved.actor, actual->actor) &&
        qa_source_save_u64(io, &saved.seat.owner) && saved.seat.owner == actual->seat.owner &&
        qa_source_save_u32(io, &saved.seat.index) && saved.seat.index == actual->seat.index &&
        qa_source_save_u32(io, &movement) && movement == (uint32_t)actual->movement &&
        qa_source_save_string(io, &saved.source_owner) && saved.source_owner == actual->source_owner &&
        qa_source_save_u32(io, &saved.source_slot) && saved.source_slot == actual->source_slot &&
        application_unified_save_blob(io, &arsenal) && arsenal.size == actual->arsenal.size &&
        (!arsenal.size || !memcmp(arsenal.data, actual->arsenal.data, arsenal.size));
    if (!writing) qa_buffer_free(&arsenal);
    return okay;
}
static bool document_equal(const qa_unified_document *a, const qa_unified_document *b)
{
    if (!a || !b) return a == b;
    qa_bytes x = qa_json_source(qa_unified_document_json(a), qa_unified_document_root(a));
    qa_bytes y = qa_json_source(qa_unified_document_json(b), qa_unified_document_root(b));
    return qa_unified_document_type(a) == qa_unified_document_type(b) && x.size == y.size &&
        (!x.size || !memcmp(x.data, y.data, x.size));
}
bool application_unified_save_output_equal(const application_unified_output *a,
    const application_unified_output *b)
{
    if (!a || !b || a->control_count != b->control_count || !document_equal(a->frame, b->frame)) return false;
    for (size_t i = 0; i < a->control_count; ++i)
        if (!document_equal(a->controls[i], b->controls[i])) return false;
    return true;
}

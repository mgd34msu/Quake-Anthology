#include "unified_components_save.h"
#include "unified_components_internal.h"
#include "network_unified_private.h"
#include "native_q2_publication.h"
#include "map_players_private.h"
#include "unified_save_internal.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return application_fail(e, QA_ERROR_FORMAT, message); }

static bool source_current(qa_application *app, const application_unified_source *source,
    qa_net_client_id client, const qa_unified_session_player *player)
{
    return (application_unified_source_current(app, source) &&
        application_unified_player_current(app, client, player)) ||
        (application_unified_source_checkpoint_current(app, source) &&
        application_unified_player_checkpoint_current(app, client, player));
}

/* These fields authenticate against the supplied imported graph. No saved
 * address or reconstructed provider callback is used as authority. */
static bool recipient_fields(qa_source_save_io *io, qa_net_client_id client, qa_actor_id actor)
{
    qa_net_client_id saved = client;
    qa_actor_id saved_actor = actor;
    return qa_source_save_u64(io, &saved.owner) && qa_source_save_u64(io, &saved.generation) &&
        qa_source_save_u32(io, &saved.slot) && qa_source_save_actor(io, &saved_actor) &&
        saved.owner == client.owner && saved.generation == client.generation && saved.slot == client.slot &&
        qa_actor_id_equal(saved_actor, actor);
}

static bool source_fields(qa_source_save_io *io, qa_application *app, const application_unified_source *actual,
    application_unified_source *retained, const qa_unified_session_player *player)
{
    application_unified_source saved = *actual;
    application_unified_source expected = *retained;
    return application_unified_save_source(io, app, actual, &saved, false) &&
        application_unified_save_retained_source(io, app, actual, retained) &&
        application_unified_save_source_stamp_equal(retained, &expected) &&
        application_unified_save_player(io, player);
}
static bool magic(qa_source_save_io *io, const char expected[4])
{
    char value[4]; memcpy(value, expected, sizeof(value));
    return qa_source_save_bytes(io, value, sizeof(value)) && !memcmp(value, expected, sizeof(value));
}

static bool cursor_fields(qa_source_save_io *io, component_cursor *row)
{
    uint32_t abi = (uint32_t)row->abi;
    if (!qa_source_save_string(io, &row->owner) || !qa_source_save_u64(io, &row->generation) ||
        !qa_source_save_i64(io, &row->game_state_revision) || !qa_source_save_i32(io, &row->sequence) ||
        !qa_source_save_u32(io, &abi) || !qa_source_save_bool(io, &row->scene) ||
        !qa_source_save_bytes(io, &row->identity, sizeof(row->identity))) return false;
    row->abi = (qa_qvm_abi)abi;
    return row->owner && row->generation && row->generation <= QA_UNIFIED_SAFE_INTEGER &&
        row->game_state_revision >= 0 && row->game_state_revision <= (int64_t)QA_UNIFIED_SAFE_INTEGER &&
        row->sequence >= 0 && (row->scene || !row->sequence) &&
        (row->abi == QA_QVM_Q3_MODERN || row->abi == QA_QVM_Q3_116N);
}

static bool rows_fields(qa_source_save_io *io, component_cursor **rows, size_t *count,
    qa_unified_frame_lease *lease)
{
    if (!qa_source_save_count(io, count, 256)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && *count) {
        *rows = lease ? qa_unified_frame_lease_alloc(lease, *count, sizeof(**rows),
            _Alignof(component_cursor), io->error) : calloc(*count, sizeof(**rows));
        if (!*rows) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining component recipient cursors");
    }
    for (size_t i = 0; i < *count; ++i) {
        if (!cursor_fields(io, *rows + i)) return false;
        for (size_t j = 0; j < i; ++j) if ((*rows)[i].owner == (*rows)[j].owner) return false;
    }
    return true;
}

static bool native_fields(qa_source_save_io *io, native_cursor *row)
{
    if (!qa_source_save_bool(io, &row->present)) return false;
    if (!row->present) return true;
    return qa_source_save_string(io, &row->owner) && qa_source_save_u64(io, &row->activation) &&
        qa_source_save_u64(io, &row->generation) &&
        qa_source_save_bytes(io, &row->identity, sizeof(row->identity)) &&
        qa_source_save_bytes(io, &row->state, sizeof(row->state)) &&
        qa_source_save_bytes(io, &row->configs, sizeof(row->configs)) && row->owner && row->activation &&
        row->activation <= QA_UNIFIED_SAFE_INTEGER && row->generation <= QA_UNIFIED_SAFE_INTEGER;
}

static bool hash_document(const qa_unified_document *doc, qa_sha256_digest *digest, qa_error *e)
{
    if (!doc) return bad(e, "Component cursor has no immutable identity");
    qa_buffer canonical = {0};
    bool ok = qa_unified_value_canonical(qa_json_source(qa_unified_document_json(doc),
        qa_unified_document_root(doc)), &canonical, e);
    if (ok) qa_sha256((qa_bytes){canonical.data, canonical.size}, digest);
    qa_buffer_free(&canonical);
    return ok;
}

static bool hash_json(const qa_json_document *json, qa_json_id value, qa_sha256_digest *digest, qa_error *e)
{
    qa_buffer canonical = {0};
    bool ok = qa_unified_value_canonical(qa_json_source(json, value), &canonical, e);
    if (ok) qa_sha256((qa_bytes){canonical.data, canonical.size}, digest);
    qa_buffer_free(&canonical);
    return ok;
}

static bool owners_valid(qa_application *app, const application_unified_source *source,
    const component_cursor *rows, size_t count, const native_cursor *native, bool pending, bool obsolete, qa_error *e)
{
    if (count + (native->present ? 1u : 0u) > 256) return bad(e, "Component cold roster exceeds its owner extent");
    for (size_t i = 0; i < count; ++i) {
        if (native->present && rows[i].owner == native->owner) return bad(e, "Component cold namespaces alias");
        if (obsolete) continue;
        application_q3_component_publication publication = {0}; bool found = false;
        if (!application_q3_components_checkpoint_publication_read(app, rows[i].owner, &publication, &found, e)) return false;
        /* An old reliable roster remains authoritative until its replacement
         * CONTROL is queued. Retired rows are not rebound to another owner. */
        if (!found || publication.generation != rows[i].generation) {
            if (pending) return bad(e, "Pending component cursor has no imported activation");
            continue;
        }
        qa_sha256_digest identity;
        int64_t gs, revision; int32_t sequence;
        if (!publication.presentation_runtime || !hash_document(publication.identity, &identity, e) ||
            !application_q3_component_source_continuation_read(publication.source, &gs, &sequence, &revision, e)) return false;
        if (publication.abi != rows[i].abi || !qa_sha256_equal(&identity, &rows[i].identity) ||
            rows[i].scene != !strcmp(publication.presentation_runtime, "qvm-scene") ||
            rows[i].game_state_revision > gs || rows[i].sequence > sequence)
            return bad(e, "Component cursor differs from its genuine imported activation");
    }
    if (native->present && !obsolete) {
        application_native_q2_publication_view publication = {0}; bool found = false;
        if (!application_native_q2_publication_checkpoint_read(app, source, &publication, &found, e)) return false;
        if (pending && (!found || publication.owner != native->owner ||
            publication.activation_generation != native->activation || publication.generation != native->generation))
            return bad(e, "Pending native component has no imported registration");
        if (found && publication.owner == native->owner && publication.activation_generation == native->activation) {
            qa_sha256_digest identity;
            if (!hash_document(publication.identity, &identity, e)) return false;
            if (native->generation > publication.generation || !qa_sha256_equal(&identity, &native->identity))
                return bad(e, "Native component cursor differs from its imported registration");
        }
    }
    return true;
}

static bool publisher_fields(qa_source_save_io *io, application_unified_component_publisher *p)
{
    return magic(io, "QUCP") && recipient_fields(io, p->recipient, p->actor) &&
        qa_source_save_u32(io, &p->epoch) && qa_source_save_u64(io, &p->revision) &&
        qa_source_save_u64(io, &p->serial) && rows_fields(io, &p->rows, &p->count, NULL) && native_fields(io, &p->native) &&
        p->revision <= QA_UNIFIED_SAFE_INTEGER &&
        (p->epoch ? p->serial && ((!p->count && !p->native.present) || p->revision) :
            (!p->revision && !p->serial && !p->count && !p->native.present));
}

static bool recipient_read(const application_unified_component_publisher *p,
    application_unified_source *source, qa_unified_session_player *player, qa_error *e)
{
    const application_player_record *found = NULL;
    if (p->application->players) for (size_t i = 0; i < p->application->players->count; ++i) {
        const application_player_record *row = p->application->players->records + i;
        if (row->remote && !row->retiring && qa_net_client_id_equal(row->remote_client, p->recipient) &&
            qa_actor_id_equal(row->actor, p->actor)) {
            if (found) return bad(e, "Component publisher aliases two physical recipients");
            found = row;
        }
    }
    if (!found) return bad(e, "Component publisher lost its actual remote player binding");
    if (p->application->operation == APPLICATION_IDLE)
        return application_unified_source_read(p->application, source, e) &&
            application_unified_player_read(p->application, p->recipient, found->remote_seat, player, e);
    return application_unified_source_checkpoint_read(p->application, source, e) &&
        application_unified_player_checkpoint_read(p->application, p->recipient, found->remote_seat, player, e);
}

bool application_unified_components_checkpoint(const application_unified_component_publisher *p,
    qa_buffer *out, qa_error *e)
{
    application_unified_source source = {0}; qa_unified_session_player player;
    if (!p)
        return application_fail(e, QA_ERROR_ARGUMENT, "Component checkpoint requires its actual recipient and returned Source");
    if (!recipient_read(p, &source, &player, e)) return false;
    return application_unified_components_checkpoint_retained(p, &source, &source, &player, out, e);
}
static bool publisher_checkpoint(const application_unified_component_publisher *p,
    const application_unified_source *source, const application_unified_source *retained,
    const qa_unified_session_player *player, const application_unified_server *drop,
    qa_buffer *out, qa_error *e)
{
    bool obsolete = application_unified_save_source_obsolete(source, retained);
    if (!p || !source || !retained || !player || !out || out->data || out->size ||
        (drop && (!application_unified_server_source_drop_current(drop, e) ||
            drop->components != p || drop->application != p->application ||
            !qa_net_client_id_equal(drop->client, p->recipient))) ||
        !(application_unified_source_current(p->application, source) ||
            application_unified_source_checkpoint_current(p->application, source)) ||
        (!obsolete && !drop && !source_current(p->application, retained, p->recipient, player)) ||
        ((obsolete || drop) && p->pending) ||
        (p->pending && (!p->pending->sealed || !application_unified_components_current(p->pending))))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component checkpoint lacks its actual current or historical recipient");
    if (!qa_actor_id_equal(player->actor, p->actor)) return bad(e, "Component publisher recipient changed");
    application_unified_component_publisher copy = *p;
    application_unified_source saved = *retained;
    component_cursor rows[256];
    if (p->count > 256 || (p->count && !p->rows)) return bad(e, "Component checkpoint cursor extent is invalid");
    if (!owners_valid(p->application, source, p->rows, p->count, &p->native, false, obsolete, e)) return false;
    if (p->count) memcpy(rows, p->rows, p->count * sizeof(*rows));
    copy.rows = rows;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, source->session, e) && source_fields(&io, p->application, source, &saved, player) &&
        publisher_fields(&io, &copy) && qa_source_save_finish(&io, out);
    if (!ok && !io.failed && (!e || e->code == QA_OK)) bad(e, "Component publisher continuation is inconsistent");
    qa_source_save_dispose(&io);
    return ok;
}
bool application_unified_components_checkpoint_retained(const application_unified_component_publisher *p,
    const application_unified_source *source, const application_unified_source *retained,
    const qa_unified_session_player *player, qa_buffer *out, qa_error *e)
{
    return publisher_checkpoint(p, source, retained, player, NULL, out, e);
}
bool application_unified_components_checkpoint_dropped(const application_unified_component_publisher *p,
    const application_unified_server *drop, qa_buffer *out, qa_error *e)
{
    application_unified_source source;
    if (!drop || !application_unified_save_source_read(drop->application, &source, e)) return false;
    return publisher_checkpoint(p, &source, &drop->offered, &drop->admitted_player, drop, out, e);
}

bool application_unified_components_restore(qa_bytes bytes, qa_application *app,
    const application_unified_source *source, qa_net_client_id client, const qa_unified_session_player *player,
    application_unified_component_publisher **out, qa_error *e)
{
    return application_unified_components_restore_retained(bytes, app, source, source, client, player, out, e);
}
static bool publisher_restore(qa_bytes bytes, qa_application *app,
    const application_unified_source *source, const application_unified_source *retained,
    qa_net_client_id client, const qa_unified_session_player *player,
    const application_unified_server *drop, application_unified_component_publisher **out, qa_error *e)
{
    bool obsolete = application_unified_save_source_obsolete(source, retained);
    if (!source || !retained || !player || !out || *out ||
        (drop && (!application_unified_server_source_drop_current(drop, e) ||
            drop->application != app || !qa_net_client_id_equal(drop->client, client))) ||
        !(application_unified_source_current(app, source) || application_unified_source_checkpoint_current(app, source)) ||
        (!obsolete && !drop && !source_current(app, retained, client, player)))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component restore requires its genuine imported recipient");
    application_unified_component_publisher *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(e, QA_ERROR_MEMORY, "Restoring recipient component publisher");
    p->application = app; p->recipient = client; p->actor = player->actor;
    application_unified_source saved = *retained;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, source->session, bytes, e) && source_fields(&io, app, source, &saved, player) &&
        publisher_fields(&io, p) && qa_source_save_finish(&io, NULL) &&
        owners_valid(app, source, p->rows, p->count, &p->native, false, obsolete, e) &&
        (obsolete || drop || source_current(app, retained, client, player));
    if (!ok && !io.failed && (!e || e->code == QA_OK)) bad(e, "Saved component publisher differs from its imported Source");
    qa_source_save_dispose(&io);
    if (!ok) { free(p->rows); free(p); return false; }
    p->capacity = p->count;
    *out = p;
    return true;
}
bool application_unified_components_restore_retained(qa_bytes bytes, qa_application *app,
    const application_unified_source *source, const application_unified_source *retained,
    qa_net_client_id client, const qa_unified_session_player *player,
    application_unified_component_publisher **out, qa_error *e)
{
    return publisher_restore(bytes, app, source, retained, client, player, NULL, out, e);
}
bool application_unified_components_restore_dropped(qa_bytes bytes, const application_unified_server *drop,
    application_unified_component_publisher **out, qa_error *e)
{
    application_unified_source source;
    if (!drop || !application_unified_save_source_read(drop->application, &source, e)) return false;
    return publisher_restore(bytes, drop->application, &source, &drop->offered, drop->client,
        &drop->admitted_player, drop, out, e);
}

static bool owner_json(const qa_json_document *json, qa_json_id value, qa_session *session,
    qa_actor_owner owner, uint64_t generation, qa_error *e)
{
    uint64_t saved;
    const char *name = qa_strings_cstr(qa_session_strings(session), owner);
    return name && qa_json_string_equal(json, qa_json_get(json, value, "provider"), name) &&
        qa_json_u64(json, qa_json_get(json, value, "generation"), &saved, e) && saved == generation;
}

static bool json_equal(const qa_json_document *a, qa_json_id av,
    const qa_json_document *b, qa_json_id bv, qa_error *e)
{
    qa_sha256_digest x, y;
    return hash_json(a, av, &x, e) && hash_json(b, bv, &y, e) && qa_sha256_equal(&x, &y);
}

static bool game_state_json(const qa_unified_document *doc, qa_json_id value, qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id offsets = qa_json_get(json, value, "stringOffsets"); uint64_t count;
    qa_buffer bytes = {0};
    bool ok = qa_json_type(json, value) == QA_JSON_OBJECT &&
        qa_json_type(json, offsets) == QA_JSON_ARRAY && qa_json_size(json, offsets) == QA_Q3_CONFIGSTRINGS &&
        qa_json_u64(json, qa_json_get(json, value, "dataCount"), &count, e) &&
        count && count <= QA_Q3_GAMESTATE_CHARS && qa_unified_document_bytes(doc,
            qa_json_get(json, value, "stringData"), &bytes, e) && bytes.size == QA_Q3_GAMESTATE_CHARS && !bytes.data[0];
    for (size_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i) {
        uint64_t offset;
        ok = qa_json_u64(json, qa_json_at(json, offsets, i), &offset, e) && offset < count &&
            memchr(bytes.data + offset, 0, (size_t)(count - offset)) != NULL;
    }
    qa_buffer_free(&bytes);
    return ok;
}

static const component_cursor *previous_row(const application_unified_component_capture *v,
    const component_cursor *row)
{
    if (v->owner->epoch != v->epoch) return NULL;
    for (size_t i = 0; i < v->owner->count; ++i)
        if (v->owner->rows[i].owner == row->owner && v->owner->rows[i].generation == row->generation)
            return v->owner->rows + i;
    return NULL;
}

static bool candidate_valid(const application_unified_component_capture *v, qa_error *e)
{
    const application_unified_component_publisher *p = v->owner;
    bool changed = (p->epoch != v->epoch && p->count != 0) || p->count != v->count;
    for (size_t i = 0; i < v->count; ++i) {
        const component_cursor *row = v->rows + i, *old = previous_row(v, row);
        if (old && (old->abi != row->abi || old->scene != row->scene ||
            !qa_sha256_equal(&old->identity, &row->identity) || row->game_state_revision < old->game_state_revision ||
            row->sequence < old->sequence)) return bad(e, "Sealed component cursor moved behind its committed activation");
        changed |= !old || old->game_state_revision != row->game_state_revision || old->sequence != row->sequence;
        if (i < p->count) changed |= row->owner != p->rows[i].owner || row->generation != p->rows[i].generation;
    }
    const native_cursor *old = &p->native, *native = &v->native;
    if (!native->present) changed |= old->present;
    else {
        bool same = p->epoch == v->epoch && old->present && old->owner == native->owner &&
            old->activation == native->activation && old->generation == native->generation;
        if (same && !qa_sha256_equal(&old->identity, &native->identity))
            return bad(e, "Sealed native component changed its activation identity");
        changed |= !same || !qa_sha256_equal(&old->state, &native->state);
    }
    uint64_t revision = p->epoch == v->epoch ? p->revision : 0;
    if (changed) {
        if (revision == QA_UNIFIED_SAFE_INTEGER) return bad(e, "Sealed component revision is exhausted");
        ++revision;
    }
    return (revision == v->revision && changed == (v->control != NULL)) ||
        bad(e, "Sealed component update does not follow its committed cursor");
}

static bool roster_control(const qa_unified_document *doc, qa_json_id root,
    const application_unified_component_capture *v, qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(doc);
    uint64_t revision;
    if (!qa_json_u64(json, qa_json_get(json, root, "revision"), &revision, e) || revision != v->revision) return false;
    qa_json_id sources = qa_json_get(json, root, "sources"), native = qa_json_get(json, root, "native");
    if (qa_json_type(json, sources) != QA_JSON_ARRAY || qa_json_size(json, sources) != v->count ||
        qa_json_type(json, native) != QA_JSON_ARRAY || qa_json_size(json, native) != (v->native.present ? 1u : 0u)) return false;
    for (size_t i = 0; i < v->count; ++i) {
        const component_cursor *row = v->rows + i;
        qa_json_id item = qa_json_at(json, sources, i);
        int64_t gs; uint64_t generation;
        if (!owner_json(json, qa_json_get(json, item, "owner"), v->source.session, row->owner, row->generation, e) ||
            !qa_json_u64(json, qa_json_get(json, item, "generation"), &generation, e) || generation != row->generation ||
            !qa_json_i64(json, qa_json_get(json, item, "gameStateRevision"), &gs, e) || gs != row->game_state_revision ||
            !qa_json_string_equal(json, qa_json_get(json, item, "abi"),
                row->abi == QA_QVM_Q3_MODERN ? "q3-modern" : "q3-1.16n-base") ||
            !qa_json_string_equal(json, qa_json_get(json, item, "runtime"),
                row->scene ? "qvm-scene" : "qvm-player-events")) return false;
        qa_sha256_digest identity; int64_t base;
        const component_cursor *old = previous_row(v, row);
        int32_t expected_base = old ? old->sequence : row->sequence;
        qa_json_id commands = qa_json_get(json, item, "commands");
        if (!hash_json(json, qa_json_get(json, item, "identity"), &identity, e) ||
            !qa_sha256_equal(&identity, &row->identity) ||
            !qa_json_i64(json, qa_json_get(json, item, "commandBase"), &base, e) || base != expected_base ||
            qa_json_type(json, commands) != QA_JSON_ARRAY || qa_json_size(json, commands) > 64) return false;
        for (size_t c = 0; c < qa_json_size(json, commands); ++c) {
            qa_json_id command = qa_json_at(json, commands, c); int64_t sequence;
            qa_json_id arguments = qa_json_get(json, command, "arguments");
            if (!qa_json_i64(json, qa_json_get(json, command, "sequence"), &sequence, e) ||
                base == INT32_MAX || sequence != ++base || sequence > row->sequence ||
                qa_json_type(json, arguments) != QA_JSON_ARRAY || qa_json_size(json, arguments) > 128) return false;
            for (size_t a = 0; a < qa_json_size(json, arguments); ++a) {
                qa_buffer argument = {0};
                if (!qa_json_string(json, qa_json_at(json, arguments, a), &argument, e)) return false;
                bool valid = argument.size <= 8192 && !memchr(argument.data, 0, argument.size);
                qa_buffer_free(&argument);
                if (!valid) return false;
            }
        }
        if (base != row->sequence) return false;
        qa_json_id state = qa_json_get(json, item, "gameState");
        bool game_changed = !old || old->game_state_revision != row->game_state_revision;
        if (game_changed ? !game_state_json(doc, state, e) : qa_json_type(json, state) != QA_JSON_NULL) return false;
    }
    if (v->native.present) {
        qa_json_id item = qa_json_at(json, native, 0); uint64_t generation;
        if (!owner_json(json, qa_json_get(json, item, "owner"), v->source.session, v->native.owner, v->native.activation, e) ||
            !qa_json_u64(json, qa_json_get(json, item, "generation"), &generation, e) || generation != v->native.generation) return false;
    }
    return true;
}

static bool frame_owner(const qa_unified_component_owner *owner, qa_session *session,
    qa_actor_owner actor_owner, uint64_t generation)
{
    const char *name = qa_strings_cstr(qa_session_strings(session), actor_owner);
    return name && owner->provider && !strcmp(name, owner->provider) && owner->generation == generation;
}
static bool roster_frame(const application_unified_component_capture *v)
{
    const qa_unified_frame *frame = qa_unified_document_frame(v->frame_document);
    if (!frame || frame->epoch != v->epoch || frame->components != v->frame ||
        !v->frame || v->frame->revision != v->revision || v->frame->source_count != v->count ||
        v->frame->native_count != (v->native.present ? 1u : 0u)) return false;
    for (size_t i = 0; i < v->count; ++i) {
        const component_cursor *cursor = v->rows + i;
        const qa_unified_component_source *row = v->frame->sources + i;
        if (!frame_owner(&row->owner, v->source.session, cursor->owner, cursor->generation) ||
            row->abi != cursor->abi || row->game_state_revision != cursor->game_state_revision ||
            row->scene != cursor->scene || !qa_actor_id_equal(row->viewer, v->player.actor) ||
            row->snapshot.server_command_number != cursor->sequence) return false;
    }
    if (v->native.present) {
        const qa_unified_native_component *row = v->frame->native;
        if (!frame_owner(&row->owner, v->source.session, v->native.owner, v->native.activation) ||
            row->generation != v->native.generation || !qa_actor_id_equal(row->viewer, v->player.actor)) return false;
    }
    return true;
}

static bool capture_documents(const application_unified_component_capture *v, qa_error *e)
{
    if (!candidate_valid(v, e)) return false;
    if (!roster_frame(v))
        return bad(e, "Component queue frame differs from its retained cursor");
    if (v->control) {
        const qa_json_document *json = qa_unified_document_json(v->control);
        qa_json_id value = qa_json_get(json, qa_unified_document_root(v->control), "value");
        uint64_t epoch;
        if (qa_unified_document_type(v->control) != QA_UNIFIED_CONTROL_DOCUMENT ||
            !qa_json_string_equal(json, qa_json_get(json, value, "kind"), "components") ||
            !qa_json_u64(json, qa_json_get(json, value, "epoch"), &epoch, e) || epoch != v->epoch ||
            !roster_control(v->control, qa_json_get(json, value, "update"), v, e))
            return bad(e, "Component reliable update differs from its immutable queue token");
    }
    if (v->native.present) {
        qa_sha256_digest state, identity, configs;
        const qa_unified_document *full = v->native_documents.state;
        const qa_unified_document *source = v->native_documents.source.hud_state;
        if (!full || !source || !hash_document(full, &state, e) || !qa_sha256_equal(&state, &v->native.state))
            return bad(e, "Sealed native component lost its complete reliable state");
        const qa_json_document *json = qa_unified_document_json(full);
        if (!hash_json(json, qa_json_get(json, qa_unified_document_root(full), "identity"), &identity, e) ||
            !qa_sha256_equal(&identity, &v->native.identity)) return bad(e, "Sealed native identity differs from its cursor");
        json = qa_unified_document_json(source);
        if (!hash_json(json, qa_json_get(json, qa_unified_document_root(source), "configstrings"), &configs, e) ||
            !qa_sha256_equal(&configs, &v->native.configs)) return bad(e, "Sealed native config table differs from its cursor");
        const qa_json_document *full_json = qa_unified_document_json(full);
        qa_json_id full_root = qa_unified_document_root(full);
        uint64_t full_generation;
        if (!owner_json(full_json, qa_json_get(full_json, full_root, "owner"), v->source.session,
            v->native.owner, v->native.activation, e) ||
            !qa_json_u64(full_json, qa_json_get(full_json, full_root, "generation"), &full_generation, e) ||
            full_generation != v->native.generation) return bad(e, "Sealed full native state has another owner");
        if (v->control) {
            const qa_json_document *control = qa_unified_document_json(v->control);
            qa_json_id update = qa_json_get(control, qa_json_get(control, qa_unified_document_root(v->control), "value"), "update");
            qa_json_id row = qa_json_at(control, qa_json_get(control, update, "native"), 0);
            qa_json_id hud = qa_json_get(control, row, "hud"), full_hud = qa_json_get(full_json, full_root, "hud");
            if (!json_equal(control, qa_json_get(control, row, "identity"), full_json,
                qa_json_get(full_json, full_root, "identity"), e)) return bad(e, "Reliable native identity differs from its full state");
            if (qa_json_type(full_json, full_hud) == QA_JSON_NULL) {
                if (qa_json_type(control, hud) != QA_JSON_NULL) return bad(e, "Reliable native update invented a HUD owner");
            } else {
                qa_json_id frame = qa_json_get(control, hud, "frame"), full_frame = qa_json_get(full_json, full_hud, "frame");
                static const char *const fields[] = {"protocol", "layout", "inventory", "playerNumber"};
                if (!json_equal(control, qa_json_get(control, hud, "mode"), full_json,
                    qa_json_get(full_json, full_hud, "mode"), e)) return bad(e, "Reliable native HUD changed mode");
                for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
                    if (!json_equal(control, qa_json_get(control, frame, fields[i]), full_json,
                        qa_json_get(full_json, full_frame, fields[i]), e)) return bad(e, "Reliable native HUD differs from its full state");
                const native_cursor *old = &v->owner->native;
                bool same = v->owner->epoch == v->epoch && old->present && old->owner == v->native.owner &&
                    old->activation == v->native.activation && old->generation == v->native.generation;
                bool omitted = same && qa_sha256_equal(&old->configs, &v->native.configs);
                qa_json_id values = qa_json_get(control, frame, "configstrings");
                if (omitted ? qa_json_type(control, values) != QA_JSON_NULL :
                    !json_equal(control, values, full_json, qa_json_get(full_json, full_frame, "configstrings"), e))
                    return bad(e, "Reliable native config delta differs from its committed cursor");
            }
        }
    } else if (v->native_documents.state || v->native_documents.source.hud_state)
        return bad(e, "Absent native component retains reliable state");
    return true;
}

static bool capture_fields(qa_source_save_io *io, application_unified_component_capture *v)
{
    application_unified_source actual = v->source;
    return magic(io, "QUCT") && recipient_fields(io, v->owner->recipient, v->owner->actor) &&
        source_fields(io, v->owner->application, &actual, &v->source, &v->player) && qa_source_save_u32(io, &v->epoch) &&
        qa_source_save_u64(io, &v->revision) && qa_source_save_u64(io, &v->serial) &&
        rows_fields(io, &v->rows, &v->count, v->lease) && native_fields(io, &v->native) &&
        application_unified_save_document(io, &v->control, QA_UNIFIED_CONTROL_DOCUMENT) &&
        application_unified_save_document(io, &v->native_documents.state, QA_UNIFIED_CHECKPOINT) &&
        application_unified_save_document(io, &v->native_documents.source.hud_state, QA_UNIFIED_CHECKPOINT) &&
        v->epoch && v->revision <= QA_UNIFIED_SAFE_INTEGER && v->serial == v->owner->serial && v->serial != UINT64_MAX;
}

bool application_unified_components_capture_checkpoint(const application_unified_component_capture *v,
    qa_buffer *out, qa_error *e)
{
    if (!v || !v->sealed || !out || out->data || out->size || !application_unified_components_current(v) || v->leases ||
        !source_current(v->owner->application, &v->source, v->owner->recipient, &v->player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component token checkpoint requires its sealed returned Source");
    if (v->count > 256 || (v->count && !v->rows) || !capture_documents(v, e) ||
        !owners_valid(v->owner->application, &v->source, v->rows, v->count, &v->native, true, false, e)) return false;
    application_unified_component_capture copy = *v; component_cursor rows[256];
    if (v->count) memcpy(rows, v->rows, v->count * sizeof(*rows));
    copy.rows = rows;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, v->source.session, e) && capture_fields(&io, &copy) && qa_source_save_finish(&io, out);
    if (!ok && !io.failed && (!e || e->code == QA_OK)) bad(e, "Sealed component continuation is inconsistent");
    qa_source_save_dispose(&io);
    return ok;
}

bool application_unified_components_capture_restore(qa_bytes bytes, application_unified_component_publisher *p,
    const application_unified_source *source, const qa_unified_session_player *player,
    const qa_unified_document *frame, application_unified_component_capture **out, qa_error *e)
{
    if (!p || p->pending || !out || *out || !source_current(p->application, source, p->recipient, player) ||
        !qa_actor_id_equal(player->actor, p->actor))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component token restore requires its actual idle publisher");
    const qa_unified_frame *target = qa_unified_document_frame(frame);
    if (!target || !target->lease || !qa_unified_frame_lease_retain(target->lease, e)) return false;
    application_unified_component_capture *v = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*v),
        _Alignof(application_unified_component_capture), e);
    if (!v) { qa_unified_frame_lease_release(target->lease); return false; }
    v->lease = target->lease;
    v->owner = p; v->source = *source; v->player = *player; v->sealed = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, source->session, bytes, e) && capture_fields(&io, v) &&
        qa_source_save_finish(&io, NULL) && application_unified_components_bind_frame(v, frame, e) &&
        capture_documents(v, e) && application_unified_components_reserve(p, v->count, e) &&
        owners_valid(p->application, source, v->rows, v->count, &v->native, true, false, e) && source_current(p->application, source, p->recipient, player);
    if (!ok && !io.failed && (!e || e->code == QA_OK)) bad(e, "Saved component token differs from its imported publisher");
    qa_source_save_dispose(&io);
    if (!ok) { application_unified_components_dispose(v); return false; }
    p->pending = v; *out = v;
    return true;
}

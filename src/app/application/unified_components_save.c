#include "qa/network_unified_control.h"
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
        !qa_source_save_u32(io, &abi) || !qa_source_save_bool(io, &row->scene)) return false;
    row->abi = (qa_qvm_abi)abi;
    return row->owner && row->generation &&
        row->game_state_revision >= 0 &&
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
        qa_source_save_u64(io, &row->configuration_revision) &&
        qa_source_save_u64(io, &row->layout_revision) && qa_source_save_u64(io, &row->inventory_revision) &&
        qa_source_save_u32(io, &row->source_slot) && row->source_slot && row->source_slot <= 256 &&
        row->owner && row->activation;
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
        int64_t gs, revision; int32_t sequence;
        if (!publication.presentation_runtime ||
            !application_q3_component_source_continuation_read(publication.source, &gs, &sequence, &revision, e)) return false;
        if (publication.abi != rows[i].abi ||
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
            if (native->generation > publication.generation)
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
            row->game_state_revision < old->game_state_revision ||
            row->sequence < old->sequence)) return bad(e, "Sealed component cursor moved behind its committed activation");
        changed |= !old || old->game_state_revision != row->game_state_revision || old->sequence != row->sequence;
        if (i < p->count) changed |= row->owner != p->rows[i].owner || row->generation != p->rows[i].generation;
    }
    const native_cursor *old = &p->native, *native = &v->native;
    if (!native->present) changed |= old->present;
    else {
        bool same = p->epoch == v->epoch && old->present && old->owner == native->owner &&
            old->activation == native->activation && old->generation == native->generation &&
            old->source_slot == native->source_slot;
        changed |= !same || old->configuration_revision != native->configuration_revision ||
            old->layout_revision != native->layout_revision || old->inventory_revision != native->inventory_revision;
    }
    uint64_t revision = p->epoch == v->epoch ? p->revision : 0;
    if (changed) {
        if (revision == UINT64_MAX) return bad(e, "Sealed component revision is exhausted");
        ++revision;
    }
    return (revision == v->revision && changed == (v->control != NULL)) ||
        bad(e, "Sealed component update does not follow its committed cursor");
}

static bool control_owner(const qa_source_owner *owner,qa_session *session,
    qa_actor_owner provider,uint64_t generation)
{
    const char *name=qa_strings_cstr(qa_session_strings(session),provider);
    return name && owner->provider && !strcmp(name,owner->provider) && owner->generation==generation;
}
static bool roster_control(const qa_unified_components_control *update,
    const application_unified_component_capture *v,qa_error *e)
{
    if (!update || update->revision!=v->revision || update->source_count!=v->count ||
        update->native_count!=(v->native.present?1u:0u)) return false;
    for (size_t i=0;i<v->count;++i) {
        const component_cursor *row=v->rows+i,*old=previous_row(v,row);
        const qa_unified_component_q3 *item=update->sources+i;
        application_q3_component_publication publication={0}; bool found;
        if (!control_owner(&item->owner,v->source.session,row->owner,row->generation) || item->generation!=row->generation ||
            item->game_state_revision!=row->game_state_revision || item->abi!=row->abi || item->scene!=row->scene ||
            !application_q3_components_checkpoint_publication_read(v->owner->application,row->owner,&publication,&found,e) ||
            !found || publication.generation!=row->generation ||
            !qa_unified_component_identity_equal(&item->identity,publication.identity) ||
            item->command_base!=(old?old->sequence:row->sequence)) return false;
        int32_t through=item->command_base;
        for (size_t k=0;k<item->command_count;++k) {
            if (through==INT32_MAX || item->commands[k].sequence!=++through || through>row->sequence) return false;
        }
        if (through!=row->sequence || (item->game_state!=NULL)!=(!old || old->game_state_revision!=row->game_state_revision)) return false;
    }
    if (v->native.present) {
        const qa_unified_component_q2 *item=update->native;
        application_native_q2_publication_view publication={0}; bool found;
        if (!control_owner(&item->owner,v->source.session,v->native.owner,v->native.activation) || item->generation!=v->native.generation ||
            !application_native_q2_publication_checkpoint_read(v->owner->application,&v->source,&publication,&found,e) ||
            !found || publication.owner!=v->native.owner || publication.activation_generation!=v->native.activation ||
            publication.generation!=v->native.generation || !qa_unified_component_identity_equal(&item->identity,publication.identity)) return false;
    }
    return true;
}

static bool frame_owner(const qa_source_owner *owner, qa_session *session,
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
        const qa_unified_control *control=qa_unified_document_control(v->control);
        if (!control || control->kind!=QA_UNIFIED_CONTROL_COMPONENTS || control->epoch!=v->epoch ||
            !roster_control(&control->value.components,v,e))
            return bad(e,"Component reliable update differs from its immutable queue token");
    }
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
        v->epoch && v->serial == v->owner->serial && v->serial != UINT64_MAX;
}

bool application_unified_components_capture_checkpoint(const application_unified_component_capture *v,
    qa_buffer *out, qa_error *e)
{
    if (!v || !v->sealed || !out || out->data || out->size || !application_unified_components_current(v) || v->leases ||
        !source_current(v->owner->application, &v->source, v->owner->recipient, &v->player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component token checkpoint requires its sealed returned Source");
    if (v->count > 256 || (v->count && !v->rows) ||
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

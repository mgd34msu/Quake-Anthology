#include "session_internal.h"
#include "qa/text.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>

void q2_download_close(q2_server *server)
{
    qa_resource_release(server->download); server->download = NULL; server->download_view = NULL; server->download_offset = 0;
    qa_vfs_acquisition_dispose(&server->download_opening);
    qa_buffer_free(&server->download_source); server->download_memory = false;
    qa_buffer_free(&server->download_wire);
}
qa_bytes q2_download_bytes(const q2_server *server)
{
    return server->download_wire.size ? (qa_bytes){server->download_wire.data, server->download_wire.size} :
        server->download_memory ? (qa_bytes){server->download_source.data, server->download_source.size} :
        qa_resource_bytes(server->download);
}
bool qa_network_q2_server_download(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_vfs **view, const qa_resource **resource, const qa_vfs_acquisition **opening, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!qa_network_q2_peer(peer) || !view || !resource || !opening)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Missing Q2 hosted immutable-holder inventory");
    q2_session *session = peer->state;
    if (!session->server) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 immutable hosted download belongs to its server owner");
    q2_server *server = &session->state.server;
    *view = server->download_view; *resource = server->download;
    *opening = server->download ? &server->download_opening : NULL; return true;
}
static bool refused(q2_session *session, qa_error *error)
{
    qa_q2_server_event event = {.kind = QA_Q2_SVC_DOWNLOAD, .data.download = {.missing = true}};
    return q2_queue_event(session, &event, 0, true, error);
}
static bool prefix(const char *text, const char *start)
{
    while (*start) {
        if (tolower((unsigned char)*text++) != (unsigned char)*start++) return false;
    }
    return true;
}
void qa_q2_download_policy_bind(qa_q2_download_policy *policy,const qa_cvars *cvars)
{
    static const char *const names[]={"allow_download","allow_download_maps","allow_download_models",
        "allow_download_sounds","allow_download_players"};
    uint64_t identity=qa_cvars_view_identity(cvars),declarations=qa_cvars_declaration_revision(cvars);
    if(policy->registry==cvars && policy->identity==identity && policy->declarations==declarations) return;
    *policy=(qa_q2_download_policy){.registry=cvars,.identity=identity,.declarations=declarations};
    for(size_t i=0;i<QA_Q2_DOWNLOAD_CATEGORY_COUNT;++i) policy->handles[i]=qa_cvars_resolve(cvars,names[i]);
}
const qa_cvar_view *qa_q2_download_policy_read(qa_q2_download_policy *policy,const qa_cvars *cvars,
    qa_q2_download_category category)
{
    qa_q2_download_policy_bind(policy,cvars);
    return qa_cvars_read(cvars,policy->handles[category]);
}
static bool enabled(qa_q2_download_policy *policy,const qa_cvars *cvars,qa_q2_download_category category)
{ const qa_cvar_view *value=qa_q2_download_policy_read(policy,cvars,category); return value && value->number!=0; }
bool qa_q2_download_categories_allowed(qa_q2_download_policy *policy,const qa_cvars *cvars,const char *path)
{
    const char *extension=strrchr(path,'.');
    if (extension && (qa_text_ascii_equal(extension,".pak") || qa_text_ascii_equal(extension,".pkz") ||
        qa_text_ascii_equal(extension,".pk3"))) {
        for (size_t i=QA_Q2_DOWNLOAD_MAPS;i<QA_Q2_DOWNLOAD_CATEGORY_COUNT;++i)
            if (!enabled(policy,cvars,(qa_q2_download_category)i)) return false;
        return true;
    }
    qa_q2_download_category category=prefix(path,"players/")?QA_Q2_DOWNLOAD_PLAYERS:
        (prefix(path,"maps/") || prefix(path,"env/") || prefix(path,"textures/"))?QA_Q2_DOWNLOAD_MAPS:
        prefix(path,"models/")?QA_Q2_DOWNLOAD_MODELS:prefix(path,"sound/")?QA_Q2_DOWNLOAD_SOUNDS:QA_Q2_DOWNLOAD_ALL;
    return category==QA_Q2_DOWNLOAD_ALL || enabled(policy,cvars,category);
}
bool q2_download_begin(q2_session *session, const char *name, const char *offset_text, qa_error *error)
{
    q2_server *server = &session->state.server;
    errno = 0; char *tail; long offset = strtol(offset_text, &tail, 10);
    if (tail == offset_text) offset = 0;
    if (offset > INT32_MAX || (errno == ERANGE && offset > 0)) offset = INT32_MAX;
    if (offset < 0 || !name || strstr(name, "..") ||
        name[0] == '.' || !strchr(name, '/')) return refused(session, error);
    qa_network_q2_download_source source = {0};
    if (!server->hooks.download_source(server->hooks.context, session->id, &source, error)) return false;
    if (!source.content || !source.cvars) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 download lacks its actual mounted Source policy");
    qa_error admission_error = {0}; char *normalized = qa_vfs_normalize_path(name, &admission_error);
    if (!normalized) return refused(session, error);
    bool allowed = enabled(&server->download_policy,source.cvars,QA_Q2_DOWNLOAD_ALL) &&
        (!prefix(normalized, "players/") || enabled(&server->download_policy,source.cvars,QA_Q2_DOWNLOAD_PLAYERS)) &&
        (!prefix(normalized, "models/") || enabled(&server->download_policy,source.cvars,QA_Q2_DOWNLOAD_MODELS)) &&
        (!prefix(normalized, "sound/") || enabled(&server->download_policy,source.cvars,QA_Q2_DOWNLOAD_SOUNDS)) &&
        (!prefix(normalized, "maps/") || enabled(&server->download_policy,source.cvars,QA_Q2_DOWNLOAD_MAPS));
    free(normalized); if (!allowed) return refused(session, error);
    q2_download_close(server);
    qa_resource *resource = NULL; qa_vfs_acquisition opening = {0}; qa_buffer source_bytes = {0}, wire_bytes = {0};
    const qa_vfs *view = source.content; qa_q2_download_resource_status status = QA_Q2_DOWNLOAD_UNHANDLED;
    if (source.resource && !source.resource(source.resource_context, name, &view, &resource,
        &opening, &source_bytes, &wire_bytes, &status, error)) {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening);
        qa_buffer_free(&source_bytes); qa_buffer_free(&wire_bytes); return false;
    }
    bool invalid_buffer = ((wire_bytes.size != 0) != (wire_bytes.data != NULL)) ||
        ((source_bytes.size != 0) != (source_bytes.data != NULL));
    bool has_opening = opening.path || opening.lookup_path || opening.link_source || opening.link_target ||
        opening.mount || opening.resource_id;
    if (invalid_buffer || (status != QA_Q2_DOWNLOAD_MEMORY && (source_bytes.data || source_bytes.size))) {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening);
        qa_buffer_free(&source_bytes); qa_buffer_free(&wire_bytes);
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 download artifact lost its actual Source byte ownership");
    }
    if (status == QA_Q2_DOWNLOAD_HELD) {
        if (!view || !resource ||
            opening.resource_id != qa_resource_id(resource) ||
            !qa_vfs_acquisition_retained(view, &opening, error)) {
            qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); qa_buffer_free(&wire_bytes);
            return q2_fail(error, QA_ERROR_ARGUMENT, "Qualified Q2 download lost its genuine Source resource opening");
        }
    } else if (status == QA_Q2_DOWNLOAD_MEMORY) {
        if (!view || resource || has_opening) {
            qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening);
            qa_buffer_free(&source_bytes); qa_buffer_free(&wire_bytes);
            return q2_fail(error, QA_ERROR_ARGUMENT, "Memory Q2 Source artifact has no genuine catalog ownership");
        }
    } else if (status == QA_Q2_DOWNLOAD_UNHANDLED || status == QA_Q2_DOWNLOAD_MISSING) {
        if (resource || has_opening || wire_bytes.data || wire_bytes.size) {
            qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); qa_buffer_free(&wire_bytes);
            return q2_fail(error, QA_ERROR_ARGUMENT, "Absent Q2 resource resolver retained an unclaimed opening");
        }
        if (status == QA_Q2_DOWNLOAD_MISSING) {
            if (!view) return q2_fail(error, QA_ERROR_ARGUMENT, "Known missing Q2 dependency has no actual Source view");
            return refused(session, error);
        }
        view = source.content;
        if (!qa_vfs_acquire_receipt(source.content, name, &resource, &opening, &admission_error)) return refused(session, error);
    } else {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); qa_buffer_free(&wire_bytes);
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 resource resolver returned an unknown ownership status");
    }
    qa_bytes bytes = wire_bytes.size ? (qa_bytes){wire_bytes.data, wire_bytes.size} :
        status == QA_Q2_DOWNLOAD_MEMORY ? (qa_bytes){source_bytes.data, source_bytes.size} : qa_resource_bytes(resource);
    bool archived_map = resource && qa_resource_archive_origin(resource, NULL, NULL) &&
        (prefix(name, "maps/") || prefix(opening.lookup_path, "maps/"));
    if (bytes.size > INT32_MAX || archived_map) {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening);
        qa_buffer_free(&source_bytes); qa_buffer_free(&wire_bytes); return refused(session, error);
    }
    server->download = resource; server->download_view = view; server->download_opening = opening;
    server->download_source = source_bytes; server->download_wire = wire_bytes;
    server->download_memory = status == QA_Q2_DOWNLOAD_MEMORY;
    server->download_offset = (size_t)offset > bytes.size ? bytes.size : (size_t)offset;
    return q2_download_next(session, error);
}
bool q2_download_next(q2_session *session, qa_error *error)
{
    q2_server *server = &session->state.server;
    if (!server->download && !server->download_memory) return true;
    qa_bytes bytes = q2_download_bytes(server);
    if (server->download_offset > bytes.size) return q2_fail(error, QA_ERROR_FORMAT, "Q2 download cursor exceeds its immutable resource");
    size_t count = bytes.size - server->download_offset; if (count > 1024) count = 1024;
    size_t next = server->download_offset + count;
    qa_q2_server_event event = {.kind = QA_Q2_SVC_DOWNLOAD, .data.download = {
        .percent = (uint8_t)(bytes.size ? (uint64_t)next * 100 / bytes.size : 0),
        .bytes = {count ? bytes.data + server->download_offset : NULL, count}}};
    if (!q2_queue_event(session, &event, 0, true, error)) return false;
    server->download_offset = next; if (next == bytes.size) q2_download_close(server); return true;
}

#include "session_internal.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>

void q2_download_close(q2_server *server)
{
    qa_resource_release(server->download); server->download = NULL; server->download_view = NULL; server->download_offset = 0;
    qa_vfs_acquisition_dispose(&server->download_opening);
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
static bool enabled(const qa_cvars *cvars, const char *name)
{
    const qa_cvar_view *value = qa_cvars_find(cvars, name); return value && value->number != 0;
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
    bool allowed = enabled(source.cvars, "allow_download") &&
        (!prefix(normalized, "players/") || enabled(source.cvars, "allow_download_players")) &&
        (!prefix(normalized, "models/") || enabled(source.cvars, "allow_download_models")) &&
        (!prefix(normalized, "sound/") || enabled(source.cvars, "allow_download_sounds")) &&
        (!prefix(normalized, "maps/") || enabled(source.cvars, "allow_download_maps"));
    free(normalized); if (!allowed) return refused(session, error);
    q2_download_close(server);
    qa_resource *resource = NULL; qa_vfs_acquisition opening = {0};
    if (!qa_vfs_acquire_receipt(source.content, name, &resource, &opening, &admission_error)) return refused(session, error);
    qa_bytes bytes = qa_resource_bytes(resource); qa_sha256_digest archive; size_t ordinal;
    bool archived_map = qa_resource_archive_origin(resource, &archive, &ordinal) &&
        (prefix(name, "maps/") || prefix(opening.lookup_path, "maps/"));
    if (bytes.size > INT32_MAX || archived_map) {
        qa_resource_release(resource); qa_vfs_acquisition_dispose(&opening); return refused(session, error);
    }
    server->download = resource; server->download_view = source.content; server->download_opening = opening;
    server->download_offset = (size_t)offset > bytes.size ? bytes.size : (size_t)offset;
    return q2_download_next(session, error);
}
bool q2_download_next(q2_session *session, qa_error *error)
{
    q2_server *server = &session->state.server;
    if (!server->download) return true;
    qa_bytes bytes = qa_resource_bytes(server->download);
    if (server->download_offset > bytes.size) return q2_fail(error, QA_ERROR_FORMAT, "Q2 download cursor exceeds its immutable resource");
    size_t count = bytes.size - server->download_offset; if (count > 1024) count = 1024;
    size_t next = server->download_offset + count;
    qa_q2_server_event event = {.kind = QA_Q2_SVC_DOWNLOAD, .data.download = {
        .percent = (uint8_t)(bytes.size ? (uint64_t)next * 100 / bytes.size : 0),
        .bytes = {count ? bytes.data + server->download_offset : NULL, count}}};
    if (!q2_queue_event(session, &event, 0, true, error)) return false;
    server->download_offset = next; if (next == bytes.size) q2_download_close(server); return true;
}

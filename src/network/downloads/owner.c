#include "qa/downloads.h"
#include "qa/vfs.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct download_job {
    qa_downloads *owner;
    qa_download_view view;
    qa_download_request request;
    qa_fs_stage *stage;
    qa_http_request_id http_id;
    uint64_t http_start, http_total;
    bool has_http_total;
} download_job;
struct qa_downloads {
    qa_http *http;
    qa_fs_root *root;
    qa_download_options options;
    download_job *jobs;
    uint64_t next_id, reserved;
    bool callback;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
static download_job *lookup(qa_downloads *owner, qa_download_id id) {
    if (!owner || !id) return NULL;
    for (uint32_t i = 0; i < owner->options.jobs; ++i)
        if (owner->jobs[i].view.id == id) return &owner->jobs[i];
    return NULL;
}
static void notify(download_job *job) {
    qa_downloads *owner = job->owner;
    if (owner->options.hooks.changed) {
        bool previous = owner->callback; owner->callback = true;
        owner->options.hooks.changed(owner->options.hooks.context, &job->view);
        owner->callback = previous;
    }
}
static void discard_stage(download_job *job, bool keep) {
    if (job->stage) {
        job->owner->reserved -= job->view.limit;
        qa_fs_stage_close(job->stage, keep); job->stage = NULL;
    }
}
static void rejected(download_job *job, const qa_error *error, bool recoverable) {
    job->view.state = QA_DOWNLOAD_FAILED;
    if (error) job->view.failure = *error;
    else qa_error_set(&job->view.failure, QA_ERROR_IO, 0, "Download failed");
    discard_stage(job, recoverable); notify(job);
}
bool qa_downloads_create(qa_http *http, qa_fs_root *root, const qa_download_options *options,
                         qa_downloads **out, qa_error *error) {
    if (!http || !root || !options || !out || !options->jobs || !options->maximum_pending_bytes ||
        !options->hooks.permit || !options->hooks.inspect || !options->hooks.remount ||
        (uint64_t)options->jobs > SIZE_MAX / sizeof(download_job)) return fail(error, "Invalid download owner options");
    qa_downloads *owner = calloc(1, sizeof(*owner));
    if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating download owner"); return false; }
    owner->jobs = calloc(options->jobs, sizeof(*owner->jobs));
    if (!owner->jobs) { free(owner); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating download slots"); return false; }
    owner->http = http; owner->root = root; owner->options = *options; owner->next_id = 1;
    qa_fs_root_retain(root); *out = owner; return true;
}
static void stop(qa_downloads *owner, qa_download_id id, bool keep) {
    download_job *job = lookup(owner, id);
    if (!job || owner->callback || job->view.state != QA_DOWNLOAD_RECEIVING) return;
    qa_http_cancel(owner->http, job->http_id); job->http_id = 0;
    job->view.state = QA_DOWNLOAD_CANCELED; discard_stage(job, keep); notify(job);
}
void qa_downloads_cancel(qa_downloads *owner, qa_download_id id) { stop(owner, id, false); }
void qa_downloads_suspend(qa_downloads *owner, qa_download_id id) { stop(owner, id, true); }
void qa_downloads_release(qa_downloads *owner, qa_download_id id) {
    download_job *job = lookup(owner, id);
    if (!job || owner->callback) return;
    qa_downloads_cancel(owner, id);
    discard_stage(job, false); free((char *)job->view.path); memset(job, 0, sizeof(*job));
}
void qa_downloads_destroy(qa_downloads *owner) {
    if (!owner || owner->callback) return;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) qa_downloads_release(owner, owner->jobs[i].view.id);
    qa_fs_root_close(owner->root); free(owner->jobs); free(owner);
}
static bool append(download_job *job, uint64_t offset, qa_bytes bytes, qa_error *error) {
    if (job->view.state != QA_DOWNLOAD_RECEIVING || !job->stage || (bytes.size && !bytes.data) || offset > job->view.received)
        return fail(error, "Invalid download block/state");
    if (offset < job->view.received) {
        if ((uint64_t)bytes.size > job->view.received - offset)
            return fail(error, "Download retransmission crosses retained block");
        uint8_t scratch[16384]; size_t compared = 0;
        while (compared < bytes.size) {
            size_t count = bytes.size - compared, read = 0;
            if (count > sizeof(scratch)) count = sizeof(scratch);
            if (!qa_fs_stage_read(job->stage, offset + compared, scratch, count, &read, error)) return false;
            if (read != count || memcmp(scratch, bytes.data + compared, count))
                return fail(error, "Download retransmission differs from retained bytes");
            compared += count;
        }
        return true;
    }
    if ((uint64_t)bytes.size > job->view.limit - job->view.received)
        return fail(error, "Download exceeds admitted size");
    size_t written = 0;
    bool ok = qa_fs_stage_write(job->stage, offset, bytes, &written, error);
    job->view.received += written;
    return ok;
}
bool qa_downloads_append(qa_downloads *owner, qa_download_id id, uint64_t offset, qa_bytes bytes, qa_error *error) {
    download_job *job = lookup(owner, id);
    if (!job || owner->callback || job->http_id) return fail(error, "Native download block does not own this job");
    bool ok = append(job, offset, bytes, error); if (ok) notify(job); return ok;
}
static bool finish(download_job *job, qa_error *error) {
    qa_downloads *owner = job->owner;
    if (job->view.state != QA_DOWNLOAD_RECEIVING || !job->stage) return fail(error, "Download is not receiving");
    uint64_t bytes;
    if (!qa_fs_stage_size(job->stage, &bytes, error)) return false;
    if (bytes != job->view.received || (job->request.exact_identity && bytes != job->request.expected_bytes) ||
        (job->has_http_total && bytes != job->http_total))
        return fail(error, "Download length differs from admitted completion");
    qa_fs_identity identity;
    if (!qa_fs_stage_seal(job->stage, &identity, error)) return false;
    qa_sha256_context hash; qa_sha256_init(&hash);
    uint8_t scratch[65536]; uint64_t offset = 0;
    while (offset < bytes) {
        size_t count = bytes - offset > sizeof(scratch) ? sizeof(scratch) : (size_t)(bytes - offset), read = 0;
        if (!qa_fs_stage_read(job->stage, offset, scratch, count, &read, error)) return false;
        if (read != count) return fail(error, "Staged download changed during digest verification");
        qa_sha256_update(&hash, (qa_bytes){scratch, read}); offset += read;
    }
    qa_sha256_final(&hash, &job->view.digest);
    if (job->request.exact_identity && !qa_sha256_equal(&job->view.digest, &job->request.digest))
        return fail(error, "Download digest differs from expected identity");
    bool previous = owner->callback; owner->callback = true;
    bool ok = owner->options.hooks.inspect(owner->options.hooks.context, job->view.path, job->stage, bytes, error);
    owner->callback = previous;
    if (!ok) return false;
    bool created = false;
    if (!qa_fs_stage_publish(job->stage, &identity, true, &created, error)) {
        job->view.published = created; return false;
    }
    if (!created) return fail(error, "Download destination already exists");
    job->view.published = true;
    job->view.state = QA_DOWNLOAD_INSTALLING;
    discard_stage(job, false); notify(job); return true;
}
bool qa_downloads_pump(qa_downloads *owner, qa_error *error) {
    if (!owner || owner->callback || !qa_http_callbacks_idle(owner->http))
        return fail(error, "Download installation requires shared HTTP callbacks to return");
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        download_job *job = &owner->jobs[i];
        if (!job->view.id || job->view.state != QA_DOWNLOAD_INSTALLING) continue;
        qa_error failure = {0}; owner->callback = true;
        bool ok = owner->options.hooks.remount(owner->options.hooks.context, job->view.path, &job->view.digest, &failure);
        owner->callback = false;
        job->view.mounted = ok;
        if (ok) job->view.state = QA_DOWNLOAD_COMPLETE;
        else {
            job->view.state = QA_DOWNLOAD_FAILED;
            if (!failure.code) fail(&failure, "Application rejected installed download publication");
            job->view.failure = failure;
        }
        notify(job);
    }
    return true;
}
bool qa_downloads_finish(qa_downloads *owner, qa_download_id id, qa_error *error) {
    download_job *job = lookup(owner, id);
    if (!job || owner->callback || job->http_id) return fail(error, "Native download completion does not own job");
    if (finish(job, error)) return true;
    rejected(job, error, false); return false;
}
static bool headers(void *context, qa_http_request_id id, const qa_http_response *response, qa_error *error) {
    download_job *job = context; (void)id;
    if (response->status >= 300 && response->status < 400) return true;
    if (job->http_start || response->status == 206) {
        if (response->status != 206 || !response->has_range || response->range_first != job->http_start ||
            response->range_total > job->view.limit || response->range_last != response->range_total - 1 ||
            (job->request.exact_identity && response->range_total != job->request.expected_bytes))
            return fail(error, "HTTP range does not match retained download identity");
        job->http_total = response->range_total; job->has_http_total = true; return true;
    }
    return response->status == 200 || fail(error, "HTTP download requires a complete successful response");
}
static bool body(void *context, qa_http_request_id id, const qa_http_response *response,
                   qa_bytes bytes, qa_error *error) {
    download_job *job = context; (void)id;
    if (response->status >= 300 && response->status < 400) return true;
    if (response->status != 200 && response->status != 206) return fail(error, "HTTP download response rejected");
    if (job->has_http_total && (uint64_t)bytes.size > job->http_total - job->view.received)
        return fail(error, "HTTP download exceeds announced range");
    bool ok = append(job, job->view.received, bytes, error); if (ok) notify(job); return ok;
}
static void complete(void *context, qa_http_request_id id, const qa_http_response *response, const qa_error *error) {
    download_job *job = context; (void)id; job->http_id = 0;
    bool previous = job->owner->callback; job->owner->callback = true;
    if (error && error->code != QA_OK) rejected(job, error, error->code == QA_ERROR_IO);
    else {
        qa_error failure = {0};
        if (response->status != 200 && response->status != 206) {
            fail(&failure, "HTTP download did not complete successfully"); rejected(job, &failure, true);
        } else if (!finish(job, &failure)) rejected(job, &failure, false);
    }
    job->owner->callback = previous;
}
bool qa_downloads_begin(qa_downloads *owner, const qa_download_request *request, const char *url,
                         qa_download_id *out, qa_error *error) {
    if (!owner || owner->callback || !request || !request->path || !out || !owner->next_id ||
        request->maximum_bytes > INT64_MAX || request->maximum_bytes > owner->options.maximum_pending_bytes - owner->reserved ||
        (request->exact_identity && request->expected_bytes > request->maximum_bytes) ||
        (request->resume && !request->stage_nonce)) return fail(error, "Invalid download request or shared staging capacity");
    download_job *job = NULL;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) if (!owner->jobs[i].view.id) { job = &owner->jobs[i]; break; }
    if (!job) return fail(error, "Download job capacity exhausted");
    char *path = qa_vfs_normalize_path(request->path, error); if (!path) return false;
    qa_download_request normalized = *request; normalized.path = path;
    owner->callback = true;
    bool permitted = owner->options.hooks.permit(owner->options.hooks.context, &normalized, url, error);
    owner->callback = false;
    if (!permitted) { free(path); return false; }
    qa_fs_entry_kind kind = QA_FS_MISSING;
    if (!qa_fs_root_status(owner->root, path, &kind, NULL, error)) { free(path); return false; }
    if (kind != QA_FS_MISSING) { free(path); return fail(error, "Download destination is installed already"); }
    qa_fs_stage *stage; uint64_t initial;
    uint64_t nonce = request->stage_nonce ? request->stage_nonce : owner->next_id;
    if (!qa_fs_stage_open(owner->root, path, nonce, request->resume, &stage, &initial, error)) { free(path); return false; }
    if (initial > request->maximum_bytes || (request->exact_identity && initial > request->expected_bytes)) {
        qa_fs_stage_close(stage, false); free(path); return fail(error, "Resumed download exceeds admitted identity");
    }
    *job = (download_job){.owner = owner, .request = normalized, .stage = stage, .http_start = initial,
        .view = {.id = owner->next_id++, .path = path, .received = initial, .limit = request->maximum_bytes,
                 .state = QA_DOWNLOAD_RECEIVING, .stage_nonce = nonce}};
    owner->reserved += request->maximum_bytes;
    if (url && !(request->exact_identity && initial == request->expected_bytes)) {
        char range[64]; qa_http_header header = {"Range", range};
        (void)snprintf(range, sizeof(range), "bytes=%" PRIu64 "-", initial);
        qa_http_request http = {.url = url, .method = "GET", .timeout_ms = 120000, .connect_timeout_ms = 10000,
            .maximum_response_bytes = request->maximum_bytes - initial, .maximum_redirects = 5,
            .headers = initial ? &header : NULL, .header_count = initial ? 1 : 0,
            .callbacks = {job, headers, body, complete}};
        if (!qa_http_submit(owner->http, &http, &job->http_id, error)) {
            discard_stage(job, request->resume); free(path); memset(job, 0, sizeof(*job)); return false;
        }
    }
    *out = job->view.id; notify(job);
    if (url && request->exact_identity && initial == request->expected_bytes) {
        if (!finish(job, error)) { rejected(job, error, false); return false; }
    }
    return true;
}
bool qa_downloads_view(const qa_downloads *owner, qa_download_id id, qa_download_view *out) {
    if (!owner || !out || !id) return false;
    for (uint32_t i = 0; i < owner->options.jobs; ++i)
        if (owner->jobs[i].view.id == id) { *out = owner->jobs[i].view; return true; }
    return false;
}

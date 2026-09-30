#include "qa/downloads.h"
#include "qa/network_downloads_save.h"
#include "../service_save_fields.h"
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
    bool has_http_total, retained_stage;
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
        job->retained_stage = keep;
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

static bool download_job_valid(const qa_downloads *owner, const download_job *job, bool staged)
{
    const qa_download_request *request = &job->request; const qa_download_view *view = &job->view;
    if (job->owner != owner || job->http_id || job->retained_stage || !view->path || request->path != view->path || !*view->path ||
            (unsigned)view->state > QA_DOWNLOAD_CANCELED || (unsigned)view->failure.code > QA_ERROR_NOT_FOUND ||
            !memchr(view->failure.message, 0, sizeof(view->failure.message)) ||
            request->maximum_bytes != view->limit || view->limit > INT64_MAX || view->received > view->limit ||
            (request->exact_identity && request->expected_bytes > view->limit) ||
            (request->resume && !request->stage_nonce) || !view->stage_nonce ||
            (request->stage_nonce && request->stage_nonce != view->stage_nonce) || job->http_start > view->received ||
            (job->has_http_total && (job->http_total > view->limit || job->http_start > job->http_total)) ||
            (owner->next_id && view->id >= owner->next_id) ||
            staged != (view->state == QA_DOWNLOAD_RECEIVING) ||
            (view->state == QA_DOWNLOAD_INSTALLING && (!view->published || view->mounted)) ||
            (view->state == QA_DOWNLOAD_COMPLETE && (!view->published || !view->mounted)) ||
            (view->mounted && view->state != QA_DOWNLOAD_COMPLETE) ||
            (staged && (view->published || view->mounted)) ||
            (view->published && request->exact_identity &&
                (view->received != request->expected_bytes || !qa_sha256_equal(&view->digest, &request->digest)))) return false;
    char *normalized = qa_vfs_normalize_path(view->path, NULL);
    bool path_ok = normalized && !strcmp(normalized, view->path); free(normalized); return path_ok;
}
static bool download_inventory_valid(const qa_downloads *owner, const bool *staged)
{
    if (!owner || owner->callback || !qa_http_callbacks_idle(owner->http)) return false;
    uint64_t reserved = 0;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        bool has_stage = staged ? staged[i] : job->stage != NULL;
        if (!download_job_valid(owner, job, has_stage)) return false;
        const qa_download_view *view = &job->view;
        for (uint32_t j = 0; j < i; ++j) if (owner->jobs[j].view.id == view->id ||
            (job->stage && owner->jobs[j].stage == job->stage)) return false;
        if (has_stage) {
            if (view->limit > owner->options.maximum_pending_bytes - reserved) return false;
            reserved += view->limit;
        }
    }
    return owner->reserved == reserved;
}
static bool download_checkpoint_valid(const qa_downloads *owner)
{
    return download_inventory_valid(owner, NULL);
}
static bool download_save_job(qa_net_writer *w, const download_job *job)
{
    const qa_download_request *request = &job->request; const qa_download_view *view = &job->view;
    size_t length = strlen(view->path);
    return qa_net_write_u64(w, view->id) && qa_net_write_u64(w, length) && qa_net_write_data(w, view->path, length) &&
        qa_net_write_u64(w, request->maximum_bytes) && qa_net_write_u64(w, request->expected_bytes) &&
        qa_net_write_data(w, request->digest.bytes, sizeof(request->digest.bytes)) && qa_net_write_u8(w, request->exact_identity) &&
        qa_net_write_u64(w, request->stage_nonce) && qa_net_write_u8(w, request->resume) &&
        qa_net_write_u64(w, view->received) && qa_net_write_u64(w, view->limit) && qa_net_write_u32(w, view->state) &&
        qa_net_write_u32(w, view->failure.code) && qa_net_write_u64(w, view->failure.offset) &&
        qa_net_write_data(w, view->failure.message, sizeof(view->failure.message)) &&
        qa_net_write_u8(w, view->published) && qa_net_write_u8(w, view->mounted) &&
        qa_net_write_data(w, view->digest.bytes, sizeof(view->digest.bytes)) && qa_net_write_u64(w, view->stage_nonce) &&
        qa_net_write_u64(w, job->http_start) && qa_net_write_u64(w, job->http_total) && qa_net_write_u8(w, job->has_http_total);
}
bool qa_downloads_checkpoint(const qa_downloads *owner, qa_buffer *out, qa_error *error)
{
    if (!out || !download_checkpoint_valid(owner) || (uint64_t)owner->options.jobs > (SIZE_MAX - 36) / 513)
        return fail(error, "Download continuation requires idle native jobs and drained HTTP ownership");
    size_t capacity = 36 + (size_t)owner->options.jobs * 513;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        size_t length = strlen(job->view.path);
        if (length > SIZE_MAX - capacity || (job->stage && job->view.received > SIZE_MAX - capacity - length))
            return fail(error, "Download continuation extent exceeds memory");
        capacity += length; if (job->stage) capacity += (size_t)job->view.received;
    }
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding native download continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x4a444151)) && qa_net_write_u32(&w, 1) &&
        qa_net_write_u32(&w, owner->options.jobs) && qa_net_write_u64(&w, owner->options.maximum_pending_bytes) &&
        qa_net_write_u64(&w, owner->next_id) && qa_net_write_u64(&w, owner->reserved);
    uint8_t scratch[65536];
    for (uint32_t i = 0; ok && i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; ok = qa_net_write_u8(&w, job->view.id != 0);
        if (!ok || !job->view.id) continue;
        ok = download_save_job(&w, job) && qa_net_write_u8(&w, job->stage != NULL);
        if (!ok || !job->stage) continue;
        uint64_t size = 0;
        ok = qa_fs_stage_size(job->stage, &size, error) && size == job->view.received && qa_net_write_u64(&w, size);
        uint64_t offset = 0;
        while (ok && offset < size) {
            size_t count = size - offset > sizeof(scratch) ? sizeof(scratch) : (size_t)(size - offset), read = 0;
            ok = qa_fs_stage_read(job->stage, offset, scratch, count, &read, error) && read == count && qa_net_write_data(&w, scratch, read);
            offset += read;
        }
        uint64_t after = 0; if (ok) ok = qa_fs_stage_size(job->stage, &after, error) && after == size;
    }
    if (!ok || w.failed) { free(data); if (!error || !error->code) fail(error, "Native stage changed during continuation capture"); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
static bool download_restore_job(qa_net_reader *r, download_job *job)
{
    qa_download_request *request = &job->request; qa_download_view *view = &job->view;
    view->id = qa_net_read_u64(r); uint64_t length = qa_net_read_u64(r);
    if (!view->id || !length || length >= SIZE_MAX || length > qa_net_reader_remaining(r))
        return qa_net_reader_fail(r, "Invalid download continuation path extent");
    char *path = malloc((size_t)length + 1);
    if (!path) { qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Restoring download target path"); r->failed = true; return false; }
    view->path = path; request->path = path;
    if (!qa_net_read_data(r, path, (size_t)length) || memchr(path, 0, (size_t)length))
        return qa_net_reader_fail(r, "Embedded NUL in download continuation path");
    path[length] = 0;
    request->maximum_bytes = qa_net_read_u64(r); request->expected_bytes = qa_net_read_u64(r);
    if (!qa_net_read_data(r, request->digest.bytes, sizeof(request->digest.bytes))) return false;
    request->exact_identity = q3_save_bool(r); request->stage_nonce = qa_net_read_u64(r); request->resume = q3_save_bool(r);
    view->received = qa_net_read_u64(r); view->limit = qa_net_read_u64(r); view->state = (qa_download_state)qa_net_read_u32(r);
    view->failure.code = (qa_status)qa_net_read_u32(r); uint64_t failure_offset = qa_net_read_u64(r);
    if (failure_offset > SIZE_MAX || !qa_net_read_data(r, view->failure.message, sizeof(view->failure.message)))
        return qa_net_reader_fail(r, "Invalid download continuation error extent");
    view->failure.offset = (size_t)failure_offset; view->published = q3_save_bool(r); view->mounted = q3_save_bool(r);
    if (!qa_net_read_data(r, view->digest.bytes, sizeof(view->digest.bytes))) return false;
    view->stage_nonce = qa_net_read_u64(r); job->http_start = qa_net_read_u64(r); job->http_total = qa_net_read_u64(r);
    job->has_http_total = q3_save_bool(r); return !r->failed;
}
static bool download_stage_matches(qa_fs_stage *stage, qa_bytes prefix, qa_error *error)
{
    uint64_t size = 0; if (!qa_fs_stage_size(stage, &size, error) || size != prefix.size) return false;
    uint8_t scratch[65536]; size_t offset = 0;
    while (offset < prefix.size) {
        size_t count = prefix.size - offset, read = 0; if (count > sizeof(scratch)) count = sizeof(scratch);
        if (!qa_fs_stage_read(stage, offset, scratch, count, &read, error) || read != count || memcmp(scratch, prefix.data + offset, count)) return false;
        offset += count;
    }
    size_t written = 0;
    return qa_fs_stage_write(stage, size, (qa_bytes){0}, &written, error) && !written;
}
bool qa_downloads_restore_checkpoint(qa_bytes bytes, qa_http *http, qa_fs_root *root, const qa_download_options *options,
    const qa_download_checkpoint_refs *refs, qa_downloads **out, qa_error *error)
{
    if (!out || *out || !options || !refs || !refs->resource || (bytes.size && !bytes.data))
        return fail(error, "Native download restore requires qualified candidate filesystem resources");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    if (qa_net_read_u32(&r) != UINT32_C(0x4a444151) || qa_net_read_u32(&r) != 1 ||
        qa_net_read_u32(&r) != options->jobs || qa_net_read_u64(&r) != options->maximum_pending_bytes)
        return fail(error, "Native download continuation schema/policy differs");
    uint64_t next = qa_net_read_u64(&r), reserved = qa_net_read_u64(&r);
    if (r.failed || options->jobs > qa_net_reader_remaining(&r) || reserved > options->maximum_pending_bytes)
        return fail(error, "Truncated download continuation inventory");
    qa_downloads *owner = NULL; if (!qa_downloads_create(http, root, options, &owner, error)) return false;
    qa_bytes *prefixes = calloc(options->jobs, sizeof(*prefixes));
    bool *staged = calloc(options->jobs, sizeof(*staged));
    owner->next_id = next; owner->reserved = reserved;
    bool ok = prefixes && staged;
    if (!ok) qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring download prefix inventory");
    for (uint32_t i = 0; ok && !r.failed && i < options->jobs; ++i) {
        bool present = q3_save_bool(&r); if (!present) continue;
        download_job *job = &owner->jobs[i]; job->owner = owner;
        ok = download_restore_job(&r, job); staged[i] = q3_save_bool(&r);
        if (ok && staged[i]) {
            uint64_t size = qa_net_read_u64(&r);
            ok = size <= SIZE_MAX && size == job->view.received && size <= job->view.limit && qa_net_read_bytes(&r, (size_t)size, &prefixes[i]);
        }
    }
    if (ok) ok = qa_net_reader_finish(&r) && download_inventory_valid(owner, staged);
    owner->reserved = 0;
    for (uint32_t i = 0; ok && i < options->jobs; ++i) {
        download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        ok = refs->resource(refs->context, &job->request, &job->view, staged[i], error);
        if (ok && staged[i]) {
            qa_fs_stage *stage = NULL; uint64_t nonce = 0;
            ok = refs->stage && job->view.limit <= options->maximum_pending_bytes - owner->reserved &&
                refs->stage(refs->context, &job->request, &job->view, prefixes[i], &stage, &nonce, error);
            if (ok && stage) {
                for (uint32_t j = 0; j < i; ++j) if (owner->jobs[j].stage == stage) { stage = NULL; ok = false; break; }
                if (stage) {
                    job->stage = stage; owner->reserved += job->view.limit;
                    ok = nonce && nonce != job->view.stage_nonce && download_stage_matches(stage, prefixes[i], error);
                    if (ok) { job->view.stage_nonce = nonce; if (job->request.stage_nonce) job->request.stage_nonce = nonce; }
                }
            } else if (ok) ok = false;
        }
    }
    if (ok) ok = owner->reserved == reserved && download_checkpoint_valid(owner);
    free(prefixes); free(staged);
    if (!ok) {
        owner->options.hooks.changed = NULL;
        qa_downloads_destroy(owner);
        if (!error || !error->code) fail(error, "Invalid native download continuation ownership"); return false;
    }
    *out = owner; return true;
}

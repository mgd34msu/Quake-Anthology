#include "qa/downloads.h"
#include "qa/network_downloads_save.h"
#include "qa/http_save.h"
#include "../service_save_fields.h"
#include "qa/vfs.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DOWNLOAD_RANGE_STREAMS = 8, DOWNLOAD_RANGE_ATTEMPTS = 3 };
typedef enum download_http_phase {
    DOWNLOAD_HTTP_NONE, DOWNLOAD_HTTP_WHOLE, DOWNLOAD_HTTP_PROBE,
    DOWNLOAD_HTTP_SPANS, DOWNLOAD_HTTP_RESET, DOWNLOAD_HTTP_FINISH, DOWNLOAD_HTTP_FAILED
} download_http_phase;
typedef struct download_job download_job;
typedef struct download_span {
    download_job *job;
    uint64_t first, last, received, request_start;
    qa_http_request_id http_id;
    qa_error failure;
    uint8_t attempts;
    bool complete;
} download_span;
struct download_job {
    qa_downloads *owner;
    qa_download_view view;
    qa_download_request request;
    qa_fs_stage *stage;
    qa_fs_root *target_root;
    qa_fs_identity publication_identity;
    uint64_t native_stage_nonce;
    qa_http_request_id http_id;
    char *http_url;
    uint64_t http_start, http_total;
    bool has_http_total, retained_stage;
    download_http_phase http_phase;
    char *etag;
    uint32_t span_count;
    download_span spans[DOWNLOAD_RANGE_STREAMS];
};
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
static bool http_progress(download_job *, qa_error *);
static uint32_t range_streams(const qa_download_options *options) {
    return options->range_streams ? options->range_streams : 4;
}
static void http_cancel(download_job *job) {
    qa_http_cancel(job->owner->http, job->http_id); job->http_id = 0;
    for (uint32_t i = 0; i < job->span_count; ++i) {
        qa_http_cancel(job->owner->http, job->spans[i].http_id);
        job->spans[i].http_id = 0;
    }
}
static uint64_t stage_extent(const download_job *job) {
    if (!job->span_count) return job->view.received;
    uint64_t extent = 0;
    for (uint32_t i = 0; i < job->span_count; ++i) {
        const download_span *span = &job->spans[i];
        uint64_t end = span->received ? span->first + span->received : 0;
        if (end > extent) extent = end;
    }
    return extent;
}
static bool stage_reset(download_job *job, qa_error *error) {
    uint64_t nonce = job->native_stage_nonce, initial = 0;
    if (!qa_fs_stage_close_checked(&job->stage, false, error)) return false;
    job->owner->reserved -= job->view.limit;
    bool opened = qa_fs_stage_open_checked(job->target_root, job->view.path, nonce, false,
        &job->stage, &initial, error);
    if (job->stage) job->owner->reserved += job->view.limit;
    if (!opened) return false;
    job->view.received = 0; job->http_start = 0; job->span_count = 0;
    memset(job->spans, 0, sizeof(job->spans));
    return true;
}
static download_job *lookup(qa_downloads *owner, qa_download_id id) {
    if (!owner || !id) return NULL;
    for (uint32_t i = 0; i < owner->options.jobs; ++i)
        if (owner->jobs[i].view.id == id) return &owner->jobs[i];
    return NULL;
}
static qa_fs_root *target_root(qa_downloads *owner, const qa_download_request *request, qa_error *error) {
    if (!owner->options.hooks.target_root) return owner->root;
    bool previous = owner->callback; owner->callback = true;
    qa_fs_root *root = owner->options.hooks.target_root(owner->options.hooks.context, request, error);
    owner->callback = previous;
    return root;
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
        if (!keep) job->native_stage_nonce = 0;
    }
    job->view.publication_pending=false;
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
        range_streams(options) > DOWNLOAD_RANGE_STREAMS ||
        sizeof(download_job) > SIZE_MAX / (size_t)options->jobs) return fail(error, "Invalid download owner options");
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
    http_cancel(job);
    if (keep && job->span_count) {
        qa_error failure = {0};
        /* A nonce-only fresh-process resume cannot describe holes. Preserve
         * an empty artifact rather than treating sparse file size as a prefix. */
        if (stage_extent(job) != job->view.received && !stage_reset(job, &failure)) {
            job->view.failure = failure; keep = false;
        }
        job->span_count = 0; memset(job->spans, 0, sizeof(job->spans));
        free(job->etag); job->etag = NULL;
        job->has_http_total = false; job->http_total = 0;
        job->http_phase = DOWNLOAD_HTTP_WHOLE;
    }
    job->view.state = QA_DOWNLOAD_CANCELED; discard_stage(job, keep && !job->view.published); notify(job);
}
void qa_downloads_cancel(qa_downloads *owner, qa_download_id id) { stop(owner, id, false); }
void qa_downloads_suspend(qa_downloads *owner, qa_download_id id) { stop(owner, id, true); }
void qa_downloads_release(qa_downloads *owner, qa_download_id id) {
    download_job *job = lookup(owner, id);
    if (!job || owner->callback) return;
    qa_downloads_cancel(owner, id);
    http_cancel(job);
    discard_stage(job, false); qa_fs_root_close(job->target_root); free((char *)job->view.path); free(job->http_url); free(job->etag); memset(job, 0, sizeof(*job));
}
void qa_downloads_destroy(qa_downloads *owner) {
    if (!owner || owner->callback) return;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) qa_downloads_release(owner, owner->jobs[i].view.id);
    qa_fs_root_close(owner->root); free(owner->jobs); free(owner);
}
static bool append(download_job *job, uint64_t offset, qa_bytes bytes, qa_error *error) {
    if (job->view.state != QA_DOWNLOAD_RECEIVING || job->view.publication_pending || !job->stage || (bytes.size && !bytes.data) || offset > job->view.received)
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
    if (!job || owner->callback || job->http_url) return fail(error, "Native download block does not own this job");
    bool ok = append(job, offset, bytes, error); if (ok) notify(job); return ok;
}
static bool publish(download_job *job,qa_error *error) {
    bool created=false;
    if (!qa_fs_stage_publish(job->stage,&job->publication_identity,true,&created,error)) {
        job->view.published=created;
        if (error) job->view.failure=*error;
        notify(job); return false;
    }
    if (!created) {
        job->view.publication_pending=false;
        return fail(error,"Download destination already exists");
    }
    job->view.published=true; job->view.failure=(qa_error){0};
    job->view.state=QA_DOWNLOAD_INSTALLING;
    discard_stage(job,false); notify(job); return true;
}
static bool finish(download_job *job, qa_error *error) {
    qa_downloads *owner = job->owner;
    if (job->view.state != QA_DOWNLOAD_RECEIVING || !job->stage) return fail(error, "Download is not receiving");
    if (job->view.publication_pending) return publish(job,error);
    uint64_t bytes;
    if (!qa_fs_stage_size(job->stage, &bytes, error)) return false;
    if (bytes != job->view.received || (job->request.exact_length && bytes != job->request.expected_bytes) ||
        (job->has_http_total && bytes != job->http_total))
        return fail(error, "Download length differs from admitted completion");
    qa_fs_identity identity;
    if (!qa_fs_stage_seal(job->stage, &identity, error)) return false;
    bool previous = owner->callback; owner->callback = true;
    bool ok = owner->options.hooks.inspect(owner->options.hooks.context, job->view.path, job->stage, bytes, error);
    owner->callback = previous;
    if (!ok) return false;
    job->publication_identity=identity; job->view.publication_pending=true;
    return publish(job,error);
}
bool qa_downloads_pump(qa_downloads *owner, qa_error *error) {
    if (!owner || owner->callback || !qa_http_callbacks_idle(owner->http))
        return fail(error, "Download installation requires shared HTTP callbacks to return");
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        download_job *job = &owner->jobs[i];
        if (job->view.id && job->view.state == QA_DOWNLOAD_RECEIVING &&
            job->http_url && !job->view.publication_pending) {
            qa_error failure = {0};
            if (!http_progress(job, &failure)) {
                http_cancel(job);
                bool prefix = stage_extent(job) == job->view.received;
                if (job->span_count && prefix && failure.code == QA_ERROR_IO) {
                    job->span_count = 0; memset(job->spans, 0, sizeof(job->spans));
                    free(job->etag); job->etag = NULL;
                    job->has_http_total = false; job->http_total = 0;
                    job->http_phase = DOWNLOAD_HTTP_WHOLE;
                }
                rejected(job, &failure, prefix && failure.code == QA_ERROR_IO);
                continue;
            }
        }
        if (job->view.id && job->view.publication_pending) {
            qa_error failure={0};
            if (!publish(job,&failure)) {
                if (!job->view.publication_pending) rejected(job,&failure,false);
                continue;
            }
        }
        if (!job->view.id || job->view.state != QA_DOWNLOAD_INSTALLING) continue;
        qa_error failure = {0}; owner->callback = true;
        bool ok = owner->options.hooks.remount(owner->options.hooks.context, job->view.path, &failure);
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
    if (!job || owner->callback || job->http_url) return fail(error, "Native download completion does not own job");
    if (finish(job, error)) return true;
    if (!job->view.publication_pending) rejected(job, error, false);
    return false;
}
static bool http_failure(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false;
}
static bool http_intermediate(const qa_http_response *response) {
    return (response->status >= 100 && response->status < 200) ||
        (response->status >= 300 && response->status < 400);
}
static bool headers(void *context, qa_http_request_id id, const qa_http_response *response, qa_error *error) {
    download_job *job = context; (void)id;
    if (job->http_phase == DOWNLOAD_HTTP_PROBE ||
        http_intermediate(response)) return true;
    if (job->etag && (response->status != 200 || !response->identity_encoding ||
        (response->etag && strcmp(response->etag, job->etag))))
        return http_failure(error, "HTTP whole-file fallback changed its ranged representation");
    if (job->http_start || response->status == 206) {
        if (response->status != 206 || !response->has_range || response->range_first != job->http_start ||
            response->range_total > job->view.limit || response->range_last != response->range_total - 1 ||
            (job->request.exact_length && response->range_total != job->request.expected_bytes))
            return http_failure(error, "HTTP range does not match retained download identity");
        job->http_total = response->range_total; job->has_http_total = true;
    } else if (response->status != 200) return http_failure(error, "HTTP download requires a complete successful response");
    if (response->has_content_length && response->identity_encoding && (response->content_length > job->view.limit - job->http_start ||
        (job->has_http_total && response->content_length != job->http_total - job->http_start) ||
        (job->request.exact_length && response->content_length != job->request.expected_bytes - job->http_start)))
        return http_failure(error, "HTTP download length differs from its admitted bounds");
    if (response->identity_encoding && response->has_content_length && !job->has_http_total) {
        job->http_total = job->http_start + response->content_length; job->has_http_total = true;
    }
    return true;
}
static bool body(void *context, qa_http_request_id id, const qa_http_response *response,
                   qa_bytes bytes, qa_error *error) {
    download_job *job = context; (void)id;
    if (job->http_phase == DOWNLOAD_HTTP_PROBE ||
        http_intermediate(response)) return true;
    if (response->status != 200 && response->status != 206) return http_failure(error, "HTTP download response rejected");
    if (job->has_http_total && (uint64_t)bytes.size > job->http_total - job->view.received)
        return http_failure(error, "HTTP download exceeds announced range");
    bool ok = append(job, job->view.received, bytes, error); if (ok) notify(job); return ok;
}
static void complete(void *context, qa_http_request_id id, const qa_http_response *response, const qa_error *error) {
    download_job *job = context;
    if (job->http_id != id) return;
    job->http_id = 0;
    if (job->http_phase == DOWNLOAD_HTTP_PROBE) {
        job->http_phase = DOWNLOAD_HTTP_WHOLE;
        if ((error && error->code != QA_OK) || response->status != 200 || !response->identity_encoding ||
            !response->has_content_length || response->content_length <= UINT64_C(1048576) ||
            !response->accepts_ranges || !qa_http_etag_is_strong(response->etag)) return;
        uint64_t total = response->content_length;
        if (total > job->view.limit || (job->request.exact_length && total != job->request.expected_bytes)) {
            http_failure(&job->view.failure, "HTTP range size differs from expected bounds");
            job->http_phase = DOWNLOAD_HTTP_FAILED; return;
        }
        size_t length = strlen(response->etag);
        job->etag = malloc(length + 1);
        if (!job->etag) {
            qa_error_set(&job->view.failure, QA_ERROR_MEMORY, 0, "Retaining HTTP representation condition");
            job->http_phase = DOWNLOAD_HTTP_FAILED; return;
        }
        memcpy(job->etag, response->etag, length + 1);
        uint32_t streams = range_streams(&job->owner->options);
        uint64_t width = total / streams + (total % streams != 0);
        job->http_total = total; job->has_http_total = true;
        for (uint64_t first = 0; first < total;) {
            download_span *span = &job->spans[job->span_count++];
            uint64_t last = total - first > width ? first + width - 1 : total - 1;
            *span = (download_span){.job = job, .first = first, .last = last, .request_start = first};
            first = last + 1;
        }
        job->http_phase = DOWNLOAD_HTTP_SPANS; return;
    }
    if (error && error->code != QA_OK) job->view.failure = *error;
    else if (response->status != 200 && response->status != 206)
        http_failure(&job->view.failure, "HTTP download did not complete successfully");
    else { job->http_phase = DOWNLOAD_HTTP_FINISH; return; }
    job->http_phase = DOWNLOAD_HTTP_FAILED;
}
static bool span_headers(void *context, qa_http_request_id id, const qa_http_response *response, qa_error *error) {
    download_span *span = context; download_job *job = span->job; (void)id;
    if (http_intermediate(response)) return true;
    if (response->status == 200 || response->status == 416) {
        job->http_phase = DOWNLOAD_HTTP_RESET; http_cancel(job);
        return http_failure(error, "HTTP server rejected byte ranges");
    }
    if (job->http_phase != DOWNLOAD_HTTP_SPANS || response->status != 206 || !response->has_range ||
        response->range_first != span->request_start || response->range_last != span->last ||
        response->range_total != job->http_total || !response->identity_encoding ||
        !response->etag || strcmp(response->etag, job->etag) ||
        (response->has_content_length && response->content_length != span->last - span->request_start + 1))
        return http_failure(error, "Invalid HTTP byte range response");
    return true;
}
static bool span_body(void *context, qa_http_request_id id, const qa_http_response *response,
    qa_bytes bytes, qa_error *error) {
    download_span *span = context; download_job *job = span->job; (void)id;
    if (http_intermediate(response)) return true;
    uint64_t remaining = span->last - span->first + 1 - span->received;
    if (job->http_phase != DOWNLOAD_HTTP_SPANS || response->status != 206 || (uint64_t)bytes.size > remaining)
        return http_failure(error, "HTTP byte range overflow or retired response");
    size_t written = 0;
    bool okay = qa_fs_stage_write(job->stage, span->first + span->received, bytes, &written, error);
    span->received += written; job->view.received += written;
    if (written) notify(job);
    return okay;
}
static void span_complete(void *context, qa_http_request_id id, const qa_http_response *response,
    const qa_error *error) {
    download_span *span = context; download_job *job = span->job;
    if (span->http_id != id) return;
    span->http_id = 0;
    if (job->http_phase != DOWNLOAD_HTTP_SPANS) return;
    if (error && error->code != QA_OK) span->failure = *error;
    else if (response->status != 206 || span->received != span->last - span->first + 1)
        http_failure(&span->failure, "Incomplete HTTP byte range");
    else span->complete = true;
}
static qa_http_request http_request(download_job *job, const char *method, uint64_t maximum,
    qa_http_callbacks callbacks) {
    return (qa_http_request){.url = job->http_url, .method = method, .timeout_ms = 120000,
        .connect_timeout_ms = 10000, .maximum_response_bytes = maximum, .maximum_redirects = 5,
        .same_origin_redirects = job->owner->options.same_origin_redirects, .callbacks = callbacks};
}
static bool http_progress(download_job *job, qa_error *error) {
    if (job->http_phase == DOWNLOAD_HTTP_FAILED) { if (error) *error = job->view.failure; return false; }
    if (job->http_phase == DOWNLOAD_HTTP_FINISH)
        return finish(job, error) || job->view.publication_pending;
    if (job->http_phase == DOWNLOAD_HTTP_RESET) {
        http_cancel(job);
        if (!stage_reset(job, error)) return false;
        job->http_phase = DOWNLOAD_HTTP_WHOLE;
    }
    if (job->http_phase == DOWNLOAD_HTTP_SPANS) {
        bool all = true;
        for (uint32_t i = 0; i < job->span_count; ++i) {
            download_span *span = &job->spans[i];
            if (span->complete) continue;
            all = false;
            if (span->http_id) continue;
            if (span->failure.code != QA_OK && (span->attempts >= DOWNLOAD_RANGE_ATTEMPTS ||
                span->received == span->last - span->first + 1)) {
                if (error) *error = span->failure;
                return false;
            }
            char range[80];
            span->request_start = span->first + span->received;
            snprintf(range, sizeof(range), "bytes=%" PRIu64 "-%" PRIu64, span->request_start, span->last);
            qa_http_header request_headers[] = {{"Range", range}, {"If-Range", job->etag}, {"Accept-Encoding", "identity"}};
            qa_http_request request = http_request(job, "GET", span->last - span->request_start + 1,
                (qa_http_callbacks){span, span_headers, span_body, span_complete});
            request.headers = request_headers; request.header_count = 3;
            ++span->attempts; span->failure = (qa_error){0};
            if (!qa_http_submit(job->owner->http, &request, &span->http_id, error)) return false;
        }
        if (all) { job->http_phase = DOWNLOAD_HTTP_FINISH; return finish(job, error) || job->view.publication_pending; }
        return true;
    }
    if (job->http_id) return true;
    if (job->http_phase == DOWNLOAD_HTTP_PROBE) {
        qa_http_header identity = {"Accept-Encoding", "identity"};
        qa_http_request request = http_request(job, "HEAD", 0, (qa_http_callbacks){job, headers, body, complete});
        request.headers = &identity; request.header_count = 1;
        return qa_http_submit(job->owner->http, &request, &job->http_id, error);
    }
    if (job->http_phase == DOWNLOAD_HTTP_WHOLE) {
        char range[80]; qa_http_header fields[3]; size_t count = 0;
        if (job->http_start) {
            snprintf(range, sizeof(range), "bytes=%" PRIu64 "-", job->http_start);
            fields[count++] = (qa_http_header){"Range", range};
        }
        if (job->etag) {
            fields[count++] = (qa_http_header){"If-Match", job->etag};
            fields[count++] = (qa_http_header){"Accept-Encoding", "identity"};
        }
        qa_http_request request = http_request(job, "GET", job->view.limit - job->http_start,
            (qa_http_callbacks){job, headers, body, complete});
        request.headers = fields; request.header_count = count;
        return qa_http_submit(job->owner->http, &request, &job->http_id, error);
    }
    return true;
}
bool qa_downloads_begin(qa_downloads *owner, const qa_download_request *request, const char *url,
                         qa_download_id *out, qa_error *error) {
    if (!owner || owner->callback || !request || !request->path || !out || !owner->next_id ||
        request->maximum_bytes > INT64_MAX || request->maximum_bytes > owner->options.maximum_pending_bytes - owner->reserved ||
        (request->exact_length && request->expected_bytes > request->maximum_bytes) ||
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
    qa_fs_root *root = target_root(owner, &normalized, error);
    if (!root) { free(path); return false; }
    qa_fs_entry_kind kind = QA_FS_MISSING;
    if (!qa_fs_root_status(root, path, &kind, NULL, error)) { free(path); return false; }
    if (kind != QA_FS_MISSING) { free(path); return fail(error, "Download destination is installed already"); }
    qa_fs_stage *stage; uint64_t initial;
    uint64_t nonce = request->stage_nonce ? request->stage_nonce : owner->next_id;
    if (!qa_fs_stage_open(root, path, nonce, request->resume, &stage, &initial, error)) { free(path); return false; }
    if (initial > request->maximum_bytes || (request->exact_length && initial > request->expected_bytes)) {
        qa_fs_stage_close(stage, false); free(path); return fail(error, "Resumed download exceeds admitted identity");
    }
    qa_fs_root_retain(root);
    *job = (download_job){.owner = owner, .request = normalized, .stage = stage, .target_root = root, .native_stage_nonce = nonce, .http_start = initial,
        .view = {.id = owner->next_id++, .path = path, .received = initial, .limit = request->maximum_bytes,
                 .state = QA_DOWNLOAD_RECEIVING, .stage_nonce = nonce}};
    owner->reserved += request->maximum_bytes;
    if (url && !(request->exact_length && initial == request->expected_bytes)) {
        if (strlen(url) > 65535) {
            discard_stage(job, request->resume); qa_fs_root_close(job->target_root); free(path); memset(job, 0, sizeof(*job));
            return fail(error, "HTTP download address exceeds retained recipe capacity");
        }
        job->http_url = malloc(strlen(url) + 1);
        if (!job->http_url) {
            discard_stage(job, request->resume); qa_fs_root_close(job->target_root); free(path); memset(job, 0, sizeof(*job));
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining HTTP download address"); return false;
        }
        strcpy(job->http_url, url);
        uint64_t expected = request->exact_length ? request->expected_bytes : request->maximum_bytes;
        job->http_phase = !initial && expected > UINT64_C(1048576) &&
            range_streams(&owner->options) > 1 ? DOWNLOAD_HTTP_PROBE : DOWNLOAD_HTTP_WHOLE;
    }
    *out = job->view.id; notify(job);
    if (url && request->exact_length && initial == request->expected_bytes) {
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
size_t qa_downloads_count(const qa_downloads *owner) {
    size_t count = 0;
    if (owner) for (uint32_t i = 0; i < owner->options.jobs; ++i)
        if (owner->jobs[i].view.id) ++count;
    return count;
}
bool qa_downloads_at(const qa_downloads *owner, size_t ordinal, qa_download_view *out) {
    if (!owner || !out) return false;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        if (!owner->jobs[i].view.id) continue;
        if (!ordinal) { *out = owner->jobs[i].view; return true; }
        --ordinal;
    }
    return false;
}

static bool http_recipe(const download_job *job, qa_http_request_id id, const char *method,
    uint64_t maximum, const qa_http_header *fields, size_t count,
    qa_http_continuation_view *view, qa_error *error)
{
    if (!job->http_url || !qa_http_continuation_read(job->owner->http, id, view, error)) return false;
    if (strcmp(view->url, job->http_url) || strcmp(view->method, method) || view->request_body.size ||
        view->header_count != count || view->timeout_ms != 120000 || view->connect_timeout_ms != 10000 ||
        view->maximum_redirects != 5 || view->same_origin_redirects != job->owner->options.same_origin_redirects ||
        view->maximum_response_bytes != maximum || view->canceled || view->failure.code != QA_OK)
        return fail(error, "HTTP download consumer differs from its actual request recipe");
    for (size_t i = 0; i < count; ++i) {
        const char *line = NULL;
        if (!qa_http_continuation_header(job->owner->http, id, i, &line, error)) return false;
        size_t name = strlen(fields[i].name);
        if (strncmp(line, fields[i].name, name) || line[name] != ':' || line[name + 1] != ' ' ||
            strcmp(line + name + 2, fields[i].value))
            return fail(error, "HTTP download request lost its retained conditional header");
    }
    return true;
}
static bool download_http_valid(const download_job *job, qa_error *error)
{
    if (job->http_id) {
        qa_http_continuation_view view;
        char range[80]; qa_http_header fields[3]; size_t count = 0;
        bool probe = job->http_phase == DOWNLOAD_HTTP_PROBE;
        if (probe) fields[count++] = (qa_http_header){"Accept-Encoding", "identity"};
        else {
            if (job->http_start) {
                snprintf(range, sizeof(range), "bytes=%" PRIu64 "-", job->http_start);
                fields[count++] = (qa_http_header){"Range", range};
            }
            if (job->etag) {
                fields[count++] = (qa_http_header){"If-Match", job->etag};
                fields[count++] = (qa_http_header){"Accept-Encoding", "identity"};
            }
        }
        if (!http_recipe(job, job->http_id, probe ? "HEAD" : "GET",
            probe ? 0 : job->view.limit - job->http_start, fields, count, &view, error)) return false;
        uint64_t received = view.response.bytes - view.response_body_start;
        bool successful = !probe && (view.response.status == 200 || view.response.status == 206);
        if (job->view.received - job->http_start != (successful ? received : 0))
            return fail(error, "HTTP download received prefix differs from its actual response continuation");
        if (job->has_http_total && view.response.status == 206 &&
            (!view.response.has_range || view.response.range_first != job->http_start ||
             view.response.range_total != job->http_total || !job->http_total || view.response.range_last != job->http_total - 1))
            return fail(error, "HTTP download range response differs from its retained total");
    }
    for (uint32_t i = 0; i < job->span_count; ++i) {
        const download_span *span = &job->spans[i]; if (!span->http_id) continue;
        char range[80];
        snprintf(range, sizeof(range), "bytes=%" PRIu64 "-%" PRIu64, span->request_start, span->last);
        qa_http_header fields[] = {{"Range", range}, {"If-Range", job->etag}, {"Accept-Encoding", "identity"}};
        qa_http_continuation_view view;
        if (!http_recipe(job, span->http_id, "GET", span->last - span->request_start + 1,
            fields, 3, &view, error)) return false;
        uint64_t received = view.response.bytes - view.response_body_start;
        bool successful = view.response.status == 206;
        if (span->first + span->received - span->request_start != (successful ? received : 0))
            return fail(error, "HTTP span prefix differs from its actual response continuation");
        if (successful && (!view.response.has_range || view.response.range_first != span->request_start ||
            view.response.range_last != span->last || view.response.range_total != job->http_total ||
            !view.response.etag || strcmp(view.response.etag, job->etag) || !view.response.identity_encoding))
            return fail(error, "HTTP span response differs from its retained representation");
    }
    return true;
}
static bool spans_valid(const download_job *job)
{
    if ((unsigned)job->http_phase > DOWNLOAD_HTTP_FAILED || job->span_count > DOWNLOAD_RANGE_STREAMS ||
        (job->etag && !qa_http_etag_is_strong(job->etag)) ||
        (job->http_id && job->http_phase != DOWNLOAD_HTTP_PROBE && job->http_phase != DOWNLOAD_HTTP_WHOLE)) return false;
    if (!job->span_count) return job->http_phase != DOWNLOAD_HTTP_SPANS;
    if (!job->etag || !job->has_http_total || !job->http_total ||
        job->span_count > range_streams(&job->owner->options)) return false;
    uint64_t next = 0, received = 0;
    for (uint32_t i = 0; i < job->span_count; ++i) {
        const download_span *span = &job->spans[i];
        if (span->job != job || span->first != next || span->last < span->first || span->last >= job->http_total ||
            span->received > span->last - span->first + 1 || span->attempts > DOWNLOAD_RANGE_ATTEMPTS ||
            span->request_start < span->first || span->request_start > span->first + span->received ||
            (span->complete && (span->received != span->last - span->first + 1 || span->http_id)) ||
            (span->http_id && (job->http_phase != DOWNLOAD_HTTP_SPANS || !span->attempts)) ||
            (unsigned)span->failure.code > QA_ERROR_NOT_FOUND ||
            !memchr(span->failure.message, 0, sizeof(span->failure.message))) return false;
        next = span->last + 1; received += span->received;
    }
    return next == job->http_total && received == job->view.received;
}
static bool download_job_valid(const qa_downloads *owner, const download_job *job, bool staged)
{
    const qa_download_request *request = &job->request; const qa_download_view *view = &job->view;
    if (!spans_valid(job) || job->owner != owner || !job->target_root || !view->path || request->path != view->path || !*view->path ||
            (unsigned)view->state > QA_DOWNLOAD_CANCELED || (unsigned)view->failure.code > QA_ERROR_NOT_FOUND ||
            !memchr(view->failure.message, 0, sizeof(view->failure.message)) ||
            request->maximum_bytes != view->limit || view->limit > INT64_MAX || view->received > view->limit ||
            (request->exact_length && request->expected_bytes > view->limit) ||
            (request->resume && !request->stage_nonce) || !view->stage_nonce ||
            (request->stage_nonce && request->stage_nonce != view->stage_nonce) || job->http_start > view->received ||
            (job->has_http_total && (job->http_total > view->limit || job->http_start > job->http_total)) ||
            (owner->next_id && view->id >= owner->next_id) ||
            staged != (view->state == QA_DOWNLOAD_RECEIVING) ||
            (job->http_id && !staged) ||
            (job->stage && !job->native_stage_nonce && !view->published) ||
            (view->publication_pending && (!staged || job->http_id || view->mounted)) ||
            (job->retained_stage && (staged || job->http_id || view->published || view->mounted ||
                (view->state != QA_DOWNLOAD_FAILED && view->state != QA_DOWNLOAD_CANCELED))) ||
            (view->state == QA_DOWNLOAD_INSTALLING && (!view->published || view->mounted)) ||
            (view->state == QA_DOWNLOAD_COMPLETE && (!view->published || !view->mounted)) ||
            (view->mounted && view->state != QA_DOWNLOAD_COMPLETE) ||
            (staged && ((view->published && !view->publication_pending) || view->mounted)) ||
            (view->published && request->exact_length &&
                view->received != request->expected_bytes)) return false;
    for (uint32_t i = 0; i < job->span_count; ++i)
        if (job->spans[i].http_id && !staged) return false;
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
        if (!download_job_valid(owner, job, has_stage) || !download_http_valid(job, NULL)) return false;
        const qa_download_view *view = &job->view;
        for (uint32_t j = 0; j < i; ++j) if (owner->jobs[j].view.id == view->id ||
            (job->stage && owner->jobs[j].stage == job->stage)) return false;
        qa_http_request_id ids[DOWNLOAD_RANGE_STREAMS + 1] = {job->http_id};
        for (uint32_t k = 0; k < job->span_count; ++k) ids[k + 1] = job->spans[k].http_id;
        for (uint32_t k = 0; k <= job->span_count; ++k) {
            if (!ids[k]) continue;
            for (uint32_t n = 0; n < k; ++n) if (ids[n] == ids[k]) return false;
            for (uint32_t j = 0; j < i; ++j) {
                const download_job *other = &owner->jobs[j];
                if (other->http_id == ids[k]) return false;
                for (uint32_t n = 0; n < other->span_count; ++n)
                    if (other->spans[n].http_id == ids[k]) return false;
            }
        }
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
    bool okay = qa_net_write_u64(w, view->id) && qa_net_write_u64(w, length) && qa_net_write_data(w, view->path, length) &&
        qa_net_write_u64(w, request->maximum_bytes) && qa_net_write_u64(w, request->expected_bytes) &&
        qa_net_write_u8(w, request->exact_length) &&
        qa_net_write_u64(w, request->stage_nonce) && qa_net_write_u8(w, request->resume) &&
        qa_net_write_u64(w, view->received) && qa_net_write_u64(w, view->limit) && qa_net_write_u32(w, view->state) &&
        qa_net_write_u32(w, view->failure.code) && qa_net_write_u64(w, view->failure.offset) &&
        qa_net_write_data(w, view->failure.message, sizeof(view->failure.message)) &&
        qa_net_write_u8(w, view->published) && qa_net_write_u8(w, view->mounted) &&
        qa_net_write_u64(w, view->stage_nonce) &&
        qa_net_write_u64(w, job->http_start) && qa_net_write_u64(w, job->http_total) && qa_net_write_u8(w, job->has_http_total) &&
        qa_net_write_u64(w, job->http_id) && qa_net_write_u8(w, job->http_url != NULL) &&
        (!job->http_url || qa_net_write_string(w, job->http_url)) && qa_net_write_u8(w, job->retained_stage) &&
        qa_net_write_u8(w,view->publication_pending) &&
        qa_net_write_u32(w, job->http_phase) && qa_net_write_u32(w, job->span_count) &&
        qa_net_write_u8(w, job->etag != NULL);
    if (okay && job->etag) {
        size_t bytes = strlen(job->etag);
        okay = qa_net_write_u64(w, bytes) && qa_net_write_data(w, job->etag, bytes);
    }
    for (uint32_t i = 0; okay && i < job->span_count; ++i) {
        const download_span *span = &job->spans[i];
        okay = qa_net_write_u64(w, span->first) && qa_net_write_u64(w, span->last) &&
            qa_net_write_u64(w, span->received) && qa_net_write_u64(w, span->request_start) &&
            qa_net_write_u64(w, span->http_id) && qa_net_write_u8(w, span->attempts) &&
            qa_net_write_u8(w, span->complete) && qa_net_write_u32(w, span->failure.code) &&
            qa_net_write_u64(w, span->failure.offset) &&
            qa_net_write_data(w, span->failure.message, sizeof(span->failure.message));
    }
    return okay;
}
bool qa_downloads_checkpoint(const qa_downloads *owner, qa_buffer *out, qa_error *error)
{
    const size_t fixed_job = 467 + 9 + sizeof(uint64_t) + DOWNLOAD_RANGE_STREAMS *
        (5 * sizeof(uint64_t) + 2 + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(((qa_error *)0)->message));
    if (!out || !download_checkpoint_valid(owner) || fixed_job > (SIZE_MAX - 40) / (size_t)owner->options.jobs)
        return fail(error, "Download continuation requires idle native jobs and HTTP callbacks");
    size_t capacity = 40 + (size_t)owner->options.jobs * fixed_job;
    for (uint32_t i = 0; i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        size_t length = strlen(job->view.path);
        uint64_t prefix_size = job->stage ? stage_extent(job) : 0;
        if (job->retained_stage) {
            qa_fs_stage *artifact = NULL;
            if (!qa_fs_stage_open_readonly(job->target_root, job->view.path, job->view.stage_nonce, &artifact, &prefix_size, error)) return false;
            qa_fs_stage_close(artifact, true);
            if (prefix_size < job->view.received || prefix_size > job->view.limit) return fail(error, "Retained download artifact prefix exceeds its actual accepted limits");
        }
        if (length > SIZE_MAX - capacity || prefix_size > SIZE_MAX - capacity - length)
            return fail(error, "Download continuation extent exceeds memory");
        capacity += length + (size_t)prefix_size;
        if (job->etag) {
            size_t bytes = strlen(job->etag);
            if (bytes > SIZE_MAX - capacity) return fail(error, "HTTP condition continuation exceeds memory");
            capacity += bytes;
        }
        if (job->http_url) {
            size_t url_bytes = strlen(job->http_url) + 1;
            if (url_bytes > SIZE_MAX - capacity) return fail(error, "Download address continuation extent exceeds memory");
            capacity += url_bytes;
        }
    }
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding native download continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x52444151)) &&
        qa_net_write_u32(&w, owner->options.jobs) && qa_net_write_u64(&w, owner->options.maximum_pending_bytes) &&
        qa_net_write_u64(&w, owner->next_id) && qa_net_write_u64(&w, owner->reserved) &&
        qa_net_write_u32(&w, range_streams(&owner->options));
    uint8_t scratch[65536];
    for (uint32_t i = 0; ok && i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; ok = qa_net_write_u8(&w, job->view.id != 0);
        if (!ok || !job->view.id) continue;
        ok = download_save_job(&w, job) && qa_net_write_u8(&w, job->stage != NULL);
        if (!ok || (!job->stage && !job->retained_stage)) continue;
        qa_fs_stage *stage = job->stage;
        bool artifact = job->retained_stage;
        uint64_t size = 0;
        if (artifact) ok = qa_fs_stage_open_readonly(job->target_root, job->view.path, job->view.stage_nonce, &stage, &size, error);
        else ok = qa_fs_stage_size(stage, &size, error);
        ok = ok && (artifact ? size >= stage_extent(job) && size <= job->view.limit : size == stage_extent(job)) && qa_net_write_u64(&w, size);
        uint64_t offset = 0;
        while (ok && offset < size) {
            size_t count = size - offset > sizeof(scratch) ? sizeof(scratch) : (size_t)(size - offset), read = 0;
            ok = qa_fs_stage_read(stage, offset, scratch, count, &read, error) && read == count && qa_net_write_data(&w, scratch, read);
            offset += read;
        }
        uint64_t after = 0; if (ok) ok = qa_fs_stage_size(stage, &after, error) && after == size;
        if (artifact) qa_fs_stage_close(stage, true);
    }
    if (!ok || w.failed) { free(data); if (!error || !error->code) fail(error, "Native stage changed during continuation capture"); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
static bool download_restore_job(qa_net_reader *r, download_job *job, bool ranges)
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
    request->exact_length = q3_save_bool(r); request->stage_nonce = qa_net_read_u64(r); request->resume = q3_save_bool(r);
    view->received = qa_net_read_u64(r); view->limit = qa_net_read_u64(r); view->state = (qa_download_state)qa_net_read_u32(r);
    view->failure.code = (qa_status)qa_net_read_u32(r); uint64_t failure_offset = qa_net_read_u64(r);
    if (failure_offset > SIZE_MAX || !qa_net_read_data(r, view->failure.message, sizeof(view->failure.message)))
        return qa_net_reader_fail(r, "Invalid download continuation error extent");
    view->failure.offset = (size_t)failure_offset; view->published = q3_save_bool(r); view->mounted = q3_save_bool(r);
    view->stage_nonce = qa_net_read_u64(r); job->http_start = qa_net_read_u64(r); job->http_total = qa_net_read_u64(r);
    job->has_http_total = q3_save_bool(r); job->http_id = qa_net_read_u64(r);
    bool has_url = q3_save_bool(r);
    if (has_url && !service_restore_text(r, &job->http_url, 65535)) return false;
    job->retained_stage = q3_save_bool(r);
    view->publication_pending=q3_save_bool(r);
    if (!ranges) {
        job->http_phase = job->http_url ? DOWNLOAD_HTTP_WHOLE : DOWNLOAD_HTTP_NONE;
        return !r->failed;
    }
    job->http_phase = (download_http_phase)qa_net_read_u32(r);
    job->span_count = qa_net_read_u32(r);
    if (job->span_count > DOWNLOAD_RANGE_STREAMS)
        return qa_net_reader_fail(r, "Invalid HTTP span continuation count");
    if (q3_save_bool(r)) {
        uint64_t etag_length = qa_net_read_u64(r);
        if (!etag_length || etag_length >= SIZE_MAX || etag_length > qa_net_reader_remaining(r))
            return qa_net_reader_fail(r, "Invalid HTTP condition continuation extent");
        job->etag = malloc((size_t)etag_length + 1);
        if (!job->etag) {
            qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Restoring HTTP representation condition");
            r->failed = true; return false;
        }
        if (!qa_net_read_data(r, job->etag, (size_t)etag_length) || memchr(job->etag, 0, (size_t)etag_length))
            return qa_net_reader_fail(r, "Embedded NUL in HTTP condition continuation");
        job->etag[etag_length] = 0;
    }
    for (uint32_t i = 0; i < job->span_count; ++i) {
        download_span *span = &job->spans[i]; span->job = job;
        span->first = qa_net_read_u64(r); span->last = qa_net_read_u64(r);
        span->received = qa_net_read_u64(r); span->request_start = qa_net_read_u64(r);
        span->http_id = qa_net_read_u64(r); span->attempts = qa_net_read_u8(r);
        span->complete = q3_save_bool(r); span->failure.code = (qa_status)qa_net_read_u32(r);
        uint64_t offset = qa_net_read_u64(r);
        if (offset > SIZE_MAX || !qa_net_read_data(r, span->failure.message, sizeof(span->failure.message)))
            return qa_net_reader_fail(r, "Invalid HTTP span failure continuation");
        span->failure.offset = (size_t)offset;
    }
    return !r->failed;
}
static bool download_stage_matches(qa_fs_stage *stage, qa_bytes prefix, bool writable, qa_error *error)
{
    uint64_t size = 0; if (!qa_fs_stage_size(stage, &size, error) || size != prefix.size) return false;
    uint8_t scratch[65536]; size_t offset = 0;
    while (offset < prefix.size) {
        size_t count = prefix.size - offset, read = 0; if (count > sizeof(scratch)) count = sizeof(scratch);
        if (!qa_fs_stage_read(stage, offset, scratch, count, &read, error) || read != count || memcmp(scratch, prefix.data + offset, count)) return false;
        offset += count;
    }
    if (!writable) return true;
    size_t written = 0;
    return qa_fs_stage_write(stage, size, (qa_bytes){0}, &written, error) && !written;
}
bool qa_downloads_restore_checkpoint(qa_bytes bytes, qa_http *http, qa_fs_root *root, const qa_download_options *options,
    const qa_download_checkpoint_refs *refs, qa_downloads **out, qa_error *error)
{
    if (!out || *out || !options || !refs || !refs->resource || (bytes.size && !bytes.data))
        return fail(error, "Native download restore requires qualified candidate filesystem resources");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    uint32_t magic=qa_net_read_u32(&r);
    bool ranges = magic == UINT32_C(0x52444151);
    if ((!ranges && magic != UINT32_C(0x4a444151)) ||
        qa_net_read_u32(&r) != options->jobs || qa_net_read_u64(&r) != options->maximum_pending_bytes)
        return fail(error, "Native download continuation schema/policy differs");
    uint64_t next = qa_net_read_u64(&r), reserved = qa_net_read_u64(&r);
    if (ranges && qa_net_read_u32(&r) != range_streams(options))
        return fail(error, "Download range continuation policy differs");
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
        ok = download_restore_job(&r, job, ranges); staged[i] = q3_save_bool(&r);
        if (ok) {
            job->target_root = target_root(owner, &job->request, error);
            ok = job->target_root != NULL;
            if (ok) qa_fs_root_retain(job->target_root);
        }
        if (ok && (staged[i] || job->retained_stage)) {
            uint64_t size = qa_net_read_u64(&r);
            ok = size <= SIZE_MAX && (staged[i] ? size == stage_extent(job) : size >= stage_extent(job)) &&
                size <= job->view.limit && qa_net_read_bytes(&r, (size_t)size, &prefixes[i]);
        }
    }
    if (ok) ok = qa_net_reader_finish(&r) && download_inventory_valid(owner, staged);
    owner->reserved = 0;
    for (uint32_t i = 0; ok && i < options->jobs; ++i) {
        download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        ok = refs->resource(refs->context, &job->request, &job->view, staged[i], error);
        if (ok && staged[i]) {
            qa_fs_stage *stage = NULL; uint64_t nonce = 0;
            ok=job->view.limit <= options->maximum_pending_bytes-owner->reserved;
            if(ok && job->view.published)
                ok=qa_fs_stage_open_published_checked(job->target_root,job->view.path,prefixes[i],&stage,&job->publication_identity,error);
            else if(ok) ok=refs->stage &&
                refs->stage(refs->context,&job->request,&job->view,prefixes[i],&stage,&nonce,error);
            if (stage) {
                for (uint32_t j = 0; j < i; ++j) if (owner->jobs[j].stage == stage) { stage = NULL; ok = false; break; }
                if (stage) {
                    job->stage = stage; owner->reserved += job->view.limit;
                    ok = ok && (job->view.published || (nonce && nonce != job->view.stage_nonce)) &&
                        download_stage_matches(stage,prefixes[i],!job->view.published,error);
                    if (ok) job->native_stage_nonce = nonce;
                    if(ok && job->view.publication_pending && !job->view.published)
                        ok=qa_fs_stage_seal(stage,&job->publication_identity,error);
                }
            } else if (ok) ok = false;
        }
        if (ok && job->retained_stage) {
            qa_fs_stage *artifact = NULL;
            ok = refs->artifact && refs->artifact(refs->context, &job->request, &job->view, &artifact, error) && artifact &&
                download_stage_matches(artifact, prefixes[i], false, error);
            qa_fs_stage_close(artifact, true);
            if (ok) job->native_stage_nonce = job->view.stage_nonce;
        }
    }
    if (ok) ok = owner->reserved == reserved && download_checkpoint_valid(owner);
    for (uint32_t i = 0; ok && i < options->jobs; ++i) {
        download_job *job = &owner->jobs[i];
        if (job->http_id) {
            qa_http_callbacks callbacks = {job, headers, body, complete};
            ok = qa_http_restore_callbacks(http, job->http_id, &callbacks, error);
        }
        for (uint32_t n = 0; ok && n < job->span_count; ++n) {
            download_span *span = &job->spans[n]; if (!span->http_id) continue;
            qa_http_callbacks callbacks = {span, span_headers, span_body, span_complete};
            ok = qa_http_restore_callbacks(http, span->http_id, &callbacks, error);
        }
    }
    free(prefixes); free(staged);
    if (!ok) {
        owner->options.hooks.changed = NULL;
        qa_downloads_destroy(owner);
        if (!error || !error->code) fail(error, "Invalid native download continuation ownership");
        return false;
    }
    *out = owner; return true;
}
bool qa_downloads_resources_ready(qa_downloads *owner, const qa_download_checkpoint_refs *refs, qa_error *error)
{
    if (!refs || !refs->resource || !download_checkpoint_valid(owner))
        return fail(error, "Download resource qualification requires idle represented jobs");
    bool ok = true; owner->callback = true;
    for (uint32_t i = 0; ok && i < owner->options.jobs; ++i) {
        const download_job *job = &owner->jobs[i]; if (!job->view.id) continue;
        ok = refs->resource(refs->context, &job->request, &job->view, job->stage != NULL, error);
    }
    owner->callback = false;
    return ok;
}
bool qa_downloads_handoff_ready(const qa_downloads *active, const qa_downloads *candidate, qa_error *error)
{
    if (!active || !candidate || active == candidate || active->options.jobs != candidate->options.jobs ||
        active->options.maximum_pending_bytes != candidate->options.maximum_pending_bytes ||
        range_streams(&active->options) != range_streams(&candidate->options) ||
        !download_checkpoint_valid(active) || !download_checkpoint_valid(candidate))
        return fail(error, "Download native handoff requires matching idle owners");
    bool ok = true;
    for (uint32_t i = 0; i < candidate->options.jobs; ++i) {
        const download_job *job = candidate->jobs + i, *original = active->jobs + i;
        if (job->view.id != original->view.id || (job->stage != NULL) != (original->stage != NULL)) { ok = false; break; }
    }
    qa_buffer current = {0}, restored = {0};
    if (ok) ok = qa_downloads_checkpoint(active, &current, error) && qa_downloads_checkpoint(candidate, &restored, error);
    if (ok) ok = current.size == restored.size && !memcmp(current.data, restored.data, current.size);
    qa_buffer_free(&current); qa_buffer_free(&restored);
    if (!ok && (!error || error->code == QA_OK))
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Live download advanced beyond its saved job and native prefix cut");
    return ok;
}
void qa_downloads_handoff_publish(qa_downloads *active, qa_downloads *candidate)
{
    for (uint32_t i = 0; i < active->options.jobs; ++i) {
        download_job *old = active->jobs + i, *next = candidate->jobs + i;
        if (!old->stage) continue;
        qa_fs_stage *stage = old->stage; old->stage = next->stage; next->stage = stage;
        uint64_t nonce = old->native_stage_nonce; old->native_stage_nonce = next->native_stage_nonce; next->native_stage_nonce = nonce;
        qa_fs_identity identity=old->publication_identity;
        old->publication_identity=next->publication_identity; next->publication_identity=identity;
    }
}

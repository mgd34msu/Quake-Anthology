#include "llm_internal.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum job_kind { JOB_REQUEST, JOB_MODELS } job_kind;
struct llm_job {
    llm_job *next;
    qa_llm *owner;
    qa_arena storage;
    qa_llm_request_id id;
    qa_http_request_id http_id;
    qa_llm_provider provider;
    job_kind kind;
    const char *prompt, *instructions, *model, *effort, *base_url, *rejected_token;
    llm_subscription credential;
    qa_llm_observer observer;
    llm_stream stream;
    llm_text response;
    qa_error error;
    double deadline;
    uint64_t catalog_generation, refresh_ticket;
    unsigned status;
    bool active, result, canceled, retry, needs_metadata, waiting_refresh;
};
static void job_free(llm_job *job) {
    if (!job) return;
    llm_stream_destroy(&job->stream); qa_buffer_free(&job->response.buffer);
    qa_arena_destroy(&job->storage); free(job);
}
static bool stream_emit(void *context, qa_bytes bytes, qa_error *error) {
    llm_job *job = context;
    if (job->canceled) return llm_fail(error, "LLM request canceled");
    if (job->observer.text) job->observer.text(job->observer.context, job->id, bytes);
    return !job->canceled || llm_fail(error, "LLM request canceled");
}
static bool content_type(const char *text) {
    if (!text) return false;
    while (*text == ' ' || *text == '\t') ++text;
    const char *expected = "text/event-stream";
    while (*expected) {
        unsigned c = (unsigned char)*text++; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)*expected++) return false;
    }
    while (*text == ' ' || *text == '\t') ++text;
    return !*text || *text == ';';
}
static bool headers(void *context, qa_http_request_id id, const qa_http_response *response, qa_error *error) {
    llm_job *job = context; (void)id;
    if (job->canceled) return false;
    if (response->status >= 100 && response->status < 200) return true;
    job->status = response->status;
    if (response->status < 200 || response->status >= 300) {
        qa_error_set(error, QA_ERROR_IO, 0, "LLM service returned HTTP %u", response->status); return false;
    }
    if (job->kind == JOB_REQUEST && job->provider != QA_LLM_SUBSCRIPTION && !content_type(response->content_type))
        return llm_fail(error, "LLM service did not return an event stream");
    return true;
}
static bool body(void *context, qa_http_request_id id, const qa_http_response *response, qa_bytes bytes, qa_error *error) {
    llm_job *job = context; (void)response;
    if (job->canceled) return false;
    qa_llm *s = job->owner; ++s->busy;
    bool ok = job->kind == JOB_MODELS ? llm_text_add(&job->response, bytes, error) : llm_stream_feed(&job->stream, bytes, error);
    if (ok && job->kind == JOB_REQUEST && job->stream.stopped) {
        ok = llm_stream_finish(&job->stream, error);
        if (!ok && error) job->error = *error;
        job->result = true; job->active = false;
        qa_http_cancel(s->options.http, id);
    }
    --s->busy; return ok;
}
static void complete(void *context, qa_http_request_id id, const qa_http_response *response, const qa_error *error) {
    llm_job *job = context; qa_llm *s = job->owner; (void)id;
    ++s->busy; job->status = response->status;
    job->active = false; job->result = true;
    if (error) job->error = *error;
    else if (job->kind == JOB_REQUEST) (void)llm_stream_finish(&job->stream, &job->error);
    --s->busy;
}
static bool request_body(llm_job *job, const char *session, qa_buffer *out, qa_error *error) {
    qa_json_writer w = {0}; qa_json_writer_object(&w); qa_json_writer_key(&w, "model");
    const char *model = job->model;
    if (job->provider == QA_LLM_SUBSCRIPTION && (!strncmp(model, "openai:", 7) || !strncmp(model, "openai/", 7))) model += 7;
    qa_json_writer_string(&w, model);
    if (job->provider == QA_LLM_OTHER_API) {
        if (job->effort) { qa_json_writer_key(&w, "reasoning_effort"); qa_json_writer_string(&w, job->effort); }
        qa_json_writer_key(&w, "messages"); qa_json_writer_array(&w);
        qa_json_writer_object(&w); qa_json_writer_key(&w, "role"); qa_json_writer_string(&w, "system");
        qa_json_writer_key(&w, "content"); qa_json_writer_string(&w, job->instructions); qa_json_writer_end(&w);
        qa_json_writer_object(&w); qa_json_writer_key(&w, "role"); qa_json_writer_string(&w, "user");
        qa_json_writer_key(&w, "content"); qa_json_writer_string(&w, job->prompt); qa_json_writer_end(&w); qa_json_writer_end(&w);
    } else {
        qa_json_writer_key(&w, "instructions"); qa_json_writer_string(&w, job->instructions[0] || job->provider != QA_LLM_SUBSCRIPTION ? job->instructions : "You are a helpful assistant.");
        qa_json_writer_key(&w, "input"); qa_json_writer_array(&w); qa_json_writer_object(&w);
        qa_json_writer_key(&w, "role"); qa_json_writer_string(&w, "user"); qa_json_writer_key(&w, "content"); qa_json_writer_array(&w);
        qa_json_writer_object(&w); qa_json_writer_key(&w, "type"); qa_json_writer_string(&w, "input_text");
        qa_json_writer_key(&w, "text"); qa_json_writer_string(&w, job->prompt); qa_json_writer_end(&w); qa_json_writer_end(&w);
        qa_json_writer_end(&w); qa_json_writer_end(&w); qa_json_writer_key(&w, "store"); qa_json_writer_bool(&w, false);
        if (job->effort) {
            qa_json_writer_key(&w, "reasoning"); qa_json_writer_object(&w); qa_json_writer_key(&w, "effort"); qa_json_writer_string(&w, job->effort); qa_json_writer_end(&w);
        }
        if (job->provider == QA_LLM_SUBSCRIPTION) {
            qa_json_writer_key(&w, "text"); qa_json_writer_object(&w); qa_json_writer_key(&w, "verbosity"); qa_json_writer_string(&w, "medium"); qa_json_writer_end(&w);
            qa_json_writer_key(&w, "include"); qa_json_writer_array(&w); qa_json_writer_string(&w, "reasoning.encrypted_content"); qa_json_writer_end(&w);
            qa_json_writer_key(&w, "prompt_cache_key"); qa_json_writer_string(&w, session);
        }
    }
    qa_json_writer_key(&w, "stream"); qa_json_writer_bool(&w, true); qa_json_writer_end(&w);
    bool ok = qa_json_writer_finish(&w, out, error); qa_json_writer_destroy(&w); return ok;
}
static bool submit(llm_job *job, bool *ready, qa_error *error) {
    qa_llm *s = job->owner; llm_credentials credentials = {0}; qa_buffer other_key = {0}, account = {0}, payload = {0};
    llm_text bearer = {0}, url = {0}; bool ok = false;
    const char *key = NULL; char session[37] = {0};
    *ready = true; job->waiting_refresh = false;
    if (job->provider == QA_LLM_SUBSCRIPTION) {
        if (!llm_auth_credential(s, job->rejected_token, &job->refresh_ticket, &job->storage, &job->credential, ready, error)) goto done;
        if (!*ready) { job->waiting_refresh = true; ok = true; goto done; }
        key = job->credential.access_token;
        if (!llm_subscription_account(key, &account, error)) goto done;
        if (job->kind == JOB_REQUEST && !llm_session_id(session, error)) goto done;
    } else if (job->provider == QA_LLM_OPENAI_API) {
        if (!llm_credentials_load(s, &credentials, error)) goto done;
        key = credentials.api_key;
    } else {
        if (!llm_other_key_load(s, &other_key, error)) goto done;
        key = (const char *)other_key.data;
    }
    if (!key) { llm_fail(error, "paste an API key for the selected provider in LLM options first"); goto done; }
    if (!llm_text_string(&bearer, "Bearer ", error) || !llm_text_string(&bearer, key, error)) goto done;
    if (job->provider == QA_LLM_SUBSCRIPTION) {
        if (!llm_text_string(&url, job->kind == JOB_MODELS ? "https://chatgpt.com/backend-api/codex/models?client_version=0.154.0" : "https://chatgpt.com/backend-api/codex/responses", error)) goto done;
    } else if (!llm_text_string(&url, job->provider == QA_LLM_OPENAI_API ? "https://api.openai.com/v1" : job->base_url, error) ||
        !llm_text_string(&url, job->kind == JOB_MODELS ? "/models" : job->provider == QA_LLM_OPENAI_API ? "/responses" : "/chat/completions", error)) goto done;
    if (job->kind == JOB_REQUEST && !request_body(job, session, &payload, error)) goto done;
    qa_http_header request_headers[8] = {
        {"Authorization", (const char *)bearer.buffer.data}, {"Accept", job->kind == JOB_MODELS ? "application/json" : "text/event-stream"},
        {"Content-Type", "application/json"}
    };
    size_t header_count = 3;
    if (job->provider == QA_LLM_SUBSCRIPTION) {
        request_headers[header_count++] = (qa_http_header){"chatgpt-account-id", (const char *)account.data};
        request_headers[header_count++] = (qa_http_header){"originator", "pi"};
        if (job->kind == JOB_REQUEST) {
            request_headers[header_count++] = (qa_http_header){"OpenAI-Beta", "responses=experimental"};
            request_headers[header_count++] = (qa_http_header){"User-Agent", "pi (quake-anthology native)"};
            request_headers[header_count++] = (qa_http_header){"session_id", session};
        }
    }
    qa_http_request r = {.url = (const char *)url.buffer.data, .method = job->kind == JOB_MODELS ? "GET" : "POST",
        .headers = request_headers, .header_count = header_count, .body = {payload.data, payload.size},
        .timeout_ms = job->kind == JOB_MODELS && s->options.request_timeout_ms > 30000 ? 30000 : s->options.request_timeout_ms,
        .maximum_response_bytes = job->kind == JOB_MODELS ? 4194304 : 1048576,
        .callbacks = {.context = job, .headers = headers, .body = body, .complete = complete}};
    if (!qa_http_submit(s->options.http, &r, &job->http_id, error)) goto done;
    job->active = true; ok = true;
done:
    qa_arena_destroy(&credentials.storage); qa_buffer_free(&other_key); qa_buffer_free(&account); qa_buffer_free(&payload);
    qa_buffer_free(&bearer.buffer); qa_buffer_free(&url.buffer); return ok;
}
static llm_job *job_create(qa_llm *s, job_kind kind, qa_llm_provider provider, qa_error *error) {
    if (!s->next_id) { llm_fail(error, "LLM request identifiers exhausted"); return NULL; }
    double now = llm_wall_milliseconds(s);
    uint32_t timeout = kind == JOB_MODELS && s->options.request_timeout_ms > 30000 ? 30000 : s->options.request_timeout_ms;
    if (!isfinite(now) || !isfinite(now + timeout)) { llm_fail(error, "invalid LLM request clock"); return NULL; }
    llm_job *job = calloc(1, sizeof *job);
    if (!job) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native LLM request"); return NULL; }
    job->owner = s; job->id = s->next_id++; job->kind = kind; job->provider = provider; job->deadline = now + timeout;
    job->stream = (llm_stream){.provider = provider, .context = job, .emit = stream_emit};
    return job;
}
bool qa_llm_request(qa_llm *s, const char *prompt, const char *instructions, const qa_llm_observer *observer,
                    qa_llm_request_id *out, qa_error *error) {
    if (!s || s->busy || !prompt || !instructions || !observer || !out) return llm_fail(error, "invalid LLM request admission");
    llm_preferences prefs = {0}; llm_other other = {0}; llm_job *job = NULL; bool ok = false;
    ++s->busy;
    if (!qa_utf8_valid((qa_bytes){(const uint8_t *)prompt, strlen(prompt)}) || !qa_utf8_valid((qa_bytes){(const uint8_t *)instructions, strlen(instructions)})) { llm_fail(error, "invalid LLM request UTF-8"); goto done; }
    if (!llm_trim((qa_bytes){(const uint8_t *)prompt, strlen(prompt)}).size) { llm_fail(error, "enter a question or command request"); goto done; }
    if (!llm_preferences_load(s, &prefs, error)) goto done;
    if (prefs.provider == QA_LLM_OTHER_API && !llm_other_load(s, &other, error)) goto done;
    job = job_create(s, JOB_REQUEST, prefs.provider, error); if (!job) goto done;
    job->observer = *observer;
    job->prompt = llm_arena_text(&job->storage, (qa_bytes){(const uint8_t *)prompt, strlen(prompt)}, error);
    qa_bytes instruction_text = {(const uint8_t *)instructions, strlen(instructions)};
    if (prefs.provider == QA_LLM_SUBSCRIPTION) instruction_text = llm_trim(instruction_text);
    job->instructions = llm_arena_text(&job->storage, instruction_text, error);
    const char *model = prefs.provider == QA_LLM_OTHER_API ? other.model : prefs.models[prefs.provider];
    if (!job->prompt || !job->instructions || !model[0]) { llm_fail(error, "choose a Model in LLM options before sending a request"); goto done; }
    job->model = llm_arena_text(&job->storage, (qa_bytes){(const uint8_t *)model, strlen(model)}, error); if (!job->model) goto done;
    if (prefs.efforts[prefs.provider]) {
        const char *effort = prefs.efforts[prefs.provider];
        job->effort = llm_arena_text(&job->storage, (qa_bytes){(const uint8_t *)effort, strlen(effort)}, error); if (!job->effort) goto done;
        qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, prefs.provider, model, &fallback);
        job->needs_metadata = metadata == NULL;
        if (metadata && !llm_model_effort(metadata, effort)) { llm_fail(error, "selected reasoning effort is not supported by this model"); goto done; }
    }
    if (prefs.provider == QA_LLM_OTHER_API) {
        if (!other.base_url[0]) { llm_fail(error, "configure Other API before sending a request"); goto done; }
        job->base_url = llm_arena_text(&job->storage, (qa_bytes){(const uint8_t *)other.base_url, strlen(other.base_url)}, error); if (!job->base_url) goto done;
    }
    job->next = s->jobs; s->jobs = job; *out = job->id; job = NULL; ok = true;
done:
    job_free(job); qa_arena_destroy(&prefs.storage); qa_arena_destroy(&other.storage); --s->busy; return ok;
}
bool llm_discover_start(qa_llm *s, qa_llm_provider provider, qa_error *error) {
    if (!llm_provider_valid(provider)) return llm_fail(error, "invalid model discovery provider");
    llm_catalog *catalog = &s->catalogs[provider];
    if (catalog->generation == UINT64_MAX) return llm_fail(error, "model catalog generation exhausted");
    llm_other other = {0}; llm_job *job = job_create(s, JOB_MODELS, provider, error); if (!job) return false;
    if (provider == QA_LLM_OTHER_API) {
        if (!llm_other_load(s, &other, error) || !other.base_url[0]) { job_free(job); qa_arena_destroy(&other.storage); return false; }
        job->base_url = llm_arena_text(&job->storage, (qa_bytes){(const uint8_t *)other.base_url, strlen(other.base_url)}, error);
        qa_arena_destroy(&other.storage); if (!job->base_url) { job_free(job); return false; }
    }
    llm_jobs_cancel_provider(s, provider);
    job->catalog_generation = ++catalog->generation; catalog->loading = true; catalog->error = (qa_error){0};
    job->next = s->jobs; s->jobs = job; return true;
}
bool qa_llm_discover_models(qa_llm *s, qa_llm_provider provider, qa_error *error) {
    if (!s || s->busy) return llm_fail(error, "model discovery requires callbacks to return");
    ++s->busy; bool ok = llm_discover_start(s, provider, error); --s->busy; return ok;
}
bool qa_llm_cancel_model_discovery(qa_llm *s, qa_llm_provider provider, qa_error *error) {
    if (!s || s->busy || !llm_provider_valid(provider)) return llm_fail(error, "model discovery cancellation requires callbacks to return");
    llm_jobs_cancel_provider(s, provider); s->catalogs[provider].loading = false; return true;
}
void qa_llm_cancel(qa_llm *s, qa_llm_request_id id) {
    if (!s) return;
    for (llm_job *job = s->jobs; job; job = job->next) if (job->id == id) {
        job->canceled = true; if (job->active) qa_http_cancel(s->options.http, job->http_id); job->active = false; return;
    }
}
void llm_jobs_cancel_provider(qa_llm *s, qa_llm_provider provider) {
    for (llm_job *job = s->jobs; job; job = job->next) if (job->kind == JOB_MODELS && job->provider == provider) qa_llm_cancel(s, job->id);
}
void llm_jobs_destroy(qa_llm *s) {
    while (s->jobs) { llm_job *job = s->jobs; s->jobs = job->next; if (job->active) qa_http_cancel(s->options.http, job->http_id); job_free(job); }
}
static void catalog_finish(llm_job *job) {
    qa_llm *s = job->owner; llm_catalog *catalog = &s->catalogs[job->provider];
    if (job->catalog_generation != catalog->generation) return;
    catalog->loading = false;
    llm_catalog next = {0};
    if (job->error.code != QA_OK || !llm_catalog_parse(job->provider, (qa_bytes){job->response.buffer.data, job->response.buffer.size}, &next, &job->error)) {
        catalog->error = job->error; return;
    }
    next.generation = catalog->generation; qa_arena_destroy(&catalog->storage); *catalog = next;
    llm_preferences prefs = {0}; qa_error error = {0};
    if (!llm_preferences_load(s, &prefs, &error)) { catalog->error = error; return; }
    const char *name = job->provider == QA_LLM_OTHER_API ? s->other.model : prefs.models[job->provider];
    const qa_llm_model *selected = NULL;
    for (size_t i = 0; i < catalog->count; ++i) if (!strcmp(catalog->models[i].id, name)) { selected = &catalog->models[i]; break; }
    bool default_model = false;
    if (!name[0]) for (size_t i = 0; i < catalog->count; ++i) if (catalog->models[i].recommended) { selected = &catalog->models[i]; default_model = true; break; }
    const char *old = prefs.efforts[job->provider], *effort = default_model ? selected->default_effort : old && !llm_model_effort(selected, old) ? NULL : old;
    if (default_model && job->provider != QA_LLM_OTHER_API) prefs.models[job->provider] = selected->id;
    prefs.efforts[job->provider] = effort;
    if (default_model || effort != old) {
        if (llm_preferences_save(s, &prefs, &error)) {
            llm_preferences committed = {0};
            if (llm_preferences_load(s, &committed, &error)) { qa_arena_destroy(&s->preferences.storage); s->preferences = committed; }
        }
        if (error.code != QA_OK) catalog->error = error;
    }
    qa_arena_destroy(&prefs.storage);
}
bool llm_jobs_tick(qa_llm *s, qa_error *error) {
    double now = llm_wall_milliseconds(s);
    if (!isfinite(now)) return llm_fail(error, "invalid LLM request clock");
    size_t refresh_users = 0;
    for (llm_job **link = &s->jobs; *link;) {
        llm_job *job = *link;
        if (!job->canceled && now >= job->deadline && (!job->result || job->status == 401 || job->status == 403)) {
            if (job->active) qa_http_cancel(s->options.http, job->http_id);
            job->active = false; job->result = true; job->status = 0; llm_fail(&job->error, "LLM request timed out. Try again");
        }
        if (!job->canceled && job->result && job->provider == QA_LLM_SUBSCRIPTION && !job->retry && (job->status == 401 || job->status == 403)) {
            job->retry = true; job->result = false; job->error = (qa_error){0}; job->status = 0; job->refresh_ticket = 0;
            job->rejected_token = job->credential.access_token;
            llm_stream_destroy(&job->stream); job->stream = (llm_stream){.provider = job->provider, .context = job, .emit = stream_emit}; llm_text_clear(&job->response);
        }
        if (job->canceled || job->result) {
            *link = job->next;
            if (!job->canceled) {
                if (job->kind == JOB_MODELS) catalog_finish(job);
                else if (job->observer.complete) job->observer.complete(job->observer.context, job->id,
                    (qa_bytes){job->stream.text.buffer.data, job->stream.text.buffer.size}, job->error.code == QA_OK ? NULL : &job->error);
            }
            job_free(job); continue;
        }
        if (!job->active) {
            if (job->needs_metadata) {
                llm_catalog *catalog = &s->catalogs[job->provider];
                qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, job->provider, job->model, &fallback);
                if (!metadata && !catalog->loading && catalog->error.code == QA_OK && !catalog->ready) {
                    if (!llm_discover_start(s, job->provider, &job->error)) job->result = true;
                    /* Discovery prepends a node. Preserve the current iterator
                     * link; the new job will be progressed on the next tick. */
                    if (link == &s->jobs && s->jobs != job) link = &s->jobs->next;
                } else if (metadata || catalog->ready || catalog->error.code != QA_OK) {
                    if (!metadata || !llm_model_effort(metadata, job->effort)) { llm_fail(&job->error, "selected reasoning effort is not supported by this model"); job->result = true; }
                    else job->needs_metadata = false;
                }
            }
            if (!job->needs_metadata && !job->result) {
                bool ready;
                if (!submit(job, &ready, &job->error)) job->result = true;
                if (job->waiting_refresh) ++refresh_users;
            }
        }
        link = &job->next;
    }
    llm_auth_refresh_users(s, refresh_users); return true;
}

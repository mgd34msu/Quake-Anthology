#include "llm_internal.h"
#include "qa/http_save.h"
#include "save_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool admit(qa_llm *s, qa_error *error) { return (s && !s->busy && !s->pending_restore) || llm_fail(error, "LLM mutation requires restored continuation and returned callbacks"); }
bool qa_llm_callbacks_idle(const qa_llm *s) { return !s || !s->busy; }
double llm_wall_milliseconds(qa_llm *s) {
    ++s->busy; double now = s->options.wall_milliseconds(s->options.context); --s->busy; return now;
}
static void invalidate(qa_llm *s, qa_llm_provider provider) {
    llm_catalog *c = &s->catalogs[provider]; uint64_t generation = c->generation + 1;
    llm_jobs_cancel_provider(s, provider); qa_arena_destroy(&c->storage);
    *c = (llm_catalog){.generation = generation};
}
static void commit_preferences(qa_llm *s, llm_preferences *next) {
    qa_arena_destroy(&s->preferences.storage); s->preferences = *next; *next = (llm_preferences){0};
    s->settings_errors[0] = (qa_error){0};
}
bool qa_llm_create_empty(const qa_llm_options *options, qa_llm **out, qa_error *error) {
    if (!options || !out || *out || !options->http || !options->settings || !options->private_mount || !options->owner ||
        !options->wall_milliseconds || !options->open_browser || !options->context_active || !options->print)
        return llm_fail(error, "invalid native LLM services");
    qa_llm *s = calloc(1, sizeof *s);
    if (!s) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native LLM owner"); return false; }
    s->options = *options; s->next_id = 1; s->pending_restore = true;
    if (!s->options.request_timeout_ms) s->options.request_timeout_ms = 120000;
    if (!s->options.callback_timeout_ms) s->options.callback_timeout_ms = 180000;
    if (!s->options.callback_port) s->options.callback_port = 1455;
    if (options->authorization_url) s->options.authorization_url = llm_arena_text(&s->option_storage,
        (qa_bytes){(const uint8_t *)options->authorization_url, strlen(options->authorization_url)}, error);
    if (options->token_url) s->options.token_url = llm_arena_text(&s->option_storage,
        (qa_bytes){(const uint8_t *)options->token_url, strlen(options->token_url)}, error);
    if ((options->authorization_url && !s->options.authorization_url) || (options->token_url && !s->options.token_url)) {
        qa_arena_destroy(&s->option_storage); free(s); return false;
    }
    s->preferences = (llm_preferences){.models = {"", ""}};
    s->other = (llm_other){.base_url = "", .model = ""};
    *out = s; return true;
}
bool qa_llm_create(const qa_llm_options *options, qa_llm **out, qa_error *error) {
    if (!qa_llm_create_empty(options, out, error)) return false;
    qa_llm *s = *out;
    (void)llm_preferences_load(s, &s->preferences, &s->settings_errors[0]);
    (void)llm_credentials_load(s, &s->credentials, &s->settings_errors[1]);
    (void)llm_other_key_load(s, &s->other_key, &s->settings_errors[2]);
    (void)llm_other_load(s, &s->other, &s->settings_errors[3]);
    /* File errors remain independently visible without exposing private data. */
    static const char *const files[] = {"llm.json", "chatgpt.key", "other.key", "other.service"};
    for (size_t i = 0; i < 4; ++i) if (s->settings_errors[i].code != QA_OK)
        qa_error_set(&s->settings_errors[i], QA_ERROR_IO, 0, "could not load %s; check its format and permissions", files[i]);
    s->pending_restore = false;
    *out = s; return true;
}
bool qa_llm_destroy(qa_llm *s, qa_error *error) {
    if (!s) return true;
    if (s->busy) return llm_fail(error, "LLM teardown requires returned callbacks");
    if (!llm_console_detach_all(s, error)) return false;
    llm_auth_cancel(s); llm_jobs_destroy(s);
    qa_arena_destroy(&s->preferences.storage); qa_arena_destroy(&s->credentials.storage); qa_arena_destroy(&s->other.storage);
    qa_buffer_free(&s->other_key); qa_arena_destroy(&s->option_storage);
    for (size_t i = 0; i < 3; ++i) qa_arena_destroy(&s->catalogs[i].storage);
    free(s); return true;
}
bool qa_llm_checkpoint_ready(const qa_llm *s, qa_error *error) {
    return (s && !s->busy && !s->pending_restore && !s->auth && !s->signing_in && llm_jobs_checkpoint_ready(s, error) &&
        qa_http_checkpoint_ready(s->options.http, error)) || llm_fail(error, "LLM continuation requires returned callbacks and no live external transaction");
}
bool llm_saved_subscription(qa_source_save_io *io, qa_arena *arena, llm_subscription *p) {
    if (!tool_save_arena_text(io, arena, &p->access_token) || !tool_save_arena_text(io, arena, &p->refresh_token) ||
        !tool_save_arena_text(io, arena, &p->token_type) || !qa_source_save_f64(io, &p->expires_at) || !isfinite(p->expires_at) ||
        !qa_source_save_count(io, &p->scope_count, SIZE_MAX / sizeof(char *))) return tool_save_fail(io, "invalid saved subscription credentials");
    const char **scopes = (const char **)p->scopes;
    if (io->direction == QA_SOURCE_SAVE_READ && p->scope_count) {
        scopes = qa_arena_alloc(arena, p->scope_count * sizeof *scopes, _Alignof(char *), io->error);
        p->scopes = scopes; if (!scopes) return tool_save_fail(io, "allocating saved subscription scopes");
        memset(scopes, 0, p->scope_count * sizeof *scopes);
    }
    for (size_t i = 0; i < p->scope_count; ++i)
        if (!tool_save_arena_text(io, arena, &scopes[i]) || !scopes[i]) return tool_save_fail(io, "invalid saved subscription scope");
    return true;
}
static bool llm_saved_options_equal(const qa_llm_options *a, const qa_llm_options *b) {
    return a->http == b->http && a->settings == b->settings && a->private_mount == b->private_mount && a->owner == b->owner &&
        a->context == b->context && a->wall_milliseconds == b->wall_milliseconds && a->open_browser == b->open_browser &&
        a->context_active == b->context_active && a->print == b->print && a->capture_context == b->capture_context;
}
static bool llm_saved_catalog(qa_source_save_io *io, llm_catalog *catalog) {
    if (!qa_source_save_u64(io, &catalog->generation) || !qa_source_save_bool(io, &catalog->ready) ||
        !qa_source_save_bool(io, &catalog->loading) || !tool_save_error(io, &catalog->error) ||
        !qa_source_save_count(io, &catalog->count, SIZE_MAX / sizeof *catalog->models)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && catalog->count) {
        catalog->models = qa_arena_alloc(&catalog->storage, catalog->count * sizeof *catalog->models, _Alignof(qa_llm_model), io->error);
        if (!catalog->models) return tool_save_fail(io, "allocating saved model catalog");
        memset(catalog->models, 0, catalog->count * sizeof *catalog->models);
    }
    for (size_t i = 0; i < catalog->count; ++i) {
        qa_llm_model *v = &catalog->models[i]; bool name_alias = v->id && v->name == v->id;
        if (!tool_save_arena_text(io, &catalog->storage, &v->id) || !v->id || !*v->id ||
            !qa_source_save_bool(io, &name_alias)) return tool_save_fail(io, "invalid saved model identity");
        if (name_alias) v->name = v->id;
        else if (!tool_save_arena_text(io, &catalog->storage, &v->name) || !v->name) return tool_save_fail(io, "invalid saved model label");
        if (!tool_save_arena_text(io, &catalog->storage, &v->default_effort) ||
            !qa_source_save_bool(io, &v->recommended) || !qa_source_save_count(io, &v->effort_count, SIZE_MAX / sizeof(char *))) return false;
        uint32_t table_id = io->direction == QA_SOURCE_SAVE_WRITE ? llm_effort_table_identity(v->efforts, v->effort_count) : 0;
        if (!qa_source_save_u32(io, &table_id)) return false;
        size_t static_count = 0; const char *const *static_table = table_id ? llm_effort_table_resolve(table_id, &static_count) : NULL;
        if (table_id && (!static_table || static_count != v->effort_count)) return tool_save_fail(io, "saved reasoning table descriptor has a foreign shape");
        const char **efforts = (const char **)v->efforts;
        if (io->direction == QA_SOURCE_SAVE_READ && v->effort_count) {
            efforts = qa_arena_alloc(&catalog->storage, v->effort_count * sizeof *efforts, _Alignof(char *), io->error);
            v->efforts = efforts; if (!efforts) return tool_save_fail(io, "allocating saved reasoning choices");
            memset(efforts, 0, v->effort_count * sizeof *efforts);
        }
        for (size_t j = 0; j < v->effort_count; ++j) {
            if (!tool_save_arena_text(io, &catalog->storage, &efforts[j]) || !efforts[j]) return tool_save_fail(io, "invalid saved reasoning choice");
            if (static_table && strcmp(static_table[j], efforts[j])) return tool_save_fail(io, "saved immutable reasoning table differs from its actual owner");
        }
        if (io->direction == QA_SOURCE_SAVE_READ && static_table) v->efforts = static_table;
        for (size_t j = 0; j < i; ++j) if (!strcmp(catalog->models[j].id, v->id)) return tool_save_fail(io, "duplicate saved model identity");
        if (!llm_model_effort(v, v->default_effort)) return tool_save_fail(io, "saved default reasoning choice is absent");
    }
    return true;
}
static void llm_saved_private_free(qa_llm *s) {
    llm_console_private_free(s); llm_jobs_destroy(s);
    qa_arena_destroy(&s->option_storage); qa_arena_destroy(&s->preferences.storage);
    qa_arena_destroy(&s->credentials.storage); qa_arena_destroy(&s->other.storage); qa_buffer_free(&s->other_key);
    for (size_t i = 0; i < 3; ++i) qa_arena_destroy(&s->catalogs[i].storage);
}
static bool llm_saved_fields(qa_source_save_io *io, qa_llm *s, const qa_llm *installed, const qa_llm_checkpoint_refs *refs) {
    uint32_t provider = (uint32_t)s->preferences.provider; uint64_t service = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE && !refs->services_encode(refs->context, &s->options, &service, io->error)) return false;
    if (!qa_source_save_u64(io, &service)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && (!refs->services_decode(refs->context, service, &s->options, io->error) ||
        !llm_saved_options_equal(&s->options, &installed->options))) return tool_save_fail(io, "LLM continuation services differ from installed candidate owners");
    if (io->direction == QA_SOURCE_SAVE_READ) { s->options.authorization_url = NULL; s->options.token_url = NULL; }
    if (!qa_source_save_u32(io, &s->options.request_timeout_ms) || !s->options.request_timeout_ms ||
        !qa_source_save_u32(io, &s->options.callback_timeout_ms) || !s->options.callback_timeout_ms ||
        !qa_source_save_u16(io, &s->options.callback_port) || !s->options.callback_port ||
        !tool_save_arena_text(io, &s->option_storage, &s->options.authorization_url) ||
        !tool_save_arena_text(io, &s->option_storage, &s->options.token_url) ||
        !qa_source_save_u64(io, &s->next_id) || !qa_source_save_u64(io, &s->auth_generation) ||
        !tool_save_error(io, &s->auth_error) || !qa_source_save_u32(io, &provider) || provider > QA_LLM_OTHER_API)
        return tool_save_fail(io, "invalid LLM continuation settings");
    s->preferences.provider = (qa_llm_provider)provider;
    for (size_t i = 0; i < 2; ++i) if (!tool_save_arena_text(io, &s->preferences.storage, &s->preferences.models[i]) || !s->preferences.models[i]) return tool_save_fail(io, "missing saved LLM model preference");
    for (size_t i = 0; i < 3; ++i) if (!tool_save_arena_text(io, &s->preferences.storage, &s->preferences.efforts[i])) return false;
    if (!tool_save_arena_text(io, &s->credentials.storage, &s->credentials.api_key) || !qa_source_save_bool(io, &s->credentials.subscribed) ||
        !llm_saved_subscription(io, &s->credentials.storage, &s->credentials.subscription) ||
        !tool_save_arena_text(io, &s->other.storage, &s->other.base_url) || !s->other.base_url ||
        !tool_save_arena_text(io, &s->other.storage, &s->other.model) || !s->other.model ||
        !tool_save_blob(io, &s->other_key, true)) return false;
    if (s->credentials.subscribed && (!s->credentials.subscription.access_token || !s->credentials.subscription.refresh_token || !s->credentials.subscription.token_type))
        return tool_save_fail(io, "saved subscribed credentials lack tokens");
    if (s->other_key.data && memchr(s->other_key.data, 0, s->other_key.size)) return tool_save_fail(io, "saved Other API key contains NUL");
    for (size_t i = 0; i < 4; ++i) if (!tool_save_error(io, &s->settings_errors[i])) return false;
    for (size_t i = 0; i < 3; ++i) if (!llm_saved_catalog(io, &s->catalogs[i])) return false;
    return llm_console_fields(io, s, installed, refs) && llm_jobs_fields(io, s, refs);
}
bool qa_llm_checkpoint(const qa_llm *s, qa_session *session, const qa_llm_checkpoint_refs *refs, qa_buffer *out, qa_error *error) {
    if (!out || !refs || !refs->services_encode || !refs->console_encode || !refs->command_context || !qa_llm_checkpoint_ready(s, error)) return false;
    qa_source_save_io io = {0}; if (!qa_source_save_writer(&io, session, error)) return false;
    bool ok = llm_saved_fields(&io, (qa_llm *)s, s, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_llm_restore(qa_llm *s, qa_session *session, const qa_llm_checkpoint_refs *refs, qa_bytes bytes, qa_error *error) {
    if (!s || s->busy || s->auth || s->signing_in || !refs || !refs->services_decode || !refs->console_decode ||
        !refs->command_context || !llm_jobs_checkpoint_ready(s, error) || !qa_http_checkpoint_ready(s->options.http, error)) return false;
    qa_llm next = {0}; qa_source_save_io io = {0}; if (!qa_source_save_reader(&io, session, bytes, error)) return false;
    bool ok = llm_saved_fields(&io, &next, s, refs) && qa_source_save_finish(&io, NULL) && llm_jobs_checkpoint_ready(&next, error);
    if (ok) {
        qa_llm old = *s; llm_console_exchange(s, &next); old.consoles = NULL;
        *s = next; llm_jobs_rebind(s); llm_saved_private_free(&old);
    } else llm_saved_private_free(&next);
    qa_source_save_dispose(&io); return ok;
}
bool qa_llm_rebind_ready(const qa_llm *s, const void *old_context, const void *new_context, qa_error *error) {
    return (s && !s->busy && !s->pending_restore && !s->auth && new_context && s->options.context == old_context) || llm_fail(error, "LLM publication requires its restored idle callback owner");
}
void qa_llm_rebind_context(qa_llm *s, void *context) { s->options.context = context; }
bool qa_llm_tick(qa_llm *s, qa_error *error) {
    if (!admit(s, error)) return false;
    ++s->busy;
    bool ok = llm_auth_tick(s, error) && llm_jobs_tick(s, error) && llm_console_tick(s, error);
    --s->busy; return ok;
}
bool qa_llm_read(const qa_llm *s, qa_llm_snapshot *out, qa_error *error) {
    if (!s || !out || s->pending_restore) return llm_fail(error, "invalid or pending LLM settings observation");
    qa_llm_snapshot snapshot = {.provider = s->preferences.provider, .models = {s->preferences.models[0], s->preferences.models[1], s->other.model},
        .efforts = {s->preferences.efforts[0], s->preferences.efforts[1], s->preferences.efforts[2]},
        .configured = {s->credentials.subscribed, s->credentials.api_key != NULL, s->other_key.data != NULL},
        .subscription_expires_at = s->credentials.subscribed ? s->credentials.subscription.expires_at : 0,
        .other_base_url = s->other.base_url,
        .signing_in = s->signing_in, .authentication_error = &s->auth_error};
    for (size_t i = 0; i < 3; ++i) {
        const llm_catalog *c = &s->catalogs[i];
        snapshot.catalogs[i] = c->loading ? QA_LLM_CATALOG_LOADING : c->error.code != QA_OK ? QA_LLM_CATALOG_ERROR : c->ready ? QA_LLM_CATALOG_READY : QA_LLM_CATALOG_IDLE;
        snapshot.catalog_errors[i] = &c->error;
    }
    for (size_t i = 0; i < 4; ++i) snapshot.settings_errors[i] = &s->settings_errors[i];
    *out = snapshot; return true;
}
bool qa_llm_select_provider(qa_llm *s, qa_llm_provider provider, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider)) return llm_fail(error, "invalid LLM provider setting");
    if (provider != QA_LLM_SUBSCRIPTION) llm_auth_cancel(s);
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    next.provider = provider;
    bool ok = llm_preferences_save(s, &next, error);
    if (ok) commit_preferences(s, &next);
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_select_effort(qa_llm *s, qa_llm_provider provider, const char *effort, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider)) return llm_fail(error, "invalid reasoning provider");
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    bool ok = false; const char *choice = NULL;
    if (effort && !llm_effort_text(&next.storage, (qa_bytes){(const uint8_t *)effort, strlen(effort)}, &choice, error)) goto done;
    const char *name = provider == QA_LLM_OTHER_API ? s->other.model : next.models[provider];
    qa_llm_model fallback; const qa_llm_model *model = llm_model_metadata(s, provider, name, &fallback);
    if (!llm_model_effort(model, choice)) { llm_fail(error, "choose a reasoning effort supported by the selected model"); goto done; }
    next.efforts[provider] = choice;
    if (!llm_preferences_save(s, &next, error)) goto done;
    commit_preferences(s, &next); ok = true;
done:
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_select_model(qa_llm *s, qa_llm_provider provider, const char *id, qa_error *error) {
    if (!admit(s, error) || !llm_provider_valid(provider) || !id) return llm_fail(error, "invalid model provider");
    llm_preferences next = {0}; if (!llm_preferences_load(s, &next, error)) return false;
    bool ok = false; const char *name;
    if (!llm_model_text(&next.storage, (qa_bytes){(const uint8_t *)id, strlen(id)}, true, &name, error)) goto done;
    qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, provider, name, &fallback);
    if (s->catalogs[provider].ready) {
        bool found = false;
        for (size_t i = 0; i < s->catalogs[provider].count; ++i) if (!strcmp(s->catalogs[provider].models[i].id, name)) { found = true; break; }
        if (!found) { llm_fail(error, "choose a model from the loaded provider list"); goto done; }
    }
    const char *old_effort = next.efforts[provider];
    next.efforts[provider] = old_effort && llm_model_effort(metadata, old_effort) ? old_effort : metadata ? metadata->default_effort : NULL;
    if (next.efforts[provider] && next.efforts[provider] != old_effort) {
        next.efforts[provider] = llm_arena_text(&next.storage, (qa_bytes){(const uint8_t *)next.efforts[provider], strlen(next.efforts[provider])}, error);
        if (!next.efforts[provider]) goto done;
    }
    if (provider == QA_LLM_OTHER_API) {
        llm_other other = {0}; if (!llm_other_load(s, &other, error)) goto done;
        other.model = llm_arena_text(&other.storage, (qa_bytes){(const uint8_t *)name, strlen(name)}, error);
        if (!other.model || !other.base_url[0] || !llm_other_save(s, &other, error)) { qa_arena_destroy(&other.storage); goto done; }
        qa_arena_destroy(&s->other.storage); s->other = other; s->settings_errors[3] = (qa_error){0};
    } else next.models[provider] = name;
    if (!llm_preferences_save(s, &next, error)) goto done;
    commit_preferences(s, &next); ok = true;
done:
    qa_arena_destroy(&next.storage); return ok;
}
bool qa_llm_set_api_key(qa_llm *s, qa_llm_provider provider, const char *key, qa_error *error) {
    if (!admit(s, error) || (provider != QA_LLM_OPENAI_API && provider != QA_LLM_OTHER_API)) return llm_fail(error, "invalid API-key provider");
    llm_credentials next = {0}; qa_arena arena = {0}; const char *value = NULL; bool ok = false;
    if (key && !llm_api_key_text(&arena, (qa_bytes){(const uint8_t *)key, strlen(key)}, &value, error)) goto done;
    if (provider == QA_LLM_OTHER_API) {
        if (value) {
            qa_buffer copy = {0}; copy.size = strlen(value); copy.data = malloc(copy.size + 1);
            if (!copy.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Other API credential"); goto done; }
            memcpy(copy.data, value, copy.size + 1);
            if (!qa_vfs_write_private(s->options.settings, s->options.private_mount, "other.key", (qa_bytes){copy.data, copy.size}, error)) { qa_buffer_free(&copy); goto done; }
            qa_buffer_free(&s->other_key); s->other_key = copy;
        } else {
            qa_resource *present = NULL; qa_error local = {0};
            bool exists = qa_vfs_acquire_from(s->options.settings, s->options.private_mount, "other.key", &present, &local);
            qa_resource_release(present);
            if (!exists && local.code != QA_ERROR_NOT_FOUND) { qa_error_set(error, QA_ERROR_IO, 0, "could not inspect other.key for removal"); goto done; }
            if (exists && !qa_vfs_remove(s->options.settings, s->options.private_mount, "other.key", error)) goto done;
            qa_buffer_free(&s->other_key);
        }
        s->settings_errors[2] = (qa_error){0};
    } else {
        if (!llm_credentials_load(s, &next, error)) goto done;
        next.api_key = value ? llm_arena_text(&next.storage, (qa_bytes){(const uint8_t *)value, strlen(value)}, error) : NULL;
        if (value && !next.api_key) goto done;
        if (!llm_credentials_save(s, &next, error)) goto done;
        qa_arena_destroy(&s->credentials.storage); s->credentials = next; next = (llm_credentials){0}; s->settings_errors[1] = (qa_error){0};
    }
    invalidate(s, provider);
    if (value) (void)llm_discover_start(s, provider, &s->catalogs[provider].error);
    ok = true;
done:
    qa_arena_destroy(&next.storage); qa_arena_destroy(&arena); return ok;
}
bool qa_llm_set_other_connection(qa_llm *s, const char *url, const char *id, qa_error *error) {
    if (!admit(s, error)) return false;
    llm_other next = {0}; if (!llm_other_prepare(url, id, &next, error)) return false;
    bool changed = strcmp(next.base_url, s->other.base_url) != 0, model_changed = strcmp(next.model, s->other.model) != 0;
    if (!changed && model_changed && s->catalogs[QA_LLM_OTHER_API].ready) {
        qa_llm_model fallback;
        if (!llm_model_metadata(s, QA_LLM_OTHER_API, next.model, &fallback)) { qa_arena_destroy(&next.storage); return llm_fail(error, "choose a model from the loaded provider list"); }
    }
    if (!llm_other_save(s, &next, error)) { qa_arena_destroy(&next.storage); return false; }
    qa_arena_destroy(&s->other.storage); s->other = next; s->settings_errors[3] = (qa_error){0};
    if (changed) invalidate(s, QA_LLM_OTHER_API);
    if (changed || model_changed) {
        llm_preferences preferences = {0}; if (!llm_preferences_load(s, &preferences, error)) return false;
        qa_llm_model fallback; const qa_llm_model *metadata = llm_model_metadata(s, QA_LLM_OTHER_API, next.model, &fallback);
        const char *old = preferences.efforts[QA_LLM_OTHER_API];
        preferences.efforts[QA_LLM_OTHER_API] = old && llm_model_effort(metadata, old) ? old : metadata ? metadata->default_effort : NULL;
        if (preferences.efforts[QA_LLM_OTHER_API]) {
            const char *effort = preferences.efforts[QA_LLM_OTHER_API];
            preferences.efforts[QA_LLM_OTHER_API] = llm_arena_text(&preferences.storage, (qa_bytes){(const uint8_t *)effort, strlen(effort)}, error);
            if (!preferences.efforts[QA_LLM_OTHER_API]) { qa_arena_destroy(&preferences.storage); return false; }
        }
        if (!llm_preferences_save(s, &preferences, error)) { qa_arena_destroy(&preferences.storage); return false; }
        commit_preferences(s, &preferences);
    }
    return true;
}
bool qa_llm_sign_out(qa_llm *s, qa_error *error) {
    if (!admit(s, error)) return false;
    llm_auth_cancel(s); llm_credentials next = {0}; if (!llm_credentials_load(s, &next, error)) return false;
    next.subscribed = false; next.subscription = (llm_subscription){0};
    if (!llm_credentials_save(s, &next, error)) { qa_arena_destroy(&next.storage); return false; }
    qa_arena_destroy(&s->credentials.storage); s->credentials = next; s->settings_errors[1] = (qa_error){0}; invalidate(s, QA_LLM_SUBSCRIPTION); return true;
}
